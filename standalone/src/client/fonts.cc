export module client.fonts;

import std;
import skia;
import skiff.paint;
import platform.system;
import platform.configuration;

namespace client::font_detail {

// The fonts inside this program, where they are inside it.
//
// A build that is one file has no share/ beside it to read from, so the
// fonts are linked in: each is an object with the bytes between two symbols
// the linker invented, and the build wrote down which ones there are.
#if defined(OSU_STATIC_FONTS)
extern "C" {
#define OSU_FONT(file, symbol)                                                 \
  extern const char _binary_##symbol##_start[];                                \
  extern const char _binary_##symbol##_end[];
#include "osu_fonts.h"
#undef OSU_FONT
}

struct EmbeddedFont {
  const char *fName;
  const char *fBegin;
  const char *fEnd;
};

[[nodiscard]] std::span<const EmbeddedFont> embeddedFonts() {
  static const EmbeddedFont fonts[] = {
#define OSU_FONT(file, symbol)                                                 \
  {file, _binary_##symbol##_start, _binary_##symbol##_end},
#include "osu_fonts.h"
#undef OSU_FONT
  };
  return fonts;
}

[[nodiscard]] skia::Sp<skia::SkData> embedded(const std::string &name) {
  for (const auto &font : embeddedFonts()) {
    if (name != font.fName) {
      continue;
    }
    return skia::SkData::MakeWithoutCopy(
        font.fBegin, static_cast<std::size_t>(font.fEnd - font.fBegin));
  }
  return nullptr;
}
#else
[[nodiscard]] skia::Sp<skia::SkData> embedded(const std::string &) {
  return nullptr;
}
#endif

[[nodiscard]] std::vector<std::filesystem::path>
assetCandidates(const std::string &name) {
  std::vector<std::filesystem::path> candidates;
  const auto exe = platform::system::executablePath({});
  if (!exe.empty()) {
    const auto prefix = exe.parent_path().parent_path();
    candidates.push_back(prefix / "share" / "osu_client" / "fonts" / name);
    candidates.push_back(exe.parent_path() / "fonts" / name);
  }
#ifdef OSU_CLIENT_DATADIR
  candidates.emplace_back(std::filesystem::path(OSU_CLIENT_DATADIR) / "fonts" /
                          name);
#endif
#ifdef OSU_CLIENT_SOURCE_ASSETS
  candidates.emplace_back(std::filesystem::path(OSU_CLIENT_SOURCE_ASSETS) /
                          "fonts" / name);
#endif
  candidates.emplace_back(std::filesystem::path("assets") / "fonts" / name);
  candidates.emplace_back(std::filesystem::path("fonts") / name);
  return candidates;
}

[[nodiscard]] skia::Sp<skia::SkTypeface> typefaceFrom(skia::Sp<skia::SkData> data) {
  if (!data || data->isEmpty()) {
    return nullptr;
  }
  std::array<skia::Sp<skia::SkData>, 1> datas{std::move(data)};
  auto manager = skia::SkFontMgr_New_Custom_Data(datas);
  if (!manager || manager->countFamilies() == 0) {
    return nullptr;
  }
  auto face = manager->matchFamilyStyle(nullptr, skia::SkFontStyle());
  if (!face) {
    face = manager->createStyleSet(0)->createTypeface(0);
  }
  return face;
}

[[nodiscard]] skia::Sp<skia::SkTypeface>
loadTypeface(const std::string &name) {
  // What is in the program, before what is on the machine: a build that
  // carries its fonts is a build that does not depend on where it was
  // installed, and a build that does not carry them falls through here
  // without noticing.
  if (auto face = typefaceFrom(embedded(name))) {
    skia::SkString family;
    face->getFamilyName(&family);
    std::println(std::cerr, "[ui] font \"{}\" from this program",
                 family.c_str());
    return face;
  }
  for (const auto &path : assetCandidates(name)) {
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
      continue;
    }
    auto data = skia::SkData::MakeFromFileName(path.c_str());
    if (!data || data->isEmpty()) {
      continue;
    }
    std::array<skia::Sp<skia::SkData>, 1> datas{std::move(data)};
    auto manager = skia::SkFontMgr_New_Custom_Data(datas);
    if (!manager || manager->countFamilies() == 0) {
      continue;
    }
    auto face = manager->matchFamilyStyle(nullptr, skia::SkFontStyle());
    if (!face) {
      face = manager->createStyleSet(0)->createTypeface(0);
    }
    if (face) {
      skia::SkString family;
      face->getFamilyName(&family);
      std::println(std::cerr, "[ui] font \"{}\" from {}", family.c_str(),
                   path.string());
      return face;
    }
  }
  std::println(std::cerr, "[ui] font missing: {}", name);
  return nullptr;
}

// The fonts this device already has, for the writing this program does not
// carry.
//
// Android names its families in a configuration file and Skia reads it; what
// comes back is the same Noto CJK that a package would otherwise carry a
// second copy of -- ten megabytes of one, for Japanese and Korean. Asked for
// by character rather than by family name, because which family holds a
// script is the system's business and differs between devices.
void addSystemFallbacks() {
#if defined(__ANDROID__)
  auto system = skia::SkFontMgr_New_Android(
      nullptr, skia::SkFontScanner_Make_FreeType());
  if (!system) {
    std::println(std::cerr, "[ui] this device offers no font configuration");
    return;
  }
  // One character per script, which is how a font manager is asked whether
  // it has the script at all: hiragana, hangul, a han ideograph.
  for (const auto sample : {0x3042, 0xAC00, 0x4E00}) {
    auto face = system->matchFamilyStyleCharacter(
        nullptr, skia::SkFontStyle(), nullptr, 0, sample);
    if (!face) {
      continue;
    }
    skia::SkString family;
    face->getFamilyName(&family);
    std::println(std::cerr, "[ui] {} for U+{:04X}, from this device",
                 family.c_str(), sample);
    skiff::paint::fonts().addFallback(std::move(face));
  }
#endif
}

void loadExtraFonts() {
  static constexpr std::array<const char *, 4> kKnown = {
      "Inter.ttf", "NotoSansJP.ttf", "NotoSansKR.ttf",
      "FontAwesome-Solid.ttf"};
  for (const auto &dir : assetCandidates("")) {
    std::error_code ec;
    if (!std::filesystem::is_directory(dir, ec)) {
      continue;
    }
    for (const auto &entry : std::filesystem::directory_iterator(dir, ec)) {
      const auto ext = entry.path().extension().string();
      if (ext != ".ttf" && ext != ".otf" && ext != ".ttc") {
        continue;
      }
      const auto name = entry.path().filename().string();
      if (std::ranges::find(kKnown, name) != kKnown.end() ||
          name == "Montserrat-ExtraBold.ttf") {
        continue;
      }
      skiff::paint::fonts().addFallback(loadTypeface(name));
    }
    return;
  }
}

[[nodiscard]] skia::SkFont uiFont(float size) {
  if (const auto &primary = skiff::paint::fonts().primary()) {
    return skia::SkFont(primary, size);
  }
  for (const char *dir : {"/usr/share/fonts/noto",
                          "/usr/share/fonts/ttf-dejavu",
                          "/usr/share/fonts/TTF", "/usr/share/fonts"}) {
    if (!std::filesystem::is_directory(dir)) {
      continue;
    }
    auto manager = skia::SkFontMgr_New_Custom_Directory(dir);
    if (!manager || manager->countFamilies() == 0) {
      continue;
    }
    auto face = manager->matchFamilyStyle("Noto Sans", skia::SkFontStyle());
    if (!face) {
      face = manager->matchFamilyStyle("DejaVu Sans", skia::SkFontStyle());
    }
    if (!face) {
      face = manager->createStyleSet(0)->createTypeface(0);
    }
    if (face) {
      std::println(std::cerr, "[ui] falling back to a system font from {}", dir);
      return skia::SkFont(std::move(face), size);
    }
  }
  std::println(std::cerr, "[ui] no font found at all");
  return skia::SkFont();
}

} // namespace client::font_detail

