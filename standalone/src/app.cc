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
#ifdef __EMSCRIPTEN__
import emscripten;
#endif

namespace client {

using audio_client::alFormat;
using audio_client::AudioContext;
using audio_client::audioContext;
using audio_client::AudioPlayer;
using audio_client::SamplePlayer;

export class App {
public:
  App(osu::BeatmapSet set, osu::ModSet mods, bool headless, bool autoplay,
      std::filesystem::path replayPath = {}, bool record = false,
      std::filesystem::path skinPath = {}, bool profile = false)
      : fSet(std::move(set)), fMods(mods), fHeadless(headless),
        fAutoplay(autoplay), fReplayPath(std::move(replayPath)),
        fRecord(record), fSkin(std::move(skinPath)), fShowProfile(profile) {}

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

  // Selection screen.
  enum class State { kSelecting, kPlaying };
  State fState = State::kSelecting;
  int fSelectedDifficulty = 0;
  bool fDifficultyConfirmed = false;
  int fEmscriptenResult = 0;
  int fHoveredDifficulty = -1;
  float fSelectScrollY = 0.0f;
  bool fClickPending = false;

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
    if (fSet.fBeatmaps.empty()) {
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
          if (action == glfw::kPress) {
            if (key == glfw::kKeyEscape) {
              glfw::glfwSetWindowShouldClose(w, glfw::kTrue);
              return;
            }
            if (key == glfw::kKeyF11) {
              self->toggleFullscreen();
              return;
            }
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

    if (fSet.fBeatmaps.size() > 1) {
      (void)this->runDifficultySelect();
      emscripten::emscripten_set_main_loop_arg(emscriptenFrameProc, this, 0, 1);
      return 0;
    }

    EM_ASM(Module.setCursorVisible(false));
    this->startGameplay(fSet.fBeatmaps.front());
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

    int selected = 0;
    if (fSet.fBeatmaps.size() > 1) {
      selected = this->runDifficultySelect();
      if (selected < 0 || selected >= static_cast<int>(fSet.fBeatmaps.size())) {
        this->requestQuit();
        glfw::glfwMakeContextCurrent(nullptr);
        return;
      }
    }

    fCursorModeRequest.store(glfw::kCursorHidden, std::memory_order_release);
    glfw::glfwPostEmptyEvent();
    this->startGameplay(fSet.fBeatmaps[static_cast<std::size_t>(selected)]);
    fExitCode.store(this->runGameplayLoop(), std::memory_order_release);
    this->requestQuit();
    glfw::glfwMakeContextCurrent(nullptr);
  }

  void requestQuit() {
    fQuit.store(true, std::memory_order_release);
    glfw::glfwPostEmptyEvent(); // wake the pump so it can exit
  }
#endif

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
      if (fState == State::kSelecting) {
        fSelectScrollY -= ev.fX * 30.0f;
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
    if (fState == State::kSelecting) {
      if (action == glfw::kPress) {
        if (key == glfw::kKeyUp || key == glfw::kKeyLeft) {
          fSelectedDifficulty = std::max(0, fSelectedDifficulty - 1);
        } else if (key == glfw::kKeyDown || key == glfw::kKeyRight) {
          fSelectedDifficulty =
              std::min(static_cast<int>(fSet.fBeatmaps.size()) - 1,
                       fSelectedDifficulty + 1);
        } else if (key == glfw::kKeyEnter || key == glfw::kKeySpace) {
          fDifficultyConfirmed = true;
        }
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

  void applyMouseButton(const Event &ev) {
    const int button = ev.fA;
    const int action = ev.fB;
    if (fState == State::kSelecting) {
      if (button == glfw::kMouseButtonLeft && action == glfw::kPress) {
        fClickPending = true;
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

  [[nodiscard]] int runDifficultySelect() {
    fState = State::kSelecting;
    this->loadSelectBackground();

#ifdef __EMSCRIPTEN__
    fEmscriptenResult = 0;
    return 0;
#else
    while (!fQuit.load(std::memory_order_acquire) &&
           !glfw::glfwWindowShouldClose(fWindow) && !fDifficultyConfirmed) {
      this->drainInput();
      this->updateSelectHover();
      this->renderDifficultySelect();
      fContext->flushAndSubmit(fSurface.get());
      glfw::glfwSwapBuffers(fWindow);
    }

    if (fQuit.load(std::memory_order_acquire) ||
        glfw::glfwWindowShouldClose(fWindow)) {
      return -1;
    }
    return fSelectedDifficulty;
#endif
  }

  [[nodiscard]] int runGameplayLoop() {
    fState = State::kPlaying;

#ifdef __EMSCRIPTEN__
    return 0;
#else
    while (!fQuit.load(std::memory_order_acquire) &&
           !glfw::glfwWindowShouldClose(fWindow) &&
           !this->shouldStop(this->nowMs())) {
      using clock = std::chrono::steady_clock;
      // Drain input first, then sample the frame clock: the engine requires
      // non-decreasing submission times, and the frame time taken after the
      // drain is >= every drained event time by construction.
      this->drainInput();
      const double now = this->nowMs();
      this->submitAutoplay(now);
      if (fShowProfile) {
        auto t0 = clock::now();
        fEngine->advance(now);
        auto t1 = clock::now();
        this->playHitsounds(now);
        this->render();
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
        this->render();
        fContext->flushAndSubmit(fSurface.get());
        glfw::glfwSwapBuffers(fWindow);
      }
    }

    this->printResult();
    if (fRecord)
      this->saveReplay();
    return 0;
#endif
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

    if (glfw::glfwWindowShouldClose(fWindow)) {
      this->printResult();
      if (fRecord)
        this->saveReplay();
      emscripten::emscripten_cancel_main_loop();
      return;
    }

    if (fState == State::kSelecting) {
      if (fDifficultyConfirmed) {
        if (fSelectedDifficulty < 0 ||
            fSelectedDifficulty >= static_cast<int>(fSet.fBeatmaps.size())) {
          emscripten::emscripten_cancel_main_loop();
          return;
        }
        EM_ASM(Module.setCursorVisible(false));
        this->startGameplay(
            fSet.fBeatmaps[static_cast<std::size_t>(fSelectedDifficulty)]);
        fState = State::kPlaying;
      } else {
        this->drainInput();
        this->updateSelectHover();
        this->renderDifficultySelect();
        fContext->flushAndSubmit(fSurface.get());
        glfw::glfwSwapBuffers(fWindow);
        return;
      }
    }

    if (fState == State::kPlaying) {
      if (this->shouldStop(this->nowMs())) {
        this->printResult();
        if (fRecord)
          this->saveReplay();
        emscripten::emscripten_cancel_main_loop();
        return;
      }
      this->drainInput();
      const double now = this->nowMs();
      this->submitAutoplay(now);
      fEngine->advance(now);
      this->playHitsounds(now);
      this->render();
      fContext->flushAndSubmit(fSurface.get());
      glfw::glfwSwapBuffers(fWindow);
    }
  }
#endif

  void loadSelectBackground() {
    for (const auto &info : fSet.fBeatmaps) {
      if (!info.fMeta.fBackground.empty()) {
        const auto bytes = fSet.findFile(info.fMeta.fBackground);
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

  void updateSelectHover() {
    fHoveredDifficulty = -1;
    const float sw = static_cast<float>(fScreenW);
    const float sh = static_cast<float>(fScreenH);
    const float cardW = std::min(600.0f, sw * 0.8f);
    const float cardH = 70.0f;
    const float gap = 12.0f;
    const float startY = sh * 0.35f;
    const float listH = sh * 0.55f;
    const float x = (sw - cardW) * 0.5f;

    if (fMouseX < x || fMouseX > x + cardW || fMouseY < startY ||
        fMouseY > startY + listH) {
      fClickPending = false;
      return;
    }

    for (std::size_t i = 0; i < fSet.fBeatmaps.size(); ++i) {
      const float y =
          startY + static_cast<float>(i) * (cardH + gap) - fSelectScrollY;
      if (fMouseY >= y && fMouseY <= y + cardH) {
        fHoveredDifficulty = static_cast<int>(i);
        if (fClickPending) {
          fSelectedDifficulty = static_cast<int>(i);
          fDifficultyConfirmed = true;
        }
        break;
      }
    }
    fClickPending = false;
  }

  void renderDifficultySelect() {
    auto *canvas = fSurface->getCanvas();
    if (fBackgroundScaled) {
      this->drawBackground(canvas);
    } else {
      canvas->clear(skia::kBlack);
    }

    const float sw = static_cast<float>(fScreenW);
    const float sh = static_cast<float>(fScreenH);

    skia::SkPaint paint;
    paint.setAntiAlias(true);

    // Title.
    const auto &first = fSet.fBeatmaps.front();
    const std::string title =
        (first.fMeta.fArtistUnicode.empty() ? first.fMeta.fArtist
                                            : first.fMeta.fArtistUnicode) +
        " - " +
        (first.fMeta.fTitleUnicode.empty() ? first.fMeta.fTitle
                                           : first.fMeta.fTitleUnicode);
    fFont.setSize(42.0f);
    paint.setColor(skia::kWhite);
    const float titleWidth = fFont.measureText(title.c_str(), title.size(),
                                               skia::SkTextEncoding::kUTF8);
    canvas->drawString(title.c_str(), (sw - titleWidth) * 0.5f, sh * 0.18f,
                       fFont, paint);

    // Mapper.
    fFont.setSize(20.0f);
    paint.setAlphaf(0.7f);
    const std::string mapper = "mapped by " + first.fMeta.fCreator;
    const float mapperWidth = fFont.measureText(mapper.c_str(), mapper.size(),
                                                skia::SkTextEncoding::kUTF8);
    canvas->drawString(mapper.c_str(), (sw - mapperWidth) * 0.5f,
                       sh * 0.18f + 32.0f, fFont, paint);

    // Difficulty cards.
    const float cardW = std::min(600.0f, sw * 0.8f);
    const float cardH = 70.0f;
    const float gap = 12.0f;
    const float startY = sh * 0.35f;
    const float x = (sw - cardW) * 0.5f;
    const float listH = sh * 0.55f;
    const float contentH =
        static_cast<float>(fSet.fBeatmaps.size()) * (cardH + gap) - gap;
    fSelectScrollY =
        std::clamp(fSelectScrollY, 0.0f, std::max(0.0f, contentH - listH));

    canvas->save();
    canvas->clipRect(skia::SkRect::MakeXYWH(x, startY, cardW, listH));
    canvas->translate(0.0f, -fSelectScrollY);

    for (std::size_t i = 0; i < fSet.fBeatmaps.size(); ++i) {
      const auto &info = fSet.fBeatmaps[i];
      const float y = startY + static_cast<float>(i) * (cardH + gap);
      if (y + cardH < startY + fSelectScrollY ||
          y > startY + fSelectScrollY + listH) {
        continue;
      }
      const bool hovered = (fHoveredDifficulty == static_cast<int>(i));
      const bool selected = (fSelectedDifficulty == static_cast<int>(i));

      skia::SkPaint card;
      card.setAntiAlias(true);
      if (selected) {
        card.setColor(skia::colorSetARGB(255, 80, 140, 200));
      } else if (hovered) {
        card.setColor(skia::colorSetARGB(255, 60, 60, 70));
      } else {
        card.setColor(skia::colorSetARGB(255, 35, 35, 40));
      }
      canvas->drawRect(skia::SkRect::MakeXYWH(x, y, cardW, cardH), card);

      skia::SkPaint border;
      border.setAntiAlias(true);
      border.setStyle(skia::kStrokeStyle);
      border.setStrokeWidth(selected ? 3.0f : 1.0f);
      border.setColor(selected ? skia::colorSetARGB(255, 120, 180, 255)
                               : skia::colorSetARGB(255, 80, 80, 90));
      canvas->drawRect(skia::SkRect::MakeXYWH(x, y, cardW, cardH), border);

      fFont.setSize(24.0f);
      paint.setColor(skia::kWhite);
      paint.setAlphaf(1.0f);
      canvas->drawString(info.fMeta.fVersion.c_str(), x + 20.0f,
                         y + cardH * 0.55f, fFont, paint);

      fFont.setSize(15.0f);
      paint.setAlphaf(0.75f);
      const std::string stats = std::format(
          "{:.2f}*  CS:{:.1f} AR:{:.1f} OD:{:.1f} HP:{:.1f}  {} objects  "
          "{:.1f}s",
          info.fStars, info.fDiff.fCs, info.fDiff.fAr, info.fDiff.fOd,
          info.fDiff.fHp, info.fObjectCount, info.fLengthMs / 1000.0);
      canvas->drawString(stats.c_str(), x + 20.0f, y + cardH * 0.82f, fFont,
                         paint);
    }
    canvas->restore();

    // Instructions.
    fFont.setSize(16.0f);
    paint.setColor(skia::kWhite);
    paint.setAlphaf(0.6f);
    const std::string hint =
        "Click / Enter to play    Arrow keys to navigate    Esc to quit";
    const float hintWidth = fFont.measureText(hint.c_str(), hint.size(),
                                              skia::SkTextEncoding::kUTF8);
    canvas->drawString(hint.c_str(), (sw - hintWidth) * 0.5f, sh - 30.0f, fFont,
                       paint);
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
        "/usr/share/fonts/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/TTF/DejaVuSans.ttf",
    };

    const std::vector<std::filesystem::path> dirs{
        "/usr/share/fonts/noto",
        "/usr/share/fonts/dejavu",
        "/usr/share/fonts/truetype/dejavu",
        "/usr/share/fonts/TTF",
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
      if (face)
        return skia::SkFont(std::move(face), size);
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

  void render() {
    using clock = std::chrono::steady_clock;
    auto rt0 = clock::now();

    const double now = this->nowMs();
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
