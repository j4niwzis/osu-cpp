cmake_minimum_required(VERSION 4.3.4 FATAL_ERROR)

include(${CMAKE_CURRENT_LIST_DIR}/../cmake/import-std.cmake)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_EXTENSIONS OFF)

project(osu_client LANGUAGES CXX)

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

# Use pkg-config's private dependency closure when linking bundled static
# libraries. This is opt-in so native distro builds retain their normal shared
# dependency selection.
option(OSU_STATIC_DEPS "Link pkg-config dependencies statically" OFF)

# Boost is header-only here. Prefer its CMake package when available, but also
# support installations which contain only the assembled Boost include tree
# (notably the Flatpak build and CMake versions without FindBoost).
find_package(Boost QUIET)
if(NOT Boost_FOUND)
  find_path(Boost_INCLUDE_DIRS NAMES boost/asio.hpp REQUIRED)
endif()

if(EMSCRIPTEN_BUILD)
  pkg_check_modules(SKIA   REQUIRED skia)
  pkg_check_modules(LIBZIP  REQUIRED libzip)
  pkg_check_modules(SNDFILE REQUIRED sndfile)
  pkg_check_modules(MPG123  REQUIRED libmpg123)
  pkg_check_modules(LIBLZMA REQUIRED liblzma)
else()
  find_package(OpenGL REQUIRED)
  find_package(Threads REQUIRED)
  # OpenSSL 3.0 and later is Apache-2.0, which GPLv3 and so AGPLv3 accept.
  # Everything before it carried the old OpenSSL/SSLeay licence, which does
  # not combine with the AGPL without an explicit linking exception -- and
  # this project would rather set a version floor than write one. Asio's TLS
  # has no other backend, so this is the whole of that decision.
  find_package(OpenSSL 3.0 REQUIRED)
  pkg_check_modules(GLFW    REQUIRED glfw3)
  # GLFW has no touch API.  When Wayland development files are available a
  # small native bridge turns the first contact into the pointer events the
  # rest of the client already understands.  It stays optional so X11 and
  # non-Linux builds retain exactly their existing dependency set.
  if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    pkg_check_modules(WAYLAND_CLIENT wayland-client)
  endif()
  pkg_check_modules(OPENAL  REQUIRED openal)
  pkg_check_modules(SKIA    REQUIRED skia)
  pkg_check_modules(LIBZIP  REQUIRED libzip)
  pkg_check_modules(SNDFILE REQUIRED sndfile)
  pkg_check_modules(MPG123  REQUIRED libmpg123)
endif()

if(OSU_STATIC_DEPS)
  foreach(dependency GLFW OPENAL SKIA LIBZIP SNDFILE MPG123)
    if(DEFINED ${dependency}_STATIC_LDFLAGS)
      set(${dependency}_LDFLAGS ${${dependency}_STATIC_LDFLAGS})
      set(${dependency}_LIBRARIES)
      set(${dependency}_INCLUDE_DIRS ${${dependency}_STATIC_INCLUDE_DIRS})
    endif()
  endforeach()
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
    GIT_TAG 3ef9effb268ab924c3cb3510a5849db116d7c6f2
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
    GIT_TAG 57392eb2456339910c21e6f701a7214c5615296d
  )
endif()

# ---- Create binary ----

