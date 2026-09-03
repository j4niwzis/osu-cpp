module;

#ifdef OSU_WAYLAND_TOUCH
#include <dlfcn.h>
#include <wayland-client.h>
#endif

export module platform.window;

import std;
import platform.glfw;
import platform.clock;
import platform.input;
import client.input;
import platform.presentation;
import platform.configuration;
import platform.web_runtime;

namespace platform {

using client::Event;
using client::EventType;

// GLFW deliberately exposes no touchscreen API.  On Wayland that means
// wl_touch events disappear inside its event pump instead of reaching the
// mouse callbacks below.  Bind the seat once more and translate the primary
// contact into the same events as a left mouse button.  The bridge is absent
// unless wayland-client was found at configure time, and init() is a no-op
// when GLFW selected another platform (notably X11/XWayland).
class WaylandTouch {
public:
  WaylandTouch() = default;
  WaylandTouch(const WaylandTouch &) = delete;
  WaylandTouch &operator=(const WaylandTouch &) = delete;
  ~WaylandTouch() { this->close(); }

  template <class Push>
  void init(GLFWwindow *window, Push push) {
#ifdef OSU_WAYLAND_TOUCH
    // Resolve these instead of linking them directly.  A GLFW built only for
    // X11 does not export its Wayland native-access entry points even when
    // wayland-client happens to be installed on the machine.
    using GetDisplay = wl_display *(*)();
    using GetWindow = wl_surface *(*)(GLFWwindow *);
    const auto getDisplay = reinterpret_cast<GetDisplay>(
        dlsym(RTLD_DEFAULT, "glfwGetWaylandDisplay"));
    const auto getWindow = reinterpret_cast<GetWindow>(
        dlsym(RTLD_DEFAULT, "glfwGetWaylandWindow"));
    if (getDisplay == nullptr || getWindow == nullptr) {
      return;
    }
    fDisplay = getDisplay();
    fSurface = getWindow(window);
    if (fDisplay == nullptr || fSurface == nullptr) {
      fDisplay = nullptr;
      fSurface = nullptr;
      return;
    }
    fPush = std::move(push);
    fRegistry = wl_display_get_registry(fDisplay);
    if (fRegistry == nullptr) {
      this->close();
      return;
    }
    wl_registry_add_listener(fRegistry, &kRegistryListener, this);
    // Discover the seat before entering glfwWaitEvents().  This round trip
    // uses GLFW's display and therefore also dispatches any harmless setup
    // events already queued for its own objects.
    if (wl_display_roundtrip(fDisplay) < 0) {
      this->close();
    }
#else
    (void)window;
    (void)push;
#endif
  }

  void close() {
#ifdef OSU_WAYLAND_TOUCH
    this->releaseContact();
    if (fTouch != nullptr) {
      wl_touch_destroy(fTouch);
      fTouch = nullptr;
    }
    if (fSeat != nullptr) {
      wl_seat_destroy(fSeat);
      fSeat = nullptr;
    }
    if (fRegistry != nullptr) {
      wl_registry_destroy(fRegistry);
      fRegistry = nullptr;
    }
    fDisplay = nullptr;
    fSurface = nullptr;
    fPush = {};
#endif
  }

  [[nodiscard]] bool nativeWayland() const {
#ifdef OSU_WAYLAND_TOUCH
    return fSurface != nullptr;
#else
    return false;
#endif
  }

private:
#ifdef OSU_WAYLAND_TOUCH
  static double wallMs() { return platform::clock::milliseconds(); }

  void move(wl_fixed_t x, wl_fixed_t y) {
    fX = static_cast<float>(wl_fixed_to_double(x));
    fY = static_cast<float>(wl_fixed_to_double(y));
    fPush({wallMs(), EventType::kCursorMove, 0, 0, fX, fY});
  }

