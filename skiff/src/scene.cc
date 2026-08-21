export module skiff.scene;

import std;
import skia;
import skiff.paint;

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
export namespace skiff::scene {

// ---- geometry -------------------------------------------------------------

enum class Axes : std::uint8_t { kNone, kX, kY, kBoth };

[[nodiscard]] inline bool hasX(Axes a) noexcept {
  return a == Axes::kX || a == Axes::kBoth;
}
[[nodiscard]] inline bool hasY(Axes a) noexcept {
  return a == Axes::kY || a == Axes::kBoth;
}

[[nodiscard]] inline Axes axesUnion(Axes a, Axes b) noexcept {
  const bool x = hasX(a) || hasX(b);
  const bool y = hasY(a) || hasY(b);
  if (x && y) return Axes::kBoth;
  if (x) return Axes::kX;
  if (y) return Axes::kY;
  return Axes::kNone;
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

// How many drawables a frame walked and how many it actually drew. Counters
// rather than anything cleverer: the question "why does a frame cost what it
// costs" has been answered by guessing twice now, and guessing is slower than
// counting.
inline std::uint64_t &visitedCount() {
  static std::uint64_t count = 0;
  return count;
}
inline std::uint64_t &drawnCount() {
  static std::uint64_t count = 0;
  return count;
}

// ---- transforms -----------------------------------------------------------

enum class Easing : std::uint8_t { kNone, kOut, kOutQuint, kOutElasticHalf };

[[nodiscard]] inline float ease(Easing e, float t) {
  t = std::clamp(t, 0.0f, 1.0f);
  switch (e) {
  case Easing::kOut:
    return 1.0f - (1.0f - t) * (1.0f - t);
  case Easing::kOutQuint:
    return skiff::paint::outQuint(t);
  case Easing::kOutElasticHalf:
    return skiff::paint::outElasticHalf(t);
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

// ---- the specification ----------------------------------------------------

// Every layout input a drawable has, gathered into one aggregate so that a
// node can be written down rather than assembled field by field:
//
//   auto *bg = row->add<nodes::Box>({.fill = true, .cornerRadius = 6.0f},
//                                   kBackground);
//
// Each member's default is the drawable's own, so an empty spec `{}` changes
// nothing and every field left out stays at what the class chose. Four of
// them are shorthands rather than fields of their own, covering the idioms
// that the screens repeat most:
//
//   place        anchor and origin at once, which is what all but a handful
//                of call sites want; anchor/origin override it individually
//   fill         relative size on both axes at 1.0 -- "as big as my parent"
//   fillX/fillY  the same on one axis, leaving the other to width/height,
//                as in "full width, forty pixels tall"
//
// Designated initialisers must be written in declaration order, so the order
// here is the order a layout is usually thought about: where it sits, how big
// it is, what surrounds it, then how it looks.
struct Spec {
  Anchor place = Anchor::kTopLeft;
  std::optional<Anchor> anchor{};
  std::optional<Anchor> origin{};
  float x = 0.0f, y = 0.0f;

  bool fill = false;
  bool fillX = false;
  bool fillY = false;
  float width = 0.0f, height = 0.0f;
  Axes relativeSize = Axes::kNone;
  Axes autoSize = Axes::kNone;

  Margin margin{};
  Margin padding{};

  float cornerRadius = 0.0f;
  bool masking = false;
  float scale = 1.0f;
  float alpha = 1.0f;
  bool visible = true;
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

  // Writes a spec onto this drawable. Anything the spec does not mention is
  // left as the class set it, which is what makes `{}` a no-op and lets a
  // custom node keep the sizing its constructor chose.
  void apply(const Spec &spec) {
    fAnchor = spec.anchor.value_or(spec.place);
    fOrigin = spec.origin.value_or(spec.place);
    fX = spec.x;
    fY = spec.y;

    Axes relative = spec.relativeSize;
    if (spec.fill || spec.fillX) {
      relative = axesUnion(relative, Axes::kX);
      fWidth = 1.0f;
    } else if (spec.width != 0.0f) {
      fWidth = spec.width;
    }
    if (spec.fill || spec.fillY) {
      relative = axesUnion(relative, Axes::kY);
      fHeight = 1.0f;
    } else if (spec.height != 0.0f) {
      fHeight = spec.height;
    }
    if (relative != Axes::kNone) {
      fRelativeSizeAxes = relative;
    }
    if (spec.autoSize != Axes::kNone) {
      fAutoSizeAxes = spec.autoSize;
    }

    fMargin = spec.margin;
    fPadding = spec.padding;
    fCornerRadius = spec.cornerRadius;
    fMasking = spec.masking;
    fScale = spec.scale;
    fAlpha = spec.alpha;
    fVisible = spec.visible;

    // Sizing an axis both from the parent and from the children asks for two
    // different numbers at once. The framework throws here; this is a build
    // that has to keep drawing, so it says which node did it and picks the
    // relative one, rather than laying out something nobody asked for.
    if ((hasX(fRelativeSizeAxes) && hasX(fAutoSizeAxes)) ||
        (hasY(fRelativeSizeAxes) && hasY(fAutoSizeAxes))) {
      std::println(std::cerr,
                   "[scene] relative and automatic sizing on the same axis");
      fAutoSizeAxes = Axes::kNone;
    }
  }

  void add(std::unique_ptr<Drawable> child) {
    child->fParent = this;
    fChildren.push_back(std::move(child));
    this->markDamaged();
  }

  // Builds a child in place, applies the spec and hands it back typed. This
  // is the whole of what building a tree costs now: the make_unique, the run
  // of field assignments, the std::move and the .get() kept for later were
  // four separate things to get right per node, and three of them were the
  // same every time.
  template <class T, class... Args>
  T *add(const Spec &spec, Args &&...args) {
    auto child = std::make_unique<T>(std::forward<Args>(args)...);
    T *raw = child.get();
    raw->apply(spec);
    this->add(std::move(child));
    return raw;
  }

  // Same, for a node that was already built elsewhere -- returns it typed so
  // that keeping a pointer does not need a separate .get() before the move.
  template <class T>
  T *adopt(std::unique_ptr<T> child) {
    T *raw = child.get();
    this->add(std::move(child));
    return raw;
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

  // Marks this drawable as needing layout again -- and its ancestors, since
  // layout is asked for at the root: a child that quietly invalidated only
  // itself was never re-laid, which is what stopped a scroll container from
  // moving anything after the first frame.
  void invalidateLayout() {
    this->markDamaged();
    for (Drawable *node = this; node != nullptr; node = node->fParent) {
      node->fLayoutValid = false;
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
    // anything is computed from it. It is told the box it is going into,
    // since a row that wraps has a height only relative to a width.
    this->measure(parent);
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
    const skia::SkRect previous = fBounds;
    fBounds = skia::SkRect::MakeXYWH(left, top, width, height);
    if (fBounds != previous) {
      // A drawable that moved or changed size has to repaint both where it is
      // now and where it used to be, and the layout is the only place that
      // knows both. Without this, a card collapsing in the beatmap browser
      // left the rows below it standing where they were: they moved, nothing
      // said so, and a clipped frame painted them at their new place over the
      // copy at the old one.
      this->damageUpwards(previous);
      this->damageUpwards(fBounds);
    }

    this->layoutChildren();
    fLayoutValid = true;
  }

  // The box children are laid out in: this drawable, less its padding.
  [[nodiscard]] skia::SkRect contentBox() const {
    return skia::SkRect::MakeLTRB(
        fBounds.fLeft + fPadding.fLeft, fBounds.fTop + fPadding.fTop,
        fBounds.fRight - fPadding.fRight, fBounds.fBottom - fPadding.fBottom);
  }

  virtual void draw(skia::SkCanvas *canvas, float inheritedAlpha = 1.0f) {
    if (!fVisible || fAlpha <= 0.001f) {
      return;
    }
    // Anything lying outside what is being repainted is skipped whole, with
    // its subtree. Without this, a list of a few hundred cards is recorded in
    // full every frame and Skia discards the off-screen ones after it has
    // been told about them -- and once frames are clipped to damage, the same
    // test is what keeps a repaint of one card from walking the other 200.
    ++visitedCount();
    if (!fBounds.isEmpty() && canvas->quickReject(fBounds)) {
      return;
    }
    ++drawnCount();
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
    this->damageUpwards(fBounds);
    this->damageUpwards(fDrawnBounds);
  }

  // A rectangle is only worth repainting where it can be seen. On the way up
  // to the root, every masking ancestor clips it -- a scroll container is one
  // -- and a hidden ancestor drops it outright. What is left is what the root
  // is told about, and a card scrolled out of the list is left with nothing:
  // it changed, and changing where nobody can see it is not a reason to draw
  // a frame.
  void damageUpwards(skia::SkRect rect) {
    if (rect.isEmpty()) {
      return;
    }
    Drawable *node = this;
    while (node->fParent != nullptr) {
      node = node->fParent;
      // The drawable's own visibility is deliberately not tested: hiding one
      // is a change, and the frame that hides it has to repaint where it was.
      if (!node->fVisible || node->fAlpha <= 0.001f) {
        return;
      }
      if (node->fMasking && !rect.intersect(node->fBounds)) {
        return;
      }
    }
    if (node->fBounds.isEmpty() || rect.intersect(node->fBounds)) {
      node->joinDamage(rect);
    }
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
      // Only where hover is drawn. Every box, flow and container in the tree
      // was marking itself as the pointer crossed it, which is a repaint for
      // a picture that did not change -- and there are a lot more containers
      // than there are things that light up.
      if (this->hoverChangesAppearance()) {
        this->markDamaged();
      }
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
  virtual void measure(const skia::SkRect &) {}
  virtual bool acceptsInput() const { return false; }
  // Whether the pointer entering or leaving changes what this draws. Taking
  // input is the usual reason to light up, so that is the default; a drawable
  // that takes input only to swallow it says so.
  virtual bool hoverChangesAppearance() const { return this->acceptsInput(); }
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

// Builds a detached node -- a screen's root, or anything handed to somebody
// else's add(). The same spec as Drawable::add, for the cases where there is
// no parent yet to hang it on.
template <class T, class... Args>
[[nodiscard]] std::unique_ptr<T> make(const Spec &spec, Args &&...args) {
  auto node = std::make_unique<T>(std::forward<Args>(args)...);
  node->apply(spec);
  return node;
}


} // namespace skiff::scene
