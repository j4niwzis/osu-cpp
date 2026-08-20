module;

// popen/pclose: POSIX rather than standard, so they come in through the
// global fragment rather than through import std.
#include <cstdio>

// OSU_VIDEO_LIBAV builds against libavcodec instead of talking to an ffmpeg
// binary. Two reasons to want it: a wasm build has no processes to spawn at
// all, and a desktop one stops depending on what is installed on the machine.
// The third possibility -- letting a browser encode through WebCodecs and
// shipping no encoder at all -- goes behind the same seam when it arrives.
#ifdef OSU_VIDEO_LIBAV
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
}
#endif

export module client.video;

import std;
import skia;

export namespace client {

// Replay-to-video export.
//
// Frames are rendered offscreen at the requested resolution and fed to ffmpeg
// down a pipe as raw pixels. There is no encoder in this stack -- Skia
// rasterises, it does not compress moving pictures -- so ffmpeg is required,
// and its absence is reported rather than guessed at.
//
// Raw down a pipe rather than a PNG sequence on disk, which is what this did
// before: a minute of 1080p is three and a half thousand frames, and PNG
// compressing every one of them cost more than the rendering did and left
// gigabytes in the temporary directory to be cleaned up afterwards.
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
    if (!ffmpegAvailable()) {
      fError = "ffmpeg not found in PATH";
      return false;
    }
    // The frames arrive on stdin; the audio, if there is any, is a second
    // input read from the file it was written to.
    std::string cmd = std::format(
        "ffmpeg -y -loglevel error -f rawvideo -pixel_format rgba "
        "-video_size {}x{} -framerate {} -i -",
        opts.fWidth, opts.fHeight, opts.fFps);
    if (!opts.fAudio.empty() && std::filesystem::exists(opts.fAudio)) {
      cmd += std::format(" -ss {:.3f} -i '{}' -c:a aac -shortest",
                         opts.fAudioOffsetSec, opts.fAudio.string());
    }
    cmd += std::format(
        " -c:v libx264 -pix_fmt yuv420p -crf 18 -preset medium '{}'",
        opts.fOutput.string());

    fPipe = ::popen(cmd.c_str(), "w");
    if (fPipe == nullptr) {
      fError = "could not start ffmpeg";
      return false;
    }
    return true;
  }

  // Called once per rendered frame with the pixels as they came off the
  // surface: tightly packed RGBA, top row first, which is what ffmpeg was
  // told to expect.
  void addFrame(std::span<const std::uint8_t> rgba) {
    if (fPipe == nullptr || rgba.empty()) {
      return;
    }
    if (std::fwrite(rgba.data(), 1, rgba.size(), fPipe) != rgba.size()) {
      fError = "ffmpeg stopped reading frames";
      ::pclose(fPipe);
      fPipe = nullptr;
      return;
    }
    ++fFrame;
  }

  [[nodiscard]] std::size_t frameCount() const noexcept { return fFrame; }
  [[nodiscard]] const std::string &error() const noexcept { return fError; }

  // Close the pipe and wait for the encoder to finish writing the file.
  [[nodiscard]] bool finish() {
    if (fPipe == nullptr) {
      if (fError.empty()) {
        fError = "the encoder was never started";
      }
      return false;
    }
    const int rc = ::pclose(fPipe);
    fPipe = nullptr;
    if (fFrame == 0) {
      fError = "no frames were rendered";
      return false;
    }
    if (rc != 0) {
      fError = std::format("ffmpeg exited with {}", rc);
      return false;
    }
    return true;
  }

  ~VideoExporter() {
    if (fPipe != nullptr) {
      ::pclose(fPipe); // an export abandoned midway still lets go of ffmpeg
    }
  }

private:
  VideoOptions fOpts;
  std::FILE *fPipe = nullptr;
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
