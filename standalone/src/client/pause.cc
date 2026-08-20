export module client.pause;

import std;
import skia;
import client.ui;
import client.scene;
import client.nodes;
import client.triangles;

// osu!lazer's PauseOverlay, which is a GameplayMenuOverlay: black over the
// frozen game at 0.75, "paused" in yellow above a column of DialogButtons,
// and the play's numbers under them.
//
// Sources: osu.Game/Screens/Play/{PauseOverlay,GameplayMenuOverlay}.cs and
// osu.Game/Graphics/UserInterface/DialogButton.cs. The numbers below are
// theirs: background_alpha 0.75, button_height 80, buttons 2 apart with 50 of
// horizontal padding, header 48 SemiBold with 5 of letter spacing in
// colours.Yellow, DialogButton's colour bar 0.8 of the width at rest and 0.9
// when selected over 400 ms OutQuint, corner radius 5, sheared by
// OsuGame.SHEAR, its label 28 Bold with the letters spreading to 1.4 and the
// whole thing scaling to 1.02 when it is the selected one.
export namespace client::pause {

inline constexpr float kBackgroundAlpha = 0.75f;
inline constexpr float kButtonHeight = 80.0f;
inline constexpr float kButtonSpacing = 2.0f;
inline constexpr float kHorizontalPadding = 50.0f;
inline constexpr float kIdleWidth = 0.8f;
inline constexpr float kHoverWidth = 0.9f;
inline constexpr float kHoverMs = 400.0f;
inline constexpr float kShear = 0.15f; // OsuGame.SHEAR
inline constexpr float kCorner = 5.0f;

inline constexpr skia::SkColor kYellow = skia::colorSetARGB(255, 0xff, 0xcc, 0x22);
inline constexpr skia::SkColor kGreen = skia::colorSetARGB(255, 0x88, 0xb3, 0x00);
inline constexpr skia::SkColor kYellowDark = skia::colorSetARGB(255, 0xee, 0xaa, 0x00);
inline constexpr skia::SkColor kRed = skia::colorSetARGB(255, 170, 27, 39);

class PauseMenu {
public:
  struct Ctx {
    skia::SkFont *fFont = nullptr;
    float fWidth = 0.0f;
    float fHeight = 0.0f;
    float fMouseX = 0.0f;
    float fMouseY = 0.0f;
    double fNowMs = 0.0;
    double fDtMs = 16.0;
    int fRetries = 0;
    float fProgress = 0.0f; // 0..1 through the playable part of the map
    float fAccuracy = 1.0f;
  };

  enum class Action : std::uint8_t { kNone, kContinue, kRetry, kQuit };

  // lazer's overlay is the same class for pause and for fail; only the header
  // and which buttons exist differ.
  void setHeader(std::string header) {
    if (header != fHeader) {
      fHeader = std::move(header);
      fScene.reset();
    }
  }

  void update(const Ctx &ctx) {
    fCtx = ctx;
    fFont = ctx.fFont;
    if (fFont == nullptr) {
      return;
    }
    if (!fScene || fBuiltW != ctx.fWidth || fBuiltH != ctx.fHeight) {
      fBuiltW = ctx.fWidth;
      fBuiltH = ctx.fHeight;
      fScene = this->build();
    }
    const skia::SkRect screen = skia::SkRect::MakeWH(ctx.fWidth, ctx.fHeight);
    fScene->updateTree(ctx.fNowMs);
    fScene->layoutIfNeeded(screen);
    fScene->setHover(ctx.fMouseX, ctx.fMouseY);
  }

  void render(skia::SkCanvas *canvas) {
    if (fScene && canvas != nullptr) {
      fScene->draw(canvas);
    }
  }

  [[nodiscard]] Action click(float x, float y) {
    fPending = Action::kNone;
    if (fScene) {
      fScene->click(x, y);
    }
    return fPending;
  }

  // SelectionCycleFillFlowContainer: the arrows walk the buttons and wrap.
  void selectNext() { this->select(fSelected + 1); }
  void selectPrevious() { this->select(fSelected - 1); }
  [[nodiscard]] Action triggerSelected() const {
    return fSelected >= 0 && fSelected < 3 ? kActions[static_cast<std::size_t>(
                                                 fSelected)]
                                           : Action::kNone;
  }

