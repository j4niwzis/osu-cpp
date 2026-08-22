export module skiff.widgets.button;

import std;
import skia;
import skiff.paint;
import skiff.scene;
export import skiff.widgets.theme;

namespace skiff::widgets {
using skiff::scene::Anchor;
using skiff::scene::Axes;
using skiff::scene::Drawable;
using skiff::scene::Easing;
using skiff::scene::Margin;
using skiff::scene::Spec;
} // namespace skiff::widgets

export namespace skiff::widgets {

// A rounded rectangle with a label in it that calls something when clicked,
// and lightens under the pointer. AdwButton, and the five hand-written ones
// this replaces.
class Button : public skiff::scene::TypedDrawable<Button> {
public:
  Button(std::string label, std::function<void()> action)
      : fLabel(std::move(label)), fAction(std::move(action)) {
    fHeight = fTheme.fRowHeight;
  }

  Theme fTheme = theme();

  void setPrimary(bool primary) {
    if (primary == fPrimary) {
      return;
    }
    fPrimary = primary;
    this->markDamaged();
  }
  [[nodiscard]] bool primary() const noexcept { return fPrimary; }

  void setOutlined(bool outlined) {
    if (outlined == fOutlined) {
      return;
    }
    fOutlined = outlined;
    this->markDamaged();
  }
  [[nodiscard]] bool outlined() const noexcept { return fOutlined; }

  void setAccent(skia::SkColor accent) {
    if (accent == fTheme.fAccent) {
      return;
    }
    fTheme.fAccent = accent;
    this->markDamaged();
  }

  void setEnabled(bool enabled) {
    if (enabled == fEnabled) {
      return;
    }
    fEnabled = enabled;
    this->setDisabled(!enabled);
  }
  [[nodiscard]] bool enabled() const noexcept { return fEnabled; }

  void setLabel(std::string label) {
    if (label == fLabel) {
      return;
    }
    fLabel = std::move(label);
    this->markDamaged();
  }

protected:
  bool acceptsInput() const override { return fEnabled; }
  bool hoverChangesAppearance() const override { return fEnabled; }

  bool onClick(float x, float y) override {
    if (!fEnabled || !fBounds.contains(x, y)) {
      return false;
    }
    if (fAction) {
      fAction();
    }
    return true;
  }

  void drawSelf(skia::SkCanvas *canvas, float alpha) override {
    skia::SkFont *font = skiff::paint::defaultFont();
    if (font == nullptr) {
      return;
    }
    const skiff::paint::Painter p(canvas, *font);
    skia::SkColor fill = fPrimary ? fTheme.fAccent : fTheme.fSurface;
    if (fHovered && fEnabled) {
      fill =
          fPrimary ? skiff::paint::lighten(fill, 0.12f) : fTheme.fSurfaceHover;
    }
    p.fillRounded(fBounds, fTheme.fCorner, fill,
                  alpha * (fEnabled ? 1.0f : 0.5f));
    if (fOutlined && !fPrimary) {
      p.strokeRounded(fBounds, fTheme.fCorner, fTheme.fAccent,
                      fHovered && fEnabled ? 3.0f : 2.0f,
                      alpha * (fEnabled ? 1.0f : 0.5f));
    }
    const skia::SkColor text =
        fPrimary ? fTheme.fOnAccent
                 : (fOutlined && fHovered && fEnabled ? fTheme.fAccent
                                                       : fTheme.fText);
    p.textCentredIn(fBounds, fLabel, fTheme.fFontSize,
                    text,
                    alpha * (fEnabled ? 1.0f : 0.5f), true);
  }

private:
  bool fPrimary = false; // filled in the accent rather than the surface
  bool fOutlined = false;
  bool fEnabled = true;
  std::string fLabel;
  std::function<void()> fAction;
};

} // namespace skiff::widgets
