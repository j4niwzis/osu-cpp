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

#ifdef OSU_VIDEO_LIBAV

// Encoded in this process by libavcodec: no ffmpeg to find, nothing to spawn,
// and a build that can be a browser one day. The video stream is encoded from
// the frames as they arrive; the beatmap's audio is copied through as it is,
// packet for packet, since re-encoding sound that is already compressed is
// work spent to make it slightly worse.
class VideoExporter {
public:
  // The encoder is linked in, so there is nothing to look for.
  [[nodiscard]] static bool ffmpegAvailable() { return true; }

  VideoExporter() = default;
  VideoExporter(const VideoExporter &) = delete;
  VideoExporter &operator=(const VideoExporter &) = delete;
  ~VideoExporter() { this->cleanup(); }

  [[nodiscard]] bool begin(const VideoOptions &opts) {
    fOpts = opts;
    fFrame = 0;
    const std::string output = opts.fOutput.string();

    if (avformat_alloc_output_context2(&fFormat, nullptr, nullptr,
                                       output.c_str()) < 0 ||
        fFormat == nullptr) {
      fError = "cannot work out a container for " + output;
      return false;
    }

    const AVCodec *codec = avcodec_find_encoder(AV_CODEC_ID_H264);
    if (codec == nullptr) {
      codec = avcodec_find_encoder(fFormat->oformat->video_codec);
    }
    if (codec == nullptr) {
      fError = "this libavcodec has no encoder for this container";
      return false;
    }

    fVideoStream = avformat_new_stream(fFormat, nullptr);
    fCodec = avcodec_alloc_context3(codec);
    if (fVideoStream == nullptr || fCodec == nullptr) {
      fError = "cannot allocate the video stream";
      return false;
    }
    fCodec->width = opts.fWidth;
    fCodec->height = opts.fHeight;
    fCodec->time_base = AVRational{1, opts.fFps};
    fCodec->framerate = AVRational{opts.fFps, 1};
    fCodec->pix_fmt = AV_PIX_FMT_YUV420P;
    fCodec->gop_size = opts.fFps * 2;
    if ((fFormat->oformat->flags & AVFMT_GLOBALHEADER) != 0) {
      fCodec->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }
    // Ignored by encoders that do not have them, which is the point of
    // setting them through the options rather than the context.
    av_opt_set(fCodec->priv_data, "preset", "medium", 0);
    av_opt_set(fCodec->priv_data, "crf", "18", 0);

    if (avcodec_open2(fCodec, codec, nullptr) < 0) {
      fError = "the encoder would not open";
      return false;
    }
    avcodec_parameters_from_context(fVideoStream->codecpar, fCodec);
    fVideoStream->time_base = fCodec->time_base;

    if (!opts.fAudio.empty()) {
      this->openAudio(opts.fAudio); // absence is not fatal; silence is
    }

    if ((fFormat->oformat->flags & AVFMT_NOFILE) == 0 &&
        avio_open(&fFormat->pb, output.c_str(), AVIO_FLAG_WRITE) < 0) {
      fError = "cannot write to " + output;
      return false;
    }
    if (avformat_write_header(fFormat, nullptr) < 0) {
      fError = "the container refused its header";
      return false;
    }

    fPicture = av_frame_alloc();
    fPacket = av_packet_alloc();
    if (fPicture == nullptr || fPacket == nullptr) {
      fError = "out of memory setting up the encoder";
      return false;
    }
    fPicture->format = AV_PIX_FMT_YUV420P;
    fPicture->width = opts.fWidth;
    fPicture->height = opts.fHeight;
    if (av_frame_get_buffer(fPicture, 0) < 0) {
      fError = "cannot allocate a frame to encode into";
      return false;
    }

    fSws = sws_getContext(opts.fWidth, opts.fHeight, AV_PIX_FMT_RGBA,
                          opts.fWidth, opts.fHeight, AV_PIX_FMT_YUV420P,
                          SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (fSws == nullptr) {
      fError = "cannot convert frames to the encoder's format";
      return false;
    }
    return true;
  }

  // Tightly packed RGBA, top row first.
  void addFrame(std::span<const std::uint8_t> rgba) {
    const std::size_t needed = static_cast<std::size_t>(fOpts.fWidth) *
                               static_cast<std::size_t>(fOpts.fHeight) * 4u;
    if (fSws == nullptr || rgba.size() < needed) {
      return;
    }
    if (av_frame_make_writable(fPicture) < 0) {
      return;
    }
    const std::uint8_t *source[1] = {rgba.data()};
    const int stride[1] = {fOpts.fWidth * 4};
    sws_scale(fSws, source, stride, 0, fOpts.fHeight, fPicture->data,
              fPicture->linesize);
    fPicture->pts = fPts++;
    this->encode(fPicture);
    ++fFrame;
    // Sound is written alongside the picture rather than all at the end: the
    // muxer interleaves what it is given, and giving it an hour of audio in
    // one go means holding an hour of audio.
    this->pumpAudio(static_cast<double>(fPts) /
                    static_cast<double>(fOpts.fFps));
  }

  [[nodiscard]] std::size_t frameCount() const noexcept { return fFrame; }
  [[nodiscard]] const std::string &error() const noexcept { return fError; }

  [[nodiscard]] bool finish() {
    if (fFormat == nullptr) {
      if (fError.empty()) {
        fError = "the encoder was never started";
      }
      return false;
    }
    if (fFrame == 0) {
      fError = "no frames were rendered";
      this->cleanup();
      return false;
    }
    this->encode(nullptr); // drain
    this->pumpAudio(std::numeric_limits<double>::max());
    const bool ok = av_write_trailer(fFormat) >= 0;
    if (!ok && fError.empty()) {
      fError = "the container refused its trailer";
    }
    this->cleanup();
    return ok;
  }

private:
  void encode(AVFrame *picture) {
    if (avcodec_send_frame(fCodec, picture) < 0) {
      return;
    }
    for (;;) {
      const int rc = avcodec_receive_packet(fCodec, fPacket);
      if (rc == AVERROR(EAGAIN) || rc == AVERROR_EOF || rc < 0) {
        return;
      }
      av_packet_rescale_ts(fPacket, fCodec->time_base,
                           fVideoStream->time_base);
      fPacket->stream_index = fVideoStream->index;
      av_interleaved_write_frame(fFormat, fPacket);
      av_packet_unref(fPacket);
    }
  }

  // The beatmap's audio, copied rather than re-encoded. A container that will
  // not take that codec gets no audio at all, which is better than a file
  // that will not play.
  void openAudio(const std::filesystem::path &path) {
    const std::string name = path.string();
    if (avformat_open_input(&fAudioIn, name.c_str(), nullptr, nullptr) < 0) {
      fAudioIn = nullptr;
      return;
    }
    if (avformat_find_stream_info(fAudioIn, nullptr) < 0) {
      this->closeAudio();
      return;
    }
    fAudioIndex =
        av_find_best_stream(fAudioIn, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (fAudioIndex < 0) {
      this->closeAudio();
      return;
    }
    AVStream *source = fAudioIn->streams[fAudioIndex];
    if (avformat_query_codec(fFormat->oformat, source->codecpar->codec_id,
                             FF_COMPLIANCE_NORMAL) != 1) {
      this->closeAudio(); // e.g. Vorbis into mp4, which is not a thing
      return;
    }
    fAudioStream = avformat_new_stream(fFormat, nullptr);
    if (fAudioStream == nullptr ||
        avcodec_parameters_copy(fAudioStream->codecpar, source->codecpar) < 0) {
      this->closeAudio();
      return;
    }
    fAudioStream->codecpar->codec_tag = 0;
    fAudioStream->time_base = source->time_base;
  }

  void pumpAudio(double untilSeconds) {
    if (fAudioIn == nullptr || fAudioStream == nullptr || fAudioDone) {
      return;
    }
    AVStream *source = fAudioIn->streams[fAudioIndex];
    for (;;) {
      if (fAudioPacket == nullptr) {
        fAudioPacket = av_packet_alloc();
        if (fAudioPacket == nullptr) {
          fAudioDone = true;
          return;
        }
        if (av_read_frame(fAudioIn, fAudioPacket) < 0) {
          av_packet_free(&fAudioPacket);
          fAudioDone = true;
          return;
        }
        if (fAudioPacket->stream_index != fAudioIndex) {
          av_packet_unref(fAudioPacket);
          av_packet_free(&fAudioPacket);
          continue;
        }
      }
      const double when =
          fAudioPacket->pts == AV_NOPTS_VALUE
              ? 0.0
              : static_cast<double>(fAudioPacket->pts) *
                    av_q2d(source->time_base);
      if (when > untilSeconds) {
        return; // it belongs to a part of the video that is not encoded yet
      }
      av_packet_rescale_ts(fAudioPacket, source->time_base,
                           fAudioStream->time_base);
      fAudioPacket->stream_index = fAudioStream->index;
      av_interleaved_write_frame(fFormat, fAudioPacket);
      av_packet_free(&fAudioPacket);
    }
  }

  void closeAudio() {
    if (fAudioPacket != nullptr) {
      av_packet_free(&fAudioPacket);
    }
    if (fAudioIn != nullptr) {
      avformat_close_input(&fAudioIn);
    }
    fAudioIn = nullptr;
    fAudioIndex = -1;
    fAudioDone = true;
  }

  void cleanup() {
    this->closeAudio();
    if (fSws != nullptr) {
      sws_freeContext(fSws);
      fSws = nullptr;
    }
    if (fPicture != nullptr) {
      av_frame_free(&fPicture);
    }
    if (fPacket != nullptr) {
      av_packet_free(&fPacket);
    }
    if (fCodec != nullptr) {
      avcodec_free_context(&fCodec);
    }
    if (fFormat != nullptr) {
      if ((fFormat->oformat->flags & AVFMT_NOFILE) == 0 &&
          fFormat->pb != nullptr) {
        avio_closep(&fFormat->pb);
      }
      avformat_free_context(fFormat);
      fFormat = nullptr;
    }
    fVideoStream = nullptr;
    fAudioStream = nullptr;
  }

  VideoOptions fOpts;
  AVFormatContext *fFormat = nullptr;
  AVCodecContext *fCodec = nullptr;
  AVStream *fVideoStream = nullptr;
  AVStream *fAudioStream = nullptr;
  AVFormatContext *fAudioIn = nullptr;
  AVPacket *fAudioPacket = nullptr;
  AVPacket *fPacket = nullptr;
  AVFrame *fPicture = nullptr;
  SwsContext *fSws = nullptr;
  int fAudioIndex = -1;
  bool fAudioDone = false;
  std::int64_t fPts = 0;
  std::size_t fFrame = 0;
  std::string fError;
};

#else

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

#endif

// Resolutions offered in the export dialog.
struct VideoPreset {
  const char *fLabel;
  int fWidth, fHeight;
};

inline constexpr std::array<VideoPreset, 4> kVideoPresets = {
    VideoPreset{"720p", 1280, 720}, VideoPreset{"1080p", 1920, 1080},
    VideoPreset{"1440p", 2560, 1440}, VideoPreset{"4K", 3840, 2160}};

} // namespace client
