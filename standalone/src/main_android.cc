module;

#include <android_native_app_glue.h>

import std;
import osu;
import app;
import platform.android_runtime;

extern "C" void android_main(android_app *state) {
  app_dummy();
  platform::android::attach(state);

  try {
    if (!platform::android::prepareAssets()) {
      throw std::runtime_error("unable to prepare bundled assets");
    }
    client::App app(std::nullopt, osu::mod::kNone, false, false, {}, false,
                    {}, false, true);
    (void)app.run();
    ANativeActivity_finish(state->activity);
  } catch (const std::exception &error) {
    std::println(std::cerr, "Android startup failed: {}", error.what());
    ANativeActivity_finish(state->activity);
  }
}
