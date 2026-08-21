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
  if (x && y)
    return Axes::kBoth;
  if (x)
    return Axes::kX;
  if (y)
    return Axes::kY;
  return Axes::kNone;
}

// The nine positions a drawable can be anchored to, as in the framework.
enum class Anchor : std::uint8_t {
  kTopLeft,
  kTopCentre,
  kTopRight,
  kCentreLeft,
  kCentre,
  kCentreRight,
  kBottomLeft,
  kBottomCentre,
  kBottomRight
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

// Where something sits across the axis it is being laid out along.
enum class Align : std::uint8_t { kStart, kMiddle, kEnd };

struct Margin {
  float fTop = 0.0f, fRight = 0.0f, fBottom = 0.0f, fLeft = 0.0f;

  [[nodiscard]] static Margin all(float v) { return {v, v, v, v}; }
  [[nodiscard]] static Margin horizontal(float v) { return {0, v, 0, v}; }
  [[nodiscard]] static Margin vertical(float v) { return {v, 0, v, 0}; }
  [[nodiscard]] float totalX() const noexcept { return fLeft + fRight; }
  [[nodiscard]] float totalY() const noexcept { return fTop + fBottom; }
};

// The box a thing of that size occupies when its `origin` point is put on the
// `anchor` point of `parent`, offset by dx and dy. This is what layout() does
// for a drawable, available on its own for the cases that are not drawables:
// a glyph inside a control, a bar inside a row.
[[nodiscard]] inline skia::SkRect
anchoredBox(const skia::SkRect &parent, float width, float height,
            Anchor anchor, Anchor origin, float dx = 0.0f, float dy = 0.0f) {
  const float ax = parent.fLeft + parent.width() * anchorX(anchor);
  const float ay = parent.fTop + parent.height() * anchorY(anchor);
  return skia::SkRect::MakeXYWH(ax - width * anchorX(origin) + dx,
                                ay - height * anchorY(origin) + dy, width,
                                height);
}

// The same point on both, which is what centring and edge-alignment are.
[[nodiscard]] inline skia::SkRect anchoredBox(const skia::SkRect &parent,
                                              float width, float height,
                                              Anchor at, float dx = 0.0f,
                                              float dy = 0.0f) {
  return anchoredBox(parent, width, height, at, at, dx, dy);
}

[[nodiscard]] inline skia::SkRect inset(const skia::SkRect &rect,
                                        const Margin &by) {
  return skia::SkRect::MakeLTRB(rect.fLeft + by.fLeft, rect.fTop + by.fTop,
                                rect.fRight - by.fRight,
                                rect.fBottom - by.fBottom);
}

[[nodiscard]] inline skia::SkRect inset(const skia::SkRect &rect,
                                        float horizontal, float vertical) {
  return inset(rect, Margin{vertical, horizontal, vertical, horizontal});
}

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
// A field left out is not written at all, which is what makes `{}` a no-op
// and lets a node keep what its constructor chose. That is not a nicety: the
// first cut wrote every field unconditionally, and a header that anchored
// itself to the top centre was silently dragged to the top left, while a
// scroll container that masks by default stopped clipping. Hence the
// optionals -- "zero" and "not mentioned" are different things.
//
// Four members are shorthands rather than fields of their own, covering the
// idioms the screens repeat most:
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
  std::optional<Anchor> place{};
  std::optional<Anchor> anchor{};
  std::optional<Anchor> origin{};
  std::optional<float> x{};
  std::optional<float> y{};

  bool fill = false;
  bool fillX = false;
  bool fillY = false;
  std::optional<float> width{};
  std::optional<float> height{};
  std::optional<Axes> relativeSize{};
  std::optional<Axes> autoSize{};
  std::optional<Axes> grow{};
  std::optional<float> minWidth{}, maxWidth{};
  std::optional<float> minHeight{}, maxHeight{};
  std::optional<Align> alignSelf{};
  std::optional<float> depth{};

  // These two are plain, not optional, because std::optional cannot be given
  // a braced list -- `.padding = {2, 6, 2, 6}` would stop compiling. An
  // all-zero margin therefore reads as "not mentioned", which costs a node
  // whose constructor set one the ability to be told to clear it. It can
  // still say so afterwards, and nothing does.
  Margin margin{};
  Margin padding{};

  std::optional<float> cornerRadius{};
  std::optional<bool> masking{};
  std::optional<float> scale{};
  std::optional<float> alpha{};
  std::optional<bool> visible{};
};

// ---- the node ------------------------------------------------------------

