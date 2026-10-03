# aura-parkour — 设计（Soft 原生）

三维无尽跑酷（走廊 / 竞技场）。**活世界就是 Soft AST**。C 只是三维视口：每 tick 采样 Soft 吐出的体素 SoA（位置、尺寸、种类），用透视相机做软件光栅（或以后的最小 GL）、固定 dt 积分和 POD 碰撞。没有侧视 80×24 卷轴这个产品。

本文对齐 Aura 标准库 tip `4c4b89bd0af44d9c615dd1e139b025a2d6a6d5a0`（下文写 `4c4b89b`）的 `lib/std` 与已落地 `query:*`。只使用那些名字。不新造 API，不新造计数器名。

## 一句话

玩家脚下的重力、缝隙、幽灵策略、下一段 chunk 语法，都是同一个 Aura 进程里的 workspace 定义。对局中途可以把这些定义换掉、治好、并行推演、按存活选优，而不重启进程、不重编 C。C/Rust/Python 写不出这个闭环当作游戏本身。

## 为什么这不是「任何语言都能写的跑酷」

旧设计是：C 拿着关卡、碰撞和关卡语法，Soft 偶尔填一张 blueprint POD。那个产品用 Python 字典、Rust ECS 或纯 C 数组就能复刻。那不是 aura-parkour。

本产品的游戏本身是下面这条**同进程闭环**，它只存在 Aura 引擎里：

1. 活世界是 workspace 里的 Soft 形式（障碍、chunk、玩家策略都是定义），不是 C 作者写的关卡语法，也不是外部脚本填表。
2. 中途 `hot-strategy:swap!` 换 gravity / gap / ghost 策略，失败 `hot-strategy:heal!`（优先 `ast:restore`，否则重绑 last-good body）。进程不退出。
3. 换完之后必须读已落地的 `(engine:metrics "query:incremental-relower-stats")`：走 partial（`adaptive-partial-decision-total`）而不是这一次 swap 的 full rebuild（`adaptive-full-decision-total`）。否则当成丢帧，`hot-strategy:heal!`。
4. 同一进程里用 `fiber:spawn` / `fiber:join` 推演 N 个未来世界；caller 上 join，不在 worker 里 join（与 `std/swarm`、rule-lantern Soft-green 同一约束）。这不是 C 线程池跑 N 份相同物理。
5. 坏 chunk 不能泄进活世界：`MutationBoundaryGuard`（`mutate:boundary-depth` / `mutate:boundary-safe?`）、`mutate:quota-ok?`、`mutate:atomic-batch-safe`、`mutate:rolled-back?`。
6. 对手幽灵是 `agent:spawn` 的 Soft agent，信箱是文档化的 `send` / `recv` / `mailbox-count`，编排是 `orch:*`。不是 C 里的 if-else AI。
7. 用户外观宏展开带卫生（gensym 不捕获用户绑定）；坏宏用 `query:where` 的 syntax-marker `MacroIntroduced` 找出来再回滚。

用 C、Rust 或 Python 重写「跳跃、碰撞、热更一个策略对象、开线程」可以得到另一款游戏。它主不了上面这条闭环，因为闭环的证据是 Aura 编译器的 AST、fiber 调度器和 incremental relower 计数，不是一张可以搬走的 POD 表。把这些计数在别的语言里仿写一遍，等于重写 Aura，不是在写跑酷。

## 北极星

从第一天起，可玩的东西就是 Soft 世界。C 是视口，不是游戏。

允许有一个**可丢掉的 spike**：用来确认三维视口能把一张 Soft 快照光栅出来。它不是产品里程碑，不合入主干，不得先写一套 C 关卡语法或侧视跑酷再「接上 Soft」。没有「M0 先纯 C」这个产品阶段，也没有「先做 2D 字符卷轴」这个产品阶段。

## 玩家感觉

第一人称 ANSI 真彩走廊（Temple Run / 地铁跑酷式追逐相机）：向前跑（+X），向上跳（+Y），左右是车道（Z）。相机锁在玩家身后略高、看向 +X；跳跃抬高视点，滑行压低，换道带动整条走廊。默认帧缓冲大约 100×36。障碍体积只来自 Soft 快照；地面、天花板和车道线是视口透视，不是关卡生成。真 GL 以后可以换，几何仍只来自 Soft。

