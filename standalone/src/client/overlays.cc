export module client.overlays;

import std;
import skia;
import osu;
import client.ui;
import client.mods;
import client.video;

export namespace client {

// ---- Mod select ----------------------------------------------------------
//
// ModSelectOverlay lays mods out in columns by category; each is a rounded
// panel carrying the acronym, name, description and score multiplier, and
// toggling one keeps the mutually exclusive pairs consistent.
class ModSelect {
public:
  struct Frame {
    int fScreenW = 0, fScreenH = 0;
    float fMouseX = 0.0f, fMouseY = 0.0f;
    double fDtMs = 16.0;
  };

  [[nodiscard]] bool open() const noexcept { return fOpen; }
  [[nodiscard]] bool animating() const noexcept {
    return fOpen ? fSlide < 0.999f : fSlide > 0.001f;
  }

  [[nodiscard]] bool visible() const noexcept {
    return fOpen || fSlide > 0.002f;
  }
  void toggle() noexcept { fOpen = !fOpen; }
  void close() noexcept { fOpen = false; }

  void draw(skia::SkCanvas *canvas, skia::SkFont &font,
            std::span<const ModEntry> entries, osu::ModSet active,
            const Frame &frame) {
    fSlide = ui::approach(fSlide, fOpen ? 1.0f : 0.0f, 120.0f, frame.fDtMs);
    fHits.clear();
    if (fSlide < 0.002f) {
      return;
    }
    const ui::Painter p(canvas, font);
    const float sw = static_cast<float>(frame.fScreenW);
    const float sh = static_cast<float>(frame.fScreenH);
    const float slide = ui::outQuint(fSlide);

    p.fillRect(skia::SkRect::MakeXYWH(0, 0, sw, sh),
               skia::colorSetARGB(static_cast<std::uint8_t>(slide * 190.0f), 8,
                                  6, 12));

    const float colW = std::min(340.0f, sw * 0.34f);
    const float panelH = 96.0f;
    const float gap = 12.0f;
    const float top = sh * 0.28f + (1.0f - slide) * 60.0f;

    for (int col = 0; col < static_cast<int>(kModColumns.size()); ++col) {
      const float cx =
          sw * 0.5f + (static_cast<float>(col) - 0.5f) * (colW + 40.0f);
      const float x = cx - colW * 0.5f;
      p.textCentered(kModColumns[static_cast<std::size_t>(col)], cx,
                     top - 18.0f, 16.0f, ui::kAccent2, slide);
      float y = top;
      for (const auto &m : entries) {
        if (m.fColumn != col) {
          continue;
        }
        const skia::SkRect r = skia::SkRect::MakeXYWH(x, y, colW, panelH);
        fHits.push_back({r, m.fFlag});
        const bool on = (active & m.fFlag) != osu::mod::kNone;
        const bool hover = r.contains(frame.fMouseX, frame.fMouseY);
        p.fillRounded(r, 12.0f,
                      on ? ui::kAccent : hover ? ui::kCardSel : ui::kCardBg);
        const skia::SkColor ink =
            on ? skia::colorSetARGB(255, 24, 18, 30) : skia::kWhite;
        p.textClipped(m.fAcronym, r.fLeft + 18.0f, r.fTop + 34.0f, 70.0f, 24.0f,
                      ink, slide);
        p.textClipped(m.fName, r.fLeft + 84.0f, r.fTop + 32.0f, colW - 100.0f,
                      16.0f, ink, slide);
        p.textClipped(m.fDescription, r.fLeft + 84.0f, r.fTop + 56.0f,
                      colW - 100.0f, 12.0f, ink, slide * 0.7f);
        p.textClipped(std::format("{:.2f}x", m.fMultiplier), r.fLeft + 84.0f,
                      r.fBottom - 12.0f, 80.0f, 11.0f,
                      on ? ink : ui::kAccent2, slide * 0.9f);
        y += panelH + gap;
      }
    }
    p.textCentered("click a mod to toggle    Esc to close", sw * 0.5f,
                   sh - 40.0f, 14.0f, skia::kWhite, slide * 0.7f);
  }

  // Applies the click to the mod set; returns true if the overlay ate it.
  [[nodiscard]] bool click(float x, float y, osu::ModSet &mods) const {
    if (fSlide < 0.5f) {
      return false;
    }
    for (const auto &hit : fHits) {
      if (!hit.fRect.contains(x, y)) {
        continue;
      }
      mods = (mods & hit.fFlag) != osu::mod::kNone ? without(mods, hit.fFlag)
                                                   : (mods | hit.fFlag);
      // Speed mods and difficulty mods are mutually exclusive, as in lazer.
      if (hasMod(mods, osu::mod::kDoubleTime) &&
          hit.fFlag == osu::mod::kDoubleTime) {
        mods = without(mods, osu::mod::kHalfTime);
      }
      if (hasMod(mods, osu::mod::kHalfTime) &&
          hit.fFlag == osu::mod::kHalfTime) {
        mods = without(mods, osu::mod::kDoubleTime);
      }
      if (hasMod(mods, osu::mod::kHardRock) &&
          hit.fFlag == osu::mod::kHardRock) {
        mods = without(mods, osu::mod::kEasy);
      }
      if (hasMod(mods, osu::mod::kEasy) && hit.fFlag == osu::mod::kEasy) {
        mods = without(mods, osu::mod::kHardRock);
      }
      return true;
    }
    return true; // the overlay swallows stray clicks while open
  }