  [[nodiscard]] skia::SkRect takeDamage() {
    return fScene ? fScene->takeDamage() : skia::SkRect::MakeEmpty();
  }

  [[nodiscard]] bool animating() const {
    return fScene && fScene->animatingTree();
  }

private:
  static constexpr std::array<Action, 3> kActions = {
      Action::kContinue, Action::kRetry, Action::kQuit};

  void select(int index) {
    fSelected = ((index % 3) + 3) % 3;
  }

  // The header. Drawn glyph by glyph because lazer spaces the letters out by
  // 5, and a font draws a string with the advances it has.
  class HeaderNode : public scene::Drawable {
  public:
    explicit HeaderNode(PauseMenu *owner) : fOwner(owner) {
      fAnchor = scene::Anchor::kTopCentre;
      fOrigin = scene::Anchor::kTopCentre;
    }

  protected:
    void measure(const skia::SkRect &) override {
      fWidth = fOwner->headerWidth();
      fHeight = 48.0f * 1.25f;
    }

    void drawSelf(skia::SkCanvas *canvas, float alpha) override {
      fOwner->drawSpaced(canvas, fOwner->fHeader, fBounds.fLeft,
                         fBounds.fTop + 48.0f, 48.0f, kYellow, alpha);
    }

  private:
    PauseMenu *fOwner;
  };

  // DialogButton: a sheared bar of colour that grows from 0.8 to 0.9 of the
  // width when it is the selected one, with the label spreading out over it.
  class ButtonNode : public scene::Drawable {
  public:
    ButtonNode(PauseMenu *owner, std::size_t index, std::string label,
               skia::SkColor colour)
        : fOwner(owner), fIndex(index), fLabel(std::move(label)),
          fColour(colour) {
      fRelativeSizeAxes = scene::Axes::kX;
      fWidth = 1.0f;
      fHeight = kButtonHeight;
      fTriangles.setVelocity(0.7f); // DialogButton's TrianglesV2
    }

  protected:
    void update(double nowMs) override {
      const double dt = fLastMs > 0.0 ? std::min(50.0, nowMs - fLastMs) : 16.0;
      fLastMs = nowMs;
      fDt = dt;
      this->markDamaged(); // the triangles are always moving
      // Hovering selects, as GameplayMenuOverlay.Button does on mouse move.
      if (fHovered && fOwner->fSelected != static_cast<int>(fIndex)) {
        fOwner->fSelected = static_cast<int>(fIndex);
      }
      const bool selected = fOwner->fSelected == static_cast<int>(fIndex);
      const float previous = fGrow;
      fGrow = client::ui::approach(fGrow, selected ? 1.0f : 0.0f,
                                   kHoverMs / 3.0f, dt);
      if (fGrow != previous) {
        this->markDamaged();
      }
    }

    void drawSelf(skia::SkCanvas *canvas, float alpha) override {
      const float eased = client::ui::outQuint(fGrow);
      const float width = fBounds.width() * (kIdleWidth +
                                             (kHoverWidth - kIdleWidth) * eased);
      const float cx = fBounds.centerX();
      const skia::SkRect bar = skia::SkRect::MakeXYWH(
          cx - width * 0.5f, fBounds.fTop, width, fBounds.height());

      // The glow behind it, which lazer fades in with the selection.
      if (eased > 0.01f) {
        this->fillSheared(canvas, this->inflated(bar, 1.08f), fColour,
                          alpha * 0.35f * eased);
      }
      const skia::SkPath shape = shearedBar(bar);
      skia::SkPaint fill;
      fill.setAntiAlias(true);
      fill.setColor(fColour);
      fill.setAlphaf(alpha);
      const int saved = canvas->save();
      canvas->clipPath(shape, true);
      canvas->drawPath(shape, fill);
      // TrianglesV2 inside the colour, at a tenth alpha, additive, drifting
      // up at 0.7 -- and upright, because lazer un-shears them against the
      // container they sit in.
      fTriangles.draw(canvas, bar, fDt, alpha * 0.1f, skia::SkBlendMode::kPlus);
      canvas->restoreToCount(saved);

      // The label: 28 bold, white, spreading to 1.4 of letter spacing and
      // scaling to 1.02 when this is the one that is selected.
      const float spacing = 1.4f * eased;
      const float size = 28.0f * (1.0f + 0.02f * eased);
      const float textWidth = fOwner->spacedWidth(fLabel, size, spacing);
      fOwner->drawSpaced(canvas, fLabel, cx - textWidth * 0.5f,
                         fBounds.centerY() + size * 0.36f, size, skia::kWhite,
                         alpha, spacing);
    }

