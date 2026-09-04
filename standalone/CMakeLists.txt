cmake_minimum_required(VERSION 4.3.4 FATAL_ERROR)

include(${CMAKE_CURRENT_LIST_DIR}/../cmake/import-std.cmake)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_EXTENSIONS OFF)

# Every find_package below is answered by this: from the system when the
# system has it and it will do, and built from a pinned source when it will
# not. It has to be named before project(), because that is the only way a
# dependency provider may be installed.
set(CMAKE_PROJECT_TOP_LEVEL_INCLUDES
    ${CMAKE_CURRENT_LIST_DIR}/../cmake/get_cme.cmake)

project(osu_client LANGUAGES C CXX)

# Whether the answer to every find_package below is actually installed.
#
# CMAKE_PROJECT_TOP_LEVEL_INCLUDES is read by the top-level project() call
# and by no other, so a build configured from somewhere else -- or with the
# variable set after project() -- has no provider and no sign of it: every
# find_package quietly falls back to whatever the machine happens to have,
# and the first library the machine has in a shape this project does not
# expect is what finally says something, about the library rather than
# about this.
if(NOT COMMAND cme_provider)
  message(FATAL_ERROR
    "cmake-everywhere is not installed in this build, so nothing answers "
    "find_package. CMAKE_PROJECT_TOP_LEVEL_INCLUDES is only read by the "
    "top-level project() call: configure this directory, or the repository "
    "root, and not a subdirectory of either.")
endif()

# Where the standard library's module metadata is, as a path.
#
# CMake asks the compiler for libc++.modules.json when it works out what the
# compiler can do, and records the answer -- which for a cross toolchain is
# the bare name, because that question was asked without this build's
# sysroot and target. The generator then cannot open it, and says so at the
# end of the configuration, about a file rather than about the question.
#
# The compiler answers correctly when it is asked properly, so it is.
if(CMAKE_CXX_STDLIB_MODULES_JSON
   AND NOT IS_ABSOLUTE "${CMAKE_CXX_STDLIB_MODULES_JSON}")
  set(osu_modules_json_flags "")
  if(CMAKE_CXX_COMPILER_TARGET)
    list(APPEND osu_modules_json_flags "--target=${CMAKE_CXX_COMPILER_TARGET}")
  endif()
  if(CMAKE_SYSROOT)
    list(APPEND osu_modules_json_flags "--sysroot=${CMAKE_SYSROOT}")
  endif()
  execute_process(
    COMMAND "${CMAKE_CXX_COMPILER}" ${osu_modules_json_flags}
            "-print-file-name=${CMAKE_CXX_STDLIB_MODULES_JSON}"
    OUTPUT_VARIABLE osu_modules_json OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET)
  if(EXISTS "${osu_modules_json}")
    set(CMAKE_CXX_STDLIB_MODULES_JSON "${osu_modules_json}")
    message(STATUS "import std: ${CMAKE_CXX_STDLIB_MODULES_JSON}")
  endif()
endif()

include(GNUInstallDirs)

# ---- Options ----

option(ENABLE_TEST_COVERAGE "Enable test coverage" OFF)
option(TEST_INSTALLED_VERSION "Test the version found by find_package" OFF)

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

# --- Import tools ----

include(../cmake/tools.cmake)

# ---- Dependencies ----

include(../cmake/CPM.cmake)

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

if(TEST_INSTALLED_VERSION)
  find_package(osucpp REQUIRED)
else()
  CPMAddPackage(NAME osucpp SOURCE_DIR ${CMAKE_CURRENT_LIST_DIR}/..)
endif()

