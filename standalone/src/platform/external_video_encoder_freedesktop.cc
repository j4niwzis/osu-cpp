module;

#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

extern "C" char **environ;

export module platform.external_video_encoder;

import std;

export namespace platform {

class ExternalVideoEncoder {
public:
  ExternalVideoEncoder() = default;
  ExternalVideoEncoder(const ExternalVideoEncoder &) = delete;
  ExternalVideoEncoder &operator=(const ExternalVideoEncoder &) = delete;
  ~ExternalVideoEncoder() { this->abandon(); }

  [[nodiscard]] static bool available() {
    const char *pathValue = std::getenv("PATH");
    if (pathValue == nullptr) {
      return false;
    }
    std::string_view remaining = pathValue;
    for (;;) {
      const auto colon = remaining.find(':');
      const auto directory = remaining.substr(0, colon);
      const auto candidate =
          (std::filesystem::path(directory.empty() ? "." : directory) /
           "ffmpeg")
              .string();
      if (::access(candidate.c_str(), X_OK) == 0) {
        return true;
      }
      if (colon == std::string_view::npos) {
        return false;
      }
      remaining.remove_prefix(colon + 1);
    }
  }

  [[nodiscard]] bool begin(int width, int height, int fps,
                           const std::filesystem::path &audio,
                           double audioOffsetSec,
                           const std::filesystem::path &output) {
    this->abandon();
    fFrames = 0;
    fError.clear();

    // A failed encoder closes stdin. Treat that as a write error which the
    // exporter can report, not as a process-wide SIGPIPE termination.
    ::signal(SIGPIPE, SIG_IGN);

    std::vector<std::string> args{
        "ffmpeg",       "-y",        "-loglevel", "error",
        "-f",           "rawvideo",  "-pixel_format", "rgba",
        "-video_size",  std::format("{}x{}", width, height),
        "-framerate",   std::to_string(fps), "-i", "-"};
    if (!audio.empty() && std::filesystem::exists(audio)) {
      args.insert(args.end(), {"-ss", std::format("{:.3f}", audioOffsetSec),
                               "-i", audio.string(), "-c:a", "aac",
                               "-shortest"});
    }
    args.insert(args.end(), {"-c:v", "libx264", "-pix_fmt", "yuv420p",
                             "-crf", "18", "-preset", "medium",
                             output.string()});
    std::vector<char *> argv;
    argv.reserve(args.size() + 1);
    for (auto &arg : args) {
      argv.push_back(arg.data());
    }
    argv.push_back(nullptr);

    int input[2] = {-1, -1};
    if (::pipe(input) != 0) {
      fError = std::format("could not create ffmpeg input: {}",
                           std::generic_category().message(errno));
      return false;
    }
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, input[0], STDIN_FILENO);
    posix_spawn_file_actions_addclose(&actions, input[0]);
    posix_spawn_file_actions_addclose(&actions, input[1]);
    const int result = ::posix_spawnp(&fPid, "ffmpeg", &actions, nullptr,
                                      argv.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    ::close(input[0]);
    if (result != 0) {
      ::close(input[1]);
      fPid = -1;
      fError = std::format("could not start ffmpeg: {}",
                           std::generic_category().message(result));
      return false;
    }
    fInput = input[1];
    return true;
  }

  void addFrame(std::span<const std::uint8_t> bytes) {
    std::size_t offset = 0;
    while (fInput >= 0 && offset < bytes.size()) {
      const auto written =
          ::write(fInput, bytes.data() + offset, bytes.size() - offset);
      if (written > 0) {
        offset += static_cast<std::size_t>(written);
      } else if (written < 0 && errno == EINTR) {
        continue;
      } else {
        fError = "ffmpeg stopped reading frames";
        this->closeInput();
      }
    }
    if (offset == bytes.size() && !bytes.empty()) {
      ++fFrames;
    }
  }

  [[nodiscard]] bool finish() {
    if (fPid < 0) {
      if (fError.empty()) {
        fError = "the encoder was never started";
      }
      return false;
    }
    this->closeInput();
    int status = 0;
    int waited = -1;
    do {
      waited = ::waitpid(fPid, &status, 0);
    } while (waited < 0 && errno == EINTR);
    fPid = -1;
    if (fFrames == 0) {
      fError = "no frames were rendered";
      return false;
    }
    if (waited < 0) {
      fError = std::format("could not wait for ffmpeg: {}",
                           std::generic_category().message(errno));
      return false;
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
      fError = WIFEXITED(status)
                   ? std::format("ffmpeg exited with {}", WEXITSTATUS(status))
                   : "ffmpeg was terminated";
      return false;
    }
    return true;
  }

  [[nodiscard]] std::size_t frameCount() const noexcept { return fFrames; }
  [[nodiscard]] const std::string &error() const noexcept { return fError; }

private:
  void closeInput() {
    if (fInput >= 0) {
      ::close(std::exchange(fInput, -1));
    }
  }
  void abandon() {
    this->closeInput();
    if (fPid >= 0) {
      int status = 0;
      while (::waitpid(fPid, &status, 0) < 0 && errno == EINTR) {
      }
      fPid = -1;
    }
  }

  int fInput = -1;
  pid_t fPid = -1;
  std::size_t fFrames = 0;
  std::string fError;
};

} // namespace platform
