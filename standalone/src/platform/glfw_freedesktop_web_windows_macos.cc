module;

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

// The Ubuntu Touch build uses GLFW 3.2's retired Mir backend.  Its packaged
// library carries these small 3.3 compatibility entry points, so declare the
// matching public surface when compiling against the older header.
#if GLFW_VERSION_MAJOR == 3 && GLFW_VERSION_MINOR < 3
#define GLFW_SCALE_TO_MONITOR 0x0002200C
#define GLFW_RAW_MOUSE_MOTION 0x00033005
extern "C" {
GLFWAPI int glfwRawMouseMotionSupported(void);
GLFWAPI void glfwGetMonitorWorkarea(GLFWmonitor *monitor, int *xpos, int *ypos,
                                    int *width, int *height);
}
#endif

export module platform.glfw;

export using ::GLFWcharfun;
export using ::GLFWdropfun;
export using ::GLFWcursorposfun;
export using ::GLFWerrorfun;
export using ::GLFWframebuffersizefun;
export using ::GLFWwindowrefreshfun;
export using ::GLFWwindowposfun;
export using ::GLFWwindowiconifyfun;
export using ::GLFWwindowfocusfun;
export using ::GLFWkeyfun;
export using ::GLFWmonitor;
export using ::GLFWmousebuttonfun;
export using ::GLFWvidmode;
export using ::GLFWwindow;

export using ::glfwCreateWindow;
export using ::glfwDestroyWindow;
export using ::glfwGetCursorPos;
export using ::glfwGetFramebufferSize;
export using ::glfwGetPrimaryMonitor;
export using ::glfwGetProcAddress;
export using ::glfwGetTime;
export using ::glfwGetVideoMode;
export using ::glfwGetWindowMonitor;
export using ::glfwGetWindowPos;
export using ::glfwGetWindowSize;
export using ::glfwGetMonitorWorkarea;
export using ::glfwGetWindowUserPointer;
export using ::glfwInit;
export using ::glfwMakeContextCurrent;
export using ::glfwPollEvents;
export using ::glfwRawMouseMotionSupported;
export using ::glfwPostEmptyEvent;
export using ::glfwSetCharCallback;
export using ::glfwSetDropCallback;
export using ::glfwSetErrorCallback;
export using ::glfwSetCursorPosCallback;
export using ::glfwSetFramebufferSizeCallback;
export using ::glfwSetWindowRefreshCallback;
export using ::glfwSetWindowPosCallback;
export using ::glfwSetWindowIconifyCallback;
export using ::glfwSetWindowFocusCallback;
export using ::glfwSetInputMode;
export using ::glfwSetKeyCallback;
export using ::glfwSetMouseButtonCallback;
export using ::glfwSetScrollCallback;
export using ::glfwSetWindowMonitor;
export using ::glfwSetWindowShouldClose;
export using ::glfwSetWindowUserPointer;
export using ::glfwSwapBuffers;
export using ::glfwSwapInterval;
export using ::glfwTerminate;
export using ::glfwWaitEvents;
export using ::glfwWaitEventsTimeout;
export using ::glfwWindowHint;
export using ::glfwWindowShouldClose;

