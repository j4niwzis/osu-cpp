cmake_minimum_required(VERSION 4.1.1 FATAL_ERROR)

set(CMAKE_EXPERIMENTAL_CXX_IMPORT_STD
  "451f2fe2-a8a2-47c3-bc32-94786d8fc91b")
set(CMAKE_CXX_MODULE_STD 1)

# The drawing half of the client, with nothing of the game in it: the Skia
# wrapper, the palette and painter, the retained scene graph, the drawables
# built on it and TrianglesV2. Split out of the client so that the boundary
# is enforced by the build rather than by good intentions -- if something in
# here starts needing a beatmap, it will not link.
project(
  osuui
  VERSION 1.0
  LANGUAGES CXX
)

if(PROJECT_SOURCE_DIR STREQUAL PROJECT_BINARY_DIR)
  message(FATAL_ERROR "In-source builds not allowed.")
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
  set(EMSCRIPTEN_BUILD TRUE)
else()
  set(EMSCRIPTEN_BUILD FALSE)
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(SKIA REQUIRED skia)

if(NOT EMSCRIPTEN_BUILD)
  find_package(OpenGL REQUIRED)
endif()

file(GLOB_RECURSE ui_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cc")

if(EMSCRIPTEN_BUILD)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fexceptions -pthread -msimd128")
endif()

add_library(${PROJECT_NAME} STATIC ${ui_sources})

# PUBLIC: everything that links this library gets `import skia`, `import
# client.ui`, `import client.scene`, `import client.nodes` and
# `import client.triangles`.
target_sources(${PROJECT_NAME} PUBLIC FILE_SET CXX_MODULES FILES ${ui_sources})

set_target_properties(${PROJECT_NAME} PROPERTIES CXX_STANDARD 26)
target_compile_features(${PROJECT_NAME} PUBLIC cxx_std_23)

if(EMSCRIPTEN_BUILD)
  target_compile_options(${PROJECT_NAME} PRIVATE -fexceptions -pthread -msimd128)
endif()

# skia.cc is the only thing here that reaches for headers, and it wants both
# Skia's and the GL one it declares SK_GL against.
target_include_directories(${PROJECT_NAME} PUBLIC ${SKIA_INCLUDE_DIRS})

target_link_libraries(${PROJECT_NAME} PUBLIC
  ${SKIA_LDFLAGS} ${SKIA_LIBRARIES}
)
if(NOT EMSCRIPTEN_BUILD)
  target_link_libraries(${PROJECT_NAME} PUBLIC OpenGL::GL)
endif()
