"""The pieces a .dex file is made of, and a small assembler for its code.

A dex file is a header, six tables that must each be sorted the way the
format says, and a data section everything else points into. Nothing here is
general: it writes what this project needs -- a handful of classes whose
bytecode is written out by hand -- and it is the whole of what stands between
the Java this project keeps as a specification and the file Android loads.
"""

import hashlib
import struct
import zlib


def uleb(value):
    out = bytearray()
    while True:
        byte = value & 0x7F
        value >>= 7
        if value:
            out.append(byte | 0x80)
        else:
            out.append(byte)
            return bytes(out)


def mutf8(text):
    """The encoding a dex string uses: UTF-8, except that NUL is two bytes."""
    out = bytearray()
    for ch in text:
        code = ord(ch)
        if code == 0:
            out += b"\xc0\x80"
        elif code < 0x80:
            out.append(code)
        elif code < 0x800:
            out.append(0xC0 | (code >> 6))
            out.append(0x80 | (code & 0x3F))
        elif code < 0x10000:
            out.append(0xE0 | (code >> 12))
            out.append(0x80 | ((code >> 6) & 0x3F))
            out.append(0x80 | (code & 0x3F))
        else:  # a surrogate pair, as the format wants it
            code -= 0x10000
            for half in (0xD800 + (code >> 10), 0xDC00 + (code & 0x3FF)):
                out.append(0xE0 | (half >> 12))
                out.append(0x80 | ((half >> 6) & 0x3F))
                out.append(0x80 | (half & 0x3F))
    return bytes(out)


class Method:
    def __init__(self, cls, name, ret, params):
        self.cls, self.name, self.ret, self.params = cls, name, ret, params


class Field:
    def __init__(self, cls, name, type_):
        self.cls, self.name, self.type = cls, name, type_


class Code:
    """Instructions, as words, with labels resolved when the method is done.

    Registers are numbers here, as they are in the file. The Java this
    mirrors is small enough that assigning them by hand is clearer than
    anything that would assign them for us.
    """

    def __init__(self, dex, registers, ins, outs):
        self.dex = dex
        self.registers = registers
        self.ins = ins
        self.outs = outs
        self.words = []
        self.labels = {}
        self.fixups = []  # (word index, label, kind)
        self.tries = []

    # -- placement ---------------------------------------------------------
    def label(self, name):
        self.labels[name] = len(self.words)

    def at(self, name):
        return self.labels[name]

    # -- the instruction forms this project uses --------------------------
    def _w(self, *words):
        self.words.extend(words)

    def op10x(self, op):
        self._w(op)

    def op11x(self, op, a):
        self._w(op | (a << 8))

    def op12x(self, op, a, b):
        self._w(op | (a << 8) | (b << 12))

    def op11n(self, op, a, lit):
        self._w(op | (a << 8) | ((lit & 0xF) << 12))

    def op21s(self, op, a, lit):
        self._w(op | (a << 8), lit & 0xFFFF)

    def op21c(self, op, a, index):
        self._w(op | (a << 8), index)

    def op22c(self, op, a, b, index):
        self._w(op | (a << 8) | (b << 12), index)

    def op23x(self, op, a, b, c):
        self._w(op | (a << 8), b | (c << 8))

    def op22b(self, op, a, b, lit):
        self._w(op | (a << 8), (b & 0xFF) | ((lit & 0xFF) << 8))

    def op35c(self, op, index, args):
        assert len(args) <= 5
        g = args[4] if len(args) > 4 else 0
        self._w(op | (len(args) << 12) | (g << 8), index)
        packed = 0
        for i, reg in enumerate(args[:4]):
            packed |= reg << (4 * i)
        self._w(packed)

    def branch(self, op, kind, a, b, target):
        if kind == "21t":
            self.fixups.append((len(self.words) + 1, target, "s2"))
            self._w(op | (a << 8), 0)
        elif kind == "22t":
            self.fixups.append((len(self.words) + 1, target, "s2"))
            self._w(op | (a << 8) | (b << 12), 0)
        elif kind == "10t":
            self.fixups.append((len(self.words), target, "s1"))
            self._w(op)
        else:
            raise AssertionError(kind)

    # named, because a reader should not have to know the numbers
    def const4(self, reg, value): self.op11n(0x12, reg, value)
    def const16(self, reg, value): self.op21s(0x13, reg, value)
    def const_string(self, reg, text): self.op21c(0x1A, reg, self.dex.string(text))
    def new_instance(self, reg, type_): self.op21c(0x22, reg, self.dex.type(type_))
    def new_array(self, dst, size, type_): self.op22c(0x23, dst, size, self.dex.type(type_))
    def check_cast(self, reg, type_): self.op21c(0x1F, reg, self.dex.type(type_))
    def move_object(self, dst, src): self.op12x(0x07, dst, src)
    def move_result(self, reg): self.op11x(0x0A, reg)
    def move_result_object(self, reg): self.op11x(0x0C, reg)
    def move_exception(self, reg): self.op11x(0x0D, reg)
    def return_void(self): self.op10x(0x0E)
    def aput_object(self, src, arr, idx): self.op23x(0x4D, src, arr, idx)
    def iget_object(self, dst, obj, field): self.op22c(0x54, dst, obj, self.dex.field(*field))
    def iput_object(self, src, obj, field): self.op22c(0x5B, src, obj, self.dex.field(*field))
    def and_int_lit8(self, dst, src, lit): self.op22b(0xDD, dst, src, lit)
    def goto(self, target): self.branch(0x28, "10t", 0, 0, target)
    def if_eq(self, a, b, target): self.branch(0x32, "22t", a, b, target)
    def if_ne(self, a, b, target): self.branch(0x33, "22t", a, b, target)
    def if_eqz(self, a, target): self.branch(0x38, "21t", a, 0, target)
    def if_nez(self, a, target): self.branch(0x39, "21t", a, 0, target)
    def invoke_virtual(self, args, method): self.op35c(0x6E, self.dex.method(*method), args)
    def invoke_super(self, args, method): self.op35c(0x6F, self.dex.method(*method), args)
    def invoke_direct(self, args, method): self.op35c(0x70, self.dex.method(*method), args)
    def nop(self): self.op10x(0x00)

    def try_catch(self, start, end, handler, type_):
        self.dex.type(type_)  # named here and nowhere else, so it is recorded
        self.tries.append((start, end, handler, type_))

    def finish(self):
        for index, target, kind in self.fixups:
            at = self.labels[target]
            base = index if kind == "s1" else index - 1
            delta = at - base
            if kind == "s1":
                assert -128 <= delta <= 127, "goto too far"
                self.words[index] |= (delta & 0xFF) << 8
            else:
                self.words[index] = delta & 0xFFFF
        return self.words


