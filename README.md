# aura-parkour

终端跑酷。**活世界是 Soft AST**（对齐 Aura 标准库 tip `4c4b89b` 的 `lib/std`）。C 只采样 Soft 吐出的 SoA 快照并 blit。这不是一个外挂 Soft 的 C 游戏：同进程的 `hot-strategy:swap!` / `heal!`、以及设计里的 fiber / 增量 relower 闭环才是游戏本身。C/Rust/Python 宿主不了这条闭环。

设计见 [DESIGN.md](DESIGN.md)。快照格式见 [docs/snapshot.md](docs/snapshot.md)。

仓库：https://github.com/cybrid-systems/aura-parkour

状态：**M0**。Soft 世界会播种 `gravity` / `gap` / `chunk-grammar`，走几拍，`hot-strategy:swap!` 再 `heal!`，并写出一张快照。C 只有视口 `parkour_blit`：解析 Soft 写的文本并画一帧。没有 C 关卡语法，没有纯 C 可玩产品。Soft 漏一帧时 C 保留上一张完整快照，不自己造 chunk。

## Soft 冒烟

主机镜像 `ghcr.io/cybrid-systems/dev:v1.0.9`，二进制只用 `/workspace/aura-grok/build/aura`（不要用 `build_soft4132`）。`set-code` 播种需要 `AURA_SANDBOX=off`。`PARKOUR_M0=1` 让 `m0_smoke.aura` 在 `load` 世界时跑完冒烟。

```bash
bash scripts/smoke_soft.sh
```

成功时进程退出码 0，stdout 有 `PARKOUR_M0_DONE`，以及 Soft 实测的 `PARKOUR_M0_TICK` / `PARKOUR_M0_SWAP` / `PARKOUR_M0_HEAL` / `PARKOUR_M0_SNAP`。分数和标量只来自 Soft，不在脚本里编造。快照写到 `build/m0.snap`。

## 薄 C blit

```bash
cmake -S c -B build/c
cmake --build build/c
./build/c/parkour_blit build/m0.snap
```

没有文件参数时从 stdin 读。`--integrate` 只对已有 POD 用快照里的标量走一步，仍然不生成障碍。一键：`bash scripts/smoke_blit.sh`（先 Soft 再 blit）。

M0 不进入 raw mode，除非以后显式调用；非 TTY 时只把一帧写到 stdout。
