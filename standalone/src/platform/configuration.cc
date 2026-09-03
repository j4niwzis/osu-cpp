export module platform.configuration;

import std;

export namespace platform {

struct RuntimeConfiguration {
  bool fUseSystemFont = false;
  bool fForcePartialRedraw = false;
  std::optional<int> fBufferAge;
  bool fShowDamage = false;
  bool fTraceRepaint = false;
  bool fPreferGles = false;
  bool fForceEgl = false;
  std::optional<std::filesystem::path> fAudioDumpDirectory;
};

[[nodiscard]] inline RuntimeConfiguration runtimeConfiguration() { return {}; }

} // namespace platform
