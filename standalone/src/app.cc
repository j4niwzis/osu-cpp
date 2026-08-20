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
import client.filter;
import client.loader;
import client.ui;
import client.settings;
import client.settingspanel;
import client.overlays;
import client.filtercontrol;
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
  std::filesystem::path fReplayPath;
  bool fRecord = false;
  std::string fBeatmapFilename;
  std::vector<osu::InputEvent> fAutoplayEvents;
  std::vector<osu::InputEvent> fRecordedEvents;
  std::size_t fAutoplayIndex = 0;
  Skin fSkin;
  bool fShowProfile = false;
  bool fNoGlow = false;
  skia::Sp<skia::SkImage> fBackground;
  skia::Sp<skia::SkImage> fBackgroundScaled;

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
  skia::SkIRect fDirtyBounds;
  bool fFirstFrame = true;

  // Input
  osu::Vec2 fCursor = osu::kPlayfieldCenter;
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
  float fCarouselScroll = 0.0f;
  bool fUserScrolled = false;
  int fBackgroundForSet = -1;
  int fMenuMusicForSet = -1; // set whose audio is looping under the menus
  std::filesystem::path fMapsDir;
  std::filesystem::path fThumbDir;
  struct CarouselHit {
    skia::SkRect fRect;
    int fSetIdx;
    int fDiffIdx; // -1 => set header
  };
  std::vector<CarouselHit> fCarouselHits; // rebuilt every song-select frame
  std::mutex fDropMutex;                  // guards fDropped
  std::vector<std::string> fDropped;      // files dropped onto the window
  skia::SkRect fDownloadsChip = skia::SkRect::MakeEmpty(); // bottom-bar button
  skia::SkRect fImportChip = skia::SkRect::MakeEmpty();    // footer button
  skia::SkRect fRandomChip = skia::SkRect::MakeEmpty();    // footer button
  skia::SkRect fSettingsChip = skia::SkRect::MakeEmpty();  // footer button

  // Download screen (mirror search + .osz fetch).
  struct DownloadEntry {
    long fSetId = -1;
    std::string fTitle, fArtist, fCreator, fRankStatus;
    float fStarsMin = 0.0f;
    float fStarsMax = 0.0f;
    int fDiffCount = 0;
    enum class St : std::uint8_t { kIdle, kFetching, kDone, kError };
    St fSt = St::kIdle;
    std::shared_ptr<client::http::Handle> fHandle;
    enum class Thumb : std::uint8_t { kNone, kFetching, kReady, kFailed };
    Thumb fThumbSt = Thumb::kNone;
    skia::Sp<skia::SkImage> fThumb;
  };
  std::string fSearchQuery;
  bool fSearchPending = false;
  bool fSwallowChar = false; // the 'D' that opened the screen also arrives
                             // as a char event; it must not enter the query
  std::vector<DownloadEntry> fFound;
  float fDownloadScroll = 0.0f;
  std::string fDownloadStatus = "Type a query and press Enter";
  std::vector<skia::SkRect> fFoundHits; // rebuilt every download frame

  // Pause / results overlays.
  struct MenuButton {
    skia::SkRect fRect;
    std::string fLabel;
    skia::SkColor fAccent;
  };
  std::vector<MenuButton> fMenuButtons; // rebuilt every pause/results frame
  double fPausedNow = 0.0;              // frozen game time while paused
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
  float fScrollAnim = 0.0f;    // smoothed carousel scroll
  float fPopAnim = 1.0f;       // selected row pop-out progress
  int fPrevSelKey = -1;

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
  };
  std::vector<MenuBtn> fMenuBtns;
  MenuState fMenuState = MenuState::kInitial;
  float fLogoX = 0.0f;
  float fLogoY = 0.0f;
  float fLogoScale = 1.0f;
  bool fLogoPlaced = false;
  float fLogoHover = 0.0f;
  float fLogoPunch = 0.0f; // click/beat impact, decays
  skia::SkRect fLogoRect = skia::SkRect::MakeEmpty();
  client::Spectrum fSpectrum;

  // ---- Settings overlay -------------------------------------------------
  // SettingsPanel: a 170px sidebar plus a 400px content column sliding in
  // from the left over 600 ms. The sidebar does not swap pages -- it scrolls
  // the single continuous column of sections, and highlights whichever
  // section the scroll is currently in.
  client::Settings fSettings;
  client::SettingsPanel fSettingsPanel;
  float fAppliedDim = 0.7f;

  // ---- Overlays (views live in client.overlays) -------------------------
  client::ModSelect fModSelect;
  client::ExportDialog fExportDialog;


  AnchoredClock fMenuClock;
  double fMenuClockSyncWall = std::numeric_limits<double>::lowest();
  double fLastMenuPosMs = 0.0;
  struct Tri {
    float fX = 0.0f;     // 0..1 of width
    float fY = 0.0f;     // 0..1 of height
    float fScale = 1.0f; // multiplies triangle_size
    float fShade = 0.5f;
  };
  std::vector<Tri> fTriangles;
  std::vector<Tri> fLogoTris; // TrianglesV2 masked inside the logo
  float fTriangleScale = 2.4f;    // TrianglesV2 uses much larger shapes
  float fSpawnRatio = 1.0f;       // TrianglesV2.SpawnRatio
  float fTriangleVelocity = 1.0f; // TrianglesV2.Velocity
  float fMenuDim = 1.0f; // MainMenu.cs: Gray(1) idle, Gray(0.8) with buttons
  std::mt19937 fUiRng{0xC0FFEEu};

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
  skia::SkPaint fHudPaint{[] {
    skia::SkPaint p;
    p.setAntiAlias(true);
    return p;
  }()};

  // Judgement pop-ups (drawn in playfield coordinates).
  struct Popup {
    osu::Judgement fResult;
    double fTime;
    osu::Vec2 fPos;
  };
  std::vector<Popup> fPopups;
  static constexpr double kPopupLifetime = 700.0;

  // Hit-burst animations.
  struct HitBurst {
    osu::Vec2 fPos;
    double fTime;
    std::size_t fComboIndex;
  };
  std::vector<HitBurst> fHitBursts;
  static constexpr double kHitBurstLifetime = 350.0;

  // Cursor trail.
  struct CursorTrailPoint {
    osu::Vec2 fPos;
    double fTime;
  };
  std::deque<CursorTrailPoint> fCursorTrail;
  static constexpr double kCursorTrailLifetime = 140.0;
  static constexpr std::size_t kCursorTrailMax = 40;

  // Fading judged objects.
  struct FadingObject {
    std::size_t fIndex;
    double fTime;
    osu::Judgement fResult;
  };
  std::vector<FadingObject> fFadingObjects;
  static constexpr double kFadeLifetime = 250.0;

  // Smoothly interpolated HUD values (matches webosu-2 LazyNumber lag=200).
  double fDisplayHealth = 1.0;
  double fDisplayScore = 0.0;
  double fDisplayCombo = 0.0;
  double fDisplayAccuracy = 1.0;
  double fLastHudTime = 0.0;

  static constexpr std::size_t kFpsSampleCount = 120;
  double fFrameTimes[kFpsSampleCount]{};
  std::size_t fFrameTimeIdx = 0;
  std::size_t fFrameTimeCount = 0;
  double fLastFrameTime = 0.0;

  static constexpr std::size_t kProfileCount = 60;
  struct ProfileFrame {
    double advUs, renderUs, flushUs, swapUs;
    double renderFollowUs, renderObjectsUs, renderRestUs, renderHudUs;
  };
  ProfileFrame fProfile[kProfileCount]{};
  std::size_t fProfileIdx = 0;
  std::size_t fProfileNum = 0;

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
    if (fAutoplay) {
      if (!fReplayPath.empty()) {
        std::ifstream file(fReplayPath, std::ios::binary);
        if (file) {
          std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>(file),
                                          std::istreambuf_iterator<char>()};
          auto replayData = osu::decodeReplay(bytes);
          fAutoplayEvents = std::move(replayData.fEvents);
          fMods = replayData.fMods;
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
        fBackground = loadImage(bytes);
        this->preScaleBackground();
      }
    }

    fStartMs = glfw::glfwGetTime() * 1000.0;
    fClock.reset(fStartMs, 0.0);
    fLastClockSyncWall = std::numeric_limits<double>::lowest();
    fAudio.setVolume(fSettings.value("master") * fSettings.value("music"));
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
    glfw::glfwWindowHint(glfw::kClientApi, glfw::kOpenGLApi);
    glfw::glfwWindowHint(glfw::kContextVersionMajor, 4);
    glfw::glfwWindowHint(glfw::kContextVersionMinor, 1);
    glfw::glfwWindowHint(glfw::kOpenGLProfile, glfw::kOpenGLCoreProfile);
    glfw::glfwWindowHint(glfw::kOpenGLForwardCompat, glfw::kTrue);
    glfw::glfwWindowHint(glfw::kResizable, glfw::kTrue);

    const auto monitor = glfw::glfwGetPrimaryMonitor();
    const glfw::GLFWvidmode *mode = glfw::glfwGetVideoMode(monitor);
    fScreenW = mode->width;
    fScreenH = mode->height;

    fWindow = glfw::glfwCreateWindow(fScreenW, fScreenH, "osu_client", monitor,
                                     nullptr);
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
          if (action == glfw::kRepeat)
            return; // key auto-repeat is meaningless for gameplay
          self->enqueue({App::wallMs(), EventType::kKey, key, action,
                         static_cast<float>(mods)});
        });
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

    fFont = this->loadFont(20.0f);
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
    }
    fQuit.store(true, std::memory_order_release);
    renderThread.join();
    return fExitCode.load(std::memory_order_acquire);
#endif
  }

