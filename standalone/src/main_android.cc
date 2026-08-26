import std;
import osu;
import app;
import platform.android.api;
import platform.android_runtime;
import platform.system;

extern "C" void android_main(android_app *state) {
  platform::android::attach(state);

  try {
    (void)platform::android::requestLandscape();
    if (!platform::android::prepareAssets()) {
      throw std::runtime_error("unable to prepare bundled assets");
    }
    const std::filesystem::path skinPath =
        platform::system::applicationDataPath() / "skin";
    std::error_code error;
    std::filesystem::create_directories(skinPath, error);
    if (error) {
      throw std::runtime_error(
          std::format("unable to create skin directory {}: {}",
                      skinPath.string(), error.message()));
    }
    client::App app(std::nullopt, osu::mod::kNone, false, false, {}, false,
                    skinPath, false, true);
    (void)app.run();
    ANativeActivity_finish(state->activity);
  } catch (const std::exception &error) {
    std::println(std::cerr, "Android startup failed: {}", error.what());
    ANativeActivity_finish(state->activity);
  }
}
