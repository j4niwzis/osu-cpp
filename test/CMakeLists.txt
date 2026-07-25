cmake_minimum_required(VERSION 4.3.3 FATAL_ERROR)

set(CMAKE_EXPERIMENTAL_CXX_IMPORT_STD
  # This specific value changes as experimental support evolves. See
  # `Help/dev/experimental.rst` in the CMake source corresponding to
  # your CMake build for the exact value to use.
  "451f2fe2-a8a2-47c3-bc32-94786d8fc91b")

set(CMAKE_CXX_MODULE_STD ON)

project(osucppTests LANGUAGES CXX)

# ---- Options ----

option(ENABLE_TEST_COVERAGE "Enable test coverage" OFF)
option(TEST_INSTALLED_VERSION "Test the version found by find_package" OFF)

# --- Import tools ----

include(../cmake/tools.cmake)

# ---- Dependencies ----

include(../cmake/CPM.cmake)

CPMAddPackage(
  NAME googletest
  GITHUB_REPOSITORY google/googletest
  GIT_TAG release-1.12.1
  VERSION 1.12.1
  OPTIONS "INSTALL_GTEST OFF" "gtest_force_shared_crt"
)
CPMAddPackage("gh:TheLartians/Format.cmake@1.7.3")

if(TEST_INSTALLED_VERSION)
  find_package(osucpp REQUIRED)
else()
  CPMAddPackage(NAME osucpp SOURCE_DIR ${CMAKE_CURRENT_LIST_DIR}/..)
endif()

# ---- Create binary ----

file(GLOB sources CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cc)
add_executable(${PROJECT_NAME} ${sources})

target_include_directories(${PROJECT_NAME} PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(${PROJECT_NAME} gtest gtest_main osucpp::osucpp)
set_target_properties(${PROJECT_NAME} PROPERTIES CXX_STANDARD 26)
set_target_properties(${PROJECT_NAME} PROPERTIES CXX_SCAN_FOR_MODULES ON)

# gtest module
set(gtest_module_source ${CMAKE_CURRENT_SOURCE_DIR}/src/gtest.cc)
if(EXISTS ${gtest_module_source})
  target_sources(${PROJECT_NAME} PRIVATE
    FILE_SET CXX_MODULES
    FILES ${gtest_module_source}
  )
endif()

# ---- Add osucppTests ----

enable_testing()

add_test(NAME ${PROJECT_NAME} COMMAND ${PROJECT_NAME} WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/..)

# ---- code coverage ----

if(ENABLE_TEST_COVERAGE)
  target_compile_options(osucpp INTERFACE -O0 -g -fprofile-arcs -ftest-coverage)
  target_link_options(osucpp INTERFACE -fprofile-arcs -ftest-coverage)
endif()
