export module client.settingspanel;

import std;
import skia;
import skiff.paint;
import client.palette;
import client.settings;
import skiff.scene;
import skiff.nodes;
import skiff.widgets.dropdown;
import skiff.widgets.sliderbar;

// The framework lives in skiff:: now; these keep the screens below
// writing scene:: and nodes:: as they did when it sat in client::.
namespace scene = skiff::scene;
namespace nodes = skiff::nodes;

// skiff::paint is the framework's drawing side; the short name keeps
// the lines below at the width they were written at.
namespace paint = skiff::paint;
namespace widgets = skiff::widgets;

// The panel's own shades for an open list: the plate, the rows on it, and a
// row under the pointer.
// A row's own control: the unfilled part of a track, an unlit pill, and the
// knob on either.
inline const widgets::Theme kControlTheme = {
    .fSurface = skia::colorSetARGB(255, 58, 48, 70),
    .fText = skia::kWhite,
    .fAccent = client::palette::kAccent,
};

inline const widgets::Theme kListTheme = {
    .fSurface = skia::colorSetARGB(255, 32, 26, 40),
    .fSurfaceHover = skia::colorSetARGB(255, 44, 36, 54),
    .fSurfaceActive = client::palette::kCardSel,
    .fText = skia::kWhite,
};

namespace settings_style {
struct Root;
struct Dim;
struct Shell;
struct Sidebar;
struct Sections;
struct Section;
struct Panel;
struct Title;
struct Subtitle;
struct Scroll;
struct Column;
struct SectionHeader;
struct Row;
struct ChoiceControl;
struct Slider;
struct Toggle;
struct ChoiceList;
struct Hint;
} // namespace settings_style

struct SettingsTheme {
  static constexpr auto styles =
      scene::makeStyleSheet()
          .rule(scene::selectAny<settings_style::Root>(),
                {.width = 1.0f,
                 .height = 1.0f,
                 .relativeSize = scene::Axes::kBoth})
          .rule(scene::select<nodes::Box, settings_style::Dim>(),
                {.width = 1.0f,
                 .height = 1.0f,
                 .relativeSize = scene::Axes::kBoth,
                 .backgroundColour = skia::colorSetARGB(255, 0, 0, 0)})
          .rule(scene::selectAny<settings_style::Shell>(),
                {.width = 570.0f,
                 .height = 1.0f,
                 .relativeSize = scene::Axes::kY})
          .rule(scene::select<nodes::Box, settings_style::Sidebar>(),
                {.width = 170.0f,
                 .height = 1.0f,
                 .relativeSize = scene::Axes::kY,
                 .alpha = 252.0f / 255.0f,
                 .backgroundColour = skia::colorSetARGB(255, 23, 19, 30)})
          .rule(scene::select<nodes::FillFlow, settings_style::Sections>(),
                {.y = 70.0f,
                 .width = 1.0f,
                 .relativeSize = scene::Axes::kX,
                 .autoSize = scene::Axes::kY})
          .rule(scene::selectAny<settings_style::Section>(),
                {.width = 1.0f,
                 .height = 46.0f,
                 .relativeSize = scene::Axes::kX})
          .rule(scene::select<nodes::Box, settings_style::Panel>(),
                {.x = 170.0f,
                 .width = 400.0f,
                 .height = 1.0f,
                 .relativeSize = scene::Axes::kY,
                 .alpha = 250.0f / 255.0f,
                 .backgroundColour = skia::colorSetARGB(255, 31, 25, 40)})
          .rule(scene::select<nodes::Text, settings_style::Title>(),
                {.x = 20.0f,
                 .y = 26.0f,
                 .colour = skia::kWhite,
                 .fontSize = 30.0f})
          .rule(scene::select<nodes::Text, settings_style::Subtitle>(),
                {.x = 20.0f,
                 .y = 65.0f,
                 .alpha = 0.6f,
                 .colour = skia::kWhite,
                 .fontSize = 13.0f})
          .rule(scene::select<nodes::ScrollContainer,
                              settings_style::Scroll>(),
                {.width = 1.0f,
                 .height = 1.0f,
                 .relativeSize = scene::Axes::kBoth,
                 .margin = scene::Margin{110.0f, 0.0f, 0.0f, 0.0f}})
          .rule(scene::select<nodes::FillFlow, settings_style::Column>(),
                {.width = 1.0f,
                 .relativeSize = scene::Axes::kX,
                 .autoSize = scene::Axes::kY,
                 .margin = scene::Margin{8.0f, 0.0f, 0.0f, 0.0f}})
          .rule(scene::select<nodes::Text, settings_style::SectionHeader>(),
                {.x = 20.0f,
                 .colour = client::palette::kAccent,
                 .fontSize = 18.0f})
          .rule(scene::selectAny<settings_style::Row>(),
                {.width = 1.0f,
                 .relativeSize = scene::Axes::kX,
                 .padding = scene::Margin::horizontal(20.0f)})
          .rule(scene::selectAny<settings_style::ChoiceControl>(),
                {.anchor = scene::Anchor::kTopRight,
                 .origin = scene::Anchor::kTopRight,
                 .y = 3.0f,
                 .height = 24.0f})
          .rule(scene::select<widgets::SliderBar, settings_style::Slider>(),
                {.y = 30.0f, .height = 6.0f})
          .rule(scene::select<widgets::Toggle, settings_style::Toggle>(),
                {.anchor = scene::Anchor::kTopRight,
                 .origin = scene::Anchor::kTopRight,
                 .x = -6.0f,
                 .y = 4.0f})
          .rule(scene::select<widgets::DropdownList,
                              settings_style::ChoiceList>(),
                {.anchor = scene::Anchor::kBottomLeft,
                 .origin = scene::Anchor::kTopLeft,
                 .y = 4.0f,
                 .width = 1.0f,
                 .relativeSize = scene::Axes::kX})
          .rule(scene::select<nodes::Text, settings_style::Hint>(),
                {.anchor = scene::Anchor::kBottomLeft,
                 .origin = scene::Anchor::kBottomLeft,
                 .x = 20.0f,
                 .y = -21.0f,
                 .alpha = 0.5f,
                 .colour = skia::kWhite,
                 .fontSize = 12.0f});
};

