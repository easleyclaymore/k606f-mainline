# 第 4 步：显示接管（进行中）

日期：2026-10-28 起　　状态：**真机失败 → 排查中，缺口已收敛到"桥收到 DSI 但报视频长度不匹配"（B44）**

> **本文是追加式记录**：新结论不断追加在中间，早期章节保留原文（失败记录不删）。
> 被推翻的章节会在标题上标「⚠ 已被推翻」。
>
> **⚠ 先读这里**：当前最新结论在 **B77/B78 章**（搜「关键反转」）与 **B74 彩条里程碑**。
> 一句话现状：eDP 输出侧已验证通过（桥内置彩条能出图）；DPU INTF 有像素流（MISR 证实）；
> **真正缺口 = DSI 只发 1080 半宽，而 panel 是 2160**（B78 反证，见该章）。
> 更早的「B28–B31 / B44」章节是历史，别当当前状态。
>
> 最后更新：2026-11-01 14:20（B78）

### B28 的确定性定位（2026-10-31 15:50）

`mode_valid=1` 修好后，`SETCRTC` 从 B27 的 EINVAL 变成**成功**（`crtc=91`、`2160x1350`），
`dsi_err_worker` 从 B26 的"5 秒 19 万次洪流"变成**全程停在 10 不再增长**。三件事同时成立：

1. **DSI 稳定、桥在接收**（错误计数停住）；
2. **桥寄存器 = 工作态那组**（`0a:00 10:0a 11:82 20:a4 21:01 23:88`）；
3. **但 `0x96 = 0x00`** —— `0x96` 是 TPS1/链路状态寄存器，工作态应为 `0x01`（训练完成）。

→ 缺口**确定性地收敛到 eDP 链路训练**：DSI 段通了、桥配置对了，唯独 `0x96` 停在 0x00，
链路没训练完。而训练是独立于 DSI 的一段（DP AUX + 面板出复位 + 参考时钟），
不是"调 DSI 参数"能解决的。这比之前所有猜测都更精确。

### B29/B30 的时钟根因定位（2026-10-31 16:00–16:21）

**B29**：补上了厂商 `dsi86_init[]` 里 0x0d 之后的 DP TX PHY 配置（`0x64/0x74/0x75/0x76/0x77/0x78`
+ `0xEE` 延时 10ms）。表从 52 条 → **59 条**（`eDP init table written (59 registers)`）。
结果：训练**仍失败**（0x0A 卡 0x02、0x96 卡 0x00）。→ 那 6 个 PHY 寄存器不是根因。

**B30**：抓到**真正的抄错**——参考时钟编号错了。

| | 时钟编号 | 时钟名 | 备注 |
|---|---|---|---|
| 厂商 FDT | `<rpmhclk 4>` = `RPMH_LN_BB_CLK3` | `"bb_clk2"`（名字，非编号） | `clk-rpmh.c`：index 4 = `ln_bb_clk3`（"lnbclka3"） |
| 我们 dts（旧） | `<rpmhcc RPMH_LN_BB_CLK2>` = index **2** | `"refclk"` | `ln_bb_clk2`（"lnbclka2"）——**错了** |

修：dts 改成 `RPMH_LN_BB_CLK3`；驱动 `devm_clk_get_optional` → `devm_clk_get`（拿不到就报错，
不再静默跳过），并打印 `refclk rate`。B30 确认 `refclk rate = 19200000 Hz`（时钟现在对了）。

**但训练仍失败**：0x0A 卡 0x02、0x96 卡 0x00。结论收敛到**最后一道边界**：

- DSI 段：通（`dsi_err_worker` 停住）；
- 桥配置：对（寄存器全图 0 差异 + 工作态那组 + refclk 19.2 MHz 在跑）；
- **DP PLL 锁不上（0x0A ≠ 0x82）** —— 而这需要 **AUX 通道与面板真实握手**。

DP PLL 锁相的条件：参考时钟（已确认在跑）+ 面板侧 AUX 应答（面板必须在供电+出复位状态）。
我们已经排除了寄存器、时钟编号、DSI、GPIO 极性、电压、时序顺序。剩下唯一没被正面验证的
就是"面板 AUX 是否真的在应答"——这需要面板驱动真正完成 prepare/reset 序列（把 gpio75 那根
reset 线驱动到位），而面板驱动的 reset 逻辑目前还是"同 elish"抄来的、未按本机原厂证据核对。

### B31 + status=5 的精确含义（2026-10-31 16:28–16:40）⚠ 已被 B44 推翻

**两个关键纠正**：

1. **"dsi_err_worker 停 10"是假象**：`status=5` 一直在刷，只是被内核 rate-limit
   （`dsi_err_worker: 189339 callbacks suppressed`），`grep -c` 只能数到有限打印行。
   B31 日志里 9.18s 一批、37.14s（SETCRTC 时）又一批 → DSI 一直在错。
2. **DPMS 不是元凶**：B31 里 `SETCRTC 成功` 且 `connector DPMS = 0（on）`，DRM 层是 on 的；
   `fb0/blank=4` 只是 fbdev 模拟层的残留变量，不影响 DRM 的 CRTC 状态。

**status=5 的位含义**（`dsi_host.c`）：
`DSI_ERR_STATE_TIMEOUT(0x1) | DSI_ERR_STATE_FIFO(0x4)` —— **没有** `DLN0_PHY(0x2)`、
**没有** `PLL_UNLOCKED(0x20)`。即：DSI 控制器在发、FIFO 堆积、传输超时 = **桥没在收 DSI**，
且 lane0 物理层正常、DSI PLL 已锁。

**结合 B7 的对照**：不加载 msm 时，ABL 的 DSI 流能被桥转发并显示 Tux（用户现场确认）。
→ **桥 + 面板 + eDP 链路物理可用；问题 100% 在 msm 的 DSI 侧与桥的期望不匹配。**

**一个新发现的可疑点（DSI 拓扑旁路）**：dts 里
- `dsi0_out → edp_mux_in`（经桥）、`edp_mux_out → panel_in_0`
- **`dsi1_out → panel_in_1`（直接连面板，旁路了桥！）**

即 DRM 认为 dsi1 直连面板（无桥），只有 dsi0 过桥。物理上 dsi0/dsi1 都进桥。
mainline 的 `drm_bridge` 是单输入模型，无法表达"桥有两个 DSI 输入"，于是 dsi1 被建模成旁路。
这可能是桥收不到正确 dual-DSI 流的根因（dsi1 的时序/配置与 dsi0 不一致）。

### 方向1 结论：bonded 双 DSI 的水平时序切分错误（2026-10-31 16:40）

顺着「status=5 = 桥没收 DSI」往下查 msm 的 bonded 双 DSI 时序，找到一个**具体、可验证的 bug**。

**msm 的切分逻辑**（`dsi_host.c: dsi_timing_setup`）：

```c
if (is_bonded_dsi) {
    h_total /= 2;  hs_end /= 2;  ha_start /= 2;  ha_end /= 2;  hdisplay /= 2;
}
```

即：DRM mode 的**整个水平时序（含 hfp/hpw/hbp）都除以 2**，得到每条 DSI 的 per-link 值。

**厂商 FDT 的 per-link 真值**（`qcom,mdss_dsi_dual_sim_video` 的 timing@0，framerate 60）：

| 参数 | 厂商 per-link |
|---|---|
| width | 1080（= 2160 的一半） |
| h-front-porch | 16 |
| h-back-porch | 12 |
| h-pulse-width | 16 |
| → htotal | **1124** |

**我们原来的 mode**：hdisplay=2160，hfp=16 / hpw=16 / hbp=12（把 per-link 的 porch 值直接当成了 full 值）。
经 msm /2 后得到 per-link：hfp=8 / hpw=8 / hbp=6，htotal=**1102** —— **与厂商的 16/16/12、1124 不符**。

**对照主线上已知可用的双 DSI 面板 `panel-novatek-nt36532`**：它的 mode 里
hfp=200 / hpw=4 / hbp=92（1800 宽的面板），正是**把 per-link 的 porch 翻倍**写在 DRM mode 里，
让 msm /2 后得到正确的 per-link 值。**我们的 mode 漏了这一步。**

**修复（已改 `panel-lenovo-k606f.c`）**：把水平 porch 翻倍——hfp 16→32、hpw 16→32、hbp 12→24，
htotal 2204→2248，clock 180772→184380（= 2248×1367×60/1000）。msm /2 后：
per-link htotal=1124、hfp=16、hpw=16、hbp=12，per-link pclk = 184380/2 = 92.19 MHz = 1124×1367×60，
与厂商完全一致。垂直时序不被切分，保持 8/1/8 不变。

**为什么这很可能就是 status=5 的原因**：桥（PSA8331/DSI86）的 DSI 接收器靠 hsync/hporch 给每行视频
定帧。hsync 脉宽只有 8（应为 16）、htotal 1102（应为 1124），桥无法正确锁帧，SoC 的 TX FIFO 持续
堆积 → TIMEOUT|FIFO（status=5）。这是今天**唯一还没拿厂商真值核对过的硬件输入**。

**验证手段（已加）**：`host_power_on` 日志新增 `timing id=N: TOTAL=… ACTIVE_H=… ACTIVE_HSYNC=… byte_clk=…`，
B32 可以直接读到实际写入的时序寄存器值，与厂商 1124/16/16/12 对表，不用再猜。

### B32–B37：时序对齐 + 完整 PHY 寄存器 diff，收敛到 DPU 数据流（2026-10-31 17:00–18:05）

**B32**：时序修复生效（`TOTAL=0x05560463`=htotal1124、`ACTIVE_HSYNC=0x00100000`=hpw16、byte_clk 69.14MHz），
但 status=5 仍在刷 → 时序不是根因。

**B33/B34**：加寄存器 dump，精确定位 status=5：
- `FIFO_STATUS=0xcccc1019`：**所有 4 lane 的 HS FIFO 同时 overflow + underflow** + VIDEO_MDP_FIFO overflow+underflow。
- `TIMEOUT_STATUS=0x01` = **HS_TX_TIMEOUT**（HS 发送超时）。
- CLK_STATUS bit16 PLL_UNLOCKED=0 → PLL 锁定；LANE_STATUS STOPSTATE 全 0 → lane active；
  STATUS0 VIDEO_MODE_ENGINE_BUSY=1 → video engine 在跑。

**B35**：写 `tools/devmem.c` 读 ABL 留下的 DSI 寄存器。时序值与 ABL 完全一致（htotal1124/hpw16），
再次确认时序正确。但 CTRL=0x20040000（ENABLE=0）→ dump 的是 ABL 退出后 reset 态，非工作态。

**B36**：insmod msm 前后各 dump 一次，做完整 PHY 寄存器 diff（96 word）：
- byte clock 分频（DIV_CTRL_3_0=1）一致；pixel clock 分频差异是**等价组合**（ABL: DSICLK_SEL=1+pix_div=3
  vs msm: SEL=0+div=6，结果都是 pll_bit/6）。
- LANE_CFG0/1=0x21/0x84 一致（lane swap 排除）。
- 唯一实质差异：LANE_CTRL1（ABL=0=non-continuous，msm=0x60=continuous）、clk_trail(5vs6)、clk_pre(18vs19)。

**B37**：加 `MIPI_DSI_CLOCK_NON_CONTINUOUS`，LANE_CTRL1 变 0（与 ABL 一致），**status=5 仍在** → continuous clock 排除。

**关键新发现（时间线）**：status=5 在 **SETCRTC（DPU commit 帧）之后 1.25s 才开始**
（host_power_on 36.86s → 首次 status=5 38.11s），而非 DSI host enable 后立即。→ **问题在 DPU 数据流开始之后**，
不是 DSI 控制器本身的 video 发送。

