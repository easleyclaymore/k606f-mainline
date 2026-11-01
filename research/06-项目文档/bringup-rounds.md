# 上机轮次表（B*）

> 目的：**每一轮上机有一个编号**，用户可以用编号精确指认"哪一轮看到过什么"，
> agent 也能在文档/对话里引用同一件事。
> 规则：每次 `fastboot boot` 之前先报编号和时刻；每轮结束在同一编号下补结果与日志路径。

## 编号规则

- 前缀 **B**＝显示 B 阶段（eDP 桥）上机轮次；更早的轮次不回溯编号。
- 一次 `fastboot boot` = 一轮。**同一镜像重启后再 boot 一次也算新一轮**
  （因为"崩溃后紧接的那次引导不可信"，两轮的环境状态不同）。
- 编号只在 `fastboot boot` 真正返回 OKAY 时占用；`Load Error` 之类的失败也算一轮，
  但结果标为「未启动」——它同样改变了设备状态（零枚举往往就是失败引导引起的）。
- 每轮记录：编号 / 起始时刻 / 镜像哈希（前 8 位）/ 做了什么 / 结果 / 日志目录。

### 字段口径（避免自欺）

- **做了什么**：只写这一轮相对上一轮**改变了哪一个变量**。一次改两个变量的轮次无效，不能当判据。
- **结果**：只写机器能复核的事实（返回码、寄存器值、错误计数、fastboot 出现时刻）。
- **人眼观察单独记**（见文末汇总），两者混在一起最容易得出"看起来好了"的错误结论。
- **「零枚举 / 盲引导」**都表示"主机侧看不到设备"，**不等于设备挂了**；判定看 fastboot 出现的时刻：
  接近探针定时点＝系统活着只是通道死了，秒级＝真崩。
- **判定用词**：`存活`（每步后主机动过 ping，或到点准时回 fastboot）、`未启动`（`Load Error` 之类）、
  `真机通过`（有人眼或硬件正证据）、`不可判`（工具没把动作真正做出去）。

## 2026-10-31（全部轮次，按时间顺序）

