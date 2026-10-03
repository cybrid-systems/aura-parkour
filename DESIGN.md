# aura-parkour — Design

Terminal endless / sectioned parkour: **C owns the hot path**, **Soft/Aura owns the living game brain**.

## North star

A playable TUI runner that feels tight at 60–120 Hz, while levels, difficulty, ghosts, and cosmetics evolve in-session through Soft (mutate → eval → select-best), not by rewriting C between runs.

## Split of responsibility

| Layer | Tech | Owns |
|-------|------|------|
| Frame loop, input, render, collision, camera | **C11** | Fixed timestep, ANSI/UTF-8 framebuffer, AABB/capsule hits, scroll, audio beeps optional |
| World rules, chunk grammar, score, difficulty | **Soft `.aura`** | Live AST: obstacle recipes, spawn density, timing windows |
| Ghosts / rivals | Soft + C | Soft policy (`hot-strategy`); C integrates motion each tick |
| Observability | Soft `engine:metrics` + thin C counters | Soft queries for denseness/relower; C exposes `frames`, `hits`, `dt_p99` |
| Self-evolution | Soft swarm / fiber / mutate:rebind | Evolve chunk generators under Soft gates (no illegal physics rewrite) |
| Host | thin shell | `parkour` binary loads Soft via embed or `aura` serve; session, assets, CLI |

**Rule:** product intelligence lands in Soft (`parkour/*.aura`). C stays a thin, boring, fast machine. Host Python (if any) is harness only.

## Player fantasy (v0)

Side-view terminal runner (left→right or rightward scroll). Cell grid ~80×24 default, scales with terminal.

Controls (v0):

- `Space` / `w` — jump
- `s` / `↓` — slide / duck
- `a`/`d` — short air / lane nudge (optional lane mode later)
- `p` — pause; `q` — quit

Obstacles: ground gaps, low beams (slide), high blocks (jump), moving pads. Coins / gates for score multipliers.

Death → Soft-scored run summary → optional Soft propose next chunk set.

## C architecture

```
src/
  main.c           // argv, Soft bootstrap, game loop
  term.c / term.h  // raw mode, resize, double buffer, damage rects
  clock.c          // monotonic ns, fixed dt accumulator
  world.c          // SoA chunks: x, kind, w, h, flags
  player.c         // pos, vel, state machine (run/jump/slide/dead)
  collide.c        // sweep tests, no alloc in hot path
  render.c         // project world → cells, HUD
  soft_bridge.c    // FFI: push frame events; pull rule tables / chunk blueprints
```

Hot-path invariants:

- No malloc in tick after warmup
- SoA obstacle rings (power-of-two capacity)
- Input polled once per frame; physics at fixed `dt` (e.g. 1/120), render interpolates
- Soft calls **amortized**: not every frame — e.g. every N ms or on chunk boundary

## Soft / Aura surfaces to lean on

1. **Chunk grammar as live AST** — `spawn-chunk`, `obstacle-recipe` as Soft lambdas; `hot-strategy:swap!` / `heal!` when a recipe fails Soft gates (must still produce finite solid/gap segments).
2. **Difficulty controller** — Soft reads C-fed residuals (`miss_jump`, `slide_late`, `idle_ms`) via thin query surface; Soft retunes density / gap length without touching C physics constants illegally.
3. **Ghost race** — Soft policy decides jump/slide; C simulates ghosts in the same SoA world (or Soft fiber parallel sims offline between runs).
4. **Fiber** — Soft parallel fitness of candidate chunk sets; C remains single-threaded for the live display (optional later: C thread for audio only).
5. **Restricted path** — user-authored Soft cosmetics / HUD scripts without `set-code`; production face stays sandbox-friendly.
6. **Incremental Soft** — after swap, Soft `query:incremental-relower-stats` must stay healthy; evolve loop Soft-gates like rule-lantern.

## Soft gate (must)

After any Soft mutation of world rules:

- Physics constants used by C (gravity, jump_v, slide_h) remain behind a **versioned C ABI table**; Soft may only retune **named knobs** Soft exports, not rewrite collide formulas in C.
- Chunk recipes must Soft-prove: finite length, at least one safe path under default jump/slide envelopes (Soft simulator, same numbers as C table).
- Illegal Soft rewrites → Soft `heal!` + stamp `GATE_REJECT`.

## Data flow (one frame)

```
input → C player step → C collide → C scroll
     → (chunk edge?) Soft: next-blueprint / difficulty tick
     → C append SoA obstacles from Soft blueprint POD
     → C render → term
     → Soft observe (sampled)
```

Blueprint POD (C ↔ Soft): packed ints/floats Soft fills into a ring Soft owns or Soft writes into a C-mapped buffer Soft-agreed layout (`kind,x,w,h,flags`).

## MVP milestones

1. **M0** — C-only runner: jump/slide, gaps/beams, death, score (no Soft).
2. **M1** — Soft loads; Soft supplies next chunk blueprint; Soft observe HUD line.
3. **M2** — Soft difficulty + Soft gate + heal; Soft-measured scores in repo log.
4. **M3** — Soft ghost + Soft fiber candidate chunks; Soft hot-strategy cosmetics.
5. **M4** — Soft self-evolve nightlies (swarm) under Soft gates; keep C ABI stable.

## Non-goals (v0)

- Network multiplayer
- Full color sprite engines (ASCII/block cells first)
- Growing host Python product logic
- Soft rewriting C collide every frame

## Repo layout (target)

```
aura-parkour/
  DESIGN.md          // this file
  README.md
  c/                 // C11 sources + CMake
  soft/              // Soft parkour/*.aura
  docs/
  scripts/           // docker Soft smoke (v1.0.9)
```

## Build / Soft recipe (planned)

- C: CMake, `-O2`, link Soft embed **or** spawn Soft serve (decide at M1; prefer embed if ABI ready, else oneshot Soft for chunk gen between runs then tighten).
- Soft binary: `/workspace/aura-grok/build/aura` in `ghcr.io/cybrid-systems/dev:v1.0.9` for CI Soft smoke.

## Open choices (default picked)

| Topic | Default |
|-------|---------|
| Scroll | Autoscroll right, player stays ~1/3 screen |
| Embed vs Soft subprocess | Soft subprocess for M1; embed later |
| Color | ANSI 16 + bold; truecolor optional |
| Seed | Soft session seed stamped in Soft run log |
