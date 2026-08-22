export module client.mainmenu;

import std;
import skia;
import skiff.paint;
import skiff.scene;
import client.logo;
import client.spectrum;
import client.timing;
import client.triangles;

// osu!lazer's MainMenu as a scene tree: the background, the row of
// MainMenuButtons and the OsuLogo over them.
//
// The seam is the one the carousel and the pause overlay use. What a piece
// looks like stays with the client -- the artwork, the sheared wedges, the
// logo with its visualiser and its triangles are all drawn by the code that
// already draws them, 1:1 with lazer. What lives here is where each piece is,
// in what order they are drawn, which one the pointer is on, and -- the part
// that was hand-rolled per element before -- what has to be repainted when
// any of that changes.
// The framework lives in skiff:: now; these keep the screens below
// writing scene:: and nodes:: as they did when it sat in client::.
namespace paint = skiff::paint;
namespace scene = skiff::scene;

export namespace client::mainmenu {

using Paint = std::function<void(skia::SkCanvas *, const skia::SkRect &, int)>;

// ---- Button system (port of lazer's ButtonSystem) ----------------------
//
// lazer models the menu as a small state machine (Initial -> TopLevel ->
// a submenu), with every button owning an expand/contract animation and
// the logo sliding aside to make room. Same structure here.
enum class State : std::uint8_t { kInitial, kTopLevel, kPlay };

enum class Action : std::uint8_t {
  kOpenPlay,
  kBrowse,
  kImport,
  kExit,
  kSolo,
  kRandom,
  kBack,
  kSettings,
};

struct Btn {
  std::string fLabel;
  std::string fGlyph; // drawn as a text glyph; no icon font here
  skia::SkColor fColor{};
  Action fAction{};
  State fVisible{};       // menu state this button belongs to
  bool fLeftSide = false; // back button sits left of the logo, as in lazer
  // How far out, how hovered and how freshly clicked live on the node that
  // draws the button, which is what asks for the frames they need.
  skia::SkRect fRect = skia::SkRect::MakeEmpty();
};

// Where the menu wants the logo; the logo eases towards it and owns
// everything else about itself.
struct LogoTarget {
  float fX = 0.0f, fY = 0.0f, fScale = 1.0f;
};

[[nodiscard]] inline const char *stateName(State st) {
  switch (st) {
  case State::kInitial:
    return "initial";
  case State::kTopLevel:
    return "top-level";
  case State::kPlay:
    return "play";
  }
  return "?";
}

// The row itself. ButtonArea's leftmost entry is Settings, present at the
// top level; the back button sits left of the logo in a submenu. Colours
// are ButtonSystem's.
[[nodiscard]] inline std::vector<Btn> defaultButtons() {
  std::vector<Btn> btns;
  Btn settings{"settings", "⚙", skia::colorSetARGB(255, 85, 85, 85),
               Action::kSettings, State::kTopLevel};
  settings.fLeftSide = true;
  btns.push_back(std::move(settings));

  btns.push_back({"play", "▶", skia::colorSetARGB(255, 102, 68, 204),
                  Action::kOpenPlay, State::kTopLevel});
  btns.push_back({"browse", "↓", skia::colorSetARGB(255, 165, 204, 0),
                  Action::kBrowse, State::kTopLevel});
  btns.push_back({"import", "+", skia::colorSetARGB(255, 238, 170, 0),
                  Action::kImport, State::kTopLevel});
  btns.push_back({"exit", "×", skia::colorSetARGB(255, 238, 51, 153),
                  Action::kExit, State::kTopLevel});

  btns.push_back({"solo", "●", skia::colorSetARGB(255, 102, 68, 204),
                  Action::kSolo, State::kPlay});
  btns.push_back({"random", "↻", skia::colorSetARGB(255, 94, 63, 186),
                  Action::kRandom, State::kPlay});

  Btn back{"back", "←", skia::colorSetARGB(255, 51, 58, 94), Action::kBack,
           State::kPlay};
  back.fLeftSide = true;
  btns.push_back(std::move(back));
  return btns;
}

class Menu {
public:
  // -1 is nothing, -2 is the logo, anything else is a button's index.
  static constexpr int kNothing = -1;
  static constexpr int kLogo = -2;

