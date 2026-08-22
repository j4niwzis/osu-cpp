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
import client.replaycache;
import client.filter;
import client.loader;
import skiff.paint;
import client.palette;
import client.settings;
import client.settingspanel;
import client.overlays;
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
import skiff.widgets.button;
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

namespace delete_dialog_style {
struct Root;
struct Panel;
struct Column;
struct Prompt;
struct Title;
struct Detail;
struct Buttons;
struct Button;
} // namespace delete_dialog_style

struct DeleteDialogTheme {
  static constexpr auto styles =
      skiff::scene::makeStyleSheet()
          .rule(skiff::scene::select<skiff::nodes::Box,
                                     delete_dialog_style::Root>(),
                {.width = 1.0f,
                 .height = 1.0f,
                 .relativeSize = skiff::scene::Axes::kBoth,
                 .backgroundColour = skia::colorSetARGB(200, 8, 6, 12)})
          .rule(skiff::scene::select<skiff::nodes::Box,
                                     delete_dialog_style::Panel>(),
                {.anchor = skiff::scene::Anchor::kCentre,
                 .origin = skiff::scene::Anchor::kCentre,
                 .width = 560.0f,
                 .height = 240.0f,
                 .padding = skiff::scene::Margin::all(20.0f),
                 .cornerRadius = 12.0f,
                 .backgroundColour = client::palette::kBackground5})
          .rule(skiff::scene::select<skiff::nodes::FillFlow,
                                     delete_dialog_style::Column>(),
                {.width = 1.0f,
                 .relativeSize = skiff::scene::Axes::kX,
                 .autoSize = skiff::scene::Axes::kY})
          .rule(skiff::scene::select<skiff::nodes::Text,
                                     delete_dialog_style::Prompt>(),
                {.anchor = skiff::scene::Anchor::kTopCentre,
                 .origin = skiff::scene::Anchor::kTopCentre,
                 .maxWidth = 520.0f,
                 .alpha = 0.75f,
                 .colour = skia::kWhite,
                 .fontSize = 16.0f})
          .rule(skiff::scene::select<skiff::nodes::Text,
                                     delete_dialog_style::Title>(),
                {.anchor = skiff::scene::Anchor::kTopCentre,
                 .origin = skiff::scene::Anchor::kTopCentre,
                 .maxWidth = 520.0f,
                 .colour = skia::kWhite,
                 .fontSize = 20.0f,
                 .fontBold = true})
          .rule(skiff::scene::select<skiff::nodes::Text,
                                     delete_dialog_style::Detail>(),
                {.anchor = skiff::scene::Anchor::kTopCentre,
                 .origin = skiff::scene::Anchor::kTopCentre,
                 .maxWidth = 520.0f,
                 .alpha = 0.6f,
                 .colour = skia::kWhite,
                 .fontSize = 13.0f})
          .rule(skiff::scene::select<skiff::nodes::FillFlow,
                                     delete_dialog_style::Buttons>(),
                {.anchor = skiff::scene::Anchor::kBottomCentre,
                 .origin = skiff::scene::Anchor::kBottomCentre,
                 .autoSize = skiff::scene::Axes::kBoth})
          .rule(skiff::scene::select<skiff::widgets::Button,
                                     delete_dialog_style::Button>(),
                {.width = 240.0f, .height = 46.0f});
};

using audio_client::alFormat;
using audio_client::AudioContext;
using audio_client::audioContext;
using audio_client::AudioPlayer;
using audio_client::SamplePlayer;

export class App {
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
  glfw::GLFWwindow *fWindow = nullptr;
  skia::Sp<skia::GrDirectContext> fContext;
  // The frame's own bookkeeping: which surface it is drawing into, what it
  // has been told changed, what it may clip to, what has to reach the window,
  // and how many more frames are owed. Grouped so that the loop that uses all
  // of it can be lifted out of this class, which is the only reason a
  // function here cannot already live in a file of its own.
  struct Frame {
    skia::Sp<skia::SkSurface> fSurface;
    skia::Sp<skia::SkSurface> fWindowSurface; // the swap chain
    skia::Sp<skia::SkSurface> fRasterSurface; // Skia's own CPU target
    bool fDrewOnRaster = false;               // this frame went to the CPU one
    int fFrameSave = 0; // canvas save count taken while the damage clip is up
    bool fDrawing = false; // inside a frame: damage reported now is not
    std::vector<skia::SkIRect> fDamage;
    bool fFullDamage = true;
    const char *fFullDamageReason = "start";
    bool fDamageDrives = false; // damage that is worth a frame of its own
    std::vector<skia::SkIRect> fComputedClip; // what the frame would have used
    bool fComputedClipFull = true;
    std::vector<skia::SkIRect> fFrameClip;
    bool fFrameClipFull = true;
    std::vector<skia::SkIRect> fBlitRegions; // what to carry over; empty = all
    std::vector<std::vector<skia::SkIRect>> fBlitHistory;
    int fBufferAge = -1; // frames since this buffer last held a frame
    bool fBufferAgeAssumed = false; // ...or what we were told to believe
    bool fAgeReported = false;
    int fFramesOwed = 0;       // frames promised to something that just moved
    int fFullRepaintsOwed = 0; // buffers still holding an older screen
    double fWakeWall = 0.0;    // when a screen asked to be woken, or 0
    double fRedrawUntilWall = 0.0; // frames are drawn until at least this time
    double fLastDrawWall = 0.0;
    const char *fFrameReason = ""; // what the last frame was drawn for
  };
  Frame fFrame;
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
  // The last framebuffer size the window system reported, written by the
  // thread that owns the window and read by the one that draws. The resize
  // event carries the same numbers, but it goes through a queue that is
  // drained at the top of a frame: a resize arriving after that is not known
  // to the frame already being drawn, and that frame is the one that clips
  // into a buffer the server has just reallocated. This is the same fact
  // without the queue.
  std::atomic<std::uint64_t> fReportedSize{0};
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
  static constexpr std::size_t kMaxDamageRects = 3;
  bool fOverlayShown = false; // an overlay covered the screen last frame
  double fDamageLogWall = 0.0;
  // What the client says about itself when asked: the frame cost
  // counters, the damage overlay and the traces. Nothing that draws
  // reads any of it.
  struct Diagnostics {
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
    const bool fForceShowDamage = std::getenv("OSU_SHOW_DAMAGE") != nullptr;
    const bool fTraceRepaint = std::getenv("OSU_TRACE_REPAINT") != nullptr ||
                               std::getenv("OSU_TRACE_RESIZE") != nullptr;
    bool fTracedClipping = false;
    int fTracedAge = -2;
    std::size_t fTracedHistory = 0;
  };
  Diagnostics fDiag;
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
  std::mutex fDropMutex;             // guards fDropped
  std::vector<std::string> fDropped; // files dropped onto the window
  client::songselect::Footer fSelectFooter;
  bool fConfirmDelete = false; // the deletion dialog is up
  // Built as a scene tree rather than drawn by hand: the first screen on the
  // retained renderer, and the pattern the rest follow.
  std::unique_ptr<skiff::scene::Drawable> fConfirmScene;

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
  struct ReplayFile {
    std::filesystem::path fPath;
    std::string fLabel;
    osu::ReplayScore fScore; // from the .osr header, via the index
    std::string fGrade;
    bool fHasScore = false;
    int fRules = -1;            // what it was recorded under, -1 if not ours
    bool fLegacyFormat = false; // no seed frame: old rules, no choice
  };
  bool fReplayListOpen = false;
  // Which rules the replay about to be watched plays under. Set from the
  // index when the selection moves -- a replay recorded here remembers what
  // it was played under -- and then it is the user's to flip.
  bool fReplayLegacyRules = false;
  std::vector<ReplayFile> fReplays;
  std::string fReplayFilter; // md5 the list was built for
  client::ReplayIndex fReplayIndex;
  client::results::Panels fPanels;
  client::results::Actions fResultActions;
  // Which replay each panel stands for; -1 is the score in hand, which has
  // no file. The strip knows nothing about replays, only about scores.
  std::vector<int> fPanelEntries;

  // Pause / results overlays.
  struct MenuButton {
    skia::SkRect fRect;
    std::string fLabel;
    skia::SkColor fAccent;
  };
  std::vector<MenuButton> fReplayButtons; // rebuilt with the replay overlay
  double fPolledCursorX = -1.0, fPolledCursorY = -1.0; // wasm cursor polling
  std::atomic<bool> fRefreshRequested{false}; // set by the event thread
  std::atomic<int> fWindowX{0}, fWindowY{0};  // where the window sits
  std::atomic<int> fWorkAreaX{0}, fWorkAreaY{0}, fWorkAreaW{0}, fWorkAreaH{0};
  client::pause::PauseMenu fPauseMenu;
  bool fRetryPending = false;
  int fPlayingSet = -1;
  int fPlayingDiff = -1;

