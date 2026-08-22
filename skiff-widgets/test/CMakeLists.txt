cmake_minimum_required(VERSION 4.3.4 FATAL_ERROR)

include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/import-std.cmake)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_EXTENSIONS OFF)

project(skiffWidgetsTests LANGUAGES CXX)

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

if(NOT TARGET skiff::widgets)
  CPMAddPackage(NAME skiff_widgets SOURCE_DIR ${CMAKE_CURRENT_LIST_DIR}/..)
endif()

add_executable(${PROJECT_NAME} ${CMAKE_CURRENT_SOURCE_DIR}/src/widgets.cc)
target_link_libraries(${PROJECT_NAME} PRIVATE
  gtest
  gtest_main
  skiff::widgets)
set_target_properties(${PROJECT_NAME} PROPERTIES
  CXX_STANDARD 23
  CXX_EXTENSIONS OFF
  CXX_SCAN_FOR_MODULES ON)

enable_testing()
add_test(NAME ${PROJECT_NAME} COMMAND ${PROJECT_NAME})
