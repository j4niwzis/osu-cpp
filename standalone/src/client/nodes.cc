export module client.nodes;

import std;
import skia;
import client.ui;
import client.scene;

// The drawables the screens are actually built out of: boxes, text, sprites,
// flows, scroll containers and clickable areas. Everything here is a
// scene::Drawable, so it inherits layout, transforms and hit testing.
export namespace client::nodes {

using client::scene::Anchor;
using client::scene::Axes;
using client::scene::Drawable;
using client::scene::Easing;
using client::scene::Margin;

// A filled rectangle, optionally rounded. The framework's Box.
class Box : public Drawable {
public:
  explicit Box(skia::SkColor colour) : fColour(colour) {}

  void setColour(skia::SkColor colour) { fColour = colour; }

protected:
  void drawSelf(skia::SkCanvas *canvas, float alpha) override {
    skia::SkPaint paint;
    paint.setAntiAlias(true);
    paint.setColor(fColour);
    paint.setAlphaf(alpha);
    if (fCornerRadius > 0.0f) {
      canvas->drawRRect(
          skia::SkRRect::MakeRectXY(fBounds, fCornerRadius, fCornerRadius),
          paint);
    } else {
      canvas->drawRect(fBounds, paint);
    }
  }

private:
  skia::SkColor fColour;
};

// A line of text. Auto-sizes to what it draws, so a flow can lay it out
// without anyone measuring by hand.
class Text : public Drawable {
public:
  Text(std::string text, float size, skia::SkColor colour, bool bold = false)
      : fText(std::move(text)), fSize(size), fColour(colour), fBold(bold) {}

  void setText(std::string text) { fText = std::move(text); }
  void setColour(skia::SkColor colour) { fColour = colour; }
  [[nodiscard]] const std::string &text() const noexcept { return fText; }

  // Set to clip instead of auto-sizing: the text is cut to the given width.
  void setMaxWidth(float width) { fMaxWidth = width; }

  static void setFont(skia::SkFont *font) { fontSlot() = font; }

protected:
  // Text sizes itself: a flow then reads the size off like any other child.
  void measure() override {
    skia::SkFont *font = fontSlot();
    if (font == nullptr) {
      return;
    }
    font->setSize(fSize);
    font->setEmbolden(fBold);
    const float measured = client::ui::fonts().measure(*font, fText);
    font->setEmbolden(false);
    fWidth = fMaxWidth > 0.0f ? std::min(fMaxWidth, measured) : measured;
    fHeight = fSize * 1.25f;
  }

  void drawSelf(skia::SkCanvas *canvas, float alpha) override {
    skia::SkFont *font = fontSlot();
    if (font == nullptr || fText.empty()) {
      return;
    }
    font->setSize(fSize);
    font->setEmbolden(fBold);
    skia::SkPaint paint;
    paint.setAntiAlias(true);
    paint.setColor(fColour);
    paint.setAlphaf(alpha);
    const int saved = canvas->save();
    if (fMaxWidth > 0.0f) {
      canvas->clipRect(fBounds, true);
    }
    // The baseline sits at the top plus the ascent share of the line box.
    client::ui::fonts().draw(canvas, *font, fText, fBounds.fLeft,
                             fBounds.fTop + fSize, paint);
    canvas->restoreToCount(saved);
    font->setEmbolden(false);
  }

private:
  // One font for the whole tree, handed over by the app at startup.
  static skia::SkFont *&fontSlot() {
    static skia::SkFont *font = nullptr;
    return font;
  }

  std::string fText;
  float fSize;
  skia::SkColor fColour;
  bool fBold;
  float fMaxWidth = 0.0f;
};

// An image, cropped to fill its box rather than squashed into it.
class Sprite : public Drawable {
public:
  explicit Sprite(skia::Sp<skia::SkImage> image) : fImage(std::move(image)) {}

  void setImage(skia::Sp<skia::SkImage> image) { fImage = std::move(image); }

protected:
  void drawSelf(skia::SkCanvas *canvas, float alpha) override {
    if (!fImage) {
      return;
    }
    const float iw = static_cast<float>(fImage->width());
    const float ih = static_cast<float>(fImage->height());
    if (iw <= 0.0f || ih <= 0.0f) {
      return;
    }
    const float scale = std::max(fBounds.width() / iw, fBounds.height() / ih);
    const float srcW = fBounds.width() / scale;
    const float srcH = fBounds.height() / scale;
    const skia::SkRect src = skia::SkRect::MakeXYWH(
        (iw - srcW) * 0.5f, (ih - srcH) * 0.5f, srcW, srcH);
    skia::SkPaint paint;
    paint.setAlphaf(alpha);
    canvas->drawImageRect(fImage.get(), src, fBounds,
                          skia::SkSamplingOptions(skia::SkFilterMode::kLinear),
                          &paint, skia::SkCanvas::kStrict_SrcRectConstraint);
  }

private:
  skia::Sp<skia::SkImage> fImage;
};

// FillFlowContainer: children laid end to end, wrapping when they run out of
// room, which is how lazer builds every list and row of filters.
class FillFlow : public Drawable {
public:
  enum class Direction : std::uint8_t { kHorizontal, kVertical };

