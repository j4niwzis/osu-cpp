module;

#include <cstdlib>

export module platform.system;

import std;

namespace {

[[nodiscard]] std::optional<std::string> environment(std::string_view name) {
  const std::string key(name);
  if (const char *value = std::getenv(key.c_str()); value != nullptr) {
    return std::string(value);
  }
  return std::nullopt;
}

} // namespace

export namespace platform::system {

[[nodiscard]] std::filesystem::path executablePath(std::string_view argv0) {
#ifdef __linux__
  std::error_code ec;
  auto self = std::filesystem::read_symlink("/proc/self/exe", ec);
  if (!ec && !self.empty()) {
    return self;
  }
#endif
  std::filesystem::path argument =
      argv0.empty() ? std::filesystem::path{"osu_client"}
                    : std::filesystem::path{argv0};
  if (argument.has_parent_path() || argument.is_absolute()) {
    return argument;
  }
  const auto path = environment("PATH");
  if (!path) {
    return argument;
  }
  std::string_view remaining = *path;
  while (!remaining.empty()) {
    const std::size_t colon = remaining.find(':');
    const std::string_view directory = remaining.substr(0, colon);
    if (!directory.empty()) {
      auto candidate = std::filesystem::path(directory) / argument;
      if (std::filesystem::exists(candidate)) {
        return candidate;
      }
    }
    if (colon == std::string_view::npos) {
      break;
    }
    remaining.remove_prefix(colon + 1);
  }
  return argument;
}

[[nodiscard]] std::filesystem::path applicationDataPath() {
  if (const auto xdg = environment("XDG_DATA_HOME"); xdg && !xdg->empty()) {
    return std::filesystem::path(*xdg) / "osu-cpp";
  }
  if (const auto home = environment("HOME"); home && !home->empty()) {
    return std::filesystem::path(*home) / ".local" / "share" / "osu-cpp";
  }
  return std::filesystem::path{"."} / ".osu-cpp";
}

[[nodiscard]] std::filesystem::path mapsDirectory() {
  if (const auto xdg = environment("XDG_DATA_HOME"); xdg && !xdg->empty()) {
    return std::filesystem::path(*xdg) / "osu-cpp" / "maps";
  }
  if (const auto home = environment("HOME"); home && !home->empty()) {
    return std::filesystem::path(*home) / ".local" / "share" / "osu_client" /
           "maps";
  }
  return "maps";
}

} // namespace platform::system