**已排除的完整清单（全部有寄存器级证据）**：时序、byte clock、桥寄存器、GPIO、电压、PHY quirks(V4_1)、
traffic mode、lane 数、continuous/non-continuous clock、PHY lane swap。

**剩余疑点（按可能性）**：
1. DPU byte interface 数据流与 DSI byte clock 的匹配（byte_intf_clk_div_2 语义）。
2. clk_trail/clk_pre 各差 1（PHY timing 微小差异，可能性低）。
3. bonded 双 DSI 的 DPU tile 数据流（SETCRTC 后才报错指向这里）。

### B39：根因级修复——"两颗芯片"写错地址（2026-10-31 20:55）

**这是 B 阶段最大的结构性问题，之前的 38 轮全踩在同一个坑上。**

厂商驱动 `dsi86-edp-mux.c` 里其实是**两颗独立的 I2C 芯片**，挂在同一条总线上：

| 芯片 | I²C 地址 | 作用 | 厂商写什么 |
|---|---|---|---|
| TI SN65DSI86 | `0x2C`（8bit `0x58`） | **真正的 DSI→eDP 桥**，DSI 接收器在这 | 59 条 `dsi86_init[]` 表 + TPS1(`0x96=0x02`) + 训练轮询(`0x0A`/`0x96`) |
| PS8331 mux | `0x33`（8bit `0x66`，DT 节点 `eDP-MUX@66`） | 只是端口选择 mux | 仅 4 条：`0x00=0`/`0x03=0x38`/`0x10=0x0A`/`0x11=0x02` |

**我们的 `psa8331.c` 一直把整张 59 条 DSI86 初始化表 + TPS1 写到了 0x33（mux）**，
真正的 DSI86(0x2C) 从未配置。这直接解释了：
- B23 的"寄存器 0 diff"是在 dump **0x33（mux）**，从头到尾比错了芯片；
- "BL 那 9 个值"也是 0x33 的寄存器，写进去南辕北辙；
- DSI86 的 DSI 接收器/DP PLL 从未被正确初始化 → `status=5`。

**修复（已上机验证）**：59 条表 + TPS1 改写到 0x2C，mux 的 4 条留在 0x33，用
`i2c_transfer()` 按地址分路（厂商是复用同一 client 换 slave 地址）。

**B39 结果**：`0x2C` 寄存器 `0x0A=0x82`（DP PLL 锁）、`0x96=0x01`（链路训练完成），
与 Android 工作态逐字节一致。**但 `status=5` 仍在刷**（10→22→34→46）→
**地址 bug 是一个独立问题，修好它 ≠ 修好 DSI 流**。

**下一步（DSI 流为何不被桥接收）**：DSI86 现在配置对了（eDP 侧训练完成），但它的
DSI 接收器仍不收 msm 的 DSI 流。唯一剩下的硬件差异是 PHY timing（clk_trail 5vs6、
clk_pre 18vs19，来自 B36 diff）；而 status=5 只在 SETCRTC（DPU 开始送像素）后才出现，
又指向 DPU→DSI 的 byte interface 数据流。两条线索都待查。

### B40/B41：PHY timing 修正后 status=5 仍在（负结果，2026-10-31 21:16–21:30）

**B40 无效**：提交内核后版本号从 `1137137d9` 变成 `178a0b3c6`，`out/kmod-probe/` 里模块
没同步 → `insmod: invalid module format`，msm/psa8331 都没加载。教训：**提交内核后必须
重新同步全部模块到 kmod-probe**（`find out/kernel-msmmod -name '*.ko'` → kmod-probe）。

**B41 负结果**：同步模块后重测。devmem 确认 `clk_trail=5`（0xae944c0=0x05）、`clk_pre=18`
（0xae944e4=0x12），与厂商完全一致（其余 12 个 timing 本来就对齐）。**但 status=5 仍在刷**
（58→142→154）。→ **PHY timing 不是根因**。

**至此 PHY 寄存器（96 word）与厂商逐字节对齐**，排除了：clk_trail/clk_pre、traffic mode、
byte/pixel clock、lane 数/swap、continuous/non-cont clock、桥寄存器、GPIO、电压、时序。
剩下的唯一微小差异是 PLL 的 FRAC_DIV 低位（0x04 vs 0xcc，VCO 差 0.01%，在容差内）。

**下一步（真正的缺口）**：status=5 在 SETCRTC（DPU 开始送像素）后 1.25s 才出现，
而 DSI 的同步/blanking 包正常发出 → **问题在 DPU→DSI 的 byte interface 数据流**，
不在 DSI 控制器/PHY/桥。待查：① DATABUS_WIDEN（VID_CFG0 bit25，ABL=0 vs msm=1，
字节总线 32 vs 64 位）与 byte_intf_clk_div_2 的匹配；② DPU bonded 双 DSI 的
pingpong/split 数据流。

### B42/B43：HS_TIMER_CTRL 消除 TIMEOUT + byte_intf_clk 排除（2026-10-31 21:45–22:00）

**关键新方法：把 DSI 控制器寄存器（不只是 PHY）也做 ABL vs msm 全量 diff。**
之前只 diff 了 PHY（96 word），这次 diff 了 DSI 控制器（0xae94000/0xae96000），
发现 mainline 从不设置的一个寄存器：**HS_TIMER_CTRL**。

| 寄存器 | ABL | msm | 说明 |
|---|---|---|---|
| `HS_TIMER_CTRL`(0x0b8) | `0x4ea60`（HS_TX_TO=60000、分辨率 4） | `0xffff`（复位默认，mainline 从不设置） | HS TX 超时定时器 |

**B42（正结果）**：在 `dsi_ctrl_enable` 里显式写 `HS_TIMER_CTRL=0x4ea60`（对齐 ABL），
`dsi_err` 从 **status=5（TIMEOUT|FIFO）变成 status=4（只有 FIFO）** → HS_TX_TIMEOUT 消除。
**但 FIFO overflow/underflow 仍在**（0xcccc1019，所有 lane 同时溢出+下溢）。

**B43（负结果）**：怀疑 FIFO 是 byte_intf_clk 速率不匹配（mainline 无条件除 2，对应
64 位接口，但无 DSC 面板 DATABUS_WIDEN=0 是 32 位），改成不除 2 后 FIFO **仍在** →
byte_intf_clk 不是根因，已回滚。

**当前认知**：HS_TIMER_CTRL 修复可能只是"把 HS 超时阈值从 ~1ms 放大到 ~14ms，
掩盖了 HS 传输卡住"，FIFO 错误说明桥仍没收 DSI 流。核心矛盾不变：桥收 ABL 的 DSI，
不收 msm 的 DSI，但寄存器（PHY + 控制器）已逐字节对齐。

**下一步**：查桥（SN65DSI86）的 DSI 接收器状态寄存器（如 0xE5 附近），确认桥的 DSI
接收到底有没有锁定；并查 DPU 侧是否有 underflow/overflow 计数（区分 DPU 侧还是 DSI 侧）。

### B44：桥其实收到了 DSI，但报视频长度不匹配（2026-10-31 22:06，重大转折）

读了 SN65DSI86 的 DSI 接收诊断寄存器（TI SLLA425/SLLA356 文档），**推翻了"桥没收 DSI"的长期假设**：

| 寄存器 | 值 | 含义 |
|---|---|---|
| `0x12`(CHA_DSI_CLK_RANGE) | `0x36` | **桥检测到了 DSI 时钟**（收到 DSI 时钟了） |
| `0xF0`(DSI PHY 错误) | `0x00` | **无 SOT/EOT/contention/escape 等 PHY 层错误**（PHY 通信干净） |
| `0xF1` bit5 | `0x20` | **CHA_INVALID_LENGTH_ERR**（DSI 视频数据长度不匹配） |
| `0xF6` bit6 | `0x40` | **LOSS_OF_DP_SYNC_LOCK_ERR**（DP 输出同步锁定丢失） |

结论：**桥确实收到了 DSI**（时钟 + PHY 层干净），但报"视频长度不匹配"和"DP 同步锁丢失"。
也就是说，之前的 `status=5/4`（HS_TX_TIMEOUT + FIFO 溢出）不是"桥没接"，而是**桥收到了
数据但对不上帧**——FIFO 溢出是数据堆积（桥收不下来/对不上），HS_TX_TIMEOUT 是连锁反应。

桥配置的是 **dual-DSI Left/Right 模式**（SN65DSI86 支持两个 DSI 输入）：
- `0x20-0x21` = CHA_ACTIVE_LINE_LENGTH = `0x0438` = 1080（左半）
- `0x22-0x23` = CHB_ACTIVE_LINE_LENGTH = `0x0438` = 1080（右半）
- 其余时序寄存器（0x2c hpw=16、0x30 vpw=1、0x34 hbp=12、0x36 vbp=8、0x38 hfp=16、0x3a vfp=8）
  与 msm 的 per-link 时序**逐项一致**。

**待查（下一步）**：既然桥收到 DSI、时序数值也对，为什么报 INVALID_LENGTH_ERR？
疑点：① mainline 把 dsi1 建模成"旁路桥直连面板"（drm_bridge 单输入模型的限制），
dsi1 的时序/数据流可能跟 dsi0 不完全一致，导致桥的 CHB 收不到正确数据；
② 桥可能被配置成单 DSI 模式（0x10=0x80 的 bit 含义待查），只收 CHA 不收 CHB。

> ⚠ B44 的两条待查在 B45/B46 都有了答案（见下）：疑点②被 Android 真值直接否定
> （Android 的 0x10 也是 0x80）；疑点①的"dsi1 建模"被探针证据否定（bonded 主从、
> 两链时序逐位一致、错误对称），真正的问题是 EOT + 链间竞态。

### B45：EOT 缺失是主因之一——去 NO_EOT_PACKET 后 CHA 干净（2026-10-31 23:02，重大进展）

**取证前提（本轮先修了一个比对污染）**：之前所有"Android 工作态 vs 我们"的桥寄存器对比，
Android 侧是在**灭屏**时抓的——灭屏态恰好等于厂商 `turn_off_dsi86_by_cmd()` 的产物
（0x5A=04/0x96=0/0x93=0/0x0D=0），会制造大量假差异。本轮在**亮屏**下用 adb+i2cdump8 重抓
DSI86@0x2C 全 256 字节（`private/logs/20261031-android-working-dsi86/dsi86-2c-page0-SCREENON.txt`），
并建立了**健康签名**：清除 0xF0-F7 后 5s 重读 = 全 0x00。

**亮屏真值 vs B42 失败态的 256 字节 diff** 只剩 8 个寄存器（全部定名）：
0x64/0x76=AUX 事务残留（benign）、0x95 TRAINING_SETTING（0xC0 vs 0x00，DP 侧次级）、
0x40（未定名，DP 侧次级）、sticky 错误 4 个、**0xC8 所在的控制器位 EOT_PACKET_CTRL：
ABL=1（TX_EOT_APPEND）vs msm=0**。另发现我们的 psa8331 表混入了厂商注释掉的
ASSR 变体表的 7 条（0x64/0x74-0x78/0xEE，实为一次 DPCD 写事务），B28"缺它训练不上"
的结论是在地址 bug 时代得出的，已作废。

**改动（单变量）**：panel-lenovo-k606f.c 去掉两个 DSI host 的
`MIPI_DSI_MODE_NO_EOT_PACKET`（内核树提交 `33840ee73`）→ mainline 写
`TX_EOT_APPEND=1`，与 ABL 一致。

**B45 结果（镜像 `dd7a4c93`）**：
- EOT 位闭环：两控制器 `EOT_PACKET_CTRL=0x00000001`（B42 是 0）。
- 清粘滞后：**0xF1(CHA INVALID_LENGTH)=0x00**、**0xF6(DP 同步锁)=0x00**，
  与 Android 健康签名一致；**但 0xF3(CHB)=0x20** 在 +3s 重锁。
