module;

#ifdef __EMSCRIPTEN__
#include <emscripten/fetch.h>
#else
#include <curl/curl.h>
#endif

export module client.http;

import std;

export namespace client::http {

struct Response {
  bool fOk = false;
  long fStatus = 0;
  std::vector<std::uint8_t> fBody;
  std::string fError;
};

// Progress/liveness handle shared with the UI.
struct Handle {
  std::atomic<float> fProgress{0.0f}; // 0..1, best effort
  std::atomic<bool> fDone{false};
};

using Callback = std::function<void(Response)>;

} // namespace client::http

namespace client::http {

namespace detail {

struct Completed {
  Callback fCb;
  Response fResp;
};

inline std::mutex gMutex;
inline std::vector<Completed> gCompleted;

inline void complete(Callback cb, Response resp) {
  const std::scoped_lock lock(gMutex);
  gCompleted.push_back({std::move(cb), std::move(resp)});
}

#ifndef __EMSCRIPTEN__

inline std::size_t writeCb(char *ptr, std::size_t size, std::size_t nmemb,
                           void *user) {
  auto *body = static_cast<std::vector<std::uint8_t> *>(user);
  const std::size_t n = size * nmemb;
  body->insert(body->end(), reinterpret_cast<std::uint8_t *>(ptr),
               reinterpret_cast<std::uint8_t *>(ptr) + n);
  return n;
}

inline int progressCb(void *user, curl_off_t dltotal, curl_off_t dlnow,
                      curl_off_t, curl_off_t) {
  auto *h = static_cast<Handle *>(user);
  if (dltotal > 0) {
    h->fProgress.store(static_cast<float>(dlnow) /
                           static_cast<float>(dltotal),
                       std::memory_order_relaxed);
  }
  return 0;
}

inline void ensureCurlInit() {
  static const bool once = [] {
    curl_global_init(CURL_GLOBAL_DEFAULT);
    return true;
  }();
  (void)once;
}

#else

struct FetchCtx {
  Callback fCb;
  std::shared_ptr<Handle> fHandle;
};

#endif

} // namespace detail

} // namespace client::http

export namespace client::http {

// Fire an async GET. The callback runs on the thread that calls poll().
inline void get(const std::string &url, std::shared_ptr<Handle> handle,
                Callback cb) {
#ifdef __EMSCRIPTEN__
  emscripten_fetch_attr_t attr;
  emscripten_fetch_attr_init(&attr);
  std::strcpy(attr.requestMethod, "GET");
  attr.attributes = EMSCRIPTEN_FETCH_LOAD_TO_MEMORY;
  auto *ctx = new detail::FetchCtx{std::move(cb), std::move(handle)};
  attr.userData = ctx;
  attr.onsuccess = [](emscripten_fetch_t *fetch) {
    auto *ctx = static_cast<detail::FetchCtx *>(fetch->userData);
    Response resp;
    resp.fStatus = fetch->status;
    resp.fOk = fetch->status >= 200 && fetch->status < 300;
    resp.fBody.assign(
        reinterpret_cast<const std::uint8_t *>(fetch->data),
        reinterpret_cast<const std::uint8_t *>(fetch->data) + fetch->numBytes);
    if (!resp.fOk) {
      resp.fError = "HTTP " + std::to_string(fetch->status);
    }
    ctx->fHandle->fProgress.store(1.0f);
    ctx->fHandle->fDone.store(true);
    detail::complete(std::move(ctx->fCb), std::move(resp));
    delete ctx;
    emscripten_fetch_close(fetch);
  };
  attr.onerror = [](emscripten_fetch_t *fetch) {
    auto *ctx = static_cast<detail::FetchCtx *>(fetch->userData);
    Response resp;
    resp.fStatus = fetch->status;
    resp.fError = "network error (HTTP " + std::to_string(fetch->status) +
                  "); possibly CORS";
    ctx->fHandle->fDone.store(true);
    detail::complete(std::move(ctx->fCb), std::move(resp));
    delete ctx;
    emscripten_fetch_close(fetch);
  };
  attr.onprogress = [](emscripten_fetch_t *fetch) {
    auto *ctx = static_cast<detail::FetchCtx *>(fetch->userData);
    if (fetch->totalBytes > 0) {
      ctx->fHandle->fProgress.store(static_cast<float>(fetch->dataOffset) /
                                    static_cast<float>(fetch->totalBytes));
    }
  };
  emscripten_fetch(&attr, url.c_str());
#else
  detail::ensureCurlInit();
  std::thread([url, handle = std::move(handle), cb = std::move(cb)]() mutable {
    Response resp;
    CURL *curl = curl_easy_init();
    if (curl == nullptr) {
      resp.fError = "curl init failed";
      handle->fDone.store(true);
      detail::complete(std::move(cb), std::move(resp));
      return;
    }
    char errbuf[CURL_ERROR_SIZE] = {};
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 8L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "osu-cpp/1.0");
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, detail::writeCb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp.fBody);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, detail::progressCb);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, handle.get());
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errbuf);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 20L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 64L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 60L);
    const CURLcode rc = curl_easy_perform(curl);
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    resp.fStatus = status;
    if (rc != CURLE_OK) {
      resp.fError = errbuf[0] != '\0' ? errbuf : curl_easy_strerror(rc);
    } else if (status >= 200 && status < 300) {
      resp.fOk = true;
    } else {
      resp.fError = "HTTP " + std::to_string(status);
    }
    curl_easy_cleanup(curl);
    handle->fProgress.store(1.0f);
    handle->fDone.store(true);
    detail::complete(std::move(cb), std::move(resp));
  }).detach();
#endif
}

// Run completed callbacks on the calling thread. Call once per frame.
inline void poll() {
  std::vector<detail::Completed> ready;
  {
    const std::scoped_lock lock(detail::gMutex);
    ready.swap(detail::gCompleted);
  }
  for (auto &c : ready) {
    c.fCb(std::move(c.fResp));
  }
}

// Percent-encode a query component.
[[nodiscard]] inline std::string urlEncode(std::string_view s) {
  static constexpr char kHex[] = "0123456789ABCDEF";
  std::string out;
  out.reserve(s.size() * 3);
  for (const char c : s) {
    const auto u = static_cast<unsigned char>(c);
    if ((u >= 'A' && u <= 'Z') || (u >= 'a' && u <= 'z') ||
        (u >= '0' && u <= '9') || u == '-' || u == '_' || u == '.' ||
        u == '~') {
      out.push_back(c);
    } else if (u == ' ') {
      out.push_back('+');
    } else {
      out.push_back('%');
      out.push_back(kHex[u >> 4]);
      out.push_back(kHex[u & 0xF]);
    }
  }
  return out;
}

} // namespace client::http
