export module platform.network.backend;

import std;
import platform.network.types;

export namespace platform::network::backend {
inline void start(std::string, std::shared_ptr<Handle> handle,
                  Callback complete) {
  Response response;
  response.fError = "network transport is unavailable";
  handle->fProgress.store(1.0f, std::memory_order_relaxed);
  handle->fDone.store(true, std::memory_order_release);
  complete(std::move(response));
}
} // namespace platform::network::backend
