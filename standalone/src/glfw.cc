module;

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

export module glfw;

export namespace glfw {

using ::GLFWcharfun;
using ::GLFWdropfun;
using ::GLFWcursorposfun;
using ::GLFWframebuffersizefun;
using ::GLFWkeyfun;
using ::GLFWmonitor;
using ::GLFWmousebuttonfun;
using ::GLFWvidmode;
using ::GLFWwindow;

using ::glfwCreateWindow;
using ::glfwDestroyWindow;
using ::glfwGetCursorPos;
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
using ::glfwPostEmptyEvent;
using ::glfwSetCharCallback;
using ::glfwSetDropCallback;
using ::glfwSetCursorPosCallback;
using ::glfwSetFramebufferSizeCallback;
using ::glfwSetInputMode;
using ::glfwSetKeyCallback;
using ::glfwSetMouseButtonCallback;
using ::glfwSetScrollCallback;
using ::glfwSetWindowMonitor;
using ::glfwSetWindowShouldClose;
using ::glfwSetWindowUserPointer;
using ::glfwSwapBuffers;
using ::glfwSwapInterval;
using ::glfwTerminate;
using ::glfwWaitEvents;
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
inline constexpr int kSamples = GLFW_SAMPLES;
inline constexpr int kScaleToMonitor = GLFW_SCALE_TO_MONITOR;
inline constexpr int kVisible = GLFW_VISIBLE;
inline constexpr int kDecorated = GLFW_DECORATED;

inline constexpr int kKeyEscape = GLFW_KEY_ESCAPE;
inline constexpr int kKeySpace = GLFW_KEY_SPACE;
inline constexpr int kKeyEnter = GLFW_KEY_ENTER;
inline constexpr int kKeyZ = GLFW_KEY_Z;
inline constexpr int kKeyX = GLFW_KEY_X;
inline constexpr int kKeyO = GLFW_KEY_O;
inline constexpr int kKeyLeftControl = GLFW_KEY_LEFT_CONTROL;
inline constexpr int kKeyRightControl = GLFW_KEY_RIGHT_CONTROL;
inline constexpr int kKeyF4 = GLFW_KEY_F4;
inline constexpr int kKeyW = GLFW_KEY_W;
inline constexpr int kKeyA = GLFW_KEY_A;
inline constexpr int kKeyF1 = GLFW_KEY_F1;
inline constexpr int kKeyF5 = GLFW_KEY_F5;
inline constexpr int kKeyF2 = GLFW_KEY_F2;
inline constexpr int kKeyF3 = GLFW_KEY_F3;
inline constexpr int kKeyF11 = GLFW_KEY_F11;
inline constexpr int kKeyBackspace = GLFW_KEY_BACKSPACE;
inline constexpr int kKeyD = GLFW_KEY_D;
inline constexpr int kKeyI = GLFW_KEY_I;
inline constexpr int kKeyP = GLFW_KEY_P;
inline constexpr int kKeyB = GLFW_KEY_B;
inline constexpr int kKeyQ = GLFW_KEY_Q;
inline constexpr int kKeyS = GLFW_KEY_S;
inline constexpr int kKeyR = GLFW_KEY_R;
inline constexpr int kKeyLeftShift = GLFW_KEY_LEFT_SHIFT;
inline constexpr int kKeyUp = GLFW_KEY_UP;
inline constexpr int kKeyDown = GLFW_KEY_DOWN;
inline constexpr int kKeyLeft = GLFW_KEY_LEFT;
inline constexpr int kKeyRight = GLFW_KEY_RIGHT;

inline constexpr int kMouseButtonLeft = GLFW_MOUSE_BUTTON_LEFT;
inline constexpr int kMouseButtonRight = GLFW_MOUSE_BUTTON_RIGHT;

inline constexpr int kModControl = GLFW_MOD_CONTROL;
inline constexpr int kModShift = GLFW_MOD_SHIFT;
inline constexpr int kPress = GLFW_PRESS;
inline constexpr int kRelease = GLFW_RELEASE;
inline constexpr int kRepeat = GLFW_REPEAT;

inline constexpr int kCursor = GLFW_CURSOR;
inline constexpr int kCursorNormal = GLFW_CURSOR_NORMAL;
inline constexpr int kCursorHidden = GLFW_CURSOR_HIDDEN;
inline constexpr int kCursorDisabled = GLFW_CURSOR_DISABLED;

} // namespace glfw