  // ModSet has | and & but no ~; removal goes through its integer conversion.
  [[nodiscard]] static osu::ModSet without(osu::ModSet set, osu::ModSet flag) {
    return osu::ModSet(static_cast<std::uint32_t>(set) &
                       ~static_cast<std::uint32_t>(flag));
  }

  [[nodiscard]] static bool hasMod(osu::ModSet set, osu::ModSet flag) {
    return (set & flag) != osu::mod::kNone;
  }

private:
  struct Hit {
    skia::SkRect fRect;
    osu::ModSet fFlag;
  };
  bool fOpen = false;
  float fSlide = 0.0f;
  std::vector<Hit> fHits;
};

// ---- Video export dialog --------------------------------------------------
class ExportDialog {
public:
  [[nodiscard]] bool open() const noexcept { return fOpen; }
  void show() {
    fOpen = true;
    fStatus.clear();
  }
  void close() noexcept { fOpen = false; }
  void setStatus(std::string status) {
    fStatusChanged = fStatusChanged || status != fStatus;
    fStatus = std::move(status);
  }

  // Whether the line of status under the buttons has changed since this was
  // last asked. A dialog waiting for an answer is a still picture; one
  // writing a video is not, and this is the difference.
  [[nodiscard]] bool takeStatusChanged() noexcept {
    const bool changed = fStatusChanged;
    fStatusChanged = false;
    return changed;
  }
  [[nodiscard]] int preset() const noexcept { return fPreset; }

  void draw(skia::SkCanvas *canvas, skia::SkFont &font, int screenW,
            int screenH, float mouseX, float mouseY) {
    fHits.clear();
    if (!fOpen) {
      return;
    }
    const ui::Painter p(canvas, font);
    const float sw = static_cast<float>(screenW);
    const float sh = static_cast<float>(screenH);
    p.fillRect(skia::SkRect::MakeXYWH(0, 0, sw, sh),
               skia::colorSetARGB(200, 8, 6, 12));

    const float w = std::min(520.0f, sw * 0.6f);
    const float h = 300.0f;
    const skia::SkRect box =
        skia::SkRect::MakeXYWH((sw - w) * 0.5f, (sh - h) * 0.5f, w, h);
    p.fillRounded(box, 14.0f, ui::kBackground5);
    p.strokeRounded(box, 14.0f, ui::kAccent, 2.0f);
    p.textCentered("export replay as video", box.centerX(), box.fTop + 46.0f,
                   24.0f, skia::kWhite);
    p.textCentered("resolution", box.centerX(), box.fTop + 82.0f, 14.0f,
                   skia::kWhite, 0.6f);

    const float bw = (w - 80.0f) / static_cast<float>(kVideoPresets.size());
    for (std::size_t i = 0; i < kVideoPresets.size(); ++i) {
      const skia::SkRect r = skia::SkRect::MakeXYWH(
          box.fLeft + 40.0f + static_cast<float>(i) * bw, box.fTop + 100.0f,
          bw - 8.0f, 40.0f);
      fHits.push_back(r);
      const bool active = static_cast<int>(i) == fPreset;
      p.fillRounded(r, 8.0f, active ? ui::kAccent : ui::kCardBg);
      p.textCentered(kVideoPresets[i].fLabel, r.centerX(), r.centerY() + 5.0f,
                     14.0f,
                     active ? skia::colorSetARGB(255, 24, 18, 30)
                            : skia::kWhite);
    }

    const skia::SkRect go = skia::SkRect::MakeXYWH(box.centerX() - 110.0f,
                                                   box.fBottom - 92.0f, 220.0f,
                                                   44.0f);
    fHits.push_back(go);
    p.fillRounded(go, 10.0f,
                  go.contains(mouseX, mouseY) ? ui::kCardSel : ui::kCardBg);
    p.strokeRounded(go, 10.0f, ui::kAccent2, 2.0f);
    p.textCentered("render", go.centerX(), go.centerY() + 6.0f, 17.0f,
                   skia::kWhite);
    p.textCentered(fStatus.empty() ? "requires ffmpeg in PATH    Esc to cancel"
                                   : fStatus,
                   box.centerX(), box.fBottom - 24.0f, 13.0f, skia::kWhite,
                   0.75f);
  }

  // Returns true when "render" was pressed.
  [[nodiscard]] bool click(float x, float y) {
    if (!fOpen) {
      return false;
    }
    for (std::size_t i = 0; i < fHits.size(); ++i) {
      if (!fHits[i].contains(x, y)) {
        continue;
      }
      if (i < kVideoPresets.size()) {
        fPreset = static_cast<int>(i);
        return false;
      }
      return true;
    }
    return false;
  }

private:
  bool fOpen = false;
  int fPreset = 1; // 1080p
  std::string fStatus;
  bool fStatusChanged = true;
  std::vector<skia::SkRect> fHits;
};

} // namespace client
