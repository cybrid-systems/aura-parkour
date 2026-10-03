# aura-parkour

终端跑酷。**活世界是 Soft AST**（对齐 Aura 标准库 tip `4c4b89b` 的 `lib/std`）。C 只每 tick 采样 SoA 快照并 blit。这不是一个外挂 Soft 的 C 游戏：同进程的 `hot-strategy:swap!` / `heal!`、fiber 候选世界、增量 relower 闭环就是游戏本身。C/Rust/Python 宿主不了这条闭环。

设计见 [DESIGN.md](DESIGN.md)。

仓库：https://github.com/cybrid-systems/aura-parkour

状态：设计。产品从 Soft 世界活着开始。没有「先做 C-only 再贴 Soft」这个产品里程碑。
