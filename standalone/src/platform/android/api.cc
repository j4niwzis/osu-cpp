module;

#include <EGL/egl.h>
#include <android/asset_manager.h>
#include <android/input.h>
#include <android/keycodes.h>
#include <android/log.h>
#include <android/native_activity.h>
#include <android/native_window.h>
#include <android_native_app_glue.h>
#include <jni.h>
// What it takes to put this program's own output where a phone can be asked
// to show it: a pipe, the two standard streams pointed into it, and a thread
// reading it back out.
#include <cstdio>
#include <unistd.h>

export module platform.android.api;

export using ::AAsset;
export using ::AAssetDir;
export using ::AAssetManager;
export using ::AInputEvent;
export using ::ANativeActivity;
export using ::ANativeWindow;
export using ::EGLConfig;
export using ::EGLContext;
export using ::EGLDisplay;
export using ::EGLSurface;
export using ::EGLint;
export using ::JNIEnv;
export using ::JavaVM;
export using ::android_app;
export using ::android_poll_source;
export using ::jbyte;
export using ::jbyteArray;
export using ::jclass;
export using ::jint;
export using ::jlong;
export using ::jmethodID;
export using ::jobject;
export using ::jsize;
export using ::jstring;

export using ::AAssetDir_close;
export using ::AAssetDir_getNextFileName;
export using ::AAssetManager_open;
export using ::AAssetManager_openDir;
export using ::AAsset_close;
export using ::AAsset_getLength64;
export using ::AAsset_read;
export using ::AInputEvent_getType;
export using ::AKeyEvent_getAction;
export using ::AKeyEvent_getKeyCode;
export using ::AKeyEvent_getMetaState;
export using ::ALooper_pollOnce;
export using ::ALooper_wake;
export using ::AMotionEvent_getAction;
export using ::AMotionEvent_getPointerCount;
export using ::AMotionEvent_getPointerId;
export using ::AMotionEvent_getX;
export using ::AMotionEvent_getY;
export using ::ANativeActivity_finish;
export using ::ANativeActivity_hideSoftInput;
export using ::ANativeActivity_showSoftInput;
export using ::ANativeWindow_getHeight;
export using ::ANativeWindow_getWidth;
export using ::ANativeWindow_setBuffersGeometry;
export using ::ANativeWindow_setBuffersTransform;
export using ::__android_log_write;
export using ::eglChooseConfig;
export using ::eglCreateContext;
export using ::eglCreateWindowSurface;
export using ::eglDestroyContext;
export using ::eglDestroySurface;
export using ::eglGetConfigAttrib;
export using ::eglGetDisplay;
// Where a GL entry point is. Needed to assemble a GL interface for a Skia
// that was not built with a factory of its own.
export using ::eglGetProcAddress;
export using ::eglInitialize;
export using ::eglMakeCurrent;
export using ::eglQuerySurface;
export using ::eglSwapBuffers;
export using ::eglSwapInterval;
export using ::eglTerminate;

