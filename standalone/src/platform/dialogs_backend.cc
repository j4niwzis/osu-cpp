export module platform.dialogs.backend;

import std;
import platform.dialogs.types;

export namespace platform::dialogs::backend {
[[nodiscard]] inline std::optional<std::filesystem::path>
openArchive(const std::string &) {
  return std::nullopt;
}
[[nodiscard]] inline SaveFileResult saveVideo(const std::string &,
                                              std::string_view) {
  return {};
}
} // namespace platform::dialogs::backend