  explicit FillFlow(Direction direction = Direction::kVertical)
      : fDirection(direction) {}

  float fSpacingX = 0.0f;
  float fSpacingY = 0.0f;
  bool fWrap = true;
  bool fCentreRows = false; // rows centred in the container, as the cards are

  void setSpacing(float x, float y) {
    fSpacingX = x;
    fSpacingY = y;
  }

protected:
  void layoutChildren() override {
    const skia::SkRect box = this->contentBox();
    if (fDirection == Direction::kVertical) {
      float y = 0.0f;
      for (auto &child : fChildren) {
        if (!child->fVisible) {
          continue;
        }
        child->fAnchor = Anchor::kTopLeft;
        child->fOrigin = Anchor::kTopLeft;
        child->fY = y;
        child->layout(box);
        y += child->fBounds.height() + child->fMargin.totalY() + fSpacingY;
      }
      return;
    }

    // Horizontal: measure each child, break rows at the edge, then place them.
    std::vector<Drawable *> row;
    float rowWidth = 0.0f;
    float y = 0.0f;
    const auto flushRow = [&] {
      if (row.empty()) {
        return;
      }
      float x = fCentreRows ? (box.width() - rowWidth) * 0.5f : 0.0f;
      float rowHeight = 0.0f;
      for (Drawable *child : row) {
        child->fAnchor = Anchor::kTopLeft;
        child->fOrigin = Anchor::kTopLeft;
        child->fX = x;
        child->fY = y;
        child->layout(box);
        x += child->fBounds.width() + child->fMargin.totalX() + fSpacingX;
        rowHeight = std::max(rowHeight, child->fBounds.height() +
                                            child->fMargin.totalY());
      }
      y += rowHeight + fSpacingY;
      row.clear();
      rowWidth = 0.0f;
    };

    for (auto &child : fChildren) {
      if (!child->fVisible) {
        continue;
      }
      // Lay it out once against the box to learn its size.
      child->fX = 0.0f;
      child->fY = 0.0f;
      child->layout(box);
      const float width = child->fBounds.width() + child->fMargin.totalX();
      if (fWrap && !row.empty() && rowWidth + fSpacingX + width > box.width()) {
        flushRow();
      }
      rowWidth += row.empty() ? width : fSpacingX + width;
      row.push_back(child.get());
    }
    flushRow();
  }

private:
  Direction fDirection;
};

// A container that scrolls its children and clips them to itself.
class ScrollContainer : public Drawable {
public:
  ScrollContainer() {
    fMasking = true;
  }

  void scrollToStart() {
    fTarget = 0.0f;
    fCurrent = 0.0f;
  }
  [[nodiscard]] float current() const noexcept { return fCurrent; }
  [[nodiscard]] float extent() const noexcept { return fExtent; }

protected:
  void layoutChildren() override {
    const skia::SkRect box = this->contentBox();
    const skia::SkRect scrolled =
        skia::SkRect::MakeXYWH(box.fLeft, box.fTop - fCurrent, box.width(),
                               box.height());
    for (auto &child : fChildren) {
      child->layout(scrolled);
    }
    const skia::SkRect content = this->childBounds();
    fExtent = std::max(0.0f, content.height() - box.height());
    fTarget = std::clamp(fTarget, 0.0f, fExtent);
  }

  void update(double nowMs) override {
    const double dt = fLastMs > 0.0 ? nowMs - fLastMs : 16.0;
    fLastMs = nowMs;
    fCurrent = client::ui::approach(fCurrent, fTarget, 30.0f, dt);
  }

  bool onScroll(float ticks) override {
    fTarget = std::clamp(fTarget - ticks * 60.0f, 0.0f, fExtent);
    return true;
  }

private:
  float fCurrent = 0.0f;
  float fTarget = 0.0f;
  float fExtent = 0.0f;
  double fLastMs = 0.0;
};

// Anything that reacts to a click. The action is what the screen wants done.
class Clickable : public Drawable {
public:
  explicit Clickable(std::function<void()> action)
      : fAction(std::move(action)) {}

protected:
  bool acceptsInput() const override { return true; }
  bool onClick(float, float) override {
    if (fAction) {
      fAction();
    }
    return true;
  }

private:
  std::function<void()> fAction;
};

} // namespace client::nodes
