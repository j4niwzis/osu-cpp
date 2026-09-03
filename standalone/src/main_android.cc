import std;
import osu;
import app;
import platform.android.api;
import platform.android_runtime;
import platform.system;

namespace {

// Everything this program says, where a phone can be asked to show it.
//
// Android drops what an application writes to its standard output; a client
// whose whole account of itself goes to stderr therefore says nothing at all
// on a phone, and every question about what it did there had no answer. The
// two streams are pointed into a pipe and a thread reads them back out, a
// line at a time, into the system log under this program's name.
//
// It is started before anything else and never stopped: what it costs is one
// thread doing nothing, and what it buys is that "it crashed" can be
// followed by "here is what it was doing".
void sayEverythingToTheSystemLog() {
  static int ends[2];
  if (pipe(ends) != 0) {
    return;
  }
  setvbuf(stdout, nullptr, kLineBuffered, 0);
  setvbuf(stderr, nullptr, kUnbuffered, 0);
  dup2(ends[1], 1);
  dup2(ends[1], 2);
  std::thread([] {
    std::string line;
    char byte = 0;
    while (read(ends[0], &byte, 1) == 1) {
      if (byte == '\n') {
        __android_log_write(ANDROID_LOG_INFO, "osu!cpp", line.c_str());
        line.clear();
        continue;
      }
      if (byte != '\r') {
        line.push_back(byte);
      }
      // A line nobody terminates is still worth reading, so it is not kept
      // for ever.
      if (line.size() >= 1024) {
        __android_log_write(ANDROID_LOG_INFO, "osu!cpp", line.c_str());
        line.clear();
      }
    }
  }).detach();
}

} // namespace

extern "C" void android_main(android_app *state) {
  sayEverythingToTheSystemLog();
  platform::android::attach(state);

  try {
    (void)platform::android::enterImmersiveMode();
    if (!platform::android::prepareAssets()) {
      throw std::runtime_error("unable to prepare bundled assets");
    }
    const std::filesystem::path skinPath =
        platform::system::applicationDataPath() / "skin";
    std::error_code error;
    std::filesystem::create_directories(skinPath, error);
    if (error) {
      throw std::runtime_error(
          std::format("unable to create skin directory {}: {}",
                      skinPath.string(), error.message()));
    }
    client::App app(std::nullopt, osu::mod::kNone, false, false, {}, false,
                    skinPath, false, true);
    (void)app.run();
    ANativeActivity_finish(state->activity);
  } catch (const std::exception &error) {
    std::println(std::cerr, "Android startup failed: {}", error.what());
    ANativeActivity_finish(state->activity);
  }
}
