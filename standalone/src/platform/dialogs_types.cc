export module platform.dialogs.types;

import std;

export namespace platform::dialogs {
struct SaveFileResult {
  bool fPortalAvailable = false;
  std::optional<std::filesystem::path> fPath;
};
} // namespace platform::dialogs
