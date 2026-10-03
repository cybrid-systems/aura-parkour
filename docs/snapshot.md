# Snapshot interchange (M0)

Soft writes this text. The C viewport only parses and blits it.
C never invents `OBS` lines. If a frame is truncated (no `END`), C keeps the
last accepted snapshot and does not generate a chunk.

Coordinates are **screen cells** after Soft's camera. `PLAYER x` is the column
Soft already chose (about 1/3 of the width). `OBS` `x` is `world_x - scroll`.
Scalars `gravity`, `jump_v`, and `slide_h` are whatever the live Soft
strategies returned this tick (integers are fine).

```
SNAP v1
SCALAR gravity=1 jump_v=3 slide_h=1
PLAYER x=8 y=0 vx=0 vy=0 state=0
OBS n=3
0 20 4 1 0
1 28 6 2 0
2 36 2 2 0
END
```

| Field | Meaning |
|-------|---------|
| `kind` | `0` gap, `1` beam, `2` block, `3` pad, `4` coin. Soft forms emit these ints. C may map int to a glyph for blit only. |
| `x w h` | Screen cell, width, height. |
| `flags` | Soft-owned. `1` on a coin means already collected. |
| `state` | `0` run, `1` air, `2` slide, `3` dead. |

Several `SNAP v1` ... `END` blocks may sit in one stream. C blits the last complete block. A bad trailing block does not erase the previous one.
