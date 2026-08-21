export module skiff.nodes;

import std;
import skia;
import skiff.paint;
import skiff.scene;

// The drawables the screens are actually built out of: boxes, text, sprites,
// flows, scroll containers and clickable areas. Everything here is a
// scene::Drawable, so it inherits layout, transforms and hit testing.
export namespace skiff::nodes {

using skiff::scene::Anchor;
using skiff::scene::Axes;
using skiff::scene::Drawable;
using skiff::scene::Easing;
using skiff::scene::Margin;

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

  void setText(std::string text) {
    if (text == fText) {
      return;
    }
    fText = std::move(text);
    fMeasuredSize = -1.0f;
    this->invalidateLayout();
  }
  void setColour(skia::SkColor colour) { fColour = colour; }
  [[nodiscard]] const std::string &text() const noexcept { return fText; }

  // Set to clip instead of auto-sizing: the text is cut to the given width.
  void setMaxWidth(float width) {
    fMaxWidth = width;
    fMeasuredSize = -1.0f;
  }

  static void setFont(skia::SkFont *font) {
    skiff::paint::defaultFont() = font;
  }

protected:
  // Text sizes itself: a flow then reads the size off like any other child.
  void measure(const skia::SkRect &) override {
    if (fMeasuredSize == fSize) {
      return; // already measured at this size, and the text has not changed
    }
    skia::SkFont *font = skiff::paint::defaultFont();
    if (font == nullptr) {
      return;
    }
    font->setSize(fSize);
    skiff::paint::fonts().applyWeight(*font, fBold);
    const float measured = skiff::paint::fonts().measure(*font, fText);
    skiff::paint::fonts().applyWeight(*font, false);
    fWidth = fMaxWidth > 0.0f ? std::min(fMaxWidth, measured) : measured;
    fHeight = fSize * 1.25f;
    fMeasuredSize = fSize;
  }

  void drawSelf(skia::SkCanvas *canvas, float alpha) override {
    skia::SkFont *font = skiff::paint::defaultFont();
    if (font == nullptr || fText.empty()) {
      return;
    }
    font->setSize(fSize);
    skiff::paint::fonts().applyWeight(*font, fBold);
    skia::SkPaint paint;
    paint.setAntiAlias(true);
    paint.setColor(fColour);
    paint.setAlphaf(alpha);
    const int saved = canvas->save();
    if (fMaxWidth > 0.0f) {
      canvas->clipRect(fBounds, true);
    }
    // The baseline sits at the top plus the ascent share of the line box.
    skiff::paint::fonts().draw(canvas, *font, fText, fBounds.fLeft,
                             fBounds.fTop + fSize, paint);
    canvas->restoreToCount(saved);
    skiff::paint::fonts().applyWeight(*font, false);
  }

private:
  std::string fText;
  float fSize;
  skia::SkColor fColour;
  bool fBold;
  float fMaxWidth = 0.0f;
  float fMeasuredSize = -1.0f; // the size the cached width was measured at
};

// An image, cropped to fill its box rather than squashed into it.
class Sprite : public Drawable {
public:
  explicit Sprite(skia::Sp<skia::SkImage> image) : fImage(std::move(image)) {}

