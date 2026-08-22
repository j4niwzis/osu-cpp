cmake_minimum_required(VERSION 4.3.4 FATAL_ERROR)

if(CMAKE_VERSION VERSION_GREATER_EQUAL 4.4)
  set(CMAKE_EXPERIMENTAL_CXX_IMPORT_STD
    "f35a9ac6-8463-4d38-8eec-5d6008153e7d")
else()
  set(CMAKE_EXPERIMENTAL_CXX_IMPORT_STD
    "451f2fe2-a8a2-47c3-bc32-94786d8fc91b")
endif()
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_MODULE_STD ON)

project(skiffTests LANGUAGES CXX)

option(ENABLE_TEST_COVERAGE "Enable test coverage" OFF)
option(TEST_INSTALLED_VERSION "Test the installed skiff package" OFF)

include(../../cmake/tools.cmake)
include(../../cmake/CPM.cmake)

CPMAddPackage(
  NAME googletest_modules
  GITHUB_REPOSITORY j4niwzis/googletest-modules
  GIT_TAG 706920f1d230f0316d271466d8885735be3e300e
  OPTIONS
    "BUILD_GMOCK OFF"
    "gtest_build_modules ON"
    "gtest_build_tests OFF"
    "gtest_build_samples OFF"
)

if(TEST_INSTALLED_VERSION)
  find_package(skiff REQUIRED)
else()
  CPMAddPackage(NAME skiff SOURCE_DIR ${CMAKE_CURRENT_LIST_DIR}/..)
endif()

add_executable(${PROJECT_NAME} ${CMAKE_CURRENT_SOURCE_DIR}/src/style.cc)

target_link_libraries(${PROJECT_NAME} PRIVATE
  gtest
  gtest_main
  skiff::skiff)
set_target_properties(${PROJECT_NAME} PROPERTIES
  CXX_STANDARD 23
  CXX_SCAN_FOR_MODULES ON)

enable_testing()
add_test(NAME ${PROJECT_NAME} COMMAND ${PROJECT_NAME})

if(ENABLE_TEST_COVERAGE)
  target_compile_options(skiff PRIVATE -O0 -g -fprofile-arcs -ftest-coverage)
  target_link_options(skiff PRIVATE -fprofile-arcs -ftest-coverage)
endif()