  void releaseContact() {
    if (fContact < 0 || !fPush) {
      fContact = -1;
      return;
    }
    fPush({wallMs(), EventType::kMouseButton, platform::input::kMouseButtonLeft,
           platform::input::kRelease});
    fContact = -1;
  }

  static void registryGlobal(void *data, wl_registry *registry, uint32_t name,
                             const char *interface, uint32_t version) {
    auto &self = *static_cast<WaylandTouch *>(data);
    if (self.fSeat != nullptr ||
        std::string_view(interface) != wl_seat_interface.name) {
      return;
    }
    const uint32_t supported = std::min(version, 7u);
    self.fSeat = static_cast<wl_seat *>(
        wl_registry_bind(registry, name, &wl_seat_interface, supported));
    if (self.fSeat != nullptr) {
      wl_seat_add_listener(self.fSeat, &kSeatListener, &self);
    }
  }

  static void registryRemove(void *, wl_registry *, uint32_t) {}

  static void seatCapabilities(void *data, wl_seat *, uint32_t capabilities) {
    auto &self = *static_cast<WaylandTouch *>(data);
    if ((capabilities & WL_SEAT_CAPABILITY_TOUCH) != 0) {
      if (self.fTouch == nullptr) {
        self.fTouch = wl_seat_get_touch(self.fSeat);
        wl_touch_add_listener(self.fTouch, &kTouchListener, &self);
      }
    } else if (self.fTouch != nullptr) {
      self.releaseContact();
      wl_touch_destroy(self.fTouch);
      self.fTouch = nullptr;
    }
  }

  static void seatName(void *, wl_seat *, const char *) {}

  static void touchDown(void *data, wl_touch *, uint32_t, uint32_t,
                        wl_surface *surface, int32_t id, wl_fixed_t x,
                        wl_fixed_t y) {
    auto &self = *static_cast<WaylandTouch *>(data);
    if (self.fContact >= 0 || surface != self.fSurface) {
      return;
    }
    self.fContact = id;
    self.move(x, y);
    self.fPush({wallMs(), EventType::kMouseButton, platform::input::kMouseButtonLeft,
                platform::input::kPress});
  }

  static void touchUp(void *data, wl_touch *, uint32_t, uint32_t, int32_t id) {
    auto &self = *static_cast<WaylandTouch *>(data);
    if (id == self.fContact) {
      self.releaseContact();
    }
  }

  static void touchMotion(void *data, wl_touch *, uint32_t, int32_t id,
                          wl_fixed_t x, wl_fixed_t y) {
    auto &self = *static_cast<WaylandTouch *>(data);
    if (id == self.fContact) {
      self.move(x, y);
    }
  }

  static void touchFrame(void *, wl_touch *) {}
  static void touchCancel(void *data, wl_touch *) {
    static_cast<WaylandTouch *>(data)->releaseContact();
  }
  static void touchShape(void *, wl_touch *, int32_t, wl_fixed_t, wl_fixed_t) {}
  static void touchOrientation(void *, wl_touch *, int32_t, wl_fixed_t) {}

  inline static const wl_registry_listener kRegistryListener = {
      registryGlobal, registryRemove};
  inline static const wl_seat_listener kSeatListener = {seatCapabilities,
                                                         seatName};
  inline static const wl_touch_listener kTouchListener = {
      touchDown,   touchUp,    touchMotion, touchFrame,
      touchCancel, touchShape, touchOrientation};

  wl_display *fDisplay = nullptr;
  wl_registry *fRegistry = nullptr;
  wl_seat *fSeat = nullptr;
  wl_touch *fTouch = nullptr;
  wl_surface *fSurface = nullptr;
  std::function<void(const Event &)> fPush;
  int32_t fContact = -1;
  float fX = 0.0f;
  float fY = 0.0f;
#endif
};

} // namespace platform

