# Every library this client asks the provider for, and nothing of the
# client: the platform it is being built for, the options that decide which
# libraries it needs, and one find_package per library with the parts of it
# that are used.
#
# Apart from CMakeLists.txt because it is read by two projects. The client
# includes it, and so does flatpak/ports, which builds these libraries in a
# Flatpak module of their own and keeps them in the provider's store for the
# client's module to take. What the two ask for has to be the same thing --
# the same libraries with the same parts are the same entries in the store --
# and a list written down twice is two lists by the second commit.
#
# osucpp, skiff and skiff-widgets are not here. They are this project's own,
# and they are built with the client.

# ---- Detect platform ----

if(CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
  set(EMSCRIPTEN_BUILD TRUE)
else()
  set(EMSCRIPTEN_BUILD FALSE)
endif()

if(EMSCRIPTEN_BUILD)
  set(WASM_PKGCONFIG_DIR "" CACHE PATH "Extra pkg-config directory for cross-compiled wasm deps")
  if(WASM_PKGCONFIG_DIR)
    set(ENV{PKG_CONFIG_PATH} "${WASM_PKGCONFIG_DIR}")
  endif()
endif()

find_package(PkgConfig REQUIRED)

# Boost by the parts that are used. asio_core for the client's sockets and its
# TLS, beast for the HTTP on top of them, json for what comes back -- and
# nothing else of Boost is fetched or built, because a component that is not
# asked for is a library that is not there.
#
# A browser has no sockets to give and this client does not ask for any: the
# web build talks through fetch, its transport is network_backend_web.cc and
# the Beast one is not compiled. Asking for the networking libraries there
# fetched and built libraries for a program with no reference to any of them.
if(EMSCRIPTEN_BUILD)
  find_package(Boost REQUIRED COMPONENTS json)
else()
  find_package(Boost REQUIRED COMPONENTS asio_core beast json)
endif()

# Whether this build is for a machine that may be running Wayland.
#
# It decides one thing, and that thing is how Skia reaches GL. With neither
# egl nor x11 Skia assembles no GL interface at all and hands back nothing at
# run time, so one of the two has to be asked for: EGL is what works under
# Wayland (and under X11 with OSU_EGL=1), GLX is what an X11-only machine
# has. Off, this build also leaves out the Wayland touch bridge.
#
# Off is not a smaller version of the same program -- it is a program for a
# different machine. It is offered because the smaller set is what an
# installed Skia is likely to have: Debian builds its libskia with neither
# skia_use_egl nor skia_use_x11 set, so a build that asks for EGL cannot use
# it and builds its own.
option(OSU_WAYLAND "Reach GL through EGL, and build the Wayland touch bridge"
       ON)

# GL, and nothing about how Skia reaches it.
#
# Skia's egl and x11 features are two spellings of one thing -- both compile
# GrGLMakeNativeInterface, from different sources, so a build may have one or
# neither -- and this client needs neither: it assembles the interface from
# the loader of whatever created the context, in initSkia.
#
# So no interface is asked for. A Skia built here is built without one, and a
# Skia already installed is used whichever way it was built.
# The other backend, where it is asked for.
#
# Graphite is the one Skia is writing in place of Ganesh, and it speaks
# Vulkan rather than GL. Which of them this client draws through is a
# setting rather than a build, so a machine whose driver refuses Vulkan
# still has the GL renderer it always had.
#
# Off by default, and the reason is what asking costs. No distribution ships
# a Skia with Graphite in it, so a build that names this component cannot be
# answered by the machine's own Skia at all: it builds one, which is an hour
# of a computer for a backend the person building may not have wanted. On,
# it is a build that has both and chooses at run time.
#
# Never on a phone, in a browser or in a cross build for a device: Android's
# Vulkan is reached differently, a browser has none, and a device build
# should not carry a backend nobody there asked for.
set(OSU_SKIA_COMPONENTS gl png jpeg freetype)
option(OSU_VULKAN "Draw through Graphite on Vulkan where a driver allows it"
       OFF)
if(UNIX AND NOT APPLE AND NOT ANDROID AND NOT EMSCRIPTEN_BUILD AND
   NOT CMAKE_CROSSCOMPILING AND OSU_VULKAN)
  list(APPEND OSU_SKIA_COMPONENTS graphite-vulkan)
else()
  set(OSU_VULKAN OFF)
endif()
find_package(Skia REQUIRED COMPONENTS ${OSU_SKIA_COMPONENTS})
find_package(libzip REQUIRED)
find_package(SndFile REQUIRED)
find_package(mpg123 REQUIRED)

if(EMSCRIPTEN_BUILD)
  find_package(LibLZMA REQUIRED)
elseif(ANDROID)
  find_package(Threads REQUIRED)
  find_package(OpenAL REQUIRED)
  find_library(ANDROID_LIBRARY NAMES android REQUIRED)
  find_library(NATIVEWINDOW_LIBRARY NAMES nativewindow REQUIRED)
  find_library(EGL_LIBRARY NAMES EGL REQUIRED)
  find_library(GLES_LIBRARY NAMES GLESv3 GLESv2 REQUIRED)
  find_library(LOG_LIBRARY NAMES log REQUIRED)
else()
  find_package(OpenGL REQUIRED)
  find_package(Threads REQUIRED)
  # OpenSSL 3.0 and later is Apache-2.0, which GPLv3 and so AGPLv3 accept.
  # Everything before it carried the old OpenSSL/SSLeay licence, which does
  # not combine with the AGPL without an explicit linking exception -- and
  # this project would rather set a version floor than write one. Asio's TLS
  # has no other backend, so this is the whole of that decision.
  find_package(OpenSSL 3.0 REQUIRED)
  find_package(glfw3 REQUIRED)
  find_package(OpenAL REQUIRED)
  # GLFW has no touch API.  When Wayland development files are available a
  # small native bridge turns the first contact into the pointer events the
  # rest of the client already understands.  It stays optional so X11 and
  # non-Linux builds retain exactly their existing dependency set.
  if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND OSU_WAYLAND)
    pkg_check_modules(WAYLAND_CLIENT wayland-client)
  endif()
endif()

# The desktop's own file dialog, over D-Bus; CMakeLists.txt uses it when it is
# found.
#
# sd-bus comes from systemd, from basu (the same API unbundled, for systems
# without systemd) or from elogind. Whichever is installed will do, and a
# machine with none of them gets basu built from source -- it is the small
# one, and the only reason to ask for the other two first is that they are
# already there. Which one answered is not asked: the port arranges the
# headers so that one spelling works for all three.
if(NOT EMSCRIPTEN_BUILD AND NOT ANDROID)
  find_package(basu)
endif()

# Video export through libavcodec rather than through an ffmpeg process. Off
# by default because it is a build dependency for a feature most builds do not
# use; on, it is the only way to export from a build that cannot spawn one --
# a browser, or anywhere ffmpeg is not installed.
option(OSU_VIDEO_LIBAV "Encode exported video with libavcodec" OFF)
if(OSU_VIDEO_LIBAV)
  # By what is used of it: H.264 into an MP4, and the raw frames the encoder
  # is fed. Asking for the codecs by name is what makes them be there -- an
  # FFmpeg built with none of them links and encodes nothing.
  #
  # H.264 through libx264, which is GPL: this program is under the AGPL, so
  # that is a licence it can carry. What it buys is a file anything will
  # play at a size anything will accept -- MPEG-4 part 2 is neither, and it
  # stays as what the encoder falls back to where x264 was not built.
  find_package(FFmpeg REQUIRED COMPONENTS x264 mpeg4 rawvideo mp4)
endif()
