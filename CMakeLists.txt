cmake_minimum_required(VERSION 4.1.1 FATAL_ERROR)

set(CMAKE_EXPERIMENTAL_CXX_IMPORT_STD
  # This specific value changes as experimental support evolves. See
  # `Help/dev/experimental.rst` in the CMake source corresponding to
  # your CMake build for the exact value to use.
  "451f2fe2-a8a2-47c3-bc32-94786d8fc91b")

set(CMAKE_CXX_MODULE_STD 1)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---- Project ----

# Note: update this to your new project's name and version
project(
  osucpp
  VERSION 1.0
  LANGUAGES CXX
)



# ---- Include guards ----

if(PROJECT_SOURCE_DIR STREQUAL PROJECT_BINARY_DIR)
  message(
    FATAL_ERROR
      "In-source builds not allowed. Please make a new directory (called a build directory) and run CMake from there."
  )
endif()

# ---- Add dependencies via CPM ----
# see https://github.com/TheLartians/CPM.cmake for more info

include(cmake/CPM.cmake)

# PackageProject.cmake will be used to make our target installable
CPMAddPackage(NAME PackageProject.cmake GIT_REPOSITORY "https://github.com/j4niwzis/PackageProject.cmake" GIT_TAG master)
# ---- Add source files ----

# Note: globbing sources is considered bad practice as CMake's generators may not detect new files
# automatically. Keep that in mind when changing files, or explicitly mention them here.
file(GLOB_RECURSE headers CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/include/*.h")
file(GLOB_RECURSE sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cc")

# ---- Create library ----

# Note: for header-only libraries change all PUBLIC flags to INTERFACE and create an interface
# target: add_library(${PROJECT_NAME} INTERFACE)
if(CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fexceptions -pthread -msimd128")
endif()
add_library(${PROJECT_NAME} STATIC ${headers} ${sources})

target_compile_features(${PROJECT_NAME}
  PRIVATE   cxx_std_23
  INTERFACE cxx_std_23)

# being a cross-platform target, we enforce standards conformance on MSVC
target_compile_options(${PROJECT_NAME} PRIVATE "$<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/permissive->")

if(CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
  target_compile_options(${PROJECT_NAME} PRIVATE -fexceptions -pthread -msimd128)
endif()


target_sources(${PROJECT_NAME} PUBLIC FILE_SET CXX_MODULES FILES ${sources})

# Link dependencies
find_package(PkgConfig)
pkg_check_modules(LIBLZMA REQUIRED liblzma)
target_link_libraries(${PROJECT_NAME} PUBLIC
  ${LIBLZMA_LIBRARIES}
)
target_include_directories(${PROJECT_NAME} PUBLIC
  ${LIBLZMA_INCLUDE_DIRS}
)
target_link_directories(${PROJECT_NAME} PUBLIC
  ${LIBLZMA_LIBRARY_DIRS}
)


# ---- Create an installable target ----
# this allows users to install and find the library via `find_package()`.

# the location where the project's version header will be placed should match the project's regular
# header paths

packageProject(
  NAME ${PROJECT_NAME}
  VERSION ${PROJECT_VERSION}
  NAMESPACE ${PROJECT_NAME}
  BINARY_DIR ${PROJECT_BINARY_DIR}

  COMPATIBILITY SameMajorVersion
)
