module;

#ifndef __EMSCRIPTEN__
#include <dlfcn.h>
#endif

export module platform.presentation;

import std;

// What the window system knows about the buffer we are drawing into, and what
// it will accept from us about the buffer we are handing back.
//
// Both halves are the browser's answers to the questions this client had been
// guessing at. "How many buffers is the window cycling through" is not a
// constant to be picked with margin: EGL_EXT_buffer_age (and its GLX twin)
// says how old the contents of *this* buffer are, in frames, so the repaint
// can be exactly the damage since then. And a compositor does not have to be
// handed a whole window when a card lit up: eglSwapBuffersWithDamageKHR takes
// the rectangles that actually changed.
//
// Resolved with dlopen rather than linked, so a build without EGL, a driver
// without the extension and a GLX context without the GLX one all end up in
// the same place: unavailable, and the client falls back to what it did
// before.
export namespace platform::presentation {

// -1 when nothing can be asked, 0 when the contents are undefined (repaint
// everything), N >= 1 when the buffer holds the frame from N swaps ago.
[[nodiscard]] int bufferAge();

// The size of the drawable being drawn into, asked of the window system on
// the thread that owns the context. False when nobody will answer.
//
// Not the same question as "what did the last resize event say". The server
// resizes the window and reallocates its buffers when it likes; GLFW learns
// of it in glfwPollEvents on another thread, and every frame drawn between
// those two moments is drawn into a buffer that is already the new size and
// has never been painted. Neither the event queue nor an atomic written by
// the callback is any fresher than the callback itself, which is the mistake
// that took two attempts. The X server knows now.
[[nodiscard]] bool surfaceSize(void *glfwWindow, int *width, int *height);


// Hands the compositor the rectangles that changed since the last swap, in
// top-left coordinates. False when the platform will not take them, and the
// caller should swap the ordinary way.
[[nodiscard]] bool swapWithDamage(int surfaceHeight,
                                  std::span<const std::array<int, 4>> rects);

// "egl", "glx" or "none" -- for the diagnostic line, since a guess that has
// been replaced by an answer should say so.
[[nodiscard]] const char *backend();

} // namespace platform::presentation

#ifdef __EMSCRIPTEN__

namespace platform::presentation {
int bufferAge() { return -1; }
bool surfaceSize(void *, int *, int *) { return false; }
bool swapWithDamage(int, std::span<const std::array<int, 4>>) { return false; }
const char *backend() { return "none"; }
} // namespace platform::presentation

#else

