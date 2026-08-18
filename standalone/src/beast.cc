module;

// Boost.Beast/Asio + std headers textually, NO `import std` in this TU:
// mixing fat foreign headers with the std module is what segfaults clang.
// The interface below only speaks std vocabulary types, which merge fine
// with the importing side (same recipe as the skia wrapper).
#include <boost/asio/connect.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/asio/ssl/error.hpp>
#include <boost/asio/ssl/stream.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>

#include <chrono>
#include <cstddef>
#include <functional>
#include <string>
#include <string_view>

export module beast;

export namespace beastnet {

struct FetchResult {
  bool fOk = false;
  long fStatus = 0;
  std::string fBody;
  std::string fError;
};

// received / total (total == 0 when unknown)
using Progress = std::function<void(std::size_t, std::size_t)>;

} // namespace beastnet

namespace beastnet::detail {

namespace http = boost::beast::http;
using tcp = boost::asio::ip::tcp;

struct Url {
  bool fTls = true;
  std::string fHost;
  std::string fPort;
  std::string fTarget;
};

inline bool parseUrl(std::string_view url, Url &out) {
  if (url.starts_with("https://")) {
    out.fTls = true;
    url.remove_prefix(8);
    out.fPort = "443";
  } else if (url.starts_with("http://")) {
    out.fTls = false;
    url.remove_prefix(7);
    out.fPort = "80";
  } else {
    return false;
  }
  const std::size_t slash = url.find('/');
  std::string_view hostPort =
      slash == std::string_view::npos ? url : url.substr(0, slash);
  out.fTarget = slash == std::string_view::npos
                    ? std::string("/")
                    : std::string(url.substr(slash));
  if (const std::size_t colon = hostPort.find(':');
      colon != std::string_view::npos) {
    out.fPort = std::string(hostPort.substr(colon + 1));
    hostPort = hostPort.substr(0, colon);
  }
  out.fHost = std::string(hostPort);
  return !out.fHost.empty();
}

template <typename Stream>
FetchResult runRequest(Stream &stream, const Url &url,
                       const Progress &progress, std::string &redirect) {
  FetchResult res;

  http::request<http::empty_body> req{http::verb::get, url.fTarget, 11};
  req.set(http::field::host, url.fHost);
  req.set(http::field::user_agent, "osu-cpp/1.0");
  req.set(http::field::accept, "*/*");
  http::write(stream, req);

  boost::beast::flat_buffer buffer;
  http::response_parser<http::string_body> parser;
  parser.body_limit(512ull * 1024ull * 1024ull);
  http::read_header(stream, buffer, parser);

  const auto &header = parser.get().base();
  const unsigned status = parser.get().result_int();
  res.fStatus = static_cast<long>(status);

  if (status >= 300 && status < 400) {
    if (const auto it = header.find(http::field::location);
        it != header.end()) {
      redirect = std::string(it->value());
      return res;
    }
  }

  std::size_t total = 0;
  if (const auto len = parser.content_length()) {
    total = static_cast<std::size_t>(*len);
  }

  boost::beast::error_code ec;
  while (!parser.is_done()) {
    http::read_some(stream, buffer, parser, ec);
    if (ec == http::error::end_of_stream ||
        ec == boost::asio::ssl::error::stream_truncated) {
      break; // server closed after body: acceptable
    }
    if (ec) {
      res.fError = ec.message();
      return res;
    }
    if (progress) {
      progress(parser.get().body().size(), total);
    }
  }

  res.fBody = std::move(parser.get().body());
  res.fOk = status >= 200 && status < 300;
  if (!res.fOk) {
    res.fError = "HTTP " + std::to_string(status);
  }
  return res;
}

inline FetchResult fetchOnce(const Url &url, const Progress &progress,
                             std::string &redirect) {
  boost::asio::io_context ioc;
  tcp::resolver resolver(ioc);
  const auto endpoints = resolver.resolve(url.fHost, url.fPort);

  if (url.fTls) {
    boost::asio::ssl::context ctx(boost::asio::ssl::context::tls_client);
    ctx.set_default_verify_paths();
    ctx.set_verify_mode(boost::asio::ssl::verify_peer);

    boost::asio::ssl::stream<boost::beast::tcp_stream> stream(ioc, ctx);
    if (SSL_set_tlsext_host_name(stream.native_handle(),
                                 url.fHost.c_str()) != 1) {
      FetchResult res;
      res.fError = "failed to set SNI hostname";
      return res;
    }
    boost::beast::get_lowest_layer(stream).expires_after(
        std::chrono::seconds(30));
    boost::beast::get_lowest_layer(stream).connect(endpoints);
    stream.handshake(boost::asio::ssl::stream_base::client);
    auto res = runRequest(stream, url, progress, redirect);
    boost::beast::error_code ec;
    stream.shutdown(ec); // best effort; many servers just close
    return res;
  }

  boost::beast::tcp_stream stream(ioc);
  stream.expires_after(std::chrono::seconds(30));
  stream.connect(endpoints);
  auto res = runRequest(stream, url, progress, redirect);
  boost::beast::error_code ec;
  stream.socket().shutdown(tcp::socket::shutdown_both, ec);
  return res;
}

} // namespace beastnet::detail

export namespace beastnet {

// Blocking GET with redirects; call from a worker thread.
inline FetchResult fetch(std::string url, const Progress &progress = {}) {
  try {
    for (int hop = 0; hop < 8; ++hop) {
      detail::Url parsed;
      if (!detail::parseUrl(url, parsed)) {
        FetchResult res;
        res.fError = "unsupported URL: " + url;
        return res;
      }
      std::string redirect;
      FetchResult res = detail::fetchOnce(parsed, progress, redirect);
      if (redirect.empty()) {
        return res;
      }
      if (redirect.starts_with('/')) {
        url = (parsed.fTls ? std::string("https://")
                           : std::string("http://")) +
              parsed.fHost + redirect;
      } else {
        url = std::move(redirect);
      }
    }
    FetchResult res;
    res.fError = "too many redirects";
    return res;
  } catch (const std::exception &e) {
    FetchResult res;
    res.fError = e.what();
    return res;
  }
}

} // namespace beastnet
