#!/usr/bin/env python3
"""Generate port/source/cards_gen.h from the extracted card JSON.

The C side needs a flat, static table: no JSON parser on the 3DS, no allocation.
Card text is pre-substituted (numbers baked in) because v0.1 has no upgrades and
no dynamic values; dynamic cards are handled as special cases in battle.c.

    python tools/gen_cards.py
"""

from __future__ import annotations

import argparse
import json
import re
import os

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

TYPE_CODE = {"ATTACK": 0, "SKILL": 1, "POWER": 2, "STATUS": 3, "CURSE": 4}
TARGET_CODE = {"ENEMY": 0, "ALL_ENEMY": 1, "SELF": 2, "ALL": 3, "NONE": 4, "SELF_AND_ENEMY": 5}
RARITY_CODE = {"BASIC": 0, "COMMON": 1, "UNCOMMON": 2, "RARE": 3, "SPECIAL": 4, "CURSE": 5, "STATUS": 6}
COLOR_CODE = {"red": 0, "green": 1, "blue": 2, "purple": 3, "colorless": 4, "curse": 5, "status": 6}


def c_string(text: str) -> str:
    out = []
    for ch in text:
        if ch == "\\":
            out.append("\\\\")
        elif ch == '"':
            out.append('\\"')
        elif ch == "\n":
            out.append("\\n")
        elif ord(ch) < 0x20:
            out.append(" ")
        else:
            out.append(ch)
    return '"' + "".join(out) + '"'


TEXT_UP_OVERRIDES = {
    # Cards whose upgrade changes wording, keywords or targets instead of just
    # one number.  The zhs card JSON keeps the base text for most of them, so
    # these strings are taken from the game's own UPGRADE_DESCRIPTION text.
    "True Grit": "获得 {block} 点格挡。\n选择一张手牌消耗。",
    "Ghostly": "获得1层无实体。\n消耗。",
    "Blind": "给予所有敌人 {magic} 层虚弱。",
    "Trip": "给予所有敌人 {magic} 层易伤。",
    "Discovery": "从3张随机牌中选择1张加入你的手牌。这张牌在本回合耗能变为0。",
    "Enlightenment": "在本场战斗，你当前手牌中所有牌的耗能降低到1。",
    "Forethought": "将手牌中的任意张牌放到抽牌堆的底部。\n并且它们在被打出之前，耗能变为0。",
    "Secret Technique": "从抽牌堆中选择一张技能牌放入你的手牌。",
    "Secret Weapon": "从抽牌堆中选择一张攻击牌放入你的手牌。",
    "Thinking Ahead": "抽2张牌，然后将手牌中的一张牌放到你抽牌堆的顶端。",
    "Transmutation": "在你的手牌中加入 X 张升级过的随机无色牌。它们在本回合的耗能变为0。\n消耗。",
    "Armaments": "获得 {block} 点格挡。\n在本场战斗中升级手牌中的所有牌。",
    "Bloodletting": "获得 E E E。\n失去 3 点生命。",
    "Brutality": "固有。\n在你的回合开始时，你失去1点生命，抽1张牌。",
    "Double Tap": "在这个回合，你打出的下 {magic} 张攻击牌会打出两次。",
    "Dual Wield": "将你手牌中的一张攻击或能力牌复制 {magic} 张。",
    "Limit Break": "将你的力量翻倍。",
    "Perfected Strike": "造成 {damage} 点伤害。\n你每有一张名字中有打击的牌，伤害+{magic}。",
    "Burn": "不能被打出。\n在你的回合结束时，你受到{magic}点伤害。",
    "Sentinel": "获得 {block} 点格挡。\n如果这张牌被消耗，\n获得 E E E。",
    "Searing Blow": "造成 16 点伤害。\n能被多次升级。",
}


def upgrade_text(card, deltas) -> str:
    override = TEXT_UP_OVERRIDES.get(card["id"])
    if override:
        return override.format(
            damage=(card.get("damage") or 0) + deltas.get("damage", 0),
            block=(card.get("block") or 0) + deltas.get("block", 0),
            magic=(card.get("magic") or 0) + deltas.get("magic", 0),
        )
    return render_text(card.get("text"), card, deltas)


