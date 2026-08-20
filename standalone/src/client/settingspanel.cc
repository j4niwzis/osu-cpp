export module client.settingspanel;

import std;
import skia;
import client.ui;
import client.settings;

export namespace client {

// The view half of osu!lazer's SettingsPanel: a 170px sidebar and a 400px
// content column sliding in from the left over TRANSITION_LENGTH with
// OutQuint, one continuous scroll of every section, the sidebar scrolling to
// a section and tracking which one the viewport is in.
class SettingsPanel {
public:
  static constexpr float kSidebarWidth = 170.0f;  // EXPANDED_WIDTH
  static constexpr float kPanelWidth = 400.0f;    // PANEL_WIDTH
  static constexpr float kContentMargins = 20.0f; // CONTENT_MARGINS
  static constexpr float kItemSpacing = 14.0f;    // ITEM_SPACING
  static constexpr float kSidebarItemHeight = 46.0f;
  static constexpr float kTransitionMs = 600.0f;  // TRANSITION_LENGTH

  struct Frame {
    int fScreenW = 0, fScreenH = 0;
    float fMouseX = 0.0f, fMouseY = 0.0f;
    double fNowMs = 0.0;
    double fDtMs = 16.0;
  };

  enum class Hit : std::uint8_t { kNone, kSwallowed, kChanged };

  [[nodiscard]] bool open() const noexcept { return fOpen; }
  [[nodiscard]] float slide() const noexcept { return fSlide; }
  // Still sliding: the only time an untouched panel needs frames.
  [[nodiscard]] bool animating() const noexcept {
    return fOpen ? fSlide < 0.999f : fSlide > 0.001f;
  }

  [[nodiscard]] bool visible() const noexcept {
    return fOpen || fSlide > 0.002f;
  }

  void toggle(double nowMs) {
    fOpen = !fOpen;
    fEnterWall = nowMs;
  }

  void close(double nowMs) {
    if (fOpen) {
      fOpen = false;
      fEnterWall = nowMs;
    }
  }

  void scroll(float delta, float screenH) {
    fScroll = std::clamp(fScroll - delta * 60.0f, 0.0f,
                         std::max(0.0f, fContentHeight - screenH * 0.6f));
  }