export namespace client {

// The view half of osu!lazer's SettingsPanel: a 170px sidebar and a 400px
// content column sliding in from the left over TRANSITION_LENGTH with
// OutQuint, one continuous scroll of every section, the sidebar scrolling to
// a section and tracking which one the viewport is in.
//
// Built as a scene tree, like the rest of the client: the panel decides what
// exists and what changed, and the frame is only drawn when something did.
class SettingsPanel {
public:
  static constexpr float kSidebarWidth = 170.0f;  // EXPANDED_WIDTH
  static constexpr float kPanelWidth = 400.0f;    // PANEL_WIDTH
  static constexpr float kContentMargins = 20.0f; // CONTENT_MARGINS
  static constexpr float kItemSpacing = 14.0f;    // ITEM_SPACING
  static constexpr float kSidebarItemHeight = 46.0f;
  static constexpr float kTransitionMs = 600.0f;  // TRANSITION_LENGTH
  static constexpr float kContentTop = 110.0f;    // under the panel's header

  struct Frame {
    int fScreenW = 0, fScreenH = 0;
    float fMouseX = 0.0f, fMouseY = 0.0f;
    double fNowMs = 0.0;
    double fDtMs = 16.0;
  };

  enum class Hit : std::uint8_t { kNone, kSwallowed, kChanged };

  [[nodiscard]] bool open() const noexcept { return fOpen; }
  [[nodiscard]] float slide() const noexcept { return fSlide; }
  [[nodiscard]] float occupiedWidth() const noexcept {
    return (kSidebarWidth + kPanelWidth) * fSlide;
  }

  // Still sliding: the only time an untouched panel needs frames.
  //
  // Asked of the clock, not of fSlide, which only advances while the panel is
  // being updated.
  [[nodiscard]] bool animating(double nowMs) const noexcept {
    return nowMs - fEnterWall < kTransitionMs;
  }

  [[nodiscard]] bool visible() const noexcept {
    return fOpen || fSlide > 0.002f;
  }

  void toggle(double nowMs) {
    fOpen = !fOpen;
    fEnterWall = nowMs;
  }

  void close(double nowMs) {
    if (fOpen) {
      fOpen = false;
      fEnterWall = nowMs;
    }
  }

  void scroll(float delta, float) {
    fScrollTicks += delta;
    fTouched = true;
  }

  // Something in the panel was clicked or dragged: whatever it did, the panel
  // draws something different next frame.
  void touched() noexcept { fTouched = true; }