操作：

- `Space` / `w` — 跳
- `s` / `↓` — 滑（压低碰撞高度，钻低梁）
- `a` / `d` — 沿 Z 短促换道
- `p` 暂停，`q` 退出

障碍种类（都是 Soft 形式生成的三维快照记录，不是 C 里的 switch）：地面缝隙、低梁（滑）、实体块（跳或绕）、移动垫。金币只是快照里的种类标记，计分规则在 Soft。Soft 冒烟可以打一张深度/线框预览，证明没有 C 也能看见体积；产品画面是三维视口。

玩家应该感觉到的 Aura 特性，而不是说明书：

- 脚下的重力或缝隙在不死、不重开的情况下被改写。
- 一次坏改写会弹回上一个好世界（`hot-strategy:heal!`），而不是崩溃或卡死。
- 屏上有几条半透明的幽灵轨迹，那是 Soft fiber 选出的未来，不是 C 里预先写死的循环。
- 跑得越久，下一段路的语法在同一局里变得更贴你的生存，因为适应度来自你刚才的计数。

## C 还做什么（薄）

C11 视口只许做四件事：

| 做 | 不做 |
|----|------|
| 透视相机、软件光栅（或以后最小 GL）、深度 | 关卡语法、障碍配方、难度曲线、侧视卷轴 |
| 每帧读一次原始输入 | 决定跳多高、缝多宽、盒子放哪、幽灵怎么跳 |
| 对 Soft 本 tick 快照里的三维 POD 做固定 dt 积分 | 在 C 里保存「真实重力常数」并禁止 Soft 改它 |
| 只对已有 AABB 做碰撞（热路径不分配） | 用线程池模拟候选世界；用 C 脚本演幽灵 |

快照是 Soft 填好、C 只读的 SoA 记录。字段只是视口采样格式，不是关卡语法：`kind, x, y, z, w, h, d, flags`，加上本 tick 的标量（重力、跳速、滑行高度）。这些数字每 tick 由 Soft 形式写出。C 把它们当成本帧的体积去投影和积分，不当成自己的规则表。C 不把缺帧补成新的走廊。

Soft 漏一帧时，C **继续积分上一张已接受的快照**，不得自己生成新 chunk。新快照提交之后，玩家脚下的几何就是新世界。

跨边界只用已落地的 `std/ffi`：`c-load` / `c-func` / `c-alloc` / `c-free` / `c-struct-size` / `c-struct-set!` / `c-struct-ref` / `c-opaque`。这是快照拷贝，不是第二套世界模型。C 不解释 Soft 源码。

热路径约束：预热之后 tick 内不 malloc；障碍环是 2 的幂容量；输入每帧一次；碰撞只看 POD。

目标文件（视口，不是游戏）：

```
c/
  main.c        启动三维视口，读 Soft 快照
  term.c        把光栅帧写到 stdout（TTY 时清屏）
  clock.c       单调时钟，固定 dt 累加器
  sample.c      把本 tick 三维 SoA 拷进 POD（只读）
  step.c        对已有 AABB 积分 + 碰撞，不生成体积
  render.c      透视投影 + 深度，快照 → 帧
```

没有 `world.c` 里的关卡生成器。没有 C 侧的难度状态机。没有侧视 80×24 产品。

## 活世界就是 Soft AST

障碍、chunk、玩家策略（gravity / gap / ghost）都是 workspace 里的具名定义。语法不写在 C 里，也不写成「C 能解释的关卡 DSL」。

会话工具：

- `std/workspace`：`ws:snapshot-current`、`ws:list-snapshots`、`ws:rollback-latest`、`ws:diff`、`ws:merge-symbols`、`ws:current-stats`、`ws:memory-pressure`。
- `std/hot-strategy` 管有名策略。文档写明它走 `mutate:rebind` + `ast:snapshot`，不走 AOT 插件。
- 持久化用 `persist:save` / `persist:load` / `persist:round-trip?`。`hot-strategy:version` 是进程内计数，**跨进程不保留**（`docs/stdlib/hot-strategy.md`）。要留到下一次打开的是 persist 的会话，不是 version 计数。

一帧（逻辑顺序，不是 C 主循环里插一次 Soft）：

