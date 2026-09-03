module;

#ifndef OSU_ANDROID_SYSTEM_FILE_PICKER
#define OSU_ANDROID_SYSTEM_FILE_PICKER 0
#endif

export module platform.dialogs.backend;

import std;
import platform.android.api;
import platform.android_runtime;
import platform.dialogs.types;

namespace platform::dialogs::backend::detail {

#if OSU_ANDROID_SYSTEM_FILE_PICKER

std::mutex gPickerMutex;
std::condition_variable gPickerChanged;
bool gPickerFinished = false;
std::string gPickerUri;

// What this path did and did not manage, in the system log.
//
// Every step of it can fail in a way that leaves the screen exactly as it
// was -- no window, no error, no crash -- and until now none of them said
// so. A user pressing the button and getting nothing had nothing to read,
// and neither did anyone asked to find out why.
void say(std::string_view message) {
  const std::string owned(message);
  __android_log_write(ANDROID_LOG_INFO, "osu!cpp", owned.c_str());
}

// Whether Java threw, and clearing it if it did.
//
// JNI does not forgive: a call made while an exception is pending is not an
// error return, it is the runtime stopping the process -- "JNI DETECTED
// ERROR IN APPLICATION: ... called with pending exception". Everything below
// that can throw is followed by this, because the things that throw here are
// ordinary: a document provider that will not open what was chosen, a
// permission that was not persisted, a stream that ends badly.
bool threw(JNIEnv *env, std::string_view what) {
  if (!env->ExceptionCheck()) {
    return false;
  }
  say(std::string("java objected while ") + std::string(what));
  env->ExceptionDescribe();
  env->ExceptionClear();
  return true;
}

struct Environment {
  JavaVM *fVm = nullptr;
  JNIEnv *fEnv = nullptr;
  bool fAttached = false;

  explicit Environment(ANativeActivity *activity) : fVm(activity->vm) {
    if (fVm == nullptr) {
      return;
    }
    void *raw = nullptr;
    const jint state = fVm->GetEnv(&raw, JNI_VERSION_1_6);
    if (state == JNI_OK) {
      fEnv = static_cast<JNIEnv *>(raw);
    } else if (state == JNI_EDETACHED &&
               fVm->AttachCurrentThread(&fEnv, nullptr) == JNI_OK) {
      fAttached = true;
    }
  }

