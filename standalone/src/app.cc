module;

#ifdef __EMSCRIPTEN__
#include "emscripten_macro.h"
#endif

export module app;

import std;
import osu;
import glfw;
import skia;
import audio;
import skin;
import archive;
import client.util;
import client.audio;
import client.input;
import client.timing;
import client.http;
import client.replaybrowser;
import client.filter;
import client.loader;
import skiff.paint;
import client.palette;
import client.settings;
import client.settingspanel;
import client.overlays;
import client.deletedialog;
import client.filtercontrol;
import client.library;
import client.listing;
import client.mirrors;
import client.carousel;
import client.songselect;
import client.pause;
import client.results;
import client.mainmenu;
import present;
import client.setpage;
import skiff.scene;
import skiff.nodes;
import client.gameplayview;
import client.mods;
import client.video;
import client.videoexport;
import client.framestate;
import client.fonts;
import client.judgements;
import client.windowruntime;
import client.playresult;
import client.appinput;
import client.applibrary;
import bjson;
#ifdef __EMSCRIPTEN__
import emscripten;
#endif

#ifdef __EMSCRIPTEN__
namespace client::detail {
inline std::atomic<bool> gMapsSynced{false};
} // namespace client::detail

extern "C" EMSCRIPTEN_KEEPALIVE void osu_maps_synced() {
  client::detail::gMapsSynced.store(true, std::memory_order_release);
}
#endif

namespace client {

using audio_client::alFormat;
using audio_client::AudioContext;
using audio_client::audioContext;
using audio_client::AudioPlayer;
using audio_client::SamplePlayer;

export extern "C++" class App {
public:
  App(std::optional<osu::BeatmapSet> set, osu::ModSet mods, bool headless,
      bool autoplay, std::filesystem::path replayPath = {}, bool record = false,
      std::filesystem::path skinPath = {}, bool profile = false)
      : fMods(mods), fHeadless(headless), fAutoplay(autoplay),
        fReplayPath(std::move(replayPath)), fRecord(record),
        fSkin(std::move(skinPath)), fShowProfile(profile) {
    fCliAutoplay = autoplay;
    if (set) {
      fSet = std::move(*set);
      fHasInitialSet = true;
    }
  }

  ~App() { this->shutdown(); }

  [[nodiscard]] int run() {
    if (fHeadless) {
      return this->runHeadless();
    }
    return this->runWindowed();
  }

private:
  friend class client::AppInput<App>;
  friend class client::AppLibrary<App>;

  osu::BeatmapSet fSet;
  osu::ModSet fMods = osu::mod::kNone;
  // The map being played and everything that only exists while it is.
  struct PlayState {
    std::optional<osu::Beatmap> fMap;
    std::optional<osu::Engine> fEngine;
    AnchoredClock fClock;
    double fStartMs = 0.0;
    double fPausedNow = 0.0; // frozen game time while paused
    int fRetryCount = 0;     // plays of this map since it was chosen
    bool fAwaitingFirstFrame = false;
    osu::StarRating fPlayAttributes;
    std::vector<osu::InputEvent> fAutoplayEvents;
    std::vector<osu::InputEvent> fRecordedEvents;
    std::filesystem::path fLastSavedReplay; // this run's own file
  };
  PlayState fPlay;
  bool fHeadless = false;
  bool fAutoplay = false;
  bool fCliAutoplay = false;         // --autoplay, which outlives a single play
  std::filesystem::path fReplayPath; // the file driving the current play
  std::filesystem::path fPendingReplay; // requested for the play about to start
  bool fRecord = false;
  std::string fBeatmapFilename;
  std::size_t fAutoplayIndex = 0;
  Skin fSkin;
  bool fShowProfile = false;
  bool fNoGlow = false;

  // Window / GL / Skia
  client::WindowRuntime fWindowRuntime;
  // Render APIs still take the native handle; ownership lives in the runtime.
  glfw::GLFWwindow *fWindow = nullptr;
  skia::Sp<skia::GrDirectContext> fContext;
  client::FrameState fFrame;
  skiff::scene::InputRouter fInputRouter;
  // The window and where the pointer is in it. What a resize changes and
  // what every screen measures itself against.
  struct WindowState {
    int fScreenW = 1280;
    int fScreenH = 960;
    float fMouseX = 0.0f;
    float fMouseY = 0.0f;
    bool fFullscreen = true;
    int fWindowedW = 1280;
    int fWindowedH = 960;
    int fWindowedX = 100;
    int fWindowedY = 100;
  };
  WindowState fWin;
  float fScale = 1.0f;
  float fOffsetX = 0.0f;
  float fOffsetY = 0.0f;

  // Input
  osu::Vec2 fCursor = osu::kPlayfieldCenter;
  osu::Vec2 fRawPrev{};                             // last pointer position
  osu::Vec2 fVirtualCursor = osu::kPlayfieldCenter; // integrated position
  bool fHasRawPrev = false;
  std::uint32_t fHeldMask = 0; // bit per held key/button (Z/X/Space/M1/M2)

  // Swap interval is a property of the context, so only the thread holding it
  // may set it; a toggle in the settings parks the value here.
  std::atomic<int> fSwapIntervalRequest{-1};
  int fSwapInterval = -1;
  int fRefreshHz = 60; // the monitor's, sampled where GLFW allows the query
  // Whether the window system answered at all, which is a different question
  // from what the answer was. Carrying a region into the window is sound only
  // when it did: with no answer, "how old is this buffer" has no value that
  // makes it safe, including the one the settings assert.
  // Two distinct things, which sharing one variable confused: what the next
  // frame has to repaint (filled in while this one draws) and what this frame
  // is clipped to (taken from that accumulator when the frame starts).
  //
  // A list rather than one rectangle: the logo and the FPS counter sit in
  // opposite corners, and their union is half the screen.
  bool fOverlayShown = false; // an overlay covered the screen last frame
  // What each of the last few frames repainted, since the buffer being drawn
  // into is missing exactly that.
  // The setting decides; the variable is there for a run before settings
  // exist, and to force it on while measuring.
  const bool fForcePartialRedraw = std::getenv("OSU_PARTIAL_REDRAW") != nullptr;
  // OSU_TRACE_REPAINT=1 prints the numbers a frame's clip decision is made
  // from -- but only when one of them changes, so a session is a handful of
  // lines rather than one per frame. A screen going black but for the live
  // region is a frame that clipped into a buffer it had never drawn into, and
  // the line where clipping resumes is the one that says why it thought it
  // could. OSU_TRACE_RESIZE is the old name and still works.
  // OSU_BUFFER_AGE=N overrides the setting of the same meaning, for measuring.
  const int fForcedBufferAge = [] {
    const char *value = std::getenv("OSU_BUFFER_AGE");
    return value != nullptr ? std::atoi(value) : 0;
  }();

  [[nodiscard]] bool partialRedraw() const {
#ifdef __EMSCRIPTEN__
    // WebGL throws the drawing buffer away after compositing unless the
    // context was made with preserveDrawingBuffer, so there is no older frame
    // to repaint a piece of: in a browser every frame is a whole frame.
    return false;
#else
    return fForcePartialRedraw || fSettings.flag("partial");
#endif
  }
  std::chrono::steady_clock::time_point fNextFrame{};
  std::int64_t fLastSwapUs = 0; // reported by the frame breakdown
  double fFpsPrevWall = 0.0;
  double fFpsFrameMs = 0.0;
  double fFpsDamageWall = 0.0;

  // ---- Screens ---------------------------------------------------------
  enum class State {
    kMainMenu,
    kSongSelect,
    kDownload,
    kPlaying,
    kPaused,
    kResults,
  };
  client::AppInput<App> fInput{*this};
  client::AppLibrary<App> fLibraryRuntime{*this};
  State fState = State::kMainMenu;
  bool fHasInitialSet = false;

  client::library::Library fLibrary;
  // What each card in the download results last drew as its own state, and
  // which set the artwork on screen belongs to. Both are about what is being
  // shown, not about what is on disk.
  std::vector<std::uint8_t> fEntryStates;
  int fBackgroundForSet = -1;
  client::Loader fLoader;

  // Filtering / sorting: the control itself lives in client.filtercontrol.
  client::FilterControl fFilter;
  bool fLibraryLoaded = false;
  int fAppliedStarChoice = -1; // forces the first ordering pass
  client::carousel::Carousel fCarousel;
  client::songselect::InfoWedge fInfoWedge;
  int fMenuMusicForSet = -1;   // set whose audio is playing under the menus
  double fMenuTrackWall = 0.0; // when it started, so its end can be told
  double fMusicPollWall = 0.0; // last time the track was asked if it ended
  std::filesystem::path fMapsDir;
  std::filesystem::path fThumbDir;
  std::filesystem::path fReplayDir;
  client::songselect::Footer fSelectFooter;
  client::DeleteDialog fDeleteDialog;

  // Download screen (mirror search + .osz fetch).
  client::listing::Listing fListing;
  client::setpage::SetPage fSetPage;
  // Searching the mirrors, fetching covers and previews, downloading a set.
  client::mirrors::Mirrors fMirrors;
  // Transfers in flight, so progress can be polled without the view knowing
  // anything about HTTP.
  bool fSwallowChar = false;  // the 'D' that opened the screen also arrives
                              // as a char event; it must not enter the query
  // A short-lived message in the corner, as lazer's notification overlay
  // shows when an import finishes.
  std::string fToast;
  skia::SkColor fToastColor = skia::kWhite;
  double fToastWall = 0.0;
  client::ReplayBrowser fReplayBrowser;

  // Pause / results overlays.
  double fPolledCursorX = -1.0, fPolledCursorY = -1.0; // wasm cursor polling
  client::pause::PauseMenu fPauseMenu;
  bool fRetryPending = false;
  int fPlayingSet = -1;
  int fPlayingDiff = -1;

  client::PlayResult fResult;

  // ---- UI animation state ----
  double fStateEnterWall = 0.0;
  double fUiPrevWall = 0.0;
  double fUiDt = 16.0; // ms since the last frame, as measured
  bool fDrawnEmpty = false;

  // ---- Main menu --------------------------------------------------------
  //
  // The screen lives in client.mainmenu: the state machine, the row of
  // buttons, the logo and everything that decides what any of it looks like.
  // What is left here is the artwork behind it, the actions it asks for, and
  // the frame it is drawn into.
  using MenuAction = client::mainmenu::Action;
  client::mainmenu::Screen fMainMenu;
  // Slider bodies are rasterised once, at the scale the playfield had when
  // the map was loaded. A resize changes that scale and leaves them behind,
  // to be stretched by the blit -- so they are rebuilt, but not while the
  // window is still being dragged.
  float fSliderBodyScale = 0.0f;
  bool fSliderBodiesStale = false;
  double fLastResizeWall = 0.0;
  // Set when a map is loaded and cleared by the frame that first shows it.
  // The difficulty of the map being played, under the ranked calculator and
  // with the mods applied, kept so the play can be priced when it ends.
  float fDrawnMouseX = -1.0f, fDrawnMouseY = -1.0f;
  client::ReplayVideoExporter fVideoExporter;

  // ---- Settings overlay -------------------------------------------------
  // SettingsPanel: a 170px sidebar plus a 400px content column sliding in
  // from the left over 600 ms. The sidebar does not swap pages -- it scrolls
  // the single continuous column of sections, and highlights whichever
  // section the scroll is currently in.
  client::GameplayView fView;
  client::Settings fSettings;
  client::SettingsPanel fSettingsPanel;
  float fAppliedDim = 0.7f;

  // ---- Overlays (views live in client.overlays) -------------------------
  client::ModSelect fModSelect;
  client::ExportDialog fExportDialog;

  // MainMenu.cs: Gray(1) idle, Gray(0.8) with buttons.
  // Seeded per run: a fixed seed meant the same "random" map on launch, the
  // same order of tracks after it, and the same triangles behind the logo,
  // every single time.
  std::mt19937 fUiRng{std::random_device{}()};

  [[nodiscard]] static float easeOutQuint(float t) {
    return skiff::paint::outQuint(t);
  }

  [[nodiscard]] float screenFade() const {
    return easeOutQuint(
        static_cast<float>((wallMs() - fStateEnterWall) / 240.0));
  }

  void drawScreenFadeIn(skia::SkCanvas *canvas) {
    const float fade = this->screenFade();
    if (fade >= 1.0f) {
      return;
    }
    // Runs on the wall clock rather than on easing, so it asks for the frames
    // it needs itself, and stops asking when it is over.
    fFrame.oweFrames(2);
    skia::SkPaint p;
    p.setColor(skia::colorSetARGB(
        static_cast<std::uint8_t>((1.0f - fade) * 160.0f), 10, 8, 14));
    canvas->drawRect(skia::SkRect::MakeXYWH(0, 0,
                                            static_cast<float>(fWin.fScreenW),
                                            static_cast<float>(fWin.fScreenH)),
                     p);
  }

  // Palette lives in client.palette; these are aliases so call sites
  // stay short.
  static constexpr auto kAccent = client::palette::kAccent;
  static constexpr auto kAccent2 = client::palette::kAccent2;
  static constexpr auto kCardBg = client::palette::kCardBg;
  static constexpr auto kCardSel = client::palette::kCardSel;
  static constexpr auto kPanelBg = client::palette::kPanelBg;

  // Timing
  double fLastClockSyncWall = std::numeric_limits<double>::lowest();
  static constexpr double kClockSyncIntervalMs = 250.0;
  AudioPlayer fAudio;
  client::JudgementPresenter fJudgements;

  // Font
  skia::SkFont fFont;
  skia::SkFont fDisplayFont;

  // Combo color group for each object.
  osu::ComboInfo fComboInfo;

  void loadComboInfo() { fComboInfo = osu::buildComboInfo(*fPlay.fMap); }

  void startGameplay(const osu::BeatmapInfo &info) {
    fPlay.fMap.emplace(client::loadBeatmap(fSet, info));
    fBeatmapFilename = info.fFilename;

    // Which rules to play by. A fresh play takes the setting. A replay takes
    // the toggle beside its panel, which starts on whatever the index says it
    // was recorded under -- except for one this client wrote back when it
    // produced .xz, which osu! cannot read and which predates the rules being
    // osu!'s, so it only ever plays back under the old model.
    osu::RuleSet rules = fSettings.choice("rules") == 1
                             ? osu::RuleSet::kLegacyClient
                             : osu::RuleSet::kLazer;
    std::optional<osu::ReplayData> replay;
    if (fAutoplay && !fReplayPath.empty()) {
      rules = fReplayBrowser.legacyRules() ? osu::RuleSet::kLegacyClient
                                           : osu::RuleSet::kLazer;
      std::ifstream file(fReplayPath, std::ios::binary);
      if (file) {
        std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>(file),
                                        std::istreambuf_iterator<char>()};
        try {
          replay = osu::decodeReplay(bytes);
          if (replay->fLegacyFormat) {
            rules = osu::RuleSet::kLegacyClient;
          }
        } catch (const std::exception &) {
          replay.reset();
        }
      }
    }
    fPlay.fEngine.emplace(*fPlay.fMap, fMods, rules);
    // Worked out once, here, rather than when the results appear: it is the
    // difficulty calculation, and it belongs with the load rather than in the
    // moment between the last note and the screen that follows it.
    fPlay.fPlayAttributes = osu::calculateStars(
        *fPlay.fMap, fMods, nullptr, std::numeric_limits<double>::infinity(),
        osu::StarAlgorithm::kRanked);
    fPlay.fPlayAttributes.fMaxCombo = fPlay.fEngine->maxAchievableCombo();
    fPlay.fPlayAttributes.fLargeTicks =
        fPlay.fEngine->maximumStatistics().fLargeTick;
    this->loadComboInfo();
    fSkin.setComboColors(fPlay.fMap->fComboColors);
    fSkin.precomputeSliderBodies(*fPlay.fMap, fComboInfo, fScale,
                                 fContext.get(), this->sliderBodyKey());
    fSliderBodyScale = fScale;
    fSliderBodiesStale = false;
    if (fSettings.choice("renderer") == 1) {
      // Built on the GPU either way, since that is what SkSL is for; moved
      // into memory now so the CPU rasteriser does not read them back on
      // every frame that draws a slider.
      fSkin.flattenBodiesToRaster(fContext.get());
    }
    if (fAutoplay) {
      if (replay) {
        fPlay.fAutoplayEvents = std::move(replay->fEvents);
        fMods = replay->fMods;
        // The video exporter renders the recorded events; a watched replay
        // is its own recording. saveReplay refuses to write it back out.
        fPlay.fRecordedEvents = fPlay.fAutoplayEvents;
      } else if (!fReplayPath.empty()) {
        // Unreadable: nothing to play back.
      } else {
        fPlay.fAutoplayEvents = osu::buildAutoplay(*fPlay.fMap, fMods);
      }
      fAutoplayIndex = 0;
    }

    if (!fPlay.fMap->fMeta.fAudioFilename.empty()) {
      const auto bytes = fSet.findFile(fPlay.fMap->fMeta.fAudioFilename);
      if (!bytes.empty()) {
        fAudio.load(bytes,
                    detail::fileExtension(fPlay.fMap->fMeta.fAudioFilename));
      }
    }

    if (!fPlay.fMap->fMeta.fBackground.empty()) {
      const auto bytes = fSet.findFile(fPlay.fMap->fMeta.fBackground);
      if (!bytes.empty()) {
        fView.setBackground(loadImage(bytes));
        fView.preScaleBackground(this->gameplayCtx(nullptr));
      }
    }

    // Said out loud once a map is loaded, because the size looking applied on
    // some runs and not others cannot happen from this code alone: the drawn
    // size is a pure function of the setting, read fresh every frame. Either
    // the number below is not what the settings panel shows, in which case it
    // is the plumbing, or it is, in which case it is the drawing.
    {
      const float size = fSettings.value("cursorsize");
      const float drawn = size * client::GameplayView::kCursorUnitScale;
      const auto [hasSprite, spriteW] = fSkin.cursorSprite();
      // On screen the cursor is all four of these multiplied together, so all
      // four are worth seeing: which one differs between runs is which one is
      // at fault.
      std::println(std::cerr,
                   "[cursor] setting {:.3f} scale {:.5f} sprite {} {}px "
                   "playfield {:.4f} -> {:.1f}px wide",
                   size, drawn, hasSprite ? "yes" : "NO(fallback circle)",
                   spriteW, fScale,
                   hasSprite
                       ? static_cast<float>(spriteW) * 0.35f * drawn * fScale
                       : 8.0f * drawn * fScale);
    }

    // The clock is not started here. Everything between this point and the
    // first frame actually being on screen would be charged to the map: the
    // rest of the load, the pacing sleep, and the first frame itself, which
    // repaints the whole window and warms every cache it touches. Audio waits
    // with it, since it does not begin the instant it is asked to either.
    fAudio.setVolume(this->musicGain());
    fPlay.fAwaitingFirstFrame = true;
  }

