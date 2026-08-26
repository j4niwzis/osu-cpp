module;
export module client.applibrary;

import std;
import osu;
import skia;
import platform.audio;
import platform.system;
import platform.web_runtime;
import client.audio;
import client.filter;
import client.library;
import client.loader;
import client.listing;
import client.mirrors;
import client.portal;
import client.replaybrowser;
import client.settings;
import client.util;

export namespace client {

template <class Host> class AppLibrary {
public:
  explicit AppLibrary(Host &app) : fApp(app) {}

  // ---- Library ----------------------------------------------------------

  void initLibrary() {
#ifdef __EMSCRIPTEN__
    fApp.fMapsDir = "/maps";
#else
    fApp.fMapsDir = platform::system::mapsDirectory();
#endif
    std::error_code ec;
    std::filesystem::create_directories(fApp.fMapsDir, ec);
    if (ec) {
      throw std::runtime_error(std::format(
          "cannot create persistent map directory {}: {}",
          fApp.fMapsDir.string(), ec.message()));
    }
    fApp.fThumbDir = fApp.fMapsDir / "thumbnails";
    fApp.fLibrary.configure(fApp.fLoader, fApp.fMapsDir, fApp.fThumbDir,
                       [this] { this->syncMapsDir(); });
    fApp.fReplayDir = fApp.fMapsDir.parent_path() / "replays";
    std::filesystem::create_directories(fApp.fReplayDir, ec);
    if (ec) {
      throw std::runtime_error(std::format(
          "cannot create replay directory {}: {}", fApp.fReplayDir.string(),
          ec.message()));
    }
    std::filesystem::create_directories(fApp.fThumbDir, ec);
    if (ec) {
      throw std::runtime_error(std::format(
          "cannot create thumbnail directory {}: {}", fApp.fThumbDir.string(),
          ec.message()));
    }
    fApp.fLibrary.loadCache();
    fApp.fReplayBrowser.initialize(
        fApp.fMapsDir.parent_path() / "replay-index.json", fApp.fReplayDir);
    fApp.fSettings.load(fApp.fMapsDir.parent_path() / "settings.json");
    fApp.fMirrors.setPreferred(
        static_cast<std::size_t>(std::max(0, fApp.fSettings.choice("mirror"))));
    // What the mirrors cannot do for themselves: the library they import
    // into, the corner they report to, and the music that steps aside for a
    // preview.
    fApp.fMirrors.configure({
                           [this](std::string text, skia::SkColor colour) {
                             fApp.notify(std::move(text), colour);
                           },
                           [this](long id) {
                             return fApp.fLibrary.libraryIndexForSet(
                                        static_cast<int>(id)) >= 0;
                           },
                           [this](const std::filesystem::path &path) {
                             return fApp.fLibrary.addOszToLibrary(
                                 path, true,
                                 client::parseQuery(fApp.fFilter.text()));
                           },
                           [this](int entry) {
                             fApp.fListing.entryChanged(entry);
                           },
                           [this] { fApp.fListing.scrollToStart(); },
                           [this] { return fApp.fOverlays.musicGain(); },
                           [this] { return fApp.fAudio.playing(); },
                           [this] { fApp.fAudio.pause(); },
                           [this] { fApp.fAudio.resume(); },
                           [this] { this->syncMapsDir(); },
                       },
                       fApp.fMapsDir);
    fApp.fSwapIntervalRequest.store(fApp.fSettings.flag("vsync") ? 1 : 0,
                               std::memory_order_release);
    fApp.fAppliedDim = fApp.fSettings.value("dim");

    if (fApp.fHasInitialSet) {
      client::library::Entry entry;
      entry.fInfos = fApp.fSet.fBeatmaps;
      entry.fLoaded = std::make_shared<osu::BeatmapSet>(fApp.fSet);
      fApp.fLibrary.sets().push_back(std::move(entry));
      fApp.fLibrary.loadedOrder().push_back(0);
    }

    fApp.fLibrary.scanArchives();
    this->syncMapsDir();

    fApp.fScreens.resortLibrary();
    fApp.fLibrary.markDirty();
    fApp.fLibrary.rebuildVisible(client::parseQuery(fApp.fFilter.text()));
    // Sets come out of the cache and off the disk in whatever order each was
    // written in; the difficulties within them are put in rating order here,
    // once, before anything selects one by position.
    fApp.fAppliedStarChoice = fApp.fSettings.choice("stars");
    fApp.fLibrary.setRanked(fApp.fAppliedStarChoice == 1);
    fApp.fLibrary.sortLibraryByStars();

    // Start on a random set so the menu isn't always greeted by the same
    // track (unless a specific beatmap was passed on the command line).
    if (!fApp.fHasInitialSet && !fApp.fLibrary.sets().empty()) {
      std::uniform_int_distribution<std::size_t> pick(
          0, fApp.fLibrary.sets().size() - 1);
      fApp.fLibrary.selSet() = static_cast<int>(pick(fApp.fUiRng));
      fApp.fLibrary.selDiff() = 0;
    }
    fApp.fLibraryLoaded = true;
    std::println(std::cerr, "[library] {} sets", fApp.fLibrary.sets().size());
  }

  // Loop the selected set's audio quietly under the menus, lazer-style. Only
  // reloads when the selection changes; stops when gameplay takes over.
  void updateMenuMusic() {
    if (fApp.fLibrary.sets().empty()) {
      return;
    }
    if (fApp.fMenuMusicForSet == fApp.fLibrary.selSet()) {
      // The track is given a moment to start before its silence counts as
      // having ended; OpenAL reports a source as stopped until it does.
      // Paused for a preview is not the same as finished.
      if (!fApp.fAudio.playing() && !fApp.fMirrors.ducked() &&
          fApp.wallMs() - fApp.fMenuTrackWall > 1000.0 &&
          fApp.fState != State::kResults) {
        // The results screen belongs to the map that was just played, and
        // moving on from it would move the selection out from under the
        // player, who is going back to that map.
        this->nextMenuTrack();
      }
      return;
    }
    auto set = fApp.fLibrary.setFor(fApp.fLibrary.selSet());
    if (!set) {
      return; // still loading; try again next frame
    }
    fApp.fMenuMusicForSet = fApp.fLibrary.selSet();
    // The grace period starts when the track is asked for, not when it starts
    // playing: decoding takes a few hundred milliseconds, and silence while
    // that happens is not a track that ended. This is what made the client
    // open on one beatmap and jump to another a second later.
    fApp.fMenuTrackWall = fApp.wallMs();
    if (set->fBeatmaps.empty()) {
      fApp.fAudio.stop();
      return;
    }
    const auto &audioName = set->fBeatmaps.front().fMeta.fAudioFilename;
    if (audioName.empty()) {
      fApp.fAudio.stop();
      return;
    }
    const auto bytes = set->findFile(audioName);
    if (bytes.empty()) {
      fApp.fAudio.stop();
      return;
    }
    // Decoding an MP3 takes hundreds of milliseconds -- the remaining stall
    // when changing selection. Decode on the worker, upload to OpenAL here.
    const std::string ext = detail::fileExtension(audioName);
    std::vector<std::uint8_t> copy(bytes.begin(), bytes.end());
    auto pcm = std::make_shared<audio_client::DecodedAudio>();
    const int forSet = fApp.fLibrary.selSet();
    // The index alone is not identity: deleting a beatmap shifts everything
    // after it, so the path is checked too before this track is adopted.
    const auto forPath = fApp.fLibrary
                             .sets()[static_cast<std::size_t>(
                                 fApp.fLibrary.selSet())]
                             .fPath;
    fApp.fLoader.submit(
        static_cast<std::uint64_t>(fApp.fLibrary.selSet()) | (3ull << 32),
        [copy = std::move(copy), ext, pcm] {
          *pcm = audio_client::decodeAudio(copy, ext);
        },
        [this, forSet, forPath, pcm] {
          if (forSet != fApp.fLibrary.selSet() || pcm->fSamples.empty()) {
            return; // selection moved on while decoding
          }
          if (forSet >= static_cast<int>(fApp.fLibrary.sets().size()) ||
              fApp.fLibrary.sets()[static_cast<std::size_t>(forSet)].fPath !=
                  forPath) {
            return; // that entry is not the one this was decoded for
          }
          fApp.fAudio.adopt(std::move(*pcm));
          fApp.fAudio.setLooping(false); // the next track is chosen when it ends
          fApp.fMenuTrackWall = fApp.wallMs();
          fApp.fAudio.setVolume(fApp.fOverlays.musicGain());
          fApp.fAudio.play();
          fApp.fMainMenu.trackChanged(fApp.wallMs());
        });
  }

  // Picks another map to listen to. Random, and never the one just heard as
  // long as there is anything else in the library.
  void nextMenuTrack() {
    fApp.fFrame.requestRedraw(fApp.wallMs(), 1500.0);
    if (fApp.fLibrary.visible().size() > 1) {
      // One draw, uniform over everything except the one just heard. Drawing
      // again until the draw differs is unbiased but can fail, and failing
      // eight times in a row meant playing the same track over again -- which
      // is the opposite of what this is for.
      std::size_t current = fApp.fLibrary.visible().size();
      for (std::size_t i = 0; i < fApp.fLibrary.visible().size(); ++i) {
        if (fApp.fLibrary.visible()[i] == fApp.fLibrary.selSet()) {
          current = i;
          break;
        }
      }
      const bool skipping = current < fApp.fLibrary.visible().size();
      std::uniform_int_distribution<std::size_t> pick(
          0, fApp.fLibrary.visible().size() - (skipping ? 2 : 1));
      std::size_t idx = pick(fApp.fUiRng);
      if (skipping && idx >= current) {
        ++idx; // the gap left by the one being skipped closes over it
      }
      fApp.fLibrary.selSet() = fApp.fLibrary.visible()[idx];
      fApp.fLibrary.selDiff() = 0; // the carousel follows the selection on its own
      fApp.fMenuTrackWall = fApp.wallMs();
      return;
    }
    // Nothing else to play: start this one again.
    fApp.fMenuTrackWall = fApp.wallMs();
    fApp.fAudio.play();
  }

  void stopMenuMusic() {
    fApp.fMenuMusicForSet = -1;
    fApp.fAudio.setLooping(false);
    fApp.fAudio.stop();
    fApp.fMainMenu.stopped();
  }

  void syncMapsDir() {
#ifdef __EMSCRIPTEN__
    platform::web::syncMapStorage();
#endif
  }

  // ---- Import an external .osz into the library -------------------------
  //
  // The platform layer owns the native picker; the browser drops the chosen
  // bytes at /import.osz and calls back. Either way the archive is copied
  // into the maps directory and added to the library. Window drops feed the
  // same path on desktops.
  void drainDroppedFiles() {
    const auto paths = fApp.fWindowRuntime.takeDroppedFiles();
    for (const auto &p : paths) {
      this->importFrom(std::filesystem::path(p));
    }
  }

  bool importFrom(const std::filesystem::path &src) {
    if (!fApp.fLibrary.importArchive(
            src, client::parseQuery(fApp.fFilter.text()))) {
      return false;
    }
    if (fApp.fState == State::kMainMenu) {
      fApp.switchState(State::kSongSelect);
    }
    return true;
  }

  void importOsz() {
#ifdef __EMSCRIPTEN__
    platform::web::requestBeatmapArchive();
#else
    // The picker is another process and the user takes as long as they take.
    // Waited for here, on the thread that draws, it stops the frame loop for
    // the whole of that: the window goes unresponsive and stays on whatever
    // was last in the buffer. It runs on the loader instead, and the archive
    // is adopted in the completion, which is back on the drawing thread.
    if (fPickerOpen) {
      return; // one dialog at a time; they share a temporary file
    }
    fPickerOpen = true;
    auto chosen = std::make_shared<std::filesystem::path>();
    fApp.fLoader.submit(
        kPickerKey, [this, chosen] { *chosen = this->runFilePicker(); },
        [this, chosen] {
          fPickerOpen = false;
          if (!chosen->empty()) {
            this->importFrom(*chosen);
          }
        });
#endif
  }

#ifndef __EMSCRIPTEN__
  [[nodiscard]] std::filesystem::path runFilePicker() {
    if (auto chosen = client::portal::openArchive("Import beatmap")) {
      return *chosen;
    }
    return {};
  }
#endif

  // ---- Download screen logic -------------------------------------------

private:
  using State = typename Host::State;
  Host &fApp;
  // A file dialog is running on the loader. Nothing else may start one: they
  // all write the chosen path to the same temporary file.
  bool fPickerOpen = false;
  // Distinct from the loader keys the library uses for sets, art and audio.
  static constexpr std::uint64_t kPickerKey = 9ull << 32;
};

} // namespace client
