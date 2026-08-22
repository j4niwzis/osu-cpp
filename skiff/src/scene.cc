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

// How close to its target an eased value has to be to count as arrived.
// Exponential easing approaches without reaching, so without a tolerance a
// node that has visibly stopped goes on asking for frames for ever.
inline constexpr float kSettled = 0.002f;

struct Margin {
  float fTop = 0.0f, fRight = 0.0f, fBottom = 0.0f, fLeft = 0.0f;

  [[nodiscard]] static constexpr Margin all(float v) { return {v, v, v, v}; }
  [[nodiscard]] static constexpr Margin horizontal(float v) {
    return {0, v, 0, v};
  }
  [[nodiscard]] static constexpr Margin vertical(float v) {
    return {v, 0, v, 0};
  }
  [[nodiscard]] constexpr float totalX() const noexcept {
    return fLeft + fRight;
  }
  [[nodiscard]] constexpr float totalY() const noexcept {
    return fTop + fBottom;
  }
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

namespace detail {
struct StyleKey {};
template <class Tag> inline constexpr StyleKey styleRoleKey{};
template <class Node> inline constexpr StyleKey styleNodeKey{};
} // namespace detail

// A role is a type-safe name shared by a node and a selector. Applications
// define empty tag types (`struct PrimaryButton;`) and never coordinate
// string spellings. The inline template object gives each tag one stable key
// across modules without RTTI, registration or hashing.
class StyleRole {
public:
  template <class Tag> [[nodiscard]] static constexpr StyleRole of() {
    return StyleRole(&detail::styleRoleKey<Tag>);
  }

  [[nodiscard]] bool operator==(const StyleRole &) const noexcept = default;

private:
  explicit constexpr StyleRole(const detail::StyleKey *key) : fKey(key) {}
  const detail::StyleKey *fKey;
};

template <class Tag> inline constexpr StyleRole role = StyleRole::of<Tag>();

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

  // Selector roles can be declared with the rest of the node.
  std::vector<StyleRole> roles{};
  std::optional<bool> selected{};
  std::optional<bool> disabled{};
};

// ---- declarative styling -------------------------------------------------

class Drawable;

// State selectors deliberately cover the states a retained UI owns. More
// involved application state remains an ordinary class: for example a
// download row can add/remove "complete" without teaching the scene graph
// what a download is.
enum class StyleState : std::uint8_t {
  kNone = 0,
  kHover = 1 << 0,
  kSelected = 1 << 1,
  kDisabled = 1 << 2,
};

[[nodiscard]] constexpr StyleState operator|(StyleState a,
                                             StyleState b) noexcept {
  return static_cast<StyleState>(static_cast<std::uint8_t>(a) |
                                 static_cast<std::uint8_t>(b));
}

[[nodiscard]] constexpr bool hasState(StyleState states,
                                      StyleState state) noexcept {
  return (static_cast<std::uint8_t>(states) &
          static_cast<std::uint8_t>(state)) != 0;
}

// The declarations understood by every drawable, plus the inheritable visual
// declarations understood by Text and Box. Unlike Spec, margins are optional
// here: a rule must be able to explicitly clear a margin supplied by a less
// specific rule.
struct Style {
  std::optional<Anchor> anchor{}, origin{};
  std::optional<float> x{}, y{};
  std::optional<float> width{}, height{};
  std::optional<Axes> relativeSize{}, autoSize{}, grow{};
  std::optional<float> minWidth{}, maxWidth{}, minHeight{}, maxHeight{};
  std::optional<Align> alignSelf{};
  std::optional<float> depth{};
  std::optional<Margin> margin{}, padding{};
  std::optional<float> cornerRadius{};
  std::optional<bool> masking{};
  std::optional<float> scale{}, alpha{};
  std::optional<bool> visible{};

