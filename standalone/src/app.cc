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
import client.spectrum;
import client.mapcache;
import client.replaycache;
import client.filter;
import client.loader;
import client.ui;
import client.settings;
import client.settingspanel;
import client.overlays;
import client.filtercontrol;
import client.listing;
import client.carousel;
import client.pause;
import client.mainmenu;
import client.triangles;
import present;
import client.setpage;
import client.scene;
import client.nodes;
import client.gameplayview;
import client.mods;
import client.video;
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

export class App {
public:
  App(std::optional<osu::BeatmapSet> set, osu::ModSet mods, bool headless,
      bool autoplay, std::filesystem::path replayPath = {},
      bool record = false, std::filesystem::path skinPath = {},
      bool profile = false)
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
  osu::BeatmapSet fSet;
  osu::ModSet fMods = osu::mod::kNone;
  std::optional<osu::Beatmap> fMap;
  std::optional<osu::Engine> fEngine;
  bool fHeadless = false;
  bool fAutoplay = false;
  bool fCliAutoplay = false;      // --autoplay, which outlives a single play
  std::filesystem::path fReplayPath;  // the file driving the current play
  std::filesystem::path fPendingReplay; // requested for the play about to start
  bool fRecord = false;
  std::string fBeatmapFilename;
  std::vector<osu::InputEvent> fAutoplayEvents;
  std::vector<osu::InputEvent> fRecordedEvents;
  std::size_t fAutoplayIndex = 0;
  Skin fSkin;
  bool fShowProfile = false;
  bool fNoGlow = false;

  // Window / GL / Skia
  glfw::GLFWwindow *fWindow = nullptr;
  skia::Sp<skia::GrDirectContext> fContext;
  skia::Sp<skia::SkSurface> fSurface;
  int fScreenW = 1280;
  int fScreenH = 960;
  int fWindowedW = 1280;
  int fWindowedH = 960;
  int fWindowedX = 100;
  int fWindowedY = 100;
  bool fFullscreen = true;
  float fScale = 1.0f;
  float fOffsetX = 0.0f;
  float fOffsetY = 0.0f;

  // Input
  osu::Vec2 fCursor = osu::kPlayfieldCenter;
  osu::Vec2 fRawPrev{};                              // last pointer position
  osu::Vec2 fVirtualCursor = osu::kPlayfieldCenter;  // integrated position
  bool fHasRawPrev = false;
  float fMouseX = 0.0f;
  float fMouseY = 0.0f;
  std::uint32_t fHeldMask = 0; // bit per held key/button (Z/X/Space/M1/M2)

  // Event queue: filled on the GLFW event-pump (main) thread, drained on the
  // render thread. 4096 slots absorb multi-second bursts of cursor reports.
  SpscQueue<4096> fInputQueue;
  std::atomic<bool> fQuit{false};
  std::atomic<int> fExitCode{0};
  // Cursor-mode change requests marshalled to the main thread (GLFW requires
  // glfwSetInputMode on the thread that owns the window). -1 = none.
  std::atomic<int> fCursorModeRequest{-1};
  std::atomic<int> fRawMotionRequest{-1};
  // Swap interval is a property of the context, so only the thread holding it
  // may set it; a toggle in the settings parks the value here.
  std::atomic<int> fSwapIntervalRequest{-1};
  int fSwapInterval = -1;
  int fRefreshHz = 60; // the monitor's, sampled where GLFW allows the query
  double fRedrawUntilWall = 0.0; // frames are drawn until at least this time
  double fLastDrawWall = 0.0;
  int fFramesOwed = 0;      // frames promised to something that just moved
  double fWakeWall = 0.0;   // when a screen asked to be woken, or 0
  bool fDamageDrives = false; // damage that is worth a frame of its own
  bool fDrawing = false;      // inside a frame: damage reported now is not
  int fFullRepaintsOwed = 0;  // buffers still holding an older screen
  int fBufferAge = -1;        // frames since this buffer last held a frame
  std::vector<skia::SkIRect> fBlitRegions; // what to carry over; empty = all
  bool fBufferAgeAssumed = false; // ...or what we were told to believe
  skia::Sp<skia::SkSurface> fWindowSurface; // the swap chain
  skia::Sp<skia::SkSurface> fRasterSurface; // Skia's own CPU target
  bool fDrewOnRaster = false;               // this frame went to the CPU one
  // Two distinct things, which sharing one variable confused: what the next
  // frame has to repaint (filled in while this one draws) and what this frame
  // is clipped to (taken from that accumulator when the frame starts).
  //
  // A list rather than one rectangle: the logo and the FPS counter sit in
  // opposite corners, and their union is half the screen.
  static constexpr std::size_t kMaxDamageRects = 3;
  std::vector<skia::SkIRect> fDamage;
  bool fFullDamage = true;
  const char *fFullDamageReason = "start";
  bool fOverlayShown = false; // an overlay covered the screen last frame
  double fDamageLogWall = 0.0;
  const char *fLoggedFullReason = nullptr;
  std::chrono::steady_clock::time_point fFrameStart{};
  std::chrono::steady_clock::time_point fBlitStart{};
  std::int64_t fCostUpdateUs = 0, fCostDrawUs = 0, fCostBlitUs = 0,
      fCostSwapUs = 0;
  std::int64_t fLastUpdateUs = 0;
  std::uint64_t fCostVisited = 0, fCostDrawn = 0;
  std::int64_t fCostClipArea = 0; // pixels the frames were allowed to touch
  int fCostFrames = 0;
  double fCostLogWall = 0.0;
  std::vector<skia::SkIRect> fFrameClip;
  bool fFrameClipFull = true;
  // What each of the last few frames repainted, since the buffer being drawn
  // into is missing exactly that.
  std::vector<std::vector<skia::SkIRect>> fBlitHistory;
  // The setting decides; the variable is there for a run before settings
  // exist, and to force it on while measuring.
  const bool fForcePartialRedraw =
      std::getenv("OSU_PARTIAL_REDRAW") != nullptr;
  const bool fForceShowDamage = std::getenv("OSU_SHOW_DAMAGE") != nullptr;
  // OSU_BUFFER_AGE=N overrides the setting of the same meaning, for measuring.
  const int fForcedBufferAge = [] {
    const char *value = std::getenv("OSU_BUFFER_AGE");
    return value != nullptr ? std::atoi(value) : 0;
  }();
  std::vector<skia::SkIRect> fComputedClip; // what the frame would have used
  bool fComputedClipFull = true;

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
  int fFrameSave = 0; // canvas save count taken while the damage clip is up
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
  State fState = State::kMainMenu;
  bool fHasInitialSet = false;

  // Library / song select (lazer-style carousel on the right).
  // Library entries hold metadata only. The full BeatmapSet (every file in
  // the archive, decoded audio, images) is loaded on demand and a handful are
  // kept alive at a time -- keeping every set resident made startup slow and
  // the resident set enormous once the library grew.
  struct LibraryEntry {
    std::filesystem::path fPath; // empty for the set passed on the CLI
    std::vector<osu::BeatmapInfo> fInfos;
    std::shared_ptr<osu::BeatmapSet> fLoaded; // nullptr until needed
    skia::Sp<skia::SkImage> fPanelArt;        // lazily fetched cover
    bool fPanelArtTried = false;
  };
  std::vector<LibraryEntry> fLibrary;
  std::deque<int> fLoadedOrder;          // LRU of entries holding a full set
  static constexpr std::size_t kMaxLoadedSets = 4;
  client::MapCache fMapCache;
  client::Loader fLoader;

  // Filtering / sorting: the control itself lives in client.filtercontrol.
  client::FilterControl fFilter;
  std::vector<int> fVisible; // indices into fLibrary passing the filter
  bool fFilterDirty = true;
  bool fLibraryLoaded = false;
  int fSelSet = 0;
  int fSelDiff = 0;
  client::carousel::Carousel fCarousel;
  std::vector<client::carousel::Row> fRows; // what the carousel lays out
  int fBackgroundForSet = -1;
  int fMenuMusicForSet = -1; // set whose audio is playing under the menus
  double fMenuTrackWall = 0.0; // when it started, so its end can be told
  double fMusicPollWall = 0.0; // last time the track was asked if it ended
  std::filesystem::path fMapsDir;
  std::filesystem::path fThumbDir;
  std::filesystem::path fReplayDir;
  std::mutex fDropMutex;                  // guards fDropped
  std::vector<std::string> fDropped;      // files dropped onto the window
  skia::SkRect fRandomChip = skia::SkRect::MakeEmpty();    // footer button
  skia::SkRect fModsChip = skia::SkRect::MakeEmpty();      // footer button
  skia::SkRect fOptionsChip = skia::SkRect::MakeEmpty();   // footer button
  skia::SkRect fBackChip = skia::SkRect::MakeEmpty();      // footer back
  bool fOptionsOpen = false;
  std::vector<skia::SkRect> fOptionHits;
  bool fConfirmDelete = false; // the deletion dialog is up
  // Built as a scene tree rather than drawn by hand: the first screen on the
  // retained renderer, and the pattern the rest follow.
  std::unique_ptr<client::scene::Drawable> fConfirmScene;

  // Download screen (mirror search + .osz fetch).
  client::listing::Listing fListing;
  client::setpage::SetPage fSetPage;
  // Track previews come from osu!'s own preview endpoint and play on their
  // own source, so the menu music is untouched.
  client::AudioPlayer fPreview;
  long fPreviewId = -1;
  bool fPreviewPending = false;
  std::uint32_t fPreviewGeneration = 0; // stale fetches must not start playing
  bool fMusicDucked = false;            // menu music paused for a preview
  std::vector<client::listing::Entry> fFound;
  // Transfers in flight, so progress can be polled without the view knowing
  // anything about HTTP.
  std::map<long, std::shared_ptr<client::http::Handle>> fTransfers;
  std::vector<std::uint8_t> fEntryStates; // what each card last drew as its own
  bool fSearchPending = false;
  int fSearchOffset = 0;        // how much of the current search is loaded
  std::uint32_t fSearchGeneration = 0; // results from older queries are dropped
  bool fMoreAvailable = true;   // a full page came back, so ask for the next
  bool fSwallowChar = false; // the 'D' that opened the screen also arrives
                             // as a char event; it must not enter the query
  std::string fDownloadStatus;
  // A short-lived message in the corner, as lazer's notification overlay
  // shows when an import finishes.
  std::string fToast;
  skia::SkColor fToastColor = skia::kWhite;
  double fToastWall = 0.0;
  struct ReplayFile {
    std::filesystem::path fPath;
    std::string fLabel;
    osu::ReplayScore fScore; // from the .osr header, via the index
    std::string fGrade;
    bool fHasScore = false;
  };
  bool fReplayListOpen = false;
  std::vector<ReplayFile> fReplays;
  std::string fReplayFilter; // md5 the list was built for
  client::ReplayIndex fReplayIndex;
  std::filesystem::path fLastSavedReplay; // this run's own file
  struct PanelHit {
    skia::SkRect fRect;
    int fIndex;
  };
  std::vector<PanelHit> fPanelHits;
  int fSelectedPanel = 0;
  float fPanelScroll = 0.0f;
  float fPanelScrollTarget = 0.0f;
  float fPanelScrollOrigin = 0.0f;
  float fPanelDragOrigin = 0.0f;
  bool fPanelDragging = false;
  bool fPanelDragged = false;
  bool fPanelFreeScroll = false; // user dragged/scrolled away from centre
  bool fPanelOwnScore = false;   // the strip includes the run just played
  std::vector<int> fPanelEntries; // index into fReplays, -1 = the score in hand
  skia::SkRect fPanelBand = skia::SkRect::MakeEmpty();

  // Pause / results overlays.
  struct MenuButton {
    skia::SkRect fRect;
    std::string fLabel;
    skia::SkColor fAccent;
  };
  std::vector<MenuButton> fMenuButtons; // rebuilt every pause/results frame
  double fPausedNow = 0.0;              // frozen game time while paused
  double fPolledCursorX = -1.0, fPolledCursorY = -1.0; // wasm cursor polling
  std::atomic<bool> fRefreshRequested{false}; // set by the event thread
  std::atomic<int> fWindowX{0}, fWindowY{0};  // where the window sits
  std::atomic<int> fWorkAreaX{0}, fWorkAreaY{0}, fWorkAreaW{0}, fWorkAreaH{0};
  client::pause::PauseMenu fPauseMenu;
  int fRetryCount = 0;      // plays of this map since it was chosen
  bool fRetryPending = false;
  int fPlayingSet = -1;
  int fPlayingDiff = -1;

  struct ResultData {
    osu::ScoreState fScore{};
    double fMean = 0.0;
    double fUr = 0.0;
    std::string fGrade = "F";
  };
  ResultData fResult;

  // ---- UI animation state ----
  double fStateEnterWall = 0.0;
  double fUiPrevWall = 0.0;
  double fUiDt = 16.0;         // ms, clamped
  // What the parts of song select that are still drawn immediately last drew,
  // so they can say when it changes rather than repainting on every frame.
  int fHotFilter = 0;   // which filter control the pointer is on
  int fHotFooter = 0;   // which footer chip the pointer is on
  skia::SkRect fHotFooterRect = skia::SkRect::MakeEmpty();
  std::int64_t fFilterState = -1;
  bool fFilterCaret = false;
  bool fDrawnOptionsOpen = false;
  bool fDrawnEmpty = false;
  std::size_t fDrawnVisibleCount = 0;
  std::string fDrawnFilterText;
  std::int64_t fWedgeKey = -1;

  // ---- Main menu button system (port of lazer's ButtonSystem) ----------
  //
  // lazer models the menu as a small state machine (Initial -> TopLevel ->
  // a submenu), with every button owning an expand/contract animation and
  // the logo sliding aside to make room. Same structure here.
  enum class MenuState : std::uint8_t { kInitial, kTopLevel, kPlay };
  enum class MenuAction : std::uint8_t {
    kOpenPlay,
    kBrowse,
    kImport,
    kExit,
    kSolo,
    kRandom,
    kBack,
    kSettings,
  };
  struct MenuBtn {
    std::string fLabel;
    std::string fGlyph;      // drawn as a text glyph; no icon font here
    skia::SkColor fColor{};
    MenuAction fAction{};
    MenuState fVisible{};    // menu state this button belongs to
    bool fLeftSide = false;  // back button sits left of the logo, as in lazer
    float fExpand = 0.0f;    // 0 contracted .. 1 expanded
    float fHover = 0.0f;     // eased hover weight
    float fFlash = 0.0f;     // click flash, decays
    skia::SkRect fRect = skia::SkRect::MakeEmpty();
    // What was last put on screen, so a button that has stopped moving stops
    // asking to be repainted.
    float fDrawnExpand = -1.0f;
    float fDrawnHover = -1.0f;
    float fDrawnFlash = -1.0f;
    skia::SkRect fDrawnRect = skia::SkRect::MakeEmpty();
  };
  std::vector<MenuBtn> fMenuBtns;
  MenuState fMenuState = MenuState::kInitial;
  float fLogoX = 0.0f;
  float fLogoY = 0.0f;
  float fLogoScale = 1.0f;
  bool fLogoPlaced = false;
  float fLogoHover = 0.0f;
  float fLogoPunch = 0.0f; // click/beat impact, decays
  float fLogoAmp = 0.0f;    // beat amplitude the logo settled at
  float fLogoRadius = 0.0f;
  float fLogoBase = 0.0f;   // unscaled radius for this screen size
  float fMenuWedge = 20.0f; // the shear on the menu's parallelograms
  int fHotResultButton = -1; // which action the pointer is on
  float fDrawnMouseX = -1.0f, fDrawnMouseY = -1.0f;
  int fHotReplayPanel = -1;
  float fDrawnPanelScroll = 0.0f; // where the strip was when it was last drawn
  // Defined further down, next to the code that steps it; a unique_ptr only
  // needs the type complete where it is destroyed, which is the end of this
  // class.
  struct ExportJob;
  std::unique_ptr<ExportJob> fExportJob; // a video being rendered, in slices
  int fHotDialogPiece = -1; // which piece of the export dialog is under it
  client::mainmenu::Menu fMenu; // where the menu's pieces are, and what moved
  skia::SkRect fLogoRect = skia::SkRect::MakeEmpty();
  client::Spectrum fSpectrum;

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


  AnchoredClock fMenuClock;
  double fMenuClockSyncWall = std::numeric_limits<double>::lowest();
  double fLastMenuPosMs = 0.0;
  client::triangles::Field fBackgroundTriangles;
  client::triangles::Field fLogoTriangles;
  float fMenuDim = 1.0f;
  // MainMenu.cs: Gray(1) idle, Gray(0.8) with buttons.
  float fDrawnMenuDim = -1.0f; // the dim the screen currently shows
  struct Angle {
    float fCos = 0.0f, fSin = 0.0f;
  };
  std::vector<Angle> fVisualiserAngles; // fixed per bar count, not per frame
  int fVisualiserCount = 0;
  // Seeded per run: a fixed seed meant the same "random" map on launch, the
  // same order of tracks after it, and the same triangles behind the logo,
  // every single time.
  std::mt19937 fUiRng{std::random_device{}()};

  [[nodiscard]] static float easeOutQuint(float t) {
    return client::ui::outQuint(t);
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
    this->oweFrames(2);
    skia::SkPaint p;
    p.setColor(skia::colorSetARGB(
        static_cast<std::uint8_t>((1.0f - fade) * 160.0f), 10, 8, 14));
    canvas->drawRect(skia::SkRect::MakeXYWH(0, 0,
                                            static_cast<float>(fScreenW),
                                            static_cast<float>(fScreenH)),
                     p);
  }

  // Palette lives in client.ui; these are aliases so call sites stay short.
  static constexpr auto kAccent = client::ui::kAccent;
  static constexpr auto kAccent2 = client::ui::kAccent2;
  static constexpr auto kCardBg = client::ui::kCardBg;
  static constexpr auto kCardSel = client::ui::kCardSel;
  static constexpr auto kPanelBg = client::ui::kPanelBg;

  // Timing
  double fStartMs = 0.0;
  AnchoredClock fClock;
  double fLastClockSyncWall = std::numeric_limits<double>::lowest();
  static constexpr double kClockSyncIntervalMs = 250.0;
  AudioPlayer fAudio;
  std::unordered_map<std::string, SamplePlayer> fSamples;
  std::size_t fPlayedEvents = 0;
  int fCombo = 0;

  // Font
  skia::SkFont fFont;
  skia::SkFont fDisplayFont;








  // Combo color group for each object.
  osu::ComboInfo fComboInfo;

  void loadComboInfo() { fComboInfo = osu::buildComboInfo(*fMap); }

  void startGameplay(const osu::BeatmapInfo &info) {
    fMap.emplace(client::loadBeatmap(fSet, info));
    fBeatmapFilename = info.fFilename;
    fEngine.emplace(*fMap, fMods);
    this->loadComboInfo();
    fSkin.setComboColors(fMap->fComboColors);
    fSkin.precomputeSliderBodies(*fMap, fComboInfo, fScale, fContext.get());
    if (fSettings.choice("renderer") == 1) {
      // Built on the GPU either way, since that is what SkSL is for; moved
      // into memory now so the CPU rasteriser does not read them back on
      // every frame that draws a slider.
      fSkin.flattenBodiesToRaster(fContext.get());
    }
    if (fAutoplay) {
      if (!fReplayPath.empty()) {
        std::ifstream file(fReplayPath, std::ios::binary);
        if (file) {
          std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>(file),
                                          std::istreambuf_iterator<char>()};
          auto replayData = osu::decodeReplay(bytes);
          fAutoplayEvents = std::move(replayData.fEvents);
          fMods = replayData.fMods;
          // The video exporter renders the recorded events; a watched replay
          // is its own recording. saveReplay refuses to write it back out.
          fRecordedEvents = fAutoplayEvents;
        }
      } else {
        fAutoplayEvents = osu::buildAutoplay(*fMap, fMods);
      }
      fAutoplayIndex = 0;
    }

    if (!fMap->fMeta.fAudioFilename.empty()) {
      const auto bytes = fSet.findFile(fMap->fMeta.fAudioFilename);
      if (!bytes.empty()) {
        fAudio.load(bytes, detail::fileExtension(fMap->fMeta.fAudioFilename));
      }
    }

    if (!fMap->fMeta.fBackground.empty()) {
      const auto bytes = fSet.findFile(fMap->fMeta.fBackground);
      if (!bytes.empty()) {
        fView.setBackground(loadImage(bytes));
        fView.preScaleBackground(this->gameplayCtx(nullptr));
      }
    }

    fStartMs = glfw::glfwGetTime() * 1000.0;
    fClock.reset(fStartMs, 0.0);
    fLastClockSyncWall = std::numeric_limits<double>::lowest();
    fAudio.setVolume(this->musicGain());
    fAudio.play();
  }

  [[nodiscard]] int runHeadless() {
    if (!fHasInitialSet || fSet.fBeatmaps.empty()) {
      return 1;
    }
    this->startGameplay(fSet.fBeatmaps.front());
    const auto result = osu::runAutoplay(*fMap, fEngine->mods());
    std::println("{}", result.fScore);
    return 0;
  }

  [[nodiscard]] int runWindowed() {
    if (!glfw::glfwInit()) {
      return 1;
    }

#ifdef __EMSCRIPTEN__
    glfw::glfwWindowHint(glfw::kClientApi, glfw::kOpenGLApi);
    glfw::glfwWindowHint(glfw::kContextVersionMajor, 3);
    glfw::glfwWindowHint(glfw::kContextVersionMinor, 0);
    glfw::glfwWindowHint(glfw::kResizable, glfw::kTrue);
    glfw::glfwWindowHint(glfw::kSamples, 0);

    int fsw = EM_ASM_INT({ return Module.canvas.width; });
    int fsh = EM_ASM_INT({ return Module.canvas.height; });
    fWindow = glfw::glfwCreateWindow(fsw, fsh, "osu_client", nullptr, nullptr);
#else
    const auto monitor = glfw::glfwGetPrimaryMonitor();
    const glfw::GLFWvidmode *mode = glfw::glfwGetVideoMode(monitor);
    fScreenW = mode->width;
    fScreenH = mode->height;
    if (mode->refreshRate > 0) {
      fRefreshHz = mode->refreshRate;
    }

    // Desktop GL first, then GLES: a phone or a tablet running a Linux the
    // client can otherwise be built for has drivers that only speak GLES,
    // often through EGL only. Skia's Ganesh backend is happy with either --
    // GrGLMakeNativeInterface picks up whichever one is current -- so the
    // difference is entirely in how the context is asked for.
    struct ContextChoice {
      const char *fName;
      int fApi;
      int fMajor;
      int fMinor;
      int fProfile;
      int fCreation;
    };
    // OSU_EGL asks for the context through EGL rather than GLX. On X11 the
    // native path is GLX, which has the buffer age extension but no way to
    // hand the compositor a damage region; EGL has both. Kept behind a switch
    // because changing how the context is created is not something to do
    // quietly on every machine.
    const int desktopCreation = std::getenv("OSU_EGL") != nullptr
                                    ? glfw::kEglContextApi
                                    : glfw::kNativeContextApi;
    const ContextChoice choices[] = {
        {"gl 4.1 core", glfw::kOpenGLApi, 4, 1, glfw::kOpenGLCoreProfile,
         desktopCreation},
        {"gl 3.0", glfw::kOpenGLApi, 3, 0, glfw::kOpenGLAnyProfile,
         desktopCreation},
        {"gles 3.0", glfw::kOpenGLEsApi, 3, 0, glfw::kOpenGLAnyProfile,
         glfw::kEglContextApi},
        {"gles 2.0", glfw::kOpenGLEsApi, 2, 0, glfw::kOpenGLAnyProfile,
         glfw::kEglContextApi},
    };
    for (const auto &choice : choices) {
      glfw::glfwWindowHint(glfw::kClientApi, choice.fApi);
      glfw::glfwWindowHint(glfw::kContextVersionMajor, choice.fMajor);
      glfw::glfwWindowHint(glfw::kContextVersionMinor, choice.fMinor);
      glfw::glfwWindowHint(glfw::kOpenGLProfile, choice.fProfile);
      glfw::glfwWindowHint(glfw::kOpenGLForwardCompat,
                           choice.fApi == glfw::kOpenGLApi && choice.fMajor >= 3
                               ? glfw::kTrue
                               : glfw::kFalse);
      glfw::glfwWindowHint(glfw::kContextCreationApi, choice.fCreation);
      glfw::glfwWindowHint(glfw::kResizable, glfw::kTrue);
      fWindow = glfw::glfwCreateWindow(fScreenW, fScreenH, "osu_client",
                                       monitor, nullptr);
      if (fWindow != nullptr) {
        std::println(std::cerr, "[gfx] context: {}", choice.fName);
        break;
      }
    }
#endif
    if (fWindow == nullptr) {
      glfw::glfwTerminate();
      return 1;
    }
    glfw::glfwSetInputMode(fWindow, glfw::kCursor, glfw::kCursorNormal);
#ifdef __EMSCRIPTEN__
    EM_ASM(Module.setCursorVisible(true));
#endif

    glfw::glfwSetWindowUserPointer(fWindow, this);
    // Callbacks run on the event-pump (main) thread. They do two things only:
    // stamp the event with the wall clock and enqueue it. Window management
    // (close, fullscreen) is handled right here because GLFW requires it on
    // this thread; everything gameplay-related is consumed by the render
    // thread via drainInput().
    glfw::glfwSetKeyCallback(
        fWindow, [](glfw::GLFWwindow *w, int key, int, int action, int mods) {
          auto *self = static_cast<App *>(glfw::glfwGetWindowUserPointer(w));
          if (self == nullptr)
            return;
          if (action == glfw::kPress && key == glfw::kKeyF11) {
            self->toggleFullscreen();
            return;
          }
          // Auto-repeat is carried through rather than dropped here: it is
          // meaningless for gameplay and necessary for a held backspace, and
          // only the consumer knows which of the two it is looking at.
          self->enqueue({App::wallMs(), EventType::kKey, key, action,
                         static_cast<float>(mods)});
        });
    // The window system asking for the window back: dragged off the screen
    // and returned, uncovered, unminimised. Whatever was in those buffers is
    // gone, and a client that repaints only what it changed has to be told,
    // or it paints its little rectangle onto whatever the compositor left.
    glfw::glfwSetWindowRefreshCallback(
        fWindow, [](glfw::GLFWwindow *w) {
          auto *self = static_cast<App *>(glfw::glfwGetWindowUserPointer(w));
          if (self != nullptr) {
            self->noteWindowPlacement();
            self->fRefreshRequested.store(true, std::memory_order_release);
          }
        });
    // Where the window is, tracked from the thread that is allowed to ask:
    // what has to be repainted after an expose is the part of the window a
    // screen is actually showing.
    glfw::glfwSetWindowPosCallback(
        fWindow, [](glfw::GLFWwindow *w, int, int) {
          auto *self = static_cast<App *>(glfw::glfwGetWindowUserPointer(w));
          if (self != nullptr) {
            self->noteWindowPlacement();
          }
        });
    this->noteWindowPlacement();
    glfw::glfwSetMouseButtonCallback(
        fWindow, [](glfw::GLFWwindow *w, int button, int action, int) {
          auto *self = static_cast<App *>(glfw::glfwGetWindowUserPointer(w));
          if (self == nullptr)
            return;
          self->enqueue({App::wallMs(), EventType::kMouseButton, button, action});
        });
    glfw::glfwSetCursorPosCallback(
        fWindow, [](glfw::GLFWwindow *w, double x, double y) {
          auto *self = static_cast<App *>(glfw::glfwGetWindowUserPointer(w));
          if (self == nullptr)
            return;
          self->enqueue({App::wallMs(), EventType::kCursorMove, 0, 0,
                         static_cast<float>(x), static_cast<float>(y)});
        });
    glfw::glfwSetScrollCallback(
        fWindow, [](glfw::GLFWwindow *w, double, double y) {
          auto *self = static_cast<App *>(glfw::glfwGetWindowUserPointer(w));
          if (self == nullptr)
            return;
          self->enqueue({App::wallMs(), EventType::kScroll, 0, 0,
                         static_cast<float>(y)});
        });
    glfw::glfwSetDropCallback(
        fWindow, [](glfw::GLFWwindow *w, int count, const char **paths) {
          auto *self = static_cast<App *>(glfw::glfwGetWindowUserPointer(w));
          if (self == nullptr) {
            return;
          }
          // Paths are only valid for the duration of the callback, and this
          // runs on the event-pump thread: copy them out for the render
          // thread to import.
          const std::scoped_lock lock(self->fDropMutex);
          for (int i = 0; i < count; ++i) {
            self->fDropped.emplace_back(paths[i]);
          }
        });
    glfw::glfwSetCharCallback(
        fWindow, [](glfw::GLFWwindow *w, unsigned int codepoint) {
          auto *self = static_cast<App *>(glfw::glfwGetWindowUserPointer(w));
          if (self == nullptr)
            return;
          self->enqueue({App::wallMs(), EventType::kChar,
                         static_cast<std::int32_t>(codepoint), 0});
        });
    glfw::glfwSetFramebufferSizeCallback(
        fWindow, [](glfw::GLFWwindow *w, int width, int height) {
          auto *self = static_cast<App *>(glfw::glfwGetWindowUserPointer(w));
          if (self == nullptr)
            return;
          // Surface recreation must happen where the GL context is current:
          // marshal to the render thread.
          self->enqueue({App::wallMs(), EventType::kResize, width, height});
        });

#ifdef __EMSCRIPTEN__
    glfw::glfwMakeContextCurrent(fWindow);

    if (!this->initSkia()) {
      glfw::glfwDestroyWindow(fWindow);
      glfw::glfwTerminate();
      return 1;
    }

    this->loadFonts();
    fFont = this->loadFont(20.0f);
    client::nodes::Text::setFont(&fFont);
    fDisplayFont = this->loadDisplayFont(20.0f);
    this->resize(fScreenW, fScreenH);

    // Persistent library at /maps (IDBFS). The initial syncfs is async; the
    // library is scanned once the flag flips (see frameSongSelect).
    EM_ASM({
      try { FS.mkdir('/maps'); } catch (e) {}
      FS.mount(IDBFS, {}, '/maps');
      FS.syncfs(true, function(err) { Module._osu_maps_synced(); });
    });

    fState = State::kMainMenu;
    fStateEnterWall = wallMs();
    // A CLI-provided .osz reaches the wasm build too (preloaded); jump to it.
    if (fHasInitialSet) {
      this->selectInitialSet();
      fState = State::kSongSelect;
    }
    EM_ASM(Module.setCursorVisible(true));
    emscripten::emscripten_set_main_loop_arg(emscriptenFrameProc, this, 0, 1);
    return 0;
#else
    // Snapshot the real framebuffer size on the main thread (that query is
    // main-thread-only in GLFW); the render thread must not call it.
    glfw::glfwGetFramebufferSize(fWindow, &fScreenW, &fScreenH);

    // The GL context is owned by the render thread from here on. The main
    // thread degrades into a pure event pump: it blocks in glfwWaitEvents,
    // stamps and enqueues input, and services the few window operations that
    // GLFW pins to this thread.
    std::thread renderThread([this] { this->renderThreadMain(); });

    while (!fQuit.load(std::memory_order_acquire) &&
           !glfw::glfwWindowShouldClose(fWindow)) {
      glfw::glfwWaitEvents();
      const int cursorMode = fCursorModeRequest.exchange(-1);
      if (cursorMode != -1) {
        glfw::glfwSetInputMode(fWindow, glfw::kCursor, cursorMode);
      }
      const int rawMotion = fRawMotionRequest.exchange(-1);
      if (rawMotion != -1 && glfw::glfwRawMouseMotionSupported()) {
        glfw::glfwSetInputMode(fWindow, glfw::kRawMouseMotion, rawMotion);
      }
    }
    fQuit.store(true, std::memory_order_release);
    renderThread.join();
    return fExitCode.load(std::memory_order_acquire);
#endif
  }

#ifndef __EMSCRIPTEN__
  void renderThreadMain() {
    glfw::glfwMakeContextCurrent(fWindow);

    if (!this->initSkia()) {
      fExitCode.store(1, std::memory_order_release);
      this->requestQuit();
      return;
    }

    this->loadFonts();
    fFont = this->loadFont(20.0f);
    client::nodes::Text::setFont(&fFont);
    fDisplayFont = this->loadDisplayFont(20.0f);
    this->resize(fScreenW, fScreenH);

    this->initLibrary();
    // Launched with a beatmap on the command line => skip the main menu and
    // drop straight into song select on the imported set (it sorts to a known
    // index, so just point the selection at it).
    if (fHasInitialSet) {
      this->selectInitialSet();
      this->switchState(State::kSongSelect);
    } else {
      fState = State::kMainMenu;
      fStateEnterWall = wallMs();
    }

    while (!fQuit.load(std::memory_order_acquire) &&
           !glfw::glfwWindowShouldClose(fWindow)) {
      this->frame();
    }

    // A play interrupted by closing the window still reports to the console.
    if (fState == State::kPlaying || fState == State::kPaused) {
      this->printResult();
      if (fRecord)
        this->saveReplay();
    }
    fExitCode.store(0, std::memory_order_release);
    this->requestQuit();
    glfw::glfwMakeContextCurrent(nullptr);
  }

