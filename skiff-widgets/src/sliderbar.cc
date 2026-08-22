export module skiff.widgets.sliderbar;

import std;
import skia;
import skiff.paint;
import skiff.scene;
export import skiff.widgets.theme;

namespace skiff::widgets {
using skiff::scene::Anchor;
using skiff::scene::Axes;
using skiff::scene::Drawable;
using skiff::scene::Margin;
using skiff::scene::Spec;
} // namespace skiff::widgets

export namespace skiff::widgets {

// A track with a filled part and a knob on the join. The value is a fraction
// of the width, so the bar never sees the units it stands for.
//
// Dragging is not handled here: a drag starts on this and continues wherever
// the pointer goes, which is the screen's business. fractionAt turns a
// pointer position into a value using the bar's own bounds, which is the part
// the screen would otherwise work out from a rectangle it kept a copy of.
class SliderBar : public skiff::scene::TypedDrawable<SliderBar> {
public:
  SliderBar() {
    fRelativeSizeAxes = Axes::kX;
    fWidth = 1.0f;
    fHeight = 6.0f;
  }

  std::function<void(float)> fOnSet;

  void setTheme(Theme value) {
    fTheme = std::move(value);
    this->markDamaged();
  }

  void setFraction(float fraction) {
    const float clamped = std::clamp(fraction, 0.0f, 1.0f);
    if (clamped == fFraction) {
      return;
    }
    fFraction = clamped;
    this->markDamaged();
  }
  [[nodiscard]] float fraction() const noexcept { return fFraction; }

  // Where along the track a pointer at x sits, as a fraction.
  [[nodiscard]] float fractionAt(float x) const {
    if (fBounds.width() <= 0.0f) {
      return fFraction;
    }
    return std::clamp((x - fBounds.fLeft) / fBounds.width(), 0.0f, 1.0f);
  }

protected:
  Theme fTheme = theme();
  float fKnobRadius = 7.0f;
  float fTrackRadius = 3.0f;

  // The knob stands proud of the track, so the box that takes a click is
  // taller than the box that is drawn.
  [[nodiscard]] skia::SkRect reach() const {
    return skia::SkRect::MakeLTRB(
        fBounds.fLeft, fBounds.centerY() - fKnobRadius, fBounds.fRight,
        fBounds.centerY() + fKnobRadius);
  }

  void drawSelf(skia::SkCanvas *canvas, float alpha) override {
    skia::SkFont *font = skiff::paint::defaultFont();
    if (font == nullptr) {
      return;
    }
    const skiff::paint::Painter p(canvas, *font);
    p.fillRounded(fBounds, fTrackRadius, fTheme.fSurface, alpha);
    p.fillRounded(skia::SkRect::MakeXYWH(fBounds.fLeft, fBounds.fTop,
                                         fBounds.width() * fFraction,
                                         fBounds.height()),
                  fTrackRadius, fTheme.fAccent, alpha);
    p.circle(fBounds.fLeft + fBounds.width() * fFraction, fBounds.centerY(),
             fKnobRadius, fTheme.fText, alpha);
    if (this->focused()) {
      p.strokeRounded(this->reach(), fKnobRadius, fTheme.fAccent, 1.5f,
                      alpha);
    }
  }

  // Without a setter it is a picture of a value and clicks fall through to
  // whatever is behind it.
  bool acceptsInput() const override { return static_cast<bool>(fOnSet); }
  bool focusChangesAppearance() const override {
    return static_cast<bool>(fOnSet);
  }