  // Foreground colour, font size and weight inherit. Background colour does
  // not, matching CSS and preventing a container's text colour from filling
  // every Box below it.
  std::optional<skia::SkColor> colour{};
  std::optional<skia::SkColor> backgroundColour{};
  std::optional<float> fontSize{};
  std::optional<bool> fontBold{};

  // A state change animates properties for which Drawable already has a
  // transform (position, size, scale and alpha). Zero or absent is immediate.
  std::optional<double> transitionMs{};
  std::optional<Easing> transitionEasing{};

  void overlay(const Style &other) {
#define SKIFF_OVERLAY(member)                                                  \
  if (other.member)                                                            \
  member = other.member
    SKIFF_OVERLAY(anchor);
    SKIFF_OVERLAY(origin);
    SKIFF_OVERLAY(x);
    SKIFF_OVERLAY(y);
    SKIFF_OVERLAY(width);
    SKIFF_OVERLAY(height);
    SKIFF_OVERLAY(relativeSize);
    SKIFF_OVERLAY(autoSize);
    SKIFF_OVERLAY(grow);
    SKIFF_OVERLAY(minWidth);
    SKIFF_OVERLAY(maxWidth);
    SKIFF_OVERLAY(minHeight);
    SKIFF_OVERLAY(maxHeight);
    SKIFF_OVERLAY(alignSelf);
    SKIFF_OVERLAY(depth);
    SKIFF_OVERLAY(margin);
    SKIFF_OVERLAY(padding);
    SKIFF_OVERLAY(cornerRadius);
    SKIFF_OVERLAY(masking);
    SKIFF_OVERLAY(scale);
    SKIFF_OVERLAY(alpha);
    SKIFF_OVERLAY(visible);
    SKIFF_OVERLAY(colour);
    SKIFF_OVERLAY(backgroundColour);
    SKIFF_OVERLAY(fontSize);
    SKIFF_OVERLAY(fontBold);
    SKIFF_OVERLAY(transitionMs);
    SKIFF_OVERLAY(transitionEasing);
#undef SKIFF_OVERLAY
  }
};

// Selector identity is entirely in the type. StaticStyleSheet retains each
// instantiation in its rule tuple, so matching a node neither allocates nor
// walks a runtime list of role keys.
struct AnyDrawable {};

template <class Node, class... Roles> class Selector {
public:
  [[nodiscard]] constexpr Selector when(this Selector self,
                                          StyleState state) {
    self.fStates = self.fStates | state;
    return self;
  }
  [[nodiscard]] constexpr Selector atLeastWidth(this Selector self,
                                                 float width) {
    self.fMinViewportWidth = width;
    return self;
  }
  [[nodiscard]] constexpr Selector atMostWidth(this Selector self,
                                                float width) {
    self.fMaxViewportWidth = width;
    return self;
  }

private:
  StyleState fStates = StyleState::kNone;
  std::optional<float> fMinViewportWidth{}, fMaxViewportWidth{};

  template <class, class...> friend struct StyleRule;
};

template <class Node, class... Roles>
[[nodiscard]] constexpr Selector<Node, Roles...> select() {
  return {};
}

template <class... Roles>
[[nodiscard]] constexpr Selector<AnyDrawable, Roles...> selectAny() {
  return {};
}

template <class Node, class... Roles> struct StyleRule {
  Selector<Node, Roles...> fSelector;
  Style fStyle;

  [[nodiscard]] bool matches(const Drawable &node) const;
};

template <class... Rules> class StaticStyleSheet {
public:
  constexpr StaticStyleSheet() requires(sizeof...(Rules) == 0) = default;
  explicit constexpr StaticStyleSheet(std::tuple<Rules...> rules)
      : fRules(std::move(rules)) {}

  template <class Node, class... Roles>
  [[nodiscard]] constexpr auto rule(this StaticStyleSheet self,
                                    Selector<Node, Roles...> selector,
                                    Style style) {
    using Rule = StyleRule<Node, Roles...>;
    Rule rule{selector, style};
    return StaticStyleSheet<Rules..., Rule>{std::tuple_cat(
        std::move(self.fRules), std::tuple<Rule>{std::move(rule)})};
  }

  [[nodiscard]] Style resolve(const Drawable &node) const;

private:
  std::tuple<Rules...> fRules;
};