  void setPainters(Paint background, Paint button, Paint logo) {
    fBackground = std::move(background);
    fButton = std::move(button);
    fLogo = std::move(logo);
  }

  // Rebuilt when the number of buttons changes, which happens once.
  void ensure(std::size_t buttons, const skia::SkRect &screen) {
    if (fScene && fButtons.size() == buttons) {
      fRoot->place(screen);
      return;
    }
    fButtons.clear();
    auto root = std::make_unique<RootNode>();
    fRoot = root.get();
    fRoot->place(screen);

    auto background = std::make_unique<PieceNode>(this, Piece::kBackground, 0);
    fBackgroundNode = background.get();
    fBackgroundNode->place(screen);
    root->add(std::move(background));

    for (std::size_t i = 0; i < buttons; ++i) {
      auto node =
          std::make_unique<PieceNode>(this, Piece::kButton, static_cast<int>(i));
      fButtons.push_back(node.get());
      root->add(std::move(node));
    }

    // Last, so it draws over the buttons and is asked about clicks first --
    // which is what the logo sitting in the middle of the row requires.
    auto logo = std::make_unique<PieceNode>(this, Piece::kLogo, 0);
    fLogoNode = logo.get();
    root->add(std::move(logo));
    fScene = std::move(root);
  }

  // Where each piece ended up. The node marks what has to be repainted when
  // its box moves; the client says separately when what it draws inside an
  // unchanged box has changed.
  void placeButton(std::size_t index, const skia::SkRect &box) {
    if (index < fButtons.size()) {
      fButtons[index]->place(box);
    }
  }
  void placeLogo(const skia::SkRect &box) {
    if (fLogoNode != nullptr) {
      fLogoNode->place(box);
    }
  }
  // The three values a button is drawn from, eased here rather than by the
  // client: a node that is part-way to somewhere is the one thing the client
  // could not tell the frame loop, because it reads as damage that has
  // already been consumed.
  //
  // Driven in the order the layout needs them -- how far out a button is
  // decides how wide it is, how wide it is decides whether the pointer is on
  // it, and that decides where the hover is going.
  float easeExpand(std::size_t index, float target, double dtMs) {
    return index < fButtons.size()
               ? fButtons[index]->ease(0, target, 95.0f, dtMs)
               : 0.0f;
  }
  float easeHover(std::size_t index, float target, double dtMs) {
    return index < fButtons.size()
               ? fButtons[index]->ease(1, target, 110.0f, dtMs)
               : 0.0f;
  }
  float decayFlash(std::size_t index, double dtMs) {
    return index < fButtons.size()
               ? fButtons[index]->ease(2, 0.0f, 160.0f, dtMs)
               : 0.0f;
  }
  void flashButton(std::size_t index) {
    if (index < fButtons.size()) {
      fButtons[index]->set(2, 1.0f);
    }
  }
  [[nodiscard]] float buttonExpand(std::size_t index) const {
    return index < fButtons.size() ? fButtons[index]->value(0) : 0.0f;
  }
  [[nodiscard]] float buttonHover(std::size_t index) const {
    return index < fButtons.size() ? fButtons[index]->value(1) : 0.0f;
  }
  [[nodiscard]] float buttonFlash(std::size_t index) const {
    return index < fButtons.size() ? fButtons[index]->value(2) : 0.0f;
  }

  void markButton(std::size_t index) {
    if (index < fButtons.size()) {
      fButtons[index]->markDamaged();
    }
  }
  void markLogo() {
    if (fLogoNode != nullptr) {
      fLogoNode->markDamaged();
    }
  }
  void markBackground() {
    if (fBackgroundNode != nullptr) {
      fBackgroundNode->markDamaged();
    }
  }

  void render(skia::SkCanvas *canvas) {
    if (fScene && canvas != nullptr) {
      fScene->draw(canvas);
    }
  }

  [[nodiscard]] skiff::scene::FrameResult finishFrame() {
    return fScene ? fScene->finishFrame() : skiff::scene::FrameResult{};
  }