def render_text(atoms, card, deltas=None) -> str:
    values = {"damage": card.get("damage"), "block": card.get("block"), "magic": card.get("magic")}
    if deltas:
        # Real deltas from tools/extract_upgrades.py (bytecode upgrade()).
        for key in ("damage", "block", "magic"):
            if values.get(key) and deltas.get(key):
                values[key] = values[key] + deltas[key]
    parts = []
    for atom in atoms or []:
        kind = atom[0]
        if kind == "t":
            parts.append(atom[1])
        elif kind == "v":
            value = values.get(atom[1])
            parts.append(str(value if value is not None else "?"))
        elif kind == "n":
            parts.append("\n")
        elif kind == "i":
            parts.append("E")
    text = "".join(parts)
    # Drop the spaces the game puts around highlighted keywords: CJK-CJK and
    # spaces before CJK punctuation read as noise on a 62px card.
    text = re.sub(r"(?<=[\u4e00-\u9fff]) (?=[\u4e00-\u9fff])", "", text)
    text = re.sub(r" (?=[。，、；：？！）])", "", text)
    text = text.replace("\n ", "\n")
    # Collapse the single spaces the game puts around inline numbers.
    while "  " in text:
        text = text.replace("  ", " ")
    return text.replace(" \n", "\n").strip()


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--root", default=HERE)
    ap.add_argument("--cards", default=None)
    ap.add_argument("--out", default=None)
    args = ap.parse_args(argv)
    cards_path = args.cards or os.path.join(args.root, "assets/data/cards.json")
    out_path = args.out or os.path.join(args.root, "port/source/cards_gen.h")

    with open(cards_path, encoding="utf-8") as fp:
        cards = json.load(fp)

    upgrades_path = os.path.join(args.root, "assets/data/upgrades.json")
    upgrades = {}
    if os.path.exists(upgrades_path):
        with open(upgrades_path, encoding="utf-8") as fp:
            upgrades = json.load(fp)

    pages = sorted({c["art"]["page"] for c in cards if c.get("art")})

    lines = [
        "/* Generated by tools/gen_cards.py -- do not edit. */",
        "#ifndef STS_CARDS_GEN_H",
        "#define STS_CARDS_GEN_H",
        "",
        "struct CardDef {",
        "    const char *id;",
        "    const char *name;",
        "    const char *text;",
        "    const char *text_up;",
        "    signed char  cost;      /* -1 = X, -2 = unplayable */",
        "    signed char  cost_up;   /* upgraded cost (absolute; same as cost if unchanged) */",
        "    unsigned char type;     /* 0 attack, 1 skill, 2 power, 3 status, 4 curse */",
        "    unsigned char target;   /* 0 enemy, 1 all enemies, 2 self, 3 all, 4 none */",
        "    unsigned char color;    /* 0 red, 4 colorless, 5 curse, 6 status */",
        "    unsigned char rarity;   /* 0 basic, 1 common, 2 uncommon, 3 rare, 4 special */",
        "    signed char up_damage, up_block, up_magic;   /* real upgrade deltas */",
        "    short damage, block, magic;",
        "    unsigned char page;     /* index into the card art sheets */",
        "    unsigned short art_x, art_y;",
        "    unsigned char art_w, art_h;",
        "    /* trimmed art sub-rect inside the 250x190 portrait: left, top, w, h */",
        "    unsigned char art_fx, art_fy, art_fw, art_fh;",
        "};",
        "",
        f"#define STS_CARD_COUNT {len(cards)}",
        f"#define STS_CARD_ART_PAGES {len(pages)}",
        "",
        "static const struct CardDef sts_cards[] = {",
    ]

    for card in cards:
        deltas = upgrades.get(card["id"], {})
        art = card.get("art")
        if art:
            x, y, w, h = art["px"]
            page = pages.index(art["page"])
            fx, fy, fw, fh = art.get("trim", [0, 0, 0, 0])
        else:
            x = y = w = h = 0
            fx = fy = fw = fh = 0
            page = 0
        lines.append(
            "    {%s, %s, %s, %s, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d},"
            % (
                c_string(card["id"]),
                c_string(card["name"]),
                c_string(render_text(card.get("text"), card)),
                c_string(upgrade_text(card, deltas)),
                card.get("cost") if card.get("cost") is not None else -2,
                deltas.get("cost", card.get("cost") if card.get("cost") is not None else -2),
                TYPE_CODE.get(card.get("type", ""), 0),
                TARGET_CODE.get(card.get("target", ""), 4),
                COLOR_CODE.get(card.get("group", "red"), 0),
                RARITY_CODE.get(card.get("rarity", ""), 1),
                deltas.get("damage", 0),
                deltas.get("block", 0),
                deltas.get("magic", 0),
                card.get("damage") or 0,
                card.get("block") or 0,
                card.get("magic") or 0,
                page,
                x,
                y,
                w,
                h,
                fx,
                fy,
                fw,
                fh,
            )
        )
    lines += ["};", "", "#endif", ""]
    os.makedirs(os.path.dirname(out_path), exist_ok=True)
    with open(out_path, "w", encoding="utf-8", newline="\n") as fp:
        fp.write("\n".join(lines))
    print(f"wrote {out_path}: {len(cards)} cards, {len(pages)} art pages")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