[[nodiscard]] constexpr StaticStyleSheet<> makeStyleSheet() { return {}; }

// ---- the node ------------------------------------------------------------

class Drawable {
public:
  using SkiffNodeType = Drawable;

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

  // Concrete node types get their key from TypedDrawable<T>. Plain Drawable
  // remains selectable too, which is useful for anonymous containers.
  [[nodiscard]] virtual const detail::StyleKey *
  styleTypeKey() const noexcept {
    return &detail::styleNodeKey<Drawable>;
  }

  void addStyleRole(StyleRole role) {
    if (this->hasStyleRole(role)) {
      return;
    }
    fStyleRoles.push_back(role);
    this->restyleFromHere(true);
  }
  template <class Role> void addStyleRole() {
    this->addStyleRole(StyleRole::of<Role>());
  }
  void removeStyleRole(StyleRole role) {
    const auto old = fStyleRoles.size();
    std::erase(fStyleRoles, role);
    if (fStyleRoles.size() != old) {
      this->restyleFromHere(true);
    }
  }
  template <class Role> void removeStyleRole() {
    this->removeStyleRole(StyleRole::of<Role>());
  }
  [[nodiscard]] bool hasStyleRole(StyleRole role) const noexcept {
    return std::ranges::find(fStyleRoles, role) != fStyleRoles.end();
  }

  void setSelected(bool selected) {
    if (selected == fSelected) {
      return;
    }
    fSelected = selected;
    this->restyleFromHere(true);
    // State is also available directly to custom drawables. A state change
    // therefore changes their picture even when no stylesheet rule happens
    // to mention it.
    this->markDamaged();
  }
  [[nodiscard]] bool selected() const noexcept { return fSelected; }

  void setDisabled(bool disabled) {
    if (disabled == fDisabled) {
      return;
    }
    fDisabled = disabled;
    this->restyleFromHere(true);
    this->markDamaged();
  }
  [[nodiscard]] bool disabled() const noexcept { return fDisabled; }

  [[nodiscard]] float styleViewportWidth() const noexcept {
    const Drawable *root = this;
    while (root->fParent != nullptr) {
      root = root->fParent;
    }
    return !root->fLastParent.isEmpty() ? root->fLastParent.width()
                                        : root->fBounds.width();
  }