class Drawable {
public:
  Drawable() = default;
  Drawable(const Drawable &) = delete;
  Drawable &operator=(const Drawable &) = delete;
  virtual ~Drawable() = default;

  // -- layout inputs, set by whoever builds the tree
  float fWidth = 0.0f, fHeight = 0.0f;  // absolute, or a fraction if relative
  Axes fRelativeSizeAxes = Axes::kNone; // size is a fraction of the parent
  Axes fAutoSizeAxes = Axes::kNone;     // size follows the children
  // Inside a flow, takes an equal share of what the other children leave
  // along the flow's axis. Ignored anywhere else.
  Axes fGrowAxes = Axes::kNone;
  // Bounds on the computed size, applied after everything else has had its
  // say. Zero means no limit, on the maximums.
  float fMinWidth = 0.0f, fMaxWidth = 0.0f;
  float fMinHeight = 0.0f, fMaxHeight = 0.0f;
  // Overrides the container's alignment for this child alone.
  std::optional<Align> fAlignSelf{};
  // Drawn and hit-tested in this order within the parent, low first. Lets a
  // child be over its siblings without being moved up the tree.
  float fDepth = 0.0f;
  Anchor fAnchor = Anchor::kTopLeft; // point in the parent to attach to
  Anchor fOrigin = Anchor::kTopLeft; // point in this drawable that lands there
  Margin fMargin;                    // outside the drawable
  Margin fPadding;                   // inside, applied to children
  float fX = 0.0f, fY = 0.0f;        // offset from the anchor
  float fScale = 1.0f;
  float fAlpha = 1.0f;
  // Positioned and sized against this drawable rather than against the
  // parent, when set. Not owned, and it has to outlive this one -- which for
  // the case it exists for, a list against the control that opens it, it
  // does.
  Drawable *fFollow = nullptr;
  bool fMasking = false; // clip children to these bounds
  float fCornerRadius = 0.0f;
  bool fVisible = true;

  // -- computed by layout()
  skia::SkRect fBounds = skia::SkRect::MakeEmpty();

