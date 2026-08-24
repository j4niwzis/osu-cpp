module;

#ifdef __EMSCRIPTEN__
#include "emscripten_macro.h"
#endif

#ifdef OSU_WAYLAND_TOUCH
#include <dlfcn.h>
#include <wayland-client.h>
#endif

export module client.windowruntime;

import std;
import glfw;
import client.input;

namespace client {

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
  void init(glfw::GLFWwindow *window, Push push) {
#ifdef OSU_WAYLAND_TOUCH
    // Resolve these instead of linking them directly.  A GLFW built only for
    // X11 does not export its Wayland native-access entry points even when
    // wayland-client happens to be installed on the machine.
    using GetDisplay = wl_display *(*)();
    using GetWindow = wl_surface *(*)(glfw::GLFWwindow *);
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

private:
#ifdef OSU_WAYLAND_TOUCH
  static double wallMs() { return glfw::glfwGetTime() * 1000.0; }

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
    fPush({wallMs(), EventType::kMouseButton, glfw::kMouseButtonLeft,
           glfw::kRelease});
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
    self.fPush({wallMs(), EventType::kMouseButton, glfw::kMouseButtonLeft,
                glfw::kPress});
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

} // namespace client

export namespace client {

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
    if (!glfw::glfwInit()) {
      return false;
    }
    fGlfwInitialized = true;
    fToggleFullscreen = std::move(toggleFullscreen);

#ifdef __EMSCRIPTEN__
    glfw::glfwWindowHint(glfw::kClientApi, glfw::kOpenGLApi);
    glfw::glfwWindowHint(glfw::kContextVersionMajor, 3);
    glfw::glfwWindowHint(glfw::kContextVersionMinor, 0);
    glfw::glfwWindowHint(glfw::kResizable, glfw::kTrue);
    glfw::glfwWindowHint(glfw::kSamples, 0);
    fInitial.fWidth = EM_ASM_INT({ return Module.canvas.width; });
    fInitial.fHeight = EM_ASM_INT({ return Module.canvas.height; });
    fWindow = glfw::glfwCreateWindow(fInitial.fWidth, fInitial.fHeight,
                                     "osu_client", nullptr, nullptr);
#else
    const auto monitor = glfw::glfwGetPrimaryMonitor();
    const glfw::GLFWvidmode *mode = glfw::glfwGetVideoMode(monitor);
    if (mode == nullptr) {
      this->close();
      return false;
    }
    fInitial = {mode->width, mode->height};
    if (mode->refreshRate > 0) {
      fRefreshHz = mode->refreshRate;
    }

    struct ContextChoice {
      const char *fName;
      int fApi;
      int fMajor;
      int fMinor;
      int fProfile;
      int fCreation;
    };
    const int desktopCreation = std::getenv("OSU_EGL") != nullptr
                                    ? glfw::kEglContextApi
                                    : glfw::kNativeContextApi;
    const ContextChoice choices[] = {
        {"gl 4.1 core", glfw::kOpenGLApi, 4, 1, glfw::kOpenGLCoreProfile,
         desktopCreation},
        {"gl 3.0", glfw::kOpenGLApi, 3, 0, glfw::kOpenGLAnyProfile,
         desktopCreation},
        {"gles 3.0", glfw::kOpenGLEsApi, 3, 0, glfw::kOpenGLAnyProfile,
         glfw::kEglContextApi},
        {"gles 2.0", glfw::kOpenGLEsApi, 2, 0, glfw::kOpenGLAnyProfile,
         glfw::kEglContextApi},
    };
    for (const auto &choice : choices) {
      glfw::glfwWindowHint(glfw::kClientApi, choice.fApi);
      glfw::glfwWindowHint(glfw::kContextVersionMajor, choice.fMajor);
      glfw::glfwWindowHint(glfw::kContextVersionMinor, choice.fMinor);
      glfw::glfwWindowHint(glfw::kOpenGLProfile, choice.fProfile);
      glfw::glfwWindowHint(glfw::kOpenGLForwardCompat,
                           choice.fApi == glfw::kOpenGLApi && choice.fMajor >= 3
                               ? glfw::kTrue
                               : glfw::kFalse);
      glfw::glfwWindowHint(glfw::kContextCreationApi, choice.fCreation);
      glfw::glfwWindowHint(glfw::kResizable, glfw::kTrue);
      fWindow = glfw::glfwCreateWindow(fInitial.fWidth, fInitial.fHeight,
                                       "osu_client", monitor, nullptr);
      if (fWindow != nullptr) {
        std::println(std::cerr, "[gfx] context: {}", choice.fName);
        break;
      }
    }
#endif
    if (fWindow == nullptr) {
      this->close();
      return false;
    }