| 编号 | 时刻 | 镜像 | 做了什么 | 结果 | 日志 |
|---|---|---|---|---|---|
| **B1** | 09:09 | `fedfe168` | `insmod psa8331.ko`（旧 API 写法） | `drm_bridge_add` oops，`rc=139`；**设备存活** | `private/logs/20261031-0910/01-*` |
| **B2** | 09:26 | `8aefeb13` | dts 引脚仲裁后分步上电 | probe 干净、16 根线全到手；stage 1 存活；**stage 2 整机复位** | `private/logs/20261031-0910/02–08-*` |
| **B3** | 09:31 | `fd2cd341` | `insmod msm` | msm.ko 是旧内核编的（vermagic 不匹配），未真正跑到 | `…/09-insmod-msm.txt` |
| **B4** | 09:38 | `8c52a965` | 重新引导（承接 B2 的复位） | **盲引导**：零 USB 枚举，系统活着，10:09:56 定时器重启 | — |
| **B5** | 10:26 | `3ab3e2a1` | 首次带 1p5 修正的 stage 1 | 暴露 `unsupportable voltage range: 1504000-1500000`，仅跑到 stage 1 | `…/1026-staged/03-*` |
| **B6** | 10:31 | `f018cfcb` | 重启后再引导 | **盲引导**（承接 B5 时桥上电状态） | — |
| **B7** | 10:37 | `f018cfcb` | 干净轮：分步 1→21→22→23→24→3 全部存活，再 `insmod msm` | **背光常亮 + 闪过 4–5 只企鹅**（用户现场观察）→ 像素到过玻璃 | `…/1026-staged/04–10-*` |
| **B8** | 10:46 | `451336e2` | 启动（含 52 条 eDP 表） | `Load Error`，**未启动**；随后设备零枚举 | — |
| **B9** | 10:52 | `451336e2` | 电源周期后重新启动 | 启动 OKAY 但**零 USB 枚举**；10:58 定时器带回 fastboot | — |
| **B10** | 10:58 | `451336e2` | 分步上电 + 写入 52 条 eDP 表 | 全程存活；**用户看到 Tux 再次闪现**（此时 msm 未加载！）→ 视频流来自 ABL；eDP 表写入后 **`0x0A=0x02`（PLL 未锁）、`0x96=0x00`（训练未完成）** | `private/logs/20261031-1058-B10/` |
| **B11** | 11:04 | `6500132a` | 按厂商顺序 `1→2→3→6→5` 手工分步 | 顺序改了，训练**仍然失败**（同样 0x02 / 0x00）→ 顺序不是主因 | `…/1104-B11/01-*` |
| **B12** | 11:07 | `6500132a` | 只加载 msm，让 `pre_enable` 全权执行 | 桥在 DSI 之后才上电 → **`dsi_err_worker` 持续刷屏**（与 B7 相反）；训练仍失败 | `…/1104-B11/02-*` |
| **B13** | 11:11 | `6500132a` | 计划"先上电再让 DSI 起来" | **未跑成**：SysRq 重启后设备再次零枚举 | — |
| **B14** | 11:15 | `6500132a` | 先手工上电（1/2/3）再 `insmod msm`，DSI 起来后再训练 | `dsi_err` 仍增长（10→21），训练仍 `0x02`/`0x00` → **"先上电"假设不成立**；同时排除 refclk（`ln_bb_clk2` = 19.2 MHz，`prepare/enable = 2/2`，绑定在本设备 `refclk`） | `private/logs/20261031-1115-B14/` |
| **B15** | 11:22 | `69f5ab20` | **把 eDP 训练从 `pre_enable` 挪到 `enable()`**（面板已出复位之后） | `msm` 加载后 USB 掉线，**无日志**；用户未观察 → 结果未知 | `…/1122-B15/`（只有断线记录） |
| **B16** | 11:30 | `f4bbc69d` | 训练在 `enable()` 里（面板出复位后） | **仍失败**：`0x0A=0x02`、`0x96=0x00`；Tux 又闪一次、背光常亮 → "面板出复位"也不是充分条件 | `private/logs/20261031-1130-B16/` |
| **B17** | 11:40 | `0c4cc017` | 分步上电后用 `i2cdump8` dump 全量寄存器 | 成功拿到 256 字节；与 Android/BL 快照 diff **只差 9 个** | `private/logs/20261031-1140-B17/` |
| **B18** | 11:42 | `af33eaa3` | 加载 msm 后准备写 BL 配置 | `msm` 加载即掉 USB，**未执行到写寄存器** | `…/1142-B18/` |
| **B19** | 待跑 | `69b36617` | **脚本化**：`k606f.blmode=1`，自动 insmod 桥 → msm → 写 BL 那 9 个值 → 停 3 分钟 → 重启 | 待观察：**屏幕是否出图像** | — |
| **B20** | 11:58 | `86b9326a` | `blmode=2`：不加载 msm，只上电 + 写 BL 那 9 个值 | 全程无 USB 枚举（盲引导）；12:09:08 定时器（600 s，应 12:08:21）带回 fastboot → **探针一直活着**。用户：**屏幕亮、无像素** | — |
| **B21** | 12:10 | `ac527c9e` | `blmode=1`：桥 + msm + 写 9 个值；**新增持久化日志（写 dtbo_b 第 4 KiB）** | 同样无 USB 枚举；用户：**屏幕亮、无像素**（没看到 Tux 闪）。日志待从 `dtbo_b` 读回 | `private/logs/20261031-1209-B21/` |
| **B22** | 12:22 | `d97364b6` | `blmode=3`：**桥先上电（1/2/3/6）再 `insmod msm`** | 桥开始接收 DSI —— `dsi_err_worker` 在三次快照里都停在 **10 不再增长**；训练仍 `0x0A=0x02`/`0x96=0x00`；用户：背光亮、无像素 | `private/logs/20261031-1222-B22/` |
| **B23** | 12:34 | `f1134c3c` | 同 B22，但快照改成 **整 256 字节全量** | **与 Android 工作态逐字节 0 差异** → 桥的配置完全正确；`dsi_err` 仍 10 | `private/logs/20261031-1235-B23/` |
| **B24** | 12:43 | `1963a310` | `blmode=3` + 往 `/dev/fb0` 写整屏白色（16 MB 一次写） | **写入失败** `rc=1`（"not in virtual address space"），所以本轮"屏幕无变化"**不算判据** | `private/logs/20261031-1243-B24/` |
| **B25** | 12:54 | `ffdba22d` | `blmode=3` + `drmpaint` 直接 modeset | **两个工具问题**：① DRM 卡是 **`card1`**（`card0` 被 simpledrm 占了），脚本打的 `card0` 不存在 → 没画；② `/dev/fb0` 小批量写入 **`rc=0` 成功**。寄存器全图仍与工作态一致 | `private/logs/20261031-1254-B25/` |
| **B26** | 15:31 | `6e28b130` | `dmesg -n 1` + 解绑 fbcon + `/dev/fb0` 整行对齐分块写（每块 7 行 = 60928 B）+ 每秒重绘 | **软件侧全部成功**：`vtcon1 bind 1→0`、`open rc=3`、193 块 `ret=60928` 全成功、`drcpaint 诊断 rc=0`。**但 `fb0/blank=4`（POWERDOWN）→ 管道是关的**；hands-on 解 blank 到 0 后仍无图像。`dsi_err_worker` 洪流：**5 秒 189,339 次 `status=5`**，随后 `status=4`。USB **首次在 msm 之后仍可用**（telnet 全程在线） | `private/logs/20261031-1531-B26/` |
| **B27** | 15:40 | `c96a503e` | 改成**DRM 优先上屏**（card1 + dumb buffer + SETCRTC），fbdev 作退路 | `card1 fd=3`、`connector=36 crtc=91 mode=2160x1350` 全部找对，但 **`SETCRTC: Invalid argument`**；退回 fbdev 后 `unblank rc=0`、blank 4→0、193 块写成功 `rc=0`。仍无图像 | `private/logs/20261031-1540-B27/` |
| **B28** | 15:50 | `53a45ca4` | 修 `drm_mode_crtc.mode_valid=1`（B27 的 EINVAL 根因）+ 逐个 CRTC 兜底 | **`SETCRTC crtc=91 成功`、`已上屏 pattern=bars 2160x1350`**；`dsi_err_worker` 全程停在 10 不再增长（对比 B26 的 5 秒 19 万次洪流）→ **DSI 稳定、桥在收**。桥寄存器=工作态那组（0a:00 10:0a 11:82）。**但 `fb0/blank=4`（POWERDOWN）、`0x96=0x00`（链路未训练）**，屏仍只亮无像素 | `private/logs/20261031-1550-B28/` |
| **B32** | 16:57 | `2039776d` | 修 bonded 双 DSI 水平时序（porch 翻倍）+ `timing` 寄存器 dump | **时序完全对齐厂商**：`TOTAL=0x05560463`(htotal=1124)、`ACTIVE_HSYNC=0x00100000`(hpw=16)、`byte_clk=69142492`(69.14MHz)。**但 `dsi_err_worker: status=5` 仍在刷**（10→43→每5秒一批）→ **时序不是根因**，重要负结果 | `private/logs/20261031-1657-B32/` |
| **B39** | 20:55 | `6a6a7a82` | **修 DSI86 地址 bug**：59 条 eDP 表 + TPS1 从 0x33(mux) 改写到 0x2C(DSI86)，mux 的 4 条留 0x33 | **DSI86 首次配置成功**：`0x2C` 寄存器 `0x0A=0x82`(DP PLL 锁)、`0x96=0x01`(链路训练完成)，与 Android 工作态一致。**但 status=5 仍在刷**（10→22→34→46）→ 地址 bug 是独立问题，DSI 流不被桥接收是另一回事 | `private/logs/20261031-2100-B39/` |
| **B40** | 21:16 | `9b382fa3` | 修 PHY timing（clk_trail/clk_pre 各 -1），但提交内核后没同步模块 | **无效轮**：`insmod: invalid module format`（vermagic `1137137d9` vs `178a0b3c6`），msm/psa8331 都没加载，`dsi_err=0` 是假象 | `private/logs/20261031-2116-B40/` |
| **B41** | 21:25 | `94f6cc9b` | 同步模块后重测 clk_trail/clk_pre 修正 | **负结果**：寄存器确认 clk_trail=5、clk_pre=18 已与厂商一致，但 **status=5 仍在刷**（58→142→154）→ **PHY timing 不是根因** | `private/logs/20261031-2125-B41/`（⚠ 日志目录缺失，数据以文档记录为准） |
| **B42** | 21:45 | `b2bc285f` | 显式设置 HS_TIMER_CTRL=0x4ea60（对齐 ABL，mainline 从不设此寄存器） | **status=5 → status=4**：HS_TX_TIMEOUT 消除（DSI_ERR_STATE_TIMEOUT 位不再置位），但 FIFO overflow/underflow 仍在刷 → 部分正结果，待确认是否只是掩盖超时 | `private/logs/20261031-2145-B42/` |
| **B43** | 21:57 | `3a39a494` | byte_intf_clk 不除 2（实验 32 位接口全速） | **负结果**：FIFO 错误仍在（status=4，10→22→34→46 与 B42 相同）→ byte_intf_clk 速率不是根因，已回滚 | `private/logs/20261031-2157-B43/`（⚠ 日志目录缺失，数据以文档记录为准） |
| **B44** | 22:06 | `6cab2ee3` | 读桥(SN65DSI86) DSI 接收诊断寄存器 0xF0-F7 + 0x12 + 0x20-0x3A | **推翻"桥没收 DSI"假设**：桥**收到了 DSI**（`0x12=0x36` 检测到 DSI 时钟、`0xF0=0x00` 无 SOT/EOT/contention 等 PHY 错误）。但报 `0xF1=0x20`(CHA_INVALID_LENGTH_ERR 视频长度不匹配) + `0xF6=0x40`(LOSS_OF_DP_SYNC_LOCK_ERR DP 同步锁丢失)。桥配置 dual-DSI Left/Right（CHA=CHB=1080），时序寄存器 0x20-0x3A 与 msm 时序逐项一致 → 待查为何长度不匹配 | `private/logs/20261031-2206-B44/`（⚠ 日志目录缺失，数据以文档记录为准） |
| **B45** | 23:02 | `dd7a4c93` | **去掉 `MIPI_DSI_MODE_NO_EOT_PACKET`**（内核 `33840ee73`，ABL 工作态 EOT_PACKET_CTRL=1/msm=0）；探针加清 0xF4-F7 | **大部分正结果**：EOT 位两控制器=1（闭环）；清粘滞后 **CHA 的 INVALID_LENGTH 消失**（0xF1=0x00，Android 健康态=0x00 ✓）、**0xF6 DP 同步锁干净**（0x00 ✓）；**但 CHB 在 +3s 重锁一次**（0xF3=0x20）；dsi_err 洪流 29.1s 后静默 ≥22s（B42 是持续到采样结束）。**另确认：此前的"Android 工作态对比"有灭屏态污染**（turn_off 序列状态），已在亮屏下重抓真值 | `private/logs/20261031-2302-B45/` |
| **B46** | 23:20 | `1fe5d07e` | 同 B45 内核（零系统变量），探针加 +30s/+60s 延迟重读 | **单链随机错确认**：CHA 持续重锁（f1=0x20 在 +3/+33/+93s 全部非零）、CHB 全程干净（f3=0x00）——与 B45 镜像反转。FIFO 洪流=坏链专属（B45=DSI1、B46=DSI0，坏链与报错通道一一对应），好链全程无错。f0/f2/f4/f5/f6/f7 全程 0x00。**特征=每次引导随机选一条链成为坏链、boot 内持续** → 链间启动竞态（两链 host_power_on 相差 22ms > 1 帧时长） | `private/logs/20261031-2345-B46/` |
| **B47** | 23:36 | `95bd8126` | 同 B46 内核，探针加 AUX/DPCD 读数；**人眼验收=亮屏无彩条** | 两链都报 INVALID_LENGTH（f1=f3=0x20）但 **DPCD 0x200=0x81 全程**（TCON 收到稳定同步）+ DP 同步锁干净 → **DP 侧一直有输出，只是像素内容坏**。AUX 通道入探针 | `private/logs/20261031-2358-B47/` |
| **B48** | 23:53 | `d7bace72` | 探针加 DPU INTF/PP/LM dump（ABL 期+msm 期） | DPU INTF 时序与 ABL 逐位一致（半幅 1080 ✓、双 timing engine 都在跑）；VID_CFG0=0x9130 一致。**msm 独有 INTF_CONFIG2=0x10（DATA_HCTL_EN）vs ABL=0x100（bit8 未定名）**；INTF_MUX[19:16] msm=0xf（复位默认）vs ABL=0 | `private/logs/20261031-2350-B48/` |
| **B49** | 00:12 | `3c0c0116` | 探针加 dispcc byte0/pclk0 RCG dump | **时钟域假设死亡**：pclk0/byte0 的父时钟 msm=ABL=dsi0 PLL（SRC=1）；msm 的 M/N=1/255 经 RCG2 公式解码为 mode=0 无效——时钟树与 ABL 等价 | `private/logs/20261101-0000-B49/` |
| **B50** | 00:40 | `cf541c90` | 内核 `0a234b7c1`：bonded 使能序列镜像厂商（DSI 引擎延迟启动 + post_kickoff master-only） | 结果 B（如四句话预判）：CHA 仍报 0x20、DSI0 独自洪流。双引擎 CTRL 启动实测 3µs，仍超 130ns 预算 | `private/logs/20261101-0100-B50/` |
| **B51** | 00:50 | `aecdcc48` | 同 B50 内核；探针加晚期 DPU dump + K606F 事件流 | **决定性**：① 双引擎 CTRL 启动实测 **3µs**；② **INTF2（slave）timing engine 从未启动**（master-only 所致）；③ **SPLIT_DISPLAY_EN=0**——厂商耦合路线对本机无效 | `private/logs/20261101-0110-B51/` |
| **B52** | 01:00 | `08e1b508` | 探针加 split_config 打印 + ABL 期 split/byte1/pclk1 dump | **三个重磅**：① mainline 有写 SPLIT_DISPLAY_EN=1（7.866s）；② **ABL 期 SPLIT_DISPLAY_EN=0——厂商耦合路线 ABL 根本没用**；③ **ABL 的 byte1/pclk1 RCG SRC=1（dsi0 PLL）——两链控制器时钟域统一** | `private/logs/20261101-0130-B52/` |
| **B53** | 01:09 | `a54b6807` | 内核 `378e9694e`：dsi1 byte/pclk 改挂 dsi0 PLL（照抄 ABL） | 时钟域统一生效（msm 四个 RCG 与 ABL 完全一致）但失败模式不变（本次 f1=f3=0x20）——**排除跨 PLL 频漂是根因** | `private/logs/20261101-0140-B53/` |
| **B54** | 01:19 | `6be8961d` | 内核 `05040a82c`：回退 post_kickoff master-only（B51 证实 slave INTF 未启动；ABL=双引擎） | 探针正常；收尾后设备 USB gadget 卡死 5h（枚举 17ef:7cdc Lenovo VID——⚠ 别按 05c6/18d1 过滤 lsusb）。**07:20 补读：双 INTF 引擎确认都在跑、f1=f3=0x20、dsi_err 130/274——与 B47 同** | `private/logs/20261101-0200-B54/` |
| **B55** | 07:19 | `a6c93a93` | 内核 `e3391aab7`：engine_start 亚µs 化 + timing 打印补 pclk/byte_intf | f1=0x20/f3=0x00（随机坏链依旧）。**关键数据：byte_clk=69,142,492 / pclk=92,189,991 / byte_intf=34,571,246——pclk/byte 比值精确 4/3，速率失配假设排除** | `private/logs/20261101-0720-B55/` |
| **B56** | 07:34 | `0ac1aabf` | 内核 `800d81d92`：单链 bisect（dts 去 bonded+面板单链时序+桥单通道） | **70s 崩回 fastboot**（SETCRTC 时段），无现场（当时无 ramoops/pstore）；dtbo_b 仍是 B55 旧内容 | `private/logs/20261101-0810-B56/` |
| **B57** | 07:40 | `a520ec71` | 同 B56 内核 + ramoops（1MB@0xB0000000）+ CONFIG_PSTORE_RAM | **40s 崩**，pstore 空——ramoops 驱动未生效（当时 PSTORE_FS 缺失） | `private/logs/20261101-0840-B57/` |
| **B58** | 07:43 | `61a2fb25` | 同内核；探针加增量 flush（边跑边写 dtbo_b） | 40s 崩；增量 flush 未生效（原因见 B59/B60 分析）；dtbo_b 仍为旧内容 | `private/logs/20261101-0900-B58/` |
| **B59** | 07:47 | `0a1102bf` | 同内核；探针加 pstore 读取 + flush 加密；双 boot 协议（第一次崩→第二次读 pstore） | 第一次 65s 崩；第二次探针完整跑完但 **pstore 读为空**——发现 **CONFIG_PSTORE_FS 缺失**（ramoops 只采集不可读） | `private/logs/20261101-0930-B59/` |
| **B60** | 07:47 | `db5e886a` | config 补 `CONFIG_PSTORE_FS=y`；双 boot 协议 | **增量 flush 首次生效**（`K606FLOG1 inc`@内核 7s）；**崩点钉死：insmod msm 的探测内（dpu/dsi 单链初始化，kernel ~36.7s）**，dmesg 断在 dpu/dsi 依赖解析处。pstore 仍空（ramoops 驱动未 probe，原因待查：根级 ramoops 节点未绑定？） | `private/logs/20261101-1010-B60/` |
| **B61** | 10:06 | `7348438d` | ramoops 改规范形态（reg 在节点上，驱动要 IORESOURCE_MEM——memory-region 方式必 -EINVAL） | **ramoops 首次 probe 成功**（`pstore: Registered ramoops`）；双 boot 读 pstore 仍空——第一次 boot 的 panic 没写进区域（0xB0000000 疑被 ABL 踩） | `private/logs/20261101-1030-B61/` |
| **B62** | 10:18 | `4756e537` | ramoops 挪 0x90000000 + 后台 flush 循环（每 2s） | 0x90000000 **与 memory@8C700000（0x8C700000-0x90D00000）重叠**——选址失败；"error in header"=区域被踩。最后一次 flush 的 dmesg 止于 36.7s | `private/logs/20261101-1130-B62/` |
| **B63** | 10:24 | `f2091453` | ramoops 挪 0x94000000（0x90D00000-0x9C000000 空洞内）+ k606f.panic=1 sysrq 自检 | 两 boot 都无输出——**发现两个 bug**：panic 块调用了未定义的 flush（定义在其后）；MAGIC_SYSRQ_DEFAULT_ENABLE=0x1 不含 'c' | `private/logs/20261101-1140-B63/` |
| **B64** | 10:32 | `e2dd4c41` | flush() 定义提前 + `echo 1 > /proc/sys/kernel/sysrq` 掩码全开 | **捕获链自检通过**：sysrq panic 生效、增量 flush 落盘（inc 头+自检标记+dmesg 尾）；但 pstore 段仍空=第一次 boot 的 panic 文本未持久化（区域在两次 boot 间被 ABL/收尾踩） | `private/logs/20261101-1140-B64/` |
| **B65** | 10:38 | `dd18aa66` | flush 收紧 0.3s；单链真实崩溃（无自检） | **定性反转：不是崩溃是静默挂死**——最后 300 行 dmesg 静止于 36.73s（dpu/dsi 依赖解析后、[K606F] dsi_bind 从未出现、零报错），软锁死检测器随后 panic 但控制台被挂死方持锁。挂死点=单链拓扑的 display probe 内 | `private/logs/20261101-1150-B65/` |
| **B66** | 11:01 | `6f51ff00` | 单链挂死取证（K606F 打点+hung panic 化+ramoops@0x1F0000000） | **无效轮（重蹈 B40）**：config 变更后未同步模块，第二次 boot `insmod: invalid module format`、桥 rc=8。教训升级：**孤儿构建跨 commit 会造成 Image 与模块 vermagic 不一致**，重建后必须 modinfo 核对 | `private/logs/20261101-1150-B66/` |
| **B67** | 11:17 | `9cb7fbf7` | vermagic 修复后重跑（Image=模块=g7e5974f6d） | 两次 boot 都挂死+panic（时间线吻合）。**进一步收窄**：[K606F] 打点（含新加的 `bridge_power_on: phy_enable 开始`）全部未出现，但 psa8331 的 pre_enable 段（stage 21-24@33.67s）正常跑完——**挂死点收窄到 psa8331.pre_enable 返回之后、dsi0 的 power_on/phy_enable 之前**（DRM 桥链的 panel/panel-bridge 一段） | `private/logs/20261101-1150-B67/` |
| **B68** | 11:35 | `ad53e0f7` | 挂死窗口打点（panel prepare/psa8331 pre_enable 头尾）+ 撤销 panic 化 | 读回仍是旧内容+零 [K606F]——当轮取证未达预期（后被 B69/B70 的发现重新解释：部分"早崩"是脚本对 fastboot 的幻影 sightings+过早收尾） | `private/logs/20261101-1200-B68/` |
| **B69** | 11:51 | `764b1880` | flush dmesg tail 300→2000 行（全 boot 回放） | **脚本 $H 变量未展开，boot 失败**（空路径）——但手动重boot后读到全 boot dmesg（1359 行） | `private/logs/20261101-1210-B69/` |
| **B70** | 12:04 | `5e462a61` | ramoops@0x1F0000000 + 打点齐全；单链完整跑 | **bisect 裁决出炉**：单链内核**没有挂死**——display probe 在多次 deferred 重试后于 ~113s 完成，DSI0 流水线持续流出 **113→148s+（≥35s）**，FIFO 错误签名与双链完全相同（status=4/0xdddd1019）→ **故障在公共路径（桥/DSI/像素源），不在 dual 专属机制**。此前的"36.7s 挂死/70s 崩"部分是幻影 fastboot sightings+过早收尾的伪象（B65 的静止现场仍待解释） | `private/logs/20261101-1215-B70/` |
| **B71** | 12:25 | `f9ea24c2` | initialize_dsi 入口/循环打点 + panel prepare 内部打点 | 证据作废：读回为 B70 旧档+新档混杂（当时无 img 标签无法自证），且 boot 早退原因不明 | `private/logs/20261101-1250-B71/` |
| **B72** | 12:28 | `5aa6e3d3` | flush 头加 `img=` 自标识（k606f.img= cmdline） | **读回确证为本轮**（inc 头 img=f9ea24c2 ✓）；dmesg 权威时间戳显示探针跑至内核 148.4s、DSI0 洪流持续——**修正 B70 的"公共路径"裁决：那些洪流疑似 ABL 残留流（B10 教训），而非我们的流水线**（绑定阶段打点 initialize_dsi/dsi_bind 全部缺席） | `private/logs/20261101-1300-B72/` |
| **B73** | 12:34 | `da4a0f19` | dpu bind 路径打点（runtime_resume 入口/clk_bulk 前后） | **证据作废（标签 bug）**：打包时 img 标签取的是旧 boot-probe.img 的哈希（5aa6e3d3），无法区分本轮与 B72；读回未变。教训：**img 标签必须在 bringup 之后再取**，且 boot 后必须确认设备先离开 fastboot | `private/logs/20261101-1300-B73/` |
| **B74** | 13:10 | 无镜像（Android 上直接 I2C） | **SN65DSI86 内置彩条测试**（GPT 外部评审建议）：Android 运行中写 0x2C 的 `0x3C=0x10`（COLOR_BAR_EN），`0x5A=0x0C`（VSTREAM 使能中） | **里程碑：屏幕出彩条！！**（用户照片 `colorbar-evidence.jpg`）——桥内置图案绕过 DSI 输入直出 eDP：**eDP 输出侧（桥/训练/mux/面板）全线正常，故障 100% 锁定 DSI 输入侧**。0x3C 已恢复 0x07。GPT 的建议完全正确 | （Android 在线操作，无探针日志；照片为证） |
| **B75** | 12:51 | `cf406f0b` | 真单链（dts 删三个 dual 属性 + dsi1 禁用 + LANE_CTRL 对齐 ABL） | 探针跑满 5.5 分钟无崩溃；`dsi_bind bonded=0` `tiles=1` 全宽 2160（byte_clk=135.6MHz/lane 1084Mbps）；桥 EDP 训练成功（EDP_INIT rc=0）；**桥错误寄存器清后 f0-f7 全 0、DPCD=0x81 稳定同步、dsi_err 停在 10**——**协议层完全正常但屏幕纯黑**（人眼确认） | `private/logs/20261101-1300-B75/` |
| **B76** | 13:35 | `b018c391` | 单链 + DSI 控制器 TPG（k606f.tpg=1，`msm_dsi_manager_tpg_enable()`） | **反而关背光**——比 B75 更退步。DSI 侧 TPG 干扰了桥训练或数据通路，已回退。改用 DPU INTF 层 TPG（B77 计划） | 无独立日志（B72 基础改） |
## 结论锚点