export using ::APP_CMD_CONFIG_CHANGED;
export using ::APP_CMD_CONTENT_RECT_CHANGED;
export using ::APP_CMD_DESTROY;
export using ::APP_CMD_GAINED_FOCUS;
export using ::APP_CMD_INIT_WINDOW;
export using ::APP_CMD_LOST_FOCUS;
export using ::APP_CMD_PAUSE;
export using ::APP_CMD_RESUME;
export using ::APP_CMD_TERM_WINDOW;
export using ::APP_CMD_WINDOW_RESIZED;
export using ::AASSET_MODE_STREAMING;
export using ::AINPUT_EVENT_TYPE_KEY;
export using ::AINPUT_EVENT_TYPE_MOTION;
export using ::AKEY_EVENT_ACTION_DOWN;
export using ::AKEY_EVENT_ACTION_UP;
export using ::AKEYCODE_BACK;
export using ::AKEYCODE_0;
export using ::AKEYCODE_9;
export using ::AKEYCODE_A;
export using ::AKEYCODE_APOSTROPHE;
export using ::AKEYCODE_BACKSLASH;
export using ::AKEYCODE_COMMA;
export using ::AKEYCODE_DEL;
export using ::AKEYCODE_DPAD_DOWN;
export using ::AKEYCODE_DPAD_LEFT;
export using ::AKEYCODE_DPAD_RIGHT;
export using ::AKEYCODE_DPAD_UP;
export using ::AKEYCODE_ENTER;
export using ::AKEYCODE_EQUALS;
export using ::AKEYCODE_GRAVE;
export using ::AKEYCODE_LEFT_BRACKET;
export using ::AKEYCODE_MINUS;
export using ::AKEYCODE_PERIOD;
export using ::AKEYCODE_RIGHT_BRACKET;
export using ::AKEYCODE_SEMICOLON;
export using ::AKEYCODE_SLASH;
export using ::AKEYCODE_SPACE;
export using ::AKEYCODE_Z;
export using ::AMETA_SHIFT_ON;
export using ::AMOTION_EVENT_ACTION_CANCEL;
export using ::AMOTION_EVENT_ACTION_DOWN;
export using ::AMOTION_EVENT_ACTION_MASK;
export using ::AMOTION_EVENT_ACTION_MOVE;
export using ::AMOTION_EVENT_ACTION_POINTER_DOWN;
export using ::AMOTION_EVENT_ACTION_POINTER_INDEX_MASK;
export using ::AMOTION_EVENT_ACTION_POINTER_UP;
export using ::AMOTION_EVENT_ACTION_UP;
export using ::ANDROID_LOG_ERROR;
export using ::ANDROID_LOG_INFO;
export using ::pipe;
export using ::dup2;
export using ::read;
export using ::close;
export using ::fflush;
export using ::setvbuf;
export inline constexpr int kLineBuffered = _IOLBF;
export inline constexpr int kUnbuffered = _IONBF;
export using ::stdout;
export using ::stderr;
export using ::ANATIVEWINDOW_TRANSFORM_IDENTITY;
export using ::ANATIVEWINDOW_TRANSFORM_ROTATE_90;
export using ::ANATIVEACTIVITY_HIDE_SOFT_INPUT_NOT_ALWAYS;
export using ::ANATIVEACTIVITY_SHOW_SOFT_INPUT_FORCED;
export using ::ANATIVEACTIVITY_SHOW_SOFT_INPUT_IMPLICIT;

#define OSU_CAPTURE_ANDROID_CONSTANT(name) \
  inline const auto captured_##name = name

OSU_CAPTURE_ANDROID_CONSTANT(EGL_ALPHA_SIZE);
OSU_CAPTURE_ANDROID_CONSTANT(EGL_BLUE_SIZE);
OSU_CAPTURE_ANDROID_CONSTANT(EGL_CONTEXT_CLIENT_VERSION);
OSU_CAPTURE_ANDROID_CONSTANT(EGL_DEFAULT_DISPLAY);
OSU_CAPTURE_ANDROID_CONSTANT(EGL_DEPTH_SIZE);
OSU_CAPTURE_ANDROID_CONSTANT(EGL_GREEN_SIZE);
OSU_CAPTURE_ANDROID_CONSTANT(EGL_HEIGHT);
OSU_CAPTURE_ANDROID_CONSTANT(EGL_NATIVE_VISUAL_ID);
OSU_CAPTURE_ANDROID_CONSTANT(EGL_NONE);
OSU_CAPTURE_ANDROID_CONSTANT(EGL_NO_CONTEXT);
OSU_CAPTURE_ANDROID_CONSTANT(EGL_NO_DISPLAY);
OSU_CAPTURE_ANDROID_CONSTANT(EGL_NO_SURFACE);
OSU_CAPTURE_ANDROID_CONSTANT(EGL_OPENGL_ES3_BIT);
OSU_CAPTURE_ANDROID_CONSTANT(EGL_RED_SIZE);
OSU_CAPTURE_ANDROID_CONSTANT(EGL_RENDERABLE_TYPE);
OSU_CAPTURE_ANDROID_CONSTANT(EGL_STENCIL_SIZE);
OSU_CAPTURE_ANDROID_CONSTANT(EGL_SURFACE_TYPE);
OSU_CAPTURE_ANDROID_CONSTANT(EGL_TRUE);
OSU_CAPTURE_ANDROID_CONSTANT(EGL_WIDTH);
OSU_CAPTURE_ANDROID_CONSTANT(EGL_WINDOW_BIT);
OSU_CAPTURE_ANDROID_CONSTANT(AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT);
OSU_CAPTURE_ANDROID_CONSTANT(JNI_EDETACHED);
OSU_CAPTURE_ANDROID_CONSTANT(JNI_OK);
OSU_CAPTURE_ANDROID_CONSTANT(JNI_TRUE);
OSU_CAPTURE_ANDROID_CONSTANT(JNI_VERSION_1_6);

