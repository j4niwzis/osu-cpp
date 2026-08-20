module;

#ifndef __EMSCRIPTEN__
#include <dlfcn.h>
#endif

export module present;

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
export namespace present {

// -1 when nothing can be asked, 0 when the contents are undefined (repaint
// everything), N >= 1 when the buffer holds the frame from N swaps ago.
[[nodiscard]] int bufferAge();

// Hands the compositor the rectangles that changed since the last swap, in
// top-left coordinates. False when the platform will not take them, and the
// caller should swap the ordinary way.
[[nodiscard]] bool swapWithDamage(int surfaceHeight,
                                  std::span<const std::array<int, 4>> rects);

// "egl", "glx" or "none" -- for the diagnostic line, since a guess that has
// been replaced by an answer should say so.
[[nodiscard]] const char *backend();

} // namespace present

#ifdef __EMSCRIPTEN__

namespace present {
int bufferAge() { return -1; }
bool swapWithDamage(int, std::span<const std::array<int, 4>>) { return false; }
const char *backend() { return "none"; }
} // namespace present

#else

namespace {

using EGLDisplay = void *;
using EGLSurface = void *;
using EGLBoolean = unsigned int;
using EGLint = std::int32_t;

constexpr EGLint kEglDraw = 0x3059;
constexpr EGLint kEglBufferAge = 0x313D; // EGL_BUFFER_AGE_EXT / _KHR
constexpr EGLint kEglExtensions = 0x3055;

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
  const char *fBackend = "none";
};

[[nodiscard]] bool mentions(const char *haystack, const char *needle) {
  return haystack != nullptr && std::string_view(haystack).contains(needle);
}

[[nodiscard]] Loaded resolve() {
  Loaded out;

  // The libraries are already in the process if the context came from them;
  // RTLD_NOLOAD would be tidier but is not portable, and loading them again
  // is harmless.
  if (void *lib = ::dlopen("libEGL.so.1", RTLD_LAZY | RTLD_LOCAL);
      lib != nullptr) {
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
        if (egl.fHasAge || egl.swapWithDamage != nullptr) {
          out.fBackend = "egl";
          return out;
        }
      }
    }
  }

  // GLX has the same question and no answer for the second half: there is no
  // swap-with-damage there, only the age.
  if (void *lib = ::dlopen("libGL.so.1", RTLD_LAZY | RTLD_LOCAL);
      lib != nullptr) {
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
    if (glx.getCurrentDisplay != nullptr && glx.queryDrawable != nullptr &&
        glx.queryExtensionsString != nullptr) {
      XDisplay *display = glx.getCurrentDisplay();
      if (display != nullptr) {
        glx.fHasAge =
            mentions(glx.queryExtensionsString(display, 0), "GLX_EXT_buffer_age");
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

namespace present {

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

} // namespace present

#endif
