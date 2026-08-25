#!/bin/sh
set -eu

: "${ROOT:?Clickable did not set ROOT}"
: "${BUILD_DIR:?Clickable did not set BUILD_DIR}"
: "${INSTALL_DIR:?Clickable did not set INSTALL_DIR}"

jobs=${NUM_PROCS:-1}
prefix="$BUILD_DIR/prefix"
sources="$BUILD_DIR/sources"
mkdir -p "$prefix/lib/pkgconfig" "$sources" "$INSTALL_DIR"

export PATH="/usr/lib/llvm-22/bin:$prefix/bin:$PATH"
export CC=clang-22 CXX=clang++-22 AR=llvm-ar-22 NM=llvm-nm-22 RANLIB=llvm-ranlib-22
export PKG_CONFIG_PATH="$prefix/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"

# CMake 4.3.4 is required for this project's named modules/import-std setup.
if [ ! -x "$prefix/bin/cmake" ]; then
  if [ ! -d "$sources/cmake/.git" ]; then
    git clone --filter=blob:none --branch v4.3.4 https://github.com/Kitware/CMake.git "$sources/cmake"
  fi
  cmake_build="$BUILD_DIR/cmake"
  mkdir -p "$cmake_build"
  cd "$cmake_build"
  "$sources/cmake/bootstrap" --prefix="$prefix" --parallel="$jobs" -- -DCMAKE_USE_OPENSSL=OFF
  make -j"$jobs"
  make install
fi

# Build the exact Skia revision used by the Flatpak, but against Noble's
# graphics/image libraries. Only wuffs is a source dependency for this setup.
if [ ! -f "$prefix/lib/libskia.a" ]; then
  if [ ! -d "$sources/skia/.git" ]; then
    git clone https://skia.googlesource.com/skia.git "$sources/skia"
    git -C "$sources/skia" checkout 13ffba253fc7854fd3b34f67c82dfb2418dc2944
  fi
  if [ ! -d "$sources/skia/third_party/externals/wuffs/.git" ]; then
    mkdir -p "$sources/skia/third_party/externals"
    git clone https://skia.googlesource.com/external/github.com/google/wuffs-mirror-release-c.git \
      "$sources/skia/third_party/externals/wuffs"
    git -C "$sources/skia/third_party/externals/wuffs" checkout e3f919ccfe3ef542cfc983a82146070258fb57f8
  fi
  cd "$sources/skia"
  gn gen out/click --args='is_official_build=true cc="clang-22" cxx="clang++-22" ar="llvm-ar-22" extra_cflags=["-O3", "-stdlib=libc++"] extra_ldflags=["-fuse-ld=lld-22", "-stdlib=libc++"] skia_enable_ganesh=true skia_use_gl=true skia_use_egl=true skia_use_vulkan=false skia_use_x11=false skia_use_libpng_decode=true skia_use_libpng_encode=true skia_use_system_libpng=true skia_use_libjpeg_turbo_decode=true skia_use_system_libjpeg_turbo=true skia_use_libwebp_decode=true skia_use_system_libwebp=true skia_use_zlib=true skia_use_system_zlib=true skia_use_freetype=true skia_use_system_freetype2=true skia_use_fontconfig=true skia_enable_fontmgr_custom_directory=true skia_enable_fontmgr_custom_embedded=true skia_use_icu=false skia_use_harfbuzz=false skia_use_expat=false skia_use_dng_sdk=false skia_enable_skshaper=false skia_enable_skottie=false skia_enable_pdf=false skia_enable_svg=false skia_enable_tools=false'
  ninja -C out/click -j"$jobs" skia
  install -Dm644 out/click/libskia.a "$prefix/lib/libskia.a"
  mkdir -p "$prefix/include/skia/include" "$prefix/include/skia/modules"
  cp -a include/. "$prefix/include/skia/"
  cp -a include/. "$prefix/include/skia/include/"
  cp -a modules/skcms "$prefix/include/skia/modules/"
fi

