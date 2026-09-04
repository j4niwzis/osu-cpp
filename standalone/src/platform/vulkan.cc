module;

export module platform.vulkan;

import std;
import skia;

export namespace platform::vulkan {

// The Vulkan presenter, where there is none.
//
// Which backends a build has is decided when it is configured -- Graphite on
// Vulkan is a component of Skia, and a Skia without it has no such thing to
// name -- so the alternative to compiling the presenter is this: the same
// calls, answering that there is nothing here. The client asks and is told
// no, once, and goes on drawing the way it always did.
//
// The alternative is an ifdef around every use, in a file that has none.

bool supported() { return false; }

// Nothing to install: this build has no drivers in it, and no backend that
// would ask them anything.
void installLoader() {}

class Presenter {
public:
  Presenter() = default;
  Presenter(const Presenter &) = delete;
  Presenter &operator=(const Presenter &) = delete;

  [[nodiscard]] bool start(void *, int, int) { return false; }
  void setWaitForDisplay(bool) {}
  void stop() {}
  [[nodiscard]] bool running() const { return false; }
  [[nodiscard]] skia::Sp<skia::SkSurface> beginFrame(int, int) {
    return nullptr;
  }
  [[nodiscard]] bool endFrame(skia::SkSurface *) { return false; }
  [[nodiscard]] skia::Sp<skia::SkSurface> offscreen(int, int) {
    return nullptr;
  }
  void finish(skia::SkSurface *) {}
  [[nodiscard]] bool takeStale() { return false; }
  [[nodiscard]] int bufferAge() const { return 0; }
};

} // namespace platform::vulkan