  // Everything that decides what the panel looks like, with nothing drawn.
  // Returns both what changed and whether time can change it again. Keeping
  // those answers together prevents the caller from consuming the damage and
  // accidentally forgetting the continuation frame of a slide or scroll.
  [[nodiscard]] scene::FrameResult update(skia::SkFont &font,
                                          Settings &settings,
                                          const Frame &frame) {
    fFont = &font;
    fSettings = &settings;
    fMouseX = frame.fMouseX;
    fMouseY = frame.fMouseY;

    const float progress =
        static_cast<float>((frame.fNowMs - fEnterWall) / kTransitionMs);
    const float eased = paint::outQuint(std::clamp(progress, 0.0f, 1.0f));
    fSlide = fOpen ? eased : 1.0f - eased;
    const float sw = static_cast<float>(frame.fScreenW);
    const float sh = static_cast<float>(frame.fScreenH);

    if (!this->visible()) {
      fScene.reset();
      fTouched = false;
      fScrollTicks = 0.0f;
      return {};
    }

    if (!fScene || fBuiltFor != settings.defs().size()) {
      fBuiltFor = settings.defs().size();
      fScene = this->build();
      fTouched = true;
    }

    // The slide is a position rather than a transform: it is driven by the
    // same clock the client asks about, so there is one answer to "is this
    // still moving".
    const float shellX = -(kSidebarWidth + kPanelWidth) * (1.0f - fSlide);
    const float fade = std::min(1.0f, fSlide * 2.0f);
    if (fShell != nullptr && fShell->x() != shellX) {
      fShell->setPosition(shellX, fShell->y());
    }
    if (fDim != nullptr) {
      fDim->setAlpha(fade * (110.0f / 255.0f));
    }
    if (fShell != nullptr) {
      fShell->setAlpha(fade);
    }

    // A viewport of nothing after the last row. Not decoration: clicking the
    // last section in the sidebar scrolls its header to the top, and it can
    // only get there if there is a screen's worth of column behind it.
    const float tail = sh - kContentTop;
    if (fColumn != nullptr && fColumn->padding().fBottom != tail) {
      scene::Margin padding = fColumn->padding();
      padding.fBottom = tail;
      fColumn->setPadding(padding);
    }

    const skia::SkRect screen = skia::SkRect::MakeWH(sw, sh);
    fScene->updateTree(frame.fNowMs);
    fScene->layoutIfNeeded(screen);
    if (fScrollTicks != 0.0f && fScroll != nullptr) {
      // After the layout, so the wheel lands on something with bounds.
      fScene->scroll(frame.fMouseX, frame.fMouseY, fScrollTicks);
      fScrollTicks = 0.0f;
      fScene->layoutIfNeeded(screen);
    }
    this->trackViewportSection();
    fScene->setHover(frame.fMouseX, frame.fMouseY);

    scene::FrameResult result = fScene->finishFrame();
    skia::SkRect &damage = result.fDamage;
    // A column in motion moves everything in it, by fractions of a pixel per
    // frame, and a clip is a whole number of them: the edges of what moved
    // and the edges of what is repainted stop agreeing, which is a seam at
    // every row. While it glides, the column is the answer rather than the
    // rows -- and it is what the rows add up to anyway.
    if (fScroll != nullptr && fScroll->moving()) {
      fTouched = true;
    }
    const bool sliding = this->animating(frame.fNowMs);
    result.fWantsAnotherFrame = result.fWantsAnotherFrame || sliding;
    if (sliding || fTouched) {
      // Sliding dims the whole screen with it, which is one of the few honest
      // whole-screen repaints there are.
      damage = sliding ? screen : damage;
      if (fTouched && !sliding) {
        damage.join(skia::SkRect::MakeXYWH(
            std::max(0.0f, shellX), 0.0f, kSidebarWidth + kPanelWidth, sh));
      }
    }
    fTouched = false;
    return result;
  }

  void render(skia::SkCanvas *canvas) {
    if (fScene && canvas != nullptr) {
      fScene->draw(canvas);
    }
  }

  [[nodiscard]] scene::Drawable *sceneRoot() noexcept { return fScene.get(); }