  // Called at the end of the first gameplay frame, once it has been handed to
  // the window. From here the map and the music start together.
  void startGameplayClock() {
    fPlay.fAwaitingFirstFrame = false;
    fPlay.fStartMs = wallMs();
    fPlay.fClock.reset(fPlay.fStartMs, 0.0);
    fLastClockSyncWall = std::numeric_limits<double>::lowest();
    fAudio.play();
  }

  [[nodiscard]] int runHeadless() {
    if (!fHasInitialSet || fSet.fBeatmaps.empty()) {
      return 1;
    }
    this->startGameplay(fSet.fBeatmaps.front());
    const auto result = osu::runAutoplay(*fPlay.fMap, fPlay.fEngine->mods());
    std::println("{}", result.fScore);
    return 0;
  }

  [[nodiscard]] int runWindowed() {
    if (!fWindowRuntime.open([this] { this->toggleFullscreen(); })) {
      return 1;
    }
    fWindow = fWindowRuntime.window();
    const auto initial = fWindowRuntime.initialExtent();
    fWin.fScreenW = initial.fWidth;
    fWin.fScreenH = initial.fHeight;
    fRefreshHz = fWindowRuntime.refreshHz();
#ifdef __EMSCRIPTEN__
    EM_ASM(Module.setCursorVisible(true));
#endif

#ifdef __EMSCRIPTEN__
    glfw::glfwMakeContextCurrent(fWindow);

    if (!this->initSkia()) {
      fWindowRuntime.close();
      fWindow = nullptr;
      return 1;
    }

    auto fonts = client::loadClientFonts(20.0f);
    fFont = std::move(fonts.fUi);
    skiff::nodes::Text::setFont(&fFont);
    fDisplayFont = std::move(fonts.fDisplay);
    this->resize(fWin.fScreenW, fWin.fScreenH);

    // Persistent library at /maps (IDBFS). The initial syncfs is async; the
    // library is scanned once the flag flips (see frameSongSelect).
    EM_ASM({
      try {
        FS.mkdir('/maps');
      } catch (e) {
      }
      FS.mount(IDBFS, {}, '/maps');
      FS.syncfs(true, function(err) { Module._osu_maps_synced(); });
    });

    fState = State::kMainMenu;
    fStateEnterWall = wallMs();
    // A CLI-provided .osz reaches the wasm build too (preloaded); jump to it.
    if (fHasInitialSet) {
      fLibrary.selectInitialSet();
      fState = State::kSongSelect;
    }
    EM_ASM(Module.setCursorVisible(true));
    emscripten::emscripten_set_main_loop_arg(emscriptenFrameProc, this, 0, 1);
    return 0;
#else
    // Snapshot the real framebuffer size on the main thread (that query is
    // main-thread-only in GLFW); the render thread must not call it.
    glfw::glfwGetFramebufferSize(fWindow, &fWin.fScreenW, &fWin.fScreenH);

    // The GL context is owned by the render thread from here on. The main
    // thread degrades into a pure event pump: it blocks in glfwWaitEvents,
    // stamps and enqueues input, and services the few window operations that
    // GLFW pins to this thread.
    std::thread renderThread([this] { this->renderThreadMain(); });
    fWindowRuntime.pumpEvents();
    renderThread.join();
    return fWindowRuntime.exitCode();
#endif
  }

#ifndef __EMSCRIPTEN__
  void renderThreadMain() {
    glfw::glfwMakeContextCurrent(fWindow);

    if (!this->initSkia()) {
      fWindowRuntime.setExitCode(1);
      this->requestQuit();
      return;
    }

    auto fonts = client::loadClientFonts(20.0f);
    fFont = std::move(fonts.fUi);
    skiff::nodes::Text::setFont(&fFont);
    fDisplayFont = std::move(fonts.fDisplay);
    this->resize(fWin.fScreenW, fWin.fScreenH);

    fLibraryRuntime.initLibrary();
    // Launched with a beatmap on the command line => skip the main menu and
    // drop straight into song select on the imported set (it sorts to a known
    // index, so just point the selection at it).
    if (fHasInitialSet) {
      fLibrary.selectInitialSet();
      this->switchState(State::kSongSelect);
    } else {
      fState = State::kMainMenu;
      fStateEnterWall = wallMs();
    }

    while (!fWindowRuntime.quitting()) {
      this->frame();
    }

    // A play interrupted by closing the window still reports to the console.
    if (fState == State::kPlaying || fState == State::kPaused) {
      this->printResult();
      if (fRecord)
        this->saveReplay();
    }
    fWindowRuntime.setExitCode(0);
    this->requestQuit();
    glfw::glfwMakeContextCurrent(nullptr);
  }

#endif

  void requestQuit() {
    fWindowRuntime.requestQuit();
  }

  [[nodiscard]] static double wallMs() { return glfw::glfwGetTime() * 1000.0; }

  void enqueue(const Event &ev) { fWindowRuntime.push(ev); }

  [[nodiscard]] static const char *stateName(State st) {
    switch (st) {
    case State::kMainMenu:
      return "main-menu";
    case State::kSongSelect:
      return "song-select";
    case State::kDownload:
      return "download";
    case State::kPlaying:
      return "playing";
    case State::kPaused:
      return "paused";
    case State::kResults:
      return "results";
    }
    return "?";
  }

  void switchState(State st) {
    // The transition fades over 240 ms and asks for its own frames after
    // that; what a new screen loads arrives as damage from a callback.
    fFrame.oweFrames(4);
    // A dialog belongs to the screen it was opened on; carried to another one
    // it would go on taking the keys typed at that screen's search box.
    fExportDialog.close();
    // Coming out of a play: the map's track ran to its end while it was being
    // played, and silence after it is not a track that finished on its own.
    // Loading it again for the same selection is what makes finishing a play
    // land back where it started, rather than on whatever came up next.
    if ((fState == State::kPlaying || fState == State::kPaused) &&
        st != State::kPlaying && st != State::kPaused) {
      fMenuMusicForSet = -1;
      fMenuTrackWall = wallMs();
    }
    fFrame.damageAll("screen change"); // the new screen owns every pixel
    std::println(std::cerr, "[ui] {} -> {}", stateName(fState), stateName(st));
    fState = st;
    fStateEnterWall = wallMs();
    if (st == State::kResults) {
      // The side panels are the other replays for this beatmap.
      fReplayBrowser.selectBeatmap(this->beatmapMd5(), fReplayPath,
                                   fReplayPath.empty());
    }
    if (st == State::kMainMenu) {
      // Returning to the menu always lands on the top level, never on a
      // stale submenu, and the logo re-eases into place from where it was.
      fMainMenu.enterTopLevel();
    }
  }

  // ---- Frame dispatch ---------------------------------------------------

  // Applied here because glfwSwapInterval only affects the calling thread's
  // context, and this is the thread that owns it.
  void applySwapInterval() {
#ifndef __EMSCRIPTEN__
    const int wanted = fSwapIntervalRequest.load(std::memory_order_acquire);
    if (wanted < 0 || wanted == fSwapInterval) {
      return;
    }
    fSwapInterval = wanted;
    glfw::glfwSwapInterval(wanted);
    fNextFrame = std::chrono::steady_clock::now();
    std::println(std::cerr, "[gfx] swap interval {} (monitor {} Hz)", wanted,
                 fRefreshHz);
#endif
  }