export namespace platform {

using client::Event;
using client::EventType;
using client::SpscQueue;

struct WindowExtent {
  int fWidth = 0;
  int fHeight = 0;
};

// Owns GLFW and the boundary between its event thread and the render thread.
// App consumes semantic input and draws; it no longer maintains a second set
// of queues, atomics, drop storage, or window-placement snapshots.
class WindowRuntime {
public:
  WindowRuntime() = default;
  WindowRuntime(const WindowRuntime &) = delete;
  WindowRuntime &operator=(const WindowRuntime &) = delete;

  ~WindowRuntime() { this->close(); }

  [[nodiscard]] bool open(std::function<void()> toggleFullscreen) {
    const auto configuration = platform::runtimeConfiguration();
    glfwSetErrorCallback([](int code, const char *message) {
      std::println(std::cerr, "[glfw] error {}: {}", code,
                   message != nullptr ? message : "unknown error");
    });
    if (!glfwInit()) {
      std::println(std::cerr, "[glfw] initialization failed");
      return false;
    }
    fGlfwInitialized = true;
    fToggleFullscreen = std::move(toggleFullscreen);

#ifdef __EMSCRIPTEN__
    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_API);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
    glfwWindowHint(GLFW_SAMPLES, 0);
    const auto canvas = platform::web::canvasExtent();
    fInitial.fWidth = canvas.fWidth;
    fInitial.fHeight = canvas.fHeight;
    fWindow = glfwCreateWindow(fInitial.fWidth, fInitial.fHeight,
                                     "osu_client", nullptr, nullptr);
#else
    const auto monitor = glfwGetPrimaryMonitor();
    const GLFWvidmode *mode =
        monitor != nullptr ? glfwGetVideoMode(monitor) : nullptr;
    if (mode != nullptr) {
      fInitial = {mode->width, mode->height};
      if (mode->refreshRate > 0) {
        fRefreshHz = mode->refreshRate;
      }
    } else if (configuration.fPreferGles) {
      // MirClient Click windows are placed and resized by Lomiri rather than
      // against a GLFW monitor. This is only the size of the first buffer;
      // the compositor's resize event supplies the real screen dimensions.
      fInitial = {1920, 1080};
    } else {
      this->close();
      return false;
    }

    struct ContextChoice {
      const char *fName;
      int fApi;
      int fMajor;
      int fMinor;
      int fProfile;
      int fCreation;
    };
    const int desktopCreation = configuration.fForceEgl
                                    ? GLFW_EGL_CONTEXT_API
                                    : GLFW_NATIVE_CONTEXT_API;
    const ContextChoice choices[] = {
        {"gl 4.1 core", GLFW_OPENGL_API, 4, 1, GLFW_OPENGL_CORE_PROFILE,
         desktopCreation},
        {"gl 3.0", GLFW_OPENGL_API, 3, 0, GLFW_OPENGL_ANY_PROFILE,
         desktopCreation},
        {"gles 3.0", GLFW_OPENGL_ES_API, 3, 0, GLFW_OPENGL_ANY_PROFILE,
         GLFW_EGL_CONTEXT_API},
        {"gles 2.0", GLFW_OPENGL_ES_API, 2, 0, GLFW_OPENGL_ANY_PROFILE,
         GLFW_EGL_CONTEXT_API},
    };
    const bool preferGles = configuration.fPreferGles;
    const std::array<int, 4> order = preferGles ? std::array{2, 3, 0, 1}
                                                : std::array{0, 1, 2, 3};
    for (const int index : order) {
      const auto &choice = choices[index];
      glfwWindowHint(GLFW_CLIENT_API, choice.fApi);
      glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, choice.fMajor);
      glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, choice.fMinor);
      glfwWindowHint(GLFW_OPENGL_PROFILE, choice.fProfile);
      glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT,
                           choice.fApi == GLFW_OPENGL_API && choice.fMajor >= 3
                               ? GLFW_TRUE
                               : GLFW_FALSE);
      glfwWindowHint(GLFW_CONTEXT_CREATION_API, choice.fCreation);
      glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
      fWindow = glfwCreateWindow(fInitial.fWidth, fInitial.fHeight,
                                       "osu_client", monitor, nullptr);
      if (fWindow != nullptr) {
        std::println(std::cerr, "[gfx] context: {}", choice.fName);
        break;
      }
      std::println(std::cerr, "[gfx] context failed: {}", choice.fName);
    }
