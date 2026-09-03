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

// The home directory, kept in the browser's own storage between visits.
//
// All of it, rather than the maps alone: the settings file, the replays and
// the skin sit beside the library under the same home, and a browser that
// keeps one of those and forgets the rest is a client that starts from
// nothing every time except its beatmaps. One mount, and every path the
// client already uses is persistent.
//
// Every step of this can fail -- a private window refuses IndexedDB -- and
// the one thing that must not happen is failing quietly: the client waits
// for the answer before it will show a library, so a mount that throws left
// "Syncing" on the screen for ever. Whatever happens, the answer comes back,
// and what happened is on the console.
inline void initializeMapStorage() {
  EM_ASM({
    var home = '/home/web_user';
    var mounted = false;
    console.log('storage: mounting ' + home);
    try {
      FS.mkdirTree(home);
      FS.mount(IDBFS, {}, home);
      mounted = true;
    } catch (e) {
      console.error('storage: cannot mount ' + home + ': ' + e);
    }
    if (!mounted) {
      Module._osu_maps_synced();
      return;
    }
    FS.syncfs(true, function(err) {
      if (err) {
        console.error('storage: cannot read what was kept: ' + err);
      } else {
        console.log('storage: ' + home + ' is what was kept last time');
      }
      Module._osu_maps_synced();
    });
  });
}

// Not inline, and neither is the one below.
//
// Both read state that lives in this file's anonymous namespace, and an
// exported inline function that touches a name only this translation unit
// has is a function every importer compiles its own copy of, against its
// own copy of that name. The callback set the flag here and the client read
// one that was false for ever: "Syncing local storage..." with the storage
// mounted, read and reported on the console.
[[nodiscard]] bool mapStorageReady();

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
[[nodiscard]] std::vector<std::string> takePendingImports();

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
// How large the canvas is being shown, in CSS pixels.
//
// Not multiplied by the display's ratio. Drawing at the device's own
// resolution is four times the pixels on a ratio of two -- which is what the
// frame rate went to -- and everything the client draws is sized in the
// pixels it is given, so the whole interface came out half the size it had
// been. A browser scales the result for us; this is the size to draw.
[[nodiscard]] inline CanvasExtent canvasExtent() {
  return {EM_ASM_INT({
            var canvas = Module.canvas;
            return Math.max(1, Math.round(canvas.clientWidth || canvas.width));
          }),
          EM_ASM_INT({
            var canvas = Module.canvas;
            return Math.max(1, Math.round(canvas.clientHeight || canvas.height));
          })};
}

} // namespace platform::web

// One definition each, in the module rather than in everyone who imports it.
bool platform::web::mapStorageReady() {
  return gMapStorageReady.load(std::memory_order_acquire);
}

std::vector<std::string> platform::web::takePendingImports() {
  const std::scoped_lock lock(gImportMutex);
  return std::exchange(gPendingImports, std::vector<std::string>{});
}