    bool acceptsInput() const override { return true; }

    bool onClick(float, float) override {
      fOwner->fPending = kActions[fIndex];
      fOwner->fSelected = static_cast<int>(fIndex);
      return true;
    }

  private:
    [[nodiscard]] static skia::SkRect inflated(const skia::SkRect &rect,
                                               float scale) {
      const float dx = rect.width() * (scale - 1.0f) * 0.5f;
      const float dy = rect.height() * (scale - 1.0f) * 0.5f;
      return skia::SkRect::MakeLTRB(rect.fLeft - dx, rect.fTop - dy,
                                    rect.fRight + dx, rect.fBottom + dy);
    }

    // The bar as lazer draws it: a rounded rectangle with the shear applied
    // to the whole thing, corners included, about its own middle.
    [[nodiscard]] static skia::SkPath shearedBar(const skia::SkRect &rect) {
      skia::SkPathBuilder builder;
      builder.addRRect(skia::SkRRect::MakeRectXY(rect, kCorner, kCorner));
      skia::SkPath path = builder.detach();
      skia::SkMatrix matrix =
          skia::SkMatrix::Translate(rect.centerX(), rect.centerY());
      matrix.preSkew(-kShear, 0.0f);
      matrix.preTranslate(-rect.centerX(), -rect.centerY());
      path.transform(matrix);
      return path;
    }

    // A rectangle sheared by OsuGame.SHEAR: the top edge leads, the bottom
    // trails, which is the shape every button in lazer has.
    void fillSheared(skia::SkCanvas *canvas, const skia::SkRect &rect,
                     skia::SkColor colour, float alpha) const {
      const float shear = kShear * rect.height() * 0.5f;
      skia::SkPathBuilder path;
      path.moveTo(rect.fLeft + shear, rect.fTop);
      path.lineTo(rect.fRight + shear, rect.fTop);
      path.lineTo(rect.fRight - shear, rect.fBottom);
      path.lineTo(rect.fLeft - shear, rect.fBottom);
      path.close();
      skia::SkPaint paint;
      paint.setAntiAlias(true);
      paint.setColor(colour);
      paint.setAlphaf(alpha);
      canvas->drawPath(path.detach(), paint);
    }

    PauseMenu *fOwner;
    std::size_t fIndex;
    std::string fLabel;
    skia::SkColor fColour;
    float fGrow = 0.0f;
    double fLastMs = 0.0;
    double fDt = 16.0;
    client::triangles::Field fTriangles;
  };

  // The three lines under the buttons: retries, how far through the map the
  // pause happened, and the accuracy so far.
  class InfoNode : public scene::Drawable {
  public:
    explicit InfoNode(PauseMenu *owner) : fOwner(owner) {
      fRelativeSizeAxes = scene::Axes::kX;
      fWidth = 1.0f;
      fHeight = 72.0f;
    }

  protected:
    void update(double) override {
      const int retries = fOwner->fCtx.fRetries;
      const int progress = static_cast<int>(fOwner->fCtx.fProgress * 100.0f);
      const int accuracy = static_cast<int>(fOwner->fCtx.fAccuracy * 10000.0f);
      if (retries != fRetries || progress != fProgress ||
          accuracy != fAccuracy) {
        fRetries = retries;
        fProgress = progress;
        fAccuracy = accuracy;
        this->markDamaged();
      }
    }

    void drawSelf(skia::SkCanvas *canvas, float alpha) override {
      const client::ui::Painter p(canvas, *fOwner->fFont);
      const float cx = fBounds.centerX();
      float y = fBounds.fTop + 18.0f;
      const auto line = [&](const std::string &text) {
        p.textCentered(text, cx, y, 18.0f, skia::kWhite, alpha * 0.9f);
        y += 24.0f;
      };
      line(std::format("retries: {}", fRetries));
      line(std::format("song progress: {}%", fProgress));
      line(std::format("accuracy: {:.2f}%",
                       static_cast<double>(fAccuracy) / 100.0));
    }

  private:
    PauseMenu *fOwner;
    int fRetries = -1;
    int fProgress = -1;
    int fAccuracy = -1;
  };