- **B10 的重要修正**：Tux 是在 **msm 还没加载**时闪的 → 视频流是 **ABL 留下的 DSI** 在跑，
  屏黑是因为桥没上电。所以"DSI 通了"这件事早就成立，不需要 msm。
- **B12 的反证**：桥在 DSI 之后上电（由 `pre_enable` 触发）时，`dsi_err_worker` 会持续刷屏；
  而 B7（先手工上电、后加载 msm）是停的 → **上电时机相对 DSI 的先后很可能是关键变量**，B13 就是为验它设计的。
- **`fb0: sys_imageblit/fillrect: framebuffer is not in virtual address space`**：fbcon 根本画不进
  `msmdrmfb`，所以"往 /dev/fb0 写噪声看能否保持"这条判据在当前配置下**不成立**，得换手段。

- **B7 是唯一看到过像素的一轮**（企鹅）。任何"能不能出图"的对照实验都以 B7 为基准。
- B4、B6、B9 三轮都是"启动成功但 USB gadget 不枚举"——**不是挂死**，等定时器即可。
- B2 的整机复位，后续定位为 **1p5 被喂 1.7V（应为 1.504V）**，B7 起已修正。

**B23 的决定性结果**（`private/logs/20261031-1235-B23/`）：256 字节寄存器全图，Android 工作态 vs 我们
**差异 0 个**。→ 桥的配置无懈可击，「调寄存器」这条路彻底关闭。