  // Returns kChanged when a value was touched (so the caller can apply and
  // persist), kSwallowed when the overlay consumed the click regardless.
  [[nodiscard]] Hit click(float x, float y, bool pressed, Settings &settings) {
    if (fSlide < 0.5f || !fScene) {
      return Hit::kNone;
    }
    if (!pressed) {
      const bool wasDragging = fDragging >= 0;
      fDragging = -1;
      return wasDragging ? Hit::kChanged : Hit::kSwallowed;
    }
    fAction = {};
    fScene->dispatchPointer(scene::PointerAction::kDown, x, y);
    switch (fAction.fKind) {
    case Action::kNone:
      // A click anywhere else in the panel closes an open list, and is
      // swallowed if it landed on the panel at all.
      if (fOpenChoice >= 0) {
        this->setOpenChoice(-1);
        return Hit::kSwallowed;
      }
      return x < (kSidebarWidth + kPanelWidth) * fSlide ? Hit::kSwallowed
                                                        : Hit::kNone;
    case Action::kRestore:
      settings.restoreDefault(fAction.fIndex);
      this->markRow(fAction.fIndex);
      return Hit::kChanged;
    case Action::kToggle:
      settings.toggle(fAction.fIndex);
      this->markRow(fAction.fIndex);
      return Hit::kChanged;
    case Action::kChoiceOpen:
      this->setOpenChoice(fOpenChoice == static_cast<int>(fAction.fIndex)
                              ? -1
                              : static_cast<int>(fAction.fIndex));
      return Hit::kSwallowed;
    case Action::kChoiceSet:
      settings.setChoice(fAction.fIndex, fAction.fOption);
      this->setOpenChoice(-1);
      return Hit::kChanged;
    case Action::kSlider:
      if (fOpenChoice >= 0) {
        this->setOpenChoice(-1);
      }
      fDragging = static_cast<int>(fAction.fIndex);
      this->drag(x, settings);
      return Hit::kChanged;
    case Action::kSection:
      this->scrollToSection(fAction.fIndex);
      return Hit::kSwallowed;
    }
    return Hit::kSwallowed;
  }

  bool drag(float x, Settings &settings) {
    if (fDragging < 0) {
      return false;
    }
    const auto index = static_cast<std::size_t>(fDragging);
    if (index >= fRowNodes.size() || fRowNodes[index] == nullptr) {
      return false;
    }
    const widgets::SliderBar *bar = fRowNodes[index]->slider();
    if (bar == nullptr) {
      return false;
    }
    settings.setFromFraction(index, bar->fractionAt(x));
    this->markRow(index);
    return true;
  }

  [[nodiscard]] bool dragging() const noexcept { return fDragging >= 0; }

private:
  // What a click in the tree asked for. The nodes do not touch the settings
  // themselves: click() owns that, because its caller has to know whether a
  // value changed in order to persist it.
  struct Action {
    enum Kind : std::uint8_t {
      kNone,
      kRestore,
      kToggle,
      kChoiceOpen,
      kChoiceSet,
      kSlider,
      kSection
    };
    Kind fKind = kNone;
    std::size_t fIndex = 0;
    int fOption = 0;
  };

  // ---- rows ---------------------------------------------------------------

  // One setting. It draws its own kind -- slider, toggle or choice -- and
  // notices when the value under it changes, which is the only way a row that
  // is never hovered ever needs repainting.
  // The box a choice row opens its list from. A node so that it sizes itself
  // to its widest option and the list can be hung off its bounds.
  class ChoiceControlNode : public scene::Drawable {
  public:
    ChoiceControlNode(SettingsPanel *owner, std::size_t index)
        : fOwner(owner), fIndex(index) {}

  protected:
    void measure(const skia::SkRect &) override {
      const paint::Painter measurer(nullptr, *fOwner->fFont);
      const auto &def = fOwner->fSettings->defs()[fIndex];
      float width = 0.0f;
      for (const auto &candidate : def.fOptions) {
        width = std::max(width, measurer.measure(candidate, 13.0f));
      }
      fWidth = width + 40.0f;
    }

    void update(double) override {
      const bool open = fOwner->fOpenChoice == static_cast<int>(fIndex);
      if (open != fDrawnOpen) {
        fDrawnOpen = open;
        this->markDamaged();
      }
    }

