export module client.mainmenu;

import std;
import skia;
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

    bool acceptsInput() const override { return fPiece != Piece::kBackground; }

    // The client draws its own hover states out of its own eased values and
    // says when they moved; the pointer crossing a box is not itself news.
    bool hoverChangesAppearance() const override { return false; }

    bool onClick(float, float) override {
      fOwner->fPending = fPiece == Piece::kLogo ? kLogo : fIndex;
      return true;
    }

  private:
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
