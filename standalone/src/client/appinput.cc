export module client.appinput;

import std;
import osu;
import glfw;
import present;
import client.input;
import client.gameplayview;
import client.listing;
import client.pause;
import client.replaybrowser;
import client.results;
import client.setpage;
import skiff.scene;

export namespace client {

// The application-specific input policy. Host stays a template so the
// controller can be friended without an RTTI boundary, virtual interface, or
// a table of type-erased callbacks.
template <class Host> class AppInput {
public:
  explicit AppInput(Host &app) : fApp(app) {}

  // Events arrive from the queue already stamped with the wall clock of the
  // moment GLFW delivered them; eventGameTime() maps that onto the game
  // timeline. Judgement therefore no longer depends on when the frame loop
  // got around to polling: at any frame rate, a hit is judged at the time it
  // physically happened.

  void drainInput() {
    // Resizes are coalesced: a window being dragged by its corner delivers
    // dozens of sizes a frame, and acting on each of them means recreating
    // surfaces dozens of times a frame and drawing none of them -- which is
    // what the black flash while resizing was.
    Event ev;
    bool resized = false;
    int width = 0;
    int height = 0;
    while (fApp.fWindowRuntime.pop(ev)) {
      if (ev.fType == EventType::kResize) {
        resized = true;
        width = ev.fA;
        height = ev.fB;
        continue;
      }
      this->applyEvent(ev);
    }
    if (resized && (width != fApp.fWin.fScreenW || height != fApp.fWin.fScreenH)) {
      fApp.resize(width, height);
    }

    // And the size the server has this instant, which is not the same thing.
    // A resize reaches this thread through glfwPollEvents on another one, and
    // every frame drawn in between is drawn into a buffer that is already the
    // new size and has never been painted -- so a frame that clips to what
    // moved leaves the rest of it black. Asking here, before the surfaces are
    // chosen, means such a frame recreates them instead of drawing into the
    // old ones.
    //
    // A round trip to the X server per frame. It is a local socket and the
    // alternative was two days of this.
    int liveW = 0;
    int liveH = 0;
    if (present::surfaceSize(fApp.fWindow, &liveW, &liveH) &&
        (liveW != fApp.fWin.fScreenW || liveH != fApp.fWin.fScreenH)) {
      fApp.resize(liveW, liveH);
    }
  }

  [[nodiscard]] double eventGameTime(double wallMs) {
    // The map's timeline does not exist until a frame of it has been shown,
    // and input is drained before the frame. Dating an event from the clock
    // in that window reads an anchor left over from the last map -- or, on
    // the first play of a session, from process start -- so a single pointer
    // movement reaches the engine stamped minutes in, and advance() walks the
    // whole map and misses every object in it.
    if (fApp.fPlay.fAwaitingFirstFrame) {
      return 0.0;
    }
#ifdef __EMSCRIPTEN__
    return wallMs - fApp.fPlay.fStartMs;
#else
    return fApp.fPlay.fClock.sample(wallMs);
#endif
  }

  void applyEvent(const Event &ev) {
    // Enough to react and to paint the reaction. Anything that keeps moving
    // after that -- an eased hover, a scroll gliding to a stop -- owes itself
    // the frames it needs, and stops owing them when it settles.
    fApp.fFrame.oweFrames(3);
    switch (ev.fType) {
    case EventType::kResize:
      fApp.resize(ev.fA, ev.fB);
      break;
    case EventType::kCursorMove:
      fApp.fWin.fMouseX = ev.fX;
      fApp.fWin.fMouseY = ev.fY;
      this->routePointer(skiff::scene::PointerAction::kMove, ev.fX, ev.fY);
      if (fApp.fSettingsPanel.dragging()) {
        fApp.fOverlays.dragSetting(ev.fX);
      }
      if (fApp.fFilter.dragging()) {
        fApp.fScreens.dragFilterRange(ev.fX);
      }
      if (fApp.fReplayBrowser.panelsDragging()) {
        fApp.fReplayBrowser.dragPanels(ev.fX);
      }
      if (fApp.fState == State::kPlaying && !fApp.fAutoplay) {
        fApp.fCursor = fApp.fPlayback.cursorFromEvent(ev);
        const double at = this->eventGameTime(ev.fWallMs);
        this->submitTimed({at, fApp.fCursor, osu::InputAction::kMove});
        // The trail is fed from the events, as it already is for a replay.
        // Taking one point per frame instead meant its shape was drawn from
        // however many frames the machine managed -- three points across a
        // whole trail at twenty a second.
        fApp.fView.addTrailPoint(fApp.fCursor, at);
      }
      break;
    case EventType::kScroll:
      if (fApp.fSettingsPanel.open()) {
        fApp.fOverlays.scrollSettings(ev.fX);
        break;
      }
      if (fApp.fScreens.panelListActive()) {
        fApp.fReplayBrowser.scrollPanels(ev.fX * 120.0f);
        break;
      }
      if (fApp.fState == State::kSongSelect) {
        // Free scrolling, like lazer's carousel: the wheel moves the view,
        // the selection stays put until the user picks something else.
        fApp.fCarousel.scroll(ev.fX);
      } else if (fApp.fState == State::kDownload) {
        if (fApp.fSetPage.open()) {
          fApp.fSetPage.scroll(ev.fX);
        } else {
          fApp.fListing.scroll(ev.fX);
        }
      }
      break;
    case EventType::kChar:
      // Before the screens, not after them: song select answers first and
      // puts everything into its filter, so a check further down was never
      // reached. The dialog is the frontmost thing while it is up, and it
      // closes when the screen changes, so this cannot eat a filter's letters
      // from somewhere else.
      if (fApp.fExportDialog.open()) {
        fApp.fExportDialog.typeInSize(static_cast<char>(ev.fA));
        break;
      }
      if (fApp.fState == State::kSongSelect) {
        if (fApp.fSwallowChar) {
          fApp.fSwallowChar = false;
          break;
        }
        std::string utf8;
        this->appendUtf8(utf8, static_cast<std::uint32_t>(ev.fA));
        if (this->routeText(utf8)) {
          fApp.fLibrary.markDirty();
          break;
        }
        fApp.fFilter.appendText(utf8);
        fApp.fLibrary.markDirty();
        break;
      }
      if (fApp.fState == State::kDownload) {
        if (fApp.fSwallowChar || fApp.fSetPage.open()) {
          fApp.fSwallowChar = false;
          break;
        }
        std::string utf8;
        this->appendUtf8(utf8, static_cast<std::uint32_t>(ev.fA));
        if (this->routeText(utf8)) {
          break;
        }
        fApp.fListing.filters().fQuery += utf8;
        fApp.fListing.queryEdited();
        fApp.fListing.scrollToStart(); // onTypingStarted
      }
      break;
    case EventType::kKey:
      this->applyKey(ev);
      break;
    case EventType::kMouseButton:
      this->applyMouseButton(ev);
      break;
    }
  }

  // A held key repeats where repeating is what the key means -- deleting a
  // character, stepping through a list -- and not where a second press would
  // undo the first. Gameplay is not in either group: a key held down is one
  // press there, however long it is held.
  [[nodiscard]] static bool repeatable(int key) {
    return key == glfw::kKeyBackspace || key == glfw::kKeyDelete ||
           key == glfw::kKeyLeft || key == glfw::kKeyRight ||
           key == glfw::kKeyUp || key == glfw::kKeyDown ||
           key == glfw::kKeyPageUp || key == glfw::kKeyPageDown;
  }

  void applyKey(const Event &ev) {
    const int key = ev.fA;
    int action = ev.fB;
    if (action == glfw::kRepeat) {
      if (fApp.fState == State::kPlaying || !repeatable(key)) {
        return;
      }
      action = glfw::kPress; // a repeat is a press to everything that repeats
    }
    // GLFW reports the modifier state with every key event; tracking press
    // and release of the control keys separately loses sync whenever focus
    // changes while held.
    const bool ctrl = (static_cast<int>(ev.fX) & glfw::kModControl) != 0;
    const bool shift = (static_cast<int>(ev.fX) & glfw::kModShift) != 0;
    if (key == glfw::kKeyLeftControl || key == glfw::kKeyRightControl) {
      return;
    }
    // Backspace edits the size while the dialog is up, for the same reason.
    if (fApp.fExportDialog.open() && action == glfw::kPress &&
        key == glfw::kKeyBackspace) {
      fApp.fExportDialog.backspaceSize();
      return;
    }
    if (action == glfw::kPress && key == glfw::kKeyO && ctrl) {
      fApp.fOverlays.toggleSettings();
      return;
    }
    if (action == glfw::kPress && key == glfw::kKeyTab &&
        this->routeKey(skiff::scene::Key::kTab, shift)) {
      return;
    }
    std::optional<skiff::scene::Key> editingKey;
    if (key == glfw::kKeyBackspace) {
      editingKey = skiff::scene::Key::kBackspace;
    } else if (key == glfw::kKeyDelete) {
      editingKey = skiff::scene::Key::kDelete;
    } else if (key == glfw::kKeyLeft) {
      editingKey = skiff::scene::Key::kLeft;
    } else if (key == glfw::kKeyRight) {
      editingKey = skiff::scene::Key::kRight;
    }
    if (action == glfw::kPress && editingKey &&
        this->routeKey(*editingKey, shift)) {
      if (fApp.fState == State::kSongSelect &&
          (*editingKey == skiff::scene::Key::kBackspace ||
           *editingKey == skiff::scene::Key::kDelete)) {
        fApp.fLibrary.markDirty();
      }
      return;
    }
    if (action == glfw::kPress && key == glfw::kKeyEscape) {
      if (fApp.fDeleteDialog.open()) {
        fApp.fDeleteDialog.close();
        return;
      }
      if (fApp.fExportDialog.open()) {
        fApp.fExportDialog.close();
        return;
      }

      if (fApp.fReplayBrowser.open()) {
        fApp.fReplayBrowser.close();
        return;
      }
      if (fApp.fSettingsPanel.open()) {
        fApp.fOverlays.closeSettings();
        return;
      }
      if (fApp.fModSelect.open()) {
        fApp.fModSelect.close();
        return;
      }
    }
    if (action != glfw::kPress && fApp.fState != State::kPlaying) {
      return; // menus only care about presses
    }

    // The browser covers the screen, so it takes keys before the screen under
    // it does -- otherwise the arrows would move the carousel behind it.
    if (fApp.fReplayBrowser.open()) {
      this->keyReplayList(key);
      return;
    }

    switch (fApp.fState) {
    case State::kMainMenu:
      if (const auto requested = fApp.fMainMenu.key(fApp.menuKey(key))) {
        fApp.menuAction(*requested);
      }
      return;
    case State::kSongSelect:
      this->keySongSelect(key);
      return;
    case State::kDownload:
      this->keyDownload(key);
      return;
    case State::kPaused:
      // GameplayMenuOverlay: the arrows cycle the buttons, enter takes the
      // selected one, and back is the first button -- Continue.
      if (key == glfw::kKeyEscape) {
        fApp.resumeGame();
      } else if (key == glfw::kKeyUp) {
        fApp.fPauseMenu.selectPrevious();
      } else if (key == glfw::kKeyDown) {
        fApp.fPauseMenu.selectNext();
      } else if (key == glfw::kKeyEnter) {
        this->applyPauseAction(fApp.fPauseMenu.triggerSelected());
      }
      return;
    case State::kResults:
      if (key == glfw::kKeyEscape) {
        fApp.quitToSelect();
      } else if (key == glfw::kKeyEnter) {
        fApp.retry();
      }
      return;
    case State::kPlaying:
      break;
    }

    // Playing.
    if (key == glfw::kKeyEscape) {
      if (action == glfw::kPress) {
        fApp.pauseGame();
      }
      return;
    }
    // Playing. The pause button in the corner belongs to the client, not to
    // the game: it is answered before the press becomes a tap, and it works
    // while a replay is being watched, where taps are ignored altogether.
    if (fApp.fState == State::kPlaying && button == glfw::kMouseButtonLeft &&
        action == glfw::kPress &&
        client::GameplayView::pauseButtonBounds(
            fApp.fWin.fScreenW, fApp.fWin.fScreenH, fApp.uiScale())
            .contains(fApp.fWin.fMouseX, fApp.fWin.fMouseY)) {
      fApp.pauseGame();
      return;
    }

    if (fApp.fAutoplay) {
      return;
    }
    std::uint32_t bit = 0;
    if (key == glfw::kKeyZ) {
      bit = 1u << 0;
    } else if (key == glfw::kKeyX) {
      bit = 1u << 1;
    } else if (key == glfw::kKeySpace) {
      bit = 1u << 2;
    } else {
      return;
    }
    this->applyButton(bit, action == glfw::kPress, ev.fWallMs);
  }

  void keySongSelect(int key) {
    const int nSets = static_cast<int>(fApp.fLibrary.visible().size());
    if (key == glfw::kKeyEscape) {
      if (!fApp.fFilter.text().empty()) {
        fApp.fFilter.clearText();
        fApp.fLibrary.markDirty();
        return;
      }
      fApp.switchState(State::kMainMenu);
      return;
    }
    if (key == glfw::kKeyBackspace) {
      if (!fApp.fFilter.text().empty()) {
        fApp.fFilter.popText();
        fApp.fLibrary.markDirty();
      }
      return;
    }
    if (key == glfw::kKeyF3) {
      fApp.fScreens.cycleSortMode();
      return;
    }
    // lazer's song select keeps the search box permanently focused: every
    // printable key belongs to the query, never to a shortcut. Actions live
    // on function keys and the footer buttons.
    if (key == glfw::kKeyF1) {
      fApp.fOverlays.toggleMods(); // lazer: F1 is mod select
      return;
    }
    if (key == glfw::kKeyF4) {
      this->openDownloads();
      return;
    }
    if (key == glfw::kKeyF5) {
      fApp.fLibraryRuntime.importOsz();
      return;
    }
    if (key == glfw::kKeyF2) {
      fApp.selectRandom();
      return;
    }
    if (nSets == 0) {
      return;
    }
    const int nDiffs =
        static_cast<int>(fApp.fLibrary.infosFor(fApp.fLibrary.selSet()).size());
    if (key == glfw::kKeyUp) {
      if (fApp.fLibrary.selDiff() > 0) {
        --fApp.fLibrary.selDiff();
      } else if (const int pos = fApp.fLibrary.visiblePos(); pos > 0) {
        fApp.fLibrary.selSet() =
            fApp.fLibrary.visible()[static_cast<std::size_t>(pos - 1)];
        fApp.fLibrary.selDiff() = std::max(
            0,
            static_cast<int>(fApp.fLibrary.infosFor(fApp.fLibrary.selSet()).size()) - 1);
      }
    } else if (key == glfw::kKeyDown) {
      if (fApp.fLibrary.selDiff() + 1 < nDiffs) {
        ++fApp.fLibrary.selDiff();
      } else if (const int pos = fApp.fLibrary.visiblePos();
                 pos >= 0 &&
                 pos + 1 < static_cast<int>(fApp.fLibrary.visible().size())) {
        fApp.fLibrary.selSet() =
            fApp.fLibrary.visible()[static_cast<std::size_t>(pos + 1)];
        fApp.fLibrary.selDiff() = 0;
      }
    } else if (key == glfw::kKeyLeft) {
      if (const int pos = fApp.fLibrary.visiblePos(); pos > 0) {
        fApp.fLibrary.selSet() =
            fApp.fLibrary.visible()[static_cast<std::size_t>(pos - 1)];
        fApp.fLibrary.selDiff() = 0;
      }
    } else if (key == glfw::kKeyRight) {
      if (const int pos = fApp.fLibrary.visiblePos();
          pos >= 0 && pos + 1 < static_cast<int>(fApp.fLibrary.visible().size())) {
        fApp.fLibrary.selSet() =
            fApp.fLibrary.visible()[static_cast<std::size_t>(pos + 1)];
        fApp.fLibrary.selDiff() = 0;
      }
    } else if (key == glfw::kKeyEnter) {
      fApp.startPlay(fApp.fLibrary.selSet(), fApp.fLibrary.selDiff());
    }
  }

  void openDownloads() {
    fApp.fSwallowChar = true;
    fApp.switchState(State::kDownload);
    if (fApp.fMirrors.results().empty() && !fApp.fMirrors.searching()) {
      fApp.fMirrors.startSearch(
          fApp.fListing.filters()); // the listing opens on results, not a blank page
    }
  }

  // Leaving the beatmap page, and leaving the download screen. Both have
  // three ways in now -- escape, the arrow drawn on the screen, and a click
  // beside the thing being left -- so neither lives inside a key handler.
  void closeSetPage() {
    // The page covered the listing; what it uncovers has to be repainted, and
    // the listing has no way of knowing it was ever covered.
    fApp.fSetPage.close(); // back to the listing, as the overlay stacks
    fApp.fFrame.damageAll("beatmap page closed");
  }

  void leaveDownload() {
    fApp.fMirrors.stopPreview();
    fApp.fMirrors.restoreMusic();
    fApp.switchState(State::kSongSelect);
  }

  void keyDownload(int key) {
    if (key == glfw::kKeyEscape) {
      if (fApp.fSetPage.open()) {
        this->closeSetPage();
        return;
      }
      this->leaveDownload();
      return;
    }
    if (key == glfw::kKeyEnter) {
      fApp.fMirrors.startSearch(fApp.fListing.filters());
      return;
    }
    if (key == glfw::kKeyBackspace) {
      this->popUtf8(fApp.fListing.filters().fQuery);
      fApp.fListing.queryEdited();
    }
  }

  void keyReplayList(int key) {
    std::optional<std::filesystem::path> watch;
    if (key == glfw::kKeyLeft) {
      watch = fApp.fReplayBrowser.key(client::ReplayBrowser::Key::kPrevious);
    } else if (key == glfw::kKeyRight) {
      watch = fApp.fReplayBrowser.key(client::ReplayBrowser::Key::kNext);
    } else if (key == glfw::kKeyEnter) {
      watch = fApp.fReplayBrowser.key(client::ReplayBrowser::Key::kActivate);
    }
    if (watch) {
      fApp.fScreens.watchReplay(*watch);
    }
  }

  void applyMouseButton(const Event &ev) {
    const int button = ev.fA;
    const int action = ev.fB;

    if (fApp.fState == State::kMainMenu || fApp.fState == State::kSongSelect ||
        fApp.fState == State::kDownload || fApp.fState == State::kPaused ||
        fApp.fState == State::kResults) {
      if (button == glfw::kMouseButtonLeft) {
        const bool pressed = action == glfw::kPress;
        // The panel strip takes the press before anything else so that a drag
        // that starts on a panel scrolls the list instead of selecting.
        if (fApp.fScreens.panelListActive() && !fApp.fExportDialog.open() &&
            !fApp.fSettingsPanel.open() && !fApp.fModSelect.open() &&
            fApp.fScreens.panelListClick(fApp.fWin.fMouseX, fApp.fWin.fMouseY,
                                         pressed)) {
          return;
        }
        if (pressed) {
          this->clickAt(fApp.fWin.fMouseX, fApp.fWin.fMouseY);
        } else {
          this->routePointer(skiff::scene::PointerAction::kUp,
                             fApp.fWin.fMouseX, fApp.fWin.fMouseY);
          fApp.fOverlays.settingsClick(fApp.fWin.fMouseX, fApp.fWin.fMouseY,
                                       false);
          fApp.fScreens.filterClick(fApp.fWin.fMouseX, fApp.fWin.fMouseY,
                                    false);
        }
      }
      return;
    }

    if (fApp.fAutoplay) {
      return;
    }
    std::uint32_t bit = 0;
    if (button == glfw::kMouseButtonLeft) {
      bit = 1u << 3;
    } else if (button == glfw::kMouseButtonRight) {
      bit = 1u << 4;
    } else {
      return;
    }
    this->applyButton(bit, action == glfw::kPress, ev.fWallMs);
  }

  void refreshInputLayers() {
    using Layer = skiff::scene::InputRouter::Layer;
    std::vector<Layer> layers;
    const auto add = [&layers](skiff::scene::Drawable *root,
                               bool modal = false) {
      if (root != nullptr) {
        layers.push_back({root, modal});
      }
    };

    // The replay strip is still immediate-mode and modal while open. An
    // empty stack also cancels capture in a retained scene it covers.
    if (!fApp.fReplayBrowser.open()) {
      switch (fApp.fState) {
      case State::kMainMenu:
        add(fApp.fMainMenu.sceneRoot());
        break;
      case State::kSongSelect:
        add(fApp.fCarousel.sceneRoot());
        add(fApp.fFilter.sceneRoot());
        add(fApp.fSelectFooter.sceneRoot());
        break;
      case State::kDownload:
        add(fApp.fListing.sceneRoot());
        if (fApp.fSetPage.open()) {
          add(fApp.fSetPage.sceneRoot(), true);
        }
        break;
      case State::kPaused:
        add(fApp.fPauseMenu.sceneRoot());
        break;
      case State::kResults:
        add(fApp.fReplayBrowser.resultActionsRoot());
        break;
      default:
        break;
      }
      if (fApp.fModSelect.open()) {
        add(fApp.fModSelect.sceneRoot(), true);
      }
      if (fApp.fSettingsPanel.visible()) {
        add(fApp.fSettingsPanel.sceneRoot());
      }
      if (fApp.fExportDialog.open()) {
        add(fApp.fExportDialog.sceneRoot(), true);
      }
      if (fApp.fDeleteDialog.open()) {
        add(fApp.fDeleteDialog.sceneRoot(), true);
      }
    }
    fApp.fInputRouter.setLayers(layers);
  }

  bool routePointer(skiff::scene::PointerAction action, float x, float y) {
    if (fApp.fState == State::kPlaying) {
      return false;
    }
    this->refreshInputLayers();
    skiff::scene::PointerEvent event;
    event.fAction = action;
    event.fX = x;
    event.fY = y;
    return fApp.fInputRouter.pointer(event);
  }

  bool routeKey(skiff::scene::Key key, bool shift = false) {
    if (fApp.fState == State::kPlaying) {
      return false;
    }
    this->refreshInputLayers();
    skiff::scene::KeyEvent event;
    event.fKey = key;
    event.fShift = shift;
    return fApp.fInputRouter.key(event);
  }

  bool routeText(std::string_view text) {
    if (fApp.fState == State::kPlaying || text.empty()) {
      return false;
    }
    this->refreshInputLayers();
    skiff::scene::TextInputEvent event;
    event.fText = text;
    event.fCommit = true;
    return fApp.fInputRouter.text(event);
  }

  void clickAt(float x, float y) {
    if (fApp.fScreens.confirmDeleteClick(x, y)) {
      return;
    }
    if (fApp.fOverlays.exportClick(x, y)) {
      return;
    }
    if (fApp.fReplayBrowser.open()) {
      if (fApp.fReplayBrowser.clickOverlay(x, y) ==
          client::ReplayBrowser::OverlayAction::kExport) {
        fApp.fScreens.exportSelectedReplay();
      }
      return;
    }
    // An overlay that is up takes the press whether or not something in it
    // was hit. A click beside it means "close this", not "reach the screen
    // underneath" -- which is what it used to mean, so a stray click next to
    // the settings panel landed in the carousel behind it.
    if (fApp.fSettingsPanel.open()) {
      if (!fApp.fOverlays.settingsClick(x, y, true)) {
        fApp.fOverlays.closeSettings();
      }
      return;
    }
    if (fApp.fModSelect.open()) {
      if (!fApp.fOverlays.modClick(x, y)) {
        fApp.fModSelect.close();
      }
      return;
    }
    switch (fApp.fState) {
    case State::kMainMenu:
      if (const auto requested = fApp.fMainMenu.click(x, y)) {
        fApp.menuAction(*requested);
        return;
      }
      break;
    case State::kSongSelect:
      if (fApp.fScreens.filterClick(x, y, true)) {
        return;
      }
      if (fApp.fScreens.selectFooterClick(x, y)) {
        return;
      }
      if (const auto hit = fApp.fCarousel.click(x, y); hit.fHit) {
        if (hit.fDiff < 0) {
          fApp.fLibrary.selSet() = hit.fSet;
          fApp.fLibrary.selDiff() = 0;
        } else if (fApp.fLibrary.selSet() == hit.fSet &&
                   fApp.fLibrary.selDiff() == hit.fDiff) {
          fApp.startPlay(hit.fSet, hit.fDiff); // second click plays
        } else {
          fApp.fLibrary.selSet() = hit.fSet;
          fApp.fLibrary.selDiff() = hit.fDiff;
        }
        return;
      }
      break;
    case State::kDownload: {
      if (fApp.fSetPage.open()) {
        const auto page = fApp.fSetPage.click(x, y);
        using PageAction = client::setpage::SetPage::Action;
        if (page.fAction == PageAction::kDownload) {
          fApp.fMirrors.startDownloadForSet(fApp.fSetPage.setId());
        } else if (page.fAction == PageAction::kPreview) {
          fApp.fMirrors.togglePreviewForSet(fApp.fSetPage.setId());
        } else if (page.fAction == PageAction::kClose) {
          this->closeSetPage();
        }
        return; // the page covers the listing underneath
      }
      const auto result = fApp.fListing.click(x, y);
      switch (result.fAction) {
      case client::listing::Listing::Action::kSearch:
        fApp.fMirrors.startSearch(fApp.fListing.filters());
        break;
      case client::listing::Listing::Action::kDownload:
        fApp.fMirrors.startDownload(result.fIndex);
        break;
      case client::listing::Listing::Action::kOpen:
        if (result.fIndex < fApp.fMirrors.results().size()) {
          fApp.fSetPage.show(fApp.fMirrors.results()[result.fIndex]);
          fApp.fMirrors.requestPageCover(result.fIndex);
        }
        break;
      case client::listing::Listing::Action::kPreview:
        fApp.fMirrors.togglePreview(result.fIndex);
        break;
      case client::listing::Listing::Action::kBack:
        this->leaveDownload();
        break;
      case client::listing::Listing::Action::kRefilter:
      case client::listing::Listing::Action::kNone:
        break;
      }
      return;
    }
    case State::kPaused:
      this->applyPauseAction(fApp.fPauseMenu.click(x, y));
      break;
    case State::kResults:
      this->applyResultAction(fApp.fReplayBrowser.clickResultAction(x, y));
      break;
    default:
      break;
    }
  }

  void applyPauseAction(client::pause::PauseMenu::Action action) {
    using Action = client::pause::PauseMenu::Action;
    switch (action) {
    case Action::kContinue:
      fApp.resumeGame();
      break;
    case Action::kRetry:
      fApp.retry();
      break;
    case Action::kQuit:
      fApp.quitToSelect();
      break;
    case Action::kNone:
      break;
    }
  }

  void applyResultAction(client::results::Action action) {
    using Action = client::results::Action;
    switch (action) {
    case Action::kRetry:
      fApp.retry();
      break;
    case Action::kBack:
      fApp.quitToSelect();
      break;
    case Action::kExport:
      fApp.fExportDialog.show();
      fApp.fReplayBrowser.close(); // one overlay at a time
      break;
    case Action::kToggleRules:
      if (fApp.fReplayBrowser.toggleRules()) {
        fApp.fView.invalidate();
      }
      break;
    case Action::kNone:
      break;
    }
  }

  static void appendUtf8(std::string &out, std::uint32_t cp) {
    if (cp < 0x20) {
      return; // control chars
    }
    if (cp < 0x80) {
      out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
      out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
      out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
      out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
      out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
      out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
      out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
  }

  static void popUtf8(std::string &out) {
    while (!out.empty()) {
      const auto c = static_cast<unsigned char>(out.back());
      out.pop_back();
      if ((c & 0xC0) != 0x80) {
        break; // removed the lead byte
      }
    }
  }

  // Every physical press attempts a hit (each of Z/X/M1/M2 is an independent
  // trigger, which is what alternating taps need); a release is forwarded to
  // the engine only when the *last* held trigger goes up, so slider tracking
  // survives overlapped taps. The old code collapsed all triggers into one
  // boolean, losing presses that overlapped a held key.
  void applyButton(std::uint32_t bit, bool down, double wallMs) {
    const double t = this->eventGameTime(wallMs);
    if (down) {
      fApp.fHeldMask |= bit;
      this->submitTimed({t, fApp.fCursor, osu::InputAction::kPress});
    } else {
      fApp.fHeldMask &= ~bit;
      if (fApp.fHeldMask == 0) {
        this->submitTimed({t, fApp.fCursor, osu::InputAction::kRelease});
      }
    }
  }

  void submitTimed(const osu::InputEvent &ev) {
    // Guard: input events can be drained in a frame where the engine isn't
    // live yet (a queued press arriving during a state transition, before
    // startGameplay populated fApp.fPlay.fEngine). Dereferencing the empty optional
    // was the SIGILL in applyButton.
    if (!fApp.fPlay.fEngine) {
      return;
    }
    fApp.fPlay.fEngine->submit(ev);
    // Always record: the events are a few bytes each, and the autosave
    // setting decides afterwards whether they are kept. Gating the recording
    // itself on --record meant automatic replays were always empty.
    fApp.fPlay.fRecordedEvents.push_back(ev);
  }

private:
  using State = typename Host::State;
  Host &fApp;
};

} // namespace client
