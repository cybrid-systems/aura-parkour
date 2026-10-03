# Soft world

`parkour/world.aura` is the live 3D world (+X run, +Y up, +Z lane).

- `gravity`, `gap`, and `chunk-grammar` are workspace strategies. Seeded once with `set-code` + `eval-current` + `hot-strategy:register!` (sandbox off). After that the session uses `hot-strategy:swap!` / `heal!` only.
- `hot-strategy` has one active name. The smoke registers `gravity` before swapping it.
- Each tick Soft advances the player with strategy scalars, applies death and score, and emits a volume snapshot (`kind x y z w h d flags`) plus a perspective depth preview. Those numbers are measured. The product picture is the C raster, not the preview.
- Kinds: `0` gap, `1` beam, `2` block, `3` pad, `4` coin.

`parkour/m0_smoke.aura` loads the world. `PARKOUR_M0=1` runs the oneshot during that load. See `docs/snapshot.md`.