  ~Environment() {
    if (fAttached && fVm != nullptr) {
      fVm->DetachCurrentThread();
    }
  }
};

bool requestPicker(ANativeActivity *activity) {
  Environment attached(activity);
  JNIEnv *env = attached.fEnv;
  if (env == nullptr) {
    say("the picker cannot be opened: this thread has no JNI environment");
    return false;
  }
  jclass activityClass = env->GetObjectClass(activity->clazz);
  jmethodID open = activityClass != nullptr
                       ? env->GetMethodID(activityClass, "openBeatmapPicker",
                                          "()V")
                       : nullptr;
  if (activityClass == nullptr) {
    say("the picker cannot be opened: the activity has no class");
  } else if (open == nullptr) {
    // Which is what happens when the activity is android.app.NativeActivity
    // rather than this project's subclass of it -- a package built without
    // the picker, or a manifest naming the other one.
    say("the picker cannot be opened: this activity has no "
        "openBeatmapPicker method");
    env->ExceptionClear();
  }
  if (open != nullptr) {
    env->CallVoidMethod(activity->clazz, open);
  }
  const bool succeeded = open != nullptr && !env->ExceptionCheck();
  if (env->ExceptionCheck()) {
    say("the picker was asked for and Java objected");
    env->ExceptionDescribe();
    env->ExceptionClear();
  }
  if (activityClass != nullptr) {
    env->DeleteLocalRef(activityClass);
  }
  return succeeded;
}

bool requestVideo(ANativeActivity *activity, std::string_view suggested) {
  Environment attached(activity);
  JNIEnv *env = attached.fEnv;
  if (env == nullptr) {
    return false;
  }
  jclass activityClass = env->GetObjectClass(activity->clazz);
  jmethodID create =
      activityClass != nullptr
          ? env->GetMethodID(activityClass, "createVideoFile",
                             "(Ljava/lang/String;)V")
          : nullptr;
  const std::string owned(suggested);
  jstring name = env->NewStringUTF(owned.c_str());
  if (create != nullptr && name != nullptr) {
    env->CallVoidMethod(activity->clazz, create, name);
  }
  const bool succeeded = create != nullptr && name != nullptr &&
                         !env->ExceptionCheck();
  if (env->ExceptionCheck()) {
    env->ExceptionClear();
  }
  if (name != nullptr) {
    env->DeleteLocalRef(name);
  }
  if (activityClass != nullptr) {
    env->DeleteLocalRef(activityClass);
  }
  return succeeded;
}

std::optional<std::filesystem::path> copyUri(ANativeActivity *activity,
                                             const std::string &uriText) {
  Environment attached(activity);
  JNIEnv *env = attached.fEnv;
  if (env == nullptr || activity->internalDataPath == nullptr) {
    say("the chosen document cannot be copied: no JNI environment or no "
        "private directory to copy it into");
    return std::nullopt;
  }
  jclass uriClass = env->FindClass("android/net/Uri");
  jmethodID parse = uriClass != nullptr
                        ? env->GetStaticMethodID(
                              uriClass, "parse",
                              "(Ljava/lang/String;)Landroid/net/Uri;")
                        : nullptr;
  jstring text = env->NewStringUTF(uriText.c_str());
  jobject uri = parse != nullptr && text != nullptr
                    ? env->CallStaticObjectMethod(uriClass, parse, text)
                    : nullptr;
  if (threw(env, "reading the chosen address")) {
    uri = nullptr;
  }
  jclass activityClass = env->GetObjectClass(activity->clazz);
  jmethodID getResolver =
      activityClass != nullptr
          ? env->GetMethodID(activityClass, "getContentResolver",
                             "()Landroid/content/ContentResolver;")
          : nullptr;
  jobject resolver = getResolver != nullptr
                         ? env->CallObjectMethod(activity->clazz, getResolver)
                         : nullptr;
  if (threw(env, "asking the activity for its content resolver")) {
    resolver = nullptr;
  }
  jclass resolverClass =
      resolver != nullptr ? env->GetObjectClass(resolver) : nullptr;
  jmethodID openStream =
      resolverClass != nullptr
          ? env->GetMethodID(resolverClass, "openInputStream",
                             "(Landroid/net/Uri;)Ljava/io/InputStream;")
          : nullptr;
  jobject stream = openStream != nullptr && uri != nullptr
                       ? env->CallObjectMethod(resolver, openStream, uri)
                       : nullptr;
  // These throw in the ordinary course of things: a provider that has gone
  // away, a permission that did not persist, a document that is not there
  // any more. Left pending, the next call takes the process with it.
  if (threw(env, "opening the chosen document")) {
    stream = nullptr;
  }
  jclass streamClass = stream != nullptr ? env->GetObjectClass(stream) : nullptr;
  jmethodID read = streamClass != nullptr
                       ? env->GetMethodID(streamClass, "read", "([B)I")
                       : nullptr;
  jmethodID close = streamClass != nullptr
                        ? env->GetMethodID(streamClass, "close", "()V")
                        : nullptr;

  const std::filesystem::path destination =
      std::filesystem::path(activity->internalDataPath) / "import.osz";
  std::ofstream output(destination, std::ios::binary | std::ios::trunc);
  constexpr jsize kBufferSize = 64 * 1024;
  jbyteArray bytes = env->NewByteArray(kBufferSize);
  if (threw(env, "making room to copy the document")) {
    bytes = nullptr;
  }
  std::vector<jbyte> buffer(static_cast<std::size_t>(kBufferSize));
  bool copied = output.good() && bytes != nullptr && read != nullptr;
  while (copied) {
    const jint count = env->CallIntMethod(stream, read, bytes);
    if (env->ExceptionCheck() || count < 0) {
      break;
    }
    if (count == 0) {
      continue;
    }
    env->GetByteArrayRegion(bytes, 0, count, buffer.data());
    if (env->ExceptionCheck()) {
      copied = false;
      break;
    }
    for (jint index = 0; index < count; ++index) {
      output.put(static_cast<char>(buffer[static_cast<std::size_t>(index)]));
    }
    copied = output.good();
  }
  if (close != nullptr && stream != nullptr) {
    env->CallVoidMethod(stream, close);
  }
  if (threw(env, "closing the document")) {
    copied = false;
  }
  if (!copied) {
    say("the document was not copied");
  }
  for (jobject reference : {
           static_cast<jobject>(bytes), static_cast<jobject>(streamClass),
           stream, static_cast<jobject>(resolverClass), resolver,
           static_cast<jobject>(activityClass), uri, static_cast<jobject>(text),
           static_cast<jobject>(uriClass)}) {
    if (reference != nullptr) {
      env->DeleteLocalRef(reference);
    }
  }
  if (!copied) {
    std::error_code error;
    std::filesystem::remove(destination, error);
    return std::nullopt;
  }
  return destination;
}

bool copyFileToUri(ANativeActivity *activity,
                   const std::filesystem::path &source,
                   const std::string &uriText) {
  Environment attached(activity);
  JNIEnv *env = attached.fEnv;
  if (env == nullptr) {
    return false;
  }
  jclass uriClass = env->FindClass("android/net/Uri");
  jmethodID parse = uriClass != nullptr
                        ? env->GetStaticMethodID(
                              uriClass, "parse",
                              "(Ljava/lang/String;)Landroid/net/Uri;")
                        : nullptr;
  jstring text = env->NewStringUTF(uriText.c_str());
  jobject uri = parse != nullptr && text != nullptr
                    ? env->CallStaticObjectMethod(uriClass, parse, text)
                    : nullptr;
  if (threw(env, "reading the chosen address")) {
    uri = nullptr;
  }
  jclass activityClass = env->GetObjectClass(activity->clazz);
  jmethodID getResolver =
      activityClass != nullptr
          ? env->GetMethodID(activityClass, "getContentResolver",
                             "()Landroid/content/ContentResolver;")
          : nullptr;
  jobject resolver = getResolver != nullptr
                         ? env->CallObjectMethod(activity->clazz, getResolver)
                         : nullptr;
  if (threw(env, "asking the activity for its content resolver")) {
    resolver = nullptr;
  }
  jclass resolverClass =
      resolver != nullptr ? env->GetObjectClass(resolver) : nullptr;
  jmethodID openStream =
      resolverClass != nullptr
          ? env->GetMethodID(resolverClass, "openOutputStream",
                             "(Landroid/net/Uri;)Ljava/io/OutputStream;")
          : nullptr;
  jobject stream = openStream != nullptr && uri != nullptr
                       ? env->CallObjectMethod(resolver, openStream, uri)
                       : nullptr;
  // These throw in the ordinary course of things: a provider that has gone
  // away, a permission that did not persist, a document that is not there
  // any more. Left pending, the next call takes the process with it.
  if (threw(env, "opening the chosen document")) {
    stream = nullptr;
  }
  jclass streamClass = stream != nullptr ? env->GetObjectClass(stream) : nullptr;
  jmethodID write = streamClass != nullptr
                        ? env->GetMethodID(streamClass, "write", "([BII)V")
                        : nullptr;
  jmethodID close = streamClass != nullptr
                        ? env->GetMethodID(streamClass, "close", "()V")
                        : nullptr;

  std::ifstream input(source, std::ios::binary);
  constexpr std::size_t kBufferSize = 64 * 1024;
  std::array<char, kBufferSize> buffer{};
  jbyteArray bytes = env->NewByteArray(static_cast<jsize>(kBufferSize));
  std::vector<jbyte> javaBytes(kBufferSize);
  bool copied = input.good() && stream != nullptr && write != nullptr &&
                bytes != nullptr;
  while (copied && input) {
    input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    const std::streamsize count = input.gcount();
    if (count <= 0) {
      break;
    }
    for (std::streamsize index = 0; index < count; ++index) {
      javaBytes[static_cast<std::size_t>(index)] =
          static_cast<jbyte>(buffer[static_cast<std::size_t>(index)]);
    }
    env->SetByteArrayRegion(bytes, 0, static_cast<jsize>(count),
                            javaBytes.data());
    env->CallVoidMethod(stream, write, bytes, 0, static_cast<jint>(count));
    copied = !env->ExceptionCheck();
  }
  if (close != nullptr && stream != nullptr) {
    env->CallVoidMethod(stream, close);
  }
  if (threw(env, "closing the document")) {
    copied = false;
  }
  if (!copied) {
    say("the document was not copied");
  }
  for (jobject reference : {
           static_cast<jobject>(bytes), static_cast<jobject>(streamClass),
           stream, static_cast<jobject>(resolverClass), resolver,
           static_cast<jobject>(activityClass), uri, static_cast<jobject>(text),
           static_cast<jobject>(uriClass)}) {
    if (reference != nullptr) {
      env->DeleteLocalRef(reference);
    }
  }
  return copied;
}

#else

std::optional<std::filesystem::path> fallbackArchive() {
  std::optional<std::filesystem::path> newest;
  std::filesystem::file_time_type newestTime{};
  for (const std::filesystem::path directory : {
           std::filesystem::path("/storage/emulated/0/Download"),
           std::filesystem::path("/sdcard/Download")}) {
    std::error_code error;
    for (std::filesystem::directory_iterator it(directory, error), end;
         !error && it != end; it.increment(error)) {
      if (!it->is_regular_file(error) || it->path().extension() != ".osz") {
        continue;
      }
      const auto modified = it->last_write_time(error);
      if (!error && (!newest || modified > newestTime)) {
        newest = it->path();
        newestTime = modified;
      }
    }
  }
  return newest;
}

#endif

} // namespace platform::dialogs::backend::detail

