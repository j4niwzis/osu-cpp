// The pieces a .dex file is made of, and a small assembler for its code.
//
// A dex file is a header, six tables that must each be sorted the way the
// format says, and a data section everything else points into. Nothing here
// is general: it writes what this project needs -- a handful of classes
// whose bytecode is written out by hand -- and it is the whole of what
// stands between the Java this project keeps as a specification and the file
// Android loads.

export module dex.file;

import std;

export namespace dex {

// -- the two encodings the format uses ------------------------------------

// A length, seven bits at a time, low group first.
inline void uleb(std::vector<std::uint8_t> &out, std::uint32_t value) {
  while (true) {
    const std::uint8_t byte = value & 0x7F;
    value >>= 7;
    if (value != 0) {
      out.push_back(static_cast<std::uint8_t>(byte | 0x80));
      continue;
    }
    out.push_back(byte);
    return;
  }
}

[[nodiscard]] inline std::vector<std::uint8_t> uleb(std::uint32_t value) {
  std::vector<std::uint8_t> out;
  uleb(out, value);
  return out;
}

// The encoding a dex string uses: UTF-8, except that a NUL is written as two
// bytes so that no string contains one, and a character outside the basic
// plane is written as the two halves of its surrogate pair rather than as
// itself.
[[nodiscard]] inline std::vector<std::uint8_t> mutf8(std::string_view text) {
  std::vector<std::uint8_t> out;
  // The source is UTF-8 already; what has to change is the two cases above,
  // so it is decoded far enough to recognise them.
  for (std::size_t at = 0; at < text.size();) {
    const auto lead = static_cast<std::uint8_t>(text[at]);
    std::uint32_t code = 0;
    std::size_t length = 1;
    if (lead < 0x80) {
      code = lead;
    } else if ((lead & 0xE0) == 0xC0) {
      length = 2;
      code = lead & 0x1FU;
    } else if ((lead & 0xF0) == 0xE0) {
      length = 3;
      code = lead & 0x0FU;
    } else {
      length = 4;
      code = lead & 0x07U;
    }
    for (std::size_t i = 1; i < length && at + i < text.size(); ++i) {
      code = (code << 6) | (static_cast<std::uint8_t>(text[at + i]) & 0x3FU);
    }
    at += length;

    if (code == 0) {
      out.push_back(0xC0);
      out.push_back(0x80);
    } else if (code < 0x80) {
      out.push_back(static_cast<std::uint8_t>(code));
    } else if (code < 0x800) {
      out.push_back(static_cast<std::uint8_t>(0xC0 | (code >> 6)));
      out.push_back(static_cast<std::uint8_t>(0x80 | (code & 0x3F)));
    } else if (code < 0x10000) {
      out.push_back(static_cast<std::uint8_t>(0xE0 | (code >> 12)));
      out.push_back(static_cast<std::uint8_t>(0x80 | ((code >> 6) & 0x3F)));
      out.push_back(static_cast<std::uint8_t>(0x80 | (code & 0x3F)));
    } else {
      const std::uint32_t rest = code - 0x10000;
      for (const std::uint32_t half :
           {0xD800U + (rest >> 10), 0xDC00U + (rest & 0x3FFU)}) {
        out.push_back(static_cast<std::uint8_t>(0xE0 | (half >> 12)));
        out.push_back(static_cast<std::uint8_t>(0x80 | ((half >> 6) & 0x3F)));
        out.push_back(static_cast<std::uint8_t>(0x80 | (half & 0x3F)));
      }
    }
  }
  return out;
}

// How many UTF-16 units the string is, which is the length a dex string
// carries -- not the number of bytes and not the number of characters.
[[nodiscard]] inline std::uint32_t utf16Length(std::string_view text) {
  std::uint32_t units = 0;
  for (std::size_t at = 0; at < text.size();) {
    const auto lead = static_cast<std::uint8_t>(text[at]);
    if (lead < 0x80) {
      at += 1;
      units += 1;
    } else if ((lead & 0xE0) == 0xC0) {
      at += 2;
      units += 1;
    } else if ((lead & 0xF0) == 0xE0) {
      at += 3;
      units += 1;
    } else {
      at += 4;
      units += 2;
    }
  }
  return units;
}

// -- what a class is made of ----------------------------------------------

struct Method {
  std::string fClass;
  std::string fName;
  std::string fReturns;
  std::vector<std::string> fParameters;
};

struct Field {
  std::string fClass;
  std::string fName;
  std::string fType;
};

} // namespace dex

export namespace dex {

// The tables, and what a build puts into them.
//
// Everything is collected twice: once to find out which strings, types,
// prototypes, fields and methods exist, and once more with the tables sorted
// and numbered -- because an instruction has to name a table index, and the
// format decides what the indices are. Strings go by their bytes, types by
// their string, and the rest by the things they are made of.
class Pool {
public:
  [[nodiscard]] bool frozen() const noexcept { return fFrozen; }

  std::uint32_t string(const std::string &text) {
    if (!fFrozen) {
      fStrings.emplace(text, 0);
      return 0;
    }
    return fStrings.at(text);
  }

