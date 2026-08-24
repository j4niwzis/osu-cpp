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
import client.appscreens;
import client.appoverlays;
import client.appplayback;
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
  friend class client::AppScreens<App>;
  friend class client::AppOverlays<App>;
  friend class client::AppPlayback<App>;

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
    // The screen in units of the tree, which is what every screen, overlay
    // and pointer coordinate is in. Multiply by the pixel scale to get what
    // the surface actually is.
    int fScreenW = 1280;
    int fScreenH = 960;
    // What the window really is, in device pixels. Only the surface, the
    // clip, the blit and the swap care.
    int fPixelW = 1280;
    int fPixelH = 960;
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
  client::AppScreens<App> fScreens{*this};
  client::AppOverlays<App> fOverlays{*this};
  client::AppPlayback<App> fPlayback{*this};
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
          // The engine, star attributes and playback clock all have to use
          // the mods recorded in the replay. Applying these after Engine was
          // constructed made a watched replay run under whichever mods were
          // currently selected in the UI instead.
          fMods = replay->fMods;
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
    fAudio.setVolume(fOverlays.musicGain());
    fPlay.fAwaitingFirstFrame = true;
  }

  // Called at the end of the first gameplay frame, once it has been handed to
  // the window. From here the map and the music start together.
  void startGameplayClock() {
    fPlay.fAwaitingFirstFrame = false;
    fPlay.fStartMs = wallMs();
    fPlay.fClock.reset(fPlay.fStartMs, 0.0);
    fPlayback.resetClockSync();
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
    fWin.fPixelW = initial.fWidth;
    fWin.fPixelH = initial.fHeight;
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
    this->resize(fWin.fPixelW, fWin.fPixelH);

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
    glfw::glfwGetFramebufferSize(fWindow, &fWin.fPixelW, &fWin.fPixelH);

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
    this->resize(fWin.fPixelW, fWin.fPixelH);

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
      fPlayback.printResult();
      if (fRecord)
        fPlayback.saveReplay();
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
      fReplayBrowser.selectBeatmap(fPlayback.beatmapMd5(), fReplayPath,
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
                  .fWidth = fWin.fPixelW,
                  .fHeight = fWin.fPixelH,
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
      fScreens.updateDownload();
    } else if (fState == State::kSongSelect) {
      fScreens.updateSongSelect();
    } else if (fState == State::kPaused) {
      fScreens.updatePause();
    } else if (fState == State::kMainMenu) {
      this->updateMainMenu();
    } else if (fState == State::kResults) {
      fScreens.updateResults();
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
      const auto entries = fOverlays.modEntries();
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
    // Continuation and invalidation are deliberately separate. A retained
    // scene can need another update without having changed pixels in this
    // one; treating that scheduling request as damage sends an empty region
    // to FrameState::begin(), where an empty region means a full repaint.
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
        fWindowRuntime.visiblePortion(fWin.fPixelW, fWin.fPixelH);
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
      if (onScreen.isEmpty() || (onScreen.width() >= fWin.fPixelW &&
                                 onScreen.height() >= fWin.fPixelH)) {
        fFrame.damageAll("the window system asked for a repaint");
      } else {
        // visiblePortion answers in pixels; damage() is told in units.
        const float scale = this->pixelScale();
        fFrame.damage(skia::SkRect::MakeLTRB(
            static_cast<float>(onScreen.fLeft) / scale,
            static_cast<float>(onScreen.fTop) / scale,
            static_cast<float>(onScreen.fRight) / scale,
            static_cast<float>(onScreen.fBottom) / scale));
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
      fOverlays.pollExportVideo();
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
      fScreens.frameSongSelect();
      break;
    case State::kDownload:
      fScreens.frameDownload();
      break;
    case State::kPlaying:
      this->framePlaying();
      break;
    case State::kPaused:
      fScreens.framePaused();
      break;
    case State::kResults:
      fScreens.frameResults();
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
    // In pixels: this is drawn after the frame's transform has been lifted,
    // so it does not follow the interface scale.
    const float sw = static_cast<float>(fWin.fPixelW);
    const float sh = static_cast<float>(fWin.fPixelH);
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
    fFrame.includeInBlit(box, fWin.fPixelW, fWin.fPixelH);
  }

  void present() {
    const auto frameStart = fFrame.fDiag.fFrameStart;
    // Overlays float above whatever screen is drawn.
    auto *canvas = fFrame.fSurface->getCanvas();
    if (fModSelect.visible()) {
      fOverlays.drawModSelect(canvas);
    }
    if (fSettingsPanel.visible()) {
      // What it repaints was worked out before the frame began, by the panel
      // itself; here it is only drawn.
      fOverlays.drawSettings(canvas);
    }
    if (fReplayBrowser.open()) {
      fScreens.drawReplayList(canvas);
    }
    if (fExportDialog.open()) {
      fOverlays.drawExportDialog(canvas);
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
    if (damage.empty() || !present::swapWithDamage(fWin.fPixelH, damage)) {
      glfw::glfwSwapBuffers(fWindow);
    }
    fLastSwapUs = std::chrono::duration_cast<std::chrono::microseconds>(
                      std::chrono::steady_clock::now() - beforeSwap)
                      .count();
    fFrame.reportCost(frameStart, beforeSwap, fWin.fPixelW, fWin.fPixelH,
                      this->partialRedraw());
    fFrame.fDrawing = false;
  }

  void framePlaying() {
    using clock = std::chrono::steady_clock;
    // Zero until the first frame has been through, so that nothing before it
    // counts against the map.
    const double now = fPlay.fAwaitingFirstFrame ? 0.0 : fPlayback.nowMs();
    if (fPlayback.shouldStop(now)) {
      this->finishPlay();
      fScreens.frameResults();
      return;
    }
    fPlayback.submitAutoplay(now);
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
    return std::format("{}:{}:{:.4f}:{}", fPlayback.beatmapMd5(),
                       fBeatmapFilename, fScale,
                       fSettings.choice("renderer"));
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
  // The HUD is written in pixels against a 1080-tall screen; this says how
  // much bigger the surface is. Asked for in two places now -- the view draws
  // with it, and the client hit-tests the pause button against it.
  // How many device pixels one unit is. One by default; the setting scales
  // the whole interface, which is what a phone or a 4K panel needs and what
  // every screen doing its own clamp(height / something) could never give.
  [[nodiscard]] float pixelScale() const {
    const float wanted = fSettings.value("uiscale");
    return wanted > 0.05f ? wanted : 1.0f;
  }

  [[nodiscard]] float uiScale() const {
    return std::clamp(static_cast<float>(fWin.fScreenH) / 1080.0f, 0.7f, 3.0f);
  }

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
    c.fUiScale = this->uiScale();
    c.fDim = fSettings.value("dim");
    c.fNoGlow = fNoGlow;
    c.fHitLighting = fSettings.flag("hitlighting");
    c.fShowCursor = fSettings.flag("cursor");
    c.fCursorTrail = fSettings.flag("cursortrail");
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
    fOverlays.closeSettings();
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
    fPlay.fPausedNow = fPlayback.nowMs();
    fAudio.pause();
    this->switchState(State::kPaused);
    this->setCursorVisible(true);
  }

  void resumeGame() {
    // Re-anchor the clock at the frozen instant: wall time spent in the
    // pause menu never existed as far as the game timeline is concerned.
    fPlay.fClock.reset(wallMs(), fPlay.fPausedNow);
    fPlayback.resetClockSync(wallMs());
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
    fPlayback.printResult();
    // Replays are kept by default (the setting can turn it off), so a good
    // run is never lost because the flag was not passed.
    // Watching a replay must not write it back out again.
    if (fReplayPath.empty() && (fRecord || fSettings.flag("savereplay"))) {
      fPlayback.saveReplay(); // the index picks it up; the results list follows
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
    if (fw != fWin.fPixelW || fh != fWin.fPixelH) {
      this->resize(fw, fh);
    }

    if (fWindowRuntime.quitting()) {
      if (fPlay.fEngine &&
          (fState == State::kPlaying || fState == State::kPaused)) {
        fPlayback.printResult();
        if (fRecord)
          fPlayback.saveReplay();
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
      fOverlays.toggleSettings();
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
    auto menuFrame = fMainMenu.finishFrame();
    menuFrame.fDamage = fSettingsPanel.composeOver(
        menuFrame.fDamage, static_cast<float>(fWin.fScreenH));
    fFrame.consume(menuFrame);
  }

  void frameMainMenu() {
    auto *canvas = fFrame.fSurface->getCanvas();
    fMainMenu.render(canvas);
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
        fScreens.drawSetPanel(canvas, rect, row.fSet, infos, selected, hovered,
                              corner);
      } else if (row.fDiff < static_cast<int>(infos.size())) {
        fScreens.drawDiffPanel(canvas, rect,
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
        fFrame.fRasterSurface->width() == fWin.fPixelW &&
        fFrame.fRasterSurface->height() == fWin.fPixelH) {
      return true;
    }
    // Same colour space as the window, or the pixels get encoded twice on the
    // way over and the whole frame comes out lighter.
    fFrame.fRasterSurface = skia::Raster(skia::SkImageInfo::Make(
        fWin.fPixelW, fWin.fPixelH, skia::kRGBA_8888_SkColorType,
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
    fWin.fPixelW = w;
    fWin.fPixelH = h;
    const float scale = this->pixelScale();
    fFrame.setUiScale(scale);
    fWin.fScreenW = std::max(1, static_cast<int>(std::lround(w / scale)));
    fWin.fScreenH = std::max(1, static_cast<int>(std::lround(h / scale)));
    this->layoutForScreen();

    skia::GrGLFramebufferInfo info;
    info.fFBOID = 0;
    info.fFormat = skia::kGlRgba8;
    skia::GrBackendRenderTarget target =
        skia::MakeGL(fWin.fPixelW, fWin.fPixelH, 0, 0, info);
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
};

} // namespace client