  void onPointerEvent(skiff::scene::PointerEvent &event) override {
    if (event.fPhase != skiff::scene::EventPhase::kTarget || !fOnSet) {
      return;
    }
    if (event.fAction == skiff::scene::PointerAction::kDown &&
        this->reach().contains(event.fX, event.fY)) {
      fDragging = true;
      fOnSet(this->fractionAt(event.fX));
      event.capturePointer();
      event.requestFocus();
      event.handle();
    } else if (event.fAction == skiff::scene::PointerAction::kMove &&
               fDragging) {
      fOnSet(this->fractionAt(event.fX));
      event.handle();
    } else if ((event.fAction == skiff::scene::PointerAction::kUp ||
                event.fAction == skiff::scene::PointerAction::kCancel) &&
               fDragging) {
      fDragging = false;
      event.releasePointer();
      event.handle();
    }
  }

  [[nodiscard]] skiff::scene::Semantics semantics() const override {
    skiff::scene::Semantics out;
    out.fRole = skiff::scene::SemanticRole::kSlider;
    out.fValue = std::format("{:.3f}", fFraction);
    out.fActions = {skiff::scene::SemanticAction::kFocus,
                    skiff::scene::SemanticAction::kIncrement,
                    skiff::scene::SemanticAction::kDecrement,
                    skiff::scene::SemanticAction::kSetValue};
    return out;
  }

  void onSemanticAction(skiff::scene::SemanticActionEvent &event) override {
    if (event.fAction == skiff::scene::SemanticAction::kIncrement && fOnSet) {
      fOnSet(std::min(1.0f, fFraction + 0.05f));
      event.handle();
    } else if (event.fAction == skiff::scene::SemanticAction::kDecrement &&
               fOnSet) {
      fOnSet(std::max(0.0f, fFraction - 0.05f));
      event.handle();
    } else if (event.fAction == skiff::scene::SemanticAction::kSetValue &&
               fOnSet) {
      fOnSet(std::clamp(event.fValue, 0.0f, 1.0f));
      event.handle();
    } else {
      Drawable::onSemanticAction(event);
    }
  }

  bool onClick(float x, float y) override {
    if (!fOnSet || !this->reach().contains(x, y)) {
      return false;
    }
    if (fOnSet) {
      fOnSet(this->fractionAt(x));
    }
    return true;
  }

private:
  float fFraction = 0.0f;
  bool fDragging = false;
};

// A track with two independently draggable ends. Values stay normalised so
// the widget can represent difficulty, price, time or any other range without
// knowing its units. It owns handle selection and pointer-to-track mapping;
// the screen only forwards a continuing drag after the initial click.
class RangeSlider : public skiff::scene::TypedDrawable<RangeSlider> {
public:
  RangeSlider() {
    fRelativeSizeAxes = Axes::kX;
    fWidth = 1.0f;
    // The track is six pixels high, but a seven-pixel-radius knob is the
    // actual interaction target. Scene hit testing uses the drawable bounds,
    // so the bounds describe the whole control rather than only its track.
    fHeight = 14.0f;
  }

  std::function<void(float, float)> fOnSet;

  void setTheme(Theme value) {
    fTheme = std::move(value);
    this->markDamaged();
  }
  void setMinimumSpan(float span) {
    span = std::clamp(span, 0.0f, 1.0f);
    if (span != fMinSpan) {
      fMinSpan = span;
      this->setRange(fLow, fHigh);
    }
  }

  void setRange(float low, float high) {
    low = std::clamp(low, 0.0f, 1.0f);
    high = std::clamp(high, 0.0f, 1.0f);
    if (low > high) {
      std::swap(low, high);
    }
    const float span = std::clamp(fMinSpan, 0.0f, 1.0f);
    if (high - low < span) {
      high = std::min(1.0f, low + span);
      low = std::max(0.0f, high - span);
    }
    if (low == fLow && high == fHigh) {
      return;
    }
    fLow = low;
    fHigh = high;
    this->markDamaged();
  }

  [[nodiscard]] float low() const noexcept { return fLow; }
  [[nodiscard]] float high() const noexcept { return fHigh; }
  [[nodiscard]] bool dragging() const noexcept { return fDragging >= 0; }