  // Hit testing walks the tree from the front, so the logo answers before the
  // buttons it overlaps.
  [[nodiscard]] int hit(float x, float y) {
    fPending = kNothing;
    if (fScene) {
      fScene->click(x, y);
    }
    return fPending;
  }

  void setPointer(float x, float y) {
    if (fScene) {
      fScene->setHover(x, y);
    }
  }

private:
  enum class Piece : std::uint8_t { kBackground, kButton, kLogo };

  // Placed by the client rather than by the layout system: the menu's
  // geometry is a row of parallelograms around a circle, which is arithmetic
  // rather than a flow.
  class PieceNode : public scene::Drawable {
  public:
    PieceNode(Menu *owner, Piece piece, int index)
        : fOwner(owner), fPiece(piece), fIndex(index) {}

    // One of expand, hover and flash. Kept together because they are the same
    // thing three times and a name each would be three copies of this.
    float ease(int which, float target, float tauMs, double dtMs) {
      fTarget[which] = target;
      const float previous = fValue[which];
      fValue[which] = paint::approach(fValue[which], target, tauMs, dtMs);
      if (std::abs(fValue[which] - previous) > 0.0f) {
        this->markDamaged();
      }
      return fValue[which];
    }
    void set(int which, float value) {
      fValue[which] = value;
      this->markDamaged();
    }
    [[nodiscard]] float value(int which) const { return fValue[which]; }

    void place(const skia::SkRect &box) {
      if (box == fBounds) {
        return;
      }
      this->markDamaged(); // where it was
      fBounds = box;
      this->markDamaged(); // and where it is now
    }

  protected:
    void drawSelf(skia::SkCanvas *canvas, float) override {
      switch (fPiece) {
      case Piece::kBackground:
        if (fOwner->fBackground) {
          fOwner->fBackground(canvas, fBounds, 0);
        }
        break;
      case Piece::kButton:
        if (fOwner->fButton) {
          fOwner->fButton(canvas, fBounds, fIndex);
        }
        break;
      case Piece::kLogo:
        if (fOwner->fLogo) {
          fOwner->fLogo(canvas, fBounds, 0);
        }
        break;
      }
    }

    // Part-way to somewhere, which is what the frame loop cannot work out
    // from damage: damage says what changed, and this says the next frame
    // will differ too.
    bool settling() const override {
      for (int i = 0; i < 3; ++i) {
        if (std::abs(fValue[i] - fTarget[i]) > scene::kSettled) {
          return true;
        }
      }
      return false;
    }

    bool acceptsInput() const override { return fPiece != Piece::kBackground; }

    // The client draws its own hover states out of its own eased values and
    // says when they moved; the pointer crossing a box is not itself news.
    bool hoverChangesAppearance() const override { return false; }

    bool onClick(float, float) override {
      fOwner->fPending = fPiece == Piece::kLogo ? kLogo : fIndex;
      return true;
    }

  private:
    std::array<float, 3> fValue{}; // expand, hover, flash
    std::array<float, 3> fTarget{};
    Menu *fOwner;
    Piece fPiece;
    int fIndex;
  };

  class RootNode : public scene::Drawable {
  public:
    void place(const skia::SkRect &box) { fBounds = box; }

  protected:
    void layoutChildren() override {} // every piece is placed by hand
  };

  Paint fBackground;
  Paint fButton;
  Paint fLogo;
  std::unique_ptr<scene::Drawable> fScene;
  RootNode *fRoot = nullptr;
  PieceNode *fBackgroundNode = nullptr;
  PieceNode *fLogoNode = nullptr;
  std::vector<PieceNode *> fButtons;
  int fPending = kNothing;
};


// The main menu itself: which level it is on, where the logo is going, the
// row of buttons and the dim behind them. What the client kept for it, minus
// the artwork -- that belongs to whoever owns the beatmap.
class Screen {
public:
  // The keys the menu answers to, named rather than numbered: which integer
  // the window system uses for each is not this module's business, and no
  // other client module depends on the windowing library.
  enum class Key : std::uint8_t {
    kOther,
    kEscape,
    kEnter,
    kSpace,
    kP,
    kB,
    kD,
    kI,
    kQ,
    kS,
    kR,
  };

