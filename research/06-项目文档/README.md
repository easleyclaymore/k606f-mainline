# YT-K606F 主线 Linux 移植 · 工作区入口

> 这份是**给你（接手的人或 agent）**看的，不是给外人看的。对外文档在
> `public/k606f-mainline/`（README + 构建/安装/回 Android 三份）。
> 最后更新：2026-11-01 11:05

## 一句话

把主线 Linux 内核 + 桌面发行版跑到联想 YT-K606F（Yoga Tab 13 / 国行 YOGA Pad Pro，骁龙 870）
并和 Android 双系统共存。**现在在第 4 步「显示接管」的最后一环：配置层（寄存器/时钟/
引擎数量）已与 ABL 工作态逐位对齐（B45-B55），失败依旧且坏链随机——下一步 B56 做
单链 bisect（dts+面板改单 DSI 2160 宽 + 桥单通道）二分 dual 机制 vs 公共路径。**

## 先看这几份（按顺序，约 12 分钟）

| 顺序 | 文件 | 看什么 |
|---|---|---|
| 1 | [`docs/status.md`](docs/status.md) | 每个子系统的当前状态、**唯一的下一步**、设备现在什么样 |
| 2 | [`docs/bringup-rounds.md`](docs/bringup-rounds.md) | **上机记录（B1…B44）**：每轮改了什么、排除什么、证据在哪。用编号说话 |
| 3 | [`docs/step4-display.md`](docs/step4-display.md) | 显示这条线的全部证据；**最新结论在「当前结论摘要」（B60 后，约 350 行起）与 B39-B60 各章，顶部更早的 B28–B31 摘要已被推翻** |
| 4 | [`docs/tech-debt.md`](docs/tech-debt.md) | 25 条临时做法，以及它们的退出条件 |
| 5 | [`docs/recovery-unbrick.md`](docs/recovery-unbrick.md) | 设备不可达时怎么救（**动手前必读**） |
| 6 | [`docs/incident-2026-10-30-brick.md`](docs/incident-2026-10-30-brick.md) | 设备失联 17 小时的完整复盘 |

要往前翻历史：`docs/worklog.md`（按迭代记，末尾是最新的）。
放弃过什么：`docs/abandoned.md`。规则与授权：`AGENTS.md`。

## 现在到哪一步了

| 阶段 | 状态 |
|---|---|
| 0 立项 / 1 构建环境 / 2 取证 / 3 首次启动 | 完成 |
| 4 点亮与内存桌面 | **进行中，只剩最后一环**：桥地址 bug（B39）、HS_TX_TIMEOUT（B42）、EOT 缺失（B45）、时钟域统一（B53）逐一修复；**配置层与 ABL 逐位对齐后失败依旧（坏链随机，B55），下一步单链 bisect** |
| 5 安装到存储 ~ 8 维护 | 未开始 |

**当前阻塞**：无。设备在 Android，adb 在线，dtbo_a 校验一致。

**唯一下一步**：**DPU INTF 内置 TPG 实验**——绕过 SSPP→LM 像素路径，让 INTF 自己
生成图案。出图=问题在 SSPP/LM 像素路径；不出图=问题在 INTF→DSI 通路。
背景：桥内置彩条已出图（eDP 输出侧 ✅）、真单链桥 RX 零错误（协议 ✅）、
但屏幕黑（像素数据 ❌）——只剩 DPU 像素数据通路。
详见 `docs/step4-display.md` 当前结论摘要与 `docs/bringup-rounds.md`。

## 目录地图

| 路径 | 放什么 | 能不能删/改 |
|---|---|---|
| `AGENTS.md` | 设备事实、已确认决策、授权边界、硬规矩、提交署名 | 决策变了才改 |
| `docs/` | **内部记录唯一存放处**：状态表、日志、技术债、放弃方案、各步骤文档、救砖决策树 | 持续更新 |
| `tools/` | 内部脚本：构建、打包、上机、采集、文档自检 | 可改，改完跑 `check-docs.sh` |
| `sources/` | 参考源码（只读）：`pmos-sm8250` 内核树、上游 tarball。**内核改动在这里编译** | 只读，除非换基线 |
| `out/` | 构建输出、ccache、启动镜像 | 可清 |
| `private/` | 原始日志、分区备份、补丁存档、本机唯一数据 | **不进任何仓库** |
| `权威数据/` | 厂商开源包（1 GB）、运行时 FDT、厂商桥驱动源码、移植 skill | 只读取证 |
| `public/k606f-mainline/` | 集成仓（独立 git，公开）：配置、dts、面板驱动、构建脚本、对外文档 | 走 git |

