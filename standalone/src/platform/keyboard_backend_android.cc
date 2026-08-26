export module platform.keyboard.backend;

import std;
import platform.android.api;
import platform.android_runtime;
import platform.keyboard.types;

namespace platform::keyboard::backend::detail {

void forceShowWithJni(ANativeActivity *activity) {
  if (activity->vm == nullptr || activity->clazz == nullptr) {
    return;
  }
  JNIEnv *env = nullptr;
  bool attached = false;
  void *raw = nullptr;
  const jint state = activity->vm->GetEnv(&raw, JNI_VERSION_1_6);
  if (state == JNI_OK) {
    env = static_cast<JNIEnv *>(raw);
  } else if (state == JNI_EDETACHED &&
             activity->vm->AttachCurrentThread(&env, nullptr) == JNI_OK) {
    attached = true;
  }
  if (env == nullptr) {
    return;
  }

  jclass activityClass = env->GetObjectClass(activity->clazz);
  jmethodID getSystemService =
      activityClass != nullptr
          ? env->GetMethodID(activityClass, "getSystemService",
                             "(Ljava/lang/String;)Ljava/lang/Object;")
          : nullptr;
  jstring serviceName = env->NewStringUTF("input_method");
  jobject inputMethod = getSystemService != nullptr && serviceName != nullptr
                            ? env->CallObjectMethod(activity->clazz,
                                                    getSystemService,
                                                    serviceName)
                            : nullptr;
  jclass inputMethodClass =
      inputMethod != nullptr ? env->GetObjectClass(inputMethod) : nullptr;
  jmethodID toggleSoftInput =
      inputMethodClass != nullptr
          ? env->GetMethodID(inputMethodClass, "toggleSoftInput", "(II)V")
          : nullptr;
  if (toggleSoftInput != nullptr) {
    env->CallVoidMethod(inputMethod, toggleSoftInput,
                        ANATIVEACTIVITY_SHOW_SOFT_INPUT_FORCED, 0);
  }
  if (env->ExceptionCheck()) {
    env->ExceptionClear();
  }
  for (jobject reference : {static_cast<jobject>(inputMethodClass), inputMethod,
                            static_cast<jobject>(serviceName),
                            static_cast<jobject>(activityClass)}) {
    if (reference != nullptr) {
      env->DeleteLocalRef(reference);
    }
  }
  if (attached) {
    activity->vm->DetachCurrentThread();
  }
}

} // namespace platform::keyboard::backend::detail

export namespace platform::keyboard::backend {

inline void setVisible(bool visible) {
  android_app *app = platform::android::application();
  if (app == nullptr || app->activity == nullptr) {
    return;
  }
  if (visible) {
    detail::forceShowWithJni(app->activity);
    ANativeActivity_showSoftInput(
        app->activity, ANATIVEACTIVITY_SHOW_SOFT_INPUT_FORCED);
  } else {
    ANativeActivity_hideSoftInput(
        app->activity, ANATIVEACTIVITY_HIDE_SOFT_INPUT_NOT_ALWAYS);
  }
}

inline std::vector<Event> poll() { return {}; }

} // namespace platform::keyboard::backend
