export module client.replaybrowser;

import std;
import osu;
import skia;
import skiff.paint;
import skiff.scene;
import client.palette;
import client.replaycache;
import client.results;

export namespace client {

// ReplayBrowser owns the saved-replay index and every piece of selection
// state derived from it. App supplies the current beatmap presentation and
// performs requested navigation; it does not mirror panel or rules state.
class ReplayBrowser {
public:
  struct OwnScore {
    osu::ScoreState fScore;
    std::string fGrade;
    double fPp = 0.0;
    double fMean = 0.0;
    double fUr = 0.0;
  };

  enum class OverlayAction : std::uint8_t { kNone, kExport };
  enum class Key : std::uint8_t { kPrevious, kNext, kActivate };

  struct PanelClick {
    bool fTaken = false;
    std::optional<std::filesystem::path> fWatch;
  };

  struct Motion {
    bool fFullDamage = false;
    std::optional<skia::SkRect> fDamage;
  };

  void initialize(std::filesystem::path indexFile,
                  std::filesystem::path replayDir) {
    fReplayDir = std::move(replayDir);
    fIndex.load(indexFile);
    fIndex.refresh(fReplayDir);
  }

  void add(const std::filesystem::path &path, int rules) {
    fIndex.add(path, rules);
  }

  [[nodiscard]] bool open() const noexcept { return fOpen; }
  void close() noexcept { fOpen = false; }
  void toggle(std::string wanted,
              const std::filesystem::path &currentReplay = {}) {
    fOpen = !fOpen;
    if (!fOpen) {
      return;
    }
    fIndex.refresh(fReplayDir);
    this->scan(std::move(wanted), currentReplay, false);
  }

  void refreshFilter(std::string wanted,
                     const std::filesystem::path &currentReplay = {}) {
    if (fOpen && wanted != fFilter) {
      this->scan(std::move(wanted), currentReplay, false);
    }
  }

  void selectBeatmap(std::string wanted,
                     const std::filesystem::path &currentReplay,
                     bool includeOwnScore) {
    this->scan(std::move(wanted), currentReplay, includeOwnScore);
  }

  [[nodiscard]] bool legacyRules() const noexcept { return fLegacyRules; }
  [[nodiscard]] bool rulesEnabled() const {
    const Replay *replay = this->selected();
    return replay != nullptr && !replay->fLegacyFormat;
  }
  [[nodiscard]] std::string rulesLabel() const {
    const Replay *replay = this->selected();
    if (replay != nullptr && replay->fLegacyFormat) {
      return "rules: old (forced)";
    }
    return fLegacyRules ? "rules: old" : "rules: osu!lazer";
  }
  [[nodiscard]] bool toggleRules() {
    if (!this->rulesEnabled()) {
      return false;
    }
    fLegacyRules = !fLegacyRules;
    return true;
  }

  [[nodiscard]] std::optional<std::filesystem::path> selectedPath() const {
    const Replay *replay = this->selected();
    return replay ? std::optional(replay->fPath) : std::nullopt;
  }

  [[nodiscard]] bool panelsDragging() const { return fPanels.dragging(); }
  void dragPanels(float x) { fPanels.drag(x); }
  void scrollPanels(float delta) { fPanels.scrollBy(delta); }
  [[nodiscard]] bool scrolling() const { return fPanels.scrolling(); }

  [[nodiscard]] PanelClick clickPanels(float x, float y, bool pressed) {
    using Hit = results::Panels::Hit;
    switch (fPanels.click(x, y, pressed)) {
    case Hit::kNone:
      return {};
    case Hit::kActivated:
      return {.fTaken = true, .fWatch = this->selectedPath()};
    case Hit::kSelected:
      this->syncRules();
      return {.fTaken = true, .fWatch = std::nullopt};
    case Hit::kTaken:
      return {.fTaken = true, .fWatch = std::nullopt};
    }
    return {.fTaken = true, .fWatch = std::nullopt};
  }

  [[nodiscard]] std::optional<std::filesystem::path> key(Key key) {
    switch (key) {
    case Key::kPrevious:
      if (fPanels.selected() > 0) {
        fPanels.select(fPanels.selected() - 1);
        this->syncRules();
      }
      break;
    case Key::kNext:
      if (fPanels.selected() + 1 < static_cast<int>(fPanelEntries.size())) {
        fPanels.select(fPanels.selected() + 1);
        this->syncRules();
      }
      break;
    case Key::kActivate:
      return this->selectedPath();
    }
    return std::nullopt;
  }

