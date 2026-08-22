export module client.videoexport;

import std;
import osu;
import skia;
import skin;
import client.gameplayview;
import client.video;

export namespace client {

// A replay render is deliberately a value snapshot. The application is free
// to change maps, mods and play state while the worker consumes this copy.
class ReplayVideoExporter {
public:
  struct Request {
    VideoOptions fOptions;
    osu::Beatmap fMap;
    osu::ComboInfo fCombo;
    std::vector<osu::InputEvent> fEvents;
    osu::ModSet fMods = osu::mod::kNone;
    osu::RuleSet fRules = osu::RuleSet::kLazer;
    Skin *fSkin = nullptr;
    skia::SkFont fFont;
    skia::SkFont fDisplayFont;
    skia::Sp<skia::SkImage> fBackground;
    float fCursorSize = 1.0f;
    float fDim = 0.7f;
    bool fNoGlow = false;
    bool fHitLighting = true;
    osu::StarRating fAttributes;
  };

  struct Status {
    int fPercent = 0;
    int fWidth = 0;
    int fHeight = 0;
    bool fFinished = false;
    bool fOk = false;
    std::string fMessage;
  };

  ReplayVideoExporter() = default;
  ReplayVideoExporter(const ReplayVideoExporter &) = delete;
  ReplayVideoExporter &operator=(const ReplayVideoExporter &) = delete;
  ~ReplayVideoExporter();

  [[nodiscard]] bool active() const noexcept;
  // Empty means that the worker started. Otherwise the returned text is safe
  // to put directly in the export dialog.
  [[nodiscard]] std::string start(Request request);
  [[nodiscard]] Status status() const;
  // Releases a completed job. Calling this while it is still rendering is a
  // no-op; the destructor is the only operation that waits for active work.
  void clearFinished();

private:
  struct Job;
  std::unique_ptr<Job> fJob;