#undef OSU_CAPTURE_ANDROID_CONSTANT

#undef EGL_ALPHA_SIZE
#undef EGL_BLUE_SIZE
#undef EGL_CONTEXT_CLIENT_VERSION
#undef EGL_DEFAULT_DISPLAY
#undef EGL_DEPTH_SIZE
#undef EGL_GREEN_SIZE
#undef EGL_HEIGHT
#undef EGL_NATIVE_VISUAL_ID
#undef EGL_NONE
#undef EGL_NO_CONTEXT
#undef EGL_NO_DISPLAY
#undef EGL_NO_SURFACE
#undef EGL_OPENGL_ES3_BIT
#undef EGL_RED_SIZE
#undef EGL_RENDERABLE_TYPE
#undef EGL_STENCIL_SIZE
#undef EGL_SURFACE_TYPE
#undef EGL_TRUE
#undef EGL_WIDTH
#undef EGL_WINDOW_BIT
#undef AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT
#undef JNI_EDETACHED
#undef JNI_OK
#undef JNI_TRUE
#undef JNI_VERSION_1_6

#define OSU_EXPORT_ANDROID_CONSTANT(name) \
  export inline const auto name = captured_##name

OSU_EXPORT_ANDROID_CONSTANT(EGL_ALPHA_SIZE);
OSU_EXPORT_ANDROID_CONSTANT(EGL_BLUE_SIZE);
OSU_EXPORT_ANDROID_CONSTANT(EGL_CONTEXT_CLIENT_VERSION);
OSU_EXPORT_ANDROID_CONSTANT(EGL_DEFAULT_DISPLAY);
OSU_EXPORT_ANDROID_CONSTANT(EGL_DEPTH_SIZE);
OSU_EXPORT_ANDROID_CONSTANT(EGL_GREEN_SIZE);
OSU_EXPORT_ANDROID_CONSTANT(EGL_HEIGHT);
OSU_EXPORT_ANDROID_CONSTANT(EGL_NATIVE_VISUAL_ID);
OSU_EXPORT_ANDROID_CONSTANT(EGL_NONE);
OSU_EXPORT_ANDROID_CONSTANT(EGL_NO_CONTEXT);
OSU_EXPORT_ANDROID_CONSTANT(EGL_NO_DISPLAY);
OSU_EXPORT_ANDROID_CONSTANT(EGL_NO_SURFACE);
OSU_EXPORT_ANDROID_CONSTANT(EGL_OPENGL_ES3_BIT);
OSU_EXPORT_ANDROID_CONSTANT(EGL_RED_SIZE);
OSU_EXPORT_ANDROID_CONSTANT(EGL_RENDERABLE_TYPE);
OSU_EXPORT_ANDROID_CONSTANT(EGL_STENCIL_SIZE);
OSU_EXPORT_ANDROID_CONSTANT(EGL_SURFACE_TYPE);
OSU_EXPORT_ANDROID_CONSTANT(EGL_TRUE);
OSU_EXPORT_ANDROID_CONSTANT(EGL_WIDTH);
OSU_EXPORT_ANDROID_CONSTANT(EGL_WINDOW_BIT);
OSU_EXPORT_ANDROID_CONSTANT(AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT);
OSU_EXPORT_ANDROID_CONSTANT(JNI_EDETACHED);
OSU_EXPORT_ANDROID_CONSTANT(JNI_OK);
OSU_EXPORT_ANDROID_CONSTANT(JNI_TRUE);
OSU_EXPORT_ANDROID_CONSTANT(JNI_VERSION_1_6);

#undef OSU_EXPORT_ANDROID_CONSTANT