## 方法升级：寄存器级对照（2026-10-31 11:32 起）

已排除的因素够多了，改为**直接对比"工作时"与"失败时"的桥寄存器**：

- 新工具 `tools/i2cdump8.c`（静态 aarch64），源码在 tools/，产物 `out/probe-bin/i2cdump8`，
  已接入 `tools/build-initramfs.sh`（`out/probe-bin/*` → 镜像 `/bin/`），也可 `adb push` 进 Android。
- **Android 侧的桥在 `/dev/i2c-5`，地址 0x33**（扫了 bus 1/4/5/6/7，只有 5 应答，读 0x00 得 0x00）。
  注意与自制内核的 `/dev/i2c-1` 编号**不同**，别混。
- 镜像 `0c4cc017` 已带该工具，下一轮在自制内核里 dump 同一范围，与 Android 的快照逐字节 diff，
  差异就是缺的那一步（预期能直接看出页面选择 0xFF、0x10/0x11 以及训练相关的位）。

## 新观察：带 `blmode` 的轮次大多没有 USB 枚举

| 轮次 | 镜像 | cmdline | USB 枚举 |
|---|---|---|---|
| B16 | `f4bbc69d` | 无 blmode | ✅ 正常（telnet 全程可用） |
| B17 | `0c4cc017` | 无 blmode | ✅ 正常 |
| B18 | `af33eaa3` | 无 blmode | ✅ 正常（直到 msm 加载才掉） |
| B19 | `69b36617` | `blmode=1` | ❌ 无 |
| B20 | `86b9326a` | `blmode=2` | ❌ 无 |
| B21 | `ac527c9e` | `blmode=1` | ❌ 无 |