class Dex:
    """The tables, and the file they are written into.

    Everything is collected twice: once to find out which strings, types,
    prototypes, fields and methods exist, and once more with the tables
    sorted and numbered, because an instruction has to name a table index and
    the format decides what the indices are -- strings by their bytes, types
    by their string, and the rest by the things they are made of.
    """

    def __init__(self):
        self.frozen = False
        self._strings = {}
        self._types = {}
        self._protos = {}
        self._fields = {}
        self._methods = {}
        self.classes = []

    # -- the tables --------------------------------------------------------
    def string(self, text):
        if not self.frozen:
            self._strings.setdefault(text, 0)
            return 0
        return self._strings[text]

    def type(self, name):
        if not self.frozen:
            self._types.setdefault(name, 0)
            self.string(name)
            return 0
        return self._types[name]

    def proto(self, ret, params):
        key = (ret, tuple(params))
        if not self.frozen:
            self._protos.setdefault(key, 0)
            self.type(ret)
            for p in params:
                self.type(p)
            self.string(self.shorty(ret, params))
            return 0
        return self._protos[key]

    def field(self, cls, name, type_):
        key = (cls, name, type_)
        if not self.frozen:
            self._fields.setdefault(key, 0)
            self.type(cls)
            self.type(type_)
            self.string(name)
            return 0
        return self._fields[key]

    def method(self, cls, name, ret, params):
        key = (cls, name, ret, tuple(params))
        if not self.frozen:
            self._methods.setdefault(key, 0)
            self.type(cls)
            self.proto(ret, params)
            self.string(name)
            return 0
        return self._methods[key]

    @staticmethod
    def shorty(ret, params):
        """What the format calls a shorty: one letter per type, L for any
        reference, which is what a prototype is looked up by."""
        def letter(t):
            return t[0] if t[0] in "VZBSCIJFD" else "L"
        return letter(ret) + "".join(letter(p) for p in params)

    def freeze(self):
        for i, text in enumerate(sorted(self._strings, key=mutf8)):
            self._strings[text] = i
        for i, name in enumerate(sorted(self._types, key=lambda n: self._strings[n])):
            self._types[name] = i
        def proto_key(key):
            ret, params = key
            return (self._types[ret], [self._types[p] for p in params])
        for i, key in enumerate(sorted(self._protos, key=proto_key)):
            self._protos[key] = i
        def field_key(key):
            cls, name, type_ = key
            return (self._types[cls], self._strings[name], self._types[type_])
        for i, key in enumerate(sorted(self._fields, key=field_key)):
            self._fields[key] = i
        def method_key(key):
            cls, name, ret, params = key
            return (self._types[cls], self._strings[name],
                    self._protos[(ret, params)])
        for i, key in enumerate(sorted(self._methods, key=method_key)):
            self._methods[key] = i
        self.frozen = True