  [[nodiscard]] float fractionAt(float x) const {
    if (fBounds.width() <= 0.0f) {
      return fDragging == 0 ? fLow : fHigh;
    }
    return std::clamp((x - fBounds.fLeft) / fBounds.width(), 0.0f, 1.0f);
  }

  void dragTo(float x) {
    if (fDragging < 0) {
      return;
    }
    const float at = this->fractionAt(x);
    const float span = std::clamp(fMinSpan, 0.0f, 1.0f);
    if (fDragging == 0) {
      this->change(std::min(at, fHigh - span), fHigh);
    } else {
      this->change(fLow, std::max(at, fLow + span));
    }
  }

  void endDrag() noexcept { fDragging = -1; }

protected:
  Theme fTheme = theme();
  float fKnobRadius = 7.0f;
  float fTrackHeight = 6.0f;
  float fTrackRadius = 3.0f;
  float fMinSpan = 0.0f;

  void drawSelf(skia::SkCanvas *canvas, float alpha) override {
    skia::SkFont *font = skiff::paint::defaultFont();
    if (font == nullptr) {
      return;
    }
    const skiff::paint::Painter p(canvas, *font);
    const float trackHeight = std::min(fTrackHeight, fBounds.height());
    const skia::SkRect track = skia::SkRect::MakeXYWH(
        fBounds.fLeft, fBounds.centerY() - trackHeight * 0.5f,
        fBounds.width(), trackHeight);
    p.fillRounded(track, fTrackRadius, fTheme.fSurface, alpha);
    p.fillRounded(
        skia::SkRect::MakeLTRB(track.fLeft + track.width() * fLow,
                               track.fTop,
                               track.fLeft + track.width() * fHigh,
                               track.fBottom),
        fTrackRadius, fTheme.fAccent, alpha);
    p.circle(track.fLeft + track.width() * fLow, track.centerY(),
             fKnobRadius, fTheme.fText, alpha);
    p.circle(track.fLeft + track.width() * fHigh, track.centerY(),
             fKnobRadius, fTheme.fText, alpha);
    if (this->focused()) {
      p.strokeRounded(fBounds, fKnobRadius, fTheme.fAccent, 1.5f, alpha);
    }
  }

  bool acceptsInput() const override { return static_cast<bool>(fOnSet); }
  bool focusChangesAppearance() const override {
    return static_cast<bool>(fOnSet);
  }

  void onPointerEvent(skiff::scene::PointerEvent &event) override {
    if (event.fPhase != skiff::scene::EventPhase::kTarget || !fOnSet) {
      return;
    }
    if (event.fAction == skiff::scene::PointerAction::kDown &&
        fBounds.contains(event.fX, event.fY)) {
      this->onClick(event.fX, event.fY);
      event.capturePointer();
      event.requestFocus();
      event.handle();
    } else if (event.fAction == skiff::scene::PointerAction::kMove &&
               this->dragging()) {
      this->dragTo(event.fX);
      event.handle();
    } else if ((event.fAction == skiff::scene::PointerAction::kUp ||
                event.fAction == skiff::scene::PointerAction::kCancel) &&
               this->dragging()) {
      this->endDrag();
      event.releasePointer();
      event.handle();
    }
  }

  [[nodiscard]] skiff::scene::Semantics semantics() const override {
    skiff::scene::Semantics out;
    out.fRole = skiff::scene::SemanticRole::kSlider;
    out.fValue = std::format("{:.3f}–{:.3f}", fLow, fHigh);
    out.fActions = {skiff::scene::SemanticAction::kFocus};
    return out;
  }

  bool onClick(float x, float) override {
    if (!fOnSet) {
      return false;
    }
    const float at = this->fractionAt(x);
    fDragging = std::abs(at - fLow) <= std::abs(at - fHigh) ? 0 : 1;
    this->dragTo(x);
    return true;
  }

private:
  void change(float low, float high) {
    low = std::clamp(low, 0.0f, 1.0f);
    high = std::clamp(high, 0.0f, 1.0f);
    if (low == fLow && high == fHigh) {
      return;
    }
    fLow = low;
    fHigh = high;
    this->markDamaged();
    if (fOnSet) {
      fOnSet(fLow, fHigh);
    }
  }

