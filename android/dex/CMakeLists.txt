# The program that writes this APK's classes.dex.
#
# A project of its own, because it runs on the machine doing the building
# while everything around it is compiled for the phone. The Android build
# configures this separately, with the host compiler, and then runs what
# comes out.
cmake_minimum_required(VERSION 4.2)

include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/import-std.cmake")

project(osu-dex CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(osu-dex)
target_sources(osu-dex
  PRIVATE main.cc
  PRIVATE FILE_SET CXX_MODULES FILES dexfile.cc)
