module;

#include <cstdint>
#include <lzma.h>

export module lzma;

export namespace lzma {

using ::lzma_check;
using ::lzma_ret;
using ::lzma_stream;

using ::lzma_code;
using ::lzma_easy_encoder;
using ::lzma_end;
using ::lzma_stream_decoder;

inline constexpr lzma_stream kStreamInit = LZMA_STREAM_INIT;
inline constexpr auto kOk = LZMA_OK;
inline constexpr auto kStreamEnd = LZMA_STREAM_END;
inline constexpr auto kFinish = LZMA_FINISH;
inline constexpr auto kCheckCrc32 = LZMA_CHECK_CRC32;

} // namespace lzma
