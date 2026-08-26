module;

#include <emscripten.h>
#ifdef __EMSCRIPTEN__
#include <emscripten/fetch.h>
#endif

export module emscripten;

export namespace emscripten {

using ::emscripten_cancel_main_loop;
using ::emscripten_set_main_loop_arg;

[[nodiscard]] inline int run_script_int(const char *code) {
  return emscripten_run_script_int(code);
}

#ifdef __EMSCRIPTEN__
// Fetch API surface for client.http.
using ::emscripten_fetch;
using ::emscripten_fetch_attr_init;
using ::emscripten_fetch_attr_t;
using ::emscripten_fetch_close;
using ::emscripten_fetch_t;

inline constexpr auto kFetchLoadToMemory = EMSCRIPTEN_FETCH_LOAD_TO_MEMORY;
#endif

} // namespace emscripten
