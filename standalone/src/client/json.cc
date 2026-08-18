export module client.json;

import std;

export namespace client::json {

// Minimal read-only JSON value. Enough for mirror API responses; not a
// general-purpose library (no writing, no exotic extensions).
class Value {
public:
  using Array = std::vector<Value>;
  using Object = std::map<std::string, Value, std::less<>>;
  using Storage =
      std::variant<std::nullptr_t, bool, double, std::string, Array, Object>;

  Storage fV = nullptr;

  [[nodiscard]] bool isNull() const noexcept {
    return std::holds_alternative<std::nullptr_t>(fV);
  }
  [[nodiscard]] bool isArray() const noexcept {
    return std::holds_alternative<Array>(fV);
  }
  [[nodiscard]] bool isObject() const noexcept {
    return std::holds_alternative<Object>(fV);
  }

  [[nodiscard]] const Array *array() const noexcept {
    return std::get_if<Array>(&fV);
  }
  [[nodiscard]] const Object *object() const noexcept {
    return std::get_if<Object>(&fV);
  }

  // Object field access; returns null value if absent or not an object.
  [[nodiscard]] const Value &operator[](std::string_view key) const noexcept {
    static const Value kNull{};
    if (const auto *o = this->object()) {
      if (const auto it = o->find(key); it != o->end()) {
        return it->second;
      }
    }
    return kNull;
  }

  [[nodiscard]] std::string str(std::string fallback = {}) const {
    if (const auto *s = std::get_if<std::string>(&fV)) {
      return *s;
    }
    return fallback;
  }
  [[nodiscard]] double num(double fallback = 0.0) const noexcept {
    if (const auto *d = std::get_if<double>(&fV)) {
      return *d;
    }
    return fallback;
  }
};

namespace detail {

struct Parser {
  std::string_view fS;
  std::size_t fI = 0;
  int fDepth = 0;

  static constexpr int kMaxDepth = 64;

  void ws() {
    while (fI < fS.size() && (fS[fI] == ' ' || fS[fI] == '\t' ||
                              fS[fI] == '\n' || fS[fI] == '\r')) {
      ++fI;
    }
  }

  [[nodiscard]] bool eat(char c) {
    if (fI < fS.size() && fS[fI] == c) {
      ++fI;
      return true;
    }
    return false;
  }

  [[nodiscard]] bool lit(std::string_view w) {
    if (fS.substr(fI, w.size()) == w) {
      fI += w.size();
      return true;
    }
    return false;
  }

  static void utf8(std::string &out, std::uint32_t cp) {
    if (cp < 0x80) {
      out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
      out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
      out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
      out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
      out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
      out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
      out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
  }

  [[nodiscard]] bool hex4(std::uint32_t &out) {
    if (fI + 4 > fS.size()) {
      return false;
    }
    out = 0;
    for (int k = 0; k < 4; ++k) {
      const char c = fS[fI++];
      out <<= 4;
      if (c >= '0' && c <= '9') {
        out |= static_cast<std::uint32_t>(c - '0');
      } else if (c >= 'a' && c <= 'f') {
        out |= static_cast<std::uint32_t>(c - 'a' + 10);
      } else if (c >= 'A' && c <= 'F') {
        out |= static_cast<std::uint32_t>(c - 'A' + 10);
      } else {
        return false;
      }
    }
    return true;
  }

  [[nodiscard]] bool string(std::string &out) {
    if (!this->eat('"')) {
      return false;
    }
    while (fI < fS.size()) {
      const char c = fS[fI++];
      if (c == '"') {
        return true;
      }
      if (c != '\\') {
        out.push_back(c);
        continue;
      }
      if (fI >= fS.size()) {
        return false;
      }
      const char e = fS[fI++];
      switch (e) {
      case '"': out.push_back('"'); break;
      case '\\': out.push_back('\\'); break;
      case '/': out.push_back('/'); break;
      case 'b': out.push_back('\b'); break;
      case 'f': out.push_back('\f'); break;
      case 'n': out.push_back('\n'); break;
      case 'r': out.push_back('\r'); break;
      case 't': out.push_back('\t'); break;
      case 'u': {
        std::uint32_t cp = 0;
        if (!this->hex4(cp)) {
          return false;
        }
        if (cp >= 0xD800 && cp <= 0xDBFF && fS.substr(fI, 2) == "\\u") {
          fI += 2;
          std::uint32_t lo = 0;
          if (!this->hex4(lo)) {
            return false;
          }
          cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
        }
        utf8(out, cp);
        break;
      }
      default:
        return false;
      }
    }
    return false;
  }

  [[nodiscard]] bool value(Value &out) {
    if (++fDepth > kMaxDepth) {
      return false;
    }
    struct Depth {
      int &fD;
      ~Depth() { --fD; }
    } depth{fDepth};

    this->ws();
    if (fI >= fS.size()) {
      return false;
    }
    const char c = fS[fI];
    if (c == '{') {
      ++fI;
      Value::Object obj;
      this->ws();
      if (this->eat('}')) {
        out.fV = std::move(obj);
        return true;
      }
      while (true) {
        this->ws();
        std::string key;
        if (!this->string(key)) {
          return false;
        }
        this->ws();
        if (!this->eat(':')) {
          return false;
        }
        Value v;
        if (!this->value(v)) {
          return false;
        }
        obj.emplace(std::move(key), std::move(v));
        this->ws();
        if (this->eat(',')) {
          continue;
        }
        if (this->eat('}')) {
          break;
        }
        return false;
      }
      out.fV = std::move(obj);
      return true;
    }
    if (c == '[') {
      ++fI;
      Value::Array arr;
      this->ws();
      if (this->eat(']')) {
        out.fV = std::move(arr);
        return true;
      }
      while (true) {
        Value v;
        if (!this->value(v)) {
          return false;
        }
        arr.push_back(std::move(v));
        this->ws();
        if (this->eat(',')) {
          continue;
        }
        if (this->eat(']')) {
          break;
        }
        return false;
      }
      out.fV = std::move(arr);
      return true;
    }
    if (c == '"') {
      std::string s;
      if (!this->string(s)) {
        return false;
      }
      out.fV = std::move(s);
      return true;
    }
    if (this->lit("true")) {
      out.fV = true;
      return true;
    }
    if (this->lit("false")) {
      out.fV = false;
      return true;
    }
    if (this->lit("null")) {
      out.fV = nullptr;
      return true;
    }
    // number
    const std::size_t start = fI;
    if (fI < fS.size() && (fS[fI] == '-' || fS[fI] == '+')) {
      ++fI;
    }
    while (fI < fS.size() &&
           ((fS[fI] >= '0' && fS[fI] <= '9') || fS[fI] == '.' ||
            fS[fI] == 'e' || fS[fI] == 'E' || fS[fI] == '-' || fS[fI] == '+')) {
      ++fI;
    }
    if (fI == start) {
      return false;
    }
    double d = 0.0;
    const auto res = std::from_chars(fS.data() + start, fS.data() + fI, d);
    if (res.ec != std::errc{}) {
      return false;
    }
    out.fV = d;
    return true;
  }
};

} // namespace detail

[[nodiscard]] inline std::optional<Value> parse(std::string_view text) {
  detail::Parser p{text};
  Value v;
  if (!p.value(v)) {
    return std::nullopt;
  }
  return v;
}

} // namespace client::json