inline constexpr int captured_GLFW_FALSE = GLFW_FALSE;
#undef GLFW_FALSE
export inline constexpr int GLFW_FALSE = captured_GLFW_FALSE;
inline constexpr int captured_GLFW_TRUE = GLFW_TRUE;
#undef GLFW_TRUE
export inline constexpr int GLFW_TRUE = captured_GLFW_TRUE;
inline constexpr int captured_GLFW_CLIENT_API = GLFW_CLIENT_API;
#undef GLFW_CLIENT_API
export inline constexpr int GLFW_CLIENT_API = captured_GLFW_CLIENT_API;
inline constexpr int captured_GLFW_CONTEXT_VERSION_MAJOR = GLFW_CONTEXT_VERSION_MAJOR;
#undef GLFW_CONTEXT_VERSION_MAJOR
export inline constexpr int GLFW_CONTEXT_VERSION_MAJOR = captured_GLFW_CONTEXT_VERSION_MAJOR;
inline constexpr int captured_GLFW_CONTEXT_VERSION_MINOR = GLFW_CONTEXT_VERSION_MINOR;
#undef GLFW_CONTEXT_VERSION_MINOR
export inline constexpr int GLFW_CONTEXT_VERSION_MINOR = captured_GLFW_CONTEXT_VERSION_MINOR;
inline constexpr int captured_GLFW_OPENGL_API = GLFW_OPENGL_API;
#undef GLFW_OPENGL_API
export inline constexpr int GLFW_OPENGL_API = captured_GLFW_OPENGL_API;
inline constexpr int captured_GLFW_OPENGL_ES_API = GLFW_OPENGL_ES_API;
#undef GLFW_OPENGL_ES_API
export inline constexpr int GLFW_OPENGL_ES_API = captured_GLFW_OPENGL_ES_API;
inline constexpr int captured_GLFW_CONTEXT_CREATION_API = GLFW_CONTEXT_CREATION_API;
#undef GLFW_CONTEXT_CREATION_API
export inline constexpr int GLFW_CONTEXT_CREATION_API = captured_GLFW_CONTEXT_CREATION_API;
inline constexpr int captured_GLFW_EGL_CONTEXT_API = GLFW_EGL_CONTEXT_API;
#undef GLFW_EGL_CONTEXT_API
export inline constexpr int GLFW_EGL_CONTEXT_API = captured_GLFW_EGL_CONTEXT_API;
inline constexpr int captured_GLFW_NATIVE_CONTEXT_API = GLFW_NATIVE_CONTEXT_API;
#undef GLFW_NATIVE_CONTEXT_API
export inline constexpr int GLFW_NATIVE_CONTEXT_API = captured_GLFW_NATIVE_CONTEXT_API;
inline constexpr int captured_GLFW_OPENGL_ANY_PROFILE = GLFW_OPENGL_ANY_PROFILE;
#undef GLFW_OPENGL_ANY_PROFILE
export inline constexpr int GLFW_OPENGL_ANY_PROFILE = captured_GLFW_OPENGL_ANY_PROFILE;
inline constexpr int captured_GLFW_OPENGL_PROFILE = GLFW_OPENGL_PROFILE;
#undef GLFW_OPENGL_PROFILE
export inline constexpr int GLFW_OPENGL_PROFILE = captured_GLFW_OPENGL_PROFILE;
inline constexpr int captured_GLFW_OPENGL_CORE_PROFILE = GLFW_OPENGL_CORE_PROFILE;
#undef GLFW_OPENGL_CORE_PROFILE
export inline constexpr int GLFW_OPENGL_CORE_PROFILE = captured_GLFW_OPENGL_CORE_PROFILE;
inline constexpr int captured_GLFW_OPENGL_FORWARD_COMPAT = GLFW_OPENGL_FORWARD_COMPAT;
#undef GLFW_OPENGL_FORWARD_COMPAT
export inline constexpr int GLFW_OPENGL_FORWARD_COMPAT = captured_GLFW_OPENGL_FORWARD_COMPAT;
inline constexpr int captured_GLFW_RESIZABLE = GLFW_RESIZABLE;
#undef GLFW_RESIZABLE
export inline constexpr int GLFW_RESIZABLE = captured_GLFW_RESIZABLE;
inline constexpr int captured_GLFW_SAMPLES = GLFW_SAMPLES;
#undef GLFW_SAMPLES
export inline constexpr int GLFW_SAMPLES = captured_GLFW_SAMPLES;
inline constexpr int captured_GLFW_SCALE_TO_MONITOR = GLFW_SCALE_TO_MONITOR;
#undef GLFW_SCALE_TO_MONITOR
export inline constexpr int GLFW_SCALE_TO_MONITOR = captured_GLFW_SCALE_TO_MONITOR;
inline constexpr int captured_GLFW_VISIBLE = GLFW_VISIBLE;
#undef GLFW_VISIBLE
export inline constexpr int GLFW_VISIBLE = captured_GLFW_VISIBLE;
inline constexpr int captured_GLFW_DECORATED = GLFW_DECORATED;
#undef GLFW_DECORATED
export inline constexpr int GLFW_DECORATED = captured_GLFW_DECORATED;

