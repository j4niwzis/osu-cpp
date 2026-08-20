module;

#include <cstdint>
#include <lzma.h>

export module lzma;

export namespace lzma {

using ::lzma_check;
using ::lzma_ret;
using ::lzma_stream;

using ::lzma_options_lzma;

using ::lzma_alone_decoder;
using ::lzma_alone_encoder;
using ::lzma_auto_decoder;
using ::lzma_code;
using ::lzma_easy_encoder;
using ::lzma_end;
using ::lzma_lzma_preset;
using ::lzma_stream_decoder;

inline constexpr lzma_stream kStreamInit = LZMA_STREAM_INIT;
inline constexpr auto kOk = LZMA_OK;
inline constexpr auto kStreamEnd = LZMA_STREAM_END;
inline constexpr auto kFinish = LZMA_FINISH;
inline constexpr auto kCheckCrc32 = LZMA_CHECK_CRC32;
inline constexpr std::uint32_t kPresetDefault = LZMA_PRESET_DEFAULT;

} // namespace lzma