#endif
    if (fWindow == nullptr) {
      std::println(std::cerr, "[gfx] no usable graphics context");
      this->close();
      return false;
    }

    glfwSetInputMode(fWindow, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    glfwSetWindowUserPointer(fWindow, this);
    // Before any event arrives, because every cursor event reads it.
    this->measurePointerScale();
    this->installCallbacks();
    fWaylandTouch.init(fWindow, [this](const Event &event) {
      if (event.fType == EventType::kCursorMove) {
        this->pushCursor(event.fX, event.fY);
      } else {
        this->push(event);
      }
    });
    fSuspendWhenInactive =
        fWaylandTouch.nativeWayland() && this->postmarketOS();
    this->notePlacement();
    return true;
  }

  [[nodiscard]] WindowExtent initialExtent() const { return fInitial; }
  [[nodiscard]] int refreshHz() const { return fRefreshHz; }
  [[nodiscard]] bool windowMayRender() const {
    if (fWindowIconified.load(std::memory_order_acquire)) {
      return false;
    }
    return !fSuspendWhenInactive ||
           fWindowActive.load(std::memory_order_acquire);
  }

  [[nodiscard]] bool pop(Event &event) { return fInput.tryPop(event); }
  void push(const Event &event) { (void)fInput.tryPush(event); }
  [[nodiscard]] std::size_t droppedInput() const { return fInput.dropped(); }

  [[nodiscard]] std::vector<std::string> takeDroppedFiles() {
    const std::scoped_lock lock(fDropMutex);
    return std::exchange(fDroppedFiles, std::vector<std::string>{});
  }

  [[nodiscard]] std::uint64_t reportedSize() const {
    return fReportedSize.load(std::memory_order_acquire);
  }
  [[nodiscard]] bool takeRefreshRequest() {
    return fRefreshRequested.exchange(false, std::memory_order_acquire);
  }

  // Returns the visible framebuffer region in window-local coordinates.
  [[nodiscard]] std::optional<std::array<int, 4>>
  visiblePortion(int width, int height) const {
    const int x = fWindowX.load(std::memory_order_acquire);
    const int y = fWindowY.load(std::memory_order_acquire);
    const int areaX = fWorkAreaX.load(std::memory_order_acquire);
    const int areaY = fWorkAreaY.load(std::memory_order_acquire);
    const int areaW = fWorkAreaW.load(std::memory_order_acquire);
    const int areaH = fWorkAreaH.load(std::memory_order_acquire);
    if (areaW <= 0 || areaH <= 0 || width <= 0 || height <= 0) {
      return std::nullopt;
    }
    const int left = std::max(x, areaX);
    const int top = std::max(y, areaY);
    const int right = std::min(x + width, areaX + areaW);
    const int bottom = std::min(y + height, areaY + areaH);
    if (right <= left || bottom <= top) {
      return std::nullopt;
    }
    constexpr int pad = 4;
    return std::array{
        std::max(0, left - x - pad), std::max(0, top - y - pad),
        std::min(width, right - x + pad), std::min(height, bottom - y + pad)};
  }

  void requestCursorMode(int mode) {
    fCursorModeRequest.store(mode, std::memory_order_release);
    glfwPostEmptyEvent();
  }
  void requestRawMotion(bool enabled) {
    fRawMotionRequest.store(enabled ? GLFW_TRUE : GLFW_FALSE,
                            std::memory_order_release);
    glfwPostEmptyEvent();
  }

  // Moving a window between a monitor and the desktop is one of the calls
  // GLFW pins to the thread that owns the window, so the drawing thread asks
  // and the pump answers.
  void requestFullscreen(bool wanted) {
    fFullscreenRequest.store(wanted ? 1 : 0, std::memory_order_release);
    glfwPostEmptyEvent();
  }

  void requestQuit() {
    fQuit.store(true, std::memory_order_release);
    glfwPostEmptyEvent();
  }
  [[nodiscard]] bool quitting() const {
    return fQuit.load(std::memory_order_acquire) ||
           (fWindow != nullptr && glfwWindowShouldClose(fWindow));
  }
  void setExitCode(int code) {
    fExitCode.store(code, std::memory_order_release);
  }
  [[nodiscard]] int exitCode() const {
    return fExitCode.load(std::memory_order_acquire);
  }

  // Graphics and input operations stay behind this boundary so application
  // code never depends on a GLFW handle or its numeric cursor constants.
  void makeContextCurrent() { glfwMakeContextCurrent(fWindow); }
  void releaseContext() { glfwMakeContextCurrent(nullptr); }
  void framebufferSize(int &width, int &height) const {
    glfwGetFramebufferSize(fWindow, &width, &height);
  }
  [[nodiscard]] bool surfaceSize(int &width, int &height) const {
    return presentation::surfaceSize(fWindow, &width, &height);
  }
  // Where a GL entry point is, for whoever needs to assemble an interface of
  // their own. GLFW knows because GLFW made the context, and it answers the
  // same way whether that context came through EGL or through GLX.
  [[nodiscard]] void (*procAddress(const char *name) const)() {
    return glfwGetProcAddress(name);
  }
  void setSwapInterval(int interval) { glfwSwapInterval(interval); }
  void swapBuffers() { glfwSwapBuffers(fWindow); }
  [[nodiscard]] bool swapWithDamage(
      int height, std::span<const std::array<int, 4>> damage) {
    return presentation::swapWithDamage(height, damage);
  }
  void pollEvents() {
#ifdef __EMSCRIPTEN__
    // A browser window that changed size is not something GLFW hears about:
    // Emscripten's GLFW knows the sizes it was told and nothing else, so a
    // page resized by the user left the client drawing at the size it
    // started with, stretched by the browser to fill the element.
    //
    // The page says how large the canvas is shown; this says how many
    // pixels are behind it. Setting the window size is what makes
    // Emscripten resize the buffer and fire the framebuffer callback, which
    // is what remakes the surface.
    const auto canvas = platform::web::canvasExtent();
    if (fWindow != nullptr && canvas.fWidth > 0 && canvas.fHeight > 0) {
      int width = 0;
      int height = 0;
      glfwGetWindowSize(fWindow, &width, &height);
      if (width != canvas.fWidth || height != canvas.fHeight) {
        glfwSetWindowSize(fWindow, canvas.fWidth, canvas.fHeight);
      }
    }
#endif
    glfwPollEvents();
  }
  void cursorPosition(double &x, double &y) const {
    glfwGetCursorPos(fWindow, &x, &y);
  }
  void setCursorMode(input::CursorMode mode) {
    const int native = mode == input::CursorMode::kNormal
                           ? GLFW_CURSOR_NORMAL
                           : mode == input::CursorMode::kHidden
                                 ? GLFW_CURSOR_HIDDEN
                                 : GLFW_CURSOR_DISABLED;
#ifdef __EMSCRIPTEN__
    glfwSetInputMode(fWindow, GLFW_CURSOR, native);
#else
    this->requestCursorMode(native);
#endif
  }