inline constexpr int captured_GLFW_KEY_ESCAPE = GLFW_KEY_ESCAPE;
#undef GLFW_KEY_ESCAPE
export inline constexpr int GLFW_KEY_ESCAPE = captured_GLFW_KEY_ESCAPE;
inline constexpr int captured_GLFW_KEY_TAB = GLFW_KEY_TAB;
#undef GLFW_KEY_TAB
export inline constexpr int GLFW_KEY_TAB = captured_GLFW_KEY_TAB;
inline constexpr int captured_GLFW_KEY_SPACE = GLFW_KEY_SPACE;
#undef GLFW_KEY_SPACE
export inline constexpr int GLFW_KEY_SPACE = captured_GLFW_KEY_SPACE;
inline constexpr int captured_GLFW_KEY_ENTER = GLFW_KEY_ENTER;
#undef GLFW_KEY_ENTER
export inline constexpr int GLFW_KEY_ENTER = captured_GLFW_KEY_ENTER;
inline constexpr int captured_GLFW_KEY_Z = GLFW_KEY_Z;
#undef GLFW_KEY_Z
export inline constexpr int GLFW_KEY_Z = captured_GLFW_KEY_Z;
inline constexpr int captured_GLFW_KEY_X = GLFW_KEY_X;
#undef GLFW_KEY_X
export inline constexpr int GLFW_KEY_X = captured_GLFW_KEY_X;
inline constexpr int captured_GLFW_KEY_O = GLFW_KEY_O;
#undef GLFW_KEY_O
export inline constexpr int GLFW_KEY_O = captured_GLFW_KEY_O;
inline constexpr int captured_GLFW_KEY_LEFT_CONTROL = GLFW_KEY_LEFT_CONTROL;
#undef GLFW_KEY_LEFT_CONTROL
export inline constexpr int GLFW_KEY_LEFT_CONTROL = captured_GLFW_KEY_LEFT_CONTROL;
inline constexpr int captured_GLFW_KEY_RIGHT_CONTROL = GLFW_KEY_RIGHT_CONTROL;
#undef GLFW_KEY_RIGHT_CONTROL
export inline constexpr int GLFW_KEY_RIGHT_CONTROL = captured_GLFW_KEY_RIGHT_CONTROL;
inline constexpr int captured_GLFW_KEY_F4 = GLFW_KEY_F4;
#undef GLFW_KEY_F4
export inline constexpr int GLFW_KEY_F4 = captured_GLFW_KEY_F4;
inline constexpr int captured_GLFW_KEY_W = GLFW_KEY_W;
#undef GLFW_KEY_W
export inline constexpr int GLFW_KEY_W = captured_GLFW_KEY_W;
inline constexpr int captured_GLFW_KEY_A = GLFW_KEY_A;
#undef GLFW_KEY_A
export inline constexpr int GLFW_KEY_A = captured_GLFW_KEY_A;
inline constexpr int captured_GLFW_KEY_F1 = GLFW_KEY_F1;
#undef GLFW_KEY_F1
export inline constexpr int GLFW_KEY_F1 = captured_GLFW_KEY_F1;
inline constexpr int captured_GLFW_KEY_F5 = GLFW_KEY_F5;
#undef GLFW_KEY_F5
export inline constexpr int GLFW_KEY_F5 = captured_GLFW_KEY_F5;
inline constexpr int captured_GLFW_KEY_F2 = GLFW_KEY_F2;
#undef GLFW_KEY_F2
export inline constexpr int GLFW_KEY_F2 = captured_GLFW_KEY_F2;
inline constexpr int captured_GLFW_KEY_F3 = GLFW_KEY_F3;
#undef GLFW_KEY_F3
export inline constexpr int GLFW_KEY_F3 = captured_GLFW_KEY_F3;
inline constexpr int captured_GLFW_KEY_F11 = GLFW_KEY_F11;
#undef GLFW_KEY_F11
export inline constexpr int GLFW_KEY_F11 = captured_GLFW_KEY_F11;
inline constexpr int captured_GLFW_KEY_BACKSPACE = GLFW_KEY_BACKSPACE;
#undef GLFW_KEY_BACKSPACE
export inline constexpr int GLFW_KEY_BACKSPACE = captured_GLFW_KEY_BACKSPACE;
inline constexpr int captured_GLFW_KEY_D = GLFW_KEY_D;
#undef GLFW_KEY_D
export inline constexpr int GLFW_KEY_D = captured_GLFW_KEY_D;
inline constexpr int captured_GLFW_KEY_I = GLFW_KEY_I;
#undef GLFW_KEY_I
export inline constexpr int GLFW_KEY_I = captured_GLFW_KEY_I;
inline constexpr int captured_GLFW_KEY_P = GLFW_KEY_P;
#undef GLFW_KEY_P
export inline constexpr int GLFW_KEY_P = captured_GLFW_KEY_P;
inline constexpr int captured_GLFW_KEY_B = GLFW_KEY_B;
#undef GLFW_KEY_B
export inline constexpr int GLFW_KEY_B = captured_GLFW_KEY_B;
inline constexpr int captured_GLFW_KEY_Q = GLFW_KEY_Q;
#undef GLFW_KEY_Q
export inline constexpr int GLFW_KEY_Q = captured_GLFW_KEY_Q;
inline constexpr int captured_GLFW_KEY_S = GLFW_KEY_S;
#undef GLFW_KEY_S
export inline constexpr int GLFW_KEY_S = captured_GLFW_KEY_S;
inline constexpr int captured_GLFW_KEY_R = GLFW_KEY_R;
#undef GLFW_KEY_R
export inline constexpr int GLFW_KEY_R = captured_GLFW_KEY_R;
inline constexpr int captured_GLFW_KEY_LEFT_SHIFT = GLFW_KEY_LEFT_SHIFT;
#undef GLFW_KEY_LEFT_SHIFT
export inline constexpr int GLFW_KEY_LEFT_SHIFT = captured_GLFW_KEY_LEFT_SHIFT;
inline constexpr int captured_GLFW_KEY_UP = GLFW_KEY_UP;
#undef GLFW_KEY_UP
export inline constexpr int GLFW_KEY_UP = captured_GLFW_KEY_UP;
inline constexpr int captured_GLFW_KEY_DOWN = GLFW_KEY_DOWN;
#undef GLFW_KEY_DOWN
export inline constexpr int GLFW_KEY_DOWN = captured_GLFW_KEY_DOWN;
inline constexpr int captured_GLFW_KEY_LEFT = GLFW_KEY_LEFT;
#undef GLFW_KEY_LEFT
export inline constexpr int GLFW_KEY_LEFT = captured_GLFW_KEY_LEFT;
inline constexpr int captured_GLFW_KEY_RIGHT = GLFW_KEY_RIGHT;
#undef GLFW_KEY_RIGHT
export inline constexpr int GLFW_KEY_RIGHT = captured_GLFW_KEY_RIGHT;
inline constexpr int captured_GLFW_KEY_DELETE = GLFW_KEY_DELETE;
#undef GLFW_KEY_DELETE
export inline constexpr int GLFW_KEY_DELETE = captured_GLFW_KEY_DELETE;
inline constexpr int captured_GLFW_KEY_PAGE_UP = GLFW_KEY_PAGE_UP;
#undef GLFW_KEY_PAGE_UP
export inline constexpr int GLFW_KEY_PAGE_UP = captured_GLFW_KEY_PAGE_UP;
inline constexpr int captured_GLFW_KEY_PAGE_DOWN = GLFW_KEY_PAGE_DOWN;
#undef GLFW_KEY_PAGE_DOWN
export inline constexpr int GLFW_KEY_PAGE_DOWN = captured_GLFW_KEY_PAGE_DOWN;

