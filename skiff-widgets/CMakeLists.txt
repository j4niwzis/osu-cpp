cmake_minimum_required(VERSION 4.1.1 FATAL_ERROR)

set(CMAKE_EXPERIMENTAL_CXX_IMPORT_STD
  "451f2fe2-a8a2-47c3-bc32-94786d8fc91b")
set(CMAKE_CXX_MODULE_STD 1)
set(CMAKE_CXX_EXTENSIONS OFF)

# The widget set that sits on skiff: ready-made controls a screen is built
# out of, themed rather than subclassed. Its own project rather than a second
# target inside skiff, because it depends on skiff and skiff does not depend
# on it, and that is easier to keep true when they cannot see each other's
# source.
#
# Every widget is its own module -- skiff.widgets.textbox, .button, .tabbar --
# so a screen imports the ones it uses. skiff.widgets is the umbrella for a
# screen that wants all of them.
project(
  skiff-widgets
  VERSION 0.1
  LANGUAGES CXX
)

if(PROJECT_SOURCE_DIR STREQUAL PROJECT_BINARY_DIR)
  message(FATAL_ERROR "In-source builds not allowed.")
endif()

if(NOT TARGET skiff::skiff)
  include(../cmake/CPM.cmake)
  CPMAddPackage(NAME skiff SOURCE_DIR ${CMAKE_CURRENT_LIST_DIR}/../skiff)
endif()

# Globbed, so that adding a widget is adding a file.
file(GLOB skiff_widget_sources CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cc")

if(CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fexceptions -pthread -msimd128")
endif()

add_library(${PROJECT_NAME} STATIC ${skiff_widget_sources})
add_library(skiff::widgets ALIAS ${PROJECT_NAME})

target_sources(${PROJECT_NAME} PUBLIC
  FILE_SET CXX_MODULES FILES ${skiff_widget_sources})

set_target_properties(${PROJECT_NAME} PROPERTIES CXX_STANDARD 23)
target_compile_features(${PROJECT_NAME} PUBLIC cxx_std_23)
target_link_libraries(${PROJECT_NAME} PUBLIC skiff::skiff)

if(CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
  target_compile_options(${PROJECT_NAME} PRIVATE -fexceptions -pthread -msimd128)
endif()
