export module client.framestate;

import std;
import skia;
import skiff.scene;
import present;

export namespace client {

class App;

// The mutable state of incremental presentation. Only App may inspect the
// frame currently being assembled; all invalidation enters through methods,
// so a caller cannot silently change damage or scheduling state.
class FrameState {
  friend class App;

public:
  void requestRedraw(double now, double durationMs = 0.0) {
    fRedrawUntilWall = std::max(fRedrawUntilWall, now + durationMs);
  }
  void oweFrames(int frames) { fFramesOwed = std::max(fFramesOwed, frames); }
  void wakeAt(double wall) {
    fWakeWall = fWakeWall <= 0.0 ? wall : std::min(fWakeWall, wall);
  }

  void damageAll(const char *reason = "unspecified", bool buffersGone = false) {
    fFullDamage = true;
    const bool reportedAge = fBufferAge >= 0 && !fBufferAgeAssumed;
    fFullRepaintsOwed =
        (!buffersGone && reportedAge) ? 0 : kFullRepaintsAfterChange;
    if (!fDrawing) {
      fDamageDrives = true;
      this->oweFrames(kFullRepaintsAfterChange + 1);
    }
    fFullDamageReason = reason;
    fDamage.clear();
  }

  void damage(const skia::SkRect &rect) {
    if (fFullDamage || rect.isEmpty()) {
      return;
    }
    if (!fDrawing) {
      fDamageDrives = true;
    }
    skia::SkIRect area = rect.roundOut();
    area.outset(2, 2);
    for (auto &existing : fDamage) {
      skia::SkIRect probe = existing;
      probe.outset(2, 2);
      if (skia::SkIRect::Intersects(probe, area)) {
        existing.join(area);
        return;
      }
    }
    for (auto &existing : fDamage) {
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
    if (fDamage.size() < kMaxDamageRects) {
      fDamage.push_back(area);
      return;
    }
    std::size_t best = 0;
    std::int64_t bestCost = std::numeric_limits<std::int64_t>::max();
    for (std::size_t i = 0; i < fDamage.size(); ++i) {
      skia::SkIRect merged = fDamage[i];
      merged.join(area);
      const auto cost =
          static_cast<std::int64_t>(merged.width()) * merged.height() -
          static_cast<std::int64_t>(fDamage[i].width()) * fDamage[i].height();
      if (cost < bestCost) {
        bestCost = cost;
        best = i;
      }
    }
    fDamage[best].join(area);
  }

  void consume(skiff::scene::FrameResult result) {
    this->damage(result.fDamage);
    fSceneWantsFrame = fSceneWantsFrame || result.fWantsAnotherFrame;
  }

  [[nodiscard]] bool because(double now, const char *reason) {
    if (fDiag.fTraceRepaint && reason != fFrameReason) {
      std::println(std::cerr, "[frame] {:8.0f} ms  drawn because {}", now,
                   reason);
    }
    fFrameReason = reason;
    return true;
  }

  [[nodiscard]] std::size_t windowReach() const {
    return fBufferAge > 0 ? static_cast<std::size_t>(fBufferAge)
                          : kSwapChainDepth;
  }
  [[nodiscard]] std::size_t drawReach() const { return this->windowReach(); }
  [[nodiscard]] bool historyShorterThan(std::size_t reach) const {
    if (fBufferAge == 0 || fBlitHistory.size() < reach) {
      return true;
    }
    std::size_t seen = 0;
    for (auto frame = fBlitHistory.rbegin();
         frame != fBlitHistory.rend() && seen < reach; ++frame, ++seen) {
      if (frame->empty()) {
        return true;
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
    fBlitHistory.push_back(fFrameClipFull ? std::vector<skia::SkIRect>{}
                                          : fFrameClip);
    while (fBlitHistory.size() > kBlitHistoryDepth) {
      fBlitHistory.erase(fBlitHistory.begin());
    }
  }
  void includeInBlit(const skia::SkRect &rect, int width, int height) {
    if (fBlitRegions.empty() || rect.isEmpty()) {
      return;
    }
    skia::SkIRect area = rect.roundOut();
    area.outset(2, 2);
    if (area.intersect(skia::SkIRect::MakeWH(width, height))) {
      fBlitRegions.push_back(area);
    }
  }

  [[nodiscard]] bool showingDamage(bool setting) const {
    return fDiag.fForceShowDamage || setting;
  }

  void showDamage(skia::SkCanvas *canvas, int width, int height, double now,
                  bool setting) {
    if (!this->showingDamage(setting)) {
      return;
    }
    skia::SkPaint paint;
    paint.setStyle(skia::kStrokeStyle);
    if (fComputedClipFull) {
      paint.setStrokeWidth(6.0f);
      paint.setColor(skia::colorSetARGB(255, 255, 40, 40));
      skia::SkRect border = skia::SkRect::MakeWH(static_cast<float>(width),
                                                 static_cast<float>(height));
      border.inset(3.0f, 3.0f);
      canvas->drawRect(border, paint);
      const bool sameReason =
          fDiag.fLoggedFullReason != nullptr &&
          std::string_view(fDiag.fLoggedFullReason) ==
              std::string_view(fFullDamageReason);
      if (!sameReason || now - fDamageLogWall > 1000.0) {
        fDamageLogWall = now;
        fDiag.fLoggedFullReason = fFullDamageReason;
        std::println(std::cerr, "[damage] would repaint everything: {}",
                     fFullDamageReason);
      }
      return;
    }
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

  void reportCost(std::chrono::steady_clock::time_point start,
                  std::chrono::steady_clock::time_point beforeSwap,
                  int width, int height, bool partial) {
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
    if (fComputedClipFull) {
      fDiag.fCostClipArea += static_cast<std::int64_t>(width) * height;
    } else {
      for (const auto &rect : fComputedClip) {
        fDiag.fCostClipArea +=
            static_cast<std::int64_t>(rect.width()) * rect.height();
      }
    }
    ++fDiag.fCostFrames;
    const double wall = std::chrono::duration<double, std::milli>(
                            now.time_since_epoch())
                            .count();
    if (wall - fDiag.fCostLogWall < 1000.0 || fDiag.fCostFrames == 0) {
      return;
    }
    const double frames = fDiag.fCostFrames;
    std::println(
        std::cerr,
        "[frame] update {:.2f} ms, draw {:.2f} ms, blit {:.2f} ms, "
        "swap {:.2f} ms over {} frames, {} of {} drawables, "
        "{:.0f}% of the screen repainted{}",
        fDiag.fCostUpdateUs / frames / 1000.0,
        fDiag.fCostDrawUs / frames / 1000.0,
        fDiag.fCostBlitUs / frames / 1000.0,
        fDiag.fCostSwapUs / frames / 1000.0, fDiag.fCostFrames,
        fDiag.fCostDrawn /
            std::max<std::uint64_t>(1, static_cast<std::uint64_t>(frames)),
        fDiag.fCostVisited /
            std::max<std::uint64_t>(1, static_cast<std::uint64_t>(frames)),
        100.0 * fDiag.fCostClipArea /
            std::max(1.0, frames * static_cast<double>(width) * height),
        std::string(fDrewOnRaster ? " [cpu]" : " [gpu]") +
            (partial
                 ? std::format(" (partial redraw, buffer age {} via {})",
                               fBufferAge,
                               fBufferAgeAssumed ? "assumption"
                                                 : present::backend())
                 : ""));
    fDiag.fCostLogWall = wall;
    fDiag.fCostUpdateUs = fDiag.fCostDrawUs = fDiag.fCostBlitUs =
        fDiag.fCostSwapUs = 0;
    fDiag.fCostVisited = fDiag.fCostDrawn = 0;
    fDiag.fCostClipArea = 0;
    fDiag.fCostFrames = 0;
  }

private:
  static constexpr std::size_t kMaxDamageRects = 3;
  static constexpr std::size_t kSwapChainDepth = 4;
  static constexpr int kFullRepaintsAfterChange = 6;
  static constexpr std::size_t kBlitHistoryDepth = 8;

  struct Diagnostics {
    const char *fLoggedFullReason = nullptr;
    std::chrono::steady_clock::time_point fFrameStart{};
    std::chrono::steady_clock::time_point fBlitStart{};
    std::int64_t fCostUpdateUs = 0, fCostDrawUs = 0, fCostBlitUs = 0,
                 fCostSwapUs = 0;
    std::int64_t fLastUpdateUs = 0;
    std::uint64_t fCostVisited = 0, fCostDrawn = 0;
    std::int64_t fCostClipArea = 0;
    int fCostFrames = 0;
    double fCostLogWall = 0.0;
    const bool fForceShowDamage = std::getenv("OSU_SHOW_DAMAGE") != nullptr;
    const bool fTraceRepaint = std::getenv("OSU_TRACE_REPAINT") != nullptr ||
                               std::getenv("OSU_TRACE_RESIZE") != nullptr;
    bool fTracedClipping = false;
    int fTracedAge = -2;
    std::size_t fTracedHistory = 0;
  };

  skia::Sp<skia::SkSurface> fSurface;
  skia::Sp<skia::SkSurface> fWindowSurface;
  skia::Sp<skia::SkSurface> fRasterSurface;
  bool fDrewOnRaster = false;
  int fFrameSave = 0;
  bool fDrawing = false;
  std::vector<skia::SkIRect> fDamage;
  bool fFullDamage = true;
  const char *fFullDamageReason = "start";
  bool fDamageDrives = false;
  bool fSceneWantsFrame = false;
  std::vector<skia::SkIRect> fComputedClip;
  bool fComputedClipFull = true;
  std::vector<skia::SkIRect> fFrameClip;
  bool fFrameClipFull = true;
  std::vector<skia::SkIRect> fBlitRegions;
  std::vector<std::vector<skia::SkIRect>> fBlitHistory;
  int fBufferAge = -1;
  bool fBufferAgeAssumed = false;
  bool fAgeReported = false;
  int fFramesOwed = 0;
  int fFullRepaintsOwed = 0;
  double fWakeWall = 0.0;
  double fRedrawUntilWall = 0.0;
  double fLastDrawWall = 0.0;
  const char *fFrameReason = "";
  double fDamageLogWall = 0.0;
  Diagnostics fDiag;
};

} // namespace client