  void draw(skia::SkCanvas *canvas, skia::SkFont &font, Settings &settings,
            const Frame &frame) {
    const float progress =
        static_cast<float>((frame.fNowMs - fEnterWall) / kTransitionMs);
    const float eased = ui::outQuint(std::clamp(progress, 0.0f, 1.0f));
    fSlide = fOpen ? eased : 1.0f - eased;

    fRows.clear();
    fRestoreHits.clear();
    fSidebarHits.clear();
    if (!fOpen && fSlide <= 0.001f) {
      return;
    }

    const ui::Painter p(canvas, font);
    const float sw = static_cast<float>(frame.fScreenW);
    const float sh = static_cast<float>(frame.fScreenH);
    const float x0 = -(kSidebarWidth + kPanelWidth) * (1.0f - fSlide);
    const float fade = std::min(1.0f, fSlide * 2.0f);

    fSectionOffsets.assign(Settings::kSections.size(), 0.0f);

    p.fillRect(skia::SkRect::MakeXYWH(0, 0, sw, sh),
               skia::colorSetARGB(static_cast<std::uint8_t>(fade * 110.0f), 0,
                                  0, 0));
    p.fillRect(skia::SkRect::MakeXYWH(x0, 0.0f, kSidebarWidth, sh),
               skia::colorSetARGB(static_cast<std::uint8_t>(fade * 252.0f), 23,
                                  19, 30));

    const float px = x0 + kSidebarWidth;
    p.fillRect(skia::SkRect::MakeXYWH(px, 0.0f, kPanelWidth, sh),
               skia::colorSetARGB(static_cast<std::uint8_t>(fade * 250.0f), 31,
                                  25, 40));
    p.textClipped("settings", px + kContentMargins, 56.0f,
                  kPanelWidth - kContentMargins * 2, 30.0f, skia::kWhite, fade);
    p.textClipped("change the way osu! behaves", px + kContentMargins, 78.0f,
                  kPanelWidth - kContentMargins * 2, 13.0f, skia::kWhite,
                  fade * 0.6f);

    constexpr float kTop = 110.0f;
    fScrollAnim = ui::approach(fScrollAnim, fScroll, 110.0f, frame.fDtMs);

    canvas->save();
    canvas->clipIRect(skia::SkIRect::MakeXYWH(
        static_cast<int>(px), static_cast<int>(kTop),
        static_cast<int>(kPanelWidth), static_cast<int>(sh - kTop)));

    const auto &defs = settings.defs();
    float y = kTop + 24.0f - fScrollAnim;
    int lastSection = -1;
    int viewportSection = 0;
    for (std::size_t i = 0; i < defs.size(); ++i) {
      const auto &d = defs[i];
      if (d.fSection != lastSection) {
        lastSection = d.fSection;
        fSectionOffsets[static_cast<std::size_t>(d.fSection)] =
            y - kTop - 24.0f + fScrollAnim;
        p.textClipped(Settings::kSections[static_cast<std::size_t>(d.fSection)],
                      px + kContentMargins, y,
                      kPanelWidth - kContentMargins * 2, 22.0f, ui::kAccent,
                      fade);
        y += 34.0f;
      }
      if (y < kTop + 80.0f) {
        viewportSection = d.fSection;
      }

      const skia::SkRect row =
          skia::SkRect::MakeXYWH(px + kContentMargins, y - 16.0f,
                                 kPanelWidth - kContentMargins * 2, 44.0f);
      fRows.push_back({row, static_cast<int>(i)});

      if (settings.isModified(i)) {
        p.fillRounded(skia::SkRect::MakeXYWH(row.fLeft - 12.0f, row.fTop + 4.0f,
                                             4.0f, row.height() - 8.0f),
                      2.0f, ui::kAccent);
        fRestoreHits.push_back({skia::SkRect::MakeXYWH(row.fLeft - 18.0f,
                                                       row.fTop, 16.0f,
                                                       row.height()),
                                static_cast<int>(i)});
      }

      p.textClipped(d.fLabel, row.fLeft, y, row.width() * 0.62f, 14.0f,
                    skia::kWhite, fade * 0.95f);

      if (d.fKind == SettingKind::kSlider) {
        const float t = (settings.value(d.fKey) - d.fMin) / (d.fMax - d.fMin);
        const skia::SkRect track =
            skia::SkRect::MakeXYWH(row.fLeft, y + 14.0f, row.width(), 6.0f);
        p.fillRounded(track, 3.0f, skia::colorSetARGB(255, 58, 48, 70));
        p.fillRounded(skia::SkRect::MakeXYWH(track.fLeft, track.fTop,
                                             track.width() * t, track.height()),
                      3.0f, ui::kAccent);
        p.circle(track.fLeft + track.width() * t, track.centerY(), 7.0f,
                 skia::kWhite, fade);
        p.textClipped(settings.displayValue(i), row.fRight - 76.0f, y, 76.0f,
                      13.0f, skia::kWhite, fade * 0.75f);
        y += 44.0f + kItemSpacing;
      } else {
        const bool on = settings.flag(d.fKey);
        const skia::SkRect box =
            skia::SkRect::MakeXYWH(row.fRight - 46.0f, y - 12.0f, 40.0f, 22.0f);
        p.fillRounded(box, 11.0f,
                      on ? ui::kAccent : skia::colorSetARGB(255, 58, 48, 70));
        p.circle(on ? box.fRight - 11.0f : box.fLeft + 11.0f, box.centerY(),
                 8.0f, skia::kWhite, fade);
        y += 30.0f + kItemSpacing;
      }
    }
    fContentHeight = y - kTop + fScrollAnim;
    canvas->restore();

    // Sidebar last, so its highlight reflects the scroll just measured.
    float sy = 70.0f;
    for (std::size_t i = 0; i < Settings::kSections.size(); ++i) {
      const skia::SkRect r =
          skia::SkRect::MakeXYWH(x0, sy, kSidebarWidth, kSidebarItemHeight);
      fSidebarHits.push_back(r);
      const bool active = static_cast<int>(i) == viewportSection;
      const bool hover = r.contains(frame.fMouseX, frame.fMouseY);

      float &grow = fGrow[i];
      grow = ui::approach(grow, active ? 1.0f : 0.0f, 90.0f, frame.fDtMs);
      const float indicatorH = 4.0f + 14.0f * ui::outElasticHalf(grow);
      if (grow > 0.01f) {
        p.fillRounded(skia::SkRect::MakeXYWH(r.fLeft + 6.0f,
                                             r.centerY() - indicatorH * 0.5f,
                                             4.0f, indicatorH),
                      2.0f, skia::kWhite);
      }
      const skia::SkColor tint =
          active ? skia::kWhite
                 : (hover ? skia::colorSetARGB(255, 200, 195, 210)
                          : skia::colorSetARGB(255, 153, 153, 153));
      p.textClipped(Settings::kSectionIcons[i], r.fLeft + 26.0f,
                    r.centerY() + 7.0f, 24.0f, 18.0f, tint, fade);
      p.textClipped(Settings::kSections[i], r.fLeft + 56.0f, r.centerY() + 6.0f,
                    kSidebarWidth - 66.0f, 15.0f, tint, fade);
      sy += kSidebarItemHeight + 5.0f;
    }

    p.textClipped("Ctrl+O to close", px + kContentMargins, sh - 24.0f,
                  kPanelWidth - kContentMargins * 2, 12.0f, skia::kWhite,
                  fade * 0.5f);
  }

