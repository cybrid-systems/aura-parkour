# aura-parkour

三维跑酷走廊。**活世界是 Soft AST**（对齐 Aura 标准库 tip `4c4b89b`）。C 只是透视视口：采样 Soft 写出的体积快照，做软件光栅。这不是侧视字符卷轴，也不是外挂 Soft 的 C 游戏。同进程的 `hot-strategy:swap!` / `heal!`、fiber 候选世界、增量 relower 闭环才是游戏本身。C/Rust/Python 宿主不了这条闭环。

设计见 [DESIGN.md](DESIGN.md)。快照见 [docs/snapshot.md](docs/snapshot.md)。

仓库：https://github.com/cybrid-systems/aura-parkour

状态：**M0**。Soft 播种 `gravity` / `gap` / `chunk-grammar`，沿 +X 跑几拍，`swap!` 再 `heal!`，写出三维快照（`x,y,z` 与尺寸）。C 程序 `parkour_blit` 只投影这张快照。没有 C 关卡语法，没有纯 C 可玩产品，没有 80×24 侧视产品。Soft 漏帧时 C 留着上一张完整快照，不自己造走廊。

## Soft 冒烟

镜像 `ghcr.io/cybrid-systems/dev:v1.0.9`，二进制只用 `/workspace/aura-grok/build/aura`（不要 `build_soft4132`）。播种需要 `AURA_SANDBOX=off`。`PARKOUR_M0=1`。

```bash
bash scripts/smoke_soft.sh
```

退出码 0，stdout 含 `PARKOUR_M0_DONE`，以及 Soft 实测的 `PARKOUR_M0_TICK` / `PARKOUR_M0_SWAP` / `PARKOUR_M0_HEAL` / `PARKOUR_M0_SNAP`。末尾有一张 Soft 自己投影的深度预览（`PARKOUR_M0_DEPTH_*`）。快照在 `build/m0.snap`。

## 三维 blit

```bash
cmake -S c -B build/c
cmake --build build/c
./build/c/parkour_blit build/m0.snap
```

stdout 是透视深度帧（`SOFT3D`），不是侧视图。无文件参数则读 stdin。`--integrate` 只让玩家穿过已有 AABB，不增加盒子。一键：`bash scripts/smoke_blit.sh`。
