export module platform.window;

import std;
import platform.android.api;
import platform.android_runtime;
import platform.clock;
import platform.input;
import client.input;

export namespace platform {

using client::Event;
using client::EventType;
using client::SpscQueue;

struct WindowExtent {
  int fWidth = 0;
  int fHeight = 0;
};

class WindowRuntime {
public:
  WindowRuntime() = default;
  WindowRuntime(const WindowRuntime &) = delete;
  WindowRuntime &operator=(const WindowRuntime &) = delete;
  ~WindowRuntime() { this->close(); }

  [[nodiscard]] bool open(std::function<void()>) {
    fApp = android::application();
    if (fApp == nullptr) {
      this->log("NativeActivity state is unavailable");
      return false;
    }
    fApp->userData = this;
    fApp->onAppCmd = &WindowRuntime::onCommand;
    fApp->onInputEvent = &WindowRuntime::onInput;

    while (fApp->window == nullptr && !fApp->destroyRequested) {
      this->pollOnce(-1);
    }
    if (fApp->window == nullptr || !this->createEgl(fApp->window)) {
      this->log("unable to create an EGL window");
      this->close();
      return false;
    }
    if (!this->surfaceSize(fInitial.fWidth, fInitial.fHeight)) {
      this->close();
      return false;
    }
    fTouchSurfaceHeight = fInitial.fHeight;
    fReportedSize.store(packSize(fInitial.fWidth, fInitial.fHeight),
                        std::memory_order_release);
    fActive.store(true, std::memory_order_release);
    return fInitial.fWidth > 0 && fInitial.fHeight > 0;
  }

  [[nodiscard]] WindowExtent initialExtent() const { return fInitial; }
  [[nodiscard]] int refreshHz() const { return 60; }
  [[nodiscard]] bool windowMayRender() const {
    return fActive.load(std::memory_order_acquire) && fSurface != EGL_NO_SURFACE;
  }

  [[nodiscard]] bool pop(Event &event) { return fInput.tryPop(event); }
  void push(const Event &event) { (void)fInput.tryPush(event); }
  [[nodiscard]] std::size_t droppedInput() const { return fInput.dropped(); }
  [[nodiscard]] std::vector<std::string> takeDroppedFiles() { return {}; }

  [[nodiscard]] std::uint64_t reportedSize() const {
    return fReportedSize.load(std::memory_order_acquire);
  }
  [[nodiscard]] bool takeRefreshRequest() {
    return fRefreshRequested.exchange(false, std::memory_order_acquire);
  }
  [[nodiscard]] std::optional<std::array<int, 4>>
  visiblePortion(int, int) const {
    return std::nullopt;
  }

  void requestCursorMode(int) {}
  void requestRawMotion(bool) {}
  void requestFullscreen(bool) {}

  void requestQuit() {
    fQuit.store(true, std::memory_order_release);
    if (fApp != nullptr && fApp->looper != nullptr) {
      ALooper_wake(fApp->looper);
    }
  }
  [[nodiscard]] bool quitting() const {
    return fQuit.load(std::memory_order_acquire) ||
           (fApp != nullptr && fApp->destroyRequested != 0);
  }
  void setExitCode(int code) {
    fExitCode.store(code, std::memory_order_release);
  }
  [[nodiscard]] int exitCode() const {
    return fExitCode.load(std::memory_order_acquire);
  }

  void makeContextCurrent() {
    if (fDisplay != EGL_NO_DISPLAY && fSurface != EGL_NO_SURFACE &&
        fContext != EGL_NO_CONTEXT) {
      eglMakeCurrent(fDisplay, fSurface, fSurface, fContext);
    }
  }
  void releaseContext() {
    if (fDisplay != EGL_NO_DISPLAY) {
      eglMakeCurrent(fDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    }
  }
  void framebufferSize(int &width, int &height) const {
    width = fInitial.fWidth;
    height = fInitial.fHeight;
    if (fDisplay != EGL_NO_DISPLAY && fSurface != EGL_NO_SURFACE) {
      eglQuerySurface(fDisplay, fSurface, EGL_WIDTH, &width);
      eglQuerySurface(fDisplay, fSurface, EGL_HEIGHT, &height);
    }
  }
  [[nodiscard]] bool surfaceSize(int &width, int &height) const {
    if (fDisplay == EGL_NO_DISPLAY || fSurface == EGL_NO_SURFACE) {
      return false;
    }
    return eglQuerySurface(fDisplay, fSurface, EGL_WIDTH, &width) == EGL_TRUE &&
           eglQuerySurface(fDisplay, fSurface, EGL_HEIGHT, &height) == EGL_TRUE;
  }
  void setSwapInterval(int interval) {
    if (fDisplay != EGL_NO_DISPLAY) {
      eglSwapInterval(fDisplay, interval);
    }
  }
  void swapBuffers() {
    if (fDisplay != EGL_NO_DISPLAY && fSurface != EGL_NO_SURFACE) {
      eglSwapBuffers(fDisplay, fSurface);
    }
  }
  [[nodiscard]] bool swapWithDamage(
      int, std::span<const std::array<int, 4>>) {
    return false;
  }
  void pollEvents() { this->pollOnce(0); }
  void cursorPosition(double &x, double &y) const {
    x = fCursorX.load(std::memory_order_acquire);
    y = fCursorY.load(std::memory_order_acquire);
  }
  void setCursorMode(input::CursorMode) {}

  void pumpEvents() {
    while (!this->quitting()) {
      this->pollOnce(-1);
    }
    fQuit.store(true, std::memory_order_release);
  }

  void close() {
    this->releaseContext();
    if (fDisplay != EGL_NO_DISPLAY && fContext != EGL_NO_CONTEXT) {
      eglDestroyContext(fDisplay, fContext);
    }
    if (fDisplay != EGL_NO_DISPLAY && fSurface != EGL_NO_SURFACE) {
      eglDestroySurface(fDisplay, fSurface);
    }
    if (fDisplay != EGL_NO_DISPLAY) {
      eglTerminate(fDisplay);
    }
    fContext = EGL_NO_CONTEXT;
    fSurface = EGL_NO_SURFACE;
    fDisplay = EGL_NO_DISPLAY;
    if (fApp != nullptr && fApp->userData == this) {
      fApp->userData = nullptr;
    }
  }

private:
  [[nodiscard]] static double wallMs() {
    return platform::clock::milliseconds();
  }
  [[nodiscard]] static std::uint64_t packSize(int width, int height) {
    return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(width))
            << 32) |
           static_cast<std::uint32_t>(height);
  }
  void log(std::string_view message) const {
    const std::string owned(message);
    __android_log_write(ANDROID_LOG_ERROR, "osu!cpp", owned.c_str());
  }

