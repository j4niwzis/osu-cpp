export module client.portal;

import std;
import platform.dialogs;

export namespace client::portal {
using SaveFileResult = platform::dialogs::SaveFileResult;

[[nodiscard]] inline std::optional<std::filesystem::path>
openArchive(const std::string &title) {
  return platform::dialogs::openArchive(title);
}
[[nodiscard]] inline SaveFileResult saveVideo(const std::string &title,
                                              std::string_view currentName) {
  return platform::dialogs::saveVideo(title, currentName);
}
} // namespace client::portal
