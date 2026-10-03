# Snapshot interchange (M0, 3D)

Soft writes this text. The C viewport only parses it and rasters a perspective
frame. C never invents `OBS` lines. A frame without `END` is dropped; the last
accepted snapshot stays on screen. That is not a new chunk.

Axes: **+X** forward, **+Y** up, **+Z** lateral. Numbers are world units Soft
measured. The camera convention (behind the player, looking +X) is viewport
code, not a level recipe.

```
SNAP v1
SCALAR gravity=1 jump_v=3 slide_h=1
PLAYER x=8 y=0 z=0 vx=1 vy=0 vz=0 state=0
OBS n=2
2 16 0 -1 2 2 2 0
0 28 0 -2 3 1 4 0
END
```

| Field | Meaning |
|-------|---------|
| `kind` | `0` gap, `1` beam, `2` block, `3` pad, `4` coin. Soft emits the int. C maps it to a raster glyph only. |
| `x y z` | World position of the box minimum corner. |
| `w h d` | Extents along X, Y, Z. |
| `flags` | Soft-owned. `1` on a coin means already collected. |
| `state` | `0` run, `1` air, `2` slide, `3` dead. |

Scalars `gravity`, `jump_v`, `slide_h` come from Soft strategies each tick.

Several `SNAP v1` … `END` blocks may share a stream. C rasters the last complete block.
