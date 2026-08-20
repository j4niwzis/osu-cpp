export module client.scene;

import std;
import skia;
import client.ui;

// A retained scene graph, in the shape osu!framework gives its drawables.
//
// The screens here were immediate-mode: every frame recomputed rectangles,
// pushed them into a list for hit testing, and drew. Layout was arithmetic
// inlined into drawing, animation state lived in vectors indexed in parallel
// with the data, and scroll offsets were subtracted from mouse coordinates by
// hand at each comparison. That is where the misplaced-row bug came from, and
// it is why porting a lazer layout meant translating its containers into
// arithmetic instead of just writing them down.
//
// This is the smaller half of what osu!framework does, kept to what the
// screens actually use: anchors and origins, relative and automatic sizing,
// flow and scroll containers, transforms with easing, and hit testing through
// the tree.
export namespace client::scene {

// ---- geometry -------------------------------------------------------------

enum class Axes : std::uint8_t { kNone, kX, kY, kBoth };

[[nodiscard]] inline bool hasX(Axes a) noexcept {
  return a == Axes::kX || a == Axes::kBoth;
}
[[nodiscard]] inline bool hasY(Axes a) noexcept {
  return a == Axes::kY || a == Axes::kBoth;
}

// The nine positions a drawable can be anchored to, as in the framework.
enum class Anchor : std::uint8_t {
  kTopLeft, kTopCentre, kTopRight,
  kCentreLeft, kCentre, kCentreRight,
  kBottomLeft, kBottomCentre, kBottomRight
};

[[nodiscard]] inline float anchorX(Anchor a) noexcept {
  switch (a) {
  case Anchor::kTopCentre:
  case Anchor::kCentre:
  case Anchor::kBottomCentre:
    return 0.5f;
  case Anchor::kTopRight:
  case Anchor::kCentreRight:
  case Anchor::kBottomRight:
    return 1.0f;
  default:
    return 0.0f;
  }
}

[[nodiscard]] inline float anchorY(Anchor a) noexcept {
  switch (a) {
  case Anchor::kCentreLeft:
  case Anchor::kCentre:
  case Anchor::kCentreRight:
    return 0.5f;
  case Anchor::kBottomLeft:
  case Anchor::kBottomCentre:
  case Anchor::kBottomRight:
    return 1.0f;
  default:
    return 0.0f;
  }
}

struct Margin {
  float fTop = 0.0f, fRight = 0.0f, fBottom = 0.0f, fLeft = 0.0f;

  [[nodiscard]] static Margin all(float v) { return {v, v, v, v}; }
  [[nodiscard]] static Margin horizontal(float v) { return {0, v, 0, v}; }
  [[nodiscard]] static Margin vertical(float v) { return {v, 0, v, 0}; }
  [[nodiscard]] float totalX() const noexcept { return fLeft + fRight; }
  [[nodiscard]] float totalY() const noexcept { return fTop + fBottom; }
};

// ---- transforms -----------------------------------------------------------

enum class Easing : std::uint8_t { kNone, kOut, kOutQuint, kOutElasticHalf };

[[nodiscard]] inline float ease(Easing e, float t) {
  t = std::clamp(t, 0.0f, 1.0f);
  switch (e) {
  case Easing::kOut:
    return 1.0f - (1.0f - t) * (1.0f - t);
  case Easing::kOutQuint:
    return client::ui::outQuint(t);
  case Easing::kOutElasticHalf:
    return client::ui::outElasticHalf(t);
  case Easing::kNone:
    break;
  }
  return t;
}

// What a transform animates. Keeping the set closed means no allocation and
// no virtual dispatch per property.
enum class Property : std::uint8_t { kAlpha, kX, kY, kWidth, kHeight, kScale };

struct Transform {
  Property fProperty = Property::kAlpha;
  float fFrom = 0.0f;
  float fTo = 0.0f;
  double fStartMs = 0.0;
  double fEndMs = 0.0;
  Easing fEasing = Easing::kNone;
};

// ---- the node ------------------------------------------------------------

class Drawable {
public:
  Drawable() = default;
  Drawable(const Drawable &) = delete;
  Drawable &operator=(const Drawable &) = delete;
  virtual ~Drawable() = default;