  std::uint32_t type(const std::string &name) {
    if (!fFrozen) {
      fTypes.emplace(name, 0);
      string(name);
      return 0;
    }
    return fTypes.at(name);
  }

  std::uint32_t proto(const std::string &returns,
                      const std::vector<std::string> &parameters) {
    const auto key = std::pair{returns, parameters};
    if (!fFrozen) {
      fProtos.emplace(key, 0);
      type(returns);
      for (const auto &one : parameters) {
        type(one);
      }
      string(shorty(returns, parameters));
      return 0;
    }
    return fProtos.at(key);
  }

  std::uint32_t field(const Field &what) {
    const auto key = std::tuple{what.fClass, what.fName, what.fType};
    if (!fFrozen) {
      fFields.emplace(key, 0);
      type(what.fClass);
      type(what.fType);
      string(what.fName);
      return 0;
    }
    return fFields.at(key);
  }

  std::uint32_t method(const Method &what) {
    const auto key =
        std::tuple{what.fClass, what.fName, what.fReturns, what.fParameters};
    if (!fFrozen) {
      fMethods.emplace(key, 0);
      type(what.fClass);
      proto(what.fReturns, what.fParameters);
      string(what.fName);
      return 0;
    }
    return fMethods.at(key);
  }

  // What the format calls a shorty: one letter per type, L for any
  // reference, which is what a prototype is looked up by.
  [[nodiscard]] static std::string
  shorty(const std::string &returns,
         const std::vector<std::string> &parameters) {
    const auto letter = [](const std::string &type) {
      const char first = type.front();
      return std::string_view("VZBSCIJFD").contains(first) ? first : 'L';
    };
    std::string out(1, letter(returns));
    for (const auto &one : parameters) {
      out.push_back(letter(one));
    }
    return out;
  }

  // Numbered, in the order the format wants them.
  void freeze() {
    fStringOrder.reserve(fStrings.size());
    for (const auto &[text, unused] : fStrings) {
      fStringOrder.push_back(text);
    }
    std::ranges::sort(fStringOrder, [](const auto &a, const auto &b) {
      return mutf8(a) < mutf8(b);
    });
    for (std::uint32_t at = 0; at < fStringOrder.size(); ++at) {
      fStrings[fStringOrder[at]] = at;
    }

    fTypeOrder.reserve(fTypes.size());
    for (const auto &[name, unused] : fTypes) {
      fTypeOrder.push_back(name);
    }
    std::ranges::sort(fTypeOrder, [this](const auto &a, const auto &b) {
      return fStrings.at(a) < fStrings.at(b);
    });
    for (std::uint32_t at = 0; at < fTypeOrder.size(); ++at) {
      fTypes[fTypeOrder[at]] = at;
    }

    fProtoOrder.reserve(fProtos.size());
    for (const auto &[key, unused] : fProtos) {
      fProtoOrder.push_back(key);
    }
    std::ranges::sort(fProtoOrder, [this](const auto &a, const auto &b) {
      const auto rank = [this](const auto &one) {
        std::vector<std::uint32_t> out{fTypes.at(one.first)};
        for (const auto &parameter : one.second) {
          out.push_back(fTypes.at(parameter));
        }
        return out;
      };
      return rank(a) < rank(b);
    });
    for (std::uint32_t at = 0; at < fProtoOrder.size(); ++at) {
      fProtos[fProtoOrder[at]] = at;
    }

    fFieldOrder.reserve(fFields.size());
    for (const auto &[key, unused] : fFields) {
      fFieldOrder.push_back(key);
    }
    std::ranges::sort(fFieldOrder, [this](const auto &a, const auto &b) {
      const auto rank = [this](const auto &one) {
        return std::tuple{fTypes.at(std::get<0>(one)),
                          fStrings.at(std::get<1>(one)),
                          fTypes.at(std::get<2>(one))};
      };
      return rank(a) < rank(b);
    });
    for (std::uint32_t at = 0; at < fFieldOrder.size(); ++at) {
      fFields[fFieldOrder[at]] = at;
    }

    fMethodOrder.reserve(fMethods.size());
    for (const auto &[key, unused] : fMethods) {
      fMethodOrder.push_back(key);
    }
    std::ranges::sort(fMethodOrder, [this](const auto &a, const auto &b) {
      const auto rank = [this](const auto &one) {
        return std::tuple{
            fTypes.at(std::get<0>(one)), fStrings.at(std::get<1>(one)),
            fProtos.at(std::pair{std::get<2>(one), std::get<3>(one)})};
      };
      return rank(a) < rank(b);
    });
    for (std::uint32_t at = 0; at < fMethodOrder.size(); ++at) {
      fMethods[fMethodOrder[at]] = at;
    }
    fFrozen = true;
  }