export namespace client {

struct ClientFonts {
  skia::SkFont fUi;
  skia::SkFont fDisplay;
};

[[nodiscard]] ClientFonts loadClientFonts(float size) {
  auto &stack = skiff::paint::fonts();
  if (!platform::runtimeConfiguration().fUseSystemFont) {
    stack.setPrimary(font_detail::loadTypeface("Inter.ttf"));
    // The device's own scripts first, and the carried ones after: a package
    // that has both should prefer what the machine already had in memory,
    // and one that carries none of them still reads Japanese.
    font_detail::addSystemFallbacks();
    for (const char *name :
         {"NotoSansJP.ttf", "NotoSansKR.ttf", "FontAwesome-Solid.ttf"}) {
      stack.addFallback(font_detail::loadTypeface(name));
    }
    font_detail::loadExtraFonts();
    std::println(std::cerr, "[ui] {} fallback fonts loaded",
                 stack.fallbackCount());
  } else {
    std::println(std::cerr, "[ui] bundled fonts skipped by request");
  }

  ClientFonts result;
  result.fUi = font_detail::uiFont(size);
  if (auto face = font_detail::loadTypeface("Montserrat-ExtraBold.ttf")) {
    result.fDisplay = skia::SkFont(std::move(face), size);
  } else {
    std::println(std::cerr,
                 "[ui] no display font found; judgements use the UI font");
    result.fDisplay = result.fUi;
  }
  return result;
}

} // namespace client