**相关性很强，但还不是因果**：B19 那版 init 里脚本块被重复插了两次（技术债 #25），B21 已经去重却仍然没有枚举，
所以"重复"不是原因。剩下的可能是 cmdline 本身、init 里新增语句的环境影响，或者只是这三轮恰好都落在
"上一轮结束时桥带电"的状态。**下一轮要用同一镜像、只去掉 cmdline 里的 `blmode` 做对照**（单变量）。

## 新增能力：持久化日志（不依赖 USB）

`blmode` 跑完后把 `/tmp/blmode.txt` + dmesg 尾部写到 **`dtbo_b` 分区第 4 KiB 起**（`PARTNAME=dtbo_b`
从 `/sys/class/block/*/uevent` 里找；首 4 KiB 不动，分区头不受影响；本地有完整备份）。
读法：恢复 Android 后 `adb shell su -c 'dd if=/dev/block/by-name/dtbo_b bs=4096 skip=1 count=4'`。
读法：恢复 Android 后
`adb shell su -c 'dd if=/dev/block/by-name/dtbo_b bs=4096 skip=1 count=48' | tr -d '\000'`。

## 设备侧新坑：`slot-unbootable`

`fastboot getvar all` 里出现 `slot-unbootable:a:yes` + `slot-retry-count:a:6` 时，
`fastboot reboot` 只会回到 fastboot。修法 `fastboot set_active a` 再重启（2026-10-31 实测有效）。