  [[nodiscard]] bool createEgl(ANativeWindow *window) {
    const int windowWidth = ANativeWindow_getWidth(window);
    const int windowHeight = ANativeWindow_getHeight(window);
    if (windowWidth <= 0 || windowHeight <= 0 ||
        ANativeWindow_setBuffersTransform(
            window, ANATIVEWINDOW_TRANSFORM_ROTATE_90) != 0) {
      return false;
    }
    fDisplay = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (fDisplay == EGL_NO_DISPLAY || eglInitialize(fDisplay, nullptr, nullptr) != EGL_TRUE) {
      return false;
    }
    const EGLint attributes[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8, EGL_DEPTH_SIZE, 0, EGL_STENCIL_SIZE, 8,
        EGL_NONE};
    EGLConfig config = nullptr;
    EGLint count = 0;
    if (eglChooseConfig(fDisplay, attributes, &config, 1, &count) != EGL_TRUE ||
        count == 0) {
      return false;
    }
    EGLint format = 0;
    eglGetConfigAttrib(fDisplay, config, EGL_NATIVE_VISUAL_ID, &format);
    if (ANativeWindow_setBuffersGeometry(window, windowHeight, windowWidth,
                                         format) != 0) {
      return false;
    }
    fSurface = eglCreateWindowSurface(fDisplay, config, window, nullptr);
    const EGLint contextAttributes[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    fContext = eglCreateContext(fDisplay, config, EGL_NO_CONTEXT,
                                contextAttributes);
    return fSurface != EGL_NO_SURFACE && fContext != EGL_NO_CONTEXT;
  }

  void pollOnce(int timeout) {
    int events = 0;
    void *data = nullptr;
    const int result = ALooper_pollOnce(timeout, nullptr, &events, &data);
    auto *source = static_cast<android_poll_source *>(data);
    if (result >= 0 && source != nullptr) {
      source->process(fApp, source);
    }
  }

  static void onCommand(android_app *app, std::int32_t command) {
    auto *self = static_cast<WindowRuntime *>(app->userData);
    if (self == nullptr) {
      return;
    }
    switch (command) {
    case APP_CMD_GAINED_FOCUS:
      (void)android::enterImmersiveMode();
      [[fallthrough]];
    case APP_CMD_RESUME:
      self->setActive(true);
      break;
    case APP_CMD_LOST_FOCUS:
    case APP_CMD_PAUSE:
      self->setActive(false);
      break;
    case APP_CMD_WINDOW_RESIZED:
    case APP_CMD_CONFIG_CHANGED:
      self->noteSize();
      break;
    case APP_CMD_TERM_WINDOW:
    case APP_CMD_DESTROY:
      self->requestQuit();
      break;
    default:
      break;
    }
  }

  static std::int32_t onInput(android_app *app, AInputEvent *event) {
    auto *self = static_cast<WindowRuntime *>(app->userData);
    if (self == nullptr) {
      return 0;
    }
    if (AInputEvent_getType(event) == AINPUT_EVENT_TYPE_MOTION) {
      return self->motion(event) ? 1 : 0;
    }
    if (AInputEvent_getType(event) == AINPUT_EVENT_TYPE_KEY) {
      return self->key(event) ? 1 : 0;
    }
    return 0;
  }

  bool motion(AInputEvent *event) {
    const std::int32_t action = AMotionEvent_getAction(event);
    const std::int32_t masked = action & AMOTION_EVENT_ACTION_MASK;
    const std::size_t index = static_cast<std::size_t>(
        (action & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK) >>
        AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT);
    if ((masked == AMOTION_EVENT_ACTION_DOWN ||
         masked == AMOTION_EVENT_ACTION_POINTER_DOWN) && fTouchId < 0) {
      fTouchId = AMotionEvent_getPointerId(event, index);
      this->moveTouch(event, index);
      this->push({wallMs(), EventType::kMouseButton,
                  input::kMouseButtonLeft, input::kPress});
      return true;
    }
    if (masked == AMOTION_EVENT_ACTION_MOVE && fTouchId >= 0) {
      const std::size_t count = AMotionEvent_getPointerCount(event);
      for (std::size_t i = 0; i < count; ++i) {
        if (AMotionEvent_getPointerId(event, i) == fTouchId) {
          this->moveTouch(event, i);
          return true;
        }
      }
    }
    if ((masked == AMOTION_EVENT_ACTION_UP ||
         masked == AMOTION_EVENT_ACTION_POINTER_UP) &&
        AMotionEvent_getPointerId(event, index) == fTouchId) {
      this->moveTouch(event, index);
      this->push({wallMs(), EventType::kMouseButton,
                  input::kMouseButtonLeft, input::kRelease});
      fTouchId = -1;
      return true;
    }
    if (masked == AMOTION_EVENT_ACTION_CANCEL && fTouchId >= 0) {
      this->push({wallMs(), EventType::kMouseButton,
                  input::kMouseButtonLeft, input::kRelease});
      fTouchId = -1;
      return true;
    }
    return fTouchId >= 0;
  }

  void moveTouch(AInputEvent *event, std::size_t index) {
    const float x = AMotionEvent_getY(event, index);
    const float y = static_cast<float>(fTouchSurfaceHeight) -
                    AMotionEvent_getX(event, index);
    fCursorX.store(x, std::memory_order_release);
    fCursorY.store(y, std::memory_order_release);
    this->push({wallMs(), EventType::kCursorMove, 0, 0, x, y});
  }

  bool key(AInputEvent *event) {
    const std::int32_t action = AKeyEvent_getAction(event);
    if (action != AKEY_EVENT_ACTION_DOWN && action != AKEY_EVENT_ACTION_UP) {
      return false;
    }
    const int mapped = mapKey(AKeyEvent_getKeyCode(event));
    if (mapped == 0) {
      return false;
    }
    this->push({wallMs(), EventType::kKey, mapped,
                action == AKEY_EVENT_ACTION_DOWN ? input::kPress
                                                  : input::kRelease});
    return true;
  }

  [[nodiscard]] static int mapKey(std::int32_t key) {
    switch (key) {
    case AKEYCODE_BACK: return input::kKeyEscape;
    case AKEYCODE_ENTER: return input::kKeyEnter;
    case AKEYCODE_DEL: return input::kKeyBackspace;
    case AKEYCODE_DPAD_UP: return input::kKeyUp;
    case AKEYCODE_DPAD_DOWN: return input::kKeyDown;
    case AKEYCODE_DPAD_LEFT: return input::kKeyLeft;
    case AKEYCODE_DPAD_RIGHT: return input::kKeyRight;
    case AKEYCODE_SPACE: return input::kKeySpace;
    default: return 0;
    }
  }

  void setActive(bool active) {
    if (fActive.exchange(active, std::memory_order_acq_rel) == active) {
      return;
    }
    this->push({wallMs(), EventType::kWindowVisible, active ? 1 : 0});
  }

  void noteSize() {
    if (fApp == nullptr || fApp->window == nullptr) {
      return;
    }
    const int width = ANativeWindow_getWidth(fApp->window);
    const int height = ANativeWindow_getHeight(fApp->window);
    if (width <= 0 || height <= 0) {
      return;
    }
    if (fDisplay != EGL_NO_DISPLAY && fSurface != EGL_NO_SURFACE) {
      eglQuerySurface(fDisplay, fSurface, EGL_WIDTH, &width);
      eglQuerySurface(fDisplay, fSurface, EGL_HEIGHT, &height);
    }
    fTouchSurfaceHeight = height;
    fInitial = {width, height};
    fReportedSize.store(packSize(width, height), std::memory_order_release);
    fRefreshRequested.store(true, std::memory_order_release);
    this->push({wallMs(), EventType::kResize, width, height});
  }

  android_app *fApp = nullptr;
  EGLDisplay fDisplay = EGL_NO_DISPLAY;
  EGLSurface fSurface = EGL_NO_SURFACE;
  EGLContext fContext = EGL_NO_CONTEXT;
  WindowExtent fInitial{};
  SpscQueue<4096> fInput;
  std::atomic<bool> fQuit{false};
  std::atomic<bool> fActive{false};
  std::atomic<int> fExitCode{0};
  std::atomic<std::uint64_t> fReportedSize{0};
  std::atomic<bool> fRefreshRequested{false};
  std::atomic<float> fCursorX{0.0f};
  std::atomic<float> fCursorY{0.0f};
  int fTouchSurfaceHeight = 1;
  std::int32_t fTouchId = -1;
};

} // namespace platform
