export module client.skindialog;

import std;
import skia;
import skiff.scene;
import skiff.nodes;
import skiff.widgets.button;
import client.http;
import client.palette;

export namespace client {

namespace scene = skiff::scene;
namespace nodes = skiff::nodes;

namespace skin_dialog_style {
struct Root;
struct Panel;
struct Title;
struct Text;
struct Status;
struct Scroll;
struct LicenceColumn;
struct ScrollTrack;
struct ScrollThumb;
struct Buttons;
struct Button;
} // namespace skin_dialog_style

struct SkinDialogTheme {
  static constexpr auto styles =
      scene::makeStyleSheet()
          .rule(scene::select<nodes::Box, skin_dialog_style::Root>(),
                {.width = 1.0f,
                 .height = 1.0f,
                 .relativeSize = scene::Axes::kBoth,
                 .backgroundColour = skia::colorSetARGB(220, 8, 6, 12)})
          .rule(scene::select<nodes::Box, skin_dialog_style::Panel>(),
                {.anchor = scene::Anchor::kCentre,
                 .origin = scene::Anchor::kCentre,
                 .width = 680.0f,
                 .height = 440.0f,
                 .padding = scene::Margin::all(24.0f),
                 .cornerRadius = 14.0f,
                 .backgroundColour = palette::kBackground5})
          .rule(scene::select<nodes::Text, skin_dialog_style::Title>(),
                {.x = 0.0f,
                 .y = 0.0f,
                 .maxWidth = 632.0f,
                 .colour = skia::kWhite,
                 .fontSize = 24.0f,
                 .fontBold = true})
          .rule(scene::select<nodes::Text, skin_dialog_style::Text>(),
                {.x = 0.0f,
                 .maxWidth = 620.0f,
                 .alpha = 0.78f,
                 .colour = skia::kWhite,
                 .fontSize = 14.0f})
          .rule(scene::select<nodes::Text, skin_dialog_style::Status>(),
                {.x = 0.0f,
                 .y = 338.0f,
                 .maxWidth = 632.0f,
                 .colour = palette::kAccent2,
                 .fontSize = 14.0f})
          .rule(scene::select<nodes::ScrollContainer,
                              skin_dialog_style::Scroll>(),
                {.x = 0.0f, .y = 122.0f, .width = 620.0f, .height = 205.0f})
          .rule(scene::select<nodes::FillFlow,
                              skin_dialog_style::LicenceColumn>(),
                {.width = 1.0f,
                 .relativeSize = scene::Axes::kX,
                 .autoSize = scene::Axes::kY,
                 .padding = scene::Margin{0.0f, 12.0f, 8.0f, 0.0f}})
          .rule(scene::select<nodes::Box, skin_dialog_style::ScrollTrack>(),
                {.anchor = scene::Anchor::kTopRight,
                 .origin = scene::Anchor::kTopRight,
                 .x = 0.0f,
                 .y = 122.0f,
                 .width = 5.0f,
                 .height = 205.0f,
                 .cornerRadius = 2.5f,
                 .backgroundColour = skia::colorSetARGB(80, 255, 255, 255)})
          .rule(scene::select<nodes::Box, skin_dialog_style::ScrollThumb>(),
                {.anchor = scene::Anchor::kTopRight,
                 .origin = scene::Anchor::kTopRight,
                 .x = 0.0f,
                 .y = 122.0f,
                 .width = 5.0f,
                 .height = 40.0f,
                 .cornerRadius = 2.5f,
                 .backgroundColour = palette::kAccent2})
          .rule(scene::select<nodes::FillFlow, skin_dialog_style::Buttons>(),
                {.anchor = scene::Anchor::kBottomCentre,
                 .origin = scene::Anchor::kBottomCentre,
                 .autoSize = scene::Axes::kBoth})
          .rule(scene::select<skiff::widgets::Button,
                              skin_dialog_style::Button>(),
                {.width = 290.0f, .height = 48.0f});
};

// Fetches a pinned set of optional artwork into the user's data directory.
// The licence displayed above the buttons is copied from that same revision.
class SkinDownloadDialog {
public:
  explicit SkinDownloadDialog(std::filesystem::path root = {},
                              bool mayOffer = false)
      : fRoot(std::move(root)), fMayOffer(mayOffer) {}