  // Writes a spec onto this drawable. Anything the spec does not mention is
  // left as the class set it, which is what makes `{}` a no-op and lets a
  // custom node keep the sizing its constructor chose.
  void apply(const Spec &spec) {
    if (spec.place) {
      fAnchor = *spec.place;
      fOrigin = *spec.place;
    }
    if (spec.anchor) {
      fAnchor = *spec.anchor;
    }
    if (spec.origin) {
      fOrigin = *spec.origin;
    }
    if (spec.x) {
      fX = *spec.x;
    }
    if (spec.y) {
      fY = *spec.y;
    }

    if (spec.width) {
      fWidth = *spec.width;
    }
    if (spec.height) {
      fHeight = *spec.height;
    }
    Axes relative = spec.relativeSize.value_or(Axes::kNone);
    if (spec.fill || spec.fillX) {
      relative = axesUnion(relative, Axes::kX);
      fWidth = 1.0f;
    }
    if (spec.fill || spec.fillY) {
      relative = axesUnion(relative, Axes::kY);
      fHeight = 1.0f;
    }
    if (relative != Axes::kNone) {
      fRelativeSizeAxes = relative;
    }
    if (spec.autoSize) {
      fAutoSizeAxes = *spec.autoSize;
    }
    if (spec.grow) {
      fGrowAxes = *spec.grow;
    }
    if (spec.minWidth) {
      fMinWidth = *spec.minWidth;
    }
    if (spec.maxWidth) {
      fMaxWidth = *spec.maxWidth;
    }
    if (spec.minHeight) {
      fMinHeight = *spec.minHeight;
    }
    if (spec.maxHeight) {
      fMaxHeight = *spec.maxHeight;
    }
    if (spec.alignSelf) {
      fAlignSelf = *spec.alignSelf;
    }
    if (spec.depth) {
      fDepth = *spec.depth;
    }

    if (spec.margin.totalX() != 0.0f || spec.margin.totalY() != 0.0f) {
      fMargin = spec.margin;
    }
    if (spec.padding.totalX() != 0.0f || spec.padding.totalY() != 0.0f) {
      fPadding = spec.padding;
    }
    if (spec.cornerRadius) {
      fCornerRadius = *spec.cornerRadius;
    }
    if (spec.masking) {
      fMasking = *spec.masking;
    }
    if (spec.scale) {
      fScale = *spec.scale;
    }
    if (spec.alpha) {
      fAlpha = *spec.alpha;
    }
    if (spec.visible) {
      fVisible = *spec.visible;
    }

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
    // A flow writes a grown child's size, so anything else claiming that axis
    // would be overwritten every frame without saying so.
    if ((hasX(fGrowAxes) && (hasX(fRelativeSizeAxes) || hasX(fAutoSizeAxes))) ||
        (hasY(fGrowAxes) && (hasY(fRelativeSizeAxes) || hasY(fAutoSizeAxes)))) {
      std::println(std::cerr,
                   "[scene] growing and sized another way on the same axis");
      fGrowAxes = Axes::kNone;
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
  template <class T, class... Args> T *add(const Spec &spec, Args &&...args) {
    auto child = std::make_unique<T>(std::forward<Args>(args)...);
    T *raw = child.get();
    raw->apply(spec);
    this->add(std::move(child));
    return raw;
  }

  // Same, for a node that was already built elsewhere -- returns it typed so
  // that keeping a pointer does not need a separate .get() before the move.
  template <class T> T *adopt(std::unique_ptr<T> child) {
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
  void layout(const skia::SkRect &parentBox) {
    // Placed against something other than the parent, when asked. A dropdown
    // list belongs to the control that opened it and has to be drawn over
    // everything below it, so it lives high in the tree and is positioned low
    // in it -- which otherwise means the screen copying coordinates across by
    // hand every frame, and working out the size to copy.
    //
    // The followed drawable has to have been laid out already, which for a
    // sibling means being earlier in the parent's children. That is the same
    // order that puts the follower on top, so the two requirements agree.
    const skia::SkRect parent =
        (fFollow != nullptr && !fFollow->fBounds.isEmpty()) ? fFollow->fBounds
                                                            : parentBox;

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

    width = std::max(width, fMinWidth);
    height = std::max(height, fMinHeight);
    if (fMaxWidth > 0.0f) {
      width = std::min(width, fMaxWidth);
    }
    if (fMaxHeight > 0.0f) {
      height = std::min(height, fMaxHeight);
    }

    width *= fScale;
    height *= fScale;

    const skia::SkRect previous = fBounds;
    fBounds = anchoredBox(parent, width, height, fAnchor, fOrigin,
                          fX + fMargin.fLeft, fY + fMargin.fTop);
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
    return inset(fBounds, fPadding);
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
        canvas->clipRRect(
            skia::SkRRect::MakeRectXY(fBounds, fCornerRadius, fCornerRadius),
            true);
      } else {
        canvas->clipRect(fBounds, true);
      }
    }
    this->drawSelf(canvas, alpha);
    for (Drawable *child : this->inDepthOrder()) {
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
    // Topmost first, which is the reverse of the order they are drawn in.
    const std::vector<Drawable *> order = this->inDepthOrder();
    for (auto it = order.rbegin(); it != order.rend(); ++it) {
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
  [[nodiscard]] float hoverX() const noexcept { return fLastHoverX; }
  [[nodiscard]] float hoverY() const noexcept { return fLastHoverY; }

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
    // Every node remembers where the pointer was, not just the root: a
    // control with parts -- a row of tabs, a bar of icons -- has to know
    // which of its own parts is under it, and asking the screen that owns it
    // was how the screens ended up passing their mouse position down by hand.
    fLastHoverX = x;
    fLastHoverY = y;
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

  // The children in the order they are drawn: the order they were added,
  // unless one of them has been given a depth. Sorting is stable, so an
  // untouched tree keeps exactly the order it was built in and pays nothing
  // for the feature.
  [[nodiscard]] std::vector<Drawable *> inDepthOrder() const {
    std::vector<Drawable *> order;
    order.reserve(fChildren.size());
    bool sorted = true;
    for (const auto &child : fChildren) {
      order.push_back(child.get());
      sorted = sorted && child->fDepth == 0.0f;
    }
    if (!sorted) {
      std::stable_sort(order.begin(), order.end(),
                       [](const Drawable *a, const Drawable *b) {
                         return a->fDepth < b->fDepth;
                       });
    }
    return order;
  }

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
      this->applyProperty(t.fProperty, t.fFrom + (t.fTo - t.fFrom) *
                                                     ease(t.fEasing, progress));
    }
    std::erase_if(fTransforms,
                  [nowMs](const Transform &t) { return nowMs >= t.fEndMs; });
  }

  void applyProperty(Property property, float value) {
    switch (property) {
    case Property::kAlpha:
      fAlpha = value;
      break;
    case Property::kX:
      fX = value;
      break;
    case Property::kY:
      fY = value;
      break;
    case Property::kWidth:
      fWidth = value;
      break;
    case Property::kHeight:
      fHeight = value;
      break;
    case Property::kScale:
      fScale = value;
      break;
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