  // A theme owns a `static constexpr auto styles = makeStyleSheet()...`.
  // The subtree keeps only this generated resolver; the sheet itself has no
  // runtime storage and cannot dangle.
  template <class Theme> void setStyleSheet() {
    static_assert(requires(const Drawable &node) {
      { Theme::styles.resolve(node) } -> std::same_as<Style>;
    });
    fStyleResolver = &resolveTheme<Theme>;
    this->restyleFromHere(false);
  }
  void clearStyleSheet() {
    if (fStyleResolver == nullptr) {
      return;
    }
    fStyleResolver = nullptr;
    this->restyleFromHere(false);
  }

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
    // Start with the constructor's mode when the spec only adds a fill axis,
    // but honour an explicitly supplied kNone: fixed-size callers need to be
    // able to clear a widget's full-width default.
    Axes relative = spec.relativeSize.value_or(fRelativeSizeAxes);
    if (spec.fill || spec.fillX) {
      relative = axesUnion(relative, Axes::kX);
      fWidth = 1.0f;
    }
    if (spec.fill || spec.fillY) {
      relative = axesUnion(relative, Axes::kY);
      fHeight = 1.0f;
    }
    if (spec.relativeSize || spec.fill || spec.fillX || spec.fillY) {
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

    bool identityChanged = false;
    for (StyleRole role : spec.roles) {
      if (!this->hasStyleRole(role)) {
        fStyleRoles.push_back(role);
        identityChanged = true;
      }
    }
    if (spec.selected && *spec.selected != fSelected) {
      fSelected = *spec.selected;
      identityChanged = true;
    }
    if (spec.disabled && *spec.disabled != fDisabled) {
      fDisabled = *spec.disabled;
      identityChanged = true;
    }
    if (identityChanged) {
      this->restyleFromHere(true);
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
    Drawable *added = fChildren.back().get();
    added->restyleSubtree(this->activeStyleResolver(),
                          fStyleApplied ? &fResolvedStyle : nullptr, false);
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
    const bool hadViewport = !fLastParent.isEmpty();
    const bool viewportChanged = parent != fLastParent;
    fLastParent = parent;
    if (viewportChanged && this->activeStyleResolver() != nullptr) {
      // Width-constrained selectors are media queries. Resolve them before
      // layout so the new declarations participate in this same pass.
      this->restyleFromHere(hadViewport);
    }
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
    if (!fTransforms.empty() || this->settling()) {
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
    if (!fVisible || fAlpha <= 0.001f || fDisabled) {
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
    if (!fVisible || fAlpha <= 0.001f || fDisabled) {
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
    if (!fVisible || fDisabled || !fBounds.contains(x, y)) {
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

  void applyHover(float x, float y, bool ancestorVisible = true) {
    // Every node remembers where the pointer was, not just the root: a
    // control with parts -- a row of tabs, a bar of icons -- has to know
    // which of its own parts is under it, and asking the screen that owns it
    // was how the screens ended up passing their mouse position down by hand.
    fLastHoverX = x;
    fLastHoverY = y;
    const bool visible = ancestorVisible && fVisible;
    const bool hovered = visible && fBounds.contains(x, y);
    if (hovered != fHovered) {
      fHovered = hovered;
      this->restyleFromHere(true);
      // Only where hover is drawn. Every box, flow and container in the tree
      // was marking itself as the pointer crossed it, which is a repaint for
      // a picture that did not change -- and there are a lot more containers
      // than there are things that light up.
      if (this->hoverChangesAppearance()) {
        this->markDamaged();
      }
    }
    const bool childrenVisible =
        visible && (!fMasking || fBounds.contains(x, y));
    for (auto &child : fChildren) {
      child->applyHover(x, y, childrenVisible);
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
  // Whether this drawable is part-way to somewhere and the next frame will
  // differ from this one. A transform answers for itself; a node easing a
  // value by hand -- a hover weight, a knob sliding -- has to say so here,
  // because damage cannot: damage is what changed, and this is the claim that
  // something is still changing.
  //
  // Left unanswered it reads as settled, so a node that does not move never
  // thinks about it.
  virtual bool settling() const { return false; }

  virtual bool acceptsInput() const { return false; }
  // Whether the pointer entering or leaving changes what this draws. Taking
  // input and drawing hover are separate capabilities: click-only surfaces,
  // sliders and toggles should not damage a frame merely because the pointer
  // crossed their bounds. Style-driven hover marks damage when the resolved
  // properties actually change; a custom-painted hover opts in here.
  virtual bool hoverChangesAppearance() const { return false; }
  virtual bool onClick(float, float) { return false; }
  virtual bool onScroll(float) { return false; }

  // Node-specific declarations. Box and Text use this for colour, and Text
  // for inherited font properties. `active` becoming false means restore the
  // constructor/setter values captured on the first styled application.
  virtual void applyNodeStyle(const Style &, bool) {}

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
  struct CommonStyleValues {
    Anchor fAnchor = Anchor::kTopLeft;
    Anchor fOrigin = Anchor::kTopLeft;
    float fX = 0.0f, fY = 0.0f;
    float fWidth = 0.0f, fHeight = 0.0f;
    Axes fRelativeSize = Axes::kNone;
    Axes fAutoSize = Axes::kNone;
    Axes fGrow = Axes::kNone;
    float fMinWidth = 0.0f, fMaxWidth = 0.0f;
    float fMinHeight = 0.0f, fMaxHeight = 0.0f;
    std::optional<Align> fAlignSelf{};
    float fDepth = 0.0f;
    Margin fMargin{}, fPadding{};
    float fCornerRadius = 0.0f;
    bool fMasking = false;
    float fScale = 1.0f, fAlpha = 1.0f;
    bool fVisible = true;
  };

  [[nodiscard]] CommonStyleValues commonStyleValues() const {
    return {fAnchor,
            fOrigin,
            fX,
            fY,
            fWidth,
            fHeight,
            fRelativeSizeAxes,
            fAutoSizeAxes,
            fGrowAxes,
            fMinWidth,
            fMaxWidth,
            fMinHeight,
            fMaxHeight,
            fAlignSelf,
            fDepth,
            fMargin,
            fPadding,
            fCornerRadius,
            fMasking,
            fScale,
            fAlpha,
            fVisible};
  }

  [[nodiscard]] static bool sameMargin(const Margin &a,
                                       const Margin &b) noexcept {
    return a.fTop == b.fTop && a.fRight == b.fRight &&
           a.fBottom == b.fBottom && a.fLeft == b.fLeft;
  }

  [[nodiscard]] static bool sameCommon(const CommonStyleValues &a,
                                       const CommonStyleValues &b) noexcept {
    return a.fAnchor == b.fAnchor && a.fOrigin == b.fOrigin && a.fX == b.fX &&
           a.fY == b.fY && a.fWidth == b.fWidth && a.fHeight == b.fHeight &&
           a.fRelativeSize == b.fRelativeSize &&
           a.fAutoSize == b.fAutoSize && a.fGrow == b.fGrow &&
           a.fMinWidth == b.fMinWidth && a.fMaxWidth == b.fMaxWidth &&
           a.fMinHeight == b.fMinHeight && a.fMaxHeight == b.fMaxHeight &&
           a.fAlignSelf == b.fAlignSelf && a.fDepth == b.fDepth &&
           sameMargin(a.fMargin, b.fMargin) &&
           sameMargin(a.fPadding, b.fPadding) &&
           a.fCornerRadius == b.fCornerRadius && a.fMasking == b.fMasking &&
           a.fScale == b.fScale && a.fAlpha == b.fAlpha &&
           a.fVisible == b.fVisible;
  }

  void setStyledProperty(Property property, float target,
                         float previousTarget, double durationMs, Easing easing,
                         bool animate) {
    if (target == previousTarget) {
      return;
    }
    if (animate && durationMs > 0.0) {
      float from = 0.0f;
      switch (property) {
      case Property::kAlpha:
        from = fAlpha;
        break;
      case Property::kX:
        from = fX;
        break;
      case Property::kY:
        from = fY;
        break;
      case Property::kWidth:
        from = fWidth;
        break;
      case Property::kHeight:
        from = fHeight;
        break;
      case Property::kScale:
        from = fScale;
        break;
      }
      this->transformTo(property, from, target, durationMs, easing);
    } else {
      this->transformTo(property, target, target, 0.0, easing);
    }
  }

  void applyCommonStyle(const Style &style, bool active, bool animate) {
    if (!active && !fStyleApplied) {
      return;
    }
    if (!fStyleApplied) {
      fStyleBase = this->commonStyleValues();
      fStyledTarget = fStyleBase;
    }

    // A sheet owns only the properties it declares. Starting from the base
    // here used to reset unrelated run-time state whenever hover caused a
    // restyle: a dialog whose stylesheet says nothing about y would jump
    // back to its pre-animation y, and a measured popup could become visible
    // again. A declaration which stops matching is restored to its base; a
    // property which was never declared is left exactly where its owner put
    // it.
    const CommonStyleValues current = this->commonStyleValues();

    // Keep the restoration value current while the application owns a
    // property. If a newly-added role starts styling a position after the
    // application moved it, removing that role must reveal the new position,
    // not the value from when the sheet was first installed.
#define SKIFF_REFRESH_BASE(declaration, member)                               \
  if (!fResolvedStyle.declaration) {                                         \
    fStyleBase.member = current.member;                                      \
  }
    SKIFF_REFRESH_BASE(anchor, fAnchor);
    SKIFF_REFRESH_BASE(origin, fOrigin);
    SKIFF_REFRESH_BASE(x, fX);
    SKIFF_REFRESH_BASE(y, fY);
    SKIFF_REFRESH_BASE(width, fWidth);
    SKIFF_REFRESH_BASE(height, fHeight);
    SKIFF_REFRESH_BASE(relativeSize, fRelativeSize);
    SKIFF_REFRESH_BASE(autoSize, fAutoSize);
    SKIFF_REFRESH_BASE(grow, fGrow);
    SKIFF_REFRESH_BASE(minWidth, fMinWidth);
    SKIFF_REFRESH_BASE(maxWidth, fMaxWidth);
    SKIFF_REFRESH_BASE(minHeight, fMinHeight);
    SKIFF_REFRESH_BASE(maxHeight, fMaxHeight);
    SKIFF_REFRESH_BASE(alignSelf, fAlignSelf);
    SKIFF_REFRESH_BASE(depth, fDepth);
    SKIFF_REFRESH_BASE(margin, fMargin);
    SKIFF_REFRESH_BASE(padding, fPadding);
    SKIFF_REFRESH_BASE(cornerRadius, fCornerRadius);
    SKIFF_REFRESH_BASE(masking, fMasking);
    SKIFF_REFRESH_BASE(scale, fScale);
    SKIFF_REFRESH_BASE(alpha, fAlpha);
    SKIFF_REFRESH_BASE(visible, fVisible);
#undef SKIFF_REFRESH_BASE

    CommonStyleValues target = current;
#define SKIFF_STYLE_TARGET(declaration, member)                               \
  if (style.declaration) {                                                    \
    target.member = *style.declaration;                                      \
  } else if (fResolvedStyle.declaration) {                                   \
    target.member = fStyleBase.member;                                       \
  }
    SKIFF_STYLE_TARGET(anchor, fAnchor);
    SKIFF_STYLE_TARGET(origin, fOrigin);
    SKIFF_STYLE_TARGET(x, fX);
    SKIFF_STYLE_TARGET(y, fY);
    SKIFF_STYLE_TARGET(width, fWidth);
    SKIFF_STYLE_TARGET(height, fHeight);
    SKIFF_STYLE_TARGET(relativeSize, fRelativeSize);
    SKIFF_STYLE_TARGET(autoSize, fAutoSize);
    SKIFF_STYLE_TARGET(grow, fGrow);
    SKIFF_STYLE_TARGET(minWidth, fMinWidth);
    SKIFF_STYLE_TARGET(maxWidth, fMaxWidth);
    SKIFF_STYLE_TARGET(minHeight, fMinHeight);
    SKIFF_STYLE_TARGET(maxHeight, fMaxHeight);
    SKIFF_STYLE_TARGET(alignSelf, fAlignSelf);
    SKIFF_STYLE_TARGET(depth, fDepth);
    SKIFF_STYLE_TARGET(margin, fMargin);
    SKIFF_STYLE_TARGET(padding, fPadding);
    SKIFF_STYLE_TARGET(cornerRadius, fCornerRadius);
    SKIFF_STYLE_TARGET(masking, fMasking);
    SKIFF_STYLE_TARGET(scale, fScale);
    SKIFF_STYLE_TARGET(alpha, fAlpha);
    SKIFF_STYLE_TARGET(visible, fVisible);
#undef SKIFF_STYLE_TARGET

    const bool changed = !sameCommon(target, current);
    const double duration = style.transitionMs.value_or(0.0);
    const Easing easing = style.transitionEasing.value_or(Easing::kOutQuint);

#define SKIFF_STYLE_DIRECT(declaration, targetMember, liveMember)             \
  if (style.declaration || fResolvedStyle.declaration) {                      \
    liveMember = target.targetMember;                                         \
  }
    SKIFF_STYLE_DIRECT(anchor, fAnchor, fAnchor);
    SKIFF_STYLE_DIRECT(origin, fOrigin, fOrigin);
    SKIFF_STYLE_DIRECT(relativeSize, fRelativeSize, fRelativeSizeAxes);
    SKIFF_STYLE_DIRECT(autoSize, fAutoSize, fAutoSizeAxes);
    SKIFF_STYLE_DIRECT(grow, fGrow, fGrowAxes);
    SKIFF_STYLE_DIRECT(minWidth, fMinWidth, fMinWidth);
    SKIFF_STYLE_DIRECT(maxWidth, fMaxWidth, fMaxWidth);
    SKIFF_STYLE_DIRECT(minHeight, fMinHeight, fMinHeight);
    SKIFF_STYLE_DIRECT(maxHeight, fMaxHeight, fMaxHeight);
    SKIFF_STYLE_DIRECT(alignSelf, fAlignSelf, fAlignSelf);
    SKIFF_STYLE_DIRECT(depth, fDepth, fDepth);
    SKIFF_STYLE_DIRECT(margin, fMargin, fMargin);
    SKIFF_STYLE_DIRECT(padding, fPadding, fPadding);
    SKIFF_STYLE_DIRECT(cornerRadius, fCornerRadius, fCornerRadius);
    SKIFF_STYLE_DIRECT(masking, fMasking, fMasking);
    SKIFF_STYLE_DIRECT(visible, fVisible, fVisible);
#undef SKIFF_STYLE_DIRECT

#define SKIFF_STYLE_ANIMATED(declaration, member, property)                   \
  if (style.declaration || fResolvedStyle.declaration) {                      \
    const float previous = fResolvedStyle.declaration                        \
                               ? fStyledTarget.member                         \
                               : current.member;                              \
    this->setStyledProperty(property, target.member, previous, duration,      \
                            easing, animate);                                 \
  }
    SKIFF_STYLE_ANIMATED(x, fX, Property::kX);
    SKIFF_STYLE_ANIMATED(y, fY, Property::kY);
    SKIFF_STYLE_ANIMATED(width, fWidth, Property::kWidth);
    SKIFF_STYLE_ANIMATED(height, fHeight, Property::kHeight);
    SKIFF_STYLE_ANIMATED(scale, fScale, Property::kScale);
    SKIFF_STYLE_ANIMATED(alpha, fAlpha, Property::kAlpha);
#undef SKIFF_STYLE_ANIMATED

    fStyledTarget = target;
    if (changed) {
      this->invalidateLayout();
    }
  }

  using StyleResolver = Style (*)(const Drawable &);

  template <class Theme>
  [[nodiscard]] static Style resolveTheme(const Drawable &node) {
    return Theme::styles.resolve(node);
  }

  [[nodiscard]] StyleResolver activeStyleResolver() const {
    for (const Drawable *node = this; node != nullptr; node = node->fParent) {
      if (node->fStyleResolver != nullptr) {
        return node->fStyleResolver;
      }
    }
    return nullptr;
  }

  void restyleFromHere(bool animate) {
    const StyleResolver resolver =
        fParent != nullptr ? fParent->activeStyleResolver() : nullptr;
    const Style *inherited =
        fParent != nullptr && fParent->fStyleApplied
            ? &fParent->fResolvedStyle
            : nullptr;
    this->restyleSubtree(resolver, inherited, animate);
  }

  void restyleSubtree(StyleResolver inheritedResolver,
                      const Style *inheritedStyle, bool animate) {
    const StyleResolver resolver =
        fStyleResolver != nullptr ? fStyleResolver : inheritedResolver;
    const bool active = resolver != nullptr;
    Style resolved = active ? resolver(*this) : Style{};
    if (inheritedStyle != nullptr) {
      if (!resolved.colour)
        resolved.colour = inheritedStyle->colour;
      if (!resolved.fontSize)
        resolved.fontSize = inheritedStyle->fontSize;
      if (!resolved.fontBold)
        resolved.fontBold = inheritedStyle->fontBold;
    }

    this->applyCommonStyle(resolved, active, animate);
    this->applyNodeStyle(resolved, active);
    fResolvedStyle = resolved;
    fStyleApplied = active;
    for (auto &child : fChildren) {
      child->restyleSubtree(resolver,
                            active ? &fResolvedStyle : inheritedStyle, animate);
    }
  }

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
  StyleResolver fStyleResolver = nullptr;
  std::vector<StyleRole> fStyleRoles;
  bool fSelected = false;
  bool fDisabled = false;
  bool fStyleApplied = false;
  Style fResolvedStyle;
  CommonStyleValues fStyleBase;
  CommonStyleValues fStyledTarget;

public:
  // Damage bookkeeping is reached through the parent chain, so these are not
  // private: a child hands its rectangle to the root it belongs to.
  skia::SkRect fDrawnBounds = skia::SkRect::MakeEmpty();
  skia::SkRect fDamageAccum = skia::SkRect::MakeEmpty(); // meaningful at roots
  Drawable *fParent = nullptr;
};

// Supplies a concrete node key without RTTI. The scene still owns drawables
// through Drawable, so an explicit-object member cannot recover Derived at
// selector-matching time; this one-method CRTP bridge retains it. Base is
// configurable so an opinionated widget can remain a FillFlow (or any other
// existing drawable) while receiving its own selector identity.
template <class Derived, class Base = Drawable>
  requires std::derived_from<Base, Drawable>
class TypedDrawable : public Base {
public:
  using Base::Base;
  using SkiffNodeType = Derived;

  [[nodiscard]] const detail::StyleKey *
  styleTypeKey() const noexcept override {
    return &detail::styleNodeKey<Derived>;
  }
};

template <class Node, class... Roles>
bool StyleRule<Node, Roles...>::matches(const Drawable &node) const {
  if constexpr (!std::same_as<Node, AnyDrawable>) {
    static_assert(std::derived_from<Node, Drawable>,
                  "a style selector must name a Drawable type");
    static_assert(
        std::same_as<typename Node::SkiffNodeType, Node>,
        "a selectable custom node must derive from TypedDrawable<T, Base>");
    if (node.styleTypeKey() != &detail::styleNodeKey<Node>) {
      return false;
    }
  }
  if (!(node.hasStyleRole(role<Roles>) && ...)) {
    return false;
  }
  const float viewportWidth = node.styleViewportWidth();
  return (!fSelector.fMinViewportWidth ||
          viewportWidth >= *fSelector.fMinViewportWidth) &&
         (!fSelector.fMaxViewportWidth ||
          viewportWidth <= *fSelector.fMaxViewportWidth) &&
         (!hasState(fSelector.fStates, StyleState::kHover) ||
          node.hovered()) &&
         (!hasState(fSelector.fStates, StyleState::kSelected) ||
          node.selected()) &&
         (!hasState(fSelector.fStates, StyleState::kDisabled) ||
          node.disabled());
}

template <class... Rules>
Style StaticStyleSheet<Rules...>::resolve(const Drawable &node) const {
  Style out;
  std::apply(
      [&]<class... SheetRules>(const SheetRules &...rules) {
        ([&] {
          if (rules.matches(node)) {
            // Source order is the cascade: a later matching rule wins.
            out.overlay(rules.fStyle);
          }
        }(),
         ...);
      },
      fRules);
  return out;
}

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
