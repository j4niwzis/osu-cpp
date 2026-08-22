export module skiff.widgets.textbox;

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

// A single line of editable text: OsuTextBox, AdwEntryRow. It owns the string
// and reports changes. Text and composition events arrive through the scene's
// focus router, so screens do not need a parallel keyboard implementation.
class TextBox : public skiff::scene::TypedDrawable<TextBox> {
public:
  explicit TextBox(std::string placeholder = {})
      : fPlaceholder(std::move(placeholder)) {
    fRelativeSizeAxes = Axes::kX;
    fWidth = 1.0f;
    fHeight = fTheme.fRowHeight;
  }

  std::function<void(std::string_view)> fOnChanged;

  void setTheme(Theme value) {
    fTheme = std::move(value);
    this->markDamaged();
  }
  void setSearchIcon(bool enabled) {
    if (enabled != fSearchIcon) {
      fSearchIcon = enabled;
      this->markDamaged();
    }
  }
  void setTrailingInset(float inset) {
    if (inset != fTrailingInset) {
      fTrailingInset = inset;
      this->markDamaged();
    }
  }

  void setText(std::string text) {
    if (text == fText) {
      return;
    }
    fText = std::move(text);
    fCaret = fText.size();
    this->markDamaged();
  }
  [[nodiscard]] const std::string &text() const noexcept { return fText; }

  // The caret is usually the only thing on a screen that changes without
  // being touched, so it marks the box when it flips and nothing else: the
  // frame after a flip repaints one rectangle, and the frames between are
  // not drawn at all. Off screen it does not blink, because a caret nobody
  // can see is not worth a frame.
  void tickCaret(double nowMs, bool visible) {
    const bool shown = visible && std::fmod(nowMs, 1000.0) < 600.0;
    if (shown != fCaretShown) {
      fCaretShown = shown;
      this->markDamaged();
    }
  }

protected:
  Theme fTheme = theme();
  std::string fPlaceholder;
  bool fSearchIcon = false; // the magnifier lazer puts in its search boxes
  // Space owned by a trailing status, clear button or other overlay.
  float fTrailingInset = 0.0f;

  bool acceptsInput() const override { return true; }
  bool focusChangesAppearance() const override { return true; }

  bool onClick(float x, float y) override {
    return fBounds.contains(x, y);
  }

  void onTextInput(skiff::scene::TextInputEvent &event) override {
    if (event.fPhase != skiff::scene::EventPhase::kTarget) {
      return;
    }
    if (event.fCommit) {
      if (!event.fText.empty()) {
        fText.insert(fCaret, event.fText);
        fCaret += event.fText.size();
        if (fOnChanged) {
          fOnChanged(fText);
        }
      }
      fComposition.clear();
    } else {
      fComposition = event.fComposition.empty() ? std::string(event.fText)
                                                : std::string(event.fComposition);
      fCompositionSelectionStart = event.fSelectionStart;
      fCompositionSelectionLength = event.fSelectionLength;
    }
    this->markDamaged();
    event.handle();
  }

  void onKeyEvent(skiff::scene::KeyEvent &event) override {
    if (event.fPhase != skiff::scene::EventPhase::kTarget ||
        !event.fPressed) {
      return;
    }
    if (event.fKey == skiff::scene::Key::kBackspace) {
      if (fCaret > 0) {
        const std::size_t eraseFrom = previousCodepoint(fText, fCaret);
        fText.erase(eraseFrom, fCaret - eraseFrom);
        fCaret = eraseFrom;
        if (fOnChanged) {
          fOnChanged(fText);
        }
        this->markDamaged();
      }
      event.handle();
    } else if (event.fKey == skiff::scene::Key::kDelete) {
      if (fCaret < fText.size()) {
        const std::size_t eraseTo = nextCodepoint(fText, fCaret);
        fText.erase(fCaret, eraseTo - fCaret);
        if (fOnChanged) {
          fOnChanged(fText);
        }
        this->markDamaged();
      }
      event.handle();
    } else if (event.fKey == skiff::scene::Key::kLeft) {
      fCaret = previousCodepoint(fText, fCaret);
      this->markDamaged();
      event.handle();
    } else if (event.fKey == skiff::scene::Key::kRight) {
      fCaret = nextCodepoint(fText, fCaret);
      this->markDamaged();
      event.handle();
    } else if (event.fKey == skiff::scene::Key::kHome) {
      fCaret = 0;
      this->markDamaged();
      event.handle();
    } else if (event.fKey == skiff::scene::Key::kEnd) {
      fCaret = fText.size();
      this->markDamaged();
      event.handle();
    } else if (event.fKey == skiff::scene::Key::kEscape &&
               !fComposition.empty()) {
      fComposition.clear();
      this->markDamaged();
      event.handle();
    }
  }

