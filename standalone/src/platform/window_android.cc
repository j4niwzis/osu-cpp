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
  // Called by the render thread at the top of every frame, which is what
  // makes it the place to attach to and detach from the native window: EGL
  // requires the surface to be released on the thread the context is current
  // on, and this is that thread.
  [[nodiscard]] bool windowMayRender() {
    const bool attached = this->syncSurface();
    return attached && fActive.load(std::memory_order_acquire);
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

  // Which way up the client wants to draw: 0 whatever the window is, 1
  // landscape, 2 portrait.
  //
  // The system is asked first, because a system that turns its screen leaves
  // nothing to turn here. What it does with the request is its own business
  // -- a display set to turn only when a person turns it ignores it -- so
  // the window that arrives is measured afterwards, and a window the wrong
  // way up is answered with a buffer whose dimensions are swapped and a
  // quarter turn during composition, which costs no second render target
  // and no work per frame.
  void requestDrawOrientation(int kind) {
    if (fWanted.exchange(kind, std::memory_order_acq_rel) == kind) {
      return;
    }
    int orientation = android::kOrientationUnspecified;
    if (kind == 1) {
      orientation = android::kOrientationSensorLandscape;
    } else if (kind == 2) {
      orientation = android::kOrientationSensorPortrait;
    }
    // Nothing is asked for "whatever the window is": what the manifest asked
    // for is what that means, and withdrawing it would turn a telephone's
    // window portrait for no reason.
    if (kind != 0) {
      (void)android::requestOrientation(orientation);
    }
    const std::lock_guard<std::mutex> lock(fSurfaceMutex);
    fGeometryStale = true;
  }

  // Nothing for the client to turn: where this window is the wrong way up,
  // the buffer is allocated the other way round and composed with a quarter
  // turn, which the client never sees and never pays for.
  [[nodiscard]] int drawTurn() const { return 0; }

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
    const std::lock_guard<std::mutex> lock(fSurfaceMutex);
    if (fDisplay != EGL_NO_DISPLAY && fSurface != EGL_NO_SURFACE &&
        fContext != EGL_NO_CONTEXT) {
      eglMakeCurrent(fDisplay, fSurface, fSurface, fContext);
    }
  }
  void releaseContext() {
    const std::lock_guard<std::mutex> lock(fSurfaceMutex);
    if (fDisplay != EGL_NO_DISPLAY) {
      eglMakeCurrent(fDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    }
  }
  void framebufferSize(int &width, int &height) const {
    const std::lock_guard<std::mutex> lock(fSurfaceMutex);
    if (!this->querySizeLocked(width, height)) {
      width = fInitial.fWidth;
      height = fInitial.fHeight;
    }
  }
  [[nodiscard]] bool surfaceSize(int &width, int &height) const {
    const std::lock_guard<std::mutex> lock(fSurfaceMutex);
    return this->querySizeLocked(width, height);
  }
  void setSwapInterval(int interval) {
    const std::lock_guard<std::mutex> lock(fSurfaceMutex);
    fSwapInterval = interval;
    if (fDisplay != EGL_NO_DISPLAY) {
      eglSwapInterval(fDisplay, interval);
    }
  }
  // The same question as on the desktop, answered by EGL because EGL is what
  // made this context.
  [[nodiscard]] void (*procAddress(const char *name) const)() {
    return eglGetProcAddress(name);
  }

  void swapBuffers() {
    const std::lock_guard<std::mutex> lock(fSurfaceMutex);
    if (fDisplay != EGL_NO_DISPLAY && fSurface != EGL_NO_SURFACE) {
      eglSwapBuffers(fDisplay, fSurface);
    }
  }
  [[nodiscard]] bool swapWithDamage(
      int, std::span<const std::array<int, 4>>) {
    return false;
  }
  void pollEvents() {
    this->pollOnce(0);
    this->drainPendingResize();
  }
  void cursorPosition(double &x, double &y) const {
    x = fCursorX.load(std::memory_order_acquire);
    y = fCursorY.load(std::memory_order_acquire);
  }
  void setCursorMode(input::CursorMode) {}

  void pumpEvents() {
    while (!this->quitting()) {
      this->pollOnce(-1);
      // The render thread wakes the looper after it attaches to a new
      // surface; the resize is turned into an event here so that the input
      // queue keeps its single producer.
      this->drainPendingResize();
    }
    fQuit.store(true, std::memory_order_release);
  }

  void close() {
    {
      const std::lock_guard<std::mutex> lock(fSurfaceMutex);
      fTargetWindow = nullptr;
      this->releaseSurfaceLocked(/*rebindContext=*/false);
      if (fDisplay != EGL_NO_DISPLAY && fContext != EGL_NO_CONTEXT) {
        eglDestroyContext(fDisplay, fContext);
      }
      if (fDisplay != EGL_NO_DISPLAY) {
        eglTerminate(fDisplay);
      }
      fContext = EGL_NO_CONTEXT;
      fDisplay = EGL_NO_DISPLAY;
    }
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
    EGLint count = 0;
    if (eglChooseConfig(fDisplay, attributes, &fConfig, 1, &count) != EGL_TRUE ||
        count == 0) {
      return false;
    }
    eglGetConfigAttrib(fDisplay, fConfig, EGL_NATIVE_VISUAL_ID, &fFormat);
    const std::lock_guard<std::mutex> lock(fSurfaceMutex);
    if (!this->createSurfaceLocked(window)) {
      return false;
    }
    // The surface outlives this call; the window it was made for is what the
    // render thread compares against every frame.
    fTargetWindow = window;
    fBoundWindow = window;
    int width = 0;
    int height = 0;
    if (this->querySizeLocked(width, height)) {
      fAttachedSize = packSize(width, height);
    }
    const EGLint contextAttributes[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    fContext = eglCreateContext(fDisplay, fConfig, EGL_NO_CONTEXT,
                                contextAttributes);
    return fContext != EGL_NO_CONTEXT;
  }

  // The buffer, named rather than left to the window's own default.
  //
  // Zero for both dimensions means "whatever the window's default is", and
  // there is at least one system where that default is not the window:
  // waydroid gave a buffer a quarter of the window's area and composed it at
  // the top left, unscaled, for as long as the geometry was left at zero.
  // Asking for the size the window reports is what filled it before, and on
  // a device whose default already is its own size this asks for exactly
  // what it would have been given.
  //
  // Where the system will not give this activity the landscape window the
  // manifest asks for, the window arrives portrait: then the buffer is
  // allocated with its dimensions swapped and handed to SurfaceFlinger with
  // a quarter turn, which rotates during composition and costs the renderer
  // no second render target.
  //
  // Naming a size pins it -- ANativeWindow_getWidth and eglQuerySurface
  // afterwards answer with what was asked for -- so a resize is heard as an
  // event and answered by asking again, from the render thread, between
  // frames: that is what the zero at the top of this function is for. No
  // buffer is dequeued while the two requests are in flight.
  [[nodiscard]] bool applyGeometryLocked(ANativeWindow *window) {
    // Zero restores the window's own dimensions, which is the only way to
    // read them back after this process has requested its own.
    if (ANativeWindow_setBuffersGeometry(window, 0, 0, 0) != 0) {
      return false;
    }
    const int windowWidth = ANativeWindow_getWidth(window);
    const int windowHeight = ANativeWindow_getHeight(window);
    if (windowWidth <= 0 || windowHeight <= 0) {
      return false;
    }
    // Turned only when the window is the other way up.
    //
    // Which is also all this can know. Where a system answers the
    // manifest's landscape by letterboxing rather than by turning its
    // screen, the window is landscape and smaller than the display -- and
    // the activity is told the display is the letterbox: waydroid, whose
    // screen is 1080x2340, answered "how large is the display" with
    // 1080x534 through android.view.Display, getMaximumWindowMetrics and
    // the system's own resources alike. Nothing here can see it, so nothing
    // here tries to; it is a property of the display, and on waydroid it is
    // turned off with "wm fixed-to-user-rotation disabled".
    //
    // It was turned every time, and on a device that had already given this
    // activity a landscape window the quarter turn was the second one: a
    // buffer allocated with the dimensions swapped and then composed into a
    // window they were not swapped for is the picture squashed along one
    // axis.
    // Turned when the window is not the way up the client draws. Asked for
    // "whatever the window is", nothing is ever turned.
    const int wanted = fWanted.load(std::memory_order_acquire);
    const bool portraitWindow = windowWidth < windowHeight;
    bool turned = false;
    if (wanted == 1) {
      turned = portraitWindow;
    } else if (wanted == 2) {
      turned = !portraitWindow;
    }
    fTurned.store(turned, std::memory_order_release);
    const int bufferWidth = turned ? windowHeight : windowWidth;
    const int bufferHeight = turned ? windowWidth : windowHeight;
    // Through the log rather than through stderr, which on Android goes
    // nowhere.
    this->log(std::format("[gfx] window {}x{}, buffer {}x{}{}", windowWidth,
                          windowHeight, bufferWidth, bufferHeight,
                          turned ? ", turned a quarter" : ""));
    return ANativeWindow_setBuffersTransform(
               window, turned ? ANATIVEWINDOW_TRANSFORM_ROTATE_90
                              : ANATIVEWINDOW_TRANSFORM_IDENTITY) == 0 &&
           ANativeWindow_setBuffersGeometry(window, bufferWidth, bufferHeight,
                                            fFormat) == 0;
  }

  [[nodiscard]] bool createSurfaceLocked(ANativeWindow *window) {
    if (!this->applyGeometryLocked(window)) {
      return false;
    }
    fSurface = eglCreateWindowSurface(fDisplay, fConfig, window, nullptr);
    return fSurface != EGL_NO_SURFACE;
  }

  // The size the renderer will actually get, handed to the main thread rather
  // than pushed from here: the event queue has one producer and it is that
  // thread.
  void reportSizeLocked() {
    int width = 0;
    int height = 0;
    if (!this->querySizeLocked(width, height)) {
      return;
    }
    const std::uint64_t packed = packSize(width, height);
    if (packed == fAttachedSize) {
      return;
    }
    fAttachedSize = packed;
    this->log(std::format("[gfx] surface {}x{}", width, height));
    fPendingResize.store(packed, std::memory_order_release);
    if (fApp != nullptr && fApp->looper != nullptr) {
      ALooper_wake(fApp->looper);
    }
  }

  // The surface has to be unbound before it can be destroyed, and only the
  // thread it is current on may unbind it -- which is why this is reached
  // from the render thread and not from the command handler.
  //
  // rebindContext asks for the context to stay current without a surface,
  // which the render thread wants: the frame loop keeps running while the
  // window is gone, and the work it does before it looks at the window --
  // uploading a decoded background, say -- is GL work that would otherwise
  // have no context to run in. Nothing is drawn there; windowMayRender is
  // false until a window comes back. A driver without surfaceless contexts
  // refuses that call and leaves the context unbound, which is the behaviour
  // this had before.
  void releaseSurfaceLocked(bool rebindContext) {
    if (fDisplay != EGL_NO_DISPLAY) {
      eglMakeCurrent(fDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
      if (fSurface != EGL_NO_SURFACE) {
        eglDestroySurface(fDisplay, fSurface);
      }
      if (rebindContext && fContext != EGL_NO_CONTEXT) {
        (void)eglMakeCurrent(fDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE,
                             fContext);
      }
    }
    fSurface = EGL_NO_SURFACE;
    fBoundWindow = nullptr;
    fSurfaceCv.notify_all();
  }

  // Render thread. Returns whether there is a surface to draw into.
  [[nodiscard]] bool syncSurface() {
    const std::lock_guard<std::mutex> lock(fSurfaceMutex);
    if (fTargetWindow != fBoundWindow) {
      this->releaseSurfaceLocked(/*rebindContext=*/true);
      // Claimed whether or not the attachment works, so a window that cannot
      // be attached to is not retried on every frame.
      fBoundWindow = fTargetWindow;
      fGeometryStale = false;
      if (fBoundWindow != nullptr && !this->attachLocked()) {
        this->log("unable to attach the renderer to the native window");
      }
    } else if (fGeometryStale) {
      fGeometryStale = false;
      if (fBoundWindow != nullptr && fSurface != EGL_NO_SURFACE) {
        if (this->applyGeometryLocked(fBoundWindow)) {
          this->reportSizeLocked();
        }
        fRefreshRequested.store(true, std::memory_order_release);
      }
    }
    return fSurface != EGL_NO_SURFACE;
  }

  [[nodiscard]] bool attachLocked() {
    if (this->createSurfaceLocked(fBoundWindow) && fContext != EGL_NO_CONTEXT &&
        eglMakeCurrent(fDisplay, fSurface, fSurface, fContext) == EGL_TRUE) {
      // The swap interval is a property of the surface rather than of the
      // context, so a new surface starts at the EGL default.
      eglSwapInterval(fDisplay, fSwapInterval);
      this->reportSizeLocked();
      fRefreshRequested.store(true, std::memory_order_release);
      return true;
    }
    // A surface the context could not be made current on is not a surface
    // anything may draw into, so it does not survive the failure.
    if (fSurface != EGL_NO_SURFACE) {
      eglDestroySurface(fDisplay, fSurface);
      fSurface = EGL_NO_SURFACE;
    }
    return false;
  }

  // Main thread. The window the activity has just been given; the renderer
  // picks it up on its next frame.
  void adoptWindow(ANativeWindow *window) {
    if (window == nullptr) {
      return;
    }
    const std::lock_guard<std::mutex> lock(fSurfaceMutex);
    fTargetWindow = window;
  }

  // Main thread. native_app_glue holds the window alive until this returns,
  // so the wait is what keeps the renderer from drawing into a window Android
  // is about to take away. The timeout bounds a render thread that is busy
  // elsewhere: the window goes regardless, and the surface is dropped on the
  // next frame instead.
  void dropWindow() {
    std::unique_lock<std::mutex> lock(fSurfaceMutex);
    fTargetWindow = nullptr;
    if (fBoundWindow == nullptr) {
      return;
    }
    if (!fSurfaceCv.wait_for(lock, std::chrono::milliseconds(500),
                             [this] { return fBoundWindow == nullptr; })) {
      this->log("the render thread did not release the window in time");
    }
  }

  [[nodiscard]] bool querySizeLocked(int &width, int &height) const {
    if (fDisplay == EGL_NO_DISPLAY || fSurface == EGL_NO_SURFACE) {
      return false;
    }
    EGLint queriedWidth = 0;
    EGLint queriedHeight = 0;
    if (eglQuerySurface(fDisplay, fSurface, EGL_WIDTH, &queriedWidth) !=
            EGL_TRUE ||
        eglQuerySurface(fDisplay, fSurface, EGL_HEIGHT, &queriedHeight) !=
            EGL_TRUE ||
        queriedWidth <= 0 || queriedHeight <= 0) {
      return false;
    }
    width = queriedWidth;
    height = queriedHeight;
    return true;
  }

  void drainPendingResize() {
    const std::uint64_t packed =
        fPendingResize.exchange(0, std::memory_order_acq_rel);
    if (packed == 0) {
      return;
    }
    this->report(static_cast<int>(packed >> 32),
                 static_cast<int>(packed & 0xffffffffu));
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
    // The window can be given its final size without a resize command --
    // this is the one that arrives when a desktop window manager decides how
    // large the activity is -- and a pinned buffer does not notice on its
    // own.
    case APP_CMD_CONTENT_RECT_CHANGED:
      self->noteGeometryStale();
      break;
    case APP_CMD_INIT_WINDOW:
      self->adoptWindow(app->window);
      break;
    case APP_CMD_TERM_WINDOW:
      self->dropWindow();
      break;
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

  // A touch is reported in the window's coordinates, and what the game is
  // drawn in is the buffer's. They are the same thing when the buffer was
  // not turned, and a quarter turn apart when it was -- so this is the same
  // decision as the one above, read from where it was made rather than
  // assumed a second time.
  void moveTouch(AInputEvent *event, std::size_t index) {
    const bool turned = fTurned.load(std::memory_order_acquire);
    const float x = turned ? AMotionEvent_getY(event, index)
                           : AMotionEvent_getX(event, index);
    const float y = turned ? static_cast<float>(fTouchSurfaceHeight) -
                                 AMotionEvent_getX(event, index)
                           : AMotionEvent_getY(event, index);
    fCursorX.store(x, std::memory_order_release);
    fCursorY.store(y, std::memory_order_release);
    this->push({wallMs(), EventType::kCursorMove, 0, 0, x, y});
  }

  bool key(AInputEvent *event) {
    const std::int32_t action = AKeyEvent_getAction(event);
    if (action != AKEY_EVENT_ACTION_DOWN && action != AKEY_EVENT_ACTION_UP) {
      return false;
    }
    const std::int32_t keyCode = AKeyEvent_getKeyCode(event);
    const std::int32_t character =
        action == AKEY_EVENT_ACTION_DOWN
            ? characterForKey(keyCode, AKeyEvent_getMetaState(event))
            : 0;
    if (character != 0) {
      this->push({wallMs(), EventType::kChar, character});
    }
    const int mapped = mapKey(keyCode);
    if (mapped == 0) {
      return character != 0;
    }
    this->push({wallMs(), EventType::kKey, mapped,
                action == AKEY_EVENT_ACTION_DOWN ? input::kPress
                                                  : input::kRelease});
    return true;
  }

  [[nodiscard]] static std::int32_t characterForKey(std::int32_t key,
                                                     std::int32_t meta) {
    const bool shift = (meta & AMETA_SHIFT_ON) != 0;
    if (key >= AKEYCODE_A && key <= AKEYCODE_Z) {
      return (shift ? 'A' : 'a') + (key - AKEYCODE_A);
    }
    if (key >= AKEYCODE_0 && key <= AKEYCODE_9) {
      static constexpr std::string_view shifted = ")!@#$%^&*(";
      const std::size_t index = static_cast<std::size_t>(key - AKEYCODE_0);
      return shift ? shifted[index] : '0' + static_cast<std::int32_t>(index);
    }
    switch (key) {
    case AKEYCODE_SPACE: return ' ';
    case AKEYCODE_COMMA: return shift ? '<' : ',';
    case AKEYCODE_PERIOD: return shift ? '>' : '.';
    case AKEYCODE_MINUS: return shift ? '_' : '-';
    case AKEYCODE_EQUALS: return shift ? '+' : '=';
    case AKEYCODE_LEFT_BRACKET: return shift ? '{' : '[';
    case AKEYCODE_RIGHT_BRACKET: return shift ? '}' : ']';
    case AKEYCODE_BACKSLASH: return shift ? '|' : '\\';
    case AKEYCODE_SEMICOLON: return shift ? ':' : ';';
    case AKEYCODE_APOSTROPHE: return shift ? '"' : '\'';
    case AKEYCODE_SLASH: return shift ? '?' : '/';
    case AKEYCODE_GRAVE: return shift ? '~' : '`';
    default: return 0;
    }
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

  // Main thread. What the window is now is only readable by asking it again,
  // which the render thread does on its next frame.
  void noteGeometryStale() {
    const std::lock_guard<std::mutex> lock(fSurfaceMutex);
    fGeometryStale = true;
  }

  void report(int width, int height) {
    if (width <= 0 || height <= 0) {
      return;
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
  EGLConfig fConfig = nullptr;
  EGLint fFormat = 0;
  EGLint fSwapInterval = 1;
  // fSurfaceMutex guards the EGL handles and both window pointers.
  // fTargetWindow is the window the activity currently has, fBoundWindow the
  // one the render thread has built a surface for; the condition variable is
  // how the command handler hears that the two agree again.
  mutable std::mutex fSurfaceMutex;
  std::condition_variable fSurfaceCv;
  ANativeWindow *fTargetWindow = nullptr;
  ANativeWindow *fBoundWindow = nullptr;
  bool fGeometryStale = false;
  std::uint64_t fAttachedSize = 0;
  std::atomic<std::uint64_t> fPendingResize{0};
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
  // Whether the buffer is being turned a quarter, decided when its geometry
  // is applied on the render thread and read when a touch arrives on
  // another. False until something asks to draw the way the window is not.
  std::atomic<bool> fTurned{false};
  // What the client asked to draw in, which only it knows: 0 whatever the
  // window is, 1 landscape, 2 portrait.
  std::atomic<int> fWanted{0};
  std::int32_t fTouchId = -1;
};

} // namespace platform
