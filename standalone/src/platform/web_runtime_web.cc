module;

#include "emscripten_macro.h"

export module platform.web_runtime;

import std;

namespace {
std::atomic<bool> gMapStorageReady{false};
}

extern "C" EMSCRIPTEN_KEEPALIVE void osu_maps_synced() {
  gMapStorageReady.store(true, std::memory_order_release);
}

export namespace platform::web {

using FrameCallback = void (*)(void *);
struct CanvasExtent { int fWidth = 0; int fHeight = 0; };

inline void initializeMapStorage() {
  EM_ASM({
    try {
      FS.mkdir('/maps');
    } catch (e) {
    }
    FS.mount(IDBFS, {}, '/maps');
    FS.syncfs(true, function(err) { Module._osu_maps_synced(); });
  });
}

[[nodiscard]] inline bool mapStorageReady() {
  return gMapStorageReady.load(std::memory_order_acquire);
}

inline void syncMapStorage() {
  EM_ASM(FS.syncfs(false, function(err){}));
}

inline void requestBeatmapArchive() {
  EM_ASM({
    if (Module.osuPickBeatmap)
      Module.osuPickBeatmap();
  });
}

inline void setCursorVisible(bool visible) {
  EM_ASM({ Module.setCursorVisible(!!$0); }, visible ? 1 : 0);
}

inline void runMainLoop(FrameCallback callback, void *context) {
  emscripten_set_main_loop_arg(callback, context, 0, 1);
}

inline void cancelMainLoop() { emscripten_cancel_main_loop(); }
[[nodiscard]] inline CanvasExtent canvasExtent() {
  return {EM_ASM_INT({ return Module.canvas.width; }),
          EM_ASM_INT({ return Module.canvas.height; })};
}

} // namespace platform::web
