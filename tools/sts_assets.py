"""Readers for the way Slay the Spire ships its assets.

The game jar is a plain zip.  Everything interesting for a port lives in:

    cards/cards.atlas + cards{,2,3,4,5}.png   card art (libGDX texture atlas)
    cardui/cardui*.png                        card frames, orbs, banners
    localization/<lang>/*.json                display strings (UTF-8)
    com/megacrit/cardcrawl/cards/**/*.class   card statistics (see javaclass)

Only the first, second, fourth and the card classes are needed to build a card
database; audio, shaders and the rest are deliberately not touched here.
"""

from __future__ import annotations

import io
import json
import re
import zipfile

from javaclass import ClassFile, CardMeta, ClassParseError
from bytecode import iter_insns, push_int, u1, u2

# --------------------------------------------------------------------------
# jar
# --------------------------------------------------------------------------


class Jar:
    def __init__(self, path):
        self.path = str(path)
        self._z = zipfile.ZipFile(self.path)
        self._names = self._z.namelist()

    def names(self, prefix="", suffix=""):
        return [n for n in self._names if n.startswith(prefix) and n.endswith(suffix)]

    def has(self, name):
        return name in self._z.namelist()

    def read(self, name):
        return self._z.read(name)

    def text(self, name):
        # the game's json files carry a BOM; utf-8-sig eats it when present
        return self._z.read(name).decode("utf-8-sig")

    def json(self, name):
        return json.loads(self.text(name))

    def image(self, name):
        from PIL import Image
        img = Image.open(io.BytesIO(self.read(name)))
        img.load()
        return img


# --------------------------------------------------------------------------
# libGDX texture atlas
# --------------------------------------------------------------------------

_ATLAS_PAGE_PROPS = {"size", "format", "filter", "repeat", "pma"}


class Region:
    __slots__ = ("name", "page", "x", "y", "w", "h", "orig", "offset")

    def __init__(self, name, page):
        self.name = name
        self.page = page
        self.x = self.y = 0
        self.w = self.h = 0
        self.orig = (0, 0)
        self.offset = (0, 0)

    @property
    def base(self):
        """Last path component: 'red/attack/bash' -> 'bash'."""
        return self.name.rsplit("/", 1)[-1]

    @property
    def group(self):
        """Path without the last component: 'red/attack/bash' -> 'red/attack'."""
        return self.name.rsplit("/", 1)[0] if "/" in self.name else ""

    def box(self):
        """(x, y, w, h) in image space, y measured from the top."""
        return self.x, self.y, self.w, self.h

    def __repr__(self):
        return f"Region({self.name!r} {self.page} {self.box()})"


def parse_atlas(text):
    """Parse libGDX .atlas text -> (pages, {region_name: Region}).

    Format (page properties are flush-left in the game's own packer output):

        cards.png
        size: 2048,2048
        format: RGBA8888
        filter: Linear,Linear
        repeat: none
        blue/skill/aggregate
          rotate: false
          xy: 491, 1831
          size: 244, 182
          orig: 250, 190
          offset: 2, 8
          index: -1
    """
    pages = []
    regions = {}
    page = None
    cur = None
    for raw in text.splitlines():
        if not raw.strip():
            continue
        s = raw.strip()
        indented = raw[0] in " \t"
        if not indented and s.lower().endswith((".png", ".jpg", ".jpeg")):
            page = s
            pages.append(s)
            cur = None
            continue
        if ":" in s:
            key, _, val = s.partition(":")
            key = key.strip()
            val = val.strip()
            if indented:
                if cur is not None:
                    reg = regions[cur]
                    if key == "xy":
                        reg.x, reg.y = (int(v) for v in val.split(","))
                    elif key == "size":
                        reg.w, reg.h = (int(v) for v in val.split(","))
                    elif key == "orig":
                        reg.orig = tuple(int(v) for v in val.split(","))
                    elif key == "offset":
                        reg.offset = tuple(int(v) for v in val.split(","))
                continue
            if key in _ATLAS_PAGE_PROPS and cur is None:
                continue  # flush-left page property
        if not indented:
            cur = s
            regions[s] = Region(s, page)
    return pages, regions


# --------------------------------------------------------------------------
# card text markup
# --------------------------------------------------------------------------

_MARK = {
    "D": "damage",      # !D!  the card's damage value
    "B": "block",       # !B!  the card's block value
    "M": "magic",       # !M!  magic number
}
_COLOR = {
    "y": "yellow", "g": "green", "b": "blue",
    "r": "red", "p": "purple", "w": "white",
}
_ICON_RE = re.compile(r"\[([A-Za-z]+)\]")


def tokenize_desc(desc, var_bytes=(b"D", b"B", b"M")):
    """Split a card description into renderable tokens.

    Output is a list of short tuples, deliberately json-friendly:
        ("t", text)          plain run
        ("v", "damage")      a computed number
        ("c", "yellow")      colour switch for the following runs
        ("n",)               line break
        ("i", "E")           inline icon by name
    """
    toks = []
    buf = []

    def flush():
        if buf:
            toks.append(("t", "".join(buf)))
            buf.clear()

    i = 0
    n = len(desc)
    while i < n:
        c = desc[i]
        if c == "!" and i + 2 < n and desc[i + 2] == "!":
            key = desc[i + 1]
            kind = _MARK.get(key)
            if kind:
                flush()
                toks.append(("v", kind))
                i += 3
                continue
        if c == "#" and i + 1 < n and desc[i + 1] in _COLOR:
            flush()
            toks.append(("c", _COLOR[desc[i + 1]]))
            i += 2
            continue
        if c == "N" and desc.startswith("NL", i):
            flush()
            toks.append(("n",))
            i += 2
            continue
        if c == "[":
            m = _ICON_RE.match(desc, i)
            if m:
                flush()
                toks.append(("i", m.group(1)))
                i = m.end()
                continue
        buf.append(c)
        i += 1
    flush()
    return toks


