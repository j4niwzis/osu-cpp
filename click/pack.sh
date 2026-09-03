#!/bin/sh
# Assemble a Click package out of a staging directory.
#
#   click/pack.sh <staging directory> <output .click>
#
# A Click package is a Debian package with a manifest in it: an ar archive of
# debian-binary, control.tar.gz and data.tar.gz, which is exactly what
# dpkg-deb writes. Clickable calls its own tool for this and its own tool
# runs in a container for the device's architecture; nothing here needs to.
set -eu

staging=${1:?the directory to package}
output=${2:?where to write the .click}

test -f "$staging/manifest.json" || {
  echo "no manifest.json in $staging: a Click package is a manifest and a payload" >&2
  exit 1
}

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

# The control side: the manifest, and dpkg's own idea of what a package is.
mkdir -p "$work/DEBIAN"
cp "$staging/manifest.json" "$work/manifest.json"
name=$(sed -n 's/.*"name"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p' "$staging/manifest.json" | head -1)
version=$(sed -n 's/.*"version"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p' "$staging/manifest.json" | head -1)
architecture=$(sed -n 's/.*"architecture"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p' "$staging/manifest.json" | head -1)
: "${architecture:=arm64}"

cat > "$work/DEBIAN/control" <<CONTROL
Package: $name
Version: $version
Architecture: $architecture
Maintainer: $(sed -n 's/.*"maintainer"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p' "$staging/manifest.json" | head -1)
Description: $(sed -n 's/.*"title"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p' "$staging/manifest.json" | head -1)
Click-Version: 0.4
CONTROL

# The payload, as the device will see it.
cp -a "$staging/." "$work/"
rm -f "$work/manifest.json.orig"

dpkg-deb --build --root-owner-group "$work" "$output"
echo "wrote $output"