```
Soft 形式（chunk / 障碍 / 策略）仍是活 AST
    → Soft 写出本 tick SoA 快照（ffi 拷贝）
    → C 读输入，按快照里的标量做固定 dt 积分，只碰 POD
    → C 透视光栅（含 Soft 选中的幽灵体积，叠层不碰撞）
    → C 把存活/得分/碰撞计数回灌给 Soft（原始整数，不是新计数器名）
    → Soft 在同一 workspace 里继续推演、换策、治愈
```

C 计数只是回灌：本局帧数、碰撞次数、死亡、得分。它们进 Soft 的适应度，不进新的 `engine:metrics` 键。

## 中途换策：脚下改写，不重启

重力、缝隙、幽灵策略是三个已 `hot-strategy:register!` 的具名策略。换的时候：

```aura
(hot-strategy:snapshot! "gravity")
(hot-strategy:swap! "gravity" "(lambda (ctx) ...)" "heavier")
```

`swap!` 的三参与类型文件一致：`hot-strategy:swap!: Any Any Any -> Any`（名、body、summary）。注册是 `hot-strategy:register!`。读最后好状态用 `hot-strategy:last-good-body` / `hot-strategy:last-good-snap` / `hot-strategy:active-name`。

Soft 门没过（配方不有有限长、没有安全落脚点、`mutate:quota-ok?` 为假、`mutate:boundary-safe?` 为假，或 relower 预算失败）时，**不重启**，调 `hot-strategy:heal!`。文档语义：优先 `ast:restore` 最后好快照，否则用 last-good body 再 `mutate:rebind`。玩家感觉是世界闪回上一个能站的形状，而不是进程退出。

多个名一起换时用 `mutate:atomic-batch-safe`（`std/hot-strategy` 文档：并发 fiber 换多个名要走这个）。批次被拒绝就当门失败，走 `heal!`，不要半套重力配半套缝隙。

`std/hot-update` 是另一条路，文档写明它**故意面向 AOT**：`hot-update:reload` / `hot-update:set-region!` / `hot-update:get-region` 用于原生插件区域隔离。**不要用它换纯 Aura 的 gravity/gap/ghost 策略。** 策略换核只走 `std/hot-strategy`。区域掩码只在真的有原生插件要隔离时才动 `set-region!`。

`std/heal` 的 `heal` 会 `set-code` 做 AST 手术。那是沙箱外修源码的工具。**对局里的门失败只调 `hot-strategy:heal!`，不调 `std/heal`。**

## 同进程 RSI

跑酷内核（chunk 语法 + 旋钮）在你玩的时候演化，不是夜里离线跑一次。

搜索模块只搜索，不自己写进活世界（`docs/stdlib/swarm.md`、`docs/stdlib/pso.md` 的约束：调用方拥有 apply）：

| 模块 | 本设计怎么用 | 不该么用 |
|------|------------|--------|
| `std/swarm` | 群体搜 chunk 语法个体。`parallel` 为真时，在 caller 上 `fiber:spawn` 再 `fiber:join` | 不要在 worker 里 `fiber:join` |
| `std/ant` | `pheromone:init` / `pheromone:update` / `pheromone:rank` / `pheromone:score` / `pheromone:export` / `colony:search`。按突变类型留信息素，不是图上 ACO | 不把信息素表当成 C 数组 |
| `std/pso` | 搜旋钮。文档里的结果是 `(pso:best)`，由调用方写进策略 | 模块自己不 `swap!` |
| `std/abc` | 已落地的搜索模块。本设计不新造它的函数名；apply 仍走下面这一步 | 不把 abc 当物理引擎 |

适应度是 Soft 函数：读 C 回灌的存活与得分（原始整数）。选中的个体**在本局**由调用方执行：

```aura
(hot-strategy:snapshot! "chunk-grammar")
(hot-strategy:swap! "chunk-grammar" best-body "rsi")
```

然后过 Soft 门和下面的 relower 预算。门失败就 `hot-strategy:heal!`，这一代丢弃，对局继续。

`std/adaptive` 不是难度控制器。它落地的是诊断与 PID 分析：`pid:analyze`、`structured-diagnosis`、`measure-distance`。RSI 可以把残差串交给 `pid:analyze` 当诊断，再决定要不要 `swap!`。不要假装它有一个游戏专用的「difficulty 函数」。