三处 `docs` 曾经重复过，现在只有工作区这一份是内部记录；公开仓只留对外文档（技术债 #18 已还）。

## 环境与构建

- 主机：Arch/CachyOS，12 核 / 31 GB。有 `aarch64-linux-gnu-gcc 16.2.0`、ccache、dtc、bc。
- 内核基线：`sources/pmos-sm8250`（postmarketOS sm8250 树，tag `sm8250-7.2.0`，本地提交叠了板级改动）。
- 构建（增量约 20–30 秒，ccache 命中率 90%）：

```sh
TOOLCHAIN=gcc SRC=$PWD/sources/pmos-sm8250 OUT=$PWD/out/kernel-msmmod \
  CFG=$PWD/sources/k606f-probe.config tools/build-kernel.sh Image modules dtbs
```

- 打启动镜像（**只 fastboot boot，不写分区**）：

```sh
TOOLCHAIN=gcc SRC=$PWD/sources/pmos-sm8250 OUT=$PWD/out/kernel-msmmod \
  CFG=$PWD/sources/k606f-probe.config tools/bringup.sh --build-only
# 产物 out/boot-probe.img，同目录 boot-probe.identity 里有全部哈希
```

## 上机流程（2026-10-31 修订）

1. **确认设备在 Android 且电量够**：`adb devices` 看到 `<SERIAL>`，`dumpsys battery | grep level` ≥ 30%。
2. **进 fastboot**：`adb reboot bootloader`；`fastboot boot` 之前**必须先 `fastboot reboot-bootloader`**，
   否则报 `Load Error`（踩过多次）。
3. **擦 dtbo**：`fastboot erase dtbo_a`（原厂 dtbo 依赖上百 vendor 符号，叠不上自有 DTB）。
   擦之前确认备份在手：`private/backups/2026-10-28/images/dtbo_a.img`（`ada89e08…`）。
4. **启动**：`fastboot boot private/images/boot-bridge-<hash>.img`。
5. **等**：探针到点（`bringup.sh` 的 `TIMEOUT`，默认 300 秒）自己重启回 fastboot。**这期间通常没有 USB**（见"零枚举"那条规矩）。
6. **收尾**（每次都一样，别跳）：
   ```sh
   fastboot flash dtbo_a private/backups/2026-10-28/images/dtbo_a.img
   fastboot set_active a     # 关键：连续失败启动会把 A 槽标成 unbootable
   fastboot reboot
   ```
7. **取证据**：探针把日志写进了 `dtbo_b` 第 4 KiB 起，
   `adb shell su -c 'dd if=/dev/block/by-name/dtbo_b bs=4096 skip=1 count=48' | tr -d '\000'`。

辅助脚本：

| 脚本 | 用途 |
|---|---|
| `tools/exp-dsi-probe.sh` | 一条命令跑完「进 fastboot → 擦 dtbo → boot → 取证据 → 刷回 → 回 Android」 |
| `tools/catch-fastboot.sh` | 高频轮询，抓到 fastboot 窗口立刻刷回 `dtbo_a`（设备在启动循环时用） |
| `tools/watch-edl.sh` | 盯 `05c6:9008`（EDL）或 fastboot 出现 |
| `tools/check-docs.sh` | 文档自检：状态词、技术债表、必备条目、内部记录没混进公开仓、无悬空引用 |
| `tools/i2cdump8.c` | 静态 aarch64 的桥寄存器读/写工具，探针与 Android 都能跑 |
| `tools/drmpaint.c` | 静态 aarch64 的画面工具：fbdev 分块写或 DRM modeset，画彩条 + 跳动数字 |

## 七条硬规矩（都是踩出来的）

1. **新驱动先编成模块**（`=m`）手动 `insmod`，别做 builtin。builtin probe 出问题时 USB 调试通道一起死，
   设备直接失联 —— 这就是 10-30 那次的代价（技术债 #21）。