class Class:
    """A class, which registers everything it declares as it is described:
    what a class says about itself has to be in the tables too, and only the
    things it calls would be there otherwise."""

    def __init__(self, dex, name, super_, interfaces, access):
        self.dex = dex
        self.name = name
        self.super = super_
        self.interfaces = list(interfaces)
        self.access = access
        self.fields = []   # (name, type, access)
        self.direct = []   # (name, ret, params, access, code or None)
        self.virtual = []
        dex.type(name)
        if super_:
            dex.type(super_)
        for interface in interfaces:
            dex.type(interface)

    def field(self, name, type_, access):
        self.dex.field(self.name, name, type_)
        self.fields.append((name, type_, access))

    def method(self, name, ret, params, access, code=None, direct=False):
        self.dex.method(self.name, name, ret, params)
        (self.direct if direct else self.virtual).append(
            (name, ret, params, access, code))


TYPE_HEADER = 0x0000
TYPE_STRING_ID = 0x0001
TYPE_TYPE_ID = 0x0002
TYPE_PROTO_ID = 0x0003
TYPE_FIELD_ID = 0x0004
TYPE_METHOD_ID = 0x0005
TYPE_CLASS_DEF = 0x0006
TYPE_MAP_LIST = 0x1000
TYPE_TYPE_LIST = 0x1001
TYPE_CODE_ITEM = 0x2001
TYPE_STRING_DATA = 0x2002
TYPE_CLASS_DATA = 0x2000


def _align(blob, to):
    while len(blob) % to:
        blob.append(0)