`std/synthesize` 落地的是 `synthesize:list-templates` / `synthesize:list-help`。候选语法可以从已有模板列出，再经过 Soft 门才允许 `swap!`。不新造 synthesize 管道。

可选：障碍策略种群的多样性用 `std/boids`。模块头写明它是协调，**不是物理模拟**。`boids:step` 吃 `sep` / `align` / `cohere` / `radius` / `bounds` / `max-speed`，返回新的位置与速度向量。这些向量是策略种群的坐标，不是 C 里的刚体。C 只画 Soft 写进快照的障碍记录。不当唯一优化器（`boids:help` 同此意）。

## Fiber 平行候选世界

每次要看「下一段路」时，Soft 自己模拟 N 个未来，不是 C 开 N 个线程跑同一份物理。

约束与 rule-lantern 的 Soft-green 以及 `std/swarm` 相同：

- 在 **caller** 上对每个候选 `(fiber:spawn (lambda () ...))`。
- 仍在 **caller** 上 `(fiber:join fid)`。
- 禁止在 worker 里 `fiber:join`。
- 长变异前后在安全边界交出：`mutate:safe-yield`，且 `mutate:boundary-depth` 为 0 才算可交出（`std/mutate` 注释；`std/orchestrator` 的 `orch:parallel` 同步这一点）。

每个 fiber 看的是候选形式的世界，不是活世界。选中的那一个才经过门，再写进活世界。C 只把 Soft 选中的最佳未来画成幽灵叠层（快照里的另一组 POD）。叠层不参加碰撞。

## 类型化变异与所有权

坏 chunk 不得泄进活世界。这不是 C 里的「if 校验失败就不 memcpy」。

活世界是一份被持有的 workspace。候选世界在 fiber 里，不持有活定义。要提交，必须进入 `MutationBoundaryGuard`：

- 进入前 `mutate:boundary-safe?`，并看 `mutate:boundary-depth`。
- 配额 `mutate:quota-ok?`。不够就不开始这一代。
- 多名提交用 `mutate:atomic-batch-safe`。持有 Guard 时拒绝批次（`std/agent` 注释）。
- 失败看 `mutate:rolled-back?` / `mutate:rollback-rate`，并 `ws:rollback-latest` 或 `hot-strategy:heal!`。
- 安全快照：`mutate:safety-snapshot`、`hot-strategy:snapshot!`、`ws:snapshot-current`。

线性故事就是这一句：候选不别名活世界；提交要么整批成功，要么整批回滚。没有「坏块已经写进环形缓冲但策略还是旧的」这种中间态给玩家看。

### Soft / Off 契约

- **生产面：Restricted + Strict。** 玩家与用户外观脚本没有 `set-code`。
- **`set-code` 只在沙箱外开发。** 用来种下第一个 workspace（`std/hot-strategy` 文档和 `std/heal` 都是这样种子）。种子一旦进入对局，活循环只用 `mutate:rebind` 与 `hot-strategy:swap!` / `heal!`。
- 生产对局不调 `std/heal`（它会 `set-code`）。

## 增量 relower 是玩法延迟预算

换策之后，视口不允许卡在一次全量重建上。预算用**已经落地**的查询，不新造键。

读法（与 `tests/compiler/test_workload_adaptive_relower.cpp` 一致）：

```aura
(engine:metrics "query:incremental-relower-stats")
```

再 `hash-ref` 这些已有键：

| 键 | 预算含义 |
|----|----------|
| `schema-2127` | 表面还在（期望 2127） |
| `workload-adaptive-relower-wired` | 自适应 relower 已接上 |
| `adaptive-partial-decision-total` | 这一次 swap 应该让它增长 |
| `adaptive-full-decision-total` | 这一次单策略 swap 不应该走它。若它为这次 swap 增长，算丢预算 |

同胞姐表面 `query:incremental-relower-policy-stats` 的已有键 `effective-partial-relower-threshold` 只用来读当前阈值，不当成新仪表。

脏位怎么读，遵循 `docs/stdlib/hot-strategy.md`（不要读错时机）：