  static void run(Job &job);
};

struct ReplayVideoExporter::Job {
  std::shared_ptr<VideoExporter> fExporter =
      std::make_shared<VideoExporter>();
  Request fRequest;
  skia::Sp<skia::SkSurface> fSurface;
  GameplayView fView;
  std::thread fThread;
  std::atomic<int> fPercent{0};
  std::atomic<bool> fFinished{false};
  bool fOk = false;
  std::string fMessage;
};

ReplayVideoExporter::~ReplayVideoExporter() {
  if (fJob && fJob->fThread.joinable()) {
    fJob->fThread.join();
  }
}

bool ReplayVideoExporter::active() const noexcept { return bool(fJob); }

std::string ReplayVideoExporter::start(Request request) {
  if (fJob) {
    return "a video is already being exported";
  }

  auto job = std::make_unique<Job>();
  job->fRequest = std::move(request);
  if (!job->fExporter->begin(job->fRequest.fOptions)) {
    return job->fExporter->error();
  }

  const auto &opts = job->fRequest.fOptions;
  job->fSurface = skia::Raster(skia::SkImageInfo::Make(
      opts.fWidth, opts.fHeight, skia::kRGBA_8888_SkColorType,
      skia::kPremul_SkAlphaType));
  if (!job->fSurface) {
    return "cannot create the offscreen surface";
  }
  job->fView.setBackground(std::move(job->fRequest.fBackground));

  Job *raw = job.get();
  job->fThread = std::thread([raw] { run(*raw); });
  fJob = std::move(job);
  return {};
}

ReplayVideoExporter::Status ReplayVideoExporter::status() const {
  if (!fJob) {
    return {};
  }
  const bool finished = fJob->fFinished.load(std::memory_order_acquire);
  const auto &opts = fJob->fRequest.fOptions;
  return {.fPercent = fJob->fPercent.load(std::memory_order_relaxed),
          .fWidth = opts.fWidth,
          .fHeight = opts.fHeight,
          .fFinished = finished,
          .fOk = finished && fJob->fOk,
          .fMessage = finished ? fJob->fMessage : std::string{}};
}

void ReplayVideoExporter::clearFinished() {
  if (!fJob || !fJob->fFinished.load(std::memory_order_acquire)) {
    return;
  }
  if (fJob->fThread.joinable()) {
    fJob->fThread.join();
  }
  fJob.reset();
}

void ReplayVideoExporter::run(Job &job) {
  auto &request = job.fRequest;
  const auto &opts = request.fOptions;
  const int width = opts.fWidth;
  const int height = opts.fHeight;
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

  GameplayView::Ctx ctx;
  ctx.fMap = &request.fMap;
  ctx.fSkin = request.fSkin;
  ctx.fCombo = &request.fCombo;
  ctx.fFont = &request.fFont;
  ctx.fDisplayFont = &request.fDisplayFont;
  ctx.fScale = scale;
  ctx.fOffsetX = offsetX;
  ctx.fOffsetY = offsetY;
  ctx.fScreenW = width;
  ctx.fScreenH = height;
  ctx.fCursorSize = request.fCursorSize;
  ctx.fUiScale = std::clamp(static_cast<float>(height) / 1080.0f, 0.7f, 3.0f);
  ctx.fDim = request.fDim;
  ctx.fNoGlow = request.fNoGlow;
  ctx.fHitLighting = request.fHitLighting;
  job.fView.preScaleBackground(ctx);

  osu::Engine engine(request.fMap, request.fMods, request.fRules);
  const double end = request.fMap.lastObjectEndTime() + 1500.0;
  const double step = 1000.0 / static_cast<double>(opts.fFps);
  const skia::SkImageInfo info = skia::SkImageInfo::Make(
      width, height, skia::kRGBA_8888_SkColorType,
      skia::kPremul_SkAlphaType);
  const std::size_t rowBytes = static_cast<std::size_t>(width) * 4u;
  std::vector<std::uint8_t> pixels(rowBytes * static_cast<std::size_t>(height));

  std::size_t event = 0;
  std::size_t judged = 0;
  osu::Vec2 cursor = osu::kPlayfieldCenter;
  for (double now = 0.0; now <= end; now += step) {
    while (event < request.fEvents.size() &&
           request.fEvents[event].fTime <= now) {
      engine.submit(request.fEvents[event]);
      if (request.fEvents[event].fAction == osu::InputAction::kMove) {
        cursor = request.fEvents[event].fPos;
        job.fView.addTrailPoint(cursor, request.fEvents[event].fTime);
      }
      ++event;
    }
    engine.advance(now);

    const auto &events = engine.events();
    while (judged < events.size()) {
      const auto &result = events[judged++];
      job.fView.setCombo(engine.score().fCombo);
      if (result.fKind != osu::HitKind::kBasic) {
        continue;
      }
      const osu::Vec2 pos =
          result.fIndex < request.fMap.fObjects.size()
              ? osu::objectPosition(request.fMap.fObjects[result.fIndex])
              : osu::kPlayfieldCenter;
      const bool counts =
          !std::holds_alternative<osu::judgement::Miss>(result.fResult) &&
          result.fIndex < request.fCombo.fIndices.size();
      job.fView.addJudgement(
          result.fResult, result.fIndex, pos, now,
          counts ? request.fCombo.fIndices[result.fIndex] : 0, counts);
    }

    ctx.fCanvas = job.fSurface->getCanvas();
    ctx.fEngine = &engine;
    ctx.fCursor = cursor;
    const auto &score = engine.score();
    osu::ScoreInput input;
    input.fGreat = score.fGreat;
    input.fOk = score.fGood;
    input.fMeh = score.fMeh;
    input.fMiss = score.fMiss;
    input.fMaxCombo = score.fMaxCombo;
    input.fSliderTailHits = score.fTailHit;
    input.fLargeTickHits = score.fLargeTickHit;
    ctx.fPp = osu::performanceRanked(request.fAttributes, input).fTotal;
    job.fView.render(ctx, now);
    if (job.fSurface->readPixels(info, pixels.data(), rowBytes, 0, 0)) {
      job.fExporter->addFrame(pixels);
    }
    job.fPercent.store(
        static_cast<int>(std::clamp(now / std::max(1.0, end), 0.0, 1.0) *
                         100.0),
        std::memory_order_relaxed);
  }

  job.fOk = job.fExporter->finish();
  job.fMessage =
      job.fOk ? opts.fOutput.string() : job.fExporter->error();
  job.fFinished.store(true, std::memory_order_release);
}

} // namespace client
