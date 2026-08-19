export module client.audio;

import std;
import audio;
import client.util;

export namespace client {
namespace audio_client {

inline audio::ALenum alFormat(int channels) {
  return channels == 1 ? audio::kFormatMono16 : audio::kFormatStereo16;
}

class AudioContext {
public:
  AudioContext() { this->init(); }
  ~AudioContext() { this->shutdown(); }

  [[nodiscard]] bool ok() const noexcept { return fContext != nullptr; }

private:
  audio::ALCdevice *fDevice = nullptr;
  audio::ALCcontext *fContext = nullptr;

  void init() {
    fDevice = audio::alcOpenDevice(nullptr);
    if (fDevice == nullptr)
      return;
    fContext = audio::alcCreateContext(fDevice, nullptr);
    if (fContext == nullptr) {
      audio::alcCloseDevice(fDevice);
      fDevice = nullptr;
      return;
    }
    audio::alcMakeContextCurrent(fContext);
    audio::alListenerf(audio::kListenerGain, 1.0f);
  }

  void shutdown() {
    if (fContext != nullptr) {
      audio::alcMakeContextCurrent(nullptr);
      audio::alcDestroyContext(fContext);
      fContext = nullptr;
    }
    if (fDevice != nullptr) {
      audio::alcCloseDevice(fDevice);
      fDevice = nullptr;
    }
  }
};

inline AudioContext &audioContext() {
  static AudioContext ctx;
  return ctx;
}

class AudioPlayer {
public:
  ~AudioPlayer() { this->shutdown(); }

  bool load(const std::filesystem::path &path) {
    const auto ext = detail::lowerExtension(path);
    const auto data = detail::readFile(path);
    return this->load(data, ext);
  }

  bool load(std::span<const std::uint8_t> data, std::string_view ext) {
    if (!audioContext().ok())
      return false;

    // Loading over an existing track (retry / new map) must release the old
    // buffer and source, or every restart leaks an OpenAL buffer.
    this->shutdown();

    int rate = 44100;
    int channels = 2;
    std::vector<std::int16_t> samples;

    if (ext == ".mp3")
      samples = audio::decode_mp3_memory(data, rate, channels);
    else
      samples = audio::decode_sndfile_memory(data, rate, channels);

    if (samples.empty())
      return false;

    return this->upload(samples, rate, channels);
  }

  void play() {
    if (fSource != 0)
      audio::alSourcePlay(fSource);
  }

  void setLooping(bool loop) {
    if (fSource != 0)
      audio::alSourcei(fSource, audio::kLooping, loop ? 1 : 0);
  }

  // Pause only if actually playing; resume only if actually paused.
  // alSourcePlay on a *stopped* source would restart it from zero, which is
  // exactly wrong when the map outlived its music.
  void pause() {
    if (fSource == 0)
      return;
    audio::ALint state = audio::kInitial;
    audio::alGetSourcei(fSource, audio::kSourceState, &state);
    if (state == audio::kPlaying)
      audio::alSourcePause(fSource);
  }

  void resume() {
    if (fSource == 0)
      return;
    audio::ALint state = audio::kInitial;
    audio::alGetSourcei(fSource, audio::kSourceState, &state);
    if (state == audio::kPaused)
      audio::alSourcePlay(fSource);
  }

  void stop() {
    if (fSource != 0)
      audio::alSourceStop(fSource);
  }

  // Decoded track kept as mono PCM so the menu visualiser can run its own
  // FFT (OpenAL exposes no spectrum API, unlike the BASS backend lazer uses).
  [[nodiscard]] std::span<const std::int16_t> monoSamples() const noexcept {
    return fMono;
  }
  [[nodiscard]] int sampleRate() const noexcept { return fRate; }

  [[nodiscard]] double positionSec() const {
    if (fSource == 0)
      return 0.0;
    audio::ALfloat sec = 0.0f;
    audio::alGetSourcef(fSource, audio::kSecOffset, &sec);
    return static_cast<double>(sec);
  }

  [[nodiscard]] bool playing() const {
    if (fSource == 0)
      return false;
    audio::ALint state = audio::kInitial;
    audio::alGetSourcei(fSource, audio::kSourceState, &state);
    return state == audio::kPlaying;
  }

private:
  audio::ALuint fBuffer = 0;
  audio::ALuint fSource = 0;
  std::vector<std::int16_t> fMono;
  int fRate = 0;

