# Ubuntu Touch Click package

This package targets Ubuntu Touch 24.04 on arm64. It is deliberately built
against Noble rather than repacking the Flatpak runtime: libc, Wayland, EGL and
OpenGL come from Ubuntu Touch, while Clickable copies application libraries
that are not guaranteed to be installed on the device.

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

Build caches, including CMake, Skia, skiff and skiff-widgets checkouts, live
under `build/click/aarch64-linux-gnu` and are reused unless that directory is
removed.
