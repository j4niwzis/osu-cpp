export module platform.network.backend;

import std;
import beast;
import platform.network.types;

export namespace platform::network::backend {
inline void start(std::string url, std::shared_ptr<Handle> handle,
                  Callback complete) {
  std::thread(
      [](std::string request, std::shared_ptr<Handle> progress,
         Callback done) {
        beastnet::FetchResult result = beastnet::fetch(
            request, [&progress](std::size_t received, std::size_t total) {
              if (total > 0) {
                progress->fProgress.store(
                    static_cast<float>(received) / static_cast<float>(total),
                    std::memory_order_relaxed);
              }
            });
        Response response;
        response.fOk = result.fOk;
        response.fStatus = result.fStatus;
        response.fBody = std::move(result.fBody);
        response.fError = std::move(result.fError);
        progress->fProgress.store(1.0f, std::memory_order_relaxed);
        progress->fDone.store(true, std::memory_order_release);
        done(std::move(response));
      },
      std::move(url), std::move(handle), std::move(complete))
      .detach();
}
} // namespace platform::network::backend