- dsi_err 洪流在 29.1s 后静默 ≥22s（B42 持续到采样结束）。
- 疑点①（dsi1 建模）正式关闭：[K606F] 探针显示 bonded=1、tiles=2、两链时序逐位一致
  （htotal=1124=1080+44），FIFO 错误两链对称。

### B46：剩余问题是"单链随机错"——链间启动竞态特征（2026-10-31 23:20）

同内核（零系统变量），探针加 +30s/+60s 延迟重读（镜像 `1fe5d07e`）：

| 读点 | 0xF1 (CHA) | 0xF3 (CHB) | dsi_err |
|---|---|---|---|
| 清除后 +3s | **0x20** | 0x00 | 46→... |
| +33s | **0x20** | 0x00 | 130 |
| +93s | **0x20** | 0x00 | 274 |

与 B45 **镜像反转**（B45：CHA 干净、CHB +3s 锁过一次）。综合两轮：

1. **每次引导随机选一条链成为"坏链"，boot 内持续**（B45=DSI1/CHB，B46=DSI0/CHA）；
   FIFO 洪流是坏链专属（按秒分布与坏链一一对应），好链全程零错误。
2. f0/f2/f4/f5/f6/f7 全程 0x00 → PHY 干净、DP 同步锁稳定（B44 的 0xF6=0x40
   是粘滞残留，不是当前故障）。
3. 旁证（⚠ 当时推测，已被迭代 41 修正）：两链 host_power_on 相差 22ms——后来发现
   24ms 只是 CTRL.ENABLE（时钟/PHY）的差，包引擎（VID 位）一直在 host_enable 里
   µs 级先后启动；µs 级恒定相位差是否被桥容忍仍是开放问题（XBL 也是 µs 级双写）。
4. AUX 通道已打通：Android 工作态经桥 AUX 读面板 DPCD 0x200(SINK_STATUS)=0x81
   （bit0=1，TCON 收到稳定同步）——DP 侧"真在出帧"的机器证据，B47 探针将加入该读数。

### B47-B60：人眼验收、ABL 对齐与单链 bisect（2026-11-01）

**B47（人眼：亮屏无彩条）+ B48/B49（DPU/时钟取证）**：人眼=黑屏（仅背光）。
但 DPCD 0x200=0x81 全程（TCON 收到稳定同步）→ DP 侧一直有输出，只是像素坏。
DPU INTF 时序与 ABL 逐位一致；时钟树经 RCG 解码与 ABL 等价——配置层全部对齐后
失败依旧，随机坏链不变。

**B50/B51（同步启动实验 + 决定性观测）**：镜像厂商使能序列（DSI 引擎延迟 +
post_kickoff master-only）。结果：双引擎启动实测 **3µs**（超 130ns 预算），
且 **slave 的 INTF timing engine 从未启动**（SPLIT_DISPLAY 耦合没起作用）。

**B52（ABL 工作态拓扑定音）**：ABL 期实测——**SPLIT_DISPLAY_EN=0**（厂商内核的
耦合路线本机根本没用）、**byte1/pclk1 RCG SRC=1（dsi0 PLL）**（两链控制器时钟域
统一到 dsi0 的 PLL）、双 INTF 引擎都在跑。ABL 的工作态=同 PLL + 双引擎 + µs 级
启动差 + 无耦合。

**B53/B54（逐项对齐 ABL）**：B53 把 dsi1 的 byte/pclk 改挂 dsi0 PLL（生效，四个
RCG 与 ABL 一致）→ 失败模式不变，排除跨 PLL 频漂。B54 回退 master-only（恢复
双 INTF 引擎=ABL 状态）→ 探针正常，证据因设备 USB 卡死未取回（待补读）。

**B66-B70（vermagic 混乱澄清 + 单链 bisect 裁决）**：B66 无效轮（孤儿构建跨 commit
致 Image/模块 vermagic 不一致——**重建后必须 modinfo 核对**）；B69 脚本变量未展开
（boot 失败）；**B70（裁决）**：单链内核正常跑——display probe deferred 重试至 ~113s
完成，DSI0 流水线持续流出 ≥35s，FIFO 错误签名与双链完全相同 → **故障在公共路径
（桥/DSI/像素源），dual 机制无辜**。此前的"36.7s 挂死/70s 崩"部分为幻影 fastboot
sightings+过早收尾伪象。取证设施全部可用（0.3s flush/ramoops@0x1F0000000/pstore）。

**B61-B65（取证链建成 + 单链定性反转）**：B61 修 ramoops 规范形态（驱动要
IORESOURCE_MEM，memory-region 方式必 -EINVAL）后 **probe 成功**；B62 选址撞
memory@8C700000；B63 发现 panic 自检块两个 bug（flush 未定义、sysrq 掩码不含 'c'）；
B64 自检通过（sysrq panic 生效、增量 flush 落盘）；**B65 定性反转：单链不是崩溃是
探测路径静默挂死**——dmesg 静止于 36.73s（dpu/dsi 依赖解析后、dsi_bind 前、零报错），
软锁死检测器 ~30s 后 panic 但控制台被挂死方持锁，panic=10 后回 fastboot——B56-B60
的"70s 崩"全部吻合此模型。下一步：hung_task/softlockup panic 化拿带栈 dump、
ramoops 搬 ABL 不踩的地址（试高地址）、或静态排查单链 probe 路径（#11 aux_bridge 嫌疑）。
另注：B54 后设备 USB gadget 曾卡死 5 小时（枚举 17ef:7cdc Lenovo VID，别按 05c6/18d1
过滤 lsusb），自行恢复，数据面无恙。B65 后设备已回 Android，dtbo_a 校验一致。



### B77/B78：MISR 证明 INTF 有数据；DSI 其实只发 1080 半宽（2026-11-01 14:20，关键反转）

**B77（MISR 探测）**：给 `devmem` 加写能力后，在 SETCRTC 后对 INTF_1 做 MISR：
- `MISR_CTRL(0xae6b980)` 写 clear(`0x400`) → enable(`0x81000101`)；
- 读回 `CTRL=0x81000301`（BIT8 ENABLE + BIT9 STATUS 都置位）；
- 两次 `SIGNATURE` 不同：`0x43be24d2` → `0x1cfa12bf`。
→ **INTF 输入端有持续变化的数据流，不是空帧**。推翻"DPU 像素通路断开、两链发空帧"的假设。

**B78（桥通道模式运行中重写）**：
| 动作 | 写前 | 写入后 | +2s |
|---|---|---|---|
| `0x10`（DSI 通道模式） | `0x80` | **`0x20`** | `0x20`（未被覆盖） |
| `0x20/0x21`（CHA line length） | 1080 | **2160** | — |

→ 驱动表里 `0x10=0x20` 读回 `0x80` 是**写入时机**问题（初始化阶段写不进），并不是寄存器只读。
**但写完 2160 后 CHA 立即报 INVALID_LENGTH**（`f1=0x20`、`f6=0x40`）——因为 DSI 实际每行只有 1080。

**由此反推 DSI 时序**：`TOTAL=0x05560463`（htotal 1124）、`ACTIVE_H=0x0454001c`（ha_end 1108 / ha_start 28）
正好等于 **1080 mode（1080 + 16+16+12 = 1124）**，而不是 panel 驱动的 2160/2204。
→ **B56–B78 一直说的"真单链 2160"从未真正生效：DSI 只发半宽**。这与"桥 line length=1080 恰好匹配"
互相印证。之前所有"改桥配置"的轮次都建立在"DSI 发 2160"的错觉上。

**下一步（B79）**：在 `dsi_mgr_bridge_mode_set`（它拿的是 `adjusted_mode`）与 `dpu_encoder` 打点，
确认 `adjusted_mode.hdisplay` 到底是 2160 还是 1080，定位切半发生在 DRM tile / DPU split / panel
`get_modes` 哪一层。

## 当前结论摘要（2026-11-01 13:55，B76 后 + 彩条里程碑）

> 本节是现行结论；13:15 的旧版（B26 时代）已整体作废删除——其中"桥=0x33"“全图 0 差异”
> 是 B39 地址 bug 发现前的错误对照，别再引用。

### 已经确认的事实（每条都带证据）

| # | 结论 | 证据 |
|---|---|---|
| 0 | **桥内置彩条出图**（`0x3C=0x10` → 八色彩条上屏）→ **eDP 输出侧（桥→PS8331→面板）全线正常**，故障锁定 DPU 像素数据通路 | 用户照片 `colorbar-evidence.jpg`；GPT 外部评审建议 |
| 1 | 显示拓扑 = DSI0/DSI1（bonded，各 1080 宽）→ **SN65DSI86 @0x2C**（真桥）→ eDP 玻璃；PS8331 @0x33 只是端口 mux | B39 修地址 bug；本机运行时 FDT `eDP-MUX@66` |
| 2 | 桥（0x2C）上电、59 条表、semi-auto 训练全部成功：`0x0A=0x82`（PLL 锁）、`0x96=0x01`（训练完成） | B39 起；`private/logs/20261031-2100-B39/` |
| 3 | **桥收到了 DSI**：时钟检测 `0x12=0x36`、PHY 层零错误 `0xF0=0x00` | B44 诊断 |
| 4 | EOT 包缺失曾是主因之一：面板去 `MIPI_DSI_MODE_NO_EOT_PACKET` 后（B45），CHA 侧 INVALID_LENGTH 与 DP 同步锁错误消失 | B45 双控制器 EOT 位=1 闭环 |
| 5 | 剩余故障 = **每次引导随机一条链报 INVALID_LENGTH**（0xF1/0xF3 bit5），boot 内持续；FIFO 洪流专属坏链，好链全程干净 | B45-B47 三轮镜像反转 |
| 6 | DP 侧健康：同步锁 0xF6 清后为 0、DPCD 0x200=0x81（TCON 收到稳定同步）→ **桥一直在出帧，只是像素内容坏** | B47 AUX 证据 |
| 7 | 配置层与 ABL 工作态逐位对齐：DSI 控制器/PHY/桥寄存器/INTF 时序/时钟树（byte0/1、pclk0/1 全挂 dsi0 PLL）——对齐后失败依旧 | B48/B49/B52/B53/B55 五轮 |
| 8 | pclk/byte 比值精确 4/3（69,142,492 / 92,189,991），速率失配排除 | B55 实测 |
| 9 | 背光由我们自己的驱动点亮；桥的电源/GPIO 序列全部存活 | B7 人眼；B22 分步 |

### 已被否定的假设（**别再重复这条路**）

| 假设 | 怎么被否掉的 | 日期 |
|---|---|---|
| 缺 eDP 桥驱动 / 桥上电整机复位 / 训练顺序错 / refclk 不对 / 面板复位时机 / gpio60 | B11-B22 各轮（见 worklog 与 rounds 表） | 10-31 |
| 桥=0x33、"全图 0 差异" | **地址 bug**：真桥在 0x2C，0x33 是 mux；B23 的 0 差异是比错了芯片 | 10-31 B39 |
| 寄存器值不对 | 修对芯片后 PHY 96 word/控制器/桥逐位对齐（B41/B48） | 10-31 |
| HS_TX 超时 | `HS_TIMER_CTRL=0x4ea60` 显式写入（B42），status=5→4 | 10-31 |
| byte_intf_clk ÷2 与否 | B43 实验（当时条件下的负结果） | 10-31 |
| 桥没收 DSI | B44：`0x12=0x36`+`0xF0=0x00`，收到了 | 10-31 |
| EOT 缺失（单独作为全部根因） | B45 修复后只剩单链随机错（B46） | 10-31 |
| 两链 22ms host_power_on 差 = HSS 偏斜 | **迭代 41 修正**：包引擎（VID 位）一直在 host_enable 里 µs 级先后启动，24ms 只是 CTRL.ENABLE 的时钟/PHY 差 | 11-01 |
| 跨 PLL 频漂 | B53 统一时钟域（byte1/pclk1 挂 dsi0 PLL，与 ABL 一致）后失败不变 | 11-01 |
| SPLIT_DISPLAY 耦合缺失 | B52：ABL 工作态 SPLIT_DISPLAY_EN=0，厂商内核那套本机不用 | 11-01 |
| slave INTF 引擎没启动 | B51 证实（当时如此），B54 回退后双引擎都在跑，失败依旧 | 11-01 |
| pclk/byte 速率失配 | B55：比值精确 4/3 | 11-01 |
| 0x95=0xC0 差异 | 定名=TPS1/TPS2 fast-training 标志残留（训练已成功），DP 侧健康 | 11-01 |
| 启动相位差（µs 级）为根因 | **存疑未定**：XBL 自己也是 µs 级双写却能用；B55 亚µs 化后仍失败 | 11-01 |

