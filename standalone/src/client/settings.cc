export module client.settings;

import std;
import bjson;

export namespace client {

// The settings model, mirroring how osu!lazer organises them: one flat list
// of items, each belonging to a section, rendered as a single scrolling
// column with the sidebar scrolling to a section rather than swapping pages.
enum class SettingKind : std::uint8_t { kSlider, kToggle, kChoice };

struct SettingDef {
  int fSection = 0;
  std::string fKey;     // persistence key
  std::string fLabel;
  SettingKind fKind = SettingKind::kSlider;
  float fMin = 0.0f;
  float fMax = 1.0f;
  float fStep = 0.0f;   // 0 = continuous
  float fDefault = 0.0f;
  std::string fSuffix;  // "%", " ms", "x", ""
  // For kChoice: what the value indexes. Clicking the row steps through it.
  std::vector<std::string> fOptions;
};

class Settings {
public:
  // Sections, in lazer's order (the ones that map onto features here).
  static constexpr std::array<const char *, 5> kSections = {
      "Audio", "Graphics", "Gameplay", "Input", "Downloads"};
  static constexpr std::array<const char *, 5> kSectionIcons = {"♪", "▣", "◎",
                                                                "⌨", "↓"};

  Settings() {
    // Section 0: Audio
    this->add({0, "master", "Master volume", SettingKind::kSlider, 0.0f, 1.0f,
               0.01f, 1.0f, "%"});
    this->add({0, "music", "Music volume", SettingKind::kSlider, 0.0f, 1.0f,
               0.01f, 0.8f, "%"});
    this->add({0, "effect", "Effect volume", SettingKind::kSlider, 0.0f, 1.0f,
               0.01f, 1.0f, "%"});
    this->add({0, "offset", "Audio offset", SettingKind::kSlider, -300.0f,
               300.0f, 1.0f, 0.0f, " ms"});
    // Section 1: Graphics
    this->add({1, "dim", "Background dim", SettingKind::kSlider, 0.0f, 1.0f,
               0.01f, 0.7f, "%"});
    this->add({1, "vsync", "Frame limiter (vsync)", SettingKind::kToggle, 0.0f,
               1.0f, 0.0f, 1.0f, ""});
    this->add({1, "fps", "Show FPS counter", SettingKind::kToggle, 0.0f, 1.0f,
               0.0f, 0.0f, ""});
    // A list rather than a switch: there is no reason to assume these two
    // are the only renderers this client will ever have.
    this->add({1, "renderer", "Renderer", SettingKind::kChoice, 0.0f, 1.0f,
               1.0f, 0.0f, "",
               {"GPU (OpenGL)", "CPU (Skia raster)"}});
    // Off by default: it wins where pixels cost, which is the CPU renderer,
    // and loses where draw calls do.
    this->add({1, "partial", "Repaint only what changed",
               SettingKind::kToggle, 0.0f, 1.0f, 0.0f, 0.0f, ""});
    this->add({1, "damageoverlay", "Outline repainted regions",
               SettingKind::kToggle, 0.0f, 1.0f, 0.0f, 0.0f, ""});
    // How old the contents of the buffer being drawn into are. The window
    // system answers this where it can (EGL_EXT_buffer_age and its GLX twin);
    // where it cannot -- a driver without DRI3, say -- it can be asserted,
    // and a driver that swaps by copying is always one.
    //
    // Assert it only if it is known. Too low a number does not merely leave
    // the last frames' leftovers on screen: a chain deeper than the number
    // hands out buffers that have never held a whole frame, and those are
    // black. "Ask the driver" is not a worse guess than a wrong assertion --
    // when nobody answers, the client reaches back as far as the deepest
    // chain it is willing to believe in and repaints whole until it is sure,
    // which costs frames and cannot go black.
    this->add({1, "bufferage", "Assume buffer age", SettingKind::kChoice, 0.0f,
               3.0f, 1.0f, 0.0f, "",
               {"ask the driver", "1 (swap copies)", "2", "3"}});
    // The two things that draw whether or not anybody touched them. Off, the
    // menu and the pause screen stop asking for frames of their own.
    this->add({1, "uiscale", "Interface scale", SettingKind::kSlider, 0.5f,
               3.0f, 0.05f, 1.0f, "x"});
    this->add({1, "visualiser", "Logo visualiser", SettingKind::kToggle, 0.0f,
               1.0f, 0.0f, 1.0f, ""});
    this->add({1, "pausetriangles", "Animate pause triangles",
               SettingKind::kToggle, 0.0f, 1.0f, 0.0f, 1.0f, ""});
    this->add({1, "menutriangles", "Animate menu triangles",
               SettingKind::kToggle, 0.0f, 1.0f, 0.0f, 1.0f, ""});
    // Section 2: Gameplay
    this->add({2, "cursorsize", "Cursor size", SettingKind::kSlider, 0.5f,
               2.0f, 0.01f, 1.0f, "x"});
    this->add({2, "cursor", "Show cursor", SettingKind::kToggle, 0.0f, 1.0f,
               0.0f, 1.0f, ""});
    this->add({2, "cursortrail", "Cursor trail", SettingKind::kToggle, 0.0f,
               1.0f, 0.0f, 1.0f, ""});
    this->add({2, "snaking", "Snaking sliders", SettingKind::kToggle, 0.0f,
               1.0f, 0.0f, 1.0f, ""});
    this->add({2, "rules", "Gameplay rules", SettingKind::kChoice, 0.0f, 1.0f,
               1.0f, 0.0f, "",
               {"osu!lazer", "This client, before 2026"}});
    this->add({2, "stars", "Star rating", SettingKind::kChoice, 0.0f, 1.0f,
               1.0f, 0.0f, "",
               {"osu!lazer (master)", "Ranked (what the servers use)"}});
    this->add({2, "hitlighting", "Hit lighting", SettingKind::kToggle, 0.0f,
               1.0f, 0.0f, 1.0f, ""});
    this->add({2, "savereplay", "Save replays automatically",
               SettingKind::kToggle, 0.0f, 1.0f, 0.0f, 1.0f, ""});
    // Section 3: Input
    this->add({3, "sensitivity", "Cursor sensitivity", SettingKind::kSlider,
               0.1f, 6.0f, 0.01f, 1.0f, "x"});
    this->add({3, "rawinput", "Raw input (bypass desktop acceleration)",
               SettingKind::kToggle, 0.0f, 1.0f, 0.0f, 0.0f, ""});
    // Section 4: Downloads
    // Which mirror to ask first. A mirror that will not answer is still
    // replaced by the next one, so this is a preference and not a promise.
    // The names are spelled out here rather than taken from client.mirrors:
    // the settings know nothing about the network, and should not start.
    this->add({4, "mirror", "Beatmap mirror", SettingKind::kChoice, 0.0f, 2.0f,
               1.0f, 0.0f, "",
               {"nerinyan", "osu.direct", "mino (catboy.best)"}});
  }

