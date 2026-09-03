# Building this client with Nix.
#
# A Nix build has no network either, and unlike flatpak-builder it does not
# even have a working directory it can be handed things in: every input is a
# store path, decided before the build starts. So the shape is the same as
# the Flatpak one and the mechanism is identical -- sources.nix says what to
# fetch, and a generated overlay of port declarations says where each one
# landed, which is the only thing cmake-everywhere needs to be told.
#
# sources.nix is generated from cme-lock.json by tools/lock-to-nix.py, which
# comes with cmake-everywhere. Regenerate it after a build that had a
# network; it holds a revision and a digest per library and nothing else.
{
  description = "osu!cpp, a native client for osu! beatmaps";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = { self, nixpkgs, flake-utils }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        pkgs = import nixpkgs { inherit system; };
        sources = import ./sources.nix { inherit pkgs; };

        # One port declaration per library: where its sources are, and
        # nothing else. What each library is, and how it is built, the
        # registry inside cmake-everywhere still says -- an overlay is read
        # before it and only fills in what it knows.
        # What each library's sources are, as a shell script that puts
        # them somewhere the build may write.
        #
        # A store path is read-only, and a project that writes into its own
        # source tree cannot be built from one: zlib renames zconf.h out of
        # the way while configuring, and stopped on "Permission denied" --
        # in a directory nothing was supposed to write to and zlib was never
        # told about. So every source is copied first, and an archive is
        # unpacked while it is copied, because the digest the lock holds is
        # the archive's.
        unpack = pkgs.lib.concatStrings (pkgs.lib.mapAttrsToList (name: source: ''
          port=${builtins.replaceStrings [ "_" ] [ "-" ] name}
          mkdir -p "$ports/$port" "$sources/$port"
          if [ -d ${source} ]; then
            cp -r ${source}/. "$sources/$port"
          else
            tar xf ${source} -C "$sources/$port" --strip-components=1
          fi
          chmod -R u+w "$sources/$port"
          cat > "$ports/$port/port.cmake" <<PORT
          cme_declare_port(
            NAME $port
            SOURCE_DIR "$sources/$port")
          PORT
        '') sources);

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
            sources=$TMPDIR/cme-sources
            mkdir -p "$ports" "$sources"
          '' + unpack + ''
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

            # Said here rather than in cmakeFlags, because these have names
            # only the builder knows.
            cmakeFlagsArray+=("-DCME_OVERLAYS=$ports")
          '';

          cmakeDir = "../standalone";
          cmakeFlags = [
            "-DCME_OFFLINE=ON"
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
