module;

#include "emscripten_macro.h"

export module platform.web_runtime;

import std;

namespace {
std::atomic<bool> gMapStorageReady{false};
std::mutex gImportMutex;
std::vector<std::string> gPendingImports;
}

extern "C" EMSCRIPTEN_KEEPALIVE void osu_maps_synced() {
  gMapStorageReady.store(true, std::memory_order_release);
}

// An archive the page has written into the module's filesystem. The picker
// is the browser's -- a file input the user chooses in -- and it answers
// whenever the user gets round to it, so the path is queued and the client
// takes it on a frame of its own.
extern "C" EMSCRIPTEN_KEEPALIVE void osu_import_archive(const char *path) {
  if (path == nullptr || *path == '\0') {
    return;
  }
  const std::scoped_lock lock(gImportMutex);
  gPendingImports.emplace_back(path);
}

export namespace platform::web {

using FrameCallback = void (*)(void *);
struct CanvasExtent { int fWidth = 0; int fHeight = 0; };

// The maps directory, kept in the browser's own storage between visits.
//
// Every step of this can fail -- the directory may already be mounted, a
// private window may refuse IndexedDB -- and the one thing that must not
// happen is failing quietly: the client waits for the answer before it will
// draw a library, so a mount that throws left "Syncing" on the screen for
// ever. Whatever happens, the answer comes back, and what went wrong is on
// the console.
inline void initializeMapStorage() {
  EM_ASM({
    var mounted = false;
    try {
      try {
        FS.mkdir('/maps');
      } catch (e) {
        // It is there already, which is the usual case on a second call.
      }
      FS.mount(IDBFS, {}, '/maps');
      mounted = true;
    } catch (e) {
      console.error('map storage: ' + e);
    }
    if (!mounted) {
      Module._osu_maps_synced();
      return;
    }
    FS.syncfs(true, function(err) {
      if (err) {
        console.error('map storage: ' + err);
      }
      Module._osu_maps_synced();
    });
  });
}

[[nodiscard]] inline bool mapStorageReady() {
  return gMapStorageReady.load(std::memory_order_acquire);
}

inline void syncMapStorage() {
  EM_ASM({
    try {
      FS.syncfs(false, function(err) {
        if (err) {
          console.error('map storage: ' + err);
        }
      });
    } catch (e) {
      console.error('map storage: ' + e);
    }
  });
}

inline void requestBeatmapArchive() {
  EM_ASM({
    if (Module.osuPickBeatmap) {
      Module.osuPickBeatmap();
    } else {
      console.error('this page has no beatmap picker');
    }
  });
}

// What the page has put there since the last frame asked.
[[nodiscard]] inline std::vector<std::string> takePendingImports() {
  const std::scoped_lock lock(gImportMutex);
  return std::exchange(gPendingImports, std::vector<std::string>{});
}

inline void setCursorVisible(bool visible) {
  EM_ASM({ Module.setCursorVisible(!!$0); }, visible ? 1 : 0);
}

inline void runMainLoop(FrameCallback callback, void *context) {
  emscripten_set_main_loop_arg(callback, context, 0, 1);
}

inline void cancelMainLoop() { emscripten_cancel_main_loop(); }
// How large the canvas is being shown, in the pixels a drawing goes into.
//
// Not canvas.width: that is the size of the buffer behind it, which is what
// this build sets in answer to this question. The page sizes the element
// with CSS and says nothing else, so the element's laid-out size times the
// display's ratio is what the client should be drawing.
[[nodiscard]] inline CanvasExtent canvasExtent() {
  return {EM_ASM_INT({
            var canvas = Module.canvas;
            var ratio = window.devicePixelRatio || 1;
            var shown = canvas.clientWidth || canvas.width;
            return Math.max(1, Math.round(shown * ratio));
          }),
          EM_ASM_INT({
            var canvas = Module.canvas;
            var ratio = window.devicePixelRatio || 1;
            var shown = canvas.clientHeight || canvas.height;
            return Math.max(1, Math.round(shown * ratio));
          })};
}

} // namespace platform::web