inline constexpr int captured_GLFW_MOUSE_BUTTON_LEFT = GLFW_MOUSE_BUTTON_LEFT;
#undef GLFW_MOUSE_BUTTON_LEFT
export inline constexpr int GLFW_MOUSE_BUTTON_LEFT = captured_GLFW_MOUSE_BUTTON_LEFT;
inline constexpr int captured_GLFW_MOUSE_BUTTON_RIGHT = GLFW_MOUSE_BUTTON_RIGHT;
#undef GLFW_MOUSE_BUTTON_RIGHT
export inline constexpr int GLFW_MOUSE_BUTTON_RIGHT = captured_GLFW_MOUSE_BUTTON_RIGHT;

inline constexpr int captured_GLFW_MOD_CONTROL = GLFW_MOD_CONTROL;
#undef GLFW_MOD_CONTROL
export inline constexpr int GLFW_MOD_CONTROL = captured_GLFW_MOD_CONTROL;
inline constexpr int captured_GLFW_MOD_SHIFT = GLFW_MOD_SHIFT;
#undef GLFW_MOD_SHIFT
export inline constexpr int GLFW_MOD_SHIFT = captured_GLFW_MOD_SHIFT;
inline constexpr int captured_GLFW_PRESS = GLFW_PRESS;
#undef GLFW_PRESS
export inline constexpr int GLFW_PRESS = captured_GLFW_PRESS;
inline constexpr int captured_GLFW_RELEASE = GLFW_RELEASE;
#undef GLFW_RELEASE
export inline constexpr int GLFW_RELEASE = captured_GLFW_RELEASE;
inline constexpr int captured_GLFW_REPEAT = GLFW_REPEAT;
#undef GLFW_REPEAT
export inline constexpr int GLFW_REPEAT = captured_GLFW_REPEAT;