  // -- layout inputs, set by whoever builds the tree
  float fWidth = 0.0f, fHeight = 0.0f;   // absolute, or a fraction if relative
  Axes fRelativeSizeAxes = Axes::kNone;  // size is a fraction of the parent
  Axes fAutoSizeAxes = Axes::kNone;      // size follows the children
  Anchor fAnchor = Anchor::kTopLeft;     // point in the parent to attach to
  Anchor fOrigin = Anchor::kTopLeft;     // point in this drawable that lands there
  Margin fMargin;                        // outside the drawable
  Margin fPadding;                       // inside, applied to children
  float fX = 0.0f, fY = 0.0f;            // offset from the anchor
  float fScale = 1.0f;
  float fAlpha = 1.0f;
  bool fMasking = false;                 // clip children to these bounds
  float fCornerRadius = 0.0f;
  bool fVisible = true;

  // -- computed by layout()
  skia::SkRect fBounds = skia::SkRect::MakeEmpty();

  void add(std::unique_ptr<Drawable> child) {
    child->fParent = this;
    fChildren.push_back(std::move(child));
    this->markDamaged();
  }
  void clear() { fChildren.clear(); }
  [[nodiscard]] std::span<const std::unique_ptr<Drawable>> children() const {
    return fChildren;
  }

  // -- transforms
  void fadeTo(float target, double durationMs, Easing e = Easing::kOutQuint) {
    this->transformTo(Property::kAlpha, fAlpha, target, durationMs, e);
  }
  void moveToX(float target, double durationMs, Easing e = Easing::kOutQuint) {
    this->transformTo(Property::kX, fX, target, durationMs, e);
  }
  void moveToY(float target, double durationMs, Easing e = Easing::kOutQuint) {
    this->transformTo(Property::kY, fY, target, durationMs, e);
  }
  void resizeWidthTo(float target, double durationMs,
                     Easing e = Easing::kOutQuint) {
    this->transformTo(Property::kWidth, fWidth, target, durationMs, e);
  }
  void resizeHeightTo(float target, double durationMs,
                      Easing e = Easing::kOutQuint) {
    this->transformTo(Property::kHeight, fHeight, target, durationMs, e);
  }
  void scaleTo(float target, double durationMs, Easing e = Easing::kOutQuint) {
    this->transformTo(Property::kScale, fScale, target, durationMs, e);
  }
  // Everything queued after this starts that much later, as With(Delay) does.
  void delay(double ms) { fDelayMs = ms; }

  [[nodiscard]] bool transforming() const noexcept {
    return !fTransforms.empty();
  }

  // Advances every transform in the tree and drops the finished ones.
  void updateTree(double nowMs) {
    if (!fTransforms.empty()) {
      fLayoutValid = false; // a moving drawable needs placing again
      this->markDamaged();
    }
    this->updateTransforms(nowMs);
    this->update(nowMs);
    for (auto &child : fChildren) {
      child->updateTree(nowMs);
    }
  }

  // Re-lays the tree only when it can have changed: the box it sits in
  // differs from last time, something is animating, or a screen said so.
  // A menu that is standing still costs nothing but a draw.
  bool layoutIfNeeded(const skia::SkRect &parent) {
    if (fLayoutValid && parent == fLastParent && !this->animatingTree()) {
      return false;
    }
    fLastParent = parent;
    this->layout(parent);
    return true;
  }

  // Marks this drawable, and everything under it, as needing layout again.
  void invalidateLayout() {
    this->markDamaged();
    fLayoutValid = false;
    for (auto &child : fChildren) {
      child->invalidateLayout();
    }
  }

  [[nodiscard]] bool animatingTree() const {
    if (!fTransforms.empty()) {
      return true;
    }
    for (const auto &child : fChildren) {
      if (child->animatingTree()) {
        return true;
      }
    }
    return false;
  }

