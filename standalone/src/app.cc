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
  struct LibraryEntry {
    std::filesystem::path fPath; // empty for the set passed on the CLI
    osu::BeatmapSet fSet;
  };
  std::vector<LibraryEntry> fLibrary;
  bool fLibraryLoaded = false;
  int fSelSet = 0;
  int fSelDiff = 0;
  float fCarouselScroll = 0.0f;
  int fBackgroundForSet = -1;
  std::filesystem::path fMapsDir;
  struct CarouselHit {
    skia::SkRect fRect;
    int fSetIdx;
    int fDiffIdx; // -1 => set header
  };
  std::vector<CarouselHit> fCarouselHits; // rebuilt every song-select frame
  skia::SkRect fDownloadsChip = skia::SkRect::MakeEmpty(); // bottom-bar button

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
  bool fMenuExpanded = false;
  float fMenuExpand = 0.0f;    // eased 0..1
  skia::SkRect fLogoRect = skia::SkRect::MakeEmpty();
  struct Tri {
    float fX, fY, fSize, fSpeed, fAlpha;
  };
  std::vector<Tri> fTriangles;
  std::mt19937 fUiRng{0xC0FFEEu};

  [[nodiscard]] static float easeOutQuint(float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    const float u = 1.0f - t;
    return 1.0f - u * u * u * u * u;
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

  // Lazer-ish palette.
  static constexpr skia::SkColor kAccent = skia::colorSetARGB(255, 255, 102, 170);
  static constexpr skia::SkColor kAccent2 = skia::colorSetARGB(255, 102, 204, 255);
  static constexpr skia::SkColor kCardBg = skia::colorSetARGB(255, 42, 36, 48);
  static constexpr skia::SkColor kCardSel = skia::colorSetARGB(255, 64, 48, 70);
  static constexpr skia::SkColor kPanelBg = skia::colorSetARGB(215, 22, 18, 28);

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
        fWindow, [](glfw::GLFWwindow *w, int key, int, int action, int) {
          auto *self = static_cast<App *>(glfw::glfwGetWindowUserPointer(w));
          if (self == nullptr)
            return;
          if (action == glfw::kPress && key == glfw::kKeyF11) {
            self->toggleFullscreen();
            return;
          }
          if (action == glfw::kRepeat)
            return; // key auto-repeat is meaningless for gameplay
          self->enqueue({App::wallMs(), EventType::kKey, key, action});
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
    fState = State::kMainMenu;
    fStateEnterWall = wallMs();

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
      if (fState == State::kPlaying && !fAutoplay) {
        fCursor = this->toPlayfield(ev.fX, ev.fY);
        this->submitTimed({this->eventGameTime(ev.fWallMs), fCursor,
                           osu::InputAction::kMove});
      }
      break;
    case EventType::kScroll:
      if (fState == State::kSongSelect) {
        fCarouselScroll -= ev.fX * 60.0f;
      } else if (fState == State::kDownload) {
        fDownloadScroll -= ev.fX * 60.0f;
      }
      break;
    case EventType::kChar:
      if (fState == State::kSongSelect) {
        // Fallback: open downloads on the typed character as well. Char
        // events take a different GLFW path than key codes, which makes
        // this immune to whatever eats the key event.
        if (ev.fA == 'd' || ev.fA == 'D') {
          this->openDownloads();
          fSwallowChar = false; // this WAS the char; nothing left to eat
        }
      } else if (fState == State::kDownload) {
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
    const int nSets = static_cast<int>(fLibrary.size());
    if (key == glfw::kKeyEscape) {
      this->switchState(State::kMainMenu);
      return;
    }
    if (key == glfw::kKeyD) {
      this->openDownloads();
      return;
    }
    if (nSets == 0) {
      return;
    }
    const int nDiffs =
        static_cast<int>(fLibrary[static_cast<std::size_t>(fSelSet)]
                             .fSet.fBeatmaps.size());
    if (key == glfw::kKeyUp) {
      if (fSelDiff > 0) {
        --fSelDiff;
      } else if (fSelSet > 0) {
        --fSelSet;
        fSelDiff = static_cast<int>(
                       fLibrary[static_cast<std::size_t>(fSelSet)]
                           .fSet.fBeatmaps.size()) -
                   1;
      }
    } else if (key == glfw::kKeyDown) {
      if (fSelDiff + 1 < nDiffs) {
        ++fSelDiff;
      } else if (fSelSet + 1 < nSets) {
        ++fSelSet;
        fSelDiff = 0;
      }
    } else if (key == glfw::kKeyLeft) {
      fSelSet = std::max(0, fSelSet - 1);
      fSelDiff = 0;
    } else if (key == glfw::kKeyRight) {
      fSelSet = std::min(nSets - 1, fSelSet + 1);
      fSelDiff = 0;
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

    if (fState == State::kSongSelect || fState == State::kDownload ||
        fState == State::kPaused || fState == State::kResults) {
      if (button == glfw::kMouseButtonLeft && action == glfw::kPress) {
        this->clickAt(fMouseX, fMouseY);
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
    switch (fState) {
    case State::kMainMenu: {
      const float dx = x - fLogoRect.centerX();
      const float dy = y - fLogoRect.centerY();
      const float r = fLogoRect.width() * 0.5f;
      if (dx * dx + dy * dy <= r * r) {
        fMenuExpanded = !fMenuExpanded;
        return;
      }
      if (fMenuExpanded) {
        for (std::size_t i = 0; i < fMenuButtons.size(); ++i) {
          if (fMenuButtons[i].fRect.contains(x, y)) {
            if (i == 0) {
              this->switchState(State::kSongSelect);
            } else if (i == 1) {
              this->openDownloads();
            } else {
              this->requestQuit();
            }
            return;
          }
        }
      }
      break;
    }
    case State::kSongSelect:
      if (fDownloadsChip.contains(x, y)) {
        this->openDownloads();
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
      } else {
        this->quitToSelect();
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
    fMenuExpanded = false;
  }

  // ---- Frame dispatch ---------------------------------------------------

  void frame() {
    client::http::poll(); // completed network callbacks land here
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
    if (fShowProfile) {
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
    auto &entry = fLibrary[static_cast<std::size_t>(setIdx)];
    if (diffIdx < 0 ||
        diffIdx >= static_cast<int>(entry.fSet.fBeatmaps.size())) {
      return;
    }
    fPlayingSet = setIdx;
    fPlayingDiff = diffIdx;
    fSet = entry.fSet; // active copy: gameplay reads audio/bg from here
    this->resetGameplayState();
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
    this->switchState(State::kSongSelect);
    fFirstFrame = true;
    fBackgroundForSet = -1; // gameplay replaced the cached background
    this->setCursorVisible(true);
  }

  void finishPlay() {
    this->captureResult();
    this->printResult();
    if (fRecord) {
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

    if (fHasInitialSet) {
      fLibrary.push_back({{}, fSet});
    }
    std::error_code iterEc;
    for (const auto &e :
         std::filesystem::directory_iterator(fMapsDir, iterEc)) {
      if (!e.is_regular_file()) {
        continue;
      }
      const auto path = e.path();
      if (path.extension() != ".osz") {
        continue;
      }
      this->addOszToLibrary(path, false);
    }
    std::ranges::sort(fLibrary, {}, [](const LibraryEntry &l) {
      return l.fSet.fBeatmaps.empty() ? std::string{}
                                      : l.fSet.fBeatmaps.front().fMeta.fTitle;
    });
    fSelSet = 0;
    fSelDiff = 0;
    fLibraryLoaded = true;
  }

  bool addOszToLibrary(const std::filesystem::path &path, bool select) {
    try {
      LibraryEntry entry{path, loadBeatmapSet(path)};
      fLibrary.push_back(std::move(entry));
      if (select) {
        fSelSet = static_cast<int>(fLibrary.size()) - 1;
        fSelDiff = 0;
      }
      return true;
    } catch (const std::exception &e) {
      std::println("failed to load {}: {}", path.string(), e.what());
      return false;
    }
  }

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
    skia::SkPaint p;
    p.setAntiAlias(true);
    p.setColor(color);
    canvas->drawRRect(skia::SkRRect::MakeRectXY(rect, radius, radius), p);
  }

  void strokeRounded(skia::SkCanvas *canvas, const skia::SkRect &rect,
                     float radius, skia::SkColor color, float width) {
    skia::SkPaint p;
    p.setAntiAlias(true);
    p.setColor(color);
    p.setStyle(skia::kStrokeStyle);
    p.setStrokeWidth(width);
    canvas->drawRRect(skia::SkRRect::MakeRectXY(rect, radius, radius), p);
  }

  void drawTextClipped(skia::SkCanvas *canvas, const std::string &text,
                       float x, float y, float maxW, float size,
                       skia::SkColor color, float alpha = 1.0f) {
    fFont.setSize(size);
    skia::SkPaint p;
    p.setAntiAlias(true);
    p.setColor(color);
    p.setAlphaf(alpha);
    canvas->save();
    canvas->clipIRect(skia::SkIRect::MakeXYWH(
        static_cast<int>(x), static_cast<int>(y - size * 1.2f),
        static_cast<int>(maxW), static_cast<int>(size * 1.8f)));
    canvas->drawString(text.c_str(), x, y, fFont, p);
    canvas->restore();
  }

  void drawTextCentered(skia::SkCanvas *canvas, const std::string &text,
                        float cx, float y, float size, skia::SkColor color,
                        float alpha = 1.0f) {
    fFont.setSize(size);
    skia::SkPaint p;
    p.setAntiAlias(true);
    p.setColor(color);
    p.setAlphaf(alpha);
    const float w =
        fFont.measureText(text.c_str(), text.size(), skia::SkTextEncoding::kUTF8);
    canvas->drawString(text.c_str(), cx - w * 0.5f, y, fFont, p);
  }

  [[nodiscard]] static skia::SkColor starColor(double stars) {
    if (stars < 2.0)
      return skia::colorSetARGB(255, 102, 204, 102);
    if (stars < 2.7)
      return skia::colorSetARGB(255, 102, 204, 255);
    if (stars < 4.0)
      return skia::colorSetARGB(255, 255, 204, 102);
    if (stars < 5.3)
      return skia::colorSetARGB(255, 255, 102, 170);
    if (stars < 6.5)
      return skia::colorSetARGB(255, 170, 102, 255);
    return skia::colorSetARGB(255, 90, 90, 110);
  }

  [[nodiscard]] static std::string setTitle(const osu::BeatmapSet &set) {
    if (set.fBeatmaps.empty())
      return "(empty set)";
    const auto &m = set.fBeatmaps.front().fMeta;
    return m.fTitleUnicode.empty() ? m.fTitle : m.fTitleUnicode;
  }

  [[nodiscard]] static std::string setArtist(const osu::BeatmapSet &set) {
    if (set.fBeatmaps.empty())
      return {};
    const auto &m = set.fBeatmaps.front().fMeta;
    return m.fArtistUnicode.empty() ? m.fArtist : m.fArtistUnicode;
  }

  void drawScreenBackground(skia::SkCanvas *canvas) {
    if (fBackgroundScaled) {
      this->drawBackground(canvas);
    } else {
      canvas->clear(skia::colorSetARGB(255, 18, 14, 24));
    }
  }

  // ---- Main menu (lazer-style logo) -------------------------------------

  void ensureTriangles() {
    if (!fTriangles.empty()) {
      return;
    }
    std::uniform_real_distribution<float> ux(0.0f, 1.0f);
    std::uniform_real_distribution<float> us(28.0f, 150.0f);
    std::uniform_real_distribution<float> uv(8.0f, 30.0f);
    std::uniform_real_distribution<float> ua(0.04f, 0.11f);
    for (int i = 0; i < 26; ++i) {
      fTriangles.push_back({ux(fUiRng), ux(fUiRng), us(fUiRng), uv(fUiRng),
                            ua(fUiRng)});
    }
  }

  void updateAndDrawTriangles(skia::SkCanvas *canvas) {
    const float sw = static_cast<float>(fScreenW);
    const float sh = static_cast<float>(fScreenH);
    std::uniform_real_distribution<float> ux(0.0f, 1.0f);
    skia::SkPaint p;
    p.setAntiAlias(true);
    for (auto &t : fTriangles) {
      t.fY -= t.fSpeed * static_cast<float>(fUiDt) / 1000.0f / sh;
      if (t.fY < -0.25f) {
        t.fY = 1.25f;
        t.fX = ux(fUiRng);
      }
      const float cx = t.fX * sw;
      const float cy = t.fY * sh;
      const float r = t.fSize;
      skia::SkPathBuilder b;
      b.moveTo(cx, cy - r * 0.577f * 2.0f * 0.5f);
      b.lineTo(cx - r * 0.5f, cy + r * 0.289f);
      b.lineTo(cx + r * 0.5f, cy + r * 0.289f);
      b.close();
      p.setColor(skia::kWhite);
      p.setAlphaf(t.fAlpha);
      canvas->drawPath(b.detach(), p);
    }
  }

  void frameMainMenu() {
    auto *canvas = fSurface->getCanvas();
    canvas->clear(skia::colorSetARGB(255, 18, 14, 24));

    this->ensureTriangles();
    this->updateAndDrawTriangles(canvas);

    const float sw = static_cast<float>(fScreenW);
    const float sh = static_cast<float>(fScreenH);
    const double wall = wallMs();

    // Expansion easing toward the target.
    const float target = fMenuExpanded ? 1.0f : 0.0f;
    fMenuExpand +=
        (target - fMenuExpand) *
        std::min(1.0f, static_cast<float>(fUiDt) / 90.0f);

    // Pulsing logo, sliding left as the buttons expand.
    const float pulse =
        1.0f + 0.016f * static_cast<float>(std::sin(wall / 420.0));
    const float logoR =
        std::min(sw, sh) * 0.17f * pulse * (1.0f - 0.10f * fMenuExpand);
    const float cx = sw * 0.5f - fMenuExpand * sw * 0.17f;
    const float cy = sh * 0.46f;

    skia::SkPaint glow;
    glow.setAntiAlias(true);
    glow.setColor(kAccent);
    glow.setAlphaf(0.22f);
    canvas->drawCircle(cx, cy, logoR * 1.13f, glow);
    skia::SkPaint disc;
    disc.setAntiAlias(true);
    disc.setColor(kAccent);
    canvas->drawCircle(cx, cy, logoR, disc);
    skia::SkPaint ring;
    ring.setAntiAlias(true);
    ring.setStyle(skia::kStrokeStyle);
    ring.setStrokeWidth(logoR * 0.055f);
    ring.setColor(skia::kWhite);
    ring.setAlphaf(0.9f);
    canvas->drawCircle(cx, cy, logoR * 0.82f, ring);
    this->drawTextCentered(canvas, "osu!", cx, cy + logoR * 0.22f,
                           logoR * 0.55f, skia::kWhite);
    fLogoRect =
        skia::SkRect::MakeXYWH(cx - logoR, cy - logoR, logoR * 2, logoR * 2);

    // Buttons fan out to the right of the logo.
    fMenuButtons.clear();
    if (fMenuExpand > 0.02f) {
      const char *labels[] = {"play", "downloads", "exit"};
      const skia::SkColor accents[] = {
          kAccent, kAccent2, skia::colorSetARGB(255, 255, 110, 110)};
      const float bx0 = cx + logoR + 28.0f;
      const float bw = std::min(210.0f, (sw - bx0 - 32.0f - 2 * 16.0f) / 3.0f);
      const float bh = 58.0f;
      const float ease = easeOutQuint(fMenuExpand);
      for (int i = 0; i < 3; ++i) {
        const float bx =
            bx0 + static_cast<float>(i) * (bw + 16.0f) -
            (1.0f - ease) * 60.0f * static_cast<float>(i + 1);
        const skia::SkRect r =
            skia::SkRect::MakeXYWH(bx, cy - bh * 0.5f, bw, bh);
        fMenuButtons.push_back({r, labels[i], accents[i]});
      }
      canvas->save();
      // Fade the buttons with the expansion.
      skia::SkPaint dummy;
      (void)dummy;
      for (auto &b : fMenuButtons) {
        // Cheap alpha: draw into layerless canvas with eased colors.
        const bool hover = b.fRect.contains(fMouseX, fMouseY);
        skia::SkPaint fill;
        fill.setAntiAlias(true);
        fill.setColor(hover ? kCardSel : kCardBg);
        fill.setAlphaf(ease);
        canvas->drawRRect(
            skia::SkRRect::MakeRectXY(b.fRect, 12.0f, 12.0f), fill);
        skia::SkPaint stroke;
        stroke.setAntiAlias(true);
        stroke.setStyle(skia::kStrokeStyle);
        stroke.setStrokeWidth(hover ? 3.0f : 2.0f);
        stroke.setColor(b.fAccent);
        stroke.setAlphaf(ease);
        canvas->drawRRect(
            skia::SkRRect::MakeRectXY(b.fRect, 12.0f, 12.0f), stroke);
        fFont.setSize(19.0f);
        skia::SkPaint tp;
        tp.setAntiAlias(true);
        tp.setColor(hover ? b.fAccent : skia::kWhite);
        tp.setAlphaf(ease);
        const float tw = fFont.measureText(b.fLabel.c_str(), b.fLabel.size(),
                                           skia::SkTextEncoding::kUTF8);
        canvas->drawString(b.fLabel.c_str(), b.fRect.centerX() - tw * 0.5f,
                           b.fRect.centerY() + 7.0f, fFont, tp);
      }
      canvas->restore();
    }

    this->drawBottomBar(canvas, fMenuExpanded
                                    ? "play / downloads / exit    Esc close"
                                    : "click the circle    Esc quit");
    this->drawScreenFadeIn(canvas);
    this->present();
  }

  void keyMainMenu(int key) {
    if (key == glfw::kKeyEscape) {
      if (fMenuExpanded) {
        fMenuExpanded = false;
      } else {
        this->requestQuit();
      }
      return;
    }
    if (key == glfw::kKeyEnter || key == glfw::kKeySpace) {
      if (!fMenuExpanded) {
        fMenuExpanded = true;
      } else {
        this->switchState(State::kSongSelect);
      }
      return;
    }
    if (key == glfw::kKeyD) {
      this->openDownloads();
    }
  }

  // ---- Song select ------------------------------------------------------

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
    auto *canvas = fSurface->getCanvas();

    // Background follows the selected set.
    if (!fLibrary.empty() && fBackgroundForSet != fSelSet) {
      fBackgroundForSet = fSelSet;
      this->loadSelectBackground(
          fLibrary[static_cast<std::size_t>(fSelSet)].fSet);
    }
    this->drawScreenBackground(canvas);

    const float sw = static_cast<float>(fScreenW);
    const float sh = static_cast<float>(fScreenH);

    fCarouselHits.clear();

    if (fLibrary.empty()) {
      this->drawTextCentered(canvas, "No beatmaps in the library",
                             sw * 0.5f, sh * 0.45f, 28.0f, skia::kWhite, 0.9f);
      this->drawTextCentered(canvas,
                             "Press D to open the beatmap listing and "
                             "download some",
                             sw * 0.5f, sh * 0.45f + 40.0f, 18.0f, kAccent);
      this->drawBottomBar(canvas, "D downloads    Esc quit    F11 fullscreen");
      this->present();
      return;
    }

    // ---- Left: selected map info panel.
    const auto &selSet = fLibrary[static_cast<std::size_t>(fSelSet)].fSet;
    const auto &selInfo =
        selSet.fBeatmaps[static_cast<std::size_t>(fSelDiff)];
    const float infoW = sw * 0.46f;
    this->fillRounded(canvas,
                      skia::SkRect::MakeXYWH(20.0f, 24.0f, infoW, 190.0f),
                      12.0f, kPanelBg);
    this->drawTextClipped(canvas, setTitle(selSet), 40.0f, 74.0f, infoW - 40.0f,
                          34.0f, skia::kWhite);
    this->drawTextClipped(canvas, setArtist(selSet), 40.0f, 106.0f,
                          infoW - 40.0f, 20.0f, skia::kWhite, 0.8f);
    this->drawTextClipped(canvas,
                          std::format("mapped by {}", selInfo.fMeta.fCreator),
                          40.0f, 132.0f, infoW - 40.0f, 16.0f, kAccent2, 0.9f);
    this->drawTextClipped(
        canvas,
        std::format("[{}]  {:.2f}*  CS{:.1f} AR{:.1f} OD{:.1f} HP{:.1f}",
                    selInfo.fMeta.fVersion, selInfo.fStars, selInfo.fDiff.fCs,
                    selInfo.fDiff.fAr, selInfo.fDiff.fOd, selInfo.fDiff.fHp),
        40.0f, 164.0f, infoW - 40.0f, 16.0f, starColor(selInfo.fStars));
    this->drawTextClipped(
        canvas,
        std::format("{} objects   {:.0f}:{:02.0f}", selInfo.fObjectCount,
                    selInfo.fLengthMs / 60000.0,
                    std::fmod(selInfo.fLengthMs / 1000.0, 60.0)),
        40.0f, 190.0f, infoW - 40.0f, 15.0f, skia::kWhite, 0.7f);

    // ---- Right: carousel.
    const float carW = std::min(600.0f, sw * 0.46f);
    const float carX = sw - carW - 20.0f;
    const float headerH = 62.0f;
    const float diffH = 40.0f;
    const float gap = 6.0f;
    const float topPad = 24.0f;
    const float bottomPad = 64.0f;

    // Layout pass: y positions with the selected set expanded.
    float y = topPad;
    float selY = topPad;
    float totalH = 0.0f;
    for (int si = 0; si < static_cast<int>(fLibrary.size()); ++si) {
      if (si == fSelSet) {
        selY = y + headerH +
               static_cast<float>(fSelDiff) * (diffH + gap);
      }
      y += headerH + gap;
      if (si == fSelSet) {
        y += static_cast<float>(
                 fLibrary[static_cast<std::size_t>(si)].fSet.fBeatmaps.size()) *
             (diffH + gap);
      }
    }
    totalH = y - topPad;

    // Keep the selection on screen; fCarouselScroll is the *target*, the
    // drawn position eases toward it exponentially (lazer-style smoothing).
    const float viewH = sh - topPad - bottomPad;
    const float target = std::clamp(selY - fCarouselScroll, 40.0f,
                                    viewH - diffH - 40.0f);
    fCarouselScroll = selY - target;
    fCarouselScroll =
        std::clamp(fCarouselScroll, 0.0f, std::max(0.0f, totalH - viewH));
    fScrollAnim += (fCarouselScroll - fScrollAnim) *
                   std::min(1.0f, static_cast<float>(fUiDt) / 90.0f);

    // Selection pop-out animation restarts on any selection change.
    const int selKey = fSelSet * 1024 + fSelDiff;
    if (selKey != fPrevSelKey) {
      fPrevSelKey = selKey;
      fPopAnim = 0.0f;
    }
    fPopAnim = std::min(1.0f, fPopAnim + static_cast<float>(fUiDt) / 220.0f);
    const float pop = easeOutQuint(fPopAnim);

    canvas->save();
    canvas->clipIRect(skia::SkIRect::MakeXYWH(
        static_cast<int>(carX) - 8, static_cast<int>(topPad) - 8,
        static_cast<int>(carW) + 16, static_cast<int>(viewH) + 16));

    y = topPad - fScrollAnim;
    for (int si = 0; si < static_cast<int>(fLibrary.size()); ++si) {
      const auto &set = fLibrary[static_cast<std::size_t>(si)].fSet;
      const bool selected = si == fSelSet;
      const skia::SkRect header = skia::SkRect::MakeXYWH(
          carX - (selected ? 14.0f * pop : 0.0f), y, carW, headerH);
      if (y + headerH >= topPad && y <= sh) {
        this->fillRounded(canvas, header, 10.0f,
                          selected ? kCardSel : kCardBg);
        if (selected) {
          this->strokeRounded(canvas, header, 10.0f, kAccent, 2.0f);
        }
        this->drawTextClipped(canvas, setTitle(set), carX + 16.0f, y + 26.0f,
                              carW - 32.0f, 18.0f, skia::kWhite);
        this->drawTextClipped(
            canvas,
            std::format("{}  //  {} difficult{}", setArtist(set),
                        set.fBeatmaps.size(),
                        set.fBeatmaps.size() == 1 ? "y" : "ies"),
            carX + 16.0f, y + 48.0f, carW - 32.0f, 14.0f, skia::kWhite, 0.65f);
        fCarouselHits.push_back({header, si, -1});
      }
      y += headerH + gap;

      if (!selected) {
        continue;
      }
      for (int di = 0; di < static_cast<int>(set.fBeatmaps.size()); ++di) {
        const auto &info = set.fBeatmaps[static_cast<std::size_t>(di)];
        const bool diffSel = di == fSelDiff;
        const skia::SkRect row = skia::SkRect::MakeXYWH(
            carX + 24.0f - (diffSel ? 18.0f * pop : 0.0f), y, carW - 24.0f,
            diffH);
        if (y + diffH >= topPad && y <= sh) {
          this->fillRounded(canvas, row, 8.0f,
                            diffSel ? kCardSel : kCardBg);
          if (diffSel) {
            this->strokeRounded(canvas, row, 8.0f, kAccent2, 2.0f);
          }
          // Star chip.
          const skia::SkRect chip = skia::SkRect::MakeXYWH(
              row.fLeft + 10.0f, y + 9.0f, 58.0f, diffH - 18.0f);
          this->fillRounded(canvas, chip, 6.0f, starColor(info.fStars));
          this->drawTextCentered(canvas,
                                 std::format("{:.2f}*", info.fStars),
                                 chip.centerX(), y + 24.0f, 13.0f,
                                 skia::colorSetARGB(255, 20, 16, 26));
          this->drawTextClipped(canvas, info.fMeta.fVersion,
                                row.fLeft + 80.0f, y + 25.0f, carW - 128.0f,
                                15.0f, skia::kWhite, 0.9f);
          fCarouselHits.push_back({row, si, di});
        }
        y += diffH + gap;
      }
    }
    canvas->restore();

    this->drawBottomBar(canvas,
                        "Enter / click twice to play    Arrows navigate    "
                        "D downloads    Esc menu");

    // Clickable downloads chip (mouse path, independent of the keyboard).
    const float chipW = 150.0f;
    fDownloadsChip =
        skia::SkRect::MakeXYWH(sw - chipW - 16.0f, sh - 38.0f, chipW, 32.0f);
    const bool chipHover = fDownloadsChip.contains(fMouseX, fMouseY);
    this->fillRounded(canvas, fDownloadsChip, 8.0f,
                      chipHover ? kCardSel : kCardBg);
    this->strokeRounded(canvas, fDownloadsChip, 8.0f, kAccent2,
                        chipHover ? 2.0f : 1.0f);
    this->drawTextCentered(canvas, "downloads", fDownloadsChip.centerX(),
                           fDownloadsChip.centerY() + 5.0f, 14.0f,
                           chipHover ? kAccent2 : skia::kWhite);
    this->drawScreenFadeIn(canvas);
    this->present();
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
        {"great", sc.fGreat, skia::colorSetARGB(255, 102, 204, 255)},
        {"good", sc.fGood, skia::colorSetARGB(255, 120, 220, 120)},
        {"meh", sc.fMeh, skia::colorSetARGB(255, 255, 204, 102)},
        {"miss", sc.fMiss, skia::colorSetARGB(255, 255, 110, 110)},
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

  void resize(int w, int h) {
    // Called on the render thread with framebuffer dimensions delivered by
    // the resize event (or the pre-thread snapshot); querying GLFW here is
    // not allowed off the main thread.
    fScreenW = w;
    fScreenH = h;
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
        if (!player.loaded())
          player.load(bytes, std::string(ext));
        player.play();
        return;
      }
    }

    const auto path = this->findSkinSamplePath(name);
    if (path.empty())
      return;
    auto &player = fSamples[path.string()];
    if (!player.loaded())
      player.load(path);
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

    if (fShowProfile) {
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
    paint.setAlphaf(0.3f);
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

  void drawCursor(skia::SkCanvas *canvas) {
    fSkin.drawCursor(canvas, fCursor, 1.0f / fScale);
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

      const auto [text, color] = popupInfo(it->fResult);
      const float x = static_cast<float>(it->fPos.fX);
      const float y =
          static_cast<float>(it->fPos.fY) - 40.0f * hitSpriteScale + yOffset;
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

    if (fShowProfile) {
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