  [[nodiscard]] const std::vector<std::string> &strings() const {
    return fStringOrder;
  }
  [[nodiscard]] const std::vector<std::string> &types() const {
    return fTypeOrder;
  }
  [[nodiscard]] const auto &protos() const { return fProtoOrder; }
  [[nodiscard]] const auto &fields() const { return fFieldOrder; }
  [[nodiscard]] const auto &methods() const { return fMethodOrder; }

private:
  using ProtoKey = std::pair<std::string, std::vector<std::string>>;
  using FieldKey = std::tuple<std::string, std::string, std::string>;
  using MethodKey =
      std::tuple<std::string, std::string, std::string, std::vector<std::string>>;

  bool fFrozen = false;
  std::map<std::string, std::uint32_t> fStrings;
  std::map<std::string, std::uint32_t> fTypes;
  std::map<ProtoKey, std::uint32_t> fProtos;
  std::map<FieldKey, std::uint32_t> fFields;
  std::map<MethodKey, std::uint32_t> fMethods;
  std::vector<std::string> fStringOrder;
  std::vector<std::string> fTypeOrder;
  std::vector<ProtoKey> fProtoOrder;
  std::vector<FieldKey> fFieldOrder;
  std::vector<MethodKey> fMethodOrder;
};

} // namespace dex

export namespace dex {

// Instructions, as words, with labels resolved when the method is done.
//
// Registers are numbers here, as they are in the file. The Java this mirrors
// is small enough that assigning them by hand is clearer than anything that
// would assign them for us, and every form below refuses a register it
// cannot encode.
class Code {
public:
  Code(Pool &pool, std::uint16_t registers, std::uint16_t ins,
       std::uint16_t outs)
      : fPool(pool), fRegisters(registers), fIns(ins), fOuts(outs) {}

  // -- placement ----------------------------------------------------------
  void label(std::string name) { fLabels[std::move(name)] = words(); }
  [[nodiscard]] std::uint32_t at(const std::string &name) const {
    return fLabels.at(name);
  }
  [[nodiscard]] std::uint32_t words() const {
    return static_cast<std::uint32_t>(fWords.size());
  }

  // -- the instruction forms this project uses ----------------------------
  void constant4(std::uint8_t reg, int value) { op11n(0x12, reg, value); }
  void constant16(std::uint8_t reg, int value) { op21s(0x13, reg, value); }
  void constantString(std::uint8_t reg, const std::string &text) {
    op21c(0x1A, reg, fPool.string(text));
  }
  void newInstance(std::uint8_t reg, const std::string &type) {
    op21c(0x22, reg, fPool.type(type));
  }
  void newArray(std::uint8_t destination, std::uint8_t size,
                const std::string &type) {
    op22c(0x23, destination, size, fPool.type(type));
  }
  void moveResult(std::uint8_t reg) { op11x(0x0A, reg); }
  void moveResultObject(std::uint8_t reg) { op11x(0x0C, reg); }
  void moveException(std::uint8_t reg) { op11x(0x0D, reg); }
  void returnVoid() { op10x(0x0E); }
  void putObject(std::uint8_t source, std::uint8_t array, std::uint8_t index) {
    op23x(0x4D, source, array, index);
  }
  void readField(std::uint8_t destination, std::uint8_t object,
                 const Field &what) {
    op22c(0x54, destination, object, fPool.field(what));
  }
  void writeField(std::uint8_t source, std::uint8_t object, const Field &what) {
    op22c(0x5B, source, object, fPool.field(what));
  }
  void andConstant(std::uint8_t destination, std::uint8_t source, int literal) {
    op22b(0xDD, destination, source, literal);
  }
  void jump(const std::string &target) { branch(0x28, Form::k10t, 0, 0, target); }
  void jumpIfEqual(std::uint8_t a, std::uint8_t b, const std::string &target) {
    branch(0x32, Form::k22t, a, b, target);
  }
  void jumpIfDifferent(std::uint8_t a, std::uint8_t b,
                       const std::string &target) {
    branch(0x33, Form::k22t, a, b, target);
  }
  void jumpIfZero(std::uint8_t a, const std::string &target) {
    branch(0x38, Form::k21t, a, 0, target);
  }
  void jumpIfNotZero(std::uint8_t a, const std::string &target) {
    branch(0x39, Form::k21t, a, 0, target);
  }
  void callVirtual(std::initializer_list<std::uint8_t> arguments,
                   const Method &what) {
    op35c(0x6E, fPool.method(what), arguments);
  }
  void callSuper(std::initializer_list<std::uint8_t> arguments,
                 const Method &what) {
    op35c(0x6F, fPool.method(what), arguments);
  }
  void callDirect(std::initializer_list<std::uint8_t> arguments,
                  const Method &what) {
    op35c(0x70, fPool.method(what), arguments);
  }

  // The one thing here that is not an instruction: what to do when the
  // instructions between two labels throw.
  void guard(std::string from, std::string to, std::string handler,
             const std::string &type) {
    fPool.type(type); // named here and nowhere else, so it is recorded
    fGuards.push_back({std::move(from), std::move(to), std::move(handler), type});
  }

