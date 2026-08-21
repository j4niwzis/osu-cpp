cmake_minimum_required(VERSION 4.1.1 FATAL_ERROR)

set(CMAKE_EXPERIMENTAL_CXX_IMPORT_STD
  "451f2fe2-a8a2-47c3-bc32-94786d8fc91b")
set(CMAKE_CXX_MODULE_STD 1)

# skiff -- a small retained-mode UI framework on Skia, in the shape
# osu!framework gives its drawables, sized for software rasterisation.
#
#   skia         a module wrapper over the Skia headers, so nothing else in
#                the tree needs a global module fragment
#   skiff.paint  the font stack with fallbacks, the canvas painter, easings
#   skiff.scene  the scene graph: anchors, origins, relative and automatic
#                sizing, transforms, hit testing, damage tracking
#   skiff.nodes  the drawables built on it: boxes, text, sprites, flows,
#                scroll and cached containers, clickable areas
#
# Nothing in here knows what a beatmap is, or what osu! is. This is meant to
# be lifted into a repository of its own; keeping it as a project rather than
# a directory is what makes that a move rather than a rewrite.
project(
  skiff
  VERSION 0.1
  LANGUAGES CXX
)

if(PROJECT_SOURCE_DIR STREQUAL PROJECT_BINARY_DIR)
  message(FATAL_ERROR "In-source builds not allowed.")
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
  set(SKIFF_EMSCRIPTEN TRUE)
else()
  set(SKIFF_EMSCRIPTEN FALSE)
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(SKIA REQUIRED skia)

if(NOT SKIFF_EMSCRIPTEN)
  find_package(OpenGL REQUIRED)
endif()

# Two targets, on purpose. skiff is the primitives -- the Skia wrapper, the
# painter, the graph, the drawables -- and skiff-widgets is the layer of
# ready-made things a screen is built out of, themed rather than subclassed.
# GTK and libadwaita, in that order: an application that wants only the graph
# links the first and never pays for the second.
set(skiff_core_sources
  ${CMAKE_CURRENT_SOURCE_DIR}/src/skia.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/src/paint.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/src/scene.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/src/nodes.cc
)
set(skiff_widget_sources
  ${CMAKE_CURRENT_SOURCE_DIR}/src/widgets.cc
)

if(SKIFF_EMSCRIPTEN)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fexceptions -pthread -msimd128")
endif()

add_library(${PROJECT_NAME} STATIC ${skiff_core_sources})
add_library(skiff::skiff ALIAS ${PROJECT_NAME})

# PUBLIC: anything linking this gets `import skia`, `import skiff.paint`,
# `import skiff.scene` and `import skiff.nodes`.
target_sources(${PROJECT_NAME} PUBLIC
  FILE_SET CXX_MODULES FILES ${skiff_core_sources})

set_target_properties(${PROJECT_NAME} PROPERTIES CXX_STANDARD 26)
target_compile_features(${PROJECT_NAME} PUBLIC cxx_std_23)

# skia.cc is the only thing here that reaches for headers, and it wants both
# Skia's and the GL one it declares SK_GL against.
target_include_directories(${PROJECT_NAME} PUBLIC ${SKIA_INCLUDE_DIRS})

target_link_libraries(${PROJECT_NAME} PUBLIC
  ${SKIA_LDFLAGS} ${SKIA_LIBRARIES}
)
if(NOT SKIFF_EMSCRIPTEN)
  target_link_libraries(${PROJECT_NAME} PUBLIC OpenGL::GL)
endif()

add_library(skiff-widgets STATIC ${skiff_widget_sources})
add_library(skiff::widgets ALIAS skiff-widgets)
target_sources(skiff-widgets PUBLIC
  FILE_SET CXX_MODULES FILES ${skiff_widget_sources})
set_target_properties(skiff-widgets PROPERTIES CXX_STANDARD 26)
target_link_libraries(skiff-widgets PUBLIC ${PROJECT_NAME})

if(SKIFF_EMSCRIPTEN)
  target_compile_options(${PROJECT_NAME} PRIVATE -fexceptions -pthread -msimd128)
  target_compile_options(skiff-widgets PRIVATE -fexceptions -pthread -msimd128)
endif()
