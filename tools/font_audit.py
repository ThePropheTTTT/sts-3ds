"""Report characters used by the port's string literals but absent from the atlas.

The atlas is built from a fixed charset plus whatever the sources contained when
the font was last baked, so adding UI text silently renders blanks (there is no
tofu fallback: font.c skips unknown glyphs and advances half a line).  Run this
after touching any string in port/source:

    python tools/font_audit.py          # exit 1 and list missing glyphs
"""

import argparse
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
DEFAULT_FONT = os.path.join(ROOT, "assets/font/font_zhs.h")
SOURCE_DIR = os.path.join(ROOT, "port/source")


def glyph_codes(header: str) -> set:
    """Codepoints present in font_zhs.h (each line is {code, glyph, w, h, ...})."""
    with open(header, encoding="utf-8") as fh:
        text = fh.read()
    return {int(m) for m in re.findall(r"\{(\d+),", text)}


def literals(directory: str):
    """(path, string) for every C string literal in the port sources."""
    for name in sorted(os.listdir(directory)):
        if not (name.endswith(".c") or name.endswith(".h")):
            continue
        if name.endswith("_gen.h") or name.startswith("font"):
            continue
        path = os.path.join(directory, name)
        with open(path, encoding="utf-8", errors="ignore") as fh:
            src = fh.read()
        for s in re.findall(r'"((?:[^"\\]|\\.)*)"', src):
            yield name, s


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--font", default=DEFAULT_FONT)
    ap.add_argument("--sources", default=SOURCE_DIR)
    args = ap.parse_args(argv)

    codes = glyph_codes(args.font)
    seen = {}
    for name, text in literals(args.sources):
        for ch in text:
            if ord(ch) > 0x2000:
                seen.setdefault(ch, name)

    missing = sorted((c for c in seen if ord(c) not in codes), key=ord)
    print(f"{len(seen)} distinct characters in port/source; atlas has {len(codes)} glyphs")
    for ch in missing:
        print(f"  missing {ch!r} U+{ord(ch):04X} used by {seen[ch]}")
    if missing:
        print("add them to the charset and re-run tools/to_t3x.sh + port/build.sh")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
