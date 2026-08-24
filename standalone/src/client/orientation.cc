module;

#if defined(__linux__) && !defined(__EMSCRIPTEN__)
#include <cstdio>
#include <cstdlib>
#endif

export module client.orientation;

import std;

export namespace client {

// A fullscreen Wayland client cannot request an output orientation.  Mobile
// wlroots compositors expose that operation through output management, and
// wlr-randr is the small reference client for it.  Keep the integration at
// arm's length: it is runtime-only, postmarketOS-only by default, and restores
// the transform it found rather than assuming the desktop started upright.
class DisplayOrientation {
public:
  DisplayOrientation() = default;
  DisplayOrientation(const DisplayOrientation &) = delete;
  DisplayOrientation &operator=(const DisplayOrientation &) = delete;
  ~DisplayOrientation() { this->restore(); }

  // 0 follows the shell, 1 is clockwise landscape, 2 counter-clockwise.
  void apply(int choice) {
#if defined(__linux__) && !defined(__EMSCRIPTEN__)
    if (!this->eligible()) {
      return;
    }
    if (choice <= 0) {
      this->restore();
      return;
    }
    if (!this->discover()) {
      return;
    }
    const std::string wanted =
        fBackend == Backend::kKScreen ? (choice == 2 ? "right" : "left")
                                     : (choice == 2 ? "270" : "90");
    if (wanted == fCurrentTransform) {
      return;
    }
    if (this->setTransform(wanted)) {
      fCurrentTransform = wanted;
      fChanged = fCurrentTransform != fOriginalTransform;
    }
#else
    (void)choice;
#endif
  }

  void restore() {
#if defined(__linux__) && !defined(__EMSCRIPTEN__)
    if (fChanged && !fOutput.empty() && !fOriginalTransform.empty()) {
      (void)this->setTransform(fOriginalTransform);
    }
    fChanged = false;
    fCurrentTransform = fOriginalTransform;
#endif
  }

private:
#if defined(__linux__) && !defined(__EMSCRIPTEN__)
  struct Output {
    std::string fName;
    std::string fTransform;
    bool fEnabled = false;
    bool fInternal = false;
  };

  enum class Backend : std::uint8_t { kNone, kKScreen, kWlrRandr };

  static bool postmarketOS() {
    // An explicit output opts other wlroots-based mobile systems in without
    // making a default-on setting rotate arbitrary Linux workstations.
    if (const char *output = std::getenv("OSU_LANDSCAPE_OUTPUT");
        output != nullptr && *output != '\0') {
      return true;
    }
    std::ifstream in("/etc/os-release");
    std::string line;
    while (std::getline(in, line)) {
      if (line == "ID=postmarketos" || line == "ID=\"postmarketos\"") {
        return true;
      }
    }
    return false;
  }

  bool eligible() const {
    const char *wayland = std::getenv("WAYLAND_DISPLAY");
    return wayland != nullptr && *wayland != '\0' && postmarketOS();
  }

  static bool safeName(std::string_view name) {
    return !name.empty() &&
           std::ranges::all_of(name, [](unsigned char c) {
             return std::isalnum(c) || c == '-' || c == '_' || c == '.';
           });
  }

  static bool internalOutput(std::string_view name) {
    return name.starts_with("DSI-") || name.starts_with("eDP-") ||
           name.starts_with("LVDS-");
  }

  bool discover() {
    if (!fOutput.empty()) {
      return true;
    }
    if (this->discoverKScreen()) {
      fBackend = Backend::kKScreen;
      return true;
    }
    if (this->discoverWlrRandr()) {
      fBackend = Backend::kWlrRandr;
      return true;
    }
    return false;
  }

  static std::string_view trimmed(std::string_view line) {
    while (!line.empty() &&
           std::isspace(static_cast<unsigned char>(line.front()))) {
      line.remove_prefix(1);
    }
    while (!line.empty() && std::isspace(static_cast<unsigned char>(line.back()))) {
      line.remove_suffix(1);
    }
    return line;
  }

  static std::string kscreenRotation(std::string_view value) {
    value = trimmed(value);
    if (value == "1" || value == "None" || value == "normal") {
      return "normal";
    }
    if (value == "2" || value == "Left" || value == "left") {
      return "left";
    }
    if (value == "4" || value == "Inverted" || value == "inverted") {
      return "inverted";
    }
    if (value == "8" || value == "Right" || value == "right") {
      return "right";
    }
    return {};
  }

