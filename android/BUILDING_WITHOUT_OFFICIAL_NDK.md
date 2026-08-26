# Building osu!cpp for Android without the official NDK binary distribution

This document records the source-built Android toolchain used for the
`android` branch on an AArch64 Alpine/postmarketOS host. It deliberately does
not use Gradle, the Android SDK command-line tools package, or the NDK binary
archive. Those are not proprietary software; they are somebody else's build of
open source, and this is the build instead. The host compiler, CMake, Ninja,
`aapt2`, Java, and `zipalign` come from the Linux distribution.

This is a description of the working development environment, not a generic
replacement for the NDK. It targets one ABI (`arm64-v8a`) and API 27, and only
contains the platform libraries needed by this project.

## Resulting layout

The examples use this root:

```sh
export ANDROID_FREE="$HOME/android-free"
export ANDROID_API=27
export ANDROID_TRIPLE=aarch64-linux-android
export ANDROID_LIBDIR="$ANDROID_FREE/sysroot/usr/lib/$ANDROID_TRIPLE/$ANDROID_API"
```

The final tree has these important directories:

```text
android-free/
  clang-resource/       Clang resource directory plus compiler-rt builtins
  prefix/               Static third-party libraries and pkg-config files
  runtime-install/      Staging installation of libc++, libc++abi and libunwind
  sysroot/              Bionic, Android API headers, libc++ and API stub DSOs
  src/                  AOSP, LLVM and third-party source checkouts
  build/                Out-of-tree build directories and generated stubs
```

Install the ordinary host tools first. Package names vary, but the required
programs are Clang/LLD 22, CMake, Ninja, Git, Python 3, Java (`javac`, `jar`,
`keytool`), `aapt2`, `zipalign`, Autoconf/Automake/Libtool for projects which
still require them, and common archive utilities. The build used the system
Clang because the host itself is AArch64.

## Source checkouts

Use matching Android platform revisions for the AOSP repositories. The
working source layout was:

```text
src/bionic                         platform/bionic
src/base                           platform/frameworks/base
src/native                         platform/frameworks/native
src/logging                        platform/system/logging
src/ndk                            platform/ndk
src/soong                          platform/build/soong
src/wilhelm                        platform/frameworks/wilhelm
src/llvm-project                   llvm/llvm-project
```

Do not assume that an Android platform tag exists in every standalone Git
repository. Resolve the tag in the AOSP manifest or choose a mutually
compatible released revision, then check out the resulting commit in each
repository. The earlier attempt to clone a nonexistent
`android-14.0.0_r75` branch from the NDK repository failed for this reason.

The project also needs the NDK native app glue sources at:

```text
$ANDROID_FREE/src/ndk/sources/android/native_app_glue/
```

## Sysroot headers

Assemble `$ANDROID_FREE/sysroot/usr/include` from Bionic's public headers and
the public Android native headers required by the application. Important
families include:

```text
android/*.h
EGL/*.h
GLES2/*.h
GLES3/*.h
SLES/*.h
aaudio/*.h
```

Headers in modern AOSP have moved compared with old NDK-building tutorials.
For example, do not expect all of these historical directories to exist:

```text
bionic/libc/arch-arm64/include
frameworks/native/libs/nativewindow/include
frameworks/native/libs/arect/include
frameworks/native/include
```

Locate headers in the checked-out revision instead of blindly copying those
old paths. Keep the normal unified include layout in the sysroot.

Skia's Android build also expects `cpu-features.c` in an NDK-compatible tree.
The compatibility path used here was:

```text
$ANDROID_FREE/ndk-compat/sources/android/cpufeatures/cpu-features.c
```

## Generating Android API stub libraries

The linker needs DSOs which contain the public symbol names and version
definitions. Empty DSOs are insufficient: they let `-lfoo` succeed but leave
every platform call undefined. Generate stubs from AOSP map files using
Soong's `ndkstubgen`.