  [[nodiscard]] bool open() const noexcept { return fScene != nullptr; }
  [[nodiscard]] bool takeInstalled() noexcept {
    return std::exchange(fInstalled, false);
  }
  [[nodiscard]] scene::Drawable *sceneRoot() noexcept { return fScene.get(); }

  void showIfNeeded() {
    if (!fMayOffer || std::filesystem::exists(fRoot / ".download-complete") ||
        std::filesystem::exists(fRoot / ".download-declined")) {
      return;
    }
    fScene = this->build();
  }

  void update(skia::SkFont &font, int width, int height, float mouseX,
              float mouseY, double nowMs) {
    if (!fScene)
      return;
    nodes::Text::setFont(&font);
    if (fStatusNode)
      fStatusNode->setText(fStatus);
    fScene->updateTree(nowMs);
    if (fScrollTicks != 0.0f) {
      fScene->scroll(mouseX, mouseY, fScrollTicks);
      fScrollTicks = 0.0f;
    }
    fScene->layoutIfNeeded(skia::SkRect::MakeWH(static_cast<float>(width),
                                                static_cast<float>(height)));
    this->updateScrollThumb();
    fScene->setHover(mouseX, mouseY);
  }

  void render(skia::SkCanvas *canvas) {
    if (fScene && canvas)
      fScene->draw(canvas);
  }

  [[nodiscard]] scene::FrameResult finishFrame() {
    return fScene ? fScene->finishFrame() : scene::FrameResult{};
  }

  void click(float x, float y) {
    if (fScene) {
      fScene->dispatchPointer(scene::PointerAction::kDown, x, y);
      if (std::exchange(fDeclineRequested, false))
        this->finishDecline();
    }
  }

  void scroll(float ticks) noexcept { fScrollTicks += ticks; }

private:
  static constexpr std::string_view kRevision =
      "71012317acc2764045e4f3112501907f877569a6";
  static constexpr std::array<std::string_view, 40> kFiles{
      "sprites/approachcircle.png", "sprites/cursor.png",
      "sprites/disc.png", "sprites/followpoint.png",
      "sprites/hitcircleoverlay.png", "sprites/hpbarleft.png",
      "sprites/hpbarmid.png", "sprites/hpbarright.png",
      "sprites/reversearrow.png", "sprites/ring-glow.png",
      "sprites/sliderb.png", "sprites/sliderfollowcircle.png",
      "sprites/sliderscorepoint.png", "sprites/spinnerbase.png",
      "sprites/spinnerprogress.png", "sprites/spinnertop.png",
      "sprites/score-0.png", "sprites/score-1.png", "sprites/score-2.png",
      "sprites/score-3.png", "sprites/score-4.png", "sprites/score-5.png",
      "sprites/score-6.png", "sprites/score-7.png", "sprites/score-8.png",
      "sprites/score-9.png", "hitsounds/normal-hitnormal.ogg",
      "hitsounds/normal-hitwhistle.ogg", "hitsounds/normal-hitfinish.ogg",
      "hitsounds/normal-hitclap.ogg", "hitsounds/soft-hitnormal.ogg",
      "hitsounds/soft-hitwhistle.ogg", "hitsounds/soft-hitfinish.ogg",
      "hitsounds/soft-hitclap.ogg", "hitsounds/drum-hitnormal.ogg",
      "hitsounds/drum-hitwhistle.ogg", "hitsounds/drum-hitfinish.ogg",
      "hitsounds/drum-hitclap.ogg", "hitsounds/combobreak.ogg",
      "hitsounds/LICENCE.md"};

  [[nodiscard]] static std::string loadLicence() {
    std::vector<std::filesystem::path> candidates;
#ifdef OSU_CLIENT_DATADIR
    candidates.emplace_back(std::filesystem::path(OSU_CLIENT_DATADIR) /
                            "licenses" / "WebOsu-CC-BY-NC-4.0.md");
#endif
#ifdef OSU_CLIENT_SOURCE_ASSETS
    candidates.emplace_back(std::filesystem::path(OSU_CLIENT_SOURCE_ASSETS) /
                            "licenses" / "WebOsu-CC-BY-NC-4.0.md");
#endif
    candidates.emplace_back(std::filesystem::path("assets") / "licenses" /
                            "WebOsu-CC-BY-NC-4.0.md");
    for (const auto &path : candidates) {
      std::ifstream in(path, std::ios::binary);
      if (in) {
        return {std::istreambuf_iterator<char>(in),
                std::istreambuf_iterator<char>()};
      }
    }
    return "LICENCE FILE NOT FOUND";
  }