#ifndef __EMSCRIPTEN__
  void pumpEvents() {
    while (!this->quitting()) {
      glfwWaitEvents();
      const int cursorMode = fCursorModeRequest.exchange(-1);
      if (cursorMode != -1) {
        glfwSetInputMode(fWindow, GLFW_CURSOR, cursorMode);
      }
      const int rawMotion = fRawMotionRequest.exchange(-1);
      if (rawMotion != -1 && glfwRawMouseMotionSupported()) {
        glfwSetInputMode(fWindow, GLFW_RAW_MOUSE_MOTION, rawMotion);
      }
      const int fullscreen = fFullscreenRequest.exchange(-1);
      if (fullscreen != -1) {
        this->applyFullscreen(fullscreen == 1);
      }
    }
    fQuit.store(true, std::memory_order_release);
  }
#else
  void pumpEvents() {}
#endif

#ifndef __EMSCRIPTEN__
  // Where the window was before it went fullscreen, so it can be put back.
  void applyFullscreen(bool wanted) {
    if (fWindow == nullptr || wanted == fFullscreen) {
      return;
    }
    if (wanted) {
      // On Wayland the window has no position to save, and it needs none:
      // leaving fullscreen hands the size back to the compositor, which put
      // the window where it wanted it in the first place.
      if (glfwWindowPositionIsKnowable()) {
        glfwGetWindowPos(fWindow, &fWindowedX, &fWindowedY);
      }
      glfwGetWindowSize(fWindow, &fWindowedW, &fWindowedH);
      const auto monitor = glfwGetPrimaryMonitor();
      const GLFWvidmode *mode =
          monitor != nullptr ? glfwGetVideoMode(monitor) : nullptr;
      if (mode == nullptr) {
        return;
      }
      glfwSetWindowMonitor(fWindow, monitor, 0, 0, mode->width,
                                 mode->height, mode->refreshRate);
    } else {
      glfwSetWindowMonitor(fWindow, nullptr, fWindowedX, fWindowedY,
                                 fWindowedW, fWindowedH, 0);
    }
    fFullscreen = wanted;
  }