Create an API map from `ndk/meta/platforms.json`. The aliases object is the
starting point. Add preview codenames found in the selected map files; for the
revision used here `VanillaIceCream` mapped to the future API value `10000`.
The resulting file was stored at:

```text
$ANDROID_FREE/build/stubs/api-map.json
```

Run the generator with Soong's `cc` directory on `PYTHONPATH`:

```sh
export STUB_DIR="$ANDROID_FREE/build/stubs/generated"
mkdir -p "$STUB_DIR" "$ANDROID_LIBDIR"

PYTHONPATH="$ANDROID_FREE/src/soong/cc" python3 "$ANDROID_FREE/src/soong/cc/ndkstubgen/__init__.py" \
--api "$ANDROID_API" \
--arch arm64 \
--api-map "$ANDROID_FREE/build/stubs/api-map.json" \
SYMBOL_MAP \
"$STUB_DIR/LIBRARY.c" \
"$STUB_DIR/LIBRARY.map" \
"$STUB_DIR/LIBRARY.symbols"
```

Compile each generated source as an API stub DSO:

```sh
clang-22 \
--target="$ANDROID_TRIPLE$ANDROID_API" \
--sysroot="$ANDROID_FREE/sysroot" \
-fPIC \
-nostdlib \
-shared \
"$STUB_DIR/LIBRARY.c" \
-Wl,--version-script="$STUB_DIR/LIBRARY.map" \
-Wl,-soname,libLIBRARY.so \
-o "$ANDROID_LIBDIR/libLIBRARY.so"
```

Generate at least `libc`, `libm`, `libdl`, `liblog`, `libandroid`, `libEGL`,
`libGLESv2`, `libGLESv3`, `libaaudio`, `libOpenSLES`, and
`libnativewindow`. Their map files in this source layout include:

```text
src/bionic/libc/libc.map.txt
src/bionic/libm/libm.map.txt
src/bionic/libdl/libdl.map.txt
src/logging/liblog/liblog.map.txt
src/native/opengl/libs/libEGL.map.txt
src/native/opengl/libs/libGLESv2.map.txt
src/native/opengl/libs/libGLESv3.map.txt
src/native/libs/nativewindow/libnativewindow.map.txt
src/wilhelm/src/libOpenSLES.map.txt
```

The `libandroid` map was located from the AOSP native sources. Verify every
stub with `readelf`; for example:

```sh
readelf -Ws "$ANDROID_LIBDIR/libnativewindow.so" | grep ANativeWindow_setBuffersTransform
readelf -d "$ANDROID_LIBDIR/libEGL.so" | grep SONAME
```

These DSOs are link-time stubs only and must not be packaged in the APK. The
real libraries are supplied by Android.

## compiler-rt builtins

Build the AArch64 Android compiler-rt builtins with the system Clang. Point
CMake at the real LLVM tools on Alpine:

```text
/usr/lib/llvm22/bin/llvm-ar
/usr/lib/llvm22/bin/llvm-ranlib
```

Do not invent `$ANDROID_FREE/tools/llvm-ranlib` paths. Configure cross
compilation with target `aarch64-linux-android27`, the assembled sysroot,
`CMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY`, and builtins-only
compiler-rt options. Disable AArch64 PAC emulation if the selected LLVM source
tree lacks its SipHash checkout; otherwise fetch LLVM's required
`third-party/siphash` source. The required result is:

```text
$ANDROID_FREE/build/compiler-rt/lib/linux/libclang_rt.builtins-aarch64-android.a
```

Install or copy it to:

```text
$ANDROID_FREE/clang-resource/lib/linux/libclang_rt.builtins-aarch64-android.a
```

The `__emutls_get_address` symbol required by libc++ comes from this archive.

## libc++, libc++abi and libunwind

Build the LLVM runtimes from source for `aarch64-linux-android27`, using the
same sysroot and compiler-rt builtins. The LLVM checkout must be complete:
new libc++ floating-point `from_chars` sources include LLVM libc shared
headers such as:

```text
libc/shared/fp_bits.h
libc/src/__support/FPUtil/FPBits.h
libc/hdr/stdint_proxy.h
```

