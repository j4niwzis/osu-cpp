module;

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

export module glfw;

export namespace glfw {

using ::GLFWcursorposfun;
using ::GLFWframebuffersizefun;
using ::GLFWkeyfun;
using ::GLFWmonitor;
using ::GLFWmousebuttonfun;
using ::GLFWvidmode;
using ::GLFWwindow;

using ::glfwCreateWindow;
using ::glfwDestroyWindow;
using ::glfwGetFramebufferSize;
using ::glfwGetPrimaryMonitor;
using ::glfwGetProcAddress;
using ::glfwGetTime;
using ::glfwGetVideoMode;
using ::glfwGetWindowMonitor;
using ::glfwGetWindowSize;
using ::glfwGetWindowUserPointer;
using ::glfwInit;
using ::glfwMakeContextCurrent;
using ::glfwPollEvents;
using ::glfwSetCursorPosCallback;
using ::glfwSetFramebufferSizeCallback;
using ::glfwSetInputMode;
using ::glfwSetKeyCallback;
using ::glfwSetMouseButtonCallback;
using ::glfwSetWindowMonitor;
using ::glfwSetWindowShouldClose;
using ::glfwSetWindowUserPointer;
using ::glfwSwapBuffers;
using ::glfwSwapInterval;
using ::glfwTerminate;
using ::glfwWindowHint;
using ::glfwWindowShouldClose;

inline constexpr int kFalse = GLFW_FALSE;
inline constexpr int kTrue = GLFW_TRUE;
inline constexpr int kClientApi = GLFW_CLIENT_API;
inline constexpr int kContextVersionMajor = GLFW_CONTEXT_VERSION_MAJOR;
inline constexpr int kContextVersionMinor = GLFW_CONTEXT_VERSION_MINOR;
inline constexpr int kOpenGLApi = GLFW_OPENGL_API;
inline constexpr int kOpenGLProfile = GLFW_OPENGL_PROFILE;
inline constexpr int kOpenGLCoreProfile = GLFW_OPENGL_CORE_PROFILE;
inline constexpr int kOpenGLForwardCompat = GLFW_OPENGL_FORWARD_COMPAT;
inline constexpr int kResizable = GLFW_RESIZABLE;
inline constexpr int kVisible = GLFW_VISIBLE;
inline constexpr int kDecorated = GLFW_DECORATED;

inline constexpr int kKeyEscape = GLFW_KEY_ESCAPE;
inline constexpr int kKeySpace = GLFW_KEY_SPACE;
inline constexpr int kKeyEnter = GLFW_KEY_ENTER;
inline constexpr int kKeyZ = GLFW_KEY_Z;
inline constexpr int kKeyX = GLFW_KEY_X;
inline constexpr int kKeyF11 = GLFW_KEY_F11;
inline constexpr int kKeyLeftShift = GLFW_KEY_LEFT_SHIFT;

inline constexpr int kMouseButtonLeft = GLFW_MOUSE_BUTTON_LEFT;
inline constexpr int kMouseButtonRight = GLFW_MOUSE_BUTTON_RIGHT;

inline constexpr int kPress = GLFW_PRESS;
inline constexpr int kRelease = GLFW_RELEASE;
inline constexpr int kRepeat = GLFW_REPEAT;

inline constexpr int kCursor = GLFW_CURSOR;
inline constexpr int kCursorNormal = GLFW_CURSOR_NORMAL;
inline constexpr int kCursorHidden = GLFW_CURSOR_HIDDEN;
inline constexpr int kCursorDisabled = GLFW_CURSOR_DISABLED;

} // namespace glfw
