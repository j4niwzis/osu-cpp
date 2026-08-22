export module skiff.nodes:scroll;

import std;
import skia;
import skiff.paint;
import skiff.scene;

namespace skiff::nodes {
using skiff::scene::Anchor;
using skiff::scene::Axes;
using skiff::scene::Drawable;
using skiff::scene::Easing;
using skiff::scene::Margin;
using skiff::scene::Spec;
} // namespace skiff::nodes

export namespace skiff::nodes {

// A container that scrolls its children and clips them to itself.
class ScrollContainer : public skiff::scene::TypedDrawable<ScrollContainer> {
public:
  ScrollContainer() { fMasking = true; }

  void scrollToStart() {
    if (fTarget == 0.0f && fCurrent == 0.0f) {
      return;
    }
    fTarget = 0.0f;
    fCurrent = 0.0f;
    this->invalidateLayout();
  }

  // Carried across a rebuild: a list that grew a page should stay where the
  // reader left it, not jump back to the top.
  void setCurrent(float offset) {
    if (fCurrent == offset && fTarget == offset) {
      return;
    }
    fCurrent = offset;
    fTarget = offset;
    this->invalidateLayout();
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
    const skia::SkRect scrolled = skia::SkRect::MakeXYWH(
        box.fLeft, box.fTop - fCurrent, box.width(), box.height());
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

} // namespace skiff::nodes