## B26 设计（判据可信度）：三个必须先修的坑

**镜像 `6e28b130`**（替换掉未跑的 `927e3f8a`）。这三条都是"工具问题会长得像物理结论"的典型，
其中第 1 条在 B7/B10/B16 那几次"Tux 闪一下就没"里就已经埋下过伏笔。

### 1. fbcon 会抢走 fb0 —— 必须静默，否则"只有背光"不能当判据

证据（B25 日志）：`Console: switching to colour frame buffer device 270x84` 出现两次，
0.24 s（simpledrm）和 **37.15 s（msmdrmfb 注册时）**。也就是说 msm 一注册 framebuffer，
fbcon 就重新绑到 fb0 上；此后**内核任何一条 printk 都会触发 fbcon 重绘整屏**，
把刚画的彩条刷成黑底。表现＝只有背光、没有图案，与"eDP 没工作"完全一样。

处置（双保险）：
- init 里 `dmesg -n 1`（控制台只留 emergency）；
- `drmpaint` 启动第一件事遍历 `/sys/class/vtconsole/vtcon*`，`name` 含 `frame buffer` 的写 `bind=0`，
  并把每个 vtcon 的 name/bind 与解绑结果都打进日志。

### 2. `rc` 的语义必须拆开，否则又会出现"rc=1 算成功还是失败"

