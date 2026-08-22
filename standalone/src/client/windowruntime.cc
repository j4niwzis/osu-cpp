module;

#ifdef __EMSCRIPTEN__
#include "emscripten_macro.h"
#endif

export module client.windowruntime;

import std;
import glfw;
import client.input;

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
    }
    fQuit.store(true, std::memory_order_release);
  }
#endif

  void close() {
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
          from(window).push({wallMs(), EventType::kCursorMove, 0, 0,
                             static_cast<float>(x), static_cast<float>(y)});
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

  glfw::GLFWwindow *fWindow = nullptr;
  bool fGlfwInitialized = false;
  WindowExtent fInitial{};
  int fRefreshHz = 60;
  std::function<void()> fToggleFullscreen;
  SpscQueue<4096> fInput;
  std::atomic<bool> fQuit{false};
  std::atomic<int> fExitCode{0};
  std::atomic<int> fCursorModeRequest{-1};
  std::atomic<int> fRawMotionRequest{-1};
  std::atomic<std::uint64_t> fReportedSize{0};
  std::atomic<bool> fRefreshRequested{false};
  std::atomic<int> fWindowX{0}, fWindowY{0};
  std::atomic<int> fWorkAreaX{0}, fWorkAreaY{0}, fWorkAreaW{0}, fWorkAreaH{0};
  std::mutex fDropMutex;
  std::vector<std::string> fDroppedFiles;
};

} // namespace client