  // Labels become offsets, and the method is what it is.
  [[nodiscard]] const std::vector<std::uint16_t> &finish() {
    for (const auto &[index, target, form] : fFixups) {
      const auto to = static_cast<std::int32_t>(fLabels.at(target));
      if (form == Form::k10t) {
        const auto delta = to - static_cast<std::int32_t>(index);
        if (delta < -128 || delta > 127) {
          throw std::runtime_error("a jump too far for its form: " + target);
        }
        fWords[index] |= static_cast<std::uint16_t>((delta & 0xFF) << 8);
        continue;
      }
      const auto delta = to - (static_cast<std::int32_t>(index) - 1);
      fWords[index] = static_cast<std::uint16_t>(delta & 0xFFFF);
    }
    return fWords;
  }

  struct Guard {
    std::string fFrom;
    std::string fTo;
    std::string fHandler;
    std::string fType;
  };

  [[nodiscard]] const std::vector<Guard> &guards() const { return fGuards; }
  [[nodiscard]] std::uint16_t registers() const { return fRegisters; }
  [[nodiscard]] std::uint16_t ins() const { return fIns; }
  [[nodiscard]] std::uint16_t outs() const { return fOuts; }

private:
  enum class Form { k10t, k21t, k22t };

  void write(std::uint16_t word) { fWords.push_back(word); }

  void op10x(std::uint8_t op) { write(op); }
  void op11x(std::uint8_t op, std::uint8_t a) {
    write(static_cast<std::uint16_t>(op | (a << 8)));
  }
  void op11n(std::uint8_t op, std::uint8_t a, int literal) {
    write(static_cast<std::uint16_t>(op | (a << 8) |
                                     ((literal & 0xF) << 12)));
  }
  void op21s(std::uint8_t op, std::uint8_t a, int literal) {
    write(static_cast<std::uint16_t>(op | (a << 8)));
    write(static_cast<std::uint16_t>(literal & 0xFFFF));
  }
  void op21c(std::uint8_t op, std::uint8_t a, std::uint32_t index) {
    write(static_cast<std::uint16_t>(op | (a << 8)));
    write(static_cast<std::uint16_t>(index));
  }
  void op22c(std::uint8_t op, std::uint8_t a, std::uint8_t b,
             std::uint32_t index) {
    write(static_cast<std::uint16_t>(op | (a << 8) | (b << 12)));
    write(static_cast<std::uint16_t>(index));
  }
  void op23x(std::uint8_t op, std::uint8_t a, std::uint8_t b, std::uint8_t c) {
    write(static_cast<std::uint16_t>(op | (a << 8)));
    write(static_cast<std::uint16_t>(b | (c << 8)));
  }
  void op22b(std::uint8_t op, std::uint8_t a, std::uint8_t b, int literal) {
    write(static_cast<std::uint16_t>(op | (a << 8)));
    write(static_cast<std::uint16_t>((b & 0xFF) | ((literal & 0xFF) << 8)));
  }
  void op35c(std::uint8_t op, std::uint32_t index,
             std::initializer_list<std::uint8_t> arguments) {
    if (arguments.size() > 5) {
      throw std::runtime_error("more arguments than this form can carry");
    }
    const auto count = static_cast<std::uint16_t>(arguments.size());
    const std::uint16_t fifth =
        arguments.size() > 4 ? *(arguments.begin() + 4) : 0;
    write(static_cast<std::uint16_t>(op | (count << 12) | (fifth << 8)));
    write(static_cast<std::uint16_t>(index));
    std::uint16_t packed = 0;
    std::size_t place = 0;
    for (const std::uint8_t reg : arguments) {
      if (place >= 4) {
        break;
      }
      packed |= static_cast<std::uint16_t>(reg << (4 * place));
      ++place;
    }
    write(packed);
  }

  void branch(std::uint8_t op, Form form, std::uint8_t a, std::uint8_t b,
              const std::string &target) {
    if (form == Form::k10t) {
      fFixups.push_back({words(), target, form});
      write(op);
      return;
    }
    const std::uint16_t first =
        form == Form::k21t
            ? static_cast<std::uint16_t>(op | (a << 8))
            : static_cast<std::uint16_t>(op | (a << 8) | (b << 12));
    fFixups.push_back({words() + 1, target, form});
    write(first);
    write(0);
  }

  struct Fixup {
    std::uint32_t fIndex;
    std::string fTarget;
    Form fForm;
  };

  Pool &fPool;
  std::uint16_t fRegisters;
  std::uint16_t fIns;
  std::uint16_t fOuts;
  std::vector<std::uint16_t> fWords;
  std::map<std::string, std::uint32_t> fLabels;
  std::vector<Fixup> fFixups;
  std::vector<Guard> fGuards;
};

} // namespace dex

export namespace dex {

// A class, which registers everything it declares as it is described: what a
// class says about itself has to be in the tables too, and only the things
// it calls would be there otherwise.
class Class {
public:
  struct DeclaredField {
    std::string fName;
    std::string fType;
    std::uint32_t fAccess;
  };

  struct DeclaredMethod {
    std::string fName;
    std::string fReturns;
    std::vector<std::string> fParameters;
    std::uint32_t fAccess;
    std::shared_ptr<Code> fCode;
  };