    void drawSelf(skia::SkCanvas *canvas, float alpha) override {
      const Settings &settings = *fOwner->fSettings;
      const auto &def = settings.defs()[fIndex];
      const paint::Painter p(canvas, *fOwner->fFont);
      const bool open = fOwner->fOpenChoice == static_cast<int>(fIndex);
      const auto index = static_cast<std::size_t>(settings.choice(def.fKey));
      const std::string &option = index < def.fOptions.size()
                                      ? def.fOptions[index]
                                      : def.fOptions.front();
      p.fillRounded(
          fBounds, 6.0f,
          open ? palette::kAccent : skia::colorSetARGB(255, 58, 48, 70), alpha);
      p.textClipped(option, fBounds.fLeft + 12.0f,
                    p.middleBaseline(fBounds, 13.0f), fBounds.width() - 32.0f,
                    13.0f, skia::kWhite, alpha);
      // The chevron, so it reads as something that opens.
      p.textClipped(open ? "^" : "v", fBounds.fRight - 18.0f,
                    p.middleBaseline(fBounds, 12.0f), 12.0f, 12.0f,
                    skia::kWhite, alpha * 0.8f);
    }

    bool acceptsInput() const override { return true; }

    bool onClick(float, float) override {
      fOwner->fAction = {Action::kChoiceOpen, fIndex, 0};
      return true;
    }

  private:
    SettingsPanel *fOwner;
    std::size_t fIndex;
    bool fDrawnOpen = false;
  };

  class RowNode : public scene::Drawable {
  public:
    RowNode(SettingsPanel *owner, std::size_t index)
        : fOwner(owner), fIndex(index) {
      switch (owner->fSettings->defs()[index].fKind) {
      case SettingKind::kChoice:
        fControl = this->add<ChoiceControlNode>(
            {.roles = {scene::role<settings_style::ChoiceControl>}}, owner,
            index);
        break;
      case SettingKind::kSlider:
        fSlider = this->add<widgets::SliderBar>(
            {.roles = {scene::role<settings_style::Slider>}});
        fSlider->setTheme(kControlTheme);
        break;
      case SettingKind::kToggle:
        fToggle = this->add<widgets::Toggle>(
            {.roles = {scene::role<settings_style::Toggle>}});
        fToggle->setTheme(kControlTheme);
        fToggle->fOnToggle = [owner, index] {
          owner->fAction = {Action::kToggle, index, 0};
        };
        break;
      }
    }

    [[nodiscard]] widgets::SliderBar *slider() const noexcept {
      return fSlider;
    }

    // Null unless this is a choice row. What its list is hung off.
    [[nodiscard]] scene::Drawable *control() const noexcept { return fControl; }

  protected:
    void measure(const skia::SkRect &) override {
      const auto &def = fOwner->fSettings->defs()[fIndex];
      // An open list does not make the row taller: it is drawn over what is
      // below it by an overlay above the column, the way a dropdown should
      // behave, rather than shoving the rest of the settings down the page.
      fHeight = def.fKind == SettingKind::kSlider ? 44.0f : 30.0f;
    }

    // The value is read out of the settings rather than held here, so this is
    // where a row finds out that something else changed it.
    void update(double) override {
      const Settings &settings = *fOwner->fSettings;
      const auto &def = settings.defs()[fIndex];
      const float value = def.fKind == SettingKind::kToggle
                              ? (settings.flag(def.fKey) ? 1.0f : 0.0f)
                          : def.fKind == SettingKind::kChoice
                              ? static_cast<float>(settings.choice(def.fKey))
                              : settings.value(def.fKey);
      const bool modified = settings.isModified(fIndex);
      if (value != fDrawnValue || modified != fDrawnModified) {
        fDrawnValue = value;
        fDrawnModified = modified;
        this->markDamaged();
      }
      if (fSlider != nullptr) {
        fSlider->setFraction((settings.value(def.fKey) - def.fMin) /
                             (def.fMax - def.fMin));
      }
      if (fToggle != nullptr) {
        fToggle->setOn(settings.flag(def.fKey));
      }
    }

    void drawSelf(skia::SkCanvas *canvas, float alpha) override {
      const Settings &settings = *fOwner->fSettings;
      const auto &def = settings.defs()[fIndex];
      const paint::Painter p(canvas, *fOwner->fFont);
      const skia::SkRect content = this->contentBox();
      const float baseline = fBounds.fTop + 16.0f;

      if (settings.isModified(fIndex)) {
        // The bar in the margin that says "this is not the default".
        p.fillRounded(skia::SkRect::MakeXYWH(content.fLeft - 12.0f,
                                             fBounds.fTop + 4.0f, 4.0f, 22.0f),
                      2.0f, palette::kAccent, alpha);
      }

      p.textClipped(def.fLabel, content.fLeft, baseline, content.width() * 0.62f,
                    14.0f, skia::kWhite, alpha * 0.95f);

      if (def.fKind == SettingKind::kSlider) {
        p.textClipped(settings.displayValue(fIndex), content.fRight - 76.0f,
                      baseline, 76.0f, 13.0f, skia::kWhite, alpha * 0.75f);
        return; // the bar is a child and draws itself
      }

      if (def.fKind == SettingKind::kChoice) {
        return; // the control is a child and draws itself
      }

      // A toggle's pill is a child and draws itself.
    }

