export module client.overlays;

import std;
import skia;
import osu;
import skiff.paint;
import skiff.scene;
import skiff.nodes;
import client.palette;
import client.mods;
import client.video;

// skiff::paint is the framework's drawing side; the short name keeps
// the lines below at the width they were written at.
namespace paint = skiff::paint;

export namespace client {

namespace scene = skiff::scene;
namespace nodes = skiff::nodes;

// ---- Mod select ----------------------------------------------------------
//
// ModSelectOverlay lays mods out in columns by category; each is a rounded
// panel carrying the acronym, name, description and score multiplier, and
// toggling one keeps the mutually exclusive pairs consistent.
namespace mod_select_style {
struct Main;
struct Spacer;
struct Columns;
struct Column;
struct Header;
struct Card;
struct Footer;
} // namespace mod_select_style

namespace mod_select_detail {

class Root : public scene::TypedDrawable<Root> {
protected:
  bool acceptsInput() const override { return true; }
  bool hoverChangesAppearance() const override { return false; }
  bool onClick(float, float) override { return true; }
};

// One mod remains a custom-painted surface because the acronym/name/detail
// typography is its visual identity. Its box, selection, hover, click and
// damage are ordinary scene behavior.
class Card : public scene::TypedDrawable<Card, nodes::Box> {
public:
  using Base = scene::TypedDrawable<Card, nodes::Box>;

  Card(const ModEntry &entry, const osu::ModSet *active,
       std::function<void(osu::ModSet)> toggle)
      : Base(palette::kCardBg), fAcronym(entry.fAcronym), fName(entry.fName),
        fDescription(entry.fDescription), fFlag(entry.fFlag),
        fMultiplier(entry.fMultiplier), fActive(active),
        fToggle(std::move(toggle)) {}

protected:
  void update(double) override {
    this->setSelected((*fActive & fFlag) != osu::mod::kNone);
  }

  void drawSelf(skia::SkCanvas *canvas, float alpha) override {
    nodes::Box::drawSelf(canvas, alpha);
    skia::SkFont *font = paint::defaultFont();
    if (font == nullptr) {
      return;
    }
    const paint::Painter p(canvas, *font);
    const skia::SkColor ink =
        this->selected() ? skia::colorSetARGB(255, 24, 18, 30)
                         : skia::kWhite;
    p.textClipped(fAcronym, fBounds.fLeft + 18.0f, fBounds.fTop + 34.0f,
                  70.0f, 24.0f, ink, alpha);
    p.textClipped(fName, fBounds.fLeft + 84.0f, fBounds.fTop + 32.0f,
                  fBounds.width() - 100.0f, 16.0f, ink, alpha);
    p.textClipped(fDescription, fBounds.fLeft + 84.0f, fBounds.fTop + 56.0f,
                  fBounds.width() - 100.0f, 12.0f, ink, alpha * 0.7f);
    p.textClipped(std::format("{:.2f}x", fMultiplier), fBounds.fLeft + 84.0f,
                  fBounds.fBottom - 12.0f, 80.0f, 11.0f,
                  this->selected() ? ink : palette::kAccent2, alpha * 0.9f);
  }

  bool acceptsInput() const override { return true; }

