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

# Which directory under lib/ this architecture's libraries are in.
#
# Multiarch keeps them in lib/<triple>, and CMake looks there only when it
# knows the triple: for a native build the compiler is asked, for a cross
# build nothing asks. Without it, find_library reads lib/ and lib64/ and
# reports that libOpenGL is missing on a machine where it is installed --
# under lib/aarch64-linux-gnu, which is where it belongs.
set(CMAKE_LIBRARY_ARCHITECTURE ${OSU_CROSS_TRIPLE})

# Where a cross build looks and where it does not: programs are this
# machine's, libraries and headers are the target's. Without this a
# find_library walks into /usr/lib/x86_64-linux-gnu and finds something that
# links and does not run.
set(CMAKE_FIND_ROOT_PATH "${OSU_CROSS_SYSROOT}/usr/${OSU_CROSS_TRIPLE}"
                         "${OSU_CROSS_SYSROOT}")
# And named outright, because CMAKE_LIBRARY_ARCHITECTURE is also what CMake
# works out for itself while it identifies the compiler, and what it works
# out there is about the machine it is running on. Said here it is a fact
# about the target: libX11 and libOpenGL are in lib/<triple> and a search
# that does not look there reports them missing on a machine that has them.
list(APPEND CMAKE_LIBRARY_PATH
     "${OSU_CROSS_SYSROOT}/usr/lib/${OSU_CROSS_TRIPLE}"
     "${OSU_CROSS_SYSROOT}/lib/${OSU_CROSS_TRIPLE}")
list(APPEND CMAKE_INCLUDE_PATH "${OSU_CROSS_SYSROOT}/usr/include")

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