  static std::unique_ptr<skiff::widgets::Button>
  button(std::string label, std::function<void()> action) {
    auto result = scene::make<skiff::widgets::Button>(
        {.roles = {scene::role<skin_dialog_style::Button>}}, std::move(label),
        std::move(action));
    auto theme = skiff::widgets::theme();
    theme.fSurface = palette::kCardBg;
    theme.fSurfaceHover = palette::kBackground4;
    theme.fText = palette::kAccent2;
    theme.fCorner = 10.0f;
    theme.fFontSize = 15.0f;
    result->setTheme(theme);
    return result;
  }

  std::unique_ptr<scene::Drawable> build() {
    auto root = scene::make<nodes::Box>(
        {.roles = {scene::role<skin_dialog_style::Root>}},
        skia::colorSetARGB(220, 8, 6, 12));
    root->setStyleSheet<SkinDialogTheme>();
    auto *panel = root->add<nodes::Box>(
        {.y = 20.0f,
         .alpha = 0.0f,
         .roles = {scene::role<skin_dialog_style::Panel>}},
        palette::kBackground5);
    panel->fadeTo(1.0f, 200.0, scene::Easing::kOutQuint);
    panel->moveToY(0.0f, 400.0, scene::Easing::kOutQuint);
    panel->add<nodes::Text>(
        {.roles = {scene::role<skin_dialog_style::Title>}},
        "Install optional classic artwork?", 24.0f, skia::kWhite, true);
    panel->add<nodes::Text>(
        {.y = 38.0f, .roles = {scene::role<skin_dialog_style::Text>}},
        "osu-cpp works without these files, but uses simple fallback graphics. "
        "The optional files are downloaded from a pinned WebOsu-2 revision. "
        "The set contains osu!/lazer resources from ppy and resources from "
        "other authors.",
        14.0f, skia::kWhite, false);
    panel->add<nodes::Text>(
        {.y = 82.0f, .roles = {scene::role<skin_dialog_style::Text>}},
        "Sources: github.com/WebOsu-2/webosu-2.github.io, "
        "github.com/111116/webosu and github.com/ppy/osu-resources. "
        "The exact licence supplied with the resources follows.",
        14.0f, skia::kWhite, false);
    auto *scroll = panel->add<nodes::ScrollContainer>(
        {.roles = {scene::role<skin_dialog_style::Scroll>}});
    fScroll = scroll;
    auto *licenceColumn = scroll->add<nodes::FillFlow>(
        {.roles = {scene::role<skin_dialog_style::LicenceColumn>}},
        nodes::FillFlow::Direction::kVertical);
    licenceColumn->add<nodes::Text>(
        {.roles = {scene::role<skin_dialog_style::Text>}}, loadLicence(),
        12.0f, skia::kWhite, false);
    panel->add<nodes::Box>(
        {.roles = {scene::role<skin_dialog_style::ScrollTrack>}},
        skia::colorSetARGB(80, 255, 255, 255));
    fScrollThumb = panel->add<nodes::Box>(
        {.roles = {scene::role<skin_dialog_style::ScrollThumb>}},
        palette::kAccent2);
    fStatusNode = panel->add<nodes::Text>(
        {.roles = {scene::role<skin_dialog_style::Status>}}, fStatus, 14.0f,
        palette::kAccent2, false);
    auto *buttons = panel->add<nodes::FillFlow>(
        {.roles = {scene::role<skin_dialog_style::Buttons>}},
        nodes::FillFlow::Direction::kHorizontal);
    buttons->setSpacing(16.0f, 0.0f);
    buttons->setWrap(false);
    buttons->add(button("Download", [this] { this->start(); }));
    buttons->add(button("Continue without skin", [this] { this->decline(); }));
    return root;
  }

