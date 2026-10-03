# Soft world

`parkour/world.aura` is the live world.

- `gravity`, `gap`, and `chunk-grammar` are workspace strategies. They are seeded once with `set-code` + `eval-current` + `hot-strategy:register!` (sandbox off). After that the session uses `hot-strategy:swap!` / `heal!` only.
- `hot-strategy` has one active name. The smoke registers `gravity` before swapping it, matching rule-lantern.
- Each tick Soft scrolls, integrates the player with strategy scalars, applies death and score, and can emit a snapshot plus an ASCII frame. Those numbers are measured, not invented in C.
- Kinds: `0` gap, `1` beam, `2` block, `3` pad, `4` coin.

`parkour/m0_smoke.aura` loads the world. With `PARKOUR_M0=1` the world runs the oneshot smoke (load replaces the caller workspace, so the smoke procedure lives in `world.aura` and runs during that load). See `docs/snapshot.md` and `scripts/smoke_soft.sh`.
