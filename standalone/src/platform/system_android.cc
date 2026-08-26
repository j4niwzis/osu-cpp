export module platform.system;

import std;
import platform.android.api;
import platform.android_runtime;

namespace {

[[nodiscard]] std::filesystem::path internalDataPath() {
  const android_app *app = platform::android::application();
  if (app == nullptr || app->activity == nullptr ||
      app->activity->internalDataPath == nullptr) {
    return "/data/local/tmp/osu-cpp";
  }
  return app->activity->internalDataPath;
}

} // namespace

export namespace platform::system {

[[nodiscard]] std::filesystem::path executablePath(std::string_view) {
  return internalDataPath() / "lib" / "libosu_client.so";
}

[[nodiscard]] std::filesystem::path applicationDataPath() {
  return internalDataPath();
}

[[nodiscard]] std::filesystem::path mapsDirectory() {
  return internalDataPath() / "maps";
}

} // namespace platform::system