  void updateScrollThumb() {
    if (!fScroll || !fScrollThumb)
      return;
    constexpr float trackTop = 122.0f;
    constexpr float trackHeight = 205.0f;
    const float extent = fScroll->extent();
    const float content = trackHeight + extent;
    const float thumbHeight =
        content > 0.0f ? std::max(28.0f, trackHeight * trackHeight / content)
                       : trackHeight;
    const float travel = trackHeight - thumbHeight;
    const float progress =
        extent > 0.0f
            ? std::clamp(fScroll->current() / extent, 0.0f, 1.0f)
            : 0.0f;
    fScrollThumb->resizeHeightTo(thumbHeight, 0.0);
    fScrollThumb->moveToY(trackTop + travel * progress, 0.0);
  }

  void decline() {
    if (fDownloading)
      return;
    fDeclineRequested = true;
  }

  void finishDecline() {
    std::error_code ec;
    std::filesystem::create_directories(fRoot, ec);
    std::filesystem::remove(fRoot / ".download-in-progress", ec);
    std::ofstream marker(fRoot / ".download-declined");
    marker << "1\n";
    fScene.reset();
  }

  void start() {
    if (fDownloading)
      return;
    fDownloading = true;
    fIndex = 0;
    std::error_code ec;
    std::filesystem::create_directories(fRoot, ec);
    std::filesystem::remove(fRoot / ".download-declined", ec);
    std::ofstream(fRoot / ".download-in-progress") << "1\n";
    this->downloadNext();
  }

  void downloadNext() {
    if (fIndex >= kFiles.size()) {
      fDownloading = false;
      fInstalled = true;
      std::error_code ec;
      std::ofstream(fRoot / ".download-complete") << kRevision << '\n';
      std::filesystem::remove(fRoot / ".download-in-progress", ec);
      fStatus = "Installed. The new artwork is now active.";
      fScene.reset();
      return;
    }
    const std::string remote(kFiles[fIndex]);
    fStatus = std::format("Downloading {} of {}...", fIndex + 1,
                          kFiles.size());
    const std::string url = std::format(
        "https://raw.githubusercontent.com/WebOsu-2/webosu-2.github.io/{}/{}",
        kRevision, remote);
    auto handle = std::make_shared<http::Handle>();
    http::get(url, std::move(handle),
              [this, remote](http::Response response) {
                if (!response.fOk) {
                  fDownloading = false;
                  fStatus = std::format("Download failed: {}. Tap Download to retry.",
                                        response.fError.empty()
                                            ? std::to_string(response.fStatus)
                                            : response.fError);
                  return;
                }
                std::filesystem::path name =
                    std::filesystem::path(remote).filename();
                if (name == "LICENCE.md")
                  name = "LICENCE.md";
                const auto destination = fRoot / name;
                const auto temporary = destination.string() + ".part";
                {
                  std::ofstream out(temporary, std::ios::binary);
                  out.write(response.fBody.data(),
                            static_cast<std::streamsize>(response.fBody.size()));
                  if (!out) {
                    fDownloading = false;
                    fStatus = "Could not write to the user skin directory.";
                    return;
                  }
                }
                std::error_code ec;
                std::filesystem::rename(temporary, destination, ec);
                if (ec) {
                  std::filesystem::remove(destination, ec);
                  ec.clear();
                  std::filesystem::rename(temporary, destination, ec);
                }
                if (ec) {
                  fDownloading = false;
                  fStatus = "Could not finish writing a downloaded file.";
                  return;
                }
                ++fIndex;
                this->downloadNext();
              });
  }

  std::filesystem::path fRoot;
  bool fMayOffer = false;
  bool fDownloading = false;
  bool fInstalled = false;
  bool fDeclineRequested = false;
  float fScrollTicks = 0.0f;
  std::size_t fIndex = 0;
  std::string fStatus = "Nothing will be downloaded without your consent.";
  nodes::Text *fStatusNode = nullptr;
  nodes::ScrollContainer *fScroll = nullptr;
  nodes::Box *fScrollThumb = nullptr;
  std::unique_ptr<scene::Drawable> fScene;
};

} // namespace client
