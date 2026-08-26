export module client.appoverlays;

import std;
import osu;
import platform.input;
import platform.capabilities;
import skia;
import skin;
import platform.audio_engine;
import client.hitsoundmix;
import client.mods;
import platform.dialogs;
import client.settingspanel;
import client.util;
import client.video;
import client.videoexport;

export namespace client {

template <class Host> class AppOverlays {
public:
  explicit AppOverlays(Host &app) : fApp(app) {}

  // ---- Settings overlay ---------------------------------------------------
  //
  // The panel itself lives in client.settingspanel; this only bridges it to
  // the app's input and to applying the values.

  void toggleSettings() {
    fApp.fSettingsPanel.toggle(fApp.wallMs());
    if (!fApp.fSettingsPanel.open()) {
      fApp.fSettings.save();
      this->applySettings();
    }
  }

  void closeSettings() {
    if (fApp.fSettingsPanel.open()) {
      fApp.fSettingsPanel.close(fApp.wallMs());
      fApp.fSettings.save();
      this->applySettings();
    }
  }

  void drawSettings(skia::SkCanvas *canvas) {
    fApp.fSettingsPanel.render(canvas);
  }

  // The interface scale decides how many pixels a unit is, and every surface,
  // layout and pointer position is derived from it. Re-deriving them is
  // exactly what a resize does, so it goes through that path -- and that path
  // throws the surfaces away, clears the blit history and repaints the window
  // whole. At the rate a mouse moves, that is a flickering crawl, so it is
  // deferred until the handle is let go.
  //
  // Nothing else may read the chosen value: until this runs, the frame, the
  // trees and the pointer are all still in the old one, and mixing the two
  // puts presses beside what they are aimed at.
  void applyInterfaceScale() {
    if (fApp.fSettingsPanel.dragging()) {
      return;
    }
    if (std::abs(fApp.pixelScale() - fApp.fFrame.uiScale()) > 1e-4f) {
      fApp.resize(fApp.fWin.fPixelW, fApp.fWin.fPixelH);
    }
  }

  bool settingsClick(float x, float y, bool pressed) {
    // Whatever it hit, the panel draws it next frame.
    fApp.fSettingsPanel.touched();
    const auto hit = fApp.fSettingsPanel.click(x, y, pressed, fApp.fSettings);
    if (hit == client::SettingsPanel::Hit::kChanged) {
      this->applySettings();
      if (!pressed || !fApp.fSettingsPanel.dragging()) {
        fApp.fSettings.save();
      }
    }
    if (!pressed) {
      // Letting go is when a deferred change lands, whether or not the panel
      // called this release a change.
      this->applyInterfaceScale();
    }
    return hit != client::SettingsPanel::Hit::kNone;
  }

  void dragSetting(float x) {
    fApp.fSettingsPanel.touched();
    if (fApp.fSettingsPanel.drag(x, fApp.fSettings)) {
      this->applyAudioSettings(); // cheap part only while dragging
    }
  }

  void scrollSettings(float delta) {
    fApp.fSettingsPanel.scroll(delta, static_cast<float>(fApp.fWin.fScreenH));
  }

  void applyAudioSettings() {
    fApp.fAudio.setVolume(this->musicGain());
    fApp.fMirrors.setVolume(this->musicGain());
    fApp.fJudgements.setGain(this->effectGain());
  }

  [[nodiscard]] float musicGain() const {
    return fApp.fSettings.value("master") * fApp.fSettings.value("music") *
           platform::audio::kMusicHeadroom;
  }
  [[nodiscard]] float effectGain() const {
    return fApp.fSettings.value("master") * fApp.fSettings.value("effect") *
           platform::audio::kEffectHeadroom;
  }

  // Difficulties are ordered by the rating being shown, which means the
  // order changes when that setting does. Safe now that the loaded set is
  // matched to the cached list by name rather than by re-sorting it, but the
  // selection is a position in that list, so it is carried across by name.
  void applyStarOrder() {
    const int chosen = fApp.fSettings.choice("stars");
    if (chosen == fApp.fAppliedStarChoice) {
      return;
    }
    fApp.fAppliedStarChoice = chosen;
    fApp.fLibrary.setRanked(chosen == 1);
    fApp.fLibrary.sortLibraryByStars();
  }


  void applySettings() {
    this->applyStarOrder();
    this->applyAudioSettings();
    fApp.setFullscreen(fApp.fSettings.flag("fullscreen"));
    fApp.fMirrors.setPreferred(
        static_cast<std::size_t>(std::max(0, fApp.fSettings.choice("mirror"))));
    this->applyInterfaceScale();
    const float dim = fApp.fSettings.value("dim");
    if (std::abs(dim - fApp.fAppliedDim) > 1e-4f) {
      fApp.fAppliedDim = dim;
      fApp.fView.preScaleBackground(fApp.gameplayCtx(nullptr));
    }
    fApp.fSwapIntervalRequest.store(fApp.fSettings.flag("vsync") ? 1 : 0,
                               std::memory_order_release);
    fApp.fFrame.damageAll("settings applied");
    // Sensitivity other than 1 needs relative motion, which needs the pointer
    // grabbed; so does raw input.
    fApp.applyPointerMode();
  }

  // ---- Mod select and export dialog ---------------------------------------
  //
  // Both views live in client.overlays; this is the bridge to app state.

  [[nodiscard]] std::vector<client::ModEntry> modEntries() const {
    return {
        {"EZ", "Easy", "Larger circles, more forgiving HP drain.",
         osu::mod::kEasy, 0, platform::input::kKeyQ, 0.5},
        {"HT", "Half Time", "Less zoom... more time to react.",
         osu::mod::kHalfTime, 0, platform::input::kKeyW, 0.3},
        {"HR", "Hard Rock", "Everything just got a bit harder...",
         osu::mod::kHardRock, 1, platform::input::kKeyA, 1.06},
        {"DT", "Double Time", "Zoooooooooom...", osu::mod::kDoubleTime, 1,
         platform::input::kKeyD, 1.12},
    };
  }

  void toggleMods() { fApp.fModSelect.toggle(); }

  void drawModSelect(skia::SkCanvas *canvas) {
    fApp.fModSelect.render(canvas);
  }

  bool modClick(float x, float y) {
    return fApp.fModSelect.click(x, y, fApp.fMods);
  }

  void drawExportDialog(skia::SkCanvas *canvas) {
    fApp.fExportDialog.render(canvas);
  }

  bool exportClick(float x, float y) {
    if (!fApp.fExportDialog.open()) {
      return false;
    }
    if (fApp.fExportDialog.click(x, y)) {
      this->exportReplayVideo({});
    }
    return true;
  }

  // Said in both places: the dialog is where it belongs, and the log is where
  // it survives being missed -- which, while the export was blocking the
  // client, it always was.
  void exportFailed(std::string reason) {
    std::println(std::cerr, "[export] failed: {}", reason);
    fApp.fExportDialog.setStatus(std::move(reason));
  }

  void exportReplayVideo(std::filesystem::path output) {
    if (fApp.fVideoExporter.active()) {
      return; // one at a time
    }
    if (!fApp.fPlay.fMap || !fApp.fPlay.fEngine ||
        fApp.fPlay.fRecordedEvents.empty()) {
      this->exportFailed("nothing to export: no play recorded for this map");
      return;
    }
    const auto preset = client::kVideoPresets[static_cast<std::size_t>(
        fApp.fExportDialog.preset())];
    // A size typed into the dialog wins over the one picked from the row.
    const auto [typedWidth, typedHeight] = fApp.fExportDialog.customSize();
    client::ReplayVideoExporter::Request request;
    request.fOptions.fWidth = typedWidth > 0 ? typedWidth : preset.fWidth;
    request.fOptions.fHeight = typedHeight > 0 ? typedHeight : preset.fHeight;
    request.fOptions.fFps = 60;

    // Suggest a name describing the replay and render size. The save portal
    // decides the directory; two sizes naturally receive different names.
    const std::string stem =
        !fApp.fReplayPath.empty()
            ? fApp.fReplayPath.stem().string()
            : std::filesystem::path(fApp.fBeatmapFilename).stem().string();
    std::string safe;
    for (const char c : stem) {
      const bool awkward = static_cast<unsigned char>(c) < 0x20 || c == '/' ||
                           c == '\\' || c == ':' || c == '\'';
      safe.push_back(awkward ? '_' : c);
    }
    const std::string suggested = std::format(
        "{}-{}x{}.mp4", safe, request.fOptions.fWidth, request.fOptions.fHeight);
    if (output.empty()) {
      if constexpr (!platform::capabilities::kNativeFileDialogs) {
        return;
      }
      if (fExportPickerOpen) {
        return;
      }
      fExportPickerOpen = true;
      fApp.fExportDialog.setStatus("choosing output file...");
      auto chosen = std::make_shared<std::filesystem::path>();
      fApp.fLoader.submit(
          kExportPickerKey,
          [chosen, suggested] {
            *chosen = runExportPicker(suggested);
          },
          [this, chosen] {
            fExportPickerOpen = false;
            if (chosen->empty()) {
              fApp.fExportDialog.setStatus("export cancelled");
              return;
            }
            this->exportReplayVideo(*chosen);
          });
      return;
    }
    if (output.extension() != ".mp4") {
      output += ".mp4";
    }
    request.fOptions.fOutput = std::move(output);

    // Written out before the encoder is started: it is told about its inputs
    // once, when it is launched, and an audio path handed over afterwards
    // reached nobody -- which is why the videos had no sound.
    if (!fApp.fPlay.fMap->fMeta.fAudioFilename.empty()) {
      const auto bytes =
          fApp.fSet.findFile(fApp.fPlay.fMap->fMeta.fAudioFilename);
      if (!bytes.empty()) {
        std::error_code ec;
        const auto temporary = std::filesystem::temp_directory_path(ec);
        const auto audioPath = temporary /
                               std::filesystem::path(
                                   fApp.fPlay.fMap->fMeta.fAudioFilename)
                                   .filename();
        std::ofstream out(audioPath, std::ios::binary);
        out.write(reinterpret_cast<const char *>(bytes.data()),
                  static_cast<std::streamsize>(bytes.size()));
        out.close();
        request.fOptions.fAudio = audioPath;
        const auto mixedPath = temporary / (safe + "-mixed.wav");
        if (const auto mixed = client::mixReplayAudio(
                bytes,
                detail::fileExtension(
                    fApp.fPlay.fMap->fMeta.fAudioFilename),
                *fApp.fPlay.fMap, fApp.fPlay.fRecordedEvents, fApp.fMods,
                fApp.fPlay.fEngine->rules(), fApp.fSet, fApp.fSkin,
                this->musicGain(), this->effectGain(), mixedPath)) {
          request.fOptions.fAudio = *mixed;
        }
      }
    }
    std::println(std::cerr, "[export] writing {}",
                 request.fOptions.fOutput.string());

    // The slider bodies are built on the GPU, at one scale, and live there.
    // They were built for the window, so a 4K export drew them soft; they are
    // rebuilt for the size being rendered and then moved into memory, since a
    // thread without a context cannot read them off the GPU. The next play
    // precomputes them for the window again.
    const float exportScale =
        0.8f * std::min(static_cast<float>(request.fOptions.fWidth) /
                            static_cast<float>(osu::kPlayfieldWidth),
                        static_cast<float>(request.fOptions.fHeight) /
                            static_cast<float>(osu::kPlayfieldHeight));
    fApp.fSkin.precomputeSliderBodies(*fApp.fPlay.fMap, fApp.fComboInfo,
                                     exportScale, fApp.fContext.get());
    fApp.fSkin.flattenBodiesToRaster(fApp.fContext.get());

    request.fMap = *fApp.fPlay.fMap;
    request.fCombo = fApp.fComboInfo;
    request.fEvents = fApp.fPlay.fRecordedEvents;
    request.fMods = fApp.fMods;
    request.fRules = fApp.fPlay.fEngine->rules();
    request.fSkin = &fApp.fSkin;
    request.fFont = fApp.fFont;
    request.fDisplayFont = fApp.fDisplayFont;
    // The cursor is drawn at a size in screen pixels, which is right for a
    // window and wrong for a render: at 4K it came out a quarter of the size
    // it has on screen. Scaled by how much bigger the playfield is, it keeps
    // the size it has relative to the play.
    request.fCursorSize = fApp.fSettings.value("cursorsize") *
                          (fApp.fScale > 0.0f ? exportScale / fApp.fScale : 1.0f);
    request.fDim = fApp.fSettings.value("dim");
    request.fNoGlow = fApp.fNoGlow;
    request.fHitLighting = fApp.fSettings.flag("hitlighting");
    request.fShowCursor = fApp.fSettings.flag("cursor");
    request.fCursorTrail = fApp.fSettings.flag("cursortrail");
    request.fAttributes = fApp.fPlay.fPlayAttributes;
    for (const auto &info : fApp.fSet.fBeatmaps) {
      if (info.fMeta.fBackground.empty()) {
        continue;
      }
      const auto bytes = fApp.fSet.findFile(info.fMeta.fBackground);
      if (!bytes.empty()) {
        request.fBackground = loadImage(bytes);
        break;
      }
    }

    const std::string error = fApp.fVideoExporter.start(std::move(request));
    if (!error.empty()) {
      this->exportFailed(error);
      return;
    }
    fApp.fExportDialog.setStatus("rendering 0%");
  }

  // Nothing to step any more: the render is on its own thread. This is the
  // client noticing how far it has got and what it had to say when it stopped.
  void pollExportVideo() {
    if (!fApp.fVideoExporter.active()) {
      return;
    }
    const auto status = fApp.fVideoExporter.status();
    if (!status.fFinished) {
      fApp.fExportDialog.setStatus(std::format(
          "rendering {}%   {}x{}", status.fPercent, status.fWidth,
          status.fHeight));
      return;
    }
    if (status.fOk) {
      fApp.fExportDialog.setStatus(std::format(
          "saved {}",
          std::filesystem::path(status.fMessage).filename().string()));
      std::println(std::cerr, "[export] saved {}", status.fMessage);
    } else {
      this->exportFailed(status.fMessage);
    }
    fApp.fVideoExporter.clearFinished();
  }

private:
  [[nodiscard]] static std::filesystem::path
  runExportPicker(const std::string &suggested) {
    const auto portal =
        platform::dialogs::saveVideo("Export replay video", suggested);
    if (portal.fPortalAvailable) {
      return portal.fPath.value_or(std::filesystem::path{});
    }
    return {};
  }

  Host &fApp;
  bool fExportPickerOpen = false;
  static constexpr std::uint64_t kExportPickerKey = 10ull << 32;
};

} // namespace client