  float fLow = 0.0f;
  float fHigh = 1.0f;
  int fDragging = -1;
};

// A pill that slides its knob from one end to the other. The state is set
// from outside; what is animated here is only the knob catching up with it.
class Toggle : public skiff::scene::TypedDrawable<Toggle> {
public:
  Toggle() {
    fWidth = 40.0f;
    fHeight = 22.0f;
  }

  std::function<void()> fOnToggle;

  void setTheme(Theme value) {
    fTheme = std::move(value);
    this->markDamaged();
  }

  void setOn(bool on) {
    if (on == fOn) {
      return;
    }
    fOn = on;
    this->markDamaged();
  }
  [[nodiscard]] bool on() const noexcept { return fOn; }

protected:
  Theme fTheme = theme();
  float fKnobRadius = 8.0f;
  float fKnobInset = 11.0f;
  float fTauMs = 60.0f;

  bool settling() const override {
    return std::abs(fKnob - (fOn ? 1.0f : 0.0f)) > skiff::scene::kSettled;
  }

  void update(double nowMs) override {
    const double dt = fLastMs > 0.0 ? nowMs - fLastMs : 16.0;
    fLastMs = nowMs;
    const float previous = fKnob;
    fKnob = skiff::paint::approach(fKnob, fOn ? 1.0f : 0.0f, fTauMs, dt);
    if (fKnob != previous) {
      this->markDamaged();
    }
  }

  void drawSelf(skia::SkCanvas *canvas, float alpha) override {
    skia::SkFont *font = skiff::paint::defaultFont();
    if (font == nullptr) {
      return;
    }
    const skiff::paint::Painter p(canvas, *font);
    p.fillRounded(fBounds, fBounds.height() * 0.5f,
                  mix(fTheme.fSurface, fTheme.fAccent, fKnob), alpha);
    p.circle(fBounds.fLeft + fKnobInset +
                 (fBounds.width() - fKnobInset * 2.0f) * fKnob,
             fBounds.centerY(), fKnobRadius, fTheme.fText, alpha);
    if (this->focused()) {
      p.strokeRounded(fBounds, fBounds.height() * 0.5f, fTheme.fAccent, 1.5f,
                      alpha);
    }
  }

  bool acceptsInput() const override { return static_cast<bool>(fOnToggle); }
  bool focusChangesAppearance() const override {
    return static_cast<bool>(fOnToggle);
  }

  [[nodiscard]] skiff::scene::Semantics semantics() const override {
    skiff::scene::Semantics out;
    out.fRole = skiff::scene::SemanticRole::kToggle;
    out.fValue = fOn ? "on" : "off";
    out.fActions = {skiff::scene::SemanticAction::kFocus,
                    skiff::scene::SemanticAction::kActivate};
    return out;
  }

  bool onClick(float, float) override {
    if (!fOnToggle) {
      return false;
    }
    fOnToggle();
    return true;
  }

private:
  [[nodiscard]] static skia::SkColor mix(skia::SkColor a, skia::SkColor b,
                                         float t) {
    const auto channel = [t](std::uint32_t from, std::uint32_t to) {
      return static_cast<std::uint8_t>(
          static_cast<float>(from) +
          (static_cast<float>(to) - static_cast<float>(from)) * t);
    };
    return skia::colorSetARGB(channel((a >> 24) & 0xffu, (b >> 24) & 0xffu),
                              channel((a >> 16) & 0xffu, (b >> 16) & 0xffu),
                              channel((a >> 8) & 0xffu, (b >> 8) & 0xffu),
                              channel(a & 0xffu, b & 0xffu));
  }

  bool fOn = false;
  float fKnob = 0.0f;
  double fLastMs = 0.0;
};

} // namespace skiff::widgets
