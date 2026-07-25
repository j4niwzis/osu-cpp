cmake_minimum_required(VERSION 4.1.1 FATAL_ERROR)

set(CMAKE_EXPERIMENTAL_CXX_IMPORT_STD
  # This specific value changes as experimental support evolves. See
  # `Help/dev/experimental.rst` in the CMake source corresponding to
  # your CMake build for the exact value to use.
  "451f2fe2-a8a2-47c3-bc32-94786d8fc91b")

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_MODULE_STD ON)

project(osu_client LANGUAGES CXX)

# ---- Options ----

option(ENABLE_TEST_COVERAGE "Enable test coverage" OFF)
option(TEST_INSTALLED_VERSION "Test the version found by find_package" OFF)

# --- Import tools ----

include(../cmake/tools.cmake)

# ---- Dependencies ----

include(../cmake/CPM.cmake)

find_package(OpenGL REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(GLFW REQUIRED glfw3)
pkg_check_modules(OPENAL REQUIRED openal)
pkg_check_modules(SNDFILE REQUIRED sndfile)
pkg_check_modules(MPG123 REQUIRED libmpg123)
pkg_check_modules(LIBZIP REQUIRED libzip)

if(TEST_INSTALLED_VERSION)
  find_package(osucpp REQUIRED)
else()
  CPMAddPackage(NAME osucpp SOURCE_DIR ${CMAKE_CURRENT_LIST_DIR}/..)
endif()

# ---- Create binary ----

file(GLOB_RECURSE module_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cc")
list(REMOVE_ITEM module_sources "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cc")
add_executable(${PROJECT_NAME} ${module_sources} ${CMAKE_CURRENT_SOURCE_DIR}/src/main.cc)


target_sources(${PROJECT_NAME} PUBLIC FILE_SET CXX_MODULES FILES ${module_sources})
target_link_libraries(${PROJECT_NAME}
  osucpp::osucpp
  OpenGL::GL
  skia
  glfw
  openal
  sndfile
  mpg123
  ${LIBZIP_LIBRARIES}
)
target_include_directories(${PROJECT_NAME} PRIVATE
  /usr/include/skia
  ${LIBZIP_INCLUDE_DIRS}
)
set_target_properties(${PROJECT_NAME} PROPERTIES CXX_STANDARD 26)

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