  void setImage(skia::Sp<skia::SkImage> image) { fImage = std::move(image); }

protected:
  void drawSelf(skia::SkCanvas *canvas, float alpha) override {
    skiff::paint::imageFilled(canvas, fImage.get(), fBounds, alpha);
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
  // Spacing is part of what a flow *is*, so it can be given at construction
  // rather than in a setter call on the line after every one of them.
  FillFlow(Direction direction, float spacingX, float spacingY)
      : fSpacingX(spacingX), fSpacingY(spacingY), fDirection(direction) {}

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

  // Carried across a rebuild: a list that grew a page should stay where the
  // reader left it, not jump back to the top.
  void setCurrent(float offset) {
    fCurrent = offset;
    fTarget = offset;
  }
  // Eased: the view glides to the offset rather than jumping to it, which is
  // what a jump to a section in a settings list should look like.
  void scrollTo(float offset) {
    fTarget = std::clamp(offset, 0.0f, fExtent);
    this->invalidateLayout();
  }

  // Still gliding towards where it was asked to go.
  [[nodiscard]] bool moving() const noexcept {
    return std::abs(fCurrent - fTarget) > 0.05f;
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
    const float previous = fCurrent;
    fCurrent = skiff::paint::approach(fCurrent, fTarget, 30.0f, dt);
    if (std::abs(fCurrent - fTarget) < 0.05f) {
      fCurrent = fTarget; // settle, so a still list stops re-laying out
    }
    if (fCurrent != previous) {
      this->invalidateLayout();
    }
  }

  bool onScroll(float ticks) override {
    fTarget = std::clamp(fTarget - ticks * 60.0f, 0.0f, fExtent);
    this->invalidateLayout();
    return true;
  }

private:
  float fCurrent = 0.0f;
  float fTarget = 0.0f;
  float fExtent = 0.0f;
  double fLastMs = 0.0;
};

// A subtree rendered once into a texture and shown from it afterwards.
//
// Lists of cards, panels of settings and pages of metadata are expensive to
// draw and change rarely: hundreds of glyphs, rounded rectangles and images
// per frame that produce the same pixels every time. This draws them once
// into an offscreen surface and blits that until something inside changes.
// While the subtree is animating, caching would cost more than it saves, so
// it draws straight through.
class CachedContainer : public Drawable {
public:
  // The GPU context the cache surfaces are created on, handed over by the app
  // once the renderer exists.
  static void setContext(skia::GrDirectContext *context) {
    contextSlot() = context;
  }

  void invalidateCache() { fCacheValid = false; }

  void draw(skia::SkCanvas *canvas, float inheritedAlpha = 1.0f) override {
    if (!fVisible || fAlpha <= 0.001f) {
      return;
    }
    skia::GrDirectContext *context = contextSlot();
    const int width = static_cast<int>(std::ceil(fBounds.width()));
    const int height = static_cast<int>(std::ceil(fBounds.height()));
    if (context == nullptr || width <= 0 || height <= 0 ||
        this->animatingTree()) {
      Drawable::draw(canvas, inheritedAlpha);
      return;
    }

    if (!fCache || fCacheWidth != width || fCacheHeight != height) {
      fCache = skia::RenderTarget(
          context, skia::kNo,
          skia::SkImageInfo::Make(width, height, skia::kRGBA_8888_SkColorType,
                                  skia::kPremul_SkAlphaType));
      fCacheWidth = width;
      fCacheHeight = height;
      fCacheValid = false;
    }
    if (!fCache) {
      Drawable::draw(canvas, inheritedAlpha);
      return;
    }

    if (!fCacheValid || fCachedOrigin != fBounds.fLeft + fBounds.fTop) {
      auto *cacheCanvas = fCache->getCanvas();
      cacheCanvas->clear(skia::colorSetARGB(0, 0, 0, 0));
      const int saved = cacheCanvas->save();
      // The subtree is laid out in screen space; shift it into the cache.
      cacheCanvas->translate(-fBounds.fLeft, -fBounds.fTop);
      Drawable::draw(cacheCanvas, 1.0f);
      cacheCanvas->restoreToCount(saved);
      context->flushAndSubmit(fCache.get());
      fCacheValid = true;
      fCachedOrigin = fBounds.fLeft + fBounds.fTop;
    }

    skia::SkPaint paint;
    paint.setAlphaf(inheritedAlpha * fAlpha);
    auto image = fCache->makeImageSnapshot();
    if (image) {
      canvas->drawImage(image.get(), fBounds.fLeft, fBounds.fTop,
                        skia::SkSamplingOptions(), &paint);
    }
    fDrawnBounds = fBounds;
  }

protected:
  // Anything that moves inside invalidates what was captured.
  void layoutChildren() override {
    Drawable::layoutChildren();
    fCacheValid = false;
  }

private:
  static skia::GrDirectContext *&contextSlot() {
    static skia::GrDirectContext *context = nullptr;
    return context;
  }

  skia::Sp<skia::SkSurface> fCache;
  int fCacheWidth = 0;
  int fCacheHeight = 0;
  bool fCacheValid = false;
  float fCachedOrigin = 0.0f;
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

} // namespace skiff::nodes