- `eval-current` **之前**：`stats:get` 的 `compile:dirty-count`、`compile:block-dirty-count`（可带名）、`compile:epoch`。
- `eval-current` **之后**脏位常常归零。改读寿命计数：`query:jit-stats-hash` 里的 `hotswap-invalidate-total`、`invalidate-function-calls`、`mutation-epoch`。
- 不要求 `hot-swap:fn`（那是 AOT 路），也不要求 dirty 在 `eval-current` 之后还粘着。

预算规则：视口的固定 dt 不为全量 relower 让路。若这一次 swap 让 `adaptive-full-decision-total` 增长，或 partial 决策没有增长且寿命失效计数也没动，则 `hot-strategy:heal!`，继续画上一张好快照。不把全量重建塞进玩家的这一帧。

## 多智能体：幽灵是 Soft agent

对手幽灵不是 C 里的 AI 脚本。它们是 Soft agent：

- 生命周期：`agent:spawn`、`agent:ask`、`agent:status`、`agent:stop`、`agent:restart`、`agent:list`（`lib/std/orchestrator.aura-type`）。
- 闭环一次：`agent:closed-loop-once`、`agent:decide`、`agent:decision-metrics`、`agent:loop-stats`（`lib/std/agent.aura-type`）。
- 信箱：`std/adaptive` 的 API 卡把 `std/agent` 写成 `my-id`、`session-active?`、`mailbox-count`、`send`、`recv`、`recv-timeout`。幽灵之间用这些通信，不共享 C 全局变量。
- 编排：`orch:define-role`、`orch:parallel`、`orch:parallel-with-yield`、`orch:conduct`、`orch:pipeline`、`orch:retry`。

每个幽灵读到的世界是 Soft 形式，决策写回自己的策略名。C 只 blit 它们发布进快照的位置。幽灵不改玩家的碰撞结果。

## 卫生与用户外观

玩家可以带一段 Soft 外观（颜色、HUD 点缀、死亡动画）。它是宏，展开必须卫生：gensym 出来的绑定不捕获玩家或活世界的名字。

展开出来的节点带 syntax-marker `MacroIntroduced`。查法已在 `lib/std/query-workspace.aura`：

```aura
(query:filter
  (query:where :syntax-marker "MacroIntroduced")
  (query:where :node-type "Define"))
```

坏外观：用这个查询找出宏引入的节点，再 `ws:rollback-latest` 或 `hot-strategy:heal!`。不让宏节点留在活世界里。

可观测只读已有表面：`query:pattern-index-stats`、`query:pattern-ir-hygiene-closed-loop-stats`（`lib/std/stats.aura` 对这两个问题的登记）。生产外观仍在 Restricted 面，没有 `set-code`。

## 可观测：只用已落地的 query

不新增 `engine:metrics` 键。对局 HUD 与门只读这些：

| 表面 | 用途 |
|------|------|
| `query:incremental-relower-stats` | partial 对 full；玩法延迟预算 |
| `query:incremental-relower-policy-stats` | 已有的 `effective-partial-relower-threshold` |
| `query:jit-stats` / `query:jit-stats-hash` | `hotswap-invalidate-total`、`invalidate-function-calls`、`mutation-epoch` |
| `stats:get` 的 `compile:dirty-count`、`compile:block-dirty-count`、`compile:epoch` | 仅在 `eval-current` 之前读脏位 |
| `query:typed-mutation-stats` | 类型化变异 |
| `query:mutate-invalidate-stats` | 变异失效 |
| `query:fiber-boundary-violation-stats` | fiber 越界 |
| `query:multi-fiber-orchestration-stats` | 多 fiber 编排 |
| `query:work-steal-stats` | 窃取与 boundary 深度 |
| `query:pattern-index-stats` | 模式 / `MacroIntroduced` |
| `query:pattern-ir-hygiene-closed-loop-stats` | 卫生闭环 |

读法是 `(engine:metrics "query:...")` 再 `hash-ref`，与编译器测试相同。C 的帧数、碰撞、得分不进这张表，它们只回灌给 Soft 适应度。

## 表面对照（tip `4c4b89b`）

