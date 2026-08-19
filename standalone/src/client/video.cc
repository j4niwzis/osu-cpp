export module client.video;

import std;
import skia;

export namespace client {

// Replay-to-video export.
//
// Frames are rendered offscreen at the requested resolution, written out as
// a PNG sequence, and handed to ffmpeg together with the beatmap audio. There
// is no encoder in this stack, so ffmpeg is required; its absence is reported
// rather than guessed at.
struct VideoOptions {
  int fWidth = 1920;
  int fHeight = 1080;
  int fFps = 60;
  std::filesystem::path fOutput;
  std::filesystem::path fAudio;   // optional source audio to mux
  double fAudioOffsetSec = 0.0;   // where the map starts inside that audio
};

class VideoExporter {
public:
  [[nodiscard]] static bool ffmpegAvailable() {
    return std::system("command -v ffmpeg > /dev/null 2>&1") == 0;
  }

  [[nodiscard]] bool begin(const VideoOptions &opts) {
    fOpts = opts;
    fFrame = 0;
    std::error_code ec;
    fDir = std::filesystem::temp_directory_path(ec) / "osu_client_export";
    std::filesystem::remove_all(fDir, ec);
    std::filesystem::create_directories(fDir, ec);
    if (ec) {
      fError = "cannot create a temporary directory";
      return false;
    }
    if (!ffmpegAvailable()) {
      fError = "ffmpeg not found in PATH";
      return false;
    }
    return true;
  }

  // Called once per rendered frame with the offscreen image.
  void addFrame(const skia::Sp<skia::SkImage> &image) {
    if (!image) {
      return;
    }
    auto png = skia::png::Encode(nullptr, image.get(), skia::png::Options{});
    if (!png || png->isEmpty()) {
      return;
    }
    const auto path = fDir / std::format("frame_{:06}.png", fFrame++);
    std::ofstream out(path, std::ios::binary);
    out.write(static_cast<const char *>(png->data()),
              static_cast<std::streamsize>(png->size()));
  }

  [[nodiscard]] std::size_t frameCount() const noexcept { return fFrame; }
  [[nodiscard]] const std::string &error() const noexcept { return fError; }

  // Mux the sequence (and audio, when given) into the output file.
  [[nodiscard]] bool finish() {
    if (fFrame == 0) {
      fError = "no frames were rendered";
      return false;
    }
    std::string cmd = std::format(
        "ffmpeg -y -loglevel error -framerate {} -i '{}/frame_%06d.png'",
        fOpts.fFps, fDir.string());
    if (!fOpts.fAudio.empty() && std::filesystem::exists(fOpts.fAudio)) {
      cmd += std::format(" -ss {:.3f} -i '{}' -c:a aac -shortest",
                         fOpts.fAudioOffsetSec, fOpts.fAudio.string());
    }
    cmd += std::format(
        " -c:v libx264 -pix_fmt yuv420p -crf 18 -preset medium '{}'",
        fOpts.fOutput.string());

    const int rc = std::system(cmd.c_str());
    std::error_code ec;
    std::filesystem::remove_all(fDir, ec);
    if (rc != 0) {
      fError = std::format("ffmpeg exited with {}", rc);
      return false;
    }
    return true;
  }

private:
  VideoOptions fOpts;
  std::filesystem::path fDir;
  std::size_t fFrame = 0;
  std::string fError;
};

// Resolutions offered in the export dialog.
struct VideoPreset {
  const char *fLabel;
  int fWidth, fHeight;
};

inline constexpr std::array<VideoPreset, 4> kVideoPresets = {
    VideoPreset{"720p", 1280, 720}, VideoPreset{"1080p", 1920, 1080},
    VideoPreset{"1440p", 2560, 1440}, VideoPreset{"4K", 3840, 2160}};

} // namespace client