    glfw::glfwSetInputMode(fWindow, glfw::kCursor, glfw::kCursorNormal);
    glfw::glfwSetWindowUserPointer(fWindow, this);
    this->installCallbacks();
    fWaylandTouch.init(fWindow, [this](const Event &event) {
      if (event.fType == EventType::kCursorMove) {
        this->pushCursor(event.fX, event.fY);
      } else {
        this->push(event);
      }
    });
    this->notePlacement();
    return true;
  }

  [[nodiscard]] glfw::GLFWwindow *window() const { return fWindow; }
  [[nodiscard]] WindowExtent initialExtent() const { return fInitial; }
  [[nodiscard]] int refreshHz() const { return fRefreshHz; }

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
    glfw::glfwPostEmptyEvent();
  }
  void requestRawMotion(bool enabled) {
    fRawMotionRequest.store(enabled ? glfw::kTrue : glfw::kFalse,
                            std::memory_order_release);
    glfw::glfwPostEmptyEvent();
  }

  // Moving a window between a monitor and the desktop is one of the calls
  // GLFW pins to the thread that owns the window, so the drawing thread asks
  // and the pump answers.
  void requestFullscreen(bool wanted) {
    fFullscreenRequest.store(wanted ? 1 : 0, std::memory_order_release);
    glfw::glfwPostEmptyEvent();
  }

  void requestQuit() {
    fQuit.store(true, std::memory_order_release);
    glfw::glfwPostEmptyEvent();
  }
  [[nodiscard]] bool quitting() const {
    return fQuit.load(std::memory_order_acquire) ||
           (fWindow != nullptr && glfw::glfwWindowShouldClose(fWindow));
  }
  void setExitCode(int code) {
    fExitCode.store(code, std::memory_order_release);
  }
  [[nodiscard]] int exitCode() const {
    return fExitCode.load(std::memory_order_acquire);
  }

#ifndef __EMSCRIPTEN__
  void pumpEvents() {
    while (!this->quitting()) {
      glfw::glfwWaitEvents();
      const int cursorMode = fCursorModeRequest.exchange(-1);
      if (cursorMode != -1) {
        glfw::glfwSetInputMode(fWindow, glfw::kCursor, cursorMode);
      }
      const int rawMotion = fRawMotionRequest.exchange(-1);
      if (rawMotion != -1 && glfw::glfwRawMouseMotionSupported()) {
        glfw::glfwSetInputMode(fWindow, glfw::kRawMouseMotion, rawMotion);
      }
      const int fullscreen = fFullscreenRequest.exchange(-1);
      if (fullscreen != -1) {
        this->applyFullscreen(fullscreen == 1);
      }
    }
    fQuit.store(true, std::memory_order_release);
  }
#endif

#ifndef __EMSCRIPTEN__
  // Where the window was before it went fullscreen, so it can be put back.
  void applyFullscreen(bool wanted) {
    if (fWindow == nullptr || wanted == fFullscreen) {
      return;
    }
    if (wanted) {
      glfw::glfwGetWindowPos(fWindow, &fWindowedX, &fWindowedY);
      glfw::glfwGetWindowSize(fWindow, &fWindowedW, &fWindowedH);
      const auto monitor = glfw::glfwGetPrimaryMonitor();
      const glfw::GLFWvidmode *mode = glfw::glfwGetVideoMode(monitor);
      glfw::glfwSetWindowMonitor(fWindow, monitor, 0, 0, mode->width,
                                 mode->height, mode->refreshRate);
    } else {
      glfw::glfwSetWindowMonitor(fWindow, nullptr, fWindowedX, fWindowedY,
                                 fWindowedW, fWindowedH, 0);
    }
    fFullscreen = wanted;
  }
#endif

  void close() {
    fWaylandTouch.close();
    if (fWindow != nullptr) {
      glfw::glfwDestroyWindow(fWindow);
      fWindow = nullptr;
    }
    if (fGlfwInitialized) {
      glfw::glfwTerminate();
      fGlfwInitialized = false;
    }
  }

