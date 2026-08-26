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
export CC=clang-22 CXX=clang++-22
export AR=llvm-ar-22 NM=llvm-nm-22 RANLIB=llvm-ranlib-22 STRIP=llvm-strip-22
export CFLAGS="-O3 -flto=full"
export CXXFLAGS="-O3 -flto=full -stdlib=libc++ -Wp,-U_FORTIFY_SOURCE"
export LDFLAGS="-flto=full -fuse-ld=lld-22"
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

# Halium devices expose their GPU through MirClient/libhybris. Mesa Wayland
# falls back to llvmpipe there, while EGL through the generic Wayland native
# window can crash in the Android vendor driver. GLFW 3.2 still has the native
# Mir backend that hands EGL the correct Mir buffer stream.
mir_sdk="$BUILD_DIR/mir-sdk"
mkdir -p "$mir_sdk/debs" "$mir_sdk/usr/include" "$mir_sdk/pkgconfig"
mir_debs='libmirclient-dev:95845ec4c094e923ae7c297a6ee88d8aac124195291fbec45441e3a9f812d8aa libmircommon-dev:a660d4bdf2b7be4ed767dafb2970cfa7269774a04af85dc8f41a5cd2097d4a1c libmircore-dev:c78c9521c2d4d4ccbbdd8df93e99a01c9b721db660a4dcc1f6b1e98c4a650dfa libmircookie-dev:dbe204baf3e5f25840d02993ec78db05d5e645c9cf74587f19c4db41c1e26e2d'
for item in $mir_debs; do
  package=${item%%:*}
  checksum=${item#*:}
  archive="$mir_sdk/debs/$package.deb"
  if [ ! -f "$archive" ]; then
    curl --fail --location --output "$archive" \
      "https://mirrors.edge.kernel.org/debian/pool/main/m/mir/${package}_1.8.0+dfsg1-18_arm64.deb"
  fi
  printf '%s  %s\n' "$checksum" "$archive" | sha256sum -c -
  dpkg-deb -x "$archive" "$mir_sdk"
done

ln -sfn /usr/lib/aarch64-linux-gnu/libmirclient.so.9 \
  "$prefix/lib/libmirclient.so"
printf '%s\n' \
  'Name: mirclient' \
  'Description: Mir compatibility client' \
  'Version: 1.8.0' \
  "Libs: -L$prefix/lib -lmirclient" \
  "Cflags: -I$mir_sdk/usr/include/mirclient -I$mir_sdk/usr/include/mircommon -I$mir_sdk/usr/include/mircore -I$mir_sdk/usr/include/mircookie" \
  > "$mir_sdk/pkgconfig/mirclient.pc"

glfw_commit=999f3556fdd80983b10051746264489f2cb1ef16
if [ ! -d "$sources/glfw-mir/.git" ]; then
  git clone https://github.com/glfw/glfw.git "$sources/glfw-mir"
fi
if ! patch --dry-run --reverse -d "$sources/glfw-mir" -p1 \
    < "$ROOT/click/glfw-3.2-mir.patch" >/dev/null 2>&1; then
  git -C "$sources/glfw-mir" checkout "$glfw_commit"
  git -C "$sources/glfw-mir" reset --hard "$glfw_commit"
  rm -f "$sources/glfw-mir/src/compat.c" "$sources/glfw-mir"/src/*.orig
  patch --forward --fuzz=0 --no-backup-if-mismatch \
    -d "$sources/glfw-mir" -p1 \
    < "$ROOT/click/glfw-3.2-mir.patch"
fi
glfw_build="$BUILD_DIR/glfw-mir"
PKG_CONFIG_PATH="$mir_sdk/pkgconfig:$PKG_CONFIG_PATH" \
"$prefix/bin/cmake" -S "$sources/glfw-mir" -B "$glfw_build" -G Ninja \
  -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="$prefix" \
  -DCMAKE_C_COMPILER=clang-22 \
  -DCMAKE_AR=llvm-ar-22 \
  -DCMAKE_RANLIB=llvm-ranlib-22 \
  -DCMAKE_INTERPROCEDURAL_OPTIMIZATION=OFF \
  -DOPENSSL_USE_STATIC_LIBS=TRUE \
  -DBUILD_SHARED_LIBS=OFF \
  -DGLFW_USE_MIR=ON \
  -DGLFW_BUILD_DOCS=OFF \
  -DGLFW_BUILD_EXAMPLES=OFF \
  -DGLFW_BUILD_TESTS=OFF
"$prefix/bin/cmake" --build "$glfw_build" -j"$jobs"
rm -f "$prefix/lib/libglfw.so" "$prefix/lib/libglfw.so.3" \
  "$prefix/lib/libglfw.so.3.2"
"$prefix/bin/cmake" --install "$glfw_build"
# GLFW 3.2's static build calls the archive libglfw3.a, while both its own
# installed pkg-config metadata and existing CMake caches may request -lglfw.
# Keep the conventional linker name as an alias so incremental and clean
# builds resolve the same archive.
if [ -f "$prefix/lib/libglfw3.a" ]; then
  ln -sfn libglfw3.a "$prefix/lib/libglfw.a"
fi
printf '%s\n' \
  "prefix=$prefix" \
  'libdir=${prefix}/lib' \
  'includedir=/usr/include' \
  '' \
  'Name: GLFW' \
  'Description: Mir-enabled GLFW for osu!cpp' \
  'Version: 3.2.1' \
  'Libs: -L${libdir} -lglfw -lmirclient -lxkbcommon -ldl -lpthread -lm' \
  'Cflags: -I${includedir}' \
  > "$prefix/lib/pkgconfig/glfw3.pc"

# Build the exact Skia revision used by the Flatpak, but against Noble's
# graphics/image libraries. GN and Ninja keep this incremental without an
# extra marker file, and changes to the args invalidate only affected objects.
skia_commit=13ffba253fc7854fd3b34f67c82dfb2418dc2944
wuffs_commit=e3f919ccfe3ef542cfc983a82146070258fb57f8
if [ ! -d "$sources/skia/.git" ]; then
  git clone https://skia.googlesource.com/skia.git "$sources/skia"
fi
git -C "$sources/skia" checkout "$skia_commit"
if [ ! -d "$sources/skia/third_party/externals/wuffs/.git" ]; then
  mkdir -p "$sources/skia/third_party/externals"
  git clone https://skia.googlesource.com/external/github.com/google/wuffs-mirror-release-c.git \
    "$sources/skia/third_party/externals/wuffs"
fi
git -C "$sources/skia/third_party/externals/wuffs" checkout "$wuffs_commit"
cd "$sources/skia"
gn gen out/click --args='is_official_build=true cc="clang-22" cxx="clang++-22" ar="llvm-ar-22" extra_cflags=["-O3", "-flto=full", "-stdlib=libc++"] extra_ldflags=["-flto=full", "-fuse-ld=lld-22", "-stdlib=libc++"] skia_enable_ganesh=true skia_use_gl=true skia_use_egl=true skia_use_vulkan=false skia_use_x11=false skia_use_libpng_decode=true skia_use_libpng_encode=true skia_use_system_libpng=true skia_use_libjpeg_turbo_decode=true skia_use_system_libjpeg_turbo=true skia_use_libwebp_decode=true skia_use_system_libwebp=true skia_use_zlib=true skia_use_system_zlib=true skia_use_freetype=true skia_use_system_freetype2=true skia_use_fontconfig=true skia_enable_fontmgr_custom_directory=true skia_enable_fontmgr_custom_embedded=true skia_use_icu=false skia_use_harfbuzz=false skia_use_expat=false skia_use_dng_sdk=false skia_enable_skshaper=false skia_enable_skottie=false skia_enable_pdf=false skia_enable_svg=false skia_enable_tools=false'
ninja -C out/click -j"$jobs" skia
install -Dm644 out/click/libskia.a "$prefix/lib/libskia.a"
mkdir -p "$prefix/include/skia/include" "$prefix/include/skia/modules"
cp -a include/. "$prefix/include/skia/"
cp -a include/. "$prefix/include/skia/include/"
cp -a modules/skcms "$prefix/include/skia/modules/"

# Expose Skia's complete static dependency closure to ordinary pkg-config
# consumers; unlike FFmpeg, every item here is part of the Noble build image.
sed "s|^prefix=.*|prefix=$prefix|" "$ROOT/click/skia.pc" |
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
skia_link=$(pkg-config --libs skia)
case " $skia_link " in
  *" -lfreetype "*" -lwebpdemux "*) ;;
  *)
    echo "broken skia.pc dependency closure: $skia_link" >&2
    exit 1
    ;;
esac

"$prefix/bin/cmake" -S "$ROOT/standalone" -B "$app_build" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR" \
  -DCMAKE_C_COMPILER=clang-22 \
  -DCMAKE_CXX_COMPILER=clang++-22 \
  -DCMAKE_AR=llvm-ar-22 \
  -DCMAKE_NM=llvm-nm-22 \
  -DCMAKE_RANLIB=llvm-ranlib-22 \
  -DCMAKE_INTERPROCEDURAL_OPTIMIZATION=OFF \
  -DCMAKE_C_FLAGS="$CFLAGS" \
  -DCMAKE_CXX_FLAGS="$CXXFLAGS" \
  -DCMAKE_EXE_LINKER_FLAGS="$LDFLAGS -stdlib=libc++" \
  -DOSU_INSTALL_LIBRARY_PACKAGE=OFF \
  -DOSU_STATIC_DEPS=ON \
  -DOSU_STATIC_VIDEO_DEPS=OFF \
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
# GLFW is linked statically. Remove a copy left by an older recipe so it cannot
# accidentally enter a package assembled from an existing install directory.
rm -f "$runtime_lib/libglfw.so.3"
ldd "$INSTALL_DIR/bin/osu_client" | awk '$2 == "=>" && $3 ~ /^\// { print $3 }' |
while IFS= read -r library; do
  name=${library##*/}
  case "$name" in
    libc.so.*|libm.so.*|libmvec.so.*|libdl.so.*|libpthread.so.*|librt.so.*|\
    libgcc_s.so.*|libwayland-*.so.*|libEGL.so.*|libGL.so.*|libGLES*.so.*|\
    libOpenGL.so.*|libGLdispatch.so.*|libdrm.so.*|libgbm.so.*|libva.so.*|\
    libsystemd.so.*|libffi.so.*|libcap.so.*|libglib-2.0.so.*|libpcre2-8.so.*|\
    libmirclient.so.*|libmir1client.so.*|libmircommon.so.*|libmircore.so.*|\
    libmircookie.so.*|\
    libdecor-0.so.*)
      continue
      ;;
  esac
  cp -L "$library" "$runtime_lib/$name"
done
