module;

#include <emscripten.h>

export module emscripten;

export namespace emscripten {

using ::emscripten_cancel_main_loop;
using ::emscripten_set_main_loop_arg;

[[nodiscard]] inline int run_script_int(const char *code) { return emscripten_run_script_int(code); }

} // namespace emscripten