private:
  [[nodiscard]] static double wallMs() {
    return glfw::glfwGetTime() * 1000.0;
  }
  [[nodiscard]] static std::uint64_t packSize(int width, int height) {
    return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(width))
            << 32) |
           static_cast<std::uint32_t>(height);
  }

  void notePlacement() {
    int x = 0, y = 0;
    glfw::glfwGetWindowPos(fWindow, &x, &y);
    fWindowX.store(x, std::memory_order_release);
    fWindowY.store(y, std::memory_order_release);
    if (const auto monitor = glfw::glfwGetPrimaryMonitor(); monitor != nullptr) {
      int ax = 0, ay = 0, aw = 0, ah = 0;
      glfw::glfwGetMonitorWorkarea(monitor, &ax, &ay, &aw, &ah);
      fWorkAreaX.store(ax, std::memory_order_release);
      fWorkAreaY.store(ay, std::memory_order_release);
      fWorkAreaW.store(aw, std::memory_order_release);
      fWorkAreaH.store(ah, std::memory_order_release);
    }
  }

  void installCallbacks() {
    glfw::glfwSetKeyCallback(
        fWindow, [](glfw::GLFWwindow *window, int key, int, int action,
                    int mods) {
          auto &self = from(window);
          if (action == glfw::kPress && key == glfw::kKeyF11) {
            if (self.fToggleFullscreen) {
              self.fToggleFullscreen();
            }
            return;
          }
          self.push({wallMs(), EventType::kKey, key, action,
                     static_cast<float>(mods)});
        });
    glfw::glfwSetWindowRefreshCallback(
        fWindow, [](glfw::GLFWwindow *window) {
          auto &self = from(window);
          self.notePlacement();
          self.fRefreshRequested.store(true, std::memory_order_release);
        });
    glfw::glfwSetWindowPosCallback(
        fWindow, [](glfw::GLFWwindow *window, int, int) {
          from(window).notePlacement();
        });
    glfw::glfwSetMouseButtonCallback(
        fWindow, [](glfw::GLFWwindow *window, int button, int action, int) {
          from(window).push(
              {wallMs(), EventType::kMouseButton, button, action});
        });
    glfw::glfwSetCursorPosCallback(
        fWindow, [](glfw::GLFWwindow *window, double x, double y) {
          from(window).pushCursor(x, y);
        });
    glfw::glfwSetScrollCallback(
        fWindow, [](glfw::GLFWwindow *window, double, double y) {
          from(window).push({wallMs(), EventType::kScroll, 0, 0,
                             static_cast<float>(y)});
        });
    glfw::glfwSetDropCallback(
        fWindow, [](glfw::GLFWwindow *window, int count, const char **paths) {
          auto &self = from(window);
          const std::scoped_lock lock(self.fDropMutex);
          for (int i = 0; i < count; ++i) {
            self.fDroppedFiles.emplace_back(paths[i]);
          }
        });
    glfw::glfwSetCharCallback(
        fWindow, [](glfw::GLFWwindow *window, unsigned int codepoint) {
          from(window).push({wallMs(), EventType::kChar,
                             static_cast<std::int32_t>(codepoint), 0});
        });
    glfw::glfwSetFramebufferSizeCallback(
        fWindow, [](glfw::GLFWwindow *window, int width, int height) {
          auto &self = from(window);
          self.fReportedSize.store(packSize(width, height),
                                   std::memory_order_release);
          self.push({wallMs(), EventType::kResize, width, height});
        });
  }

  [[nodiscard]] static WindowRuntime &from(glfw::GLFWwindow *window) {
    return *static_cast<WindowRuntime *>(
        glfw::glfwGetWindowUserPointer(window));
  }

  // GLFW cursor positions are in window coordinates while rendering uses
  // framebuffer pixels.  They differ on scaled Wayland outputs (and on any
  // other HiDPI platform), so normalize both mouse and synthesized touch
  // before AppInput applies the client's own UI scale.
  void pushCursor(double x, double y) {
    int windowW = 0, windowH = 0, framebufferW = 0, framebufferH = 0;
    glfw::glfwGetWindowSize(fWindow, &windowW, &windowH);
    glfw::glfwGetFramebufferSize(fWindow, &framebufferW, &framebufferH);
    const double scaleX = windowW > 0 ? static_cast<double>(framebufferW) /
                                           static_cast<double>(windowW)
                                     : 1.0;
    const double scaleY = windowH > 0 ? static_cast<double>(framebufferH) /
                                           static_cast<double>(windowH)
                                     : 1.0;
    this->push({wallMs(), EventType::kCursorMove, 0, 0,
                static_cast<float>(x * scaleX),
                static_cast<float>(y * scaleY)});
  }

  glfw::GLFWwindow *fWindow = nullptr;
  bool fGlfwInitialized = false;
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
};

} // namespace client