  bool onClick(float, float) override {
    fToggle(fFlag);
    return true;
  }

private:
  std::string fAcronym;
  std::string fName;
  std::string fDescription;
  osu::ModSet fFlag;
  double fMultiplier;
  const osu::ModSet *fActive;
  std::function<void(osu::ModSet)> fToggle;
};

} // namespace mod_select_detail

struct ModSelectTheme {
  static constexpr auto styles =
      scene::makeStyleSheet()
          .rule(scene::select<mod_select_detail::Root>(),
                {.width = 1.0f,
                 .height = 1.0f,
                 .relativeSize = scene::Axes::kBoth})
          .rule(scene::select<nodes::Box>(),
                {.width = 1.0f,
                 .height = 1.0f,
                 .relativeSize = scene::Axes::kBoth,
                 .alpha = 190.0f / 255.0f,
                 .backgroundColour = skia::colorSetARGB(255, 8, 6, 12)})
          .rule(scene::selectAny<mod_select_style::Main>(),
                {.width = 1.0f,
                 .height = 1.0f,
                 .relativeSize = scene::Axes::kBoth})
          .rule(scene::selectAny<mod_select_style::Spacer>(),
                {.height = 0.28f, .relativeSize = scene::Axes::kY})
          .rule(scene::select<nodes::Grid, mod_select_style::Columns>(),
                {.width = 0.72f,
                 .relativeSize = scene::Axes::kX,
                 .autoSize = scene::Axes::kY,
                 .maxWidth = 720.0f,
                 .alignSelf = scene::Align::kMiddle})
          .rule(scene::select<nodes::FillFlow, mod_select_style::Column>(),
                {.width = 1.0f,
                 .relativeSize = scene::Axes::kX,
                 .autoSize = scene::Axes::kY})
          .rule(scene::select<nodes::Text, mod_select_style::Header>(),
                {.alignSelf = scene::Align::kMiddle,
                 .colour = palette::kAccent2,
                 .fontSize = 16.0f})
          .rule(scene::select<mod_select_detail::Card,
                              mod_select_style::Card>(),
                {.width = 1.0f,
                 .height = 96.0f,
                 .relativeSize = scene::Axes::kX,
                 .cornerRadius = 12.0f,
                 .backgroundColour = palette::kCardBg})
          .rule(scene::select<mod_select_detail::Card,
                              mod_select_style::Card>()
                    .when(scene::StyleState::kHover),
                {.backgroundColour = palette::kCardSel})
          .rule(scene::select<mod_select_detail::Card,
                              mod_select_style::Card>()
                    .when(scene::StyleState::kSelected),
                {.backgroundColour = palette::kAccent})
          .rule(scene::select<nodes::Text, mod_select_style::Footer>(),
                {.anchor = scene::Anchor::kBottomCentre,
                 .origin = scene::Anchor::kBottomCentre,
                 .y = -32.0f,
                 .alpha = 0.7f,
                 .colour = skia::kWhite,
                 .fontSize = 14.0f});
};

class ModSelect {
public:
  struct Frame {
    int fScreenW = 0, fScreenH = 0;
    float fMouseX = 0.0f, fMouseY = 0.0f;
    double fNowMs = 0.0;
  };

  [[nodiscard]] bool open() const noexcept { return fOpen; }
  [[nodiscard]] bool animating() const noexcept {
    return fScene && fScene->animatingTree();
  }
  [[nodiscard]] bool visible() const noexcept {
    return fOpen || (fScene && fScene->fAlpha > 0.002f);
  }
  void toggle() noexcept { fOpen = !fOpen; }
  void close() noexcept { fOpen = false; }

  void update(skia::SkFont &font, std::span<const ModEntry> entries,
              osu::ModSet active, const Frame &frame) {
    nodes::Text::setFont(&font);
    fActive = active;
    const std::size_t shape = this->shapeOf(entries);
    bool rebuilt = false;
    if (!fScene || shape != fShape) {
      fShape = shape;
      fScene = this->build(entries);
      fScene->fAlpha = 0.0f;
      fTargetOpen = !fOpen;
      rebuilt = true;
    }
    if (fTargetOpen != fOpen) {
      fTargetOpen = fOpen;
      fScene->fadeTo(fOpen ? 1.0f : 0.0f, 120.0,
                     scene::Easing::kOutQuint);
    }
    fScene->updateTree(frame.fNowMs);
    fScene->layoutIfNeeded(skia::SkRect::MakeWH(
        static_cast<float>(frame.fScreenW),
        static_cast<float>(frame.fScreenH)));
    if (rebuilt) {
      fScene->markDamaged();
    }
    fScene->setHover(frame.fMouseX, frame.fMouseY);
  }

  void render(skia::SkCanvas *canvas) {
    if (fScene && canvas != nullptr && this->visible()) {
      fScene->draw(canvas);
    }
  }

  [[nodiscard]] skia::SkRect takeDamage() {
    return fScene ? fScene->takeDamage() : skia::SkRect::MakeEmpty();
  }

