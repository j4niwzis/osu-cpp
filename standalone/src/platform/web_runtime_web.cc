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

// Which way up the page is asked to be: 0 whatever it is, 1 landscape, 2
// portrait.
//
// The browser grants this only in fullscreen and only on a device that has
// an orientation to lock, and refuses with a rejected promise otherwise --
// which is the ordinary answer on a desktop and is not an error. The client
// draws the way it decided either way.
inline void lockOrientation(int kind) {
  EM_ASM({
    try {
      const kind = $0;
      if (!screen.orientation) {
        return;
      }
      if (kind === 0) {
        screen.orientation.unlock();
      } else {
        const wanted = kind === 1 ? 'landscape' : 'portrait';
        const locked = screen.orientation.lock(wanted);
        if (locked && locked.catch) {
          locked.catch(function() {});
        }
      }
    } catch (e) {
    }
  }, kind);
}

// A file the page hands to the person looking at it.
//
// There is no filesystem a browser will write to and no place a file
// written into this one can be found afterwards, so what "saved" means here
// is the download the browser already knows how to do: the bytes are read
// out of the module's own filesystem, wrapped in a blob and offered under
// the name they were written with.
[[nodiscard]] inline bool offerDownload(const std::string &path) {
  return EM_ASM_INT({
           try {
             const path = UTF8ToString($0);
             const bytes = FS.readFile(path);
             const name = path.split('/').pop();
             const blob = new Blob([bytes], {type : 'video/mp4'});
             const url = URL.createObjectURL(blob);
             const link = document.createElement('a');
             link.href = url;
             link.download = name;
             document.body.appendChild(link);
             link.click();
             document.body.removeChild(link);
             // Not at once: a click that has been dispatched has not
             // necessarily been acted on yet, and a revoked URL is a
             // download that never starts.
             setTimeout(function() { URL.revokeObjectURL(url); }, 60000);
             return 1;
           } catch (e) {
             return 0;
           }
         },
         path.c_str()) != 0;
}

inline void setCursorVisible(bool visible) {
  EM_ASM({ Module.setCursorVisible(!!$0); }, visible ? 1 : 0);
}

// Whether the client wants the pointer held to the canvas.
//
// Hiding the cursor and holding it are different things: a play with the
// pointer read as a position wants it hidden and free, and only relative
// input -- raw input, or a sensitivity other than 1:1 -- wants it held. The
// page keeps this so it can ask again after a refusal, and a browser refuses
// for about a second after Escape released the lock, which is how the game
// is paused.
inline void wantPointerLock(bool wanted) {
  EM_ASM({ Module.osuWantsPointer = !!$0; }, wanted ? 1 : 0);
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