# libskia is static even though the rest of the Noble dependencies are shared.
# Expose only Skia's private closure to ordinary pkg-config consumers. Turning
# on OSU_STATIC_DEPS globally would instead request FFmpeg's enormous static
# closure (vpx, dav1d, aom, rav1e, cairo, and many more development packages).
sed "s|^prefix=.*|prefix=$prefix|" "$ROOT/flatpak/skia.pc" |
awk '
  /^Libs: / { public = $0; next }
  /^Libs.private: / {
    private = $0
    sub(/^Libs.private:[[:space:]]*/, "", private)
    print public " " private
    next
  }
  { print }
' > "$prefix/lib/pkgconfig/skia.pc"

fetch_checkout() {
  name=$1 url=$2 commit=$3
  if [ ! -d "$sources/$name/.git" ]; then
    git clone "$url" "$sources/$name"
  fi
  git -C "$sources/$name" checkout "$commit"
}

fetch_checkout skiff https://github.com/j4niwzis/skiff.git 3ef9effb268ab924c3cb3510a5849db116d7c6f2
fetch_checkout skiff-widgets https://github.com/j4niwzis/skiff-widgets.git 57392eb2456339910c21e6f701a7214c5615296d

mkdir -p "$sources/cpm"
if [ ! -f "$sources/cpm/CPM.cmake" ]; then
  curl --fail --location --output "$sources/cpm/CPM.cmake" \
    https://github.com/cpm-cmake/CPM.cmake/releases/download/v0.42.1/CPM.cmake
  printf '%s  %s\n' f3a6dcc6a04ce9e7f51a127307fa4f699fb2bade357a8eb4c5b45df76e1dc6a5 \
    "$sources/cpm/CPM.cmake" | sha256sum -c -
fi

app_build="$BUILD_DIR/osu-cpp"
"$prefix/bin/cmake" -S "$ROOT/standalone" -B "$app_build" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR" \
  -DCMAKE_C_COMPILER=clang-22 \
  -DCMAKE_CXX_COMPILER=clang++-22 \
  -DCMAKE_CXX_FLAGS="-O3 -stdlib=libc++" \
  -DCMAKE_EXE_LINKER_FLAGS="-fuse-ld=lld-22 -stdlib=libc++" \
  -DOSU_INSTALL_LIBRARY_PACKAGE=OFF \
  -DOSU_VIDEO_LIBAV=ON \
  -DOSU_SKIFF_SOURCE_DIR="$sources/skiff" \
  -DOSU_SKIFF_WIDGETS_SOURCE_DIR="$sources/skiff-widgets" \
  -DCPM_DOWNLOAD_LOCATION="$sources/cpm/CPM.cmake"
"$prefix/bin/cmake" --build "$app_build" -j"$jobs"
"$prefix/bin/cmake" --install "$app_build"

# Clickable 8.9 rejects wildcards and '+' characters in install_lib entries.
# Obtain the exact runtime closure from the linked executable instead. Keep
# the host boundary on Ubuntu Touch: its loader, glibc, Wayland and graphics
# libraries must not be replaced by copies from the build container.
runtime_lib="$INSTALL_DIR/lib/aarch64-linux-gnu"
mkdir -p "$runtime_lib"
ldd "$INSTALL_DIR/bin/osu_client" | awk '$2 == "=>" && $3 ~ /^\// { print $3 }' |
while IFS= read -r library; do
  name=${library##*/}
  case "$name" in
    libc.so.*|libm.so.*|libmvec.so.*|libdl.so.*|libpthread.so.*|librt.so.*|\
    libgcc_s.so.*|libwayland-*.so.*|libEGL.so.*|libGL.so.*|libGLES*.so.*|\
    libOpenGL.so.*|libGLdispatch.so.*|libdrm.so.*|libgbm.so.*|libva.so.*|\
    libsystemd.so.*|libffi.so.*|libcap.so.*|libglib-2.0.so.*|libpcre2-8.so.*|\
    libdecor-0.so.*)
      continue
      ;;
  esac
  cp -L "$library" "$runtime_lib/$name"
done