B24 记的 `fb0 white write rc=1` 是**管道里 `tr` 的退出码**，不是 `write(2)` 的返回值 —— 两者完全不同。
B26 的日志分成三类，各自可判读：

| 日志 | 含义 |
|---|---|
| `open(/dev/fb0) rc=<fd>` | 负数＝打不开（节点不存在/权限），此时什么都没写 |
| `FBIOGET_FSCREENINFO 失败: <errno>` | 节点在但不是 fbdev 或驱动没实现 |
| `write 失败 chunk@<off> n=<len> ret=<ret> <errno>` | 真写到一半失败（"not in virtual address space" 属这类） |
| `drmpaint(诊断, /dev/fb0) rc=<n>` | **程序自己的退出码**，0＝整屏写完且无错 |

另外会单独打印 `pitch / bpp / smem_len / 每块行数 / 块数 / 首块与末块的 write 返回值`。

### 3. 分块必须按整行对齐

2160×1350×4 = 11.66 MB，`line_length` = 8704 B（2160×4 再向上对齐）。
按 64 KiB 硬切时块边界会落在行中间，fbdev 的脏区就不是整行，DRM 侧可能画出半帧/撕裂。
B26 改成 **每块 ≈64 KiB 但必须是 `line_length` 的整数倍**（本次 7 行 = 60928 B），
并打印实际块大小与块数。

