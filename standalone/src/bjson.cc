module;

#include <boost/json.hpp>
#include <boost/json/src.hpp> // header-only implementation lives here
#include <boost/system/error_code.hpp>

#include <optional>
#include <string_view>

export module bjson;

export namespace bjson {

using boost::json::array;
using boost::json::object;
using boost::json::string;
using boost::json::value;

// Non-throwing parse.
[[nodiscard]] inline std::optional<value> tryParse(std::string_view text) {
  boost::system::error_code ec;
  value v = boost::json::parse(text, ec);
  if (ec) {
    return std::nullopt;
  }
  return v;
}

} // namespace bjson