| 需要 | 真实名字 |
|------|----------|
| 换策 / 治愈 / 注册 / 快照 | `hot-strategy:swap!` `hot-strategy:heal!` `hot-strategy:register!` `hot-strategy:snapshot!` |
| AOT 区域（不是策略换核） | `hot-update:reload` `hot-update:set-region!` |
| 边界 / 配额 / 回滚 / 批次 | `mutate:boundary-depth` `mutate:boundary-safe?` `mutate:quota-ok?` `mutate:rolled-back?` `mutate:rollback-rate` `mutate:atomic-batch-safe` `mutate:safe-yield` `mutate:safety-snapshot`；引擎原语 `mutate:rebind` |
| workspace | `ws:snapshot-current` `ws:rollback-latest` `ws:diff` `ws:list-snapshots` `ws:merge-symbols` `ws:current-stats` `ws:memory-pressure` |
| fiber | `fiber:spawn` `fiber:join`（caller 上 join） |
| 搜索 | `std/swarm` `std/ant` `std/pso` `std/abc`；apply 在调用方 |
| 合成 + 门 | `synthesize:list-templates` `synthesize:list-help`，提交前过门 |
| 沙箱 | 生产 Restricted + Strict；无 `set-code` |
| relower | `engine:metrics` + `query:incremental-relower-stats` |
| agent / orch | `agent:spawn` `agent:ask` `agent:closed-loop-once`；`orch:parallel` `orch:conduct`；信箱 `send` `recv` `mailbox-count` |
| 持久 / 修复 / 诊断 | `persist:save` `persist:load`；对局治愈是 `hot-strategy:heal!`（不是 `std/heal`）；`pid:analyze` `structured-diagnosis` |
| 鸟群（可选） | `boids:step` 等；协调，不是物理 |
| 拷贝快照 | `std/ffi` 的 `c-struct-set!` / `c-struct-ref` / `c-func` |

## 里程碑

顺序就是产品顺序：先 Soft 世界活着，C 是视口。

1. **M0 — Soft 三维世界先活，C 只光栅。** workspace 吐出三维快照（位置、尺寸、种类、标量）。极薄 C 视口做透视深度，不写关卡。任何纯 C 跑酷或侧视字符卷轴都不得留成产品。
2. **M1 — 活三维世界。** 障碍体积、chunk、玩家策略都是 Soft 定义。C 每 tick 只采样 SoA 并投影。没有 C 关卡语法。能跑、能跳、能死、能记分，分数规则在 Soft。
3. **M2 — 脚下改写。** 对局中途 `hot-strategy:swap!` gravity/gap/ghost，不重启。门失败 `hot-strategy:heal!`。玩家看得到世界变形或弹回。
4. **M3 — 同局 RSI。** `swarm` / `ant` / `pso` / `abc` 的适应度读 C 回灌的存活与得分。局内 `swap!` 选优。失败则 `heal!`。
5. **M4 — 候选未来。** `fiber:spawn` / `fiber:join` 模拟 N 个未来。C 只画 Soft 选中的幽灵叠层。
6. **M5 — 所有权。** `MutationBoundary`、配额、`mutate:atomic-batch-safe`、回滚。生产面 Restricted + Strict。`set-code` 只留在沙箱外。
7. **M6 — 延迟预算。** 换策后读 `query:incremental-relower-stats`。不是 partial 就 `heal!`。视口不为全量重建掉帧。
8. **M7 — 对手。** 幽灵是 `agent:spawn` + 信箱 + `orch:*`。C 不写 AI。
9. **M8 — 外观。** 用户 Soft 宏，gensym 卫生，`MacroIntroduced` 查询后回滚。

`std/boids` 鸟群障碍可以在 M3 之后任何时候加，不阻塞主线。

## 非目标

- 网络多人。
- 在 C、Rust 或 Python 里重写一套「看起来像」的游戏当成产品。
- 夜间离线进化当成主循环。主循环是同局 RSI。
- 用 `hot-update:reload` 冒充纯 Aura 策略换核。
- 对局里调 `set-code` 或 `std/heal`。
- 新造 `engine:metrics` 键或假的 Soft 函数名。
- 主机用 Python 写产品逻辑。
- 侧视 2D 卷轴或 80×24 字符跑酷当成产品。
- 在 C 里造网格、关卡或「看起来像三维」的假走廊。视口只投影 Soft 已经写出的体积。

## 仓库布局（目标）

```
aura-parkour/
  DESIGN.md
  README.md
  soft/          游戏。parkour/*.aura
  c/             视口。C11 + CMake
  docs/
  scripts/       Soft 冒烟（主机镜像 ghcr.io/cybrid-systems/dev 已有的 aura 二进制）
```