### 当前前沿（下一步）

**单链 bisect 结论已出（B75-B76）**：
- 真单链（去 dual 属性 + dsi1 禁用 + LANE_CTRL 对齐）**不挂死**——B56-B70 的
  "单链挂死"是 bonded 属性未删干净导致的组件等待矛盾（GPT 外部评审发现）。
- 单链下桥 DSI RX 零错误（0xF0-F7 全 0）、TCON 报稳定同步（DPCD 0x200=0x81）、
  探针 5.5 分钟无崩溃——协议层完全正常。
- 但**屏幕仍黑**（人眼确认）→ 故障在 DPU 的像素数据通路（SSPP→LM→INTF），
  协议正常但像素内容为黑/空。
- 桥内置彩条（0x3C=0x10）证明 eDP 输出侧完好 → 问题锁定在 DPU 发出的
  DSI 流里像素内容为空/黑。
- **B76（DSI TPG）反而关背光**——DSI 侧 TPG 干扰了桥训练，已回退。
- **下一步：DPU INTF 内置 TPG**（INTF_TPG_ENABLE @0xae6b800+0x100），
  绕过 SSPP→LM 数据路径直接出图，二分"像素路径 vs INTF→DSI 通路"。
取证设施已建成：增量 flush（B60 生效）、pstore 双 boot 协议、pclk/byte 实测。

---

## ⚠ 以下为早期章节（部分已被推翻，保留原文）

## 现状

- `simpledrm` 已经在用：`Console: switching to colour frame buffer device 270x84`，
  `simple-framebuffer 9c000000.framebuffer: [drm] fb0: simpledrmdrmfb`。这是"借用 bootloader 点亮的画面"。
- `msm` 内置时日志报 **`DSI PLL(0) lock failed, status=0x00000000`**。
  原因已定位：我们的板级 dts 里挂的面板节点是从 q706f 抄来的
  `compatible = "samsung,amsa26zp01"`（另一块屏，带初始化序列），时序与本机完全不匹配。

## 本机面板的原厂参数（来自运行时 FDT，`~/amp/fdt/fdt.dts`）

| 参数 | 值 |
|---|---|
| 面板名 | `"Sim dual video mode dsi panel"`（DT 节点 `qcom,mdss_dsi_dual_sim_video`） |
| 类型 | **双 DSI split**、video mode、24 bpp、**无 DSC** |
| 分辨率 | 每链路 1080 × 1350，合计 **2160×1350 @ 60 Hz** |
| 水平时序 | hfp 16 / hbp 12 / hpw 16 |
| 垂直时序 | vbp 8 / vfp 8 / vpw 1 |
| PHY timings（14 B） | `00 11 05 05 08 08 05 05 05 03 02 04 0f 0a` |
| 拓扑 | `<2 0 2 1 0 2>`（2 个 DSI 控制器，无压缩） |
| 背光 | PWM，周期 500 µs（2 kHz），0–255 级，默认 0x4d |
| 物理尺寸 | 280 × 175 mm |
| 初始化序列 | **无**（靠 bootloader 点亮，面板自身不需要 DCS init） |

## 路线（按 skill 的顺序）

1. **写最小面板驱动** `panel-lenovo-k606f.c`：只有 enable/disable + 时序，**没有** init 序列；
   双 DSI 用主线的 dual-link 描述（两个 endpoint 指向 dsi0/dsi1）。
2. 板级 dts 里把 `samsung,amsa26zp01` 换成新驱动，timings 用上表的值。
3. **`CONFIG_DRM_MSM=m`**：shell 里手动 `insmod`，看得到完整 dmesg，调一次不用重启整机。
4. 先在**固件当前刷新率（60 Hz）**下完成接管，再谈背光曲线、熄屏、其他刷新率。

## 正在准备的东西

- `out/kernel-msmmod`：`CONFIG_DRM_MSM=m` 的 Image + modules 构建（配置见 `sources/k606f-probe.config`）。
- 之后把 `msm.ko` 与依赖链（`drm_display_helper`、`drm_kms_helper`、`drm_exec` 等）放进 initramfs 的
  `/lib/modules/`，启动后在 shell 里 `insmod`。

## 迭代记录

### 迭代 1：换掉错误的面板（15:48）
- 把 `samsung,amsa26zp01`（q706f 的屏，带 init 序列）换成 `lenovo,k606f-panel`（加进 `panel-simple.c`，
  时序 2160×1350@60、无 init），dts 改成 `qcom,dual-dsi-mode` + `qcom,sync-dual-dsi` + 两个 port。
- 同时发现 `CONFIG_DRM_PANEL_SIMPLE` 在 pmOS 配置里是**关的**，已在自己的配置 `sources/k606f.config` 里打开。
- 结果：DSI 仍然不上，但拿到了明确原因：

```
platform ae94000.dsi: deferred probe pending: msm_dsi: Failed to get supply 'refgen'
platform ae96000.dsi: deferred probe pending: msm_dsi: Failed to get supply 'refgen'
```

### 迭代 2：补 refgen 供电（15:50，待验证）
- 参照 `kodiak.dtsi` 的写法，给 `&mdss_dsi0`/`&mdss_dsi1` 加 `refgen-supply = <&refgen>;`
  （`refgen` 是 `sm8250.dtsi` 里的 `qcom,sm8250-refgen-regulator`）。
- 这一项如果对上，DSI 应该开始试锁 PLL —— 下一步就是看 `DSI PLL(0) lock failed` 是否消失。

### 迭代 3：打开 refgen 驱动（15:58）
- 上一条 `Failed to get supply 'refgen'` 的真因不是 dts 缺属性，而是 **`CONFIG_REGULATOR_QCOM_REFGEN` 没开**。
  在 `sources/k606f.config` 里打开后重建（增量 27 秒）。
- 结果：`Failed to get supply 'refgen'` 归零 ✅，但 **`DSI PLL(0) lock failed, status=0x00000000` 仍然出现（4 次）**。
- 现在卡在 PHY 锁相环本身，下一轮要查的三个方向（按优先级）：
  1. **DSI PHY 的 `vdds` 供电取值**：现在用的是 q706f 的 `vreg_l5a_0p88`，本机可能不是这一路
     （从原厂 DT 的 `qcom,dsi-phy` supply 表取真实值）。
  2. **模式时钟**：`panel-simple` 里写的 `184380 kHz` 是从 `/sys/class/drm` 的模式行反推的，
     与本机 porch 算出来的 180.77 MHz 不一致，PHY 分频可能落不到合法区间；先用 porch 计算值。
  3. **双 DSI split 的时钟仲裁**：日志里的 `dsi0_phy_pll_out_dsiclk already disabled/unprepared`
     说明时钟状态被反复切换，需要看 `msm_dsi_manager` 对 dual 模式的处理路径。

### 迭代 4：关掉 DP 与 type-C 桥（16:19）
- 现象：`msm` 主设备（`msm-mdss`）已绑定，dpu/dsi0/dsi1 组件也都绑定了，但 `/sys/class/drm` 里**只有 simpledrm 的 card0**。
- 处理：`&mdss_dp`、`onnn,nb7vpq904m`、`fcs,fsa4480` 全部 `status = "disabled"`。
- 结果：`deferred probe pending` 从 6 条降到 3 条（剩下都是音频相关），但 **DRM card 仍然没出现**，
  且 `aux_bridge.aux_bridge.0: error -ENODEV: failed to acquire drm_bridge` 依旧存在。

### 上游已有答案（重要，省掉我们自己摸索）

搜到的 2026-09 dri-devel 讨论（Luca Weiss / Dmitry Baryshkov）与我们的现象**逐字对应**：

> `dsi_pll_7nm_vco_prepare()` 只解除 PLL_SHUTDOWNB，没有把 PHY digital top 上电（`DIGTOP_PWRDN_B`），
> PLL 锁不上。自从 commit `acf7a91d0b0e`（clk: qcom: dispcc-sm8250: Enable parents for pixel clocks）之后，
> 时钟框架会在 `of_clk_set_defaults()` 应用 DT 的 `assigned-clock-parents` 时**提前**使能 PHY PLL，
> 于是 probe 期就报 `DSI PLL(0) lock failed, status=0x00000000` + `dsi0_phy_pll_out_dsiclk already disabled`，
> 并且每次 probe 重试都会把启动拖慢几十秒。

关键结论有两条：
1. 我们的板子用的正是 **10nm DSI PHY**（`ae94400.phy`），该贴里明确说 10nm 也命中同一问题；
2. **这个报错本身不是"屏幕不亮"的充分原因**——很多能正常出图的设备也带着它。
   → 所以真正卡住的是 msm master 没注册 DRM 设备，`aux_bridge` 的 `-ENODEV` 是当前最可疑的一环。

### 迭代 5：msm 模块化调试平台 + DRM 设备出现（17:00）
- 把 `CONFIG_DRM_MSM=m` 改成模块，其余显示配置（panel-simple=y、refgen=y、关 DP）不变，
  做成 `sources/k606f-probe.config` + `out/kernel-msmmod`。
- initramfs 里按依赖顺序 insmod 9 个模块：
  `drm_dp_aux_bus → drm_exec → drm_gpuvm → gpu-sched → cec → drm_display_helper → mdt_loader → ubwc_config → msm`，
  全部 OK（`/tmp/msm-load.txt`）。
- **结果：`/sys/class/drm` 出现 `card0` + `card0-Unknown-1`，`/dev/dri/card0` 出现**。
  这是从「只有 simpledrm」到「msm 拿到显示」的关键突破——msm DRM 设备注册成功，DSI connector 建立。
- 两个待修问题：
  1. **`panel-simple-dsi: Expected bpc in {6,8} but got: 0`** —— panel 描述符漏了 `.bpc = 8`，已补。
  2. **connector 名字是 `Unknown-1` 而非 `DSI-1`** —— connector_type 没从 panel 传上来，待查
     （dual-DSI 下只有 master dsi0 建 connector，slave 跳过，逻辑正确，但 type 仍是 Unknown）。
- 附带修复：busybox telnetd 客户端断开后会退出导致调试失联，改成 `while true` 循环保活。
- 附带：`clk_core_unprepare` 的 WARNING+Call trace（`of_clk_set_defaults` 提前使能 PHY PLL），
  即之前搜到的上游已知噪声，不致命。

### 迭代 6：定位 dsi1 缺 assigned-clock-parents（17:25）
- 对比能正常工作的同 SoC 双 DSI 板 `sm8250-xiaomi-elish-common.dtsi`，发现我们的 `&mdss_dsi1` 缺了关键配置：
  - `qcom,dual-dsi-mode` / `qcom,sync-dual-dsi`（两个节点都要写）
  - **`assigned-clock-parents = <&mdss_dsi0_phy DSI_BYTE_PLL_CLK>, <&mdss_dsi0_phy DSI_PIXEL_PLL_CLK>`** ← 最关键