  bool discoverKScreen() {
    FILE *pipe = ::popen("kscreen-doctor -o 2>/dev/null", "r");
    if (pipe == nullptr) {
      return false;
    }
    std::vector<Output> outputs;
    Output current;
    const auto finish = [&] {
      if (!current.fName.empty()) {
        outputs.push_back(std::move(current));
        current = {};
      }
    };
    std::array<char, 1024> buffer{};
    while (::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) !=
           nullptr) {
      std::string_view line(buffer.data());
      const std::string_view text = trimmed(line);
      if (text.starts_with("Output: ")) {
        finish();
        std::istringstream fields{std::string(text)};
        std::string label;
        fields >> label >> current.fName;
      } else if (text == "enabled") {
        current.fEnabled = true;
      } else if (text == "Panel") {
        current.fInternal = true;
      } else if (text.starts_with("Rotation: ")) {
        current.fTransform = kscreenRotation(text.substr(10));
      }
    }
    finish();
    if (::pclose(pipe) != 0) {
      return false;
    }

    const Output *selected = nullptr;
    for (const auto &output : outputs) {
      if (!output.fEnabled || !safeName(output.fName) ||
          output.fTransform.empty()) {
        continue;
      }
      if (selected == nullptr || output.fInternal) {
        selected = &output;
      }
      if (output.fInternal) {
        break;
      }
    }
    if (selected == nullptr) {
      return false;
    }
    fOutput = selected->fName;
    fOriginalTransform = selected->fTransform;
    fCurrentTransform = fOriginalTransform;
    return true;
  }

  bool discoverWlrRandr() {
    if (const char *chosen = std::getenv("OSU_LANDSCAPE_OUTPUT");
        chosen != nullptr && safeName(chosen)) {
      fOutput = chosen;
    }

    FILE *pipe = ::popen("wlr-randr 2>/dev/null", "r");
    if (pipe == nullptr) {
      return false;
    }
    std::vector<Output> outputs;
    Output current;
    const auto finish = [&] {
      if (!current.fName.empty()) {
        outputs.push_back(std::move(current));
        current = {};
      }
    };
    std::array<char, 512> buffer{};
    while (::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) !=
           nullptr) {
      std::string_view line(buffer.data());
      while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) {
        line.remove_suffix(1);
      }
      if (!line.empty() && line.front() != ' ') {
        finish();
        const std::size_t end = line.find(' ');
        current.fName = std::string(line.substr(0, end));
      } else if (line == " Enabled: yes") {
        current.fEnabled = true;
      } else if (line.starts_with(" Transform: ")) {
        current.fTransform = std::string(line.substr(12));
      }
    }
    finish();
    const int status = ::pclose(pipe);
    if (status != 0) {
      if (!fWarned) {
        std::println(std::cerr,
                     "[display] landscape requested, but wlr-randr is not "
                     "available or the compositor does not support it");
        fWarned = true;
      }
      return false;
    }

    const Output *selected = nullptr;
    for (const auto &output : outputs) {
      if (!output.fEnabled || !safeName(output.fName)) {
        continue;
      }
      if (!fOutput.empty()) {
        if (output.fName == fOutput) {
          selected = &output;
          break;
        }
        continue;
      }
      if (selected == nullptr || internalOutput(output.fName)) {
        selected = &output;
      }
      if (internalOutput(output.fName)) {
        break;
      }
    }
    if (selected == nullptr || selected->fTransform.empty()) {
      if (!fWarned) {
        std::println(std::cerr,
                     "[display] landscape requested, but no enabled output "
                     "with a reported transform was found");
        fWarned = true;
      }
      return false;
    }
    fOutput = selected->fName;
    fOriginalTransform = selected->fTransform;
    fCurrentTransform = fOriginalTransform;
    return true;
  }

  bool setTransform(std::string_view transform) const {
    if (fBackend == Backend::kKScreen) {
      if (!safeName(fOutput) ||
          (transform != "normal" && transform != "left" &&
           transform != "right" && transform != "inverted")) {
        return false;
      }
      const std::string command =
          std::format("kscreen-doctor output.{}.rotation.{}", fOutput,
                      transform);
      return std::system(command.c_str()) == 0;
    }
    if (!safeName(fOutput) ||
        (transform != "normal" && transform != "90" && transform != "180" &&
         transform != "270" && transform != "flipped" &&
         transform != "flipped-90" && transform != "flipped-180" &&
         transform != "flipped-270")) {
      return false;
    }
    const std::string command = std::format(
        "wlr-randr --output {} --transform {}", fOutput, transform);
    return std::system(command.c_str()) == 0;
  }

  std::string fOutput;
  std::string fOriginalTransform;
  std::string fCurrentTransform;
  Backend fBackend = Backend::kNone;
  bool fChanged = false;
  bool fWarned = false;
#endif
};

} // namespace client
