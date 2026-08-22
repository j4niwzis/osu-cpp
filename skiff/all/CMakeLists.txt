cmake_minimum_required(VERSION 4.3.4 FATAL_ERROR)

if(CMAKE_VERSION VERSION_GREATER_EQUAL 4.4)
  set(CMAKE_EXPERIMENTAL_CXX_IMPORT_STD
    "f35a9ac6-8463-4d38-8eec-5d6008153e7d")
else()
  set(CMAKE_EXPERIMENTAL_CXX_IMPORT_STD
    "451f2fe2-a8a2-47c3-bc32-94786d8fc91b")
endif()
set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_MODULE_STD ON)

project(SkiffBuildAll LANGUAGES CXX)

include(../../cmake/tools.cmake)

enable_testing()
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../test
                 ${CMAKE_BINARY_DIR}/test)