def write(dex, classes):
    """The file, laid out in the order the format expects to find it."""
    strings = sorted(dex._strings, key=lambda s: dex._strings[s])
    types = sorted(dex._types, key=lambda t: dex._types[t])
    protos = sorted(dex._protos, key=lambda p: dex._protos[p])
    fields = sorted(dex._fields, key=lambda f: dex._fields[f])
    methods = sorted(dex._methods, key=lambda m: dex._methods[m])

    header_size = 112
    off = header_size
    string_ids_off = off; off += 4 * len(strings)
    type_ids_off = off; off += 4 * len(types)
    proto_ids_off = off; off += 12 * len(protos)
    field_ids_off = off; off += 8 * len(fields)
    method_ids_off = off; off += 8 * len(methods)
    class_defs_off = off; off += 32 * len(classes)

    data = bytearray()
    data_off = off

    def here():
        return data_off + len(data)

    # type lists: the parameters of a prototype, and the interfaces of a class
    type_lists = {}
    def type_list(items):
        key = tuple(items)
        if not key:
            return 0
        if key in type_lists:
            return type_lists[key]
        _align(data, 4)
        at = here()
        data.extend(struct.pack('<I', len(key)))
        for t in key:
            data.extend(struct.pack('<H', dex.type(t)))
        type_lists[key] = at
        return at

    proto_params = [type_list(list(p[1])) for p in protos]
    interfaces_at = {c.name: type_list(c.interfaces) for c in classes}
    type_list_count = len(type_lists)

    # code items
    code_at = {}
    code_count = 0
    for cls in classes:
        for kind in (cls.direct, cls.virtual):
            for (name, ret, params, access, code) in kind:
                if code is None:
                    continue
                words = code.finish()
                _align(data, 4)
                at = here()
                handlers = bytearray()
                tries = bytearray()
                if code.tries:
                    # One list of handlers, and every try points into it.
                    handler_offsets = []
                    body = bytearray()
                    body.extend(uleb(len(code.tries)))
                    for (start, end, handler, type_) in code.tries:
                        handler_offsets.append(len(body))
                        body.extend(uleb(1))  # one typed catch, no catch-all
                        body.extend(uleb(dex.type(type_)))
                        body.extend(uleb(code.at(handler)))
                    handlers = body
                    for (start, end, handler, type_), where in zip(
                            code.tries, handler_offsets):
                        first = code.at(start)
                        tries.extend(struct.pack('<IHH', first,
                                                 code.at(end) - first, where))
                data.extend(struct.pack('<HHHHII', code.registers, code.ins,
                                        code.outs, len(code.tries), 0,
                                        len(words)))
                for w in words:
                    data.extend(struct.pack('<H', w & 0xFFFF))
                if code.tries and len(words) % 2:
                    data.extend(b'\x00\x00')
                data.extend(tries)
                data.extend(handlers)
                code_at[(cls.name, name, ret, tuple(params))] = at
                code_count += 1

    # class data
    class_data_at = {}
    for cls in classes:
        at = here()
        blob = bytearray()
        statics = [f for f in cls.fields if f[2] & 0x8]
        instances = [f for f in cls.fields if not f[2] & 0x8]
        blob.extend(uleb(len(statics)))
        blob.extend(uleb(len(instances)))
        blob.extend(uleb(len(cls.direct)))
        blob.extend(uleb(len(cls.virtual)))
        for group in (statics, instances):
            previous = 0
            for (name, type_, access) in sorted(
                    group, key=lambda f: dex.field(cls.name, f[0], f[1])):
                index = dex.field(cls.name, name, type_)
                blob.extend(uleb(index - previous))
                blob.extend(uleb(access))
                previous = index
        for group in (cls.direct, cls.virtual):
            previous = 0
            for (name, ret, params, access, code) in sorted(
                    group, key=lambda m: dex.method(cls.name, m[0], m[1], m[2])):
                index = dex.method(cls.name, name, ret, params)
                blob.extend(uleb(index - previous))
                blob.extend(uleb(access))
                blob.extend(uleb(code_at.get((cls.name, name, ret, tuple(params)), 0)))
                previous = index
        data.extend(blob)
        class_data_at[cls.name] = at

    # string data
    string_data_at = []
    for text in strings:
        string_data_at.append(here())
        encoded = mutf8(text)
        units = sum(2 if ord(c) > 0xFFFF else 1 for c in text)
        data.extend(uleb(units))
        data.extend(encoded)
        data.append(0)

    # the map: every section, in the order it appears
    _align(data, 4)
    map_off = here()
    entries = [
        (TYPE_HEADER, 1, 0),
        (TYPE_STRING_ID, len(strings), string_ids_off),
        (TYPE_TYPE_ID, len(types), type_ids_off),
        (TYPE_PROTO_ID, len(protos), proto_ids_off),
        (TYPE_FIELD_ID, len(fields), field_ids_off),
        (TYPE_METHOD_ID, len(methods), method_ids_off),
        (TYPE_CLASS_DEF, len(classes), class_defs_off),
        (TYPE_TYPE_LIST, type_list_count, min(type_lists.values()) if type_lists else 0),
        (TYPE_CODE_ITEM, code_count, min(code_at.values()) if code_at else 0),
        (TYPE_CLASS_DATA, len(classes), min(class_data_at.values())),
        (TYPE_STRING_DATA, len(strings), string_data_at[0]),
        (TYPE_MAP_LIST, 1, map_off),
    ]
    entries = [e for e in entries if e[1]]
    entries.sort(key=lambda e: e[2])
    data.extend(struct.pack('<I', len(entries)))
    for kind, size, at in entries:
        data.extend(struct.pack('<HHII', kind, 0, size, at))

    # and now the tables that point into all of that
    out = bytearray(b'\x00' * data_off)
    for i, at in enumerate(string_data_at):
        struct.pack_into('<I', out, string_ids_off + 4 * i, at)
    for i, t in enumerate(types):
        struct.pack_into('<I', out, type_ids_off + 4 * i, dex.string(t))
    for i, (ret, params) in enumerate(protos):
        struct.pack_into('<III', out, proto_ids_off + 12 * i,
                         dex.string(Dex.shorty(ret, list(params))),
                         dex.type(ret), proto_params[i])
    for i, (cls, name, type_) in enumerate(fields):
        struct.pack_into('<HHI', out, field_ids_off + 8 * i,
                         dex.type(cls), dex.type(type_), dex.string(name))
    for i, (cls, name, ret, params) in enumerate(methods):
        struct.pack_into('<HHI', out, method_ids_off + 8 * i,
                         dex.type(cls), dex.proto(ret, list(params)),
                         dex.string(name))
    for i, cls in enumerate(classes):
        struct.pack_into('<IIIIIIII', out, class_defs_off + 32 * i,
                         dex.type(cls.name), cls.access,
                         dex.type(cls.super) if cls.super else 0xFFFFFFFF,
                         interfaces_at[cls.name], 0xFFFFFFFF, 0,
                         class_data_at[cls.name], 0)
    out.extend(data)

    struct.pack_into('<8s', out, 0, b'dex\n035\x00')
    struct.pack_into('<IIIIIIIIIIIIIIIIIII', out, 32,
                     len(out), header_size, 0x12345678, 0, 0, map_off,
                     len(strings), string_ids_off,
                     len(types), type_ids_off,
                     len(protos), proto_ids_off,
                     len(fields), field_ids_off,
                     len(methods), method_ids_off,
                     len(classes), class_defs_off,
                     len(out) - data_off)
    struct.pack_into('<I', out, 108, data_off)
    struct.pack_into('<20s', out, 12, hashlib.sha1(out[32:]).digest())
    struct.pack_into('<I', out, 8, zlib.adler32(bytes(out[12:])) & 0xFFFFFFFF)
    return bytes(out)