  Class(Pool &pool, std::string name, std::string base,
        std::vector<std::string> interfaces, std::uint32_t access)
      : fPool(pool), fName(std::move(name)), fBase(std::move(base)),
        fInterfaces(std::move(interfaces)), fAccess(access) {
    pool.type(fName);
    if (!fBase.empty()) {
      pool.type(fBase);
    }
    for (const auto &one : fInterfaces) {
      pool.type(one);
    }
  }

  void field(std::string name, std::string type, std::uint32_t access) {
    fPool.field({fName, name, type});
    fFields.push_back({std::move(name), std::move(type), access});
  }

  void method(std::string name, std::string returns,
              std::vector<std::string> parameters, std::uint32_t access,
              std::shared_ptr<Code> code, bool direct) {
    fPool.method({fName, name, returns, parameters});
    auto &group = direct ? fDirect : fVirtual;
    group.push_back({std::move(name), std::move(returns), std::move(parameters),
                     access, std::move(code)});
  }

  [[nodiscard]] const std::string &name() const { return fName; }
  [[nodiscard]] const std::string &base() const { return fBase; }
  [[nodiscard]] const std::vector<std::string> &interfaces() const {
    return fInterfaces;
  }
  [[nodiscard]] std::uint32_t access() const { return fAccess; }
  [[nodiscard]] const std::vector<DeclaredField> &fields() const {
    return fFields;
  }
  [[nodiscard]] const std::vector<DeclaredMethod> &direct() const {
    return fDirect;
  }
  [[nodiscard]] const std::vector<DeclaredMethod> &virtuals() const {
    return fVirtual;
  }

private:
  Pool &fPool;
  std::string fName;
  std::string fBase;
  std::vector<std::string> fInterfaces;
  std::uint32_t fAccess;
  std::vector<DeclaredField> fFields;
  std::vector<DeclaredMethod> fDirect;
  std::vector<DeclaredMethod> fVirtual;
};

// -- what the header carries about the rest of the file -------------------

// Adler-32 of everything after the checksum itself.
[[nodiscard]] inline std::uint32_t adler32(std::span<const std::uint8_t> data) {
  std::uint32_t a = 1;
  std::uint32_t b = 0;
  for (const std::uint8_t byte : data) {
    a = (a + byte) % 65521;
    b = (b + a) % 65521;
  }
  return (b << 16) | a;
}

// SHA-1 of everything after the signature, which is what the format asks for
// and the only reason this is here rather than in a library.
[[nodiscard]] inline std::array<std::uint8_t, 20>
sha1(std::span<const std::uint8_t> data) {
  std::array<std::uint32_t, 5> state{0x67452301, 0xEFCDAB89, 0x98BADCFE,
                                     0x10325476, 0xC3D2E1F0};
  const auto rotate = [](std::uint32_t value, int by) {
    return static_cast<std::uint32_t>((value << by) | (value >> (32 - by)));
  };
  std::vector<std::uint8_t> message(data.begin(), data.end());
  const std::uint64_t bits = static_cast<std::uint64_t>(data.size()) * 8;
  message.push_back(0x80);
  while (message.size() % 64 != 56) {
    message.push_back(0);
  }
  for (int shift = 56; shift >= 0; shift -= 8) {
    message.push_back(static_cast<std::uint8_t>((bits >> shift) & 0xFF));
  }
  for (std::size_t at = 0; at < message.size(); at += 64) {
    std::array<std::uint32_t, 80> words{};
    for (std::size_t i = 0; i < 16; ++i) {
      words[i] = static_cast<std::uint32_t>(message[at + 4 * i] << 24) |
                 static_cast<std::uint32_t>(message[at + 4 * i + 1] << 16) |
                 static_cast<std::uint32_t>(message[at + 4 * i + 2] << 8) |
                 static_cast<std::uint32_t>(message[at + 4 * i + 3]);
    }
    for (std::size_t i = 16; i < 80; ++i) {
      words[i] = rotate(words[i - 3] ^ words[i - 8] ^ words[i - 14] ^
                            words[i - 16], 1);
    }
    auto [a, b, c, d, e] = state;
    for (std::size_t i = 0; i < 80; ++i) {
      std::uint32_t mixed = 0;
      std::uint32_t constant = 0;
      if (i < 20) {
        mixed = (b & c) | (~b & d);
        constant = 0x5A827999;
      } else if (i < 40) {
        mixed = b ^ c ^ d;
        constant = 0x6ED9EBA1;
      } else if (i < 60) {
        mixed = (b & c) | (b & d) | (c & d);
        constant = 0x8F1BBCDC;
      } else {
        mixed = b ^ c ^ d;
        constant = 0xCA62C1D6;
      }
      const std::uint32_t next = rotate(a, 5) + mixed + e + constant + words[i];
      e = d;
      d = c;
      c = rotate(b, 30);
      b = a;
      a = next;
    }
    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
  }
  std::array<std::uint8_t, 20> digest{};
  for (std::size_t i = 0; i < 5; ++i) {
    digest[4 * i] = static_cast<std::uint8_t>(state[i] >> 24);
    digest[4 * i + 1] = static_cast<std::uint8_t>(state[i] >> 16);
    digest[4 * i + 2] = static_cast<std::uint8_t>(state[i] >> 8);
    digest[4 * i + 3] = static_cast<std::uint8_t>(state[i]);
  }
  return digest;
}

[[nodiscard]] std::vector<std::uint8_t> write(Pool &pool,
                                              std::vector<Class> &classes);

} // namespace dex