Missing any of those indicates a partial or mismatched LLVM checkout, not a
libc++ configuration problem. LLVM's runtimes configuration may also require
`llvm/utils/llvm-lit`; disable tests when building the installable runtimes if
that source is intentionally absent.

Install the complete result, not just the three archives. Required outputs
include:

```text
runtime-install/lib/libc++.a
runtime-install/lib/libc++abi.a
runtime-install/lib/libunwind.a
runtime-install/include/c++/v1/
runtime-install/share/libc++/v1/
runtime-install/lib/libc++.modules.json
```

Copy the headers, module sources, module metadata, and libraries into the
sysroot. The project expects the metadata at:

```text
$ANDROID_FREE/sysroot/usr/lib/libc++.modules.json
```

and the C++ module sources under:

```text
$ANDROID_FREE/sysroot/usr/share/libc++/v1/
```

Bionic's `ctype.h` normally uses internal-linkage inline functions, which
Clang cannot export from `import std`. The project toolchain compiles C++ with
`-D__BIONIC_CTYPE_INLINE=inline` to give those definitions compatible
linkage.

## Android platform resources

Build the framework resource package from `frameworks/base/core/res` using
the matching AOSP sources. This build has always used that
`framework-res.apk` directly as the `aapt2 link -I` input; it does not build
or use `android.jar`.

Modern platform resources use feature-flagged manifest attributes. A raw
`aapt2 link` may report missing values for `android:featureFlag`; provide the
matching feature flag input or prepare the resource manifest consistently
with the platform build. The public resource file in this revision is split
between `public-final.xml` and `public-staging.xml`, rather than a single
`public.xml`. Set the platform SDK level high enough for adaptive icon
resources; otherwise `aapt2` rejects `<adaptive-icon>` as requiring API 26.

`--package-id 0x01` cannot be combined with `--shared-lib`, and a regular app
link only accepts IDs in the `0x7f` to `0xff` range. Framework resources need
the platform-specific aapt2 mode rather than those incompatible options.

Set the resulting resource APK at project configuration time:

```text
-DOSU_ANDROID_FRAMEWORK_RES_APK=$ANDROID_FREE/build/framework/framework-res.apk
```

The former `OSU_ANDROID_PLATFORM_JAR` spelling is retained only as a
deprecated cache alias for existing build directories.

## Building apksigner from source

The working signer is an `apksig` JAR built with `javac` and `jar` and stored
at:

```text
$HOME/.local/lib/apksigner/apksigner.jar
```

Do not compile the optional AWS and GCP KMS implementations unless their SDKs
are available. Keep the common KMS interfaces required by
`SignerEngineFactory`, while excluding the provider implementations. The CLI
also has an optional Conscrypt provider reference; either provide Conscrypt
or build the CLI without that optional registration. The resulting command
must work:

```sh
java -jar "$HOME/.local/lib/apksigner/apksigner.jar" --version
```

Pass it to CMake with `OSU_ANDROID_APKSIGNER_JAR`.

## Optional system document picker

`OSU_ANDROID_SYSTEM_FILE_PICKER=ON` builds a minimal DEX bridge around
`NativeActivity`. It uses Android's own `ACTION_OPEN_DOCUMENT` UI for beatmap
imports and `ACTION_CREATE_DOCUMENT` for exported videos. The selected
`content://` stream is copied by the C++ platform backend; no application
file handling lives in Java and Gradle is not used.

This mode explicitly requires `javac` and `d8`. The bridge is compiled
against small compile-only Android API declarations under
`android/java-stubs`; only the bridge classes are passed to `d8`, so those
declarations never enter the APK. Consequently no `android.jar` is needed.
Set `OSU_ANDROID_SYSTEM_FILE_PICKER=OFF` to omit `classes.dex`, retain the
plain `android.app.NativeActivity`, and avoid both bridge tools.

The dexer may be supplied either as a `d8` executable on `PATH` or as the R8
build output:

```text
-DOSU_ANDROID_D8_JAR=$ANDROID_FREE/src/r8/build/libs/r8.jar
```

In the latter case CMake invokes `com.android.tools.r8.D8` with the configured
host Java runtime.

## Third-party libraries

Everything in `$ANDROID_FREE/prefix` is built for the Android target, normally
as a static library with position-independent code. Each pkg-config file must
describe the target prefix rather than host `/usr/local` paths.

The working dependency set contains:

```text
Skia (Ganesh GLES)
OpenAL Soft with Oboe
Oboe, AAudio and OpenSL ES stubs
libzip and zlib
libsndfile
Ogg, Vorbis, FLAC and Opus
mpg123
liblzma
```

Important lessons from the individual builds:

- Build libraries but disable executables, examples, tests, benchmarks and
  tools. Executables require Android CRT startup objects which this minimal
  link sysroot intentionally does not provide.
- Android pthread functions are in libc; there is no separate `libpthread` or
  `libpthreads`. The project seeds `FindThreads` accordingly.
- A fabricated empty `libc.so` cannot link libc++ because it lacks `malloc`,
  `free`, pthread functions and the rest of Bionic. Use generated API stubs.
- libzip must not select Darwin's `sys/attr.h` source on Android.
- Do not use Chromium's modified `zconf.h`; its `chromeconf.h` dependency is
  not part of normal zlib. Install headers from the zlib build used here.
- Disable FLAC microbenchmarks and programs; only install `libFLAC`.
- mpg123's generated Autotools files can be rebuilt accidentally when source
  timestamps change. Install Autoconf and Automake or restore timestamps and
  use release-generated files.
- Build only the liblzma library; the `xz`, `xzdec`, `lzmadec` and `lzmainfo`
  executables need CRT startup objects.
- OpenAL Soft's Android backend uses Oboe. Oboe also compiles its OpenSL ES
  fallback at API 27, so both AAudio and OpenSL ES headers/stubs are needed.
- Oboe namespace errors around `flowgraph` indicate incompatible source
  configuration or revision combinations. Use the Oboe revision expected by
  OpenAL Soft and its normal namespace definitions.
- Some bundled fmt versions fail to see Bionic `malloc`/`free` if configured
  with the wrong platform macros or headers. Ensure the Android target and
  Bionic stdlib headers are selected consistently.

Example pkg-config closures used by the final project were:

```text
openal: -I.../prefix/include -L.../prefix/lib -lopenal -loboe -lOpenSLES -laaudio -landroid -llog -ldl -lm
skia:   -I.../prefix/include -I.../prefix/include/skia -L.../prefix/lib -lskia -lskcms -lpng -ljpeg -lwebp -lfreetype2 -lzlib -lcpu-features -lEGL -lGLESv3 -landroid -llog -ldl -lm
sndfile: -I.../prefix/include -I.../prefix/include/opus -L.../prefix/lib -lsndfile -lFLAC -lvorbisenc -lvorbis -logg -lopus -lm
```

## Minimal Skia checkout and build

Skia's full dependency sync is unnecessary for this GLES build. The retained
`third_party/externals` directories were:

```text
freetype
libjpeg-turbo
libpng
libwebp
zlib
```

Configure Ganesh with GLES and without Vulkan. Confirm the effective value,
not merely the text in a copied command:

```sh
gn args --list out/android | grep -A3 skia_use_vulkan
```

It must report `Current value = false`. If Vulkan is accidentally enabled,
Skia requires the complete Vulkan and `vk_video` header closure. The build
used the Android NDK font manager disabled because it pulls ICU headers such
as `unicode/uchar.h`; use the portable font manager appropriate to the
project instead. The resulting static archives included:

```text
libskia.a
libskcms.a
libpng.a
libjpeg.a
libwebp.a
libfreetype2.a
libzlib.a
libcpu-features.a
```

Install Skia headers, archives and a correct `skia.pc` into the prefix. Give
the pkg-config file the actual Skia revision rather than the placeholder
version `1`.