  // Plenty of drivers and compositors ignore the swap interval outright, so
  // asking for it is not enough: with vsync on, the loop is also paced to the
  // monitor's refresh here. When the driver does honour the interval the swap
  // has already blocked and this sleeps for nothing.
  void limitFrameRate() {
#ifndef __EMSCRIPTEN__
    if (fSwapInterval <= 0 || fRefreshHz <= 0) {
      return;
    }
    // Paced unconditionally. Asking first whether the swap had blocked meant
    // the answer changed from frame to frame -- one slow frame blocked, so
    // the fast one after it was left unpaced, and the rate ran away in
    // exactly the screens where frames vary in cost. When the driver does
    // honour the interval the deadline has already passed and this returns
    // without sleeping, which costs nothing.
    using clock = std::chrono::steady_clock;
    const auto period = std::chrono::nanoseconds(
        static_cast<std::int64_t>(1'000'000'000.0 / fRefreshHz));
    const auto now = clock::now();
    if (fNextFrame.time_since_epoch().count() == 0 ||
        now > fNextFrame + period * 4) {
      fNextFrame = now + period; // first frame, or too far behind to catch up
      return;
    }
    if (now < fNextFrame) {
      // Sleeping is imprecise, but a spin costs a core, and on this renderer
      // that core is doing the drawing.
      std::this_thread::sleep_until(fNextFrame);
    }
    fNextFrame += period;
#endif
  }

  // A text caret is shown for 600 ms of every 1000; this is when it next
  // changes, which is when a frame is next worth drawing for it.
  [[nodiscard]] static double nextCaretFlip(double nowMs) {
    const double phase = std::fmod(nowMs, 1000.0);
    return nowMs + (phase < 600.0 ? 600.0 - phase : 1000.0 - phase);
  }

  void beginFrame() {
    const bool partial = this->partialRedraw();
    const int assumedAge =
        fForcedBufferAge > 0 ? fForcedBufferAge
                             : fSettings.choice("bufferage");
    fFrame.begin({.fPartial = partial,
                  .fAssumedBufferAge = assumedAge,
                  .fReportedSize = fWindowRuntime.reportedSize(),
                  .fWidth = fWin.fScreenW,
                  .fHeight = fWin.fScreenH,
                  .fNow = wallMs(),
                  .fLastResize = fLastResizeWall,
                  .fShowDamage =
                      fFrame.showingDamage(
                          fSettings.flag("damageoverlay")),
                  .fPlaying = fState == State::kPlaying});
  }

  // The half of a screen that is not drawing: what is in the tree, where it
  // sits, what the pointer is over. Damage marked here is marked before the
  // frame begins, so it is a reason to draw rather than a note about what to
  // repaint next time.
  void updateScreens() {
    fFrame.fSceneWantsFrame = false;
    // The settings panel floats over whatever screen is up, and the screen
    // underneath has no idea it is there. It says what it repaints -- a
    // sidebar whose indicator is easing, an option list following the
    // pointer, the column while it scrolls -- and says nothing at all while
    // it is open and untouched, which is most of the time.
    if (fSettingsPanel.visible()) {
      fFrame.consume(
          fSettingsPanel.update(fFont, fSettings,
                                {fWin.fScreenW, fWin.fScreenH, fWin.fMouseX,
                                 fWin.fMouseY, wallMs(), fUiDt}));
    }
    if (fState == State::kDownload) {
      this->updateDownload();
    } else if (fState == State::kSongSelect) {
      this->updateSongSelect();
    } else if (fState == State::kPaused) {
      this->updatePause();
    } else if (fState == State::kMainMenu) {
      this->updateMainMenu();
    } else if (fState == State::kResults) {
      this->updateResults();
    }
    // A transition dims the whole screen, so a frame drawn during one has to
    // repaint whole: clipped to a region, everything outside it would stay
    // undimmed.
    if (this->screenFade() < 1.0f) {
      fFrame.damageAll("screen fade");
    }

    // The overlays that cover the screen say what they repaint here, before
    // the frame is committed to. Said while drawing -- which is where this
    // lived -- it describes a frame that has already been declined, and the
    // screen it belongs to freezes: the strip of panels easing to the one
    // that was picked marked itself for a frame that was never drawn, and so
    // never advanced, and so kept marking itself.
    if (fModSelect.visible()) {
      const auto entries = this->modEntries();
      fModSelect.update(
          fFont, entries, fMods,
          {fWin.fScreenW, fWin.fScreenH, fWin.fMouseX, fWin.fMouseY,
           wallMs()});
      const auto modFrame = fModSelect.finishFrame();
      fFrame.fSceneWantsFrame =
          fFrame.fSceneWantsFrame || modFrame.fWantsAnotherFrame;
      const skia::SkRect region = modFrame.fDamage;
      if (region.width() >= static_cast<float>(fWin.fScreenW)) {
        fFrame.damageAll("mod select fading");
      } else {
        fFrame.damage(region);
      }
    }
    if (fReplayBrowser.open()) {
      const auto motion =
          fReplayBrowser.updateMotion(fWin.fMouseX, fWin.fMouseY);
      if (motion.fFullDamage) {
        fFrame.damageAll("replay browser moving");
      } else if (motion.fDamage) {
        fFrame.damage(*motion.fDamage);
      }
    }
    if (fExportDialog.open()) {
      fExportDialog.update(fFont, fWin.fScreenW, fWin.fScreenH, fWin.fMouseX,
                           fWin.fMouseY, wallMs());
      fFrame.consume(fExportDialog.finishFrame());
    }
    if (fDeleteDialog.open()) {
      fDeleteDialog.update(fFont, fWin.fScreenW, fWin.fScreenH, fWin.fMouseX,
                           fWin.fMouseY, wallMs());
      fFrame.consume(fDeleteDialog.finishFrame());
    }

    // Overlays are drawn after the screen, over most of it, so appearance and
    // disappearance repaint the underlying screen once. Retained overlays
    // report their own damage between those two transitions.
    const bool overlay = fSettingsPanel.visible() || fModSelect.visible() ||
                         fExportDialog.open() || fReplayBrowser.open() ||
                         fDeleteDialog.open() || fSetPage.open();
    // Only while one is moving, or on the frame it appears or goes away: a
    // settled overlay is as static as the screen under it, and the screens do
    // mark what they change beneath it.
    if (overlay != fOverlayShown) {
      fFrame.damageAll("overlay appeared or went away");
    }
    fOverlayShown = overlay;
  }

  // Whether the frame that was asked for would put anything new on screen.
  // Only a screen that reports its damage can be asked: for the others,
  // "nothing is marked" means "nobody was asked", not "nothing changed".
  [[nodiscard]] bool nothingToPaint() {
    if (fFrame.fFullDamage || !fFrame.fDamage.empty() ||
        fFrame.fFullRepaintsOwed > 0) {
      return false;
    }
    if (fState != State::kDownload && fState != State::kSongSelect &&
        fState != State::kMainMenu && fState != State::kResults &&
        fState != State::kPaused) {
      // The screens still drawn immediately cannot answer this. Their state
      // advances while they draw -- an eased hover, a logo settling -- so a
      // frame skipped for want of damage is a frame in which nothing moves,
      // which produces no damage, which skips the next one. A screen may only
      // be skipped once it settles in a pass that runs whether or not the
      // frame is drawn, which is what the update half is for.
      return false;
    }
    // Anything drawn over the screen repaints whole and does not report a
    // region, so it cannot be skipped on the strength of the screen's silence.
    // The settings panel reports its own regions, so it does not stop a frame
    // from being skipped. The rest do not, and cannot be skipped over.
    // Every overlay reports its own regions now -- the dialog its box and its
    // status line, the mod select its chips, the replay browser its band, the
    // delete confirmation its tree. A frame drawn for one of them with
    // nothing marked repaints the whole window, which is what a pointer
    // resting inside any of them used to cost.
    if (fFrame.fSceneWantsFrame) {
      return false;
    }
    // A preview still being fetched has nothing on screen to show for it
    // yet; once it plays, the ring marks the card it is on.
    if (fMirrors.previewPending()) {
      return false;
    }
    // Things the screen draws that live on a clock rather than on damage.
    if (this->screenFade() < 1.0f) {
      return false;
    }
    if (!fToast.empty() && wallMs() - fToastWall < 4000.0) {
      return false;
    }
    return true;
  }

  // Whether the pointer has moved since the last frame was drawn. What an
  // overlay that covers the screen and reports no regions has instead of
  // knowing what it changed.
  [[nodiscard]] bool pointerMoved() const {
    return fWin.fMouseX != fDrawnMouseX || fWin.fMouseY != fDrawnMouseY;
  }

  [[nodiscard]] bool needsFrame() {
    // Gameplay is a moving picture by definition, and so is anything with a
    // clock on screen.
    if (fState == State::kPlaying) {
      return fFrame.because(wallMs(), "gameplay"); // a moving picture by definition
    }
    // The results screen counts up, slides its panels and fades in, all of
    // which end. After that it is a still picture like any other.
    if (fState == State::kResults &&
        (wallMs() - fStateEnterWall < 2500.0 ||
         fReplayBrowser.scrolling())) {
      return fFrame.because(wallMs(), "results settling");
    }
    // The logo tracks the music and the triangles drift, and either is a
    // reason to keep drawing. Neither is a reason to stop: everything below
    // -- an event, a panel sliding, something that marked itself -- still
    // applies, and returning an answer here rather than a reason took the
    // whole menu out of the conversation.
    if (fState == State::kMainMenu &&
        (fSettings.flag("visualiser") || fSettings.flag("menutriangles"))) {
      return fFrame.because(wallMs(), "menu visualiser or triangles");
    }
    if (fState == State::kPaused && fSettings.flag("pausetriangles")) {
      return fFrame.because(
          wallMs(), "pause triangles"); // triangles drift inside the buttons, as lazer's
                              // do
    }
    if (fMirrors.searching() || fMirrors.previewPending() ||
        fMirrors.transferring()) {
      return fFrame.because(
          wallMs(),
          "a search, preview or transfer"); // progress that is being watched
    }
    if (fMirrors.previewId() >= 0 || fExportDialog.open()) {
      return fFrame.because(
          wallMs(),
          "a preview or the export dialog"); // a transfer or a dialog with live
                                             // status in it
    }
    // The replay browser's strip glides to the panel that was picked. It is
    // drawn as an overlay rather than as a screen, so it has nobody else to
    // ask for the frames that carry it there.
    if (fReplayBrowser.open() && fReplayBrowser.scrolling()) {
      return fFrame.because(wallMs(), "replay strip gliding");
    }
    // Every retained tree reports damage and continuation as one atomic
    // result during updateScreens(). The application does not inspect scene
    // internals or duplicate each widget's settling condition here.
    if (fFrame.fSceneWantsFrame) {
      return fFrame.because(wallMs(), "a retained scene requested continuation");
    }
    const double now = wallMs();
    if (fFrame.fFramesOwed > 0) {
      --fFrame.fFramesOwed;
      return fFrame.because(wallMs(), "frames owed");
    }
    if (fFrame.fWakeWall > 0.0 && now >= fFrame.fWakeWall) {
      fFrame.fWakeWall = 0.0;
      return fFrame.because(wallMs(), "a screen asked to be woken");
    }
    // Something marked itself outside a frame: an event handler, or work
    // that finished in the background. Damage marked while drawing only says
    // what to repaint when a frame next happens -- a screen that repaints
    // itself whole every time it draws would otherwise be asking for the next
    // frame, every frame, for ever.
    if (fFrame.fDamageDrives) {
      return fFrame.because(wallMs(), "something marked itself outside a frame");
    }
    if (now <= fFrame.fRedrawUntilWall) {
      return fFrame.because(wallMs(), "redraw window");
    }
    // Safety net: whatever the screens forgot to announce shows up within
    // this long rather than never.
    if (fFrame.fDiag.fTraceRepaint && fState == State::kMainMenu &&
        now - fFrame.fLastDrawWall > 500.0) {
      std::println(std::cerr, "[menu] idle: animating {} dt {:.0f} ms",
                   fFrame.fSceneWantsFrame, fUiDt);
    }
    return now - fFrame.fLastDrawWall > 500.0
               ? fFrame.because(wallMs(), "the half-second safety net")
               : false;
  }

  // The part of the window that a screen is actually showing, in the window's
  // own coordinates. Empty when nothing is known about the placement.
  [[nodiscard]] skia::SkIRect visiblePortion() const {
    const auto visible =
        fWindowRuntime.visiblePortion(fWin.fScreenW, fWin.fScreenH);
    if (!visible) {
      return skia::SkIRect::MakeEmpty();
    }
    return skia::SkIRect::MakeLTRB((*visible)[0], (*visible)[1],
                                    (*visible)[2], (*visible)[3]);
  }

  void frame() {
    // A screen that returned early last time -- gameplay without a beatmap
    // loaded, say -- could leave this set; outside frame() nothing is drawing
    // by definition.
    fFrame.fDrawing = false;
    if (fWindowRuntime.takeRefreshRequest()) {
      // Set on the event thread, acted on here: everything those buffers held
      // is suspect, so the history of what they hold goes with it.
      fFrame.fBlitHistory.clear();
      // X11 says which rectangles were exposed and GLFW does not pass them
      // on, so the region is reconstructed from where the window sits: the
      // part of it a screen is showing is the part worth painting. Dragging a
      // window back in from off the edge then costs the strip that came back,
      // not the whole window, every frame of the drag.
      const skia::SkIRect onScreen = this->visiblePortion();
      if (onScreen.isEmpty() || (onScreen.width() >= fWin.fScreenW &&
                                 onScreen.height() >= fWin.fScreenH)) {
        fFrame.damageAll("the window system asked for a repaint");
      } else {
        fFrame.damage(skia::SkRect::Make(onScreen));
      }
    }
    this->applySwapInterval();
    // Work finishing in the background changes what is on screen, so it is
    // as good a reason to draw as an event.
    if (client::http::poll() > 0 || fLoader.poll() > 0) {
      fFrame.requestRedraw(wallMs(), 600.0);
    }
    fLibraryRuntime.drainDroppedFiles();
    fInput.drainInput();
    {
      const double wallNow = wallMs();
      // The time that actually passed. An exponential ease is a function of
      // elapsed time and converges for any of it, so a frame that arrives
      // late produces one large step, which is what a gap should look like.
      // What cannot take an arbitrary step is anything integrating time
      // linearly, and the only such thing here is the triangle field, which
      // caps its own travel at one triangle's height.
      fUiDt = fUiPrevWall > 0.0 ? wallNow - fUiPrevWall : 16.0;
      fUiPrevWall = wallNow;
    }
    // The music belongs to the client rather than to the screen that happens
    // to be up: it plays under everything that is not gameplay, and a track
    // that ends has to be followed by another wherever the player is standing
    // -- including a screen that is not drawing frames at all just now.
    // Twenty times a second rather than once per iteration: this asks OpenAL
    // whether the source is still going, and the loop spins several hundred
    // times a second on a screen that is not drawing anything.
    if (fState != State::kPlaying && fState != State::kPaused &&
        wallMs() - fMusicPollWall > 50.0) {
      fMusicPollWall = wallMs();
      fLibraryRuntime.updateMenuMusic();
    }
    if (!this->needsFrame()) {
      // Nothing to show: no clear, no draw, no swap, so the front buffer
      // keeps what it already had.
#ifndef __EMSCRIPTEN__
      std::this_thread::sleep_for(std::chrono::milliseconds(4));
#endif
      // In a browser this is a callback on the main thread and the frame is
      // paced by requestAnimationFrame: sleeping here would block the page,
      // and emscripten implements the sleep as a spin, which is worse than
      // the frame we are trying not to draw.
      return;
    }
    // A video being rendered runs on its own thread; this is the client
    // asking how far it has got.
    if (fVideoExporter.active()) {
      this->pollExportVideo();
    }
    // Screens built as a scene tree settle before anything is drawn: the
    // pointer lands where it lands, values ease, the layout is redone, and
    // what changed is marked. Only then is it known whether the frame that
    // was asked for has anything to put on screen.
    const auto updateStart = std::chrono::steady_clock::now();
    this->updateScreens();
    fFrame.fDiag.fLastUpdateUs = std::chrono::duration_cast<std::chrono::microseconds>(
                              std::chrono::steady_clock::now() - updateStart)
                              .count();
    if (this->nothingToPaint()) {
      // The question the safety net exists to ask has just been asked and
      // answered, so the net does not need to fire.
      fFrame.fLastDrawWall = wallMs();
#ifndef __EMSCRIPTEN__
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
#endif
      return;
    }
    fFrame.fLastDrawWall = wallMs();
    fFrame.fDiag.fFrameStart = std::chrono::steady_clock::now();

    // The renderer is chosen per frame, because only the UI screens may use
    // the CPU one: gameplay draws precomputed GPU textures.
    // Gameplay draws on the CPU as well when asked to: the slider bodies it
    // needs were computed on the GPU and flattened into memory once, at load,
    // so nothing is read back per frame.
    const bool software =
        fSettings.choice("renderer") == 1 && this->ensureRasterSurface();
    if (software != fFrame.fDrewOnRaster) {
      // Same reasoning: the frame moves to a different surface, and whatever
      // that one holds has nothing to do with the ages being reported.
      fFrame.damageAll("renderer changed", /*buffersGone=*/true);
    }
    fFrame.fDrewOnRaster = software;
    fFrame.fSurface = software ? fFrame.fRasterSurface : fFrame.fWindowSurface;

    // Only screens that have been taught to say what they changed may be
    // clipped to it. The rest have to repaint whole, and saying so here is
    // what stops a small widget that does report itself -- the FPS counter,
    // say -- from becoming the only thing a frame is allowed to touch.
    switch (fState) {
    case State::kMainMenu:
    case State::kResults:
    case State::kDownload:
    case State::kSongSelect:
    case State::kPaused:
      break; // these mark what they change: the listing does it per node
    case State::kPlaying:
      fFrame.damageAll("gameplay"); // a moving picture by definition
      break;
    default:
      fFrame.damageAll("screen does not report damage");
      break;
    }
    this->beginFrame();
    switch (fState) {
    case State::kMainMenu:
      this->frameMainMenu();
      break;
    case State::kSongSelect:
      this->frameSongSelect();
      break;
    case State::kDownload:
      this->frameDownload();
      break;
    case State::kPlaying:
      this->framePlaying();
      break;
    case State::kPaused:
      this->framePaused();
      break;
    case State::kResults:
      this->frameResults();
      break;
    }
    this->limitFrameRate();
  }

  // lazer's FPSCounter sits in the corner of every screen, and shows the
  // frame time beside the rate. The profiling readout is a separate thing and
  // stays behind --profile.
  void notify(std::string text,
              skia::SkColor color = client::palette::kAccent2) {
    fFrame.requestRedraw(wallMs(), 4500.0); // the toast has to fade out on its own
    fToast = std::move(text);
    fToastColor = color;
    fToastWall = wallMs();
    std::println(std::cerr, "[notify] {}", fToast);
  }

  void drawToast(skia::SkCanvas *canvas) {
    constexpr double kLifetimeMs = 4000.0;
    const double age = wallMs() - fToastWall;
    if (fToast.empty() || age > kLifetimeMs) {
      return;
    }
    const skiff::paint::Painter p(canvas, fFont);
    const float alpha = static_cast<float>(
        std::min(1.0, std::min(age / 200.0, (kLifetimeMs - age) / 400.0)));
    const float w = p.measure(fToast, 14.0f) + 32.0f;
    const skia::SkRect box = skia::SkRect::MakeXYWH(
        static_cast<float>(fWin.fScreenW) - w - 20.0f, 20.0f, w, 44.0f);
    p.fillRounded(box, 8.0f, client::palette::kBackground5, alpha * 0.95f);
    p.fillRect(skia::SkRect::MakeXYWH(box.fLeft, box.fTop, 4.0f, box.height()),
               fToastColor, alpha);
    p.textCentered(fToast, box.centerX() + 2.0f, box.centerY() + 5.0f, 14.0f,
                   skia::kWhite, alpha);
    fFrame.damage(box);
  }

  void drawFpsCounter(skia::SkCanvas *canvas) {
    const double now = wallMs();
    if (fFpsPrevWall > 0.0) {
      const double dt = now - fFpsPrevWall;
      // Exponential smoothing, so the number is readable rather than jittery.
      fFpsFrameMs = fFpsFrameMs > 0.0 ? fFpsFrameMs * 0.9 + dt * 0.1 : dt;
    }
    fFpsPrevWall = now;
    if (!fSettings.flag("fps") || fFpsFrameMs <= 0.0) {
      return;
    }
    const skiff::paint::Painter p(canvas, fFont);
    const float sw = static_cast<float>(fWin.fScreenW);
    const float sh = static_cast<float>(fWin.fScreenH);
    const skia::SkRect box =
        skia::SkRect::MakeXYWH(sw - 92.0f, sh - 52.0f, 80.0f, 42.0f);
    // Drawn after the clip is lifted, straight into a buffer that is several
    // frames old, so a translucent background lets the numbers that were
    // there before show through and pile up. Partial redraw is the mode where
    // that happens, and the mode where this has to be opaque.
    p.fillRounded(
        box, 6.0f,
        skia::colorSetARGB(this->partialRedraw() ? 255 : 150, 8, 6, 12));
    p.textCentered(std::format("{:.0f} fps", std::round(1000.0 / fFpsFrameMs)),
                   box.centerX(), box.fTop + 18.0f, 15.0f, skia::kWhite);
    p.textCentered(std::format("{:.1f} ms", fFpsFrameMs), box.centerX(),
                   box.fBottom - 8.0f, 12.0f, skia::kWhite, 0.7f);
    fFrame.includeInBlit(box, fWin.fScreenW, fWin.fScreenH);
  }

  void present() {
    const auto frameStart = fFrame.fDiag.fFrameStart;
    // Overlays float above whatever screen is drawn.
    auto *canvas = fFrame.fSurface->getCanvas();
    if (fModSelect.visible()) {
      this->drawModSelect(canvas);
    }
    if (fSettingsPanel.visible()) {
      // What it repaints was worked out before the frame began, by the panel
      // itself; here it is only drawn.
      this->drawSettings(canvas);
    }
    if (fReplayBrowser.open()) {
      this->drawReplayList(canvas);
    }
    if (fExportDialog.open()) {
      this->drawExportDialog(canvas);
    }
    fDeleteDialog.render(canvas);
    this->drawToast(canvas);
    fFrame.showDamage(canvas, fWin.fScreenW, fWin.fScreenH, wallMs(),
                      fSettings.flag("damageoverlay"));
    canvas->restoreToCount(fFrame.fFrameSave);
    // Drawn after the clip is lifted: a small thing in the far corner that
    // changes every frame, so clipping to it would drag the repainted region
    // across the whole screen, and clipping it away would freeze it. It still
    // has to reach the window, which on the CPU renderer means saying so --
    // otherwise the counter is drawn into a surface whose corner is never
    // carried over, and the number stops.
    this->drawFpsCounter(canvas);
    fFrame.fDiag.fBlitStart = std::chrono::steady_clock::now();

    // Everything about this frame, for the four hundred milliseconds after a
    // size event: what the client thinks the screen is, what the two surfaces
    // actually are, what was repainted and what is carried to the window.
    // Four fixes have been aimed at this from four theories and none of them
    // was measured; this is all of the state at once so the next one is not a
    // fifth theory.
    if (fFrame.fDiag.fTraceRepaint && wallMs() - fLastResizeWall < 400.0) {
      const auto dims = [](const skia::Sp<skia::SkSurface> &surface) {
        return surface
                   ? std::format("{}x{}", surface->width(), surface->height())
                   : std::string("none");
      };
      std::string clip = "whole";
      if (!fFrame.fComputedClipFull && !fFrame.fComputedClip.empty()) {
        const skia::SkIRect &r = fFrame.fComputedClip.front();
        clip =
            std::format("{}+{}+{}x{}", r.fLeft, r.fTop, r.width(), r.height());
        if (fFrame.fComputedClip.size() > 1) {
          clip += std::format(" and {} more", fFrame.fComputedClip.size() - 1);
        }
      }
      std::string blit = "whole";
      if (!fFrame.fBlitRegions.empty()) {
        const skia::SkIRect &r = fFrame.fBlitRegions.front();
        blit =
            std::format("{}+{}+{}x{}", r.fLeft, r.fTop, r.width(), r.height());
      }
      std::println(std::cerr,
                   "[frame] +{:4.0f} ms  screen {}x{}  raster {}  window {}  "
                   "{}  repaint {}  blit {}  {}",
                   wallMs() - fLastResizeWall, fWin.fScreenW, fWin.fScreenH,
                   dims(fFrame.fRasterSurface), dims(fFrame.fWindowSurface),
                   fFrame.fDrewOnRaster ? "cpu" : "gpu", clip, blit,
                   fFrame.fComputedClipFull ? fFrame.fFullDamageReason : "");
    }

    if (fFrame.fDrewOnRaster && fFrame.fWindowSurface) {
      // The CPU frame lives in main memory; the window wants it as pixels.
      // Only the part that was repainted is carried over: the window's buffer
      // already holds the rest, under exactly the assumption that let the
      // frame be clipped in the first place. This is what makes the cost of
      // the blit follow the damage instead of being a whole window every
      // frame -- which it was, at a constant millisecond and a half.
      if (auto image = fFrame.fRasterSurface->makeImageSnapshot()) {
        auto *windowCanvas = fFrame.fWindowSurface->getCanvas();
        if (fFrame.fBlitRegions.empty()) {
          windowCanvas->drawImage(image.get(), 0.0f, 0.0f);
        } else {
          // A subset image per region, drawn through the canvas. writePixels
          // was quicker to say and put the pixels in the wrong place: the
          // window surface is wrapped bottom-left, and that call does not go
          // through the canvas that knows it. Cut the piece out first and let
          // the canvas place it -- only the piece is uploaded either way.
          //
          // Separate pieces rather than one box around them: the frame
          // counter sits in a far corner, and a box containing it and the
          // middle of the screen is most of the screen.
          for (const auto &area : fFrame.fBlitRegions) {
            // Drawn as a rectangle of the frame rather than cut out of it
            // first: makeSubset is free to answer nothing -- a null image is
            // not an error, it is a "not like this" -- and a blit that
            // quietly does nothing is a window that stops updating while the
            // client goes on drawing into a surface nobody is reading.
            const skia::SkRect piece = skia::SkRect::Make(area);
            windowCanvas->drawImageRect(
                image.get(), piece, piece, skia::SkSamplingOptions(), nullptr,
                skia::SkCanvas::kStrict_SrcRectConstraint);
          }
        }
      }
      fContext->flushAndSubmit(fFrame.fWindowSurface.get());
    } else {
      fContext->flushAndSubmit(fFrame.fSurface.get());
    }

    fDrawnMouseX = fWin.fMouseX;
    fDrawnMouseY = fWin.fMouseY;
    const auto beforeSwap = std::chrono::steady_clock::now();
    // What changed since the last frame the compositor was given. Handing it
    // over means it can leave the rest of the window alone instead of taking
    // the whole surface every frame -- the other half of what buffer age
    // buys, and the half that lands on the "swap" line of the frame report.
    // The same claim, made to the compositor instead of to the blit: these
    // rectangles are what changed since the buffer coming back was last ours,
    // which is only knowable from a reported age.
    std::vector<std::array<int, 4>> damage;
    if (fFrame.fAgeReported && !fFrame.fComputedClipFull &&
        !fFrame.fComputedClip.empty()) {
      damage.reserve(fFrame.fComputedClip.size());
      for (const auto &rect : fFrame.fComputedClip) {
        damage.push_back({rect.fLeft, rect.fTop, rect.width(), rect.height()});
      }
    }
    if (damage.empty() || !present::swapWithDamage(fWin.fScreenH, damage)) {
      glfw::glfwSwapBuffers(fWindow);
    }
    fLastSwapUs = std::chrono::duration_cast<std::chrono::microseconds>(
                      std::chrono::steady_clock::now() - beforeSwap)
                      .count();
    fFrame.reportCost(frameStart, beforeSwap, fWin.fScreenW, fWin.fScreenH,
                      this->partialRedraw());
    fFrame.fDrawing = false;
  }

  void framePlaying() {
    using clock = std::chrono::steady_clock;
    // Zero until the first frame has been through, so that nothing before it
    // counts against the map.
    const double now = fPlay.fAwaitingFirstFrame ? 0.0 : this->nowMs();
    if (this->shouldStop(now)) {
      this->finishPlay();
      this->frameResults();
      return;
    }
    this->submitAutoplay(now);
    this->rebuildSliderBodiesIfStale();

    // One frame path, measured or not. There were two of these, and the one
    // taken under --profile called flushAndSubmit and glfwSwapBuffers itself
    // instead of present(). present() is where canvas->restoreToCount runs,
    // so without it every frame left its save and its clip on the stack, and
    // clips intersect: the visible area closed in on nothing while the stack
    // grew without bound. The picture froze and the client slowed down the
    // longer it ran -- and every number measured that way was measured
    // against a canvas doing something no normal frame does.
    const auto t0 = clock::now();
    fPlay.fEngine->advance(now);
    const auto t1 = clock::now();
    fJudgements.process(now, *fPlay.fEngine, *fPlay.fMap, fComboInfo, fView,
                        fSet, fSkin);
    fView.render(this->gameplayCtx(fFrame.fSurface->getCanvas()), now);
    const auto t2 = clock::now();
    this->present();
    const auto t3 = clock::now();

    if (fShowProfile) {
      const auto us = [](auto from, auto to) {
        return static_cast<double>(
            std::chrono::duration_cast<std::chrono::microseconds>(to - from)
                .count());
      };
      auto &p = fView.profileSlot();
      p.advUs = us(t0, t1);
      p.renderUs = us(t1, t2);
      // present() times the swap itself; what is left of it is the overlays,
      // the blit to the window and the flush.
      p.swapUs = static_cast<double>(fLastSwapUs);
      p.flushUs = std::max(0.0, us(t2, t3) - static_cast<double>(fLastSwapUs));
      fView.advanceProfile();
    }

    if (fPlay.fAwaitingFirstFrame) {
      this->startGameplayClock();
    }
  }

  // Which map, at which playfield scale, and drawn how. All three change the
  // pictures, so all three are in the name they are filed under.
  [[nodiscard]] std::string sliderBodyKey() const {
    // The md5 rather than the file name: two sets can both hold a Normal.osu.
    return std::format("{}:{}:{:.4f}:{}", this->beatmapMd5(), fBeatmapFilename,
                       fScale, fSettings.choice("renderer"));
  }

  // A rasterised body is only as sharp as the scale it was built at. Rebuilt
  // after a resize has settled rather than during it: this costs on the order
  // of a second on a map with hundreds of sliders, and a window being dragged
  // delivers a resize per frame.
  void rebuildSliderBodiesIfStale() {
    if (!fSliderBodiesStale || !fPlay.fMap) {
      return;
    }
    if (wallMs() - fLastResizeWall < 250.0) {
      return;
    }
    fSkin.precomputeSliderBodies(*fPlay.fMap, fComboInfo, fScale,
                                 fContext.get(), this->sliderBodyKey());
    if (fSettings.choice("renderer") == 1) {
      fSkin.flattenBodiesToRaster(fContext.get());
    }
    fSliderBodyScale = fScale;
    fSliderBodiesStale = false;
    fFrame.damageAll("slider bodies rebuilt");
  }

  // The renderer holds no back-reference to the app; each frame it is handed
  // exactly what it needs.
  [[nodiscard]] client::GameplayView::Ctx gameplayCtx(skia::SkCanvas *canvas) {
    client::GameplayView::Ctx c;
    c.fCanvas = canvas;
    c.fMap = fPlay.fMap ? &*fPlay.fMap : nullptr;
    c.fEngine = fPlay.fEngine ? &*fPlay.fEngine : nullptr;
    c.fSkin = &fSkin;
    c.fCombo = &fComboInfo;
    c.fFont = &fFont;
    c.fDisplayFont = &fDisplayFont;
    c.fScale = fScale;
    c.fOffsetX = fOffsetX;
    c.fOffsetY = fOffsetY;
    c.fScreenW = fWin.fScreenW;
    c.fScreenH = fWin.fScreenH;
    c.fCursor = fCursor;
    c.fCursorSize = fSettings.value("cursorsize");
    c.fUiScale =
        std::clamp(static_cast<float>(fWin.fScreenH) / 1080.0f, 0.7f, 3.0f);
    c.fDim = fSettings.value("dim");
    c.fNoGlow = fNoGlow;
    c.fHitLighting = fSettings.flag("hitlighting");
    c.fPp = fPlay.fEngine
                ? client::pricePlay(fPlay.fPlayAttributes,
                                    fPlay.fEngine->score())
                : 0.0;
    c.fShowProfile = fShowProfile;
    return c;
  }

  // ---- Play lifecycle ---------------------------------------------------

  void resetGameplayState() {
    fHasRawPrev = false;
    fVirtualCursor = osu::kPlayfieldCenter;
    fJudgements.reset();
    fView.reset();
    fPlay.fAutoplayEvents.clear();
    fAutoplayIndex = 0;
    fPlay.fRecordedEvents.clear();
    fHeldMask = 0;
  }

  void startPlay(int setIdx, int diffIdx) {
    // Retries are counted per map: coming here from anywhere but the retry
    // button starts the count again.
    fPlay.fRetryCount = fRetryPending ? fPlay.fRetryCount + 1 : 0;
    fRetryPending = false;
    fMirrors.forgetPreview();
    if (setIdx < 0 || setIdx >= static_cast<int>(fLibrary.sets().size())) {
      return;
    }
    auto set = fLibrary.setForBlocking(setIdx);
    if (!set || diffIdx < 0 ||
        diffIdx >= static_cast<int>(set->fBeatmaps.size())) {
      return;
    }
    fPlayingSet = setIdx;
    fPlayingDiff = diffIdx;
    fPlay.fLastSavedReplay.clear();
    // Only a play that was asked for by watchReplay is driven from a file.
    // Without this, everything after watching one replay would keep playing
    // that replay back -- and, being "recorded", would be saved as a copy.
    fReplayPath = std::exchange(fPendingReplay, {});
    fAutoplay = fCliAutoplay || !fReplayPath.empty();
    fSet = *set;           // active copy: gameplay reads audio/bg from here
    fMenuMusicForSet = -1; // gameplay reloads the track from scratch
    fAudio.setLooping(false);
    this->resetGameplayState();
    // Overlays must not survive into gameplay.
    this->closeSettings();
    fModSelect.close();
    fExportDialog.close();
    this->startGameplay(fSet.fBeatmaps[static_cast<std::size_t>(diffIdx)]);
    this->switchState(State::kPlaying);
    fView.invalidate();
    this->setCursorVisible(false);
  }

  void retry() {
    fRetryPending = true; // the pause overlay counts these
    this->startPlay(fPlayingSet, fPlayingDiff);
  }

  void pauseGame() {
    fPlay.fPausedNow = this->nowMs();
    fAudio.pause();
    this->switchState(State::kPaused);
    this->setCursorVisible(true);
  }

  void resumeGame() {
    // Re-anchor the clock at the frozen instant: wall time spent in the
    // pause menu never existed as far as the game timeline is concerned.
    fPlay.fClock.reset(wallMs(), fPlay.fPausedNow);
    fLastClockSyncWall = wallMs();
    fAudio.resume();
    this->switchState(State::kPlaying);
    fView.invalidate();
    this->setCursorVisible(false);
  }

  void quitToSelect() {
    fAudio.stop();
    fMenuMusicForSet = -1; // let updateMenuMusic restart the loop
    this->switchState(State::kSongSelect);
    fView.invalidate();
    fBackgroundForSet = -1; // gameplay replaced the cached background
    this->setCursorVisible(true);
  }

  void finishPlay() {
    fResult =
        client::captureResult(*fPlay.fEngine, fPlay.fPlayAttributes);
    this->printResult();
    // Replays are kept by default (the setting can turn it off), so a good
    // run is never lost because the flag was not passed.
    // Watching a replay must not write it back out again.
    if (fReplayPath.empty() && (fRecord || fSettings.flag("savereplay"))) {
      this->saveReplay(); // the index picks it up; the results list follows
    }
    this->switchState(State::kResults);
    fView.invalidate();
    this->setCursorVisible(true);
  }

  // With an absolute pointer the desktop cursor stops at the screen edge, so
  // scaling its position by a sensitivity below 1 fences the playfield into a
  // smaller box -- the "invisible walls". Anything other than 1:1 therefore
  // grabs the pointer and integrates its motion instead, which is also what
  // raw input needs.
  [[nodiscard]] bool relativeCursor() const {
    return std::abs(fSettings.value("sensitivity") - 1.0f) > 1e-3f ||
           fSettings.flag("rawinput");
  }

  void applyPointerMode() {
    if (fState == State::kPlaying) {
      this->setCursorVisible(false); // re-evaluates the mode below
    }
    fWindowRuntime.requestRawMotion(fSettings.flag("rawinput"));
  }

  void setCursorVisible(bool visible) {
#ifdef __EMSCRIPTEN__
    if (visible) {
      EM_ASM(Module.setCursorVisible(true));
    } else {
      EM_ASM(Module.setCursorVisible(false));
    }
    glfw::glfwSetInputMode(fWindow, glfw::kCursor,
                           visible ? glfw::kCursorNormal : glfw::kCursorHidden);
#else
    const int hidden =
        this->relativeCursor() ? glfw::kCursorDisabled : glfw::kCursorHidden;
    fWindowRuntime.requestCursorMode(visible ? glfw::kCursorNormal : hidden);
#endif
  }

  // ---- Download screen logic -------------------------------------------



















#ifdef __EMSCRIPTEN__
  static void emscriptenFrameProc(void *arg) {
    static_cast<App *>(arg)->emscriptenFrame();
  }

  void emscriptenFrame() {
    glfw::glfwPollEvents();

    {
      // Polled rather than delivered by a callback here, so it is only an
      // event when it actually moved: pushing the same position every frame
      // made every frame an event, and an event owes frames.
      double cx = 0, cy = 0;
      glfw::glfwGetCursorPos(fWindow, &cx, &cy);
      if (cx != fPolledCursorX || cy != fPolledCursorY) {
        fPolledCursorX = cx;
        fPolledCursorY = cy;
        this->enqueue({wallMs(), EventType::kCursorMove, 0, 0,
                       static_cast<float>(cx), static_cast<float>(cy)});
      }
    }

    int fw = 0, fh = 0;
    glfw::glfwGetFramebufferSize(fWindow, &fw, &fh);
    if (fw != fWin.fScreenW || fh != fWin.fScreenH) {
      this->resize(fw, fh);
    }

    if (fWindowRuntime.quitting()) {
      if (fPlay.fEngine &&
          (fState == State::kPlaying || fState == State::kPaused)) {
        this->printResult();
        if (fRecord)
          this->saveReplay();
      }
      emscripten::emscripten_cancel_main_loop();
      return;
    }

    this->frame();
  }
#endif

  // ---- Shared UI helpers ------------------------------------------------

  // Decode the background off-thread; the UI keeps the previous artwork
  // until the new one is ready instead of stalling on a JPEG decode.
  void requestBackground(int setIndex,
                         const std::shared_ptr<osu::BeatmapSet> &set) {
    std::vector<std::uint8_t> bytes;
    for (const auto &info : set->fBeatmaps) {
      if (info.fMeta.fBackground.empty()) {
        continue;
      }
      const auto span = set->findFile(info.fMeta.fBackground);
      if (!span.empty()) {
        bytes.assign(span.begin(), span.end());
        break;
      }
    }
    if (bytes.empty()) {
      fView.setBackground(nullptr);
      fFrame.damageAll("artwork cleared");
      return;
    }
    auto image = std::make_shared<skia::Sp<skia::SkImage>>();
    fLoader.submit(
        static_cast<std::uint64_t>(setIndex) | (4ull << 32),
        [bytes = std::move(bytes), image] { *image = loadImage(bytes); },
        [this, setIndex, image] {
          if (setIndex != fLibrary.selSet() || !*image) {
            return;
          }
          fView.setBackground(*image);
          fView.preScaleBackground(this->gameplayCtx(nullptr));
          fFrame.damageAll("artwork arrived");
          fFrame.requestRedraw(wallMs(), 400.0);
        });
  }

  void loadSelectBackground(const osu::BeatmapSet &set) {
    fFrame.damageAll("select background"); // the backdrop changes either way
    for (const auto &info : set.fBeatmaps) {
      if (!info.fMeta.fBackground.empty()) {
        const auto bytes = set.findFile(info.fMeta.fBackground);
        if (!bytes.empty()) {
          fView.setBackground(loadImage(bytes));
          fView.preScaleBackground(this->gameplayCtx(nullptr));
          return;
        }
      }
    }
    fView.setBackground(nullptr);
  }

  void fillRounded(skia::SkCanvas *canvas, const skia::SkRect &rect,
                   float radius, skia::SkColor color) {
    skiff::paint::Painter(canvas, fFont).fillRounded(rect, radius, color);
  }

  void strokeRounded(skia::SkCanvas *canvas, const skia::SkRect &rect,
                     float radius, skia::SkColor color, float width) {
    skiff::paint::Painter(canvas, fFont)
        .strokeRounded(rect, radius, color, width);
  }

  void drawTextClipped(skia::SkCanvas *canvas, const std::string &text, float x,
                       float y, float maxW, float size, skia::SkColor color,
                       float alpha = 1.0f) {
    skiff::paint::Painter(canvas, fFont)
        .textClipped(text, x, y, maxW, size, color, alpha);
  }

  void drawTextCentered(skia::SkCanvas *canvas, const std::string &text,
                        float cx, float y, float size, skia::SkColor color,
                        float alpha = 1.0f) {
    skiff::paint::Painter(canvas, fFont)
        .textCentered(text, cx, y, size, color, alpha);
  }

  [[nodiscard]] static skia::SkColor starColor(double stars) {
    return client::palette::starColor(stars);
  }

  void drawScreenBackground(skia::SkCanvas *canvas) {
    if (fView.hasBackground()) {
      fView.drawBackground(this->gameplayCtx(canvas), canvas);
    } else {
      canvas->clear(skia::colorSetARGB(255, 18, 14, 24));
    }
  }

  // ---- Main menu (lazer-style logo) -------------------------------------

  // ---- Main menu button system (port of lazer's ButtonSystem) -----------

  [[nodiscard]] float approach(float current, float target, float tauMs) const {
    return skiff::paint::approach(current, target, tauMs, fUiDt);
  }

  // F2 in song select: move the selection (lazer's FooterButtonRandom).
  void selectRandom() {
    if (fLibrary.visible().empty()) {
      return;
    }
    std::uniform_int_distribution<std::size_t> pick(
        0, fLibrary.visible().size() - 1);
    fLibrary.selSet() = fLibrary.visible()[pick(fUiRng)];
    fLibrary.selDiff() = 0;
  }

  // "random" in the menu's play submenu: pick a map and start it.
  void playRandom() {
    if (fLibrary.sets().empty()) {
      this->switchState(State::kSongSelect);
      return;
    }
    std::uniform_int_distribution<std::size_t> pick(0,
                                                    fLibrary.sets().size() - 1);
    const auto si = pick(fUiRng);
    const auto &infos = fLibrary.sets()[si].fInfos;
    if (infos.empty()) {
      return;
    }
    std::uniform_int_distribution<std::size_t> pickDiff(0, infos.size() - 1);
    fLibrary.selSet() = static_cast<int>(si);
    fLibrary.selDiff() = static_cast<int>(pickDiff(fUiRng));
    this->startPlay(fLibrary.selSet(), fLibrary.selDiff());
  }

  // ---- Main menu ---------------------------------------------------------
  //
  // Split the way the ported screens are: everything that decides what the
  // menu looks like happens here, with nothing drawn, so that what changed is
  // known before the client commits to a frame.

  // The artwork behind the menu and behind song select is the same picture of
  // the same selection, fetched the same way. setFor() is asynchronous: the
  // selection is only marked as done once the set has actually arrived, or
  // the first frame consumes the request and the artwork never appears.
  void ensureBackgroundForSelection() {
    if (fLibrary.sets().empty() || fBackgroundForSet == fLibrary.selSet()) {
      return;
    }
    if (auto set = fLibrary.setFor(fLibrary.selSet())) {
      fBackgroundForSet = fLibrary.selSet();
      this->requestBackground(fLibrary.selSet(), set);
    }
  }

  // Which keys the menu answers to. The screen names them rather than taking
  // the window system's numbers, so that nothing under client.mainmenu has to
  // know what a GLFW key code is.
  [[nodiscard]] static client::mainmenu::Screen::Key menuKey(int key) {
    using Key = client::mainmenu::Screen::Key;
    switch (key) {
    case glfw::kKeyEscape:
      return Key::kEscape;
    case glfw::kKeyEnter:
      return Key::kEnter;
    case glfw::kKeySpace:
      return Key::kSpace;
    case glfw::kKeyP:
      return Key::kP;
    case glfw::kKeyB:
      return Key::kB;
    case glfw::kKeyD:
      return Key::kD;
    case glfw::kKeyI:
      return Key::kI;
    case glfw::kKeyQ:
      return Key::kQ;
    case glfw::kKeyS:
      return Key::kS;
    case glfw::kKeyR:
      return Key::kR;
    default:
      return Key::kOther;
    }
  }

  // What the menu asked for. Moving between its own levels it does itself;
  // these are the ones that need the client.
  void menuAction(MenuAction action) {
    switch (action) {
    case MenuAction::kBrowse:
      fInput.openDownloads();
      break;
    case MenuAction::kImport:
      fLibraryRuntime.importOsz();
      break;
    case MenuAction::kExit:
      this->requestQuit();
      break;
    case MenuAction::kSolo:
      fSwallowChar = true; // the 's' that triggered this must not be typed
      this->switchState(State::kSongSelect);
      break;
    case MenuAction::kRandom:
      this->playRandom();
      break;
    case MenuAction::kSettings:
      this->toggleSettings();
      break;
    case MenuAction::kOpenPlay:
    case MenuAction::kBack:
      break; // answered inside the screen
    }
  }

  void updateMainMenu() {
    this->ensureBackgroundForSelection();

    client::mainmenu::Screen::Ctx ctx;
    ctx.fWidth = static_cast<float>(fWin.fScreenW);
    ctx.fHeight = static_cast<float>(fWin.fScreenH);
    ctx.fMouseX = fWin.fMouseX;
    ctx.fMouseY = fWin.fMouseY;
    ctx.fPointerActive = !fSettingsPanel.visible() ||
                         fWin.fMouseX >= fSettingsPanel.occupiedWidth();
    ctx.fDtMs = fUiDt;
    ctx.fNowWall = wallMs();
    ctx.fFont = &fFont;
    ctx.fVisualiser = fSettings.flag("visualiser");
    ctx.fTriangles = fSettings.flag("menutriangles");
    ctx.fHasArtwork = fView.hasBackground();
    ctx.fLibraryEmpty = fLibrary.sets().empty();
    ctx.fAudioPlaying = fAudio.playing();
    ctx.fSamples = fAudio.monoSamples();
    ctx.fSampleRate = fAudio.sampleRate();
    ctx.fAudioPositionMs = [this] { return fAudio.positionSec() * 1000.0; };
    fMainMenu.update(ctx);

    if (const char *reason = fMainMenu.takeFullDamage()) {
      fFrame.damageAll(reason);
    }
    fFrame.consume(fMainMenu.finishFrame());
  }

  void frameMainMenu() {
    auto *canvas = fFrame.fSurface->getCanvas();
    fMainMenu.render(canvas);
    this->drawScreenFadeIn(canvas);
    this->present();
  }

  // ---- Song select ------------------------------------------------------

  // ---- Settings overlay ---------------------------------------------------
  //
  // The panel itself lives in client.settingspanel; this only bridges it to
  // the app's input and to applying the values.

  void toggleSettings() {
    fSettingsPanel.toggle(wallMs());
    if (!fSettingsPanel.open()) {
      fSettings.save();
      this->applySettings();
    }
  }

  void closeSettings() {
    if (fSettingsPanel.open()) {
      fSettingsPanel.close(wallMs());
      fSettings.save();
      this->applySettings();
    }
  }

  void drawSettings(skia::SkCanvas *canvas) { fSettingsPanel.render(canvas); }

  bool settingsClick(float x, float y, bool pressed) {
    fSettingsPanel.touched(); // whatever it hit, the panel draws it next frame
    const auto hit = fSettingsPanel.click(x, y, pressed, fSettings);
    if (hit == client::SettingsPanel::Hit::kChanged) {
      this->applySettings();
      if (!pressed || !fSettingsPanel.dragging()) {
        fSettings.save();
      }
    }
    return hit != client::SettingsPanel::Hit::kNone;
  }

  void dragSetting(float x) {
    fSettingsPanel.touched();
    if (fSettingsPanel.drag(x, fSettings)) {
      this->applyAudioSettings(); // cheap part only while dragging
    }
  }

  void scrollSettings(float delta) {
    fSettingsPanel.scroll(delta, static_cast<float>(fWin.fScreenH));
  }

  void applyAudioSettings() {
    fAudio.setVolume(this->musicGain());
    fMirrors.setVolume(this->musicGain());
    fJudgements.setGain(this->effectGain());
  }

  [[nodiscard]] float musicGain() const {
    return fSettings.value("master") * fSettings.value("music") *
           audio_client::kMusicHeadroom;
  }
  [[nodiscard]] float effectGain() const {
    return fSettings.value("master") * fSettings.value("effect") *
           audio_client::kEffectHeadroom;
  }

  // Difficulties are ordered by the rating being shown, which means the
  // order changes when that setting does. Safe now that the loaded set is
  // matched to the cached list by name rather than by re-sorting it, but the
  // selection is a position in that list, so it is carried across by name.
  void applyStarOrder() {
    const int chosen = fSettings.choice("stars");
    if (chosen == fAppliedStarChoice) {
      return;
    }
    fAppliedStarChoice = chosen;
    fLibrary.setRanked(chosen == 1);
    fLibrary.sortLibraryByStars();
  }


  void applySettings() {
    this->applyStarOrder();
    this->applyAudioSettings();
    const float dim = fSettings.value("dim");
    if (std::abs(dim - fAppliedDim) > 1e-4f) {
      fAppliedDim = dim;
      fView.preScaleBackground(this->gameplayCtx(nullptr));
    }
    fSwapIntervalRequest.store(fSettings.flag("vsync") ? 1 : 0,
                               std::memory_order_release);
    fFrame.damageAll("settings applied");
    // Sensitivity other than 1 needs relative motion, which needs the pointer
    // grabbed; so does raw input.
    this->applyPointerMode();
  }

  // ---- Mod select and export dialog ---------------------------------------
  //
  // Both views live in client.overlays; this is the bridge to app state.

  [[nodiscard]] std::vector<client::ModEntry> modEntries() const {
    return {
        {"EZ", "Easy", "Larger circles, more forgiving HP drain.",
         osu::mod::kEasy, 0, glfw::kKeyQ, 0.5},
        {"HT", "Half Time", "Less zoom... more time to react.",
         osu::mod::kHalfTime, 0, glfw::kKeyW, 0.3},
        {"HR", "Hard Rock", "Everything just got a bit harder...",
         osu::mod::kHardRock, 1, glfw::kKeyA, 1.06},
        {"DT", "Double Time", "Zoooooooooom...", osu::mod::kDoubleTime, 1,
         glfw::kKeyD, 1.12},
    };
  }

  void toggleMods() { fModSelect.toggle(); }

  void drawModSelect(skia::SkCanvas *canvas) {
    fModSelect.render(canvas);
  }

  bool modClick(float x, float y) {
    return fModSelect.click(x, y, fMods);
  }

  void drawExportDialog(skia::SkCanvas *canvas) {
    fExportDialog.render(canvas);
  }

  bool exportClick(float x, float y) {
    if (!fExportDialog.open()) {
      return false;
    }
    if (fExportDialog.click(x, y)) {
      this->exportReplayVideo();
    }
    return true;
  }

  // Said in both places: the dialog is where it belongs, and the log is where
  // it survives being missed -- which, while the export was blocking the
  // client, it always was.
  void exportFailed(std::string reason) {
    std::println(std::cerr, "[export] failed: {}", reason);
    fExportDialog.setStatus(std::move(reason));
  }

  void exportReplayVideo() {
    if (fVideoExporter.active()) {
      return; // one at a time
    }
    if (!fPlay.fMap || fPlay.fRecordedEvents.empty()) {
      this->exportFailed("nothing to export: no play recorded for this map");
      return;
    }
    const auto preset =
        client::kVideoPresets[static_cast<std::size_t>(fExportDialog.preset())];
    // A size typed into the dialog wins over the one picked from the row.
    const auto [typedWidth, typedHeight] = fExportDialog.customSize();
    client::ReplayVideoExporter::Request request;
    request.fOptions.fWidth = typedWidth > 0 ? typedWidth : preset.fWidth;
    request.fOptions.fHeight = typedHeight > 0 ? typedHeight : preset.fHeight;
    request.fOptions.fFps = 60;

    // Into the working directory, named for what it is: the replay it came
    // from when there is one, the difficulty otherwise, and the size it was
    // rendered at. Two exports of the same play at different sizes are two
    // files rather than one overwriting the other.
    const std::string stem =
        !fReplayPath.empty()
            ? fReplayPath.stem().string()
            : std::filesystem::path(fBeatmapFilename).stem().string();
    std::string safe;
    for (const char c : stem) {
      const bool awkward = static_cast<unsigned char>(c) < 0x20 || c == '/' ||
                           c == '\\' || c == ':';
      safe.push_back(awkward ? '_' : c);
    }
    std::error_code cwdError;
    const auto here = std::filesystem::current_path(cwdError);
    request.fOptions.fOutput =
        (cwdError ? fMapsDir.parent_path() : here) /
        std::format("{}-{}x{}.mp4", safe, request.fOptions.fWidth,
                    request.fOptions.fHeight);

    // Written out before the encoder is started: it is told about its inputs
    // once, when it is launched, and an audio path handed over afterwards
    // reached nobody -- which is why the videos had no sound.
    if (!fPlay.fMap->fMeta.fAudioFilename.empty()) {
      const auto bytes = fSet.findFile(fPlay.fMap->fMeta.fAudioFilename);
      if (!bytes.empty()) {
        std::error_code ec;
        const auto audioPath = std::filesystem::temp_directory_path(ec) /
                               fPlay.fMap->fMeta.fAudioFilename;
        std::ofstream out(audioPath, std::ios::binary);
        out.write(reinterpret_cast<const char *>(bytes.data()),
                  static_cast<std::streamsize>(bytes.size()));
        out.close();
        request.fOptions.fAudio = audioPath;
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
    fSkin.precomputeSliderBodies(*fPlay.fMap, fComboInfo, exportScale,
                                 fContext.get());
    fSkin.flattenBodiesToRaster(fContext.get());

    request.fMap = *fPlay.fMap;
    request.fCombo = fComboInfo;
    request.fEvents = fPlay.fRecordedEvents;
    request.fMods = fMods;
    request.fSkin = &fSkin;
    request.fFont = fFont;
    request.fDisplayFont = fDisplayFont;
    // The cursor is drawn at a size in screen pixels, which is right for a
    // window and wrong for a render: at 4K it came out a quarter of the size
    // it has on screen. Scaled by how much bigger the playfield is, it keeps
    // the size it has relative to the play.
    request.fCursorSize = fSettings.value("cursorsize") *
                          (fScale > 0.0f ? exportScale / fScale : 1.0f);
    request.fDim = fSettings.value("dim");
    request.fNoGlow = fNoGlow;
    request.fHitLighting = fSettings.flag("hitlighting");
    request.fAttributes = fPlay.fPlayAttributes;
    for (const auto &info : fSet.fBeatmaps) {
      if (info.fMeta.fBackground.empty()) {
        continue;
      }
      const auto bytes = fSet.findFile(info.fMeta.fBackground);
      if (!bytes.empty()) {
        request.fBackground = loadImage(bytes);
        break;
      }
    }

    const std::string error = fVideoExporter.start(std::move(request));
    if (!error.empty()) {
      this->exportFailed(error);
      return;
    }
    fExportDialog.setStatus("rendering 0%");
  }

  // Nothing to step any more: the render is on its own thread. This is the
  // client noticing how far it has got and what it had to say when it stopped.
  void pollExportVideo() {
    if (!fVideoExporter.active()) {
      return;
    }
    const auto status = fVideoExporter.status();
    if (!status.fFinished) {
      fExportDialog.setStatus(std::format(
          "rendering {}%   {}x{}", status.fPercent, status.fWidth,
          status.fHeight));
      return;
    }
    if (status.fOk) {
      fExportDialog.setStatus(std::format(
          "saved {}",
          std::filesystem::path(status.fMessage).filename().string()));
      std::println(std::cerr, "[export] saved {}", status.fMessage);
    } else {
      this->exportFailed(status.fMessage);
    }
    fVideoExporter.clearFinished();
  }

  // ---- Replay browser ------------------------------------------------------
  //
  // lazer surfaces past plays through the leaderboard beside song select and
  // replays them with the standard playback path. Here the saved .osr files
  // are listed in a panel; picking one starts the map with that replay.

  void toggleReplayList() {
    const std::string wanted =
        fState == State::kResults || fState == State::kPlaying
            ? this->beatmapMd5()
            : this->difficultyMd5(fLibrary.selSet(), fLibrary.selDiff());
    fReplayBrowser.toggle(wanted, fReplayPath);
  }

  // The browser lists the selected difficulty's replays, so a selection made
  // while it is open has to rebuild the list.
  void refreshReplayFilter() {
    fReplayBrowser.refreshFilter(
        this->difficultyMd5(fLibrary.selSet(), fLibrary.selDiff()),
        fReplayPath);
  }

  // md5 of a difficulty in the library, which is what an .osr records. It is
  // computed when the archive is parsed and kept in the metadata cache, so
  // this costs nothing and never has to open the archive.
  [[nodiscard]] std::string difficultyMd5(int setIdx, int diffIdx) const {
    const auto &infos = fLibrary.infosFor(setIdx);
    if (diffIdx < 0 || diffIdx >= static_cast<int>(infos.size())) {
      return {};
    }
    return infos[static_cast<std::size_t>(diffIdx)].fMd5;
  }

  void drawReplayList(skia::SkCanvas *canvas) {
    fReplayBrowser.renderOverlay(canvas, this->panelCtx(false));
  }

  // Renders a saved replay to video: the exporter draws whatever gameplay
  // state is loaded, so the map and the replay's events are brought in
  // exactly as starting a playback would, without entering gameplay.
  void exportSelectedReplay() {
    const auto replay = fReplayBrowser.selectedPath();
    if (!replay) {
      return;
    }
    auto set = fLibrary.setForBlocking(fLibrary.selSet());
    if (!set || fLibrary.selDiff() < 0 ||
        fLibrary.selDiff() >= static_cast<int>(set->fBeatmaps.size())) {
      return;
    }
    fSet = *set;
    fPlayingSet = fLibrary.selSet();
    fPlayingDiff = fLibrary.selDiff();
    fReplayPath = *replay;
    fAutoplay = true;
    this->resetGameplayState();
    this->startGameplay(
        fSet.fBeatmaps[static_cast<std::size_t>(fLibrary.selDiff())]);
    fAudio.stop();
    fMenuMusicForSet = -1; // the menu loop restarts once the export is done
    fReplayPath.clear();
    fAutoplay = fCliAutoplay;
    fReplayBrowser.close();
    fExportDialog.show();
  }

  // The beatmap every panel in the strip belongs to: the one just played on
  // the results screen, the selected one in the browser.
  [[nodiscard]] client::results::Ctx panelCtx(bool ownScore) {
    client::results::Ctx ctx;
    ctx.fFont = &fFont;
    ctx.fWidth = static_cast<float>(fWin.fScreenW);
    ctx.fHeight = static_cast<float>(fWin.fScreenH);
    ctx.fMouseX = fWin.fMouseX;
    ctx.fMouseY = fWin.fMouseY;
    ctx.fNowWall = wallMs();
    ctx.fEnterWall = fStateEnterWall;
    ctx.fDtMs = fUiDt;
    ctx.fOwnScore = ownScore;
    ctx.fPp = fResult.fPp;
    ctx.fMean = fResult.fMean;
    ctx.fUr = fResult.fUr;

    const bool results = fState == State::kResults;
    const int setIdx = results ? fPlayingSet : fLibrary.selSet();
    const int diffIdx = results ? fPlayingDiff : fLibrary.selDiff();
    if (results && fPlay.fMap) {
      const auto &m = fPlay.fMap->fMeta;
      ctx.fTitle = m.fTitleUnicode.empty() ? m.fTitle : m.fTitleUnicode;
      ctx.fArtist = m.fArtistUnicode.empty() ? m.fArtist : m.fArtistUnicode;
    } else if (setIdx >= 0) {
      const auto &infos = fLibrary.infosFor(setIdx);
      if (diffIdx >= 0 && diffIdx < static_cast<int>(infos.size())) {
        const auto &m = infos[static_cast<std::size_t>(diffIdx)].fMeta;
        ctx.fTitle = m.fTitleUnicode.empty() ? m.fTitle : m.fTitleUnicode;
        ctx.fArtist = m.fArtistUnicode.empty() ? m.fArtist : m.fArtistUnicode;
      }
    }
    if (setIdx >= 0) {
      const auto &infos = fLibrary.infosFor(setIdx);
      if (diffIdx >= 0 && diffIdx < static_cast<int>(infos.size())) {
        const auto &info = infos[static_cast<std::size_t>(diffIdx)];
        ctx.fVersion = info.fMeta.fVersion;
        ctx.fStars = fLibrary.shownStars(info);
        ctx.fHasDifficulty = true;
      }
    }
    return ctx;
  }

  // The strip took the press; what follows from it is the client's.
  bool panelListClick(float x, float y, bool pressed) {
    const auto result = fReplayBrowser.clickPanels(x, y, pressed);
    if (result.fWatch) {
      this->watchReplay(*result.fWatch);
    }
    return result.fTaken;
  }

  // The strip is live on the results screen and in the browser overlay.
  [[nodiscard]] bool panelListActive() const {
    return fReplayBrowser.open() || fState == State::kResults;
  }

  void watchReplay(const std::filesystem::path &path) {
    // On the results screen the strip belongs to the map just played, which is
    // not necessarily the one selected in the carousel.
    const bool results = fState == State::kResults;
    const int setIdx = results ? fPlayingSet : fLibrary.selSet();
    const int diffIdx = results ? fPlayingDiff : fLibrary.selDiff();
    if (setIdx < 0) {
      return;
    }
    fReplayBrowser.close();
    fPendingReplay = path; // startPlay picks it up and drives the engine
    this->startPlay(setIdx, diffIdx);
  }

  // ---- Song select ------------------------------------------------------
  //
  // Song select is a collection of scene trees: they decide where their nodes
  // are, route their input and report what has to be repainted.

  void updateSongSelect() {
#ifdef __EMSCRIPTEN__
    if (!fLibraryLoaded) {
      if (detail::gMapsSynced.load(std::memory_order_acquire)) {
        fLibraryRuntime.initLibrary();
      } else {
        fFrame.damageAll("waiting on local storage");
        return;
      }
    }
#endif
    this->refreshReplayFilter();
    fLibrary.rebuildVisible(client::parseQuery(fFilter.text()));

    this->ensureBackgroundForSelection();

    const float sw = static_cast<float>(fWin.fScreenW);
    const float sh = static_cast<float>(fWin.fScreenH);

    if (fLibrary.visible().empty() != fDrawnEmpty) {
      fDrawnEmpty = fLibrary.visible().empty();
      fFrame.damageAll("song select has nothing to list");
    }

    // The carousel retains this projection and rebuilds it only when the
    // filtered library or expanded set changes.
    if (!fLibrary.visible().empty()) {
      const auto &infos = fLibrary.infosFor(fLibrary.selSet());
      fLibrary.selDiff() =
          std::clamp(fLibrary.selDiff(), 0,
                     std::max(0, static_cast<int>(infos.size()) - 1));
    }
    fCarousel.setRows(
        fLibrary.visibleRevision(), fLibrary.visible(), fLibrary.selSet(),
        [this](int set) { return fLibrary.infosFor(set).size(); });

    client::carousel::Carousel::Ctx ctx;
    ctx.fWidth = sw;
    ctx.fHeight = sh;
    ctx.fTop = client::FilterControl::kHeight + 8.0f;
    ctx.fBottom = sh - 62.0f;
    ctx.fMouseX = fWin.fMouseX;
    ctx.fMouseY = fWin.fMouseY;
    ctx.fNowMs = wallMs();
    ctx.fDtMs = fUiDt;
    ctx.fSelectedSet = fLibrary.selSet();
    ctx.fSelectedDiff = fLibrary.selDiff();
    fCarousel.update(ctx);
    fFrame.consume(fCarousel.finishFrame());

    client::FilterControl::Ctx filterCtx;
    filterCtx.fFont = &fFont;
    filterCtx.fWidth = sw;
    filterCtx.fHeight = sh;
    filterCtx.fMouseX = fWin.fMouseX;
    filterCtx.fMouseY = fWin.fMouseY;
    filterCtx.fVisibleCount = fLibrary.visible().size();
    filterCtx.fNowMs = wallMs();
    fFilter.update(filterCtx);
    fFrame.consume(fFilter.finishFrame());
    if (!fFilter.text().empty()) {
      fFrame.wakeAt(nextCaretFlip(wallMs()));
    }

    fSelectFooter.update({.fFont = &fFont,
                          .fWidth = sw,
                          .fHeight = sh,
                          .fMouseX = fWin.fMouseX,
                          .fMouseY = fWin.fMouseY,
                          .fNowMs = wallMs()});
    fFrame.consume(fSelectFooter.finishFrame());

    if (!fLibrary.visible().empty()) {
      const auto &infos = fLibrary.infosFor(fLibrary.selSet());
      if (!infos.empty()) {
        fInfoWedge.update(
            {.fFont = &fFont,
             .fWidth = sw,
             .fHeight = sh,
             .fSet = fLibrary.selSet(),
             .fDifficulty = fLibrary.selDiff(),
             .fRankedStars = fLibrary.ranked(),
             .fInfos = std::span<const osu::BeatmapInfo>(infos)});
        fFrame.consume(fInfoWedge.finishFrame());
      }
    }
  }

  void frameSongSelect() {
    auto *canvas = fFrame.fSurface->getCanvas();
#ifdef __EMSCRIPTEN__
    if (!fLibraryLoaded) {
      canvas->clear(skia::colorSetARGB(255, 18, 14, 24));
      this->drawTextCentered(canvas, "Syncing local storage...",
                             static_cast<float>(fWin.fScreenW) * 0.5f,
                             static_cast<float>(fWin.fScreenH) * 0.5f, 24.0f,
                             skia::kWhite, 0.8f);
      this->present();
      return;
    }
#endif
    this->drawScreenBackground(canvas);

    const float sw = static_cast<float>(fWin.fScreenW);
    const float sh = static_cast<float>(fWin.fScreenH);

    if (fLibrary.visible().empty()) {
      const bool filtered = !fFilter.text().empty();
      this->drawTextCentered(
          canvas, filtered ? "No maps match the filter" : "No beatmaps yet",
          sw * 0.5f, sh * 0.45f, 28.0f, skia::kWhite, 0.9f);
      this->drawTextCentered(
          canvas,
          filtered ? "Backspace to edit, Esc to clear"
                   : "Drag a .osz onto the window, or press F1 to browse",
          sw * 0.5f, sh * 0.45f + 40.0f, 18.0f, kAccent);
      this->drawFilterControl(canvas);
      fSelectFooter.render(canvas);
      this->drawScreenFadeIn(canvas);
      this->present();
      return;
    }

    // ---- Left: the info wedge (lazer's BeatmapTitleWedge area).
    fInfoWedge.render(canvas);

    // ---- Right: the carousel, which masks itself to its own viewport.
    fCarousel.render(canvas);

    this->drawFilterControl(canvas);
    fSelectFooter.render(canvas);
    this->drawScreenFadeIn(canvas);
    this->present();
  }

  // ---- FilterControl ------------------------------------------------------
  //
  // The widget lives in client.filtercontrol; the app supplies the library
  // view it filters and reacts when the criteria change.

  void drawFilterControl(skia::SkCanvas *canvas) {
    fFilter.render(canvas);
  }

  bool filterClick(float x, float y, bool pressed) {
    if (!pressed) {
      fFilter.endDrag();
      return false;
    }
    const bool used = fFilter.click(x, y, pressed);
    if (fFilter.takeDirty()) {
      this->onFilterChanged();
    }
    return used;
  }

  void dragFilterRange(float x) {
    fFilter.dragRange(x);
    if (fFilter.takeDirty()) {
      fLibrary.markDirty();
    }
  }

  void cycleSortMode() {
    fFilter.cycleSort();
    this->onFilterChanged();
  }

  // Sorting and the visible set both depend on the criteria.
  // Which ordering the widget is offering, in the library's own terms.
  [[nodiscard]] client::library::Sort sortChoice() const {
    switch (fFilter.sortMode()) {
    case client::FilterControl::SortMode::kAuthor:
      return client::library::Sort::kAuthor;
    case client::FilterControl::SortMode::kArtist:
      return client::library::Sort::kArtist;
    case client::FilterControl::SortMode::kDifficulty:
      return client::library::Sort::kDifficulty;
    case client::FilterControl::SortMode::kLength:
      return client::library::Sort::kLength;
    case client::FilterControl::SortMode::kTitle:
      break;
    }
    return client::library::Sort::kTitle;
  }

  // The ordering and the difficulty range live on the widget; the library is
  // told them before it is asked to sort by them.
  void resortLibrary() {
    fLibrary.setSort(this->sortChoice());
    fLibrary.setRange(fFilter.rangeMin(), fFilter.rangeMax());
    fLibrary.sortLibrary();
  }

  void onFilterChanged() {
    this->resortLibrary();
    fLibrary.markDirty();
    fLibrary.rebuildVisible(client::parseQuery(fFilter.text()));
  }

  // Set panels carry the beatmap's cover art behind the text, as lazer's
  // PanelSetBackground does. The image is pulled from the archive the first
  // time the panel is drawn and kept with the entry.
  void drawSetPanel(skia::SkCanvas *canvas, const skia::SkRect &rect,
                    int setIndex, const std::vector<osu::BeatmapInfo> &infos,
                    bool expanded, bool hover, float corner) {
    this->fillRounded(canvas, rect, corner,
                      expanded ? skia::colorSetARGB(255, 66, 48, 74)
                      : hover  ? skia::colorSetARGB(255, 52, 42, 60)
                               : skia::colorSetARGB(255, 40, 33, 48));

    if (auto art = fLibrary.panelArt(setIndex)) {
      canvas->save();
      canvas->clipRRect(skia::SkRRect::MakeRectXY(rect, corner, corner), true);
      // Cover the panel preserving aspect (FillMode.Fill), cropping overflow.
      const float iw = static_cast<float>(art->width());
      const float ih = static_cast<float>(art->height());
      if (iw > 0.0f && ih > 0.0f) {
        const float scale = std::max(rect.width() / iw, rect.height() / ih);
        const float dw = iw * scale;
        const float dh = ih * scale;
        canvas->drawImageRect(
            art.get(),
            skia::SkRect::MakeXYWH(rect.centerX() - dw * 0.5f,
                                   rect.centerY() - dh * 0.5f, dw, dh),
            skia::SkSamplingOptions(skia::SkFilterMode::kLinear), nullptr);
      }
      // PanelSetBackground's darkening: a sheared three-step black gradient,
      // 0.5 alpha over the left 40%, easing to 0.2 by the right edge.
      this->drawPanelVeil(canvas, rect);
      canvas->restore();
    }
    // The hover tint above is the panel's own colour, which artwork covers
    // completely -- so on any set with a cover, hovering changed nothing that
    // could be seen while still costing the repaint. This sits over the
    // artwork instead, so the highlight exists on every panel or on none.
    if (hover && !expanded) {
      this->fillRounded(canvas, rect, corner,
                        skia::colorSetARGB(28, 255, 255, 255));
    }
    if (expanded) {
      this->strokeRounded(canvas, rect, corner, kAccent, 2.0f);
    }

    const float pad = 18.0f;
    const std::string title = infos.empty() ? "(empty)"
                              : infos.front().fMeta.fTitleUnicode.empty()
                                  ? infos.front().fMeta.fTitle
                                  : infos.front().fMeta.fTitleUnicode;
    const std::string artist = infos.empty() ? std::string{}
                               : infos.front().fMeta.fArtistUnicode.empty()
                                   ? infos.front().fMeta.fArtist
                                   : infos.front().fMeta.fArtistUnicode;
    this->drawTextClipped(canvas, title, rect.fLeft + pad,
                          rect.fTop + rect.height() * 0.44f,
                          rect.width() - pad * 2 - 90.0f, 19.0f, skia::kWhite);
    this->drawTextClipped(
        canvas, artist, rect.fLeft + pad, rect.fTop + rect.height() * 0.72f,
        rect.width() - pad * 2 - 90.0f, 14.0f, skia::kWhite, 0.75f);

    // Difficulty spread dots (PanelBeatmapSet.SpreadDisplay).
    float dotX = rect.fRight - pad;
    for (auto it = infos.rbegin(); it != infos.rend(); ++it) {
      skia::SkPaint dot;
      dot.setAntiAlias(true);
      dot.setColor(starColor(fLibrary.shownStars(*it)));
      canvas->drawCircle(dotX, rect.centerY(), 4.0f, dot);
      dotX -= 12.0f;
      if (dotX < rect.fRight - 120.0f) {
        break;
      }
    }
  }

  // PanelSetBackground: a FillFlowContainer of three boxes sheared by 0.8 on
  // X, giving a ~40-degree diagonal fade -- solid 50% black over the first
  // 40% of the width, then 50%->30%, then 30%->20%.
  void drawPanelVeil(skia::SkCanvas *canvas, const skia::SkRect &rect) {
    const float w = rect.width();
    const float h = rect.height();
    const float shear = 0.8f * h; // horizontal displacement over the height
    struct Step {
      float fFrom, fTo; // fractions of width
      float fAlphaL, fAlphaR;
    };
    const Step steps[] = {
        {0.0f, 0.40f, 0.5f, 0.5f},
        {0.40f, 0.60f, 0.5f, 0.3f},
        {0.60f, 1.05f, 0.3f, 0.2f},
    };
    for (const auto &st : steps) {
      const float x0 = rect.fLeft + st.fFrom * w;
      const float x1 = rect.fLeft + st.fTo * w;
      skia::SkPathBuilder quad;
      quad.moveTo(x0 + shear * 0.5f, rect.fTop);
      quad.lineTo(x1 + shear * 0.5f, rect.fTop);
      quad.lineTo(x1 - shear * 0.5f, rect.fBottom);
      quad.lineTo(x0 - shear * 0.5f, rect.fBottom);
      quad.close();
      skia::SkPaint p;
      p.setAntiAlias(true);
      // Approximate the per-box horizontal gradient with its mean alpha; the
      // steps are narrow enough that the banding is not visible.
      const float alpha = (st.fAlphaL + st.fAlphaR) * 0.5f;
      p.setColor(skia::colorSetARGB(static_cast<std::uint8_t>(alpha * 255.0f),
                                    0, 0, 0));
      canvas->drawPath(quad.detach(), p);
    }
  }


  void drawDiffPanel(skia::SkCanvas *canvas, const skia::SkRect &rect,
                     const osu::BeatmapInfo &info, bool selected, bool hover,
                     float corner) {
    this->fillRounded(canvas, rect, corner,
                      selected ? skia::colorSetARGB(255, 74, 56, 84)
                      : hover  ? skia::colorSetARGB(255, 48, 39, 56)
                               : skia::colorSetARGB(255, 34, 28, 42));
    if (selected) {
      this->strokeRounded(canvas, rect, corner, kAccent2, 2.0f);
    }
    const float pad = 16.0f;
    const skia::SkRect badge = skia::SkRect::MakeXYWH(
        rect.fLeft + pad, rect.centerY() - 11.0f, 62.0f, 22.0f);
    this->fillRounded(canvas, badge, 11.0f,
                      starColor(fLibrary.shownStars(info)));
    this->drawTextCentered(canvas,
                           std::format("{:.2f}", fLibrary.shownStars(info)),
                           badge.centerX(), badge.centerY() + 5.0f, 13.0f,
                           skia::colorSetARGB(255, 20, 16, 26));
    this->drawTextClipped(canvas, info.fMeta.fVersion, badge.fRight + 14.0f,
                          rect.centerY() + 5.0f,
                          rect.width() - badge.width() - pad * 3, 15.0f,
                          skia::kWhite, 0.95f);
  }

  bool selectFooterClick(float x, float y) {
    using Action = client::songselect::Action;
    switch (fSelectFooter.click(x, y)) {
    case Action::kBack:
      this->switchState(State::kMainMenu);
      return true;
    case Action::kMods:
      this->toggleMods();
      return true;
    case Action::kRandom:
      this->selectRandom();
      return true;
    case Action::kImport:
      fLibraryRuntime.importOsz();
      return true;
    case Action::kBrowse:
      fInput.openDownloads();
      return true;
    case Action::kReplays:
      this->toggleReplayList();
      return true;
    case Action::kDelete:
      this->askDeleteBeatmap();
      return true;
    case Action::kSettings:
      this->toggleSettings();
      return true;
    case Action::kTaken:
      return true;
    case Action::kNone:
      break;
    }
    return false;
  }

  // lazer never deletes a beatmap without asking, and neither does this.
  void askDeleteBeatmap() {
    if (fLibrary.selSet() < 0 ||
        fLibrary.selSet() >= static_cast<int>(fLibrary.sets().size())) {
      return;
    }
    const auto &infos = fLibrary.infosFor(fLibrary.selSet());
    if (infos.empty()) {
      return;
    }
    const auto &meta = infos.front().fMeta;
    const std::string title = std::format(
        "{} - {}",
        meta.fArtistUnicode.empty() ? meta.fArtist : meta.fArtistUnicode,
        meta.fTitleUnicode.empty() ? meta.fTitle : meta.fTitleUnicode);
    fDeleteDialog.show(title, infos.size());
  }

  bool confirmDeleteClick(float x, float y) {
    if (!fDeleteDialog.open()) {
      return false;
    }
    if (fDeleteDialog.click(x, y) ==
        client::DeleteDialog::Choice::kDelete) {
      this->deleteSelectedBeatmap();
    }
    return true; // the dialog is modal either way
  }

  // Removes the archive and everything the client remembers about it.
  void deleteSelectedBeatmap() {
    if (fLibrary.selSet() < 0 ||
        fLibrary.selSet() >= static_cast<int>(fLibrary.sets().size())) {
      return;
    }
    const auto index = static_cast<std::size_t>(fLibrary.selSet());
    // The track playing under the menu belongs to this set; let it go before
    // the file does.
    fLibraryRuntime.stopMenuMusic();
    fMenuMusicForSet = -1;
    fBackgroundForSet = -1;

    const std::string name = fLibrary.deleteSet(index);
    this->resortLibrary();
    fLibrary.rebuildVisible(client::parseQuery(fFilter.text()));
    fPlayingSet = -1;
    fPlayingDiff = -1;
    this->notify(name.empty() ? "beatmap deleted"
                              : std::format("deleted {}", name));
  }

  void drawBottomBar(skia::SkCanvas *canvas, const std::string &hint) {
    const float sw = static_cast<float>(fWin.fScreenW);
    const float sh = static_cast<float>(fWin.fScreenH);
    this->fillRounded(canvas,
                      skia::SkRect::MakeXYWH(0.0f, sh - 44.0f, sw, 44.0f), 0.0f,
                      kPanelBg);
    this->drawTextCentered(canvas, hint, sw * 0.5f, sh - 16.0f, 15.0f,
                           skia::kWhite, 0.75f);
  }

  // ---- Download screen ---------------------------------------------------
  //
  // The whole listing -- header, filters, sort bar and cards -- is drawn by
  // client.listing; here it only gets the data and the transfer state.

  // Everything about this screen that is not drawing. It runs before the
  // client has committed to a frame, so what it marks as damaged is the
  // answer to "is this frame worth drawing".
  void updateDownload() {
    fMirrors.pollProgress();
    // A card also draws its own state -- idle, fetching, done, failed -- out
    // of the entry, and that is written from a dozen places: a transfer
    // starting, one finishing, an import marking everything already owned.
    // Comparing it here catches all of them, including the ones written after
    // this was, which a call at each site would not.
    fEntryStates.resize(fMirrors.results().size(), 0xFF);
    for (std::size_t i = 0; i < fMirrors.results().size(); ++i) {
      const auto state = static_cast<std::uint8_t>(fMirrors.results()[i].fSt);
      if (fEntryStates[i] != state) {
        fEntryStates[i] = state;
        fListing.entryChanged(static_cast<int>(i));
      }
    }
    client::listing::Listing::Ctx ctx;
    ctx.fFont = &fFont;
    ctx.fWidth = static_cast<float>(fWin.fScreenW);
    ctx.fHeight = static_cast<float>(fWin.fScreenH);
    ctx.fMouseX = fWin.fMouseX;
    ctx.fMouseY = fWin.fMouseY;
    ctx.fNowMs = wallMs();
    ctx.fDtMs = fUiDt;
    ctx.fEntries = fMirrors.results();
    ctx.fLoading = fMirrors.searching();
    fMirrors.pollPreview();
    fListing.setPreview(fMirrors.previewId(), fMirrors.previewProgress());
    fListing.update(ctx);
    fFrame.consume(fListing.finishFrame());
    if (fSetPage.open()) {
      const std::size_t idx = fMirrors.indexOfSet(fSetPage.setId());
      if (idx >= fMirrors.results().size()) {
        fSetPage.close(); // the set fell out of the results
        fFrame.damageAll("beatmap page closed");
      }
      client::setpage::SetPage::Ctx page;
      page.fEntry =
          idx < fMirrors.results().size() ? &fMirrors.results()[idx] : nullptr;
      page.fFont = &fFont;
      page.fWidth = ctx.fWidth;
      page.fHeight = ctx.fHeight;
      page.fMouseX = fWin.fMouseX;
      page.fMouseY = fWin.fMouseY;
      page.fNowMs = wallMs();
      page.fPreviewPlaying = fMirrors.previewId() == fSetPage.setId();
      page.fPreviewProgress = fMirrors.previewProgress();
      fSetPage.update(page);
      fFrame.consume(fSetPage.finishFrame());
    }
    // Covers are only fetched for what is on screen, which the listing knows
    // and the client did not: this used to walk every result that passed the
    // filters, on screen or four hundred cards below it.
    for (const int idx : fListing.onScreen()) {
      fMirrors.requestThumb(static_cast<std::size_t>(idx));
    }
    // Scrolling near the end pages the next batch in, as the overlay's
    // scroll container asks for the next cursor.
    if (fListing.wantsMore()) {
      fMirrors.fetchPage();
    }
    // The caret blinks on a clock of its own. Rather than keeping frames
    // coming so the moment is not missed, the screen says when the moment is
    // -- and says nothing when there is no such moment: no caret on screen,
    // or the beatmap page covering the listing it belongs to.
    if (!fSetPage.open()) {
      const double wake = fListing.nextChangeWall(wallMs());
      if (wake > 0.0) {
        fFrame.wakeAt(wake);
      }
    }
  }

  void frameDownload() {
    auto *canvas = fFrame.fSurface->getCanvas();
    fListing.render(canvas);
    fSetPage.render(canvas);
    this->drawScreenFadeIn(canvas);
    this->present();
  }

  // ---- Pause ------------------------------------------------------------
  //
  // client.pause is lazer's GameplayMenuOverlay; here it only gets the play's
  // numbers and says what was clicked.

  void updatePause() {
    client::pause::PauseMenu::Ctx ctx;
    ctx.fFont = &fFont;
    ctx.fWidth = static_cast<float>(fWin.fScreenW);
    ctx.fHeight = static_cast<float>(fWin.fScreenH);
    ctx.fMouseX = fWin.fMouseX;
    ctx.fMouseY = fWin.fMouseY;
    ctx.fNowMs = wallMs();
    ctx.fDtMs = fUiDt;
    ctx.fAnimateTriangles = fSettings.flag("pausetriangles");
    ctx.fRetries = fPlay.fRetryCount;
    ctx.fProgress = this->playProgress();
    ctx.fAccuracy = fPlay.fEngine
                        ? static_cast<float>(fPlay.fEngine->score().accuracy())
                        : 1.0f;
    fPauseMenu.update(ctx);
    fFrame.consume(fPauseMenu.finishFrame());
  }

  // How far into the playable part of the map the pause happened, which is
  // what GameplayMenuOverlay puts under the buttons.
  [[nodiscard]] float playProgress() const {
    if (!fPlay.fMap || fPlay.fMap->fObjects.empty()) {
      return 0.0f;
    }
    const double first = osu::startTime(fPlay.fMap->fObjects.front());
    const double last = osu::startTime(fPlay.fMap->fObjects.back());
    if (last <= first) {
      return 0.0f;
    }
    return static_cast<float>(
        std::clamp((fPlay.fPausedNow - first) / (last - first), 0.0, 1.0));
  }

  void framePaused() {
    // The frozen game underneath does not change while it is paused; what
    // moves is the overlay, and the frame is clipped to what the overlay
    // said. The scene is still redrawn, because a clipped repaint has to put
    // back whatever was under the piece being repainted.
    fView.invalidate();
    fView.render(this->gameplayCtx(fFrame.fSurface->getCanvas()),
                 fPlay.fPausedNow);
    fPauseMenu.render(fFrame.fSurface->getCanvas());
    this->present();
  }

  // ---- Results ----------------------------------------------------------

  void updateResults() {
    fReplayBrowser.updateResultActions(
        {.fFont = &fFont,
         .fWidth = static_cast<float>(fWin.fScreenW),
         .fHeight = static_cast<float>(fWin.fScreenH),
         .fMouseX = fWin.fMouseX,
         .fMouseY = fWin.fMouseY,
         .fNowMs = wallMs()});
    fFrame.consume(fReplayBrowser.finishResultFrame());
    const auto motion =
        fReplayBrowser.updateMotion(fWin.fMouseX, fWin.fMouseY);
    if (motion.fFullDamage) {
      fFrame.damageAll("results strip moving");
    } else if (motion.fDamage) {
      fFrame.damage(*motion.fDamage);
    }
  }

  void frameResults() {
    fView.invalidate();
    auto *canvas = fFrame.fSurface->getCanvas();
    this->drawScreenBackground(canvas);
    const skiff::paint::Painter p(canvas, fFont);

    const float sw = static_cast<float>(fWin.fScreenW);
    const float sh = static_cast<float>(fWin.fScreenH);
    p.fillRect(skia::SkRect::MakeXYWH(0, 0, sw, sh),
               skia::colorSetARGB(160, 10, 8, 14));

    std::optional<client::ReplayBrowser::OwnScore> own;
    if (fReplayPath.empty()) {
      own = client::ReplayBrowser::OwnScore{.fScore = fResult.fScore,
                                            .fGrade = fResult.fGrade,
                                            .fPp = fResult.fPp,
                                            .fMean = fResult.fMean,
                                            .fUr = fResult.fUr};
    }
    fReplayBrowser.renderResults(canvas, this->panelCtx(bool(own)),
                                 std::move(own), fPlay.fLastSavedReplay);

    this->drawScreenFadeIn(canvas);
    this->present();
  }

  bool initSkia() {
    auto interface = skia::GrGLMakeNativeInterface();
    if (!interface) {
      return false;
    }
    fContext = skia::MakeGL(std::move(interface));
    skiff::nodes::CachedContainer::setContext(fContext.get());
    // The carousel owns where a panel is and when it has to be repainted; the
    // client still owns what one looks like, and hands it over here.
    // The artwork behind the menu is the client's: it owns the beatmap and
    // the view that scales it. The screen draws everything else itself.
    fMainMenu.setArtworkPainter([this](skia::SkCanvas *canvas) {
      fView.drawBackground(this->gameplayCtx(canvas), canvas);
    });
    fCarousel.setPainter([this](skia::SkCanvas *canvas,
                                const skia::SkRect &rect,
                                const client::carousel::Row &row, bool selected,
                                bool hovered, float corner) {
      const auto &infos = fLibrary.infosFor(row.fSet);
      if (row.fDiff < 0) {
        this->drawSetPanel(canvas, rect, row.fSet, infos, selected, hovered,
                           corner);
      } else if (row.fDiff < static_cast<int>(infos.size())) {
        this->drawDiffPanel(canvas, rect,
                            infos[static_cast<std::size_t>(row.fDiff)],
                            selected, hovered, corner);
      }
    });
    return static_cast<bool>(fContext);
  }


  // Playfield placement for the current screen size. Split out so the video
  // exporter can re-derive it for its offscreen resolution.
  void layoutForScreen() {
    // Match webosu-2: playfield occupies 80% of the limiting screen dimension.
    constexpr float kPlayfieldSize = 0.8f;
    const float sx = static_cast<float>(fWin.fScreenW) /
                     static_cast<float>(osu::kPlayfieldWidth);
    const float sy = static_cast<float>(fWin.fScreenH) /
                     static_cast<float>(osu::kPlayfieldHeight);
    fScale = kPlayfieldSize * std::min(sx, sy);
    fOffsetX =
        (fWin.fScreenW - static_cast<float>(osu::kPlayfieldWidth) * fScale) *
        0.5f;
    fOffsetY =
        (fWin.fScreenH - static_cast<float>(osu::kPlayfieldHeight) * fScale) *
        0.5f;
  }

  // Skia's CPU rasteriser, as an alternative target for the menus, made on
  // the frame that first asks for it. On a software GL stack the driver's own
  // rasteriser is not obviously better than Skia's, and which one wins is a
  // question for measurement rather than for argument -- hence the setting.
  //
  // Gameplay is never drawn this way, and that is deliberate: slider bodies
  // are precomputed once into textures through SkSL, which llvmpipe's JIT
  // handles better than a CPU rasteriser would, and drawing those textures
  // into a raster canvas would mean reading them back every frame.
  [[nodiscard]] bool ensureRasterSurface() {
    if (fFrame.fRasterSurface &&
        fFrame.fRasterSurface->width() == fWin.fScreenW &&
        fFrame.fRasterSurface->height() == fWin.fScreenH) {
      return true;
    }
    // Same colour space as the window, or the pixels get encoded twice on the
    // way over and the whole frame comes out lighter.
    fFrame.fRasterSurface = skia::Raster(skia::SkImageInfo::Make(
        fWin.fScreenW, fWin.fScreenH, skia::kRGBA_8888_SkColorType,
        skia::kPremul_SkAlphaType,
        fFrame.fWindowSurface
            ? fFrame.fWindowSurface->imageInfo().refColorSpace()
            : nullptr));
    return static_cast<bool>(fFrame.fRasterSurface);
  }

  void resize(int w, int h) {
    // Called on the render thread with framebuffer dimensions delivered by
    // the resize event (or the pre-thread snapshot); querying GLFW here is
    // not allowed off the main thread.
    fWin.fScreenW = w;
    fWin.fScreenH = h;
    this->layoutForScreen();

    skia::GrGLFramebufferInfo info;
    info.fFBOID = 0;
    info.fFormat = skia::kGlRgba8;
    skia::GrBackendRenderTarget target =
        skia::MakeGL(fWin.fScreenW, fWin.fScreenH, 0, 0, info);
    fFrame.fWindowSurface = skia::WrapBackendRenderTarget(
        fContext.get(), target, skia::kBottomLeft_GrSurfaceOrigin,
        skia::kRGBA_8888_SkColorType, nullptr, nullptr);
    fFrame.fSurface = fFrame.fWindowSurface;
    // Dropped rather than remade: eight megabytes for a screen nobody may be
    // drawing on that way. The frame that needs it makes it.
    fFrame.fRasterSurface.reset();
    fFrame.fBlitHistory.clear();
    fLastResizeWall = wallMs();
    if (fSliderBodyScale > 0.0f &&
        std::abs(fScale - fSliderBodyScale) > fSliderBodyScale * 0.01f) {
      fSliderBodiesStale = true;
    }
    fFrame.damageAll("resize", /*buffersGone=*/true);
    fView.invalidate();
    fView.preScaleBackground(this->gameplayCtx(nullptr));
  }

  void toggleFullscreen() {
    if (fWindow == nullptr)
      return;
#ifndef __EMSCRIPTEN__
    fWin.fFullscreen = !fWin.fFullscreen;
    if (fWin.fFullscreen) {
      const auto monitor = glfw::glfwGetPrimaryMonitor();
      const glfw::GLFWvidmode *mode = glfw::glfwGetVideoMode(monitor);
      glfw::glfwSetWindowMonitor(fWindow, monitor, 0, 0, mode->width,
                                 mode->height, mode->refreshRate);
    } else {
      glfw::glfwSetWindowMonitor(fWindow, nullptr, fWin.fWindowedX,
                                 fWin.fWindowedY, fWin.fWindowedW,
                                 fWin.fWindowedH, 0);
    }
#endif
  }

  void shutdown() {
    fAudio.stop();
    fFrame.fSurface.reset();
    fContext.reset();
    fWindowRuntime.close();
    fWindow = nullptr;
  }

  [[nodiscard]] double nowMs() {
#ifdef __EMSCRIPTEN__
    return glfw::glfwGetTime() * 1000.0 - fPlay.fStartMs;
#else
    // The audio device is consulted at most every kClockSyncIntervalMs;
    // between syncs the game clock extrapolates from the wall clock. This
    // removes blocking OpenAL round-trips (mixer mutex, PipeWire latency
    // probes) from the hot path: they used to cost whole milliseconds per
    // call, several calls per frame. It also makes the timeline seamless
    // when the music ends: extrapolation simply continues from the last
    // anchor instead of jumping to an unrelated wall-clock epoch.
    const double wall = wallMs();
    if (wall - fLastClockSyncWall >= kClockSyncIntervalMs) {
      fLastClockSyncWall = wall;
      if (fAudio.playing()) {
        fPlay.fClock.sync(wall, fAudio.positionSec() * 1000.0);
      }
    }
    return fPlay.fClock.sample(wall);
#endif
  }

  [[nodiscard]] bool shouldStop(double now) const {
    return fPlay.fEngine->finished() &&
           now > fPlay.fMap->lastObjectEndTime() + 1000.0;
  }

  void submitAutoplay(double now) {
    if (!fAutoplay) {
      return; // the player is driving
    }
    while (fAutoplayIndex < fPlay.fAutoplayEvents.size() &&
           fPlay.fAutoplayEvents[fAutoplayIndex].fTime <= now) {
      const auto &ev = fPlay.fAutoplayEvents[fAutoplayIndex];
      fPlay.fEngine->submit(ev);
      if (fReplayPath.empty()) {
        // Generated autoplay is worth recording; a replay being watched is
        // already on disk.
        fPlay.fRecordedEvents.push_back(ev);
      }
      if (ev.fAction == osu::InputAction::kMove) {
        fCursor = ev.fPos;
        fView.addTrailPoint(fCursor, ev.fTime);
      }
      ++fAutoplayIndex;
    }
  }


  // Cursor trail as a single feathered ribbon.
  //
  // The old implementation stroked each segment separately with butt caps:
  // visible notches at joints, alpha banding between segments and double
  // blending where semi-transparent strokes overlapped. Instead we build one
  // triangle mesh along the smoothed polyline: an opaque spine that fades to
  // zero alpha at both edges. The feather is the antialiasing (SkVertices is
  // not AA'd by itself), there are no joints to mismatch, and the whole
  // trail is one draw call.

  // Cursor sensitivity scales movement about the playfield centre, which is
  // what osu! does when the setting is not 1x.
  // 1:1 maps the pointer straight onto the playfield, which is what a tablet
  // wants. Otherwise the pointer is grabbed and its motion is integrated at
  // the chosen sensitivity, so the whole playfield stays reachable however
  // low the sensitivity is.
  [[nodiscard]] osu::Vec2 cursorFromEvent(const Event &ev) {
    const auto raw = this->toPlayfield(ev.fX, ev.fY);
    if (!this->relativeCursor()) {
      fVirtualCursor = raw;
      fHasRawPrev = false;
      return raw;
    }
    const double s = fSettings.value("sensitivity");
    if (!fHasRawPrev) {
      fRawPrev = raw;
      fHasRawPrev = true;
    }
    const osu::Vec2 delta{(raw.fX - fRawPrev.fX) * s,
                          (raw.fY - fRawPrev.fY) * s};
    fRawPrev = raw;
    // Held inside the window, not inside the playfield. An absolute pointer
    // is bounded by the window and nothing else -- the branch above does not
    // clamp at all -- so confining the integrated one to the playfield made
    // turning this on put walls across the middle of the screen: on a 16:9
    // display the playfield is 60% of the width, and the pointer stopped
    // dead at its edge with the desk still going.
    const osu::Vec2 lo = this->toPlayfield(0.0f, 0.0f);
    const osu::Vec2 hi = this->toPlayfield(static_cast<float>(fWin.fScreenW),
                                           static_cast<float>(fWin.fScreenH));
    fVirtualCursor = {std::clamp(fVirtualCursor.fX + delta.fX, lo.fX, hi.fX),
                      std::clamp(fVirtualCursor.fY + delta.fY, lo.fY, hi.fY)};
    return fVirtualCursor;
  }

  void printResult() {
    if (fPlay.fEngine) {
      client::printResult(*fPlay.fEngine, fWindowRuntime.droppedInput());
    }
  }

  [[nodiscard]] std::string beatmapMd5() const {
    return client::beatmapMd5(fSet, fBeatmapFilename);
  }

  void saveReplay() {
    if (!fPlay.fMap || !fPlay.fEngine || !fReplayPath.empty()) {
      return;
    }
    const auto saved =
        client::saveReplay(fPlay.fRecordedEvents, *fPlay.fMap, *fPlay.fEngine,
                           this->beatmapMd5(), fMods, fReplayDir);
    if (!saved) {
      return;
    }
    fPlay.fLastSavedReplay = *saved;
    fReplayBrowser.add(
        *saved, fPlay.fEngine->rules() == osu::RuleSet::kLegacyClient ? 1 : 0);
    std::println(std::cerr, "[replay] saved {}", saved->string());
  }

  [[nodiscard]] osu::Vec2 toPlayfield(float sx, float sy) const {
    return {(static_cast<double>(sx) - fOffsetX) / fScale,
            (static_cast<double>(sy) - fOffsetY) / fScale};
  }
};

} // namespace client