### 判据（不变，而且这三条修完之后才成立）

屏幕出现 **8 条彩条 + 正中一个每秒 +1 的超大数字 + 顶部横扫的白块**：
数字在跳/白块在动 = 显示链路真的在工作；一直只有背光 = eDP 输出端确实没工作。

## 人眼观察汇总（与机器证据分开记）

| 时刻 | 轮次 | 观察 | 当时机器状态 |
|---|---|---|---|
| 10:37 | B7 | 背光常亮，**闪过 4–5 只企鹅** | msm 未加载，DSI 由 ABL 驱动 |
| 10:49 | B9/B10 之间 | 又见 Tux 一闪 | 同上 |
| 10:58 | B10 | Tux 闪一下即消失 | 桥已上电并写完 52 条表，训练失败 |
| 11:30 | B16 | Tux 闪一次、背光常亮 | 训练挪到 `enable()` 仍失败 |
| 11:58 | B20 | 亮屏、无像素 | 未加载 msm |
| 12:10 | B21 | 亮屏、无像素 | 写入 9 个 BL 值且回读一致 |
| 12:22 | B22 | 背光亮、无像素 | 桥先上电，`dsi_err` 停在 10 |
| 13:00 前后 | B24/B25 | 亮屏、无彩条 | **两轮都没真正画上去**（工具问题），不可判 |

> 共同点：**从来没有出现过"稳定图像"**；出现过的都是 ABL 留下的那一帧。
> 所以"像素是否曾由我们的 DSI 送出"至今没有正面证据——这正是 B26 要回答的。