构建时：先让 Soft workspace 能在没有「好玩的 C 物理」的情况下吐出快照。C 只连上这个快照。不要先做一个可玩的 C 游戏再找地方插 Soft。

## 默认拍板

| 题 | 默认 |
|----|------|
| 空间 | +X 向前跑，+Y 向上，Z 车道。相机在玩家身后略高，透视 |
| 进程 | 一个进程。Soft workspace 与 C 视口同住。不要每段路拉起子进程 |
| 画面 | 软件光栅深度帧。GL 以后只换投影，不换世界 |
| 种子 | 沙箱外 `set-code` 一次；对局内只 `swap!` / `heal!` |
| 存档 | `persist:save` / `persist:load`；不要把 `hot-strategy:version` 当存档 |

## Review delta

相对旧的「C 热路径 + Soft 外挂」设计（那一版可以用 Python/C/Rust 原样做出来），本文改了这些：

1. **北极星。** 从「C 拥有热路径，Soft 拥有活大脑」改为「活世界就是 Soft AST，C 只是视口」。
2. **杀掉 M0 产品化。** 旧文第一个里程碑是「C-only runner，没有 Soft」。现在 M0 只能是可丢弃 spike。里程碑改为先 Soft 世界活，再接视口。
3. **关卡语法不再在 C。** 旧文让 C 持有 `world.c` 的 chunk SoA 并从 Soft blueprint 追加障碍。现在障碍、chunk、策略都是 Soft 形式；C 不作者语法。
4. **物理常数不再锁在 C ABI。** 旧文规定重力、跳速、滑翔高度是版本化 C 表，Soft 只能调导出的旋钮。现在这些策略本身是 Soft 定义，每 tick 把数字写进快照，C 只积分快照。
5. **中途换策是产品，不是后期装饰。** `hot-strategy:swap!` 在对局中发生，失败 `hot-strategy:heal!`，不重启。旧文把换策放在块边界偶尔调一次，而且物理公式仍属于 C。
6. **RSI 改为同局。** 旧文 M4 是「夜间 swarm，C ABI 保持稳定」。现在适应度是对局中 C 计数回灌，局内 select-best 并 `swap!`。
7. **Fiber 不再是离线打分。** 旧文写「Soft fiber 在跑之间离线算候选，C 单线程显示」。现在 Soft 用 `fiber:spawn`/`fiber:join` 模拟 N 个未来，C 只画选中的叠层。明确不是 C 线程池。
8. **幽灵不再是 C 积分的策略。** 旧文：Soft 出策略，C 在同一 SoA 里模拟幽灵。现在幽灵是 `agent:spawn` + 信箱 + `orch:*`。
9. **补上所有权。** `MutationBoundaryGuard`、配额、`mutate:atomic-batch-safe`、回滚。旧文没有这一层，坏配方可以从逻辑上泄进 C 环形缓冲。
10. **Soft/Off。** 生产面 Restricted + Strict；`set-code` 与 `std/heal` 只在沙箱外。旧文只写了「没有 set-code 的 Restricted 外观」，没把它当成整个对局的面。
11. **relower 成为玩法预算。** 必须看到 partial（`adaptive-partial-decision-total`）而不是这一次 swap 的 full（`adaptive-full-decision-total`），否则 `heal!`。旧文只说「保持健康」。不新造计数器名。脏位读法按 hot-strategy 文档：`eval-current` 之后不要求 dirty 仍粘着。
12. **卫生。** 用户外观宏用 gensym；回滚依赖已有的 `MacroIntroduced` 查询（`query:where` / `query:filter`）以及 `query:pattern-ir-hygiene-closed-loop-stats`。旧文没有。
13. **写明主语言不能宿主这个闭环。** C/Rust/Python 可以做另一款跑酷，不能把 mutate + heal + fiber + incremental relower 当成游戏本身。
14. **API 诚实。** `hot-update` 不再和策略换核混用；`std/boids` 按模块头标成协调而非物理；`std/heal` 不进对局；`std/adaptive` 不假装成难度 API。
15. **三维，不是侧视。** 产品是透视走廊里的体积。快照字段是 `kind, x, y, z, w, h, d, flags` 加 Soft 标量。C 只做相机和深度。侧视 80×24 不是里程碑。