    bool acceptsInput() const override { return true; }

    bool onClick(float x, float y) override {
      const auto &def = fOwner->fSettings->defs()[fIndex];
      const skia::SkRect content = this->contentBox();
      if (fOwner->fSettings->isModified(fIndex) && x < content.fLeft) {
        fOwner->fAction = {Action::kRestore, fIndex, 0};
        return true;
      }
      if (def.fKind == SettingKind::kChoice) {
        return true; // the control is a child and was asked first
      }
      if (def.fKind == SettingKind::kToggle) {
        fOwner->fAction = {Action::kToggle, fIndex, 0};
        return true;
      }
      fOwner->fAction = {Action::kSlider, fIndex, 0};
      return true;
    }

  private:
    SettingsPanel *fOwner;
    std::size_t fIndex;
    ChoiceControlNode *fControl = nullptr; // one of these three, by kind
    widgets::SliderBar *fSlider = nullptr;
    widgets::Toggle *fToggle = nullptr;
    float fDrawnValue = std::numeric_limits<float>::quiet_NaN();
    bool fDrawnModified = false;
  };

  // One entry in the sidebar: an icon, a label, and the elastic indicator
  // that grows when the viewport is inside that section.
  class SectionNode : public scene::Drawable {
  public:
    SectionNode(SettingsPanel *owner, std::size_t index)
        : fOwner(owner), fIndex(index) {}

  protected:
    bool settling() const override {
      return std::abs(fGrow -
                      (fOwner->fActiveSection == static_cast<int>(fIndex)
                           ? 1.0f
                           : 0.0f)) > scene::kSettled;
    }

    void update(double nowMs) override {
      const double dt = fLastMs > 0.0 ? nowMs - fLastMs : 16.0;
      fLastMs = nowMs;
      const bool active = fOwner->fActiveSection == static_cast<int>(fIndex);
      const float previous = fGrow;
      fGrow = paint::approach(fGrow, active ? 1.0f : 0.0f, 90.0f, dt);
      if (fGrow != previous) {
        this->markDamaged();
      }
    }

    void drawSelf(skia::SkCanvas *canvas, float alpha) override {
      const paint::Painter p(canvas, *fOwner->fFont);
      const bool active = fOwner->fActiveSection == static_cast<int>(fIndex);
      const float indicatorH = 4.0f + 14.0f * paint::outElasticHalf(fGrow);
      if (fGrow > 0.01f) {
        p.fillRounded(scene::anchoredBox(fBounds, 4.0f, indicatorH,
                                         scene::Anchor::kCentreLeft, 6.0f),
                      2.0f, skia::kWhite, alpha);
      }
      const skia::SkColor tint =
          active ? skia::kWhite
                 : (fHovered ? skia::colorSetARGB(255, 200, 195, 210)
                             : skia::colorSetARGB(255, 153, 153, 153));
      p.textClipped(Settings::kSectionIcons[fIndex], fBounds.fLeft + 26.0f,
                    p.middleBaseline(fBounds, 18.0f), 24.0f, 18.0f, tint,
                    alpha);
      p.textClipped(Settings::kSections[fIndex], fBounds.fLeft + 56.0f,
                    p.middleBaseline(fBounds, 15.0f), kSidebarWidth - 66.0f,
                    15.0f, tint, alpha);
    }

    bool acceptsInput() const override { return true; }
    bool hoverChangesAppearance() const override {
      return fOwner->fActiveSection != static_cast<int>(fIndex);
    }

    bool onClick(float, float) override {
      fOwner->fAction = {Action::kSection, fIndex, 0};
      return true;
    }

  private:
    SettingsPanel *fOwner;
    std::size_t fIndex;
    float fGrow = 0.0f;
    double fLastMs = 0.0;
  };