2. **每次上机先用已知好镜像确认设备能起**，再换新镜像。没有基线就没法判断是设备坏了还是镜像坏了。
3. **一次只改一个变量**。10-30 有过一次 initramfs + cmdline 一起改，白跑一轮。
4. **判据要取正面证据**：`DSI_CTRL` 的 ENABLE 位置 1、`CLK_CTRL` 非零才算 DSI 真的起来了；
   connector `connected`、`fb0` 存在、驱动加载成功都**不算点亮**。
5. **每个临时做法当场登记**到 `docs/tech-debt.md`，和改动同一个提交。
6. **"没有 USB 枚举"不等于设备挂了**。探针定时重启会准时发生：到点回 fastboot 就是活着、只是通道死了；
   秒级回 fastboot 才是真崩。判断依据是 fastboot 出现的**时刻**。
7. **连续失败启动会让 ABL 把槽标成 unbootable**（`fastboot getvar all` 里的
   `slot-unbootable:a:yes`）。此后 `fastboot reboot` 只会回 fastboot，必须 `fastboot set_active a` 才能复位。

## 关键标识（换机器/复活后对照用）

| 项 | 值 |
|---|---|
| 设备 serial | `<SERIAL>`（fastboot / adb 都用它） |
| 活动槽 | `_a`（若 `slot-unbootable:a:yes`，先 `fastboot set_active a`） |
| dtbo 备份 | `private/backups/2026-10-28/images/dtbo_a.img`，sha256 `ada89e08c881d0f10d3a2c420449d7373d7c22ad4ef28f7c69ca51f2080a33fd` |
| dtbo_b 备份 | `private/backups/2026-10-28/images/dtbo_b.img`（探针会往这里写日志，事后可从此恢复） |
| 下一轮待跑 | B66：单链挂死定位（hung_task panic 化 / ramoops 高地址 / #11 排查） |
| 最近一轮 | B65（`dd18aa66`，0.3s flush 抓到挂死静止现场，定性反转=probe 静默挂死） |
| 内核改动 | 本地提交序列：`33840ee73`(EOT)→`0a234b7c1`(bonded 同步启动)→`378e9694e`(时钟域统一)→`05040a82c`(回退 master-only)→`e3391aab7`(亚µs+实测打印)；补丁存档 `private/patches/` |
| 镜像归档 | `private/images/boot-bridge-<boot_img 前 8 位>.img`，同名 `.identity` 里有全部哈希（check-docs 第 11 项会核对存在性） |

## 接手后第一件事

照 `docs/status.md` 的「唯一的下一步」继续，然后：

1. 先读 `docs/step4-display.md` 的 **「当前结论摘要」（2026-11-01 版）与 B39-B60 各章**——
   摘要含现行事实表、已否定假设全表和当前前沿；更早的章节凡标 ⚠ 已被推翻。
2. 按 status.md 的"下一步（唯一）"做 **B56 单链 bisect**（改 dts+面板+桥配置）。
   读桥错误寄存器一律**先写 0xFF 清除再重读**（粘滞标志）；lsusb 别按 VID 预设过滤
   （平板是 17ef:7cdc）。
3. 无论哪种结果，都把镜像哈希、判据、证据路径写回 `docs/bringup-rounds.md` 与
   `docs/step4-display.md`；批量编辑文档时逐处断言命中数（B47-B54 行曾静默丢失）。

## 外部资料在哪

| 资料 | 位置 |
|---|---|
| 厂商开源包（内核 4.19.136 + ODM 驱动） | `权威数据/yt_k606f_s100101_210209_row_opensource/`（1 GB tar） |
| 厂商 eDP 桥驱动（已抽出） | `权威数据/vendor-dsi/dsi86-edp-mux.{c,h}` |
| 运行时设备树（正确反编译版） | `~/amp/fdt/fdt-clean.dts`（`dtc -I dtb -O dts`；旧 `fdt.dts` 是坏的，见技术债 #15） |
| 移植方法论 skill | `~/.workbuddy/skills/android-mainline-port/`（流程 0–8 步 + 13 个领域经验库） |
