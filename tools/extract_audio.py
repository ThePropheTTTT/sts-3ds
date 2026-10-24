#!/usr/bin/env python3
"""Bake the music and sound effects into raw PCM for the 3DS audio engine.

libctru has no Vorbis decoder, so the 3DS cannot play the game's OGG files
directly.  This tool pulls the original audio out of desktop-1.0.jar and
converts it on the host with ffmpeg to 22050 Hz mono PCM16 little-endian.

Music (the default; streamed in one-third-second buffers by audio.c):

    act1.pcm  <- audio/music/STS_Level1_NewMix_v1.ogg
    act2.pcm  <- audio/music/STS_Level2_NewMix_v1.ogg
    act3.pcm  <- audio/music/STS_Level3_v2.ogg

Sound effects (--sfx; one whole clip per file, played on NDSP channels 1..23):

    sfx/<name>.pcm <- audio/sound/<original clip>

    python tools/extract_audio.py
    python tools/extract_audio.py --dest "D:/3ds/sts3ds/audio"
    python tools/extract_audio.py --sfx --dest "D:/3ds/sts3ds/audio"

ffmpeg must be on PATH (or set FFMPEG to its executable); the jar defaults to
desktop-1.0.jar and can be overridden with --jar or STS_JAR.
"""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import tempfile
import zipfile

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_JAR = os.environ.get("STS_JAR", "desktop-1.0.jar")
RATE = 22050

# (output name, member inside the jar)
TRACKS = [
    ("act1", "audio/music/STS_Level1_NewMix_v1.ogg"),
    ("act2", "audio/music/STS_Level2_NewMix_v1.ogg"),
    ("act3", "audio/music/STS_Level3_v2.ogg"),
]

# The combat/UI sounds the port actually uses.  Names match the AudioSfx enum
# in port/source/audio.h (the enum is sorted the same way, NONE excluded).
# SoundMaster.java is the canonical mapping; these are the clips it asks for.
SFX = [
    ("ui_click", "audio/sound/SOTE_SFX_UIClick_1_v2.wav"),
    ("card_select", "audio/sound/SOTE_SFX_CardSelect_v2.ogg"),
    ("card_play", "audio/sound/SOTE_SFX_UI_Parchment_3_v1.ogg"),
    ("card_reject", "audio/sound/SOTE_SFX_CardReject_v1.ogg"),
    ("attack_fast", "audio/sound/SOTE_SFX_FastAtk_v2.ogg"),
    ("attack_heavy", "audio/sound/SOTE_SFX_HeavyAtk_v2.ogg"),
    ("hit", "audio/sound/SOTE_SFX_FastBlunt_v2.ogg"),
    ("block", "audio/sound/SOTE_SFX_GainDefense_v2.ogg"),
    ("buff", "audio/sound/SOTE_SFX_Buff_1_v1.ogg"),
    ("debuff", "audio/sound/SOTE_SFX_Debuff_1_v1.ogg"),
    ("end_turn", "audio/sound/SOTE_SFX_EndTurn_v2.ogg"),
    ("battle_start", "audio/sound/STS_SFX_BattleStart_1_v1.ogg"),
    ("victory", "audio/sound/SOTE_SFX_Victory_v1.ogg"),
    ("defeat", "audio/sound/STS_DeathStinger_v4_SFX.ogg"),
    ("exhaust", "audio/sound/SOTE_SFX_ExhaustCard.ogg"),
    ("heal", "audio/sound/SOTE_SFX_HealShort_1_v2.ogg"),
    ("gold", "audio/sound/SOTE_SFX_Gold_RR1_v3.ogg"),
]


def find_ffmpeg() -> str:
    exe = os.environ.get("FFMPEG") or shutil.which("ffmpeg")
    if not exe:
        raise SystemExit("ffmpeg not found; install it or set FFMPEG to its path")
    return exe


def convert(ffmpeg: str, zf: zipfile.ZipFile, member: str, dst: str) -> None:
    data = zf.read(member)
    suffix = os.path.splitext(member)[1] or ".bin"
    tmp = tempfile.NamedTemporaryFile(suffix=suffix, delete=False)
    try:
        tmp.write(data)
        tmp.close()
        subprocess.run(
            [ffmpeg, "-y", "-loglevel", "error", "-i", tmp.name,
             "-vn", "-ac", "1", "-ar", str(RATE),
             "-acodec", "pcm_s16le", "-f", "s16le", dst],
            check=True,
        )
    finally:
        try:
            os.unlink(tmp.name)
        except OSError:
            pass


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--jar", default=DEFAULT_JAR)
    ap.add_argument("--out-dir", default=None,
                    help="output directory (default: assets/audio, or "
                         "assets/audio/sfx with --sfx)")
    ap.add_argument("--sfx", action="store_true",
                    help="bake the combat/UI sound effects instead of the act themes")
    ap.add_argument("--dest", default=None,
                    help="also copy the .pcm files here (SD card folder)")
    args = ap.parse_args()

    ffmpeg = find_ffmpeg()
    if args.sfx:
        out_dir = args.out_dir or os.path.join(HERE, "assets/audio/sfx")
        items = SFX
    else:
        out_dir = args.out_dir or os.path.join(HERE, "assets/audio")
        items = TRACKS
    dest_dir = os.path.join(args.dest, "sfx") if (args.dest and args.sfx) else args.dest

    os.makedirs(out_dir, exist_ok=True)
    if dest_dir:
        os.makedirs(dest_dir, exist_ok=True)

    with zipfile.ZipFile(args.jar) as zf:
        names = set(zf.namelist())
        for name, member in items:
            if member not in names:
                raise SystemExit(f"{member} is missing from {args.jar}")
            dst = os.path.join(out_dir, name + ".pcm")
            convert(ffmpeg, zf, member, dst)

            seconds = os.path.getsize(dst) / (RATE * 2)
            print(f"{name}: {member} -> {dst} "
                  f"({seconds:.2f}s, {os.path.getsize(dst)} bytes)")
            if dest_dir:
                copied = shutil.copyfile(dst, os.path.join(dest_dir, name + ".pcm"))
                print(f"    copied to {copied}")

    if args.sfx:
        print("\nCopy assets/audio/sfx/*.pcm to sdmc:/3ds/sts3ds/audio/sfx/ on the SD card.")
    else:
        print("\nCopy assets/audio/*.pcm to sdmc:/3ds/sts3ds/audio/ on the SD card.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