namespace dex::detail {

inline void append16(std::vector<std::uint8_t> &out, std::uint16_t value) {
  out.push_back(static_cast<std::uint8_t>(value & 0xFF));
  out.push_back(static_cast<std::uint8_t>(value >> 8));
}

inline void append32(std::vector<std::uint8_t> &out, std::uint32_t value) {
  for (int shift = 0; shift < 32; shift += 8) {
    out.push_back(static_cast<std::uint8_t>((value >> shift) & 0xFF));
  }
}

inline void put32(std::vector<std::uint8_t> &out, std::size_t at,
                  std::uint32_t value) {
  for (int shift = 0; shift < 32; shift += 8) {
    out[at + static_cast<std::size_t>(shift / 8)] =
        static_cast<std::uint8_t>((value >> shift) & 0xFF);
  }
}

inline void put16(std::vector<std::uint8_t> &out, std::size_t at,
                  std::uint16_t value) {
  out[at] = static_cast<std::uint8_t>(value & 0xFF);
  out[at + 1] = static_cast<std::uint8_t>(value >> 8);
}

inline void align(std::vector<std::uint8_t> &out, std::size_t to) {
  while (out.size() % to != 0) {
    out.push_back(0);
  }
}

// The kinds of section a map lists, which is every kind this writes.
inline constexpr std::uint16_t kHeader = 0x0000;
inline constexpr std::uint16_t kStringId = 0x0001;
inline constexpr std::uint16_t kTypeId = 0x0002;
inline constexpr std::uint16_t kProtoId = 0x0003;
inline constexpr std::uint16_t kFieldId = 0x0004;
inline constexpr std::uint16_t kMethodId = 0x0005;
inline constexpr std::uint16_t kClassDef = 0x0006;
inline constexpr std::uint16_t kMapList = 0x1000;
inline constexpr std::uint16_t kTypeList = 0x1001;
inline constexpr std::uint16_t kClassData = 0x2000;
inline constexpr std::uint16_t kCodeItem = 0x2001;
inline constexpr std::uint16_t kStringData = 0x2002;

} // namespace dex::detail