- 根因：`sm8250.dtsi` 里 dsi1 默认 `assigned-clock-parents = <&mdss_dsi1_phy ...>`（用自己的 PLL），
  而 dual-DSI 下 slave（dsi1）必须用 master（dsi0）的 PHY PLL。不改的话 dsi1 会锁自己的 PLL →
  **`DSI PLL lock failed`** 的直接原因。
- 已修正 dts，dtb 里确认 dsi1 的 `assigned-clock-parents` 指向 `mdss_dsi0_phy`（phandle 0xf0）。
- 预期连带解决 connector 名字 `Unknown-1`（时钟配置正确后 msm 组件绑定完整）。
- 另：原厂 PHY 是 `qcom,dsi-phy-v4.1`（下游），主线上对应 `qcom,dsi-phy-7nm`（sm8250.dtsi 已用），
  供电 `vdda-0p9`=0.88V 对应 `vreg_l5a_0p88`，正确。

## 显示链路拓扑已确认：DSI → PSA8331 eDP 桥 → 玻璃（2026-10-30）

> 结论来源：**本机运行时 FDT**（`~/amp/fdt/fdt.dtb`，用 `dtc -I dtb -O dts` 反编译，见 `docs/tech-debt.md` #15）
> + 厂商开源包 `yt_k606f_s100101_210209_row`（`techpack/display/msm/dsi/dsi86-edp-mux.c`，已核实在包内）。
> 本节推翻「面板直连 DSI 玻璃」的建模，但**不改变当前唯一下一步**。

### 硬证据

| 项 | 值 |
|---|---|
| 节点 | `/soc/i2c@984000/eDP-MUX@66`（`qupv3_se1_i2c`，100 kHz） |
| compatible | `"eDP-MUX-8331"` |
| 地址 | `reg = <0x66>` = 8 bit → **7 bit `0x33`**，与厂商驱动 `EDP_MUX_I2C_ADDR 0x33` 一致 |
| 芯片 | PSA8331A（厂商驱动错误日志里的型号串） |
| IRQ | `dsi86-irq` = TLMM gpio 110，`dsi86_edp2mip_irq` |
| 供电 | `vdd-1p2` = `pm8150a_l3`、`vdd-1p5` = `pm8150_l7`、`vdd-1p8` = `pm8150_l15`、`vdd-3p3` = `pm8150a_l11` |
| 状态回读 | `dsi86-pwr-status` = gpio 72（输入） |
| 时钟 | `bb_clk2`（`clocks = <0x15 0x04>`） |

GPIO 表（全部 TLMM，`<&tlmm n 0>`；已按正确 cell 解析，**旧 `fdt.dts` 里这些值全是乱码**）：

| 功能 | gpio | 功能 | gpio |
|---|---|---|---|
| `mipi-to-edp-en` | 12 | `lcd-bob-en` | 71 |
| `i2s-mux-en` | 14 | `865-lcd-power-on` | 101 |
| `panel-gpio-mux-en` | 23 | `865-bl-en` | 102 |
| `edp-aux-port-sel` | 40 | `865-bl-pwr-en` | 9 |
| `edp-mux-pwr-en` | 42 | `io-mux-en` | 67 |
| `boost-bypass-en` | 46 | `io-mux-en-select` | 108 |
| `boost-vsel-en` | 48 | `i2s-mux-select` | 104 |
| `boost-nbyp-en` | 49 | `dsi86-irq` | 110 |

### 这条证据解释了什么

1. **为什么活动面板是 `qcom,mdss_dsi_dual_sim_video`**（"Sim dual video"，无 DCS init 序列）：
   真正干活的显示器件不是 DCS 面板，DSI 只按固定时序吐视频流，屏侧初始化与 eDP 链路训练由桥负责。
   → 本文档上方那张面板参数表（每链路 1080×1350、无 init）**依然有效**，已用 dtc 重新反编译逐项复核一致。
2. **为什么 FDT 的 `ports` 里看不到屏**：桥是纯 I²C + GPIO 器件，不走 OF-graph，从 DSI 的 endpoint 追不出来。

### 但它不是当前黑屏的原因（重要）

厂商代码里桥的初始化排在 **DSI 使能之后**（`dsi_drm.c` → `dsi_display_enable()` 之后才是 `dsi_display_dsi86_init()`）；
主线对应 `dsi_mgr_bridge_pre_enable()` → `dsi_mgr_phy_enable()` + `dsi_ctrl_enable()`。
`dsi_ctrl_enable` 是 msm 按 DT 时序算完就写的，**与系统里有没有桥无关**。所以桥再缺，`DSI_CTRL` 也会被写。
我们的 `DSI_CTRL = 0x20040000`（ENABLE 未置位）说明根本没走到那一步 —— 仍是 bridge 链装配问题。

### 落地顺序（钉死，不许跳）

| 阶段 | 目标 | 判据 |
|---|---|---|
| **A（现在）** | 修 bridge 链，让 DSI 真正 enable | `DSI_CTRL` ENABLE 置位、`CLK_CTRL ≠ 0`、`dsi_mgr_bridge_pre_enable` 有 DBG |
| B | DSI 通了但仍黑 → 引入 `psa8331` 桥（最简版：4 路电 + 上电 GPIO 序列 + `dsi86_edp_mux_init` 的 4 条写） | 出画面，或至少 I²C 有回包（"桥被访问过"） |
| C | 仍黑 → 抄 `dsi86_init[]` 106 条寄存器 + `pre/post ULPS` 时序 | 出图 |

B 阶段的模型改动：`dsi_mgr_bridge` → `psa8331_bridge`（`drm_bridge`）→ `drm_bridge_connector`；
面板供电从主线 dts 的 `vddio` 挪进桥驱动，避免两边抢同一路电；背光拆成 `865-bl-en`（开关）+ PM8150L LPG PWM（调光）两级。
**不要**整份照搬厂商驱动（`board_id` / `first_probe` / `switch_to_tv_mode` 是产品形态相关）。


### 11:15–11:30 又排除两条，并发现一个结构性时序错误

**B14 排除**：① "桥要比 DSI 先上电"（先上电后加载 msm，`dsi_err` 照样增长 10→21）；
② refclk（`ln_bb_clk2` 19.2 MHz，`prepare/enable = 2/2`，绑定在本设备 `refclk` 上，是真的在跑）。

**发现的结构性错误（B15 起修正）**：DRM 的 `pre_enable` 按链序执行
（`dsi_mgr → psa8331 → 面板`），所以**我做 eDP 训练时面板还在复位状态**，DP AUX 不会应答，
`0x0A`/`0x96` 自然停在空闲值。厂商的顺序恰好相反：
`dsi_panel_power_on`（面板上电/出复位）**先**，`dsi_display_dsi86_init`（桥初始化）**后**。
→ 修法：把 stage 6（mux）+ stage 5（训练）从 `pre_enable` 挪到 **`enable()`**，它在面板 `pre_enable` 之后。

**还没用的线索（下一步首要怀疑）**：原厂 `sde_dsi_active` 引脚组里有**两根**线 ——
`gpio75` **和 `gpio60`**；我们的板级 dts 只用了 75（还注着"同 elish"抄来的），**gpio60 从未驱动**。
原厂 FDT 里没有任何属性引用 gpio60，说明它由厂商面板驱动按编号直接驱动，很可能是面板的
VDD/EN 使能。下一轮应把 gpio60 纳入上电序列（极性待用 Android 侧真值确认）。

**观测手段补强（镜像 `f4bbc69d`）**：`msm` 加载常把 USB 一起带走，导致读不到 dmesg。
现在训练结果用**背光闪码**自报：**3 短闪 = 训练成功**、**1.5 秒长暗 = 失败**。


---

## ⚠ 已被推翻：寄存器级对照（2026-10-31 11:35–11:45）——当时以为"只差 9 个寄存器"

> **这一节的方法是对的，结论被 B23 推翻了。** 当时只比较了 12 个寄存器，看出 9 处不同，
> 于是以为"照搬这 9 个值就能出画面"。B23 做了**整 256 字节**比对，结果是 **0 差异** ——
> 也就是说那 9 处不同只是"采样时机的差别"，不是根因。
> 保留这一节是因为"机械提取厂商表 + 逐字节 diff + 照搬可工作配置"这套方法本身值得复用。


方法：`tools/i2cdump8`（静态 aarch64，读/写桥寄存器）分别抓两份快照，逐字节 diff。

| 快照 | 来源 | 说明 |
|---|---|---|
| `private/logs/20261031-1135-regdump/android-BL-state.txt` | Android 运行中，桥被**引导程序**配好、屏幕正常显示 | 256 字节，0 读失败 |
| `private/logs/20261031-1140-B17/03-full-dump.txt` | 自制内核，按厂商 52 条表写完（训练失败态） | 256 字节 |

**差异只有 9 个**，而且全部是"厂商表写入的值"与"能让屏幕正常显示的值"不同：

| 寄存器 | 能正常显示（BL） | 我们（厂商表） |
|---|---|---|
| `0x0a` | `00` | `02` |
| `0x10` | `0a` | `80` |
| `0x11` | **`82`** | `02` |
| `0x12` | `0a` | `36` |
| `0x13` | `0a` | `36` |
| `0x16` | `00` | `01` |
| `0x20` | `a4` | `38` |
| `0x21` | `01` | `04` |
| `0x23` | `88` | `04` |

解读（都还没证实，按可能性排序）：

1. **`0x11` 的 bit7**：BL 是 `0x82`（bit7 置位），我们是 `0x02`。0x82 这个值在厂商驱动里恰好是
   **"DP PLL 锁成功"时 `0x0A` 的读数**，很可能 bit7 就是链路/时钟就绪位。
2. **`0x10` 的 `0x80` vs `0x0a`**：`0x80` 更像状态位（错误/未同步），`0x0a` 是配置值。
   我们的写入（厂商表的 `0x10=0x80`）**覆盖了 mux_init 写的 `0x0a`** —— 这两处写同一个寄存器，
   顺序上 mux 在前、52 条表在后，后者把前者的值冲掉了。
3. `0x12/0x13/0x16/0x20/0x21/0x23` 属于 DP 发送端参数（lane 数、速率、预加重、摆幅一类），
   BL 与厂商驱动选了不同组合。

**由此得到两条可执行的验证**：

- **stage 7（已实现）**：直接把上表"BL 那 9 个值"写进去，跳过厂商表，看画面是否出现。
  镜像 `af33eaa3` 带它（B18 跑了但 `msm` 加载后 USB 掉线，没来得及实测）。
- 若 stage 7 有效，说明**根本不需要那 52 条表**，直接用 BL 的组合 + 自己的 DSI 就能出画。

### 工具升级

`tools/i2cdump8.c` 现在支持：
- 读：`i2cdump8 <bus> <addr> [from] [to]`
- 写：`i2cdump8 w <bus> <addr> <reg> <val>`
- 读写都会在 `I2C_SLAVE` 失败时退到 `I2C_SLAVE_FORCE`，所以**驱动占着地址时也能poke**。

### 新发现的工具性问题（下一轮必须先解决）

`msm` 加载会带走 USB（今天 B3/B12/B15/B18 都是），**加载之后的任何交互实验都做不了**。
下一轮把实验做成**脚本化**：在 initramfs 里用 cmdline 开关串起
「上电 → insmod msm → 写 BL 那组值 → 用背光闪码回报结果 → 自动重启」，
人只需要看一次屏幕，不再依赖 USB 会话。


---

## B21（12:10）：寄存器写进去并且**稳住了**，但屏幕仍然空白 —— 缺口不在寄存器值

