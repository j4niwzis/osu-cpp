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

# Boost by the parts that are used. asio for the client's sockets and its
# TLS, beast for the HTTP on top of them, json for what comes back -- and
# nothing else of Boost is fetched or built, because a component that is not
# asked for is a library that is not there.
find_package(Boost REQUIRED COMPONENTS asio beast json)

# egl beside gl, because how Skia reaches GL is decided when Skia is built:
# with neither egl nor x11 its own build assembles no GL interface at all
# and hands back nothing at run time. EGL is the one that works under
# Wayland, and under X11 with OSU_EGL=1; a GLX build would be x11 instead,
# and then the context has to be a GLX one.
find_package(Skia REQUIRED COMPONENTS gl egl png jpeg freetype)
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
  if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
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
set(OSU_SKIFF_SOURCE_DIR "" CACHE PATH
  "Existing skiff checkout; avoids fetching it during offline builds")
if(OSU_SKIFF_SOURCE_DIR)
  CPMAddPackage(NAME skiff SOURCE_DIR "${OSU_SKIFF_SOURCE_DIR}")
else()
  CPMAddPackage(
    NAME skiff
    GITHUB_REPOSITORY j4niwzis/skiff
    GIT_TAG fa5c97be3009737020c114221e4ccb2867f631f1
  )
endif()

# The widgets that sit on it, one module each. Pulls skiff in behind it.
set(OSU_SKIFF_WIDGETS_SOURCE_DIR "" CACHE PATH
  "Existing skiff-widgets checkout; avoids fetching it during offline builds")
if(OSU_SKIFF_WIDGETS_SOURCE_DIR)
  CPMAddPackage(NAME skiff_widgets
    SOURCE_DIR "${OSU_SKIFF_WIDGETS_SOURCE_DIR}")
else()
  CPMAddPackage(
    NAME skiff_widgets
    GITHUB_REPOSITORY j4niwzis/skiff-widgets
    GIT_TAG 065ff975ef99fe197b9b2b5d2762c060e91c5af2
  )
endif()

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

if(EMSCRIPTEN_BUILD)
  target_link_libraries(${PROJECT_NAME}
    osucpp::osucpp
    skiff::widgets
    Skia::skia
    libzip::zip
    SndFile::sndfile
    MPG123::libmpg123
    LibLZMA::LibLZMA
    Boost::asio Boost::beast Boost::json
    openal
  )
elseif(ANDROID)
  find_library(CXX_LIBRARY NAMES c++ REQUIRED)
  find_library(CXXABI_LIBRARY NAMES c++abi REQUIRED)
  find_library(UNWIND_LIBRARY NAMES unwind REQUIRED)
  # Which file clang would link its builtins from, asked of clang with the
  # flags this build compiles with. Not -print-resource-dir: that answers
  # about the clang installation -- /usr/lib/llvm22 -- and the runtime for
  # this target was built and put somewhere else entirely by whoever made
  # the toolchain. -print-libgcc-file-name answers about this target.
  if(NOT COMPILER_RT_LIBRARY)
    separate_arguments(OSU_ANDROID_RT_FLAGS UNIX_COMMAND "${CMAKE_C_FLAGS}")
    execute_process(
      COMMAND "${CMAKE_C_COMPILER}" ${OSU_ANDROID_RT_FLAGS}
              --target=${CMAKE_C_COMPILER_TARGET}
              --sysroot=${CMAKE_SYSROOT} -print-libgcc-file-name
      OUTPUT_VARIABLE COMPILER_RT_LIBRARY
      OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
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
      "Android compiler-rt not found: ${COMPILER_RT_LIBRARY}. That path is "
      "what ${CMAKE_C_COMPILER} answers -print-libgcc-file-name with, for "
      "this target and sysroot. Pass COMPILER_RT_LIBRARY, or "
      "OSU_ANDROID_RESOURCE_DIR to build the name the old way.")
  endif()
  target_link_libraries(${PROJECT_NAME}
    osucpp::osucpp
    skiff::widgets
    Threads::Threads
    OpenAL::OpenAL
    Skia::skia
    libzip::zip
    SndFile::sndfile
    MPG123::libmpg123
    Boost::asio Boost::beast Boost::json
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
    Threads::Threads
    OpenGL::GL
    glfw::glfw
    ${WAYLAND_CLIENT_LDFLAGS} ${WAYLAND_CLIENT_LIBRARIES}
    OpenAL::OpenAL
    Skia::skia
    libzip::zip
    SndFile::sndfile
    MPG123::libmpg123
    Boost::asio Boost::beast Boost::json
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
  # By what is used of it: MPEG-4 into an MP4, and the raw frames the
  # encoder is fed. Asking for the codecs by name is what makes them be
  # there -- an FFmpeg built with none of them links and encodes nothing.
  find_package(FFmpeg REQUIRED COMPONENTS mpeg4 rawvideo mp4)
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
    if(TARGET ${osu_module_target})
      target_compile_options(${osu_module_target} PRIVATE
        $<$<COMPILE_LANGUAGE:CXX>:-g0>)
    endif()
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
    "SHELL:-sEXPORTED_RUNTIME_METHODS=['GL','callMain','FS']"
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