# skiff, the UI framework: Skia wrapper, painter, scene graph, drawables.
# Everything the client draws with comes from here; nothing in there knows
# what a beatmap is. It lives in a repository of its own now, pinned to a
# commit -- moving to a newer skiff should be a visible change here.
# Half a declaration each: where they come from, which is what this project
# knows because this project chose them. What they are -- what they need,
# what they produce, what they are licensed under -- each says in its own
# tree, and that is read once the provider has fetched it.
#
# OSU_SKIFF_SOURCE_DIR and OSU_SKIFF_WIDGETS_SOURCE_DIR still name a checkout
# to build instead, for a build with no network.
set(OSU_SKIFF_SOURCE_DIR "" CACHE PATH
  "Existing skiff checkout; avoids fetching it during offline builds")
set(OSU_SKIFF_WIDGETS_SOURCE_DIR "" CACHE PATH
  "Existing skiff-widgets checkout; avoids fetching it during offline builds")

if(OSU_SKIFF_SOURCE_DIR)
  cme_declare_port(NAME skiff PROVIDES skiff
                   SOURCE_DIR "${OSU_SKIFF_SOURCE_DIR}")
else()
  cme_declare_port(
    NAME skiff
    PROVIDES skiff
    GITHUB_REPOSITORY j4niwzis/skiff
    GIT_TAG 4884907b6209aec96a03d5b1164c1374035e9082
  )
endif()
if(OSU_SKIFF_WIDGETS_SOURCE_DIR)
  cme_declare_port(NAME skiff-widgets PROVIDES skiff-widgets skiff_widgets
                   SOURCE_DIR "${OSU_SKIFF_WIDGETS_SOURCE_DIR}")
else()
  cme_declare_port(
    NAME skiff-widgets
    PROVIDES skiff-widgets skiff_widgets
    GITHUB_REPOSITORY j4niwzis/skiff-widgets
    GIT_TAG 065ff975ef99fe197b9b2b5d2762c060e91c5af2
  )
endif()

find_package(skiff-widgets REQUIRED)

# ---- Create binary ----

# Platform implementations are source overlays. For foo.cc, a matching
# foo_android.cc, foo_web.cc, foo_wasi.cc or foo_freedesktop.cc replaces the
# common file on that platform. A suffixed file may also stand alone when
# there is no common fallback. Keeping the choice here avoids platform ifdefs
# throughout the client and symlinks that packagers may copy incorrectly.
set(OSU_PLATFORM_VARIANT_PATTERN
  "android|web|wasi|freedesktop|windows|macos")
if(ANDROID)
  set(OSU_PLATFORM_SUFFIX android)
elseif(EMSCRIPTEN_BUILD)
  set(OSU_PLATFORM_SUFFIX web)
elseif(CMAKE_SYSTEM_NAME STREQUAL "WASI")
  set(OSU_PLATFORM_SUFFIX wasi)
elseif(UNIX AND NOT APPLE)
  # The conventional Wayland/X11, D-Bus and xdg ecosystem. This includes
  # glibc and musl desktops, Flatpak, Click and postmarketOS, not Android.
  set(OSU_PLATFORM_SUFFIX freedesktop)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows")
  set(OSU_PLATFORM_SUFFIX windows)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
  set(OSU_PLATFORM_SUFFIX macos)
else()
  # The unsuffixed source is the generic implementation.
  set(OSU_PLATFORM_SUFFIX "")
endif()

