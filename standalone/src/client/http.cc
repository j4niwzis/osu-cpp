export module client.http;

import std;
import platform.network;

export namespace client::http {
using Response = platform::network::Response;
using Handle = platform::network::Handle;
using Callback = platform::network::Callback;

inline void get(std::string url, std::shared_ptr<Handle> handle,
                Callback callback) {
  platform::network::get(std::move(url), std::move(handle),
                         std::move(callback));
}
inline std::size_t poll() { return platform::network::poll(); }

// Query formatting belongs to the client protocol, not the transport.
[[nodiscard]] inline std::string urlEncode(std::string_view value) {
  static constexpr char kHex[] = "0123456789ABCDEF";
  std::string out;
  out.reserve(value.size() * 3);
  for (const char c : value) {
    const auto byte = static_cast<unsigned char>(c);
    if ((byte >= 'A' && byte <= 'Z') || (byte >= 'a' && byte <= 'z') ||
        (byte >= '0' && byte <= '9') || byte == '-' || byte == '_' ||
        byte == '.' || byte == '~') {
      out.push_back(c);
    } else if (byte == ' ') {
      out.push_back('+');
    } else {
      out.push_back('%');
      out.push_back(kHex[byte >> 4]);
      out.push_back(kHex[byte & 0x0f]);
    }
  }
  return out;
}
} // namespace client::http