  // The open list of a choice. It is a sibling of the scrolling column rather
  // than a part of the row, because a dropdown belongs over what is under it:
  // as a child of the row it would have been drawn before the rows below it
  // and hit-tested after them, which is exactly backwards.
  // The list a choice row opens. Where it goes and what is in it are this
  // panel's; the plate, the rows, the hovering and the clicking are the
  // widget's.
  class ChoiceListNode : public widgets::DropdownList {
  public:
    explicit ChoiceListNode(SettingsPanel *owner) : fOwner(owner) {
      fTheme = kListTheme;
      // Hung off the control that opens it: fFollow makes the anchor and the
      // relative width come from that node instead of from the parent.
      this->setVisible(false);
      fOnChoose = [owner](int option) {
        owner->fAction = {Action::kChoiceSet,
                          static_cast<std::size_t>(owner->fOpenChoice), option};
      };
    }

    // Set when a row opens or closes, not per frame: layout() reads fFollow
    // before measure() runs, so it cannot be decided in measure().
    void openFor(const RowNode *row) {
      this->setFollow(row != nullptr ? row->control() : nullptr);
      this->setVisible(fFollow != nullptr);
    }

  protected:
    void measure(const skia::SkRect &) override {
      if (!fVisible) {
        this->setOptions({});
        return;
      }
      const auto &def =
          fOwner->fSettings
              ->defs()[static_cast<std::size_t>(fOwner->fOpenChoice)];
      this->setOptions({def.fOptions.begin(), def.fOptions.end()});
      this->setCurrent(fOwner->fSettings->choice(def.fKey));
    }

  private:
    SettingsPanel *fOwner;
  };

  [[nodiscard]] RowNode *openRow() const {
    if (fOpenChoice < 0 ||
        static_cast<std::size_t>(fOpenChoice) >= fRowNodes.size()) {
      return nullptr;
    }
    return fRowNodes[static_cast<std::size_t>(fOpenChoice)];
  }

  // ---- the tree -----------------------------------------------------------

  [[nodiscard]] std::unique_ptr<scene::Drawable> build() {
    fShell = nullptr;
    fDim = nullptr;
    fScroll = nullptr;
    fColumn = nullptr;
    fChoiceList = nullptr;
    fRowNodes.assign(fSettings->defs().size(), nullptr);
    fSectionHeaders.fill(nullptr);

    auto root = scene::make<scene::Drawable>(
        {.roles = {scene::role<settings_style::Root>}});

    // The dim is its own drawable rather than the root, so that the panel
    // over it does not inherit its transparency.
    fDim = root->add<nodes::Box>(
        {.roles = {scene::role<settings_style::Dim>}},
        skia::colorSetARGB(255, 0, 0, 0));

    auto shell = scene::make<scene::Drawable>(
        {.roles = {scene::role<settings_style::Shell>}});
    fShell = shell.get();

    // The alpha goes on the drawable, not into the colour: a Box paints its
    // colour and then sets the alpha it was given, so an alpha carried in the
    // colour is thrown away and the panel comes out opaque.
    auto sidebar = scene::make<nodes::Box>(
        {.roles = {scene::role<settings_style::Sidebar>}},
        skia::colorSetARGB(255, 23, 19, 30));
    auto sections = scene::make<nodes::FillFlow>(
        {.roles = {scene::role<settings_style::Sections>}},
        nodes::FillFlow::Direction::kVertical);
    sections->setSpacing(0.0f, 5.0f);
    for (std::size_t i = 0; i < Settings::kSections.size(); ++i) {
      sections->add<SectionNode>(
          {.roles = {scene::role<settings_style::Section>}}, this, i);
    }
    sidebar->add(std::move(sections));
    shell->add(std::move(sidebar));

    auto panel = scene::make<nodes::Box>(
        {.roles = {scene::role<settings_style::Panel>}},
        skia::colorSetARGB(255, 31, 25, 40));

    // Text draws its baseline at the top of its box plus the size, so these
    // are placed by where the baseline used to be: 56 and 78.
    panel->add<nodes::Text>({.roles = {scene::role<settings_style::Title>}},
                            "settings", 30.0f, skia::kWhite);
    panel->add<nodes::Text>(
        {.roles = {scene::role<settings_style::Subtitle>}},
        "change the way osu! behaves", 13.0f, skia::kWhite);

    auto scroll = scene::make<nodes::ScrollContainer>(
        {.roles = {scene::role<settings_style::Scroll>}});
    fScroll = scroll.get();

    auto column = scene::make<nodes::FillFlow>(
        {.roles = {scene::role<settings_style::Column>}},
        nodes::FillFlow::Direction::kVertical);
    column->setSpacing(0.0f, kItemSpacing);
    fColumn = column.get();

    int lastSection = -1;
    const auto &defs = fSettings->defs();
    for (std::size_t i = 0; i < defs.size(); ++i) {
      if (defs[i].fSection != lastSection) {
        lastSection = defs[i].fSection;
        auto header = scene::make<nodes::Text>(
            {.roles = {scene::role<settings_style::SectionHeader>}},
            Settings::kSections[static_cast<std::size_t>(defs[i].fSection)],
            18.0f, palette::kAccent);
        fSectionHeaders[static_cast<std::size_t>(defs[i].fSection)] =
            header.get();
        column->add(std::move(header));
      }
      auto row = scene::make<RowNode>(
          {.roles = {scene::role<settings_style::Row>}}, this, i);
      fRowNodes[i] = row.get();
      column->add(std::move(row));
    }
    scroll->add(std::move(column));
    panel->add(std::move(scroll));

    auto list = scene::make<ChoiceListNode>(
        {.roles = {scene::role<settings_style::ChoiceList>}}, this);
    fChoiceList = list.get();
    // A tree can be rebuilt with a list already open.
    fChoiceList->openFor(this->openRow());
    panel->add(std::move(list));

    panel->add<nodes::Text>({.roles = {scene::role<settings_style::Hint>}},
                            "Ctrl+O to close", 12.0f, skia::kWhite);

    shell->add(std::move(panel));
    root->add(std::move(shell));
    root->setStyleSheet<SettingsTheme>();
    return root;
  }

