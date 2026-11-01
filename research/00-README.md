# YT-K606F 主线 Linux 移植 · 共同研究资料包

> 打包时间：**2026-11-01 12:10**（快照）；GitHub 版更新至 **2026-11-01 13:30**（含彩条里程碑与真单链跑通）
> ⚠️ 项目仍在**活跃推进**中，本包是某一时刻的切片。最新状态请以包内 `06-项目文档/status.md`
> 和内核 `git log` 为准，不要把它当作已完成的结论。

---

## 一、这份包按你那份清单组织

你给的清单有四项，本包逐项对应：

| 清单项 | 在包里看哪里 | 一句话结论 |
|---|---|---|
| **1. 硬件信息** | `01-硬件信息/01-硬件信息.md` | Lenovo YT-K606F（Yoga Tab 13 / 国行 YOGA Pad Pro），SM8250-AC 骁龙 870，13" 2160×1350 OLED，**屏幕经 TI SN65DSI86 桥转 eDP**（不是直连 DSI）。原厂 Android 显示完全正常。 |
| **2. 当前构建信息** | `02-构建信息/` | postmarketOS sm8250 树（tag `sm8250-7.2.0`），当前 commit `4f90c5c98`；Image 46 MB（版本 `7.2.0-g4f90c5c9800d`）+ dtb + `.config` + 完整 cmdline 都在。 |
| **3. 当前现象** | `03-当前现象/03-当前现象.md` | **背光亮、无图像**（不是花屏/偏移）。Android 侧可 adb；自制内核侧走 USB NCM + telnet（非标准 adb/ssh）。面板驱动**已移植**。 |
| **4. 原厂设备树** | `04-原厂设备树/` | 原厂 dtb 原件 `fdt.dtb` + 正确反编译的 `fdt-clean.dts`（1012 KB）。面板时序、供电、GPIO 都以它为准。 |

---

## 二、包的目录结构

```
K606F-共同研究包-20261101/
├── 00-README-先看这个.md      ← 本文件（索引用）
├── 01-硬件信息/               设备型号、SoC、屏幕与桥接 IC、原厂显示状态
├── 02-构建信息/
│   ├── 02-构建信息.md         源码地址/分支/commit、工具链、关键 config
│   ├── Image                  实际构建的内核镜像（46 MB）
│   ├── sm8250-lenovo-k606f.dtb   板级设备树（181 KB）
│   ├── cmdline.txt            实际启动参数（原样）
│   └── config/                k606f-probe.config（源）+ out 实际 .config
├── 03-当前现象/               黑屏/背光/花屏判定、通道可达性、驱动移植状态、当前卡点
├── 04-原厂设备树/
│   ├── 04-原厂设备树说明.md
│   ├── fdt-clean.dts          原厂 dtb 正确反编译版（推荐用这份）
│   └── fdt.dtb                原厂运行时 dtb 原件
├── 05-源码改动/               面板驱动 / eDP 桥驱动 / 板级 dts 三个关键文件
├── 06-项目文档/               项目内部记录（README/status/rounds/step4/tech-debt/AGENTS）
└── 07-最近日志/               最新两轮（B68/B70）的设备侧取证日志
```

---

## 三、如果你只想快速理解"现在卡在哪"

1. **链路拓扑**：SoC 的 DSI（双链路 bonded，各 1080×1350）→ **TI SN65DSI86 桥**（I²C `0x2C`，做 DSI→eDP）→ **PS8331 mux**（I²C `0x33`，端口切换）→ eDP OLED 面板。
2. **已经确认的事实**：
   - 桥配置、DSI 时序、PHY 寄存器已与 Android 工作态**逐字节/逐位对齐**；
   - 桥**确实收到了 DSI**（DSI 时钟被检测到、PHY 层无 SOT/EOT 错误）；
   - DP/eDP 侧**一直在输出**（DPCD `0x200=0x81` 稳定），只是**像素内容坏**；
   - 双链存在**随机坏链**：每次引导随机一条链报 INVALID_LENGTH 并刷 FIFO 错误，持续整个 boot。
3. **重大进展（2026-11-01）**：
   - **SN65DSI86 内置彩条测试出图**（写 `0x3C=0x10`）：eDP 输出侧（桥/训练/mux/面板）全线正常；
   - **真单链 2160x1350@60 跑通**：删除 DSI0 节点的三个 dual-dsi 属性后（此前 B56 的编辑曾被
     脚本回滚丢失，"单链"轮实际仍是 bonded+dsi1 禁用的矛盾态——这是单链"挂死"的根因），
     桥的 DSI RX **零错误**（0xF0-F7 全 0）、TCON 报稳定同步、探针 5.5 分钟无崩溃。
4. **还没解决的**：双链模式的随机坏链（单链已通，双链是否需要/如何同步启动待研究）；
   ramoops 跨 boot 持久化被 ABL 覆盖（panic 文本取证改用增量落盘方案已够用）。

---

## 四、复现与构建（简要）

- 交叉工具链：`aarch64-linux-gnu-gcc 16.2.0` + `GNU ld 2.47`（本机 ccache 加速）。
- 构建命令（在项目根目录，`SRC` 必须显式指定，否则会用错树）：
  ```sh
  SRC=$PWD/sources/pmos-sm8250 OUT=$PWD/out/kernel-msmmod \
    CFG=$PWD/sources/k606f-probe.config tools/build-kernel.sh Image modules dtbs
  ```
- 上机方式：`fastboot boot <boot.img>`（**不写分区**），探针 300 s 后自动重启回 fastboot。
- ⚠ 每次改内核后**必须**把 `out/kernel-msmmod` 里的 `.ko` 同步到 `out/kmod-probe/`，
  否则会发生 vermagic 不匹配（B40/B66 两轮就是因此作废）。

---

## 五、重要提醒

1. **含设备唯一信息**：`cmdline.txt` 与部分文档里有设备序列号（`<SERIAL>`）。这是私下研究包，
   **已脱敏后发布到 GitHub（序列号以 <SERIAL> 占位）**。
2. **`fdt-clean.dts` 才是可信的原厂 DTS**。如果看到别处有个 `fdt.dts`，那是早期自写 dumper 的产物，
   属性解析会错位（GPIO 编号、cell 数会读错），**不要用它做取证**。
3. 内核源码树是 **`--depth 1` 浅克隆**，看不到完整上游历史；要挑补丁需先 `git fetch --deepen`。
4. 文档里的 `B45…B70` 是**上机轮次编号**（每次 `fastboot boot` 算一轮），不是版本号。
5. 设备当前状态：已回到 Android、电量接近满，可随时上机；`userdata` 全程未触碰。
