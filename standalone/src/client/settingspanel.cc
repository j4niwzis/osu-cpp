export module client.settingspanel;

import std;
import skia;
import skiff.paint;
import client.palette;
import client.settings;
import skiff.scene;
import skiff.nodes;

// The framework lives in skiff:: now; these keep the screens below
// writing scene:: and nodes:: as they did when it sat in client::.
namespace scene = skiff::scene;
namespace nodes = skiff::nodes;

// skiff::paint is the framework's drawing side; the short name keeps
// the lines below at the width they were written at.
namespace paint = skiff::paint;

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
  // Returns what has to be repainted for it: empty means it is on screen and
  // showing exactly what it showed last frame, which is what an open settings
  // panel does almost all of the time.
  [[nodiscard]] skia::SkRect update(skia::SkFont &font, Settings &settings,
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
      return skia::SkRect::MakeEmpty();
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
    if (fShell != nullptr && fShell->fX != shellX) {
      fShell->fX = shellX;
      fShell->invalidateLayout();
    }
    if (fDim != nullptr) {
      fDim->fAlpha = fade * (110.0f / 255.0f);
    }
    if (fShell != nullptr) {
      fShell->fAlpha = fade;
    }

    // A viewport of nothing after the last row. Not decoration: clicking the
    // last section in the sidebar scrolls its header to the top, and it can
    // only get there if there is a screen's worth of column behind it.
    const float tail = sh - kContentTop;
    if (fColumn != nullptr && fColumn->fPadding.fBottom != tail) {
      fColumn->fPadding.fBottom = tail;
      fColumn->invalidateLayout();
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

    skia::SkRect damage = fScene->takeDamage();
    // A column in motion moves everything in it, by fractions of a pixel per
    // frame, and a clip is a whole number of them: the edges of what moved
    // and the edges of what is repainted stop agreeing, which is a seam at
    // every row. While it glides, the column is the answer rather than the
    // rows -- and it is what the rows add up to anyway.
    if (fScroll != nullptr && fScroll->moving()) {
      fTouched = true;
    }
    if (this->animating(frame.fNowMs) || fTouched) {
      // Sliding dims the whole screen with it, which is one of the few honest
      // whole-screen repaints there are.
      damage = this->animating(frame.fNowMs) ? screen : damage;
      if (fTouched && !this->animating(frame.fNowMs)) {
        damage.join(skia::SkRect::MakeXYWH(
            std::max(0.0f, shellX), 0.0f, kSidebarWidth + kPanelWidth, sh));
      }
    }
    fTouched = false;
    return damage;
  }

  void render(skia::SkCanvas *canvas) {
    if (fScene && canvas != nullptr) {
      fScene->draw(canvas);
    }
  }

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
    fScene->click(x, y);
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
    const skia::SkRect track = fRowNodes[index]->trackRect();
    if (track.width() <= 0.0f) {
      return false;
    }
    settings.setFromFraction(index, (x - track.fLeft) / track.width());
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

  [[nodiscard]] static skia::SkColor lerpColour(skia::SkColor from,
                                                skia::SkColor to, float t) {
    const auto mix = [t](std::uint8_t a, std::uint8_t b) {
      return static_cast<std::uint8_t>(static_cast<float>(a) +
                                       (static_cast<float>(b) -
                                        static_cast<float>(a)) *
                                           t);
    };
    const auto channel = [](skia::SkColor c, int shift) {
      return static_cast<std::uint8_t>((c >> shift) & 0xffu);
    };
    return skia::colorSetARGB(
        mix(channel(from, 24), channel(to, 24)),
        mix(channel(from, 16), channel(to, 16)),
        mix(channel(from, 8), channel(to, 8)),
        mix(channel(from, 0), channel(to, 0)));
  }

  // ---- rows ---------------------------------------------------------------

  // One setting. It draws its own kind -- slider, toggle or choice -- and
  // notices when the value under it changes, which is the only way a row that
  // is never hovered ever needs repainting.
  class RowNode : public scene::Drawable {
  public:
    RowNode(SettingsPanel *owner, std::size_t index)
        : fOwner(owner), fIndex(index) {
      fRelativeSizeAxes = scene::Axes::kX;
      fWidth = 1.0f;
    }

    [[nodiscard]] skia::SkRect trackRect() const {
      const skia::SkRect content = this->contentRect();
      return skia::SkRect::MakeXYWH(content.fLeft, fBounds.fTop + 30.0f,
                                    content.width(), 6.0f);
    }

    // Where the open list goes: under the control, one row per option, in
    // screen coordinates, for the overlay that draws it.
    [[nodiscard]] skia::SkRect listRect() const {
      const paint::Painter measurer(nullptr, *fOwner->fFont);
      const skia::SkRect box = this->choiceBox(measurer);
      const auto &def = fOwner->fSettings->defs()[fIndex];
      const float height =
          static_cast<float>(def.fOptions.size()) * 26.0f + 4.0f;
      return skia::SkRect::MakeXYWH(box.fLeft, box.fBottom + 4.0f, box.width(),
                                    height);
    }

    [[nodiscard]] static skia::SkRect optionBox(const skia::SkRect &list,
                                                std::size_t option) {
      return skia::SkRect::MakeXYWH(
          list.fLeft, list.fTop + 2.0f + static_cast<float>(option) * 26.0f,
          list.width(), 24.0f);
    }

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
    void update(double nowMs) override {
      const double dt = fLastMs > 0.0 ? std::min(50.0, nowMs - fLastMs) : 16.0;
      fLastMs = nowMs;
      const Settings &settings = *fOwner->fSettings;
      const auto &def = settings.defs()[fIndex];
      const float value = def.fKind == SettingKind::kToggle
                              ? (settings.flag(def.fKey) ? 1.0f : 0.0f)
                          : def.fKind == SettingKind::kChoice
                              ? static_cast<float>(settings.choice(def.fKey))
                              : settings.value(def.fKey);
      const bool modified = settings.isModified(fIndex);
      const bool open = fOwner->fOpenChoice == static_cast<int>(fIndex);
      if (value != fDrawnValue || modified != fDrawnModified) {
        fDrawnValue = value;
        fDrawnModified = modified;
        this->markDamaged();
      }
      if (open != fDrawnOpen) {
        fDrawnOpen = open; // the control itself is tinted while its list is up
        this->markDamaged();
      }
      // The knob slides between its two ends rather than teleporting.
      if (def.fKind == SettingKind::kToggle) {
        const float previous = fKnob;
        fKnob = paint::approach(fKnob, settings.flag(def.fKey) ? 1.0f : 0.0f,
                             60.0f, dt);
        if (fKnob != previous) {
          this->markDamaged();
        }
      }
    }

    void drawSelf(skia::SkCanvas *canvas, float alpha) override {
      const Settings &settings = *fOwner->fSettings;
      const auto &def = settings.defs()[fIndex];
      const paint::Painter p(canvas, *fOwner->fFont);
      const skia::SkRect content = this->contentRect();
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
        const float t = (settings.value(def.fKey) - def.fMin) /
                        (def.fMax - def.fMin);
        const skia::SkRect track = this->trackRect();
        p.fillRounded(track, 3.0f, skia::colorSetARGB(255, 58, 48, 70), alpha);
        p.fillRounded(skia::SkRect::MakeXYWH(track.fLeft, track.fTop,
                                             track.width() * t, track.height()),
                      3.0f, palette::kAccent, alpha);
        p.circle(track.fLeft + track.width() * t, track.centerY(), 7.0f,
                 skia::kWhite, alpha);
        p.textClipped(settings.displayValue(fIndex), content.fRight - 76.0f,
                      baseline, 76.0f, 13.0f, skia::kWhite, alpha * 0.75f);
        return;
      }

      if (def.fKind == SettingKind::kChoice) {
        const skia::SkRect box = this->choiceBox(p);
        const bool open = fOwner->fOpenChoice == static_cast<int>(fIndex);
        const auto index = static_cast<std::size_t>(settings.choice(def.fKey));
        const std::string &option =
            index < def.fOptions.size() ? def.fOptions[index]
                                        : def.fOptions.front();
        p.fillRounded(box, 6.0f,
                      open ? palette::kAccent : skia::colorSetARGB(255, 58, 48, 70),
                      alpha);
        p.textClipped(option, box.fLeft + 12.0f, box.centerY() + 4.0f,
                      box.width() - 32.0f, 13.0f, skia::kWhite, alpha);
        // The chevron, so it reads as something that opens.
        p.textClipped(open ? "^" : "v", box.fRight - 18.0f, box.centerY() + 4.0f,
                      12.0f, 12.0f, skia::kWhite, alpha * 0.8f);
        return; // the list, if it is open, belongs to the overlay above
      }

      const skia::SkRect box = skia::SkRect::MakeXYWH(
          content.fRight - 46.0f, fBounds.fTop + 4.0f, 40.0f, 22.0f);
      p.fillRounded(box, 11.0f,
                    lerpColour(skia::colorSetARGB(255, 58, 48, 70), palette::kAccent,
                               fKnob),
                    alpha);
      p.circle(box.fLeft + 11.0f + (box.width() - 22.0f) * fKnob,
               box.centerY(), 8.0f, skia::kWhite, alpha);
    }

    bool acceptsInput() const override { return true; }

    bool onClick(float x, float y) override {
      const auto &def = fOwner->fSettings->defs()[fIndex];
      const skia::SkRect content = this->contentRect();
      if (fOwner->fSettings->isModified(fIndex) && x < content.fLeft) {
        fOwner->fAction = {Action::kRestore, fIndex, 0};
        return true;
      }
      if (def.fKind == SettingKind::kChoice) {
        const paint::Painter p(nullptr, *fOwner->fFont);
        if (this->choiceBox(p).contains(x, y)) {
          fOwner->fAction = {Action::kChoiceOpen, fIndex, 0};
          return true;
        }
        return true; // inside the row, but not on the control
      }
      if (def.fKind == SettingKind::kToggle) {
        fOwner->fAction = {Action::kToggle, fIndex, 0};
        return true;
      }
      fOwner->fAction = {Action::kSlider, fIndex, 0};
      return true;
    }

  private:
    [[nodiscard]] skia::SkRect contentRect() const {
      return skia::SkRect::MakeLTRB(fBounds.fLeft + kContentMargins,
                                    fBounds.fTop,
                                    fBounds.fRight - kContentMargins,
                                    fBounds.fBottom);
    }

    // Sized to the widest option, so the control does not resize as it is
    // used.
    [[nodiscard]] skia::SkRect choiceBox(const paint::Painter &p) const {
      const auto &def = fOwner->fSettings->defs()[fIndex];
      float width = 0.0f;
      for (const auto &candidate : def.fOptions) {
        width = std::max(width, p.measure(candidate, 13.0f));
      }
      width += 40.0f;
      const skia::SkRect content = this->contentRect();
      return skia::SkRect::MakeXYWH(content.fRight - width,
                                    fBounds.fTop + 3.0f, width, 24.0f);
    }

    SettingsPanel *fOwner;
    std::size_t fIndex;
    float fDrawnValue = std::numeric_limits<float>::quiet_NaN();
    bool fDrawnModified = false;
    bool fDrawnOpen = false;
    float fKnob = 0.0f; // eased position of a toggle's knob
    double fLastMs = 0.0;
  };

  // One entry in the sidebar: an icon, a label, and the elastic indicator
  // that grows when the viewport is inside that section.
  class SectionNode : public scene::Drawable {
  public:
    SectionNode(SettingsPanel *owner, std::size_t index)
        : fOwner(owner), fIndex(index) {
      fRelativeSizeAxes = scene::Axes::kX;
      fWidth = 1.0f;
      fHeight = kSidebarItemHeight;
    }

  protected:
    void update(double nowMs) override {
      const double dt = fLastMs > 0.0 ? std::min(50.0, nowMs - fLastMs) : 16.0;
      fLastMs = nowMs;
      const bool active = fOwner->fActiveSection == static_cast<int>(fIndex);
      const float previous = fGrow;
      fGrow = paint::approach(fGrow, active ? 1.0f : 0.0f, 90.0f, dt);
      if (fGrow != previous || fHovered != fDrawnHovered) {
        fDrawnHovered = fHovered;
        this->markDamaged();
      }
    }

    void drawSelf(skia::SkCanvas *canvas, float alpha) override {
      const paint::Painter p(canvas, *fOwner->fFont);
      const bool active = fOwner->fActiveSection == static_cast<int>(fIndex);
      const float indicatorH = 4.0f + 14.0f * paint::outElasticHalf(fGrow);
      if (fGrow > 0.01f) {
        p.fillRounded(skia::SkRect::MakeXYWH(fBounds.fLeft + 6.0f,
                                             fBounds.centerY() -
                                                 indicatorH * 0.5f,
                                             4.0f, indicatorH),
                      2.0f, skia::kWhite, alpha);
      }
      const skia::SkColor tint =
          active ? skia::kWhite
                 : (fHovered ? skia::colorSetARGB(255, 200, 195, 210)
                             : skia::colorSetARGB(255, 153, 153, 153));
      p.textClipped(Settings::kSectionIcons[fIndex], fBounds.fLeft + 26.0f,
                    fBounds.centerY() + 7.0f, 24.0f, 18.0f, tint, alpha);
      p.textClipped(Settings::kSections[fIndex], fBounds.fLeft + 56.0f,
                    fBounds.centerY() + 6.0f, kSidebarWidth - 66.0f, 15.0f,
                    tint, alpha);
    }

    bool acceptsInput() const override { return true; }

    bool onClick(float, float) override {
      fOwner->fAction = {Action::kSection, fIndex, 0};
      return true;
    }

  private:
    SettingsPanel *fOwner;
    std::size_t fIndex;
    float fGrow = 0.0f;
    bool fDrawnHovered = false;
    double fLastMs = 0.0;
  };

  // The open list of a choice. It is a sibling of the scrolling column rather
  // than a part of the row, because a dropdown belongs over what is under it:
  // as a child of the row it would have been drawn before the rows below it
  // and hit-tested after them, which is exactly backwards.
  class ChoiceListNode : public scene::Drawable {
  public:
    explicit ChoiceListNode(SettingsPanel *owner) : fOwner(owner) {}

  protected:
    // Placed by hand from the row it belongs to: measure() is where a
    // drawable gets to say where it goes before the box is computed.
    void measure(const skia::SkRect &parent) override {
      const skia::SkRect list = fOwner->openListRect();
      fVisible = !list.isEmpty();
      if (!fVisible) {
        fWidth = 0.0f;
        fHeight = 0.0f;
        return;
      }
      fX = list.fLeft - parent.fLeft;
      fY = list.fTop - parent.fTop;
      fWidth = list.width();
      fHeight = list.height();
    }

    void update(double) override {
      const int open = fOwner->fOpenChoice;
      const int hot = this->hotOption();
      if (open != fDrawnOpen || hot != fDrawnHot) {
        fDrawnOpen = open;
        fDrawnHot = hot;
        this->markDamaged();
      }
    }

    void drawSelf(skia::SkCanvas *canvas, float alpha) override {
      const RowNode *row = fOwner->openRow();
      if (row == nullptr) {
        return;
      }
      const paint::Painter p(canvas, *fOwner->fFont);
      const auto &def =
          fOwner->fSettings->defs()[static_cast<std::size_t>(
              fOwner->fOpenChoice)];
      const auto chosen =
          static_cast<std::size_t>(fOwner->fSettings->choice(def.fKey));
      // Its own backing plate, since what is underneath it stays where it is.
      p.fillRounded(fBounds, 6.0f, skia::colorSetARGB(255, 32, 26, 40), alpha);
      for (std::size_t o = 0; o < def.fOptions.size(); ++o) {
        const skia::SkRect item = RowNode::optionBox(fBounds, o);
        const bool hovered = item.contains(fOwner->fMouseX, fOwner->fMouseY);
        p.fillRounded(item, 6.0f,
                      hovered ? palette::kCardSel
                              : skia::colorSetARGB(255, 44, 36, 54),
                      alpha);
        p.textClipped(def.fOptions[o], item.fLeft + 12.0f,
                      item.centerY() + 4.0f, item.width() - 24.0f, 13.0f,
                      skia::kWhite, alpha * (o == chosen ? 1.0f : 0.8f));
      }
    }

    bool acceptsInput() const override { return fVisible; }

    bool onClick(float x, float y) override {
      const int option = this->optionAt(x, y);
      if (option < 0) {
        return true; // inside the list but between rows: swallow it
      }
      fOwner->fAction = {Action::kChoiceSet,
                         static_cast<std::size_t>(fOwner->fOpenChoice), option};
      return true;
    }

  private:
    [[nodiscard]] int optionAt(float x, float y) const {
      const RowNode *row = fOwner->openRow();
      if (row == nullptr) {
        return -1;
      }
      const auto &def =
          fOwner->fSettings->defs()[static_cast<std::size_t>(
              fOwner->fOpenChoice)];
      for (std::size_t o = 0; o < def.fOptions.size(); ++o) {
        if (RowNode::optionBox(fBounds, o).contains(x, y)) {
          return static_cast<int>(o);
        }
      }
      return -1;
    }

    [[nodiscard]] int hotOption() const {
      return this->optionAt(fOwner->fMouseX, fOwner->fMouseY);
    }

    SettingsPanel *fOwner;
    int fDrawnOpen = -1;
    int fDrawnHot = -1;
  };

  [[nodiscard]] RowNode *openRow() const {
    if (fOpenChoice < 0 ||
        static_cast<std::size_t>(fOpenChoice) >= fRowNodes.size()) {
      return nullptr;
    }
    return fRowNodes[static_cast<std::size_t>(fOpenChoice)];
  }

  [[nodiscard]] skia::SkRect openListRect() const {
    const RowNode *row = this->openRow();
    return row != nullptr ? row->listRect() : skia::SkRect::MakeEmpty();
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

    auto root = std::make_unique<scene::Drawable>();
    root->fRelativeSizeAxes = scene::Axes::kBoth;
    root->fWidth = 1.0f;
    root->fHeight = 1.0f;

    // The dim is its own drawable rather than the root, so that the panel
    // over it does not inherit its transparency.
    auto dim = std::make_unique<nodes::Box>(skia::colorSetARGB(255, 0, 0, 0));
    dim->fRelativeSizeAxes = scene::Axes::kBoth;
    dim->fWidth = 1.0f;
    dim->fHeight = 1.0f;
    fDim = dim.get();
    root->add(std::move(dim));

    auto shell = std::make_unique<scene::Drawable>();
    shell->fRelativeSizeAxes = scene::Axes::kY;
    shell->fWidth = kSidebarWidth + kPanelWidth;
    shell->fHeight = 1.0f;
    fShell = shell.get();

    // The alpha goes on the drawable, not into the colour: a Box paints its
    // colour and then sets the alpha it was given, so an alpha carried in the
    // colour is thrown away and the panel comes out opaque.
    auto sidebar =
        std::make_unique<nodes::Box>(skia::colorSetARGB(255, 23, 19, 30));
    sidebar->fAlpha = 252.0f / 255.0f;
    sidebar->fRelativeSizeAxes = scene::Axes::kY;
    sidebar->fWidth = kSidebarWidth;
    sidebar->fHeight = 1.0f;
    auto sections =
        std::make_unique<nodes::FillFlow>(nodes::FillFlow::Direction::kVertical);
    sections->fRelativeSizeAxes = scene::Axes::kX;
    sections->fWidth = 1.0f;
    sections->fAutoSizeAxes = scene::Axes::kY;
    sections->fY = 70.0f;
    sections->setSpacing(0.0f, 5.0f);
    for (std::size_t i = 0; i < Settings::kSections.size(); ++i) {
      sections->add(std::make_unique<SectionNode>(this, i));
    }
    sidebar->add(std::move(sections));
    shell->add(std::move(sidebar));

    auto panel =
        std::make_unique<nodes::Box>(skia::colorSetARGB(255, 31, 25, 40));
    panel->fAlpha = 250.0f / 255.0f;
    panel->fRelativeSizeAxes = scene::Axes::kY;
    panel->fWidth = kPanelWidth;
    panel->fHeight = 1.0f;
    panel->fX = kSidebarWidth;

    // Text draws its baseline at the top of its box plus the size, so these
    // are placed by where the baseline used to be: 56 and 78.
    auto title = std::make_unique<nodes::Text>("settings", 30.0f, skia::kWhite);
    title->fX = kContentMargins;
    title->fY = 26.0f;
    panel->add(std::move(title));
    auto subtitle = std::make_unique<nodes::Text>(
        "change the way osu! behaves", 13.0f, skia::kWhite);
    subtitle->fAlpha = 0.6f;
    subtitle->fX = kContentMargins;
    subtitle->fY = 65.0f;
    panel->add(std::move(subtitle));

    auto scroll = std::make_unique<nodes::ScrollContainer>();
    scroll->fRelativeSizeAxes = scene::Axes::kBoth;
    scroll->fWidth = 1.0f;
    scroll->fHeight = 1.0f;
    scroll->fMargin = {kContentTop, 0.0f, 0.0f, 0.0f};
    fScroll = scroll.get();

    auto column =
        std::make_unique<nodes::FillFlow>(nodes::FillFlow::Direction::kVertical);
    column->fRelativeSizeAxes = scene::Axes::kX;
    column->fWidth = 1.0f;
    column->fAutoSizeAxes = scene::Axes::kY;
    column->fMargin = {8.0f, 0.0f, 0.0f, 0.0f};
    column->setSpacing(0.0f, kItemSpacing);
    fColumn = column.get();

    int lastSection = -1;
    const auto &defs = fSettings->defs();
    for (std::size_t i = 0; i < defs.size(); ++i) {
      if (defs[i].fSection != lastSection) {
        lastSection = defs[i].fSection;
        auto header = std::make_unique<nodes::Text>(
            Settings::kSections[static_cast<std::size_t>(defs[i].fSection)],
            18.0f, palette::kAccent);
        header->fX = kContentMargins;
        fSectionHeaders[static_cast<std::size_t>(defs[i].fSection)] =
            header.get();
        column->add(std::move(header));
      }
      auto row = std::make_unique<RowNode>(this, i);
      fRowNodes[i] = row.get();
      column->add(std::move(row));
    }
    scroll->add(std::move(column));
    panel->add(std::move(scroll));

    auto list = std::make_unique<ChoiceListNode>(this);
    fChoiceList = list.get();
    panel->add(std::move(list));

    auto hint = std::make_unique<nodes::Text>("Ctrl+O to close", 12.0f,
                                              skia::kWhite);
    hint->fAlpha = 0.5f;
    hint->fAnchor = scene::Anchor::kBottomLeft;
    hint->fOrigin = scene::Anchor::kBottomLeft;
    hint->fX = kContentMargins;
    hint->fY = -21.0f; // baseline 24 above the bottom edge
    panel->add(std::move(hint));

    shell->add(std::move(panel));
    root->add(std::move(shell));
    return root;
  }

  // Which section the top of the viewport is in, which is what the sidebar
  // highlights. Taken from where the headers ended up rather than from a
  // running total kept while drawing.
  void trackViewportSection() {
    if (fScroll == nullptr) {
      return;
    }
    const float top = fScroll->fBounds.fTop + 80.0f;
    int active = 0;
    for (std::size_t i = 0; i < fSectionHeaders.size(); ++i) {
      const scene::Drawable *header = fSectionHeaders[i];
      if (header != nullptr && header->fBounds.fTop <= top) {
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
    const float offset = fSectionHeaders[section]->fBounds.fTop -
                         fColumn->fBounds.fTop;
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
      fChoiceList->invalidateLayout(); // it is placed from the open row
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
