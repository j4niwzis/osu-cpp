# Building this client with Nix.
#
# Libraries come from nixpkgs. CME is kept as the dependency provider so the
# CMake graph is unchanged, but CME_SYSTEM=ALWAYS makes a missing nixpkgs
# package an error instead of silently compiling another copy from source.
{
  description = "osu!cpp, a native client for osu! beatmaps";

  inputs = {
    # An exact nixos-unstable revision: repository packages without a
    # different package set appearing beneath the same source revision.
    nixpkgs.url = "github:NixOS/nixpkgs/83199d0d373dd3ac2b9a1996b1d0263f76ab7a4c";
    flake-utils.url = "github:numtide/flake-utils/11707dc2f618dd54ca8739b309ec4fc024de578b";
  };

  outputs = { self, nixpkgs, flake-utils }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        pkgs = import nixpkgs { inherit system; };
        # These are application components, not distribution libraries.
        # Keep only their pinned checkouts from the generated lock input.
        componentSources = pkgs.lib.filterAttrs
          (name: _: builtins.elem name [ "skiff" "skiff_widgets" ])
          (import ./sources.nix { inherit pkgs; });
        unpackComponents = pkgs.lib.concatStrings
          (pkgs.lib.mapAttrsToList (name: source: ''
            port=${builtins.replaceStrings [ "_" ] [ "-" ] name}
            mkdir -p "$ports/$port" "$components/$port"
            cp -r ${source}/. "$components/$port"
            chmod -R u+w "$components/$port"
            cat > "$ports/$port/port.cmake" <<PORT
            cme_declare_port(
              NAME $port
              SOURCE_DIR "$components/$port")
            PORT
          '') componentSources);
        # The provider itself, by the revision and digest cmake/get_cme.cmake
        # pins. Fetched here because the build may not fetch.
        pinned = builtins.readFile ../cmake/get_cme.cmake;
        revision = builtins.head (builtins.match
          ".*CME_PINNED \"([0-9a-f]{40})\".*" pinned);
        digest = builtins.head (builtins.match
          ".*CME_PINNED_SHA256 \"([0-9a-f]{64})\".*" pinned);
        cme = pkgs.fetchurl {
          url = "https://github.com/j4niwzis/cmake-everywhere/archive/"
            + revision + ".tar.gz";
          sha256 = digest;
        };
      in
      {
        # Clang, because this is compiled with Clang everywhere else and
        # Skia's headers say so: GCC ignores the clang:: attributes they
        # carry, warns about every one of them, and then disagrees about
        # modules. A compiler in nativeBuildInputs is not the compiler a
        # derivation is built with -- the stdenv is.
        packages.default = pkgs.llvmPackages_latest.stdenv.mkDerivation {
          pname = "osu-cpp";
          version = "1.0.0";
          src = ../.;

          nativeBuildInputs = with pkgs; [
            cmake ninja pkg-config python3 gn meson gperf
            llvmPackages_latest.lld
            # A program rather than a library: glfw generates its Wayland
            # protocol bindings with it, and stops when it is not there.
            wayland-scanner
          ];
          # What a desktop build reaches through rather than builds.
          buildInputs = with pkgs; [
            libGL libglvnd libxkbcommon wayland wayland-protocols
            xorg.libX11 xorg.libXrandr xorg.libXinerama xorg.libXcursor
            xorg.libXi alsa-lib libpulseaudio dbus systemd
            boost skia libzip libsndfile mpg123 openal glfw
            xz zlib libpng libjpeg_turbo freetype expat
            flac fmt libogg opus libvorbis vulkan-headers
            # Asio's TLS has one backend and this is it. 3.0 and later,
            # because everything before it carried a licence the AGPL does
            # not combine with.
            openssl
          ];

          # No _FORTIFY_SOURCE.
          #
          # glibc's fortified headers declare the printf family through
          # clang overloads with internal linkage, and libstdc++'s std
          # module exports those names: "using declaration referring to
          # 'fprintf' with internal linkage cannot be exported". The native
          # build says the same thing with -Wp,-U_FORTIFY_SOURCE.
          hardeningDisable = [ "fortify" "fortify3" ];

          # A home that can be written to. Nix points HOME at
          # /homeless-shelter, and the first thing that wants to put
          # something under it -- the source cache -- fails there.
          preConfigure = ''
            export HOME=$TMPDIR
            ports=$TMPDIR/cme-ports
            components=$TMPDIR/cme-components
            mkdir -p "$ports" "$components"
          '' + unpackComponents + ''
            # Where the standard library this compiler uses keeps the
            # source of its std module.
            #
            # libc++ ships a manifest saying so and CMake finds it by
            # itself; libstdc++ ships none, and the manifest written here
            # has to name the same headers the compiler includes -- naming
            # another copy of GCC in the store got as far as scanning
            # bits/std.cc and stopped at "bits/stdc++.h not found".
            searched=$(echo | $CXX -std=c++23 -x c++ -E -v - 2>&1 \
              | sed -n '/#include <\.\.\.> search starts here:/,/End of search list/p' \
              | sed -n 's/^ //p')
            # And said outright, as flags.
            #
            # Nix gives the compiler its include directories through a
            # wrapper script that sets NIX_CFLAGS_COMPILE. clang-scan-deps
            # does not run that script -- it reads the command line and
            # scans by itself -- so the module scan saw a compiler with no
            # C++ headers at all and stopped on bits/stdc++.h, a file in a
            # directory the compiler uses and the command line never named.
            includes=""
            std=""
            for dir in $searched; do
              includes="$includes -isystem $dir"
              if [ -f "$dir/bits/std.cc" ]; then
                std="$dir/bits/std.cc"
              fi
            done
            # The two directories beside the C++ headers that belong to the
            # same standard library: the machine-dependent one that holds
            # c++config.h, and backward, which is where <strstream> is and
            # where the std module reaches for it.
            if [ -n "$std" ]; then
              root=$(dirname "$(dirname "$std")")
              for extra in "$root/backward" "$root"/*/bits/c++config.h; do
                case "$extra" in
                  */bits/c++config.h) extra=$(dirname "$(dirname "$extra")") ;;
                esac
                if [ -d "$extra" ]; then
                  includes="$includes -isystem $extra"
                fi
              done
            fi
            echo "include directories:$includes"
            cmakeFlagsArray+=("-DCMAKE_CXX_FLAGS=$includes")
            if [ -n "$std" ]; then
              echo "std module source: $std"
              cat > "$TMPDIR/libstdc++.modules.json" <<JSON
            {
              "version": 1,
              "revision": 1,
              "modules": [
                { "logical-name": "std", "source-path": "$std",
                  "is-std-library": true },
                { "logical-name": "std.compat",
                  "source-path": "''${std%std.cc}std.compat.cc",
                  "is-std-library": true }
              ]
            }
            JSON
              cmakeFlagsArray+=("-DCMAKE_CXX_STDLIB_MODULES_JSON=$TMPDIR/libstdc++.modules.json")
            else
              echo "no bits/std.cc among the compiler's include directories;"
              echo "leaving CMake to find whatever manifest the standard"
              echo "library ships."
            fi
            cmakeFlagsArray+=("-DCME_OVERLAYS=$ports")
          '';

          cmakeDir = "../standalone";
          cmakeFlags = [
            "-DCME_OFFLINE=ON"
            "-DCME_SYSTEM=ALWAYS"
            "-DCME_SYSTEM_OSUCPP=NEVER"
            "-DCME_SYSTEM_SKIFF=NEVER"
            "-DCME_SYSTEM_SKIFF-WIDGETS=NEVER"
            "-DCME_ARCHIVE=${cme}"
            "-DCMAKE_BUILD_TYPE=Release"
          ];

          meta = with pkgs.lib; {
            description = "A native C++ client for playing osu! beatmaps";
            homepage = "https://github.com/j4niwzis/osu-cpp";
            license = licenses.agpl3Only;
            platforms = platforms.linux;
          };
        };
      });
}
