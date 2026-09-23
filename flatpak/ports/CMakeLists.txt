cmake_minimum_required(VERSION 4.3.4 FATAL_ERROR)

# The Flatpak's ports module: every library the client asks the provider
# for, built by the provider and kept in its store, and nothing of the
# client. It runs among the libraries, before the client's module, and the
# client's module takes each of these from the store and builds none.
#
# What is asked for is not written down here. standalone/dependencies.cmake
# is what the client asks with, and this reads the same file -- the same
# libraries with the same parts are the same entries in the store, and two
# lists would be two lists by the second commit.
#
# Configured the way standalone/ is, line for line up to that point: the
# same standard, the same `import std`, the same provider. How a library is
# kept depends on how it was built, so a setting that differed here would be
# a store the client's module could not use.
include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/import-std.cmake)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_PROJECT_TOP_LEVEL_INCLUDES
    ${CMAKE_CURRENT_LIST_DIR}/../../cmake/get_cme.cmake)

project(osu_client_libraries LANGUAGES C CXX)

include(GNUInstallDirs)
include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/CPM.cmake)

include(${CMAKE_CURRENT_LIST_DIR}/../sources.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/../../standalone/dependencies.cmake)