namespace {

using EGLDisplay = void *;
using EGLSurface = void *;
using EGLBoolean = unsigned int;
using EGLint = std::int32_t;

constexpr EGLint kEglDraw = 0x3059;
constexpr EGLint kEglBufferAge = 0x313D; // EGL_BUFFER_AGE_EXT / _KHR
constexpr EGLint kEglExtensions = 0x3055;
constexpr EGLint kEglWidth = 0x3057;
constexpr EGLint kEglHeight = 0x3056;

using XDisplay = void;
using GLXDrawable = unsigned long;
constexpr int kGlxBackBufferAge = 0x20F4; // GLX_BACK_BUFFER_AGE_EXT

struct Egl {
  EGLDisplay (*getCurrentDisplay)() = nullptr;
  EGLSurface (*getCurrentSurface)(EGLint) = nullptr;
  EGLBoolean (*querySurface)(EGLDisplay, EGLSurface, EGLint, EGLint *) = nullptr;
  const char *(*queryString)(EGLDisplay, EGLint) = nullptr;
  void *(*getProcAddress)(const char *) = nullptr;
  EGLBoolean (*swapWithDamage)(EGLDisplay, EGLSurface, EGLint *, EGLint) =
      nullptr;
  bool fHasAge = false;
};

struct X11 {
  // XGetGeometry wants an X drawable. glXGetCurrentDrawable does not give one:
  // it gives the GLX drawable, which on this driver is a GLXWindow -- a
  // different XID that X_GetGeometry rejects, fatally, because an unhandled
  // Xlib error ends the process. The window itself has to come from GLFW.
  int (*getGeometry)(XDisplay *, GLXDrawable, GLXDrawable *, int *, int *,
                     unsigned int *, unsigned int *, unsigned int *,
                     unsigned int *) = nullptr;
  XDisplay *(*glfwDisplay)() = nullptr;
  GLXDrawable (*glfwWindow)(void *) = nullptr;
};

struct Glx {
  XDisplay *(*getCurrentDisplay)() = nullptr;
  GLXDrawable (*getCurrentDrawable)() = nullptr;
  void (*queryDrawable)(XDisplay *, GLXDrawable, int, unsigned int *) = nullptr;
  const char *(*queryExtensionsString)(XDisplay *, int) = nullptr;
  bool fHasAge = false;
};

// One resolve for the process: these are pointers into libraries the process
// has already loaded, and the answer cannot change while it runs.
struct Loaded {
  Egl fEgl;
  Glx fGlx;
  X11 fX11;
  const char *fBackend = "none";
};

[[nodiscard]] bool mentions(const char *haystack, const char *needle) {
  return haystack != nullptr && std::string_view(haystack).contains(needle);
}

[[nodiscard]] Loaded resolve() {
  Loaded out;
  // Said out loud, once: "via none" is three different answers -- the library
  // is not there, nothing is current on this thread, or the driver does not
  // have the extension -- and they call for three different next moves.
  const auto note = [](std::string_view what) {
    std::println(std::cerr, "[present] {}", what);
  };

  // The libraries are already in the process if the context came from them;
  // RTLD_NOLOAD would be tidier but is not portable, and loading them again
  // is harmless.
  void *eglLib = ::dlopen("libEGL.so.1", RTLD_LAZY | RTLD_LOCAL);
  if (eglLib == nullptr) {
    note("libEGL.so.1 did not load");
  }
  if (void *lib = eglLib; lib != nullptr) {
    auto &egl = out.fEgl;
    egl.getCurrentDisplay =
        reinterpret_cast<decltype(egl.getCurrentDisplay)>(
            ::dlsym(lib, "eglGetCurrentDisplay"));
    egl.getCurrentSurface =
        reinterpret_cast<decltype(egl.getCurrentSurface)>(
            ::dlsym(lib, "eglGetCurrentSurface"));
    egl.querySurface = reinterpret_cast<decltype(egl.querySurface)>(
        ::dlsym(lib, "eglQuerySurface"));
    egl.queryString = reinterpret_cast<decltype(egl.queryString)>(
        ::dlsym(lib, "eglQueryString"));
    egl.getProcAddress = reinterpret_cast<decltype(egl.getProcAddress)>(
        ::dlsym(lib, "eglGetProcAddress"));

    if (egl.getCurrentDisplay != nullptr && egl.querySurface != nullptr &&
        egl.queryString != nullptr) {
      const EGLDisplay display = egl.getCurrentDisplay();
      if (display == nullptr) {
        note("egl: no display current on this thread (a GLX context, then)");
      }
      if (display != nullptr) {
        const char *extensions = egl.queryString(display, kEglExtensions);
        egl.fHasAge = mentions(extensions, "EGL_EXT_buffer_age") ||
                      mentions(extensions, "EGL_KHR_partial_update");
        if (egl.getProcAddress != nullptr) {
          const char *name =
              mentions(extensions, "EGL_KHR_swap_buffers_with_damage")
                  ? "eglSwapBuffersWithDamageKHR"
              : mentions(extensions, "EGL_EXT_swap_buffers_with_damage")
                  ? "eglSwapBuffersWithDamageEXT"
                  : nullptr;
          if (name != nullptr) {
            egl.swapWithDamage =
                reinterpret_cast<decltype(egl.swapWithDamage)>(
                    egl.getProcAddress(name));
          }
        }
        std::println(std::cerr,
                     "[present] egl: buffer age {}, swap with damage {}",
                     egl.fHasAge ? "yes" : "no",
                     egl.swapWithDamage != nullptr ? "yes" : "no");
        if (egl.fHasAge || egl.swapWithDamage != nullptr) {
          out.fBackend = "egl";
          return out;
        }
      }
    }
  }

  // GLX has the same question and no answer for the second half: there is no
  // swap-with-damage there, only the age.
  if (void *x11 = ::dlopen("libX11.so.6", RTLD_LAZY | RTLD_LOCAL);
      x11 != nullptr) {
    out.fX11.getGeometry = reinterpret_cast<decltype(out.fX11.getGeometry)>(
        ::dlsym(x11, "XGetGeometry"));
  }
  // GLFW is already linked in, so these are looked up in the process and its
  // libraries rather than in one opened by name. Absent when GLFW was built
  // without X11 native access, which is answered by not asking.
  if (void *self = ::dlopen(nullptr, RTLD_LAZY); self != nullptr) {
    out.fX11.glfwDisplay = reinterpret_cast<decltype(out.fX11.glfwDisplay)>(
        ::dlsym(self, "glfwGetX11Display"));
    out.fX11.glfwWindow = reinterpret_cast<decltype(out.fX11.glfwWindow)>(
        ::dlsym(self, "glfwGetX11Window"));
  }
  if (out.fX11.getGeometry == nullptr || out.fX11.glfwWindow == nullptr) {
    note("x11: no XGetGeometry on a window GLFW will name");
  }

  void *glLib = ::dlopen("libGL.so.1", RTLD_LAZY | RTLD_LOCAL);
  if (glLib == nullptr) {
    note("libGL.so.1 did not load");
  }
  if (void *lib = glLib; lib != nullptr) {
    auto &glx = out.fGlx;
    glx.getCurrentDisplay = reinterpret_cast<decltype(glx.getCurrentDisplay)>(
        ::dlsym(lib, "glXGetCurrentDisplay"));
    glx.getCurrentDrawable = reinterpret_cast<decltype(glx.getCurrentDrawable)>(
        ::dlsym(lib, "glXGetCurrentDrawable"));
    glx.queryDrawable = reinterpret_cast<decltype(glx.queryDrawable)>(
        ::dlsym(lib, "glXQueryDrawable"));
    glx.queryExtensionsString =
        reinterpret_cast<decltype(glx.queryExtensionsString)>(
            ::dlsym(lib, "glXQueryExtensionsString"));
    if (glx.getCurrentDisplay == nullptr || glx.queryDrawable == nullptr ||
        glx.queryExtensionsString == nullptr) {
      note("glx: the entry points are not in libGL");
    } else {
      XDisplay *display = glx.getCurrentDisplay();
      if (display == nullptr) {
        note("glx: no display current on this thread");
      } else {
        const char *extensions = glx.queryExtensionsString(display, 0);
        glx.fHasAge = mentions(extensions, "GLX_EXT_buffer_age");
        std::println(std::cerr, "[present] glx: buffer age {}{}",
                     glx.fHasAge ? "yes" : "no",
                     extensions == nullptr ? " (no extension string)" : "");
        if (glx.fHasAge) {
          out.fBackend = "glx";
          return out;
        }
      }
    }
  }
  return out;
}

[[nodiscard]] const Loaded &loaded() {
  // Resolved on the thread that owns the context, on its first frame: the
  // current display and drawable are per-thread, and there is nothing to ask
  // before one exists.
  static const Loaded value = resolve();
  return value;
}

} // namespace

