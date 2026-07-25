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

namespace client {

using audio_client::alFormat;
using audio_client::AudioContext;
using audio_client::audioContext;
using audio_client::AudioPlayer;
using audio_client::SamplePlayer;

export class App {
public:
  App(osu::BeatmapSet set, osu::ModSet mods, bool headless, bool autoplay,
      std::filesystem::path skinPath = {})
      : fSet(std::move(set)), fMods(mods), fHeadless(headless),
        fAutoplay(autoplay), fSkin(std::move(skinPath)) {}

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
  std::vector<osu::InputEvent> fAutoplayEvents;
  std::size_t fAutoplayIndex = 0;
  Skin fSkin;
  skia::Sp<skia::SkImage> fBackground;

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
  bool fKeyDown = false;
  bool fKeyWasDown = false;
  float fMouseX = 0.0f;
  float fMouseY = 0.0f;

  // Selection screen.
  enum class State { kSelecting, kPlaying };
  State fState = State::kSelecting;
  int fSelectedDifficulty = 0;
  bool fDifficultyConfirmed = false;
  bool fClickPending = false;
  int fHoveredDifficulty = -1;
  float fSelectScrollY = 0.0f;

  // Timing
  double fStartMs = 0.0;
  AudioPlayer fAudio;
  std::unordered_map<std::string, SamplePlayer> fSamples;
  std::size_t fPlayedEvents = 0;
  int fCombo = 0;

  // Font
  skia::SkFont fFont;

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
  static constexpr double kCursorTrailLifetime = 80.0;
  static constexpr std::size_t kCursorTrailMax = 16;

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

  // Combo color group for each object.
  osu::ComboInfo fComboInfo;

  void loadComboInfo() { fComboInfo = osu::buildComboInfo(*fMap); }

  void startGameplay(const osu::BeatmapInfo &info) {
    fMap.emplace(client::loadBeatmap(fSet, info));
    fEngine.emplace(*fMap, fMods);
    this->loadComboInfo();
    fSkin.setComboColors(fMap->fComboColors);
    if (fAutoplay) {
      fAutoplayEvents = osu::buildAutoplay(*fMap, fMods);
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
      }
    }

    fStartMs = glfw::glfwGetTime() * 1000.0;
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
    if (fWindow == nullptr) {
      glfw::glfwTerminate();
      return 1;
    }
    glfw::glfwSetInputMode(fWindow, glfw::kCursor, glfw::kCursorNormal);

    glfw::glfwSetWindowUserPointer(fWindow, this);
    glfw::glfwSetKeyCallback(
        fWindow, [](glfw::GLFWwindow *w, int key, int, int action, int) {
          auto *self = static_cast<App *>(glfw::glfwGetWindowUserPointer(w));
          if (self == nullptr)
            return;
          self->onKey(key, action);
        });
    glfw::glfwSetMouseButtonCallback(
        fWindow, [](glfw::GLFWwindow *w, int button, int action, int) {
          auto *self = static_cast<App *>(glfw::glfwGetWindowUserPointer(w));
          if (self == nullptr)
            return;
          self->onMouseButton(button, action);
        });
    glfw::glfwSetCursorPosCallback(
        fWindow, [](glfw::GLFWwindow *w, double x, double y) {
          auto *self = static_cast<App *>(glfw::glfwGetWindowUserPointer(w));
          if (self == nullptr)
            return;
          self->onCursorMove(static_cast<float>(x), static_cast<float>(y));
        });
    glfw::glfwSetScrollCallback(
        fWindow, [](glfw::GLFWwindow *w, double, double y) {
          auto *self = static_cast<App *>(glfw::glfwGetWindowUserPointer(w));
          if (self == nullptr)
            return;
          self->onScroll(static_cast<float>(y));
        });
    glfw::glfwSetFramebufferSizeCallback(
        fWindow, [](glfw::GLFWwindow *w, int width, int height) {
          auto *self = static_cast<App *>(glfw::glfwGetWindowUserPointer(w));
          if (self == nullptr)
            return;
          self->resize(width, height);
        });

    glfw::glfwMakeContextCurrent(fWindow);
    glfw::glfwSwapInterval(1);

    if (!this->initSkia()) {
      glfw::glfwDestroyWindow(fWindow);
      glfw::glfwTerminate();
      return 1;
    }

    fFont = this->loadFont(20.0f);
    this->resize(fScreenW, fScreenH);

    int selected = 0;
    if (fSet.fBeatmaps.size() > 1) {
      selected = this->runDifficultySelect();
      if (selected < 0 || selected >= static_cast<int>(fSet.fBeatmaps.size())) {
        return 0;
      }
    }

    glfw::glfwSetInputMode(fWindow, glfw::kCursor, glfw::kCursorHidden);
    this->startGameplay(fSet.fBeatmaps[static_cast<std::size_t>(selected)]);
    return this->runGameplayLoop();
  }