#if OSU_ANDROID_SYSTEM_FILE_PICKER
extern "C" __attribute__((visibility("default"))) void
Java_io_github_j4niwzis_osu_1cpp_OsuNativeActivity_nativeDocumentSelected(
    JNIEnv *env, jobject, jint, jstring uri) {
  std::string selected;
  if (uri != nullptr) {
    const char *text = env->GetStringUTFChars(uri, nullptr);
    if (text != nullptr) {
      selected = text;
      env->ReleaseStringUTFChars(uri, text);
    }
  }
  {
    std::lock_guard lock(platform::dialogs::backend::detail::gPickerMutex);
    platform::dialogs::backend::detail::gPickerUri = std::move(selected);
    platform::dialogs::backend::detail::gPickerFinished = true;
  }
  platform::dialogs::backend::detail::gPickerChanged.notify_one();
}
#endif

export namespace platform::dialogs::backend {

[[nodiscard]] inline std::optional<std::filesystem::path>
openArchive(const std::string &) {
#if OSU_ANDROID_SYSTEM_FILE_PICKER
  android_app *app = platform::android::application();
  if (app == nullptr || app->activity == nullptr) {
    detail::say("import: there is no activity to ask");
    return std::nullopt;
  }
  detail::say("import: asking for the document picker");
  {
    std::lock_guard lock(detail::gPickerMutex);
    detail::gPickerFinished = false;
    detail::gPickerUri.clear();
  }
  if (!detail::requestPicker(app->activity)) {
    return std::nullopt;
  }
  std::string uri;
  {
    // Waited for, but not for ever.
    //
    // This runs on the loader rather than on the thread that draws, so a
    // wait that never ends is not a window that freezes -- it is a thread
    // parked for the life of the process and a button that does nothing
    // from then on, because the client will not open a second dialog while
    // it believes one is open. Every way this can happen is on the far side
    // of a process boundary: an intent nothing answers, a result delivered
    // to an activity that has been recreated, a picker the user left by a
    // route that reports nothing. Ten minutes is longer than any of those
    // and shorter than for ever.
    std::unique_lock lock(detail::gPickerMutex);
    if (!detail::gPickerChanged.wait_for(
            lock, std::chrono::minutes(10),
            [] { return detail::gPickerFinished; })) {
      detail::say("nothing came back from the document picker; giving up");
      return std::nullopt;
    }
    uri = detail::gPickerUri;
  }
  if (uri.empty()) {
    detail::say("the document picker was closed without choosing anything");
    return std::nullopt;
  }
  detail::say("a document was chosen; copying it in");
  return detail::copyUri(app->activity, uri);
#else
  return detail::fallbackArchive();
#endif
}

[[nodiscard]] inline SaveFileResult saveVideo(const std::string &,
                                              std::string_view suggested) {
#if OSU_ANDROID_SYSTEM_FILE_PICKER
  android_app *app = platform::android::application();
  if (app == nullptr || app->activity == nullptr ||
      app->activity->internalDataPath == nullptr) {
    return {true, std::nullopt, {}};
  }
  {
    std::lock_guard lock(detail::gPickerMutex);
    detail::gPickerFinished = false;
    detail::gPickerUri.clear();
  }
  if (!detail::requestVideo(app->activity, suggested)) {
    return {true, std::nullopt, {}};
  }
  std::string uri;
  {
    std::unique_lock lock(detail::gPickerMutex);
    if (!detail::gPickerChanged.wait_for(
            lock, std::chrono::minutes(10),
            [] { return detail::gPickerFinished; })) {
      detail::say("nothing came back from the file creator; giving up");
      return {true, std::nullopt, {}};
    }
    uri = detail::gPickerUri;
  }
  if (uri.empty()) {
    return {true, std::nullopt, {}};
  }
  const auto path = std::filesystem::path(app->activity->internalDataPath) /
                    "export-video.mp4";
  return {true, path, std::move(uri)};
#else
  return {};
#endif
}

[[nodiscard]] inline bool commitSave(const SaveFileResult &result) {
#if OSU_ANDROID_SYSTEM_FILE_PICKER
  if (result.fPlatformToken.empty() || !result.fPath) {
    return true;
  }
  android_app *app = platform::android::application();
  const bool copied =
      app != nullptr && app->activity != nullptr &&
      detail::copyFileToUri(app->activity, *result.fPath,
                            result.fPlatformToken);
  if (copied) {
    std::error_code error;
    std::filesystem::remove(*result.fPath, error);
  }
  return copied;
#else
  return true;
#endif
}

} // namespace platform::dialogs::backend
