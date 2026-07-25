module;

#include <zip.h>

export module osz;

import std;

export namespace osz {

using ::zip_close;
using ::zip_fclose;
using ::zip_fopen_index;
using ::zip_fread;
using ::zip_get_name;
using ::zip_get_num_entries;
using ::zip_open;
using ::zip_stat_index;

using ::zip_file_t;
using ::zip_int64_t;
using ::zip_stat_t;
using ::zip_t;
using ::zip_uint64_t;

inline constexpr int kRdOnly = ZIP_RDONLY;
inline constexpr int kFlEncGuess = ZIP_FL_ENC_GUESS;

} // namespace osz
