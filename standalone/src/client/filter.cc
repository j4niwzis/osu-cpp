export module client.filter;

import std;

export namespace client {

// Port of osu.Game/Screens/Select/FilterQueryParser.cs.
//
// lazer's search box accepts free text plus `key op value` terms; the terms
// are stripped out of the query and applied as numeric/text criteria, and
// whatever is left is matched against the beatmap's text. Operators, keys and
// aliases here mirror the original.
enum class Op : std::uint8_t { kEq, kNeq, kLt, kLe, kGt, kGe };

struct Range {
  bool fHasMin = false, fHasMax = false;
  double fMin = 0.0, fMax = 0.0;
  bool fExcludeMin = false, fExcludeMax = false;

  [[nodiscard]] bool matches(double v) const {
    if (fHasMin && (fExcludeMin ? v <= fMin : v < fMin)) {
      return false;
    }
    if (fHasMax && (fExcludeMax ? v >= fMax : v > fMax)) {
      return false;
    }
    return true;
  }

  void apply(Op op, double value, double tolerance = 0.0) {
    switch (op) {
    case Op::kEq:
      // lazer matches an epsilon window for decimal criteria.
      fHasMin = fHasMax = true;
      fMin = value - tolerance;
      fMax = value + tolerance;
      fExcludeMin = fExcludeMax = false;
      break;
    case Op::kNeq:
      break; // not expressible as a single range; ignored, as lazer does
    case Op::kLt:
      fHasMax = true;
      fMax = value;
      fExcludeMax = true;
      break;
    case Op::kLe:
      fHasMax = true;
      fMax = value;
      fExcludeMax = false;
      break;
    case Op::kGt:
      fHasMin = true;
      fMin = value;
      fExcludeMin = true;
      break;
    case Op::kGe:
      fHasMin = true;
      fMin = value;
      fExcludeMin = false;
      break;
    }
  }
};

struct Criteria {
  Range fStars, fAr, fCs, fOd, fHp, fLengthSec, fObjects;
  std::string fCreator, fArtist, fTitle, fDiff;
  std::string fSearchText; // leftover free text
};

namespace detail {

[[nodiscard]] inline std::string lower(std::string_view in) {
  std::string out(in);
  std::ranges::transform(out, out.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return out;
}

// `\b(?<key>\w+)(?<op>(!?(:|=)|(>|<)(:|=)?))(?<value>("".*?""[!]?)|(\S*))`
// implemented by hand: find an operator, walk back over the key, forward over
// the value (quoted or bare).
struct Term {
  std::size_t fStart = 0, fEnd = 0;
  std::string fKey, fValue;
  Op fOp = Op::kEq;
};

[[nodiscard]] inline std::optional<Term> nextTerm(std::string_view q,
                                                  std::size_t from) {
  for (std::size_t i = from; i < q.size(); ++i) {
    const char c = q[i];
    if (c != ':' && c != '=' && c != '<' && c != '>' && c != '!') {
      continue;
    }
    // Operator, longest match first.
    std::size_t opLen = 1;
    Op op = Op::kEq;
    if (c == '!') {
      if (i + 1 >= q.size() || (q[i + 1] != ':' && q[i + 1] != '=')) {
        continue;
      }
      op = Op::kNeq;
      opLen = 2;
    } else if (c == '<') {
      op = Op::kLt;
      if (i + 1 < q.size() && (q[i + 1] == ':' || q[i + 1] == '=')) {
        op = Op::kLe;
        opLen = 2;
      }
    } else if (c == '>') {
      op = Op::kGt;
      if (i + 1 < q.size() && (q[i + 1] == ':' || q[i + 1] == '=')) {
        op = Op::kGe;
        opLen = 2;
      }
    }

    // Key: word characters immediately before the operator.
    std::size_t keyEnd = i;
    std::size_t keyStart = keyEnd;
    while (keyStart > 0) {
      const unsigned char k = static_cast<unsigned char>(q[keyStart - 1]);
      if (std::isalnum(k) != 0 || k == '_') {
        --keyStart;
      } else {
        break;
      }
    }
    if (keyStart == keyEnd) {
      continue;
    }

    // Value: quoted (possibly with a trailing !) or up to the next space.
    std::size_t valStart = i + opLen;
    std::size_t valEnd = valStart;
    std::string value;
    if (valStart < q.size() && q[valStart] == '"') {
      const std::size_t close = q.find('"', valStart + 1);
      if (close == std::string_view::npos) {
        valEnd = q.size();
        value = std::string(q.substr(valStart + 1));
      } else {
        value = std::string(q.substr(valStart + 1, close - valStart - 1));
        valEnd = close + 1;
        if (valEnd < q.size() && q[valEnd] == '!') {
          ++valEnd;
        }
      }
    } else {
      while (valEnd < q.size() &&
             std::isspace(static_cast<unsigned char>(q[valEnd])) == 0) {
        ++valEnd;
      }
      value = std::string(q.substr(valStart, valEnd - valStart));
    }

    Term t;
    t.fStart = keyStart;
    t.fEnd = valEnd;
    t.fKey = lower(q.substr(keyStart, keyEnd - keyStart));
    t.fValue = value;
    t.fOp = op;
    return t;
  }
  return std::nullopt;
}

[[nodiscard]] inline bool parseDouble(const std::string &in, double &out) {
  if (in.empty()) {
    return false;
  }
  const auto res =
      std::from_chars(in.data(), in.data() + in.size(), out);
  return res.ec == std::errc{} && res.ptr == in.data() + in.size();
}

// "3:30" style lengths, as lazer's tryParseDoubleWithPoint chain allows.
[[nodiscard]] inline bool parseLengthSeconds(const std::string &in,
                                             double &out) {
  const auto colon = in.find(':');
  if (colon == std::string::npos) {
    double v = 0.0;
    if (!parseDouble(in, v)) {
      return false;
    }
    out = v; // bare number is seconds
    return true;
  }
  double mins = 0.0;
  double secs = 0.0;
  if (!parseDouble(in.substr(0, colon), mins) ||
      !parseDouble(in.substr(colon + 1), secs)) {
    return false;
  }
  out = mins * 60.0 + secs;
  return true;
}

} // namespace detail

[[nodiscard]] inline Criteria parseQuery(std::string_view query) {
  Criteria c;
  std::string rest(query);
  std::size_t from = 0;
  while (auto term = detail::nextTerm(rest, from)) {
    bool consumed = true;
    double value = 0.0;
    const auto &k = term->fKey;

    if (k == "star" || k == "stars" || k == "sr") {
      consumed = detail::parseDouble(term->fValue, value);
      if (consumed) {
        c.fStars.apply(term->fOp, value, 0.005);
      }
    } else if (k == "ar") {
      consumed = detail::parseDouble(term->fValue, value);
      if (consumed) {
        c.fAr.apply(term->fOp, value, 0.005);
      }
    } else if (k == "cs") {
      consumed = detail::parseDouble(term->fValue, value);
      if (consumed) {
        c.fCs.apply(term->fOp, value, 0.005);
      }
    } else if (k == "od") {
      consumed = detail::parseDouble(term->fValue, value);
      if (consumed) {
        c.fOd.apply(term->fOp, value, 0.005);
      }
    } else if (k == "hp" || k == "dr") {
      consumed = detail::parseDouble(term->fValue, value);
      if (consumed) {
        c.fHp.apply(term->fOp, value, 0.005);
      }
    } else if (k == "length") {
      consumed = detail::parseLengthSeconds(term->fValue, value);
      if (consumed) {
        c.fLengthSec.apply(term->fOp, value, 0.5);
      }
    } else if (k == "objects") {
      consumed = detail::parseDouble(term->fValue, value);
      if (consumed) {
        c.fObjects.apply(term->fOp, value);
      }
    } else if (k == "creator" || k == "author" || k == "mapper") {
      c.fCreator = detail::lower(term->fValue);
    } else if (k == "artist") {
      c.fArtist = detail::lower(term->fValue);
    } else if (k == "title") {
      c.fTitle = detail::lower(term->fValue);
    } else if (k == "diff" || k == "difficulty" || k == "version") {
      c.fDiff = detail::lower(term->fValue);
    } else {
      consumed = false;
    }

    if (consumed) {
      rest.erase(term->fStart, term->fEnd - term->fStart);
      from = term->fStart;
    } else {
      from = term->fEnd;
    }
  }
  // Collapse whitespace left behind by removed terms.
  std::string text;
  bool space = false;
  for (const char ch : rest) {
    if (std::isspace(static_cast<unsigned char>(ch)) != 0) {
      space = !text.empty();
      continue;
    }
    if (space) {
      text.push_back(' ');
      space = false;
    }
    text.push_back(ch);
  }
  c.fSearchText = detail::lower(text);
  return c;
}

} // namespace client