file(GLOB_RECURSE module_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cc")

if(NOT EMSCRIPTEN_BUILD)
  list(FILTER module_sources EXCLUDE REGEX "emscripten\\.cc$")
else()
  # No sockets in the browser: the beast wrapper is native-only.
  list(FILTER module_sources EXCLUDE REGEX "beast\\.cc$")
endif()

list(REMOVE_ITEM module_sources "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cc")

if(EMSCRIPTEN_BUILD)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fexceptions -pthread -msimd128")
endif()
add_executable(${PROJECT_NAME} ${module_sources} ${CMAKE_CURRENT_SOURCE_DIR}/src/main.cc)

target_sources(${PROJECT_NAME} PUBLIC FILE_SET CXX_MODULES FILES ${module_sources})

if(OSU_STATIC_DEPS)
  target_compile_options(${PROJECT_NAME} PRIVATE
    ${GLFW_STATIC_CFLAGS_OTHER}
    ${OPENAL_STATIC_CFLAGS_OTHER}
    ${SKIA_STATIC_CFLAGS_OTHER}
    ${LIBZIP_STATIC_CFLAGS_OTHER}
    ${SNDFILE_STATIC_CFLAGS_OTHER}
    ${MPG123_STATIC_CFLAGS_OTHER}
  )
endif()

target_include_directories(${PROJECT_NAME} PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${SKIA_INCLUDE_DIRS}
  ${Boost_INCLUDE_DIRS}
  ${LIBZIP_INCLUDE_DIRS}
  ${SNDFILE_INCLUDE_DIRS}
  ${MPG123_INCLUDE_DIRS}
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
    ${SKIA_LDFLAGS} ${SKIA_LIBRARIES}
    ${LIBZIP_LDFLAGS} ${LIBZIP_LIBRARIES}
    ${SNDFILE_LDFLAGS} ${SNDFILE_LIBRARIES}
    ${MPG123_LDFLAGS} ${MPG123_LIBRARIES}
    ${LIBLZMA_LDFLAGS} ${LIBLZMA_LIBRARIES}
    openal
  )
else()
  target_link_libraries(${PROJECT_NAME}
    osucpp::osucpp
    skiff::widgets
    Threads::Threads
    OpenGL::GL
    ${GLFW_LDFLAGS} ${GLFW_LIBRARIES}
    ${WAYLAND_CLIENT_LDFLAGS} ${WAYLAND_CLIENT_LIBRARIES}
    ${OPENAL_LDFLAGS} ${OPENAL_LIBRARIES}
    ${SKIA_LDFLAGS} ${SKIA_LIBRARIES}
    ${LIBZIP_LDFLAGS} ${LIBZIP_LIBRARIES}
    ${SNDFILE_LDFLAGS} ${SNDFILE_LIBRARIES}
    ${MPG123_LDFLAGS} ${MPG123_LIBRARIES}
    OpenSSL::SSL
    OpenSSL::Crypto
    # present.cc resolves EGL/GLX at run time rather than linking against
    # them, so that a machine without either still builds and runs.
    ${CMAKE_DL_LIBS}
  )
endif()

# The desktop's own file dialog, over D-Bus. Detected rather than required:
# without libsystemd the portal module compiles to "no portal here" and the
# client falls back to whatever dialog binary is installed. Inside a Flatpak
# there is no such binary and no such fallback, so this is what makes an
# import work there at all.
if(NOT EMSCRIPTEN_BUILD)
  # sd-bus comes from systemd, from basu (the same API unbundled, for systems
  # without systemd) or from elogind. Whichever is installed will do.
  pkg_check_modules(SDBUS IMPORTED_TARGET libsystemd)
  if(SDBUS_FOUND)
    set(OSU_SDBUS_NAME libsystemd)
  endif()
  if(NOT SDBUS_FOUND)
    pkg_check_modules(SDBUS IMPORTED_TARGET basu)
    if(SDBUS_FOUND)
      set(OSU_SDBUS_FLAVOUR OSU_SDBUS_BASU)
      set(OSU_SDBUS_NAME basu)
    endif()
  endif()
  if(NOT SDBUS_FOUND)
    pkg_check_modules(SDBUS IMPORTED_TARGET libelogind)
    if(SDBUS_FOUND)
      set(OSU_SDBUS_FLAVOUR OSU_SDBUS_ELOGIND)
      set(OSU_SDBUS_NAME libelogind)
    endif()
  endif()
  if(SDBUS_FOUND)
    target_compile_definitions(${PROJECT_NAME} PRIVATE OSU_HAVE_SDBUS=1)
    if(OSU_SDBUS_FLAVOUR)
      target_compile_definitions(${PROJECT_NAME} PRIVATE ${OSU_SDBUS_FLAVOUR}=1)
    endif()
    target_link_libraries(${PROJECT_NAME} PkgConfig::SDBUS)
    message(STATUS "file dialogs: xdg-desktop-portal via ${OSU_SDBUS_NAME}")
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
  if(OSU_STATIC_DEPS)
    pkg_check_modules(LIBAV REQUIRED
      libavcodec libavformat libavutil libswresample libswscale)
    target_include_directories(${PROJECT_NAME} PRIVATE
      ${LIBAV_STATIC_INCLUDE_DIRS})
    target_compile_options(${PROJECT_NAME} PRIVATE
      ${LIBAV_STATIC_CFLAGS_OTHER})
    target_link_libraries(${PROJECT_NAME} ${LIBAV_STATIC_LDFLAGS})
  else()
    pkg_check_modules(LIBAV REQUIRED IMPORTED_TARGET
      libavcodec libavformat libavutil libswresample libswscale)
    target_link_libraries(${PROJECT_NAME} PkgConfig::LIBAV)
  endif()
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


install(TARGETS ${PROJECT_NAME}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

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

if(NOT EMSCRIPTEN_BUILD)
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
