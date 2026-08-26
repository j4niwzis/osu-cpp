set(CMAKE_SYSTEM_NAME Linux)
set(ANDROID TRUE CACHE BOOL "Build the native Android application" FORCE)

set(OSU_ANDROID_ROOT "$ENV{ANDROID_FREE}" CACHE PATH
  "Root of the source-built Android toolchain")
if(NOT IS_DIRECTORY "${OSU_ANDROID_ROOT}")
  message(FATAL_ERROR "Set ANDROID_FREE or OSU_ANDROID_ROOT")
endif()

set(OSU_ANDROID_API "26" CACHE STRING "Android API level")
set(OSU_ANDROID_TRIPLE "aarch64-linux-android" CACHE STRING
  "Android compiler target triple")
set(OSU_ANDROID_ABI "arm64-v8a" CACHE STRING "Android APK ABI")
set(OSU_ANDROID_SYSROOT "${OSU_ANDROID_ROOT}/sysroot" CACHE PATH
  "Source-built Android sysroot")
set(OSU_ANDROID_RESOURCE_DIR "${OSU_ANDROID_ROOT}/clang-resource" CACHE PATH
  "Clang resource directory containing Android compiler-rt")
set(OSU_ANDROID_NATIVE_APP_GLUE_DIR
  "${OSU_ANDROID_ROOT}/src/ndk/sources/android/native_app_glue" CACHE PATH
  "Directory containing android_native_app_glue.c")
set(OSU_ANDROID_LIBRARY_DIR
  "${OSU_ANDROID_SYSROOT}/usr/lib/${OSU_ANDROID_TRIPLE}/${OSU_ANDROID_API}"
  CACHE PATH "Android API-specific library directory")

set(CMAKE_SYSROOT "${OSU_ANDROID_SYSROOT}")
set(CMAKE_C_COMPILER clang-22 CACHE FILEPATH "Android C compiler")
set(CMAKE_CXX_COMPILER clang++-22 CACHE FILEPATH "Android C++ compiler")
set(CMAKE_C_COMPILER_TARGET "${OSU_ANDROID_TRIPLE}${OSU_ANDROID_API}")
set(CMAKE_CXX_COMPILER_TARGET "${OSU_ANDROID_TRIPLE}${OSU_ANDROID_API}")
set(CMAKE_CXX_STDLIB_MODULES_JSON
  "${OSU_ANDROID_SYSROOT}/usr/lib/libc++.modules.json" CACHE FILEPATH
  "libc++ standard library modules metadata" FORCE)
find_program(OSU_LLVM_AR NAMES llvm-ar HINTS /usr/lib/llvm22/bin REQUIRED)
find_program(OSU_LLVM_RANLIB NAMES llvm-ranlib
  HINTS /usr/lib/llvm22/bin REQUIRED)
set(CMAKE_AR "${OSU_LLVM_AR}" CACHE FILEPATH "LLVM archiver" FORCE)
set(CMAKE_RANLIB "${OSU_LLVM_RANLIB}" CACHE FILEPATH "LLVM ranlib" FORCE)

set(android_common_flags
  "--sysroot=${OSU_ANDROID_SYSROOT} -resource-dir=${OSU_ANDROID_RESOURCE_DIR}")
set(CMAKE_C_FLAGS_INIT "${android_common_flags} -fPIC")
set(CMAKE_CXX_FLAGS_INIT
  "${android_common_flags} -fPIC -stdlib=libc++")
set(CMAKE_SHARED_LINKER_FLAGS_INIT
  "${android_common_flags} -fuse-ld=lld -nostdlib")

set(CMAKE_FIND_ROOT_PATH "${OSU_ANDROID_SYSROOT}")
set(CMAKE_LIBRARY_PATH "${OSU_ANDROID_LIBRARY_DIR}")
set(CMAKE_INCLUDE_PATH
  "${OSU_ANDROID_SYSROOT}/usr/include"
  "${OSU_ANDROID_SYSROOT}/usr/include/c++/v1")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