inline constexpr int captured_GLFW_CURSOR = GLFW_CURSOR;
#undef GLFW_CURSOR
export inline constexpr int GLFW_CURSOR = captured_GLFW_CURSOR;
inline constexpr int captured_GLFW_CURSOR_NORMAL = GLFW_CURSOR_NORMAL;
#undef GLFW_CURSOR_NORMAL
export inline constexpr int GLFW_CURSOR_NORMAL = captured_GLFW_CURSOR_NORMAL;
inline constexpr int captured_GLFW_CURSOR_HIDDEN = GLFW_CURSOR_HIDDEN;
#undef GLFW_CURSOR_HIDDEN
export inline constexpr int GLFW_CURSOR_HIDDEN = captured_GLFW_CURSOR_HIDDEN;
inline constexpr int captured_GLFW_CURSOR_DISABLED = GLFW_CURSOR_DISABLED;
#undef GLFW_CURSOR_DISABLED
export inline constexpr int GLFW_CURSOR_DISABLED = captured_GLFW_CURSOR_DISABLED;
inline constexpr int captured_GLFW_RAW_MOUSE_MOTION = GLFW_RAW_MOUSE_MOTION;
#undef GLFW_RAW_MOUSE_MOTION
export inline constexpr int GLFW_RAW_MOUSE_MOTION = captured_GLFW_RAW_MOUSE_MOTION;

// Whether asking where the window is means anything.
//
// Wayland does not tell a client where its window is: the position is the
// compositor's business, and asking GLFW for it is answered with
// GLFW_FEATURE_UNAVAILABLE rather than with a zero. Which platform GLFW
// chose can only be asked from 3.4 on, and a build against anything older
// than that is one where the question had a single answer anyway.
export inline bool glfwWindowPositionIsKnowable() {
#if defined(GLFW_PLATFORM_WAYLAND)
  return glfwGetPlatform() != GLFW_PLATFORM_WAYLAND;
#else
  return true;
#endif
}
