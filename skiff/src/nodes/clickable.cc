export module skiff.nodes:clickable;

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

// Anything that reacts to a click. The action is what the screen wants done.
class Clickable : public skiff::scene::TypedDrawable<Clickable> {
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