  // Places this drawable inside `parent` (already absolute) and its children
  // inside itself.
  void layout(const skia::SkRect &parent) {
    // A drawable that knows its own size -- text, mainly -- says so before
    // anything is computed from it.
    this->measure();
    const float parentW = parent.width();
    const float parentH = parent.height();

    float width = hasX(fRelativeSizeAxes) ? parentW * fWidth : fWidth;
    float height = hasY(fRelativeSizeAxes) ? parentH * fHeight : fHeight;
    width -= fMargin.totalX();
    height -= fMargin.totalY();

    // Auto-sized axes need the children measured first, which needs a
    // provisional box to lay them out in.
    if (fAutoSizeAxes != Axes::kNone) {
      const skia::SkRect provisional = skia::SkRect::MakeXYWH(
          parent.fLeft, parent.fTop, hasX(fAutoSizeAxes) ? parentW : width,
          hasY(fAutoSizeAxes) ? parentH : height);
      fBounds = provisional;
      this->layoutChildren();
      const skia::SkRect content = this->childBounds();
      if (hasX(fAutoSizeAxes)) {
        width = content.width() + fPadding.totalX();
      }
      if (hasY(fAutoSizeAxes)) {
        height = content.height() + fPadding.totalY();
      }
    }

    width *= fScale;
    height *= fScale;

    const float ax = parent.fLeft + parentW * anchorX(fAnchor);
    const float ay = parent.fTop + parentH * anchorY(fAnchor);
    const float left = ax - width * anchorX(fOrigin) + fX + fMargin.fLeft;
    const float top = ay - height * anchorY(fOrigin) + fY + fMargin.fTop;
    fBounds = skia::SkRect::MakeXYWH(left, top, width, height);

    this->layoutChildren();
    fLayoutValid = true;
  }

  // The box children are laid out in: this drawable, less its padding.
  [[nodiscard]] skia::SkRect contentBox() const {
    return skia::SkRect::MakeLTRB(
        fBounds.fLeft + fPadding.fLeft, fBounds.fTop + fPadding.fTop,
        fBounds.fRight - fPadding.fRight, fBounds.fBottom - fPadding.fBottom);
  }

  void draw(skia::SkCanvas *canvas, float inheritedAlpha = 1.0f) {
    if (!fVisible || fAlpha <= 0.001f) {
      return;
    }
    const float alpha = inheritedAlpha * fAlpha;
    const int saved = canvas->save();
    if (fMasking) {
      if (fCornerRadius > 0.0f) {
        canvas->clipRRect(skia::SkRRect::MakeRectXY(fBounds, fCornerRadius,
                                                    fCornerRadius),
                          true);
      } else {
        canvas->clipRect(fBounds, true);
      }
    }
    this->drawSelf(canvas, alpha);
    for (auto &child : fChildren) {
      child->draw(canvas, alpha);
    }
    canvas->restoreToCount(saved);
    fDrawnBounds = fBounds; // where a later move has to repaint from
  }

  // Hit testing walks the tree from the front, so what is drawn last is what
  // is clicked first. Coordinates are absolute the whole way down, which is
  // what having laid everything out in absolute terms buys.
  [[nodiscard]] Drawable *hitTest(float x, float y) {
    if (!fVisible || fAlpha <= 0.001f) {
      return nullptr;
    }
    if (fMasking && !fBounds.contains(x, y)) {
      return nullptr;
    }
    for (auto it = fChildren.rbegin(); it != fChildren.rend(); ++it) {
      if (Drawable *hit = (*it)->hitTest(x, y)) {
        return hit;
      }
    }
    return this->acceptsInput() && fBounds.contains(x, y) ? this : nullptr;
  }

  // Delivers a click to the front-most drawable that wants it, then up the
  // tree until something handles it.
  bool click(float x, float y) {
    if (!fVisible || fAlpha <= 0.001f) {
      return false;
    }
    if (fMasking && !fBounds.contains(x, y)) {
      return false;
    }
    for (auto it = fChildren.rbegin(); it != fChildren.rend(); ++it) {
      if ((*it)->click(x, y)) {
        return true;
      }
    }
    return fBounds.contains(x, y) && this->onClick(x, y);
  }

  bool scroll(float x, float y, float ticks) {
    if (!fVisible || !fBounds.contains(x, y)) {
      return false;
    }
    for (auto it = fChildren.rbegin(); it != fChildren.rend(); ++it) {
      if ((*it)->scroll(x, y, ticks)) {
        return true;
      }
    }
    return this->onScroll(ticks);
  }

  // Walked only when the pointer actually moved: hover state cannot change
  // by itself, and this is a whole-tree traversal.
  void setHover(float x, float y) {
    if (x == fLastHoverX && y == fLastHoverY && fHoverSeen) {
      return;
    }
    fLastHoverX = x;
    fLastHoverY = y;
    fHoverSeen = true;
    this->applyHover(x, y);
  }
  [[nodiscard]] bool hovered() const noexcept { return fHovered; }

  // Where this drawable is, and where it was: a drawable that moved damages
  // both, or it leaves a copy of itself behind.
  void markDamaged() {
    Drawable *root = this;
    while (root->fParent != nullptr) {
      root = root->fParent;
    }
    root->joinDamage(fBounds);
    root->joinDamage(fDrawnBounds);
  }

