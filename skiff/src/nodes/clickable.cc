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
  explicit Clickable(std::function<void()> action, std::string label = {})
      : fAction(std::move(action)), fLabel(std::move(label)) {}

protected:
  bool acceptsInput() const override { return true; }
  [[nodiscard]] skiff::scene::Semantics semantics() const override {
    skiff::scene::Semantics out;
    out.fRole = skiff::scene::SemanticRole::kButton;
    out.fLabel = fLabel;
    out.fActions = {skiff::scene::SemanticAction::kFocus,
                    skiff::scene::SemanticAction::kActivate};
    return out;
  }
  bool onClick(float, float) override {
    if (fAction) {
      fAction();
    }
    return true;
  }

private:
  std::function<void()> fAction;
  std::string fLabel;
};

} // namespace skiff::nodes
