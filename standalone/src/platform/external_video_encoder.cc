export module platform.external_video_encoder;

import std;

export namespace platform {

class ExternalVideoEncoder {
public:
  [[nodiscard]] static bool available() { return false; }
  [[nodiscard]] bool begin(int, int, int, const std::filesystem::path &, double,
                           const std::filesystem::path &) {
    fError = "external video encoder is unavailable";
    return false;
  }
  void addFrame(std::span<const std::uint8_t>) {}
  [[nodiscard]] bool finish() { return false; }
  [[nodiscard]] std::size_t frameCount() const noexcept { return 0; }
  [[nodiscard]] const std::string &error() const noexcept { return fError; }

private:
  std::string fError;
};

} // namespace platform
