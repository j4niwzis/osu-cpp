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
        ports = pkgs.runCommand "osu-cpp-cme-ports" { } (
          pkgs.lib.concatStrings (pkgs.lib.mapAttrsToList (name: source: ''
            mkdir -p $out/${name}
            cat > $out/${name}/port.cmake <<'PORT'
            cme_declare_port(
              NAME ${builtins.replaceStrings [ "_" ] [ "-" ] name}
              SOURCE_DIR "${source}")
            PORT
          '') sources));

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
          ];
          # What a desktop build reaches through rather than builds.
          buildInputs = with pkgs; [
            libGL libglvnd libxkbcommon wayland wayland-protocols
            xorg.libX11 xorg.libXrandr xorg.libXinerama xorg.libXcursor
            xorg.libXi alsa-lib libpulseaudio dbus systemd
          ];

          cmakeDir = "../standalone";
          cmakeFlags = [
            "-DCME_OFFLINE=ON"
            "-DCME_ARCHIVE=${cme}"
            "-DCME_OVERLAYS=${ports}"
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