# --------------------------------------------------------------------------
# card statistics out of bytecode
# --------------------------------------------------------------------------

_ENUM_SUFFIX = (
    ("CardType;", "type"),
    ("CardColor;", "color"),
    ("CardRarity;", "rarity"),
    ("CardTarget;", "target"),
)

_ABSTRACT_CARD = "com/megacrit/cardcrawl/cards/AbstractCard"

# field name in the card class -> key in CardMeta
_VALUE_FIELDS = {
    "baseDamage": "damage",
    "baseBlock": "block",
    "baseMagicNumber": "magic",
}


def _super_call(cf, code):
    """Offset of the invokespecial that reaches AbstractCard.<init>, or None.

    Card classes declare a no-argument constructor and call super(...) with
    the nine card arguments inside it, so everything we want sits in the byte
    range in front of this instruction.
    """
    for off, op, operand in iter_insns(code):
        if op != 0xB7:
            continue
        ref = cf.methodref(u2(operand))
        if ref is not None and ref[0] == _ABSTRACT_CARD and ref[1] == "<init>":
            return off
    return None


def _enum_slot(desc):
    for suffix, slot in _ENUM_SUFFIX:
        if desc.endswith(suffix):
            return slot
    return None


def read_card_meta(jar, class_path):
    """Recover CardMeta from one card class inside the jar."""
    cf = ClassFile(jar.read(class_path))
    full = None
    end = None
    for (name, desc), _attrs in cf.methods.items():
        if name != "<init>":
            continue
        try:
            body = cf.code(name, desc)
            cut = _super_call(cf, body)
        except (ClassParseError, ValueError):
            continue
        if cut is not None:
            full, end = body, cut
            break
    if full is None:
        raise ClassParseError("no constructor calling AbstractCard.<init>")
    code = full[:end]
    literals = []         # (offset, text)      ldc of a String constant
    enums = []            # (offset, slot, name) getstatic of a Card* enum
    ints = []             # (offset, value)      iconst/bipush/sipush
    for off, op, operand in iter_insns(code):
        if op == 0xB2:
            ref = cf.fieldref(u2(operand))
            if ref is not None:
                slot = _enum_slot(ref[2])
                if slot:
                    enums.append((off, slot, ref[1]))
        elif op == 0x12:
            s = cf.string_value(u1(operand))
            if s is not None:
                literals.append((off, s))
        elif op == 0x13:
            s = cf.string_value(u2(operand))
            if s is not None:
                literals.append((off, s))
        else:
            v = push_int(op, operand)
            if v is not None:
                ints.append((off, v))

    enums.sort()
    literals.sort()
    ints.sort()
    if len(enums) < 4:
        raise ClassParseError(f"found {len(enums)} card enum constants, need 4")

    # Argument order is id, name, img, cost, rawDescription, then the enums.
    # `name` and `rawDescription` are usually getfields of the card's static
    # CardStrings, so the trailing int literal before the enum block is the
    # cost and the last string literal before it is the image path.
    first_enum = enums[0][0]
    pre_ints = [t for t in ints if t[0] < first_enum]
    if not pre_ints:
        raise ClassParseError("no integer literal before the enum arguments")
    cost_off, cost = pre_ints[-1]

    pre_lits = [t for t in literals if t[0] < cost_off]
    if len(pre_lits) < 2:
        raise ClassParseError(f"only {len(pre_lits)} string literals before the cost")
    card_id = pre_lits[0][1]
    img = pre_lits[-1][1]

    slots = {slot: name for _off, slot, name in enums[:4]}

    # Base numbers are plain field stores right after the super call:
    #   this.baseDamage = 8   /  this.baseBlock = 5  /  this.baseMagicNumber = 2
    values = {}
    prev_int = None
    for off, op, operand in iter_insns(full[end:]):
        if op == 0xB5:  # putfield
            ref = cf.fieldref(u2(operand))
            if ref is not None:
                slot = _VALUE_FIELDS.get(ref[1])
                if slot and prev_int is not None:
                    values.setdefault(slot, prev_int)
            prev_int = None
        else:
            prev_int = push_int(op, operand)

    return CardMeta(
        card_id=card_id,
        class_name=cf.this_class_name.rsplit("/", 1)[-1],
        img=img,
        cost=cost,
        type=slots.get("type"),
        color=slots.get("color"),
        rarity=slots.get("rarity"),
        target=slots.get("target"),
        damage=values.get("damage"),
        block=values.get("block"),
        magic=values.get("magic"),
    )


CARD_ROOT = "com/megacrit/cardcrawl/cards/"


def card_class_paths(jar, groups=None):
    """Card class file names, optionally limited to colour subpackages."""
    out = []
    for n in jar.names(prefix=CARD_ROOT, suffix=".class"):
        rest = n[len(CARD_ROOT):]
        if "/" not in rest:
            continue  # AbstractCard and friends
        parts = rest.split("/")
        colour = parts[0]
        if groups and colour not in groups:
            continue
        if "$" in parts[-1]:
            continue  # anonymous / inner classes
        out.append(n)
    return sorted(out)


def card_group(class_path):
    """'com/.../cards/red/Bash.class' -> 'red'."""
    return class_path[len(CARD_ROOT):].split("/")[0]
