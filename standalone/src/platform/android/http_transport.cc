export module platform.android.http_transport;

import std;
import platform.android.api;
import platform.android_runtime;

export namespace platform::android::http_transport {

struct FetchResult {
  bool fOk = false;
  long fStatus = 0;
  std::string fBody;
  std::string fError;
};

using Progress = std::function<void(std::size_t, std::size_t)>;

} // namespace platform::android::http_transport

namespace platform::android::http_transport::detail {

class Environment {
public:
  explicit Environment(JavaVM *vm) : fVm(vm) {
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

  Environment(const Environment &) = delete;
  Environment &operator=(const Environment &) = delete;

  ~Environment() {
    if (fAttached) {
      fVm->DetachCurrentThread();
    }
  }

  [[nodiscard]] JNIEnv *get() const { return fEnv; }

private:
  JavaVM *fVm = nullptr;
  JNIEnv *fEnv = nullptr;
  bool fAttached = false;
};

inline bool clearException(JNIEnv *env, std::string &error,
                           std::string_view operation) {
  if (!env->ExceptionCheck()) {
    return false;
  }
  env->ExceptionClear();
  error = std::string(operation) + " failed";
  return true;
}

inline FetchResult fetchImpl(JNIEnv *env, std::string_view url,
                             const Progress &progress) {
  FetchResult result;
  jclass urlClass = env->FindClass("java/net/URL");
  jclass connectionClass = env->FindClass("java/net/HttpURLConnection");
  jclass inputClass = env->FindClass("java/io/InputStream");
  const bool classError =
      clearException(env, result.fError, "loading Android HTTP classes");
  if (classError || urlClass == nullptr || connectionClass == nullptr ||
      inputClass == nullptr) {
    result.fError = "Android HTTP classes are unavailable";
    return result;
  }

  const jmethodID urlConstructor =
      env->GetMethodID(urlClass, "<init>", "(Ljava/lang/String;)V");
  const jmethodID openConnection = env->GetMethodID(
      urlClass, "openConnection", "()Ljava/net/URLConnection;");
  const jmethodID setConnectTimeout =
      env->GetMethodID(connectionClass, "setConnectTimeout", "(I)V");
  const jmethodID setReadTimeout =
      env->GetMethodID(connectionClass, "setReadTimeout", "(I)V");
  const jmethodID setFollowRedirects = env->GetMethodID(
      connectionClass, "setInstanceFollowRedirects", "(Z)V");
  const jmethodID setRequestProperty = env->GetMethodID(
      connectionClass, "setRequestProperty",
      "(Ljava/lang/String;Ljava/lang/String;)V");
  const jmethodID responseCode =
      env->GetMethodID(connectionClass, "getResponseCode", "()I");
  const jmethodID contentLength =
      env->GetMethodID(connectionClass, "getContentLengthLong", "()J");
  const jmethodID inputStream = env->GetMethodID(
      connectionClass, "getInputStream", "()Ljava/io/InputStream;");
  const jmethodID errorStream = env->GetMethodID(
      connectionClass, "getErrorStream", "()Ljava/io/InputStream;");
  const jmethodID disconnect =
      env->GetMethodID(connectionClass, "disconnect", "()V");
  const jmethodID read = env->GetMethodID(inputClass, "read", "([B)I");
  const jmethodID close = env->GetMethodID(inputClass, "close", "()V");
  if (clearException(env, result.fError, "resolving Android HTTP methods")) {
    return result;
  }

  const std::string ownedUrl(url);
  jstring urlString = env->NewStringUTF(ownedUrl.c_str());
  jobject urlObject =
      env->NewObject(urlClass, urlConstructor, urlString);
  jobject connection =
      env->CallObjectMethod(urlObject, openConnection);
  const bool openError = clearException(env, result.fError, "opening URL");
  if (openError || connection == nullptr) {
    return result;
  }

  env->CallVoidMethod(connection, setConnectTimeout, 30000);
  env->CallVoidMethod(connection, setReadTimeout, 30000);
  env->CallVoidMethod(connection, setFollowRedirects, JNI_TRUE);
  const auto property = [&](const char *name, const char *value) {
    jstring key = env->NewStringUTF(name);
    jstring text = env->NewStringUTF(value);
    env->CallVoidMethod(connection, setRequestProperty, key, text);
    env->DeleteLocalRef(text);
    env->DeleteLocalRef(key);
  };
  property("User-Agent", "osu-cpp/1.0");
  property("Accept", "*/*");
  // Keep the response bytes identical across the native Beast and Android
  // transports. HttpURLConnection otherwise negotiates compression using
  // implementation-dependent defaults, while the client parses the returned
  // body directly as JSON or an archive.
  property("Accept-Encoding", "identity");
  result.fStatus =
      static_cast<long>(env->CallIntMethod(connection, responseCode));
  if (clearException(env, result.fError, "connecting")) {
    env->CallVoidMethod(connection, disconnect);
    return result;
  }

  const jlong reportedLength =
      env->CallLongMethod(connection, contentLength);
  const std::size_t total = reportedLength > 0
                                ? static_cast<std::size_t>(reportedLength)
                                : 0;
  jobject stream = result.fStatus >= 400
                       ? env->CallObjectMethod(connection, errorStream)
                       : env->CallObjectMethod(connection, inputStream);
  if (clearException(env, result.fError, "opening response body")) {
    env->CallVoidMethod(connection, disconnect);
    return result;
  }

  if (stream != nullptr) {
    constexpr jsize kBufferSize = 64 * 1024;
    jbyteArray bytes = env->NewByteArray(kBufferSize);
    std::vector<jbyte> buffer(static_cast<std::size_t>(kBufferSize));
    for (;;) {
      const jint count = env->CallIntMethod(stream, read, bytes);
      if (clearException(env, result.fError, "reading response body")) {
        break;
      }
      if (count <= 0) {
        break;
      }
      env->GetByteArrayRegion(bytes, 0, count, buffer.data());
      for (jint index = 0; index < count; ++index) {
        result.fBody.push_back(
            static_cast<char>(buffer[static_cast<std::size_t>(index)]));
      }
      if (progress) {
        progress(result.fBody.size(), total);
      }
    }
    env->CallVoidMethod(stream, close);
    env->DeleteLocalRef(bytes);
    env->DeleteLocalRef(stream);
  }
  env->CallVoidMethod(connection, disconnect);
  result.fOk = result.fError.empty() && result.fStatus >= 200 &&
               result.fStatus < 300;
  if (!result.fOk && result.fError.empty()) {
    result.fError = "HTTP " + std::to_string(result.fStatus);
  }
  return result;
}

} // namespace platform::android::http_transport::detail

export namespace platform::android::http_transport {

inline FetchResult fetch(std::string_view url, const Progress &progress = {}) {
  android_app *app = platform::android::application();
  if (app == nullptr || app->activity == nullptr || app->activity->vm == nullptr) {
    FetchResult result;
    result.fError = "Android activity is unavailable";
    return result;
  }
  detail::Environment environment(app->activity->vm);
  if (environment.get() == nullptr) {
    FetchResult result;
    result.fError = "unable to attach HTTP worker to Android runtime";
    return result;
  }
  return detail::fetchImpl(environment.get(), url, progress);
}

} // namespace platform::android::http_transport
