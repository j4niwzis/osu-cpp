export module platform.network.backend;

import std;
import platform.emscripten_api;
import platform.network.types;

export namespace platform::network::backend {
namespace detail {
struct Fetch {
  Callback fComplete;
  std::shared_ptr<Handle> fHandle;
};
} // namespace detail

inline void start(std::string url, std::shared_ptr<Handle> handle,
                  Callback complete) {
  platform::emscripten::emscripten_fetch_attr_t attributes;
  platform::emscripten::emscripten_fetch_attr_init(&attributes);
  std::strcpy(attributes.requestMethod, "GET");
  attributes.attributes = platform::emscripten::kFetchLoadToMemory;
  auto *fetch = new detail::Fetch{std::move(complete), std::move(handle)};
  attributes.userData = fetch;
  attributes.onsuccess = [](platform::emscripten::emscripten_fetch_t *result) {
    auto *state = static_cast<detail::Fetch *>(result->userData);
    Response response;
    response.fStatus = result->status;
    response.fOk = result->status >= 200 && result->status < 300;
    response.fBody.assign(result->data,
                          static_cast<std::size_t>(result->numBytes));
    if (!response.fOk) response.fError = "HTTP " + std::to_string(result->status);
    state->fHandle->fProgress.store(1.0f, std::memory_order_relaxed);
    state->fHandle->fDone.store(true, std::memory_order_release);
    state->fComplete(std::move(response));
    delete state;
    platform::emscripten::emscripten_fetch_close(result);
  };
  attributes.onerror = [](platform::emscripten::emscripten_fetch_t *result) {
    auto *state = static_cast<detail::Fetch *>(result->userData);
    Response response;
    response.fStatus = result->status;
    response.fError = "network error (HTTP " + std::to_string(result->status) +
                      "); possibly CORS";
    state->fHandle->fDone.store(true, std::memory_order_release);
    state->fComplete(std::move(response));
    delete state;
    platform::emscripten::emscripten_fetch_close(result);
  };
  attributes.onprogress = [](platform::emscripten::emscripten_fetch_t *result) {
    auto *state = static_cast<detail::Fetch *>(result->userData);
    if (result->totalBytes > 0) {
      state->fHandle->fProgress.store(
          static_cast<float>(result->dataOffset) /
              static_cast<float>(result->totalBytes),
          std::memory_order_relaxed);
    }
  };
  platform::emscripten::emscripten_fetch(&attributes, url.c_str());
}
} // namespace platform::network::backend
