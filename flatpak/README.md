# Flatpak

The manifest supplies CPM, skiff and skiff-widgets as Flatpak sources, so
CMake never needs network access inside the build sandbox.

Install the SDK and builder:

```sh
flatpak install flathub org.freedesktop.Platform//25.08 \
  org.freedesktop.Sdk//25.08 \
  org.freedesktop.Sdk.Extension.llvm22//25.08 \
  org.flatpak.Builder
```

From the repository root, build and install it for the current user:

```sh
flatpak run org.flatpak.Builder --user --install --force-clean \
  flatpak-build flatpak/io.github.j4niwzis.osu_cpp.yml
flatpak run io.github.j4niwzis.osu_cpp
```

The manifest permits Wayland with an X11 fallback, audio, GPU acceleration and
network access. Beatmap import uses the file chooser portal, so it deliberately
does not grant broad access to the host filesystem.

Before a Flathub submission, add at least one real Linux screenshot to the
MetaInfo file using a stable, commit-pinned HTTPS URL. Flathub requires one,
but the repository does not currently contain an authentic application
screenshot to publish.
