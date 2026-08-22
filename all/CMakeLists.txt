if(CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
  set(CMAKE_CXX_FLAGS "-fexceptions" CACHE STRING "" FORCE)
endif()

cmake_minimum_required(VERSION 4.3.3 FATAL_ERROR)

include(${CMAKE_CURRENT_LIST_DIR}/../cmake/import-std.cmake)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_EXTENSIONS OFF)


project(BuildAll LANGUAGES CXX)

include(../cmake/tools.cmake)

# needed to generate test target
enable_testing()

add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../standalone ${CMAKE_BINARY_DIR}/standalone)
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../test ${CMAKE_BINARY_DIR}/test)
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../skiff/test ${CMAKE_BINARY_DIR}/skiff-test)
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../skiff-widgets/test
                 ${CMAKE_BINARY_DIR}/skiff-widgets-test)