第一次用**持久化日志**取回完整证据（写 `dtbo_b` 第 4 KiB，恢复 Android 后 `dd` 读回），
终于摆脱了"msm 一加载 USB 就掉、什么也读不到"的死结。

日志原文（`private/logs/20261031-1209-B21/probe-log.txt`）：

```
mode=1
bridge rc=0 → insmod 桥成功
msm rc=0    → insmod msm 成功
stage1..6 rc=0
--- before (dsi_err_worker=…) ---
0a: 02   10: 0a   11: 02   12: 36   13: 36   16: 01   20: 38   21: 04   23: 04
（写入 BL 那 9 个值）
--- after ---
0a: 00   10: 0a   11: 82   12: 0a   13: 0a   16: 00   20: a4   21: 01   23: 88
--- after+5s ---
（与 after 完全相同，两次采样一致）
```

三条硬结论：

1. **写入生效且稳定**：9 个寄存器全部落到"引导程序能正常显示"时的取值，5 秒后回读一致。
   → **"寄存器值不对"这个假设可以彻底排除**。前面那 9 字节的 diff 不是根因。
2. **`dsi_err_worker: status=5` 仍在刷屏**（34.13–34.20 s，两两成对，与双 DSI 对应）。
   这是 TIMEOUT|FIFO，意味着 **DSI 发出的数据没有被接收方消费** —— 桥此刻并没有真正在收 DSI。
3. 顺带确认 `fb0: msmdrmfb` 注册成功、`bridge powered and set to MIPI->eDP` 也打印了，
   即整条软件链路"看起来"都成功，唯独 DSI 时序上没人接。

### 由此得到的下一个判据：DSI 错误的**停止时刻**

| 轮次 | 桥上电相对 DSI | `dsi_err_worker` |
|---|---|---|
| B7（手工先上电，后 insmod msm） | 桥**先** | **停止增长**（10 → 10） |
| B12 / B21（msm 自己在 `pre_enable` 里上电） | 桥**后** | 持续刷屏 |

→ B22 的设计：**在 `insmod msm` 之前先 `stage 1/2/3/6`**，让桥进入 MIPI→eDP 模式后再让 DSI 起来。
判据是日志里 `dsi_err_worker` 的计数是否停在某个值不再增长（我在每份快照里都记了 `dsi_err_worker=N`）。


---

## B23–B25：寄存器全图 0 差异 + 两个工具层面的真相

### B23（12:34 镜像 `f1134c3c`）：256 字节全图 diff，**0 个差异**

前几次只看 12 个寄存器，这次把整份 256 字节映射抓下来，与 Android 工作态逐字节比：

```
寄存器数: android=256  after=256  after+5s=256
Android(工作态) vs 我们(after+5s) 差异 0 个
```

**结论：桥的配置完全正确，问题不在桥的寄存器。** 这一条把"再调寄存器"这条路彻底关掉了。

### B24（12:43 镜像 `1963a310`）：往 `/dev/fb0` 写整屏 → **失败**

```
fb0 present, node=2160,1350 stride=8704 bpp=32
fb0 white write rc=1
```

`rc=1` 说明 16 MB 的整体写入失败了（与 dmesg 里 `framebuffer is not in virtual address space` 一致）。
所以"屏幕没变化"这一轮**不能作为判据** —— 我们其实什么都没写进去。

### B25（12:54 镜像 `ffdba22d`）：两个新事实

```
dri: card1 renderD128
no /dev/dri/card0
fb0: virtual_size=2160,1350 stride=8704 bpp=32
fb0 white write rc=0        ← 小批量写入成功了
```

1. **DRM 卡号是 `card1`，不是 `card0`**：`simpledrm` 先占了 `card0`（
   `simple-framebuffer 9c000000.framebuffer: [drm] fb0: simpledrmdrmfb`），msm 只能拿 `card1`。
   我的 `drmpaint` 默认打 `card0`，于是直接 `no /dev/dri/card0` 跳过了 —— **整轮没画**。
2. **`/dev/fb0` 是可写的**：64 KiB 写入 `rc=0`。B24 失败是因为一次写 16 MB。
   → **不需要走 DRM，直接把画面分块写进 `/dev/fb0` 就行**，这是下一轮该走的路。

### 附带的重要发现（设备侧，非显示）

**`slot-unbootable:a:yes`** —— 反复失败启动之后，引导程序把 A 槽标记为不可启动，
于是 `fastboot reboot` 只会回到 fastboot，Android 永远起不来。修法：

```
fastboot set_active a      # 复位不可启动标志与重试计数
fastboot reboot
```

这一条以前没记录过，今天卡了十几分钟，已写进 AGENTS.md 与 skill。

### 还确认了一件事：GPIO 映射全对

从 Android dmesg 里拿到厂商驱动请求的 16 个 GPIO 名字与编号（base 1100），逐条对照我们的 dts：

| 厂商名字 | 引脚 | 我们的属性 | 结果 |
|---|---|---|---|
| lcd-bob-en | 71 | lcd-bob-en | ✓ |
| 865-lcd-power-on | 101 | lcd-3v3-en | ✓ |
| edp-mux-pwr-en / edp-aux-port-sel | 42 / 40 | 同名 | ✓ |
| panel-gpio-mux-en / io-mux-en / io-mux-en-select | 23 / 67 / 108 | 同名 | ✓ |
| i2s-mux-en / i2s-mux-select | 14 / 104 | 同名 | ✓ |
| mipi-to-edp-en | 12 | 同名 | ✓ |
| boost-bypass-en / boost-vsel-en / boost-nbyp-en | 46 / 48 / 49 | 同名 | ✓ |
| 865-bl-pwr-en / 865-bl-en | 9 / 102 | bl-pwr-en / bl-en | ✓ |
| dsi86-pwr-status | 72 | pwr-status | ✓ |

**16 个全对**，而且厂商表里**没有 gpio60** —— 之前怀疑的"漏了 gpio60"不成立。
极性也对：厂商 `switch_to_port1(true)` 输出 0，我们的这些属性是 `GPIO_ACTIVE_LOW`，
`gpiod_set_value(1)` 正好驱动成低电平。

## 两个已知风险

1. **双 DSI split 是最大的不确定项**：主线 `msm` 的双链路支持主要面向 bonded 宽屏；
   我们是"每链路各 1080"的 split 模式，需要确认 `dsi_manager` 的接线方式。
   参考：xiaomi-elish 用的是 `dual_cphy_video` 且已在主线跑通，可对照其 DTS 写法。
2. **面板无 init 序列**：现在靠 ABL 点亮。`fastboot boot` 场景 ABL 先点过屏，所以 simpledrm 有画面；
   等到冷启动（从分区启动）时，如果面板没被初始化过，可能全黑——这需要面板驱动自己完成上电时序，
   或在 initramfs 里保留一次"点屏"（技术债）。

### 迭代 7（准备）：用 pr_info 探针把「bridge 链到底怎么接的」一次问清（10-30 14:05）

> 上机前先写四句话（skill 要求）：
> 1. **只改了什么**：只往 msm 的 4 个文件里加 `pr_info("[K606F] …")`，**不改任何逻辑**；
>    探针覆盖 `dsi_bind`（bonded/master/next_bridge 是否取到）、`msm_dsi_modeset_init`（含 slave 早返回分支）、
>    `msm_dsi_manager_connector_init`（`drm_bridge_attach` 返回码 + connector 类型）、
>    `dsi_mgr_bridge_pre_enable`（进入/判定 slave/power_on 结果）、`msm_dsi_host_power_on`
>    （进入 + `dsi_ctrl_enable` 之后回读 `DSI_CTRL`/`CLK_CTRL`/`TRIG_CTRL`）、`dpu_kms` 建 encoder。
> 2. **如果看到 `pre_enable ENTER id=0`**：说明 bridge 链里有 `dsi_mgr_bridge`，问题在链内 —— 再看
>    `power_on rc` 与 `host_power_on` 的回读值，定位是 PHY 上电失败还是 `dsi_ctrl_enable` 没写进 `ENABLE` 位。
> 3. **如果 `pre_enable ENTER` 一次都没有**：说明 `dsi_mgr_bridge` 没进 encoder 链（回到「attach 失败被回滚」
>    或「根本没走到 connector_init」），此时看 `dsi_bind`/`modeset_init`/`connector_init` 三处的输出就能定位到具体一步。
> 4. **判据**：不是「屏幕亮」，而是 `[K606F] host_power_on … 完成: DSI_CTRL=0x…` 里 `bit0(ENABLE)` 是否为 1，
>    以及 `CLK_CTRL != 0`。屏幕是否点亮另算（那是 A 阶段通过之后的 B 阶段）。
>
> 补一条对上一轮结论的修正：`pre-enabling bridges [ENCODER:x:y]` 这行日志是
> `drm_atomic_helper_commit_encoder_bridge_pre_enable()` 在**遍历链之前**打的（`drm_atomic_helper.c:1637`），
> 链为空也照样打印 —— 所以它只能证明「这台 encoder 走到了启用流程」，**不能**证明链里有 `dsi_mgr_bridge`。

### 迭代 7 结果：A 阶段其实早就通过了，之前是被一个错误读数带偏（10-30 14:25）

真机证据：`private/logs/20261030-1424-baseline/`（`insmod.txt` 原始会话、`probe-summary.txt` 摘要）。
镜像 `boot_img=d1733608…`、`kernel_image=19209330…`、内核 `7.2.0-g73131af110c3-dirty`；只 `fastboot boot`，没写分区。

关键日志（原文）：

```
[K606F] dsi_bind id=0 bonded=1 master=1 ext_bridge=bridge(0)
[K606F] dpu encoder: id=35 name=DSI-35 tiles=2
[K606F] modeset_init enter id=0 bonded=1 master=1 encoder=DSI-35
[K606F] connector_init id=0 encoder=DSI-35 next_bridge=yes attach rc=0
[K606F] connector_init id=0 bridge_connector=ok type=16        ← 16 = DRM_MODE_CONNECTOR_DSI
[K606F] modeset_init id=1 是 slave，跳过 encoder/connector
[K606F] pre_enable ENTER id=0 bonded=1 master_link=1
[K606F] host_power_on id=0 完成: DSI_CTRL=0x000001f1 CLK_CTRL=0x0000023f TRIG_CTRL=0x80001004
[K606F] host_power_on id=1 完成: DSI_CTRL=0x000001f1 CLK_CTRL=0x0000023f TRIG_CTRL=0x80001004
[K606F] pre_enable power_on rc=0
[K606F] pre_enable host_enable rc=0
[drm] fb0: msmdrmfb frame buffer device
dsi_err_worker: status=5        ← 反复出现
```

以及 `cat /sys/class/drm/card1-DSI-1/{status,enabled}` → `connected` / `enabled`。

`DSI_CTRL=0x1f1` 解出来：ENABLE(bit0)=1、LANE0-3(bit4-7)=1、CLK_EN(bit8)=1。
**两个 DSI 控制器都真的被使能了**，bridge 链完整（`next_bridge` 取到、`attach rc=0`、connector 类型 16=DSI）。

#### 之前那个「DSI 从未 enable」的结论是错的，原因有两个

1. `0x20040000` 这个值里含 bit29 / bit18，而 `dsi.xml.h` 里 `DSI_CTRL` 根本没有这两个位
   （只有 ENABLE/VID/CMD_MODE_EN/LANE0-3/CLK_EN/ECC/CRC）——**读到的是无意义值**，
   很可能是在 DSI 块时钟未开或读错 offset 的情况下取的。
2. `pre-enabling bridges [ENCODER:35:DSI-35]` 这行是遍历链**之前**打的（`drm_atomic_helper.c:1637`），
   链为空也照打，所以它从来不能作为「链里没有 dsi_mgr_bridge」的证据。

