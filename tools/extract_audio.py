#!/usr/bin/env python3
"""Bake the three act themes into raw PCM for the in-game music streamer.

libctru has no Vorbis decoder, so the 3DS cannot play the game's OGG files
directly.  This tool pulls the original tracks out of desktop-1.0.jar and
converts them on the host with ffmpeg to the streamer's format:

    act1.pcm  <- audio/music/STS_Level1_NewMix_v1.ogg
    act2.pcm  <- audio/music/STS_Level2_NewMix_v1.ogg
    act3.pcm  <- audio/music/STS_Level3_v2.ogg

The output is 22050 Hz mono PCM16 little-endian and contains no header; the
game reads it in one-third-second buffers (port/source/audio.c).

    python tools/extract_audio.py
    python tools/extract_audio.py --dest "D:/3ds/sts3ds/audio"

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


def find_ffmpeg() -> str:
    exe = os.environ.get("FFMPEG") or shutil.which("ffmpeg")
    if not exe:
        raise SystemExit("ffmpeg not found; install it or set FFMPEG to its path")
    return exe


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--jar", default=DEFAULT_JAR)
    ap.add_argument("--out-dir", default=os.path.join(HERE, "assets/audio"))
    ap.add_argument("--dest", default=None,
                    help="also copy the .pcm files here (SD card folder)")
    args = ap.parse_args()

    ffmpeg = find_ffmpeg()
    os.makedirs(args.out_dir, exist_ok=True)
    if args.dest:
        os.makedirs(args.dest, exist_ok=True)

    with zipfile.ZipFile(args.jar) as zf:
        for name, member in TRACKS:
            if member not in zf.namelist():
                raise SystemExit(f"{member} is missing from {args.jar}")
            ogg = zf.read(member)
            tmp = tempfile.NamedTemporaryFile(suffix=".ogg", delete=False)
            try:
                tmp.write(ogg)
                tmp.close()
                dst = os.path.join(args.out_dir, name + ".pcm")
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

            seconds = os.path.getsize(dst) / (RATE * 2)
            print(f"{name}: {member} -> {dst} "
                  f"({seconds:.1f}s, {os.path.getsize(dst)} bytes)")
            if args.dest:
                copied = shutil.copyfile(dst, os.path.join(args.dest, name + ".pcm"))
                print(f"    copied to {copied}")

    print("\nCopy assets/audio/*.pcm to sdmc:/3ds/sts3ds/audio/ on the SD card.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
