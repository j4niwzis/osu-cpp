export module platform.dialogs;

export import platform.dialogs.types;
import platform.dialogs.backend;
import std;

export namespace platform::dialogs {
[[nodiscard]] inline std::optional<std::filesystem::path>
openArchive(const std::string &title) {
  return backend::openArchive(title);
}
[[nodiscard]] inline SaveFileResult saveVideo(const std::string &title,
                                              std::string_view currentName) {
  return backend::saveVideo(title, currentName);
}
[[nodiscard]] inline bool commitSave(const SaveFileResult &result) {
  return backend::commitSave(result);
}
} // namespace platform::dialogs
