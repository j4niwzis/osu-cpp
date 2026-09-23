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
  VERSION 0.1
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



# ---- The language ----
#
# C++23 as the standard of every target made here, and not only as a compile
# feature of the library. CMake builds `import std` a second time for whoever
# will import this library's modules, and that second build takes the
# directory's standard rather than the library's features: left at the
# default it was C++17, which a module cannot be compiled as, and the library
# could not be configured on its own -- which is what a consumer fetching it
# through cmake-everywhere does.
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---- Include guards ----

if(PROJECT_SOURCE_DIR STREQUAL PROJECT_BINARY_DIR)
  message(
    FATAL_ERROR
      "In-source builds not allowed. Please make a new directory (called a build directory) and run CMake from there."
  )
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
#
# In this build only. LibLZMA::LibLZMA may be an alias of a target a port made,
# and an export writes what an alias stands for rather than the alias -- the
# installed package named liblzma::liblzma, a target nothing on another
# machine makes. So the installed config asks for liblzma the way that
# machine can answer, and puts the name it gets back on the target itself.
find_package(LibLZMA REQUIRED)
target_link_libraries(${PROJECT_NAME} PUBLIC $<BUILD_INTERFACE:LibLZMA::LibLZMA>)


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
    GIT_TAG main
    LICENSE AGPL-3.0-only
    DEPENDS xz
    TARGETS osucpp::osucpp
  )
endif()

# ---- Installing it ----
#
# The module interface units go with it: a consumer of a C++ module library
# compiles its own binary module interface from the source, so an installed
# library that kept its sources to itself could not be imported at all.
#
# Written out rather than handed to PackageProject.cmake, which was fetched at
# configure time from a branch -- unpinned, and a download in a build that may
# not have a network -- and which is not where module file sets are installed.
# Installing is what a build of this library does; a build that embeds it, as
# the client does, does not.
option(OSU_INSTALL_LIBRARY_PACKAGE
  "Install osucpp and its CMake package" ${PROJECT_IS_TOP_LEVEL})
if(OSU_INSTALL_LIBRARY_PACKAGE)
  include(GNUInstallDirs)
  include(CMakePackageConfigHelpers)
  install(TARGETS ${PROJECT_NAME}
    EXPORT osucppTargets
    FILE_SET CXX_MODULES DESTINATION ${CMAKE_INSTALL_LIBDIR}/osucpp/modules
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
  install(EXPORT osucppTargets
    NAMESPACE osucpp::
    CXX_MODULES_DIRECTORY modules
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/osucpp)
  # What the exported target names has to be found again by whoever includes
  # this, in the same words: liblzma, the one thing it links. A consumer with a
  # dependency provider has this call answered by it; one with nothing but a
  # prefix has it answered by CMake's own FindLibLZMA.
  file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/osucppConfig.cmake"
    "include(CMakeFindDependencyMacro)\n"
    "find_dependency(LibLZMA)\n"
    "include(\"\${CMAKE_CURRENT_LIST_DIR}/osucppTargets.cmake\")\n"
    "set_property(TARGET osucpp::osucpp APPEND PROPERTY\n"
    "             INTERFACE_LINK_LIBRARIES LibLZMA::LibLZMA)\n")
  write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/osucppConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion)
  install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/osucppConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/osucppConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/osucpp)
endif()

# The name a consumer links, whether this is installed or embedded.
add_library(osucpp::osucpp ALIAS osucpp)