  // What the screen is told each frame and cannot work out for itself.
  struct Ctx {
    float fWidth = 0.0f;
    float fHeight = 0.0f;
    float fMouseX = 0.0f;
    float fMouseY = 0.0f;
    // A modal surface above the menu owns the pointer while it covers it.
    // Keep the menu's last pointer in that case: clearing hover underneath a
    // panel repaints a state the user cannot see.
    bool fPointerActive = true;
    double fDtMs = 16.0;
    double fNowWall = 0.0;
    skia::SkFont *fFont = nullptr;
    bool fVisualiser = true;   // the logo's bars follow the music
    bool fTriangles = true;    // the ones behind it, and inside it, drift
    bool fHasArtwork = false;  // the client has a background to draw
    bool fLibraryEmpty = true; // no beatmaps at all: lazer's own backdrop
    bool fAudioPlaying = false;
    std::span<const std::int16_t> fSamples;
    int fSampleRate = 0;
    // Asked for rather than handed over, because it is only wanted every
    // quarter second: reading the position off the audio device costs whole
    // milliseconds per call, and the visualiser runs on an anchored clock
    // between reads precisely so that it is not paid per frame.
    std::function<double()> fAudioPositionMs;
  };

  // The artwork is drawn by the client, which owns the beatmap and the view
  // that scales it. Everything else the screen draws itself.
  void setArtworkPainter(std::function<void(skia::SkCanvas *)> paint) {
    fArtwork = std::move(paint);
  }