  // What has to be repainted for this tree, and forgets it.
  [[nodiscard]] skia::SkRect takeDamage() {
    const skia::SkRect out = fDamageAccum;
    fDamageAccum = skia::SkRect::MakeEmpty();
    return out;
  }

  void joinDamage(const skia::SkRect &rect) {
    if (rect.isEmpty()) {
      return;
    }
    if (fDamageAccum.isEmpty()) {
      fDamageAccum = rect;
    } else {
      fDamageAccum.join(rect);
    }
  }

  void applyHover(float x, float y) {
    const bool hovered = fVisible && fBounds.contains(x, y);
    if (hovered != fHovered) {
      fHovered = hovered;
      this->markDamaged(); // hover is drawn, so a change to it is damage
    }
    for (auto &child : fChildren) {
      child->applyHover(x, y);
    }
  }

protected:
  virtual void drawSelf(skia::SkCanvas *, float) {}
  virtual void layoutChildren() {
    const skia::SkRect box = this->contentBox();
    for (auto &child : fChildren) {
      child->layout(box);
    }
  }
  virtual void update(double) {}
  // Chance to set fWidth/fHeight from content before layout uses them.
  virtual void measure() {}
  virtual bool acceptsInput() const { return false; }
  virtual bool onClick(float, float) { return false; }
  virtual bool onScroll(float) { return false; }

  [[nodiscard]] skia::SkRect childBounds() const {
    skia::SkRect content = skia::SkRect::MakeEmpty();
    for (const auto &child : fChildren) {
      if (!child->fVisible) {
        continue;
      }
      if (content.isEmpty()) {
        content = child->fBounds;
      } else {
        content.join(child->fBounds);
      }
    }
    return content;
  }

  std::vector<std::unique_ptr<Drawable>> fChildren;
  bool fHovered = false;
  float fLastHoverX = 0.0f, fLastHoverY = 0.0f;
  bool fHoverSeen = false;

private:
  void transformTo(Property property, float from, float to, double durationMs,
                   Easing e) {
    // A new transform on a property replaces whatever was animating it.
    std::erase_if(fTransforms, [property](const Transform &t) {
      return t.fProperty == property;
    });
    if (durationMs <= 0.0) {
      this->applyProperty(property, to);
      return;
    }
    fTransforms.push_back({property, from, to, fPendingStartMs + fDelayMs,
                           fPendingStartMs + fDelayMs + durationMs, e});
  }

  void updateTransforms(double nowMs) {
    fPendingStartMs = nowMs;
    if (fTransforms.empty()) {
      return;
    }
    for (auto &t : fTransforms) {
      // Transforms queued before the first update have no clock yet; start
      // them now rather than treating them as long finished.
      if (t.fStartMs <= 0.0) {
        const double duration = t.fEndMs - t.fStartMs;
        t.fStartMs = nowMs;
        t.fEndMs = nowMs + duration;
      }
      const double span = t.fEndMs - t.fStartMs;
      const float progress =
          span > 0.0 ? static_cast<float>((nowMs - t.fStartMs) / span) : 1.0f;
      this->applyProperty(t.fProperty,
                          t.fFrom + (t.fTo - t.fFrom) *
                                        ease(t.fEasing, progress));
    }
    std::erase_if(fTransforms,
                  [nowMs](const Transform &t) { return nowMs >= t.fEndMs; });
  }

  void applyProperty(Property property, float value) {
    switch (property) {
    case Property::kAlpha: fAlpha = value; break;
    case Property::kX: fX = value; break;
    case Property::kY: fY = value; break;
    case Property::kWidth: fWidth = value; break;
    case Property::kHeight: fHeight = value; break;
    case Property::kScale: fScale = value; break;
    }
  }

  std::vector<Transform> fTransforms;
  double fPendingStartMs = 0.0;
  double fDelayMs = 0.0;
  bool fLayoutValid = false;
  skia::SkRect fLastParent = skia::SkRect::MakeEmpty();

public:
  // Damage bookkeeping is reached through the parent chain, so these are not
  // private: a child hands its rectangle to the root it belongs to.
  skia::SkRect fDrawnBounds = skia::SkRect::MakeEmpty();
  skia::SkRect fDamageAccum = skia::SkRect::MakeEmpty(); // meaningful at roots
  Drawable *fParent = nullptr;
};

} // namespace client::scene