  // Returns kChanged when a value was touched (so the caller can apply and
  // persist), kSwallowed when the overlay consumed the click regardless.
  [[nodiscard]] Hit click(float x, float y, bool pressed, Settings &settings) {
    if (fSlide < 0.5f) {
      return Hit::kNone;
    }
    if (!pressed) {
      const bool wasDragging = fDragging >= 0;
      fDragging = -1;
      return wasDragging ? Hit::kChanged : Hit::kSwallowed;
    }
    for (const auto &hit : fRestoreHits) {
      if (hit.fRect.contains(x, y)) {
        settings.restoreDefault(static_cast<std::size_t>(hit.fIndex));
        return Hit::kChanged;
      }
    }
    for (std::size_t i = 0; i < fSidebarHits.size(); ++i) {
      if (fSidebarHits[i].contains(x, y)) {
        if (i < fSectionOffsets.size()) {
          fScroll = fSectionOffsets[i];
        }
        return Hit::kSwallowed;
      }
    }
    for (const auto &row : fRows) {
      if (!row.fRect.contains(x, y)) {
        continue;
      }
      const auto idx = static_cast<std::size_t>(row.fIndex);
      if (settings.defs()[idx].fKind == SettingKind::kToggle) {
        settings.toggle(idx);
        return Hit::kChanged;
      }
      fDragging = row.fIndex;
      this->drag(x, settings);
      return Hit::kChanged;
    }
    return x < kSidebarWidth + kPanelWidth ? Hit::kSwallowed : Hit::kNone;
  }

  bool drag(float x, Settings &settings) {
    if (fDragging < 0) {
      return false;
    }
    for (const auto &row : fRows) {
      if (row.fIndex != fDragging) {
        continue;
      }
      settings.setFromFraction(static_cast<std::size_t>(row.fIndex),
                               (x - row.fRect.fLeft) / row.fRect.width());
      return true;
    }
    return false;
  }

  [[nodiscard]] bool dragging() const noexcept { return fDragging >= 0; }

private:
  struct Row {
    skia::SkRect fRect;
    int fIndex;
  };

  bool fOpen = false;
  float fSlide = 0.0f;
  double fEnterWall = 0.0;
  float fScroll = 0.0f;
  float fScrollAnim = 0.0f;
  float fContentHeight = 0.0f;
  int fDragging = -1;
  std::vector<Row> fRows;
  std::vector<Row> fRestoreHits;
  std::vector<skia::SkRect> fSidebarHits;
  std::vector<float> fSectionOffsets;
  std::array<float, 8> fGrow{};
};

} // namespace client