  void update(const Ctx &input) {
    Ctx ctx = input;
    if (!ctx.fPointerActive && fHaveLast) {
      ctx.fMouseX = fLast.fMouseX;
      ctx.fMouseY = fLast.fMouseY;
    }
    this->ensurePainters();
    this->ensureButtons();
    this->updateSpectrum(ctx);
    fLast = ctx;
    fHaveLast = true;
    fLast.fSamples = {}; // borrowed for this call only
    fLast.fAudioPositionMs = {};

    const float sw = ctx.fWidth;
    const float sh = ctx.fHeight;

    const float dimTarget = fState == State::kInitial ? 1.0f : 0.8f;
    fDim = paint::approach(fDim, dimTarget, 220.0f, ctx.fDtMs);

    // What moves here is the logo with its visualiser and the buttons; the
    // background is the beatmap's artwork, which sits still. The dim fade and
    // the triangle fallback do cover the screen, so those say so.
    // Compared with a tolerance: an eased value that has settled must stop
    // counting as a change, or this fires on every frame for ever.
    if (std::abs(fDim - fDrawnDim) > 0.001f) {
      fFullDamage = "menu dim";
    } else if (!ctx.fHasArtwork && ctx.fLibraryEmpty && ctx.fTriangles) {
      fFullDamage = "triangle background, drifting";
    }
    fDrawnDim = fDim;

    // ---- Layout: logo plus the visible button row, centred as a group.
    const float uiScale = std::clamp(sh / 900.0f, 0.75f, 1.6f);
    const float btnW = 150.0f * uiScale;
    const float btnH = 96.0f * uiScale;
    const float btnGap = 6.0f * uiScale;
    fWedge = 20.0f * uiScale; // lazer's parallelogram shear

    int rightCount = 0;
    int leftCount = 0;
    for (const auto &b : fBtns) {
      if (b.fVisible != fState) {
        continue;
      }
      if (b.fLeftSide) {
        ++leftCount;
      } else {
        ++rightCount;
      }
    }

    fLogoBase = std::min(sw, sh) * 0.17f;
    const float logoR = fLogoBase * (fState == State::kInitial ? 1.0f : 0.62f);
    const float rightW = static_cast<float>(rightCount) * (btnW + btnGap);
    const float leftW = static_cast<float>(leftCount) * (btnW + btnGap);
    const float groupW = leftW + 2.0f * logoR + 28.0f * uiScale + rightW;

    const float targetLogoX = fState == State::kInitial
                                  ? sw * 0.5f
                                  : (sw - groupW) * 0.5f + leftW + logoR;
    const float targetLogoY = sh * (fState == State::kInitial ? 0.46f : 0.5f);
    const float targetScale = fState == State::kInitial ? 1.0f : 0.62f;

    fLogoTarget = {targetLogoX, targetLogoY, targetScale};
    fLogo.moveTowards(this->logoCtx(ctx));

    // ---- Buttons: animate and lay out, without drawing any of them.
    const float logoReach = fLogoBase * fLogo.scale();
    const float rowY = fLogo.y() - btnH * 0.5f;
    float xRight = fLogo.x() + logoReach + 28.0f * uiScale;
    float xLeft = fLogo.x() - logoReach - 28.0f * uiScale;

    fMenu.ensure(fBtns.size(), skia::SkRect::MakeWH(sw, sh));

    // The eased values live on the button's node: how far out it is, whether
    // the pointer is on it, and the click flash. Driven here because the
    // order matters -- how far out decides how wide, how wide decides whether
    // the pointer is on it, and that decides where the hover is going.
    for (std::size_t i = 0; i < fBtns.size(); ++i) {
      auto &b = fBtns[i];
      const bool visible = b.fVisible == fState;
      const float expand =
          fMenu.easeExpand(i, visible ? 1.0f : 0.0f, ctx.fDtMs);
      fMenu.decayFlash(i, ctx.fDtMs);

      if (expand < 0.01f) {
        b.fRect = skia::SkRect::MakeEmpty();
        continue;
      }

      const float w = btnW * expand * (1.0f + 0.18f * fMenu.buttonHover(i));
      skia::SkRect rect;
      if (b.fLeftSide) {
        rect = skia::SkRect::MakeXYWH(xLeft - w, rowY, w, btnH);
        xLeft -= w + btnGap;
      } else {
        rect = skia::SkRect::MakeXYWH(xRight, rowY, w, btnH);
        xRight += w + btnGap;
      }
      b.fRect = rect;

      const bool hovered = visible && rect.contains(ctx.fMouseX, ctx.fMouseY);
      fMenu.easeHover(i, hovered ? 1.0f : 0.0f, ctx.fDtMs);
    }

    // The dim and the logo are the client's, not a node's: one covers the
    // screen and the other is drawn by the piece it sits in.
    constexpr float kMoving = 0.002f;
    fLogo.settle(this->logoCtx(ctx));
    fMoving = std::abs(fDim - dimTarget) > kMoving ||
              fLogo.moving(this->logoCtx(ctx));

    fMenu.setPointer(ctx.fMouseX, ctx.fMouseY);

    // How far the bars actually reach this frame. A flat guess of three
    // quarters of the logo's width was covering 40% of the screen on its own,
    // which with the counter in the opposite corner pushed the frame over the
    // "repaint it whole" threshold and saved nothing at all.
    const float reach = fLogo.reach(this->logoCtx(ctx));
    skia::SkRect moving = fLogo.bounds();
    moving.outset(reach + 4.0f, reach + 4.0f);
    // Marked while something in there is actually moving -- a live
    // visualiser, drifting triangles inside the logo, or the logo itself
    // having shifted. Marking it every frame regardless is a repaint of the
    // busiest part of the screen for a picture that is identical.
    fMenu.placeLogo(moving);
    if (ctx.fTriangles || ctx.fVisualiser) {
      fMenu.markLogo(); // something inside the same box is moving
    }

    // A button only needs repainting while something about it changes. Its
    // drawn shape is a parallelogram sheared by the wedge, and the label and
    // glow reach past that, so the marked area is its rectangle grown by the
    // shear plus a margin -- and by the rectangle it occupied before, or a
    // button that moved leaves its old self behind.
    for (std::size_t i = 0; i < fBtns.size(); ++i) {
      skia::SkRect box = fBtns[i].fRect;
      if (!box.isEmpty()) {
        box.outset(fWedge + 12.0f, 12.0f);
      }
      fMenu.placeButton(i, box);
      // What moved inside the box is the node's own business: it eased the
      // value and marked itself. What is left here is the box.
    }
  }

  void render(skia::SkCanvas *canvas) { fMenu.render(canvas); }