  struct ResultData {
    osu::ScoreState fScore{};
    double fMean = 0.0;
    double fUr = 0.0;
    std::string fGrade = "F";
    double fPp = 0.0;
  };
  ResultData fResult;

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
  int fHotReplayPanel = -1;
  // Defined further down, next to the code that steps it; a unique_ptr only
  // needs the type complete where it is destroyed, which is the end of this
  // class.
  struct ExportJob;
  std::unique_ptr<ExportJob> fExportJob; // a video being rendered, in slices

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
    this->oweFrames(2);
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
  std::unordered_map<std::string, SamplePlayer> fSamples;
  std::size_t fPlayedEvents = 0;
  int fCombo = 0;

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
      rules = fReplayLegacyRules ? osu::RuleSet::kLegacyClient
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
    fWin.fScreenW = mode->width;
    fWin.fScreenH = mode->height;
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
      fWindow = glfw::glfwCreateWindow(fWin.fScreenW, fWin.fScreenH,
                                       "osu_client", monitor, nullptr);
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
    glfw::glfwSetWindowRefreshCallback(fWindow, [](glfw::GLFWwindow *w) {
      auto *self = static_cast<App *>(glfw::glfwGetWindowUserPointer(w));
      if (self != nullptr) {
        self->noteWindowPlacement();
        self->fRefreshRequested.store(true, std::memory_order_release);
      }
    });
    // Where the window is, tracked from the thread that is allowed to ask:
    // what has to be repainted after an expose is the part of the window a
    // screen is actually showing.
    glfw::glfwSetWindowPosCallback(fWindow, [](glfw::GLFWwindow *w, int, int) {
      auto *self = static_cast<App *>(glfw::glfwGetWindowUserPointer(w));
      if (self != nullptr) {
        self->noteWindowPlacement();
      }
    });
    this->noteWindowPlacement();
    glfw::glfwSetMouseButtonCallback(fWindow, [](glfw::GLFWwindow *w,
                                                 int button, int action, int) {
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
          self->enqueue(
              {App::wallMs(), EventType::kScroll, 0, 0, static_cast<float>(y)});
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
          self->fReportedSize.store(App::packSize(width, height),
                                    std::memory_order_release);
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
    skiff::nodes::Text::setFont(&fFont);
    fDisplayFont = this->loadDisplayFont(20.0f);
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
    skiff::nodes::Text::setFont(&fFont);
    fDisplayFont = this->loadDisplayFont(20.0f);
    this->resize(fWin.fScreenW, fWin.fScreenH);

    this->initLibrary();
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
    if (resized && (width != fWin.fScreenW || height != fWin.fScreenH)) {
      this->resize(width, height);
    }

    // And the size the server has this instant, which is not the same thing.
    // A resize reaches this thread through glfwPollEvents on another one, and
    // every frame drawn in between is drawn into a buffer that is already the
    // new size and has never been painted -- so a frame that clips to what
    // moved leaves the rest of it black. Asking here, before the surfaces are
    // chosen, means such a frame recreates them instead of drawing into the
    // old ones.
    //
    // A round trip to the X server per frame. It is a local socket and the
    // alternative was two days of this.
    int liveW = 0;
    int liveH = 0;
    if (present::surfaceSize(fWindow, &liveW, &liveH) &&
        (liveW != fWin.fScreenW || liveH != fWin.fScreenH)) {
      this->resize(liveW, liveH);
    }
  }

  [[nodiscard]] double eventGameTime(double wallMs) {
    // The map's timeline does not exist until a frame of it has been shown,
    // and input is drained before the frame. Dating an event from the clock
    // in that window reads an anchor left over from the last map -- or, on
    // the first play of a session, from process start -- so a single pointer
    // movement reaches the engine stamped minutes in, and advance() walks the
    // whole map and misses every object in it.
    if (fPlay.fAwaitingFirstFrame) {
      return 0.0;
    }
#ifdef __EMSCRIPTEN__
    return wallMs - fPlay.fStartMs;
#else
    return fPlay.fClock.sample(wallMs);
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
      fWin.fMouseX = ev.fX;
      fWin.fMouseY = ev.fY;
      if (fSettingsPanel.dragging()) {
        this->dragSetting(ev.fX);
      }
      if (fFilter.dragging()) {
        this->dragFilterRange(ev.fX);
      }
      if (fPanels.dragging()) {
        fPanels.drag(ev.fX);
      }
      if (fState == State::kPlaying && !fAutoplay) {
        fCursor = this->cursorFromEvent(ev);
        const double at = this->eventGameTime(ev.fWallMs);
        this->submitTimed({at, fCursor, osu::InputAction::kMove});
        // The trail is fed from the events, as it already is for a replay.
        // Taking one point per frame instead meant its shape was drawn from
        // however many frames the machine managed -- three points across a
        // whole trail at twenty a second.
        fView.addTrailPoint(fCursor, at);
      }
      break;
    case EventType::kScroll:
      if (fSettingsPanel.open()) {
        this->scrollSettings(ev.fX);
        break;
      }
      if (this->panelListActive()) {
        fPanels.scrollBy(ev.fX * 120.0f);
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
        fLibrary.markDirty();
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
      if (const auto requested = fMainMenu.key(menuKey(key))) {
        this->menuAction(*requested);
      }
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
    const int nSets = static_cast<int>(fLibrary.visible().size());
    if (key == glfw::kKeyEscape) {
      if (!fFilter.text().empty()) {
        fFilter.clearText();
        fLibrary.markDirty();
        return;
      }
      this->switchState(State::kMainMenu);
      return;
    }
    if (key == glfw::kKeyBackspace) {
      if (!fFilter.text().empty()) {
        fFilter.popText();
        fLibrary.markDirty();
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
    const int nDiffs =
        static_cast<int>(fLibrary.infosFor(fLibrary.selSet()).size());
    if (key == glfw::kKeyUp) {
      if (fLibrary.selDiff() > 0) {
        --fLibrary.selDiff();
      } else if (const int pos = fLibrary.visiblePos(); pos > 0) {
        fLibrary.selSet() =
            fLibrary.visible()[static_cast<std::size_t>(pos - 1)];
        fLibrary.selDiff() = std::max(
            0,
            static_cast<int>(fLibrary.infosFor(fLibrary.selSet()).size()) - 1);
      }
    } else if (key == glfw::kKeyDown) {
      if (fLibrary.selDiff() + 1 < nDiffs) {
        ++fLibrary.selDiff();
      } else if (const int pos = fLibrary.visiblePos();
                 pos >= 0 &&
                 pos + 1 < static_cast<int>(fLibrary.visible().size())) {
        fLibrary.selSet() =
            fLibrary.visible()[static_cast<std::size_t>(pos + 1)];
        fLibrary.selDiff() = 0;
      }
    } else if (key == glfw::kKeyLeft) {
      if (const int pos = fLibrary.visiblePos(); pos > 0) {
        fLibrary.selSet() =
            fLibrary.visible()[static_cast<std::size_t>(pos - 1)];
        fLibrary.selDiff() = 0;
      }
    } else if (key == glfw::kKeyRight) {
      if (const int pos = fLibrary.visiblePos();
          pos >= 0 && pos + 1 < static_cast<int>(fLibrary.visible().size())) {
        fLibrary.selSet() =
            fLibrary.visible()[static_cast<std::size_t>(pos + 1)];
        fLibrary.selDiff() = 0;
      }
    } else if (key == glfw::kKeyEnter) {
      this->startPlay(fLibrary.selSet(), fLibrary.selDiff());
    }
  }

  void openDownloads() {
    fSwallowChar = true;
    this->switchState(State::kDownload);
    if (fMirrors.results().empty() && !fMirrors.searching()) {
      fMirrors.startSearch(
          fListing.filters()); // the listing opens on results, not a blank page
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
      fMirrors.stopPreview();
      fMirrors.restoreMusic();
      this->switchState(State::kSongSelect);
      return;
    }
    if (key == glfw::kKeyEnter) {
      fMirrors.startSearch(fListing.filters());
      return;
    }
    if (key == glfw::kKeyBackspace) {
      this->popUtf8(fListing.filters().fQuery);
      fListing.queryEdited();
    }
  }

  void keyReplayList(int key) {
    if (key == glfw::kKeyLeft && fPanels.selected() > 0) {
      fPanels.select(fPanels.selected() - 1);
    } else if (key == glfw::kKeyRight &&
               fPanels.selected() + 1 <
                   static_cast<int>(fPanelEntries.size())) {
      fPanels.select(fPanels.selected() + 1);
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
            this->panelListClick(fWin.fMouseX, fWin.fMouseY, pressed)) {
          return;
        }
        if (pressed) {
          this->clickAt(fWin.fMouseX, fWin.fMouseY);
        } else {
          this->settingsClick(fWin.fMouseX, fWin.fMouseY, false);
          this->filterClick(fWin.fMouseX, fWin.fMouseY, false);
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
      // The strip handled anything over it; the actions below it are ours,
      // and they are the rules toggle then export, in that order.
      for (std::size_t i = 0; i < fReplayButtons.size(); ++i) {
        if (!fReplayButtons[i].fRect.contains(x, y)) {
          continue;
        }
        if (i == 0) {
          this->toggleReplayRules();
        } else {
          this->exportSelectedReplay();
        }
        return;
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
    case State::kMainMenu:
      if (const auto requested = fMainMenu.click(x, y)) {
        this->menuAction(*requested);
        return;
      }
      break;
    case State::kSongSelect:
      if (this->filterClick(x, y, true)) {
        return;
      }
      if (this->selectFooterClick(x, y)) {
        return;
      }
      if (const auto hit = fCarousel.click(x, y); hit.fHit) {
        if (hit.fDiff < 0) {
          fLibrary.selSet() = hit.fSet;
          fLibrary.selDiff() = 0;
        } else if (fLibrary.selSet() == hit.fSet &&
                   fLibrary.selDiff() == hit.fDiff) {
          this->startPlay(hit.fSet, hit.fDiff); // second click plays
        } else {
          fLibrary.selSet() = hit.fSet;
          fLibrary.selDiff() = hit.fDiff;
        }
        return;
      }
      break;
    case State::kDownload: {
      if (fSetPage.open()) {
        const auto page = fSetPage.click(x, y);
        using PageAction = client::setpage::SetPage::Action;
        if (page.fAction == PageAction::kDownload) {
          fMirrors.startDownloadForSet(fSetPage.setId());
        } else if (page.fAction == PageAction::kPreview) {
          fMirrors.togglePreviewForSet(fSetPage.setId());
        }
        return; // the page covers the listing underneath
      }
      const auto result = fListing.click(x, y);
      switch (result.fAction) {
      case client::listing::Listing::Action::kSearch:
        fMirrors.startSearch(fListing.filters());
        break;
      case client::listing::Listing::Action::kDownload:
        fMirrors.startDownload(result.fIndex);
        break;
      case client::listing::Listing::Action::kOpen:
        if (result.fIndex < fMirrors.results().size()) {
          fSetPage.show(fMirrors.results()[result.fIndex]);
          fMirrors.requestPageCover(result.fIndex);
        }
        break;
      case client::listing::Listing::Action::kPreview:
        fMirrors.togglePreview(result.fIndex);
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
      this->applyResultAction(fResultActions.click(x, y));
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

  void applyResultAction(client::results::Action action) {
    using Action = client::results::Action;
    switch (action) {
    case Action::kRetry:
      this->retry();
      break;
    case Action::kBack:
      this->quitToSelect();
      break;
    case Action::kExport:
      fExportDialog.show();
      fReplayListOpen = false; // one overlay at a time
      break;
    case Action::kToggleRules:
      this->toggleReplayRules();
      break;
    case Action::kNone:
      break;
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
    // startGameplay populated fPlay.fEngine). Dereferencing the empty optional
    // was the SIGILL in applyButton.
    if (!fPlay.fEngine) {
      return;
    }
    fPlay.fEngine->submit(ev);
    // Always record: the events are a few bytes each, and the autosave
    // setting decides afterwards whether they are kept. Gating the recording
    // itself on --record meant automatic replays were always empty.
    fPlay.fRecordedEvents.push_back(ev);
  }

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

  // A frame is only drawn when there is a reason to: an event arrived,
  // something is animating, a transfer is running, or the safety interval
  // elapsed. A menu nobody is touching costs a poll and a sleep, which is
  // how a compositor treats a window that has not damaged itself.
  void requestRedraw(double durationMs = 0.0) {
    fFrame.fRedrawUntilWall =
        std::max(fFrame.fRedrawUntilWall, wallMs() + durationMs);
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
  void oweFrames(int frames) {
    fFrame.fFramesOwed = std::max(fFrame.fFramesOwed, frames);
  }

  // A screen that knows when it next changes by itself -- a caret blinking on
  // its own clock -- says so, and sleeps until then instead of keeping frames
  // coming in the hope of catching the moment.
  void wakeAt(double wall) {
    fFrame.fWakeWall =
        fFrame.fWakeWall <= 0.0 ? wall : std::min(fFrame.fWakeWall, wall);
  }

  // Damage marked while a frame is being drawn says what to repaint; it does
  // not ask for another frame. That distinction is what lets a screen repaint
  // itself whole every time it draws -- most of them still do -- without that
  // becoming a reason to draw again, for ever. Damage marked outside drawing,
  // by an event or by work finishing in the background, does ask.

  // The whole screen has to be repainted: a screen changed, the window
  // resized, or something that does not report its bounds moved.
  // `buffersGone` says the window's buffers were thrown away and remade, not
  // merely repainted. Buffer age describes the contents of a buffer that
  // survived a frame; after a reallocation there are no such buffers, and the
  // history that would have covered them has just been cleared, so the ages
  // that come back describe buffers this client has never drawn into. Trusting
  // them there is what left everything but the live region black during a
  // window drag.
  void damageAll(const char *reason = "unspecified", bool buffersGone = false) {
    fFrame.fFullDamage = true;
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
    //
    // An assumed age is not a real one, and this is where treating it as one
    // hurts: the assumption cannot be zero, so a buffer whose contents are
    // undefined can never say so, and a chain deeper than the assumption
    // hands out buffers this client has never drawn a whole frame into.
    // Asserting an age of one on a driver that does not copy on swap then
    // shows the screen black everywhere but the region that moved -- not
    // after a resize, not after anything in particular, just whenever one of
    // those buffers comes round. Owing a full repaint per buffer costs six
    // frames at a screen change and takes the failure away.
    const bool reportedAge =
        fFrame.fBufferAge >= 0 && !fFrame.fBufferAgeAssumed;
    fFrame.fFullRepaintsOwed =
        (!buffersGone && reportedAge) ? 0 : kFullRepaintsAfterChange;
    if (!fFrame.fDrawing) {
      fFrame.fDamageDrives = true;
      this->oweFrames(kFullRepaintsAfterChange + 1);
    }
    fFrame.fFullDamageReason = reason;
    fFrame.fDamage.clear();
  }

  // A region did. Rounded outwards, because a rectangle that is a pixel too
  // small leaves a seam behind.
  void damage(const skia::SkRect &rect) {
    if (fFrame.fFullDamage || rect.isEmpty()) {
      return;
    }
    if (!fFrame.fDrawing) {
      fFrame.fDamageDrives = true;
    }
    skia::SkIRect area = rect.roundOut();
    area.outset(2, 2);

    // Merge into a rectangle it already touches, so a widget that reports
    // itself every frame does not add a new entry every frame.
    for (auto &existing : fFrame.fDamage) {
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
    for (auto &existing : fFrame.fDamage) {
      skia::SkIRect merged = existing;
      merged.join(area);
      const auto mergedArea =
          static_cast<std::int64_t>(merged.width()) * merged.height();
      const auto separate =
          static_cast<std::int64_t>(existing.width()) * existing.height() +
          static_cast<std::int64_t>(area.width()) * area.height();
      if (mergedArea <= separate * 5 / 4) {
        existing = merged;
        return;
      }
    }
    if (fFrame.fDamage.size() < kMaxDamageRects) {
      fFrame.fDamage.push_back(area);
      return;
    }

    // Full: fold it into whichever rectangle grows least by taking it, which
    // keeps the list short without swallowing the screen.
    std::size_t best = 0;
    std::int64_t bestCost = std::numeric_limits<std::int64_t>::max();
    for (std::size_t i = 0; i < fFrame.fDamage.size(); ++i) {
      skia::SkIRect merged = fFrame.fDamage[i];
      merged.join(area);
      const auto cost =
          static_cast<std::int64_t>(merged.width()) * merged.height() -
          static_cast<std::int64_t>(fFrame.fDamage[i].width()) *
              fFrame.fDamage[i].height();
      if (cost < bestCost) {
        bestCost = cost;
        best = i;
      }
    }
    fFrame.fDamage[best].join(area);
  }

  // Clips the frame to what changed. Everything draws exactly as it would
  // have -- the screens repaint from state, so a clipped repaint of a region
  // is the same pixels -- only the work outside the clip is skipped.
  void beginFrame() {
    fFrame.fDrawing = true;
    fFrame.fBlitRegions.clear(); // empty means the whole surface goes over
    // How old the contents of the buffer being drawn into are, from the
    // window system rather than from a constant of mine. -1 when nobody will
    // say, which is when the constants come back.
    fFrame.fBufferAge = this->partialRedraw() ? present::bufferAge() : -1;
    // Reported, before anything below asserts one. Every question about what
    // the window's buffers hold has to be answerable as "nobody said", and
    // this is the only place that knows.
    fFrame.fAgeReported = fFrame.fBufferAge >= 0;
    fFrame.fBufferAgeAssumed = false;
    // Nobody will say, but the answer may still be knowable: a driver that
    // swaps by copying leaves the back buffer holding the last frame, which
    // is an age of one, and a repaint of this frame's damage alone. Asserted
    // rather than guessed, because getting it wrong looks like smearing.
    if (fFrame.fBufferAge < 0 && this->partialRedraw()) {
      const int assumed = fForcedBufferAge > 0 ? fForcedBufferAge
                                               : fSettings.choice("bufferage");
      if (assumed > 0) {
        fFrame.fBufferAge = assumed;
        fFrame.fBufferAgeAssumed = true;
      }
    }
    // Take the accumulator as this frame's damage and hand a fresh one to the
    // screens, which fill it in as they draw for the frame after this.
    //
    // The window is resized by the server; the event saying so reaches this
    // thread through a queue. Every frame in between is drawn into a buffer
    // that has already been reallocated at the new size -- and a buffer that
    // has just been reallocated holds nothing, so a frame clipped to what
    // moved leaves the rest of it black. That is the flicker: not the frames
    // after a size event, which repaint whole and always did, but the ones
    // before it arrives.
    //
    // Nothing in the damage bookkeeping can know that, because the whole of
    // it is downstream of the event. So the size is asked of the window
    // system directly, on the thread that is drawing, once a frame.
    // Second line: a resize that landed after the surfaces were chosen cannot
    // be acted on within this frame, but it can stop it clipping.
    const std::uint64_t reported =
        fReportedSize.load(std::memory_order_acquire);
    const auto windowW = static_cast<int>(reported >> 32);
    const auto windowH = static_cast<int>(reported & 0xffffffffu);
    if (windowW > 0 && windowH > 0 &&
        (windowW != fWin.fScreenW || windowH != fWin.fScreenH)) {
      fFrame.fBlitHistory.clear();
      fFrame.fFullDamage = true;
      fFrame.fFullDamageReason = "the window is not the size we were told";
    }

    // A full repaint still owed to a buffer that has not had one.
    if (fFrame.fFullRepaintsOwed > 0) {
      --fFrame.fFullRepaintsOwed;
      if (!fFrame.fFullDamage) {
        fFrame.fFullDamage = true;
        fFrame.fFullDamageReason = "buffer has not had this screen yet";
      }
    }
    // A window being dragged by its corner does not stop reallocating buffers
    // when the last size event arrives, and it reallocates them on frames
    // where the size came back the same -- which is a frame resize() is not
    // called for, so nothing clears the blit history and nothing owes a full
    // repaint. The frame then clips to what moved and lands in a buffer that
    // has never been drawn into, which is the whole screen black but the live
    // region. Counting frames cannot cover that, because the frames are not
    // the thing that ends: the drag is. So the settle is in wall time, and
    // every frame inside it repaints whole.
    if (wallMs() - fLastResizeWall < kResizeSettleMs) {
      if (!fFrame.fFullDamage) {
        fFrame.fFullDamage = true;
        fFrame.fFullDamageReason = "resize settling";
      }
    }
    // What the frame repaints. A frame drawn with nothing marked repaints
    // everything -- the buffer being drawn into is several frames old and
    // there is no region to trust -- so it is reported as full, honestly.
    // The answer to those frames is not to relabel them: it is not to draw
    // them, which is what the owed-frame count above is for.
    if (!fFrame.fFullDamage && fFrame.fDamage.empty()) {
      fFrame.fFullDamageReason = "nothing marked itself";
    }
    fFrame.fComputedClipFull = fFrame.fFullDamage || fFrame.fDamage.empty();
    fFrame.fComputedClip = fFrame.fDamage;
    fFrame.fFrameClipFull = fFrame.fComputedClipFull;
    fFrame.fFrameClip.clear();
    if (!fFrame.fFrameClipFull) {
      fFrame.fFrameClip = fFrame.fDamage;
    }
    fFrame.fDamage.clear();
    fFrame.fFullDamage = false;
    fFrame.fDamageDrives = false;

    // Showing the regions means repainting everything: the outlines are put
    // on a back buffer the window will come back to, and cleaning them up
    // afterwards would have to land on that same buffer rather than on
    // whichever one is next. Repainting whole sidesteps that entirely -- the
    // tool costs frames while it is on, and in exchange it never lies or
    // leaves anything behind.
    if (this->showingDamage()) {
      fFrame.fFrameClipFull = true;
      fFrame.fFrameClip.clear();
    }
    this->rememberBlitRegion();

    // Every number the clip decision is made from, at the point it is made:
    // after the blit history has been updated for this frame, so the line
    // cannot disagree with what the frame then does. Printed when the answer
    // or the buffer age changes, and for the second after a size event.
    if (fDiag.fTraceRepaint) {
      const bool willClip = !fFrame.fComputedClipFull &&
                            this->partialRedraw() &&
                            !this->historyShorterThan(this->drawReach());
      const bool resizing = wallMs() - fLastResizeWall < 1000.0;
      if (willClip != fDiag.fTracedClipping ||
          fFrame.fBufferAge != fDiag.fTracedAge ||
          (fFrame.fBlitHistory.size() < fDiag.fTracedHistory) || resizing) {
        std::println(std::cerr,
                     "[repaint] {:8.0f} ms  age {}{}  reach {}  history {}  "
                     "{}  -> {}{}",
                     wallMs(), fFrame.fBufferAge,
                     fFrame.fBufferAgeAssumed ? " (assumed)" : "",
                     this->drawReach(), fFrame.fBlitHistory.size(),
                     fFrame.fComputedClipFull ? "whole screen" : "a region",
                     willClip ? "CLIPS" : "repaints whole",
                     fFrame.fComputedClipFull
                         ? std::string(" -- ") + fFrame.fFullDamageReason
                         : std::string());
      }
      fDiag.fTracedClipping = willClip;
      fDiag.fTracedAge = fFrame.fBufferAge;
      fDiag.fTracedHistory = fFrame.fBlitHistory.size();
    }

    if (!fFrame.fSurface) {
      return;
    }
    auto *canvas = fFrame.fSurface->getCanvas();
    fFrame.fFrameSave = canvas->save();

    // A full repaint paints every pixel only if every screen covers every
    // pixel, and the clipped path below is the only one that clears. That
    // holds while the surface being drawn into held the last frame: whatever
    // a screen leaves uncovered was already right. It stops holding the
    // moment the surface is new -- resize() drops the raster one and the
    // next frame allocates a fresh one, and a window buffer coming round for
    // the first time is fresh too. Then what a screen leaves uncovered is
    // not the last frame, it is nothing, and nothing is black.
    //
    // Only while there is reason to doubt the surface, which is the frames a
    // repaint is owed for and the settle after a size event. In the steady
    // state this would be a full-screen memset on every gameplay frame, paid
    // to cover a case that cannot happen there.
    if (fFrame.fComputedClipFull &&
        (fFrame.fFullRepaintsOwed > 0 ||
         wallMs() - fLastResizeWall < kResizeSettleMs)) {
      canvas->clear(skia::colorSetARGB(255, 0, 0, 0));
    }

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
        static_cast<std::int64_t>(fWin.fScreenW) * fWin.fScreenH;
    const std::int64_t boundsArea =
        static_cast<std::int64_t>(bounds.width()) * bounds.height();
    if (boundsArea * 2 > screenArea) {
      return;
    }
    canvas->clipIRect(bounds);
    // Remembered for the blit, which is a different question with a different
    // answer: what the window is missing rather than what this surface is.
    //
    // Only where the window system reports a buffer age. An assumed age is a
    // claim about the window's buffers that nothing has checked, and carrying
    // a region across on the strength of it is what leaves the screen black
    // around whatever moved: the server resizes the window between one frame
    // and the next, the buffer that comes back has never been painted, and a
    // frame that carries only its own damage into it fills in a rectangle and
    // leaves the rest as it found it. Blitting whole costs one window-sized
    // copy on a renderer that has the whole frame in memory anyway, and the
    // drawing above is still clipped, which is where the work actually is.
    if (fFrame.fAgeReported && !this->historyShorterThan(this->windowReach())) {
      const skia::SkIRect carry = this->damageOver(this->windowReach());
      if (!carry.isEmpty()) {
        fFrame.fBlitRegions.push_back(carry);
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
      this->damage(
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
      this->damageAll("screen fade");
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
      const skia::SkRect region = fModSelect.takeDamage();
      if (region.width() >= static_cast<float>(fWin.fScreenW)) {
        this->damageAll("mod select fading");
      } else {
        this->damage(region);
      }
    }
    if (fReplayListOpen) {
      if (fPanels.scrolling() || fPanels.movedSinceDrawn()) {
        // Dragged or gliding: a drag sets the position outright, so the
        // target says nothing about it. Where it was when it was last drawn
        // does.
        fPanels.noteDrawn();
        this->damageAll("replay browser moving");
      } else {
        const int hot = fPanels.hot(fWin.fMouseX, fWin.fMouseY);
        if (hot != fHotReplayPanel) {
          fHotReplayPanel = hot;
          this->damage(fPanels.band());
        }
      }
    }
    if (fExportDialog.open()) {
      fExportDialog.update(fFont, fWin.fScreenW, fWin.fScreenH, fWin.fMouseX,
                           fWin.fMouseY, wallMs());
      this->damage(fExportDialog.takeDamage());
    }

    // Overlays are drawn after the screen, over most of it, so appearance and
    // disappearance repaint the underlying screen once. Retained overlays
    // report their own damage between those two transitions.
    const bool overlay = fSettingsPanel.visible() || fModSelect.visible() ||
                         fExportDialog.open() || fReplayListOpen ||
                         fConfirmDelete || fSetPage.open();
    // Only while one is moving, or on the frame it appears or goes away: a
    // settled overlay is as static as the screen under it, and the screens do
    // mark what they change beneath it.
    if (overlay != fOverlayShown) {
      this->damageAll("overlay appeared or went away");
    } else if (fSettingsPanel.animating(wallMs())) {
      this->damageAll("overlay sliding");
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
    if (fConfirmDelete && fConfirmScene && fConfirmScene->animatingTree()) {
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

  // Why a frame is happening. needsFrame answers yes from sixteen places and
  // said which of them only to itself, so "it draws when it should not" and
  // "it does not draw when it should" were both unanswerable from outside.
  [[nodiscard]] bool frameBecause(const char *reason) {
    if (fDiag.fTraceRepaint && reason != fFrame.fFrameReason) {
      std::println(std::cerr, "[frame] {:8.0f} ms  drawn because {}", wallMs(),
                   reason);
    }
    fFrame.fFrameReason = reason;
    return true;
  }

  [[nodiscard]] bool needsFrame() {
    // Gameplay is a moving picture by definition, and so is anything with a
    // clock on screen.
    if (fState == State::kPlaying) {
      return this->frameBecause("gameplay"); // a moving picture by definition
    }
    // The results screen counts up, slides its panels and fades in, all of
    // which end. After that it is a still picture like any other.
    if (fState == State::kResults &&
        (wallMs() - fStateEnterWall < 2500.0 || fPanels.scrolling())) {
      return this->frameBecause("results settling");
    }
    // The logo tracks the music and the triangles drift, and either is a
    // reason to keep drawing. Neither is a reason to stop: everything below
    // -- an event, a panel sliding, something that marked itself -- still
    // applies, and returning an answer here rather than a reason took the
    // whole menu out of the conversation.
    if (fState == State::kMainMenu &&
        (fSettings.flag("visualiser") || fSettings.flag("menutriangles"))) {
      return this->frameBecause("menu visualiser or triangles");
    }
    // Something in the menu is part-way to where it is going: the buttons say
    // so through the tree, and the dim and the logo through the flag, since
    // neither of those is a node.
    if (fState == State::kMainMenu && fMainMenu.animating()) {
      return this->frameBecause("menu still easing");
    }
    if (fState == State::kPaused && fSettings.flag("pausetriangles")) {
      return this->frameBecause(
          "pause triangles"); // triangles drift inside the buttons, as lazer's
                              // do
    }
    if (fMirrors.searching() || fMirrors.previewPending() ||
        fMirrors.transferring()) {
      return this->frameBecause(
          "a search, preview or transfer"); // progress that is being watched
    }
    if (fMirrors.previewId() >= 0 || fExportDialog.open()) {
      return this->frameBecause(
          "a preview or the export dialog"); // a transfer or a dialog with live
                                             // status in it
    }
    // The replay browser's strip glides to the panel that was picked. It is
    // drawn as an overlay rather than as a screen, so it has nobody else to
    // ask for the frames that carry it there.
    if (fReplayListOpen && fPanels.scrolling()) {
      return this->frameBecause("replay strip gliding");
    }
    // Overlays are drawn while they transition in and out, and after that only
    // when something touches them -- which arrives as an event.
    if (fSettingsPanel.animating(wallMs()) || fModSelect.animating()) {
      return this->frameBecause("an overlay transitioning");
    }
    if (fConfirmDelete && fConfirmScene && fConfirmScene->animatingTree()) {
      return this->frameBecause("the confirm dialog");
    }
    // Scene trees know whether anything in them is still moving, which is the
    // one thing they cannot express as damage in advance.
    if (fState == State::kDownload &&
        (fListing.animating() || fSetPage.animating())) {
      return this->frameBecause("the download screen");
    }
    if (fState == State::kSongSelect && fCarousel.animating()) {
      return this->frameBecause("the carousel");
    }
    const double now = wallMs();
    if (fFrame.fFramesOwed > 0) {
      --fFrame.fFramesOwed;
      return this->frameBecause("frames owed");
    }
    if (fFrame.fWakeWall > 0.0 && now >= fFrame.fWakeWall) {
      fFrame.fWakeWall = 0.0;
      return this->frameBecause("a screen asked to be woken");
    }
    // Something marked itself outside a frame: an event handler, or work
    // that finished in the background. Damage marked while drawing only says
    // what to repaint when a frame next happens -- a screen that repaints
    // itself whole every time it draws would otherwise be asking for the next
    // frame, every frame, for ever.
    if (fFrame.fDamageDrives) {
      return this->frameBecause("something marked itself outside a frame");
    }
    if (now <= fFrame.fRedrawUntilWall) {
      return this->frameBecause("redraw window");
    }
    // Safety net: whatever the screens forgot to announce shows up within
    // this long rather than never.
    if (fDiag.fTraceRepaint && fState == State::kMainMenu &&
        now - fFrame.fLastDrawWall > 500.0) {
      std::println(std::cerr, "[menu] idle: animating {} dt {:.0f} ms",
                   fMainMenu.animating(), fUiDt);
    }
    return now - fFrame.fLastDrawWall > 500.0
               ? this->frameBecause("the half-second safety net")
               : false;
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
    if (areaW <= 0 || areaH <= 0 || fWin.fScreenW <= 0 || fWin.fScreenH <= 0) {
      return skia::SkIRect::MakeEmpty();
    }
    skia::SkIRect window =
        skia::SkIRect::MakeXYWH(x, y, fWin.fScreenW, fWin.fScreenH);
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
    if (const auto monitor = glfw::glfwGetPrimaryMonitor();
        monitor != nullptr) {
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
    fFrame.fDrawing = false;
    if (fRefreshRequested.exchange(false, std::memory_order_acquire)) {
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
    fDiag.fLastUpdateUs = std::chrono::duration_cast<std::chrono::microseconds>(
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
    fDiag.fFrameStart = std::chrono::steady_clock::now();

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
      this->damageAll("renderer changed", /*buffersGone=*/true);
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
  void notify(std::string text,
              skia::SkColor color = client::palette::kAccent2) {
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
    this->includeInBlit(box);
  }

  // Something drawn outside the frame's clip still has to be carried into the
  // window when the CPU renderer only carries over what it repainted.
  void includeInBlit(const skia::SkRect &rect) {
    if (fFrame.fBlitRegions.empty()) {
      return; // the whole surface is going over anyway
    }
    skia::SkIRect area = rect.roundOut();
    area.outset(2, 2);
    if (!area.intersect(skia::SkIRect::MakeWH(fWin.fScreenW, fWin.fScreenH))) {
      return;
    }
    fFrame.fBlitRegions.push_back(area);
  }

  void present() {
    const auto frameStart = fDiag.fFrameStart;
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
    if (fReplayListOpen) {
      this->drawReplayList(canvas);
    }
    if (fExportDialog.open()) {
      this->drawExportDialog(canvas);
    }
    this->drawDeleteConfirmation(canvas);
    this->drawToast(canvas);
    this->showDamage(canvas);
    canvas->restoreToCount(fFrame.fFrameSave);
    // Drawn after the clip is lifted: a small thing in the far corner that
    // changes every frame, so clipping to it would drag the repainted region
    // across the whole screen, and clipping it away would freeze it. It still
    // has to reach the window, which on the CPU renderer means saying so --
    // otherwise the counter is drawn into a surface whose corner is never
    // carried over, and the number stops.
    this->drawFpsCounter(canvas);
    fDiag.fBlitStart = std::chrono::steady_clock::now();

    // Everything about this frame, for the four hundred milliseconds after a
    // size event: what the client thinks the screen is, what the two surfaces
    // actually are, what was repainted and what is carried to the window.
    // Four fixes have been aimed at this from four theories and none of them
    // was measured; this is all of the state at once so the next one is not a
    // fifth theory.
    if (fDiag.fTraceRepaint && wallMs() - fLastResizeWall < 400.0) {
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
    this->reportFrameCost(frameStart, beforeSwap);
    fFrame.fDrawing = false;
  }

  // Where a frame goes, once a second, under OSU_SHOW_DAMAGE: drawing into
  // the surface, copying it to the window, and waiting for the swap. Guessing
  // at which of the three got slower has not worked so far.
  void reportFrameCost(std::chrono::steady_clock::time_point start,
                       std::chrono::steady_clock::time_point beforeSwap) {
    if (!fDiag.fForceShowDamage) {
      return;
    }
    const auto now = std::chrono::steady_clock::now();
    const auto us = [](auto from, auto to) {
      return std::chrono::duration_cast<std::chrono::microseconds>(to - from)
          .count();
    };
    fDiag.fCostUpdateUs += fDiag.fLastUpdateUs;
    fDiag.fCostDrawUs += us(start, fDiag.fBlitStart) - fDiag.fLastUpdateUs;
    fDiag.fCostBlitUs += us(fDiag.fBlitStart, beforeSwap);
    fDiag.fCostSwapUs += us(beforeSwap, now);
    fDiag.fCostVisited += skiff::scene::visitedCount();
    fDiag.fCostDrawn += skiff::scene::drawnCount();
    skiff::scene::visitedCount() = 0;
    skiff::scene::drawnCount() = 0;
    if (fFrame.fComputedClipFull) {
      fDiag.fCostClipArea +=
          static_cast<std::int64_t>(fWin.fScreenW) * fWin.fScreenH;
    } else if (!fFrame.fComputedClip.empty()) {
      for (const auto &rect : fFrame.fComputedClip) {
        fDiag.fCostClipArea +=
            static_cast<std::int64_t>(rect.width()) * rect.height();
      }
    }
    ++fDiag.fCostFrames;
    if (wallMs() - fDiag.fCostLogWall < 1000.0 || fDiag.fCostFrames == 0) {
      return;
    }
    // Named for what they are: settling the screen, drawing it (which on the
    // CPU renderer is where rasterisation happens too), handing the result to
    // the window, and waiting for the swap.
    std::println(
        std::cerr,
        "[frame] update {:.2f} ms, draw {:.2f} ms, blit {:.2f} ms, "
        "swap {:.2f} ms over {} frames, {} of {} drawables, "
        "{:.0f}% of the screen repainted{}",
        static_cast<double>(fDiag.fCostUpdateUs) / fDiag.fCostFrames / 1000.0,
        static_cast<double>(fDiag.fCostDrawUs) / fDiag.fCostFrames / 1000.0,
        static_cast<double>(fDiag.fCostBlitUs) / fDiag.fCostFrames / 1000.0,
        static_cast<double>(fDiag.fCostSwapUs) / fDiag.fCostFrames / 1000.0,
        fDiag.fCostFrames,
        fDiag.fCostDrawn / std::max<std::uint64_t>(1, fDiag.fCostFrames),
        fDiag.fCostVisited / std::max<std::uint64_t>(1, fDiag.fCostFrames),
        100.0 * static_cast<double>(fDiag.fCostClipArea) /
            std::max<double>(1.0, static_cast<double>(fDiag.fCostFrames) *
                                      fWin.fScreenW * fWin.fScreenH),
        std::string(fFrame.fDrewOnRaster ? " [cpu]" : " [gpu]") +
            (this->partialRedraw()
                 ? std::format(" (partial redraw, buffer age {} via {})",
                               fFrame.fBufferAge,
                               fFrame.fBufferAgeAssumed ? "assumption"
                                                        : present::backend())
                 : ""));
    fDiag.fCostLogWall = wallMs();
    fDiag.fCostUpdateUs = fDiag.fCostDrawUs = fDiag.fCostBlitUs =
        fDiag.fCostSwapUs = 0;
    fDiag.fCostVisited = fDiag.fCostDrawn = 0;
    fDiag.fCostClipArea = 0;
    fDiag.fCostFrames = 0;
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
  [[nodiscard]] static std::uint64_t packSize(int width, int height) {
    return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(width))
            << 32) |
           static_cast<std::uint32_t>(height);
  }

  static constexpr int kFullRepaintsAfterChange = 6;
  // How long after the last size event a window is still assumed to be
  // throwing its buffers away. The same quarter second the slider bodies wait
  // for, and for the same reason: that is how long a drag keeps arriving.
  static constexpr double kResizeSettleMs = 250.0;
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
    return fFrame.fBufferAge > 0 ? static_cast<std::size_t>(fFrame.fBufferAge)
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
    if (fFrame.fBufferAge == 0) {
      return true; // the window system says the contents are undefined
    }
    if (fFrame.fBlitHistory.size() < reach) {
      return true;
    }
    std::size_t seen = 0;
    for (auto frame = fFrame.fBlitHistory.rbegin();
         frame != fFrame.fBlitHistory.rend() && seen < reach; ++frame, ++seen) {
      if (frame->empty()) {
        return true; // that frame repainted everything
      }
    }
    return false;
  }

  [[nodiscard]] skia::SkIRect damageOver(std::size_t reach) const {
    skia::SkIRect bounds = skia::SkIRect::MakeEmpty();
    std::size_t seen = 0;
    for (auto frame = fFrame.fBlitHistory.rbegin();
         frame != fFrame.fBlitHistory.rend() && seen < reach; ++frame, ++seen) {
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
    fFrame.fBlitHistory.push_back(fFrame.fFrameClipFull
                                      ? std::vector<skia::SkIRect>{}
                                      : fFrame.fFrameClip);
    while (fFrame.fBlitHistory.size() > kBlitHistoryDepth) {
      fFrame.fBlitHistory.erase(fFrame.fBlitHistory.begin());
    }
  }

  // OSU_SHOW_DAMAGE=1 outlines what was repainted, which is the only way to
  // see whether a screen is reporting its damage honestly.
  [[nodiscard]] bool showingDamage() const {
    return fDiag.fForceShowDamage || fSettings.flag("damageoverlay");
  }

  void showDamage(skia::SkCanvas *canvas) {
    if (!this->showingDamage()) {
      return;
    }
    // Magenta around each region the frame would have been clipped to; a red
    // border when it would have repainted everything, with the reason,
    // because "why is it full again" is the question that keeps coming up.
    if (fFrame.fComputedClipFull) {
      skia::SkPaint paint;
      paint.setStyle(skia::kStrokeStyle);
      paint.setStrokeWidth(6.0f);
      paint.setColor(skia::colorSetARGB(255, 255, 40, 40));
      skia::SkRect border = skia::SkRect::MakeWH(
          static_cast<float>(fWin.fScreenW), static_cast<float>(fWin.fScreenH));
      border.inset(3.0f, 3.0f);
      canvas->drawRect(border, paint);
      // Deduplicated by reason rather than by the clock: a one-off -- a
      // screen change, a resize -- would otherwise be swallowed by whatever
      // repeating reason spoke first in that second.
      const bool sameReason = fDiag.fLoggedFullReason != nullptr &&
                              std::string_view(fDiag.fLoggedFullReason) ==
                                  std::string_view(fFrame.fFullDamageReason);
      if (!sameReason || wallMs() - fDamageLogWall > 1000.0) {
        fDamageLogWall = wallMs();
        fDiag.fLoggedFullReason = fFrame.fFullDamageReason;
        std::println(std::cerr, "[damage] would repaint everything: {}",
                     fFrame.fFullDamageReason);
      }
      return;
    }
    skia::SkPaint paint;
    paint.setStyle(skia::kStrokeStyle);
    paint.setStrokeWidth(2.0f);
    paint.setColor(skia::colorSetARGB(255, 255, 0, 255));
    for (const auto &rect : fFrame.fComputedClip) {
      skia::SkRect outline = skia::SkRect::Make(rect);
      outline.inset(1.0f, 1.0f);
      if (!outline.isEmpty()) {
        canvas->drawRect(outline, paint);
      }
    }
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
    this->playHitsounds(now);
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
    this->damageAll("slider bodies rebuilt");
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
    c.fPp = fPlay.fEngine ? this->pricePlay(fPlay.fEngine->score()) : 0.0;
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

  // What a score is worth against the map being played. Pure arithmetic over
  // the counts, so it is cheap enough to ask on every frame.
  [[nodiscard]] double pricePlay(const osu::ScoreState &sc) const {
    osu::ScoreInput input;
    input.fGreat = sc.fGreat;
    input.fOk = sc.fGood;
    input.fMeh = sc.fMeh;
    input.fMiss = sc.fMiss;
    input.fMaxCombo = sc.fMaxCombo;
    input.fSliderTailHits = sc.fTailHit;
    input.fLargeTickHits = sc.fLargeTickHit;
    return osu::performanceRanked(fPlay.fPlayAttributes, input).fTotal;
  }

  void captureResult() {
    fResult.fScore = fPlay.fEngine->score();
    double sum = 0.0;
    double sumSq = 0.0;
    std::size_t n = 0;
    for (const double d : fPlay.fEngine->tapDeltas()) {
      sum += d;
      sumSq += d * d;
      ++n;
    }
    if (n > 0) {
      const double mean = sum / static_cast<double>(n);
      fResult.fMean = mean;
      fResult.fUr =
          10.0 * std::sqrt(std::max(0.0, sumSq / static_cast<double>(n) -
                                             mean * mean));
    } else {
      fResult.fMean = 0.0;
      fResult.fUr = 0.0;
    }
    fResult.fGrade = osu::gradeString(osu::computeGrade(fResult.fScore));

    // What the play is worth. The counts come straight off the score: the
    // tails and ticks are the ones a performance calculation asks for, and
    // this client has them because it judges them.
    fResult.fPp = this->pricePlay(fResult.fScore);
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
                           visible ? glfw::kCursorNormal : glfw::kCursorHidden);
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
    fLibrary.configure(fLoader, fMapsDir, fThumbDir,
                       [this] { this->syncMapsDir(); });
    fReplayDir = fMapsDir.parent_path() / "replays";
    std::filesystem::create_directories(fThumbDir, ec);
    fLibrary.loadCache();
    fReplayIndex.load(fMapsDir.parent_path() / "replay-index.json");
    fReplayIndex.refresh(fReplayDir);
    fSettings.load(fMapsDir.parent_path() / "settings.json");
    // What the mirrors cannot do for themselves: the library they import
    // into, the corner they report to, and the music that steps aside for a
    // preview.
    fMirrors.configure({
                           [this](std::string text, skia::SkColor colour) {
                             this->notify(std::move(text), colour);
                           },
                           [this](long id) {
                             return fLibrary.libraryIndexForSet(
                                        static_cast<int>(id)) >= 0;
                           },
                           [this](const std::filesystem::path &path) {
                             return fLibrary.addOszToLibrary(
                                 path, true,
                                 client::parseQuery(fFilter.text()));
                           },
                           [this](int entry) { fListing.entryChanged(entry); },
                           [this] {
                             fListing.resetSortForSearch();
                             fListing.scrollToStart();
                           },
                           [this] { return this->musicGain(); },
                           [this] { return fAudio.playing(); },
                           [this] { fAudio.pause(); },
                           [this] { fAudio.resume(); },
                           [this] { this->syncMapsDir(); },
                       },
                       fMapsDir);
    fSwapIntervalRequest.store(fSettings.flag("vsync") ? 1 : 0,
                               std::memory_order_release);
    fAppliedDim = fSettings.value("dim");

    if (fHasInitialSet) {
      client::library::Entry entry;
      entry.fInfos = fSet.fBeatmaps;
      entry.fLoaded = std::make_shared<osu::BeatmapSet>(fSet);
      fLibrary.sets().push_back(std::move(entry));
      fLibrary.loadedOrder().push_back(0);
    }

    fLibrary.scanArchives();
    this->syncMapsDir();

    this->resortLibrary();
    fLibrary.markDirty();
    fLibrary.rebuildVisible(client::parseQuery(fFilter.text()));
    // Sets come out of the cache and off the disk in whatever order each was
    // written in; the difficulties within them are put in rating order here,
    // once, before anything selects one by position.
    fAppliedStarChoice = fSettings.choice("stars");
    fLibrary.setRanked(fAppliedStarChoice == 1);
    fLibrary.sortLibraryByStars();

    // Start on a random set so the menu isn't always greeted by the same
    // track (unless a specific beatmap was passed on the command line).
    if (!fHasInitialSet && !fLibrary.sets().empty()) {
      std::uniform_int_distribution<std::size_t> pick(
          0, fLibrary.sets().size() - 1);
      fLibrary.selSet() = static_cast<int>(pick(fUiRng));
      fLibrary.selDiff() = 0;
    }
    fLibraryLoaded = true;
    std::println(std::cerr, "[library] {} sets", fLibrary.sets().size());
  }

  // Loop the selected set's audio quietly under the menus, lazer-style. Only
  // reloads when the selection changes; stops when gameplay takes over.
  void updateMenuMusic() {
    if (fLibrary.sets().empty()) {
      return;
    }
    if (fMenuMusicForSet == fLibrary.selSet()) {
      // The track is given a moment to start before its silence counts as
      // having ended; OpenAL reports a source as stopped until it does.
      // Paused for a preview is not the same as finished.
      if (!fAudio.playing() && !fMirrors.ducked() &&
          wallMs() - fMenuTrackWall > 1000.0 && fState != State::kResults) {
        // The results screen belongs to the map that was just played, and
        // moving on from it would move the selection out from under the
        // player, who is going back to that map.
        this->nextMenuTrack();
      }
      return;
    }
    auto set = fLibrary.setFor(fLibrary.selSet());
    if (!set) {
      return; // still loading; try again next frame
    }
    fMenuMusicForSet = fLibrary.selSet();
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
    const int forSet = fLibrary.selSet();
    // The index alone is not identity: deleting a beatmap shifts everything
    // after it, so the path is checked too before this track is adopted.
    const auto forPath =
        fLibrary.sets()[static_cast<std::size_t>(fLibrary.selSet())].fPath;
    fLoader.submit(
        static_cast<std::uint64_t>(fLibrary.selSet()) | (3ull << 32),
        [copy = std::move(copy), ext, pcm] {
          *pcm = audio_client::decodeAudio(copy, ext);
        },
        [this, forSet, forPath, pcm] {
          if (forSet != fLibrary.selSet() || pcm->fSamples.empty()) {
            return; // selection moved on while decoding
          }
          if (forSet >= static_cast<int>(fLibrary.sets().size()) ||
              fLibrary.sets()[static_cast<std::size_t>(forSet)].fPath !=
                  forPath) {
            return; // that entry is not the one this was decoded for
          }
          fAudio.adopt(std::move(*pcm));
          fAudio.setLooping(false); // the next track is chosen when it ends
          fMenuTrackWall = wallMs();
          fAudio.setVolume(this->musicGain());
          fAudio.play();
          fMainMenu.trackChanged(wallMs());
        });
  }

  // Picks another map to listen to. Random, and never the one just heard as
  // long as there is anything else in the library.
  void nextMenuTrack() {
    this->requestRedraw(1500.0);
    if (fLibrary.visible().size() > 1) {
      // One draw, uniform over everything except the one just heard. Drawing
      // again until the draw differs is unbiased but can fail, and failing
      // eight times in a row meant playing the same track over again -- which
      // is the opposite of what this is for.
      std::size_t current = fLibrary.visible().size();
      for (std::size_t i = 0; i < fLibrary.visible().size(); ++i) {
        if (fLibrary.visible()[i] == fLibrary.selSet()) {
          current = i;
          break;
        }
      }
      const bool skipping = current < fLibrary.visible().size();
      std::uniform_int_distribution<std::size_t> pick(
          0, fLibrary.visible().size() - (skipping ? 2 : 1));
      std::size_t idx = pick(fUiRng);
      if (skipping && idx >= current) {
        ++idx; // the gap left by the one being skipped closes over it
      }
      fLibrary.selSet() = fLibrary.visible()[idx];
      fLibrary.selDiff() = 0; // the carousel follows the selection on its own
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
    fMainMenu.stopped();
  }

  void syncMapsDir() {
#ifdef __EMSCRIPTEN__
    EM_ASM(FS.syncfs(false, function(err){}));
#endif
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
    if (!fLibrary.importArchive(src, client::parseQuery(fFilter.text()))) {
      return false;
    }
    if (fState == State::kMainMenu) {
      this->switchState(State::kSongSelect);
    }
    return true;
  }

  void importOsz() {
#ifdef __EMSCRIPTEN__
    EM_ASM({
      if (Module.osuPickBeatmap)
        Module.osuPickBeatmap();
    });
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
    const auto tmp =
        std::filesystem::temp_directory_path(ec) / "osu_client_import.txt";
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

    if (fQuit.load(std::memory_order_acquire) ||
        glfw::glfwWindowShouldClose(fWindow)) {
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
      this->damageAll("artwork cleared");
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
      this->damageAll(reason);
    }
    this->damage(fMainMenu.takeDamage());
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
    bool fHitLighting = true;
    // Copied rather than read from the app: the export runs on its own thread
    // and must not reach back for something the app may be changing.
    osu::StarRating fAttributes;
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
    if (!fPlay.fMap || fPlay.fRecordedEvents.empty()) {
      this->exportFailed("nothing to export: no play recorded for this map");
      return;
    }
    const auto preset =
        client::kVideoPresets[static_cast<std::size_t>(fExportDialog.preset())];
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
    job->fOpts.fOutput = (cwdError ? fMapsDir.parent_path() : here) /
                         std::format("{}-{}x{}.mp4", safe, job->fOpts.fWidth,
                                     job->fOpts.fHeight);

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
        job->fOpts.fAudio = audioPath;
      }
    }
    std::println(std::cerr, "[export] writing {}", job->fOpts.fOutput.string());

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
    fSkin.precomputeSliderBodies(*fPlay.fMap, fComboInfo, exportScale,
                                 fContext.get());
    fSkin.flattenBodiesToRaster(fContext.get());

    job->fMap = *fPlay.fMap;
    job->fCombo = fComboInfo;
    job->fEvents = fPlay.fRecordedEvents;
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
    job->fHitLighting = fSettings.flag("hitlighting");
    job->fAttributes = fPlay.fPlayAttributes;
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
    const float offsetX = (static_cast<float>(width) -
                           static_cast<float>(osu::kPlayfieldWidth) * scale) *
                          0.5f;
    const float offsetY = (static_cast<float>(height) -
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
    ctx.fHitLighting = job.fHitLighting;
    job.fView.preScaleBackground(ctx);

    osu::Engine engine(job.fMap, job.fMods);
    const double end = job.fMap.lastObjectEndTime() + 1500.0;
    const double step = 1000.0 / static_cast<double>(job.fOpts.fFps);
    const skia::SkImageInfo info = skia::SkImageInfo::Make(
        width, height, skia::kRGBA_8888_SkColorType, skia::kPremul_SkAlphaType);
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
        combo = engine.score().fCombo;
        job.fView.setCombo(combo);
        if (result.fKind != osu::HitKind::kBasic) {
          continue;
        }
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
      }

      ctx.fCanvas = job.fSurface->getCanvas();
      ctx.fEngine = &engine;
      ctx.fCursor = cursor;
      // The HUD draws whatever it is handed, so a field the exporter forgets
      // is a field the video shows as zero.
      {
        const auto &sc = engine.score();
        osu::ScoreInput input;
        input.fGreat = sc.fGreat;
        input.fOk = sc.fGood;
        input.fMeh = sc.fMeh;
        input.fMiss = sc.fMiss;
        input.fMaxCombo = sc.fMaxCombo;
        input.fSliderTailHits = sc.fTailHit;
        input.fLargeTickHits = sc.fLargeTickHit;
        ctx.fPp = osu::performanceRanked(job.fAttributes, input).fTotal;
      }
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
    job.fMessage =
        job.fOk ? job.fOpts.fOutput.string() : job.fExporter->error();
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
      fExportDialog.setStatus(std::format(
          "rendering {}%   {}x{}", job.fPercent.load(std::memory_order_relaxed),
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
    if (this->difficultyMd5(fLibrary.selSet(), fLibrary.selDiff()) !=
        fReplayFilter) {
      this->scanReplays();
    }
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

  // The replays of the difficulty in question, as a leaderboard shows. The
  // index answers this without touching the disk.
  void scanReplays() {
    const std::string wanted =
        fState == State::kResults || fState == State::kPlaying
            ? this->beatmapMd5()
            : this->difficultyMd5(fLibrary.selSet(), fLibrary.selDiff());
    fReplayFilter = wanted;
    fReplays.clear();
    for (const auto *e : fReplayIndex.forBeatmap(wanted)) {
      fReplays.push_back({e->fPath, e->fLabel, e->fScore, e->fGrade,
                          e->fHasScore, e->fRules, e->fLegacyFormat});
    }
    // Best first, which is the order a leaderboard is in and the order these
    // panels imply by sitting in a row. The index hands them over in whatever
    // order the directory was read in, which is no order at all.
    std::ranges::stable_sort(fReplays, [](const ReplayFile &a,
                                          const ReplayFile &b) {
      const std::int64_t left =
          a.fHasScore ? static_cast<std::int64_t>(a.fScore.fTotalScore) : -1;
      const std::int64_t right =
          b.fHasScore ? static_cast<std::int64_t>(b.fScore.fTotalScore) : -1;
      return left > right;
    });

    // The score in hand starts expanded and centred; after watching a replay
    // it is that replay's own panel, which is already in the list.
    fPanels.select(0);
    for (std::size_t i = 0; i < fReplays.size(); ++i) {
      if (!fReplayPath.empty() && fReplays[i].fPath == fReplayPath) {
        fPanels.select(static_cast<int>(i));
        break;
      }
    }
    fPanelEntries.clear();
    this->syncReplayRulesToSelection();
  }

  void drawReplayList(skia::SkCanvas *canvas) {
    if (!fReplayListOpen) {
      fPanels.clear();
      return;
    }
    const skiff::paint::Painter p(canvas, fFont);
    const float sw = static_cast<float>(fWin.fScreenW);
    const float sh = static_cast<float>(fWin.fScreenH);
    p.fillRect(skia::SkRect::MakeXYWH(0, 0, sw, sh),
               skia::colorSetARGB(220, 8, 6, 12));
    p.textCentered("replays", sw * 0.5f, 62.0f, 26.0f, skia::kWhite);
    this->renderPanels(canvas, /*ownScore=*/false);

    // The same action the results screen offers, for a replay off the disk.
    fReplayButtons.clear();
    if (this->selectedReplay() != nullptr) {
      const float bw = std::min(260.0f, sw * 0.22f);
      const float gap = 14.0f;
      float bx = (sw - (bw * 2.0f + gap)) * 0.5f;
      fReplayButtons.push_back(
          {skia::SkRect::MakeXYWH(bx, sh - 92.0f, bw, 46.0f),
           this->rulesToggleLabel(),
           this->rulesToggleEnabled()
               ? client::palette::kAccent2
               : skia::colorSetARGB(255, 120, 120, 130)});
      this->drawMenuButton(canvas, fReplayButtons.back());
      bx += bw + gap;
      fReplayButtons.push_back(
          {skia::SkRect::MakeXYWH(bx, sh - 92.0f, bw, 46.0f), "export video",
           skia::colorSetARGB(255, 170, 102, 255)});
      this->drawMenuButton(canvas, fReplayButtons.back());
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
    auto set = fLibrary.setForBlocking(fLibrary.selSet());
    if (!set || fLibrary.selDiff() < 0 ||
        fLibrary.selDiff() >= static_cast<int>(set->fBeatmaps.size())) {
      return;
    }
    fSet = *set;
    fPlayingSet = fLibrary.selSet();
    fPlayingDiff = fLibrary.selDiff();
    fReplayPath = replay->fPath;
    fAutoplay = true;
    this->resetGameplayState();
    this->startGameplay(
        fSet.fBeatmaps[static_cast<std::size_t>(fLibrary.selDiff())]);
    fAudio.stop();
    fMenuMusicForSet = -1; // the menu loop restarts once the export is done
    fReplayPath.clear();
    fAutoplay = fCliAutoplay;
    fReplayListOpen = false;
    fExportDialog.show();
  }

  // The strip of score panels, from the run in hand and the replays saved for
  // this difficulty. Rebuilt for every frame that draws it, as it always was:
  // a download finishing or a replay being saved changes what belongs in it.
  void renderPanels(skia::SkCanvas *canvas, bool ownScore) {
    fPanelEntries.clear();
    std::vector<client::results::Entry> entries;
    if (ownScore) {
      const auto &sc = fResult.fScore;
      client::results::Entry own;
      own.fOwn = true;
      own.fGrade = fResult.fGrade;
      own.fTotal = sc.fScore;
      own.f300 = sc.fGreat;
      own.f100 = sc.fGood;
      own.f50 = sc.fMeh;
      own.fMiss = sc.fMiss;
      own.fCombo = sc.fMaxCombo;
      own.fAccuracy = sc.accuracy();
      own.fDetail = true;
      own.fTickHit = sc.fLargeTickHit;
      own.fTickTotal = sc.fLargeTickHit + sc.fLargeTickMiss;
      own.fTailHit = sc.fTailHit;
      own.fTailTotal = sc.fTailHit + sc.fTailMiss;
      entries.push_back(std::move(own));
      fPanelEntries.push_back(-1);
    }
    for (std::size_t i = 0; i < fReplays.size(); ++i) {
      // The run just played is already the expanded panel, so its own file is
      // not listed a second time.
      if (ownScore && !fPlay.fLastSavedReplay.empty() &&
          fReplays[i].fPath == fPlay.fLastSavedReplay) {
        continue;
      }
      const auto &r = fReplays[i];
      client::results::Entry e;
      e.fHasScore = r.fHasScore;
      e.fGrade = r.fGrade;
      e.fLabel = r.fLabel;
      if (r.fHasScore) {
        e.fTotal = static_cast<std::uint64_t>(r.fScore.fTotalScore);
        e.f300 = r.fScore.f300;
        e.f100 = r.fScore.f100;
        e.f50 = r.fScore.f50;
        e.fMiss = r.fScore.fMiss;
        e.fCombo = r.fScore.fMaxCombo;
        e.fAccuracy = r.fScore.accuracy();
      }
      entries.push_back(std::move(e));
      fPanelEntries.push_back(static_cast<int>(i));
    }
    fPanels.setEntries(std::move(entries));
    fPanels.render(canvas, this->panelCtx(ownScore));
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
    using Hit = client::results::Panels::Hit;
    switch (fPanels.click(x, y, pressed)) {
    case Hit::kNone:
      return false;
    case Hit::kActivated:
      this->watchSelectedReplay();
      return true;
    case Hit::kSelected:
      this->syncReplayRulesToSelection();
      return true;
    case Hit::kTaken:
      return true;
    }
    return true;
  }

  // The strip is live on the results screen and in the browser overlay.
  [[nodiscard]] bool panelListActive() const {
    return fReplayListOpen || fState == State::kResults;
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
    fReplayListOpen = false;
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
        this->initLibrary();
      } else {
        this->damageAll("waiting on local storage");
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
      this->damageAll("song select has nothing to list");
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
    this->damage(fCarousel.takeDamage());

    client::FilterControl::Ctx filterCtx;
    filterCtx.fFont = &fFont;
    filterCtx.fWidth = sw;
    filterCtx.fHeight = sh;
    filterCtx.fMouseX = fWin.fMouseX;
    filterCtx.fMouseY = fWin.fMouseY;
    filterCtx.fVisibleCount = fLibrary.visible().size();
    filterCtx.fNowMs = wallMs();
    fFilter.update(filterCtx);
    this->damage(fFilter.takeDamage());
    if (!fFilter.text().empty()) {
      this->wakeAt(nextCaretFlip(wallMs()));
    }

    fSelectFooter.update({.fFont = &fFont,
                          .fWidth = sw,
                          .fHeight = sh,
                          .fMouseX = fWin.fMouseX,
                          .fMouseY = fWin.fMouseY,
                          .fNowMs = wallMs()});
    this->damage(fSelectFooter.takeDamage());

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
        this->damage(fInfoWedge.takeDamage());
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
      this->importOsz();
      return true;
    case Action::kBrowse:
      this->openDownloads();
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
    fConfirmDelete = true;
    fConfirmScene = this->buildDeleteDialog(infos);
  }

  // The dialog as a tree: a dimmed backdrop, a panel anchored to the centre,
  // a vertical flow of text, and two buttons in a horizontal one. No
  // coordinates are computed here -- anchors and flows place everything, and
  // the panel fades and rises into place with transforms.
  [[nodiscard]] std::unique_ptr<skiff::scene::Drawable>
  buildDeleteDialog(const std::vector<osu::BeatmapInfo> &infos) {
    namespace scene = skiff::scene;
    namespace nodes = skiff::nodes;

    auto root = scene::make<nodes::Box>(
        {.roles = {scene::role<delete_dialog_style::Root>}},
        skia::colorSetARGB(200, 8, 6, 12));
    root->setStyleSheet<DeleteDialogTheme>();

    auto *panel = root->add<nodes::Box>(
        {.y = 20.0f,
         .alpha = 0.0f,
         .roles = {scene::role<delete_dialog_style::Panel>}},
        client::palette::kBackground5);
    panel->fadeTo(1.0f, 200.0, scene::Easing::kOutQuint);
    panel->moveToY(0.0f, 400.0, scene::Easing::kOutQuint);

    auto *column = panel->add<nodes::FillFlow>(
        {.roles = {scene::role<delete_dialog_style::Column>}},
        nodes::FillFlow::Direction::kVertical);
    column->setSpacing(0.0f, 8.0f);

    const auto &meta = infos.front().fMeta;
    const std::string title = std::format(
        "{} - {}",
        meta.fArtistUnicode.empty() ? meta.fArtist : meta.fArtistUnicode,
        meta.fTitleUnicode.empty() ? meta.fTitle : meta.fTitleUnicode);

    column->add<nodes::Text>(
        {.roles = {scene::role<delete_dialog_style::Prompt>}},
        "Confirm deletion of", 16.0f, skia::kWhite, false);
    column->add<nodes::Text>(
        {.roles = {scene::role<delete_dialog_style::Title>}}, title, 20.0f,
        skia::kWhite, true);
    column->add<nodes::Text>(
        {.roles = {scene::role<delete_dialog_style::Detail>}},
        std::format("{} difficulties will be removed from disk", infos.size()),
        13.0f, skia::kWhite, false);

    auto *buttons = panel->add<nodes::FillFlow>(
        {.roles = {scene::role<delete_dialog_style::Buttons>}},
        nodes::FillFlow::Direction::kHorizontal);
    buttons->setSpacing(20.0f, 0.0f);
    buttons->fWrap = false;
    buttons->add(this->dialogButton("Yes. Totally. Delete it.",
                                    skia::colorSetARGB(255, 255, 110, 110),
                                    [this] {
                                      fConfirmDelete = false;
                                      fConfirmScene.reset();
                                      this->deleteSelectedBeatmap();
                                    }));
    buttons->add(
        this->dialogButton("Cancel", client::palette::kAccent2, [this] {
          fConfirmDelete = false;
          fConfirmScene.reset();
        }));
    return root;
  }

  [[nodiscard]] std::unique_ptr<skiff::scene::Drawable>
  dialogButton(std::string label, skia::SkColor accent,
               std::function<void()> action) {
    namespace scene = skiff::scene;
    auto button = scene::make<skiff::widgets::Button>(
        {.roles = {scene::role<delete_dialog_style::Button>}},
        std::move(label), std::move(action));
    button->fTheme.fSurface = client::palette::kCardBg;
    button->fTheme.fSurfaceHover = client::palette::kBackground4;
    button->fTheme.fText = accent;
    button->fTheme.fCorner = 10.0f;
    button->fTheme.fFontSize = 15.0f;
    return button;
  }

  void drawDeleteConfirmation(skia::SkCanvas *canvas) {
    if (!fConfirmDelete || !fConfirmScene) {
      return;
    }
    const skia::SkRect screen = skia::SkRect::MakeWH(
        static_cast<float>(fWin.fScreenW), static_cast<float>(fWin.fScreenH));
    fConfirmScene->updateTree(wallMs());
    fConfirmScene->layoutIfNeeded(screen);
    fConfirmScene->setHover(fWin.fMouseX, fWin.fMouseY);
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
    if (fLibrary.selSet() < 0 ||
        fLibrary.selSet() >= static_cast<int>(fLibrary.sets().size())) {
      return;
    }
    const auto index = static_cast<std::size_t>(fLibrary.selSet());
    // The track playing under the menu belongs to this set; let it go before
    // the file does.
    this->stopMenuMusic();
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
    this->damage(fListing.takeDamage());
    if (fSetPage.open()) {
      const std::size_t idx = fMirrors.indexOfSet(fSetPage.setId());
      if (idx >= fMirrors.results().size()) {
        fSetPage.close(); // the set fell out of the results
        this->damageAll("beatmap page closed");
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
      this->damage(fSetPage.takeDamage());
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
        this->wakeAt(wake);
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
    this->damage(fPauseMenu.takeDamage());
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

  void drawMenuButton(skia::SkCanvas *canvas, const MenuButton &b) {
    const bool hover = b.fRect.contains(fWin.fMouseX, fWin.fMouseY);
    this->fillRounded(canvas, b.fRect, 12.0f, hover ? kCardSel : kCardBg);
    this->strokeRounded(canvas, b.fRect, 12.0f, b.fAccent, hover ? 3.0f : 2.0f);
    this->drawTextCentered(canvas, b.fLabel, b.fRect.centerX(),
                           b.fRect.centerY() + 7.0f, 20.0f,
                           hover ? b.fAccent : skia::kWhite);
  }

  // ---- Results ----------------------------------------------------------

  void updateResults() {
    // The rules toggle only exists when there is a saved replay to watch
    // under them; the score in hand was played under whatever it was played
    // under and cannot be replayed from here.
    const bool rulesToggle = this->selectedReplay() != nullptr;
    fResultActions.update({.fFont = &fFont,
                           .fWidth = static_cast<float>(fWin.fScreenW),
                           .fHeight = static_cast<float>(fWin.fScreenH),
                           .fMouseX = fWin.fMouseX,
                           .fMouseY = fWin.fMouseY,
                           .fNowMs = wallMs(),
                           .fRulesAvailable = rulesToggle,
                           .fRulesLabel = rulesToggle ? this->rulesToggleLabel()
                                                     : std::string{},
                           .fRulesEnabled = this->rulesToggleEnabled()});
    this->damage(fResultActions.takeDamage());
    // The strip of panels moves when another score is chosen and while it is
    // dragged; a drag sets the position outright, so the target says nothing
    // about it.
    if (fPanels.scrolling() || fPanels.movedSinceDrawn()) {
      fPanels.noteDrawn();
      this->damageAll("results strip moving");
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

    this->renderPanels(canvas, /*ownScore=*/fReplayPath.empty());
    fResultActions.render(canvas);

    this->drawScreenFadeIn(canvas);
    this->present();
  }

  // The replay behind the expanded panel, if it is not the score in hand.
  [[nodiscard]] const ReplayFile *selectedReplay() const {
    const int panel = fPanels.selected();
    if (panel < 0 || panel >= static_cast<int>(fPanelEntries.size())) {
      return nullptr;
    }
    const int idx = fPanelEntries[static_cast<std::size_t>(panel)];
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

  // The label on the rules toggle, and whether it can be pressed at all.
  // A replay from the .xz era was scored by the old model and its frames are
  // in the old shape, so there is nothing to choose.
  [[nodiscard]] std::string rulesToggleLabel() const {
    const auto *replay = this->selectedReplay();
    if (replay != nullptr && replay->fLegacyFormat) {
      return "rules: old (forced)";
    }
    return fReplayLegacyRules ? "rules: old" : "rules: osu!lazer";
  }

  [[nodiscard]] bool rulesToggleEnabled() const {
    const auto *replay = this->selectedReplay();
    return replay != nullptr && !replay->fLegacyFormat;
  }

  void toggleReplayRules() {
    if (this->rulesToggleEnabled()) {
      fReplayLegacyRules = !fReplayLegacyRules;
      fView.invalidate();
    }
  }

  // Moving the selection re-reads what that replay was recorded under, which
  // is the only sensible starting point: it is the answer that reproduces the
  // score in the panel. Anything not written by this client starts on osu!'s
  // rules, which is what the client plays by now.
  void syncReplayRulesToSelection() {
    const auto *replay = this->selectedReplay();
    if (replay == nullptr) {
      fReplayLegacyRules = false;
      return;
    }
    fReplayLegacyRules = replay->fLegacyFormat || replay->fRules == 1;
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

  [[nodiscard]] skia::Sp<skia::SkTypeface>
  loadTypeface(const std::string &name) {
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
    auto &stack = skiff::paint::fonts();
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
        skiff::paint::fonts().addFallback(this->loadTypeface(name));
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
    if (const auto &primary = skiff::paint::fonts().primary()) {
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
    this->damageAll("resize", /*buffersGone=*/true);
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
    if (fWindow != nullptr) {
      glfw::glfwDestroyWindow(fWindow);
      fWindow = nullptr;
    }
    glfw::glfwTerminate();
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

  void playHitsounds(double now) {
    const auto &events = fPlay.fEngine->events();
    while (fPlayedEvents < events.size()) {
      const auto &ev = events[fPlayedEvents++];
      const int previous = fCombo;
      // The engine owns the combo now that a slider hands out several
      // judgements: its ticks and its tail each raise it, and only some of
      // them break it.
      fCombo = fPlay.fEngine->score().fCombo;
      fView.setCombo(fCombo);
      // Ticks and tails are scored but not shown; lazer pops the head's
      // judgement at the head and nothing at all for what is under it.
      if (ev.fKind != osu::HitKind::kBasic) {
        // Not shown as a judgement, but the follow circle reacts to every one
        // of them: it pops on a tick and blows out on a dropped one.
        if (ev.fKind == osu::HitKind::kLargeTick ||
            ev.fKind == osu::HitKind::kSliderTail) {
          const bool tail = ev.fKind == osu::HitKind::kSliderTail;
          const bool hit =
              !std::holds_alternative<osu::judgement::Miss>(ev.fResult);
          // A tail's own time is 36ms before the slider's end; lazer plays
          // the end animation at the end.
          const double when =
              tail && ev.fIndex < fPlay.fMap->fObjects.size()
                  ? osu::objectEnd(fPlay.fMap->fObjects[ev.fIndex], *fPlay.fMap)
                        .second
                  : now;
          fView.noteSliderNested(ev.fIndex, tail, hit, when);
        }
        continue;
      }
      const auto pos = this->objectPosition(ev.fIndex);
      const bool counts =
          !std::holds_alternative<osu::judgement::Miss>(ev.fResult) &&
          ev.fIndex < fComboInfo.fIndices.size();
      fView.addJudgement(ev.fResult, ev.fIndex, pos, now,
                         counts ? fComboInfo.fIndices[ev.fIndex] : 0, counts);
      if (std::holds_alternative<osu::judgement::Miss>(ev.fResult)) {
        if (previous > 20) {
          this->playSample("combobreak");
        }
        continue;
      }
      const double hitTime =
          ev.fIndex < fPlay.fMap->fObjects.size()
              ? osu::startTime(fPlay.fMap->fObjects[ev.fIndex])
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
    if (const auto *tp = fPlay.fMap->activeTiming(time)) {
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
    if (index >= fPlay.fMap->fObjects.size())
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
               fPlay.fMap->fObjects[index]);
  }

  [[nodiscard]] osu::Vec2 objectPosition(std::size_t index) const {
    if (index >= fPlay.fMap->fObjects.size()) {
      return osu::kPlayfieldCenter;
    }
    return osu::objectPosition(fPlay.fMap->fObjects[index]);
  }

  [[nodiscard]] std::pair<osu::Vec2, double>
  objectEnd(std::size_t index) const {
    if (index >= fPlay.fMap->fObjects.size()) {
      return {osu::kPlayfieldCenter, 0.0};
    }
    return osu::objectEnd(fPlay.fMap->fObjects[index], *fPlay.fMap);
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
    std::println("{}", fPlay.fEngine->score());

    // Timing statistics over actual taps (circles + slider heads). Judgement
    // events are the wrong series for this: sliders/spinners are finalized at
    // their end and carry the object duration as delta, which is why the
    // first version of these stats produced impossible URs.
    double sum = 0.0;
    double sumSq = 0.0;
    std::size_t n = 0;
    for (const double d : fPlay.fEngine->tapDeltas()) {
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
    if (fPlay.fRecordedEvents.empty() || !fPlay.fMap)
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
    const auto &sc = fPlay.fEngine->score();
    osu::ReplayScore score;
    score.f300 = static_cast<std::uint16_t>(sc.fGreat);
    score.f100 = static_cast<std::uint16_t>(sc.fGood);
    score.f50 = static_cast<std::uint16_t>(sc.fMeh);
    score.fMiss = static_cast<std::uint16_t>(sc.fMiss);
    score.fTotalScore = static_cast<std::int32_t>(sc.fScore);
    score.fMaxCombo = static_cast<std::uint16_t>(sc.fMaxCombo);
    score.fPerfect = sc.fMiss == 0 && sc.fGood == 0 && sc.fMeh == 0;
    // The counts the four legacy shorts have no room for, written into the
    // block osu! reads at the end of the file so an import gets this score's
    // real accuracy and rank instead of a legacy approximation of them.
    const auto maxStats = fPlay.fEngine->maximumStatistics();
    osu::ReplayStatistics stats;
    stats.fPresent = true;
    stats.fGreat = sc.fGreat;
    stats.fOk = sc.fGood;
    stats.fMeh = sc.fMeh;
    stats.fMiss = sc.fMiss;
    stats.fLargeTickHit = sc.fLargeTickHit;
    stats.fLargeTickMiss = sc.fLargeTickMiss;
    stats.fSliderTailHit = sc.fTailHit;
    stats.fSmallBonus = sc.fSmallBonus;
    stats.fLargeBonus = sc.fLargeBonus;
    stats.fMaxGreat = maxStats.fGreat;
    stats.fMaxLargeTick = maxStats.fLargeTick;
    stats.fMaxSliderTail = maxStats.fSliderTail;
    stats.fMaxSmallBonus = maxStats.fSmallBonus;
    stats.fMaxLargeBonus = maxStats.fLargeBonus;
    stats.fRank = osu::gradeString(osu::computeGrade(sc));
    stats.fTotalScore = static_cast<std::int64_t>(sc.fScore);
    auto replayBytes =
        osu::encodeReplay(fPlay.fRecordedEvents, this->beatmapMd5(), "Player",
                          fMods, score, stats);
    std::error_code ec;
    std::filesystem::create_directories(fReplayDir, ec);
    const std::filesystem::path outPath =
        fReplayDir /
        (fPlay.fMap->fMeta.fVersion + "_" + nameStream.str() + ".osr");
    std::ofstream out(outPath, std::ios::binary);
    for (std::uint8_t b : replayBytes)
      out.put(static_cast<char>(b));
    out.close();
    fPlay.fLastSavedReplay = outPath;
    // Which rules this was played under lives here, in the index, and not in
    // the .osr: every field of that file belongs to osu!'s format.
    fReplayIndex.add(
        outPath, fPlay.fEngine->rules() == osu::RuleSet::kLegacyClient ? 1 : 0);
    std::println(std::cerr, "[replay] saved {}", outPath.string());
  }

  [[nodiscard]] osu::Vec2 toPlayfield(float sx, float sy) const {
    return {(static_cast<double>(sx) - fOffsetX) / fScale,
            (static_cast<double>(sy) - fOffsetY) / fScale};
  }
};

} // namespace client