→ 教训：**判据要取「已使能」的正证据（寄存器中 ENABLE 位 + CLK_CTRL 非零），不要去解释一个可疑的读数。**

#### 现在真正的缺口：`dsi_err_worker: status=5`

`dsi.h` 里 `DSI_ERR_STATE_TIMEOUT=0x1`、`DSI_ERR_STATE_FIFO=0x4`，所以 `5 = TIMEOUT | FIFO`。
这是「DSI 在有拍地发，但链路上没有接收方/回不来」的典型表现 —— 与「玻璃挂在 eDP 桥后面、桥还没上电没配置」完全吻合。

#### B 阶段的具体做法（厂商实现已抽出，见 `权威数据/vendor-dsi/dsi86-edp-mux.c`）

厂商把桥的上电分两段，夹着 DSI 进/出 ULPS：

`dsi86_power_on_pre_ulps()`（DSI 进 ULPS 之前）
1. `turn_on_lcd_vcc(true)` → `lcd-bob-en`(gpio 71) = 1
2. `turn_on_lcd_vcc_loadswitch(true)` → 3V3 load switch
3. `turn_on_dp_mux(true)` → `edp-mux-pwr-en`(42)、`edp-aux-port-sel`(40)（板级 > EVT 时走 I²C 而不是纯 GPIO）
4. `turn_on_io_mux(true)` → `panel-gpio-mux-en`(23)、`io-mux-en`(67)、`i2s-mux-en`(14) **全部拉低（value = on ? 0 : 1）**
5. `switch_to_port1(true)` → 切到 MIPI 输入这一路
6. `turn_on_led_vss(true)`

`dsi86_power_on_post_ulps()`（DSI 出 ULPS 之后）
1. `turn_on_dsi86(true)` → 桥上电（`mipi-to-edp-en` 12、`mipi-to-edp-1v2-en`、`mipi-to-edp-1v8-en` 等等级联）
2. 等 `dsi86-pwr-status`(gpio 72) **读到 0**，最多 40×5 ms
3. `dsi86_set_to_tps1()` → I²C 写 `reg 0x96 = 0x02`

之后才是 `dsi86_edp_regs_init()`（106 条）与 `dsi86_edp_mux_init()`（4 条：`0x00=0`、`0x03=0x38`、`0x10=0x0A`、`0x11=0x02`）。

主线落地：把这套做成 `drm_bridge`，挂在 `dsi_mgr_bridge` 与面板之间：
`pre_enable` 里按上面两段顺序执行（DRM 的 pre_enable 从链尾往链头走，天然保证「DSI 起来之后、面板之前」），
`post_disable` 反序关；先用最简版（不加载 106 条寄存器、不做 eDP 训练），判据是有没有画面 / I²C 有没有回包。

---

## B 阶段现状（2026-10-30 15:20 收尾）

| 项 | 状态 |
|---|---|
| 桥驱动 `drivers/gpu/drm/bridge/psa8331.c` | 写完，编译干净（`psa8331.ko` 87 KB），**未上机** |
| Kconfig / Makefile | `DRM_PSA8331`（当前 =m） |
| dts 接线 | `mdss_dsi0_out → edp_mux_in → edp_mux_out → panel_in_0`，反编译 dtb 已核对 |
| 桥节点 | `&i2c1` 的 `bridge@33`（`reg = 0x33`），总线 100 kHz |
| 上机镜像 | `boot_img=95ab444a…`、`kernel_image=4f077cf6…`（含 `psa8331.ko`） |
| 上机结果 | **没有结果** —— builtin 版本挂死，模块版本还没跑过 |

### 驱动做了什么（对照厂商 `dsi86-edp-mux.c`）

- `pre_enable`：`lcd-bob-en` → `lcd-3v3-en` → `edp-mux-pwr-en` → io-mux 组 + 切 port1（低有效，DT 里写
  `GPIO_ACTIVE_LOW`）→ boost 三根 → 1p2/1p8 + refclk + `mipi-to-edp-en`（含厂商的 100 µs / 1 ms 延时）
- 等 `pwr-status`(gpio 72) 读到 0（40×5 ms）→ 写 `0x96=0x02`（TPS1）→ 写 `0x00=0`、`0x03=0x38`、`0x10=0x0a`、`0x11=0x02`
- `post_disable` 全反序；`attach` 用 `devm_drm_of_get_bridge(port 1)` 接面板
- **故意不做**：`dsi86_init[]` 那 106 条寄存器、eDP 链路训练、`board_id` / TV-in 模式切换、触控联动

### 上机顺序（设备恢复后照这个来）

1. 刷回 `dtbo_a`（`ada89e08…`）→ 确认 Android 正常，证明设备没被搞坏
2. `fastboot boot` **已知好镜像**（`d1733608…`，无桥驱动）→ 确认 USB NCM 2 秒枚举、telnet 可用
3. 换 `95ab444a…`（带 `psa8331.ko`）→ `insmod psa8331.ko`，看三行 `probe:` 日志
4. 再 `insmod msm.ko` → 期待 `psa8331` 的 `bridge powered and set to MIPI->eDP` + 画面 / I²C 回包
5. 仍黑就分两路查：桥是否真的上了电（读回 `0x96`、看 `pwr-status`）、DSI 的 `status=5` 是否消失

### 已知风险

- `bb_clk2` 的映射不确定：原厂 DT 写 `<rpmhcc 4>`（那是**厂商** rpmh 编号），主线按名字取了 `RPMH_LN_BB_CLK2`（=2）。
  若桥需要 19.2 MHz 参考时钟，这里可能取错，需要在设备上验证。
- `vdd-1p5` 在厂商代码里没显式调压（只 enable），我们照做；`vdd-1p2` 用的是 `vreg_l3c_0p8`，默认 0.8 V，
  驱动里会设到 1.2 V —— 这条 LDO 若有别的消费者，要复查。
- 背光仍走 `pwm-backlight`，桥只负责 `bl-pwr-en` / `bl-en`；两级是否协调未验证。

---

## B 阶段第一次上机（2026-10-31 09:10–09:45）：桥能加载了，但让它上电会整机复位

> 镜像：`fedfe168`（内核 `4f077cf6`，桥模块旧版）→ `f05bfe17`（`drm_bridge_add` 修复）
> → `8aefeb13`（引脚仲裁）→ `fd2cd341`（模块全部同步）→ `ed07c533` / `8c52a965`（分步上电开关）。
> 全部归档在 `private/images/`；日志在 `private/logs/20261031-0910/`。

### 发现 1：`drm_bridge_add` 直接 oops（rc=139，Segmentation fault）

现象：`insmod psa8331.ko` 段错误，dmesg 里 `lr : drm_bridge_add+0x30/0xd0`、`x0 = x1 = 0`、
`Code: … (f9000420)`（= `str x0,[x1,#8]`）。设备**没有**死，telnet 仍然可用——模块化的价值在这一步就兑现了。

根因：内核 7.x 起 `drm_bridge` 必须由 DRM 核心分配。`drm_bridge_add()` 会先

```c
if (!list_empty(&bridge->list))
        list_del_init(&bridge->list);
```

我们的 `struct drm_bridge` 是 `devm_kzalloc` 出来的，`list` 全零 → `list_empty()` 判为“在 lingering 链表上”
→ `list_del_init()` 去写 `NULL->prev`，就是上面那条 `str x0,[x1,#8]`。

修法（照 `drivers/gpu/drm/bridge/simple-bridge.c`）：

```c
ctx = devm_drm_bridge_alloc(dev, struct psa8331, bridge, &psa8331_bridge_funcs);
...
ctx->bridge.of_node = dev->of_node;
ret = devm_drm_bridge_add(dev, &ctx->bridge);
```

教训：**树里已有桥驱动就是最好的 API 参考**；`drm_bridge` 的分配/引用计数在 7.x 改过，别照 6.x 的老写法抄。

### 发现 2：四根 GPIO 拿不到，三处都是我们自己的 dts 写错

| GPIO | 功能 | 报错 | 真因 | 原厂证据 |
|---|---|---|---|---|
| 40 | `edp-aux-port-sel` | -EINVAL | 板级 dts 写了 `gpio-reserved-ranges = <40 4>`，把 40~43 声明为保留 | 原厂 FDT **没有** `gpio-reserved-ranges` |
| 42 | `edp-mux-pwr-en` | -EINVAL | 同上 | 同上 |
| 12 | `mipi-to-edp-en` | -EINVAL | 我们启用了 i2c5（994000），主线默认引脚就是 gpio12/13 | 原厂 `qupv3_se5_i2c` 是 `status = "disabled"` |
| 49 | `boost-nbyp-en` | -EPROBE_DEFER | `audio_1p8_supply` 这个 regulator 的 `gpio = <&tlmm 49>` 抢了同一根 | 原厂 FDT 里**没有** cs35l45 相关节点（我们是从别的板子抄来的） |

修完这三处后：`probe: start / regulators ok / gpios ok / PSA8331 eDP bridge registered`，一条 warning 都没有。

教训：**-EINVAL 不是“引脚被占”**。真占用的表现是 pinctrl 打
`pin GPIO_12 already requested by 994000.i2c` 并返回 -EBUSY/-EINVAL；而 40/42 是
`gpiochip_line_is_valid()` 判定为保留区 → 也是 -EINVAL。两者要分开：查
`/sys/kernel/debug/pinctrl/*/pinmux-pins`（UNCLAIMED vs 被占）+ 直接看 dtb 里有没有
`gpio-reserved-ranges`。

### 发现 3：`insmod msm` 后整机复位（不是黑屏，是 SoC 掉了）

`insmod msm.ko` 返回 0，随后设备从 NCM 上消失，主机侧看到它**落回 fastboot**（`dtbo_a` 是空的）。
10-30 那次 msm 单独加载只报 `dsi_err_worker: status=5` 不崩，所以这次多出来的唯一变量是**桥开始参与 DRM**。

### 发现 4：分步上电把范围压到 stage 2

给桥驱动加了 sysfs 开关 `/sys/bus/i2c/devices/1-0033/power_stage`，每步之间宿主侧 ping 一次：

| 步 | 动作 | 结果 |
|---|---|---|
| insmod | 只取资源、注册 bridge | 存活 |
| 1 | 面板轨 + io-mux 组 + boost + bl-pwr-en（不含背光使能） | **存活** |
| 2 | 桥上电（1p2 / 1p8 / refclk / `mipi-to-edp-en` / 1p5） | **掉线 → 整机复位** |
| 3 | 等 power-good + TPS1 + 四条 mux 写 | 未测到（上一步就崩了） |
| 4 | `bl-en` | 未测到 |

下一步就是把它再拆细：stage 2 已细分为 21..25 五个子步（1p2 / 1p8 / refclk / `mipi-to-edp-en` / 1p5），
镜像 `8c52a965` 已含此开关，一次上机即可逐条试出是哪一根动作把机器打掉的。

已排除的假设（有据）：
- **不是电压值抄错**：厂商 `turn_on_dsi86()` 同样是 1p8=1.8V、1p2=1.2V、先 1p2 → `udelay(100)` → 1p8 → refclk → `mdelay(1)` → `mipi-to-edp-en`，与我们一致。
- **不是共用轨被带崩**：`vreg_l3c_0p8`（vdd-1p2）在本板只有桥一个消费者（`grep` 全 qcom 目录确认）。
- **不是 GPIO 极性写反**（io-mux 六根低有效，按厂商 `value = on ? 0 : 1` 用 `GPIO_ACTIVE_LOW` 表达）。
- 待查：`vdd-1p5` 厂商设 **1.5V** 且在更早的阶段就打开，我们映射到 `vreg_l7a_1p7` 且**没设电压**、放在最后一步。

### 09:41–10:10 那段"失联"的真相：设备一直活着，是 USB gadget 没起来