  // Marked while the screen updated. The rectangle is what to repaint; the
  // reason is non-null when the whole screen changed and says why.
  [[nodiscard]] skiff::scene::FrameResult finishFrame() {
    auto result = fMenu.finishFrame();
    result.fWantsAnotherFrame = result.fWantsAnotherFrame || fMoving;
    return result;
  }
  [[nodiscard]] const char *takeFullDamage() {
    return std::exchange(fFullDamage, nullptr);
  }

  // Returning to the menu always lands on the top level, never on a stale
  // submenu, and the logo re-eases into place from where it was.
  void enterTopLevel() { this->setState(State::kTopLevel); }

  // A new track: the analysis clock is anchored to the old one until reset.
  void trackChanged(double nowWall) {
    fClock.reset(nowWall, 0.0);
    fClockSyncWall = std::numeric_limits<double>::lowest();
    fLogo.spectrum().reset();
  }
  void stopped() { fLogo.spectrum().reset(); }

  // The pointer. Nothing happens for the two buttons that only move the menu
  // between its own levels; the rest are the client's to carry out.
  [[nodiscard]] std::optional<Action> click(float x, float y) {
    this->ensureButtons();
    const int piece = fMenu.hit(x, y);
    if (piece == Menu::kLogo) {
      // The logo's box is square and reaches as far as its visualiser does,
      // and the logo is a circle inside it -- so the circle has the last
      // word, and a miss falls through to the buttons the box covers.
      const float dx = x - fLogo.bounds().centerX();
      const float dy = y - fLogo.bounds().centerY();
      const float r = fLogo.bounds().width() * 0.5f;
      if (r > 0.0f && dx * dx + dy * dy <= r * r) {
        return this->triggerLogo();
      }
      for (std::size_t i = 0; i < fBtns.size(); ++i) {
        if (fBtns[i].fVisible == fState && fMenu.buttonExpand(i) > 0.5f &&
            fBtns[i].fRect.contains(x, y)) {
          return this->trigger(i);
        }
      }
      return std::nullopt;
    }
    if (piece >= 0 && piece < static_cast<int>(fBtns.size())) {
      const auto index = static_cast<std::size_t>(piece);
      if (fBtns[index].fVisible == fState && fMenu.buttonExpand(index) > 0.5f) {
        return this->trigger(index);
      }
    }
    return std::nullopt;
  }

  [[nodiscard]] std::optional<Action> key(Key key) {
    this->ensureButtons();
    if (key == Key::kEscape) {
      switch (fState) {
      case State::kPlay:
        this->setState(State::kTopLevel);
        return std::nullopt;
      case State::kTopLevel:
        this->setState(State::kInitial);
        return std::nullopt;
      case State::kInitial:
        return Action::kExit;
      }
      return std::nullopt;
    }
    if (key == Key::kEnter || key == Key::kSpace) {
      return this->triggerLogo();
    }
    // Letter shortcuts, as in lazer.
    if (fState == State::kTopLevel) {
      if (key == Key::kP) {
        return this->fire(Action::kOpenPlay);
      }
      if (key == Key::kB || key == Key::kD) {
        return this->fire(Action::kBrowse);
      }
      if (key == Key::kI) {
        return this->fire(Action::kImport);
      }
      if (key == Key::kQ) {
        return this->fire(Action::kExit);
      }
    } else if (fState == State::kPlay) {
      if (key == Key::kS) {
        return this->fire(Action::kSolo);
      }
      if (key == Key::kR) {
        return this->fire(Action::kRandom);
      }
    }
    return std::nullopt;
  }

private:
  // How often the visualiser's clock is re-anchored against the device.
  static constexpr double kClockSyncIntervalMs = 250.0;

  void ensureButtons() {
    if (fBtns.empty()) {
      fBtns = defaultButtons();
    }
  }

  void ensurePainters() {
    if (fPainted) {
      return;
    }
    fPainted = true;
    // The tree owns where the pieces are and what has to be repainted; what
    // they look like is here.
    fMenu.setPainters(
        [this](skia::SkCanvas *canvas, const skia::SkRect &, int) {
          this->drawBackground(canvas);
        },
        [this](skia::SkCanvas *canvas, const skia::SkRect &, int index) {
          if (index >= 0 && index < static_cast<int>(fBtns.size())) {
            this->drawButton(canvas, static_cast<std::size_t>(index));
          }
        },
        [this](skia::SkCanvas *canvas, const skia::SkRect &, int) {
          fLogo.draw(canvas, this->logoCtx(fLast));
        });
  }

