export module platform.keyboard.backend;

import std;
import platform.android.api;
import platform.android_runtime;
import platform.keyboard.types;

export namespace platform::keyboard::backend {

inline void setVisible(bool visible) {
  android_app *app = platform::android::application();
  if (app == nullptr || app->activity == nullptr) {
    return;
  }
  if (visible) {
    ANativeActivity_showSoftInput(
        app->activity, ANATIVEACTIVITY_SHOW_SOFT_INPUT_IMPLICIT);
  } else {
    ANativeActivity_hideSoftInput(
        app->activity, ANATIVEACTIVITY_HIDE_SOFT_INPUT_NOT_ALWAYS);
  }
}

inline std::vector<Event> poll() { return {}; }

} // namespace platform::keyboard::backend
