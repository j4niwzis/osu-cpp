export module platform.android_runtime;

import std;
import platform.android.api;

export namespace platform::android {

void attach(android_app *app);
[[nodiscard]] android_app *application();
[[nodiscard]] bool prepareAssets();
[[nodiscard]] bool requestLandscape();

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

bool requestLandscape() {
  android_app *app = application();
  if (app == nullptr || app->activity == nullptr ||
      app->activity->vm == nullptr || app->activity->clazz == nullptr) {
    return false;
  }
  JavaVM *vm = app->activity->vm;
  JNIEnv *env = nullptr;
  bool attached = false;
  void *raw = nullptr;
  const jint state = vm->GetEnv(&raw, JNI_VERSION_1_6);
  if (state == JNI_OK) {
    env = static_cast<JNIEnv *>(raw);
  } else if (state == JNI_EDETACHED &&
             vm->AttachCurrentThread(&env, nullptr) == JNI_OK) {
    attached = true;
  }
  if (env == nullptr) {
    return false;
  }
  jclass activityClass = env->GetObjectClass(app->activity->clazz);
  jmethodID setRequestedOrientation =
      activityClass != nullptr
          ? env->GetMethodID(activityClass, "setRequestedOrientation", "(I)V")
          : nullptr;
  bool succeeded = setRequestedOrientation != nullptr;
  if (succeeded) {
    constexpr jint kSensorLandscape = 6;
    env->CallVoidMethod(app->activity->clazz, setRequestedOrientation,
                        kSensorLandscape);
    succeeded = !env->ExceptionCheck();
  }
  if (env->ExceptionCheck()) {
    env->ExceptionClear();
  }
  if (activityClass != nullptr) {
    env->DeleteLocalRef(activityClass);
  }
  if (attached) {
    vm->DetachCurrentThread();
  }
  return succeeded;
}

} // namespace platform::android
