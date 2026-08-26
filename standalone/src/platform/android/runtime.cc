module;

#include <android/asset_manager.h>
#include <android/native_activity.h>
#include <android_native_app_glue.h>

export module platform.android_runtime;

import std;

export namespace platform::android {

void attach(android_app *app);
[[nodiscard]] android_app *application();
[[nodiscard]] bool prepareAssets();

} // namespace platform::android

namespace platform::android {

namespace {
android_app *gApplication = nullptr;
}

void attach(android_app *app) { gApplication = app; }

android_app *application() { return gApplication; }

namespace {

bool copyAsset(AAssetManager *manager, const std::string &name,
               const std::filesystem::path &destination) {
  AAsset *asset = AAssetManager_open(manager, name.c_str(), AASSET_MODE_STREAMING);
  if (asset == nullptr) {
    return false;
  }
  const auto close = std::unique_ptr<AAsset, decltype(&AAsset_close)>(
      asset, &AAsset_close);
  const auto length = static_cast<std::uintmax_t>(AAsset_getLength64(asset));
  std::error_code ec;
  if (std::filesystem::file_size(destination, ec) == length && !ec) {
    return true;
  }
  std::filesystem::create_directories(destination.parent_path(), ec);
  if (ec) {
    return false;
  }
  std::ofstream output(destination, std::ios::binary | std::ios::trunc);
  if (!output) {
    return false;
  }
  std::array<char, 64 * 1024> buffer{};
  for (;;) {
    const int count = AAsset_read(asset, buffer.data(), buffer.size());
    if (count < 0) {
      return false;
    }
    if (count == 0) {
      return true;
    }
    output.write(buffer.data(), count);
    if (!output) {
      return false;
    }
  }
}

bool copyDirectory(AAssetManager *manager, std::string_view directory,
                   const std::filesystem::path &destination) {
  const std::string owned(directory);
  AAssetDir *raw = AAssetManager_openDir(manager, owned.c_str());
  if (raw == nullptr) {
    return false;
  }
  const auto close = std::unique_ptr<AAssetDir, decltype(&AAssetDir_close)>(
      raw, &AAssetDir_close);
  bool copied = true;
  while (const char *name = AAssetDir_getNextFileName(raw)) {
    copied = copyAsset(manager, owned + "/" + name, destination / name) &&
             copied;
  }
  return copied;
}

} // namespace

bool prepareAssets() {
  android_app *app = application();
  if (app == nullptr || app->activity == nullptr ||
      app->activity->assetManager == nullptr ||
      app->activity->internalDataPath == nullptr) {
    return false;
  }
  const std::filesystem::path root =
      std::filesystem::path(app->activity->internalDataPath) / "share" /
      "osu_client";
  return copyDirectory(app->activity->assetManager, "fonts", root / "fonts") &&
         copyDirectory(app->activity->assetManager, "licenses",
                       root / "licenses");
}

} // namespace platform::android