  [[nodiscard]] std::unique_ptr<scene::Drawable> build() {
    auto root = std::make_unique<scene::Drawable>();
    root->fRelativeSizeAxes = scene::Axes::kBoth;
    root->fWidth = 1.0f;
    root->fHeight = 1.0f;

    auto dim = std::make_unique<nodes::Box>(skia::colorSetARGB(255, 0, 0, 0));
    dim->fRelativeSizeAxes = scene::Axes::kBoth;
    dim->fWidth = 1.0f;
    dim->fHeight = 1.0f;
    dim->fAlpha = kBackgroundAlpha;
    root->add(std::move(dim));

    // lazer lays this out as a grid of four rows: the header centred in what
    // is left above, the buttons at their own height, the numbers below, and
    // a footer. Two centred blocks come out in the same place.
    auto header = std::make_unique<HeaderNode>(this);
    header->fY = fBuiltH * 0.5f - kButtonHeight * 1.5f - kButtonSpacing - 96.0f;
    root->add(std::move(header));

    auto column =
        std::make_unique<nodes::FillFlow>(nodes::FillFlow::Direction::kVertical);
    column->fRelativeSizeAxes = scene::Axes::kX;
    column->fWidth = 1.0f;
    column->fAutoSizeAxes = scene::Axes::kY;
    column->fAnchor = scene::Anchor::kCentreLeft;
    column->fOrigin = scene::Anchor::kCentreLeft;
    column->fY = -kButtonHeight * 1.5f;
    column->fPadding = {0.0f, kHorizontalPadding, 0.0f, kHorizontalPadding};
    column->setSpacing(0.0f, kButtonSpacing);
    column->add(std::make_unique<ButtonNode>(this, 0, "Continue", kGreen));
    column->add(std::make_unique<ButtonNode>(this, 1, "Retry", kYellowDark));
    column->add(std::make_unique<ButtonNode>(this, 2, "Quit", kRed));
    root->add(std::move(column));

    auto info = std::make_unique<InfoNode>(this);
    info->fY = fBuiltH * 0.5f + kButtonHeight * 1.5f + kButtonSpacing + 24.0f;
    root->add(std::move(info));
    return root;
  }

  // ---- letter-spaced text ---------------------------------------------------

  [[nodiscard]] float spacedWidth(const std::string &text, float size,
                                  float spacing) const {
    skia::SkFont *font = fFont;
    if (font == nullptr) {
      return 0.0f;
    }
    font->setSize(size);
    return client::ui::fonts().measure(*font, text) +
           spacing * static_cast<float>(text.size());
  }

  [[nodiscard]] float headerWidth() const {
    return this->spacedWidth(fHeader, 48.0f, 5.0f);
  }

  // One glyph at a time, since the spacing lazer asks for is not something a
  // font can be told about.
  void drawSpaced(skia::SkCanvas *canvas, const std::string &text, float x,
                  float baseline, float size, skia::SkColor colour, float alpha,
                  float spacing = 5.0f) const {
    skia::SkFont *font = fFont;
    if (font == nullptr) {
      return;
    }
    font->setSize(size);
    client::ui::fonts().applyWeight(*font, true);
    skia::SkPaint paint;
    paint.setAntiAlias(true);
    paint.setColor(colour);
    paint.setAlphaf(alpha);
    float cursor = x;
    for (std::size_t i = 0; i < text.size();) {
      std::size_t length = 1;
      const auto lead = static_cast<unsigned char>(text[i]);
      if ((lead & 0xE0u) == 0xC0u) {
        length = 2;
      } else if ((lead & 0xF0u) == 0xE0u) {
        length = 3;
      } else if ((lead & 0xF8u) == 0xF0u) {
        length = 4;
      }
      const std::string glyph = text.substr(i, length);
      client::ui::fonts().draw(canvas, *font, glyph, cursor, baseline, paint);
      cursor += client::ui::fonts().measure(*font, glyph) + spacing;
      i += length;
    }
    client::ui::fonts().applyWeight(*font, false);
  }

  Ctx fCtx;
  skia::SkFont *fFont = nullptr;
  std::string fHeader = "paused";
  std::unique_ptr<scene::Drawable> fScene;
  float fBuiltW = 0.0f;
  float fBuiltH = 0.0f;
  int fSelected = -1;
  Action fPending = Action::kNone;
};

} // namespace client::pause
