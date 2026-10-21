"""Minimal JVM .class reader, sized to one job.

Slay the Spire stores every card's *statistics* (cost, type, rarity, colour,
target) in Java bytecode, not in any json.  Only the display name and the
description text live in localization/.  So to build a data table for a port
we have to read the constructors of com/megacrit/cardcrawl/cards/**/*.class:

    super(cardID, NAME, imagePath, cost, RAW_DESCRIPTION,
          CardType.X, CardColor.X, CardRarity.X, CardTarget.X);

The arguments are pushed in that order, so the constructor's Code gives us:

  * the 1st LDC string  -> cardID      ("Strike_R", "Bash", ...)
  * the 3rd LDC string  -> atlas region used for the art ("red/attack/bash")
  * the int push between the 3rd and 4th LDC -> cost
  * the four `getstatic` refs whose type is a Card* enum, in push order
    -> type, colour, rarity, target

We do not need a full instruction decoder for that: we scan for candidate
byte patterns and then *validate* them against the constant pool, so a bogus
hit gets rejected instead of desynchronising the walk.  Anything unexpected
raises ClassParseError; callers report it per card rather than guessing.
"""

from __future__ import annotations

import struct

_UTF8 = 1
_INT = 3
_FLOAT = 4
_LONG = 5
_DOUBLE = 6
_CLASS = 7
_STRING = 8
_FIELDREF = 9
_METHODREF = 10
_IMETHODREF = 11
_NAMEANDTYPE = 12
_MHANDLE = 15
_MTYPE = 16
_DYNAMIC = 17
_INVOKEDYNAMIC = 18
_MODULE = 19
_PACKAGE = 20

_ICONST_M1 = 0x02
_ICONST_0 = 0x03
_ICONST_5 = 0x08
_BIPUSH = 0x10
_SIPUSH = 0x11
_LDC = 0x12
_LDC_W = 0x13
_GETSTATIC = 0xB2


class ClassParseError(Exception):
    pass


class CardMeta:
    """Statistics of one card, recovered from its class file."""

    __slots__ = ("card_id", "class_name", "img", "cost", "type", "color",
                 "rarity", "target", "damage", "block", "magic")

    def __init__(self, **kw):
        for k in self.__slots__:
            setattr(self, k, kw.get(k))

    def as_dict(self):
        return {k: getattr(self, k) for k in self.__slots__}

    def __repr__(self):
        return (f"CardMeta({self.card_id!r} cost={self.cost} {self.type}/"
                f"{self.rarity}/{self.color} img={self.img!r})")


class ClassFile:
    """Just enough of a .class reader: constant pool + method Code bytes."""

    def __init__(self, data: bytes):
        if len(data) < 10 or data[:4] != b"\xca\xfe\xba\xbe":
            raise ClassParseError("not a class file")
        self.data = data
        self.fields = {}
        self.methods = {}
        self._read()

    # -- structure --------------------------------------------------------
    def _read(self):
        d = self.data
        cp_count = struct.unpack(">H", d[8:10])[0]
        off = 10
        cp = [None] * cp_count
        i = 1
        while i < cp_count:
            tag = d[off]
            off += 1
            if tag == _UTF8:
                ln = struct.unpack(">H", d[off:off + 2])[0]
                off += 2
                cp[i] = (_UTF8, d[off:off + ln].decode("utf-8", "replace"))
                off += ln
            elif tag == _INT:
                cp[i] = (tag, struct.unpack(">i", d[off:off + 4])[0])
                off += 4
            elif tag == _FLOAT:
                cp[i] = (tag, struct.unpack(">f", d[off:off + 4])[0])
                off += 4
            elif tag in (_LONG, _DOUBLE):
                cp[i] = (tag, struct.unpack(">q" if tag == _LONG else ">d",
                                            d[off:off + 8])[0])
                off += 8
                i += 1  # longs and doubles take two slots
            elif tag in (_CLASS, _STRING, _MTYPE, _MODULE, _PACKAGE):
                cp[i] = (tag, struct.unpack(">H", d[off:off + 2])[0])
                off += 2
            elif tag in (_FIELDREF, _METHODREF, _IMETHODREF, _NAMEANDTYPE,
                         _DYNAMIC, _INVOKEDYNAMIC):
                a, b = struct.unpack(">HH", d[off:off + 4])
                cp[i] = (tag, a, b)
                off += 4
            elif tag == _MHANDLE:
                a = d[off]
                b = struct.unpack(">H", d[off + 1:off + 3])[0]
                cp[i] = (tag, a, b)
                off += 3
            else:
                raise ClassParseError(f"unknown constant pool tag {tag} at #{i}")
            i += 1
        self.cp = cp

        off += 2  # access_flags
        this_idx = struct.unpack(">H", d[off:off + 2])[0]
        off += 4  # this_class, super_class
        n_if = struct.unpack(">H", d[off:off + 2])[0]
        off += 2 + 2 * n_if
        self.this_class_name = self.utf8(self.class_name(this_idx))

        for target in (self.fields, self.methods):
            count = struct.unpack(">H", d[off:off + 2])[0]
            off += 2
            for _ in range(count):
                _acc, name_i, desc_i, n_attr = struct.unpack(">HHHH", d[off:off + 8])
                off += 8
                attrs = []
                for _ in range(n_attr):
                    an_i, alen = struct.unpack(">HI", d[off:off + 6])
                    off += 6
                    attrs.append((self.utf8(an_i), d[off:off + alen]))
                    off += alen
                target[(self.utf8(name_i), self.utf8(desc_i))] = attrs

    # -- constant pool accessors -----------------------------------------
    def entry(self, idx):
        if not (0 < idx < len(self.cp)):
            return None
        return self.cp[idx]

    def utf8(self, idx) -> str:
        e = self.entry(idx)
        if e is None or e[0] != _UTF8:
            raise ClassParseError(f"cp#{idx} is not Utf8")
        return e[1]

    def class_name(self, idx) -> int:
        e = self.entry(idx)
        if e is None or e[0] != _CLASS:
            raise ClassParseError(f"cp#{idx} is not Class")
        return e[1]

    def string_value(self, idx):
        e = self.entry(idx)
        if e is None or e[0] != _STRING:
            return None
        return self.utf8(e[1])

    def _ref(self, idx, tag):
        e = self.entry(idx)
        if e is None or e[0] != tag:
            return None
        nat = self.entry(e[2])
        if nat is None or nat[0] != _NAMEANDTYPE:
            return None
        return self.utf8(self.class_name(e[1])), self.utf8(nat[1]), self.utf8(nat[2])

    def fieldref(self, idx):
        """-> (owner, name, descriptor) or None."""
        return self._ref(idx, _FIELDREF)

    def methodref(self, idx):
        """-> (owner, name, descriptor) or None."""
        return self._ref(idx, _METHODREF)

    # -- code attribute ---------------------------------------------------
    def code(self, name, desc):
        attrs = self.methods.get((name, desc))
        if attrs is None:
            raise ClassParseError(f"no method {name}{desc}")
        for aname, body in attrs:
            if aname == "Code":
                if len(body) < 8:
                    break
                code_len = struct.unpack(">I", body[4:8])[0]
                return body[8:8 + code_len]
        raise ClassParseError(f"{name}{desc} has no usable Code attribute")