namespace platform::presentation {

int bufferAge() {
  const Loaded &state = loaded();
  if (state.fEgl.fHasAge) {
    const EGLDisplay display = state.fEgl.getCurrentDisplay();
    const EGLSurface surface = state.fEgl.getCurrentSurface(kEglDraw);
    EGLint age = 0;
    if (display != nullptr && surface != nullptr &&
        state.fEgl.querySurface(display, surface, kEglBufferAge, &age) != 0) {
      return static_cast<int>(age);
    }
    return -1;
  }
  if (state.fGlx.fHasAge) {
    XDisplay *display = state.fGlx.getCurrentDisplay();
    const GLXDrawable drawable = state.fGlx.getCurrentDrawable();
    if (display == nullptr || drawable == 0) {
      return -1;
    }
    unsigned int age = 0;
    state.fGlx.queryDrawable(display, drawable, kGlxBackBufferAge, &age);
    return static_cast<int>(age);
  }
  return -1;
}

bool surfaceSize(void *glfwWindow, int *width, int *height) {
  const Loaded &state = loaded();
  // EGL answers from the client side without a round trip, so it is asked
  // first where it is current at all.
  if (state.fEgl.getCurrentDisplay != nullptr &&
      state.fEgl.getCurrentSurface != nullptr &&
      state.fEgl.querySurface != nullptr) {
    const EGLDisplay display = state.fEgl.getCurrentDisplay();
    const EGLSurface surface = state.fEgl.getCurrentSurface(kEglDraw);
    EGLint w = 0;
    EGLint h = 0;
    if (display != nullptr && surface != nullptr &&
        state.fEgl.querySurface(display, surface, kEglWidth, &w) != 0 &&
        state.fEgl.querySurface(display, surface, kEglHeight, &h) != 0 &&
        w > 0 && h > 0) {
      *width = static_cast<int>(w);
      *height = static_cast<int>(h);
      return true;
    }
  }
  // Otherwise the X server, about the window GLFW made -- not about the GLX
  // drawable, which is a different XID and not one X_GetGeometry accepts.
  if (state.fX11.getGeometry == nullptr || state.fX11.glfwWindow == nullptr ||
      state.fX11.glfwDisplay == nullptr || glfwWindow == nullptr) {
    return false;
  }
  XDisplay *display = state.fX11.glfwDisplay();
  const GLXDrawable window = state.fX11.glfwWindow(glfwWindow);
  if (display == nullptr || window == 0) {
    return false;
  }
  GLXDrawable root = 0;
  int x = 0;
  int y = 0;
  unsigned int w = 0;
  unsigned int h = 0;
  unsigned int border = 0;
  unsigned int depth = 0;
  if (state.fX11.getGeometry(display, window, &root, &x, &y, &w, &h, &border,
                             &depth) == 0 ||
      w == 0 || h == 0) {
    return false;
  }
  *width = static_cast<int>(w);
  *height = static_cast<int>(h);
  return true;
}

bool swapWithDamage(int surfaceHeight,
                    std::span<const std::array<int, 4>> rects) {
  const Loaded &state = loaded();
  if (state.fEgl.swapWithDamage == nullptr || rects.empty()) {
    return false;
  }
  const EGLDisplay display = state.fEgl.getCurrentDisplay();
  const EGLSurface surface = state.fEgl.getCurrentSurface(kEglDraw);
  if (display == nullptr || surface == nullptr) {
    return false;
  }
  // EGL counts from the bottom left; everything in this client counts from
  // the top left.
  std::vector<EGLint> flat;
  flat.reserve(rects.size() * 4);
  for (const auto &rect : rects) {
    flat.push_back(rect[0]);
    flat.push_back(surfaceHeight - (rect[1] + rect[3]));
    flat.push_back(rect[2]);
    flat.push_back(rect[3]);
  }
  return state.fEgl.swapWithDamage(display, surface, flat.data(),
                                   static_cast<EGLint>(rects.size())) != 0;
}

const char *backend() { return loaded().fBackend; }

} // namespace platform::presentation

#endif
