module;

#include <cstdlib>

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

[[nodiscard]] inline RuntimeConfiguration runtimeConfiguration() {
  const auto present = [](const char *name) {
    return std::getenv(name) != nullptr;
  };
  RuntimeConfiguration result{
      .fUseSystemFont = present("OSU_SYSTEM_FONT"),
      .fForcePartialRedraw = present("OSU_PARTIAL_REDRAW"),
      .fShowDamage = present("OSU_SHOW_DAMAGE"),
      .fTraceRepaint =
          present("OSU_TRACE_REPAINT") || present("OSU_TRACE_RESIZE"),
      .fPreferGles = present("OSU_GLES"),
      .fForceEgl = present("OSU_EGL"),
  };
  if (const char *age = std::getenv("OSU_BUFFER_AGE"); age != nullptr) {
    int value = 0;
    const std::string_view text(age);
    const auto parsed =
        std::from_chars(text.data(), text.data() + text.size(), value);
    if (parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size()) {
      result.fBufferAge = value;
    }
  }
  if (const char *directory = std::getenv("OSU_AUDIO_DUMP");
      directory != nullptr && *directory != '\0') {
    result.fAudioDumpDirectory = directory;
  }
  return result;
}

} // namespace platform
