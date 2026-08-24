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
#include <libavutil/audio_fifo.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
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
// the frames as they arrive; the beatmap's audio is copied through packet for
// packet when the container speaks its codec and re-encoded to AAC when it
// does not, which for a beatmap's Vorbis in an mp4 is always.
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

    // Prefer the software encoder packaged by the Flatpak. Generic lookup can
    // return a hardware wrapper first; opening that fails on devices without
    // the matching kernel codec even though libx264 is available beside it.
    const AVCodec *codec = avcodec_find_encoder_by_name("libx264");
    if (codec == nullptr) {
      codec = avcodec_find_encoder(AV_CODEC_ID_H264);
    }
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
    // Up to the last picture and no further: the beatmap's mp3 outlives the
    // replay by whatever tail the track has, and draining all of it here left
    // a file whose video stopped seven seconds before its sound did. This is
    // what -shortest does on the pipe path.
    this->pumpAudio(static_cast<double>(fPts) /
                    static_cast<double>(fOpts.fFps));
    if (!fAudioCopy) {
      this->drainAudioFifo(true); // and whatever the queue still holds
    }
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

  // The beatmap's audio, re-encoded to AAC unless it is AAC already.
  //
  // Copying it through was cheaper and produced files with a sound track that
  // nothing would play: a beatmap's audio is usually Vorbis, mp4 will accept
  // a Vorbis track without complaining, and no player outside this stack will
  // decode one. So the rule is what the container really means rather than
  // what it will tolerate.
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
    // The video starts where the replay does, not where the mp3 does, so the
    // sound is read from that point and its timestamps are moved back by it.
    fAudioShift = av_rescale_q(
        static_cast<std::int64_t>(std::llround(fOpts.fAudioOffsetSec * 1e6)),
        AVRational{1, 1000000}, source->time_base);
    if (fAudioShift > 0) {
      av_seek_frame(fAudioIn, fAudioIndex, fAudioShift, AVSEEK_FLAG_BACKWARD);
    }

    fAudioStream = avformat_new_stream(fFormat, nullptr);
    if (fAudioStream == nullptr) {
      this->closeAudio();
      return;
    }

    // Already in the container's own language: copy it and skip all of this.
    if (source->codecpar->codec_id == AV_CODEC_ID_AAC ||
        source->codecpar->codec_id == AV_CODEC_ID_MP3) {
      if (avcodec_parameters_copy(fAudioStream->codecpar, source->codecpar) <
          0) {
        this->closeAudio();
        return;
      }
      fAudioStream->codecpar->codec_tag = 0;
      fAudioStream->time_base = source->time_base;
      fAudioCopy = true;
      return;
    }

    const AVCodec *decoder = avcodec_find_decoder(source->codecpar->codec_id);
    const AVCodec *encoder = avcodec_find_encoder(AV_CODEC_ID_AAC);
    if (decoder == nullptr || encoder == nullptr) {
      this->closeAudio();
      return;
    }
    fAudioDec = avcodec_alloc_context3(decoder);
    fAudioEnc = avcodec_alloc_context3(encoder);
    if (fAudioDec == nullptr || fAudioEnc == nullptr ||
        avcodec_parameters_to_context(fAudioDec, source->codecpar) < 0 ||
        avcodec_open2(fAudioDec, decoder, nullptr) < 0) {
      this->closeAudio();
      return;
    }

    fAudioEnc->sample_rate = fAudioDec->sample_rate;
    av_channel_layout_default(&fAudioEnc->ch_layout,
                              fAudioDec->ch_layout.nb_channels > 1 ? 2 : 1);
    fAudioEnc->sample_fmt = AV_SAMPLE_FMT_FLTP;
    fAudioEnc->bit_rate = 192000;
    fAudioEnc->time_base = AVRational{1, fAudioEnc->sample_rate};
    if ((fFormat->oformat->flags & AVFMT_GLOBALHEADER) != 0) {
      fAudioEnc->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }
    if (avcodec_open2(fAudioEnc, encoder, nullptr) < 0 ||
        avcodec_parameters_from_context(fAudioStream->codecpar, fAudioEnc) <
            0) {
      this->closeAudio();
      return;
    }
    fAudioStream->time_base = fAudioEnc->time_base;

    if (swr_alloc_set_opts2(&fSwr, &fAudioEnc->ch_layout,
                            fAudioEnc->sample_fmt, fAudioEnc->sample_rate,
                            &fAudioDec->ch_layout, fAudioDec->sample_fmt,
                            fAudioDec->sample_rate, 0, nullptr) < 0 ||
        swr_init(fSwr) < 0) {
      this->closeAudio();
      return;
    }
    // An encoder wants a fixed number of samples per frame and a decoder does
    // not produce them in that size, so they meet in a queue.
    fFifo = av_audio_fifo_alloc(fAudioEnc->sample_fmt,
                                fAudioEnc->ch_layout.nb_channels, 1);
    fAudioFrame = av_frame_alloc();
    fAudioPacketOut = av_packet_alloc();
    if (fFifo == nullptr || fAudioFrame == nullptr ||
        fAudioPacketOut == nullptr) {
      this->closeAudio();
    }
  }

  // Everything in the queue that makes a whole frame, encoded and written.
  void drainAudioFifo(bool flush) {
    if (fFifo == nullptr) {
      return;
    }
    const int frameSize = fAudioEnc->frame_size > 0 ? fAudioEnc->frame_size
                                                    : 1024;
    while (av_audio_fifo_size(fFifo) >= (flush ? 1 : frameSize)) {
      const int take = std::min(frameSize, av_audio_fifo_size(fFifo));
      av_frame_unref(fAudioFrame);
      fAudioFrame->nb_samples = take;
      fAudioFrame->format = fAudioEnc->sample_fmt;
      fAudioFrame->sample_rate = fAudioEnc->sample_rate;
      if (av_channel_layout_copy(&fAudioFrame->ch_layout,
                                 &fAudioEnc->ch_layout) < 0 ||
          av_frame_get_buffer(fAudioFrame, 0) < 0) {
        return;
      }
      if (av_audio_fifo_read(fFifo,
                             reinterpret_cast<void **>(fAudioFrame->data),
                             take) < take) {
        return;
      }
      fAudioFrame->pts = fAudioPts;
      fAudioPts += take;
      this->encodeAudio(fAudioFrame);
    }
    if (flush) {
      this->encodeAudio(nullptr);
    }
  }

  void encodeAudio(AVFrame *frame) {
    if (fAudioEnc == nullptr ||
        avcodec_send_frame(fAudioEnc, frame) < 0) {
      return;
    }
    for (;;) {
      const int rc = avcodec_receive_packet(fAudioEnc, fAudioPacketOut);
      if (rc < 0) {
        return;
      }
      av_packet_rescale_ts(fAudioPacketOut, fAudioEnc->time_base,
                           fAudioStream->time_base);
      fAudioPacketOut->stream_index = fAudioStream->index;
      av_interleaved_write_frame(fFormat, fAudioPacketOut);
      av_packet_unref(fAudioPacketOut);
    }
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
              : static_cast<double>(fAudioPacket->pts - fAudioShift) *
                    av_q2d(source->time_base);
      if (when > untilSeconds) {
        return; // it belongs to a part of the video that is not encoded yet
      }
      if (fAudioCopy) {
        // A seek lands on a packet boundary at or before the point asked for,
        // so what is left of the run-up is dropped here.
        const double ends =
            when + static_cast<double>(fAudioPacket->duration) *
                       av_q2d(source->time_base);
        if (ends <= 0.0) {
          av_packet_free(&fAudioPacket);
          continue;
        }
        if (fAudioPacket->pts != AV_NOPTS_VALUE) {
          fAudioPacket->pts -= fAudioShift;
        }
        if (fAudioPacket->dts != AV_NOPTS_VALUE) {
          fAudioPacket->dts -= fAudioShift;
        }
        av_packet_rescale_ts(fAudioPacket, source->time_base,
                             fAudioStream->time_base);
        fAudioPacket->stream_index = fAudioStream->index;
        av_interleaved_write_frame(fFormat, fAudioPacket);
        av_packet_free(&fAudioPacket);
        continue;
      }
      // Decode, resample, queue, and write out whole encoder frames.
      if (avcodec_send_packet(fAudioDec, fAudioPacket) >= 0) {
        AVFrame *decoded = av_frame_alloc();
        while (decoded != nullptr &&
               avcodec_receive_frame(fAudioDec, decoded) == 0) {
          const int maxOut = static_cast<int>(av_rescale_rnd(
              swr_get_delay(fSwr, fAudioDec->sample_rate) + decoded->nb_samples,
              fAudioEnc->sample_rate, fAudioDec->sample_rate, AV_ROUND_UP));
          if (!fSkipResolved) {
            fSkipResolved = true;
            const double at = decoded->pts == AV_NOPTS_VALUE
                                  ? fOpts.fAudioOffsetSec
                                  : static_cast<double>(decoded->pts) *
                                        av_q2d(source->time_base);
            fSkipSamples = static_cast<int>(std::max<std::int64_t>(
                0, std::llround((fOpts.fAudioOffsetSec - at) *
                                fAudioEnc->sample_rate)));
          }
          std::uint8_t **converted = nullptr;
          if (av_samples_alloc_array_and_samples(
                  &converted, nullptr, fAudioEnc->ch_layout.nb_channels, maxOut,
                  fAudioEnc->sample_fmt, 0) >= 0) {
            const int produced = swr_convert(
                fSwr, converted, maxOut,
                const_cast<const std::uint8_t **>(decoded->data),
                decoded->nb_samples);
            int offset = 0;
            if (fSkipSamples > 0) {
              offset = std::min(fSkipSamples, produced);
              fSkipSamples -= offset;
            }
            if (produced - offset > 0) {
              const int width = av_get_bytes_per_sample(fAudioEnc->sample_fmt);
              std::array<std::uint8_t *, 8> planes{};
              const int channels = std::min<int>(
                  8, fAudioEnc->ch_layout.nb_channels);
              for (int ch = 0; ch < channels; ++ch) {
                planes[static_cast<std::size_t>(ch)] =
                    converted[ch] + static_cast<std::ptrdiff_t>(offset) * width;
              }
              av_audio_fifo_write(fFifo,
                                  reinterpret_cast<void **>(planes.data()),
                                  produced - offset);
            }
            if (converted != nullptr) {
              av_freep(&converted[0]);
              av_freep(&converted);
            }
          }
          av_frame_unref(decoded);
        }
        if (decoded != nullptr) {
          av_frame_free(&decoded);
        }
      }
      av_packet_free(&fAudioPacket);
      this->drainAudioFifo(false);
    }
  }

  void closeAudio() {
    if (fAudioPacket != nullptr) {
      av_packet_free(&fAudioPacket);
    }
    if (fAudioPacketOut != nullptr) {
      av_packet_free(&fAudioPacketOut);
    }
    if (fAudioFrame != nullptr) {
      av_frame_free(&fAudioFrame);
    }
    if (fFifo != nullptr) {
      av_audio_fifo_free(fFifo);
      fFifo = nullptr;
    }
    if (fSwr != nullptr) {
      swr_free(&fSwr);
    }
    if (fAudioDec != nullptr) {
      avcodec_free_context(&fAudioDec);
    }
    if (fAudioEnc != nullptr) {
      avcodec_free_context(&fAudioEnc);
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
  AVPacket *fAudioPacketOut = nullptr;
  AVCodecContext *fAudioDec = nullptr;
  AVCodecContext *fAudioEnc = nullptr;
  AVFrame *fAudioFrame = nullptr;
  SwrContext *fSwr = nullptr;
  AVAudioFifo *fFifo = nullptr;
  std::int64_t fAudioPts = 0;
  std::int64_t fAudioShift = 0;
  int fSkipSamples = 0;
  bool fSkipResolved = false;
  bool fAudioCopy = false;
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