  // Applies the click to the mod set; the root swallows misses while open.
  [[nodiscard]] bool click(float x, float y, osu::ModSet &mods) {
    if (!fOpen) {
      return false;
    }
    fChanged = false;
    if (fScene) {
      fScene->click(x, y);
    }
    if (fChanged) {
      mods = fActive;
    }
    return true;
  }

private:
  [[nodiscard]] std::unique_ptr<scene::Drawable>
  build(std::span<const ModEntry> entries) {
    auto root = scene::make<mod_select_detail::Root>({});
    root->add<nodes::Box>({}, skia::colorSetARGB(255, 8, 6, 12));

    auto *main = root->add<nodes::FillFlow>(
        {.roles = {scene::role<mod_select_style::Main>}},
        nodes::FillFlow::Direction::kVertical);
    main->fWrap = false;
    main->fCrossAlign = scene::Align::kMiddle;
    main->add<scene::Drawable>(
        {.roles = {scene::role<mod_select_style::Spacer>}});

    auto *columns = main->add<nodes::Grid>(
        {.roles = {scene::role<mod_select_style::Columns>}});
    columns->setColumns({nodes::Grid::Track::fraction(),
                         nodes::Grid::Track::fraction()});
    columns->fColumnGap = 40.0f;
    for (int column = 0; column < static_cast<int>(kModColumns.size());
         ++column) {
      auto *flow = columns->add<nodes::FillFlow>(
          {.roles = {scene::role<mod_select_style::Column>}},
          nodes::FillFlow::Direction::kVertical, 0.0f, 12.0f);
      flow->fWrap = false;
      flow->add<nodes::Text>(
          {.roles = {scene::role<mod_select_style::Header>}},
          kModColumns[static_cast<std::size_t>(column)], 16.0f,
          palette::kAccent2);
      for (const ModEntry &entry : entries) {
        if (entry.fColumn != column) {
          continue;
        }
        flow->add<mod_select_detail::Card>(
            {.roles = {scene::role<mod_select_style::Card>}}, entry, &fActive,
            [this](osu::ModSet flag) { this->toggleFlag(flag); });
      }
    }
    root->add<nodes::Text>(
        {.roles = {scene::role<mod_select_style::Footer>}},
        "click a mod to toggle    Esc to close", 14.0f, skia::kWhite);
    root->setStyleSheet<ModSelectTheme>();
    return root;
  }

  [[nodiscard]] static std::size_t
  shapeOf(std::span<const ModEntry> entries) {
    std::size_t shape = entries.size();
    for (const ModEntry &entry : entries) {
      const auto mix = [&shape](std::size_t value) {
        shape ^= value + 0x9e3779b9U + (shape << 6U) + (shape >> 2U);
      };
      mix(static_cast<std::uint32_t>(entry.fFlag));
      mix(static_cast<std::size_t>(entry.fColumn));
      mix(std::hash<std::string_view>{}(entry.fAcronym));
      mix(std::hash<std::string_view>{}(entry.fName));
      mix(std::hash<std::string_view>{}(entry.fDescription));
      mix(static_cast<std::size_t>(std::bit_cast<std::uint64_t>(
          entry.fMultiplier)));
    }
    return shape;
  }

  void toggleFlag(osu::ModSet flag) {
    fActive = (fActive & flag) != osu::mod::kNone ? without(fActive, flag)
                                                  : (fActive | flag);
    // Speed and difficulty mods are mutually exclusive, as in lazer.
    if (hasMod(fActive, osu::mod::kDoubleTime) &&
        flag == osu::mod::kDoubleTime) {
      fActive = without(fActive, osu::mod::kHalfTime);
    }
    if (hasMod(fActive, osu::mod::kHalfTime) && flag == osu::mod::kHalfTime) {
      fActive = without(fActive, osu::mod::kDoubleTime);
    }
    if (hasMod(fActive, osu::mod::kHardRock) && flag == osu::mod::kHardRock) {
      fActive = without(fActive, osu::mod::kEasy);
    }
    if (hasMod(fActive, osu::mod::kEasy) && flag == osu::mod::kEasy) {
      fActive = without(fActive, osu::mod::kHardRock);
    }
    fChanged = true;
  }

  [[nodiscard]] static osu::ModSet without(osu::ModSet set,
                                           osu::ModSet flag) {
    return osu::ModSet(static_cast<std::uint32_t>(set) &
                       ~static_cast<std::uint32_t>(flag));
  }

  [[nodiscard]] static bool hasMod(osu::ModSet set, osu::ModSet flag) {
    return (set & flag) != osu::mod::kNone;
  }

