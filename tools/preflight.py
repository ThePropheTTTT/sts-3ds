#!/usr/bin/env python3
"""Check a built STS 3DS package, and optionally read the logs off an SD card.

This is the host-side half of the hardware test pipeline.  It needs no
devkitARM, no jar and no third-party packages:

    python tools/preflight.py
    python tools/preflight.py --sdmc E:\\

The first form checks that the .3dsx, SMDH, romfs, generated t3x files and
(if assets/audio exists) the packed PCM tracks are present and non-empty.  The
second also reads

    3ds/sts3ds/log.txt
    3ds/sts3ds/crash.txt

and reports FAIL/WARN lines and a crash dump.  Exit code 1 means a real
problem (missing package file, a FAIL row, or a crash); warnings are printed
but do not fail the run.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BUILD = ROOT / "port" / "build"
ASSETS = ROOT / "assets" / "t3x"
AUDIO = ROOT / "assets" / "audio"
AUDIO_TRACKS = ["act1.pcm", "act2.pcm", "act3.pcm"]

REQUIRED_ROMFS = [
    "font_12.t3x", "font_16.t3x", "font_20.t3x",
    "act1.t3x", "act2.t3x", "act3.t3x",
    "monsters.t3x", "combat.t3x", "ui.t3x", "map_paper.t3x",
    "relic_icons.t3x", "visuals.t3x",
]
CARD_PAGES = ["cards_000.t3x", "cards_001.t3x"]


def ok(msg: str) -> None:
    print(f"[ OK ] {msg}")


def bad(msg: str) -> None:
    print(f"[FAIL] {msg}")


def warn(msg: str) -> None:
    print(f"[WARN] {msg}")


def check_file(path: Path, label: str) -> bool:
    if not path.is_file():
        bad(f"{label}: missing ({path})")
        return False
    size = path.stat().st_size
    if size < 4:
        bad(f"{label}: too small ({size} bytes)")
        return False
    ok(f"{label}: {size} bytes")
    return True


def check_package() -> int:
    failures = 0
    print("== package ==")
    if not check_file(BUILD / "sts3ds.3dsx", "sts3ds.3dsx"):
        failures += 1
    else:
        magic = (BUILD / "sts3ds.3dsx").read_bytes()[:4]
        if magic != b"3DSX":
            bad(f"sts3ds.3dsx: bad magic {magic!r}")
            failures += 1

    smdh = sorted(BUILD.glob("*.smdh"))
    if not smdh:
        bad("SMDH: no .smdh next to the .3dsx")
        failures += 1
    else:
        for path in smdh:
            data = path.read_bytes()
            if len(data) < 0x36C0 or data[:4] != b"SMDH":
                bad(f"SMDH: {path.name} is malformed ({len(data)} bytes)")
                failures += 1
            else:
                ok(f"SMDH: {path.name} ({len(data)} bytes)")

    romfs = BUILD / "romfs"
    print("== romfs ==")
    for name in REQUIRED_ROMFS:
        if not check_file(romfs / name, f"romfs/{name}"):
            failures += 1
    for name in CARD_PAGES:
        if not check_file(romfs / name, f"romfs/{name}"):
            failures += 1

    print("== generated assets ==")
    for name in REQUIRED_ROMFS:
        if not check_file(ASSETS / name, f"assets/t3x/{name}"):
            failures += 1

    # Audio is optional in the repository, but if it was extracted then the
    # build must have packed it; otherwise the 3dsx silently ships silent.
    if (AUDIO / "act1.pcm").is_file():
        print("== romfs audio ==")
        total = 0
        for name in AUDIO_TRACKS:
            path = romfs / "audio" / name
            if check_file(path, f"romfs/audio/{name}"):
                total += path.stat().st_size
            else:
                failures += 1
        for src in sorted((AUDIO / "sfx").glob("*.pcm")):
            path = romfs / "audio" / "sfx" / src.name
            if check_file(path, f"romfs/audio/sfx/{src.name}"):
                total += path.stat().st_size
            else:
                failures += 1
        ok(f"packed audio: {total / (1024 * 1024):.1f} MiB")
    else:
        warn("assets/audio missing; the 3dsx has no music or effects")
    return failures


def find_sts_dir(sdmc: Path) -> Path:
    for candidate in (sdmc / "3ds" / "sts3ds", sdmc / "sts3ds", sdmc):
        if (candidate / "log.txt").is_file():
            return candidate
    return sdmc / "3ds" / "sts3ds"


def check_sdmc(sdmc: Path) -> int:
    sts = find_sts_dir(sdmc)
    print(f"== SD card: {sts} ==")
    log = sts / "log.txt"
    crash = sts / "crash.txt"
    if not log.is_file():
        bad(f"log.txt: missing ({log})")
        return 1

    text = log.read_text(encoding="utf-8", errors="replace")
    lines = text.splitlines()
    fails = [line for line in lines if "[FAIL]" in line]
    warns = [line for line in lines if "[WARN]" in line]

    print(f"log lines: {len(lines)}  fail: {len(fails)}  warn: {len(warns)}")
    for line in fails:
        print(f"  FAIL {line}")
    for line in warns:
        print(f"  WARN {line}")

    print("-- log tail --")
    for line in lines[-20:]:
        print(f"  {line}")

    crashed = False
    if crash.is_file():
        crash_text = crash.read_text(encoding="utf-8", errors="replace")
        crashed = "*** crash ***" in crash_text
        if crashed:
            print("-- crash.txt --")
            for line in crash_text.splitlines()[-40:]:
                print(f"  {line}")
        elif crash_text.strip():
            ok("crash.txt present but no crash recorded")

    n = len(fails) + (1 if crashed else 0)
    if n:
        bad(f"{n} blocking problem(s) on the SD card")
        return 1
    ok("no blocking problems on the SD card")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description="STS 3DS package / SD card checks")
    parser.add_argument("--sdmc", type=Path, default=None,
                        help="SD card root (or the 3ds/sts3ds directory)")
    args = parser.parse_args()

    failures = check_package()
    if args.sdmc is not None:
        failures += check_sdmc(args.sdmc)
    if failures:
        print(f"\n{failures} blocking problem(s)")
        return 1
    print("\nall package checks passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
