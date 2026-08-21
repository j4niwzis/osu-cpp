export module client.mainmenu;

import std;
import skia;
import skiff.paint;
import skiff.scene;

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

  // Anything in the menu still on its way somewhere.
  [[nodiscard]] bool animating() const {
    return fScene && fScene->animatingTree();
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

  [[nodiscard]] skia::SkRect takeDamage() {
    return fScene ? fScene->takeDamage() : skia::SkRect::MakeEmpty();
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

} // namespace client::mainmenu