  bool upload(const std::vector<std::int16_t> &samples, int rate,
              int channels) {
    // Downmix a mono copy for analysis before handing the interleaved data
    // to OpenAL (one extra int16 per frame; a five-minute track costs ~26 MB).
    fRate = rate;
    fMono.clear();
    if (channels <= 1) {
      fMono = samples;
    } else {
      fMono.reserve(samples.size() / static_cast<std::size_t>(channels));
      for (std::size_t i = 0; i + static_cast<std::size_t>(channels) <=
                              samples.size();
           i += static_cast<std::size_t>(channels)) {
        int sum = 0;
        for (int c = 0; c < channels; ++c) {
          sum += samples[i + static_cast<std::size_t>(c)];
        }
        fMono.push_back(static_cast<std::int16_t>(sum / channels));
      }
    }

    audio::alGenBuffers(1, &fBuffer);
    audio::alBufferData(
        fBuffer, alFormat(channels), samples.data(),
        static_cast<audio::ALsizei>(samples.size() * sizeof(std::int16_t)),
        rate);
    audio::alGenSources(1, &fSource);
    audio::alSourcei(fSource, audio::kBuffer,
                     static_cast<audio::ALint>(fBuffer));
    return true;
  }

  void shutdown() {
    if (fSource != 0) {
      audio::alDeleteSources(1, &fSource);
      fSource = 0;
    }
    if (fBuffer != 0) {
      audio::alDeleteBuffers(1, &fBuffer);
      fBuffer = 0;
    }
    fMono.clear();
    fMono.shrink_to_fit();
    fRate = 0;
  }
};

class SamplePlayer {
public:
  ~SamplePlayer() { this->shutdown(); }

  bool load(const std::filesystem::path &path) {
    const auto ext = detail::lowerExtension(path);
    const auto data = detail::readFile(path);
    return this->load(data, ext);
  }

  bool load(std::span<const std::uint8_t> data, std::string_view ext) {
    if (fBuffer != 0)
      return true;
    if (!audioContext().ok())
      return false;

    int rate = 44100;
    int channels = 2;
    std::vector<std::int16_t> samples;

    if (ext == ".mp3")
      samples = audio::decode_mp3_memory(data, rate, channels);
    else
      samples = audio::decode_sndfile_memory(data, rate, channels);

    if (samples.empty())
      return false;

    return this->upload(samples, rate, channels);
  }

  [[nodiscard]] bool loaded() const noexcept { return fBuffer != 0; }

  void play() {
    if (fBuffer == 0 || fSources.empty() || !audioContext().ok())
      return;
    for (audio::ALuint source : fSources) {
      audio::ALint state = audio::kInitial;
      audio::alGetSourcei(source, audio::kSourceState, &state);
      if (state != audio::kPlaying) {
        audio::alSourcei(source, audio::kBuffer,
                         static_cast<audio::ALint>(fBuffer));
        audio::alSourcePlay(source);
        return;
      }
    }
  }

private:
  audio::ALuint fBuffer = 0;
  std::vector<audio::ALuint> fSources;

  bool upload(const std::vector<std::int16_t> &samples, int rate,
              int channels) {
    audio::alGenBuffers(1, &fBuffer);
    audio::alBufferData(
        fBuffer, alFormat(channels), samples.data(),
        static_cast<audio::ALsizei>(samples.size() * sizeof(std::int16_t)),
        rate);

    constexpr std::size_t kPoolSize = 8;
    fSources.resize(kPoolSize, 0);
    audio::alGenSources(static_cast<audio::ALsizei>(fSources.size()),
                        fSources.data());
    return true;
  }

  void shutdown() {
    if (!fSources.empty()) {
      audio::alDeleteSources(static_cast<audio::ALsizei>(fSources.size()),
                             fSources.data());
      fSources.clear();
    }
    if (fBuffer != 0) {
      audio::alDeleteBuffers(1, &fBuffer);
      fBuffer = 0;
    }
  }
};

} // namespace audio_client
} // namespace client
