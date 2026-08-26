export module platform.network.backend;

import std;
import platform.network.types;

export namespace platform::network::backend {

// WASI HTTP is a Component Model host interface rather than a POSIX socket
// API. Keep it as its own backend so a generated WIT adapter can be added
// without pulling Beast, OpenSSL or browser fetch into a WASI component.
inline void start(std::string, std::shared_ptr<Handle> handle,
                  Callback complete) {
  Response response;
  response.fError = "wasi:http host bindings are not linked";
  handle->fDone.store(true, std::memory_order_release);
  complete(std::move(response));
}

} // namespace platform::network::backend