#endif

  void requestQuit() {
    fQuit.store(true, std::memory_order_release);
    glfw::glfwPostEmptyEvent(); // wakes the native pump; harmless on wasm
  }

  [[nodiscard]] static double wallMs() { return glfw::glfwGetTime() * 1000.0; }

  void enqueue(const Event &ev) { fInputQueue.tryPush(ev); }

  // ---- Input consumption (render thread) -------------------------------
  //
  // Events arrive from the queue already stamped with the wall clock of the
  // moment GLFW delivered them; eventGameTime() maps that onto the game
  // timeline. Judgement therefore no longer depends on when the frame loop
  // got around to polling: at any frame rate, a hit is judged at the time it
  // physically happened.

  void drainInput() {
    // Resizes are coalesced: a window being dragged by its corner delivers
    // dozens of sizes a frame, and acting on each of them means recreating
    // surfaces dozens of times a frame and drawing none of them -- which is
    // what the black flash while resizing was.
    Event ev;
    bool resized = false;
    int width = 0;
    int height = 0;
    while (fInputQueue.tryPop(ev)) {
      if (ev.fType == EventType::kResize) {
        resized = true;
        width = ev.fA;
        height = ev.fB;
        continue;
      }
      this->applyEvent(ev);
    }
    if (resized && (width != fScreenW || height != fScreenH)) {
      this->resize(width, height);
    }
  }

  [[nodiscard]] double eventGameTime(double wallMs) {
#ifdef __EMSCRIPTEN__
    return wallMs - fStartMs;
#else
    return fClock.sample(wallMs);
#endif
  }

  void applyEvent(const Event &ev) {
    // Enough to react and to paint the reaction. Anything that keeps moving
    // after that -- an eased hover, a scroll gliding to a stop -- owes itself
    // the frames it needs, and stops owing them when it settles.
    this->oweFrames(3);
    switch (ev.fType) {
    case EventType::kResize:
      this->resize(ev.fA, ev.fB);
      break;
    case EventType::kCursorMove:
      fMouseX = ev.fX;
      fMouseY = ev.fY;
      if (fSettingsPanel.dragging()) {
        this->dragSetting(ev.fX);
      }
      if (fFilter.dragging()) {
        this->dragFilterRange(ev.fX);
      }
      if (fPanelDragging) {
        this->panelListDrag(ev.fX);
      }
      if (fState == State::kPlaying && !fAutoplay) {
        fCursor = this->cursorFromEvent(ev);
        this->submitTimed({this->eventGameTime(ev.fWallMs), fCursor,
                           osu::InputAction::kMove});
      }
      break;
    case EventType::kScroll:
      if (fSettingsPanel.open()) {
        this->scrollSettings(ev.fX);
        break;
      }
      if (this->panelListActive()) {
        fPanelScrollTarget -= ev.fX * 120.0f;
        fPanelFreeScroll = true;
        break;
      }
      if (fState == State::kSongSelect) {
        // Free scrolling, like lazer's carousel: the wheel moves the view,
        // the selection stays put until the user picks something else.
        fCarousel.scroll(ev.fX);
      } else if (fState == State::kDownload) {
        if (fSetPage.open()) {
          fSetPage.scroll(ev.fX);
        } else {
          fListing.scroll(ev.fX);
        }
      }
      break;
    case EventType::kChar:
      // Before the screens, not after them: song select answers first and
      // puts everything into its filter, so a check further down was never
      // reached. The dialog is the frontmost thing while it is up, and it
      // closes when the screen changes, so this cannot eat a filter's letters
      // from somewhere else.
      if (fExportDialog.open()) {
        fExportDialog.typeInSize(static_cast<char>(ev.fA));
        break;
      }
      if (fState == State::kSongSelect) {
        if (fSwallowChar) {
          fSwallowChar = false;
          break;
        }
        std::string utf8;
        this->appendUtf8(utf8, static_cast<std::uint32_t>(ev.fA));
        fFilter.appendText(utf8);
        fFilterDirty = true;
        break;
      }
      if (fState == State::kDownload) {
        if (fSwallowChar || fSetPage.open()) {
          fSwallowChar = false;
          break;
        }
        this->appendUtf8(fListing.filters().fQuery,
                         static_cast<std::uint32_t>(ev.fA));
        fListing.queryEdited();
        fListing.scrollToStart(); // onTypingStarted
      }
      break;
    case EventType::kKey:
      this->applyKey(ev);
      break;
    case EventType::kMouseButton:
      this->applyMouseButton(ev);
      break;
    }
  }

  // A held key repeats where repeating is what the key means -- deleting a
  // character, stepping through a list -- and not where a second press would
  // undo the first. Gameplay is not in either group: a key held down is one
  // press there, however long it is held.
  [[nodiscard]] static bool repeatable(int key) {
    return key == glfw::kKeyBackspace || key == glfw::kKeyDelete ||
           key == glfw::kKeyLeft || key == glfw::kKeyRight ||
           key == glfw::kKeyUp || key == glfw::kKeyDown ||
           key == glfw::kKeyPageUp || key == glfw::kKeyPageDown;
  }

  void applyKey(const Event &ev) {
    const int key = ev.fA;
    int action = ev.fB;
    if (action == glfw::kRepeat) {
      if (fState == State::kPlaying || !repeatable(key)) {
        return;
      }
      action = glfw::kPress; // a repeat is a press to everything that repeats
    }
    // GLFW reports the modifier state with every key event; tracking press
    // and release of the control keys separately loses sync whenever focus
    // changes while held.
    const bool ctrl = (static_cast<int>(ev.fX) & glfw::kModControl) != 0;
    if (key == glfw::kKeyLeftControl || key == glfw::kKeyRightControl) {
      return;
    }
    // Backspace edits the size while the dialog is up, for the same reason.
    if (fExportDialog.open() && action == glfw::kPress &&
        key == glfw::kKeyBackspace) {
      fExportDialog.backspaceSize();
      return;
    }
    if (action == glfw::kPress && key == glfw::kKeyO && ctrl) {
      this->toggleSettings();
      return;
    }
    if (action == glfw::kPress && key == glfw::kKeyEscape) {
      if (fConfirmDelete) {
        fConfirmDelete = false;
        fConfirmScene.reset();
        return;
      }
      if (fExportDialog.open()) {
        fExportDialog.close();
        return;
      }

      if (fReplayListOpen) {
        fReplayListOpen = false;
        return;
      }
      if (fSettingsPanel.open()) {
        this->closeSettings();
        return;
      }
      if (fModSelect.open()) {
        fModSelect.close();
        return;
      }
    }
    if (action != glfw::kPress && fState != State::kPlaying) {
      return; // menus only care about presses
    }

    // The browser covers the screen, so it takes keys before the screen under
    // it does -- otherwise the arrows would move the carousel behind it.
    if (fReplayListOpen) {
      this->keyReplayList(key);
      return;
    }

    switch (fState) {
    case State::kMainMenu:
      this->keyMainMenu(key);
      return;
    case State::kSongSelect:
      this->keySongSelect(key);
      return;
    case State::kDownload:
      this->keyDownload(key);
      return;
    case State::kPaused:
      // GameplayMenuOverlay: the arrows cycle the buttons, enter takes the
      // selected one, and back is the first button -- Continue.
      if (key == glfw::kKeyEscape) {
        this->resumeGame();
      } else if (key == glfw::kKeyUp) {
        fPauseMenu.selectPrevious();
      } else if (key == glfw::kKeyDown) {
        fPauseMenu.selectNext();
      } else if (key == glfw::kKeyEnter) {
        this->applyPauseAction(fPauseMenu.triggerSelected());
      }
      return;
    case State::kResults:
      if (key == glfw::kKeyEscape) {
        this->quitToSelect();
      } else if (key == glfw::kKeyEnter) {
        this->retry();
      }
      return;
    case State::kPlaying:
      break;
    }

    // Playing.
    if (key == glfw::kKeyEscape) {
      if (action == glfw::kPress) {
        this->pauseGame();
      }
      return;
    }
    if (fAutoplay) {
      return;
    }
    std::uint32_t bit = 0;
    if (key == glfw::kKeyZ) {
      bit = 1u << 0;
    } else if (key == glfw::kKeyX) {
      bit = 1u << 1;
    } else if (key == glfw::kKeySpace) {
      bit = 1u << 2;
    } else {
      return;
    }
    this->applyButton(bit, action == glfw::kPress, ev.fWallMs);
  }

  void keySongSelect(int key) {
    const int nSets = static_cast<int>(fVisible.size());
    if (key == glfw::kKeyEscape) {
      if (!fFilter.text().empty()) {
        fFilter.clearText();
        fFilterDirty = true;
        return;
      }
      this->switchState(State::kMainMenu);
      return;
    }
    if (key == glfw::kKeyBackspace) {
      if (!fFilter.text().empty()) {
        fFilter.popText();
        fFilterDirty = true;
      }
      return;
    }
    if (key == glfw::kKeyF3) {
      this->cycleSortMode();
      return;
    }
    // lazer's song select keeps the search box permanently focused: every
    // printable key belongs to the query, never to a shortcut. Actions live
    // on function keys and the footer buttons.
    if (key == glfw::kKeyF1) {
      this->toggleMods(); // lazer: F1 is mod select
      return;
    }
    if (key == glfw::kKeyF4) {
      this->openDownloads();
      return;
    }
    if (key == glfw::kKeyF5) {
      this->importOsz();
      return;
    }
    if (key == glfw::kKeyF2) {
      this->selectRandom();
      return;
    }
    if (nSets == 0) {
      return;
    }
    const int nDiffs = static_cast<int>(this->infosFor(fSelSet).size());
    if (key == glfw::kKeyUp) {
      if (fSelDiff > 0) {
        --fSelDiff;
      } else if (const int pos = this->visiblePos(); pos > 0) {
        fSelSet = fVisible[static_cast<std::size_t>(pos - 1)];
        fSelDiff = std::max(0, static_cast<int>(this->infosFor(fSelSet).size()) - 1);
      }
    } else if (key == glfw::kKeyDown) {
      if (fSelDiff + 1 < nDiffs) {
        ++fSelDiff;
      } else if (const int pos = this->visiblePos();
                 pos >= 0 && pos + 1 < static_cast<int>(fVisible.size())) {
        fSelSet = fVisible[static_cast<std::size_t>(pos + 1)];
        fSelDiff = 0;
      }
    } else if (key == glfw::kKeyLeft) {
      if (const int pos = this->visiblePos(); pos > 0) {
        fSelSet = fVisible[static_cast<std::size_t>(pos - 1)];
        fSelDiff = 0;
      }
    } else if (key == glfw::kKeyRight) {
      if (const int pos = this->visiblePos();
          pos >= 0 && pos + 1 < static_cast<int>(fVisible.size())) {
        fSelSet = fVisible[static_cast<std::size_t>(pos + 1)];
        fSelDiff = 0;
      }
    } else if (key == glfw::kKeyEnter) {
      this->startPlay(fSelSet, fSelDiff);
    }
  }

  void openDownloads() {
    fSwallowChar = true;
    this->switchState(State::kDownload);
    if (fFound.empty() && !fSearchPending) {
      this->startSearch(); // the listing opens on results, not a blank page
    }
  }

  void keyDownload(int key) {
    if (key == glfw::kKeyEscape) {
      if (fSetPage.open()) {
        // The page covered the listing; what it uncovers has to be repainted,
        // and the listing has no way of knowing it was ever covered.
        fSetPage.close(); // back to the listing, as the overlay stacks
        this->damageAll("beatmap page closed");
        return;
      }
      fPreview.stop();
      fPreviewId = -1;
      this->restoreMusic();
      this->switchState(State::kSongSelect);
      return;
    }
    if (key == glfw::kKeyEnter) {
      this->startSearch();
      return;
    }
    if (key == glfw::kKeyBackspace) {
      this->popUtf8(fListing.filters().fQuery);
      fListing.queryEdited();
    }
  }

  void keyReplayList(int key) {
    if (key == glfw::kKeyLeft && fSelectedPanel > 0) {
      --fSelectedPanel;
      fPanelFreeScroll = false;
    } else if (key == glfw::kKeyRight &&
               fSelectedPanel + 1 < static_cast<int>(fPanelEntries.size())) {
      ++fSelectedPanel;
      fPanelFreeScroll = false;
    } else if (key == glfw::kKeyEnter) {
      this->watchSelectedReplay();
    }
  }

  void applyMouseButton(const Event &ev) {
    const int button = ev.fA;
    const int action = ev.fB;

    if (fState == State::kMainMenu || fState == State::kSongSelect ||
        fState == State::kDownload || fState == State::kPaused ||
        fState == State::kResults) {
      if (button == glfw::kMouseButtonLeft) {
        const bool pressed = action == glfw::kPress;
        // The panel strip takes the press before anything else so that a drag
        // that starts on a panel scrolls the list instead of selecting.
        if (this->panelListActive() && !fExportDialog.open() &&
            !fSettingsPanel.open() && !fModSelect.open() &&
            this->panelListClick(fMouseX, fMouseY, pressed)) {
          return;
        }
        if (pressed) {
          this->clickAt(fMouseX, fMouseY);
        } else {
          this->settingsClick(fMouseX, fMouseY, false);
          this->filterClick(fMouseX, fMouseY, false);
        }
      }
      return;
    }

    if (fAutoplay) {
      return;
    }
    std::uint32_t bit = 0;
    if (button == glfw::kMouseButtonLeft) {
      bit = 1u << 3;
    } else if (button == glfw::kMouseButtonRight) {
      bit = 1u << 4;
    } else {
      return;
    }
    this->applyButton(bit, action == glfw::kPress, ev.fWallMs);
  }

  void clickAt(float x, float y) {
    if (this->confirmDeleteClick(x, y)) {
      return;
    }
    if (this->exportClick(x, y)) {
      return;
    }
    if (fReplayListOpen) {
      // The strip handled anything over it; the actions below it are ours.
      for (const auto &b : fMenuButtons) {
        if (b.fRect.contains(x, y)) {
          this->exportSelectedReplay();
          return;
        }
      }
      return;
    }
    if (this->settingsClick(x, y, true)) {
      return;
    }
    if (fModSelect.open() && this->modClick(x, y)) {
      return;
    }
    switch (fState) {
    case State::kMainMenu: {
      this->ensureMenuButtons();
      const int piece = fMenu.hit(x, y);
      if (piece == client::mainmenu::Menu::kLogo) {
        // The logo's box is square, reaches as far as its visualiser does,
        // and the logo is a circle inside it -- so the circle has the last
        // word, and a miss falls through to the buttons the box covers.
        const float dx = x - fLogoRect.centerX();
        const float dy = y - fLogoRect.centerY();
        const float r = fLogoRect.width() * 0.5f;
        if (r > 0.0f && dx * dx + dy * dy <= r * r) {
          this->triggerLogo();
          return;
        }
        for (auto &b : fMenuBtns) {
          if (b.fVisible == fMenuState && b.fExpand > 0.5f &&
              b.fRect.contains(x, y)) {
            this->menuTrigger(b);
            return;
          }
        }
      } else if (piece >= 0 &&
                 piece < static_cast<int>(fMenuBtns.size())) {
        auto &b = fMenuBtns[static_cast<std::size_t>(piece)];
        if (b.fVisible == fMenuState && b.fExpand > 0.5f) {
          this->menuTrigger(b);
          return;
        }
      }
      break;
    }
    case State::kSongSelect:
      if (this->filterClick(x, y, true)) {
        return;
      }
      if (this->optionsClick(x, y)) {
        return;
      }
      if (const auto hit = fCarousel.click(x, y); hit.fHit) {
        if (hit.fDiff < 0) {
          fSelSet = hit.fSet;
          fSelDiff = 0;
        } else if (fSelSet == hit.fSet && fSelDiff == hit.fDiff) {
          this->startPlay(hit.fSet, hit.fDiff); // second click plays
        } else {
          fSelSet = hit.fSet;
          fSelDiff = hit.fDiff;
        }
        return;
      }
      break;
    case State::kDownload: {
      if (fSetPage.open()) {
        const auto page = fSetPage.click(x, y);
        using PageAction = client::setpage::SetPage::Action;
        if (page.fAction == PageAction::kDownload) {
          this->startDownloadForSet(fSetPage.setId());
        } else if (page.fAction == PageAction::kPreview) {
          this->togglePreviewForSet(fSetPage.setId());
        }
        return; // the page covers the listing underneath
      }
      const auto result = fListing.click(x, y);
      switch (result.fAction) {
      case client::listing::Listing::Action::kSearch:
        this->startSearch();
        break;
      case client::listing::Listing::Action::kDownload:
        this->startDownload(result.fIndex);
        break;
      case client::listing::Listing::Action::kOpen:
        if (result.fIndex < fFound.size()) {
          fSetPage.show(fFound[result.fIndex]);
          this->requestPageCover(result.fIndex);
        }
        break;
      case client::listing::Listing::Action::kPreview:
        this->togglePreview(result.fIndex);
        break;
      case client::listing::Listing::Action::kRefilter:
      case client::listing::Listing::Action::kNone:
        break;
      }
      return;
    }
    case State::kPaused:
      this->applyPauseAction(fPauseMenu.click(x, y));
      break;
    case State::kResults:
      for (std::size_t i = 0; i < fMenuButtons.size(); ++i) {
        if (fMenuButtons[i].fRect.contains(x, y)) {
          this->menuButtonPressed(i);
          return;
        }
      }
      break;
    default:
      break;
    }
  }

  void applyPauseAction(client::pause::PauseMenu::Action action) {
    using Action = client::pause::PauseMenu::Action;
    switch (action) {
    case Action::kContinue:
      this->resumeGame();
      break;
    case Action::kRetry:
      this->retry();
      break;
    case Action::kQuit:
      this->quitToSelect();
      break;
    case Action::kNone:
      break;
    }
  }

  // The results screen is the last one still drawing its own buttons; the
  // pause overlay reports what was pressed instead.
  void menuButtonPressed(std::size_t idx) {
    if (fState == State::kResults) {
      if (idx == 0) {
        this->retry();
      } else if (idx == 1) {
        this->quitToSelect();
      } else {
        fExportDialog.show();
        fReplayListOpen = false; // one overlay at a time
      }
    }
  }

  static void appendUtf8(std::string &out, std::uint32_t cp) {
    if (cp < 0x20) {
      return; // control chars
    }
    if (cp < 0x80) {
      out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
      out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
      out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
      out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
      out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
      out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
      out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
  }

  static void popUtf8(std::string &out) {
    while (!out.empty()) {
      const auto c = static_cast<unsigned char>(out.back());
      out.pop_back();
      if ((c & 0xC0) != 0x80) {
        break; // removed the lead byte
      }
    }
  }

  // Every physical press attempts a hit (each of Z/X/M1/M2 is an independent
  // trigger, which is what alternating taps need); a release is forwarded to
  // the engine only when the *last* held trigger goes up, so slider tracking
  // survives overlapped taps. The old code collapsed all triggers into one
  // boolean, losing presses that overlapped a held key.
  void applyButton(std::uint32_t bit, bool down, double wallMs) {
    const double t = this->eventGameTime(wallMs);
    if (down) {
      fHeldMask |= bit;
      this->submitTimed({t, fCursor, osu::InputAction::kPress});
    } else {
      fHeldMask &= ~bit;
      if (fHeldMask == 0) {
        this->submitTimed({t, fCursor, osu::InputAction::kRelease});
      }
    }
  }

  void submitTimed(const osu::InputEvent &ev) {
    // Guard: input events can be drained in a frame where the engine isn't
    // live yet (a queued press arriving during a state transition, before
    // startGameplay populated fEngine). Dereferencing the empty optional was
    // the SIGILL in applyButton.
    if (!fEngine) {
      return;
    }
    fEngine->submit(ev);
    // Always record: the events are a few bytes each, and the autosave
    // setting decides afterwards whether they are kept. Gating the recording
    // itself on --record meant automatic replays were always empty.
    fRecordedEvents.push_back(ev);
  }

  [[nodiscard]] static const char *stateName(State st) {
    switch (st) {
    case State::kMainMenu: return "main-menu";
    case State::kSongSelect: return "song-select";
    case State::kDownload: return "download";
    case State::kPlaying: return "playing";
    case State::kPaused: return "paused";
    case State::kResults: return "results";
    }
    return "?";
  }

  void switchState(State st) {
    // The transition fades over 240 ms and asks for its own frames after
    // that; what a new screen loads arrives as damage from a callback.
    this->oweFrames(4);
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
    this->damageAll("screen change"); // the new screen owns every pixel
    std::println(std::cerr, "[ui] {} -> {}", stateName(fState), stateName(st));
    fState = st;
    fStateEnterWall = wallMs();
    if (st == State::kResults) {
      // The side panels are the other replays for this beatmap.
      this->scanReplays();
    }
    if (st == State::kMainMenu) {
      // Returning to the menu always lands on the top level, never on a
      // stale submenu, and the logo re-eases into place from where it was.
      fMenuState = MenuState::kTopLevel;
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


  // A frame is only drawn when there is a reason to: an event arrived,
  // something is animating, a transfer is running, or the safety interval
  // elapsed. A menu nobody is touching costs a poll and a sleep, which is
  // how a compositor treats a window that has not damaged itself.
  void requestRedraw(double durationMs = 0.0) {
    fRedrawUntilWall = std::max(fRedrawUntilWall, wallMs() + durationMs);
  }

  // Which footer element the pointer is on, and where that element is. The
  // footer draws a highlight on exactly one of them at a time.
  [[nodiscard]] std::pair<int, skia::SkRect> footerHot() const {
    const skia::SkRect chips[] = {fBackChip, fModsChip, fRandomChip,
                                  fOptionsChip};
    for (int i = 0; i < 4; ++i) {
      if (chips[static_cast<std::size_t>(i)].contains(fMouseX, fMouseY)) {
        return {i + 1, chips[static_cast<std::size_t>(i)]};
      }
    }
    for (std::size_t i = 0; i < fOptionHits.size(); ++i) {
      if (fOptionHits[i].contains(fMouseX, fMouseY)) {
        return {100 + static_cast<int>(i), fOptionHits[i]};
      }
    }
    return {0, skia::SkRect::MakeEmpty()};
  }

  // A text caret is shown for 600 ms of every 1000; this is when it next
  // changes, which is when a frame is next worth drawing for it.
  [[nodiscard]] static double nextCaretFlip(double nowMs) {
    const double phase = std::fmod(nowMs, 1000.0);
    return nowMs + (phase < 600.0 ? 600.0 - phase : 1000.0 - phase);
  }

  // Promises a number of frames. Two is the useful minimum: the first lets
  // whatever moved react and mark what it touched, the second paints that.
  // This is what an event or an eased value owes -- rather than a blanket
  // "keep drawing for the next 400 ms", which is how a listing nobody was
  // touching ended up repainting at the refresh rate.
  void oweFrames(int frames) { fFramesOwed = std::max(fFramesOwed, frames); }

  // A screen that knows when it next changes by itself -- a caret blinking on
  // its own clock -- says so, and sleeps until then instead of keeping frames
  // coming in the hope of catching the moment.
  void wakeAt(double wall) {
    fWakeWall = fWakeWall <= 0.0 ? wall : std::min(fWakeWall, wall);
  }

  // Damage marked while a frame is being drawn says what to repaint; it does
  // not ask for another frame. That distinction is what lets a screen repaint
  // itself whole every time it draws -- most of them still do -- without that
  // becoming a reason to draw again, for ever. Damage marked outside drawing,
  // by an event or by work finishing in the background, does ask.

  // The whole screen has to be repainted: a screen changed, the window
  // resized, or something that does not report its bounds moved.
  void damageAll(const char *reason = "unspecified") {
    fFullDamage = true;
    // The window is cycling through several buffers, and a repaint lands in
    // exactly one of them: the others still hold what was on screen before.
    // So "everything changed" is a statement about all of them, and is owed
    // one full repaint each. Getting this from a screen change used to be an
    // accident of asking for 1500 ms of frames afterwards -- a hundred full
    // repaints to be sure of three -- and it stopped being an accident when
    // that went away, which is how coming back to the menu started leaving
    // the screen it was called from underneath.
    // With a real buffer age each frame works out for itself how far back it
    // has to repaint, so the margin is only for the case where nobody says.
    fFullRepaintsOwed = fBufferAge >= 0 ? 0 : kFullRepaintsAfterChange;
    if (!fDrawing) {
      fDamageDrives = true;
      this->oweFrames(kFullRepaintsAfterChange + 1);
    }
    fFullDamageReason = reason;
    fDamage.clear();
  }

  // A region did. Rounded outwards, because a rectangle that is a pixel too
  // small leaves a seam behind.
  void damage(const skia::SkRect &rect) {
    if (fFullDamage || rect.isEmpty()) {
      return;
    }
    if (!fDrawing) {
      fDamageDrives = true;
    }
    skia::SkIRect area = rect.roundOut();
    area.outset(2, 2);

    // Merge into a rectangle it already touches, so a widget that reports
    // itself every frame does not add a new entry every frame.
    for (auto &existing : fDamage) {
      skia::SkIRect probe = existing;
      probe.outset(2, 2);
      if (skia::SkIRect::Intersects(probe, area)) {
        existing.join(area);
        return;
      }
    }
    // Merging early keeps the clip a couple of rectangles rather than a
    // region with many: every draw call is tested against it, and that is
    // paid per call, not per pixel.
    for (auto &existing : fDamage) {
      skia::SkIRect merged = existing;
      merged.join(area);
      const auto mergedArea = static_cast<std::int64_t>(merged.width()) *
                              merged.height();
      const auto separate = static_cast<std::int64_t>(existing.width()) *
                                existing.height() +
                            static_cast<std::int64_t>(area.width()) *
                                area.height();
      if (mergedArea <= separate * 5 / 4) {
        existing = merged;
        return;
      }
    }
    if (fDamage.size() < kMaxDamageRects) {
      fDamage.push_back(area);
      return;
    }

    // Full: fold it into whichever rectangle grows least by taking it, which
    // keeps the list short without swallowing the screen.
    std::size_t best = 0;
    std::int64_t bestCost = std::numeric_limits<std::int64_t>::max();
    for (std::size_t i = 0; i < fDamage.size(); ++i) {
      skia::SkIRect merged = fDamage[i];
      merged.join(area);
      const auto cost = static_cast<std::int64_t>(merged.width()) *
                            merged.height() -
                        static_cast<std::int64_t>(fDamage[i].width()) *
                            fDamage[i].height();
      if (cost < bestCost) {
        bestCost = cost;
        best = i;
      }
    }
    fDamage[best].join(area);
  }

  // Clips the frame to what changed. Everything draws exactly as it would
  // have -- the screens repaint from state, so a clipped repaint of a region
  // is the same pixels -- only the work outside the clip is skipped.
  void beginFrame() {
    fDrawing = true;
    fBlitRegions.clear(); // empty means the whole surface goes over
    // How old the contents of the buffer being drawn into are, from the
    // window system rather than from a constant of mine. -1 when nobody will
    // say, which is when the constants come back.
    fBufferAge = this->partialRedraw() ? present::bufferAge() : -1;
    // Nobody will say, but the answer may still be knowable: a driver that
    // swaps by copying leaves the back buffer holding the last frame, which
    // is an age of one, and a repaint of this frame's damage alone. Asserted
    // rather than guessed, because getting it wrong looks like smearing.
    if (fBufferAge < 0 && this->partialRedraw()) {
      const int assumed = fForcedBufferAge > 0 ? fForcedBufferAge
                                               : fSettings.choice("bufferage");
      if (assumed > 0) {
        fBufferAge = assumed;
        fBufferAgeAssumed = true;
      }
    } else {
      fBufferAgeAssumed = false;
    }
    // Take the accumulator as this frame's damage and hand a fresh one to the
    // screens, which fill it in as they draw for the frame after this.
    //
    // A full repaint still owed to a buffer that has not had one.
    if (fFullRepaintsOwed > 0) {
      --fFullRepaintsOwed;
      if (!fFullDamage) {
        fFullDamage = true;
        fFullDamageReason = "buffer has not had this screen yet";
      }
    }
    // What the frame repaints. A frame drawn with nothing marked repaints
    // everything -- the buffer being drawn into is several frames old and
    // there is no region to trust -- so it is reported as full, honestly.
    // The answer to those frames is not to relabel them: it is not to draw
    // them, which is what the owed-frame count above is for.
    if (!fFullDamage && fDamage.empty()) {
      fFullDamageReason = "nothing marked itself";
    }
    fComputedClipFull = fFullDamage || fDamage.empty();
    fComputedClip = fDamage;
    fFrameClipFull = fComputedClipFull;
    fFrameClip.clear();
    if (!fFrameClipFull) {
      fFrameClip = fDamage;
    }
    fDamage.clear();
    fFullDamage = false;
    fDamageDrives = false;

    // Showing the regions means repainting everything: the outlines are put
    // on a back buffer the window will come back to, and cleaning them up
    // afterwards would have to land on that same buffer rather than on
    // whichever one is next. Repainting whole sidesteps that entirely -- the
    // tool costs frames while it is on, and in exchange it never lies or
    // leaves anything behind.
    if (this->showingDamage()) {
      fFrameClipFull = true;
      fFrameClip.clear();
    }
    this->rememberBlitRegion();

    if (!fSurface) {
      return;
    }
    auto *canvas = fSurface->getCanvas();
    fFrameSave = canvas->save();

    // Partial repainting draws straight into the window: there is no second
    // surface and nothing is copied. The buffer being drawn into is one of
    // several the window cycles through, so it is missing everything the last
    // few frames changed.
    //
    // Clipped to one rectangle rather than to a region of several: a region
    // takes Skia off its analytic clip path and onto clip masks, which are
    // paid per draw call and cost more than the pixels they save.
    if (!this->partialRedraw() || this->historyShorterThan(this->drawReach())) {
      return;
    }
    const skia::SkIRect bounds = this->damageOver(this->drawReach());
    if (bounds.isEmpty()) {
      return;
    }
    // Past half the screen the clip stops paying for itself: every draw call
    // is recorded and tested either way, and only pixels are saved.
    const std::int64_t screenArea =
        static_cast<std::int64_t>(fScreenW) * fScreenH;
    const std::int64_t boundsArea =
        static_cast<std::int64_t>(bounds.width()) * bounds.height();
    if (boundsArea * 2 > screenArea) {
      return;
    }
    canvas->clipIRect(bounds);
    // Remembered for the blit, which is a different question with a different
    // answer: what the window is missing rather than what this surface is.
    if (!this->historyShorterThan(this->windowReach())) {
      const skia::SkIRect carry = this->damageOver(this->windowReach());
      if (!carry.isEmpty()) {
        fBlitRegions.push_back(carry);
      }
    }
    // What is repainted starts clean: the buffer holds an older frame, and
    // anything translucent drawn over it would otherwise stack up.
    if (fState != State::kPlaying) {
      canvas->clear(skia::colorSetARGB(255, 0, 0, 0));
    }
  }

  // The half of a screen that is not drawing: what is in the tree, where it
  // sits, what the pointer is over. Damage marked here is marked before the
  // frame begins, so it is a reason to draw rather than a note about what to
  // repaint next time.
  void updateScreens() {
    // The settings panel floats over whatever screen is up, and the screen
    // underneath has no idea it is there. It says what it repaints -- a
    // sidebar whose indicator is easing, an option list following the
    // pointer, the column while it scrolls -- and says nothing at all while
    // it is open and untouched, which is most of the time.
    if (fSettingsPanel.visible()) {
      this->damage(fSettingsPanel.update(
          fFont, fSettings,
          {fScreenW, fScreenH, fMouseX, fMouseY, wallMs(), fUiDt}));
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
      this->damageAll("screen fade");
    }

    // The overlays that cover the screen say what they repaint here, before
    // the frame is committed to. Said while drawing -- which is where this
    // lived -- it describes a frame that has already been declined, and the
    // screen it belongs to freezes: the strip of panels easing to the one
    // that was picked marked itself for a frame that was never drawn, and so
    // never advanced, and so kept marking itself.
    if (fModSelect.visible()) {
      const skia::SkRect region = fModSelect.damageFor(
          {fScreenW, fScreenH, fMouseX, fMouseY, fUiDt});
      if (region.width() >= static_cast<float>(fScreenW)) {
        this->damageAll("mod select sliding");
      } else {
        this->damage(region);
      }
    }
    if (fReplayListOpen) {
      if (std::abs(fPanelScroll - fPanelScrollTarget) > 0.05f ||
          fPanelScroll != fDrawnPanelScroll) {
        // Dragged or gliding: a drag sets the position outright, so the
        // target says nothing about it. Where it was when it was last drawn
        // does.
        fDrawnPanelScroll = fPanelScroll;
        this->damageAll("replay browser moving");
      } else {
        int hot = -1;
        for (std::size_t i = 0; i < fPanelHits.size(); ++i) {
          if (fPanelHits[i].fRect.contains(fMouseX, fMouseY)) {
            hot = static_cast<int>(i);
            break;
          }
        }
        if (hot != fHotReplayPanel) {
          fHotReplayPanel = hot;
          this->damage(fPanelBand);
        }
      }
    }

    // Overlays are drawn after the screen, over most of it, and none of them
    // declares a region -- so while one is up, and on the frame it goes away,
    // the screen underneath cannot be trusted to have marked enough.
    const bool overlay = fSettingsPanel.visible() || fModSelect.visible() ||
                         fExportDialog.open() || fReplayListOpen ||
                         fConfirmDelete || fSetPage.open();
    // Only while one is moving, or on the frame it appears or goes away: a
    // settled overlay is as static as the screen under it, and the screens do
    // mark what they change beneath it.
    if (overlay != fOverlayShown) {
      this->damageAll("overlay appeared or went away");
    } else if (fSettingsPanel.animating(wallMs()) || fModSelect.animating()) {
      this->damageAll("overlay sliding");
    } else if (fExportDialog.open()) {
      // Neither the window nor even the whole box: what is behind the dim
      // does not change while the dialog is up, and while a video is being
      // written the only thing that moves is the per cent on the status line.
      // The box itself is repainted for the pointer, whose buttons light up.
      const skia::SkRect box =
          client::ExportDialog::bounds(fScreenW, fScreenH);
      const int hot = fExportDialog.hotElement(fMouseX, fMouseY);
      if (hot != fHotDialogPiece || fExportDialog.takeEdited()) {
        fHotDialogPiece = hot;
        this->damage(box);
      } else if (fExportDialog.takeStatusChanged()) {
        this->damage(client::ExportDialog::statusBounds(fScreenW, fScreenH));
      }
    }
    fOverlayShown = overlay;
  }

  // Whether the frame that was asked for would put anything new on screen.
  // Only a screen that reports its damage can be asked: for the others,
  // "nothing is marked" means "nobody was asked", not "nothing changed".
  [[nodiscard]] bool nothingToPaint() {
    if (fFullDamage || !fDamage.empty() || fFullRepaintsOwed > 0) {
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
    if (fConfirmDelete && fConfirmScene && fConfirmScene->animatingTree()) {
      return false;
    }
    // A preview still being fetched has nothing on screen to show for it
    // yet; once it plays, the ring marks the card it is on.
    if (fPreviewPending) {
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
    return fMouseX != fDrawnMouseX || fMouseY != fDrawnMouseY;
  }

  [[nodiscard]] bool needsFrame() {
    // Gameplay is a moving picture by definition, and so is anything with a
    // clock on screen.
    if (fState == State::kPlaying) {
      return true; // a moving picture by definition
    }
    // The results screen counts up, slides its panels and fades in, all of
    // which end. After that it is a still picture like any other.
    if (fState == State::kResults &&
        (wallMs() - fStateEnterWall < 2500.0 ||
         std::abs(fPanelScroll - fPanelScrollTarget) > 0.05f)) {
      return true;
    }
    // The logo tracks the music and the triangles drift, and either is a
    // reason to keep drawing. Neither is a reason to stop: everything below
    // -- an event, a panel sliding, something that marked itself -- still
    // applies, and returning an answer here rather than a reason took the
    // whole menu out of the conversation.
    if (fState == State::kMainMenu &&
        (fSettings.flag("visualiser") || fSettings.flag("menutriangles"))) {
      return true;
    }
    if (fState == State::kPaused && fSettings.flag("pausetriangles")) {
      return true; // triangles drift inside the buttons, as lazer's do
    }
    if (fSearchPending || fPreviewPending || !fTransfers.empty()) {
      return true; // progress that is being watched
    }
    if (fPreviewId >= 0 || fExportDialog.open()) {
      return true; // a transfer or a dialog with live status in it
    }
    // The replay browser's strip glides to the panel that was picked. It is
    // drawn as an overlay rather than as a screen, so it has nobody else to
    // ask for the frames that carry it there.
    if (fReplayListOpen &&
        std::abs(fPanelScroll - fPanelScrollTarget) > 0.05f) {
      return true;
    }
    // Overlays are drawn while they slide in and out, and after that only
    // when something touches them -- which arrives as an event.
    if (fSettingsPanel.animating(wallMs()) || fModSelect.animating()) {
      return true;
    }
    if (fConfirmDelete && fConfirmScene && fConfirmScene->animatingTree()) {
      return true;
    }
    // Scene trees know whether anything in them is still moving, which is the
    // one thing they cannot express as damage in advance.
    if (fState == State::kDownload &&
        (fListing.animating() || fSetPage.animating())) {
      return true;
    }
    if (fState == State::kSongSelect && fCarousel.animating()) {
      return true;
    }
    const double now = wallMs();
    if (fFramesOwed > 0) {
      --fFramesOwed;
      return true;
    }
    if (fWakeWall > 0.0 && now >= fWakeWall) {
      fWakeWall = 0.0;
      return true;
    }
    // Something marked itself outside a frame: an event handler, or work
    // that finished in the background. Damage marked while drawing only says
    // what to repaint when a frame next happens -- a screen that repaints
    // itself whole every time it draws would otherwise be asking for the next
    // frame, every frame, for ever.
    if (fDamageDrives) {
      return true;
    }
    if (now <= fRedrawUntilWall) {
      return true;
    }
    // Safety net: whatever the screens forgot to announce shows up within
    // this long rather than never.
    return now - fLastDrawWall > 500.0;
  }

  // The part of the window that a screen is actually showing, in the window's
  // own coordinates. Empty when nothing is known about the placement.
  [[nodiscard]] skia::SkIRect visiblePortion() const {
    const int x = fWindowX.load(std::memory_order_acquire);
    const int y = fWindowY.load(std::memory_order_acquire);
    const int areaX = fWorkAreaX.load(std::memory_order_acquire);
    const int areaY = fWorkAreaY.load(std::memory_order_acquire);
    const int areaW = fWorkAreaW.load(std::memory_order_acquire);
    const int areaH = fWorkAreaH.load(std::memory_order_acquire);
    if (areaW <= 0 || areaH <= 0 || fScreenW <= 0 || fScreenH <= 0) {
      return skia::SkIRect::MakeEmpty();
    }
    skia::SkIRect window = skia::SkIRect::MakeXYWH(x, y, fScreenW, fScreenH);
    const skia::SkIRect area =
        skia::SkIRect::MakeXYWH(areaX, areaY, areaW, areaH);
    if (!window.intersect(area)) {
      return skia::SkIRect::MakeEmpty();
    }
    // Back into the window's own coordinates, and grown a little: the edge
    // between shown and hidden is not worth being exact about.
    window.offset(-x, -y);
    window.outset(4, 4);
    return window;
  }

  // Called on the event thread, where asking about windows and monitors is
  // allowed; the render thread reads the numbers.
  void noteWindowPlacement() {
    int x = 0, y = 0;
    glfw::glfwGetWindowPos(fWindow, &x, &y);
    fWindowX.store(x, std::memory_order_release);
    fWindowY.store(y, std::memory_order_release);
    if (const auto monitor = glfw::glfwGetPrimaryMonitor(); monitor != nullptr) {
      int ax = 0, ay = 0, aw = 0, ah = 0;
      glfw::glfwGetMonitorWorkarea(monitor, &ax, &ay, &aw, &ah);
      fWorkAreaX.store(ax, std::memory_order_release);
      fWorkAreaY.store(ay, std::memory_order_release);
      fWorkAreaW.store(aw, std::memory_order_release);
      fWorkAreaH.store(ah, std::memory_order_release);
    }
  }

  void frame() {
    // A screen that returned early last time -- gameplay without a beatmap
    // loaded, say -- could leave this set; outside frame() nothing is drawing
    // by definition.
    fDrawing = false;
    if (fRefreshRequested.exchange(false, std::memory_order_acquire)) {
      // Set on the event thread, acted on here: everything those buffers held
      // is suspect, so the history of what they hold goes with it.
      fBlitHistory.clear();
      // X11 says which rectangles were exposed and GLFW does not pass them
      // on, so the region is reconstructed from where the window sits: the
      // part of it a screen is showing is the part worth painting. Dragging a
      // window back in from off the edge then costs the strip that came back,
      // not the whole window, every frame of the drag.
      const skia::SkIRect onScreen = this->visiblePortion();
      if (onScreen.isEmpty() ||
          (onScreen.width() >= fScreenW && onScreen.height() >= fScreenH)) {
        this->damageAll("the window system asked for a repaint");
      } else {
        this->damage(skia::SkRect::Make(onScreen));
      }
    }
    this->applySwapInterval();
    // Work finishing in the background changes what is on screen, so it is
    // as good a reason to draw as an event.
    if (client::http::poll() > 0 || fLoader.poll() > 0) {
      this->requestRedraw(600.0);
    }
    this->drainDroppedFiles();
    this->drainInput();
    {
      const double wallNow = wallMs();
      fUiDt = fUiPrevWall > 0.0 ? std::min(50.0, wallNow - fUiPrevWall) : 16.0;
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
      this->updateMenuMusic();
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
    if (fExportJob) {
      this->pollExportVideo();
    }
    // Screens built as a scene tree settle before anything is drawn: the
    // pointer lands where it lands, values ease, the layout is redone, and
    // what changed is marked. Only then is it known whether the frame that
    // was asked for has anything to put on screen.
    const auto updateStart = std::chrono::steady_clock::now();
    this->updateScreens();
    fLastUpdateUs = std::chrono::duration_cast<std::chrono::microseconds>(
                        std::chrono::steady_clock::now() - updateStart)
                        .count();
    if (this->nothingToPaint()) {
      // The question the safety net exists to ask has just been asked and
      // answered, so the net does not need to fire.
      fLastDrawWall = wallMs();
#ifndef __EMSCRIPTEN__
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
#endif
      return;
    }
    fLastDrawWall = wallMs();
    fFrameStart = std::chrono::steady_clock::now();

    // The renderer is chosen per frame, because only the UI screens may use
    // the CPU one: gameplay draws precomputed GPU textures.
    // Gameplay draws on the CPU as well when asked to: the slider bodies it
    // needs were computed on the GPU and flattened into memory once, at load,
    // so nothing is read back per frame.
    const bool software =
        fSettings.choice("renderer") == 1 && this->ensureRasterSurface();
    if (software != fDrewOnRaster) {
      this->damageAll("renderer changed");
    }
    fDrewOnRaster = software;
    fSurface = software ? fRasterSurface : fWindowSurface;

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
      this->damageAll("gameplay"); // a moving picture by definition
      break;
    default:
      this->damageAll("screen does not report damage");
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
  void notify(std::string text, skia::SkColor color = client::ui::kAccent2) {
    this->requestRedraw(4500.0); // the toast has to fade out on its own
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
    const client::ui::Painter p(canvas, fFont);
    const float alpha = static_cast<float>(
        std::min(1.0, std::min(age / 200.0, (kLifetimeMs - age) / 400.0)));
    const float w = p.measure(fToast, 14.0f) + 32.0f;
    const skia::SkRect box = skia::SkRect::MakeXYWH(
        static_cast<float>(fScreenW) - w - 20.0f, 20.0f, w, 44.0f);
    p.fillRounded(box, 8.0f, client::ui::kBackground5, alpha * 0.95f);
    p.fillRect(skia::SkRect::MakeXYWH(box.fLeft, box.fTop, 4.0f, box.height()),
               fToastColor, alpha);
    p.textCentered(fToast, box.centerX() + 2.0f, box.centerY() + 5.0f, 14.0f,
                   skia::kWhite, alpha);
    this->damage(box);
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
    const client::ui::Painter p(canvas, fFont);
    const float sw = static_cast<float>(fScreenW);
    const float sh = static_cast<float>(fScreenH);
    const skia::SkRect box =
        skia::SkRect::MakeXYWH(sw - 92.0f, sh - 52.0f, 80.0f, 42.0f);
    // Drawn after the clip is lifted, straight into a buffer that is several
    // frames old, so a translucent background lets the numbers that were
    // there before show through and pile up. Partial redraw is the mode where
    // that happens, and the mode where this has to be opaque.
    p.fillRounded(box, 6.0f,
                  skia::colorSetARGB(this->partialRedraw() ? 255 : 150, 8, 6,
                                     12));
    p.textCentered(std::format("{:.0f} fps", std::round(1000.0 / fFpsFrameMs)),
                   box.centerX(), box.fTop + 18.0f, 15.0f, skia::kWhite);
    p.textCentered(std::format("{:.1f} ms", fFpsFrameMs), box.centerX(),
                   box.fBottom - 8.0f, 12.0f, skia::kWhite, 0.7f);
    this->includeInBlit(box);
  }

  // Something drawn outside the frame's clip still has to be carried into the
  // window when the CPU renderer only carries over what it repainted.
  void includeInBlit(const skia::SkRect &rect) {
    if (fBlitRegions.empty()) {
      return; // the whole surface is going over anyway
    }
    skia::SkIRect area = rect.roundOut();
    area.outset(2, 2);
    if (!area.intersect(skia::SkIRect::MakeWH(fScreenW, fScreenH))) {
      return;
    }
    fBlitRegions.push_back(area);
  }

  void present() {
    const auto frameStart = fFrameStart;
    // Overlays float above whatever screen is drawn.
    auto *canvas = fSurface->getCanvas();
    if (fModSelect.visible()) {
      this->drawModSelect(canvas);
    }
    if (fSettingsPanel.visible()) {
      // What it repaints was worked out before the frame began, by the panel
      // itself; here it is only drawn.
      this->drawSettings(canvas);
    }
    if (fReplayListOpen) {
      this->drawReplayList(canvas);
    }
    if (fExportDialog.open()) {
      this->drawExportDialog(canvas);
    }
    this->drawDeleteConfirmation(canvas);
    this->drawToast(canvas);
    this->showDamage(canvas);
    canvas->restoreToCount(fFrameSave);
    // Drawn after the clip is lifted: a small thing in the far corner that
    // changes every frame, so clipping to it would drag the repainted region
    // across the whole screen, and clipping it away would freeze it. It still
    // has to reach the window, which on the CPU renderer means saying so --
    // otherwise the counter is drawn into a surface whose corner is never
    // carried over, and the number stops.
    this->drawFpsCounter(canvas);
    fBlitStart = std::chrono::steady_clock::now();
    if (fDrewOnRaster && fWindowSurface) {
      // The CPU frame lives in main memory; the window wants it as pixels.
      // Only the part that was repainted is carried over: the window's buffer
      // already holds the rest, under exactly the assumption that let the
      // frame be clipped in the first place. This is what makes the cost of
      // the blit follow the damage instead of being a whole window every
      // frame -- which it was, at a constant millisecond and a half.
      if (auto image = fRasterSurface->makeImageSnapshot()) {
        auto *windowCanvas = fWindowSurface->getCanvas();
        if (fBlitRegions.empty()) {
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
          for (const auto &area : fBlitRegions) {
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
      fContext->flushAndSubmit(fWindowSurface.get());
    } else {
      fContext->flushAndSubmit(fSurface.get());
    }

    fDrawnMouseX = fMouseX;
    fDrawnMouseY = fMouseY;
    const auto beforeSwap = std::chrono::steady_clock::now();
    // What changed since the last frame the compositor was given. Handing it
    // over means it can leave the rest of the window alone instead of taking
    // the whole surface every frame -- the other half of what buffer age
    // buys, and the half that lands on the "swap" line of the frame report.
    std::vector<std::array<int, 4>> damage;
    if (!fComputedClipFull && !fComputedClip.empty()) {
      damage.reserve(fComputedClip.size());
      for (const auto &rect : fComputedClip) {
        damage.push_back({rect.fLeft, rect.fTop, rect.width(), rect.height()});
      }
    }
    if (damage.empty() || !present::swapWithDamage(fScreenH, damage)) {
      glfw::glfwSwapBuffers(fWindow);
    }
    fLastSwapUs = std::chrono::duration_cast<std::chrono::microseconds>(
                      std::chrono::steady_clock::now() - beforeSwap)
                      .count();
    this->reportFrameCost(frameStart, beforeSwap);
    fDrawing = false;
  }

  // Where a frame goes, once a second, under OSU_SHOW_DAMAGE: drawing into
  // the surface, copying it to the window, and waiting for the swap. Guessing
  // at which of the three got slower has not worked so far.
  void reportFrameCost(std::chrono::steady_clock::time_point start,
                       std::chrono::steady_clock::time_point beforeSwap) {
    if (!fForceShowDamage) {
      return;
    }
    const auto now = std::chrono::steady_clock::now();
    const auto us = [](auto from, auto to) {
      return std::chrono::duration_cast<std::chrono::microseconds>(to - from)
          .count();
    };
    fCostUpdateUs += fLastUpdateUs;
    fCostDrawUs += us(start, fBlitStart) - fLastUpdateUs;
    fCostBlitUs += us(fBlitStart, beforeSwap);
    fCostSwapUs += us(beforeSwap, now);
    fCostVisited += client::scene::visitedCount();
    fCostDrawn += client::scene::drawnCount();
    client::scene::visitedCount() = 0;
    client::scene::drawnCount() = 0;
    if (fComputedClipFull) {
      fCostClipArea += static_cast<std::int64_t>(fScreenW) * fScreenH;
    } else if (!fComputedClip.empty()) {
      for (const auto &rect : fComputedClip) {
        fCostClipArea +=
            static_cast<std::int64_t>(rect.width()) * rect.height();
      }
    }
    ++fCostFrames;
    if (wallMs() - fCostLogWall < 1000.0 || fCostFrames == 0) {
      return;
    }
    // Named for what they are: settling the screen, drawing it (which on the
    // CPU renderer is where rasterisation happens too), handing the result to
    // the window, and waiting for the swap.
    std::println(std::cerr,
                 "[frame] update {:.2f} ms, draw {:.2f} ms, blit {:.2f} ms, "
                 "swap {:.2f} ms over {} frames, {} of {} drawables, "
                 "{:.0f}% of the screen repainted{}",
                 static_cast<double>(fCostUpdateUs) / fCostFrames / 1000.0,
                 static_cast<double>(fCostDrawUs) / fCostFrames / 1000.0,
                 static_cast<double>(fCostBlitUs) / fCostFrames / 1000.0,
                 static_cast<double>(fCostSwapUs) / fCostFrames / 1000.0,
                 fCostFrames, fCostDrawn / std::max<std::uint64_t>(1, fCostFrames),
                 fCostVisited / std::max<std::uint64_t>(1, fCostFrames),
                 100.0 * static_cast<double>(fCostClipArea) /
                     std::max<double>(1.0, static_cast<double>(fCostFrames) *
                                               fScreenW * fScreenH),
                 std::string(fDrewOnRaster ? " [cpu]" : " [gpu]") +
                     (this->partialRedraw()
                          ? std::format(
                                " (partial redraw, buffer age {} via {})",
                                fBufferAge,
                                fBufferAgeAssumed ? "assumption"
                                                  : present::backend())
                          : ""));
    fCostLogWall = wallMs();
    fCostUpdateUs = fCostDrawUs = fCostBlitUs = fCostSwapUs = 0;
    fCostVisited = fCostDrawn = 0;
    fCostClipArea = 0;
    fCostFrames = 0;
  }

  // How many back buffers the window may be cycling through; each of them
  // needs the pixels a repaint produced, so a region stays in the blit set
  // for that many frames.
  //
  // There is no way to ask: GLFW does not say, the driver need not tell, and
  // a compositor can hold one of its own on top of whatever the driver does.
  // Three was a guess, and the guess is what a screen change was landing on
  // -- one buffer short means one buffer still holding the screen that was
  // left. A screen change is worth more margin than it costs, so the frames
  // that repaint whole after one are counted with some to spare.
  static constexpr std::size_t kSwapChainDepth = 4;
  static constexpr int kFullRepaintsAfterChange = 6;
  // Kept longer than the guess, so that a window system reporting an age of
  // six has six frames of history to be told about.
  static constexpr std::size_t kBlitHistoryDepth = 8;

  // How far back a repaint has to reach, and how far back the window does.
  // Two questions that look like one until the CPU renderer is on: there the
  // surface being drawn into is ours and persists, holding the last complete
  // frame whatever the driver does with its own buffers, so drawing reaches
  // back exactly one frame. The window is however many buffers deep the
  // driver keeps, which is what buffer age answers -- and what the assumption
  // guesses at when it will not.
  [[nodiscard]] std::size_t windowReach() const {
    return fBufferAge > 0 ? static_cast<std::size_t>(fBufferAge)
                          : kSwapChainDepth;
  }

  // It reaches as far as the window does, on either renderer. The raster
  // surface holding the last complete frame means one frame is *enough* --
  // but only if every screen reports everything it changes, and a gap in that
  // reporting stops being a flicker and becomes a piece of the screen that
  // never updates again, because nothing else will ever repaint it. Reaching
  // further papers over those gaps; it also hides them, which is the trade
  // being made here knowingly rather than by accident.
  [[nodiscard]] std::size_t drawReach() const { return this->windowReach(); }

  // Whether the history says enough about that many frames back.
  [[nodiscard]] bool historyShorterThan(std::size_t reach) const {
    if (fBufferAge == 0) {
      return true; // the window system says the contents are undefined
    }
    if (fBlitHistory.size() < reach) {
      return true;
    }
    std::size_t seen = 0;
    for (auto frame = fBlitHistory.rbegin();
         frame != fBlitHistory.rend() && seen < reach; ++frame, ++seen) {
      if (frame->empty()) {
        return true; // that frame repainted everything
      }
    }
    return false;
  }

  [[nodiscard]] skia::SkIRect damageOver(std::size_t reach) const {
    skia::SkIRect bounds = skia::SkIRect::MakeEmpty();
    std::size_t seen = 0;
    for (auto frame = fBlitHistory.rbegin();
         frame != fBlitHistory.rend() && seen < reach; ++frame, ++seen) {
      for (const auto &area : *frame) {
        if (bounds.isEmpty()) {
          bounds = area;
        } else {
          bounds.join(area);
        }
      }
    }
    return bounds;
  }

  void rememberBlitRegion() {
    // An empty entry means "that frame was full", which forces a full copy
    // until it ages out.
    fBlitHistory.push_back(fFrameClipFull ? std::vector<skia::SkIRect>{}
                                          : fFrameClip);
    while (fBlitHistory.size() > kBlitHistoryDepth) {
      fBlitHistory.erase(fBlitHistory.begin());
    }
  }

  // OSU_SHOW_DAMAGE=1 outlines what was repainted, which is the only way to
  // see whether a screen is reporting its damage honestly.
  [[nodiscard]] bool showingDamage() const {
    return fForceShowDamage || fSettings.flag("damageoverlay");
  }

  void showDamage(skia::SkCanvas *canvas) {
    if (!this->showingDamage()) {
      return;
    }
    // Magenta around each region the frame would have been clipped to; a red
    // border when it would have repainted everything, with the reason,
    // because "why is it full again" is the question that keeps coming up.
    if (fComputedClipFull) {
      skia::SkPaint paint;
      paint.setStyle(skia::kStrokeStyle);
      paint.setStrokeWidth(6.0f);
      paint.setColor(skia::colorSetARGB(255, 255, 40, 40));
      skia::SkRect border = skia::SkRect::MakeWH(static_cast<float>(fScreenW),
                                                 static_cast<float>(fScreenH));
      border.inset(3.0f, 3.0f);
      canvas->drawRect(border, paint);
      // Deduplicated by reason rather than by the clock: a one-off -- a
      // screen change, a resize -- would otherwise be swallowed by whatever
      // repeating reason spoke first in that second.
      const bool sameReason = fLoggedFullReason != nullptr &&
                              std::string_view(fLoggedFullReason) ==
                                  std::string_view(fFullDamageReason);
      if (!sameReason || wallMs() - fDamageLogWall > 1000.0) {
        fDamageLogWall = wallMs();
        fLoggedFullReason = fFullDamageReason;
        std::println(std::cerr, "[damage] would repaint everything: {}",
                     fFullDamageReason);
      }
      return;
    }
    skia::SkPaint paint;
    paint.setStyle(skia::kStrokeStyle);
    paint.setStrokeWidth(2.0f);
    paint.setColor(skia::colorSetARGB(255, 255, 0, 255));
    for (const auto &rect : fComputedClip) {
      skia::SkRect outline = skia::SkRect::Make(rect);
      outline.inset(1.0f, 1.0f);
      if (!outline.isEmpty()) {
        canvas->drawRect(outline, paint);
      }
    }
  }

  void framePlaying() {
    using clock = std::chrono::steady_clock;
    const double now = this->nowMs();
    if (this->shouldStop(now)) {
      this->finishPlay();
      this->frameResults();
      return;
    }
    this->submitAutoplay(now);
    if (fShowProfile) {
      auto t0 = clock::now();
      fEngine->advance(now);
      auto t1 = clock::now();
      this->playHitsounds(now);
      fView.render(this->gameplayCtx(fSurface->getCanvas()), now);
      auto t2 = clock::now();
      fContext->flushAndSubmit(fSurface.get());
      auto t3 = clock::now();
      glfw::glfwSwapBuffers(fWindow);
      auto t4 = clock::now();

      auto &p = fView.profileSlot();
      p.advUs = static_cast<double>(
          std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0)
              .count());
      p.renderUs = static_cast<double>(
          std::chrono::duration_cast<std::chrono::microseconds>(t2 - t1)
              .count());
      p.flushUs = static_cast<double>(
          std::chrono::duration_cast<std::chrono::microseconds>(t3 - t2)
              .count());
      p.swapUs = static_cast<double>(
          std::chrono::duration_cast<std::chrono::microseconds>(t4 - t3)
              .count());
      fView.advanceProfile();
    } else {
      fEngine->advance(now);
      this->playHitsounds(now);
      fView.render(this->gameplayCtx(fSurface->getCanvas()), now);
      this->present();
    }
  }

  // The renderer holds no back-reference to the app; each frame it is handed
  // exactly what it needs.
  [[nodiscard]] client::GameplayView::Ctx gameplayCtx(skia::SkCanvas *canvas) {
    client::GameplayView::Ctx c;
    c.fCanvas = canvas;
    c.fMap = fMap ? &*fMap : nullptr;
    c.fEngine = fEngine ? &*fEngine : nullptr;
    c.fSkin = &fSkin;
    c.fCombo = &fComboInfo;
    c.fFont = &fFont;
    c.fDisplayFont = &fDisplayFont;
    c.fScale = fScale;
    c.fOffsetX = fOffsetX;
    c.fOffsetY = fOffsetY;
    c.fScreenW = fScreenW;
    c.fScreenH = fScreenH;
    c.fCursor = fCursor;
    c.fCursorSize = fSettings.value("cursorsize");
    c.fUiScale = std::clamp(static_cast<float>(fScreenH) / 1080.0f, 0.7f, 3.0f);
    c.fDim = fSettings.value("dim");
    c.fNoGlow = fNoGlow;
    c.fShowProfile = fShowProfile;
    return c;
  }

  // ---- Play lifecycle ---------------------------------------------------

  void resetGameplayState() {
    fHasRawPrev = false;
    fVirtualCursor = osu::kPlayfieldCenter;
    fPlayedEvents = 0;
    fCombo = 0;
    fView.reset();
    fAutoplayEvents.clear();
    fAutoplayIndex = 0;
    fRecordedEvents.clear();
    fHeldMask = 0;
  }

  void startPlay(int setIdx, int diffIdx) {
    // Retries are counted per map: coming here from anywhere but the retry
    // button starts the count again.
    fRetryCount = fRetryPending ? fRetryCount + 1 : 0;
    fRetryPending = false;
    fPreview.stop();
    fPreviewId = -1;
    fMusicDucked = false; // gameplay takes the track over anyway
    if (setIdx < 0 || setIdx >= static_cast<int>(fLibrary.size())) {
      return;
    }
    auto set = this->setForBlocking(setIdx);
    if (!set || diffIdx < 0 ||
        diffIdx >= static_cast<int>(set->fBeatmaps.size())) {
      return;
    }
    fPlayingSet = setIdx;
    fPlayingDiff = diffIdx;
    fLastSavedReplay.clear();
    // Only a play that was asked for by watchReplay is driven from a file.
    // Without this, everything after watching one replay would keep playing
    // that replay back -- and, being "recorded", would be saved as a copy.
    fReplayPath = std::exchange(fPendingReplay, {});
    fAutoplay = fCliAutoplay || !fReplayPath.empty();
    fSet = *set; // active copy: gameplay reads audio/bg from here
    fMenuMusicForSet = -1;  // gameplay reloads the track from scratch
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
    fPausedNow = this->nowMs();
    fAudio.pause();
    this->switchState(State::kPaused);
    this->setCursorVisible(true);
  }

  void resumeGame() {
    // Re-anchor the clock at the frozen instant: wall time spent in the
    // pause menu never existed as far as the game timeline is concerned.
    fClock.reset(wallMs(), fPausedNow);
    fLastClockSyncWall = wallMs();
    fAudio.resume();
    this->switchState(State::kPlaying);
    fView.invalidate();
    this->setCursorVisible(false);
  }

  void quitToSelect() {
    fAudio.stop();
    fMenuMusicForSet = -1;  // let updateMenuMusic restart the loop
    this->switchState(State::kSongSelect);
    fView.invalidate();
    fBackgroundForSet = -1; // gameplay replaced the cached background
    this->setCursorVisible(true);
  }

  void finishPlay() {
    this->captureResult();
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

  void captureResult() {
    fResult.fScore = fEngine->score();
    double sum = 0.0;
    double sumSq = 0.0;
    std::size_t n = 0;
    for (const double d : fEngine->tapDeltas()) {
      sum += d;
      sumSq += d * d;
      ++n;
    }
    if (n > 0) {
      const double mean = sum / static_cast<double>(n);
      fResult.fMean = mean;
      fResult.fUr = 10.0 * std::sqrt(std::max(
                               0.0, sumSq / static_cast<double>(n) -
                                        mean * mean));
    } else {
      fResult.fMean = 0.0;
      fResult.fUr = 0.0;
    }
    fResult.fGrade = osu::gradeString(osu::computeGrade(fResult.fScore));
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
    fRawMotionRequest.store(fSettings.flag("rawinput") ? glfw::kTrue
                                                       : glfw::kFalse,
                            std::memory_order_release);
#ifndef __EMSCRIPTEN__
    glfw::glfwPostEmptyEvent();
#endif
  }

  void setCursorVisible(bool visible) {
#ifdef __EMSCRIPTEN__
    if (visible) {
      EM_ASM(Module.setCursorVisible(true));
    } else {
      EM_ASM(Module.setCursorVisible(false));
    }
    glfw::glfwSetInputMode(fWindow, glfw::kCursor,
                           visible ? glfw::kCursorNormal
                                   : glfw::kCursorHidden);
#else
    const int hidden =
        this->relativeCursor() ? glfw::kCursorDisabled : glfw::kCursorHidden;
    fCursorModeRequest.store(visible ? glfw::kCursorNormal : hidden,
                             std::memory_order_release);
    glfw::glfwPostEmptyEvent();
#endif
  }

  // ---- Library ----------------------------------------------------------

  void initLibrary() {
#ifdef __EMSCRIPTEN__
    fMapsDir = "/maps";
#else
    if (const char *home = std::getenv("HOME"); home != nullptr) {
      fMapsDir = std::filesystem::path(home) / ".local" / "share" /
                 "osu_client" / "maps";
    } else {
      fMapsDir = "maps";
    }
#endif
    std::error_code ec;
    std::filesystem::create_directories(fMapsDir, ec);
    fThumbDir = fMapsDir / "thumbnails";
    fReplayDir = fMapsDir.parent_path() / "replays";
    std::filesystem::create_directories(fThumbDir, ec);
    fMapCache.load(fMapsDir / "metadata-cache.json");
    fReplayIndex.load(fMapsDir.parent_path() / "replay-index.json");
    fReplayIndex.refresh(fReplayDir);
    fSettings.load(fMapsDir.parent_path() / "settings.json");
    fSwapIntervalRequest.store(fSettings.flag("vsync") ? 1 : 0,
                               std::memory_order_release);
    fAppliedDim = fSettings.value("dim");

    if (fHasInitialSet) {
      LibraryEntry entry;
      entry.fInfos = fSet.fBeatmaps;
      entry.fLoaded = std::make_shared<osu::BeatmapSet>(fSet);
      fLibrary.push_back(std::move(entry));
      fLoadedOrder.push_back(0);
    }

    // Collect the archive list first, then parse the cache misses on every
    // core: unzipping and star-rating a set is pure computation, and doing it
    // one at a time was leaving the machine idle.
    std::vector<std::filesystem::path> archives;
    std::error_code iterEc;
    for (const auto &e :
         std::filesystem::directory_iterator(fMapsDir, iterEc)) {
      if (e.is_regular_file() && e.path().extension() == ".osz") {
        archives.push_back(e.path());
      }
    }

    std::vector<std::filesystem::path> misses;
    for (const auto &path : archives) {
      if (auto cached = this->cachedEntryFor(path)) {
        fLibrary.push_back(std::move(*cached));
      } else {
        misses.push_back(path);
      }
    }

    if (!misses.empty()) {
      const auto threads = std::max(
          1u, std::min(std::thread::hardware_concurrency(),
                       static_cast<unsigned>(misses.size())));
      std::println(std::cerr, "[library] parsing {} new sets on {} threads",
                   misses.size(), threads);
      std::mutex resultMutex;
      std::atomic<std::size_t> next{0};
      std::vector<std::thread> pool;
      pool.reserve(threads);
      for (unsigned t = 0; t < threads; ++t) {
        pool.emplace_back([&] {
          for (;;) {
            const std::size_t i = next.fetch_add(1);
            if (i >= misses.size()) {
              return;
            }
            try {
              const auto set = loadBeatmapSet(misses[i]);
              LibraryEntry entry;
              entry.fPath = misses[i];
              entry.fInfos = set.fBeatmaps;
              auto diffs = this->cacheRecordFor(set);
              const auto stamp = fileStamp(misses[i]);
              const std::scoped_lock lock(resultMutex);
              fLibrary.push_back(std::move(entry));
              fMapCache.store(misses[i].filename().string(), stamp.first,
                              stamp.second, std::move(diffs));
            } catch (const std::exception &e) {
              std::println(std::cerr, "[library] skipping {}: {}",
                           misses[i].filename().string(), e.what());
            }
          }
        });
      }
      for (auto &th : pool) {
        th.join();
      }
    }
    fMapCache.save();
    this->syncMapsDir();

    this->sortLibrary();
    fFilterDirty = true;
    this->rebuildVisible();
    // Start on a random set so the menu isn't always greeted by the same
    // track (unless a specific beatmap was passed on the command line).
    if (!fHasInitialSet && !fLibrary.empty()) {
      std::uniform_int_distribution<std::size_t> pick(0, fLibrary.size() - 1);
      fSelSet = static_cast<int>(pick(fUiRng));
      fSelDiff = 0;
    }
    fLibraryLoaded = true;
    std::println(std::cerr, "[library] {} sets", fLibrary.size());
  }

  // Loop the selected set's audio quietly under the menus, lazer-style. Only
  // reloads when the selection changes; stops when gameplay takes over.
  void updateMenuMusic() {
    if (fLibrary.empty()) {
      return;
    }
    if (fMenuMusicForSet == fSelSet) {
      // The track is given a moment to start before its silence counts as
      // having ended; OpenAL reports a source as stopped until it does.
      // Paused for a preview is not the same as finished.
      if (!fAudio.playing() && !fMusicDucked &&
          wallMs() - fMenuTrackWall > 1000.0 && fState != State::kResults) {
        // The results screen belongs to the map that was just played, and
        // moving on from it would move the selection out from under the
        // player, who is going back to that map.
        this->nextMenuTrack();
      }
      return;
    }
    auto set = this->setFor(fSelSet);
    if (!set) {
      return; // still loading; try again next frame
    }
    fMenuMusicForSet = fSelSet;
    // The grace period starts when the track is asked for, not when it starts
    // playing: decoding takes a few hundred milliseconds, and silence while
    // that happens is not a track that ended. This is what made the client
    // open on one beatmap and jump to another a second later.
    fMenuTrackWall = wallMs();
    if (set->fBeatmaps.empty()) {
      fAudio.stop();
      return;
    }
    const auto &audioName = set->fBeatmaps.front().fMeta.fAudioFilename;
    if (audioName.empty()) {
      fAudio.stop();
      return;
    }
    const auto bytes = set->findFile(audioName);
    if (bytes.empty()) {
      fAudio.stop();
      return;
    }
    // Decoding an MP3 takes hundreds of milliseconds -- the remaining stall
    // when changing selection. Decode on the worker, upload to OpenAL here.
    const std::string ext = detail::fileExtension(audioName);
    std::vector<std::uint8_t> copy(bytes.begin(), bytes.end());
    auto pcm = std::make_shared<audio_client::DecodedAudio>();
    const int forSet = fSelSet;
    // The index alone is not identity: deleting a beatmap shifts everything
    // after it, so the path is checked too before this track is adopted.
    const auto forPath = fLibrary[static_cast<std::size_t>(fSelSet)].fPath;
    fLoader.submit(
        static_cast<std::uint64_t>(fSelSet) | (3ull << 32),
        [copy = std::move(copy), ext, pcm] {
          *pcm = audio_client::decodeAudio(copy, ext);
        },
        [this, forSet, forPath, pcm] {
          if (forSet != fSelSet || pcm->fSamples.empty()) {
            return; // selection moved on while decoding
          }
          if (forSet >= static_cast<int>(fLibrary.size()) ||
              fLibrary[static_cast<std::size_t>(forSet)].fPath != forPath) {
            return; // that entry is not the one this was decoded for
          }
          fAudio.adopt(std::move(*pcm));
          fAudio.setLooping(false); // the next track is chosen when it ends
          fMenuTrackWall = wallMs();
          fAudio.setVolume(this->musicGain());
          fAudio.play();
          // The analysis clock is anchored to the *old* track until reset.
          fMenuClock.reset(wallMs(), 0.0);
          fMenuClockSyncWall = std::numeric_limits<double>::lowest();
          fSpectrum.reset();
        });
  }

  // Picks another map to listen to. Random, and never the one just heard as
  // long as there is anything else in the library.
  void nextMenuTrack() {
    this->requestRedraw(1500.0);
    if (fVisible.size() > 1) {
      // One draw, uniform over everything except the one just heard. Drawing
      // again until the draw differs is unbiased but can fail, and failing
      // eight times in a row meant playing the same track over again -- which
      // is the opposite of what this is for.
      std::size_t current = fVisible.size();
      for (std::size_t i = 0; i < fVisible.size(); ++i) {
        if (fVisible[i] == fSelSet) {
          current = i;
          break;
        }
      }
      const bool skipping = current < fVisible.size();
      std::uniform_int_distribution<std::size_t> pick(
          0, fVisible.size() - (skipping ? 2 : 1));
      std::size_t idx = pick(fUiRng);
      if (skipping && idx >= current) {
        ++idx; // the gap left by the one being skipped closes over it
      }
      fSelSet = fVisible[idx];
      fSelDiff = 0; // the carousel follows the selection on its own
      fMenuTrackWall = wallMs();
      return;
    }
    // Nothing else to play: start this one again.
    fMenuTrackWall = wallMs();
    fAudio.play();
  }

  void stopMenuMusic() {
    fMenuMusicForSet = -1;
    fAudio.setLooping(false);
    fAudio.stop();
    fSpectrum.reset();
  }

  [[nodiscard]] static std::pair<std::uintmax_t, std::int64_t>
  fileStamp(const std::filesystem::path &path) {
    std::error_code ec;
    const auto size = std::filesystem::file_size(path, ec);
    const auto writeTime = std::filesystem::last_write_time(path, ec);
    return {size, static_cast<std::int64_t>(writeTime.time_since_epoch().count())};
  }

  // Library entry straight from the cache, or nothing when it is stale.
  [[nodiscard]] std::optional<LibraryEntry>
  cachedEntryFor(const std::filesystem::path &path) {
    const auto [size, mtime] = fileStamp(path);
    const auto *cached =
        fMapCache.lookup(path.filename().string(), size, mtime);
    if (cached == nullptr || cached->empty()) {
      return std::nullopt;
    }
    LibraryEntry entry;
    entry.fPath = path;
    for (const auto &d : *cached) {
      entry.fInfos.push_back(infoFromCache(d));
    }
    return entry;
  }

  [[nodiscard]] static osu::BeatmapInfo
  infoFromCache(const client::CachedDifficulty &d) {
    osu::BeatmapInfo info;
    info.fFilename = d.fFilename;
    info.fMd5 = d.fMd5;
    info.fMeta.fBeatmapSetId = d.fSetId;
    info.fMeta.fTitle = d.fTitle;
    info.fMeta.fTitleUnicode = d.fTitleUnicode;
    info.fMeta.fArtist = d.fArtist;
    info.fMeta.fArtistUnicode = d.fArtistUnicode;
    info.fMeta.fCreator = d.fCreator;
    info.fMeta.fVersion = d.fVersion;
    info.fMeta.fAudioFilename = d.fAudioFilename;
    info.fMeta.fBackground = d.fBackground;
    info.fStars = d.fStars;
    info.fDiff.fCs = d.fCs;
    info.fDiff.fAr = d.fAr;
    info.fDiff.fOd = d.fOd;
    info.fDiff.fHp = d.fHp;
    info.fLengthMs = d.fLengthMs;
    info.fObjectCount = d.fObjectCount;
    return info;
  }

  [[nodiscard]] static std::vector<client::CachedDifficulty>
  cacheRecordFor(const osu::BeatmapSet &set) {
    std::vector<client::CachedDifficulty> diffs;
    diffs.reserve(set.fBeatmaps.size());
    for (const auto &info : set.fBeatmaps) {
      diffs.push_back({info.fFilename, info.fMd5, info.fMeta.fBeatmapSetId,
                       info.fMeta.fTitle,
                       info.fMeta.fTitleUnicode, info.fMeta.fArtist,
                       info.fMeta.fArtistUnicode, info.fMeta.fCreator,
                       info.fMeta.fVersion, info.fMeta.fAudioFilename,
                       info.fMeta.fBackground, info.fStars, info.fDiff.fCs,
                       info.fDiff.fAr, info.fDiff.fOd, info.fDiff.fHp,
                       info.fLengthMs, info.fObjectCount});
    }
    return diffs;
  }

  // Metadata for one archive, from the cache when the file is unchanged.
  void scanArchive(const std::filesystem::path &path) {
    std::error_code ec;
    const auto size = std::filesystem::file_size(path, ec);
    const auto writeTime = std::filesystem::last_write_time(path, ec);
    const auto mtime = static_cast<std::int64_t>(
        writeTime.time_since_epoch().count());
    const std::string key = path.filename().string();

    if (const auto *cached = fMapCache.lookup(key, size, mtime)) {
      LibraryEntry entry;
      entry.fPath = path;
      for (const auto &d : *cached) {
        entry.fInfos.push_back(infoFromCache(d));
      }
      if (!entry.fInfos.empty()) {
        fLibrary.push_back(std::move(entry));
        return;
      }
    }

    // Cache miss: parse the archive once, then remember the result.
    try {
      auto set = std::make_shared<osu::BeatmapSet>(loadBeatmapSet(path));
      LibraryEntry entry;
      entry.fPath = path;
      entry.fInfos = set->fBeatmaps;
      fLibrary.push_back(std::move(entry));

      fMapCache.store(key, size, mtime, cacheRecordFor(*set));
    } catch (const std::exception &e) {
      std::println(std::cerr, "[library] skipping {}: {}", key, e.what());
    }
  }

  // Full set for an entry if it is already resident. Never blocks: a miss
  // queues a background load and returns null, and the caller retries on a
  // later frame. Unzipping an archive and decoding its audio takes hundreds
  // of milliseconds, which is a visible freeze when done inline.
  [[nodiscard]] std::shared_ptr<osu::BeatmapSet> setFor(int index) {
    if (index < 0 || index >= static_cast<int>(fLibrary.size())) {
      return nullptr;
    }
    auto &entry = fLibrary[static_cast<std::size_t>(index)];
    if (entry.fLoaded) {
      return entry.fLoaded;
    }
    if (entry.fPath.empty()) {
      return nullptr;
    }
    this->requestSet(index);
    return nullptr;
  }

  // Blocking load, for the one case that genuinely cannot proceed without
  // the data: starting gameplay.
  [[nodiscard]] std::shared_ptr<osu::BeatmapSet> setForBlocking(int index) {
    if (index < 0 || index >= static_cast<int>(fLibrary.size())) {
      return nullptr;
    }
    auto &entry = fLibrary[static_cast<std::size_t>(index)];
    if (entry.fLoaded) {
      return entry.fLoaded;
    }
    if (entry.fPath.empty()) {
      return nullptr;
    }
    try {
      entry.fLoaded = std::make_shared<osu::BeatmapSet>(
          loadBeatmapSet(entry.fPath, false));
      this->adoptCachedStars(index, *entry.fLoaded);
      this->touchLoaded(index);
    } catch (const std::exception &e) {
      std::println(std::cerr, "[library] load failed {}: {}",
                   entry.fPath.filename().string(), e.what());
      return nullptr;
    }
    return entry.fLoaded;
  }

  void requestSet(int index) {
    const auto path = fLibrary[static_cast<std::size_t>(index)].fPath;
    if (path.empty()) {
      return;
    }
    auto result = std::make_shared<std::shared_ptr<osu::BeatmapSet>>();
    fLoader.submit(
        static_cast<std::uint64_t>(index) | (1ull << 32),
        [path, result] {
          *result = std::make_shared<osu::BeatmapSet>(
              loadBeatmapSet(path, false));
        },
        [this, index, path, result] {
          if (index >= static_cast<int>(fLibrary.size()) ||
              fLibrary[static_cast<std::size_t>(index)].fPath != path) {
            return; // library was re-sorted underneath us
          }
          if (!*result) {
            return;
          }
          // The scan already worked the star ratings out and wrote them to the
          // cache; this load was for the objects and the audio.
          this->adoptCachedStars(index, **result);
          fLibrary[static_cast<std::size_t>(index)].fLoaded = *result;
          this->touchLoaded(index);
        });
  }

  // The library knows the star ratings from its cache, so a set loaded for
  // its objects and its audio takes them from there rather than spending
  // seconds working them out again.
  void adoptCachedStars(int index, osu::BeatmapSet &set) const {
    const auto &known = this->infosFor(index);
    for (auto &info : set.fBeatmaps) {
      for (const auto &cached : known) {
        if (cached.fFilename == info.fFilename) {
          info.fStars = cached.fStars;
          break;
        }
      }
    }
  }

  void touchLoaded(int index) {
    fLoadedOrder.push_back(index);
    while (fLoadedOrder.size() > kMaxLoadedSets) {
      const int evict = fLoadedOrder.front();
      fLoadedOrder.pop_front();
      if (evict != index && evict >= 0 &&
          evict < static_cast<int>(fLibrary.size()) &&
          !fLibrary[static_cast<std::size_t>(evict)].fPath.empty()) {
        fLibrary[static_cast<std::size_t>(evict)].fLoaded.reset();
      }
    }
  }

  [[nodiscard]] const std::vector<osu::BeatmapInfo> &infosFor(int index) const {
    static const std::vector<osu::BeatmapInfo> kEmpty;
    if (index < 0 || index >= static_cast<int>(fLibrary.size())) {
      return kEmpty;
    }
    return fLibrary[static_cast<std::size_t>(index)].fInfos;
  }

  void sortLibrary() {
    const int selected = fSelSet;
    const std::filesystem::path selPath =
        selected >= 0 && selected < static_cast<int>(fLibrary.size())
            ? fLibrary[static_cast<std::size_t>(selected)].fPath
            : std::filesystem::path{};

    const auto key = [](const LibraryEntry &e) -> const osu::BeatmapInfo * {
      return e.fInfos.empty() ? nullptr : &e.fInfos.front();
    };
    switch (fFilter.sortMode()) {
    case client::FilterControl::SortMode::kAuthor:
      std::ranges::stable_sort(fLibrary, {}, [&](const LibraryEntry &e) {
        const auto *i = key(e);
        return i ? toLowerAscii(i->fMeta.fCreator) : std::string{};
      });
      break;
    case client::FilterControl::SortMode::kTitle:
      std::ranges::stable_sort(fLibrary, {}, [&](const LibraryEntry &e) {
        const auto *i = key(e);
        return i ? toLowerAscii(i->fMeta.fTitle) : std::string{};
      });
      break;
    case client::FilterControl::SortMode::kArtist:
      std::ranges::stable_sort(fLibrary, {}, [&](const LibraryEntry &e) {
        const auto *i = key(e);
        return i ? toLowerAscii(i->fMeta.fArtist) : std::string{};
      });
      break;
    case client::FilterControl::SortMode::kDifficulty:
      std::ranges::stable_sort(fLibrary, {}, [&](const LibraryEntry &e) {
        const auto *i = key(e);
        return i ? i->fStars : 0.0;
      });
      break;
    case client::FilterControl::SortMode::kLength:
      std::ranges::stable_sort(fLibrary, {}, [&](const LibraryEntry &e) {
        const auto *i = key(e);
        return i ? i->fLengthMs : 0.0;
      });
      break;
    }
    // The LRU holds indices, which the sort just invalidated.
    fLoadedOrder.clear();
    for (int i = 0; i < static_cast<int>(fLibrary.size()); ++i) {
      if (fLibrary[static_cast<std::size_t>(i)].fLoaded) {
        fLoadedOrder.push_back(i);
      }
      if (!selPath.empty() &&
          fLibrary[static_cast<std::size_t>(i)].fPath == selPath) {
        fSelSet = i;
      }
    }
    fFilterDirty = true;
  }

  [[nodiscard]] static std::string toLowerAscii(std::string_view in) {
    std::string out(in);
    std::ranges::transform(out, out.begin(), [](unsigned char c) {
      return static_cast<char>(std::tolower(c));
    });
    return out;
  }

  void rebuildVisible() {
    if (!fFilterDirty) {
      return;
    }
    fFilterDirty = false;
    const client::Criteria criteria = client::parseQuery(fFilter.text());
    fVisible.clear();

    for (int i = 0; i < static_cast<int>(fLibrary.size()); ++i) {
      const auto &infos = fLibrary[static_cast<std::size_t>(i)].fInfos;
      if (infos.empty()) {
        continue;
      }
      // A set matches when any of its difficulties matches, which is how
      // lazer's carousel filters (sets are hidden only if every child fails).
      bool any = false;
      for (const auto &info : infos) {
        if (this->matchesCriteria(criteria, infos.front().fMeta, info)) {
          any = true;
          break;
        }
      }
      if (any) {
        fVisible.push_back(i);
      }
    }

    if (!fVisible.empty() &&
        std::ranges::find(fVisible, fSelSet) == fVisible.end()) {
      fSelSet = fVisible.front();
      fSelDiff = 0;
    }
  }

  [[nodiscard]] bool matchesCriteria(const client::Criteria &c,
                                     const osu::Metadata &setMeta,
                                     const osu::BeatmapInfo &info) const {
    // DifficultyRangeSlider is an additional constraint on top of the query.
    if (info.fStars < fFilter.rangeMin() ||
        (fFilter.rangeMax() < client::FilterControl::kDiffRangeCap &&
         info.fStars > fFilter.rangeMax())) {
      return false;
    }
    if (!c.fStars.matches(info.fStars) || !c.fAr.matches(info.fDiff.fAr) ||
        !c.fCs.matches(info.fDiff.fCs) || !c.fOd.matches(info.fDiff.fOd) ||
        !c.fHp.matches(info.fDiff.fHp) ||
        !c.fLengthSec.matches(info.fLengthMs / 1000.0) ||
        !c.fObjects.matches(info.fObjectCount)) {
      return false;
    }
    const auto contains = [](std::string_view hay, const std::string &needle) {
      return needle.empty() || toLowerAscii(hay).find(needle) != std::string::npos;
    };
    if (!contains(info.fMeta.fCreator, c.fCreator)) {
      return false;
    }
    if (!c.fArtist.empty() && !contains(setMeta.fArtist, c.fArtist) &&
        !contains(setMeta.fArtistUnicode, c.fArtist)) {
      return false;
    }
    if (!c.fTitle.empty() && !contains(setMeta.fTitle, c.fTitle) &&
        !contains(setMeta.fTitleUnicode, c.fTitle)) {
      return false;
    }
    if (!contains(info.fMeta.fVersion, c.fDiff)) {
      return false;
    }
    if (c.fSearchText.empty()) {
      return true;
    }
    // Remaining free text: every space-separated term must appear somewhere,
    // as FilterCriteria.Matches does.
    const std::string haystack =
        toLowerAscii(setMeta.fTitle) + '\x1f' +
        toLowerAscii(setMeta.fTitleUnicode) + '\x1f' +
        toLowerAscii(setMeta.fArtist) + '\x1f' +
        toLowerAscii(setMeta.fArtistUnicode) + '\x1f' +
        toLowerAscii(info.fMeta.fCreator) + '\x1f' +
        toLowerAscii(info.fMeta.fVersion);
    std::size_t pos = 0;
    while (pos < c.fSearchText.size()) {
      const auto next = c.fSearchText.find(' ', pos);
      const auto term = c.fSearchText.substr(
          pos, next == std::string::npos ? std::string::npos : next - pos);
      if (!term.empty() && haystack.find(term) == std::string::npos) {
        return false;
      }
      if (next == std::string::npos) {
        break;
      }
      pos = next + 1;
    }
    return true;
  }

  [[nodiscard]] int visiblePos() const {
    const auto it = std::ranges::find(fVisible, fSelSet);
    return it == fVisible.end() ? -1
                                : static_cast<int>(it - fVisible.begin());
  }

  void syncMapsDir() {
#ifdef __EMSCRIPTEN__
    EM_ASM(FS.syncfs(false, function(err) {}));
#endif
  }

  // Point the selection at the set that came from the command line.
  void selectInitialSet() {
    for (std::size_t i = 0; i < fLibrary.size(); ++i) {
      if (fLibrary[i].fPath.empty()) {
        fSelSet = static_cast<int>(i);
        fSelDiff = 0;
        return;
      }
    }
  }

  // The online id of a library entry, from the .osu files themselves.
  [[nodiscard]] static int onlineSetId(const LibraryEntry &entry) {
    for (const auto &info : entry.fInfos) {
      if (info.fMeta.fBeatmapSetId > 0) {
        return info.fMeta.fBeatmapSetId;
      }
    }
    return 0;
  }

  [[nodiscard]] int libraryIndexForSet(int setId) const {
    if (setId <= 0) {
      return -1;
    }
    for (std::size_t i = 0; i < fLibrary.size(); ++i) {
      if (onlineSetId(fLibrary[i]) == setId) {
        return static_cast<int>(i);
      }
    }
    return -1;
  }

  // Marks the results that are already installed, so they read as owned and
  // cannot be downloaded a second time.
  void markOwnedResults() {
    for (auto &e : fFound) {
      if (e.fSt == client::listing::Entry::St::kFetching) {
        continue;
      }
      e.fSt = this->libraryIndexForSet(static_cast<int>(e.fSetId)) >= 0
                  ? client::listing::Entry::St::kDone
                  : client::listing::Entry::St::kIdle;
    }
  }

  bool addOszToLibrary(const std::filesystem::path &path, bool select) {
    const std::size_t before = fLibrary.size();
    this->scanArchive(path);
    if (fLibrary.size() == before) {
      return false;
    }
    // The same set can already be in the library under another file name --
    // imported from elsewhere, or downloaded before. Keep the new archive and
    // drop the old entry rather than listing the beatmap twice.
    const int setId = onlineSetId(fLibrary.back());
    // Unsubmitted maps carry no online id; their difficulty hashes identify
    // them just as well.
    std::vector<std::string> hashes;
    if (setId <= 0) {
      for (const auto &info : fLibrary.back().fInfos) {
        if (!info.fMd5.empty()) {
          hashes.push_back(info.fMd5);
        }
      }
      std::ranges::sort(hashes);
    }
    const auto sameSet = [&](const LibraryEntry &entry) {
      if (setId > 0) {
        return onlineSetId(entry) == setId;
      }
      if (hashes.empty()) {
        return false;
      }
      std::vector<std::string> other;
      for (const auto &info : entry.fInfos) {
        if (!info.fMd5.empty()) {
          other.push_back(info.fMd5);
        }
      }
      std::ranges::sort(other);
      return other == hashes;
    };
    {
      for (std::size_t i = 0; i + 1 < fLibrary.size();) {
        if (!sameSet(fLibrary[i])) {
          ++i;
          continue;
        }
        const auto stale = fLibrary[i].fPath;
        fLibrary.erase(fLibrary.begin() + static_cast<std::ptrdiff_t>(i));
        if (!stale.empty() && stale != path &&
            stale.parent_path() == fMapsDir) {
          std::error_code ec;
          std::filesystem::remove(stale, ec);
          std::println(std::cerr, "[library] replaced {} with {}",
                       stale.filename().string(), path.filename().string());
        }
      }
    }
    fMapCache.save();
    this->syncMapsDir();
    const auto added = fLibrary.back().fPath;
    this->sortLibrary();
    this->rebuildVisible();
    if (select) {
      for (int i = 0; i < static_cast<int>(fLibrary.size()); ++i) {
        if (fLibrary[static_cast<std::size_t>(i)].fPath == added) {
          fSelSet = i;
          fSelDiff = 0;
          break;
        }
      }
    }
    return true;
  }

  // ---- Import an external .osz into the library -------------------------
  //
  // No portable file dialog exists in this stack, so: on desktop we shell out
  // to whatever GTK/KDE picker is installed (zenity/kdialog/matedialog/qarma);
  // in the browser the JS side handles the <input type=file> and drops the
  // bytes at /import.osz, then calls back. Either way the chosen archive is
  // copied into the maps dir and added to the library.
  // Files dropped onto the window (the reliable import path: no dialog
  // binary required, works on any desktop).
  void drainDroppedFiles() {
    std::vector<std::string> paths;
    {
      const std::scoped_lock lock(fDropMutex);
      if (fDropped.empty()) {
        return;
      }
      paths.swap(fDropped);
    }
    for (const auto &p : paths) {
      this->importFrom(std::filesystem::path(p));
    }
  }

  bool importFrom(const std::filesystem::path &src) {
    std::error_code ec;
    if (!std::filesystem::exists(src, ec)) {
      std::println(std::cerr, "[import] no such file: {}", src.string());
      return false;
    }
    const auto ext = detail::lowerExtension(src);
    if (ext != ".osz" && ext != ".zip") {
      std::println(std::cerr, "[import] not a beatmap archive: {}",
                   src.string());
      return false;
    }
    const auto dest = fMapsDir / src.filename();
    std::filesystem::copy_file(
        src, dest, std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) {
      std::println(std::cerr, "[import] copy failed: {}", ec.message());
      return false;
    }
    if (!this->addOszToLibrary(dest, true)) {
      return false;
    }
    std::println(std::cerr, "[import] added {}", dest.filename().string());
    if (fState == State::kMainMenu) {
      this->switchState(State::kSongSelect);
    }
    return true;
  }

  void importOsz() {
#ifdef __EMSCRIPTEN__
    EM_ASM({ if (Module.osuPickBeatmap) Module.osuPickBeatmap(); });
#else
    const std::filesystem::path chosen = this->runFilePicker();
    if (chosen.empty()) {
      return; // cancelled, or no dialog available (already reported)
    }
    this->importFrom(chosen);
#endif
  }

#ifndef __EMSCRIPTEN__
  // Shell out to a native file dialog via std::system (no POSIX popen: that
  // needs <stdio.h>, which mixes badly with `import std` on this toolchain).
  // The picker writes the chosen path to a temp file; we read it back.
  [[nodiscard]] std::filesystem::path runFilePicker() {
    std::error_code ec;
    const auto tmp = std::filesystem::temp_directory_path(ec) /
                     "osu_client_import.txt";
    const std::string tmpStr = tmp.string();
    const std::string commands[] = {
        "zenity --file-selection "
        "--file-filter='osu! beatmap | *.osz *.zip' --title='Import beatmap'",
        "kdialog --getopenfilename . '*.osz *.zip|osu! beatmap'",
        "matedialog --file-selection",
        "qarma --file-selection",
    };
    for (const auto &pick : commands) {
      // Is the binary even installed? `command -v` keeps a missing dialog
      // from looking like a user cancellation.
      const std::string bin = pick.substr(0, pick.find(' '));
      if (std::system(("command -v " + bin + " > /dev/null 2>&1").c_str()) !=
          0) {
        continue;
      }
      std::filesystem::remove(tmp, ec);
      const std::string cmd = pick + " > '" + tmpStr + "' 2>/dev/null";
      const int rc = std::system(cmd.c_str());
      if (rc != 0) {
        std::println(std::cerr, "[import] {} exited {} (cancelled?)", bin, rc);
        return {};
      }
      std::ifstream in(tmp);
      if (!in) {
        continue;
      }
      std::string path;
      std::getline(in, path);
      std::filesystem::remove(tmp, ec);
      while (!path.empty() && (path.back() == '\n' || path.back() == '\r')) {
        path.pop_back();
      }
      if (!path.empty()) {
        return std::filesystem::path(path);
      }
      return {};
    }
    std::println(std::cerr,
                 "[import] no file dialog installed (tried zenity, kdialog, "
                 "matedialog, qarma). Drag a .osz onto the window instead, or "
                 "copy it into {}",
                 fMapsDir.string());
    return {};
  }
#endif

  // ---- Download screen logic -------------------------------------------

  // Mirrors, in the order they are tried. None of them serves the whole
  // filter set, so each says what it can do and the rest is applied to the
  // page after it arrives.
  //
  // Verified by hand against each API:
  //   nerinyan   q, m, s (name), sort, e (video/storyboard), nsfw, p/ps
  //   osu.direct q, mode, status (id), sort (<field>:<dir>), amount/offset
  //   mino       query, mode, status (id), limit/offset
  enum class MirrorStyle : std::uint8_t { kNerinyan, kOsuDirect, kMino };
  struct Mirror {
    const char *fName;
    MirrorStyle fStyle;
    const char *fDownload; // {} takes the set id
  };
  static constexpr std::array<Mirror, 3> kMirrors{
      Mirror{"nerinyan", MirrorStyle::kNerinyan,
             "https://api.nerinyan.moe/d/{}"},
      Mirror{"osu.direct", MirrorStyle::kOsuDirect,
             "https://osu.direct/api/d/{}"},
      Mirror{"mino", MirrorStyle::kMino, "https://catboy.best/d/{}"}};
  std::size_t fMirror = 0;
  static constexpr int kSearchPageSize = 50;

  // "2020-04-24T18:08:56+02:00" -> a number that sorts by date. Only the
  // ordering matters, so the digits are simply concatenated.
  [[nodiscard]] static std::int64_t dateStamp(std::string_view iso) {
    std::int64_t v = 0;
    int digits = 0;
    for (const char c : iso) {
      if (c >= '0' && c <= '9') {
        v = v * 10 + (c - '0');
        if (++digits == 14) {
          break;
        }
      }
    }
    return v;
  }

  // osu! status ids, which two of the three mirrors take directly.
  [[nodiscard]] static int statusId(client::listing::Category c) {
    using Category = client::listing::Category;
    switch (c) {
    case Category::kRanked: return 1;
    case Category::kQualified: return 3;
    case Category::kLoved: return 4;
    case Category::kPending: return 0;
    case Category::kWip: return -1;
    case Category::kGraveyard: return -2;
    default: return 100; // no server-side status
    }
  }

  [[nodiscard]] static const char *statusName(client::listing::Category c) {
    using Category = client::listing::Category;
    switch (c) {
    case Category::kRanked: return "ranked";
    case Category::kQualified: return "qualified";
    case Category::kLoved: return "loved";
    case Category::kPending: return "pending";
    case Category::kWip: return "wip";
    case Category::kGraveyard: return "graveyard";
    case Category::kLeaderboard: return "leaderboard";
    default: return "";
    }
  }

  // lazer's criteria mapped onto what each mirror calls them. An empty string
  // means the mirror cannot sort by it and the listing does it itself.
  [[nodiscard]] static std::string sortParam(client::listing::Sort sort,
                                             bool descending,
                                             MirrorStyle style) {
    using Sort = client::listing::Sort;
    const char *field = nullptr;
    switch (sort) {
    case Sort::kTitle: field = "title"; break;
    case Sort::kArtist: field = "artist"; break;
    case Sort::kRanked: field = style == MirrorStyle::kOsuDirect
                                    ? "ranked_date" : "ranked"; break;
    case Sort::kUpdated: field = style == MirrorStyle::kOsuDirect
                                     ? "last_updated" : "updated"; break;
    case Sort::kPlays: field = style == MirrorStyle::kOsuDirect
                                   ? "play_count" : "plays"; break;
    case Sort::kFavourites: field = style == MirrorStyle::kOsuDirect
                                        ? "favourite_count" : "favourites";
      break;
    default: return {}; // difficulty, rating, relevance, nominations
    }
    const char *dir = descending ? "desc" : "asc";
    return style == MirrorStyle::kOsuDirect
               ? std::format("{}:{}", field, dir)
               : std::format("{}_{}", field, dir);
  }

  [[nodiscard]] std::string searchUrl(int offset) const {
    const auto &f = fListing.filters();
    const auto &mirror = kMirrors[fMirror];
    const std::string q = client::http::urlEncode(f.fQuery);
    const std::string sort =
        sortParam(f.fSort, f.fDescending, mirror.fStyle);
    const int status = statusId(f.fCategory);
    std::string url;
    switch (mirror.fStyle) {
    case MirrorStyle::kNerinyan: {
      url = std::format(
          "https://api.nerinyan.moe/search?q={}&m={}&ps={}&p={}", q,
          f.fRuleset, kSearchPageSize, offset / kSearchPageSize);
      if (const char *name = statusName(f.fCategory); *name != '\0') {
        url += std::format("&s={}", name);
      }
      // Extra and explicit content are server-side here, and only here.
      std::string extra;
      if (f.fExtra[0]) {
        extra += "video";
      }
      if (f.fExtra[1]) {
        extra += extra.empty() ? "storyboard" : ".storyboard";
      }
      if (!extra.empty()) {
        url += "&e=" + extra;
      }
      url += f.fExplicit == client::listing::Explicit::kShow ? "&nsfw=true"
                                                             : "&nsfw=false";
      break;
    }
    case MirrorStyle::kOsuDirect:
      url = std::format(
          "https://osu.direct/api/v2/search?q={}&mode={}&amount={}&offset={}",
          q, f.fRuleset, kSearchPageSize, offset);
      if (status != 100) {
        url += std::format("&status={}", status);
      }
      break;
    case MirrorStyle::kMino:
      url = std::format(
          "https://catboy.best/api/v2/search?query={}&mode={}&limit={}&offset={}",
          q, f.fRuleset, kSearchPageSize, offset);
      if (status != 100) {
        url += std::format("&status={}", status);
      }
      break;
    }
    if (!sort.empty()) {
      url += "&sort=" + sort;
    }
    return url;
  }

  void startSearch() {
    fSearchOffset = 0;
    fMoreAvailable = true;
    // Pages already in flight belong to the previous query; without this they
    // arrive afterwards and are appended to the new results.
    ++fSearchGeneration;
    fSearchPending = false;
    fListing.resetSortForSearch();
    fListing.scrollToStart();
    fFound.clear();
    this->fetchPage();
  }

  // The next page of the current search, appended to what is already there.
  void fetchPage() {
    if (fSearchPending || !fMoreAvailable) {
      return;
    }
    fSearchPending = true;
    fDownloadStatus = fSearchOffset == 0 ? "Searching..." : "Loading more...";
    const int offset = fSearchOffset;
    const std::uint32_t generation = fSearchGeneration;
    auto handle = std::make_shared<client::http::Handle>();
    client::http::get(this->searchUrl(offset), std::move(handle),
                      [this, offset, generation](client::http::Response r) {
                        if (generation != fSearchGeneration) {
                          return; // the query moved on while this was in flight
                        }
                        this->onSearchDone(offset, std::move(r));
                      });
  }

  void onSearchDone(int offset, client::http::Response r) {
    fSearchPending = false;
    if (!r.fOk) {
      // A mirror that will not answer is replaced by the next one; the same
      // page is then asked of it.
      if (fMirror + 1 < kMirrors.size()) {
        ++fMirror;
        std::println(std::cerr, "[listing] {} failed ({}), falling back to {}",
                     kMirrors[fMirror - 1].fName, r.fError,
                     kMirrors[fMirror].fName);
        this->fetchPage();
        return;
      }
      fDownloadStatus = "Search failed: " + r.fError;
      fMoreAvailable = false; // stop the scroll from asking again every frame
      this->notify("search failed: " + r.fError,
                   skia::colorSetARGB(255, 255, 110, 110));
      return;
    }
    const auto parsed = bjson::tryParse(r.fBody);
    if (!parsed) {
      fDownloadStatus = "Search failed: malformed JSON";
      fMoreAvailable = false;
      return;
    }
    const bjson::array *arr = parsed->if_array();
    if (arr == nullptr) {
      // Some mirrors wrap the array in an envelope object.
      if (const bjson::object *obj = parsed->if_object()) {
        if (const bjson::value *data = obj->if_contains("data")) {
          arr = data->if_array();
        }
      }
    }
    if (arr == nullptr) {
      fDownloadStatus = "Search failed: unexpected response shape";
      fMoreAvailable = false;
      return;
    }

    const auto getNum = [](const bjson::object &o,
                           std::string_view key) -> double {
      if (const bjson::value *v = o.if_contains(key)) {
        if (v->is_number()) {
          return v->to_number<double>();
        }
      }
      return 0.0;
    };
    const auto getBool = [](const bjson::object &o,
                            std::string_view key) -> bool {
      if (const bjson::value *v = o.if_contains(key)) {
        if (const bool *b = v->if_bool()) {
          return *b;
        }
      }
      return false;
    };
    const auto getStr = [](const bjson::object &o,
                           std::string_view key) -> std::string {
      if (const bjson::value *v = o.if_contains(key)) {
        if (const bjson::string *str = v->if_string()) {
          return std::string(str->begin(), str->end());
        }
      }
      return {};
    };

    if (offset == 0) {
      fFound.clear();
    }
    const std::size_t before = fFound.size();
    for (const auto &e : *arr) {
      const bjson::object *o = e.if_object();
      if (o == nullptr) {
        continue;
      }
      client::listing::Entry d;
      const bjson::value *id = o->if_contains("id");
      if (id == nullptr || !id->is_int64()) {
        continue;
      }
      d.fSetId = static_cast<long>(id->as_int64());
      d.fTitle = getStr(*o, "title");
      d.fTitleUnicode = getStr(*o, "title_unicode");
      d.fArtist = getStr(*o, "artist");
      d.fArtistUnicode = getStr(*o, "artist_unicode");
      d.fCreator = getStr(*o, "creator");
      d.fStatus = getStr(*o, "status");
      d.fUpdated = getStr(*o, "last_updated").substr(0, 10);
      d.fUpdatedDate = dateStamp(getStr(*o, "last_updated"));
      d.fRankedDate = dateStamp(getStr(*o, "ranked_date"));
      d.fSource = getStr(*o, "source");
      if (const bjson::value *covers = o->if_contains("covers")) {
        if (const bjson::object *co = covers->if_object()) {
          // card@2x for the cards, cover@2x for the set page: the sizes
          // lazer's BeatmapSetCoverType picks for each.
          d.fCardCover = getStr(*co, "card@2x");
          if (d.fCardCover.empty()) {
            d.fCardCover = getStr(*co, "card");
          }
          d.fFullCover = getStr(*co, "cover@2x");
          if (d.fFullCover.empty()) {
            d.fFullCover = getStr(*co, "cover");
          }
        }
      }
      if (const bjson::value *ratings = o->if_contains("ratings")) {
        if (const bjson::array *ra = ratings->if_array()) {
          for (const auto &v : *ra) {
            d.fRatings.push_back(v.is_number()
                                     ? static_cast<int>(v.to_number<double>())
                                     : 0);
          }
        }
      }
      d.fTags = getStr(*o, "tags");
      d.fBpm = getNum(*o, "bpm");
      d.fRating = getNum(*o, "rating");
      d.fPlayCount = static_cast<long>(getNum(*o, "play_count"));
      d.fFavouriteCount = static_cast<long>(getNum(*o, "favourite_count"));
      d.fVideo = getBool(*o, "video");
      d.fStoryboard = getBool(*o, "storyboard");
      d.fNsfw = getBool(*o, "nsfw");
      d.fSpotlight = getBool(*o, "spotlight");
      if (const bjson::value *track = o->if_contains("track_id")) {
        d.fFeatured = !track->is_null();
      }
      if (const bjson::value *g = o->if_contains("genre")) {
        if (const bjson::object *go = g->if_object()) {
          d.fGenre = static_cast<int>(getNum(*go, "id"));
        }
      }
      if (const bjson::value *l = o->if_contains("language")) {
        if (const bjson::object *lo = l->if_object()) {
          d.fLanguage = static_cast<int>(getNum(*lo, "id"));
        }
      }
      if (const bjson::value *bms = o->if_contains("beatmaps")) {
        if (const bjson::array *ba = bms->if_array()) {
          for (const auto &bm : *ba) {
            const bjson::object *bo = bm.if_object();
            if (bo == nullptr) {
              continue;
            }
            const bjson::value *sr = bo->if_contains("difficulty_rating");
            if (sr == nullptr || !sr->is_number()) {
              continue;
            }
            const auto v = static_cast<float>(sr->to_number<double>());
            client::listing::Entry::Difficulty diff;
            diff.fStars = v;
            diff.fVersion = getStr(*bo, "version");
            diff.fMode = static_cast<int>(getNum(*bo, "mode_int"));
            diff.fLengthMs = getNum(*bo, "total_length") * 1000.0;
            diff.fCs = getNum(*bo, "cs");
            diff.fAr = getNum(*bo, "ar");
            diff.fOd = getNum(*bo, "accuracy");
            diff.fHp = getNum(*bo, "drain");
            diff.fMaxCombo = static_cast<int>(getNum(*bo, "max_combo"));
            d.fDiffs.push_back(std::move(diff));
            if (d.fDiffCount == 0) {
              d.fStarsMin = d.fStarsMax = v;
            } else {
              d.fStarsMin = std::min(d.fStarsMin, v);
              d.fStarsMax = std::max(d.fStarsMax, v);
            }
            ++d.fDiffCount;
          }
          std::ranges::sort(d.fDiffs, {},
                            &client::listing::Entry::Difficulty::fStars);
        }
      }
      fFound.push_back(std::move(d));
    }
    this->markOwnedResults();
    const std::size_t added = fFound.size() - before;
    fSearchOffset = offset + kSearchPageSize;
    fMoreAvailable = added >= static_cast<std::size_t>(kSearchPageSize) / 2;
    fDownloadStatus = std::format("{} results", fFound.size());
  }

  // PlayButton on the card thumbnail: fetches the 10-second preview osu!
  // serves for every set and plays it on its own source.
  void togglePreview(std::size_t idx) {
    if (idx >= fFound.size()) {
      std::println(std::cerr, "[preview] no entry at {}", idx);
      return;
    }
    const long id = fFound[idx].fSetId;
    if (fPreviewId == id) {
      fPreview.stop();
      fPreviewId = -1;
      this->restoreMusic();
      return;
    }
    fPreview.stop();
    fPreviewId = -1;
    ++fPreviewGeneration; // whatever is in flight is no longer wanted
    const std::uint32_t generation = fPreviewGeneration;
    fPreviewPending = true;
    // A preview replaces the menu music while it runs, as lazer's does;
    // playing both at once doubles the level and the limiter clamps it.
    if (!fMusicDucked && fAudio.playing()) {
      fAudio.pause();
      fMusicDucked = true;
    }
    const std::string url = std::format("https://b.ppy.sh/preview/{}.mp3", id);
    std::println(std::cerr, "[preview] fetching {}", url);
    auto handle = std::make_shared<client::http::Handle>();
    client::http::get(url, std::move(handle),
                      [this, id, generation](client::http::Response r) {
                        if (generation != fPreviewGeneration) {
                          return; // superseded by a later click
                        }
                        fPreviewPending = false;
                        if (!r.fOk || r.fBody.size() < 1024) {
                          std::println(std::cerr,
                                       "[preview] fetch failed: {} ({} bytes) {}",
                                       r.fStatus, r.fBody.size(), r.fError);
                          this->notify("preview unavailable",
                                       skia::colorSetARGB(255, 255, 110, 110));
                          return;
                        }
                        const std::vector<std::uint8_t> bytes(r.fBody.begin(),
                                                              r.fBody.end());
                        if (!fPreview.load(bytes, ".mp3")) {
                          std::println(std::cerr,
                                       "[preview] decode failed ({} bytes)",
                                       bytes.size());
                          return;
                        }
                        fPreview.setLooping(false);
                        fPreview.setVolume(this->musicGain());
                        fPreview.play();
                        fPreviewId = id;
                        std::println(std::cerr, "[preview] playing {} ({:.1f}s)",
                                     id, fPreview.durationSec());
                      });
  }

  void restoreMusic() {
    if (fMusicDucked) {
      fMusicDucked = false;
      fAudio.resume();
    }
  }

  // How far through the preview is, for the ring around the play button.
  [[nodiscard]] float previewProgress() const {
    const double duration = fPreview.durationSec();
    if (fPreviewId < 0 || duration <= 0.0) {
      return 0.0f;
    }
    return static_cast<float>(fPreview.positionSec() / duration);
  }

  // The page shows covers.cover@2x (1920x360), not the card crop.
  void requestPageCover(std::size_t idx) {
    if (idx >= fFound.size()) {
      return;
    }
    auto &d = fFound[idx];
    if (d.fPageCoverSt != client::listing::Entry::Cover::kNone) {
      return;
    }
    d.fPageCoverSt = client::listing::Entry::Cover::kFetching;
    const long id = d.fSetId;
    const std::string url =
        d.fFullCover.empty()
            ? std::format("https://assets.ppy.sh/beatmaps/{}/covers/cover@2x.jpg",
                          id)
            : d.fFullCover;
    auto handle = std::make_shared<client::http::Handle>();
    client::http::get(url, std::move(handle),
                      [this, id](client::http::Response r) {
                        for (auto &e : fFound) {
                          if (e.fSetId != id) {
                            continue;
                          }
                          if (r.fOk && r.fBody.size() > 256) {
                            const std::vector<std::uint8_t> bytes(
                                r.fBody.begin(), r.fBody.end());
                            e.fPageCover = loadImage(bytes);
                          }
                          e.fPageCoverSt =
                              e.fPageCover
                                  ? client::listing::Entry::Cover::kReady
                                  : client::listing::Entry::Cover::kFailed;
                          break;
                        }
                      });
  }

  void requestThumb(std::size_t idx) {
    if (idx >= fFound.size()) {
      return;
    }
    auto &d = fFound[idx];
    if (d.fThumbSt != client::listing::Entry::Thumb::kNone) {
      return;
    }
    int inflight = 0;
    for (const auto &e : fFound) {
      if (e.fThumbSt == client::listing::Entry::Thumb::kFetching) {
        ++inflight;
      }
    }
    if (inflight >= 4) {
      return; // retry on a later frame
    }
    d.fThumbSt = client::listing::Entry::Thumb::kFetching;
    const long id = d.fSetId;
    const std::string url =
        d.fCardCover.empty()
            ? std::format("https://assets.ppy.sh/beatmaps/{}/covers/card@2x.jpg",
                          id)
            : d.fCardCover;
    auto handle = std::make_shared<client::http::Handle>();
    client::http::get(
        url, std::move(handle), [this, id](client::http::Response r) {
          for (std::size_t i = 0; i < fFound.size(); ++i) {
            auto &e = fFound[i];
            if (e.fSetId != id) {
              continue;
            }
            if (r.fOk && r.fBody.size() > 256) {
              std::vector<std::uint8_t> bytes(r.fBody.begin(), r.fBody.end());
              e.fThumb = loadImage(bytes);
              e.fThumbSt = e.fThumb ? client::listing::Entry::Thumb::kReady
                                    : client::listing::Entry::Thumb::kFailed;
            } else {
              e.fThumbSt = client::listing::Entry::Thumb::kFailed;
            }
            // The card draws the image out of the entry, so it cannot notice
            // this by itself: one card is marked, rather than the screen.
            fListing.entryChanged(static_cast<int>(i));
            break;
          }
        });
  }

  [[nodiscard]] std::size_t indexOfSet(long id) const {
    for (std::size_t i = 0; i < fFound.size(); ++i) {
      if (fFound[i].fSetId == id) {
        return i;
      }
    }
    return fFound.size();
  }

  void startDownloadForSet(long id) { this->startDownload(this->indexOfSet(id)); }
  void togglePreviewForSet(long id) { this->togglePreview(this->indexOfSet(id)); }

  void startDownload(std::size_t idx) {
    if (idx >= fFound.size()) {
      return;
    }
    auto &d = fFound[idx];
    if (d.fSt == client::listing::Entry::St::kFetching) {
      return;
    }
    if (this->libraryIndexForSet(static_cast<int>(d.fSetId)) >= 0) {
      d.fSt = client::listing::Entry::St::kDone;
      this->notify("already in the library");
      return;
    }
    if (d.fSt == client::listing::Entry::St::kDone) {
      return;
    }
    d.fSt = client::listing::Entry::St::kFetching;
    const long id = d.fSetId;
    auto handle = std::make_shared<client::http::Handle>();
    fTransfers[id] = handle;
    d.fProgress = 0.0f;
    const std::string url = std::vformat(kMirrors[fMirror].fDownload,
                                         std::make_format_args(id));
    client::http::get(url, std::move(handle),
                      [this, id](client::http::Response r) {
                        this->onDownloadDone(id, std::move(r));
                      });
  }

  void onDownloadDone(long id, client::http::Response r) {
    fTransfers.erase(id);
    client::listing::Entry *d = nullptr;
    for (auto &e : fFound) {
      if (e.fSetId == id) {
        d = &e;
        break;
      }
    }
    if (!r.fOk || r.fBody.size() < 1024) {
      if (d != nullptr) {
        d->fSt = client::listing::Entry::St::kError;
      }
      fDownloadStatus =
          "Download failed: " + (r.fError.empty() ? "empty file" : r.fError);
      this->notify(std::format("download failed: {}",
                               r.fError.empty() ? "empty file" : r.fError),
                   skia::colorSetARGB(255, 255, 110, 110));
      return;
    }
    const auto path = fMapsDir / std::format("{}.osz", id);
    {
      std::ofstream out(path, std::ios::binary);
      out.write(r.fBody.data(),
                static_cast<std::streamsize>(r.fBody.size()));
    }
#ifdef __EMSCRIPTEN__
    EM_ASM(FS.syncfs(false, function(err) {}));
#endif
    if (this->addOszToLibrary(path, true)) {
      if (d != nullptr) {
        d->fSt = client::listing::Entry::St::kDone;
      }
      fDownloadStatus = "Added to library: " +
                        (d != nullptr ? d->fTitle : std::to_string(id));
      this->notify(std::format("imported {}",
                               d != nullptr ? d->fTitle : std::to_string(id)),
                   skia::colorSetARGB(255, 120, 220, 120));
      this->markOwnedResults();
    } else if (d != nullptr) {
      d->fSt = client::listing::Entry::St::kError;
    }
  }

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
    if (fw != fScreenW || fh != fScreenH) {
      this->resize(fw, fh);
    }

    if (fQuit.load(std::memory_order_acquire) ||
        glfw::glfwWindowShouldClose(fWindow)) {
      if (fEngine &&
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
      this->damageAll("artwork cleared");
      return;
    }
    auto image = std::make_shared<skia::Sp<skia::SkImage>>();
    fLoader.submit(static_cast<std::uint64_t>(setIndex) | (4ull << 32),
                   [bytes = std::move(bytes), image] {
                     *image = loadImage(bytes);
                   },
                   [this, setIndex, image] {
                     if (setIndex != fSelSet || !*image) {
                       return;
                     }
                     fView.setBackground(*image);
                     fView.preScaleBackground(this->gameplayCtx(nullptr));
                     this->damageAll("artwork arrived");
                     this->requestRedraw(400.0);
                   });
  }

  void loadSelectBackground(const osu::BeatmapSet &set) {
    this->damageAll("select background"); // the backdrop changes either way
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
    client::ui::Painter(canvas, fFont).fillRounded(rect, radius, color);
  }

  void strokeRounded(skia::SkCanvas *canvas, const skia::SkRect &rect,
                     float radius, skia::SkColor color, float width) {
    client::ui::Painter(canvas, fFont).strokeRounded(rect, radius, color, width);
  }

  void drawTextClipped(skia::SkCanvas *canvas, const std::string &text,
                       float x, float y, float maxW, float size,
                       skia::SkColor color, float alpha = 1.0f) {
    client::ui::Painter(canvas, fFont)
        .textClipped(text, x, y, maxW, size, color, alpha);
  }

  void drawTextCentered(skia::SkCanvas *canvas, const std::string &text,
                        float cx, float y, float size, skia::SkColor color,
                        float alpha = 1.0f) {
    client::ui::Painter(canvas, fFont)
        .textCentered(text, cx, y, size, color, alpha);
  }

  [[nodiscard]] static skia::SkColor starColor(double stars) {
    return client::ui::starColor(stars);
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

  void ensureMenuButtons() {
    if (!fMenuBtns.empty()) {
      return;
    }
    // Colours lifted from lazer's ButtonSystem so the palette reads familiar.
    // ButtonArea's leftmost entry is Settings, present at the top level.
    MenuBtn settings{"settings", "⚙", skia::colorSetARGB(255, 85, 85, 85),
                     MenuAction::kSettings, MenuState::kTopLevel};
    settings.fLeftSide = true;
    fMenuBtns.push_back(std::move(settings));

    fMenuBtns.push_back({"play", "▶", skia::colorSetARGB(255, 102, 68, 204),
                         MenuAction::kOpenPlay, MenuState::kTopLevel});
    fMenuBtns.push_back({"browse", "↓", skia::colorSetARGB(255, 165, 204, 0),
                         MenuAction::kBrowse, MenuState::kTopLevel});
    fMenuBtns.push_back({"import", "+", skia::colorSetARGB(255, 238, 170, 0),
                         MenuAction::kImport, MenuState::kTopLevel});
    fMenuBtns.push_back({"exit", "×", skia::colorSetARGB(255, 238, 51, 153),
                         MenuAction::kExit, MenuState::kTopLevel});

    fMenuBtns.push_back({"solo", "●", skia::colorSetARGB(255, 102, 68, 204),
                         MenuAction::kSolo, MenuState::kPlay});
    fMenuBtns.push_back({"random", "↻", skia::colorSetARGB(255, 94, 63, 186),
                         MenuAction::kRandom, MenuState::kPlay});

    MenuBtn back{"back", "←", skia::colorSetARGB(255, 51, 58, 94),
                 MenuAction::kBack, MenuState::kPlay};
    back.fLeftSide = true;
    fMenuBtns.push_back(std::move(back));
  }

  [[nodiscard]] static const char *menuStateName(MenuState st) {
    switch (st) {
    case MenuState::kInitial: return "initial";
    case MenuState::kTopLevel: return "top-level";
    case MenuState::kPlay: return "play";
    }
    return "?";
  }

  void setMenuState(MenuState st) {
    if (fMenuState == st) {
      return;
    }
    std::println(std::cerr, "[menu] {} -> {}", menuStateName(fMenuState),
                 menuStateName(st));
    fMenuState = st;
  }

  [[nodiscard]] float approach(float current, float target, float tauMs) const {
    return client::ui::approach(current, target, tauMs, fUiDt);
  }

  void menuTrigger(MenuBtn &b) {
    b.fFlash = 1.0f;
    switch (b.fAction) {
    case MenuAction::kOpenPlay:
      this->setMenuState(MenuState::kPlay);
      break;
    case MenuAction::kBrowse:
      this->openDownloads();
      break;
    case MenuAction::kImport:
      this->importOsz();
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
    case MenuAction::kBack:
      this->setMenuState(MenuState::kTopLevel);
      break;
    case MenuAction::kSettings:
      this->toggleSettings();
      break;
    }
  }

  // F2 in song select: move the selection (lazer's FooterButtonRandom).
  void selectRandom() {
    if (fVisible.empty()) {
      return;
    }
    std::uniform_int_distribution<std::size_t> pick(0, fVisible.size() - 1);
    fSelSet = fVisible[pick(fUiRng)];
    fSelDiff = 0;
  }

  // "random" in the menu's play submenu: pick a map and start it.
  void playRandom() {
    if (fLibrary.empty()) {
      this->switchState(State::kSongSelect);
      return;
    }
    std::uniform_int_distribution<std::size_t> pick(0, fLibrary.size() - 1);
    const auto si = pick(fUiRng);
    const auto &infos = fLibrary[si].fInfos;
    if (infos.empty()) {
      return;
    }
    std::uniform_int_distribution<std::size_t> pickDiff(0, infos.size() - 1);
    fSelSet = static_cast<int>(si);
    fSelDiff = static_cast<int>(pickDiff(fUiRng));
    this->startPlay(fSelSet, fSelDiff);
  }

  // The logo is the menu's primary control: clicking advances a level, and at
  // a populated level it triggers that level's first button (onOsuLogo).
  void triggerLogo() {
    fLogoPunch = 1.0f;
    switch (fMenuState) {
    case MenuState::kInitial:
      this->setMenuState(MenuState::kTopLevel);
      break;
    case MenuState::kTopLevel:
    case MenuState::kPlay:
      for (auto &b : fMenuBtns) {
        if (b.fVisible == fMenuState && !b.fLeftSide) {
          this->menuTrigger(b);
          break;
        }
      }
      break;
    }
  }


  // The background field, when there is no artwork to show: client.triangles
  // is the port of TrianglesV2, and this is the same one the pause buttons
  // and the logo use.
  void drawMenuTriangles(skia::SkCanvas *canvas) {
    const float sw = static_cast<float>(fScreenW);
    const float sh = static_cast<float>(fScreenH);
    fBackgroundTriangles.setScaleAdjust(2.4f);
    fBackgroundTriangles.setAlphaRange(0.06f, 0.16f);
    fBackgroundTriangles.draw(canvas, skia::SkRect::MakeWH(sw, sh),
                              fSettings.flag("menutriangles") ? fUiDt : 0.0,
                              1.0f);
  }


  // ---- Main menu ---------------------------------------------------------
  //
  // Split the way the ported screens are: everything that decides what the
  // menu looks like happens here, with nothing drawn, so that what changed is
  // known before the client commits to a frame.

  void updateMainMenu() {
    this->ensureMenuButtons();
    this->updateMenuSpectrum();

    const float sw = static_cast<float>(fScreenW);
    const float sh = static_cast<float>(fScreenH);

    if (!fLibrary.empty() && fBackgroundForSet != fSelSet) {
      // setFor() is asynchronous: only mark the background as up to date once
      // the set has actually arrived, otherwise the first frame consumes the
      // request and the artwork never appears.
      if (auto set = this->setFor(fSelSet)) {
        fBackgroundForSet = fSelSet;
        this->requestBackground(fSelSet, set);
      }
    }
    const float dimTarget = fMenuState == MenuState::kInitial ? 1.0f : 0.8f;
    fMenuDim = this->approach(fMenuDim, dimTarget, 220.0f);

    // What moves here is the logo with its visualiser and the buttons; the
    // background is the beatmap's artwork, which sits still. The dim fade and
    // the triangle fallback do cover the screen, so those say so.
    // Compared with a tolerance: an eased value that has settled must stop
    // counting as a change, or this fires on every frame for ever.
    if (std::abs(fMenuDim - fDrawnMenuDim) > 0.001f) {
      this->damageAll("menu dim");
    } else if (!fView.hasBackground() && fLibrary.empty() &&
               fSettings.flag("menutriangles")) {
      this->damageAll("triangle background, drifting");
    }
    fDrawnMenuDim = fMenuDim;

    // ---- Layout: logo plus the visible button row, centred as a group.
    const float uiScale = std::clamp(sh / 900.0f, 0.75f, 1.6f);
    const float btnW = 150.0f * uiScale;
    const float btnH = 96.0f * uiScale;
    const float btnGap = 6.0f * uiScale;
    fMenuWedge = 20.0f * uiScale; // lazer's parallelogram shear

    int rightCount = 0;
    int leftCount = 0;
    for (const auto &b : fMenuBtns) {
      if (b.fVisible != fMenuState) {
        continue;
      }
      if (b.fLeftSide) {
        ++leftCount;
      } else {
        ++rightCount;
      }
    }

    fLogoBase = std::min(sw, sh) * 0.17f;
    const float logoR =
        fLogoBase * (fMenuState == MenuState::kInitial ? 1.0f : 0.62f);
    const float rightW = static_cast<float>(rightCount) * (btnW + btnGap);
    const float leftW = static_cast<float>(leftCount) * (btnW + btnGap);
    const float groupW = leftW + 2.0f * logoR + 28.0f * uiScale + rightW;

    const float targetLogoX =
        fMenuState == MenuState::kInitial
            ? sw * 0.5f
            : (sw - groupW) * 0.5f + leftW + logoR;
    const float targetLogoY =
        sh * (fMenuState == MenuState::kInitial ? 0.46f : 0.5f);
    const float targetScale = fMenuState == MenuState::kInitial ? 1.0f : 0.62f;

    if (!fLogoPlaced) {
      fLogoX = targetLogoX;
      fLogoY = targetLogoY;
      fLogoScale = targetScale;
      fLogoPlaced = true;
    }
    fLogoX = this->approach(fLogoX, targetLogoX, 140.0f);
    fLogoY = this->approach(fLogoY, targetLogoY, 140.0f);
    fLogoScale = this->approach(fLogoScale, targetScale, 140.0f);

    // ---- Buttons: animate and lay out, without drawing any of them.
    const float rowY = fLogoY - btnH * 0.5f;
    float xRight = fLogoX + fLogoBase * fLogoScale + 28.0f * uiScale;
    float xLeft = fLogoX - fLogoBase * fLogoScale - 28.0f * uiScale;

    for (auto &b : fMenuBtns) {
      const bool visible = b.fVisible == fMenuState;
      b.fExpand = this->approach(b.fExpand, visible ? 1.0f : 0.0f, 95.0f);
      b.fFlash = this->approach(b.fFlash, 0.0f, 160.0f);

      if (b.fExpand < 0.01f) {
        b.fRect = skia::SkRect::MakeEmpty();
        continue;
      }

      const float w = btnW * b.fExpand * (1.0f + 0.18f * b.fHover);
      skia::SkRect rect;
      if (b.fLeftSide) {
        rect = skia::SkRect::MakeXYWH(xLeft - w, rowY, w, btnH);
        xLeft -= w + btnGap;
      } else {
        rect = skia::SkRect::MakeXYWH(xRight, rowY, w, btnH);
        xRight += w + btnGap;
      }
      b.fRect = rect;

      const bool hovered = visible && rect.contains(fMouseX, fMouseY);
      b.fHover = this->approach(b.fHover, hovered ? 1.0f : 0.0f, 110.0f);
    }

    this->settleLogo(fLogoBase);

    fMenu.ensure(fMenuBtns.size(), skia::SkRect::MakeWH(sw, sh));
    fMenu.setPointer(fMouseX, fMouseY);

    // How far the bars actually reach this frame. A flat guess of three
    // quarters of the logo's width was covering 40% of the screen on its own,
    // which with the counter in the opposite corner pushed the frame over the
    // "repaint it whole" threshold and saved nothing at all.
    float loudest = 0.0f;
    for (const float amp : fSettings.flag("visualiser") ? fSpectrum.bars()
                                                        : this->stillBars()) {
      loudest = std::max(loudest, amp);
    }
    const float reach = fLogoRadius * 2.0f * (600.0f / 480.0f) * loudest;
    skia::SkRect moving = fLogoRect;
    moving.outset(reach + 4.0f, reach + 4.0f);
    // Marked while something in there is actually moving -- a live
    // visualiser, drifting triangles inside the logo, or the logo itself
    // having shifted. Marking it every frame regardless is a repaint of the
    // busiest part of the screen for a picture that is identical.
    fMenu.placeLogo(moving);
    if (fSettings.flag("menutriangles") || fSettings.flag("visualiser")) {
      fMenu.markLogo(); // something inside the same box is moving
    }

    // A button only needs repainting while something about it changes. Its
    // drawn shape is a parallelogram sheared by the wedge, and the label and
    // glow reach past that, so the marked area is its rectangle grown by the
    // shear plus a margin -- and by the rectangle it occupied before, or a
    // button that moved leaves its old self behind.
    for (auto &b : fMenuBtns) {
      // The box a button occupies, grown by the shear and a margin: its
      // parallelogram leans out of its rectangle, and the label and glow
      // reach past that.
      skia::SkRect box = b.fRect;
      if (!box.isEmpty()) {
        box.outset(fMenuWedge + 12.0f, 12.0f);
      }
      fMenu.placeButton(static_cast<std::size_t>(&b - fMenuBtns.data()), box);
      // Eased values approach their target without reaching it; comparing
      // them exactly kept every button "changing" for ever, which is what
      // held the repainted region open around the whole row.
      constexpr float kSettled = 0.002f;
      const bool changed = std::abs(b.fExpand - b.fDrawnExpand) > kSettled ||
                           std::abs(b.fHover - b.fDrawnHover) > kSettled ||
                           std::abs(b.fFlash - b.fDrawnFlash) > kSettled ||
                           b.fRect != b.fDrawnRect;
      if (!changed) {
        continue;
      }
      fMenu.markButton(static_cast<std::size_t>(&b - fMenuBtns.data()));
      b.fDrawnExpand = b.fExpand;
      b.fDrawnHover = b.fHover;
      b.fDrawnFlash = b.fFlash;
      b.fDrawnRect = b.fRect;
    }
    this->damage(fMenu.takeDamage());
  }

  // lazer's main menu shows the beatmap background at full brightness --
  // MainMenu.cs fades it to Gray(1) at the logo-only state and Gray(0.8)
  // once the buttons are out. There is no triangle overlay over artwork;
  // triangles are only the fallback background when no art exists at all.
  void drawMenuBackground(skia::SkCanvas *canvas) {
    const float sw = static_cast<float>(fScreenW);
    const float sh = static_cast<float>(fScreenH);
    if (fView.hasBackground()) {
      fView.drawBackground(this->gameplayCtx(canvas), canvas);
      if (fMenuDim < 0.999f) {
        skia::SkPaint dim;
        dim.setColor(skia::colorSetARGB(
            static_cast<std::uint8_t>((1.0f - fMenuDim) * 255.0f), 0, 0, 0));
        canvas->drawRect(skia::SkRect::MakeXYWH(0, 0, sw, sh), dim);
      }
    } else if (fLibrary.empty()) {
      // Only with no beatmaps at all does lazer's default (triangle)
      // background show; while artwork is still loading, stay dark.
      canvas->clear(skia::colorSetARGB(255, 32, 24, 44));
      this->drawMenuTriangles(canvas);
    } else {
      canvas->clear(skia::colorSetARGB(255, 18, 14, 24));
    }
  }

  void frameMainMenu() {
    auto *canvas = fSurface->getCanvas();
    fMenu.render(canvas);
    this->drawScreenFadeIn(canvas);
    this->present();
  }

  // A lazer menu button is a parallelogram: vertical edges sheared by a fixed
  // wedge width, label under a glyph, both fading in only once the button is
  // most of the way open (lazer clamps content alpha the same way).
  void drawMenuWedge(skia::SkCanvas *canvas, const MenuBtn &b, float wedge) {
    const skia::SkRect &r = b.fRect;
    skia::SkPathBuilder shape;
    shape.moveTo(r.fLeft + wedge, r.fTop);
    shape.lineTo(r.fRight + wedge, r.fTop);
    shape.lineTo(r.fRight, r.fBottom);
    shape.lineTo(r.fLeft, r.fBottom);
    shape.close();
    const auto path = shape.detach();

    skia::SkPaint fill;
    fill.setAntiAlias(true);
    fill.setColor(b.fColor);
    // Hover brightens; the click flash blows it out briefly, then decays.
    fill.setAlphaf(std::clamp(b.fExpand * (0.86f + 0.14f * b.fHover), 0.0f,
                              1.0f));
    canvas->drawPath(path, fill);

    if (b.fFlash > 0.01f) {
      skia::SkPaint flash;
      flash.setAntiAlias(true);
      flash.setColor(skia::kWhite);
      flash.setAlphaf(0.55f * b.fFlash);
      flash.setBlendMode(skia::SkBlendMode::kPlus);
      canvas->drawPath(path, flash);
    }

    const float contentAlpha =
        std::clamp((b.fExpand - 0.5f) / 0.3f, 0.0f, 1.0f);
    if (contentAlpha <= 0.0f) {
      return;
    }
    const float cx = r.centerX() + wedge * 0.5f;
    // Icon lifts slightly on hover, mirroring lazer's bouncing icon.
    const float lift = 6.0f * b.fHover;
    this->drawTextCentered(canvas, b.fGlyph, cx, r.centerY() - lift, 30.0f,
                           skia::kWhite, contentAlpha);
    this->drawTextCentered(canvas, b.fLabel, cx, r.fBottom - 18.0f, 17.0f,
                           skia::kWhite, contentAlpha * 0.95f);
  }

  // OsuLogo: a circular container filled with a vertical pink gradient
  // (#ff66ab -> #cc5289) with TrianglesV2 masked inside it, the logo mark on
  // top, a ripple of the same shape that scales out and fades on each beat,
  // and a white impact ring that only appears when the logo is struck. There
  // is no permanent halo -- the earlier glow ring was invented.
  // Where the logo is and how big, worked out without drawing anything.
  void settleLogo(float logoBase) {
    const bool hovered =
        (fMouseX - fLogoX) * (fMouseX - fLogoX) +
            (fMouseY - fLogoY) * (fMouseY - fLogoY) <=
        (logoBase * fLogoScale) * (logoBase * fLogoScale);
    fLogoHover = this->approach(fLogoHover, hovered ? 1.0f : 0.0f, 110.0f);
    fLogoPunch = this->approach(fLogoPunch, 0.0f, 180.0f);

    // Amplitude-driven beat: lazer drives these from timing points, which the
    // menu has not loaded, so bass energy stands in. Switched off with the
    // visualiser, or the logo would keep breathing on a screen that has
    // stopped drawing frames and would jump whenever one happened.
    fLogoAmp = fSettings.flag("visualiser") ? fSpectrum.bass() : 0.0f;
    const float beat = 1.0f - 0.02f * fLogoAmp;
    fLogoRadius = logoBase * fLogoScale * beat *
                  (1.0f + 0.06f * fLogoHover + 0.10f * fLogoPunch);
    fLogoRect = skia::SkRect::MakeXYWH(fLogoX - fLogoRadius,
                                       fLogoY - fLogoRadius, fLogoRadius * 2,
                                       fLogoRadius * 2);
  }

  void drawLogo(skia::SkCanvas *canvas, float) {
    const float wall = static_cast<float>(wallMs());
    const float amp = fLogoAmp;
    const float r = fLogoRadius;

    this->drawVisualiser(canvas, r);

    // Ripple: same circle, scaled slightly out, alpha 0.15 * amplitude.
    if (amp > 0.01f) {
      skia::SkPaint ripple;
      ripple.setAntiAlias(true);
      ripple.setColor(skia::colorSetARGB(255, 0xff, 0x66, 0xab));
      ripple.setAlphaf(0.15f * std::min(1.0f, amp * 2.0f));
      canvas->drawCircle(fLogoX, fLogoY, r * (1.0f + 0.04f * amp), ripple);
    }

    // Body: vertical gradient disc, clipped triangles, then the mark.
    canvas->save();
    skia::SkPathBuilder disc;
    disc.addCircle(fLogoX, fLogoY, r);
    canvas->clipPath(disc.detach(), true);
    this->drawVerticalGradient(canvas, fLogoRect,
                               skia::colorSetARGB(255, 0xff, 0x66, 0xab),
                               skia::colorSetARGB(255, 0xcc, 0x52, 0x89));
    this->drawLogoTriangles(canvas, fLogoRect);
    canvas->restore();

    // Impact ring: white border, only while punching.
    if (fLogoPunch > 0.01f) {
      skia::SkPaint ring;
      ring.setAntiAlias(true);
      ring.setStyle(skia::kStrokeStyle);
      ring.setStrokeWidth(r * 0.08f);
      ring.setColor(skia::kWhite);
      ring.setAlphaf(fLogoPunch * 0.8f);
      canvas->drawCircle(fLogoX, fLogoY, r * (1.0f + 0.12f * (1.0f - fLogoPunch)),
                         ring);
    }

    this->drawTextCentered(canvas, "osu!", fLogoX, fLogoY + r * 0.22f,
                           r * 0.55f, skia::kWhite);
  }

  // Cheap vertical two-stop gradient without pulling in a shader: a stack of
  // horizontal bands. The logo is small enough that 24 steps are seamless.
  void drawVerticalGradient(skia::SkCanvas *canvas, const skia::SkRect &rect,
                            skia::SkColor top, skia::SkColor bottom) {
    constexpr int kSteps = 24;
    skia::SkPaint p;
    p.setAntiAlias(false);
    for (int i = 0; i < kSteps; ++i) {
      const float t = static_cast<float>(i) / static_cast<float>(kSteps - 1);
      const auto lerp = [t](std::uint32_t a, std::uint32_t b) {
        return static_cast<std::uint8_t>(
            static_cast<float>(a) + (static_cast<float>(b) - static_cast<float>(a)) * t);
      };
      p.setColor(skia::colorSetARGB(
          255, lerp((top >> 16) & 0xFF, (bottom >> 16) & 0xFF),
          lerp((top >> 8) & 0xFF, (bottom >> 8) & 0xFF),
          lerp(top & 0xFF, bottom & 0xFF)));
      const float y0 =
          rect.fTop + rect.height() * static_cast<float>(i) / kSteps;
      canvas->drawRect(
          skia::SkRect::MakeLTRB(rect.fLeft, y0, rect.fRight,
                                 y0 + rect.height() / kSteps + 1.0f),
          p);
    }
  }

  // TrianglesV2 inside the logo: Thickness 0.009, ScaleAdjust 3, SpawnRatio
  // 1.4, tinted #ff66ab at the top to #b6346f at the bottom.
  void drawLogoTriangles(skia::SkCanvas *canvas, const skia::SkRect &rect) {
    fLogoTriangles.setScaleAdjust(1.05f);
    fLogoTriangles.setSpawnRatio(1.4f);
    fLogoTriangles.setThickness(0.009f);
    fLogoTriangles.setAlphaRange(0.85f, 0.85f);
    fLogoTriangles.setColours(skia::colorSetARGB(255, 0xff, 0x66, 0xab),
                              skia::colorSetARGB(255, 0xb6, 0x34, 0x6f));
    fLogoTriangles.draw(canvas, rect,
                        fSettings.flag("menutriangles") ? fUiDt : 0.0, 1.0f);
  }

  // LogoVisualisation.VisualisationDrawNode, transcribed. Each bar is a quad
  // sitting on the logo's circumference, `bar_length * amplitude` long, with
  // width equal to the chord subtended by one bar; the whole ring is drawn
  // `visualiser_rounds` times, rotated, additively at 20% white.
  // What the bars stand at when the visualiser is switched off: a fixed
  // shape, so the logo keeps its skirt instead of sitting on the background
  // as a bare circle. Deterministic on purpose -- it is drawn once and never
  // asks to be drawn again.
  [[nodiscard]] std::span<const float> stillBars() const {
    static const std::vector<float> kBars = [] {
      constexpr int kCount = 200;
      std::vector<float> bars(static_cast<std::size_t>(kCount));
      // A spectrum does not undulate; it spikes. Neighbouring bins differ by
      // a lot, the whole thing slopes down as the frequency rises, and a
      // handful of partials stand well above the rest. So: a falling
      // envelope, a hash per bin for the jumps, and a few peaks on top.
      const auto hash01 = [](std::uint32_t x) {
        x ^= x >> 16;
        x *= 0x7feb352dU;
        x ^= x >> 15;
        x *= 0x846ca68bU;
        x ^= x >> 16;
        return static_cast<float>(x & 0xffffffU) /
               static_cast<float>(0x1000000U);
      };
      for (int i = 0; i < kCount; ++i) {
        const auto index = static_cast<std::uint32_t>(i);
        const float t = static_cast<float>(i) / static_cast<float>(kCount);
        // Loud at the bass end, thin at the top, as music is.
        const float envelope = 0.06f + 0.30f * std::pow(1.0f - t, 1.6f);
        // Squared, so most bins sit low and the occasional one jumps.
        const float jump = hash01(index * 2654435761U);
        float amp = envelope * (0.25f + 1.35f * jump * jump);
        if (hash01(index * 40503U + 17U) > 0.94f) {
          amp *= 2.1f; // a partial standing out of the noise
        }
        // Shorter than a loud moment of music: this one is on screen for as
        // long as the menu is, and a skirt that reaches out as far as a drop
        // does looks wrong standing still.
        bars[static_cast<std::size_t>(i)] = std::clamp(amp * 0.62f, 0.01f,
                                                       0.55f);
      }
      return bars;
    }();
    return kBars;
  }

  void drawVisualiser(skia::SkCanvas *canvas, float logoRadius) {
    const auto bars = fSettings.flag("visualiser") ? fSpectrum.bars()
                                                   : this->stillBars();
    if (bars.empty()) {
      return;
    }
    constexpr int kRounds = 5;          // visualiser_rounds
    constexpr float kAmplitudeDeadZone = 1.0f / 600.0f;
    const auto count = static_cast<int>(bars.size());

    // Nothing above the dead zone is the usual case between tracks.
    bool anyAudible = false;
    for (const float amp : bars) {
      if (amp >= kAmplitudeDeadZone) {
        anyAudible = true;
        break;
      }
    }
    if (!anyAudible) {
      return;
    }

    // A bar at a time, as a small convex path with antialiasing.
    //
    // Two attempts at making this cheaper both cost more, and both for
    // reasons worth keeping written down. Collecting the bars of a round into
    // one path takes Skia off the analytic route it has for convex shapes and
    // onto a coverage mask over the whole circle. Sending them as vertices
    // removes the per-draw overhead but also the antialiasing, and adding it
    // back as a ring of transparent geometry means blending five times the
    // triangles -- which on a software rasteriser is paid per pixel, and cost
    // more than the draws it saved. What is here is what measured best.
    const float barLength = logoRadius * 2.0f * (600.0f / 480.0f);
    // barSize.X = size * sqrt(2 * (1 - cos(360/bars))) / 2  -- the chord.
    const float chord =
        logoRadius * 2.0f *
        std::sqrt(2.0f * (1.0f - std::cos(2.0f * std::numbers::pi_v<float> /
                                          static_cast<float>(count)))) /
        2.0f;

    this->ensureVisualiserAngles(count, kRounds);

    skia::SkPaint paint;
    paint.setAntiAlias(true);
    paint.setColor(skia::kWhite);
    paint.setAlphaf(0.2f); // transparent_white
    paint.setBlendMode(skia::SkBlendMode::kPlus);

    for (int round = 0; round < kRounds; ++round) {
      for (int i = 0; i < count; ++i) {
        const float amp = bars[static_cast<std::size_t>(i)];
        if (amp < kAmplitudeDeadZone) {
          continue;
        }
        const auto &angle =
            fVisualiserAngles[static_cast<std::size_t>(round * count + i)];
        const float bx = fLogoX + angle.fCos * logoRadius;
        const float by = fLogoY + angle.fSin * logoRadius;
        // bottomOffset is perpendicular; amplitudeOffset is radial.
        const float ox = -angle.fSin * chord * 0.5f;
        const float oy = angle.fCos * chord * 0.5f;
        const float ax = angle.fCos * barLength * amp;
        const float ay = angle.fSin * barLength * amp;

        skia::SkPathBuilder bar;
        bar.moveTo(bx - ox, by - oy);
        bar.lineTo(bx - ox + ax, by - oy + ay);
        bar.lineTo(bx + ox + ax, by + oy + ay);
        bar.lineTo(bx + ox, by + oy);
        bar.close();
        canvas->drawPath(bar.detach(), paint);
      }
    }
  }

  // The bars sit at fixed angles; only their lengths change. Computing two
  // trigonometric functions per bar per frame -- two thousand of them at five
  // rounds of two hundred -- was work repeated for an answer that never
  // changes.
  void ensureVisualiserAngles(int count, int rounds) {
    const auto needed = static_cast<std::size_t>(count) *
                        static_cast<std::size_t>(rounds);
    if (fVisualiserAngles.size() == needed && fVisualiserCount == count) {
      return;
    }
    fVisualiserAngles.resize(needed);
    fVisualiserCount = count;
    for (int round = 0; round < rounds; ++round) {
      for (int i = 0; i < count; ++i) {
        const float rotation =
            2.0f * std::numbers::pi_v<float> *
            (static_cast<float>(i) / static_cast<float>(count) +
             static_cast<float>(round) / static_cast<float>(rounds));
        fVisualiserAngles[static_cast<std::size_t>(round * count + i)] = {
            std::cos(rotation), std::sin(rotation)};
      }
    }
  }

  void updateMenuSpectrum() {
    const double wall = wallMs();
    if (!fAudio.playing()) {
      fSpectrum.update({}, 0, 0.0, wall);
      return;
    }
    // Same anchored-clock trick as gameplay: querying the device every frame
    // is what used to cost whole milliseconds per call.
    if (wall - fMenuClockSyncWall >= kClockSyncIntervalMs) {
      fMenuClockSyncWall = wall;
      const double devicePos = fAudio.positionSec() * 1000.0;
      // A looping track jumps back to zero; the anchored clock is monotonic
      // by design, so detect the wrap and re-anchor instead of syncing.
      if (devicePos + 500.0 < fLastMenuPosMs) {
        fMenuClock.reset(wall, devicePos);
      } else {
        fMenuClock.sync(wall, devicePos);
      }
      fLastMenuPosMs = devicePos;
    }
    const double posMs = fMenuClock.sample(wall);
    fSpectrum.update(fAudio.monoSamples(), fAudio.sampleRate(),
                     posMs / 1000.0, wall);
  }

  void keyMainMenu(int key) {
    this->ensureMenuButtons();
    if (key == glfw::kKeyEscape) {
      switch (fMenuState) {
      case MenuState::kPlay:
        this->setMenuState(MenuState::kTopLevel);
        break;
      case MenuState::kTopLevel:
        this->setMenuState(MenuState::kInitial);
        break;
      case MenuState::kInitial:
        this->requestQuit();
        break;
      }
      return;
    }
    if (key == glfw::kKeyEnter || key == glfw::kKeySpace) {
      this->triggerLogo();
      return;
    }
    // Letter shortcuts, as in lazer.
    const auto fire = [this](MenuAction act) {
      for (auto &b : fMenuBtns) {
        if (b.fVisible == fMenuState && b.fAction == act) {
          this->menuTrigger(b);
          return;
        }
      }
    };
    if (fMenuState == MenuState::kTopLevel) {
      if (key == glfw::kKeyP) {
        fire(MenuAction::kOpenPlay);
      } else if (key == glfw::kKeyB || key == glfw::kKeyD) {
        fire(MenuAction::kBrowse);
      } else if (key == glfw::kKeyI) {
        fire(MenuAction::kImport);
      } else if (key == glfw::kKeyQ) {
        fire(MenuAction::kExit);
      }
    } else if (fMenuState == MenuState::kPlay) {
      if (key == glfw::kKeyS) {
        fire(MenuAction::kSolo);
      } else if (key == glfw::kKeyR) {
        fire(MenuAction::kRandom);
      }
    }
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
    fSettingsPanel.scroll(delta, static_cast<float>(fScreenH));
  }

  void applyAudioSettings() {
    fAudio.setVolume(this->musicGain());
    fPreview.setVolume(this->musicGain());
    for (auto &[name, player] : fSamples) {
      player.setVolume(this->effectGain());
    }
  }

  [[nodiscard]] float musicGain() const {
    return fSettings.value("master") * fSettings.value("music") *
           audio_client::kMusicHeadroom;
  }
  [[nodiscard]] float effectGain() const {
    return fSettings.value("master") * fSettings.value("effect") *
           audio_client::kEffectHeadroom;
  }

  void applySettings() {
    this->applyAudioSettings();
    const float dim = fSettings.value("dim");
    if (std::abs(dim - fAppliedDim) > 1e-4f) {
      fAppliedDim = dim;
      fView.preScaleBackground(this->gameplayCtx(nullptr));
    }
    fSwapIntervalRequest.store(fSettings.flag("vsync") ? 1 : 0,
                               std::memory_order_release);
    this->damageAll("settings applied");
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
    const auto entries = this->modEntries();
    fModSelect.draw(canvas, fFont, entries, fMods,
                    {fScreenW, fScreenH, fMouseX, fMouseY, fUiDt});
  }

  bool modClick(float x, float y) {
    fModSelect.touched(); // whatever it hit, the chips draw the answer
    return fModSelect.click(x, y, fMods);
  }

  void drawExportDialog(skia::SkCanvas *canvas) {
    fExportDialog.draw(canvas, fFont, fScreenW, fScreenH, fMouseX, fMouseY);
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

  // Re-renders the replay offscreen at the chosen resolution by driving a
  // fresh engine from the recorded events, then hands the frames to ffmpeg.
  // Rendering a replay to video is minutes of work at sixty frames a second
  // of map time. It used to be a loop: the client stopped answering for the
  // length of it, the dialog showing "rendering..." was the last thing drawn
  // before the window went unresponsive, and there was no way to tell a slow
  // export from a hung one. It is a job with a step now, and the step is
  // bounded by a slice of wall clock, so the client keeps drawing and the
  // dialog keeps counting.
  struct ExportJob {
    // Everything the render needs, copied rather than referred to: the client
    // goes on playing, changing tracks and starting plays while this runs, and
    // a video that follows it around is what the last three of these were.
    std::shared_ptr<client::VideoExporter> fExporter =
        std::make_shared<client::VideoExporter>();
    client::VideoOptions fOpts;
    skia::Sp<skia::SkSurface> fSurface; // raster: no GL on this thread
    osu::Beatmap fMap;
    osu::ComboInfo fCombo;
    std::vector<osu::InputEvent> fEvents;
    osu::ModSet fMods = osu::mod::kNone;
    client::Skin *fSkin = nullptr; // shared, and read only from here
    skia::SkFont fFont;
    skia::SkFont fDisplayFont;
    client::GameplayView fView;
    float fCursorSize = 1.0f;
    float fDim = 0.7f;
    bool fNoGlow = false;
    std::thread fThread;
    std::atomic<int> fPercent{0};
    std::atomic<bool> fFinished{false};
    bool fOk = false;
    std::string fMessage;
  };

  // Said in both places: the dialog is where it belongs, and the log is where
  // it survives being missed -- which, while the export was blocking the
  // client, it always was.
  void exportFailed(std::string reason) {
    std::println(std::cerr, "[export] failed: {}", reason);
    fExportDialog.setStatus(std::move(reason));
  }

  void exportReplayVideo() {
    if (fExportJob) {
      return; // one at a time
    }
    if (!fMap || fRecordedEvents.empty()) {
      this->exportFailed("nothing to export: no play recorded for this map");
      return;
    }
    const auto preset = client::kVideoPresets[static_cast<std::size_t>(
        fExportDialog.preset())];
    // A size typed into the dialog wins over the one picked from the row.
    const auto [typedWidth, typedHeight] = fExportDialog.customSize();
    auto job = std::make_unique<ExportJob>();
    job->fOpts.fWidth = typedWidth > 0 ? typedWidth : preset.fWidth;
    job->fOpts.fHeight = typedHeight > 0 ? typedHeight : preset.fHeight;
    job->fOpts.fFps = 60;

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
    job->fOpts.fOutput =
        (cwdError ? fMapsDir.parent_path() : here) /
        std::format("{}-{}x{}.mp4", safe, job->fOpts.fWidth,
                    job->fOpts.fHeight);

    // Written out before the encoder is started: it is told about its inputs
    // once, when it is launched, and an audio path handed over afterwards
    // reached nobody -- which is why the videos had no sound.
    if (!fMap->fMeta.fAudioFilename.empty()) {
      const auto bytes = fSet.findFile(fMap->fMeta.fAudioFilename);
      if (!bytes.empty()) {
        std::error_code ec;
        const auto audioPath = std::filesystem::temp_directory_path(ec) /
                               fMap->fMeta.fAudioFilename;
        std::ofstream out(audioPath, std::ios::binary);
        out.write(reinterpret_cast<const char *>(bytes.data()),
                  static_cast<std::streamsize>(bytes.size()));
        out.close();
        job->fOpts.fAudio = audioPath;
      }
    }
    std::println(std::cerr, "[export] writing {}",
                 job->fOpts.fOutput.string());

    if (!job->fExporter->begin(job->fOpts)) {
      this->exportFailed(job->fExporter->error());
      return;
    }

    // Raster, because the thread that will draw into it has no GL context and
    // is not getting one: a second context does not share Skia's resources,
    // so it would mean a second copy of the skin and the slider bodies.
    job->fSurface = skia::Raster(skia::SkImageInfo::Make(
        job->fOpts.fWidth, job->fOpts.fHeight, skia::kRGBA_8888_SkColorType,
        skia::kPremul_SkAlphaType));
    if (!job->fSurface) {
      this->exportFailed("cannot create the offscreen surface");
      return;
    }

    // The slider bodies are built on the GPU, at one scale, and live there.
    // They were built for the window, so a 4K export drew them soft; they are
    // rebuilt for the size being rendered and then moved into memory, since a
    // thread without a context cannot read them off the GPU. The next play
    // precomputes them for the window again.
    const float exportScale =
        0.8f * std::min(static_cast<float>(job->fOpts.fWidth) /
                            static_cast<float>(osu::kPlayfieldWidth),
                        static_cast<float>(job->fOpts.fHeight) /
                            static_cast<float>(osu::kPlayfieldHeight));
    fSkin.precomputeSliderBodies(*fMap, fComboInfo, exportScale,
                                 fContext.get());
    fSkin.flattenBodiesToRaster(fContext.get());

    job->fMap = *fMap;
    job->fCombo = fComboInfo;
    job->fEvents = fRecordedEvents;
    job->fMods = fMods;
    job->fSkin = &fSkin;
    job->fFont = fFont;
    job->fDisplayFont = fDisplayFont;
    // The cursor is drawn at a size in screen pixels, which is right for a
    // window and wrong for a render: at 4K it came out a quarter of the size
    // it has on screen. Scaled by how much bigger the playfield is, it keeps
    // the size it has relative to the play.
    job->fCursorSize = fSettings.value("cursorsize") *
                       (fScale > 0.0f ? exportScale / fScale : 1.0f);
    job->fDim = fSettings.value("dim");
    job->fNoGlow = fNoGlow;
    for (const auto &info : fSet.fBeatmaps) {
      if (info.fMeta.fBackground.empty()) {
        continue;
      }
      const auto bytes = fSet.findFile(info.fMeta.fBackground);
      if (!bytes.empty()) {
        job->fView.setBackground(loadImage(bytes));
        break;
      }
    }

    fExportDialog.setStatus("rendering 0%");
    ExportJob *raw = job.get();
    job->fThread = std::thread([raw] { runExport(*raw); });
    fExportJob = std::move(job);
  }

  // The whole render, on a thread of its own. Nothing here touches the client:
  // every input is the job's own copy, the surface is raster, and the only way
  // back is a per cent and a verdict.
  static void runExport(ExportJob &job) {
    const int width = job.fOpts.fWidth;
    const int height = job.fOpts.fHeight;
    // The same layout the client uses: the playfield takes 80% of the
    // limiting dimension, worked out for the size being rendered.
    const float scale =
        0.8f * std::min(static_cast<float>(width) /
                            static_cast<float>(osu::kPlayfieldWidth),
                        static_cast<float>(height) /
                            static_cast<float>(osu::kPlayfieldHeight));
    const float offsetX =
        (static_cast<float>(width) -
         static_cast<float>(osu::kPlayfieldWidth) * scale) *
        0.5f;
    const float offsetY =
        (static_cast<float>(height) -
         static_cast<float>(osu::kPlayfieldHeight) * scale) *
        0.5f;

    client::GameplayView::Ctx ctx;
    ctx.fMap = &job.fMap;
    ctx.fSkin = job.fSkin;
    ctx.fCombo = &job.fCombo;
    ctx.fFont = &job.fFont;
    ctx.fDisplayFont = &job.fDisplayFont;
    ctx.fScale = scale;
    ctx.fOffsetX = offsetX;
    ctx.fOffsetY = offsetY;
    ctx.fScreenW = width;
    ctx.fScreenH = height;
    ctx.fCursorSize = job.fCursorSize;
    ctx.fUiScale = std::clamp(static_cast<float>(height) / 1080.0f, 0.7f, 3.0f);
    ctx.fDim = job.fDim;
    ctx.fNoGlow = job.fNoGlow;
    job.fView.preScaleBackground(ctx);

    osu::Engine engine(job.fMap, job.fMods);
    const double end = job.fMap.lastObjectEndTime() + 1500.0;
    const double step = 1000.0 / static_cast<double>(job.fOpts.fFps);
    const skia::SkImageInfo info =
        skia::SkImageInfo::Make(width, height, skia::kRGBA_8888_SkColorType,
                                skia::kPremul_SkAlphaType);
    const std::size_t rowBytes = static_cast<std::size_t>(width) * 4u;
    std::vector<std::uint8_t> pixels(rowBytes *
                                     static_cast<std::size_t>(height));

    std::size_t event = 0;
    std::size_t judged = 0;
    int combo = 0;
    osu::Vec2 cursor = osu::kPlayfieldCenter;

    for (double now = 0.0; now <= end; now += step) {
      while (event < job.fEvents.size() && job.fEvents[event].fTime <= now) {
        engine.submit(job.fEvents[event]);
        if (job.fEvents[event].fAction == osu::InputAction::kMove) {
          cursor = job.fEvents[event].fPos;
          job.fView.addTrailPoint(cursor, job.fEvents[event].fTime);
        }
        ++event;
      }
      engine.advance(now);

      // The popups and the combo are handed to the view by whoever is
      // playing; nobody is, here, so the export does it out of the same
      // events the client would have used.
      const auto &events = engine.events();
      while (judged < events.size()) {
        const auto &result = events[judged++];
        const osu::Vec2 pos =
            result.fIndex < job.fMap.fObjects.size()
                ? osu::objectPosition(job.fMap.fObjects[result.fIndex])
                : osu::kPlayfieldCenter;
        const bool counts =
            !std::holds_alternative<osu::judgement::Miss>(result.fResult) &&
            result.fIndex < job.fCombo.fIndices.size();
        job.fView.addJudgement(result.fResult, result.fIndex, pos, now,
                               counts ? job.fCombo.fIndices[result.fIndex] : 0,
                               counts);
        combo = std::holds_alternative<osu::judgement::Miss>(result.fResult)
                    ? 0
                    : combo + 1;
        job.fView.setCombo(combo);
      }

      ctx.fCanvas = job.fSurface->getCanvas();
      ctx.fEngine = &engine;
      ctx.fCursor = cursor;
      job.fView.render(ctx, now);
      if (job.fSurface->readPixels(info, pixels.data(), rowBytes, 0, 0)) {
        job.fExporter->addFrame(pixels);
      }
      // Whole per cent. The dialog repaints when this changes, so at 4K that
      // is a frame every few seconds -- which is what a client that draws
      // only what changed looks like, and is meant to.
      job.fPercent.store(
          static_cast<int>(std::clamp(now / std::max(1.0, end), 0.0, 1.0) *
                           100.0),
          std::memory_order_relaxed);
    }

    job.fOk = job.fExporter->finish();
    job.fMessage = job.fOk ? job.fOpts.fOutput.string()
                           : job.fExporter->error();
    job.fFinished.store(true, std::memory_order_release);
  }

  // Nothing to step any more: the render is on its own thread. This is the
  // client noticing how far it has got and what it had to say when it stopped.
  void pollExportVideo() {
    if (!fExportJob) {
      return;
    }
    ExportJob &job = *fExportJob;
    if (!job.fFinished.load(std::memory_order_acquire)) {
      fExportDialog.setStatus(
          std::format("rendering {}%   {}x{}",
                      job.fPercent.load(std::memory_order_relaxed),
                      job.fOpts.fWidth, job.fOpts.fHeight));
      return;
    }
    if (job.fThread.joinable()) {
      job.fThread.join();
    }
    if (job.fOk) {
      fExportDialog.setStatus(std::format(
          "saved {}", std::filesystem::path(job.fMessage).filename().string()));
      std::println(std::cerr, "[export] saved {}", job.fMessage);
    } else {
      this->exportFailed(job.fMessage);
    }
    fExportJob.reset();
  }

  // ---- Replay browser ------------------------------------------------------
  //
  // lazer surfaces past plays through the leaderboard beside song select and
  // replays them with the standard playback path. Here the saved .osr files
  // are listed in a panel; picking one starts the map with that replay.

  void toggleReplayList() {
    fReplayListOpen = !fReplayListOpen;
    if (fReplayListOpen) {
      // Catch replays dropped in from outside; unchanged files are only
      // stat'ed, so this is a directory listing and nothing more.
      fReplayIndex.refresh(fReplayDir);
      this->scanReplays();
    }
  }

  // The browser lists the selected difficulty's replays, so a selection made
  // while it is open has to rebuild the list.
  void refreshReplayFilter() {
    if (!fReplayListOpen) {
      return;
    }
    if (this->difficultyMd5(fSelSet, fSelDiff) != fReplayFilter) {
      this->scanReplays();
    }
  }

  // md5 of a difficulty in the library, which is what an .osr records. It is
  // computed when the archive is parsed and kept in the metadata cache, so
  // this costs nothing and never has to open the archive.
  [[nodiscard]] std::string difficultyMd5(int setIdx, int diffIdx) const {
    const auto &infos = this->infosFor(setIdx);
    if (diffIdx < 0 || diffIdx >= static_cast<int>(infos.size())) {
      return {};
    }
    return infos[static_cast<std::size_t>(diffIdx)].fMd5;
  }

  // The replays of the difficulty in question, as a leaderboard shows. The
  // index answers this without touching the disk.
  void scanReplays() {
    const std::string wanted =
        fState == State::kResults || fState == State::kPlaying
            ? this->beatmapMd5()
            : this->difficultyMd5(fSelSet, fSelDiff);
    fReplayFilter = wanted;
    fReplays.clear();
    for (const auto *e : fReplayIndex.forBeatmap(wanted)) {
      fReplays.push_back({e->fPath, e->fLabel, e->fScore, e->fGrade,
                          e->fHasScore});
    }
    // The score in hand starts expanded and centred; after watching a replay
    // it is that replay's own panel, which is already in the list.
    fSelectedPanel = 0;
    for (std::size_t i = 0; i < fReplays.size(); ++i) {
      if (!fReplayPath.empty() && fReplays[i].fPath == fReplayPath) {
        fSelectedPanel = static_cast<int>(i);
        break;
      }
    }
    fPanelFreeScroll = false;
    fPanelDragging = false;
    fPanelEntries.clear();
  }

  void drawReplayList(skia::SkCanvas *canvas) {
    if (!fReplayListOpen) {
      fPanelHits.clear();
      fPanelBand = skia::SkRect::MakeEmpty();
      return;
    }
    const client::ui::Painter p(canvas, fFont);
    const float sw = static_cast<float>(fScreenW);
    const float sh = static_cast<float>(fScreenH);
    p.fillRect(skia::SkRect::MakeXYWH(0, 0, sw, sh),
               skia::colorSetARGB(220, 8, 6, 12));
    p.textCentered("replays", sw * 0.5f, 62.0f, 26.0f, skia::kWhite);
    this->drawScorePanelList(canvas, p, sw, sh, /*ownScore=*/false);

    // The same action the results screen offers, for a replay off the disk.
    fMenuButtons.clear();
    if (this->selectedReplay() != nullptr) {
      const float bw = std::min(260.0f, sw * 0.22f);
      fMenuButtons.push_back({skia::SkRect::MakeXYWH((sw - bw) * 0.5f,
                                                     sh - 92.0f, bw, 46.0f),
                              "export video",
                              skia::colorSetARGB(255, 170, 102, 255)});
      this->drawMenuButton(canvas, fMenuButtons.back());
    }
  }

  // Renders a saved replay to video: the exporter draws whatever gameplay
  // state is loaded, so the map and the replay's events are brought in
  // exactly as starting a playback would, without entering gameplay.
  void exportSelectedReplay() {
    const auto *replay = this->selectedReplay();
    if (replay == nullptr) {
      return;
    }
    auto set = this->setForBlocking(fSelSet);
    if (!set || fSelDiff < 0 ||
        fSelDiff >= static_cast<int>(set->fBeatmaps.size())) {
      return;
    }
    fSet = *set;
    fPlayingSet = fSelSet;
    fPlayingDiff = fSelDiff;
    fReplayPath = replay->fPath;
    fAutoplay = true;
    this->resetGameplayState();
    this->startGameplay(fSet.fBeatmaps[static_cast<std::size_t>(fSelDiff)]);
    fAudio.stop();
    fMenuMusicForSet = -1; // the menu loop restarts once the export is done
    fReplayPath.clear();
    fAutoplay = fCliAutoplay;
    fReplayListOpen = false;
    fExportDialog.show();
  }

  // The strip is live on the results screen and in the browser overlay.
  [[nodiscard]] bool panelListActive() const {
    return fReplayListOpen || fState == State::kResults;
  }


  void watchReplay(const std::filesystem::path &path) {
    // On the results screen the strip belongs to the map just played, which is
    // not necessarily the one selected in the carousel.
    const bool results = fState == State::kResults;
    const int setIdx = results ? fPlayingSet : fSelSet;
    const int diffIdx = results ? fPlayingDiff : fSelDiff;
    if (setIdx < 0) {
      return;
    }
    fReplayListOpen = false;
    fPendingReplay = path; // startPlay picks it up and drives the engine
    this->startPlay(setIdx, diffIdx);
  }

  // ---- Song select ------------------------------------------------------
  //
  // The carousel is a scene tree in client.carousel: it decides where panels
  // are, which of them exist, and what has to be repainted. The wedge, the
  // filter control and the footer are still drawn immediately, and each says
  // which strip of the screen it covers and when what it shows changed.

  void updateSongSelect() {
#ifdef __EMSCRIPTEN__
    if (!fLibraryLoaded) {
      if (detail::gMapsSynced.load(std::memory_order_acquire)) {
        this->initLibrary();
      } else {
        this->damageAll("waiting on local storage");
        return;
      }
    }
#endif
    this->refreshReplayFilter();
    this->rebuildVisible();

    if (!fLibrary.empty() && fBackgroundForSet != fSelSet) {
      // setFor() is asynchronous: only mark the background as up to date once
      // the set has actually arrived, otherwise the first frame consumes the
      // request and the artwork never appears.
      if (auto set = this->setFor(fSelSet)) {
        fBackgroundForSet = fSelSet;
        this->requestBackground(fSelSet, set);
      }
    }

    const float sw = static_cast<float>(fScreenW);
    const float sh = static_cast<float>(fScreenH);

    if (fVisible.empty() != fDrawnEmpty) {
      fDrawnEmpty = fVisible.empty();
      this->damageAll("song select has nothing to list");
    }

    // The rows of the list: every set, and the difficulties of the one that
    // is open. Data, not drawables -- the carousel makes a panel only for
    // what is actually within the viewport.
    fRows.clear();
    for (const int si : fVisible) {
      fRows.push_back({si, -1});
      if (si != fSelSet) {
        continue;
      }
      const auto &infos = this->infosFor(si);
      fSelDiff = std::clamp(fSelDiff, 0,
                            std::max(0, static_cast<int>(infos.size()) - 1));
      for (int di = 0; di < static_cast<int>(infos.size()); ++di) {
        fRows.push_back({si, di});
      }
    }

    client::carousel::Carousel::Ctx ctx;
    ctx.fWidth = sw;
    ctx.fHeight = sh;
    ctx.fTop = client::FilterControl::kHeight + 8.0f;
    ctx.fBottom = sh - 62.0f;
    ctx.fMouseX = fMouseX;
    ctx.fMouseY = fMouseY;
    ctx.fNowMs = wallMs();
    ctx.fDtMs = fUiDt;
    ctx.fRows = fRows;
    ctx.fSelectedSet = fSelSet;
    ctx.fSelectedDiff = fSelDiff;
    fCarousel.update(ctx);
    this->damage(fCarousel.takeDamage());

    // Two things in the filter respond to the pointer -- the dropdowns and the
    // rows of an open list -- so it is repainted when which of them is under
    // the pointer changes, rather than for as long as the pointer is inside
    // it. The handles of the difficulty slider move while dragged.
    const int hotFilter = fFilter.hotElement(fMouseX, fMouseY);
    const std::int64_t filterState = fFilter.stateKey();
    if (hotFilter != fHotFilter || filterState != fFilterState ||
        fFilter.dragging()) {
      fHotFilter = hotFilter;
      fFilterState = filterState;
      this->damage(client::FilterControl::bounds(fScreenW));
    }
    // The caret and the set count are inside the search box, which is what
    // gets repainted for them -- and the caret only exists while there is
    // text to put it after, so an empty filter asks for nothing at all.
    const bool caret = fFilter.caretShown(wallMs());
    if (caret != fFilterCaret || fVisible.size() != fDrawnVisibleCount ||
        fFilter.text() != fDrawnFilterText) {
      fFilterCaret = caret;
      fDrawnVisibleCount = fVisible.size();
      fDrawnFilterText = fFilter.text();
      this->damage(fFilter.searchBox());
    }
    if (!fFilter.text().empty()) {
      this->wakeAt(nextCaretFlip(wallMs()));
    }

    // The footer lights the chip under the pointer and nothing else, so
    // crossing it is worth the two chips involved rather than the strip. The
    // popover grows out of it, and appearing or going away is worth the lot.
    const bool optionsChanged = fOptionsOpen != fDrawnOptionsOpen;
    fDrawnOptionsOpen = fOptionsOpen;
    if (optionsChanged) {
      this->damage(skia::SkRect::MakeLTRB(0.0f, sh - 320.0f, sw, sh));
    }
    const auto [hotFooter, hotFooterRect] = this->footerHot();
    if (hotFooter != fHotFooter) {
      fHotFooter = hotFooter;
      this->damage(fHotFooterRect); // where the highlight was
      this->damage(hotFooterRect);  // and where it is now
      fHotFooterRect = hotFooterRect;
    }

    // The wedge is the selection written out: it changes when the selection
    // does, or when a mod changes what the numbers on it say.
    const std::int64_t wedgeKey =
        (static_cast<std::int64_t>(fSelSet) << 24) ^
        (static_cast<std::int64_t>(fSelDiff) << 8) ^
        static_cast<std::int64_t>(static_cast<std::uint32_t>(fMods));
    if (wedgeKey != fWedgeKey) {
      fWedgeKey = wedgeKey;
      this->damage(skia::SkRect::MakeXYWH(
          0.0f, 32.0f, std::min(560.0f, sw * 0.44f) + 22.0f, 168.0f));
    }
  }

  void frameSongSelect() {
    auto *canvas = fSurface->getCanvas();
#ifdef __EMSCRIPTEN__
    if (!fLibraryLoaded) {
      canvas->clear(skia::colorSetARGB(255, 18, 14, 24));
      this->drawTextCentered(canvas, "Syncing local storage...",
                             static_cast<float>(fScreenW) * 0.5f,
                             static_cast<float>(fScreenH) * 0.5f, 24.0f,
                             skia::kWhite, 0.8f);
      this->present();
      return;
    }
#endif
    this->drawScreenBackground(canvas);

    const float sw = static_cast<float>(fScreenW);
    const float sh = static_cast<float>(fScreenH);

    if (fVisible.empty()) {
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
      this->drawSelectFooter(canvas);
      this->drawScreenFadeIn(canvas);
      this->present();
      return;
    }

    // ---- Left: the info wedge (lazer's BeatmapTitleWedge area).
    const auto &selInfos = this->infosFor(fSelSet);
    if (!selInfos.empty()) {
      this->drawInfoWedge(canvas, selInfos,
                          selInfos[static_cast<std::size_t>(std::clamp(
                              fSelDiff, 0,
                              static_cast<int>(selInfos.size()) - 1))]);
    }

    // ---- Right: the carousel, which masks itself to its own viewport.
    fCarousel.render(canvas);

    this->drawFilterControl(canvas);
    this->drawSelectFooter(canvas);
    this->drawScreenFadeIn(canvas);
    this->present();
  }

  // ---- FilterControl ------------------------------------------------------
  //
  // The widget lives in client.filtercontrol; the app supplies the library
  // view it filters and reacts when the criteria change.

  void drawFilterControl(skia::SkCanvas *canvas) {
    fFilter.draw(canvas, fFont, fScreenW, fMouseX, fMouseY, fVisible.size(),
                 wallMs());
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
      fFilterDirty = true;
    }
  }

  void cycleSortMode() {
    fFilter.cycleSort();
    this->onFilterChanged();
  }

  // Sorting and the visible set both depend on the criteria.
  void onFilterChanged() {
    this->sortLibrary();
    fFilterDirty = true;
    this->rebuildVisible();
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

    if (auto art = this->panelArt(setIndex)) {
      canvas->save();
      canvas->clipRRect(skia::SkRRect::MakeRectXY(rect, corner, corner), true);
      // Cover the panel preserving aspect (FillMode.Fill), cropping overflow.
      const float iw = static_cast<float>(art->width());
      const float ih = static_cast<float>(art->height());
      if (iw > 0.0f && ih > 0.0f) {
        const float scale =
            std::max(rect.width() / iw, rect.height() / ih);
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
    const std::string title =
        infos.empty() ? "(empty)"
        : infos.front().fMeta.fTitleUnicode.empty()
            ? infos.front().fMeta.fTitle
            : infos.front().fMeta.fTitleUnicode;
    const std::string artist =
        infos.empty() ? std::string{}
        : infos.front().fMeta.fArtistUnicode.empty()
            ? infos.front().fMeta.fArtist
            : infos.front().fMeta.fArtistUnicode;
    this->drawTextClipped(canvas, title, rect.fLeft + pad,
                          rect.fTop + rect.height() * 0.44f,
                          rect.width() - pad * 2 - 90.0f, 19.0f, skia::kWhite);
    this->drawTextClipped(canvas, artist, rect.fLeft + pad,
                          rect.fTop + rect.height() * 0.72f,
                          rect.width() - pad * 2 - 90.0f, 14.0f, skia::kWhite,
                          0.75f);

    // Difficulty spread dots (PanelBeatmapSet.SpreadDisplay).
    float dotX = rect.fRight - pad;
    for (auto it = infos.rbegin(); it != infos.rend(); ++it) {
      skia::SkPaint dot;
      dot.setAntiAlias(true);
      dot.setColor(starColor(it->fStars));
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
      float fFrom, fTo;    // fractions of width
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
      p.setColor(skia::colorSetARGB(
          static_cast<std::uint8_t>(alpha * 255.0f), 0, 0, 0));
      canvas->drawPath(quad.detach(), p);
    }
  }

  // Cover art for a set panel: decoded on the loader thread and cached with
  // the entry, so every visible panel gets one without stalling a frame.
  [[nodiscard]] skia::Sp<skia::SkImage> panelArt(int setIndex) {
    if (setIndex < 0 || setIndex >= static_cast<int>(fLibrary.size())) {
      return nullptr;
    }
    auto &entry = fLibrary[static_cast<std::size_t>(setIndex)];
    if (entry.fPanelArt || entry.fPanelArtTried) {
      return entry.fPanelArt;
    }
    const auto path = entry.fPath;
    if (path.empty()) {
      // The command-line set is already resident; decode it directly.
      entry.fPanelArtTried = true;
      if (auto set = entry.fLoaded) {
        entry.fPanelArt = this->decodeSetArt(*set);
      }
      return entry.fPanelArt;
    }

    // Thumbnails live on disk next to the metadata cache, so the archive is
    // only opened the first time a cover is needed. Everything after that is
    // a small PNG read.
    auto image = std::make_shared<skia::Sp<skia::SkImage>>();
    const auto thumb = this->thumbPathFor(path);
    fLoader.submit(
        static_cast<std::uint64_t>(setIndex) | (2ull << 32),
        [path, thumb, image, this] {
          if (std::filesystem::exists(thumb)) {
            *image = loadImage(thumb);
            if (*image) {
              return;
            }
          }
          const auto set = loadBeatmapSet(path, false); // artwork only
          auto full = this->decodeSetArt(set);
          if (!full) {
            return;
          }
          // Downscale once and keep the small copy; panels are ~680px wide.
          *image = this->makeThumbnail(full);
          if (*image) {
            // Raster image, so no GPU context is needed for the encode.
            auto png = skia::png::Encode(nullptr, (*image).get(),
                                         skia::png::Options{});
            if (png && !png->isEmpty()) {
              std::ofstream out(thumb, std::ios::binary);
              out.write(static_cast<const char *>(png->data()),
                        static_cast<std::streamsize>(png->size()));
            }
          }
        },
        [this, setIndex, path, image] {
          if (setIndex >= static_cast<int>(fLibrary.size()) ||
              fLibrary[static_cast<std::size_t>(setIndex)].fPath != path) {
            return;
          }
          auto &e = fLibrary[static_cast<std::size_t>(setIndex)];
          e.fPanelArt = *image;
          e.fPanelArtTried = true;
        });
    return nullptr;
  }

  [[nodiscard]] std::filesystem::path
  thumbPathFor(const std::filesystem::path &archive) const {
    return fThumbDir / (archive.stem().string() + ".png");
  }

  // 512px-wide raster copy: enough for a panel, a fraction of the memory and
  // disk of the original background.
  [[nodiscard]] static skia::Sp<skia::SkImage>
  makeThumbnail(const skia::Sp<skia::SkImage> &src) {
    if (!src) {
      return nullptr;
    }
    constexpr int kWidth = 512;
    const float scale =
        static_cast<float>(kWidth) / static_cast<float>(src->width());
    if (scale >= 1.0f) {
      return src;
    }
    const int h = std::max(1, static_cast<int>(
                                  static_cast<float>(src->height()) * scale));
    skia::SkBitmap bmp;
    if (!bmp.tryAllocPixels(skia::SkImageInfo::Make(
            kWidth, h, skia::kRGBA_8888_SkColorType,
            skia::kPremul_SkAlphaType))) {
      return src;
    }
    skia::SkCanvas canvas(bmp);
    skia::SkPaint paint;
    paint.setAntiAlias(true);
    canvas.drawImageRect(
        src.get(),
        skia::SkRect::MakeXYWH(0.0f, 0.0f, static_cast<float>(kWidth),
                               static_cast<float>(h)),
        skia::SkSamplingOptions(skia::SkFilterMode::kLinear), &paint);
    return skia::RasterFromBitmap(bmp);
  }

  [[nodiscard]] static skia::Sp<skia::SkImage>
  decodeSetArt(const osu::BeatmapSet &set) {
    for (const auto &info : set.fBeatmaps) {
      if (info.fMeta.fBackground.empty()) {
        continue;
      }
      const auto bytes = set.findFile(info.fMeta.fBackground);
      if (bytes.empty()) {
        continue;
      }
      if (auto img = loadImage(bytes)) {
        return img;
      }
    }
    return nullptr;
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
    this->fillRounded(canvas, badge, 11.0f, starColor(info.fStars));
    this->drawTextCentered(canvas, std::format("{:.2f}", info.fStars),
                           badge.centerX(), badge.centerY() + 5.0f, 13.0f,
                           skia::colorSetARGB(255, 20, 16, 26));
    this->drawTextClipped(canvas, info.fMeta.fVersion, badge.fRight + 14.0f,
                          rect.centerY() + 5.0f,
                          rect.width() - badge.width() - pad * 3, 15.0f,
                          skia::kWhite, 0.95f);
  }

  // The left-hand wedge showing the selected beatmap, à la BeatmapTitleWedge.
  void drawInfoWedge(skia::SkCanvas *canvas,
                     const std::vector<osu::BeatmapInfo> &infos,
                     const osu::BeatmapInfo &info) {
    const float sh = static_cast<float>(fScreenH);
    const float w = std::min(560.0f, static_cast<float>(fScreenW) * 0.44f);
    const float top = 32.0f;
    const float h = 168.0f;
    const float shear = 22.0f; // lazer's wedges are sheared parallelograms

    skia::SkPathBuilder wedge;
    wedge.moveTo(0.0f, top);
    wedge.lineTo(w + shear, top);
    wedge.lineTo(w, top + h);
    wedge.lineTo(0.0f, top + h);
    wedge.close();
    skia::SkPaint bg;
    bg.setAntiAlias(true);
    bg.setColor(kPanelBg);
    canvas->drawPath(wedge.detach(), bg);

    const float pad = 28.0f;
    const auto &meta = infos.empty() ? info.fMeta : infos.front().fMeta;
    this->drawTextClipped(
        canvas, meta.fTitleUnicode.empty() ? meta.fTitle : meta.fTitleUnicode,
        pad, top + 46.0f, w - pad * 2, 32.0f, skia::kWhite);
    this->drawTextClipped(
        canvas,
        meta.fArtistUnicode.empty() ? meta.fArtist : meta.fArtistUnicode, pad,
        top + 76.0f, w - pad * 2, 18.0f, skia::kWhite, 0.8f);
    this->drawTextClipped(canvas,
                          std::format("mapped by {}", info.fMeta.fCreator),
                          pad, top + 100.0f, w - pad * 2, 15.0f, kAccent2, 0.9f);
    this->drawTextClipped(
        canvas,
        std::format("{}   {:.2f}*   CS {:.1f}  AR {:.1f}  OD {:.1f}  HP {:.1f}",
                    info.fMeta.fVersion, info.fStars, info.fDiff.fCs,
                    info.fDiff.fAr, info.fDiff.fOd, info.fDiff.fHp),
        pad, top + 128.0f, w - pad * 2, 15.0f, starColor(info.fStars));
    this->drawTextClipped(
        canvas,
        std::format("{} objects   {:.0f}:{:02.0f}", info.fObjectCount,
                    info.fLengthMs / 60000.0,
                    std::fmod(info.fLengthMs / 1000.0, 60.0)),
        pad, top + 152.0f, w - pad * 2, 14.0f, skia::kWhite, 0.7f);

    // Difficulty spread: one star-coloured circle per difficulty, the
    // selected one ringed (BeatmapTitleWedge's difficulty display).
    float dotX = pad + 6.0f;
    const float dotY = top + h + 22.0f;
    for (std::size_t i = 0; i < infos.size(); ++i) {
      const auto &d = infos[i];
      const bool isSelected = static_cast<int>(i) == fSelDiff;
      skia::SkPaint dot;
      dot.setAntiAlias(true);
      dot.setColor(starColor(d.fStars));
      canvas->drawCircle(dotX, dotY, isSelected ? 9.0f : 6.0f, dot);
      if (isSelected) {
        skia::SkPaint ring;
        ring.setAntiAlias(true);
        ring.setStyle(skia::kStrokeStyle);
        ring.setStrokeWidth(2.0f);
        ring.setColor(skia::kWhite);
        canvas->drawCircle(dotX, dotY, 12.0f, ring);
      }
      dotX += isSelected ? 30.0f : 22.0f;
      if (dotX > w - pad) {
        break;
      }
    }
  }

  // SongSelect.CreateFooterButtons gives exactly three: Mods, Random and
  // Options; Options opens a popover with the per-beatmap actions. The back
  // button sits on the left, as ScreenBackButton does.
  void drawSelectFooter(skia::SkCanvas *canvas) {
    const client::ui::Painter p(canvas, fFont);
    const float sw = static_cast<float>(fScreenW);
    const float sh = static_cast<float>(fScreenH);
    constexpr float kFooterHeight = 60.0f;
    p.fillRect(skia::SkRect::MakeXYWH(0.0f, sh - kFooterHeight, sw,
                                      kFooterHeight),
               client::ui::kBackground5);

    // Back button, bottom-left.
    fBackChip = skia::SkRect::MakeXYWH(24.0f, sh - 46.0f, 100.0f, 34.0f);
    const bool backHover = fBackChip.contains(fMouseX, fMouseY);
    p.fillRounded(fBackChip, 17.0f,
                  backHover ? client::ui::kCardSel : client::ui::kCardBg);
    p.textCentered("back", fBackChip.centerX(), fBackChip.centerY() + 5.0f,
                   14.0f, skia::kWhite, 0.9f);

    struct FooterBtn {
      const char *fLabel;
      skia::SkColor fColor;
      skia::SkRect *fHit;
    };
    const FooterBtn btns[] = {
        {"mods", client::ui::kAccent, &fModsChip},
        {"random", skia::colorSetARGB(255, 102, 204, 255), &fRandomChip},
        {"options", skia::colorSetARGB(255, 170, 102, 255), &fOptionsChip},
    };
    const float bw = 140.0f;
    const float gap = 10.0f;
    float x = (sw - (bw * 3.0f + gap * 2.0f)) * 0.5f;
    for (const auto &b : btns) {
      const skia::SkRect r =
          skia::SkRect::MakeXYWH(x, sh - 48.0f, bw, 36.0f);
      *b.fHit = r;
      const bool hover = r.contains(fMouseX, fMouseY);
      p.fillRounded(r, 18.0f,
                    hover ? client::ui::kCardSel : client::ui::kCardBg);
      p.strokeRounded(r, 18.0f, b.fColor, hover ? 2.0f : 1.0f);
      p.textCentered(b.fLabel, r.centerX(), r.centerY() + 5.0f, 14.0f,
                     hover ? b.fColor : skia::kWhite);
      x += bw + gap;
    }

    this->drawOptionsPopover(p, sw, sh);
  }

  // FooterButtonOptions.Popover: the per-beatmap actions that do not deserve
  // their own footer slot.
  void drawOptionsPopover(const client::ui::Painter &p, float sw, float sh) {
    fOptionHits.clear();
    if (!fOptionsOpen) {
      return;
    }
    static constexpr std::array<const char *, 5> kItems = {
        "import .osz", "browse beatmaps", "replays", "delete beatmap",
        "settings"};
    const float w = 220.0f;
    const float itemH = 38.0f;
    const float h = itemH * static_cast<float>(kItems.size()) + 12.0f;
    const skia::SkRect box = skia::SkRect::MakeXYWH(
        fOptionsChip.centerX() - w * 0.5f, sh - 60.0f - h - 8.0f, w, h);
    p.fillRounded(box, 10.0f, client::ui::kBackground4);
    p.strokeRounded(box, 10.0f, skia::colorSetARGB(255, 170, 102, 255), 2.0f);
    for (std::size_t i = 0; i < kItems.size(); ++i) {
      const skia::SkRect r = skia::SkRect::MakeXYWH(
          box.fLeft + 6.0f, box.fTop + 6.0f + static_cast<float>(i) * itemH,
          w - 12.0f, itemH);
      fOptionHits.push_back(r);
      if (r.contains(fMouseX, fMouseY)) {
        p.fillRounded(r, 8.0f, client::ui::kCardSel);
      }
      p.textClipped(kItems[i], r.fLeft + 14.0f, r.centerY() + 5.0f,
                    r.width() - 28.0f, 14.0f, skia::kWhite, 0.95f);
    }
  }

  bool optionsClick(float x, float y) {
    if (fOptionsOpen) {
      for (std::size_t i = 0; i < fOptionHits.size(); ++i) {
        if (!fOptionHits[i].contains(x, y)) {
          continue;
        }
        fOptionsOpen = false;
        switch (i) {
        case 0: this->importOsz(); break;
        case 1: this->openDownloads(); break;
        case 2: this->toggleReplayList(); break;
        case 3: this->askDeleteBeatmap(); break;
        default: this->toggleSettings(); break;
        }
        return true;
      }
      fOptionsOpen = false;
    }
    if (fOptionsChip.contains(x, y)) {
      fOptionsOpen = !fOptionsOpen;
      return true;
    }
    if (fModsChip.contains(x, y)) {
      this->toggleMods();
      return true;
    }
    if (fRandomChip.contains(x, y)) {
      this->selectRandom();
      return true;
    }
    if (fBackChip.contains(x, y)) {
      this->switchState(State::kMainMenu);
      return true;
    }
    return false;
  }

  // lazer never deletes a beatmap without asking, and neither does this.
  void askDeleteBeatmap() {
    if (fSelSet < 0 || fSelSet >= static_cast<int>(fLibrary.size())) {
      return;
    }
    const auto &infos = this->infosFor(fSelSet);
    if (infos.empty()) {
      return;
    }
    fConfirmDelete = true;
    fConfirmScene = this->buildDeleteDialog(infos);
  }

  // The dialog as a tree: a dimmed backdrop, a panel anchored to the centre,
  // a vertical flow of text, and two buttons in a horizontal one. No
  // coordinates are computed here -- anchors and flows place everything, and
  // the panel fades and rises into place with transforms.
  [[nodiscard]] std::unique_ptr<client::scene::Drawable>
  buildDeleteDialog(const std::vector<osu::BeatmapInfo> &infos) {
    namespace scene = client::scene;
    namespace nodes = client::nodes;

    auto root = std::make_unique<nodes::Box>(skia::colorSetARGB(200, 8, 6, 12));
    root->fRelativeSizeAxes = scene::Axes::kBoth;
    root->fWidth = 1.0f;
    root->fHeight = 1.0f;

    auto panel = std::make_unique<nodes::Box>(client::ui::kBackground5);
    panel->fWidth = 560.0f;
    panel->fHeight = 240.0f;
    panel->fAnchor = scene::Anchor::kCentre;
    panel->fOrigin = scene::Anchor::kCentre;
    panel->fCornerRadius = 12.0f;
    panel->fPadding = scene::Margin::all(20.0f);
    panel->fAlpha = 0.0f;
    panel->fY = 20.0f;
    panel->fadeTo(1.0f, 200.0, scene::Easing::kOutQuint);
    panel->moveToY(0.0f, 400.0, scene::Easing::kOutQuint);

    auto column = std::make_unique<nodes::FillFlow>(
        nodes::FillFlow::Direction::kVertical);
    column->fRelativeSizeAxes = scene::Axes::kX;
    column->fWidth = 1.0f;
    column->fAutoSizeAxes = scene::Axes::kY;
    column->setSpacing(0.0f, 8.0f);

    const auto &meta = infos.front().fMeta;
    const std::string title = std::format(
        "{} - {}",
        meta.fArtistUnicode.empty() ? meta.fArtist : meta.fArtistUnicode,
        meta.fTitleUnicode.empty() ? meta.fTitle : meta.fTitleUnicode);

    const auto line = [&](std::string text, float size, skia::SkColor colour,
                          bool bold, float alpha) {
      auto node = std::make_unique<nodes::Text>(std::move(text), size, colour,
                                                bold);
      node->fAnchor = scene::Anchor::kTopCentre;
      node->fOrigin = scene::Anchor::kTopCentre;
      node->setMaxWidth(520.0f);
      node->fAlpha = alpha;
      return node;
    };
    column->add(line("Confirm deletion of", 16.0f, skia::kWhite, false, 0.75f));
    column->add(line(title, 20.0f, skia::kWhite, true, 1.0f));
    column->add(line(std::format("{} difficulties will be removed from disk",
                                 infos.size()),
                     13.0f, skia::kWhite, false, 0.6f));
    panel->add(std::move(column));

    auto buttons = std::make_unique<nodes::FillFlow>(
        nodes::FillFlow::Direction::kHorizontal);
    buttons->fAutoSizeAxes = scene::Axes::kBoth;
    buttons->fAnchor = scene::Anchor::kBottomCentre;
    buttons->fOrigin = scene::Anchor::kBottomCentre;
    buttons->setSpacing(20.0f, 0.0f);
    buttons->fWrap = false;
    buttons->add(this->dialogButton("Yes. Totally. Delete it.",
                                    skia::colorSetARGB(255, 255, 110, 110),
                                    [this] {
                                      fConfirmDelete = false;
                                      fConfirmScene.reset();
                                      this->deleteSelectedBeatmap();
                                    }));
    buttons->add(this->dialogButton("Cancel", client::ui::kAccent2, [this] {
      fConfirmDelete = false;
      fConfirmScene.reset();
    }));
    panel->add(std::move(buttons));

    root->add(std::move(panel));
    return root;
  }

  [[nodiscard]] std::unique_ptr<client::scene::Drawable>
  dialogButton(std::string label, skia::SkColor accent,
               std::function<void()> action) {
    namespace scene = client::scene;
    namespace nodes = client::nodes;

    auto button = std::make_unique<nodes::Clickable>(std::move(action));
    button->fWidth = 240.0f;
    button->fHeight = 46.0f;

    auto background = std::make_unique<nodes::Box>(client::ui::kCardBg);
    background->fRelativeSizeAxes = scene::Axes::kBoth;
    background->fWidth = 1.0f;
    background->fHeight = 1.0f;
    background->fCornerRadius = 10.0f;
    button->add(std::move(background));

    auto text = std::make_unique<nodes::Text>(std::move(label), 15.0f, accent,
                                              false);
    text->fAnchor = scene::Anchor::kCentre;
    text->fOrigin = scene::Anchor::kCentre;
    button->add(std::move(text));
    return button;
  }

  void drawDeleteConfirmation(skia::SkCanvas *canvas) {
    if (!fConfirmDelete || !fConfirmScene) {
      return;
    }
    const skia::SkRect screen = skia::SkRect::MakeWH(
        static_cast<float>(fScreenW), static_cast<float>(fScreenH));
    fConfirmScene->updateTree(wallMs());
    fConfirmScene->layoutIfNeeded(screen);
    fConfirmScene->setHover(fMouseX, fMouseY);
    fConfirmScene->draw(canvas);
    this->damage(fConfirmScene->takeDamage());
  }

  bool confirmDeleteClick(float x, float y) {
    if (!fConfirmDelete || !fConfirmScene) {
      return false;
    }
    fConfirmScene->click(x, y);
    return true; // the dialog is modal either way
  }

  // Removes the archive and everything the client remembers about it.
  void deleteSelectedBeatmap() {
    if (fSelSet < 0 || fSelSet >= static_cast<int>(fLibrary.size())) {
      return;
    }
    const auto index = static_cast<std::size_t>(fSelSet);
    const auto path = fLibrary[index].fPath;
    const std::string name =
        path.empty() ? std::string{} : path.filename().string();

    // The track playing under the menu belongs to this set; let it go before
    // the file does.
    this->stopMenuMusic();
    fMenuMusicForSet = -1;
    fBackgroundForSet = -1;

    fLibrary.erase(fLibrary.begin() + static_cast<std::ptrdiff_t>(index));
    if (!path.empty()) {
      std::error_code ec;
      std::filesystem::remove(path, ec);
      std::filesystem::remove(this->thumbPathFor(path), ec);
      fMapCache.remove(name);
      fMapCache.save();
    }
    this->sortLibrary();
    this->rebuildVisible();
    fSelSet = std::clamp(fSelSet, 0,
                         std::max(0, static_cast<int>(fLibrary.size()) - 1));
    fSelDiff = 0;
    fPlayingSet = -1;
    fPlayingDiff = -1;
    this->notify(name.empty() ? "beatmap deleted"
                              : std::format("deleted {}", name));
  }

  void drawBottomBar(skia::SkCanvas *canvas, const std::string &hint) {
    const float sw = static_cast<float>(fScreenW);
    const float sh = static_cast<float>(fScreenH);
    this->fillRounded(canvas,
                      skia::SkRect::MakeXYWH(0.0f, sh - 44.0f, sw, 44.0f),
                      0.0f, kPanelBg);
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
    // Progress lives on the transfer handles; the view just reads a float.
    // A card whose number moved says so, which is what keeps a download from
    // being worth the whole screen on every frame of it.
    for (std::size_t i = 0; i < fFound.size(); ++i) {
      auto &e = fFound[i];
      const auto it = fTransfers.find(e.fSetId);
      if (it != fTransfers.end() && it->second) {
        const float progress =
            it->second->fProgress.load(std::memory_order_relaxed);
        if (progress != e.fProgress) {
          e.fProgress = progress;
          fListing.entryChanged(static_cast<int>(i));
        }
      }
    }
    // A card also draws its own state -- idle, fetching, done, failed -- out
    // of the entry, and that is written from a dozen places: a transfer
    // starting, one finishing, an import marking everything already owned.
    // Comparing it here catches all of them, including the ones written after
    // this was, which a call at each site would not.
    fEntryStates.resize(fFound.size(), 0xFF);
    for (std::size_t i = 0; i < fFound.size(); ++i) {
      const auto state = static_cast<std::uint8_t>(fFound[i].fSt);
      if (fEntryStates[i] != state) {
        fEntryStates[i] = state;
        fListing.entryChanged(static_cast<int>(i));
      }
    }
    client::listing::Listing::Ctx ctx;
    ctx.fFont = &fFont;
    ctx.fWidth = static_cast<float>(fScreenW);
    ctx.fHeight = static_cast<float>(fScreenH);
    ctx.fMouseX = fMouseX;
    ctx.fMouseY = fMouseY;
    ctx.fNowMs = wallMs();
    ctx.fDtMs = fUiDt;
    ctx.fEntries = fFound;
    ctx.fLoading = fSearchPending;
    if (fPreviewId >= 0 && !fPreviewPending && !fPreview.playing()) {
      fPreviewId = -1; // the clip ran out; the button goes back to play
      this->restoreMusic();
    }
    fListing.setPreview(fPreviewId, this->previewProgress());
    fListing.update(ctx);
    this->damage(fListing.takeDamage());
    if (fSetPage.open()) {
      const std::size_t idx = this->indexOfSet(fSetPage.setId());
      if (idx >= fFound.size()) {
        fSetPage.close(); // the set fell out of the results
        this->damageAll("beatmap page closed");
      }
      client::setpage::SetPage::Ctx page;
      page.fEntry = idx < fFound.size() ? &fFound[idx] : nullptr;
      page.fFont = &fFont;
      page.fWidth = ctx.fWidth;
      page.fHeight = ctx.fHeight;
      page.fMouseX = fMouseX;
      page.fMouseY = fMouseY;
      page.fNowMs = wallMs();
      page.fPreviewPlaying = fPreviewId == fSetPage.setId();
      page.fPreviewProgress = this->previewProgress();
      fSetPage.update(page);
      this->damage(fSetPage.takeDamage());
    }
    // Covers are only fetched for what is on screen, which the listing knows
    // and the client did not: this used to walk every result that passed the
    // filters, on screen or four hundred cards below it.
    for (const int idx : fListing.onScreen()) {
      this->requestThumb(static_cast<std::size_t>(idx));
    }
    // Scrolling near the end pages the next batch in, as the overlay's
    // scroll container asks for the next cursor.
    if (fListing.wantsMore()) {
      this->fetchPage();
    }
    // The caret blinks on a clock of its own. Rather than keeping frames
    // coming so the moment is not missed, the screen says when the moment is
    // -- and says nothing when there is no such moment: no caret on screen,
    // or the beatmap page covering the listing it belongs to.
    if (!fSetPage.open()) {
      const double wake = fListing.nextChangeWall(wallMs());
      if (wake > 0.0) {
        this->wakeAt(wake);
      }
    }
  }

  void frameDownload() {
    auto *canvas = fSurface->getCanvas();
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
    ctx.fWidth = static_cast<float>(fScreenW);
    ctx.fHeight = static_cast<float>(fScreenH);
    ctx.fMouseX = fMouseX;
    ctx.fMouseY = fMouseY;
    ctx.fNowMs = wallMs();
    ctx.fDtMs = fUiDt;
    ctx.fAnimateTriangles = fSettings.flag("pausetriangles");
    ctx.fRetries = fRetryCount;
    ctx.fProgress = this->playProgress();
    ctx.fAccuracy = fEngine ? static_cast<float>(fEngine->score().accuracy())
                            : 1.0f;
    fPauseMenu.update(ctx);
    this->damage(fPauseMenu.takeDamage());
  }

  // How far into the playable part of the map the pause happened, which is
  // what GameplayMenuOverlay puts under the buttons.
  [[nodiscard]] float playProgress() const {
    if (!fMap || fMap->fObjects.empty()) {
      return 0.0f;
    }
    const double first = osu::startTime(fMap->fObjects.front());
    const double last = osu::startTime(fMap->fObjects.back());
    if (last <= first) {
      return 0.0f;
    }
    return static_cast<float>(
        std::clamp((fPausedNow - first) / (last - first), 0.0, 1.0));
  }

  void framePaused() {
    // The frozen game underneath does not change while it is paused; what
    // moves is the overlay, and the frame is clipped to what the overlay
    // said. The scene is still redrawn, because a clipped repaint has to put
    // back whatever was under the piece being repainted.
    fView.invalidate();
    fView.render(this->gameplayCtx(fSurface->getCanvas()), fPausedNow);
    fPauseMenu.render(fSurface->getCanvas());
    this->present();
  }

  void drawMenuButton(skia::SkCanvas *canvas, const MenuButton &b) {
    const bool hover = b.fRect.contains(fMouseX, fMouseY);
    this->fillRounded(canvas, b.fRect, 12.0f, hover ? kCardSel : kCardBg);
    this->strokeRounded(canvas, b.fRect, 12.0f, b.fAccent, hover ? 3.0f : 2.0f);
    this->drawTextCentered(canvas, b.fLabel,
                           b.fRect.centerX(), b.fRect.centerY() + 7.0f, 20.0f,
                           hover ? b.fAccent : skia::kWhite);
  }

  // ---- Results ----------------------------------------------------------

  // ResultsScreen is a ScorePanelList: the played score expanded in the
  // middle, every other score for the beatmap contracted beside it.
  // ScorePanel gives the sizes -- EXPANDED_WIDTH 360, CONTRACTED_WIDTH 130,
  // CONTRACTED_HEIGHT 385 -- with 5px between panels and 15px extra either
  // side of the expanded one.
  static constexpr float kPanelExpandedW = 360.0f;
  static constexpr float kPanelContractedW = 130.0f;
  static constexpr float kPanelContractedH = 385.0f;
  static constexpr float kPanelSpacing = 5.0f;
  static constexpr float kExpandedSpacing = 15.0f;

  // The actions under the panel strip. Laid out before anything is drawn, so
  // that what the pointer is on -- and therefore what has to be repainted --
  // is known without drawing them first.
  void updateResults() {
    const float sw = static_cast<float>(fScreenW);
    const float sh = static_cast<float>(fScreenH);
    fMenuButtons.clear();
    const float bw = std::min(260.0f, sw * 0.22f);
    const float bh = 46.0f;
    const float gap = 14.0f;
    float bx = (sw - (bw * 3.0f + gap * 2.0f)) * 0.5f;
    const char *labels[] = {"retry", "back to song select", "export video"};
    const skia::SkColor accents[] = {skia::colorSetARGB(255, 255, 204, 102),
                                     client::ui::kAccent2,
                                     skia::colorSetARGB(255, 170, 102, 255)};
    for (int i = 0; i < 3; ++i) {
      fMenuButtons.push_back(
          {skia::SkRect::MakeXYWH(bx, sh - 92.0f, bw, bh), labels[i],
           accents[i]});
      bx += bw + gap;
    }

    // The row lights the button under the pointer and nothing else.
    int hot = -1;
    for (std::size_t i = 0; i < fMenuButtons.size(); ++i) {
      if (fMenuButtons[i].fRect.contains(fMouseX, fMouseY)) {
        hot = static_cast<int>(i);
        break;
      }
    }
    if (hot != fHotResultButton) {
      fHotResultButton = hot;
      this->damage(skia::SkRect::MakeXYWH(0.0f, sh - 100.0f, sw, 100.0f));
    }
    // The strip of panels moves when another score is chosen and while it is
    // dragged; a drag sets the position outright, so the target says nothing
    // about it.
    if (std::abs(fPanelScroll - fPanelScrollTarget) > 0.05f ||
        fPanelScroll != fDrawnPanelScroll) {
      fDrawnPanelScroll = fPanelScroll;
      this->damageAll("results strip moving");
    }
  }

  void frameResults() {
    fView.invalidate();
    auto *canvas = fSurface->getCanvas();
    this->drawScreenBackground(canvas);
    const client::ui::Painter p(canvas, fFont);

    const float sw = static_cast<float>(fScreenW);
    const float sh = static_cast<float>(fScreenH);
    p.fillRect(skia::SkRect::MakeXYWH(0, 0, sw, sh),
               skia::colorSetARGB(160, 10, 8, 14));

    this->drawScorePanelList(canvas, p, sw, sh,
                            /*ownScore=*/fReplayPath.empty());
    for (const auto &b : fMenuButtons) {
      this->drawMenuButton(canvas, b);
    }

    this->drawScreenFadeIn(canvas);
    this->present();
  }

  // ScorePanelList: a horizontal strip of panels with one expanded. The strip
  // scrolls (drag or wheel) and clicking a contracted panel expands it --
  // selecting a score, not launching it. Sizes are ScorePanel's:
  // EXPANDED_WIDTH 360, CONTRACTED_WIDTH 130, CONTRACTED_HEIGHT 385, with 5px
  // between panels and 15px either side of the expanded one.
  //
  // `ownScore` marks a leading entry carrying the score in hand (the run just
  // played); the browser has none and shows each replay's own header.
  void drawScorePanelList(skia::SkCanvas *canvas, const client::ui::Painter &p,
                          float sw, float sh, bool ownScore) {
    fPanelHits.clear();
    fPanelOwnScore = ownScore;

    // Native size unless the window cannot fit it; the panels are meant to be
    // read at a glance, so they scale up on a large window rather than sit
    // tiny in the middle of it.
    const float scale =
        std::clamp(std::min((sh - 250.0f) / (kPanelContractedH + 60.0f),
                            sw / 1280.0f * 1.35f),
                   0.6f, 1.6f);
    const float contractedW = kPanelContractedW * scale;
    const float expandedW = kPanelExpandedW * scale;
    const float panelH = kPanelContractedH * scale;
    const float expandedH = panelH + 70.0f * scale;
    const float spacing = kPanelSpacing * scale;
    const float expandedGap = kExpandedSpacing * scale;
    const float cy = sh * 0.47f;
    fPanelBand = skia::SkRect::MakeLTRB(0.0f, cy - expandedH * 0.5f - 10.0f,
                                        sw, cy + expandedH * 0.5f + 10.0f);

    // Entries: the score in hand first when there is one, then every replay
    // for this difficulty. The run just played is already the expanded panel,
    // so its own file is not listed a second time.
    fPanelEntries.clear();
    if (ownScore) {
      fPanelEntries.push_back(-1);
    }
    for (std::size_t i = 0; i < fReplays.size(); ++i) {
      if (ownScore && !fLastSavedReplay.empty() &&
          fReplays[i].fPath == fLastSavedReplay) {
        continue;
      }
      fPanelEntries.push_back(static_cast<int>(i));
    }
    const int count = static_cast<int>(fPanelEntries.size());
    if (count == 0) {
      fPanelBand = skia::SkRect::MakeEmpty();
      p.textCentered("no replays saved for this difficulty", sw * 0.5f,
                     sh * 0.5f, 18.0f, skia::kWhite, 0.6f);
      return;
    }
    fSelectedPanel = std::clamp(fSelectedPanel, 0, count - 1);

    // Lay the strip out, then scroll so the expanded panel sits centred --
    // unless the user has dragged or wheeled away from it.
    std::vector<float> widths(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
      widths[static_cast<std::size_t>(i)] =
          i == fSelectedPanel ? expandedW : contractedW;
    }
    float centreOffset = 0.0f;
    for (int i = 0; i < fSelectedPanel; ++i) {
      centreOffset += widths[static_cast<std::size_t>(i)] + spacing;
    }
    centreOffset += expandedGap + expandedW * 0.5f;
    if (!fPanelFreeScroll) {
      fPanelScrollTarget = centreOffset - sw * 0.5f;
    }
    fPanelScroll = this->approach(fPanelScroll, fPanelScrollTarget, 70.0f);

    float x = -fPanelScroll;
    for (int i = 0; i < count; ++i) {
      const bool expanded = i == fSelectedPanel;
      if (expanded) {
        x += expandedGap;
      }
      const float w = widths[static_cast<std::size_t>(i)];
      const float h = expanded ? expandedH : panelH;
      const skia::SkRect r = skia::SkRect::MakeXYWH(x, cy - h * 0.5f, w, h);
      const int entry = fPanelEntries[static_cast<std::size_t>(i)];
      const ReplayFile *replay =
          entry < 0 ? nullptr : &fReplays[static_cast<std::size_t>(entry)];
      if (r.fRight > -60.0f && r.fLeft < sw + 60.0f) {
        if (expanded) {
          this->drawExpandedPanel(canvas, p, r, scale, replay);
        } else {
          this->drawContractedPanel(p, r, replay, scale);
        }
      }
      fPanelHits.push_back({r, i});
      x += w + spacing + (expanded ? expandedGap : 0.0f);
    }

    p.textCentered(ownScore
                       ? "click a panel to view it    drag to scroll"
                       : "click to select, click again to watch    Esc closes",
                   sw * 0.5f, sh - 44.0f, 13.0f, skia::kWhite, 0.6f);
  }

  // Press starts a drag anywhere over the strip; release either resolves the
  // drag or, if the pointer barely moved, counts as a click on a panel.
  bool panelListClick(float x, float y, bool pressed) {
    if (pressed) {
      if (!fPanelBand.contains(x, y)) {
        return false;
      }
      fPanelDragging = true;
      fPanelDragged = false;
      fPanelDragOrigin = x;
      fPanelScrollOrigin = fPanelScroll;
      return true;
    }
    if (!fPanelDragging) {
      return false;
    }
    const bool dragged = fPanelDragged;
    fPanelDragging = false;
    fPanelDragged = false;
    if (dragged) {
      return true;
    }
    for (const auto &hit : fPanelHits) {
      if (!hit.fRect.contains(x, y)) {
        continue;
      }
      if (hit.fIndex == fSelectedPanel) {
        // The expanded panel is already selected; activating it plays the
        // replay it stands for. The score in hand has none to play.
        this->watchSelectedReplay();
      } else {
        fSelectedPanel = hit.fIndex;
        fPanelFreeScroll = false; // re-centre on the new selection
      }
      return true;
    }
    return true; // a press that landed on the strip is ours either way
  }

  void panelListDrag(float x) {
    const float delta = fPanelDragOrigin - x;
    if (std::abs(delta) > 4.0f) {
      fPanelDragged = true;
      fPanelFreeScroll = true;
    }
    fPanelScrollTarget = fPanelScrollOrigin + delta;
    fPanelScroll = fPanelScrollTarget;
  }

  // The replay behind the expanded panel, if it is not the score in hand.
  [[nodiscard]] const ReplayFile *selectedReplay() const {
    if (fSelectedPanel < 0 ||
        fSelectedPanel >= static_cast<int>(fPanelEntries.size())) {
      return nullptr;
    }
    const int idx = fPanelEntries[static_cast<std::size_t>(fSelectedPanel)];
    if (idx < 0 || idx >= static_cast<int>(fReplays.size())) {
      return nullptr; // the score in hand, which has no file to play
    }
    return &fReplays[static_cast<std::size_t>(idx)];
  }

  void watchSelectedReplay() {
    if (const auto *replay = this->selectedReplay()) {
      this->watchReplay(replay->fPath);
    }
  }

  // `replay` is null for the score in hand, which is contracted whenever some
  // other panel is expanded.
  void drawContractedPanel(const client::ui::Painter &p, const skia::SkRect &r,
                           const ReplayFile *replayPtr, float scale) {
    const bool hover = r.contains(fMouseX, fMouseY);
    const float h = r.height();
    p.fillRounded(r, 10.0f * scale,
                  hover ? client::ui::kCardSel : client::ui::kBackground4);

    if (replayPtr == nullptr) {
      const auto &sc = fResult.fScore;
      p.textCentered(fResult.fGrade, r.centerX(),
                     r.fTop + h * 0.17f, 46.0f * scale, client::ui::kAccent);
      p.textCentered(std::format("{}", sc.fScore), r.centerX(),
                     r.fTop + h * 0.32f, 19.0f * scale, skia::kWhite);
      p.textCentered(std::format("{:.2f}%", sc.accuracy() * 100.0),
                     r.centerX(), r.fTop + h * 0.40f, 14.0f * scale,
                     client::ui::kAccent2, 0.95f);
      p.textCentered(std::format("{}x", sc.fMaxCombo), r.centerX(),
                     r.fTop + h * 0.47f, 14.0f * scale, skia::kWhite, 0.75f);
      p.textCentered("this play", r.centerX(), r.fBottom - 18.0f * scale,
                     11.0f * scale, client::ui::kAccent2, 0.8f);
      return;
    }
    const ReplayFile &replay = *replayPtr;

    // ContractedPanelMiddleContent: rank, total score, accuracy, combo, then
    // the date the score was set at the bottom.
    if (replay.fHasScore) {
      p.textCentered(replay.fGrade, r.centerX(), r.fTop + h * 0.17f,
                     46.0f * scale, client::ui::kAccent);
      p.textCentered(std::format("{}", replay.fScore.fTotalScore), r.centerX(),
                     r.fTop + h * 0.32f, 19.0f * scale, skia::kWhite);
      p.textCentered(std::format("{:.2f}%", replay.fScore.accuracy() * 100.0),
                     r.centerX(), r.fTop + h * 0.40f, 14.0f * scale,
                     client::ui::kAccent2, 0.95f);
      p.textCentered(std::format("{}x", replay.fScore.fMaxCombo), r.centerX(),
                     r.fTop + h * 0.47f, 14.0f * scale, skia::kWhite, 0.75f);
    } else {
      p.textCentered("replay", r.centerX(), r.fTop + h * 0.20f, 18.0f * scale,
                     client::ui::kAccent2, 0.9f);
      p.textCentered("no score stored", r.centerX(), r.fTop + h * 0.28f,
                     11.0f * scale, skia::kWhite, 0.5f);
    }

    // The stem carries the difficulty and the timestamp it was saved with.
    const auto underscore = replay.fLabel.rfind('_');
    const std::string diff = underscore == std::string::npos
                                 ? replay.fLabel
                                 : replay.fLabel.substr(0, underscore);
    const std::string when = underscore == std::string::npos
                                 ? std::string{}
                                 : replay.fLabel.substr(underscore + 1);
    p.textClipped(diff, r.fLeft + 8.0f * scale, r.fTop + h * 0.66f,
                  r.width() - 16.0f * scale, 13.0f * scale, skia::kWhite,
                  0.95f);
    p.textCentered(when, r.centerX(), r.fBottom - 18.0f * scale,
                   11.0f * scale, skia::kWhite, 0.55f);
  }

  // The expanded panel: the score in hand when `replay` is null, otherwise the
  // score stored in that replay's header.
  void drawExpandedPanel(skia::SkCanvas *canvas, const client::ui::Painter &p,
                         const skia::SkRect &panel, float scale,
                         const ReplayFile *replay) {
    p.fillRounded(panel, 20.0f * scale, client::ui::kBackground5);

    // Values, from whichever score this panel stands for.
    struct Shown {
      std::uint64_t fTotal = 0;
      int f300 = 0, f100 = 0, f50 = 0, fMiss = 0, fCombo = 0;
      double fAccuracy = 1.0;
      bool fDetail = false; // hit error and UR are only kept for this session
    };
    Shown sh;
    if (replay == nullptr) {
      const auto &sc = fResult.fScore;
      sh = {sc.fScore,  sc.fGreat,      sc.fGood, sc.fMeh, sc.fMiss,
            sc.fMaxCombo, sc.accuracy(), true};
    } else if (replay->fHasScore) {
      const auto &sc = replay->fScore;
      sh = {static_cast<std::uint64_t>(sc.fTotalScore),
            sc.f300,
            sc.f100,
            sc.f50,
            sc.fMiss,
            sc.fMaxCombo,
            sc.accuracy(),
            false};
    }

    // The beatmap: the one just played on the results screen, the selected one
    // in the browser. Every replay in the strip belongs to it.
    const bool results = fState == State::kResults;
    const int setIdx = results ? fPlayingSet : fSelSet;
    const int diffIdx = results ? fPlayingDiff : fSelDiff;
    std::string title;
    std::string artist;
    if (results && fMap) {
      const auto &m = fMap->fMeta;
      title = m.fTitleUnicode.empty() ? m.fTitle : m.fTitleUnicode;
      artist = m.fArtistUnicode.empty() ? m.fArtist : m.fArtistUnicode;
    } else if (setIdx >= 0) {
      const auto &infos = this->infosFor(setIdx);
      if (diffIdx >= 0 && diffIdx < static_cast<int>(infos.size())) {
        const auto &m = infos[static_cast<std::size_t>(diffIdx)].fMeta;
        title = m.fTitleUnicode.empty() ? m.fTitle : m.fTitleUnicode;
        artist = m.fArtistUnicode.empty() ? m.fArtist : m.fArtistUnicode;
      }
    }

    float y = panel.fTop + 34.0f * scale;
    p.textCenteredClipped(title, panel.centerX(), y,
                          panel.width() - 32.0f * scale, 20.0f * scale,
                          skia::kWhite);
    y += 24.0f * scale;
    p.textCenteredClipped(artist, panel.centerX(), y,
                          panel.width() - 32.0f * scale, 14.0f * scale,
                          skia::kWhite, 0.8f);
    y += 30.0f * scale;

    const float circleR = 100.0f * scale;
    const float ccy = y + circleR;
    this->drawAccuracyCircle(canvas, panel.centerX(), ccy, circleR,
                             sh.fAccuracy);
    y = ccy + circleR + 24.0f * scale;

    // The score counts up as the panel appears; a replay's stored score is
    // shown outright.
    std::uint64_t shownScore = sh.fTotal;
    if (replay == nullptr) {
      const float countUp = client::ui::outQuint(
          static_cast<float>((wallMs() - fStateEnterWall) / 900.0));
      shownScore =
          static_cast<std::uint64_t>(static_cast<double>(sh.fTotal) * countUp);
    }
    p.textCentered(std::format("{:07}", shownScore), panel.centerX(), y,
                   40.0f * scale, skia::kWhite);
    y += 24.0f * scale;

    if (setIdx >= 0) {
      const auto &infos = this->infosFor(setIdx);
      if (diffIdx >= 0 && diffIdx < static_cast<int>(infos.size())) {
        const auto &info = infos[static_cast<std::size_t>(diffIdx)];
        const skia::SkRect chip = skia::SkRect::MakeXYWH(
            panel.centerX() - 88.0f * scale, y - 11.0f * scale, 60.0f * scale,
            22.0f * scale);
        p.fillRounded(chip, 11.0f * scale, client::ui::starColor(info.fStars));
        p.textCentered(std::format("{:.2f}", info.fStars), chip.centerX(),
                       chip.centerY() + 5.0f * scale, 12.0f * scale,
                       skia::colorSetARGB(255, 20, 16, 26));
        p.textClipped(info.fMeta.fVersion, chip.fRight + 10.0f * scale,
                      y + 5.0f * scale, 150.0f * scale, 13.0f * scale,
                      skia::kWhite, 0.9f);
      }
      y += 30.0f * scale;
    }

    struct Stat {
      const char *fLabel;
      std::string fValue;
      skia::SkColor fColor;
    };
    const Stat top[] = {
        {"300", std::format("{}", sh.f300), client::ui::kGreat},
        {"100", std::format("{}", sh.f100), client::ui::kGood},
        {"50", std::format("{}", sh.f50), client::ui::kMeh},
        {"miss", std::format("{}", sh.fMiss), client::ui::kMiss},
    };
    const float cellW = (panel.width() - 32.0f * scale) / 4.0f;
    float cx = panel.fLeft + 16.0f * scale;
    for (const auto &st : top) {
      p.textCentered(st.fLabel, cx + cellW * 0.5f, y, 11.0f * scale,
                     st.fColor);
      p.textCentered(st.fValue, cx + cellW * 0.5f, y + 20.0f * scale,
                     18.0f * scale, skia::kWhite);
      cx += cellW;
    }
    y += 44.0f * scale;

    std::vector<Stat> bottom{
        {"combo", std::format("{}x", sh.fCombo), skia::kWhite},
        {"accuracy", std::format("{:.2f}%", sh.fAccuracy * 100.0),
         skia::kWhite},
    };
    if (sh.fDetail) {
      bottom.push_back(
          {"hit error", std::format("{:+.1f}ms", fResult.fMean), skia::kWhite});
      bottom.push_back({"UR", std::format("{:.0f}", fResult.fUr),
                        skia::kWhite});
    } else if (replay != nullptr) {
      // A stored replay keeps no hit statistics, only when it was played.
      const auto underscore = replay->fLabel.rfind('_');
      bottom.push_back({"played",
                        underscore == std::string::npos
                            ? std::string{"-"}
                            : replay->fLabel.substr(underscore + 1),
                        skia::kWhite});
    }
    const float bottomW = (panel.width() - 32.0f * scale) /
                          static_cast<float>(bottom.size());
    cx = panel.fLeft + 16.0f * scale;
    for (const auto &st : bottom) {
      p.textCentered(st.fLabel, cx + bottomW * 0.5f, y, 11.0f * scale,
                     skia::kWhite, 0.55f);
      p.textCentered(st.fValue, cx + bottomW * 0.5f, y + 18.0f * scale,
                     15.0f * scale, st.fColor);
      cx += bottomW;
    }

    if (replay != nullptr) {
      p.textCentered("click to watch this replay", panel.centerX(),
                     panel.fBottom - 16.0f * scale, 12.0f * scale,
                     client::ui::kAccent2, 0.85f);
    }
  }

  // AccuracyCircle: a grey backing ring, the graded arcs (D/C/B/A/S/SS at
  // their accuracy cutoffs), the achieved accuracy drawn over them, and the
  // rank letter in the middle. Cutoffs are lazer's standard ones.
  void drawAccuracyCircle(skia::SkCanvas *canvas, float cx, float cy, float r,
                          double accuracy) {
    const client::ui::Painter p(canvas, fFont);
    const float thickness = r * 0.2f; // accuracy_circle_radius
    const skia::SkRect bounds =
        skia::SkRect::MakeXYWH(cx - r, cy - r, r * 2.0f, r * 2.0f);

    skia::SkPaint arc;
    arc.setAntiAlias(true);
    arc.setStyle(skia::kStrokeStyle);
    arc.setStrokeWidth(thickness);
    arc.setStrokeCap(skia::kButtCap);

    // Backing ring, OsuColour.Gray(47).
    arc.setColor(skia::colorSetARGB(255, 47, 47, 47));
    canvas->drawArc(bounds, -90.0f, 360.0f, false, arc);

    // Graded segments: each rank owns the span from its cutoff to the next.
    struct Grade {
      double fFrom, fTo;
      skia::SkColor fColor;
    };
    const Grade grades[] = {
        {0.0, 0.60, skia::colorSetARGB(255, 0xff, 0x54, 0x5a)},   // D
        {0.60, 0.70, skia::colorSetARGB(255, 0xff, 0xa0, 0x55)},  // C
        {0.70, 0.80, skia::colorSetARGB(255, 0xff, 0xdd, 0x55)},  // B
        {0.80, 0.90, skia::colorSetARGB(255, 0x88, 0xdd, 0x20)},  // A
        {0.90, 0.95, skia::colorSetARGB(255, 0x02, 0xb8, 0xd7)},  // S
        {0.95, 1.00, skia::colorSetARGB(255, 0xde, 0x31, 0xae)},  // SS
    };
    skia::SkPaint graded;
    graded.setAntiAlias(true);
    graded.setStyle(skia::kStrokeStyle);
    graded.setStrokeWidth(thickness * 0.45f);
    graded.setStrokeCap(skia::kButtCap);
    const float gradedR = r - thickness * 0.78f;
    const skia::SkRect gradedBounds = skia::SkRect::MakeXYWH(
        cx - gradedR, cy - gradedR, gradedR * 2.0f, gradedR * 2.0f);
    for (const auto &g : grades) {
      graded.setColor(g.fColor);
      const float from = -90.0f + static_cast<float>(g.fFrom) * 360.0f;
      const float sweep = static_cast<float>(g.fTo - g.fFrom) * 360.0f - 1.5f;
      canvas->drawArc(gradedBounds, from, sweep, false, graded);
    }

    // Achieved accuracy, animated in with the panel.
    const float progress = client::ui::outQuint(static_cast<float>(
        (wallMs() - fStateEnterWall) / 1400.0));
    arc.setColor(skia::kWhite);
    canvas->drawArc(bounds, -90.0f,
                    static_cast<float>(accuracy) * 360.0f * progress, false,
                    arc);

    // Rank badge in the middle.
    p.textCentered(fResult.fGrade, cx, cy + r * 0.28f, r * 0.72f,
                   client::ui::kAccent);
    p.textCentered(std::format("{:.2f}%", accuracy * 100.0), cx,
                   cy + r * 0.62f, r * 0.16f, skia::kWhite, 0.85f);
  }

  bool initSkia() {
    auto interface = skia::GrGLMakeNativeInterface();
    if (!interface) {
      return false;
    }
    fContext = skia::MakeGL(std::move(interface));
    client::nodes::CachedContainer::setContext(fContext.get());
    // The carousel owns where a panel is and when it has to be repainted; the
    // client still owns what one looks like, and hands it over here.
    // The menu's pieces: the tree owns where they are and what has to be
    // repainted; what they look like stays here.
    fMenu.setPainters(
        [this](skia::SkCanvas *canvas, const skia::SkRect &, int) {
          this->drawMenuBackground(canvas);
        },
        [this](skia::SkCanvas *canvas, const skia::SkRect &, int index) {
          if (index >= 0 && index < static_cast<int>(fMenuBtns.size())) {
            this->drawMenuWedge(canvas, fMenuBtns[static_cast<std::size_t>(index)],
                                fMenuWedge);
          }
        },
        [this](skia::SkCanvas *canvas, const skia::SkRect &, int) {
          this->drawLogo(canvas, fLogoBase);
        });
    fCarousel.setPainter([this](skia::SkCanvas *canvas,
                                const skia::SkRect &rect,
                                const client::carousel::Row &row, bool selected,
                                bool hovered, float corner) {
      const auto &infos = this->infosFor(row.fSet);
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

  // Judgements are set in a heavy geometric display face. osu! and webosu-2
  // use Venera, which is a commercial typeface, so the client ships
  // Montserrat ExtraBold instead (SIL OFL 1.1, see assets/fonts/OFL.txt).
  // Where a bundled asset can be: beside the install prefix, at the configured
  // data dir, in the source tree, or next to the working directory.
  [[nodiscard]] std::vector<std::filesystem::path>
  assetCandidates(const std::string &name) const {
    std::vector<std::filesystem::path> candidates;
    std::error_code ec;
    const auto exe = std::filesystem::read_symlink("/proc/self/exe", ec);
    if (!ec && !exe.empty()) {
      const auto prefix = exe.parent_path().parent_path(); // .../bin/x -> ...
      candidates.push_back(prefix / "share" / "osu_client" / "fonts" / name);
      candidates.push_back(exe.parent_path() / "fonts" / name);
    }
#ifdef OSU_CLIENT_DATADIR
    candidates.emplace_back(std::filesystem::path(OSU_CLIENT_DATADIR) /
                            "fonts" / name);
#endif
#ifdef OSU_CLIENT_SOURCE_ASSETS
    candidates.emplace_back(std::filesystem::path(OSU_CLIENT_SOURCE_ASSETS) /
                            "fonts" / name);
#endif
    candidates.emplace_back(std::filesystem::path("assets") / "fonts" / name);
    candidates.emplace_back(std::filesystem::path("fonts") / name);
    return candidates;
  }

  [[nodiscard]] skia::Sp<skia::SkTypeface> loadTypeface(const std::string &name) {
    for (const auto &path : this->assetCandidates(name)) {
      std::error_code ec;
      if (!std::filesystem::exists(path, ec)) {
        continue;
      }
      auto data = skia::SkData::MakeFromFileName(path.c_str());
      if (!data || data->isEmpty()) {
        continue;
      }
      std::array<skia::Sp<skia::SkData>, 1> datas{std::move(data)};
      auto mgr = skia::SkFontMgr_New_Custom_Data(datas);
      if (!mgr || mgr->countFamilies() == 0) {
        continue;
      }
      auto face = mgr->matchFamilyStyle(nullptr, skia::SkFontStyle());
      if (!face) {
        face = mgr->createStyleSet(0)->createTypeface(0);
      }
      if (face) {
        skia::SkString family;
        face->getFamilyName(&family);
        std::println(std::cerr, "[ui] font \"{}\" from {}", family.c_str(),
                     path.string());
        return face;
      }
    }
    std::println(std::cerr, "[ui] font missing: {}", name);
    return nullptr;
  }

  // Everything the client draws with ships with it: Latin, Cyrillic and Greek
  // from Inter, Japanese and Korean from Noto Sans, icons from Font Awesome.
  // Any other font dropped into the same directory joins the fallback chain,
  // which is how Chinese or anything else can be added without a rebuild.
  void loadFonts() {
    auto &stack = client::ui::fonts();
    if (std::getenv("OSU_SYSTEM_FONT") != nullptr) {
      // For comparing against what the system provides: when text is what
      // costs, swapping the faces out in one run is worth the branch.
      std::println(std::cerr, "[ui] bundled fonts skipped by request");
      return;
    }
    auto primary = this->loadTypeface("Inter.ttf");
    stack.setPrimary(primary);

    for (const char *name :
         {"NotoSansJP.ttf", "NotoSansKR.ttf", "FontAwesome-Solid.ttf"}) {
      stack.addFallback(this->loadTypeface(name));
    }
    this->loadExtraFonts();

    std::println(std::cerr, "[ui] {} fallback fonts loaded",
                 stack.fallbackCount());
  }

  // Fonts the build does not know about, taken from the same directory.
  void loadExtraFonts() {
    static constexpr std::array<const char *, 4> kKnown = {
        "Inter.ttf", "NotoSansJP.ttf", "NotoSansKR.ttf",
        "FontAwesome-Solid.ttf"};
    for (const auto &dir : this->assetCandidates("")) {
      std::error_code ec;
      if (!std::filesystem::is_directory(dir, ec)) {
        continue;
      }
      for (const auto &entry : std::filesystem::directory_iterator(dir, ec)) {
        const auto ext = entry.path().extension().string();
        if (ext != ".ttf" && ext != ".otf" && ext != ".ttc") {
          continue;
        }
        const auto name = entry.path().filename().string();
        if (std::ranges::find(kKnown, name) != kKnown.end() ||
            name == "Montserrat-ExtraBold.ttf") {
          continue;
        }
        client::ui::fonts().addFallback(this->loadTypeface(name));
      }
      return; // the first directory that exists is the one in use
    }
  }

  [[nodiscard]] skia::SkFont loadDisplayFont(float size) {
    if (auto face = this->loadTypeface("Montserrat-ExtraBold.ttf")) {
      return skia::SkFont(std::move(face), size);
    }
    std::println(std::cerr,
                 "[ui] no display font found; judgements fall back to the UI "
                 "font");
    return this->loadFont(size);
  }

  [[nodiscard]] skia::SkFont loadFont(float size) {
    if (const auto &primary = client::ui::fonts().primary()) {
      return skia::SkFont(primary, size);
    }
    // Only reached if the bundled fonts did not install; better a system face
    // than no text at all.
    for (const char *dir :
         {"/usr/share/fonts/noto", "/usr/share/fonts/ttf-dejavu",
          "/usr/share/fonts/TTF", "/usr/share/fonts"}) {
      if (!std::filesystem::is_directory(dir)) {
        continue;
      }
      auto mgr = skia::SkFontMgr_New_Custom_Directory(dir);
      if (!mgr || mgr->countFamilies() == 0) {
        continue;
      }
      auto face = mgr->matchFamilyStyle("Noto Sans", skia::SkFontStyle());
      if (!face) {
        face = mgr->matchFamilyStyle("DejaVu Sans", skia::SkFontStyle());
      }
      if (!face) {
        face = mgr->createStyleSet(0)->createTypeface(0);
      }
      if (face) {
        std::println(std::cerr, "[ui] falling back to a system font from {}",
                     dir);
        return skia::SkFont(std::move(face), size);
      }
    }
    std::println(std::cerr, "[ui] no font found at all");
    return skia::SkFont();
  }


  // Playfield placement for the current screen size. Split out so the video
  // exporter can re-derive it for its offscreen resolution.
  void layoutForScreen() {
    // Match webosu-2: playfield occupies 80% of the limiting screen dimension.
    constexpr float kPlayfieldSize = 0.8f;
    const float sx =
        static_cast<float>(fScreenW) / static_cast<float>(osu::kPlayfieldWidth);
    const float sy = static_cast<float>(fScreenH) /
                     static_cast<float>(osu::kPlayfieldHeight);
    fScale = kPlayfieldSize * std::min(sx, sy);
    fOffsetX =
        (fScreenW - static_cast<float>(osu::kPlayfieldWidth) * fScale) * 0.5f;
    fOffsetY =
        (fScreenH - static_cast<float>(osu::kPlayfieldHeight) * fScale) * 0.5f;
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
    if (fRasterSurface && fRasterSurface->width() == fScreenW &&
        fRasterSurface->height() == fScreenH) {
      return true;
    }
    // Same colour space as the window, or the pixels get encoded twice on the
    // way over and the whole frame comes out lighter.
    fRasterSurface = skia::Raster(skia::SkImageInfo::Make(
        fScreenW, fScreenH, skia::kRGBA_8888_SkColorType,
        skia::kPremul_SkAlphaType,
        fWindowSurface ? fWindowSurface->imageInfo().refColorSpace()
                       : nullptr));
    return static_cast<bool>(fRasterSurface);
  }

  void resize(int w, int h) {
    // Called on the render thread with framebuffer dimensions delivered by
    // the resize event (or the pre-thread snapshot); querying GLFW here is
    // not allowed off the main thread.
    fScreenW = w;
    fScreenH = h;
    this->layoutForScreen();

    skia::GrGLFramebufferInfo info;
    info.fFBOID = 0;
    info.fFormat = skia::kGlRgba8;
    skia::GrBackendRenderTarget target =
        skia::MakeGL(fScreenW, fScreenH, 0, 0, info);
    fWindowSurface = skia::WrapBackendRenderTarget(
        fContext.get(), target, skia::kBottomLeft_GrSurfaceOrigin,
        skia::kRGBA_8888_SkColorType, nullptr, nullptr);
    fSurface = fWindowSurface;
    // Dropped rather than remade: eight megabytes for a screen nobody may be
    // drawing on that way. The frame that needs it makes it.
    fRasterSurface.reset();
    fBlitHistory.clear();
    this->damageAll("resize");
    fView.invalidate();
    fView.preScaleBackground(this->gameplayCtx(nullptr));
  }

  void toggleFullscreen() {
    if (fWindow == nullptr)
      return;
#ifndef __EMSCRIPTEN__
    fFullscreen = !fFullscreen;
    if (fFullscreen) {
      const auto monitor = glfw::glfwGetPrimaryMonitor();
      const glfw::GLFWvidmode *mode = glfw::glfwGetVideoMode(monitor);
      glfw::glfwSetWindowMonitor(fWindow, monitor, 0, 0, mode->width,
                                 mode->height, mode->refreshRate);
    } else {
      glfw::glfwSetWindowMonitor(fWindow, nullptr, fWindowedX, fWindowedY,
                                 fWindowedW, fWindowedH, 0);
    }
#endif
  }

  void shutdown() {
    fAudio.stop();
    fSurface.reset();
    fContext.reset();
    if (fWindow != nullptr) {
      glfw::glfwDestroyWindow(fWindow);
      fWindow = nullptr;
    }
    glfw::glfwTerminate();
  }

  [[nodiscard]] double nowMs() {
#ifdef __EMSCRIPTEN__
    return glfw::glfwGetTime() * 1000.0 - fStartMs;
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
        fClock.sync(wall, fAudio.positionSec() * 1000.0);
      }
    }
    return fClock.sample(wall);
#endif
  }

  [[nodiscard]] bool shouldStop(double now) const {
    return fEngine->finished() && now > fMap->lastObjectEndTime() + 1000.0;
  }


  void submitAutoplay(double now) {
    if (!fAutoplay) {
      return; // the player is driving
    }
    while (fAutoplayIndex < fAutoplayEvents.size() &&
           fAutoplayEvents[fAutoplayIndex].fTime <= now) {
      const auto &ev = fAutoplayEvents[fAutoplayIndex];
      fEngine->submit(ev);
      if (fReplayPath.empty()) {
        // Generated autoplay is worth recording; a replay being watched is
        // already on disk.
        fRecordedEvents.push_back(ev);
      }
      if (ev.fAction == osu::InputAction::kMove) {
        fCursor = ev.fPos;
        fView.addTrailPoint(fCursor, ev.fTime);
      }
      ++fAutoplayIndex;
    }
  }

  void playHitsounds(double now) {
    const auto &events = fEngine->events();
    while (fPlayedEvents < events.size()) {
      const auto &ev = events[fPlayedEvents++];
      const auto pos = this->objectPosition(ev.fIndex);
      const bool counts =
          !std::holds_alternative<osu::judgement::Miss>(ev.fResult) &&
          ev.fIndex < fComboInfo.fIndices.size();
      fView.addJudgement(ev.fResult, ev.fIndex, pos, now,
                         counts ? fComboInfo.fIndices[ev.fIndex] : 0, counts);
      if (std::holds_alternative<osu::judgement::Miss>(ev.fResult)) {
        if (fCombo > 20) {
          this->playSample("combobreak");
        }
        fCombo = 0;
        fView.setCombo(0);
        continue;
      }
      ++fCombo;
      fView.setCombo(fCombo);
      const double hitTime = ev.fIndex < fMap->fObjects.size()
                                 ? osu::startTime(fMap->fObjects[ev.fIndex])
                                 : now;
      this->playObjectHitsound(hitTime, ev.fIndex);
    }
  }

  [[nodiscard]] static const char *
  sampleSetName(const osu::SampleSet &set) noexcept {
    return std::visit(
        osu::Overloaded{
            [](osu::sampleSet::None) -> const char * { return nullptr; },
            [](osu::sampleSet::Normal) -> const char * { return "normal"; },
            [](osu::sampleSet::Soft) -> const char * { return "soft"; },
            [](osu::sampleSet::Drum) -> const char * { return "drum"; },
        },
        set);
  }

  [[nodiscard]] const char *sampleSetNameOrDefault(const osu::SampleSet &set,
                                                   double time) const {
    if (const char *name = sampleSetName(set))
      return name;
    if (const auto *tp = fMap->activeTiming(time)) {
      if (const char *name = sampleSetName(tp->fSet))
        return name;
    }
    return "normal";
  }

  [[nodiscard]] std::filesystem::path
  findSkinSamplePath(const std::string &name) const {
    for (const std::string_view ext : {".wav", ".ogg"}) {
      const auto skinPath = fSkin.root() / (name + std::string(ext));
      if (std::filesystem::exists(skinPath))
        return skinPath;
    }
    return {};
  }

  void playSample(const std::string &name) {
    if (name.empty())
      return;

    for (const std::string_view ext : {".wav", ".ogg"}) {
      const std::string key = name + std::string(ext);
      const auto bytes = fSet.findFile(key);
      if (!bytes.empty()) {
        auto &player = fSamples[key];
        if (!player.loaded()) {
          player.load(bytes, std::string(ext));
          player.setVolume(this->effectGain());
        }
        player.play();
        return;
      }
    }

    const auto path = this->findSkinSamplePath(name);
    if (path.empty())
      return;
    auto &player = fSamples[path.string()];
    if (!player.loaded()) {
      player.load(path);
      player.setVolume(this->effectGain());
    }
    player.play();
  }

  void playHitSample(double time, osu::HitSound sound,
                     const osu::HitSample &sample) {
    const std::string normalSet =
        this->sampleSetNameOrDefault(sample.fNormalSet, time);
    const std::string additionSet =
        this->sampleSetNameOrDefault(sample.fAdditionSet, time);
    this->playSample(normalSet + "-hitnormal");
    if ((sound & osu::HitSound::kWhistle) != osu::HitSound::kNone)
      this->playSample(additionSet + "-hitwhistle");
    if ((sound & osu::HitSound::kFinish) != osu::HitSound::kNone)
      this->playSample(additionSet + "-hitfinish");
    if ((sound & osu::HitSound::kClap) != osu::HitSound::kNone)
      this->playSample(additionSet + "-hitclap");
  }

  void playObjectHitsound(double time, std::size_t index) {
    if (index >= fMap->fObjects.size())
      return;
    std::visit(osu::Overloaded{
                   [this, time](const osu::Circle &o) {
                     this->playHitSample(time, o.fSound, o.fSample);
                   },
                   [this, time](const osu::Slider &o) {
                     this->playHitSample(time, o.fSound, o.fSample);
                   },
                   [this, time](const osu::Spinner &o) {
                     this->playHitSample(time, o.fSound, o.fSample);
                   },
               },
               fMap->fObjects[index]);
  }

  [[nodiscard]] osu::Vec2 objectPosition(std::size_t index) const {
    if (index >= fMap->fObjects.size()) {
      return osu::kPlayfieldCenter;
    }
    return osu::objectPosition(fMap->fObjects[index]);
  }

  [[nodiscard]] std::pair<osu::Vec2, double>
  objectEnd(std::size_t index) const {
    if (index >= fMap->fObjects.size()) {
      return {osu::kPlayfieldCenter, 0.0};
    }
    return osu::objectEnd(fMap->fObjects[index], *fMap);
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
    fVirtualCursor = {
        std::clamp(fVirtualCursor.fX + delta.fX, 0.0, osu::kPlayfieldWidth),
        std::clamp(fVirtualCursor.fY + delta.fY, 0.0, osu::kPlayfieldHeight)};
    return fVirtualCursor;
  }




  void printResult() {
    std::println("{}", fEngine->score());

    // Timing statistics over actual taps (circles + slider heads). Judgement
    // events are the wrong series for this: sliders/spinners are finalized at
    // their end and carry the object duration as delta, which is why the
    // first version of these stats produced impossible URs.
    double sum = 0.0;
    double sumSq = 0.0;
    std::size_t n = 0;
    for (const double d : fEngine->tapDeltas()) {
      sum += d;
      sumSq += d * d;
      ++n;
    }
    if (n > 0) {
      const double mean = sum / static_cast<double>(n);
      const double variance =
          std::max(0.0, sumSq / static_cast<double>(n) - mean * mean);
      const double ur = 10.0 * std::sqrt(variance);
      std::println("hit error: {:+.1f} ms avg, UR {:.0f}", mean, ur);
    }
    const auto dropped = fInputQueue.dropped();
    if (dropped > 0) {
      std::println("warning: {} input events dropped", dropped);
    }
  }

  [[nodiscard]] std::string beatmapMd5() const {
    for (const auto &info : fSet.fBeatmaps) {
      if (info.fFilename == fBeatmapFilename) {
        return info.fMd5;
      }
    }
    return {};
  }

  void saveReplay() {
    if (fRecordedEvents.empty() || !fMap)
      return;
    if (!fReplayPath.empty()) {
      return; // watching a replay must not write it back out as a new one
    }
    auto now = std::chrono::system_clock::now();
    auto t = std::chrono::system_clock::to_time_t(now);
    std::ostringstream nameStream;
    nameStream << std::put_time(std::localtime(&t), "%Y%m%d_%H%M%S");
    // The .osr header has fields for the score, so it goes in there rather
    // than into a sidecar: the file stays a plain, original-format replay.
    const auto &sc = fEngine->score();
    osu::ReplayScore score;
    score.f300 = static_cast<std::uint16_t>(sc.fGreat);
    score.f100 = static_cast<std::uint16_t>(sc.fGood);
    score.f50 = static_cast<std::uint16_t>(sc.fMeh);
    score.fMiss = static_cast<std::uint16_t>(sc.fMiss);
    score.fTotalScore = static_cast<std::int32_t>(sc.fScore);
    score.fMaxCombo = static_cast<std::uint16_t>(sc.fMaxCombo);
    score.fPerfect = sc.fMiss == 0 && sc.fGood == 0 && sc.fMeh == 0;
    auto replayBytes = osu::encodeReplay(fRecordedEvents, this->beatmapMd5(),
                                         "Player", fMods, score);
    std::error_code ec;
    std::filesystem::create_directories(fReplayDir, ec);
    const std::filesystem::path outPath =
        fReplayDir / (fMap->fMeta.fVersion + "_" + nameStream.str() + ".osr");
    std::ofstream out(outPath, std::ios::binary);
    for (std::uint8_t b : replayBytes)
      out.put(static_cast<char>(b));
    out.close();
    fLastSavedReplay = outPath;
    fReplayIndex.add(outPath);
    std::println(std::cerr, "[replay] saved {}", outPath.string());
  }

  [[nodiscard]] osu::Vec2 toPlayfield(float sx, float sy) const {
    return {(static_cast<double>(sx) - fOffsetX) / fScale,
            (static_cast<double>(sy) - fOffsetY) / fScale};
  }

};

} // namespace client