  bool fOpen = false;
  bool fTargetOpen = false;
  bool fChanged = false;
  std::size_t fShape = 0;
  osu::ModSet fActive = osu::mod::kNone;
  std::unique_ptr<scene::Drawable> fScene;
};

// ---- Video export dialog --------------------------------------------------
class ExportDialog {
public:
  [[nodiscard]] bool open() const noexcept { return fOpen; }
  void show() {
    fOpen = true;
    fStatus.clear();
  }
  void close() noexcept { fOpen = false; }
  void setStatus(std::string status) {
    fStatusChanged = fStatusChanged || status != fStatus;
    fStatus = std::move(status);
  }

  // Whether the line of status under the buttons has changed since this was
  // last asked. A dialog waiting for an answer is a still picture; one
  // writing a video is not, and this is the difference.
  // The box it draws, which is all of it: the rest of the screen behind the
  // dim does not change while it is up.
  [[nodiscard]] static skia::SkRect bounds(int screenW, int screenH) {
    const float sw = static_cast<float>(screenW);
    const float sh = static_cast<float>(screenH);
    const float w = std::min(520.0f, sw * 0.6f);
    const float h = 300.0f;
    return skia::SkRect::MakeXYWH((sw - w) * 0.5f, (sh - h) * 0.5f, w, h);
  }

  // The line under the buttons, which is the only thing that moves while a
  // video is being written: a per cent that counts up.
  [[nodiscard]] static skia::SkRect statusBounds(int screenW, int screenH) {
    const skia::SkRect box = bounds(screenW, screenH);
    return skia::SkRect::MakeLTRB(box.fLeft + 8.0f, box.fBottom - 42.0f,
                                  box.fRight - 8.0f, box.fBottom - 8.0f);
  }

  [[nodiscard]] bool takeStatusChanged() noexcept {
    const bool changed = fStatusChanged;
    fStatusChanged = false;
    return changed;
  }
  [[nodiscard]] int preset() const noexcept { return fPreset; }

  // A size typed in rather than picked. Zero when the field is empty or does
  // not parse, which is when the presets are used instead.
  [[nodiscard]] std::pair<int, int> customSize() const {
    const auto cross = fCustom.find('x');
    if (cross == std::string::npos) {
      return {0, 0};
    }
    int width = 0;
    int height = 0;
    const auto toNumber = [](std::string_view text) {
      int value = 0;
      for (const char c : text) {
        if (c < '0' || c > '9' || value > 20000) {
          return 0;
        }
        value = value * 10 + (c - '0');
      }
      return value;
    };
    width = toNumber(std::string_view(fCustom).substr(0, cross));
    height = toNumber(std::string_view(fCustom).substr(cross + 1));
    // Even numbers, since the encoders want them; and something a codec will
    // actually accept at the top end.
    if (width < 64 || height < 64 || width > 7680 || height > 4320) {
      return {0, 0};
    }
    return {width & ~1, height & ~1};
  }

  // Digits and the cross between them; anything else is ignored, which is
  // simpler than explaining it afterwards.
  void typeInSize(char c) {
    if ((c >= '0' && c <= '9') || c == 'x' || c == 'X') {
      if (fCustom.size() < 12) {
        fCustom.push_back(c == 'X' ? 'x' : c);
        fCustomEdited = true;
      }
    }
  }

  void backspaceSize() {
    if (!fCustom.empty()) {
      fCustom.pop_back();
      fCustomEdited = true;
    }
  }

  // Which of its pieces the pointer is on, so a client repainting regions can
  // repaint when that changes rather than while the pointer is anywhere near.
  [[nodiscard]] int hotElement(float x, float y) const {
    for (std::size_t i = 0; i < fHits.size(); ++i) {
      if (fHits[i].contains(x, y)) {
        return static_cast<int>(i);
      }
    }
    return -1;
  }

  [[nodiscard]] bool takeEdited() noexcept {
    const bool edited = fCustomEdited;
    fCustomEdited = false;
    return edited;
  }

