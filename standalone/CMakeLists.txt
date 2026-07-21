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

if(TEST_INSTALLED_VERSION)
  find_package(osucpp REQUIRED)
else()
  CPMAddPackage(NAME osucpp SOURCE_DIR ${CMAKE_CURRENT_LIST_DIR}/..)
endif()

# ---- Create binary ----

set(module_sources
  ${CMAKE_CURRENT_SOURCE_DIR}/src/glfw.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/src/skia.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/src/audio.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/src/skin.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/src/app.cc
)
set(main_source ${CMAKE_CURRENT_SOURCE_DIR}/src/main.cc)
add_executable(${PROJECT_NAME} ${module_sources} ${main_source})

# Copy bundled skin next to the executable so the default --skin path resolves.
set(default_skin_source ${CMAKE_CURRENT_LIST_DIR}/../skin)
set(default_skin_dest ${CMAKE_CURRENT_BINARY_DIR}/../skin)
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          ${default_skin_source} ${default_skin_dest})
target_sources(${PROJECT_NAME} PUBLIC FILE_SET CXX_MODULES FILES ${module_sources})
target_link_libraries(${PROJECT_NAME}
  osucpp::osucpp
  OpenGL::GL
  skia
  glfw
  openal
  sndfile
  mpg123
)
target_include_directories(${PROJECT_NAME} PRIVATE
  /usr/include/skia
)
set_target_properties(${PROJECT_NAME} PROPERTIES CXX_STANDARD 26)

# ---- Install rules ----

include(GNUInstallDirs)

install(TARGETS ${PROJECT_NAME}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(DIRECTORY ${default_skin_source}
  DESTINATION ${CMAKE_INSTALL_DATADIR}/osu_client
)

# ---- Add osu_clientTests ----

enable_testing()

add_test(${PROJECT_NAME} ${PROJECT_NAME} --headless "${CMAKE_CURRENT_LIST_DIR}/../sample_beatmap/Icon For Hire - Make a Move (Speed Up Ver.) (Sotarks) [Akitoshi's Normal].osu")

# ---- code coverage ----

if(ENABLE_TEST_COVERAGE)
  target_compile_options(osu_client INTERFACE -O0 -g -fprofile-arcs -ftest-coverage)
  target_link_options(osu_client INTERFACE -fprofile-arcs -ftest-coverage)
endif()