#ifndef __EMSCRIPTEN__
  void renderThreadMain() {
    glfw::glfwMakeContextCurrent(fWindow);
    glfw::glfwSwapInterval(1);

    if (!this->initSkia()) {
      fExitCode.store(1, std::memory_order_release);
      this->requestQuit();
      return;
    }

    fFont = this->loadFont(20.0f);
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
    Event ev;
    while (fInputQueue.tryPop(ev)) {
      this->applyEvent(ev);
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
      if (fState == State::kPlaying && !fAutoplay) {
        fCursor = this->applySensitivity(this->toPlayfield(ev.fX, ev.fY));
        this->submitTimed({this->eventGameTime(ev.fWallMs), fCursor,
                           osu::InputAction::kMove});
      }
      break;
    case EventType::kScroll:
      if (fSettingsPanel.open()) {
        this->scrollSettings(ev.fX);
        break;
      }
      if (fState == State::kSongSelect) {
        // Free scrolling, like lazer's carousel: the wheel moves the view,
        // the selection stays put until the user picks something else.
        fCarouselScroll -= ev.fX * 90.0f;
        fUserScrolled = true;
      } else if (fState == State::kDownload) {
        fDownloadScroll -= ev.fX * 60.0f;
      }
      break;
    case EventType::kChar:
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
        if (fSwallowChar) {
          fSwallowChar = false;
          break;
        }
        this->appendUtf8(fSearchQuery, static_cast<std::uint32_t>(ev.fA));
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

  void applyKey(const Event &ev) {
    const int key = ev.fA;
    const int action = ev.fB;
    // GLFW reports the modifier state with every key event; tracking press
    // and release of the control keys separately loses sync whenever focus
    // changes while held.
    const bool ctrl = (static_cast<int>(ev.fX) & glfw::kModControl) != 0;
    if (key == glfw::kKeyLeftControl || key == glfw::kKeyRightControl) {
      return;
    }
    if (action == glfw::kPress && key == glfw::kKeyO && ctrl) {
      this->toggleSettings();
      return;
    }
    if (action == glfw::kPress && key == glfw::kKeyEscape) {
      if (fExportDialog.open()) {
        fExportDialog.close();
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
      if (key == glfw::kKeyEscape) {
        this->resumeGame();
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
      this->startSearch(); // empty query => recently ranked listing
    }
  }

  void keyDownload(int key) {
    if (key == glfw::kKeyEscape) {
      this->switchState(State::kSongSelect);
      return;
    }
    if (key == glfw::kKeyEnter) {
      this->startSearch();
      return;
    }
    if (key == glfw::kKeyBackspace) {
      this->popUtf8(fSearchQuery);
    }
  }

  void applyMouseButton(const Event &ev) {
    const int button = ev.fA;
    const int action = ev.fB;

    if (fState == State::kMainMenu || fState == State::kSongSelect ||
        fState == State::kDownload || fState == State::kPaused ||
        fState == State::kResults) {
      if (button == glfw::kMouseButtonLeft) {
        if (action == glfw::kPress) {
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
    if (this->exportClick(x, y)) {
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
      break;
    }
    case State::kSongSelect:
      if (this->filterClick(x, y, true)) {
        return;
      }
      if (fDownloadsChip.contains(x, y)) {
        this->openDownloads();
        return;
      }
      if (fImportChip.contains(x, y)) {
        this->importOsz();
        return;
      }
      if (fSettingsChip.contains(x, y)) {
        this->toggleSettings();
        return;
      }
      if (fRandomChip.contains(x, y)) {
        this->selectRandom();
        return;
      }
      for (const auto &hit : fCarouselHits) {
        if (hit.fRect.contains(x, y)) {
          if (hit.fDiffIdx < 0) {
            fSelSet = hit.fSetIdx;
            fSelDiff = 0;
          } else if (fSelSet == hit.fSetIdx && fSelDiff == hit.fDiffIdx) {
            this->startPlay(hit.fSetIdx, hit.fDiffIdx); // second click plays
          } else {
            fSelSet = hit.fSetIdx;
            fSelDiff = hit.fDiffIdx;
          }
          return;
        }
      }
      break;
    case State::kDownload:
      for (std::size_t i = 0; i < fFoundHits.size(); ++i) {
        if (fFoundHits[i].contains(x, y)) {
          this->startDownload(i);
          return;
        }
      }
      break;
    case State::kPaused:
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

  void menuButtonPressed(std::size_t idx) {
    if (fState == State::kPaused) {
      if (idx == 0) {
        this->resumeGame();
      } else if (idx == 1) {
        this->retry();
      } else {
        this->quitToSelect();
      }
    } else if (fState == State::kResults) {
      if (idx == 0) {
        this->retry();
      } else if (idx == 1) {
        this->quitToSelect();
      } else {
        fExportDialog.show();
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
    if (fRecord) {
      fRecordedEvents.push_back(ev);
    }
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
    std::println(std::cerr, "[ui] {} -> {}", stateName(fState), stateName(st));
    fState = st;
    fStateEnterWall = wallMs();
    if (st == State::kMainMenu) {
      // Returning to the menu always lands on the top level, never on a
      // stale submenu, and the logo re-eases into place from where it was.
      fMenuState = MenuState::kTopLevel;
    }
  }

  // ---- Frame dispatch ---------------------------------------------------

  void frame() {
    client::http::poll();  // completed network callbacks land here
    fLoader.poll();        // finished background loads land here
    this->drainDroppedFiles();
    this->drainInput();
    {
      const double wallNow = wallMs();
      fUiDt = fUiPrevWall > 0.0 ? std::min(50.0, wallNow - fUiPrevWall) : 16.0;
      fUiPrevWall = wallNow;
    }
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
  }

  void present() {
    // Overlays float above whatever screen is drawn.
    auto *canvas = fSurface->getCanvas();
    if (fModSelect.visible()) {
      this->drawModSelect(canvas);
    }
    if (fSettingsPanel.visible()) {
      this->drawSettings(canvas);
    }
    if (fExportDialog.open()) {
      this->drawExportDialog(canvas);
    }
    fContext->flushAndSubmit(fSurface.get());
    glfw::glfwSwapBuffers(fWindow);
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
    if (fShowProfile || fSettings.flag("fps")) {
      auto t0 = clock::now();
      fEngine->advance(now);
      auto t1 = clock::now();
      this->playHitsounds(now);
      this->render(now);
      auto t2 = clock::now();
      fContext->flushAndSubmit(fSurface.get());
      auto t3 = clock::now();
      glfw::glfwSwapBuffers(fWindow);
      auto t4 = clock::now();

      auto &p = fProfile[fProfileIdx];
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
      fProfileIdx = (fProfileIdx + 1) % kProfileCount;
      if (fProfileNum < kProfileCount)
        ++fProfileNum;
    } else {
      fEngine->advance(now);
      this->playHitsounds(now);
      this->render(now);
      this->present();
    }
  }

  // ---- Play lifecycle ---------------------------------------------------

  void resetGameplayState() {
    fPlayedEvents = 0;
    fCombo = 0;
    fPopups.clear();
    fHitBursts.clear();
    fCursorTrail.clear();
    fFadingObjects.clear();
    fAutoplayIndex = 0;
    fRecordedEvents.clear();
    fHeldMask = 0;
    fDisplayHealth = 1.0;
    fDisplayScore = 0.0;
    fDisplayCombo = 0.0;
    fDisplayAccuracy = 1.0;
    fLastHudTime = 0.0;
    fFrameTimeIdx = 0;
    fFrameTimeCount = 0;
    fLastFrameTime = 0.0;
  }

  void startPlay(int setIdx, int diffIdx) {
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
    fFirstFrame = true;
    this->setCursorVisible(false);
  }

  void retry() { this->startPlay(fPlayingSet, fPlayingDiff); }

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
    fFirstFrame = true;
    this->setCursorVisible(false);
  }

  void quitToSelect() {
    fAudio.stop();
    fMenuMusicForSet = -1;  // let updateMenuMusic restart the loop
    this->switchState(State::kSongSelect);
    fFirstFrame = true;
    fBackgroundForSet = -1; // gameplay replaced the cached background
    this->setCursorVisible(true);
  }

  void finishPlay() {
    this->captureResult();
    this->printResult();
    // Replays are kept by default (the setting can turn it off), so a good
    // run is never lost because the flag was not passed.
    if (fRecord || fSettings.flag("savereplay")) {
      this->saveReplay();
    }
    this->switchState(State::kResults);
    fFirstFrame = true;
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
    fCursorModeRequest.store(visible ? glfw::kCursorNormal
                                     : glfw::kCursorHidden,
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
    std::filesystem::create_directories(fThumbDir, ec);
    fMapCache.load(fMapsDir / "metadata-cache.json");
    fSettings.load(fMapsDir.parent_path() / "settings.json");
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
      if (!fAudio.playing()) {
        fAudio.play(); // track ended and looping missed the wrap; nudge it
      }
      return;
    }
    auto set = this->setFor(fSelSet);
    if (!set) {
      return; // still loading; try again next frame
    }
    fMenuMusicForSet = fSelSet;
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
    fLoader.submit(
        static_cast<std::uint64_t>(fSelSet) | (3ull << 32),
        [copy = std::move(copy), ext, pcm] {
          *pcm = audio_client::decodeAudio(copy, ext);
        },
        [this, forSet, pcm] {
          if (forSet != fSelSet || pcm->fSamples.empty()) {
            return; // selection moved on while decoding
          }
          fAudio.adopt(std::move(*pcm));
          fAudio.setLooping(true);
          fAudio.setVolume(fSettings.value("master") * fSettings.value("music"));
          fAudio.play();
          // The analysis clock is anchored to the *old* track until reset.
          fMenuClock.reset(wallMs(), 0.0);
          fMenuClockSyncWall = std::numeric_limits<double>::lowest();
          fSpectrum.reset();
        });
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
    for (const auto &info : set.fBeatmaps) {
      diffs.push_back({info.fFilename, info.fMeta.fTitle,
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
        osu::BeatmapInfo info;
        info.fFilename = d.fFilename;
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
        entry.fInfos.push_back(std::move(info));
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

      std::vector<client::CachedDifficulty> diffs;
      for (const auto &info : set->fBeatmaps) {
        diffs.push_back({info.fFilename, info.fMeta.fTitle,
                         info.fMeta.fTitleUnicode, info.fMeta.fArtist,
                         info.fMeta.fArtistUnicode, info.fMeta.fCreator,
                         info.fMeta.fVersion, info.fMeta.fAudioFilename,
                         info.fMeta.fBackground, info.fStars, info.fDiff.fCs,
                         info.fDiff.fAr, info.fDiff.fOd, info.fDiff.fHp,
                         info.fLengthMs, info.fObjectCount});
      }
      fMapCache.store(key, size, mtime, std::move(diffs));
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
      entry.fLoaded =
          std::make_shared<osu::BeatmapSet>(loadBeatmapSet(entry.fPath));
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
          *result = std::make_shared<osu::BeatmapSet>(loadBeatmapSet(path));
        },
        [this, index, path, result] {
          if (index >= static_cast<int>(fLibrary.size()) ||
              fLibrary[static_cast<std::size_t>(index)].fPath != path) {
            return; // library was re-sorted underneath us
          }
          if (!*result) {
            return;
          }
          fLibrary[static_cast<std::size_t>(index)].fLoaded = *result;
          this->touchLoaded(index);
        });
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

  bool addOszToLibrary(const std::filesystem::path &path, bool select) {
    const std::size_t before = fLibrary.size();
    this->scanArchive(path);
    if (fLibrary.size() == before) {
      return false;
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

  static constexpr const char *kMirror = "https://catboy.best";

  void startSearch() {
    if (fSearchPending) {
      return;
    }
    fSearchPending = true;
    fDownloadStatus = "Searching...";
    auto handle = std::make_shared<client::http::Handle>();
    const std::string url = std::string(kMirror) +
                            "/api/v2/search?mode=0&limit=50&query=" +
                            client::http::urlEncode(fSearchQuery);
    client::http::get(url, std::move(handle),
                      [this](client::http::Response r) {
                        this->onSearchDone(std::move(r));
                      });
  }

  void onSearchDone(client::http::Response r) {
    fSearchPending = false;
    if (!r.fOk) {
      fDownloadStatus = "Search failed: " + r.fError;
      return;
    }
    const auto parsed = bjson::tryParse(r.fBody);
    if (!parsed) {
      fDownloadStatus = "Search failed: malformed JSON";
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
      return;
    }

    const auto getStr = [](const bjson::object &o,
                           std::string_view key) -> std::string {
      if (const bjson::value *v = o.if_contains(key)) {
        if (const bjson::string *str = v->if_string()) {
          return std::string(str->begin(), str->end());
        }
      }
      return {};
    };

    fFound.clear();
    for (const auto &e : *arr) {
      const bjson::object *o = e.if_object();
      if (o == nullptr) {
        continue;
      }
      DownloadEntry d;
      const bjson::value *id = o->if_contains("id");
      if (id == nullptr || !id->is_int64()) {
        continue;
      }
      d.fSetId = static_cast<long>(id->as_int64());
      d.fTitle = getStr(*o, "title");
      d.fArtist = getStr(*o, "artist");
      d.fCreator = getStr(*o, "creator");
      d.fRankStatus = getStr(*o, "status");
      if (const bjson::value *bms = o->if_contains("beatmaps")) {
        if (const bjson::array *ba = bms->if_array()) {
          for (const auto &bm : *ba) {
            if (const bjson::object *bo = bm.if_object()) {
              if (const bjson::value *sr = bo->if_contains("difficulty_rating");
                  sr != nullptr && sr->is_number()) {
                const auto v = static_cast<float>(sr->to_number<double>());
                if (d.fDiffCount == 0) {
                  d.fStarsMin = d.fStarsMax = v;
                } else {
                  d.fStarsMin = std::min(d.fStarsMin, v);
                  d.fStarsMax = std::max(d.fStarsMax, v);
                }
                ++d.fDiffCount;
              }
            }
          }
        }
      }
      fFound.push_back(std::move(d));
    }
    fDownloadScroll = 0.0f;
    fDownloadStatus =
        fSearchQuery.empty()
            ? std::format("recently ranked - {} maps", fFound.size())
            : std::format("{} result(s)", fFound.size());
  }

  void requestThumb(std::size_t idx) {
    if (idx >= fFound.size()) {
      return;
    }
    auto &d = fFound[idx];
    if (d.fThumbSt != DownloadEntry::Thumb::kNone) {
      return;
    }
    int inflight = 0;
    for (const auto &e : fFound) {
      if (e.fThumbSt == DownloadEntry::Thumb::kFetching) {
        ++inflight;
      }
    }
    if (inflight >= 4) {
      return; // retry on a later frame
    }
    d.fThumbSt = DownloadEntry::Thumb::kFetching;
    const long id = d.fSetId;
    auto handle = std::make_shared<client::http::Handle>();
    client::http::get(
        std::format("https://assets.ppy.sh/beatmaps/{}/covers/card.jpg", id),
        std::move(handle), [this, id](client::http::Response r) {
          for (auto &e : fFound) {
            if (e.fSetId != id) {
              continue;
            }
            if (r.fOk && r.fBody.size() > 256) {
              std::vector<std::uint8_t> bytes(r.fBody.begin(), r.fBody.end());
              e.fThumb = loadImage(bytes);
              e.fThumbSt = e.fThumb ? DownloadEntry::Thumb::kReady
                                    : DownloadEntry::Thumb::kFailed;
            } else {
              e.fThumbSt = DownloadEntry::Thumb::kFailed;
            }
            break;
          }
        });
  }

  [[nodiscard]] static skia::SkColor statusColorFor(std::string_view st) {
    if (st == "ranked" || st == "approved") {
      return skia::colorSetARGB(255, 102, 204, 255);
    }
    if (st == "loved") {
      return skia::colorSetARGB(255, 255, 102, 170);
    }
    if (st == "qualified") {
      return skia::colorSetARGB(255, 255, 204, 102);
    }
    return skia::colorSetARGB(255, 140, 140, 155);
  }

  void startDownload(std::size_t idx) {
    if (idx >= fFound.size()) {
      return;
    }
    auto &d = fFound[idx];
    if (d.fSt == DownloadEntry::St::kFetching ||
        d.fSt == DownloadEntry::St::kDone) {
      return;
    }
    d.fSt = DownloadEntry::St::kFetching;
    d.fHandle = std::make_shared<client::http::Handle>();
    const long id = d.fSetId;
    client::http::get(std::format("{}/d/{}", kMirror, id), d.fHandle,
                      [this, id](client::http::Response r) {
                        this->onDownloadDone(id, std::move(r));
                      });
  }

  void onDownloadDone(long id, client::http::Response r) {
    DownloadEntry *d = nullptr;
    for (auto &e : fFound) {
      if (e.fSetId == id) {
        d = &e;
        break;
      }
    }
    if (!r.fOk || r.fBody.size() < 1024) {
      if (d != nullptr) {
        d->fSt = DownloadEntry::St::kError;
      }
      fDownloadStatus =
          "Download failed: " + (r.fError.empty() ? "empty file" : r.fError);
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
        d->fSt = DownloadEntry::St::kDone;
      }
      fDownloadStatus = "Added to library: " +
                        (d != nullptr ? d->fTitle : std::to_string(id));
    } else if (d != nullptr) {
      d->fSt = DownloadEntry::St::kError;
    }
  }

#ifdef __EMSCRIPTEN__
  static void emscriptenFrameProc(void *arg) {
    static_cast<App *>(arg)->emscriptenFrame();
  }

  void emscriptenFrame() {
    glfw::glfwPollEvents();

    {
      double cx = 0, cy = 0;
      glfw::glfwGetCursorPos(fWindow, &cx, &cy);
      this->enqueue({wallMs(), EventType::kCursorMove, 0, 0,
                     static_cast<float>(cx), static_cast<float>(cy)});
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
      fBackground.reset();
      fBackgroundScaled.reset();
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
                     fBackground = *image;
                     this->preScaleBackground();
                   });
  }

  void loadSelectBackground(const osu::BeatmapSet &set) {
    for (const auto &info : set.fBeatmaps) {
      if (!info.fMeta.fBackground.empty()) {
        const auto bytes = set.findFile(info.fMeta.fBackground);
        if (!bytes.empty()) {
          fBackground = loadImage(bytes);
          this->preScaleBackground();
          return;
        }
      }
    }
    fBackground.reset();
    fBackgroundScaled.reset();
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
    if (fBackgroundScaled) {
      this->drawBackground(canvas);
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

  // Port of osu.Game/Graphics/Backgrounds/Triangles.cs. Constants are the
  // originals: 100px base size, 0.866 equilateral ratio, 50px/s base velocity,
  // scale drawn from a normal distribution (mean 0.5, sigma 0.16, floor 0.1),
  // count derived from the drawing area. Particles drift upward and respawn
  // below once they leave the top edge.
  static constexpr float kTriangleSize = 100.0f;
  static constexpr float kTriangleRatio = 0.866f;
  static constexpr float kTriangleBaseVelocity = 50.0f;
  static constexpr int kMaxTriangles = 1000;

  [[nodiscard]] float randomTriangleScale() {
    // Box-Muller, as in TrianglesV2.createTriangle: normal(0.5, 0.16^2).
    std::uniform_real_distribution<float> u(1e-6f, 1.0f);
    const float u1 = u(fUiRng);
    const float u2 = u(fUiRng);
    const float randStdNormal =
        std::sqrt(-2.0f * std::log(u1)) *
        std::sin(2.0f * std::numbers::pi_v<float> * u2);
    return std::max(0.5f + 0.16f * randStdNormal, 0.1f);
  }

  void ensureTriangles() {
    const float sw = static_cast<float>(fScreenW);
    const float sh = static_cast<float>(fScreenH);
    if (sw <= 0.0f || sh <= 0.0f) {
      return;
    }
    // TrianglesV2: AimCount = clamp(DrawWidth * 0.02 * SpawnRatio, 1, max).
    // Far sparser than the old Triangles -- a couple of dozen large shapes,
    // not a swarm.
    const int aim = std::clamp(static_cast<int>(sw * 0.02f * fSpawnRatio), 1,
                               kMaxTriangles);
    if (static_cast<int>(fTriangles.size()) == aim) {
      return;
    }
    std::uniform_real_distribution<float> ux(0.0f, 1.0f);
    while (static_cast<int>(fTriangles.size()) < aim) {
      fTriangles.push_back({ux(fUiRng), ux(fUiRng), this->randomTriangleScale(),
                            ux(fUiRng)});
    }
    while (static_cast<int>(fTriangles.size()) > aim) {
      fTriangles.pop_back();
    }
    std::ranges::sort(fTriangles, {}, &Tri::fScale);
  }

  void updateAndDrawTriangles(skia::SkCanvas *canvas) {
    const float sw = static_cast<float>(fScreenW);
    const float sh = static_cast<float>(fScreenH);
    const float elapsedSeconds = static_cast<float>(fUiDt) / 1000.0f;
    // TrianglesV2: movedDistance = -elapsed * Velocity * base_velocity / height
    const float moved =
        elapsedSeconds * fTriangleVelocity * kTriangleBaseVelocity / sh;

    std::uniform_real_distribution<float> ux(0.0f, 1.0f);
    skia::SkPaint paint;
    paint.setAntiAlias(true);
    paint.setStyle(skia::kStrokeStyle);

    for (auto &t : fTriangles) {
      t.fY -= moved;
      const float w = kTriangleSize * t.fScale * fTriangleScale;
      const float h = w * kTriangleRatio;
      if (t.fY * sh + h < 0.0f) {
        t.fY = 1.0f + h / sh;
        t.fX = ux(fUiRng);
        t.fScale = this->randomTriangleScale();
        t.fShade = ux(fUiRng);
      }

      const float cx = t.fX * sw;
      const float cy = t.fY * sh;
      skia::SkPathBuilder b;
      b.moveTo(cx, cy - h * 0.5f);
      b.lineTo(cx - w * 0.5f, cy + h * 0.5f);
      b.lineTo(cx + w * 0.5f, cy + h * 0.5f);
      b.close();
      // Thickness = 0.02 of the triangle's own size, per TrianglesV2.
      paint.setStrokeWidth(std::max(1.0f, w * 0.02f * 2.0f));
      paint.setColor(skia::kWhite);
      paint.setAlphaf(0.06f + 0.10f * t.fShade);
      canvas->drawPath(b.detach(), paint);
    }
  }


  void frameMainMenu() {
    this->ensureMenuButtons();
    this->updateMenuMusic();
    this->updateMenuSpectrum();

    auto *canvas = fSurface->getCanvas();
    const float sw = static_cast<float>(fScreenW);
    const float sh = static_cast<float>(fScreenH);

    // lazer's main menu shows the beatmap background at full brightness --
    // MainMenu.cs fades it to Gray(1) at the logo-only state and Gray(0.8)
    // once the buttons are out. There is no triangle overlay over artwork;
    // triangles are only the fallback background when no art exists at all.
    if (!fLibrary.empty() && fBackgroundForSet != fSelSet) {
      // setFor() is asynchronous: only mark the background as up to date once
      // the set has actually arrived, otherwise the first frame consumes the
      // request and the artwork never appears.
      if (auto set = this->setFor(fSelSet)) {
        fBackgroundForSet = fSelSet;
        this->requestBackground(fSelSet, set);
      }
    }
    const float dimTarget =
        fMenuState == MenuState::kInitial ? 1.0f : 0.8f;
    fMenuDim = this->approach(fMenuDim, dimTarget, 220.0f);

    if (fBackgroundScaled) {
      this->drawBackground(canvas);
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
      this->ensureTriangles();
      this->updateAndDrawTriangles(canvas);
    } else {
      canvas->clear(skia::colorSetARGB(255, 18, 14, 24));
    }

    // ---- Layout: logo plus the visible button row, centred as a group.
    const float uiScale = std::clamp(sh / 900.0f, 0.75f, 1.6f);
    const float btnW = 150.0f * uiScale;
    const float btnH = 96.0f * uiScale;
    const float btnGap = 6.0f * uiScale;
    const float wedge = 20.0f * uiScale; // lazer's parallelogram shear

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

    const float logoBase = std::min(sw, sh) * 0.17f;
    const float logoR =
        logoBase * (fMenuState == MenuState::kInitial ? 1.0f : 0.62f);
    const float rightW =
        static_cast<float>(rightCount) * (btnW + btnGap);
    const float leftW = static_cast<float>(leftCount) * (btnW + btnGap);
    const float groupW = leftW + 2.0f * logoR + 28.0f * uiScale + rightW;

    const float targetLogoX =
        fMenuState == MenuState::kInitial
            ? sw * 0.5f
            : (sw - groupW) * 0.5f + leftW + logoR;
    const float targetLogoY = sh * (fMenuState == MenuState::kInitial ? 0.46f
                                                                     : 0.5f);
    const float targetScale =
        fMenuState == MenuState::kInitial ? 1.0f : 0.62f;

    if (!fLogoPlaced) {
      fLogoX = targetLogoX;
      fLogoY = targetLogoY;
      fLogoScale = targetScale;
      fLogoPlaced = true;
    }
    fLogoX = this->approach(fLogoX, targetLogoX, 140.0f);
    fLogoY = this->approach(fLogoY, targetLogoY, 140.0f);
    fLogoScale = this->approach(fLogoScale, targetScale, 140.0f);

    // ---- Buttons: animate, lay out, draw.
    const float rowY = fLogoY - btnH * 0.5f;
    float xRight = fLogoX + logoBase * fLogoScale + 28.0f * uiScale;
    float xLeft = fLogoX - logoBase * fLogoScale - 28.0f * uiScale;

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

      this->drawMenuWedge(canvas, b, wedge);
    }

    // ---- Logo on top of the visualiser.
    this->drawLogo(canvas, logoBase);

    const char *hint = fMenuState == MenuState::kInitial
                           ? "click the logo    Esc quit"
                       : fMenuState == MenuState::kTopLevel
                           ? "P play   B browse   I import   Q exit   "
                             "Ctrl+O settings"
                           : "S solo    R random    Esc back";
    this->drawBottomBar(canvas, hint);
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
  void drawLogo(skia::SkCanvas *canvas, float logoBase) {
    const float wall = static_cast<float>(wallMs());
    const bool hovered =
        (fMouseX - fLogoX) * (fMouseX - fLogoX) +
            (fMouseY - fLogoY) * (fMouseY - fLogoY) <=
        (logoBase * fLogoScale) * (logoBase * fLogoScale);
    fLogoHover = this->approach(fLogoHover, hovered ? 1.0f : 0.0f, 110.0f);
    fLogoPunch = this->approach(fLogoPunch, 0.0f, 180.0f);

    // Amplitude-driven beat: lazer drives these from timing points, which the
    // menu has not loaded, so bass energy stands in.
    const float amp = fSpectrum.bass();
    const float beat = 1.0f - 0.02f * amp;
    const float r = logoBase * fLogoScale * beat *
                    (1.0f + 0.06f * fLogoHover + 0.10f * fLogoPunch);
    fLogoRect = skia::SkRect::MakeXYWH(fLogoX - r, fLogoY - r, r * 2, r * 2);

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

  // TrianglesV2 inside the logo: Thickness 0.009, ScaleAdjust 3,
  // SpawnRatio 1.4, gradient #ff66ab -> #b6346f.
  void drawLogoTriangles(skia::SkCanvas *canvas, const skia::SkRect &rect) {
    const float w = rect.width();
    const float h = rect.height();
    if (fLogoTris.empty()) {
      std::uniform_real_distribution<float> u(0.0f, 1.0f);
      const int aim = std::max(1, static_cast<int>(w * 0.02f * 1.4f));
      for (int i = 0; i < aim; ++i) {
        fLogoTris.push_back({u(fUiRng), u(fUiRng),
                             this->randomTriangleScale() * 3.0f, u(fUiRng)});
      }
      std::ranges::sort(fLogoTris, {}, &Tri::fScale);
    }
    const float moved = static_cast<float>(fUiDt) / 1000.0f *
                        kTriangleBaseVelocity / std::max(1.0f, h);
    std::uniform_real_distribution<float> u(0.0f, 1.0f);
    skia::SkPaint p;
    p.setAntiAlias(true);
    p.setStyle(skia::kStrokeStyle);
    for (auto &t : fLogoTris) {
      t.fY -= moved;
      const float tw = kTriangleSize * t.fScale * 0.35f;
      const float th = tw * kTriangleRatio;
      if (t.fY * h + th < 0.0f) {
        t.fY = 1.0f + th / h;
        t.fX = u(fUiRng);
        t.fScale = this->randomTriangleScale() * 3.0f;
      }
      const float cx = rect.fLeft + t.fX * w;
      const float cy = rect.fTop + t.fY * h;
      skia::SkPathBuilder b;
      b.moveTo(cx, cy - th * 0.5f);
      b.lineTo(cx - tw * 0.5f, cy + th * 0.5f);
      b.lineTo(cx + tw * 0.5f, cy + th * 0.5f);
      b.close();
      p.setStrokeWidth(std::max(1.0f, tw * 0.009f * 4.0f));
      // Gradient #ff66ab -> #b6346f across the logo. Clamp: fY runs outside
      // [0,1] while a particle is above the top edge or waiting to respawn,
      // and the unclamped lerp wrapped the colour bytes (hence blue tips).
      const float shade = std::clamp(t.fY, 0.0f, 1.0f);
      p.setColor(skia::colorSetARGB(
          255, static_cast<std::uint8_t>(0xff + (0xb6 - 0xff) * shade),
          static_cast<std::uint8_t>(0x66 + (0x34 - 0x66) * shade),
          static_cast<std::uint8_t>(0xab + (0x6f - 0xab) * shade)));
      p.setAlphaf(0.85f);
      canvas->drawPath(b.detach(), p);
    }
  }

  // LogoVisualisation.VisualisationDrawNode, transcribed. Each bar is a quad
  // sitting on the logo's circumference, `bar_length * amplitude` long, with
  // width equal to the chord subtended by one bar; the whole ring is drawn
  // `visualiser_rounds` times, rotated, additively at 20% white.
  void drawVisualiser(skia::SkCanvas *canvas, float logoRadius) {
    const auto bars = fSpectrum.bars();
    if (bars.empty()) {
      return;
    }
    constexpr int kRounds = 5;          // visualiser_rounds
    constexpr float kAmplitudeDeadZone = 1.0f / 600.0f;
    const auto count = static_cast<int>(bars.size());
    // lazer works in a box of `size` = logo diameter, with bar_length = 600
    // against a default logo of ~480px; keep the same proportion here.
    const float barLength = logoRadius * 2.0f * (600.0f / 480.0f);
    // barSize.X = size * sqrt(2 * (1 - cos(360/bars))) / 2  -- the chord.
    const float chord =
        logoRadius * 2.0f *
        std::sqrt(2.0f * (1.0f - std::cos(2.0f * std::numbers::pi_v<float> /
                                          static_cast<float>(count)))) /
        2.0f;

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
        const float rotation =
            2.0f * std::numbers::pi_v<float> *
            (static_cast<float>(i) / static_cast<float>(count) +
             static_cast<float>(round) / static_cast<float>(kRounds));
        const float cosA = std::cos(rotation);
        const float sinA = std::sin(rotation);

        const float bx = fLogoX + cosA * logoRadius;
        const float by = fLogoY + sinA * logoRadius;
        // bottomOffset is perpendicular; amplitudeOffset is radial.
        const float ox = -sinA * chord * 0.5f;
        const float oy = cosA * chord * 0.5f;
        const float ax = cosA * barLength * amp;
        const float ay = sinA * barLength * amp;

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

  void drawSettings(skia::SkCanvas *canvas) {
    fSettingsPanel.draw(canvas, fFont, fSettings,
                        {fScreenW, fScreenH, fMouseX, fMouseY, wallMs(),
                         fUiDt});
  }

  bool settingsClick(float x, float y, bool pressed) {
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
    if (fSettingsPanel.drag(x, fSettings)) {
      this->applyAudioSettings(); // cheap part only while dragging
    }
  }

  void scrollSettings(float delta) {
    fSettingsPanel.scroll(delta, static_cast<float>(fScreenH));
  }

  void applyAudioSettings() {
    const float master = fSettings.value("master");
    fAudio.setVolume(master * fSettings.value("music"));
    for (auto &[name, player] : fSamples) {
      player.setVolume(master * fSettings.value("effect"));
    }
  }

  void applySettings() {
    this->applyAudioSettings();
    const float dim = fSettings.value("dim");
    if (std::abs(dim - fAppliedDim) > 1e-4f) {
      fAppliedDim = dim;
      this->preScaleBackground();
    }
#ifndef __EMSCRIPTEN__
    glfw::glfwSwapInterval(fSettings.flag("vsync") ? 1 : 0);
#endif
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

  bool modClick(float x, float y) { return fModSelect.click(x, y, fMods); }

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
  void exportReplayVideo() {
    if (!fMap || fRecordedEvents.empty()) {
      fExportDialog.setStatus("nothing to export");
      return;
    }
    const auto preset = client::kVideoPresets[static_cast<std::size_t>(
        fExportDialog.preset())];
    client::VideoOptions opts;
    opts.fWidth = preset.fWidth;
    opts.fHeight = preset.fHeight;
    opts.fFps = 60;
    opts.fOutput =
        fMapsDir.parent_path() / std::format("replay-{}.mp4", fBeatmapFilename);

    client::VideoExporter exporter;
    if (!exporter.begin(opts)) {
      fExportDialog.setStatus(exporter.error());
      return;
    }

    auto surface = skia::RenderTarget(
        fContext.get(), skia::kNo,
        skia::SkImageInfo::Make(opts.fWidth, opts.fHeight,
                                skia::kRGBA_8888_SkColorType,
                                skia::kPremul_SkAlphaType));
    if (!surface) {
      fExportDialog.setStatus("cannot create the offscreen surface");
      return;
    }

    const int savedW = fScreenW;
    const int savedH = fScreenH;
    auto savedSurface = fSurface;
    fSurface = surface;
    fScreenW = opts.fWidth;
    fScreenH = opts.fHeight;
    this->layoutForScreen();

    osu::Engine engine(*fMap, fMods);
    const double end = fMap->lastObjectEndTime() + 1500.0;
    const double step = 1000.0 / static_cast<double>(opts.fFps);
    std::size_t evt = 0;
    fExportDialog.setStatus("rendering...");

    for (double t = 0.0; t <= end; t += step) {
      while (evt < fRecordedEvents.size() && fRecordedEvents[evt].fTime <= t) {
        engine.submit(fRecordedEvents[evt]);
        if (fRecordedEvents[evt].fAction == osu::InputAction::kMove) {
          fCursor = fRecordedEvents[evt].fPos;
        }
        ++evt;
      }
      engine.advance(t);
      fEngine.emplace(engine);
      this->render(t);
      fContext->flushAndSubmit(fSurface.get());
      exporter.addFrame(fSurface->makeImageSnapshot());
    }

    fSurface = savedSurface;
    fScreenW = savedW;
    fScreenH = savedH;
    this->layoutForScreen();

    std::filesystem::path audioPath;
    if (!fMap->fMeta.fAudioFilename.empty()) {
      const auto bytes = fSet.findFile(fMap->fMeta.fAudioFilename);
      if (!bytes.empty()) {
        std::error_code ec;
        audioPath = std::filesystem::temp_directory_path(ec) /
                    fMap->fMeta.fAudioFilename;
        std::ofstream out(audioPath, std::ios::binary);
        out.write(reinterpret_cast<const char *>(bytes.data()),
                  static_cast<std::streamsize>(bytes.size()));
      }
    }
    opts.fAudio = audioPath;

    fExportDialog.setStatus(
        exporter.finish()
            ? std::format("saved {}", opts.fOutput.filename().string())
            : exporter.error());
  }

  // ---- Song select (port of lazer's carousel geometry) ------------------
  //
  // Numbers from osu.Game/Screens/Select: CarouselItem.DEFAULT_HEIGHT = 45 for
  // difficulty panels, PanelBeatmapSet.HEIGHT = 45 * 1.6 for set panels,
  // Panel.CORNER_RADIUS = 10, active_x_offset = 25 (doubled for unselected
  // difficulty panels, quadrupled for unselected sets, plus another 25 when
  // not keyboard-selected), transitions 400 ms OutQuint. The horizontal curve
  // is Carousel.offsetX: (3 - sqrt(9 - dist^2)) * halfHeight.
  [[nodiscard]] static float carouselOffsetX(float dist, float halfHeight) {
    constexpr float kCircleRadius = 3.0f;
    const float discriminant =
        std::max(0.0f, kCircleRadius * kCircleRadius - dist * dist);
    return (kCircleRadius - std::sqrt(discriminant)) * halfHeight;
  }

  void frameSongSelect() {
#ifdef __EMSCRIPTEN__
    if (!fLibraryLoaded) {
      if (detail::gMapsSynced.load(std::memory_order_acquire)) {
        this->initLibrary();
      } else {
        auto *canvas = fSurface->getCanvas();
        canvas->clear(skia::colorSetARGB(255, 18, 14, 24));
        this->drawTextCentered(canvas, "Syncing local storage...",
                               static_cast<float>(fScreenW) * 0.5f,
                               static_cast<float>(fScreenH) * 0.5f, 24.0f,
                               skia::kWhite, 0.8f);
        this->present();
        return;
      }
    }
#endif
    this->updateMenuMusic();
    auto *canvas = fSurface->getCanvas();

    if (!fLibrary.empty() && fBackgroundForSet != fSelSet) {
      // setFor() is asynchronous: only mark the background as up to date once
      // the set has actually arrived, otherwise the first frame consumes the
      // request and the artwork never appears.
      if (auto set = this->setFor(fSelSet)) {
        fBackgroundForSet = fSelSet;
        this->requestBackground(fSelSet, set);
      }
    }
    this->drawScreenBackground(canvas);

    const float sw = static_cast<float>(fScreenW);
    const float sh = static_cast<float>(fScreenH);
    fCarouselHits.clear();
    this->rebuildVisible();

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
      this->present();
      return;
    }

    // ---- Left: the info wedge (lazer's BeatmapTitleWedge area).
    const auto &selInfos = this->infosFor(fSelSet);
    if (!selInfos.empty()) {
      fSelDiff = std::clamp(fSelDiff, 0, static_cast<int>(selInfos.size()) - 1);
      this->drawInfoWedge(canvas, selInfos,
                          selInfos[static_cast<std::size_t>(fSelDiff)]);
    }

    // ---- Right: the carousel.
    const float uiScale = std::clamp(sh / 900.0f, 0.8f, 1.6f);
    const float setH = 45.0f * 1.6f * uiScale;  // PanelBeatmapSet.HEIGHT
    const float diffH = 45.0f * uiScale;        // CarouselItem.DEFAULT_HEIGHT
    const float gap = 5.0f * uiScale;
    const float corner = 10.0f * uiScale;       // Panel.CORNER_RADIUS
    const float activeX = 25.0f * uiScale;      // active_x_offset
    const float panelW = std::min(680.0f * uiScale, sw * 0.52f);
    const float carLeft = sw - panelW - 20.0f * uiScale;
    const float halfHeight = sh * 0.5f;
    // The filter control occupies the top of the screen; the carousel starts
    // below it, as lazer's does.
    const float carTop = kFilterHeight + 8.0f;

    float total = 0.0f;
    float selCentre = 0.0f;
    for (const int si : fVisible) {
      if (si == fSelSet) {
        selCentre = total + setH +
                    (static_cast<float>(fSelDiff) + 0.5f) * (diffH + gap);
      }
      total += setH + gap;
      if (si == fSelSet) {
        total += static_cast<float>(this->infosFor(si).size()) * (diffH + gap);
      }
    }

    const int selKey = fSelSet * 1024 + fSelDiff;
    if (selKey != fPrevSelKey) {
      fPrevSelKey = selKey;
      fPopAnim = 0.0f;
      fCarouselScroll = selCentre - halfHeight;
      fUserScrolled = false;
    }
    fCarouselScroll = std::clamp(fCarouselScroll, -halfHeight * 0.5f,
                                 std::max(0.0f, total - halfHeight * 0.5f));
    fScrollAnim = this->approach(fScrollAnim, fCarouselScroll, 120.0f);
    fPopAnim = std::min(1.0f, fPopAnim + static_cast<float>(fUiDt) / 400.0f);
    const float pop = easeOutQuint(fPopAnim);

    canvas->save();
    canvas->clipIRect(skia::SkIRect::MakeXYWH(
        0, static_cast<int>(carTop), fScreenW,
        std::max(0, fScreenH - static_cast<int>(carTop) - 62)));
    float y = carTop - fScrollAnim;
    for (const int si : fVisible) {
      const auto &infos = this->infosFor(si);
      const bool expanded = si == fSelSet;

      if (y + setH >= carTop - setH && y <= sh) {
        const float dist = std::abs(1.0f - (y + setH * 0.5f) / halfHeight);
        float x = carLeft + carouselOffsetX(dist, halfHeight) + corner;
        if (!expanded) {
          x += activeX * 4.0f;
        }
        x += activeX * (1.0f - (expanded ? pop : 0.0f));

        const skia::SkRect rect = skia::SkRect::MakeXYWH(x, y, panelW, setH);
        this->drawSetPanel(canvas, rect, si, infos, expanded, corner);
        fCarouselHits.push_back({rect, si, -1});
      }
      y += setH + gap;

      if (!expanded) {
        continue;
      }
      for (int di = 0; di < static_cast<int>(infos.size()); ++di) {
        const bool selected = di == fSelDiff;
        if (y + diffH >= carTop - diffH && y <= sh) {
          const float dist = std::abs(1.0f - (y + diffH * 0.5f) / halfHeight);
          float x = carLeft + carouselOffsetX(dist, halfHeight) + corner;
          if (!selected) {
            x += activeX * 2.0f;
          }
          x += activeX * (1.0f - (selected ? pop : 0.0f));

          const skia::SkRect rect = skia::SkRect::MakeXYWH(x, y, panelW, diffH);
          this->drawDiffPanel(canvas, rect,
                              infos[static_cast<std::size_t>(di)], selected,
                              corner);
          fCarouselHits.push_back({rect, si, di});
        }
        y += diffH + gap;
      }
    }
    canvas->restore();

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
    fFilter.draw(canvas, fFont, fScreenW, fMouseX, fMouseY, fVisible.size());
  }

  bool filterClick(float x, float y, bool pressed) {
    if (!pressed) {
      fFilter.endDrag();
      return false;
    }
    const bool used = fFilter.click(x, y);
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
                    bool expanded, float corner) {
    const bool hover = rect.contains(fMouseX, fMouseY);
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
          const auto set = loadBeatmapSet(path);
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
                     const osu::BeatmapInfo &info, bool selected,
                     float corner) {
    const bool hover = rect.contains(fMouseX, fMouseY);
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

  // lazer's song select footer: a row of pill buttons along the bottom.
  void drawSelectFooter(skia::SkCanvas *canvas) {
    const float sw = static_cast<float>(fScreenW);
    const float sh = static_cast<float>(fScreenH);
    skia::SkPaint bar;
    bar.setColor(kPanelBg);
    canvas->drawRect(skia::SkRect::MakeXYWH(0.0f, sh - 62.0f, sw, 62.0f), bar);

    struct FooterBtn {
      const char *fLabel;
      skia::SkColor fColor;
      skia::SkRect *fHit;
    };
    const FooterBtn btns[] = {
        {"settings", skia::colorSetARGB(255, 140, 140, 155), &fSettingsChip},
        {"random", skia::colorSetARGB(255, 102, 204, 255), &fRandomChip},
        {"import", kAccent, &fImportChip},
        {"browse", skia::colorSetARGB(255, 165, 204, 0), &fDownloadsChip},
    };
    float x = 24.0f;
    for (const auto &b : btns) {
      const skia::SkRect r = skia::SkRect::MakeXYWH(x, sh - 50.0f, 148.0f, 38.0f);
      *b.fHit = r;
      const bool hover = r.contains(fMouseX, fMouseY);
      this->fillRounded(canvas, r, 19.0f, hover ? kCardSel : kCardBg);
      this->strokeRounded(canvas, r, 19.0f, b.fColor, hover ? 2.0f : 1.0f);
      this->drawTextCentered(canvas, b.fLabel, r.centerX(), r.centerY() + 5.0f,
                             14.0f, hover ? b.fColor : skia::kWhite);
      x += 158.0f;
    }
    this->drawTextCentered(
        canvas,
        "Enter play   F1 browse   F2 random   F5 import   Ctrl+O settings",
        sw * 0.72f, sh - 24.0f, 14.0f, skia::kWhite, 0.7f);
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

  // ---- Download screen --------------------------------------------------

  void frameDownload() {
    auto *canvas = fSurface->getCanvas();
    this->drawScreenBackground(canvas);

    const float sw = static_cast<float>(fScreenW);
    const float sh = static_cast<float>(fScreenH);

    this->drawTextClipped(canvas, "beatmap listing", 24.0f, 44.0f, sw - 48.0f,
                          26.0f, skia::kWhite);

    // Search box.
    const skia::SkRect box =
        skia::SkRect::MakeXYWH(24.0f, 62.0f, sw - 48.0f, 44.0f);
    this->fillRounded(canvas, box, 10.0f, kCardBg);
    this->strokeRounded(canvas, box, 10.0f, kAccent, 2.0f);
    const bool caretOn =
        std::fmod(wallMs(), 1000.0) < 600.0;
    if (fSearchQuery.empty()) {
      this->drawTextClipped(canvas,
                            std::string(caretOn ? "_" : " ") +
                                "  type to search, Enter to submit",
                            40.0f, 90.0f, sw - 80.0f, 18.0f, skia::kWhite,
                            0.45f);
    } else {
      this->drawTextClipped(canvas, fSearchQuery + (caretOn ? "_" : " "),
                            40.0f, 90.0f, sw - 80.0f, 18.0f, skia::kWhite);
    }

    this->drawTextClipped(canvas, fDownloadStatus, 24.0f, 130.0f, sw - 48.0f,
                          14.0f, kAccent2, 0.9f);

    // Result cards.
    fFoundHits.clear();
    fFoundHits.resize(fFound.size(), skia::SkRect::MakeEmpty());
    const float listTop = 148.0f;
    const float cardH = 76.0f;
    const float gap = 10.0f;
    const float viewH = sh - listTop - 52.0f;
    const float totalH =
        static_cast<float>(fFound.size()) * (cardH + gap);
    fDownloadScroll =
        std::clamp(fDownloadScroll, 0.0f, std::max(0.0f, totalH - viewH));

    canvas->save();
    canvas->clipIRect(skia::SkIRect::MakeXYWH(
        0, static_cast<int>(listTop) - 4, static_cast<int>(sw),
        static_cast<int>(viewH) + 8));
    float y = listTop - fDownloadScroll;
    for (std::size_t i = 0; i < fFound.size(); ++i) {
      const auto &d = fFound[i];
      const skia::SkRect card =
          skia::SkRect::MakeXYWH(24.0f, y, sw - 48.0f, cardH);
      if (y + cardH >= listTop && y <= sh) {
        const bool hover = card.contains(fMouseX, fMouseY);
        this->fillRounded(canvas, card, 10.0f, hover ? kCardSel : kCardBg);

        // Cover art as the card background, lazer-style.
        this->requestThumb(i);
        if (d.fThumbSt == DownloadEntry::Thumb::kReady && d.fThumb) {
          canvas->save();
          canvas->clipRRect(skia::SkRRect::MakeRectXY(card, 10.0f, 10.0f),
                            true);
          canvas->drawImageRect(
              d.fThumb.get(), card,
              skia::SkSamplingOptions(skia::SkFilterMode::kLinear), nullptr);
          skia::SkPaint dark;
          dark.setColor(
              skia::colorSetARGB(hover ? 120 : 150, 12, 10, 18));
          canvas->drawRect(card, dark);
          canvas->restore();
        }

        this->drawTextClipped(canvas,
                              std::format("{} - {}", d.fArtist, d.fTitle),
                              40.0f, y + 26.0f, sw - 320.0f, 17.0f,
                              skia::kWhite);
        this->drawTextClipped(canvas,
                              std::format("mapped by {}", d.fCreator), 40.0f,
                              y + 47.0f, sw - 320.0f, 13.0f, skia::kWhite,
                              0.75f);
        if (d.fDiffCount > 0) {
          this->drawTextClipped(
              canvas,
              d.fDiffCount == 1
                  ? std::format("{:.1f}* - 1 difficulty", d.fStarsMin)
                  : std::format("{:.1f}*..{:.1f}* - {} difficulties",
                                d.fStarsMin, d.fStarsMax, d.fDiffCount),
              40.0f, y + 66.0f, sw - 320.0f, 12.0f,
              starColor(d.fStarsMax), 0.95f);
        }
        if (!d.fRankStatus.empty()) {
          const float pillW = 86.0f;
          const skia::SkRect pill = skia::SkRect::MakeXYWH(
              sw - 44.0f - pillW, y + 10.0f, pillW, 20.0f);
          this->fillRounded(canvas, pill, 10.0f,
                            statusColorFor(d.fRankStatus));
          this->drawTextCentered(canvas, d.fRankStatus, pill.centerX(),
                                 pill.centerY() + 4.0f, 11.0f,
                                 skia::colorSetARGB(255, 20, 16, 26));
        }
        std::string status;
        skia::SkColor statusColor = kAccent2;
        switch (d.fSt) {
        case DownloadEntry::St::kIdle:
          status = "download";
          break;
        case DownloadEntry::St::kFetching: {
          const float p =
              d.fHandle ? d.fHandle->fProgress.load(std::memory_order_relaxed)
                        : 0.0f;
          status = std::format("{:.0f}%", static_cast<double>(p) * 100.0);
          break;
        }
        case DownloadEntry::St::kDone:
          status = "in library";
          statusColor = skia::colorSetARGB(255, 120, 220, 120);
          break;
        case DownloadEntry::St::kError:
          status = "failed - retry?";
          statusColor = skia::colorSetARGB(255, 255, 120, 120);
          break;
        }
        fFont.setSize(15.0f);
        const float statusW = fFont.measureText(status.c_str(), status.size(),
                                                skia::SkTextEncoding::kUTF8);
        skia::SkPaint sp;
        sp.setAntiAlias(true);
        sp.setColor(statusColor);
        canvas->drawString(status.c_str(), sw - 44.0f - statusW, y + 52.0f,
                           fFont, sp);
        fFoundHits[i] = card;
      }
      y += cardH + gap;
    }
    canvas->restore();

    this->drawBottomBar(canvas,
                        "Type to search, Enter to submit    Click a card to "
                        "download    Esc back");
    this->drawScreenFadeIn(canvas);
    this->present();
  }

  // ---- Pause ------------------------------------------------------------

  void framePaused() {
    fFirstFrame = true; // static scene: always repaint fully
    this->render(fPausedNow);
    auto *canvas = fSurface->getCanvas();

    const float sw = static_cast<float>(fScreenW);
    const float sh = static_cast<float>(fScreenH);
    skia::SkPaint dim;
    dim.setColor(skia::colorSetARGB(170, 8, 6, 12));
    canvas->drawRect(skia::SkRect::MakeXYWH(0, 0, sw, sh), dim);

    this->drawTextCentered(canvas, "paused", sw * 0.5f, sh * 0.26f, 42.0f,
                           skia::kWhite);

    fMenuButtons.clear();
    const char *labels[] = {"continue", "retry", "quit"};
    const skia::SkColor accents[] = {
        skia::colorSetARGB(255, 120, 220, 120),
        skia::colorSetARGB(255, 255, 204, 102),
        skia::colorSetARGB(255, 255, 110, 110)};
    const float bw = std::min(420.0f, sw * 0.5f);
    const float bh = 60.0f;
    const float fade = this->screenFade();
    float y = sh * 0.38f;
    for (int i = 0; i < 3; ++i) {
      // Buttons slide in from alternating sides, lazer-style.
      const float side = (i % 2 == 0) ? -1.0f : 1.0f;
      const float slide = (1.0f - fade) * 90.0f * side;
      const skia::SkRect r =
          skia::SkRect::MakeXYWH((sw - bw) * 0.5f + slide, y, bw, bh);
      fMenuButtons.push_back({r, labels[i], accents[i]});
      this->drawMenuButton(canvas, fMenuButtons.back());
      y += bh + 16.0f;
    }
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

  void frameResults() {
    fFirstFrame = true;
    auto *canvas = fSurface->getCanvas();
    this->drawScreenBackground(canvas);

    const float sw = static_cast<float>(fScreenW);
    const float sh = static_cast<float>(fScreenH);
    const auto &sc = fResult.fScore;

    this->drawTextCentered(canvas, "results", sw * 0.5f, 52.0f, 30.0f,
                           skia::kWhite, 0.9f);

    // Grade badge.
    const float gx = sw * 0.72f;
    const float gy = sh * 0.40f;
    skia::SkPaint gp;
    gp.setAntiAlias(true);
    gp.setColor(kAccent);
    gp.setAlphaf(0.18f);
    canvas->drawCircle(gx, gy, 110.0f, gp);
    this->drawTextCentered(canvas, fResult.fGrade, gx, gy + 30.0f, 96.0f,
                           kAccent);

    // Score panel.
    const float px = sw * 0.08f;
    const float pw = sw * 0.5f;
    this->fillRounded(canvas,
                      skia::SkRect::MakeXYWH(px, sh * 0.18f, pw, sh * 0.52f),
                      14.0f, kPanelBg);
    float ty = sh * 0.18f + 58.0f;
    // Score counts up over the first second, classic osu results feel.
    const float countUp = easeOutQuint(
        static_cast<float>((wallMs() - fStateEnterWall) / 900.0));
    const auto shownScore =
        static_cast<std::uint64_t>(static_cast<double>(sc.fScore) * countUp);
    this->drawTextClipped(canvas, std::format("{:010}", shownScore),
                          px + 30.0f, ty, pw - 60.0f, 40.0f, skia::kWhite);
    ty += 54.0f;
    this->drawTextClipped(
        canvas,
        std::format("accuracy {:.2f}%   combo {}x", sc.accuracy() * 100.0,
                    sc.fMaxCombo),
        px + 30.0f, ty, pw - 60.0f, 20.0f, kAccent2);
    ty += 42.0f;
    struct Row {
      const char *fLabel;
      int fCount;
      skia::SkColor fColor;
    };
    const Row rows[] = {
        {"300", sc.fGreat, client::ui::kGreat},
        {"100", sc.fGood, client::ui::kGood},
        {"50", sc.fMeh, client::ui::kMeh},
        {"miss", sc.fMiss, client::ui::kMiss},
    };
    for (const auto &row : rows) {
      this->drawTextClipped(canvas, row.fLabel, px + 30.0f, ty, 120.0f, 18.0f,
                            row.fColor);
      this->drawTextClipped(canvas, std::format("{}", row.fCount), px + 150.0f,
                            ty, 120.0f, 18.0f, skia::kWhite);
      ty += 30.0f;
    }
    ty += 12.0f;
    this->drawTextClipped(
        canvas,
        std::format("hit error {:+.1f} ms   UR {:.0f}", fResult.fMean,
                    fResult.fUr),
        px + 30.0f, ty, pw - 60.0f, 16.0f, skia::kWhite, 0.8f);

    // Buttons.
    fMenuButtons.clear();
    const float bw = std::min(360.0f, sw * 0.4f);
    const float bh = 56.0f;
    const skia::SkRect r1 =
        skia::SkRect::MakeXYWH(px, sh * 0.76f, bw, bh);
    const skia::SkRect r2 =
        skia::SkRect::MakeXYWH(px + bw + 20.0f, sh * 0.76f, bw, bh);
    fMenuButtons.push_back(
        {r1, "retry", skia::colorSetARGB(255, 255, 204, 102)});
    fMenuButtons.push_back({r2, "back to song select", kAccent2});
    const skia::SkRect r3 =
        skia::SkRect::MakeXYWH(px, sh * 0.76f + bh + 12.0f, bw, bh);
    fMenuButtons.push_back(
        {r3, "export video", skia::colorSetARGB(255, 102, 204, 255)});
    for (const auto &b : fMenuButtons) {
      this->drawMenuButton(canvas, b);
    }

    this->drawBottomBar(canvas, "Enter retry    Esc back to song select");
    this->drawScreenFadeIn(canvas);
    this->present();
  }

  bool initSkia() {
    auto interface = skia::GrGLMakeNativeInterface();
    if (!interface) {
      return false;
    }
    fContext = skia::MakeGL(std::move(interface));
    return static_cast<bool>(fContext);
  }

  [[nodiscard]] skia::SkFont loadFont(float size) {
    const std::vector<std::filesystem::path> files{
        "/usr/share/fonts/noto/NotoSans-Regular.ttf",
        "/usr/share/fonts/ttf-dejavu/DejaVuSans.ttf", // Alpine
        "/usr/share/fonts/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/TTF/DejaVuSans.ttf",
        "/usr/share/fonts/liberation/LiberationSans-Regular.ttf",
    };

    const std::vector<std::filesystem::path> dirs{
        "/usr/share/fonts/noto",
        "/usr/share/fonts/ttf-dejavu", // Alpine's dejavu package path
        "/usr/share/fonts/dejavu",
        "/usr/share/fonts/truetype/dejavu",
        "/usr/share/fonts/TTF",
        "/usr/share/fonts/liberation",
        "/usr/share/fonts/ttf-liberation",
        "/usr/share/fonts",
    };

    for (const auto &dir : dirs) {
      if (!std::filesystem::is_directory(dir))
        continue;
      auto mgr = skia::SkFontMgr_New_Custom_Directory(dir.c_str());
      if (!mgr)
        continue;
      auto face = mgr->matchFamilyStyle("Noto Sans", skia::SkFontStyle());
      if (!face)
        face = mgr->matchFamilyStyle("DejaVu Sans", skia::SkFontStyle());
      if (!face)
        face = mgr->matchFamilyStyle(nullptr, skia::SkFontStyle());
      if (face) {
        skia::SkString family;
        face->getFamilyName(&family);
        std::println(std::cerr, "[ui] font: \"{}\" from {}", family.c_str(),
                     dir.string());
        return skia::SkFont(std::move(face), size);
      }
    }

    auto rootMgr = skia::SkFontMgr_New_Custom_Directory("/usr/share/fonts");
    if (rootMgr) {
      for (const auto &path : files) {
        if (!std::filesystem::exists(path))
          continue;
        auto stream = skia::SkStream::MakeFromFile(path.c_str());
        if (!stream)
          continue;
        auto face = rootMgr->makeFromStream(std::move(stream));
        if (face)
          return skia::SkFont(std::move(face), size);
      }
    }

    for (const auto &path : files) {
      if (!std::filesystem::exists(path))
        continue;
      auto data = skia::SkData::MakeFromFileName(path.c_str());
      if (!data || data->isEmpty())
        continue;
      std::vector<skia::Sp<skia::SkData>> fonts;
      fonts.push_back(std::move(data));
      auto dataMgr = skia::SkFontMgr_New_Custom_Data(fonts);
      if (!dataMgr)
        continue;
      auto face = dataMgr->matchFamilyStyle(nullptr, skia::SkFontStyle());
      if (face)
        return skia::SkFont(std::move(face), size);
      const int families = dataMgr->countFamilies();
      for (int i = 0; i < families; ++i) {
        auto set = dataMgr->createStyleSet(i);
        if (!set)
          continue;
        for (int j = 0; j < set->count(); ++j) {
          face = set->createTypeface(j);
          if (face)
            return skia::SkFont(std::move(face), size);
        }
      }
    }

    std::println(std::cerr,
                 "[ui] font: NONE FOUND - text will not render. Install "
                 "font-noto or ttf-dejavu (apk add font-noto ttf-dejavu).");
    return skia::SkFont(nullptr, size);
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
    fSurface = skia::WrapBackendRenderTarget(
        fContext.get(), target, skia::kBottomLeft_GrSurfaceOrigin,
        skia::kRGBA_8888_SkColorType, nullptr, nullptr);
    fFirstFrame = true;
    this->preScaleBackground();
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
    while (fAutoplayIndex < fAutoplayEvents.size() &&
           fAutoplayEvents[fAutoplayIndex].fTime <= now) {
      const auto &ev = fAutoplayEvents[fAutoplayIndex];
      fEngine->submit(ev);
      if (fRecord)
        fRecordedEvents.push_back(ev);
      if (ev.fAction == osu::InputAction::kMove) {
        fCursor = ev.fPos;
        fCursorTrail.push_back({fCursor, ev.fTime});
      }
      ++fAutoplayIndex;
    }
  }

  void playHitsounds(double now) {
    const auto &events = fEngine->events();
    while (fPlayedEvents < events.size()) {
      const auto &ev = events[fPlayedEvents++];
      const auto pos = this->objectPosition(ev.fIndex);
      fPopups.push_back({ev.fResult, now, pos});
      fFadingObjects.push_back({ev.fIndex, now, ev.fResult});
      if (std::holds_alternative<osu::judgement::Miss>(ev.fResult)) {
        if (fCombo > 20) {
          this->playSample("combobreak");
        }
        fCombo = 0;
        continue;
      }
      ++fCombo;
      const double hitTime = ev.fIndex < fMap->fObjects.size()
                                 ? osu::startTime(fMap->fObjects[ev.fIndex])
                                 : now;
      this->playObjectHitsound(hitTime, ev.fIndex);
      if (ev.fIndex < fComboInfo.fIndices.size()) {
        fHitBursts.push_back({pos, now, fComboInfo.fIndices[ev.fIndex]});
      }
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
          player.setVolume(fSettings.value("master") * fSettings.value("effect"));
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
      player.setVolume(fSettings.value("master") * fSettings.value("effect"));
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

  void drawFollowPoints(skia::SkCanvas *canvas, double now, double ar,
                        double cs) {
    if (fMap->fObjects.size() < 2)
      return;
    const double preempt = osu::preemptTime(ar);
    const double fadeIn = osu::fadeInTime(ar);
    const double radius = osu::circleRadius(cs);
    const double spacing = radius * 0.7;
    for (std::size_t i = 0; i + 1 < fMap->fObjects.size(); ++i) {
      if (fComboInfo.fGroups[i] != fComboInfo.fGroups[i + 1])
        continue;
      if (std::holds_alternative<osu::Spinner>(fMap->fObjects[i]) ||
          std::holds_alternative<osu::Spinner>(fMap->fObjects[i + 1])) {
        continue;
      }
      const auto [startPos, startTime] = this->objectEnd(i);
      const osu::Vec2 endPos = this->objectPosition(i + 1);
      const double endTime = osu::startTime(fMap->fObjects[i + 1]);
      const osu::Vec2 dir = endPos - startPos;
      const double distance = dir.length();
      if (distance < spacing * 3.0)
        continue;
      const double angle = std::atan2(dir.fY, dir.fX);
      const double dt = endTime - startTime;
      for (double d = spacing * 2.0; d < distance - 1.5 * spacing;
           d += spacing) {
        const double fraction = d / distance;
        const double pointTime = startTime + dt * fraction;
        const double fadeInTime = pointTime - preempt;
        const double fadeOutTime = pointTime;
        double rawAlpha = 0.0;
        if (now >= fadeInTime && now < fadeOutTime)
          rawAlpha = (now - fadeInTime) / fadeIn;
        else if (now >= fadeOutTime)
          rawAlpha = 1.0 - (now - fadeOutTime) / fadeIn;
        rawAlpha = std::clamp(rawAlpha, 0.0, 1.0);
        const double relpos = rawAlpha * (2.0 - rawAlpha);
        const double drawFraction = fraction - 0.1 * (1.0 - relpos);
        const osu::Vec2 p = startPos + dir * drawFraction;
        const float alpha = static_cast<float>(rawAlpha * 0.5);
        if (alpha > 0.0f) {
          fSkin.drawFollowPoint(canvas, p, angle, alpha, cs);
        }
      }
    }
  }

  void drawHitBursts(skia::SkCanvas *canvas, double now, double cs) {
    auto it = fHitBursts.begin();
    while (it != fHitBursts.end()) {
      const double age = now - it->fTime;
      if (age > kHitBurstLifetime) {
        it = fHitBursts.erase(it);
        continue;
      }
      fSkin.drawHitBurst(canvas, it->fPos, cs, age, it->fComboIndex);
      ++it;
    }
  }

  void render(double now) {
    using clock = std::chrono::steady_clock;
    auto rt0 = clock::now();

    if (fLastFrameTime > 0.0 && fLastFrameTime < now) {
      const double ft = now - fLastFrameTime;
      fFrameTimes[fFrameTimeIdx] = ft;
      fFrameTimeIdx = (fFrameTimeIdx + 1) % kFpsSampleCount;
      if (fFrameTimeCount < kFpsSampleCount)
        ++fFrameTimeCount;
    }
    fLastFrameTime = now;

    const double ar = fEngine->clockRate() > 0.0
                          ? fMap->fDiff.fAr * fEngine->clockRate()
                          : fMap->fDiff.fAr;
    const double cs = fMap->fDiff.fCs;
    const double od = fMap->fDiff.fOd;

    this->updateCursorTrail(now);

    auto dirty = this->computeDirtyBounds(now, ar, cs, od);
    if (!fFirstFrame)
      dirty.join(fDirtyBounds);
    else
      dirty = skia::SkIRect::MakeXYWH(0, 0, fScreenW, fScreenH);

    if (dirty.isEmpty())
      return;

    fDirtyBounds = dirty;
    fFirstFrame = false;

    auto *canvas = fSurface->getCanvas();

    canvas->save();
    canvas->clipIRect(dirty);
    if (fBackgroundScaled) {
      this->drawBackground(canvas);
    } else {
      canvas->clear(skia::kBlack);
    }

    canvas->save();
    canvas->translate(fOffsetX, fOffsetY);
    canvas->scale(fScale, fScale);

    this->drawPlayfield(canvas);

    auto rta = clock::now();
    this->drawFollowPoints(canvas, now, ar, cs);
    auto rtb = clock::now();

    for (std::size_t i = 0; i < fMap->fObjects.size(); ++i) {
      this->drawObject(canvas, fMap->fObjects[i], i, now, ar, cs, od);
    }
    auto rtc = clock::now();

    this->drawFadingObjects(canvas, now, ar, cs, od);
    this->drawHitBursts(canvas, now, cs);
    this->drawPopups(canvas, now, cs);
    this->drawCursorTrail(canvas, now);
    this->drawCursor(canvas);
    canvas->restore();

    auto rtd = clock::now();
    this->drawHud(canvas, now);
    auto rte = clock::now();

    canvas->restore();

    if (fShowProfile || fSettings.flag("fps")) {
      auto &p = fProfile[fProfileIdx];
      p.renderFollowUs = static_cast<double>(
          std::chrono::duration_cast<std::chrono::microseconds>(rtb - rta)
              .count());
      p.renderObjectsUs = static_cast<double>(
          std::chrono::duration_cast<std::chrono::microseconds>(rtc - rtb)
              .count());
      p.renderRestUs = static_cast<double>(
          std::chrono::duration_cast<std::chrono::microseconds>(rtd - rtc)
              .count());
      p.renderHudUs = static_cast<double>(
          std::chrono::duration_cast<std::chrono::microseconds>(rte - rtd)
              .count());
    }
  }

  [[nodiscard]] skia::SkIRect computeDirtyBounds(double now, double ar,
                                                 double cs, double od) const {
    skia::SkIRect dirty = skia::SkIRect::MakeEmpty();
    const float r = static_cast<float>(osu::circleRadius(cs)) * 1.05f;

    auto addPlayfieldPt = [&](float px, float py, float radius) {
      float sx = px * fScale + fOffsetX;
      float sy = py * fScale + fOffsetY;
      float sr = radius * fScale + 2.0f;
      dirty.join(skia::SkIRect::MakeLTRB(
          std::max(0, static_cast<int>(sx - sr)),
          std::max(0, static_cast<int>(sy - sr)),
          std::min(fScreenW, static_cast<int>(sx + sr + 1.0f)),
          std::min(fScreenH, static_cast<int>(sy + sr + 1.0f))));
    };

    for (std::size_t i = 0; i < fMap->fObjects.size(); ++i) {
      const auto &obj = fMap->fObjects[i];
      const double time = osu::startTime(obj);
      const double preempt = osu::preemptTime(ar);
      if (now < time - preempt)
        continue;
      if (fEngine->isJudged(i))
        continue;

      std::visit(osu::Overloaded{
                     [&](const osu::Circle &o) {
                       addPlayfieldPt(static_cast<float>(o.fPos.fX),
                                      static_cast<float>(o.fPos.fY), r * 5.0f);
                     },
                     [&](const osu::Slider &o) {
                       const auto &path = fMap->fSliderPaths[i];
                       for (const auto &pt : path.points()) {
                         addPlayfieldPt(static_cast<float>(pt.fX),
                                        static_cast<float>(pt.fY), r);
                       }
                       addPlayfieldPt(static_cast<float>(o.fPos.fX),
                                      static_cast<float>(o.fPos.fY), r * 5.0f);
                     },
                     [&](const osu::Spinner &) {
                       addPlayfieldPt(
                           static_cast<float>(osu::kPlayfieldCenter.fX),
                           static_cast<float>(osu::kPlayfieldCenter.fY), 90.0f);
                     },
                 },
                 obj);
    }

    for (const auto &fo : fFadingObjects) {
      const auto &obj = fMap->fObjects[fo.fIndex];
      std::visit(osu::Overloaded{
                     [&](const osu::Circle &o) {
                       addPlayfieldPt(static_cast<float>(o.fPos.fX),
                                      static_cast<float>(o.fPos.fY), r * 4.0f);
                     },
                     [&](const osu::Slider &o) {
                       const auto &path = fMap->fSliderPaths[fo.fIndex];
                       for (const auto &pt : path.points()) {
                         addPlayfieldPt(static_cast<float>(pt.fX),
                                        static_cast<float>(pt.fY), r);
                       }
                       addPlayfieldPt(static_cast<float>(o.fPos.fX),
                                      static_cast<float>(o.fPos.fY), r * 4.0f);
                     },
                     [&](const osu::Spinner &) {},
                 },
                 obj);
    }

    for (const auto &hb : fHitBursts)
      addPlayfieldPt(static_cast<float>(hb.fPos.fX),
                     static_cast<float>(hb.fPos.fY), r * 2.5f);

    for (const auto &pp : fPopups)
      addPlayfieldPt(static_cast<float>(pp.fPos.fX),
                     static_cast<float>(pp.fPos.fY), 60.0f);

    for (const auto &pt : fCursorTrail)
      addPlayfieldPt(static_cast<float>(pt.fPos.fX),
                     static_cast<float>(pt.fPos.fY), 24.0f);

    addPlayfieldPt(static_cast<float>(fCursor.fX),
                   static_cast<float>(fCursor.fY), 24.0f);

    for (std::size_t i = 0; i + 1 < fMap->fObjects.size(); ++i) {
      const double endTime = osu::startTime(fMap->fObjects[i + 1]);
      const double preempt = osu::preemptTime(ar);
      if (now < endTime - preempt)
        continue;
      if (now > endTime)
        continue;
      const auto [startPos, startTime] = this->objectEnd(i);
      const osu::Vec2 endPos = this->objectPosition(i + 1);
      addPlayfieldPt(static_cast<float>(startPos.fX),
                     static_cast<float>(startPos.fY), 10.0f);
      addPlayfieldPt(static_cast<float>(endPos.fX),
                     static_cast<float>(endPos.fY), 10.0f);
    }

    dirty.join(skia::SkIRect::MakeXYWH(0, 0, fScreenW, 160));

    return dirty;
  }
  void drawBackground(skia::SkCanvas *canvas) {
    if (!fBackgroundScaled)
      return;
    skia::SkPaint paint;
    paint.setAntiAlias(false);
    paint.setBlendMode(skia::SkBlendMode::kSrc);
    canvas->drawImageRect(
        fBackgroundScaled.get(),
        skia::SkRect::MakeXYWH(0, 0, static_cast<float>(fScreenW),
                               static_cast<float>(fScreenH)),
        skia::SkSamplingOptions(skia::SkFilterMode::kNearest), &paint);
  }

  void preScaleBackground() {
    fBackgroundScaled.reset();
    if (!fBackground)
      return;
    const int sw = fScreenW;
    const int sh = fScreenH;
    if (sw <= 0 || sh <= 0)
      return;
    const float iw = static_cast<float>(fBackground->width());
    const float ih = static_cast<float>(fBackground->height());
    if (iw <= 0.0f || ih <= 0.0f)
      return;

    const float scale =
        std::max(static_cast<float>(sw) / iw, static_cast<float>(sh) / ih);
    const float dw = iw * scale;
    const float dh = ih * scale;
    const float dx = (static_cast<float>(sw) - dw) * 0.5f;
    const float dy = (static_cast<float>(sh) - dh) * 0.5f;

    skia::SkBitmap bmp;
    if (!bmp.tryAllocPixels(skia::SkImageInfo::Make(
            sw, sh, skia::kRGBA_8888_SkColorType, skia::kPremul_SkAlphaType)))
      return;
    bmp.eraseColor(skia::kBlack);
    skia::SkCanvas offscreen(bmp);
    skia::SkPaint paint;
    paint.setAntiAlias(false);
    paint.setAlphaf(1.0f - fSettings.value("dim"));
    offscreen.drawImageRect(
        fBackground.get(), skia::SkRect::MakeXYWH(dx, dy, dw, dh),
        skia::SkSamplingOptions(skia::SkFilterMode::kLinear), &paint);
    fBackgroundScaled = skia::RasterFromBitmap(bmp);
  }

  void drawPlayfield(skia::SkCanvas *) {
    // No visible playfield border.
  }

  void drawObject(skia::SkCanvas *canvas, const osu::HitObject &obj,
                  std::size_t index, double now, double ar, double cs,
                  double od) {
    const double time = osu::startTime(obj);
    const double preempt = osu::preemptTime(ar);
    if (now < time - preempt) {
      return;
    }
    if (fEngine->isJudged(index)) {
      return;
    }

    std::visit(osu::Overloaded{
                   [&](const osu::Circle &o) {
                     fSkin.drawHitCircle(canvas, o.fPos, time, now, cs, ar,
                                         o.fCombo, fComboInfo.fIndices[index]);
                   },
                   [&](const osu::Slider &o) {
                     fSkin.drawSlider(canvas, o, index,
                                      fMap->fSliderPaths[index],
                                      fMap->sliderSpanDuration(o),
                                      fMap->sliderTickDistance(o), now, cs, ar,
                                      od, o.fCombo, fComboInfo.fIndices[index],
                                      1.0f, fEngine->isTracking(index));
                   },
                   [&](const osu::Spinner &o) {
                     this->drawSpinner(canvas, o, index, now, cs, od);
                   },
               },
               obj);
  }

  void drawFadingObjects(skia::SkCanvas *canvas, double now, double ar,
                         double cs, double od) {
    auto it = fFadingObjects.begin();
    while (it != fFadingObjects.end()) {
      const double age = now - it->fTime;
      if (age > kFadeLifetime) {
        it = fFadingObjects.erase(it);
        continue;
      }
      const float alpha = static_cast<float>(1.0 - age / kFadeLifetime);
      std::visit(
          osu::Overloaded{
              [&](const osu::Circle &o) {
                fSkin.drawHitCircle(canvas, o.fPos, o.fTime, now, cs, ar,
                                    o.fCombo, fComboInfo.fIndices[it->fIndex],
                                    alpha);
              },
              [&](const osu::Slider &o) {
                fSkin.drawSlider(
                    canvas, o, it->fIndex, fMap->fSliderPaths[it->fIndex],
                    fMap->sliderSpanDuration(o), fMap->sliderTickDistance(o),
                    now, cs, ar, od, o.fCombo, fComboInfo.fIndices[it->fIndex],
                    alpha, false);
              },
              [&](const osu::Spinner &) {},
          },
          fMap->fObjects[it->fIndex]);
      ++it;
    }
  }

  void drawSpinner(skia::SkCanvas *canvas, const osu::Spinner &s,
                   std::size_t index, double now, double cs, double od) {
    const float cx = static_cast<float>(osu::kPlayfieldCenter.fX);
    const float cy = static_cast<float>(osu::kPlayfieldCenter.fY);
    const float radius = 80.0f;

    const double progress =
        now < s.fTime
            ? 0.0
            : std::clamp(osu::spinnerProgress(fEngine->spinnerRotations(index),
                                              s.fEnd - s.fTime, od),
                         0.0, 1.0);
    fSkin.drawSpinner(canvas, cx, cy, radius, progress);

    skia::SkPaint textPaint;
    textPaint.setColor(skia::kWhite);
    textPaint.setStyle(skia::kFillStyle);
    textPaint.setAntiAlias(true);
    fFont.setSize(20.0f / fScale);
    const std::string label =
        std::format("{}/{}", std::max(0, fEngine->spinnerRotations(index)),
                    static_cast<int>(std::ceil(
                        osu::spinnerRequiredRotations(s.fEnd - s.fTime, od))));
    canvas->drawString(label.c_str(), cx, cy + 6.0f / fScale, fFont, textPaint);
  }

  void updateCursorTrail(double now) {
    if (!fCursorTrail.empty() && fCursorTrail.back().fPos.fX == fCursor.fX &&
        fCursorTrail.back().fPos.fY == fCursor.fY) {
      return;
    }
    fCursorTrail.push_back({fCursor, now});
    while (!fCursorTrail.empty() &&
           now - fCursorTrail.front().fTime > kCursorTrailLifetime) {
      fCursorTrail.pop_front();
    }
    if (fCursorTrail.size() > kCursorTrailMax) {
      fCursorTrail.pop_front();
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
  void drawCursorTrail(skia::SkCanvas *canvas, double now) {
    const float scale = 1.0f / fScale;
    const bool hasImg = static_cast<bool>(fSkin.cursorTrail());

    struct TrailPt {
      float fX, fY;
      float fAlpha;
    };
    std::vector<TrailPt> raw;
    raw.reserve(fCursorTrail.size());
    for (const auto &p : fCursorTrail) {
      const double age = now - p.fTime;
      if (age > kCursorTrailLifetime)
        continue;
      const float alpha =
          static_cast<float>((1.0 - age / kCursorTrailLifetime) * 0.6);
      const float x = static_cast<float>(p.fPos.fX);
      const float y = static_cast<float>(p.fPos.fY);
      if (!raw.empty()) {
        const float dx = x - raw.back().fX;
        const float dy = y - raw.back().fY;
        if (dx * dx + dy * dy < 1e-6f)
          continue; // duplicate point => degenerate tangent
      }
      raw.push_back({x, y, alpha});
    }
    if (raw.size() < 2)
      return;

    // One round of Chaikin corner cutting rounds off sharp turns that would
    // otherwise fold the ribbon onto itself.
    std::vector<TrailPt> pts;
    pts.reserve(raw.size() * 2);
    pts.push_back(raw.front());
    for (std::size_t i = 0; i + 1 < raw.size(); ++i) {
      const auto &a = raw[i];
      const auto &b = raw[i + 1];
      pts.push_back({a.fX * 0.75f + b.fX * 0.25f, a.fY * 0.75f + b.fY * 0.25f,
                     a.fAlpha * 0.75f + b.fAlpha * 0.25f});
      pts.push_back({a.fX * 0.25f + b.fX * 0.75f, a.fY * 0.25f + b.fY * 0.75f,
                     a.fAlpha * 0.25f + b.fAlpha * 0.75f});
    }
    pts.push_back(raw.back());

    const float baseW = hasImg ? 6.0f * scale : 12.0f * scale;
    const float feather = 1.5f * scale; // ~1.5 device px of edge fade
    const std::size_t n = pts.size();

    // Four vertices per point: outer-left (alpha 0), inner-left, inner-right
    // (full alpha), outer-right (alpha 0). The solid core keeps the visual
    // width of the old stroked trail; only a narrow margin feathers to zero,
    // which is what provides the antialiasing.
    std::vector<skia::SkPoint> pos(n * 4);
    std::vector<skia::SkColor> col(n * 4);
    for (std::size_t i = 0; i < n; ++i) {
      const auto &prev = pts[i == 0 ? 0 : i - 1];
      const auto &next = pts[i + 1 < n ? i + 1 : n - 1];
      float tx = next.fX - prev.fX;
      float ty = next.fY - prev.fY;
      const float len = std::sqrt(tx * tx + ty * ty);
      if (len > 1e-6f) {
        tx /= len;
        ty /= len;
      } else {
        tx = 1.0f;
        ty = 0.0f;
      }
      // Solid half-width tapers with the same profile as the old stroke
      // width (which was the *total* width => halve it here).
      const float t =
          static_cast<float>(i + 1) / static_cast<float>(n);
      const float w = 0.5f * baseW * (0.12f + 0.88f * t * (2.0f - t));
      const float nxCore = -ty * w;
      const float nyCore = tx * w;
      const float nxOut = -ty * (w + feather);
      const float nyOut = tx * (w + feather);

      const auto a8 = static_cast<std::uint8_t>(
          std::clamp(pts[i].fAlpha, 0.0f, 1.0f) * 255.0f + 0.5f);
      pos[i * 4 + 0] = {pts[i].fX + nxOut, pts[i].fY + nyOut};
      pos[i * 4 + 1] = {pts[i].fX + nxCore, pts[i].fY + nyCore};
      pos[i * 4 + 2] = {pts[i].fX - nxCore, pts[i].fY - nyCore};
      pos[i * 4 + 3] = {pts[i].fX - nxOut, pts[i].fY - nyOut};
      col[i * 4 + 0] = skia::colorSetARGB(0, 255, 255, 255);
      col[i * 4 + 1] = skia::colorSetARGB(a8, 255, 255, 255);
      col[i * 4 + 2] = skia::colorSetARGB(a8, 255, 255, 255);
      col[i * 4 + 3] = skia::colorSetARGB(0, 255, 255, 255);
    }

    std::vector<std::uint16_t> idx;
    idx.reserve((n - 1) * 18);
    for (std::size_t i = 0; i + 1 < n; ++i) {
      const auto base0 = static_cast<std::uint16_t>(i * 4);
      const auto base1 = static_cast<std::uint16_t>((i + 1) * 4);
      for (std::uint16_t band = 0; band < 3; ++band) {
        const auto a0 = static_cast<std::uint16_t>(base0 + band);
        const auto b0 = static_cast<std::uint16_t>(base0 + band + 1);
        const auto a1 = static_cast<std::uint16_t>(base1 + band);
        const auto b1 = static_cast<std::uint16_t>(base1 + band + 1);
        idx.insert(idx.end(), {a0, b0, a1, a1, b0, b1});
      }
    }

    auto verts = skia::SkVertices::MakeCopy(
        skia::SkVertices::kTriangles_VertexMode, static_cast<int>(pos.size()),
        pos.data(), nullptr, col.data(), static_cast<int>(idx.size()),
        idx.data());

    skia::SkPaint paint;
    if (hasImg && !fNoGlow)
      paint.setBlendMode(skia::SkBlendMode::kPlus);
    // No shader on the paint: kDst keeps the interpolated vertex colors.
    canvas->drawVertices(verts, skia::SkBlendMode::kDst, paint);

    if (hasImg && !fNoGlow) {
      fSkin.drawCursorTrail(canvas, fCursorTrail.back().fPos, scale, 0.6f);
    }
  }

  // Cursor sensitivity scales movement about the playfield centre, which is
  // what osu! does when the setting is not 1x.
  [[nodiscard]] osu::Vec2 applySensitivity(osu::Vec2 raw) const {
    const double s = fSettings.value("sensitivity");
    if (std::abs(s - 1.0) < 1e-3) {
      return raw;
    }
    const auto c = osu::kPlayfieldCenter;
    return {c.fX + (raw.fX - c.fX) * s, c.fY + (raw.fY - c.fY) * s};
  }

  void drawCursor(skia::SkCanvas *canvas) {
    fSkin.drawCursor(canvas, fCursor,
                     fSettings.value("cursorsize") / fScale);
  }

  void drawPopups(skia::SkCanvas *canvas, double now, double cs) {
    const double hitSpriteScale = osu::circleRadius(cs) / 60.0;
    auto it = fPopups.begin();
    while (it != fPopups.end()) {
      const double age = now - it->fTime;
      const bool isMiss =
          std::holds_alternative<osu::judgement::Miss>(it->fResult);
      const double lifetime = isMiss ? 800.0 : 500.0;
      if (age > lifetime) {
        it = fPopups.erase(it);
        continue;
      }

      double alpha;
      float yOffset = 0.0f;
      if (isMiss) {
        alpha = age < 100.0   ? age / 100.0
                : age < 600.0 ? 1.0
                              : 1.0 - (age - 600.0) / 200.0;
        yOffset = static_cast<float>(100.0 * std::pow(age / lifetime, 5.0) *
                                     hitSpriteScale);
      } else {
        alpha = age < 100.0 ? age / 100.0 : 1.0 - (age - 100.0) / 400.0;
      }

      const int value = judgementValue(it->fResult);
      const float x = static_cast<float>(it->fPos.fX);
      const float y =
          static_cast<float>(it->fPos.fY) - 40.0f * hitSpriteScale + yOffset;

      // Skins provide hit300/hit100/hit50/hit0 sprites; that is what stable
      // and web-osu2 show. Fall back to text only when the skin has none.
      if (auto sprite = fSkin.judgement(value)) {
        skia::SkPaint paint;
        paint.setAntiAlias(true);
        paint.setAlphaf(static_cast<float>(alpha));
        const float w = static_cast<float>(sprite->width()) * 0.5f *
                        static_cast<float>(hitSpriteScale);
        const float h = static_cast<float>(sprite->height()) * 0.5f *
                        static_cast<float>(hitSpriteScale);
        canvas->drawImageRect(
            sprite.get(),
            skia::SkRect::MakeXYWH(x - w * 0.5f, y - h * 0.5f, w, h),
            skia::SkSamplingOptions(skia::SkFilterMode::kLinear), &paint);
        ++it;
        continue;
      }

      const auto [text, color] = popupInfo(it->fResult);
      const float fontSize = static_cast<float>(20.0 * hitSpriteScale);
      fFont.setSize(fontSize);

      const float textWidth = fFont.measureText(text, std::strlen(text),
                                                skia::SkTextEncoding::kUTF8);
      const float drawX = x - textWidth * 0.5f;

      skia::SkPaint stroke;
      stroke.setColor(skia::kBlack);
      stroke.setStyle(skia::kStrokeAndFillStyle);
      stroke.setStrokeWidth(fontSize * 0.12f);
      stroke.setAntiAlias(true);
      stroke.setAlphaf(static_cast<float>(alpha));
      canvas->drawString(text, drawX, y, fFont, stroke);

      skia::SkPaint paint;
      paint.setColor(color);
      paint.setStyle(skia::kFillStyle);
      paint.setAntiAlias(true);
      paint.setAlphaf(static_cast<float>(alpha));
      canvas->drawString(text, drawX, y, fFont, paint);
      ++it;
    }
  }

  [[nodiscard]] static int judgementValue(const osu::Judgement &j) {
    return std::visit(osu::Overloaded{
                          [](osu::judgement::Great) { return 300; },
                          [](osu::judgement::Good) { return 100; },
                          [](osu::judgement::Meh) { return 50; },
                          [](osu::judgement::Miss) { return 0; },
                      },
                      j);
  }

  [[nodiscard]] static std::pair<const char *, skia::SkColor>
  popupInfo(const osu::Judgement &j) {
    const auto [label, rgb] = osu::judgementInfo(j);
    return {label, skia::colorSetARGB(255, rgb[0], rgb[1], rgb[2])};
  }

  void drawHud(skia::SkCanvas *canvas, double now) {
    const auto &score = fEngine->score();
    const float sw = static_cast<float>(fScreenW);
    const float sh = static_cast<float>(fScreenH);

    if (fLastHudTime == 0.0)
      fLastHudTime = now;
    const double dt = now - fLastHudTime;
    fLastHudTime = now;
    constexpr double kLazyLag = 200.0;
    const double lagFactor = 1.0 - std::exp(-dt / kLazyLag);
    fDisplayHealth += (score.fHealth - fDisplayHealth) * lagFactor;
    fDisplayScore +=
        (static_cast<double>(score.fScore) - fDisplayScore) * lagFactor;
    fDisplayCombo +=
        (static_cast<double>(score.fCombo) - fDisplayCombo) * lagFactor;
    fDisplayAccuracy += (score.accuracy() - fDisplayAccuracy) * lagFactor;

    fHudPaint.setColor(skia::kWhite);
    fHudPaint.setAlphaf(1.0f);

    // Combo counter (large, top-left).
    fFont.setSize(48.0f);
    const std::string comboText =
        std::format("{:.0f}x", std::max(0.0, fDisplayCombo));
    canvas->drawString(comboText.c_str(), 20.0f, 60.0f, fFont, fHudPaint);

    // Score, accuracy, grade (top-center).
    fFont.setSize(22.0f);
    const std::string statsText =
        std::format("{:.0f}  {:.2f}%  {}", fDisplayScore,
                    std::clamp(fDisplayAccuracy, 0.0, 1.0) * 100.0,
                    osu::gradeString(osu::computeGrade(score)));
    canvas->drawString(statsText.c_str(), 20.0f, 90.0f, fFont, fHudPaint);

    // Difficulty / mods (top-right).
    fFont.setSize(16.0f);
    const std::string diffText = std::format(
        "CS:{:.1f} AR:{:.1f} OD:{:.1f} HP:{:.1f} {}", fMap->fDiff.fCs,
        fMap->fDiff.fAr, fMap->fDiff.fOd, fMap->fDiff.fHp, fEngine->mods());
    canvas->drawString(diffText.c_str(), 20.0f, 115.0f, fFont, fHudPaint);

    // Health bar (top).
    this->drawHealthBar(canvas, 0.0f, 0.0f, sw, 14.0f, now);

    // Judgement counts.
    fFont.setSize(16.0f);
    const std::string countsText =
        std::format("Great {}  Good {}  Meh {}  Miss {}", score.fGreat,
                    score.fGood, score.fMeh, score.fMiss);
    canvas->drawString(countsText.c_str(), 20.0f, 140.0f, fFont, fHudPaint);

    // Progress time.
    fFont.setSize(14.0f);
    fHudPaint.setAlphaf(0.7f);
    const std::string timeText = std::format("{:.1f}s", now / 1000.0);
    canvas->drawString(timeText.c_str(), sw - 80.0f, sh - 20.0f, fFont,
                       fHudPaint);

    double avgFrameMs = 0.0;
    if (fFrameTimeCount > 0) {
      for (std::size_t i = 0; i < fFrameTimeCount; ++i)
        avgFrameMs += fFrameTimes[i];
      avgFrameMs /= static_cast<double>(fFrameTimeCount);
    }
    const double fps = avgFrameMs > 0.0 ? 1000.0 / avgFrameMs : 0.0;
    const std::string fpsText = std::format("{:.0f} fps", std::round(fps));
    canvas->drawString(fpsText.c_str(), sw - 80.0f, sh - 40.0f, fFont,
                       fHudPaint);

    if (fShowProfile || fSettings.flag("fps")) {
      double avgAdv = 0.0, avgRender = 0.0, avgFlush = 0.0, avgSwap = 0.0;
      double avgFollow = 0.0, avgObjs = 0.0, avgRest = 0.0, avgHud = 0.0;
      if (fProfileNum > 0) {
        for (std::size_t i = 0; i < fProfileNum; ++i) {
          avgAdv += fProfile[i].advUs;
          avgRender += fProfile[i].renderUs;
          avgFlush += fProfile[i].flushUs;
          avgSwap += fProfile[i].swapUs;
          avgFollow += fProfile[i].renderFollowUs;
          avgObjs += fProfile[i].renderObjectsUs;
          avgRest += fProfile[i].renderRestUs;
          avgHud += fProfile[i].renderHudUs;
        }
        avgAdv /= static_cast<double>(fProfileNum);
        avgRender /= static_cast<double>(fProfileNum);
        avgFlush /= static_cast<double>(fProfileNum);
        avgSwap /= static_cast<double>(fProfileNum);
        avgFollow /= static_cast<double>(fProfileNum);
        avgObjs /= static_cast<double>(fProfileNum);
        avgRest /= static_cast<double>(fProfileNum);
        avgHud /= static_cast<double>(fProfileNum);
      }
      fFont.setSize(11.0f);
      fHudPaint.setAlphaf(0.6f);
      const std::string profText =
          std::format("adv {:.0f}  rend {:.0f}  flush {:.0f}  swap {:.0f} us",
                      avgAdv, avgRender, avgFlush, avgSwap);
      canvas->drawString(profText.c_str(), sw - 240.0f, sh - 60.0f, fFont,
                         fHudPaint);
      const std::string subText =
          std::format("follow {:.0f}  objs {:.0f}  rest {:.0f}  hud {:.0f} us",
                      avgFollow, avgObjs, avgRest, avgHud);
      canvas->drawString(subText.c_str(), sw - 240.0f, sh - 75.0f, fFont,
                         fHudPaint);
    }
  }

  void drawHealthBar(skia::SkCanvas *canvas, float x, float y, float w, float h,
                     double now) {
    auto left = fSkin.hpBarLeft();
    auto right = fSkin.hpBarRight();
    auto mid = fSkin.hpBarMid();
    const float hpX =
        x + w * static_cast<float>(std::clamp(fDisplayHealth, 0.0, 1.0));

    if (left && right && mid) {
      skia::SkPaint paint;
      paint.setAntiAlias(false);

      // Background/empty portion: right sprite stretches from hpX to the end.
      const float rightW = w;
      canvas->drawImageRect(
          right.get(), skia::SkRect::MakeXYWH(hpX, y, rightW, h),
          skia::SkSamplingOptions(skia::SkFilterMode::kLinear), &paint);

      // Filled portion: left sprite stretches from the start to hpX.
      const float leftW = hpX - x;
      canvas->drawImageRect(
          left.get(), skia::SkRect::MakeXYWH(x, y, leftW, h),
          skia::SkSamplingOptions(skia::SkFilterMode::kLinear), &paint);

      // Mid marker centered on the HP boundary.
      const float midScale = h / static_cast<float>(mid->height());
      const float midW = static_cast<float>(mid->width()) * midScale;
      canvas->drawImageRect(
          mid.get(), skia::SkRect::MakeXYWH(hpX - midW * 0.5f, y, midW, h),
          skia::SkSamplingOptions(skia::SkFilterMode::kLinear), &paint);
      return;
    }

    const float fill =
        w * static_cast<float>(std::clamp(fDisplayHealth, 0.0, 1.0));
    skia::SkPaint bg;
    bg.setColor(skia::kBlack);
    bg.setStyle(skia::kFillStyle);
    bg.setAlphaf(0.5f);
    canvas->drawRect(skia::SkRect::MakeXYWH(x, y, w, h), bg);

    skia::SkPaint fg;
    fg.setStyle(skia::kFillStyle);
    fg.setAntiAlias(true);
    if (fDisplayHealth > 0.5) {
      fg.setColor(skia::colorSetARGB(255, 50, 205, 50));
    } else if (fDisplayHealth > 0.25) {
      fg.setColor(skia::colorSetARGB(255, 255, 215, 0));
    } else {
      fg.setColor(skia::colorSetARGB(255, 255, 50, 50));
    }
    canvas->drawRect(skia::SkRect::MakeXYWH(x, y, fill, h), fg);

    skia::SkPaint border;
    border.setColor(skia::kWhite);
    border.setStyle(skia::kStrokeStyle);
    border.setStrokeWidth(2.0f);
    border.setAntiAlias(true);
    canvas->drawRect(skia::SkRect::MakeXYWH(x, y, w, h), border);
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
    if (!fMap)
      return {};
    const auto bytes = fSet.findFile(fBeatmapFilename);
    if (bytes.empty())
      return {};
    return osu::md5HashString(bytes);
  }

  void saveReplay() {
    if (fRecordedEvents.empty() || !fMap)
      return;
    auto now = std::chrono::system_clock::now();
    auto t = std::chrono::system_clock::to_time_t(now);
    std::ostringstream nameStream;
    nameStream << std::put_time(std::localtime(&t), "%Y%m%d_%H%M%S");
    auto replayBytes =
        osu::encodeReplay(fRecordedEvents, this->beatmapMd5(), "Player", fMods);
    std::filesystem::path outPath =
        fMap->fMeta.fVersion + "_" + nameStream.str() + ".osr";
    std::ofstream out(outPath, std::ios::binary);
    for (std::uint8_t b : replayBytes)
      out.put(static_cast<char>(b));
    std::println("Saved replay to {}", outPath.string());
  }

  [[nodiscard]] osu::Vec2 toPlayfield(float sx, float sy) const {
    return {(static_cast<double>(sx) - fOffsetX) / fScale,
            (static_cast<double>(sy) - fOffsetY) / fScale};
  }

};

} // namespace client