  // Which section the top of the viewport is in, which is what the sidebar
  // highlights. Taken from where the headers ended up rather than from a
  // running total kept while drawing.
  void trackViewportSection() {
    if (fScroll == nullptr) {
      return;
    }
    const float top = fScroll->bounds().fTop + 80.0f;
    int active = 0;
    for (std::size_t i = 0; i < fSectionHeaders.size(); ++i) {
      const scene::Drawable *header = fSectionHeaders[i];
      if (header != nullptr && header->bounds().fTop <= top) {
        active = static_cast<int>(i);
      }
    }
    fActiveSection = active;
  }

  void scrollToSection(std::size_t section) {
    if (fScroll == nullptr || fColumn == nullptr ||
        section >= fSectionHeaders.size() ||
        fSectionHeaders[section] == nullptr) {
      return;
    }
    // Where the header is, in the column's own coordinates: the column has
    // already been scrolled by however much, so its own top moved with it.
    const float offset = fSectionHeaders[section]->bounds().fTop -
                         fColumn->bounds().fTop;
    fScroll->scrollTo(std::max(0.0f, offset));
    fTouched = true;
  }

  void setOpenChoice(int index) {
    if (fOpenChoice == index) {
      return;
    }
    fOpenChoice = index;
    fTouched = true;
    if (fChoiceList != nullptr) {
      fChoiceList->openFor(this->openRow());
      fChoiceList->invalidateLayout();
    }
  }

  void markRow(std::size_t index) {
    if (index < fRowNodes.size() && fRowNodes[index] != nullptr) {
      fRowNodes[index]->markDamaged();
    }
  }

  bool fOpen = false;
  float fSlide = 0.0f;
  double fEnterWall = 0.0;
  float fScrollTicks = 0.0f;
  int fDragging = -1;
  int fOpenChoice = -1; // which choice has its list open
  int fActiveSection = 0;
  bool fTouched = true;
  float fMouseX = 0.0f, fMouseY = 0.0f;
  std::size_t fBuiltFor = 0;
  skia::SkFont *fFont = nullptr;
  Settings *fSettings = nullptr;
  Action fAction;

  std::unique_ptr<scene::Drawable> fScene;
  scene::Drawable *fShell = nullptr;
  nodes::Box *fDim = nullptr;
  nodes::ScrollContainer *fScroll = nullptr;
  nodes::FillFlow *fColumn = nullptr;
  ChoiceListNode *fChoiceList = nullptr;
  std::vector<RowNode *> fRowNodes;
  std::array<scene::Drawable *, 8> fSectionHeaders{};
};

} // namespace client
