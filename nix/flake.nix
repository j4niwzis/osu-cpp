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
        packages.default = pkgs.stdenv.mkDerivation {
          pname = "osu-cpp";
          version = "1.0.0";
          src = ../.;

          nativeBuildInputs = with pkgs; [
            cmake ninja pkg-config python3 gn meson gperf
            llvmPackages_latest.clang llvmPackages_latest.lld
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

          # A home that can be written to. Nix points HOME at
          # /homeless-shelter, and the first thing that wants to put
          # something under it -- the source cache -- fails there.
          preConfigure = ''
            export HOME=$TMPDIR
            ports=$TMPDIR/cme-ports
            sources=$TMPDIR/cme-sources
            mkdir -p "$ports" "$sources"
          '' + unpack + ''
            # Said here rather than in cmakeFlags, because the directory has
            # a name only the builder knows.
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
