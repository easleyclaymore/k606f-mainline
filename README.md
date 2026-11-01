# Lenovo YT-K606F (Yoga Tab 13) 主线 Linux 移植

把主线 Linux 内核 + 桌面发行版移植到联想 YT-K606F（Yoga Tab 13 / 国行 YOGA Pad Pro）平板，与 Android 双系统共存。目标是在这台骁龙 870（SM8250-AC）设备上原生运行 Ubuntu + KDE Plasma。

> **状态：进行中（第 4 步 · 显示接管，接近收口）。** 内核通过 `fastboot boot` 启动、USB shell 可用。
> **「eDP 输出侧全线正常」已被证明**：给桥写 `0x3C=0x10` 让它输出内置彩条，屏幕直接出图（B74 里程碑，有照片）；
> DPU INTF 输入端也确认有像素流（MISR 签名逐帧变化，B77）。
> **当前唯一缺口**：DSI 实际只按 **1080/行（半宽）** 发送，而面板是 2160——B78 用"运行中重写桥寄存器"反证了
> 这点（写 2160 后桥立刻报 `INVALID_LENGTH_ERR`）。已排除 EOT 缺失、跨 PLL 频漂、PHY timing、桥配置、
> DPU 拆分、pclk/byte 速率比、桥通道模式写入等假设（见 `docs/investigation/`）。
> 下一步：定位 `adjusted_mode` 被切半的那一层（DRM tile / DPU split / panel `get_modes`）。目前**不提供安装器**，也不写任何分区。

## 设备

| 项 | 值 |
|---|---|
| 机型 | Lenovo YT-K606F = Yoga Tab 13 / YOGA Pad Pro，13" 2160×1350@60 |
| SoC | SM8250-AC（骁龙 870，kona），8 GB RAM / 256 GB UFS 3.1 |
| 出厂 | Android 12（SDK 31），内核 **4.19.136**（= 联想官方开源包的版本） |
| 设备当前 | **已 OTA 到最新系统**：Android 12（SDK 31），内核 **4.19.157-perf+**（不是出厂版本） |
| 内核基线 | 主线 Linux 7.2（基于 [postmarketOS sm8250 树](https://gitlab.postmarketos.org/soc/qualcomm-sm8250/linux)，tag `sm8250-7.2.0`） |
| 显示 | 双 DSI split → **PSA8331 eDP 桥** → eDP 玻璃；面板无 DCS 初始化序列（video 模式） |

## 当前支持

| 功能 | 状态 | 说明 |
|---|---|---|
| 启动（`fastboot boot`） | ✅ 真机通过 | 内核 7.2.0 启动、UFS 枚举、USB NCM + shell |
| 装到存储 / 双系统切换 | ⬜ 未开始 | 第 5 步 |
| 显示 | ❌ 未点亮（接近收口） | **eDP 输出侧已证明正常**（桥内置彩条出图，B74）；DPU INTF 有像素流（MISR，B77）；**唯一缺口 = DSI 只发 1080 半宽而面板 2160**（B78 反证）。见 `docs/investigation/` |
| GPU | 🔶 部分 | Adreno 650 的 DRM 卡已注册，未做渲染验证 |
| 触控 | ⬜ 待做 | 已定性为 GT738x（I2C，地址 `0x5d`） |
| Wi-Fi / 蓝牙 | ⬜ 待做 | QCA6390 |
| 音频 | ⬜ 待做 | wcd938x + wsa881x，UCM 需自写 |
| 充电控制 / 相机 | ⛔ 不做（首版） | 无上游驱动；电量只读可走 bq27xxx |

状态词：真机通过 = 有硬件指标或人眼确认；离线通过 = 只验证了构建/逻辑。

## 文档

| 文档 | 内容 |
|---|---|
| [`docs/build.md`](docs/build.md) | 主机依赖、拿内核源码、应用改动、构建命令 |
| [`docs/install.md`](docs/install.md) | 打启动镜像、`fastboot boot` 临时启动、风险与恢复 |
| [`docs/return-to-android.md`](docs/return-to-android.md) | 回到 Android，以及刷过 `dtbo` 后必须做的事 |
| [`docs/investigation/`](docs/investigation/README.md) | **调查记录**：显示点亮问题的全部技术过程（B1-B78 台账、已排除假设、寄存器对齐数据），未润色的内部工作文档镜像 |

## 给做同类移植的人

- 显示这条线的核心发现：eDP 桥（SN65DSI86）的 DSI RX 对**包帧结构敏感**（EOT 缺失
  会报 `INVALID_LENGTH_ERR`）；排查手段是**与 bootloader 工作态做寄存器逐字节对照**
  （I2C 工具在 `tools/`，上机自动化在 `tools/build-initramfs.sh` 的 blmode 机制）。
- 踩过的坑：桥是两颗芯片（0x2C 真桥 + 0x33 mux，别写错地址）、模块必须与内核
  vermagic 严格一致、builtin 驱动会把 USB 调试通道一起带走（技术债 #21）。
- 我们没解决的问题（随机坏链、单链 probe 挂死）在调查文档里有完整的排除清单——
  如果你在这台设备（或同拓扑设备）上推进，请先读它，别重复我们走过的死路。

## 目录结构

```
├── README.md            本文件
├── docs/                构建、安装、回到 Android（对外文档）
├── config/              内核配置（k606f.config / k606f-probe.config）
├── kernel/              内核侧改动：板级 dts、面板驱动、合并 patch
└── tools/               构建、打包、上机、取证脚本
```

**本仓不包含**：内核源码树、构建产物、固件镜像，以及设备唯一数据（序列号、MAC、蓝牙地址、校准数据）。

## 内核侧改动

详见 [`kernel/README.md`](kernel/README.md)：

- `kernel/sm8250-lenovo-k606f.dts` —— 板级设备树
- `kernel/panel-lenovo-k606f.c` —— 双 DSI 视频模式面板驱动（`panel-simple` 不支持 dual-DSI）
- `kernel/k606f-kernel.patch` —— 全部改动的合并 patch（含 DSI PHY 7nm PLL 修复）

## 免责声明

- 解锁 bootloader 会清空设备数据、可能影响保修；刷写分区操作不当可能导致设备无法启动。
- 动手前务必完整备份分区并校验，确认有恢复手段。
- 本仓库只提供方法、配置与代码，不提供刷机工具、镜像或固件。内容按「原样」提供，使用风险自负。

## 许可

MIT（见 LICENSE）。
