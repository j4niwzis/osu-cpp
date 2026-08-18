module;

#include <curl/curl.h>

export module curl;

export namespace curl {

// Types. CURL itself is an opaque typedef; the enums come through as-is.
using ::CURL;
using ::CURLcode;
using ::CURLINFO;
using ::CURLoption;
using ::curl_off_t;

// Functions (curl_easy_setopt / curl_easy_getinfo are variadic and export
// fine through a using-declaration).
using ::curl_easy_cleanup;
using ::curl_easy_getinfo;
using ::curl_easy_init;
using ::curl_easy_perform;
using ::curl_easy_setopt;
using ::curl_easy_strerror;
using ::curl_global_init;

// Constants the client needs, re-exported as constexpr values so no macro
// leaks past the module boundary.
inline constexpr long kGlobalDefault = CURL_GLOBAL_DEFAULT;
inline constexpr int kErrorSize = CURL_ERROR_SIZE;

inline constexpr CURLcode kOk = CURLE_OK;

inline constexpr CURLoption kOptUrl = CURLOPT_URL;
inline constexpr CURLoption kOptFollowLocation = CURLOPT_FOLLOWLOCATION;
inline constexpr CURLoption kOptMaxRedirs = CURLOPT_MAXREDIRS;
inline constexpr CURLoption kOptUserAgent = CURLOPT_USERAGENT;
inline constexpr CURLoption kOptAcceptEncoding = CURLOPT_ACCEPT_ENCODING;
inline constexpr CURLoption kOptWriteFunction = CURLOPT_WRITEFUNCTION;
inline constexpr CURLoption kOptWriteData = CURLOPT_WRITEDATA;
inline constexpr CURLoption kOptXferInfoFunction = CURLOPT_XFERINFOFUNCTION;
inline constexpr CURLoption kOptXferInfoData = CURLOPT_XFERINFODATA;
inline constexpr CURLoption kOptNoProgress = CURLOPT_NOPROGRESS;
inline constexpr CURLoption kOptErrorBuffer = CURLOPT_ERRORBUFFER;
inline constexpr CURLoption kOptConnectTimeout = CURLOPT_CONNECTTIMEOUT;
inline constexpr CURLoption kOptLowSpeedLimit = CURLOPT_LOW_SPEED_LIMIT;
inline constexpr CURLoption kOptLowSpeedTime = CURLOPT_LOW_SPEED_TIME;

inline constexpr CURLINFO kInfoResponseCode = CURLINFO_RESPONSE_CODE;

} // namespace curl