  [[nodiscard]] skiff::scene::Semantics semantics() const override {
    skiff::scene::Semantics out;
    out.fRole = skiff::scene::SemanticRole::kTextBox;
    out.fLabel = fPlaceholder;
    out.fValue = fText;
    return out;
  }

  void drawSelf(skia::SkCanvas *canvas, float alpha) override {
    skia::SkFont *font = skiff::paint::defaultFont();
    if (font == nullptr) {
      return;
    }
    const skiff::paint::Painter p(canvas, *font);
    const bool active = this->selected() || this->focused();
    p.fillRounded(fBounds, fTheme.fCorner,
                  active ? fTheme.fAccent : fTheme.fSurface, alpha);
    const skia::SkColor textColour =
        active ? fTheme.fOnAccent : fTheme.fText;

    float textLeft = fBounds.fLeft + fTheme.fPaddingX;
    if (fSearchIcon) {
      skia::SkPaint icon;
      icon.setAntiAlias(true);
      icon.setStyle(skia::kStrokeStyle);
      icon.setStrokeWidth(1.8f);
      icon.setColor(fTheme.fTextDim);
      icon.setAlphaf(alpha);
      const float ix = fBounds.fLeft + fTheme.fPaddingX + 6.0f;
      const float iy = fBounds.centerY();
      canvas->drawCircle(ix, iy - 1.0f, 5.5f, icon);
      canvas->drawLine(ix + 4.0f, iy + 3.0f, ix + 8.0f, iy + 7.0f, icon);
      textLeft = ix + 14.0f;
    }

    const float baseline = p.middleBaseline(fBounds, fTheme.fFontSize);
    const float room = std::max(
        0.0f, fBounds.fRight - textLeft - fTheme.fPaddingX - fTrailingInset);
    const std::string shown = fText.substr(0, fCaret) + fComposition +
                              fText.substr(fCaret);
    if (shown.empty()) {
      p.text(fPlaceholder, textLeft, baseline, fTheme.fFontSize,
             fTheme.fTextFaint, alpha * 0.6f);
    } else {
      p.textClipped(shown, textLeft, baseline, room, fTheme.fFontSize,
                    textColour, alpha);
    }
    if (fCaretShown) {
      const float cx =
          textLeft +
          std::min(room,
                   p.measure(fText.substr(0, fCaret) + fComposition,
                             fTheme.fFontSize)) +
          2.0f;
      p.fillRect(skia::SkRect::MakeXYWH(cx, fBounds.centerY() - 9.0f, 1.5f,
                                        fTheme.fFontSize + 2.0f),
                 textColour, alpha * 0.8f);
    }
  }

private:
  [[nodiscard]] static std::size_t previousCodepoint(std::string_view text,
                                                      std::size_t from) {
    if (from == 0) {
      return 0;
    }
    --from;
    while (from > 0 &&
           (static_cast<unsigned char>(text[from]) & 0xc0u) == 0x80u) {
      --from;
    }
    return from;
  }

  [[nodiscard]] static std::size_t nextCodepoint(std::string_view text,
                                                  std::size_t from) {
    if (from >= text.size()) {
      return text.size();
    }
    ++from;
    while (from < text.size() &&
           (static_cast<unsigned char>(text[from]) & 0xc0u) == 0x80u) {
      ++from;
    }
    return from;
  }

  std::string fText;
  std::string fComposition;
  std::size_t fCaret = 0;
  int fCompositionSelectionStart = 0;
  int fCompositionSelectionLength = 0;
  bool fCaretShown = false;
};

} // namespace skiff::widgets
