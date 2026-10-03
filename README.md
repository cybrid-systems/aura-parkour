# aura-parkour

三维跑酷走廊。**活世界是 Soft AST**（对齐 Aura 标准库 tip `4c4b89b`）。C 只是透视视口：采样 Soft 写出的体积快照，做软件光栅。这不是侧视字符卷轴，也不是外挂 Soft 的 C 游戏。

设计见 [DESIGN.md](DESIGN.md)。快照见 [docs/snapshot.md](docs/snapshot.md)。

仓库：https://github.com/cybrid-systems/aura-parkour

## Play

```bash
bash scripts/play.sh
```

One command. Soft owns gravity / gap / chunk / score / death; C blits the Soft SoA snapshot and feeds keys. Controls: `Space`/`w` jump, `s` slide, `a`/`d` lane, `p` pause, `r` restart, `q` quit.

Requires Docker image `ghcr.io/cybrid-systems/dev:v1.0.9` and Soft binary `/workspace/aura-grok/build/aura` (tip `4c4b89b`). Host builds the thin C viewport with cmake (`scripts/play.sh` expands `c/play.c` from `c/play.c.z64` first).

`scripts/soft_play.sh` skips the image entrypoint. That entrypoint `chown -R`s `/home/dev` before Aura starts (tens of seconds; longer on a cold disk) and the viewport gives up after 120s with `Soft did not produce an initial SNAP`. Soft still emits `SNAP v1` … `END` on stdout **before** it reads the first `INPUT`. Logs (`PARKOUR_PLAY_READY`) go to stderr so the pipe stays SNAP-only.

## DeepSeek objects (optional)

DeepSeek V4.1 Flash (`deepseek-flash`, same id as aura-typeplay) may **propose** the next chunk grammar: one Soft lambda string. Soft gates the string (no `set!`, no score, no host calls), `hot-strategy:swap!`s `chunk-grammar`, probes `(chunk-grammar origin)`, and `heal!`s if the live chunk is not a list of obstacle rows. C never generates chunks. Score stays Soft-owned; the model is not asked for a score and a proposal that mentions score is dropped.

```bash
export DEEPSEEK_API_KEY_FILE=~/code/keys/deepseek
export DEEPSEEK_MODEL=deepseek-flash   # default
export DEEPSEEK_BASE_URL=https://api.deepseek.com
bash scripts/play.sh
```

`soft_play.sh` also checks `~/code/keys/deepseek`, `/home/dev/code/keys/deepseek`, and `~/.config/aura-build/deepseek_api_key`, and mounts the file into the container as `DEEPSEEK_API_KEY_FILE`. Without a key, play uses the seeded grammar and does not call the network. The first `INPUT` is when a proposal is applied, after the initial SNAP is already on the pipe.

状态：**M1 playable**。软件光栅终端视角（约 64×20）。不是 GL。

## Soft smoke (CI)

镜像 `ghcr.io/cybrid-systems/dev:v1.0.9`，二进制只用 `/workspace/aura-grok/build/aura`（不要 `build_soft4132`）。播种需要 `AURA_SANDBOX=off`。`PARKOUR_M0=1`。

```bash
bash scripts/smoke_soft.sh
```

退出码 0，stdout 含 `PARKOUR_M0_DONE`。快照在 `build/m0.snap`。这是冒烟，不是玩法入口。

## 三维 blit（单帧）

```bash
cmake -S c -B build/c && cmake --build build/c
./build/c/parkour_blit build/m0.snap
```

或 `bash scripts/smoke_blit.sh`。
