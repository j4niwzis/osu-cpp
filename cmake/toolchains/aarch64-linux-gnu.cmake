# Cross-compiling for a 64-bit ARM GNU/Linux, which is what an Ubuntu Touch
# device is.
#
# Not an emulator. Clickable builds inside a container for the device's
# architecture and runs every compiler under qemu, which turns twelve minutes
# of work into hours -- and none of it is necessary: clang is a cross
# compiler by construction, and what it needs besides a target triple is a
# sysroot. Debian and Ubuntu can install one through dpkg's foreign
# architectures, so the headers and libraries of the device's distribution
# sit in this filesystem, natively readable.
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

set(OSU_CROSS_TRIPLE aarch64-linux-gnu CACHE STRING
    "The triple to compile for")
set(OSU_CROSS_SYSROOT "/" CACHE PATH
    "Where that architecture's headers and libraries are")

set(CMAKE_C_COMPILER_TARGET ${OSU_CROSS_TRIPLE})
set(CMAKE_CXX_COMPILER_TARGET ${OSU_CROSS_TRIPLE})
set(CMAKE_ASM_COMPILER_TARGET ${OSU_CROSS_TRIPLE})

if(NOT OSU_CROSS_SYSROOT STREQUAL "/")
  set(CMAKE_SYSROOT "${OSU_CROSS_SYSROOT}")
endif()

# Where a cross build looks and where it does not: programs are this
# machine's, libraries and headers are the target's. Without this a
# find_library walks into /usr/lib/x86_64-linux-gnu and finds something that
# links and does not run.
set(CMAKE_FIND_ROOT_PATH "${OSU_CROSS_SYSROOT}/usr/${OSU_CROSS_TRIPLE}"
                         "${OSU_CROSS_SYSROOT}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# pkg-config answers about the target too, or it answers about this machine
# and every .pc file it reads is the wrong one.
set(ENV{PKG_CONFIG_LIBDIR}
    "${OSU_CROSS_SYSROOT}/usr/lib/${OSU_CROSS_TRIPLE}/pkgconfig:${OSU_CROSS_SYSROOT}/usr/share/pkgconfig")
set(ENV{PKG_CONFIG_SYSROOT_DIR} "")

set(CMAKE_C_FLAGS_INIT "-fPIC")
set(CMAKE_CXX_FLAGS_INIT "-fPIC")
set(cross_link "-fuse-ld=lld")
set(CMAKE_EXE_LINKER_FLAGS_INIT "${cross_link}")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "${cross_link}")
set(CMAKE_MODULE_LINKER_FLAGS_INIT "${cross_link}")