  void setState(State st) {
    if (fState == st) {
      return;
    }
    std::println(std::cerr, "[menu] {} -> {}", stateName(fState),
                 stateName(st));
    fState = st;
  }

  // What the logo is told each frame. Nothing it could work out itself.
  [[nodiscard]] client::logo::Logo::Ctx logoCtx(const Ctx &ctx) const {
    return {ctx.fFont,       ctx.fMouseX,       ctx.fMouseY, ctx.fDtMs,
            ctx.fVisualiser, ctx.fTriangles,    fLogoBase,   fLogoTarget.fX,
            fLogoTarget.fY,  fLogoTarget.fScale};
  }

  [[nodiscard]] std::optional<Action> trigger(std::size_t index) {
    const Action action = fBtns[index].fAction;
    fMenu.flashButton(index);
    switch (action) {
    case Action::kOpenPlay:
      this->setState(State::kPlay);
      return std::nullopt;
    case Action::kBack:
      this->setState(State::kTopLevel);
      return std::nullopt;
    default:
      return action;
    }
  }

  // The logo is the menu's primary control: clicking advances a level, and at
  // a populated level it triggers that level's first button (onOsuLogo).
  [[nodiscard]] std::optional<Action> triggerLogo() {
    fLogo.strike();
    switch (fState) {
    case State::kInitial:
      this->setState(State::kTopLevel);
      return std::nullopt;
    case State::kTopLevel:
    case State::kPlay:
      for (std::size_t i = 0; i < fBtns.size(); ++i) {
        if (fBtns[i].fVisible == fState && !fBtns[i].fLeftSide) {
          return this->trigger(i);
        }
      }
      break;
    }
    return std::nullopt;
  }

  [[nodiscard]] std::optional<Action> fire(Action action) {
    for (std::size_t i = 0; i < fBtns.size(); ++i) {
      if (fBtns[i].fVisible == fState && fBtns[i].fAction == action) {
        return this->trigger(i);
      }
    }
    return std::nullopt;
  }

  void updateSpectrum(const Ctx &ctx) {
    if (!ctx.fAudioPlaying || !ctx.fAudioPositionMs) {
      fLogo.spectrum().update({}, 0, 0.0, ctx.fNowWall);
      return;
    }
    // Same anchored-clock trick as gameplay: querying the device every frame
    // is what used to cost whole milliseconds per call.
    if (ctx.fNowWall - fClockSyncWall >= kClockSyncIntervalMs) {
      fClockSyncWall = ctx.fNowWall;
      const double devicePos = ctx.fAudioPositionMs();
      // A looping track jumps back to zero; the anchored clock is monotonic
      // by design, so detect the wrap and re-anchor instead of syncing.
      if (devicePos + 500.0 < fLastPosMs) {
        fClock.reset(ctx.fNowWall, devicePos);
      } else {
        fClock.sync(ctx.fNowWall, devicePos);
      }
      fLastPosMs = devicePos;
    }
    const double posMs = fClock.sample(ctx.fNowWall);
    fLogo.spectrum().update(ctx.fSamples, ctx.fSampleRate, posMs / 1000.0,
                            ctx.fNowWall);
  }

