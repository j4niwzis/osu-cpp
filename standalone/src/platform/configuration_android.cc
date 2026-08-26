export module platform.configuration;

import std;

export namespace platform {

struct RuntimeConfiguration {
  bool fUseSystemFont = false;
  bool fForcePartialRedraw = false;
  std::optional<int> fBufferAge;
  bool fShowDamage = false;
  bool fTraceRepaint = false;
  bool fPreferGles = true;
  bool fForceEgl = true;
  std::optional<std::filesystem::path> fAudioDumpDirectory;
};

[[nodiscard]] inline RuntimeConfiguration runtimeConfiguration() { return {}; }

} // namespace platform