  [[nodiscard]] Motion updateMotion(float mouseX, float mouseY) {
    if (fPanels.scrolling() || fPanels.movedSinceDrawn()) {
      fPanels.noteDrawn();
      return {.fFullDamage = true, .fDamage = std::nullopt};
    }
    const int hot = fPanels.hot(mouseX, mouseY);
    if (hot == fHotPanel) {
      return {};
    }
    fHotPanel = hot;
    return {.fDamage = fPanels.band()};
  }

  void renderOverlay(skia::SkCanvas *canvas, const results::Ctx &ctx) {
    if (!fOpen) {
      fPanels.clear();
      return;
    }
    const skiff::paint::Painter painter(canvas, *ctx.fFont);
    painter.fillRect(skia::SkRect::MakeXYWH(0, 0, ctx.fWidth, ctx.fHeight),
                     skia::colorSetARGB(220, 8, 6, 12));
    painter.textCentered("replays", ctx.fWidth * 0.5f, 62.0f, 26.0f,
                         skia::kWhite);
    this->renderPanels(canvas, ctx, std::nullopt, {});

    fButtons.clear();
    if (this->selected() == nullptr) {
      return;
    }
    const float width = std::min(260.0f, ctx.fWidth * 0.22f);
    const float gap = 14.0f;
    float x = (ctx.fWidth - (width * 2.0f + gap)) * 0.5f;
    fButtons.push_back(
        {skia::SkRect::MakeXYWH(x, ctx.fHeight - 92.0f, width, 46.0f),
         this->rulesLabel(),
         this->rulesEnabled()
             ? palette::kAccent2
             : skia::colorSetARGB(255, 120, 120, 130)});
    this->drawButton(painter, fButtons.back(), ctx.fMouseX, ctx.fMouseY);
    x += width + gap;
    fButtons.push_back(
        {skia::SkRect::MakeXYWH(x, ctx.fHeight - 92.0f, width, 46.0f),
         "export video", skia::colorSetARGB(255, 170, 102, 255)});
    this->drawButton(painter, fButtons.back(), ctx.fMouseX, ctx.fMouseY);
  }

  [[nodiscard]] OverlayAction clickOverlay(float x, float y) {
    for (std::size_t i = 0; i < fButtons.size(); ++i) {
      if (!fButtons[i].fRect.contains(x, y)) {
        continue;
      }
      if (i == 0) {
        (void)this->toggleRules();
        return OverlayAction::kNone;
      }
      return OverlayAction::kExport;
    }
    return OverlayAction::kNone;
  }

  void renderResults(skia::SkCanvas *canvas, const results::Ctx &ctx,
                     std::optional<OwnScore> own,
                     const std::filesystem::path &lastSaved) {
    this->renderPanels(canvas, ctx, std::move(own), lastSaved);
    fActions.render(canvas);
  }

  void updateResultActions(results::ActionCtx ctx) {
    const bool available = this->selected() != nullptr;
    ctx.fRulesAvailable = available;
    ctx.fRulesLabel = available ? this->rulesLabel() : std::string{};
    ctx.fRulesEnabled = this->rulesEnabled();
    fActions.update(ctx);
  }
  [[nodiscard]] skiff::scene::FrameResult finishResultFrame() {
    return fActions.finishFrame();
  }
  [[nodiscard]] skiff::scene::Drawable *resultActionsRoot() noexcept {
    return fActions.sceneRoot();
  }
  [[nodiscard]] results::Action clickResultAction(float x, float y) {
    return fActions.click(x, y);
  }

private:
  struct Replay {
    std::filesystem::path fPath;
    std::string fLabel;
    osu::ReplayScore fScore;
    std::string fGrade;
    bool fHasScore = false;
    int fRules = -1;
    bool fLegacyFormat = false;
  };

  struct Button {
    skia::SkRect fRect;
    std::string fLabel;
    skia::SkColor fAccent;
  };

  void scan(std::string wanted, const std::filesystem::path &currentReplay,
            bool includeOwnScore) {
    fFilter = std::move(wanted);
    fReplays.clear();
    for (const auto *entry : fIndex.forBeatmap(fFilter)) {
      fReplays.push_back({entry->fPath, entry->fLabel, entry->fScore,
                          entry->fGrade, entry->fHasScore, entry->fRules,
                          entry->fLegacyFormat});
    }
    std::ranges::stable_sort(fReplays, [](const Replay &left,
                                          const Replay &right) {
      const std::int64_t leftScore =
          left.fHasScore ? static_cast<std::int64_t>(left.fScore.fTotalScore)
                         : -1;
      const std::int64_t rightScore =
          right.fHasScore ? static_cast<std::int64_t>(right.fScore.fTotalScore)
                          : -1;
      return leftScore > rightScore;
    });

    fPanelEntries.clear();
    if (includeOwnScore) {
      fPanelEntries.push_back(-1);
    }
    for (std::size_t i = 0; i < fReplays.size(); ++i) {
      fPanelEntries.push_back(static_cast<int>(i));
    }
    fPanels.select(0);
    for (std::size_t i = 0; i < fReplays.size(); ++i) {
      if (!currentReplay.empty() && fReplays[i].fPath == currentReplay) {
        fPanels.select(static_cast<int>(i) + (includeOwnScore ? 1 : 0));
        break;
      }
    }
    this->syncRules();
  }