std::vector<std::uint8_t> dex::write(dex::Pool &pool,
                                     std::vector<dex::Class> &classes) {
  using namespace dex::detail;

  const auto &strings = pool.strings();
  const auto &types = pool.types();
  const auto &protos = pool.protos();
  const auto &fields = pool.fields();
  const auto &methods = pool.methods();

  constexpr std::uint32_t kHeaderSize = 112;
  std::uint32_t at = kHeaderSize;
  const std::uint32_t stringIds = at;
  at += 4 * static_cast<std::uint32_t>(strings.size());
  const std::uint32_t typeIds = at;
  at += 4 * static_cast<std::uint32_t>(types.size());
  const std::uint32_t protoIds = at;
  at += 12 * static_cast<std::uint32_t>(protos.size());
  const std::uint32_t fieldIds = at;
  at += 8 * static_cast<std::uint32_t>(fields.size());
  const std::uint32_t methodIds = at;
  at += 8 * static_cast<std::uint32_t>(methods.size());
  const std::uint32_t classDefs = at;
  at += 32 * static_cast<std::uint32_t>(classes.size());
  const std::uint32_t dataStart = at;

  std::vector<std::uint8_t> data;
  const auto here = [&] {
    return dataStart + static_cast<std::uint32_t>(data.size());
  };

  // Type lists: the parameters of a prototype, and the interfaces of a class.
  std::map<std::vector<std::string>, std::uint32_t> typeLists;
  const auto typeList = [&](const std::vector<std::string> &items) {
    if (items.empty()) {
      return 0u;
    }
    if (const auto found = typeLists.find(items); found != typeLists.end()) {
      return found->second;
    }
    align(data, 4);
    const std::uint32_t where = here();
    append32(data, static_cast<std::uint32_t>(items.size()));
    for (const auto &one : items) {
      append16(data, static_cast<std::uint16_t>(pool.type(one)));
    }
    typeLists.emplace(items, where);
    return where;
  };

  std::vector<std::uint32_t> protoParameters;
  protoParameters.reserve(protos.size());
  for (const auto &[returns, parameters] : protos) {
    protoParameters.push_back(typeList(parameters));
  }
  std::map<std::string, std::uint32_t> interfacesAt;
  for (const auto &one : classes) {
    interfacesAt[one.name()] = typeList(one.interfaces());
  }
  const auto typeListCount = static_cast<std::uint32_t>(typeLists.size());

  // Code items.
  std::map<std::string, std::uint32_t> codeAt;
  std::uint32_t codeCount = 0;
  const auto codeKey = [](const std::string &owner, const auto &one) {
    std::string key = owner + "." + one.fName + one.fReturns;
    for (const auto &parameter : one.fParameters) {
      key += parameter;
    }
    return key;
  };
  for (auto &one : classes) {
    for (const auto *group : {&one.direct(), &one.virtuals()}) {
      for (const auto &member : *group) {
        if (!member.fCode) {
          continue;
        }
        auto &code = *member.fCode;
        const auto &words = code.finish();
        align(data, 4);
        const std::uint32_t where = here();

        std::vector<std::uint8_t> handlers;
        std::vector<std::uint8_t> guards;
        if (!code.guards().empty()) {
          std::vector<std::uint32_t> handlerAt;
          uleb(handlers, static_cast<std::uint32_t>(code.guards().size()));
          for (const auto &guard : code.guards()) {
            handlerAt.push_back(static_cast<std::uint32_t>(handlers.size()));
            uleb(handlers, 1); // one typed catch, and no catch-all
            uleb(handlers, pool.type(guard.fType));
            uleb(handlers, code.at(guard.fHandler));
          }
          for (std::size_t i = 0; i < code.guards().size(); ++i) {
            const auto &guard = code.guards()[i];
            const std::uint32_t from = code.at(guard.fFrom);
            append32(guards, from);
            append16(guards,
                     static_cast<std::uint16_t>(code.at(guard.fTo) - from));
            append16(guards, static_cast<std::uint16_t>(handlerAt[i]));
          }
        }

        append16(data, code.registers());
        append16(data, code.ins());
        append16(data, code.outs());
        append16(data, static_cast<std::uint16_t>(code.guards().size()));
        append32(data, 0); // no debug information
        append32(data, static_cast<std::uint32_t>(words.size()));
        for (const std::uint16_t word : words) {
          append16(data, word);
        }
        if (!code.guards().empty() && words.size() % 2 != 0) {
          append16(data, 0);
        }
        data.insert(data.end(), guards.begin(), guards.end());
        data.insert(data.end(), handlers.begin(), handlers.end());
        codeAt[codeKey(one.name(), member)] = where;
        ++codeCount;
      }
    }
  }

  // Class data.
  std::map<std::string, std::uint32_t> classDataAt;
  for (auto &one : classes) {
    const std::uint32_t where = here();
    std::vector<Class::DeclaredField> statics;
    std::vector<Class::DeclaredField> instances;
    for (const auto &member : one.fields()) {
      ((member.fAccess & 0x8) != 0 ? statics : instances).push_back(member);
    }
    uleb(data, static_cast<std::uint32_t>(statics.size()));
    uleb(data, static_cast<std::uint32_t>(instances.size()));
    uleb(data, static_cast<std::uint32_t>(one.direct().size()));
    uleb(data, static_cast<std::uint32_t>(one.virtuals().size()));
    for (auto *group : {&statics, &instances}) {
      std::ranges::sort(*group, [&](const auto &a, const auto &b) {
        return pool.field({one.name(), a.fName, a.fType}) <
               pool.field({one.name(), b.fName, b.fType});
      });
      std::uint32_t previous = 0;
      for (const auto &member : *group) {
        const std::uint32_t index =
            pool.field({one.name(), member.fName, member.fType});
        uleb(data, index - previous);
        uleb(data, member.fAccess);
        previous = index;
      }
    }
    for (const auto *group : {&one.direct(), &one.virtuals()}) {
      auto ordered = *group;
      std::ranges::sort(ordered, [&](const auto &a, const auto &b) {
        return pool.method({one.name(), a.fName, a.fReturns, a.fParameters}) <
               pool.method({one.name(), b.fName, b.fReturns, b.fParameters});
      });
      std::uint32_t previous = 0;
      for (const auto &member : ordered) {
        const std::uint32_t index = pool.method(
            {one.name(), member.fName, member.fReturns, member.fParameters});
        uleb(data, index - previous);
        uleb(data, member.fAccess);
        const auto found = codeAt.find(codeKey(one.name(), member));
        uleb(data, found == codeAt.end() ? 0 : found->second);
        previous = index;
      }
    }
    classDataAt[one.name()] = where;
  }

  // String data.
  std::vector<std::uint32_t> stringDataAt;
  stringDataAt.reserve(strings.size());
  for (const auto &text : strings) {
    stringDataAt.push_back(here());
    uleb(data, utf16Length(text));
    const auto encoded = mutf8(text);
    data.insert(data.end(), encoded.begin(), encoded.end());
    data.push_back(0);
  }

  // The map: every section, in the order it appears.
  align(data, 4);
  const std::uint32_t mapAt = here();
  struct Entry {
    std::uint16_t fKind;
    std::uint32_t fCount;
    std::uint32_t fWhere;
  };
  const auto smallest = [](const std::map<std::string, std::uint32_t> &of) {
    std::uint32_t least = std::numeric_limits<std::uint32_t>::max();
    for (const auto &[name, where] : of) {
      least = std::min(least, where);
    }
    return least;
  };
  std::vector<Entry> entries{
      {kHeader, 1, 0},
      {kStringId, static_cast<std::uint32_t>(strings.size()), stringIds},
      {kTypeId, static_cast<std::uint32_t>(types.size()), typeIds},
      {kProtoId, static_cast<std::uint32_t>(protos.size()), protoIds},
      {kFieldId, static_cast<std::uint32_t>(fields.size()), fieldIds},
      {kMethodId, static_cast<std::uint32_t>(methods.size()), methodIds},
      {kClassDef, static_cast<std::uint32_t>(classes.size()), classDefs},
  };
  if (typeListCount != 0) {
    std::uint32_t least = std::numeric_limits<std::uint32_t>::max();
    for (const auto &[items, where] : typeLists) {
      least = std::min(least, where);
    }
    entries.push_back({kTypeList, typeListCount, least});
  }
  if (codeCount != 0) {
    entries.push_back({kCodeItem, codeCount, smallest(codeAt)});
  }
  entries.push_back({kClassData, static_cast<std::uint32_t>(classes.size()),
                     smallest(classDataAt)});
  entries.push_back({kStringData, static_cast<std::uint32_t>(strings.size()),
                     stringDataAt.front()});
  entries.push_back({kMapList, 1, mapAt});
  std::erase_if(entries, [](const Entry &one) { return one.fCount == 0; });
  std::ranges::sort(entries, {}, &Entry::fWhere);
  append32(data, static_cast<std::uint32_t>(entries.size()));
  for (const auto &entry : entries) {
    append16(data, entry.fKind);
    append16(data, 0);
    append32(data, entry.fCount);
    append32(data, entry.fWhere);
  }

  // And now the tables that point into all of that.
  std::vector<std::uint8_t> out(dataStart, 0);
  for (std::size_t i = 0; i < stringDataAt.size(); ++i) {
    put32(out, stringIds + 4 * i, stringDataAt[i]);
  }
  for (std::size_t i = 0; i < types.size(); ++i) {
    put32(out, typeIds + 4 * i, pool.string(types[i]));
  }
  for (std::size_t i = 0; i < protos.size(); ++i) {
    const auto &[returns, parameters] = protos[i];
    put32(out, protoIds + 12 * i, pool.string(Pool::shorty(returns, parameters)));
    put32(out, protoIds + 12 * i + 4, pool.type(returns));
    put32(out, protoIds + 12 * i + 8, protoParameters[i]);
  }
  for (std::size_t i = 0; i < fields.size(); ++i) {
    const auto &[owner, name, type] = fields[i];
    put16(out, fieldIds + 8 * i, static_cast<std::uint16_t>(pool.type(owner)));
    put16(out, fieldIds + 8 * i + 2, static_cast<std::uint16_t>(pool.type(type)));
    put32(out, fieldIds + 8 * i + 4, pool.string(name));
  }
  for (std::size_t i = 0; i < methods.size(); ++i) {
    const auto &[owner, name, returns, parameters] = methods[i];
    put16(out, methodIds + 8 * i, static_cast<std::uint16_t>(pool.type(owner)));
    put16(out, methodIds + 8 * i + 2,
          static_cast<std::uint16_t>(pool.proto(returns, parameters)));
    put32(out, methodIds + 8 * i + 4, pool.string(name));
  }
  for (std::size_t i = 0; i < classes.size(); ++i) {
    auto &one = classes[i];
    const std::size_t base = classDefs + 32 * i;
    put32(out, base, pool.type(one.name()));
    put32(out, base + 4, one.access());
    put32(out, base + 8,
          one.base().empty() ? 0xFFFFFFFFu : pool.type(one.base()));
    put32(out, base + 12, interfacesAt[one.name()]);
    put32(out, base + 16, 0xFFFFFFFFu); // no source file
    put32(out, base + 20, 0);           // no annotations
    put32(out, base + 24, classDataAt[one.name()]);
    put32(out, base + 28, 0);           // no static values
  }
  out.insert(out.end(), data.begin(), data.end());

  static constexpr std::array<std::uint8_t, 8> kMagic{'d', 'e', 'x', '\n',
                                                      '0', '3', '5', 0};
  std::ranges::copy(kMagic, out.begin());
  const std::array<std::uint32_t, 19> header{
      static_cast<std::uint32_t>(out.size()),
      kHeaderSize,
      0x12345678, // this file is little-endian
      0,
      0, // no link section
      mapAt,
      static_cast<std::uint32_t>(strings.size()),
      stringIds,
      static_cast<std::uint32_t>(types.size()),
      typeIds,
      static_cast<std::uint32_t>(protos.size()),
      protoIds,
      static_cast<std::uint32_t>(fields.size()),
      fieldIds,
      static_cast<std::uint32_t>(methods.size()),
      methodIds,
      static_cast<std::uint32_t>(classes.size()),
      classDefs,
      static_cast<std::uint32_t>(out.size() - dataStart)};
  for (std::size_t i = 0; i < header.size(); ++i) {
    put32(out, 32 + 4 * i, header[i]);
  }
  put32(out, 108, dataStart);

  const auto digest = sha1(std::span{out}.subspan(32));
  std::ranges::copy(digest, out.begin() + 12);
  put32(out, 8, adler32(std::span{out}.subspan(12)));
  return out;
}
