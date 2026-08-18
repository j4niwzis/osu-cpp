cmake_minimum_required(VERSION 4.1.1 FATAL_ERROR)

set(CMAKE_EXPERIMENTAL_CXX_IMPORT_STD "451f2fe2-a8a2-47c3-bc32-94786d8fc91b")
set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_MODULE_STD ON)

project(osu_client LANGUAGES CXX)

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

if(EMSCRIPTEN_BUILD)
  pkg_check_modules(SKIA   REQUIRED skia)
  pkg_check_modules(LIBZIP  REQUIRED libzip)
  pkg_check_modules(SNDFILE REQUIRED sndfile)
  pkg_check_modules(MPG123  REQUIRED libmpg123)
  pkg_check_modules(LIBLZMA REQUIRED liblzma)
else()
  find_package(OpenGL REQUIRED)
  find_package(Threads REQUIRED)
  pkg_check_modules(GLFW    REQUIRED glfw3)
  pkg_check_modules(OPENAL  REQUIRED openal)
  pkg_check_modules(SKIA    REQUIRED skia)
  pkg_check_modules(LIBZIP  REQUIRED libzip)
  pkg_check_modules(SNDFILE REQUIRED sndfile)
  pkg_check_modules(MPG123  REQUIRED libmpg123)
endif()

if(TEST_INSTALLED_VERSION)
  find_package(osucpp REQUIRED)
else()
  CPMAddPackage(NAME osucpp SOURCE_DIR ${CMAKE_CURRENT_LIST_DIR}/..)
endif()

# ---- Create binary ----

file(GLOB_RECURSE module_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cc")

if(NOT EMSCRIPTEN_BUILD)
  list(FILTER module_sources EXCLUDE REGEX "emscripten\\.cc$")
endif()

list(REMOVE_ITEM module_sources "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cc")

if(EMSCRIPTEN_BUILD)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fexceptions -pthread -msimd128")
endif()
add_executable(${PROJECT_NAME} ${module_sources} ${CMAKE_CURRENT_SOURCE_DIR}/src/main.cc)

target_sources(${PROJECT_NAME} PUBLIC FILE_SET CXX_MODULES FILES ${module_sources})

target_include_directories(${PROJECT_NAME} PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${SKIA_INCLUDE_DIRS}
  ${LIBZIP_INCLUDE_DIRS}
  ${SNDFILE_INCLUDE_DIRS}
  ${MPG123_INCLUDE_DIRS}
)

set_target_properties(${PROJECT_NAME} PROPERTIES CXX_STANDARD 26)

# ---- Link ----

if(EMSCRIPTEN_BUILD)
  target_link_libraries(${PROJECT_NAME}
    osucpp::osucpp
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
    Threads::Threads
    OpenGL::GL
    ${GLFW_LDFLAGS} ${GLFW_LIBRARIES}
    ${OPENAL_LDFLAGS} ${OPENAL_LIBRARIES}
    ${SKIA_LDFLAGS} ${SKIA_LIBRARIES}
    ${LIBZIP_LDFLAGS} ${LIBZIP_LIBRARIES}
    ${SNDFILE_LDFLAGS} ${SNDFILE_LIBRARIES}
    ${MPG123_LDFLAGS} ${MPG123_LIBRARIES}
  )
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

include(GNUInstallDirs)

install(TARGETS ${PROJECT_NAME}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# ---- Add osu_clientTests ----

enable_testing()

# ---- code coverage ----

if(ENABLE_TEST_COVERAGE)
  target_compile_options(osu_client INTERFACE -O0 -g -fprofile-arcs -ftest-coverage)
  target_link_options(osu_client INTERFACE -fprofile-arcs -ftest-coverage)
endif()