file(GLOB_RECURSE all_cpp_sources CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cc")
set(common_cpp_sources ${all_cpp_sources})
list(FILTER common_cpp_sources EXCLUDE REGEX
  "_(${OSU_PLATFORM_VARIANT_PATTERN})(_(${OSU_PLATFORM_VARIANT_PATTERN}))*\\.cc$")
# Implementation pieces live below platform/<variant>/. They are not public
# overlays and only the selected platform's directory enters the target.
list(FILTER common_cpp_sources EXCLUDE REGEX
  "/platform/(${OSU_PLATFORM_VARIANT_PATTERN})/")

set(selected_cpp_sources)
foreach(source IN LISTS common_cpp_sources)
  get_filename_component(source_dir "${source}" DIRECTORY)
  get_filename_component(source_name "${source}" NAME_WE)
  if(OSU_PLATFORM_SUFFIX)
    set(overlay "${source_dir}/${source_name}_${OSU_PLATFORM_SUFFIX}.cc")
  else()
    set(overlay "")
  endif()
  if(overlay AND EXISTS "${overlay}")
    list(APPEND selected_cpp_sources "${overlay}")
  else()
    list(APPEND selected_cpp_sources "${source}")
  endif()
endforeach()

# Also admit platform-only implementation files without a common foo.cc. A
# filename may list multiple alternatives, for example
# foo_freedesktop_web.cc, and is selected when any suffix matches.
foreach(source IN LISTS all_cpp_sources)
  if(OSU_PLATFORM_SUFFIX AND
     source MATCHES "_(${OSU_PLATFORM_VARIANT_PATTERN})(_(${OSU_PLATFORM_VARIANT_PATTERN}))*\\.cc$" AND
     source MATCHES "(^|_)${OSU_PLATFORM_SUFFIX}(_|\\.cc$)")
    list(APPEND selected_cpp_sources "${source}")
  endif()
endforeach()
list(REMOVE_DUPLICATES selected_cpp_sources)

if(OSU_PLATFORM_SUFFIX)
  file(GLOB_RECURSE platform_implementation_sources CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/${OSU_PLATFORM_SUFFIX}/*.cc")
  list(APPEND selected_cpp_sources ${platform_implementation_sources})
endif()

# The Vulkan presenter is a freedesktop overlay like any other, and it names
# Skia's Graphite backend: a build that did not ask for that backend has no
# such thing to name. Where it was not asked for, the unsuffixed file stands
# in -- the same calls, answering that there is no device here -- which is
# what keeps the client free of ifdefs about it.
if(NOT OSU_VULKAN)
  list(FILTER selected_cpp_sources EXCLUDE REGEX "/platform/vulkan_[^/]*\\.cc$")
  list(APPEND selected_cpp_sources
       "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/vulkan.cc")
  list(REMOVE_DUPLICATES selected_cpp_sources)
endif()

list(REMOVE_DUPLICATES selected_cpp_sources)

set(module_sources ${selected_cpp_sources})
if(EMSCRIPTEN_BUILD)
  # Browser fetch replaces the native Beast socket transport.
  list(FILTER module_sources EXCLUDE REGEX "beast\\.cc$")
endif()

set(main_source "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cc")
if(OSU_PLATFORM_SUFFIX)
  set(main_overlay
    "${CMAKE_CURRENT_SOURCE_DIR}/src/main_${OSU_PLATFORM_SUFFIX}.cc")
else()
  set(main_overlay "")
endif()
if(main_overlay AND EXISTS "${main_overlay}")
  set(main_source "${main_overlay}")
endif()
list(REMOVE_ITEM module_sources "${main_source}")

if(EMSCRIPTEN_BUILD)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fexceptions -pthread -msimd128")
endif()
if(ANDROID)
  set(OSU_ANDROID_NATIVE_APP_GLUE_DIR "" CACHE PATH
    "Directory containing android_native_app_glue.c")
  set(native_app_glue
    "${OSU_ANDROID_NATIVE_APP_GLUE_DIR}/android_native_app_glue.c")
  if(NOT EXISTS "${native_app_glue}")
    message(FATAL_ERROR
      "Set OSU_ANDROID_NATIVE_APP_GLUE_DIR to the native_app_glue sources")
  endif()
  add_library(${PROJECT_NAME} SHARED
    ${module_sources} ${main_source} "${native_app_glue}")
  target_include_directories(${PROJECT_NAME} PRIVATE
    "${OSU_ANDROID_NATIVE_APP_GLUE_DIR}")
  # The one function in this library that Android looks up by name.
  #
  # A build that hides its symbols by default hides this one too, and a
  # hidden symbol is not in the dynamic table at all -- no linker option
  # exports it back, because hidden is a statement about the table rather
  # than about what is kept. So the file that defines it is compiled as
  # visible; everything else this library is made of stays hidden, which is
  # the point.
  set_source_files_properties("${native_app_glue}" PROPERTIES
    COMPILE_OPTIONS "-fvisibility=default")
  target_link_options(${PROJECT_NAME} PRIVATE
    "LINKER:-u,ANativeActivity_onCreate"
    "LINKER:--no-undefined")
else()
  add_executable(${PROJECT_NAME} ${module_sources} ${main_source})
endif()

target_sources(${PROJECT_NAME} PUBLIC FILE_SET CXX_MODULES FILES ${module_sources})

# Only this project's own. Every library below arrives as a target that
# carries its own headers, which is the difference between a name and a
# path: a target cannot be found in the wrong place.
target_include_directories(${PROJECT_NAME} PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)

set_target_properties(${PROJECT_NAME} PROPERTIES CXX_STANDARD 23)

# Where the bundled assets end up, so the client can find them when installed
# and when run straight from the build tree.
target_compile_definitions(${PROJECT_NAME} PRIVATE
  OSU_CLIENT_DATADIR="${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_DATADIR}/osu_client"
  OSU_CLIENT_SOURCE_ASSETS="${CMAKE_CURRENT_SOURCE_DIR}/assets"
)

if(NOT EMSCRIPTEN_BUILD AND WAYLAND_CLIENT_FOUND)
  target_compile_definitions(${PROJECT_NAME} PRIVATE OSU_WAYLAND_TOUCH=1)
  target_include_directories(${PROJECT_NAME} PRIVATE
    ${WAYLAND_CLIENT_INCLUDE_DIRS}
  )
  # The touch bridge resolves GLFW's native Wayland accessors dynamically so
  # X11-only builds remain valid. When GLFW is linked statically those two
  # symbols need to be retained in the executable's dynamic symbol table.
  target_link_options(${PROJECT_NAME} PRIVATE
    "LINKER:--export-dynamic-symbol=glfwGetWaylandDisplay"
    "LINKER:--export-dynamic-symbol=glfwGetWaylandWindow"
  )
endif()

# ---- Link ----
#
# skiff as well as skiff::widgets, because this client imports skiff's own
# modules -- import skia, import scene -- and a target that imports a module
# links the library that provides it. Through the widgets alone it worked
# only while skiff was built here: a library taken from the store is an
# imported target, and CMake synthesises its module sources for the targets
# that link it, not for the targets that link those.

if(EMSCRIPTEN_BUILD)
  target_link_libraries(${PROJECT_NAME}
    osucpp::osucpp
    skiff::widgets
    skiff::skiff
    Skia::skia
    libzip::zip
    SndFile::sndfile
    MPG123::libmpg123
    LibLZMA::LibLZMA
    Boost::json
    openal
  )
elseif(ANDROID)
  find_library(CXX_LIBRARY NAMES c++ REQUIRED)
  find_library(CXXABI_LIBRARY NAMES c++abi REQUIRED)
  find_library(UNWIND_LIBRARY NAMES unwind REQUIRED)
  # The toolchain file may already have asked, in which case it knows better
  # than anything reconstructed here.
  if(NOT COMPILER_RT_LIBRARY AND MANDK_BUILTINS)
    set(COMPILER_RT_LIBRARY "${MANDK_BUILTINS}")
  endif()
  # Otherwise: which file clang would link its builtins from, asked of clang
  # with the flags that link, not only the flags that compile. -rtlib is a
  # link flag, and a clang built to default to libgcc -- which apt.llvm.org's
  # is -- answers this question about libgcc unless told otherwise, naming a
  # file no Android sysroot has. Not -print-resource-dir either: that answers
  # about the clang installation, and the runtime for this target was built
  # and put somewhere else by whoever made the toolchain.
  if(NOT COMPILER_RT_LIBRARY)
    separate_arguments(OSU_ANDROID_RT_FLAGS UNIX_COMMAND
      "${CMAKE_C_FLAGS} ${CMAKE_EXE_LINKER_FLAGS}")
    execute_process(
      COMMAND "${CMAKE_C_COMPILER}" ${OSU_ANDROID_RT_FLAGS}
              --target=${CMAKE_C_COMPILER_TARGET}
              --sysroot=${CMAKE_SYSROOT} -rtlib=compiler-rt
              -print-libgcc-file-name
      OUTPUT_VARIABLE OSU_ANDROID_RT_ANSWER
      OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    set(COMPILER_RT_LIBRARY "${OSU_ANDROID_RT_ANSWER}")
  endif()
  # The architecture is the one being built for, not the one this was
  # written on.
  set(OSU_ANDROID_RT_ARCH "${CMAKE_SYSTEM_PROCESSOR}")
  if(CMAKE_ANDROID_ARCH_ABI STREQUAL "arm64-v8a")
    set(OSU_ANDROID_RT_ARCH aarch64)
  elseif(CMAKE_ANDROID_ARCH_ABI STREQUAL "armeabi-v7a")
    set(OSU_ANDROID_RT_ARCH arm)
  elseif(CMAKE_ANDROID_ARCH_ABI)
    set(OSU_ANDROID_RT_ARCH "${CMAKE_ANDROID_ARCH_ABI}")
  endif()
  # And the old way, for a toolchain whose clang answers with something
  # that is not there: a resource directory and the name inside it.
  if(NOT COMPILER_RT_LIBRARY OR NOT EXISTS "${COMPILER_RT_LIBRARY}")
    set(COMPILER_RT_LIBRARY
      "${OSU_ANDROID_RESOURCE_DIR}/lib/linux/libclang_rt.builtins-${OSU_ANDROID_RT_ARCH}-android.a")
  endif()
  if(NOT EXISTS "${COMPILER_RT_LIBRARY}")
    message(FATAL_ERROR
      "Android compiler-rt not found: \"${COMPILER_RT_LIBRARY}\".\n"
      "  the toolchain file says MANDK_BUILTINS is \"${MANDK_BUILTINS}\"\n"
      "  ${CMAKE_C_COMPILER} answers -print-libgcc-file-name with "
      "\"${OSU_ANDROID_RT_ANSWER}\"\n"
      "  the name built from OSU_ANDROID_RESOURCE_DIR "
      "\"${OSU_ANDROID_RESOURCE_DIR}\" is what is left when both are empty\n"
      "Pass COMPILER_RT_LIBRARY to say it outright.")
  endif()
  target_link_libraries(${PROJECT_NAME}
    osucpp::osucpp
    skiff::widgets
    skiff::skiff
    Threads::Threads
    OpenAL::OpenAL
    Skia::skia
    libzip::zip
    SndFile::sndfile
    MPG123::libmpg123
    Boost::asio_core Boost::beast Boost::json
    ${CXX_LIBRARY} ${CXXABI_LIBRARY} ${UNWIND_LIBRARY}
    ${COMPILER_RT_LIBRARY}
    ${ANDROID_LIBRARY} ${NATIVEWINDOW_LIBRARY}
    ${EGL_LIBRARY} ${GLES_LIBRARY} ${LOG_LIBRARY}
    m dl c
  )
  include(../cmake/android-apk.cmake)
  osu_add_android_apk(${PROJECT_NAME})
else()
  target_link_libraries(${PROJECT_NAME}
    osucpp::osucpp
    skiff::widgets
    skiff::skiff
    Threads::Threads
    OpenGL::GL
    glfw::glfw
    ${WAYLAND_CLIENT_LDFLAGS} ${WAYLAND_CLIENT_LIBRARIES}
    OpenAL::OpenAL
    Skia::skia
    libzip::zip
    SndFile::sndfile
    MPG123::libmpg123
    Boost::asio_core Boost::beast Boost::json
    OpenSSL::SSL
    OpenSSL::Crypto
    # present.cc resolves EGL/GLX at run time rather than linking against
    # them, so that a machine without either still builds and runs.
    ${CMAKE_DL_LIBS}
  )
endif()

# The desktop's own file dialog, over D-Bus. Detected rather than required:
# without sd-bus the portal module compiles to "no portal here" and the
# client falls back to whatever dialog binary is installed. Inside a Flatpak
# there is no such binary and no such fallback, so this is what makes an
# import work there at all.
if(NOT EMSCRIPTEN_BUILD AND NOT ANDROID)
  # sd-bus comes from systemd, from basu (the same API unbundled, for systems
  # without systemd) or from elogind. Whichever is installed will do, and a
  # machine with none of them gets basu built from source -- it is the small
  # one, and the only reason to ask for the other two first is that they are
  # already there. Which one answered is not asked: the port arranges the
  # headers so that one spelling works for all three.
  find_package(basu)
  if(TARGET basu::basu)
    target_compile_definitions(${PROJECT_NAME} PRIVATE OSU_HAVE_SDBUS=1)
    target_link_libraries(${PROJECT_NAME} basu::basu)
    message(STATUS "file dialogs: xdg-desktop-portal over sd-bus")
  else()
    message(STATUS
      "file dialogs: no sd-bus (libsystemd, basu or elogind), "
      "falling back to zenity/kdialog")
  endif()
endif()

# Video export through libavcodec rather than through an ffmpeg process. Off
# by default because it is a build dependency for a feature most builds do not
# use; on, it is the only way to export from a build that cannot spawn one --
# a browser, or anywhere ffmpeg is not installed.
option(OSU_VIDEO_LIBAV "Encode exported video with libavcodec" OFF)
if(OSU_VIDEO_LIBAV)
  target_compile_definitions(${PROJECT_NAME} PRIVATE OSU_VIDEO_LIBAV=1)
  # By what is used of it: H.264 into an MP4, and the raw frames the encoder
  # is fed. Asking for the codecs by name is what makes them be there -- an
  # FFmpeg built with none of them links and encodes nothing.
  #
  # H.264 through libx264, which is GPL: this program is under the AGPL, so
  # that is a licence it can carry. What it buys is a file anything will
  # play at a size anything will accept -- MPEG-4 part 2 is neither, and it
  # stays as what the encoder falls back to where x264 was not built.
  find_package(FFmpeg REQUIRED COMPONENTS x264 mpeg4 rawvideo mp4)
  target_link_libraries(${PROJECT_NAME}
    FFmpeg::avcodec FFmpeg::avformat FFmpeg::avutil
    FFmpeg::swscale FFmpeg::swresample)
endif()

# Debug information for the module libraries, which clang 22 cannot always
# produce.
#
# Compiling skiff's text node with -g crashes the frontend outright --
# "clang frontend command failed with exit code 139", inside a module
# interface unit. The information is worth having when it can be had, so
# this is a switch rather than a removal, and it names what it is for.
option(OSU_MODULE_DEBUG_INFO
  "Compile this project's module libraries with debug information" OFF)
if(NOT OSU_MODULE_DEBUG_INFO)
  foreach(osu_module_target skiff skiff-widgets osucpp ${PROJECT_NAME})
    if(NOT TARGET ${osu_module_target})
      continue()
    endif()
    # A library that came from the store or from the system is not compiled
    # by this build, and how it was compiled is not this build's to change:
    # what is here is an imported target, or an alias for one, and neither
    # takes compile options. Only the ones this build builds are told
    # anything.
    get_target_property(osu_alias ${osu_module_target} ALIASED_TARGET)
    if(osu_alias)
      set(osu_module_target "${osu_alias}")
    endif()
    get_target_property(osu_imported ${osu_module_target} IMPORTED)
    if(osu_imported)
      continue()
    endif()
    target_compile_options(${osu_module_target} PRIVATE
      $<$<COMPILE_LANGUAGE:CXX>:-g0>)
  endforeach()
endif()

# ---- Emscripten-specific options ----

if(EMSCRIPTEN_BUILD)
  set(WASM_PRELOAD_DATA "" CACHE PATH "Directory to preload into emscripten virtual filesystem")

  target_link_options(${PROJECT_NAME} PRIVATE
    "SHELL:-sUSE_GLFW=3"
    "SHELL:-sMAX_WEBGL_VERSION=2"
    "SHELL:-sMIN_WEBGL_VERSION=2"
    "SHELL:-sGL_ASSERTIONS=0"
    "SHELL:-sINITIAL_MEMORY=536870912"
    "SHELL:-sSTACK_SIZE=5MB"
    "SHELL:-sENVIRONMENT=web"
    "SHELL:-pthread"
    "SHELL:-msimd128"
    # ccall among them: the page hands the client the path of an archive it
    # has just written, and a string argument is what ccall is for.
    # getExceptionMessage among them: asking for the helpers by their own
    # setting left them out of the build, and the page that asks what was
    # thrown got nothing. A runtime method that is named is a runtime method
    # that is there.
    "SHELL:-sEXPORTED_RUNTIME_METHODS=['GL','callMain','FS','ccall','getExceptionMessage']"
    "SHELL:-sFETCH"
    "SHELL:-lidbfs.js"
    "SHELL:--shell-file ${CMAKE_CURRENT_SOURCE_DIR}/shell.html"
  )

  if(WASM_PRELOAD_DATA)
    target_link_options(${PROJECT_NAME} PRIVATE
      "SHELL:--preload-file ${WASM_PRELOAD_DATA}@/"
    )
  endif()

  set_target_properties(${PROJECT_NAME} PROPERTIES SUFFIX ".html")
endif()

# ---- Install rules ----


if(ANDROID)
  install(TARGETS ${PROJECT_NAME}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})
else()
  install(TARGETS ${PROJECT_NAME}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()

# Every face the client draws with travels with it: the UI font, the CJK
# fallbacks, the icon font and the display face for judgements, each under
# SIL OFL 1.1 (the licences sit beside them). Nothing is taken from the
# system, and any extra font dropped in here joins the fallback chain.
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/
  DESTINATION ${CMAKE_INSTALL_DATADIR}/osu_client/fonts
  FILES_MATCHING
    PATTERN "*.ttf" PATTERN "*.otf" PATTERN "*.ttc"
    PATTERN "OFL*.txt" PATTERN "LICENSE*.txt"
)

# Exact upstream legal text displayed before the optional artwork download.
install(FILES
  ${CMAKE_CURRENT_SOURCE_DIR}/assets/licenses/WebOsu-CC-BY-NC-4.0.md
  DESTINATION ${CMAKE_INSTALL_DATADIR}/osu_client/licenses
)

if(NOT EMSCRIPTEN_BUILD AND NOT ANDROID)
  install(FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/packaging/io.github.j4niwzis.osu_cpp.desktop
    DESTINATION ${CMAKE_INSTALL_DATADIR}/applications
  )
  install(FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/packaging/io.github.j4niwzis.osu_cpp.metainfo.xml
    DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo
  )
  install(FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/packaging/io.github.j4niwzis.osu_cpp.svg
    DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps
  )
  install(FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/packaging/io.github.j4niwzis.osu_cpp.xml
    DESTINATION ${CMAKE_INSTALL_DATADIR}/mime/packages
  )
endif()

# ---- Add osu_clientTests ----

enable_testing()

# ---- code coverage ----

if(ENABLE_TEST_COVERAGE)
  target_compile_options(osu_client INTERFACE -O0 -g -fprofile-arcs -ftest-coverage)
  target_link_options(osu_client INTERFACE -fprofile-arcs -ftest-coverage)
endif()