  // lazer's main menu shows the beatmap background at full brightness --
  // MainMenu.cs fades it to Gray(1) at the logo-only state and Gray(0.8)
  // once the buttons are out. There is no triangle overlay over artwork;
  // triangles are only the fallback background when no art exists at all.
  void drawBackground(skia::SkCanvas *canvas) {
    const float sw = fLast.fWidth;
    const float sh = fLast.fHeight;
    if (fLast.fHasArtwork) {
      if (fArtwork) {
        fArtwork(canvas);
      }
      if (fDim < 0.999f) {
        skia::SkPaint dim;
        dim.setColor(skia::colorSetARGB(
            static_cast<std::uint8_t>((1.0f - fDim) * 255.0f), 0, 0, 0));
        canvas->drawRect(skia::SkRect::MakeXYWH(0, 0, sw, sh), dim);
      }
      return;
    }
    if (fLast.fLibraryEmpty) {
      // Only with no beatmaps at all does lazer's default (triangle)
      // background show; while artwork is still loading, stay dark.
      canvas->clear(skia::colorSetARGB(255, 32, 24, 44));
      // client.triangles is the port of TrianglesV2, and this is the same
      // field the pause buttons and the logo use.
      fBackdrop.setScaleAdjust(2.4f);
      fBackdrop.setAlphaRange(0.06f, 0.16f);
      fBackdrop.draw(canvas, skia::SkRect::MakeWH(sw, sh),
                     fLast.fTriangles ? fLast.fDtMs : 0.0, 1.0f);
      return;
    }
    canvas->clear(skia::colorSetARGB(255, 18, 14, 24));
  }

  // A lazer menu button is a parallelogram: vertical edges sheared by a fixed
  // wedge width, label under a glyph, both fading in only once the button is
  // most of the way open (lazer clamps content alpha the same way).
  void drawButton(skia::SkCanvas *canvas, std::size_t index) {
    const Btn &b = fBtns[index];
    const float expandWeight = fMenu.buttonExpand(index);
    const float hoverWeight = fMenu.buttonHover(index);
    const float flashWeight = fMenu.buttonFlash(index);
    const skia::SkRect &r = b.fRect;
    skia::SkPathBuilder shape;
    shape.moveTo(r.fLeft + fWedge, r.fTop);
    shape.lineTo(r.fRight + fWedge, r.fTop);
    shape.lineTo(r.fRight, r.fBottom);
    shape.lineTo(r.fLeft, r.fBottom);
    shape.close();
    const auto path = shape.detach();

    skia::SkPaint fill;
    fill.setAntiAlias(true);
    fill.setColor(b.fColor);
    // Hover brightens; the click flash blows it out briefly, then decays.
    fill.setAlphaf(
        std::clamp(expandWeight * (0.86f + 0.14f * hoverWeight), 0.0f, 1.0f));
    canvas->drawPath(path, fill);

    if (flashWeight > 0.01f) {
      skia::SkPaint flashPaint;
      flashPaint.setAntiAlias(true);
      flashPaint.setColor(skia::kWhite);
      flashPaint.setAlphaf(0.55f * flashWeight);
      flashPaint.setBlendMode(skia::SkBlendMode::kPlus);
      canvas->drawPath(path, flashPaint);
    }

    const float contentAlpha =
        std::clamp((expandWeight - 0.5f) / 0.3f, 0.0f, 1.0f);
    if (contentAlpha <= 0.0f || fLast.fFont == nullptr) {
      return;
    }
    const paint::Painter p(canvas, *fLast.fFont);
    const float cx = r.centerX() + fWedge * 0.5f;
    // Icon lifts slightly on hover, mirroring lazer's bouncing icon.
    const float lift = 6.0f * hoverWeight;
    p.textCentered(b.fGlyph, cx, r.centerY() - lift, 30.0f, skia::kWhite,
                   contentAlpha);
    p.textCentered(b.fLabel, cx, r.fBottom - 18.0f, 17.0f, skia::kWhite,
                   contentAlpha * 0.95f);
  }

  Menu fMenu;               // where the pieces are, and what moved
  client::logo::Logo fLogo; // and the one in the middle of them
  std::vector<Btn> fBtns;
  State fState = State::kInitial;
  LogoTarget fLogoTarget;
  float fLogoBase = 0.0f; // unscaled radius for this screen size
  float fWedge = 20.0f;   // the shear on the parallelograms
  float fDim = 1.0f;
  float fDrawnDim = -1.0f; // the dim the screen currently shows
  bool fMoving = false;
  const char *fFullDamage = nullptr;
  client::triangles::Field fBackdrop;
  client::AnchoredClock fClock;
  double fClockSyncWall = std::numeric_limits<double>::lowest();
  double fLastPosMs = 0.0;
  std::function<void(skia::SkCanvas *)> fArtwork;
  Ctx fLast;
  bool fHaveLast = false;
  bool fPainted = false;
};

} // namespace client::mainmenu
