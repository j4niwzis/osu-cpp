# Enable CMake's experimental `import std` support.
#
# The UUID is a gate: CMake compares it against the value of the release it
# ships in, and refuses anything else with
#
#   Experimental `import std` support not enabled when detecting toolchain;
#   it must be set before `CXX` is enabled (usually a `project()` call).
#
# A hard-coded value therefore ties the project to one CMake release. Choose
# the UUID for the CMake that is actually configuring this tree, so the root
# project and every independently configured subproject agree.
#
# Each value comes from `Help/dev/experimental.rst` in that CMake release.
# This file must be included before `project()`, where CXX is enabled.

if(CMAKE_VERSION VERSION_GREATER_EQUAL 4.4)
  # Also used for releases newer than we know about. If CMake moves the gate
  # again, its configure error identifies the new entry this table needs.
  set(CMAKE_EXPERIMENTAL_CXX_IMPORT_STD
      "f35a9ac6-8463-4d38-8eec-5d6008153e7d")
elseif(CMAKE_VERSION VERSION_GREATER_EQUAL 4.3)
  set(CMAKE_EXPERIMENTAL_CXX_IMPORT_STD
      "451f2fe2-a8a2-47c3-bc32-94786d8fc91b")
elseif(CMAKE_VERSION VERSION_GREATER_EQUAL 4.2)
  set(CMAKE_EXPERIMENTAL_CXX_IMPORT_STD
      "d0edc3af-4c50-42ea-a356-e2862fe7a444")
else()
  message(FATAL_ERROR
    "osu-cpp is built as C++23 modules against `import std`, which needs "
    "CMake 4.2 or newer (found ${CMAKE_VERSION}).")
endif()

set(CMAKE_CXX_MODULE_STD 1)
