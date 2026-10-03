#!/usr/bin/env python3
"""Ensure c/play.c exists. Prefer committed play.c; z64 is optional fallback."""
import base64, zlib, pathlib, sys

root = pathlib.Path(__file__).resolve().parents[1]
play = root / "c" / "play.c"
z64 = root / "c" / "play.c.z64"

if play.is_file() and play.stat().st_size > 0:
    print(f"using c/play.c ({play.stat().st_size} bytes)")
    sys.exit(0)

if not z64.is_file():
    print("error: missing c/play.c and c/play.c.z64", file=sys.stderr)
    sys.exit(1)

try:
    data = zlib.decompress(base64.b64decode(z64.read_text().strip()))
except Exception as e:
    print(f"error: c/play.c.z64 corrupt ({e}); commit c/play.c instead", file=sys.stderr)
    sys.exit(1)

play.write_bytes(data)
print(f"expanded c/play.c ({len(data)} bytes)")
