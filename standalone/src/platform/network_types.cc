export module platform.network.types;

import std;

export namespace platform::network {
struct Response {
  bool fOk = false;
  long fStatus = 0;
  std::string fBody;
  std::string fError;
};
struct Handle {
  std::atomic<float> fProgress{0.0f};
  std::atomic<bool> fDone{false};
};
using Callback = std::function<void(Response)>;
} // namespace platform::network
