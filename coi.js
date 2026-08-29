// Cross-origin isolation, from a page that cannot set headers.
//
// This client is compiled with pthreads, so its runtime asks for a
// SharedArrayBuffer, and a browser only gives one to a page that is
// cross-origin isolated. That is two response headers -- COOP and COEP --
// and GitHub Pages sends neither and cannot be told to.
//
// A service worker answers requests for its own origin, so it can add them.
// The first visit registers it and reloads once; every load after that is
// isolated. This file is both the page script and the worker, told apart by
// whether there is a window.
// And what it has already fetched, it keeps.
//
// The client is thirty megabytes -- the module, and the fonts it draws with
// -- and a browser that revalidates all of it on every reload is a wait
// before every visit. These files are named after the build that made them
// only in the sense that they change together, so the cache is emptied
// whenever this worker's own version changes.
var CACHE = "osu-cpp-v1";
var KEPT = /\.(?:wasm|data|js|html)$|\/$/;

if (typeof window === "undefined") {
  self.addEventListener("install", function () {
    self.skipWaiting();
  });

  self.addEventListener("activate", function (event) {
    event.waitUntil(
      caches
        .keys()
        .then(function (names) {
          return Promise.all(
            names.map(function (name) {
              return name === CACHE ? null : caches.delete(name);
            })
          );
        })
        .then(function () {
          return self.clients.claim();
        })
    );
  });

  self.addEventListener("fetch", function (event) {
    var request = event.request;
    // A request the page made only to read its own cache, which a worker
    // must not answer with a fetch of its own.
    if (request.cache === "only-if-cached" && request.mode !== "same-origin") {
      return;
    }

    // The two headers, added to whatever the response was.
    var isolate = function (response) {
      if (response.status === 0) {
        // An opaque response has no headers to copy and no body to read.
        return response;
      }
      var headers = new Headers(response.headers);
      headers.set("Cross-Origin-Embedder-Policy", "require-corp");
      headers.set("Cross-Origin-Opener-Policy", "same-origin");
      return new Response(response.body, {
        status: response.status,
        statusText: response.statusText,
        headers: headers,
      });
    };

    var url = new URL(request.url);
    var keepable =
      request.method === "GET" &&
      url.origin === self.location.origin &&
      KEPT.test(url.pathname);

    if (!keepable) {
      event.respondWith(
        fetch(request)
          .then(isolate)
          .catch(function (error) {
            console.error("cross-origin isolation:", error);
          })
      );
      return;
    }

    event.respondWith(
      caches.open(CACHE).then(function (cache) {
        return cache.match(request).then(function (kept) {
          if (kept) {
            return isolate(kept);
          }
          return fetch(request).then(function (response) {
            if (response.status === 200) {
              cache.put(request, response.clone());
            }
            return isolate(response);
          });
        });
      })
    );
  });
} else if (!window.crossOriginIsolated) {
  // The worker is this file, so it is registered by the address it was
  // loaded from -- read now, because currentScript is null by the time a
  // promise settles.
  var source = document.currentScript.src;
  if (navigator.serviceWorker) {
    navigator.serviceWorker
      .register(source)
      .then(function (registration) {
        // Registered a moment ago and not yet in charge of this page: one
        // reload and it is.
        if (registration.active && !navigator.serviceWorker.controller) {
          window.location.reload();
        }
        registration.addEventListener("updatefound", function () {
          window.location.reload();
        });
      })
      .catch(function (error) {
        console.error("cross-origin isolation:", error);
      });
  } else {
    console.error("no service workers here, so no SharedArrayBuffer either");
  }
}