  void draw(skia::SkCanvas *canvas, skia::SkFont &font, int screenW,
            int screenH, float mouseX, float mouseY) {
    fHits.clear();
    if (!fOpen) {
      return;
    }
    const paint::Painter p(canvas, font);
    const float sw = static_cast<float>(screenW);
    const float sh = static_cast<float>(screenH);
    p.fillRect(skia::SkRect::MakeXYWH(0, 0, sw, sh),
               skia::colorSetARGB(200, 8, 6, 12));

    const float w = std::min(520.0f, sw * 0.6f);
    const float h = 300.0f;
    const skia::SkRect box =
        skia::SkRect::MakeXYWH((sw - w) * 0.5f, (sh - h) * 0.5f, w, h);
    p.fillRounded(box, 14.0f, palette::kBackground5);
    p.strokeRounded(box, 14.0f, palette::kAccent, 2.0f);
    p.textCentered("export replay as video", box.centerX(), box.fTop + 46.0f,
                   24.0f, skia::kWhite);
    p.textCentered("resolution", box.centerX(), box.fTop + 82.0f, 14.0f,
                   skia::kWhite, 0.6f);

    const float bw = (w - 80.0f) / static_cast<float>(kVideoPresets.size());
    for (std::size_t i = 0; i < kVideoPresets.size(); ++i) {
      const skia::SkRect r = skia::SkRect::MakeXYWH(
          box.fLeft + 40.0f + static_cast<float>(i) * bw, box.fTop + 100.0f,
          bw - 8.0f, 40.0f);
      fHits.push_back(r);
      const bool active = static_cast<int>(i) == fPreset;
      p.fillRounded(r, 8.0f, active ? palette::kAccent : palette::kCardBg);
      p.textCentered(kVideoPresets[i].fLabel, r.centerX(), r.centerY() + 5.0f,
                     14.0f,
                     active ? skia::colorSetARGB(255, 24, 18, 30)
                            : skia::kWhite);
    }

    // Or a size typed in. Empty, it says what it is for; filled, it is what
    // gets rendered, and the presets above stop being the answer.
    const skia::SkRect custom = skia::SkRect::MakeXYWH(
        box.fLeft + 40.0f, box.fTop + 152.0f, w - 80.0f, 32.0f);
    fHits.push_back(custom);
    const auto [customWidth, customHeight] = this->customSize();
    const bool usingCustom = customWidth > 0;
    p.fillRounded(custom, 8.0f, usingCustom ? palette::kAccent : palette::kCardBg);
    p.textCentered(fCustom.empty() ? "or type a size, like 2560x1440"
                                   : fCustom,
                   custom.centerX(), custom.centerY() + 5.0f, 14.0f,
                   usingCustom ? skia::colorSetARGB(255, 24, 18, 30)
                               : skia::kWhite,
                   fCustom.empty() ? 0.5f : 1.0f);

    const skia::SkRect go = skia::SkRect::MakeXYWH(box.centerX() - 110.0f,
                                                   box.fBottom - 92.0f, 220.0f,
                                                   44.0f);
    fHits.push_back(go);
    p.fillRounded(go, 10.0f,
                  go.contains(mouseX, mouseY) ? palette::kCardSel : palette::kCardBg);
    p.strokeRounded(go, 10.0f, palette::kAccent2, 2.0f);
    p.textCentered("render", go.centerX(), go.centerY() + 6.0f, 17.0f,
                   skia::kWhite);
    p.textCentered(fStatus.empty() ? "requires ffmpeg in PATH    Esc to cancel"
                                   : fStatus,
                   box.centerX(), box.fBottom - 24.0f, 13.0f, skia::kWhite,
                   0.75f);
  }

  // Returns true when "render" was pressed.
  [[nodiscard]] bool click(float x, float y) {
    if (!fOpen) {
      return false;
    }
    for (std::size_t i = 0; i < fHits.size(); ++i) {
      if (!fHits[i].contains(x, y)) {
        continue;
      }
      if (i < kVideoPresets.size()) {
        fPreset = static_cast<int>(i);
        fCustom.clear(); // picking one is the way to stop using a typed size
        fCustomEdited = true;
        return false;
      }
      if (i == kVideoPresets.size()) {
        return false; // the size field: clicking it does nothing but focus
      }
      return true;
    }
    return false;
  }

private:
  bool fOpen = false;
  int fPreset = 1; // 1080p
  std::string fCustom;
  bool fCustomEdited = false;
  std::string fStatus;
  bool fStatusChanged = true;
  std::vector<skia::SkRect> fHits;
};

} // namespace client
