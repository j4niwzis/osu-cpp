# cmake-everywhere: one find_package for a library, wherever it comes from.
#
# Set before project(), because a dependency provider may only be installed
# from a file named by CMAKE_PROJECT_TOP_LEVEL_INCLUDES:
#
#   set(CMAKE_PROJECT_TOP_LEVEL_INCLUDES ${CMAKE_CURRENT_LIST_DIR}/cmake/get_cme.cmake)
#
# A revision and its digest, not a branch. What resolves every dependency of
# this project is a dependency of this project.

set(CME_VERSION "4fe91e3ef109e36f6873fee54b096464e66d6e9f" CACHE STRING
  "cmake-everywhere revision")
set(CME_SHA256 "8879c994afa29d7de735589c14ed5cbff1b646e5f17b211d3c790c3833231f3f" CACHE STRING
  "The digest of that revision's archive")
set(CME_SOURCE_DIR "${CMAKE_BINARY_DIR}/_cme" CACHE PATH
  "Where it is unpacked")

# Which revision the directory already holds is not readable from the
# directory, so it is written down beside it. Without that, raising the pin
# changed nothing: the same tree was used, with a number in the log that no
# longer described it. --fresh does not help either -- it removes the cache
# and not the tree.
set(cme_stamp "${CME_SOURCE_DIR}/.cme-revision")
set(cme_have "")
if(EXISTS "${cme_stamp}")
  file(READ "${cme_stamp}" cme_have)
  string(STRIP "${cme_have}" cme_have)
endif()

if(NOT EXISTS "${CME_SOURCE_DIR}/cmake-everywhere.cmake"
   OR NOT cme_have STREQUAL "${CME_VERSION}")
  set(archive "${CMAKE_BINARY_DIR}/cme-${CME_VERSION}.tar.gz")
  if(cme_have)
    message(STATUS
      "cmake-everywhere: fetching ${CME_VERSION}, replacing ${cme_have}")
  else()
    message(STATUS "cmake-everywhere: fetching ${CME_VERSION}")
  endif()
  file(DOWNLOAD
    "https://github.com/j4niwzis/cmake-everywhere/archive/${CME_VERSION}.tar.gz"
    "${archive}" STATUS status EXPECTED_HASH SHA256=${CME_SHA256})
  list(GET status 0 code)
  if(NOT code EQUAL 0)
    list(GET status 1 reason)
    message(FATAL_ERROR "cmake-everywhere: cannot fetch ${CME_VERSION}: ${reason}")
  endif()
  file(ARCHIVE_EXTRACT INPUT "${archive}"
       DESTINATION "${CMAKE_BINARY_DIR}/_cme-unpack")
  file(GLOB unpacked "${CMAKE_BINARY_DIR}/_cme-unpack/*")
  list(GET unpacked 0 root)
  file(REMOVE_RECURSE "${CME_SOURCE_DIR}")
  file(RENAME "${root}" "${CME_SOURCE_DIR}")
  file(WRITE "${cme_stamp}" "${CME_VERSION}\n")
endif()

include("${CME_SOURCE_DIR}/cmake-everywhere.cmake")
