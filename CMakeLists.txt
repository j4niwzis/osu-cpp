cmake_minimum_required(VERSION 4.3.4 FATAL_ERROR)

include(${CMAKE_CURRENT_LIST_DIR}/cmake/import-std.cmake)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---- Project ----

# Note: update this to your new project's name and version
# Answered by cmake-everywhere, named before project() because that is the
# only place a dependency provider may be installed from.
set(CMAKE_PROJECT_TOP_LEVEL_INCLUDES ${CMAKE_CURRENT_LIST_DIR}/cmake/get_cme.cmake)

project(
  osucpp
  VERSION 1.0
  LANGUAGES CXX
)

# Whether the answer to every find_package below is actually installed.
#
# CMAKE_PROJECT_TOP_LEVEL_INCLUDES is read by the top-level project() call
# and by no other, so a build configured from somewhere else -- or with the
# variable set after project() -- has no provider and no sign of it: every
# find_package quietly falls back to whatever the machine happens to have,
# and the first library the machine has in a shape this project does not
# expect is what finally says something, about the library rather than
# about this.
if(NOT COMMAND cme_provider)
  message(FATAL_ERROR
    "cmake-everywhere is not installed in this build, so nothing answers "
    "find_package. CMAKE_PROJECT_TOP_LEVEL_INCLUDES is only read by the "
    "top-level project() call: configure this directory, or the repository "
    "root, and not a subdirectory of either.")
endif()



# ---- Include guards ----

if(PROJECT_SOURCE_DIR STREQUAL PROJECT_BINARY_DIR)
  message(
    FATAL_ERROR
      "In-source builds not allowed. Please make a new directory (called a build directory) and run CMake from there."
  )
endif()

# ---- Add dependencies via CPM ----
# see https://github.com/TheLartians/CPM.cmake for more info

option(OSU_INSTALL_LIBRARY_PACKAGE
  "Generate and install the standalone osucpp CMake package" ON)

if(OSU_INSTALL_LIBRARY_PACKAGE)
  include(cmake/CPM.cmake)
endif()

# PackageProject.cmake will be used to make our target installable
if(OSU_INSTALL_LIBRARY_PACKAGE)
  CPMAddPackage(NAME PackageProject.cmake GIT_REPOSITORY "https://github.com/j4niwzis/PackageProject.cmake" GIT_TAG master)
endif()
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
#
# One call, and where it comes from is not this project's business: the
# system's liblzma when there is one, and a pinned source build when there
# is not. See cmake/get_cme.cmake.
find_package(LibLZMA REQUIRED)
target_link_libraries(${PROJECT_NAME} PUBLIC LibLZMA::LibLZMA)


# ---- What this library is, said by the library ----
#
# The client, the flatpak build and anything else that embeds osucpp should
# not have to know what it is built against: it writes find_package(osucpp)
# and this says the rest. The declaration is installed beside the library,
# under share/cmake-everywhere/ports, with the features it was built with
# baked in -- so a prefix that has osucpp has the description of osucpp.
if(COMMAND cme_declare_port)
  cme_declare_port(
    NAME osucpp
    PROVIDES osucpp
    VERSION ${PROJECT_VERSION}
    GITHUB_REPOSITORY j4niwzis/osu-cpp
    GIT_TAG cmake-everywhere
    LICENSE AGPL-3.0-or-later
    DEPENDS xz
    TARGETS osucpp::osucpp
  )
endif()

# ---- Create an installable target ----
# this allows users to install and find the library via `find_package()`.

# the location where the project's version header will be placed should match the project's regular
# header paths

if(OSU_INSTALL_LIBRARY_PACKAGE)
  packageProject(
    NAME ${PROJECT_NAME}
    VERSION ${PROJECT_VERSION}
    NAMESPACE ${PROJECT_NAME}
    BINARY_DIR ${PROJECT_BINARY_DIR}

    COMPATIBILITY SameMajorVersion
  )
else()
  # The standalone client links the namespaced target whether osucpp is being
  # packaged or embedded. Flatpak embeds it and deliberately avoids fetching
  # PackageProject.cmake during its network-isolated build.
  add_library(osucpp::osucpp ALIAS osucpp)
endif()