09:41 那次 `fastboot boot`（镜像 `8c52a965`）之后**没有任何 USB 枚举、屏幕全黑、按键无反应**。
当时先后给过两个解释，**两个都错**，记录在此以免后人再被带偏：

| 尝试过的解释 | 为什么错 |
|---|---|
| 电量耗尽（59% 撑不住 40 分钟 fastboot） | **算一下就死**：电池 10 000 mAh，59% ≈ 5 900 mAh；fastboot 亮屏 1–2 W，40 分钟掉不到 1 000 mAh。实测 09:05→10:11 共 66 分钟（含多次引导与 fastboot 常亮），电量只从 **59% 掉到 55%** |
| SoC/PMIC 挂死锁存（照 10-30 那次推） | **设备没挂**：探针 init 里的 `sleep 1800; reboot -f` 在 **10:09:56 准时执行**，设备自己重启落回 fastboot —— 一个挂死的 SoC 不会按时重启 |

真实情况：**Linux 起来了，但 USB gadget 没有枚举**，所以主机侧什么都看不到。
`tools/catch-fastboot.sh`（09:57 挂上、每 5 秒轮询）在 10:09:56 抓到设备，校验哈希后刷回 `dtbo_a`
（`ada89e08…`）→ `fastboot reboot` → **Android 正常启动，10:11 电量 55%**。日志：
`private/logs/20261031-0957-catch-fastboot/flash.log`。

两个能直接复用的结论：

1. **"无 USB 枚举" 推不出 "设备挂了"。** 探针系统必须带自动重启定时器，它就是这种情况下的兜底：
   等一个定时周期，设备会自己进 fastboot。判断"挂死"之前，先等过这个周期。
2. **上一轮 `stage 2` 复位之后，紧接着的那次 Linux 引导是"盲"的**（USB 起不来）。
   同样的内核 `527f64d7` + dtb `f99a6d45` + 同一批基础模块，在 09:26 那次是正常的，
   唯一差别是它承接了一次 stage 2 造成的硬复位 —— 疑似复位把 USB PHY/VBUS 会话状态留脏了，
   再走一次 ABL 周期（10:08 那次重启）就恢复。所以：**崩溃后的下一次引导不能作为判据**，
   要再做一次干净上电/重启。



---

## 从 Android 侧取到的供电真值（2026-10-31 10:20，root 可用后）

思路：原厂系统把这个桥驱动得好好的，所以它下面的 regulator/GPIO 状态就是**本机的 ground truth**。
比在 Linux 里盲试安全得多。方法：`adb shell su -c …` 读 `/sys/class/regulator/*/{name,microvolts,state,num_users}`。

### 四个 supply 句柄解析（原厂 FDT，逐个核过）

| 桥的属性 | 原厂 phandle | 实际节点 | 厂商实际电压 | 我们的 dts（旧） | 结论 |
|---|---|---|---|---|---|
| `vdd-1p2-supply` | `0x386` | `pm8150a_l3` | **1.200 V** | `vreg_l3c_0p8`，驱动设 1.2V | ✓ 一致 |
| `vdd-1p5-supply` | `0x37c` | `pm8150_l7` | **1.500 V** | `vreg_l7a_1p7`，上下限 **1.704–1.8 V**，驱动不设电压 | ✗ **超压 13–20%** |
| `vdd-1p8-supply` | `0xae` | `pm8150_l15` | 1.800 V | `vreg_l15a_1p8` | ✓ |
| `vdd-3p3-supply` | `0x7f` | `pm8150a_l11` | 3.300 V | `vreg_l11c_3p3` | ✓ |

### 熄屏 → 亮屏的差分（关键实验）

| 时刻 | l7(1p5) | l15(1p8) | a_l3(1p2) | a_l11(3p3) |
|---|---|---|---|---|
| 亮屏且一直亮着（BL 点亮的） | disabled / 0 users | disabled / 0 | disabled / 0 | enabled / 1 |
| 熄屏后 | 同上 | 同上 | 同上 | 同上 |
| **再亮屏后** | **enabled / 1 user** | **enabled / 1** | **enabled / 1** | enabled / 1 |

两条结论：

1. **桥是这四路电各自的唯一消费者**（每次亮屏后 `num_users` 都是 1）→ 我们 dts 的映射没错，
   也说明"某路电被别的子系统共用"这个假设可以彻底排除。
2. **同一次会话里厂商反复熄屏/亮屏都没崩** → "给桥断电再上电"本身是安全的。
   所以 stage 2 打掉整机的原因不是"上电"这个动作，而是**我们的参数/时序不对**，其中唯一已证实的偏差就是 1p5 超压。

### 据此改了三个地方（镜像 `3ab3e2a1`，dtb `cc1d1a62`）

1. dts：`vreg_l7a_1p7` → **`vreg_l7a_1p5`**，上下限改成 **1.5–1.8 V**（原来 1.704–1.8 V 让 `set_voltage(1.5V)` 直接失败）。
2. 驱动：`turn_on_dp_mux` 语义 —— **1p5（1.5 V）与 3v3 一起在 stage 1 打开**，不再放在 stage 2 最后。
3. stage 2 收窄成厂商 `turn_on_dsi86()` 的四步：`21 = 1p2`、`22 = 1p8`、`23 = refclk`、`24 = mipi-to-edp-en`。

### 10:26–10:35 分步上机的两条新发现

1. **`set_voltage(1.5V, 1.5V)` 在这个 PMIC 上必然失败**：
   `vreg_l7a_1p5: unsupportable voltage range: 1504000-1500000uV` —— 电压步进让 1500000 不可达，
   下界被向上取整到 1504000，于是 min>max，调用失败，**这路电实际仍是默认电压**。
   改成给窗口 `set_voltage(1.5V, 1.6V)` 让 core 自己选最近的档位（镜像 `f018cfcb`）。
   教训：`set_voltage(min, max)` 传同一个值时，任何有步进量化的 PMIC 都可能直接失败，且失败被静默忽略后就"看起来设过了"。
2. **盲引导与"重启时桥还带着电"有关**：上一轮结束时 stage 1 已经把 3v3/1p5 打开，
   直接 SysRq 重启 → 下一次 Linux 引导 USB gadget 又不枚举（与前一次 stage 2 复位后一模一样）。
   → **规矩**：动过桥上电之后要重启，先 `echo 0 > power_stage` 把电全关掉再重启；
   而且判断结果必须在**干净的那一轮**（ABL 走一遍之后）做。

---

## 10:26–10:50 分步上机：桥上电全过，背光亮了（2026-10-31）

在干净的一轮（ABL 走过一遍之后）里，用 sysfs 分步开关一步一步上电，每步之间从主机动一次 ping：

| 步 | 动作 | 结果 | 备注 |
|---|---|---|---|
| — | `insmod psa8331.ko` | **存活** | probe 三行、零 warning |
| 1 | 面板轨（bob/3v3/load-switch/edp-mux-pwr/io-mux/boost/bl-pwr）+ **1p5@1.504V** | **存活** | 1p5 电压这次被接受（`1p5 rail on`，无 unsupportable 报错） |
| 21 | 1p2 = 1.2 V + enable | **存活** | 上次同一步是整机复位 |
| 22 | 1p8 = 1.8 V + enable | **存活** | |
| 23 | refclk（`RPMH_LN_BB_CLK2`） | **存活** | 顺带证明这个时钟映射不会炸 |
| 24 | `mipi-to-edp-en` = 1 | **存活** | 桥芯片真正上电 |
| 3 | 等 `pwr-status`=0 + TPS1(`0x96=0x02`) | **存活** | **没有** "power-good never asserted" → 桥自报电已就绪；I²C 无报错 |
| — | `insmod msm.ko` | **存活**（USB 也没掉） | `fb0=msmdrmfb`、两条 DSI 都 `DSI_CTRL=0x1f1`、`bridge powered and set to MIPI->eDP` |

**两个关键正面结果**

1. **`dsi_err_worker: status=5` 停了**：insmod msm 时集中出现 10 条，之后 5 秒内计数 10→10 不再增长。
   没有桥的时候这个错误是持续刷屏的 → **DSI 侧确实有了接收方**。
2. **背光亮了**（用户现场确认：背光已亮、无像素内容）。这组 GPIO/轨序（含 `bl-pwr-en`、`bl-en`）
   第一次由我们自己的驱动点亮面板。

所以缺口被精确地压到最后一环：**eDP 侧没有配置** —— 桥把 DSI 收下了，但 eDP 输出端没有初始化，
面板拿不到有效像素流，表现为"点亮但空白"。

### 已实现：厂商 eDP 初始化表（52 条，不是文档先前说的 106 条）

- 从 `权威数据/vendor-dsi/dsi86-edp-mux.c` 的 `dsi86_init[]` **机械提取**，Python 逐行正则解析，
  共 **52 条**（`{reg, {val}, delay}`，`delay` 字段厂商循环并未使用，仅作元数据保留）。
  ⚠ 之前文档里"106 条"是估算，已按源码订正。
- 语义按厂商循环实现：`0x0D` 之后等 10 ms 并轮询 `0x0A == 0x82`（DP PLL lock）；
  `0x96` 之后等 20 ms 并轮询 `0x96 == 0x01`（链路训练完成）；失败只 `dev_warn` 不中断。
- stage 重排：`3 = 等 power-good + TPS1`、**`5 = eDP 初始化表`**、`6 = 四条 mux 写`，
  `pre_enable` 按厂商顺序 1→2→3→5→6 执行。
- 镜像 `451336e2`（已核过 ramdisk 里的 `psa8331.ko` 确实含新表），**尚未在真机上跑到**。

### 现场观察（用户口述，2026-10-31 10:49）：它其实闪过 4–5 只企鹅

> 「上一轮有 4、5 只 Linux 小企鹅一瞬间闪过，然后没了。」

这 4–5 只企鹅是 **内核的 Tux logo**（`CONFIG_LOGO`，由 fbcon 在 framebuffer 注册时画进 fb0，
数量随控制台宽度而变）。它一闪而过，说明两件事：

1. **像素真的到了玻璃**：DSI → PSA8331 → eDP → 面板这一整条链**曾经工作过一次**，扫描输出也跑了。
   → 所以缺口不是"链路不通"，而是**图像没有被保持住**。这一条把问题从"没接通"重新定级为"接通后掉了"。
2. **msm 的 fbdev 是活的**：logo 是画进 `msmdrmfb` 的，说明 msm 的 CRTC/DSI 当时在正常扫描。

"闪一下就没"最常见的三种解释（下一轮按顺序排除）：
- fbcon 画完 logo 后，DRM 的后续 commit 用黑帧覆盖（fbdev 模拟的 blank / 首次 dirtyfb）；
- 面板在 video mode 下需要持续供帧，pipe 被关掉后立刻就黑（背光不受影响，所以"亮而空"）；
- eDP 侧未训练完成，只偶然透了几帧（这正是 `451336e2` 里那 52 条表要解决的）。

**下一轮判据改成"能不能持住一帧"**：`insmod msm` 之后，用脚本**持续**往 `/dev/fb0` 写噪声
（例如 `for i in $(seq 1 40); do dd if=/dev/urandom of=/dev/fb0 bs=4096 count=64 2>/dev/null; done`），
让用户观察是"一直有噪点"还是"只闪一下"。同时看 dmesg 里两条训练轮询是否成功
（`0x0A == 0x82`、`0x96 == 0x01`）。这比"有没有画面"这种二值判断信息量大得多。

### 遗留

- 最后一次 `fastboot boot 451336e2` 因会话陈旧报 `Command failed`，随后设备进入"零 USB 枚举"状态
  （今天已出现三次，均与"上一次会话结束时桥还带着电"或失败引导有关）。按新规矩：
  主机侧等一个周期/让用户做一次电源周期，再在**干净轮**里验证 eDP 表。
