#!/usr/bin/env python3
"""Tiny resumable HTTPS downloader that honours the *_proxy env vars.

pwsh's Invoke-WebRequest and curl.exe (schannel) both fail in this sandbox with
SEC_E_NO_CREDENTIALS; Python's OpenSSL stack does not, so this is the way we get
things off the network here.

    python tools/dl.py URL DEST
"""

from __future__ import annotations

import os
import sys
import time
import urllib.request

UA = "sts-for-3ds-setup/1.0"


def human(n):
    for unit in ("B", "KB", "MB", "GB"):
        if n < 1024 or unit == "GB":
            return f"{n:.1f} {unit}"
        n /= 1024.0


def main(argv):
    if len(argv) != 3:
        print(__doc__.strip())
        return 2
    url, dest = argv[1], argv[2]
    os.makedirs(os.path.dirname(os.path.abspath(dest)) or ".", exist_ok=True)

    have = os.path.getsize(dest) if os.path.exists(dest) else 0
    req = urllib.request.Request(url, headers={"User-Agent": UA})
    if have:
        req.add_header("Range", f"bytes={have}-")

    t0 = time.time()
    try:
        resp = urllib.request.urlopen(req, timeout=60)
    except urllib.error.HTTPError as e:
        if e.code == 416:  # already complete
            print(f"[dl] {dest} already complete ({human(have)})")
            return 0
        raise
    total = int(resp.headers.get("Content-Length") or 0) + (have if getattr(resp, "status", 200) == 206 else 0)
    mode = "ab" if getattr(resp, "status", 200) == 206 else "wb"
    if mode == "wb":
        have = 0
    print(f"[dl] {url} -> {dest} ({human(total) if total else 'size unknown'})")

    done = have
    with open(dest, mode) as f:
        while True:
            chunk = resp.read(1 << 20)
            if not chunk:
                break
            f.write(chunk)
            done += len(chunk)
            if total:
                pct = done * 100.0 / total
                sys.stdout.write(f"\r[dl] {pct:5.1f}%  {human(done)} / {human(total)}")
            else:
                sys.stdout.write(f"\r[dl] {human(done)}")
            sys.stdout.flush()
    dt = max(1e-9, time.time() - t0)
    print(f"\n[dl] done: {human(done)} in {dt:.1f}s ({human(done / dt)}/s)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
