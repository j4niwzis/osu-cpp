export module client.overlays;

import std;
import skia;
import osu;
import skiff.paint;
import skiff.scene;
import skiff.nodes;
import skiff.widgets.button;
import skiff.widgets.textbox;
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
  [[nodiscard]] bool visible() const noexcept {
    return fOpen || (fScene && fScene->alpha() > 0.002f);
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
      fScene->setAlpha(0.0f);
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

  [[nodiscard]] skiff::scene::FrameResult finishFrame() {
    return fScene ? fScene->finishFrame() : skiff::scene::FrameResult{};
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
    main->setWrap(false);
    main->setCrossAlign(scene::Align::kMiddle);
    main->add<scene::Drawable>(
        {.roles = {scene::role<mod_select_style::Spacer>}});

    auto *columns = main->add<nodes::Grid>(
        {.roles = {scene::role<mod_select_style::Columns>}});
    columns->setColumns({nodes::Grid::Track::fraction(),
                         nodes::Grid::Track::fraction()});
    columns->setGaps(0.0f, 40.0f);
    for (int column = 0; column < static_cast<int>(kModColumns.size());
         ++column) {
      auto *flow = columns->add<nodes::FillFlow>(
          {.roles = {scene::role<mod_select_style::Column>}},
          nodes::FillFlow::Direction::kVertical, 0.0f, 12.0f);
      flow->setWrap(false);
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
namespace export_dialog_style {
struct Dim;
struct Title;
struct Resolution;
struct Presets;
struct Preset;
struct Custom;
struct Render;
struct Status;
} // namespace export_dialog_style

namespace export_dialog_detail {

class Root : public scene::TypedDrawable<Root> {
protected:
  bool acceptsInput() const override { return true; }
  bool hoverChangesAppearance() const override { return false; }
  bool onClick(float, float) override { return true; }
};

class Panel : public scene::TypedDrawable<Panel, nodes::Box> {
public:
  using Base = scene::TypedDrawable<Panel, nodes::Box>;
  Panel() : Base(palette::kBackground5) {}

protected:
  void drawSelf(skia::SkCanvas *canvas, float alpha) override {
    nodes::Box::drawSelf(canvas, alpha);
    skia::SkFont *font = paint::defaultFont();
    if (font != nullptr) {
      paint::Painter(canvas, *font)
          .strokeRounded(fBounds, fCornerRadius, palette::kAccent, 2.0f,
                         alpha);
    }
  }
};

} // namespace export_dialog_detail

struct ExportDialogTheme {
  static constexpr auto styles =
      scene::makeStyleSheet()
          .rule(scene::select<export_dialog_detail::Root>(),
                {.width = 1.0f,
                 .height = 1.0f,
                 .relativeSize = scene::Axes::kBoth})
          .rule(scene::select<nodes::Box, export_dialog_style::Dim>(),
                {.width = 1.0f,
                 .height = 1.0f,
                 .relativeSize = scene::Axes::kBoth,
                 .alpha = 200.0f / 255.0f,
                 .backgroundColour = skia::colorSetARGB(255, 8, 6, 12)})
          .rule(scene::select<export_dialog_detail::Panel>(),
                {.anchor = scene::Anchor::kCentre,
                 .origin = scene::Anchor::kCentre,
                 .width = 0.6f,
                 .height = 300.0f,
                 .relativeSize = scene::Axes::kX,
                 .maxWidth = 520.0f,
                 .padding = scene::Margin{0.0f, 40.0f, 0.0f, 40.0f},
                 .cornerRadius = 14.0f,
                 .backgroundColour = palette::kBackground5})
          .rule(scene::select<nodes::Text, export_dialog_style::Title>(),
                {.anchor = scene::Anchor::kTopCentre,
                 .origin = scene::Anchor::kTopCentre,
                 .y = 18.0f,
                 .colour = skia::kWhite,
                 .fontSize = 24.0f})
          .rule(scene::select<nodes::Text,
                              export_dialog_style::Resolution>(),
                {.anchor = scene::Anchor::kTopCentre,
                 .origin = scene::Anchor::kTopCentre,
                 .y = 66.0f,
                 .alpha = 0.6f,
                 .colour = skia::kWhite,
                 .fontSize = 14.0f})
          .rule(scene::select<nodes::FillFlow,
                              export_dialog_style::Presets>(),
                {.y = 100.0f,
                 .width = 1.0f,
                 .height = 40.0f,
                 .relativeSize = scene::Axes::kX})
          .rule(scene::select<skiff::widgets::Button,
                              export_dialog_style::Preset>(),
                {.height = 40.0f, .grow = scene::Axes::kX})
          .rule(scene::select<skiff::widgets::TextBox,
                              export_dialog_style::Custom>(),
                {.y = 152.0f,
                 .width = 1.0f,
                 .height = 32.0f,
                 .relativeSize = scene::Axes::kX})
          .rule(scene::select<skiff::widgets::Button,
                              export_dialog_style::Render>(),
                {.anchor = scene::Anchor::kBottomCentre,
                 .origin = scene::Anchor::kBottomCentre,
                 .y = -48.0f,
                 .width = 220.0f,
                 .height = 44.0f})
          .rule(scene::select<nodes::Text, export_dialog_style::Status>(),
                {.anchor = scene::Anchor::kBottomCentre,
                 .origin = scene::Anchor::kBottomCentre,
                 .y = -16.0f,
                 .alpha = 0.75f,
                 .colour = skia::kWhite,
                 .fontSize = 13.0f});
};

class ExportDialog {
public:
  [[nodiscard]] bool open() const noexcept { return fOpen; }
  void show() {
    fOpen = true;
    fStatus.clear();
  }
  void close() noexcept { fOpen = false; }
  void setStatus(std::string status) {
    fStatus = std::move(status);
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
      }
    }
  }

  void backspaceSize() {
    if (!fCustom.empty()) {
      fCustom.pop_back();
    }
  }

  void update(skia::SkFont &font, int screenW, int screenH, float mouseX,
              float mouseY, double nowMs) {
    if (!fOpen) {
      return;
    }
    nodes::Text::setFont(&font);
    bool rebuilt = false;
    if (!fScene) {
      fScene = this->build();
      rebuilt = true;
    }
    const auto [customWidth, customHeight] = this->customSize();
    const bool usingCustom = customWidth > 0;
    for (std::size_t i = 0; i < fPresetButtons.size(); ++i) {
      fPresetButtons[i]->setPrimary(
          !usingCustom && static_cast<int>(i) == fPreset);
    }
    fCustomBox->setText(fCustom);
    fCustomBox->setSelected(usingCustom);
    fCustomBox->tickCaret(nowMs, !fCustom.empty());
    fStatusText->setText(
        fStatus.empty() ? "requires ffmpeg in PATH    Esc to cancel" : fStatus);

    const skia::SkRect screen = skia::SkRect::MakeWH(
        static_cast<float>(screenW), static_cast<float>(screenH));
    fScene->updateTree(nowMs);
    fScene->layoutIfNeeded(screen);
    if (rebuilt) {
      fScene->markDamaged();
    }
    fScene->setHover(mouseX, mouseY);
  }

  void render(skia::SkCanvas *canvas) {
    if (fOpen && fScene && canvas != nullptr) {
      fScene->draw(canvas);
    }
  }

  [[nodiscard]] skiff::scene::FrameResult finishFrame() {
    return fScene ? fScene->finishFrame() : skiff::scene::FrameResult{};
  }

  // Returns true when "render" was pressed.
  [[nodiscard]] bool click(float x, float y) {
    if (!fOpen) {
      return false;
    }
    fRenderRequested = false;
    if (fScene) {
      fScene->click(x, y);
    }
    return fRenderRequested;
  }

private:
  inline static const skiff::widgets::Theme kPresetTheme = {
      .fSurface = palette::kCardBg,
      .fSurfaceHover = palette::kCardSel,
      .fSurfaceActive = palette::kCardSel,
      .fText = skia::kWhite,
      .fLabel = skia::kWhite,
      .fTextDim = skia::kWhite,
      .fTextFaint = skia::colorSetARGB(255, 150, 140, 160),
      .fAccent = palette::kAccent,
      .fOnAccent = skia::colorSetARGB(255, 24, 18, 30),
      .fCorner = 8.0f,
      .fFontSize = 14.0f,
      .fRowHeight = 40.0f,
      .fPaddingX = 12.0f,
  };

  inline static const skiff::widgets::Theme kCustomTheme = {
      .fSurface = palette::kCardBg,
      .fSurfaceHover = palette::kCardSel,
      .fSurfaceActive = palette::kCardSel,
      .fText = skia::kWhite,
      .fLabel = skia::kWhite,
      .fTextDim = skia::kWhite,
      .fTextFaint = skia::colorSetARGB(255, 170, 160, 180),
      .fAccent = palette::kAccent,
      .fOnAccent = skia::colorSetARGB(255, 24, 18, 30),
      .fCorner = 8.0f,
      .fFontSize = 14.0f,
      .fRowHeight = 32.0f,
      .fPaddingX = 12.0f,
  };

  [[nodiscard]] std::unique_ptr<scene::Drawable> build() {
    auto root = scene::make<export_dialog_detail::Root>({});
    root->add<nodes::Box>(
        {.roles = {scene::role<export_dialog_style::Dim>}},
        skia::colorSetARGB(255, 8, 6, 12));
    auto *panel = root->add<export_dialog_detail::Panel>({});
    panel->add<nodes::Text>(
        {.roles = {scene::role<export_dialog_style::Title>}},
        "export replay as video", 24.0f, skia::kWhite);
    panel->add<nodes::Text>(
        {.roles = {scene::role<export_dialog_style::Resolution>}},
        "resolution", 14.0f, skia::kWhite);

    auto *presets = panel->add<nodes::FillFlow>(
        {.roles = {scene::role<export_dialog_style::Presets>}},
        nodes::FillFlow::Direction::kHorizontal, 8.0f, 0.0f);
    presets->setWrap(false);
    for (std::size_t i = 0; i < kVideoPresets.size(); ++i) {
      auto *button = presets->add<skiff::widgets::Button>(
          {.roles = {scene::role<export_dialog_style::Preset>}},
          kVideoPresets[i].fLabel, [this, i] { this->choosePreset(i); });
      button->setTheme(kPresetTheme);
      fPresetButtons.push_back(button);
    }

    fCustomBox = panel->add<skiff::widgets::TextBox>(
        {.roles = {scene::role<export_dialog_style::Custom>}},
        "or type a size, like 2560x1440");
    fCustomBox->setTheme(kCustomTheme);

    auto *render = panel->add<skiff::widgets::Button>(
        {.roles = {scene::role<export_dialog_style::Render>}}, "render",
        [this] { fRenderRequested = true; });
    render->setTheme(kPresetTheme);
    render->setPrimary(true);

    fStatusText = panel->add<nodes::Text>(
        {.roles = {scene::role<export_dialog_style::Status>}}, "", 13.0f,
        skia::kWhite);
    root->setStyleSheet<ExportDialogTheme>();
    return root;
  }

  void choosePreset(std::size_t index) {
    fPreset = static_cast<int>(index);
    fCustom.clear();
  }

  bool fOpen = false;
  int fPreset = 1; // 1080p
  std::string fCustom;
  std::string fStatus;
  bool fRenderRequested = false;
  std::unique_ptr<scene::Drawable> fScene;
  std::vector<skiff::widgets::Button *> fPresetButtons;
  skiff::widgets::TextBox *fCustomBox = nullptr;
  nodes::Text *fStatusText = nullptr;
};

} // namespace client
