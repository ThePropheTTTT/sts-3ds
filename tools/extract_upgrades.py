#!/usr/bin/env python3
"""Extract real upgrade deltas from the game's bytecode.

Modern StS card classes upgrade through helper calls:

    aload_0; iconst_3; invokevirtual upgradeDamage(I)V

while older ones assign or add to the field directly:

    aload_0; dup; getfield baseDamage; iconst_2; iadd; putfield baseDamage

Both shapes are handled here. Output keys are the card ids from cards.json.

    python tools/extract_upgrades.py          -> assets/data/upgrades.json
"""

from __future__ import annotations

import argparse
import io
import json
import os
import sys
import zipfile

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(HERE, "tools"))

import bytecode  # noqa: E402
from javaclass import ClassFile  # noqa: E402

DEFAULT_JAR = os.environ.get("STS_JAR", "desktop-1.0.jar")
CARD_PREFIX = "com/megacrit/cardcrawl/cards/"

FIELDS = {"baseDamage": "damage", "baseBlock": "block", "baseMagicNumber": "magic"}
HELPERS = {
    "upgradeDamage": "damage",
    "upgradeBlock": "block",
    "upgradeMagicNumber": "magic",
}

# Opcodes allowed between the constant push and the consuming instruction.
ALLOWED = {0x2A, 0x2B, 0x2C, 0x2D, 0x59, 0xB4} | set(range(0x02, 0x09)) | {0x10, 0x11, 0x84}


def int_before(insns, index):
    for k in range(index - 1, max(-1, index - 8), -1):
        op, operand = insns[k][1], insns[k][2]
        if op in ALLOWED:
            value = bytecode.push_int(op, operand)
            if value is not None:
                return value
            continue
        return None
    return None


def analyse_upgrade(cf: ClassFile, base: dict) -> dict:
    code = None
    for (name, desc), _attrs in cf.methods.items():
        if name == "upgrade":
            code = cf.code(name, desc)
            break
    if not code:
        return {}

    insns = list(bytecode.iter_insns(code))
    deltas: dict[str, int] = {}

    for i, (_offset, op, operand) in enumerate(insns):
        ref = None
        if op in (0xB6, 0xB7, 0xB8):
            ref = cf.methodref(bytecode.u2(operand))
            if ref and ref[1] in HELPERS:
                value = int_before(insns, i)
                if value is not None:
                    field = HELPERS[ref[1]]
                    deltas[field] = deltas.get(field, 0) + value
        elif op == 0xB5:
            ref = cf.fieldref(bytecode.u2(operand))
            if ref and ref[1] in FIELDS:
                field = FIELDS[ref[1]]
                value = int_before(insns, i)
                if value is None:
                    continue
                is_add = i > 0 and insns[i - 1][1] == 0x60
                deltas[field] = value if is_add else value - (base.get(field) or 0)

    return {k: v for k, v in deltas.items() if v}


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--jar", default=DEFAULT_JAR)
    ap.add_argument("--cards", default=os.path.join(HERE, "assets/data/cards.json"))
    ap.add_argument("--out", default=os.path.join(HERE, "assets/data/upgrades.json"))
    args = ap.parse_args(argv)

    with open(args.cards, encoding="utf-8") as fp:
        cards = json.load(fp)
    by_class = {card["class"]: card for card in cards if card.get("class")}

    result = {}
    missing = []
    with zipfile.ZipFile(args.jar) as zf:
        for name in zf.namelist():
            if not name.startswith(CARD_PREFIX) or not name.endswith(".class") or "$" in name:
                continue
            simple = os.path.basename(name)[:-6]
            card = by_class.get(simple)
            if card is None:
                continue
            try:
                cf = ClassFile(zf.read(name))
            except Exception:
                continue
            deltas = analyse_upgrade(cf, card)
            if deltas:
                result[card["id"]] = deltas
            else:
                missing.append(simple)

    with io.open(args.out, "w", encoding="utf-8") as fp:
        json.dump(result, fp, ensure_ascii=False, indent=1, sort_keys=True)

    print(f"cards with upgrade deltas: {len(result)}  (none found: {len(missing)})")
    for sample in ("Strike_R", "Defend_R", "Bash", "Anger", "Cleave", "Clothesline", "Uppercut"):
        if sample in result:
            print(f"  {sample}: {result[sample]}")
    if missing:
        print("  no delta:", ", ".join(missing[:10]))
    print(f"-> {args.out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
