module;

#include <emscripten.h>
#ifdef __EMSCRIPTEN__
#include <emscripten/fetch.h>
#endif

export module platform.emscripten_api;

export namespace platform::emscripten {

#ifdef __EMSCRIPTEN__
// Fetch API surface for client.http.
using ::emscripten_fetch;
using ::emscripten_fetch_attr_init;
using ::emscripten_fetch_attr_t;
using ::emscripten_fetch_close;
using ::emscripten_fetch_t;

inline constexpr auto kFetchLoadToMemory = EMSCRIPTEN_FETCH_LOAD_TO_MEMORY;
#endif

} // namespace platform::emscripten