  [[nodiscard]] const std::vector<SettingDef> &defs() const noexcept {
    return fDefs;
  }

  [[nodiscard]] float value(std::string_view key) const {
    const auto it = fValues.find(key);
    return it == fValues.end() ? 0.0f : it->second;
  }

  [[nodiscard]] bool flag(std::string_view key) const {
    return this->value(key) >= 0.5f;
  }

  [[nodiscard]] int choice(std::string_view key) const {
    return static_cast<int>(this->value(key) + 0.5f);
  }

  void setChoice(std::size_t index, int option) {
    if (index >= fDefs.size()) {
      return;
    }
    const auto &def = fDefs[index];
    if (def.fOptions.empty()) {
      return;
    }
    fValues[def.fKey] = static_cast<float>(std::clamp(
        option, 0, static_cast<int>(def.fOptions.size()) - 1));
  }

  [[nodiscard]] float valueAt(std::size_t index) const {
    return index < fDefs.size() ? this->value(fDefs[index].fKey) : 0.0f;
  }

  void set(std::string_view key, float value) {
    const auto it = fValues.find(key);
    if (it != fValues.end()) {
      it->second = value;
    }
  }

  // Quantise to the item's step, so a slider cannot land between ticks.
  void setFromFraction(std::size_t index, float t) {
    if (index >= fDefs.size()) {
      return;
    }
    const auto &d = fDefs[index];
    float v = d.fMin + (d.fMax - d.fMin) * std::clamp(t, 0.0f, 1.0f);
    if (d.fStep > 0.0f) {
      v = d.fMin + std::round((v - d.fMin) / d.fStep) * d.fStep;
    }
    this->set(d.fKey, std::clamp(v, d.fMin, d.fMax));
  }

  void toggle(std::size_t index) {
    if (index >= fDefs.size()) {
      return;
    }
    const auto &d = fDefs[index];
    this->set(d.fKey, this->flag(d.fKey) ? 0.0f : 1.0f);
  }

  void restoreDefault(std::size_t index) {
    if (index < fDefs.size()) {
      this->set(fDefs[index].fKey, fDefs[index].fDefault);
    }
  }

  [[nodiscard]] bool isModified(std::size_t index) const {
    if (index >= fDefs.size()) {
      return false;
    }
    return std::abs(this->value(fDefs[index].fKey) - fDefs[index].fDefault) >
           1e-4f;
  }

  [[nodiscard]] std::string displayValue(std::size_t index) const {
    if (index >= fDefs.size()) {
      return {};
    }
    const auto &d = fDefs[index];
    const float v = this->value(d.fKey);
    if (d.fSuffix == "%") {
      return std::format("{:.0f}%", v * 100.0f);
    }
    if (d.fSuffix == "x") {
      return std::format("{:.2f}x", v);
    }
    if (d.fSuffix == " ms") {
      return std::format("{:.0f} ms", v);
    }
    return std::format("{:.2f}", v);
  }

  void load(const std::filesystem::path &file) {
    fFile = file;
    std::ifstream in(file);
    if (!in) {
      return;
    }
    const std::string text((std::istreambuf_iterator<char>(in)),
                           std::istreambuf_iterator<char>());
    const auto parsed = bjson::tryParse(text);
    if (!parsed) {
      return;
    }
    const bjson::object *o = parsed->if_object();
    if (o == nullptr) {
      return;
    }
    for (const auto &d : fDefs) {
      if (const bjson::value *v = o->if_contains(d.fKey);
          v != nullptr && v->is_number()) {
        this->set(d.fKey, static_cast<float>(v->to_number<double>()));
      }
    }
  }

  void save() const {
    if (fFile.empty()) {
      return;
    }
    bjson::object o;
    for (const auto &d : fDefs) {
      o[d.fKey] = this->value(d.fKey);
    }
    std::ofstream out(fFile, std::ios::trunc);
    if (out) {
      out << bjson::serialize(bjson::value(std::move(o)));
    }
  }

private:
  void add(SettingDef def) {
    fValues.emplace(def.fKey, def.fDefault);
    fDefs.push_back(std::move(def));
  }

  std::vector<SettingDef> fDefs;
  std::map<std::string, float, std::less<>> fValues;
  std::filesystem::path fFile;
};

} // namespace client
