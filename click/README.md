# Ubuntu Touch Click package

This package targets Ubuntu Touch 24.04 (`ubuntu-touch-24.04-2.x`) on arm64. It is deliberately built
against Noble rather than repacking the Flatpak runtime: libc, Wayland, EGL and
OpenGL come from Ubuntu Touch, while Clickable copies application libraries
that are not guaranteed to be installed on the device. The exact runtime
closure is collected from the finished executable because Clickable rejects
the wildcard and `+` characters needed to express it through `install_lib`.
LLVM 22 is installed during Clickable's image-setup phase with apt.llvm.org's
official installer; putting it in `dependencies_host` would make Clickable ask
Noble for the packages before that repository exists.

Everything built by this recipe uses Clang 22, lld, `-O3` and full LLVM LTO.
Skia, GLFW and the project's own libraries are static archives. Ubuntu Touch's
ABI and hardware boundary remains dynamic: glibc, Mir/libhybris, EGL/GLES and
the distribution multimedia libraries come from Noble or are copied as shared
runtime dependencies. Keeping those components dynamic is required for the
same Click to use the device's Android GPU driver.

Automatic review is disabled because the reviewer bundled by the current
Clickable Noble image does not yet know the `2404.2` policy database, although
Clickable 8.9 itself supports the framework. Review the package once the image
ships matching reviewer data.

Install Clickable 8.9 or newer, then build with one job (the CMake and final
link steps use substantial memory):

```sh
clickable build --arch arm64
```

The resulting `.click` is placed below `build/click/`. To install and launch on
an attached device:

```sh
clickable install --arch arm64
clickable launch --arch arm64
clickable logs
```

Build caches, including CMake, Skia, GLFW, skiff and skiff-widgets checkouts,
live under `build/click/aarch64-linux-gnu`. The recipe always reconfigures its
Ninja graphs, so compiler or source changes invalidate the affected targets
without versioned marker files or wholesale cache deletion.
