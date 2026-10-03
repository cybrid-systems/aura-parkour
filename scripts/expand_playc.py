#!/usr/bin/env python3
import base64,zlib,pathlib,sys
root=pathlib.Path(__file__).resolve().parents[1]
z=(root/"c"/"play.c.z64").read_text().strip()
(root/"c"/"play.c").write_bytes(zlib.decompress(base64.b64decode(z)))
print("expanded c/play.c", len(zlib.decompress(base64.b64decode(z))))
