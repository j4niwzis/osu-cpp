export module platform.network;

export import platform.network.types;
import platform.network.backend;
import std;

namespace platform::network::detail {
struct Completed {
  Callback fCallback;
  Response fResponse;
};
inline std::mutex &mutex() {
  static std::mutex value;
  return value;
}
inline std::vector<Completed> &queue() {
  static std::vector<Completed> value;
  return value;
}
inline void complete(Callback callback, Response response) {
  const std::scoped_lock lock(mutex());
  queue().push_back({std::move(callback), std::move(response)});
}
} // namespace platform::network::detail

export namespace platform::network {
inline void get(std::string url, std::shared_ptr<Handle> handle,
                Callback callback) {
  backend::start(
      std::move(url), std::move(handle),
      [callback = std::move(callback)](Response response) mutable {
        detail::complete(std::move(callback), std::move(response));
      });
}
inline std::size_t poll() {
  std::vector<detail::Completed> ready;
  {
    const std::scoped_lock lock(detail::mutex());
    ready.swap(detail::queue());
  }
  for (auto &completed : ready) {
    completed.fCallback(std::move(completed.fResponse));
  }
  return ready.size();
}
} // namespace platform::network
