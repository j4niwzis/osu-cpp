export module client.appscreens;

import std;
import platform.web_runtime;
import platform.capabilities;
import osu;
import skia;
import client.carousel;
import client.deletedialog;
import client.filter;
import client.filtercontrol;
import client.library;
import client.listing;
import client.pause;
import client.replaybrowser;
import client.results;
import client.setpage;
import client.songselect;
import skiff.paint;

export namespace client {

// Coordinates the connected library-facing screens. The host remains a
// compile-time policy: no RTTI, virtual interface, or type-erased callbacks.
template <class Host> class AppScreens {
public:
  explicit AppScreens(Host &app) : fApp(app) {}

  // ---- Replay browser ------------------------------------------------------
  //
  // lazer surfaces past plays through the leaderboard beside song select and
  // replays them with the standard playback path. Here the saved .osr files
  // are listed in a panel; picking one starts the map with that replay.

  void toggleReplayList() {
    const std::string wanted =
        fApp.fState == State::kResults || fApp.fState == State::kPlaying
            ? fApp.fPlayback.beatmapMd5()
            : this->difficultyMd5(fApp.fLibrary.selSet(), fApp.fLibrary.selDiff());
    fApp.fReplayBrowser.toggle(wanted, fApp.fReplayPath);
  }

  // The browser lists the selected difficulty's replays, so a selection made
  // while it is open has to rebuild the list.
  void refreshReplayFilter() {
    fApp.fReplayBrowser.refreshFilter(
        this->difficultyMd5(fApp.fLibrary.selSet(), fApp.fLibrary.selDiff()),
        fApp.fReplayPath);
  }

  // md5 of a difficulty in the library, which is what an .osr records. It is
  // computed when the archive is parsed and kept in the metadata cache, so
  // this costs nothing and never has to open the archive.
  [[nodiscard]] std::string difficultyMd5(int setIdx, int diffIdx) const {
    const auto &infos = fApp.fLibrary.infosFor(setIdx);
    if (diffIdx < 0 || diffIdx >= static_cast<int>(infos.size())) {
      return {};
    }
    return infos[static_cast<std::size_t>(diffIdx)].fMd5;
  }

  void drawReplayList(skia::SkCanvas *canvas) {
    fApp.fReplayBrowser.renderOverlay(canvas, this->panelCtx(false));
  }

  // Renders a saved replay to video: the exporter draws whatever gameplay
  // state is loaded, so the map and the replay's events are brought in
  // exactly as starting a playback would, without entering gameplay.
  void exportSelectedReplay() {
    const auto replay = fApp.fReplayBrowser.selectedPath();
    if (!replay) {
      return;
    }
    auto set = fApp.fLibrary.setForBlocking(fApp.fLibrary.selSet());
    if (!set || fApp.fLibrary.selDiff() < 0 ||
        fApp.fLibrary.selDiff() >= static_cast<int>(set->fBeatmaps.size())) {
      return;
    }
    fApp.fSet = *set;
    fApp.fPlayingSet = fApp.fLibrary.selSet();
    fApp.fPlayingDiff = fApp.fLibrary.selDiff();
    fApp.fReplayPath = *replay;
    fApp.fAutoplay = true;
    fApp.resetGameplayState();
    fApp.startGameplay(
        fApp.fSet.fBeatmaps[static_cast<std::size_t>(fApp.fLibrary.selDiff())]);
    fApp.fAudio.stop();
    fApp.fMenuMusicForSet = -1; // the menu loop restarts once the export is done
    fApp.fReplayPath.clear();
    fApp.fAutoplay = fApp.fCliAutoplay;
    fApp.fReplayBrowser.close();
    fApp.fExportDialog.show();
    // One overlay replaced another in the same event, so the application's
    // boolean "an overlay is visible" state never changes. The replay page
    // covered the whole window; repaint it now or every pixel outside the
    // smaller export dialog remains from that closed page.
    fApp.fFrame.damageAll("replay browser replaced by export dialog");
  }

  // The beatmap every panel in the strip belongs to: the one just played on
  // the results screen, the selected one in the browser.
  [[nodiscard]] client::results::Ctx panelCtx(bool ownScore) {
    client::results::Ctx ctx;
    ctx.fFont = &fApp.fFont;
    ctx.fWidth = static_cast<float>(fApp.fWin.fScreenW);
    ctx.fHeight = static_cast<float>(fApp.fWin.fScreenH);
    ctx.fMouseX = fApp.fWin.fMouseX;
    ctx.fMouseY = fApp.fWin.fMouseY;
    ctx.fNowWall = fApp.wallMs();
    ctx.fEnterWall = fApp.fStateEnterWall;
    ctx.fDtMs = fApp.fUiDt;
    ctx.fOwnScore = ownScore;
    ctx.fPp = fApp.fResult.fPp;
    ctx.fMean = fApp.fResult.fMean;
    ctx.fUr = fApp.fResult.fUr;

    const bool results = fApp.fState == State::kResults;
    const int setIdx = results ? fApp.fPlayingSet : fApp.fLibrary.selSet();
    const int diffIdx = results ? fApp.fPlayingDiff : fApp.fLibrary.selDiff();
    if (results && fApp.fPlay.fMap) {
      const auto &m = fApp.fPlay.fMap->fMeta;
      ctx.fTitle = m.fTitleUnicode.empty() ? m.fTitle : m.fTitleUnicode;
      ctx.fArtist = m.fArtistUnicode.empty() ? m.fArtist : m.fArtistUnicode;
    } else if (setIdx >= 0) {
      const auto &infos = fApp.fLibrary.infosFor(setIdx);
      if (diffIdx >= 0 && diffIdx < static_cast<int>(infos.size())) {
        const auto &m = infos[static_cast<std::size_t>(diffIdx)].fMeta;
        ctx.fTitle = m.fTitleUnicode.empty() ? m.fTitle : m.fTitleUnicode;
        ctx.fArtist = m.fArtistUnicode.empty() ? m.fArtist : m.fArtistUnicode;
      }
    }
    if (setIdx >= 0) {
      const auto &infos = fApp.fLibrary.infosFor(setIdx);
      if (diffIdx >= 0 && diffIdx < static_cast<int>(infos.size())) {
        const auto &info = infos[static_cast<std::size_t>(diffIdx)];
        ctx.fVersion = info.fMeta.fVersion;
        ctx.fStars = fApp.fLibrary.shownStars(info);
        ctx.fHasDifficulty = true;
      }
    }
    return ctx;
  }

  // The strip took the press; what follows from it is the client's.
  bool panelListClick(float x, float y, bool pressed) {
    const auto result = fApp.fReplayBrowser.clickPanels(x, y, pressed);
    if (result.fWatch) {
      this->watchReplay(*result.fWatch);
    }
    return result.fTaken;
  }

  // The strip is live on the results screen and in the browser overlay.
  [[nodiscard]] bool panelListActive() const {
    return fApp.fReplayBrowser.open() || fApp.fState == State::kResults;
  }

  void watchReplay(const std::filesystem::path &path) {
    // On the results screen the strip belongs to the map just played, which is
    // not necessarily the one selected in the carousel.
    const bool results = fApp.fState == State::kResults;
    const int setIdx = results ? fApp.fPlayingSet : fApp.fLibrary.selSet();
    const int diffIdx = results ? fApp.fPlayingDiff : fApp.fLibrary.selDiff();
    if (setIdx < 0) {
      return;
    }
    fApp.fReplayBrowser.close();
    fApp.fPendingReplay = path; // startPlay picks it up and drives the engine
    fApp.startPlay(setIdx, diffIdx);
  }

  // ---- Song select ------------------------------------------------------
  //
  // Song select is a collection of scene trees: they decide where their nodes
  // are, route their input and report what has to be repainted.

  void updateSongSelect() {
    if constexpr (platform::capabilities::kBrowser) {
      if (!fApp.fLibraryLoaded) {
      if (platform::web::mapStorageReady()) {
        fApp.fLibraryRuntime.initLibrary();
      } else {
        fApp.fFrame.damageAll("waiting on local storage");
        return;
      }
      }
    }
    this->refreshReplayFilter();
    fApp.fLibrary.rebuildVisible(client::parseQuery(fApp.fFilter.text()));

    fApp.ensureBackgroundForSelection();

    const float sw = static_cast<float>(fApp.fWin.fScreenW);
    const float sh = static_cast<float>(fApp.fWin.fScreenH);

    if (fApp.fLibrary.visible().empty() != fApp.fDrawnEmpty) {
      fApp.fDrawnEmpty = fApp.fLibrary.visible().empty();
      fApp.fFrame.damageAll("song select has nothing to list");
    }

    // The carousel retains this projection and rebuilds it only when the
    // filtered library or expanded set changes.
    if (!fApp.fLibrary.visible().empty()) {
      const auto &infos = fApp.fLibrary.infosFor(fApp.fLibrary.selSet());
      fApp.fLibrary.selDiff() =
          std::clamp(fApp.fLibrary.selDiff(), 0,
                     std::max(0, static_cast<int>(infos.size()) - 1));
    }
    fApp.fCarousel.setRows(
        fApp.fLibrary.visibleRevision(), fApp.fLibrary.visible(), fApp.fLibrary.selSet(),
        [this](int set) { return fApp.fLibrary.infosFor(set).size(); });

    client::carousel::Carousel::Ctx ctx;
    ctx.fWidth = sw;
    ctx.fHeight = sh;
    ctx.fTop = client::FilterControl::kHeight + 8.0f;
    ctx.fBottom = sh - 62.0f;
    ctx.fMouseX = fApp.fWin.fMouseX;
    ctx.fMouseY = fApp.fWin.fMouseY;
    ctx.fNowMs = fApp.wallMs();
    ctx.fDtMs = fApp.fUiDt;
    ctx.fSelectedSet = fApp.fLibrary.selSet();
    ctx.fSelectedDiff = fApp.fLibrary.selDiff();
    fApp.fCarousel.update(ctx);
    fApp.fFrame.consume(fApp.fCarousel.finishFrame());

    client::FilterControl::Ctx filterCtx;
    filterCtx.fFont = &fApp.fFont;
    filterCtx.fWidth = sw;
    filterCtx.fHeight = sh;
    filterCtx.fMouseX = fApp.fWin.fMouseX;
    filterCtx.fMouseY = fApp.fWin.fMouseY;
    filterCtx.fVisibleCount = fApp.fLibrary.visible().size();
    filterCtx.fNowMs = fApp.wallMs();
    fApp.fFilter.update(filterCtx);
    fApp.fFrame.consume(fApp.fFilter.finishFrame());
    if (!fApp.fFilter.text().empty()) {
      fApp.fFrame.wakeAt(fApp.nextCaretFlip(fApp.wallMs()));
    }

    fApp.fSelectFooter.update({.fFont = &fApp.fFont,
                          .fWidth = sw,
                          .fHeight = sh,
                          .fMouseX = fApp.fWin.fMouseX,
                          .fMouseY = fApp.fWin.fMouseY,
                          .fNowMs = fApp.wallMs()});
    fApp.fFrame.consume(fApp.fSelectFooter.finishFrame());

    if (!fApp.fLibrary.visible().empty()) {
      const auto &infos = fApp.fLibrary.infosFor(fApp.fLibrary.selSet());
      if (!infos.empty()) {
        fApp.fInfoWedge.update(
            {.fFont = &fApp.fFont,
             .fWidth = sw,
             .fHeight = sh,
             .fSet = fApp.fLibrary.selSet(),
             .fDifficulty = fApp.fLibrary.selDiff(),
             .fRankedStars = fApp.fLibrary.ranked(),
             .fInfos = std::span<const osu::BeatmapInfo>(infos)});
        fApp.fFrame.consume(fApp.fInfoWedge.finishFrame());
      }
    }
  }

  void frameSongSelect() {
    auto *canvas = fApp.fFrame.canvas();
    if constexpr (platform::capabilities::kBrowser) {
      if (!fApp.fLibraryLoaded) {
      canvas->clear(skia::colorSetARGB(255, 18, 14, 24));
      fApp.drawTextCentered(canvas, "Syncing local storage...",
                             static_cast<float>(fApp.fWin.fScreenW) * 0.5f,
                             static_cast<float>(fApp.fWin.fScreenH) * 0.5f, 24.0f,
                             skia::kWhite, 0.8f);
      fApp.present();
      return;
      }
    }
    fApp.drawScreenBackground(canvas);

    const float sw = static_cast<float>(fApp.fWin.fScreenW);
    const float sh = static_cast<float>(fApp.fWin.fScreenH);

    if (fApp.fLibrary.visible().empty()) {
      const bool filtered = !fApp.fFilter.text().empty();
      fApp.drawTextCentered(
          canvas, filtered ? "No maps match the filter" : "No beatmaps yet",
          sw * 0.5f, sh * 0.45f, 28.0f, skia::kWhite, 0.9f);
      fApp.drawTextCentered(
          canvas,
          filtered ? "Backspace to edit, Esc to clear"
                   : "Drag a .osz onto the window, or press F1 to browse",
          sw * 0.5f, sh * 0.45f + 40.0f, 18.0f, fApp.kAccent);
      this->drawFilterControl(canvas);
      fApp.fSelectFooter.render(canvas);
      fApp.drawScreenFadeIn(canvas);
      fApp.present();
      return;
    }

    // ---- Left: the info wedge (lazer's BeatmapTitleWedge area).
    fApp.fInfoWedge.render(canvas);

    // ---- Right: the carousel, which masks itself to its own viewport.
    fApp.fCarousel.render(canvas);

    this->drawFilterControl(canvas);
    fApp.fSelectFooter.render(canvas);
    fApp.drawScreenFadeIn(canvas);
    fApp.present();
  }

  // ---- FilterControl ------------------------------------------------------
  //
  // The widget lives in client.filtercontrol; the app supplies the library
  // view it filters and reacts when the criteria change.

  void drawFilterControl(skia::SkCanvas *canvas) {
    fApp.fFilter.render(canvas);
  }

  bool filterClick(float x, float y, bool pressed) {
    if (!pressed) {
      fApp.fFilter.endDrag();
      return false;
    }
    const bool used = fApp.fFilter.click(x, y, pressed);
    if (fApp.fFilter.takeDirty()) {
      this->onFilterChanged();
    }
    return used;
  }

  void dragFilterRange(float x) {
    fApp.fFilter.dragRange(x);
    if (fApp.fFilter.takeDirty()) {
      fApp.fLibrary.markDirty();
    }
  }

  void cycleSortMode() {
    fApp.fFilter.cycleSort();
    this->onFilterChanged();
  }

  // Sorting and the visible set both depend on the criteria.
  // Which ordering the widget is offering, in the library's own terms.
  [[nodiscard]] client::library::Sort sortChoice() const {
    switch (fApp.fFilter.sortMode()) {
    case client::FilterControl::SortMode::kAuthor:
      return client::library::Sort::kAuthor;
    case client::FilterControl::SortMode::kArtist:
      return client::library::Sort::kArtist;
    case client::FilterControl::SortMode::kDifficulty:
      return client::library::Sort::kDifficulty;
    case client::FilterControl::SortMode::kLength:
      return client::library::Sort::kLength;
    case client::FilterControl::SortMode::kTitle:
      break;
    }
    return client::library::Sort::kTitle;
  }

  // The ordering and the difficulty range live on the widget; the library is
  // told them before it is asked to sort by them.
  void resortLibrary() {
    fApp.fLibrary.setSort(this->sortChoice());
    fApp.fLibrary.setRange(fApp.fFilter.rangeMin(), fApp.fFilter.rangeMax());
    fApp.fLibrary.sortLibrary();
  }

  void onFilterChanged() {
    this->resortLibrary();
    fApp.fLibrary.markDirty();
    fApp.fLibrary.rebuildVisible(client::parseQuery(fApp.fFilter.text()));
  }

  // Set panels carry the beatmap's cover art behind the text, as lazer's
  // PanelSetBackground does. The image is pulled from the archive the first
  // time the panel is drawn and kept with the entry.
  void drawSetPanel(skia::SkCanvas *canvas, const skia::SkRect &rect,
                    int setIndex, const std::vector<osu::BeatmapInfo> &infos,
                    bool expanded, bool hover, float corner) {
    fApp.fillRounded(canvas, rect, corner,
                      expanded ? skia::colorSetARGB(255, 66, 48, 74)
                      : hover  ? skia::colorSetARGB(255, 52, 42, 60)
                               : skia::colorSetARGB(255, 40, 33, 48));

    if (auto art = fApp.fLibrary.panelArt(setIndex)) {
      canvas->save();
      canvas->clipRRect(skia::SkRRect::MakeRectXY(rect, corner, corner), true);
      // Cover the panel preserving aspect (FillMode.Fill), cropping overflow.
      const float iw = static_cast<float>(art->width());
      const float ih = static_cast<float>(art->height());
      if (iw > 0.0f && ih > 0.0f) {
        const float scale = std::max(rect.width() / iw, rect.height() / ih);
        const float dw = iw * scale;
        const float dh = ih * scale;
        canvas->drawImageRect(
            art.get(),
            skia::SkRect::MakeXYWH(rect.centerX() - dw * 0.5f,
                                   rect.centerY() - dh * 0.5f, dw, dh),
            skia::SkSamplingOptions(skia::SkFilterMode::kLinear), nullptr);
      }
      // PanelSetBackground's darkening: a sheared three-step black gradient,
      // 0.5 alpha over the left 40%, easing to 0.2 by the right edge.
      this->drawPanelVeil(canvas, rect);
      canvas->restore();
    }
    // The hover tint above is the panel's own colour, which artwork covers
    // completely -- so on any set with a cover, hovering changed nothing that
    // could be seen while still costing the repaint. This sits over the
    // artwork instead, so the highlight exists on every panel or on none.
    if (hover && !expanded) {
      fApp.fillRounded(canvas, rect, corner,
                        skia::colorSetARGB(28, 255, 255, 255));
    }
    if (expanded) {
      fApp.strokeRounded(canvas, rect, corner, fApp.kAccent, 2.0f);
    }

    const float pad = 18.0f;
    const std::string title = infos.empty() ? "(empty)"
                              : infos.front().fMeta.fTitleUnicode.empty()
                                  ? infos.front().fMeta.fTitle
                                  : infos.front().fMeta.fTitleUnicode;
    const std::string artist = infos.empty() ? std::string{}
                               : infos.front().fMeta.fArtistUnicode.empty()
                                   ? infos.front().fMeta.fArtist
                                   : infos.front().fMeta.fArtistUnicode;
    fApp.drawTextClipped(canvas, title, rect.fLeft + pad,
                          rect.fTop + rect.height() * 0.44f,
                          rect.width() - pad * 2 - 90.0f, 19.0f, skia::kWhite);
    fApp.drawTextClipped(
        canvas, artist, rect.fLeft + pad, rect.fTop + rect.height() * 0.72f,
        rect.width() - pad * 2 - 90.0f, 14.0f, skia::kWhite, 0.75f);

    // Difficulty spread dots (PanelBeatmapSet.SpreadDisplay).
    float dotX = rect.fRight - pad;
    for (auto it = infos.rbegin(); it != infos.rend(); ++it) {
      skia::SkPaint dot;
      dot.setAntiAlias(true);
      dot.setColor(fApp.starColor(fApp.fLibrary.shownStars(*it)));
      canvas->drawCircle(dotX, rect.centerY(), 4.0f, dot);
      dotX -= 12.0f;
      if (dotX < rect.fRight - 120.0f) {
        break;
      }
    }
  }

  // PanelSetBackground: a FillFlowContainer of three boxes sheared by 0.8 on
  // X, giving a ~40-degree diagonal fade -- solid 50% black over the first
  // 40% of the width, then 50%->30%, then 30%->20%.
  void drawPanelVeil(skia::SkCanvas *canvas, const skia::SkRect &rect) {
    const float w = rect.width();
    const float h = rect.height();
    const float shear = 0.8f * h; // horizontal displacement over the height
    struct Step {
      float fFrom, fTo; // fractions of width
      float fAlphaL, fAlphaR;
    };
    const Step steps[] = {
        {0.0f, 0.40f, 0.5f, 0.5f},
        {0.40f, 0.60f, 0.5f, 0.3f},
        {0.60f, 1.05f, 0.3f, 0.2f},
    };
    for (const auto &st : steps) {
      const float x0 = rect.fLeft + st.fFrom * w;
      const float x1 = rect.fLeft + st.fTo * w;
      skia::SkPathBuilder quad;
      quad.moveTo(x0 + shear * 0.5f, rect.fTop);
      quad.lineTo(x1 + shear * 0.5f, rect.fTop);
      quad.lineTo(x1 - shear * 0.5f, rect.fBottom);
      quad.lineTo(x0 - shear * 0.5f, rect.fBottom);
      quad.close();
      skia::SkPaint p;
      p.setAntiAlias(true);
      // Approximate the per-box horizontal gradient with its mean alpha; the
      // steps are narrow enough that the banding is not visible.
      const float alpha = (st.fAlphaL + st.fAlphaR) * 0.5f;
      p.setColor(skia::colorSetARGB(static_cast<std::uint8_t>(alpha * 255.0f),
                                    0, 0, 0));
      canvas->drawPath(quad.detach(), p);
    }
  }


  void drawDiffPanel(skia::SkCanvas *canvas, const skia::SkRect &rect,
                     const osu::BeatmapInfo &info, bool selected, bool hover,
                     float corner) {
    fApp.fillRounded(canvas, rect, corner,
                      selected ? skia::colorSetARGB(255, 74, 56, 84)
                      : hover  ? skia::colorSetARGB(255, 48, 39, 56)
                               : skia::colorSetARGB(255, 34, 28, 42));
    if (selected) {
      fApp.strokeRounded(canvas, rect, corner, fApp.kAccent2, 2.0f);
    }
    const float pad = 16.0f;
    const skia::SkRect badge = skia::SkRect::MakeXYWH(
        rect.fLeft + pad, rect.centerY() - 11.0f, 62.0f, 22.0f);
    fApp.fillRounded(canvas, badge, 11.0f,
                      fApp.starColor(fApp.fLibrary.shownStars(info)));
    fApp.drawTextCentered(canvas,
                           std::format("{:.2f}", fApp.fLibrary.shownStars(info)),
                           badge.centerX(), badge.centerY() + 5.0f, 13.0f,
                           skia::colorSetARGB(255, 20, 16, 26));
    fApp.drawTextClipped(canvas, info.fMeta.fVersion, badge.fRight + 14.0f,
                          rect.centerY() + 5.0f,
                          rect.width() - badge.width() - pad * 3, 15.0f,
                          skia::kWhite, 0.95f);
  }

  bool selectFooterClick(float x, float y) {
    using Action = client::songselect::Action;
    switch (fApp.fSelectFooter.click(x, y)) {
    case Action::kBack:
      fApp.switchState(State::kMainMenu);
      return true;
    case Action::kMods:
      fApp.fOverlays.toggleMods();
      return true;
    case Action::kRandom:
      fApp.selectRandom();
      return true;
    case Action::kImport:
      fApp.fLibraryRuntime.importOsz();
      return true;
    case Action::kBrowse:
      fApp.fInput.openDownloads();
      return true;
    case Action::kReplays:
      this->toggleReplayList();
      return true;
    case Action::kDelete:
      this->askDeleteBeatmap();
      return true;
    case Action::kSettings:
      fApp.fOverlays.toggleSettings();
      return true;
    case Action::kTaken:
      return true;
    case Action::kNone:
      break;
    }
    return false;
  }

  // lazer never deletes a beatmap without asking, and neither does this.
  void askDeleteBeatmap() {
    if (fApp.fLibrary.selSet() < 0 ||
        fApp.fLibrary.selSet() >= static_cast<int>(fApp.fLibrary.sets().size())) {
      return;
    }
    const auto &infos = fApp.fLibrary.infosFor(fApp.fLibrary.selSet());
    if (infos.empty()) {
      return;
    }
    const auto &meta = infos.front().fMeta;
    const std::string title = std::format(
        "{} - {}",
        meta.fArtistUnicode.empty() ? meta.fArtist : meta.fArtistUnicode,
        meta.fTitleUnicode.empty() ? meta.fTitle : meta.fTitleUnicode);
    fApp.fDeleteDialog.show(title, infos.size());
  }

  bool confirmDeleteClick(float x, float y) {
    if (!fApp.fDeleteDialog.open()) {
      return false;
    }
    if (fApp.fDeleteDialog.click(x, y) ==
        client::DeleteDialog::Choice::kDelete) {
      this->deleteSelectedBeatmap();
    }
    return true; // the dialog is modal either way
  }

  // Removes the archive and everything the client remembers about it.
  void deleteSelectedBeatmap() {
    if (fApp.fLibrary.selSet() < 0 ||
        fApp.fLibrary.selSet() >= static_cast<int>(fApp.fLibrary.sets().size())) {
      return;
    }
    const auto index = static_cast<std::size_t>(fApp.fLibrary.selSet());
    // The track playing under the menu belongs to this set; let it go before
    // the file does.
    fApp.fLibraryRuntime.stopMenuMusic();
    fApp.fMenuMusicForSet = -1;
    fApp.fBackgroundForSet = -1;

    const std::string name = fApp.fLibrary.deleteSet(index);
    this->resortLibrary();
    fApp.fLibrary.rebuildVisible(client::parseQuery(fApp.fFilter.text()));
    fApp.fPlayingSet = -1;
    fApp.fPlayingDiff = -1;
    fApp.notify(name.empty() ? "beatmap deleted"
                              : std::format("deleted {}", name));
  }

  void drawBottomBar(skia::SkCanvas *canvas, const std::string &hint) {
    const float sw = static_cast<float>(fApp.fWin.fScreenW);
    const float sh = static_cast<float>(fApp.fWin.fScreenH);
    fApp.fillRounded(canvas,
                      skia::SkRect::MakeXYWH(0.0f, sh - 44.0f, sw, 44.0f), 0.0f,
                      fApp.kPanelBg);
    fApp.drawTextCentered(canvas, hint, sw * 0.5f, sh - 16.0f, 15.0f,
                           skia::kWhite, 0.75f);
  }

  // ---- Download screen ---------------------------------------------------
  //
  // The whole listing -- header, filters, sort bar and cards -- is drawn by
  // client.listing; here it only gets the data and the transfer state.

  // Everything about this screen that is not drawing. It runs before the
  // client has committed to a frame, so what it marks as damaged is the
  // answer to "is this frame worth drawing".
  void updateDownload() {
    fApp.fMirrors.pollProgress();
    fApp.fMirrors.pollSearchRetry();
    // A card also draws its own state -- idle, fetching, done, failed -- out
    // of the entry, and that is written from a dozen places: a transfer
    // starting, one finishing, an import marking everything already owned.
    // Comparing it here catches all of them, including the ones written after
    // this was, which a call at each site would not.
    fApp.fEntryStates.resize(fApp.fMirrors.results().size(), 0xFF);
    for (std::size_t i = 0; i < fApp.fMirrors.results().size(); ++i) {
      const auto state = static_cast<std::uint8_t>(fApp.fMirrors.results()[i].fSt);
      if (fApp.fEntryStates[i] != state) {
        fApp.fEntryStates[i] = state;
        fApp.fListing.entryChanged(static_cast<int>(i));
      }
    }
    client::listing::Listing::Ctx ctx;
    ctx.fFont = &fApp.fFont;
    ctx.fWidth = static_cast<float>(fApp.fWin.fScreenW);
    ctx.fHeight = static_cast<float>(fApp.fWin.fScreenH);
    ctx.fMouseX = fApp.fWin.fMouseX;
    ctx.fMouseY = fApp.fWin.fMouseY;
    ctx.fNowMs = fApp.wallMs();
    ctx.fDtMs = fApp.fUiDt;
    ctx.fEntries = fApp.fMirrors.results();
    ctx.fLoading = fApp.fMirrors.searching();
    fApp.fMirrors.pollPreview();
    fApp.fListing.setPreview(fApp.fMirrors.previewId(), fApp.fMirrors.previewProgress());
    fApp.fListing.update(ctx);
    fApp.fFrame.consume(fApp.fListing.finishFrame());
    if (fApp.fSetPage.open()) {
      const std::size_t idx = fApp.fMirrors.indexOfSet(fApp.fSetPage.setId());
      if (idx >= fApp.fMirrors.results().size()) {
        fApp.fSetPage.close(); // the set fell out of the results
        fApp.fFrame.damageAll("beatmap page closed");
      }
      client::setpage::SetPage::Ctx page;
      page.fEntry =
          idx < fApp.fMirrors.results().size() ? &fApp.fMirrors.results()[idx] : nullptr;
      page.fFont = &fApp.fFont;
      page.fWidth = ctx.fWidth;
      page.fHeight = ctx.fHeight;
      page.fMouseX = fApp.fWin.fMouseX;
      page.fMouseY = fApp.fWin.fMouseY;
      page.fNowMs = fApp.wallMs();
      page.fPreviewPlaying = fApp.fMirrors.previewId() == fApp.fSetPage.setId();
      page.fPreviewProgress = fApp.fMirrors.previewProgress();
      fApp.fSetPage.update(page);
      fApp.fFrame.consume(fApp.fSetPage.finishFrame());
    }
    // Covers are only fetched for what is on screen, which the listing knows
    // and the client did not: this used to walk every result that passed the
    // filters, on screen or four hundred cards below it.
    for (const int idx : fApp.fListing.onScreen()) {
      fApp.fMirrors.requestThumb(static_cast<std::size_t>(idx));
    }
    // Scrolling near the end pages the next batch in, as the overlay's
    // scroll container asks for the next cursor.
    if (fApp.fListing.wantsMore()) {
      fApp.fMirrors.fetchPage();
    }
    // The caret blinks on a clock of its own. Rather than keeping frames
    // coming so the moment is not missed, the screen says when the moment is
    // -- and says nothing when there is no such moment: no caret on screen,
    // or the beatmap page covering the listing it belongs to.
    if (!fApp.fSetPage.open()) {
      const double wake = fApp.fListing.nextChangeWall(fApp.wallMs());
      if (wake > 0.0) {
        fApp.fFrame.wakeAt(wake);
      }
    }
  }

  void frameDownload() {
    auto *canvas = fApp.fFrame.canvas();
    fApp.fListing.render(canvas);
    fApp.fSetPage.render(canvas);
    fApp.drawScreenFadeIn(canvas);
    fApp.present();
  }

  // ---- Pause ------------------------------------------------------------
  //
  // client.pause is lazer's GameplayMenuOverlay; here it only gets the play's
  // numbers and says what was clicked.

  void updatePause() {
    client::pause::PauseMenu::Ctx ctx;
    ctx.fFont = &fApp.fFont;
    ctx.fWidth = static_cast<float>(fApp.fWin.fScreenW);
    ctx.fHeight = static_cast<float>(fApp.fWin.fScreenH);
    ctx.fMouseX = fApp.fWin.fMouseX;
    ctx.fMouseY = fApp.fWin.fMouseY;
    ctx.fNowMs = fApp.wallMs();
    ctx.fDtMs = fApp.fUiDt;
    ctx.fAnimateTriangles = fApp.fSettings.flag("pausetriangles");
    ctx.fRetries = fApp.fPlay.fRetryCount;
    ctx.fProgress = this->playProgress();
    ctx.fAccuracy = fApp.fPlay.fEngine
                        ? static_cast<float>(fApp.fPlay.fEngine->score().accuracy())
                        : 1.0f;
    fApp.fPauseMenu.update(ctx);
    fApp.fFrame.consume(fApp.fPauseMenu.finishFrame());
  }

  // How far into the playable part of the map the pause happened, which is
  // what GameplayMenuOverlay puts under the buttons.
  [[nodiscard]] float playProgress() const {
    if (!fApp.fPlay.fMap || fApp.fPlay.fMap->fObjects.empty()) {
      return 0.0f;
    }
    const double first = osu::startTime(fApp.fPlay.fMap->fObjects.front());
    const double last = osu::startTime(fApp.fPlay.fMap->fObjects.back());
    if (last <= first) {
      return 0.0f;
    }
    return static_cast<float>(
        std::clamp((fApp.fPlay.fPausedNow - first) / (last - first), 0.0, 1.0));
  }

  void framePaused() {
    // The frozen game underneath does not change while it is paused; what
    // moves is the overlay, and the frame is clipped to what the overlay
    // said. The scene is still redrawn, because a clipped repaint has to put
    // back whatever was under the piece being repainted.
    fApp.fView.invalidate();
    auto *canvas = fApp.fFrame.canvas();
    fApp.fView.render(fApp.gameplayCtx(canvas), fApp.fPlay.fPausedNow);
    fApp.fPauseMenu.render(canvas);
    fApp.present();
  }

  // ---- Results ----------------------------------------------------------

  void updateResults() {
    fApp.fReplayBrowser.updateResultActions(
        {.fFont = &fApp.fFont,
         .fWidth = static_cast<float>(fApp.fWin.fScreenW),
         .fHeight = static_cast<float>(fApp.fWin.fScreenH),
         .fMouseX = fApp.fWin.fMouseX,
         .fMouseY = fApp.fWin.fMouseY,
         .fNowMs = fApp.wallMs()});
    fApp.fFrame.consume(fApp.fReplayBrowser.finishResultFrame());
    const auto motion =
        fApp.fReplayBrowser.updateMotion(fApp.fWin.fMouseX, fApp.fWin.fMouseY);
    if (motion.fFullDamage) {
      fApp.fFrame.damageAll("results strip moving");
    } else if (motion.fDamage) {
      fApp.fFrame.damage(*motion.fDamage);
    }
  }

  void frameResults() {
    fApp.fView.invalidate();
    auto *canvas = fApp.fFrame.canvas();
    fApp.drawScreenBackground(canvas);
    const skiff::paint::Painter p(canvas, fApp.fFont);

    const float sw = static_cast<float>(fApp.fWin.fScreenW);
    const float sh = static_cast<float>(fApp.fWin.fScreenH);
    p.fillRect(skia::SkRect::MakeXYWH(0, 0, sw, sh),
               skia::colorSetARGB(160, 10, 8, 14));

    std::optional<client::ReplayBrowser::OwnScore> own;
    if (fApp.fReplayPath.empty()) {
      own = client::ReplayBrowser::OwnScore{.fScore = fApp.fResult.fScore,
                                            .fGrade = fApp.fResult.fGrade,
                                            .fPp = fApp.fResult.fPp,
                                            .fMean = fApp.fResult.fMean,
                                            .fUr = fApp.fResult.fUr};
    }
    fApp.fReplayBrowser.renderResults(canvas, this->panelCtx(bool(own)),
                                 std::move(own), fApp.fPlay.fLastSavedReplay);

    fApp.drawScreenFadeIn(canvas);
    fApp.present();
  }

private:
  using State = typename Host::State;
  Host &fApp;
};

} // namespace client