  void renderPanels(skia::SkCanvas *canvas, results::Ctx ctx,
                    std::optional<OwnScore> own,
                    const std::filesystem::path &lastSaved) {
    fPanelEntries.clear();
    std::vector<results::Entry> entries;
    if (own) {
      const auto &score = own->fScore;
      results::Entry entry;
      entry.fOwn = true;
      entry.fGrade = own->fGrade;
      entry.fTotal = score.fScore;
      entry.f300 = score.fGreat;
      entry.f100 = score.fGood;
      entry.f50 = score.fMeh;
      entry.fMiss = score.fMiss;
      entry.fCombo = score.fMaxCombo;
      entry.fAccuracy = score.accuracy();
      entry.fDetail = true;
      entry.fTickHit = score.fLargeTickHit;
      entry.fTickTotal = score.fLargeTickHit + score.fLargeTickMiss;
      entry.fTailHit = score.fTailHit;
      entry.fTailTotal = score.fTailHit + score.fTailMiss;
      entries.push_back(std::move(entry));
      fPanelEntries.push_back(-1);
      ctx.fPp = own->fPp;
      ctx.fMean = own->fMean;
      ctx.fUr = own->fUr;
      ctx.fOwnScore = true;
    } else {
      ctx.fOwnScore = false;
    }

    for (std::size_t i = 0; i < fReplays.size(); ++i) {
      if (own && !lastSaved.empty() && fReplays[i].fPath == lastSaved) {
        continue;
      }
      const auto &replay = fReplays[i];
      results::Entry entry;
      entry.fHasScore = replay.fHasScore;
      entry.fGrade = replay.fGrade;
      entry.fLabel = replay.fLabel;
      if (replay.fHasScore) {
        entry.fTotal = static_cast<std::uint64_t>(replay.fScore.fTotalScore);
        entry.f300 = replay.fScore.f300;
        entry.f100 = replay.fScore.f100;
        entry.f50 = replay.fScore.f50;
        entry.fMiss = replay.fScore.fMiss;
        entry.fCombo = replay.fScore.fMaxCombo;
        entry.fAccuracy = replay.fScore.accuracy();
      }
      entries.push_back(std::move(entry));
      fPanelEntries.push_back(static_cast<int>(i));
    }
    fPanels.setEntries(std::move(entries));
    fPanels.render(canvas, ctx);
  }

  [[nodiscard]] const Replay *selected() const {
    const int panel = fPanels.selected();
    if (panel < 0 || panel >= static_cast<int>(fPanelEntries.size())) {
      return nullptr;
    }
    const int index = fPanelEntries[static_cast<std::size_t>(panel)];
    if (index < 0 || index >= static_cast<int>(fReplays.size())) {
      return nullptr;
    }
    return &fReplays[static_cast<std::size_t>(index)];
  }

  void syncRules() {
    const Replay *replay = this->selected();
    fLegacyRules = replay != nullptr &&
                   (replay->fLegacyFormat || replay->fRules == 1);
  }

  static void drawButton(const skiff::paint::Painter &p, const Button &button,
                         float mouseX, float mouseY) {
    const bool hover = button.fRect.contains(mouseX, mouseY);
    p.fillRounded(button.fRect, 12.0f,
                  hover ? palette::kCardSel : palette::kCardBg);
    p.strokeRounded(button.fRect, 12.0f, button.fAccent,
                    hover ? 3.0f : 2.0f);
    p.textCentered(button.fLabel, button.fRect.centerX(),
                   button.fRect.centerY() + 7.0f, 20.0f,
                   hover ? button.fAccent : skia::kWhite);
  }

  std::filesystem::path fReplayDir;
  ReplayIndex fIndex;
  bool fOpen = false;
  bool fLegacyRules = false;
  std::vector<Replay> fReplays;
  std::string fFilter;
  results::Panels fPanels;
  results::Actions fActions;
  std::vector<int> fPanelEntries;
  std::vector<Button> fButtons;
  int fHotPanel = -1;
};

} // namespace client