  void onKey(int key, int action) {
    if (fState == State::kSelecting) {
      if (action == glfw::kPress) {
        if (key == glfw::kKeyEscape) {
          glfw::glfwSetWindowShouldClose(fWindow, glfw::kTrue);
        } else if (key == glfw::kKeyUp || key == glfw::kKeyLeft) {
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

    if (action == glfw::kPress) {
      if (key == glfw::kKeyF11) {
        this->toggleFullscreen();
      } else if (key == glfw::kKeyEscape) {
        glfw::glfwSetWindowShouldClose(fWindow, glfw::kTrue);
      }
    }
    if (action == glfw::kPress || action == glfw::kRepeat) {
      if (key == glfw::kKeyZ || key == glfw::kKeyX || key == glfw::kKeySpace) {
        fKeyDown = true;
      }
    } else if (action == glfw::kRelease) {
      if (key == glfw::kKeyZ || key == glfw::kKeyX || key == glfw::kKeySpace) {
        fKeyDown = false;
      }
    }
  }

  void onMouseButton(int button, int action) {
    if (fState == State::kSelecting) {
      if (button == glfw::kMouseButtonLeft && action == glfw::kPress) {
        fClickPending = true;
      }
      return;
    }
    if (button == glfw::kMouseButtonLeft || button == glfw::kMouseButtonRight) {
      fKeyDown = (action == glfw::kPress || action == glfw::kRepeat);
    }
  }

  void onCursorMove(float sx, float sy) {
    fMouseX = sx;
    fMouseY = sy;
    if (fState == State::kPlaying) {
      fCursor = this->toPlayfield(sx, sy);
    }
  }

  void onScroll(float delta) {
    if (fState == State::kSelecting) {
      fSelectScrollY -= delta * 30.0f;
    }
  }

  [[nodiscard]] int runDifficultySelect() {
    fState = State::kSelecting;
    this->loadSelectBackground();

    while (!glfw::glfwWindowShouldClose(fWindow) && !fDifficultyConfirmed) {
      glfw::glfwPollEvents();
      this->updateSelectHover();
      this->renderDifficultySelect();
      fContext->flushAndSubmit(fSurface.get());
      glfw::glfwSwapBuffers(fWindow);
    }

    if (glfw::glfwWindowShouldClose(fWindow)) {
      return -1;
    }
    return fSelectedDifficulty;
  }

  [[nodiscard]] int runGameplayLoop() {
    fState = State::kPlaying;

    while (!glfw::glfwWindowShouldClose(fWindow) &&
           !this->shouldStop(this->nowMs())) {
      glfw::glfwPollEvents();
      const double now = this->nowMs();
      this->handleInput(now);
      fEngine->advance(now);
      this->playHitsounds(now);
      this->render();
      fContext->flushAndSubmit(fSurface.get());
      glfw::glfwSwapBuffers(fWindow);
    }

    this->printResult();
    return 0;
  }

  void loadSelectBackground() {
    for (const auto &info : fSet.fBeatmaps) {
      if (!info.fMeta.fBackground.empty()) {
        const auto bytes = fSet.findFile(info.fMeta.fBackground);
        if (!bytes.empty()) {
          fBackground = loadImage(bytes);
          return;
        }
      }
    }
    fBackground.reset();
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
    canvas->clear(skia::kBlack);
    this->drawBackground(canvas);

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
    fScreenW = w;
    fScreenH = h;
    if (fWindow != nullptr) {
      glfw::glfwGetFramebufferSize(fWindow, &fScreenW, &fScreenH);
    }
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
  }

  void toggleFullscreen() {
    if (fWindow == nullptr)
      return;
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

  [[nodiscard]] double nowMs() const {
    if (fAudio.playing()) {
      return fAudio.positionSec() * 1000.0;
    }
    return glfw::glfwGetTime() * 1000.0 - fStartMs;
  }

  [[nodiscard]] bool shouldStop(double now) const {
    return fEngine->finished() && now > fMap->lastObjectEndTime() + 1000.0;
  }

  void handleInput(double now) {
    this->submitAutoplay(now);

    if (fCursor.fX != fLastCursor.fX || fCursor.fY != fLastCursor.fY) {
      fEngine->submit({now, fCursor, osu::InputAction::kMove});
      fLastCursor = fCursor;
    }
    if (fKeyDown && !fKeyWasDown) {
      fEngine->submit({now, fCursor, osu::InputAction::kPress});
    } else if (!fKeyDown && fKeyWasDown) {
      fEngine->submit({now, fCursor, osu::InputAction::kRelease});
    }
    fKeyWasDown = fKeyDown;
  }

  void submitAutoplay(double now) {
    while (fAutoplayIndex < fAutoplayEvents.size() &&
           fAutoplayEvents[fAutoplayIndex].fTime <= now) {
      fEngine->submit(fAutoplayEvents[fAutoplayIndex]);
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
    auto *canvas = fSurface->getCanvas();
    canvas->clear(skia::kBlack);

    this->drawBackground(canvas);

    canvas->save();
    canvas->translate(fOffsetX, fOffsetY);
    canvas->scale(fScale, fScale);

    this->drawPlayfield(canvas);
    const double now = this->nowMs();
    const double ar = fEngine->clockRate() > 0.0
                          ? fMap->fDiff.fAr * fEngine->clockRate()
                          : fMap->fDiff.fAr;
    const double cs = fMap->fDiff.fCs;
    const double od = fMap->fDiff.fOd;

    this->updateCursorTrail(now);
    this->drawFollowPoints(canvas, now, ar, cs);

    for (std::size_t i = 0; i < fMap->fObjects.size(); ++i) {
      this->drawObject(canvas, fMap->fObjects[i], i, now, ar, cs, od);
    }

    this->drawFadingObjects(canvas, now, ar, cs, od);
    this->drawHitBursts(canvas, now, cs);
    this->drawPopups(canvas, now, cs);
    this->drawCursorTrail(canvas, now);
    this->drawCursor(canvas);
    canvas->restore();

    this->drawHud(canvas, now);
  }

  void drawBackground(skia::SkCanvas *canvas) {
    if (!fBackground)
      return;
    const float sw = static_cast<float>(fScreenW);
    const float sh = static_cast<float>(fScreenH);
    const float iw = static_cast<float>(fBackground->width());
    const float ih = static_cast<float>(fBackground->height());
    if (iw <= 0.0f || ih <= 0.0f)
      return;

    const float scale = std::max(sw / iw, sh / ih);
    const float dw = iw * scale;
    const float dh = ih * scale;
    const float dx = (sw - dw) * 0.5f;
    const float dy = (sh - dh) * 0.5f;

    skia::SkPaint paint;
    paint.setAntiAlias(true);
    paint.setAlphaf(0.3f);
    canvas->drawImageRect(
        fBackground.get(), skia::SkRect::MakeXYWH(dx, dy, dw, dh),
        skia::SkSamplingOptions(skia::SkFilterMode::kLinear), &paint);
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
                     osu::SliderPath path = osu::SliderPath::from(o);
                     fSkin.drawSlider(canvas, o, index, path,
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
                osu::SliderPath path = osu::SliderPath::from(o);
                fSkin.drawSlider(
                    canvas, o, it->fIndex, path, fMap->sliderSpanDuration(o),
                    fMap->sliderTickDistance(o), now, cs, ar, od, o.fCombo,
                    fComboInfo.fIndices[it->fIndex], alpha, false);
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

  void drawCursorTrail(skia::SkCanvas *canvas, double now) {
    const float scale = 1.0f / fScale;
    for (const auto &p : fCursorTrail) {
      const double age = now - p.fTime;
      if (age > kCursorTrailLifetime)
        continue;
      const float alpha =
          static_cast<float>((1.0 - age / kCursorTrailLifetime) * 0.6);
      fSkin.drawCursorTrail(canvas, p.fPos, scale, alpha);
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

    skia::SkPaint paint;
    paint.setAntiAlias(true);

    // Combo counter (large, top-left).
    fFont.setSize(48.0f);
    paint.setColor(skia::kWhite);
    const std::string comboText =
        std::format("{:.0f}x", std::max(0.0, fDisplayCombo));
    canvas->drawString(comboText.c_str(), 20.0f, 60.0f, fFont, paint);

    // Score, accuracy, grade (top-center).
    fFont.setSize(22.0f);
    const std::string statsText =
        std::format("{:.0f}  {:.2f}%  {}", fDisplayScore,
                    std::clamp(fDisplayAccuracy, 0.0, 1.0) * 100.0,
                    osu::gradeString(osu::computeGrade(score)));
    canvas->drawString(statsText.c_str(), 20.0f, 90.0f, fFont, paint);

    // Difficulty / mods (top-right).
    fFont.setSize(16.0f);
    const std::string diffText = std::format(
        "CS:{:.1f} AR:{:.1f} OD:{:.1f} HP:{:.1f} {}", fMap->fDiff.fCs,
        fMap->fDiff.fAr, fMap->fDiff.fOd, fMap->fDiff.fHp, fEngine->mods());
    canvas->drawString(diffText.c_str(), 20.0f, 115.0f, fFont, paint);

    // Health bar (top).
    this->drawHealthBar(canvas, 0.0f, 0.0f, sw, 14.0f, now);

    // Judgement counts.
    fFont.setSize(16.0f);
    const std::string countsText =
        std::format("Great {}  Good {}  Meh {}  Miss {}", score.fGreat,
                    score.fGood, score.fMeh, score.fMiss);
    canvas->drawString(countsText.c_str(), 20.0f, 140.0f, fFont, paint);

    // Progress time.
    fFont.setSize(14.0f);
    paint.setAlphaf(0.7f);
    const std::string timeText = std::format("{:.1f}s", now / 1000.0);
    canvas->drawString(timeText.c_str(), sw - 80.0f, sh - 20.0f, fFont, paint);
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
      paint.setAntiAlias(true);

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

  void printResult() { std::println("{}", fEngine->score()); }

  [[nodiscard]] osu::Vec2 toPlayfield(float sx, float sy) const {
    return {(static_cast<double>(sx) - fOffsetX) / fScale,
            (static_cast<double>(sy) - fOffsetY) / fScale};
  }

  osu::Vec2 fLastCursor = osu::kPlayfieldCenter;
};

} // namespace client