## Configuring and building osu!cpp

The repository toolchain file centralizes the target, sysroot, Clang resource
directory, libc++ modules, LLVM archive tools, search roots, and Android
pthread behavior.

Configure with paths adapted to the locally built framework resources and
signer:

```sh
export ANDROID_FREE="$HOME/android-free"

cmake -S standalone -B build/android-free -G Ninja \
-DCMAKE_TOOLCHAIN_FILE="$PWD/cmake/toolchains/android-free.cmake" \
-DCMAKE_BUILD_TYPE=Release \
-DCMAKE_PREFIX_PATH="$ANDROID_FREE/prefix" \
-DOSU_ANDROID_FRAMEWORK_RES_APK="$ANDROID_FREE/build/framework/framework-res.apk" \
-DOSU_ANDROID_APKSIGNER_JAR="$HOME/.local/lib/apksigner/apksigner.jar" \
-DOSU_ANDROID_SYSTEM_FILE_PICKER=OFF
```

Then build the shared native application and signed APK:

```sh
cmake --build build/android-free -j"$(nproc)"
```

The output is:

```text
build/android-free/apk/osu-cpp.apk
```

The CMake packaging rule performs resource compilation, manifest linking,
native library staging, zip alignment, debug-key creation, and APK signing.
It uses native/min API 27 and target SDK 35. No Gradle project or shell build
driver is involved.

Install in Waydroid with:

```sh
waydroid shell pm uninstall io.github.j4niwzis.osu_cpp
waydroid app install build/android-free/apk/osu-cpp.apk
```

## Waydroid notes

On postmarketOS, DNS inside Waydroid worked while external traffic did not.
The Waydroid NAT rule was present, but Docker installed a later forwarding
chain with a drop policy. Allow Waydroid in `DOCKER-USER`:

```sh
iptables -I DOCKER-USER 1 -i waydroid0 -j ACCEPT
iptables -I DOCKER-USER 1 -o waydroid0 -m conntrack --ctstate ESTABLISHED,RELATED -j ACCEPT
```

Persist those rules using a small systemd oneshot service after both Docker
and Waydroid networking are available.

Waydroid desktop mode keeps its virtual display portrait and may ignore a
fixed landscape Activity. A resizable landscape Activity became a centered
freeform `966x515` task; an unresizable one became a top-aligned fixed-
orientation letterbox. The application therefore does not request Android
landscape. It allocates a swapped landscape `ANativeWindow` buffer and applies
`ANATIVEWINDOW_TRANSFORM_ROTATE_90`; SurfaceFlinger rotates that buffer during
composition. Touch coordinates are transformed into the same landscape
coordinate system. This avoids an extra Skia render target and blit.

The application also enters immersive sticky mode through the NativeActivity
JNI handle and reapplies it after focus returns, because Android may restore
system bars after showing another system surface.

Useful diagnostics are:

```sh
dumpsys package io.github.j4niwzis.osu_cpp | grep -E 'minSdk|targetSdk|resizeable'
dumpsys activity activities | grep -A 40 io.github.j4niwzis.osu_cpp
logcat -d -t 200 | grep -E 'osu_cpp|osu!cpp|NativeActivity|AndroidRuntime|FATAL'
```

The healthy APK reports `minSdk=27 targetSdk=35`. A task dump containing
`letterboxReason=FIXED_ORIENTATION` means Android orientation policy is still
controlling the surface instead of the native buffer-transform path.

## What should eventually be automated

The manual environment proved that a fully source-built toolchain is viable,
but it should eventually move to a separate reproducible toolchain-builder
repository. That builder should pin every source commit, download only the
required source closures, generate API maps and stubs, build compiler-rt and
LLVM runtimes, build the static dependency graph, produce `framework-res.apk`
and `apksigner.jar`, and emit pkg-config metadata plus a manifest of hashes.

CMake remains the application build system. The toolchain bootstrap itself
can be a purpose-built program or conventional build orchestration; it does
not need to force every AOSP preparation step into CMake.
