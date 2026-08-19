export module client.mods;

import std;
import osu;

export namespace client {

// Mod selection, mirroring lazer's ModSelectOverlay layout: mods grouped in
// columns by type, each a rounded panel with an acronym and a description,
// toggled by click or by its key. Only the mods the engine implements are
// listed -- there is no point offering ones that would not apply.
struct ModEntry {
  const char *fAcronym;
  const char *fName;
  const char *fDescription;
  osu::ModSet fFlag;
  int fColumn;      // 0 = difficulty reduction, 1 = difficulty increase
  int fKey;         // shortcut, a GLFW key code filled in by the caller
  double fMultiplier;
};

// Columns as lazer names them.
inline constexpr std::array<const char *, 2> kModColumns = {
    "Difficulty Reduction", "Difficulty Increase"};

} // namespace client