#endif

  void close() {
    fWaylandTouch.close();
    if (fWindow != nullptr) {
      glfwDestroyWindow(fWindow);
      fWindow = nullptr;
    }
    if (fGlfwInitialized) {
      glfwTerminate();
      fGlfwInitialized = false;
    }
  }

private:
  [[nodiscard]] static bool postmarketOS() {
#if defined(__linux__) && !defined(__EMSCRIPTEN__)
    std::ifstream in("/etc/os-release");
    std::string line;
    while (std::getline(in, line)) {
      if (line == "ID=postmarketos" || line == "ID=\"postmarketos\"") {
        return true;
      }
    }
#endif
    return false;
  }

  [[nodiscard]] static double wallMs() {
    return platform::clock::milliseconds();
  }
  [[nodiscard]] static std::uint64_t packSize(int width, int height) {
    return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(width))
            << 32) |
           static_cast<std::uint32_t>(height);
  }

  void notePlacement() {
    int x = 0, y = 0;
    if (glfwWindowPositionIsKnowable()) {
      glfwGetWindowPos(fWindow, &x, &y);
    }
    fWindowX.store(x, std::memory_order_release);
    fWindowY.store(y, std::memory_order_release);
    if (const auto monitor = glfwGetPrimaryMonitor(); monitor != nullptr) {
      int ax = 0, ay = 0, aw = 0, ah = 0;
      glfwGetMonitorWorkarea(monitor, &ax, &ay, &aw, &ah);
      fWorkAreaX.store(ax, std::memory_order_release);
      fWorkAreaY.store(ay, std::memory_order_release);
      fWorkAreaW.store(aw, std::memory_order_release);
      fWorkAreaH.store(ah, std::memory_order_release);
    }
  }

  void installCallbacks() {
    glfwSetKeyCallback(
        fWindow, [](GLFWwindow *window, int key, int, int action,
                    int mods) {
          auto &self = from(window);
          if (action == platform::input::kPress && key == platform::input::kKeyF11) {
            if (self.fToggleFullscreen) {
              self.fToggleFullscreen();
            }
            return;
          }
          self.push({wallMs(), EventType::kKey, key, action,
                     static_cast<float>(mods)});
        });
    glfwSetWindowRefreshCallback(
        fWindow, [](GLFWwindow *window) {
          auto &self = from(window);
          self.notePlacement();
          self.fRefreshRequested.store(true, std::memory_order_release);
        });
    glfwSetWindowPosCallback(
        fWindow, [](GLFWwindow *window, int, int) {
          from(window).notePlacement();
        });
    glfwSetWindowIconifyCallback(
        fWindow, [](GLFWwindow *window, int iconified) {
          auto &self = from(window);
          self.fWindowIconified.store(iconified != GLFW_FALSE,
                                      std::memory_order_release);
          self.pushWindowActivity();
        });
    glfwSetWindowFocusCallback(
        fWindow, [](GLFWwindow *window, int focused) {
          auto &self = from(window);
          self.fWindowFocused = focused != GLFW_FALSE;
          self.pushWindowActivity();
        });
    glfwSetMouseButtonCallback(
        fWindow, [](GLFWwindow *window, int button, int action, int) {
          from(window).push(
              {wallMs(), EventType::kMouseButton, button, action});
        });
    glfwSetCursorPosCallback(
        fWindow, [](GLFWwindow *window, double x, double y) {
          from(window).pushCursor(x, y);
        });
    glfwSetScrollCallback(
        fWindow, [](GLFWwindow *window, double, double y) {
          from(window).push({wallMs(), EventType::kScroll, 0, 0,
                             static_cast<float>(y)});
        });
    glfwSetDropCallback(
        fWindow, [](GLFWwindow *window, int count, const char **paths) {
          auto &self = from(window);
          const std::scoped_lock lock(self.fDropMutex);
          for (int i = 0; i < count; ++i) {
            self.fDroppedFiles.emplace_back(paths[i]);
          }
        });
    glfwSetCharCallback(
        fWindow, [](GLFWwindow *window, unsigned int codepoint) {
          from(window).push({wallMs(), EventType::kChar,
                             static_cast<std::int32_t>(codepoint), 0});
        });
    glfwSetFramebufferSizeCallback(
        fWindow, [](GLFWwindow *window, int width, int height) {
          auto &self = from(window);
          self.fReportedSize.store(packSize(width, height),
                                   std::memory_order_release);
          self.measurePointerScale();
          self.push({wallMs(), EventType::kResize, width, height});
        });
  }

  [[nodiscard]] static WindowRuntime &from(GLFWwindow *window) {
    return *static_cast<WindowRuntime *>(
        glfwGetWindowUserPointer(window));
  }

  // GLFW cursor positions are in window coordinates while rendering uses
  // framebuffer pixels.  They differ on scaled Wayland outputs (and on any
  // other HiDPI platform), so normalize both mouse and synthesized touch
  // before AppInput applies the client's own UI scale.
  void pushCursor(double x, double y) {
    // The scale is read from what was measured when the window last changed
    // size, not asked for here.
    //
    // Asking is two calls into the window system, and this runs once per
    // cursor event: with raw input that is the mouse's own rate, a thousand
    // times a second, on the thread whose job is to keep taking events. The
    // pointer moved in steps because the pump was busy asking how big the
    // window was -- which it had just been told.
    const double scaleX = fPointerScaleX;
    const double scaleY = fPointerScaleY;

    // Lomiri can rotate a fullscreen Wayland surface before GLFW has swapped
    // its logical width and height.  The framebuffer is already landscape in
    // that interval (and can remain so for the lifetime of a Click window),
    // which makes the two ratios above reciprocal distortions rather than
    // content scales.  Pixel density is unchanged by a quarter turn, and its
    // uniform value is preserved by the ratio of the two areas.  Only repair
    // clearly inconsistent axes so genuinely anisotropic desktop scaling
    // keeps the GLFW values it has always used.
    this->push({wallMs(), EventType::kCursorMove, 0, 0,
                static_cast<float>(x * scaleX),
                static_cast<float>(y * scaleY)});
  }

  // How many framebuffer pixels one window coordinate is, measured when the
  // window says it has changed and kept until it says so again.
  void measurePointerScale() {
    int windowW = 0, windowH = 0, framebufferW = 0, framebufferH = 0;
    glfwGetWindowSize(fWindow, &windowW, &windowH);
    glfwGetFramebufferSize(fWindow, &framebufferW, &framebufferH);
    double scaleX = windowW > 0 ? static_cast<double>(framebufferW) /
                                      static_cast<double>(windowW)
                                : 1.0;
    double scaleY = windowH > 0 ? static_cast<double>(framebufferH) /
                                      static_cast<double>(windowH)
                                : 1.0;

    // Lomiri can rotate a fullscreen Wayland surface before GLFW has swapped
    // its logical width and height. The framebuffer is already landscape in
    // that interval (and can remain so for the lifetime of a Click window),
    // which makes the two ratios above reciprocal distortions rather than
    // content scales. Pixel density is unchanged by a quarter turn, and its
    // uniform value is preserved by the ratio of the two areas. Only repair
    // clearly inconsistent axes so genuinely anisotropic desktop scaling
    // keeps the GLFW values it has always used.
    const double smaller = std::min(scaleX, scaleY);
    const double larger = std::max(scaleX, scaleY);
    if (windowW > 0 && windowH > 0 && framebufferW > 0 && framebufferH > 0 &&
        smaller > 0.0 && larger / smaller > 1.5) {
      const double uniform =
          std::sqrt(static_cast<double>(framebufferW) * framebufferH /
                    (static_cast<double>(windowW) * windowH));
      scaleX = uniform;
      scaleY = uniform;
    }
    fPointerScaleX = scaleX;
    fPointerScaleY = scaleY;
  }

  void pushWindowActivity() {
    const bool active =
        fWindowFocused && !fWindowIconified.load(std::memory_order_acquire);
    if (active == fWindowActive.load(std::memory_order_acquire)) {
      return;
    }
    fWindowActive.store(active, std::memory_order_release);
    this->push({wallMs(), EventType::kWindowVisible, active ? 1 : 0});
  }

  GLFWwindow *fWindow = nullptr;
  bool fGlfwInitialized = false;
  bool fWindowFocused = true;
  std::atomic<bool> fWindowIconified{false};
  std::atomic<bool> fWindowActive{true};
  bool fSuspendWhenInactive = false;
  WindowExtent fInitial{};
  int fRefreshHz = 60;
  std::function<void()> fToggleFullscreen;
  WaylandTouch fWaylandTouch;
  SpscQueue<4096> fInput;
  std::atomic<bool> fQuit{false};
  std::atomic<int> fExitCode{0};
  std::atomic<int> fCursorModeRequest{-1};
  std::atomic<int> fRawMotionRequest{-1};
  std::atomic<int> fFullscreenRequest{-1};
  bool fFullscreen = true; // the window is created on a monitor
  int fWindowedX = 100, fWindowedY = 100;
  int fWindowedW = 1280, fWindowedH = 960;
  std::atomic<std::uint64_t> fReportedSize{0};
  std::atomic<bool> fRefreshRequested{false};
  std::atomic<int> fWindowX{0}, fWindowY{0};
  std::atomic<int> fWorkAreaX{0}, fWorkAreaY{0}, fWorkAreaW{0}, fWorkAreaH{0};
  std::mutex fDropMutex;
  std::vector<std::string> fDroppedFiles;
  // Framebuffer pixels per window coordinate, in each axis. Measured when
  // the window changes size and read on every cursor event.
  double fPointerScaleX = 1.0;
  double fPointerScaleY = 1.0;
};

} // namespace platform
