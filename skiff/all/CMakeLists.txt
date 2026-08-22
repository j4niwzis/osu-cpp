cmake_minimum_required(VERSION 4.3.4 FATAL_ERROR)

include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/import-std.cmake)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_EXTENSIONS OFF)

project(SkiffBuildAll LANGUAGES CXX)

include(../../cmake/tools.cmake)

enable_testing()
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../test
                 ${CMAKE_BINARY_DIR}/test)
