# Soft world

`parkour/world.aura` is the live 3D world (+X run, +Y up, +Z lane).

- `gravity`, `gap`, and `chunk-grammar` are workspace strategies. Seeded once with `set-code` + `eval-current` + `hot-strategy:register!` (sandbox off). After that the session uses `hot-strategy:swap!` / `heal!` only.
- Soft owns integrate / coin score / lethal death. Play mode reads player INPUT instead of the smoke auto-intent.
- Each tick Soft emits a volume snapshot (`kind x y z w h d flags`) plus Soft `SCORE`. The product picture is the C raster.

`parkour/play.aura` is the interactive lockstep loop (`INPUT` on stdin → `SNAP` on stdout).

`parkour/m0_smoke.aura` loads the world. `PARKOUR_M0=1` runs the oneshot during that load (CI). See `docs/snapshot.md`.

`play.aura` writes one `SNAP v1` … `END` before `read-line`. `objects.aura` is the DeepSeek proposal gate: the host script `scripts/propose_objects.py` only writes a lambda file; Soft `hot-strategy:swap!`s it or refuses it. Proposals must not carry a score.

