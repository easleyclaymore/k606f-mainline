# Lenovo YT-K606F (Yoga Tab 13) 主线 Linux 移植

把主线 Linux 内核 + 桌面发行版移植到联想 YT-K606F（Yoga Tab 13 / 国行 YOGA Pad Pro）平板，与 Android 双系统共存。目标是在这台骁龙 870（SM8250-AC）设备上原生运行 Ubuntu + KDE Plasma。

> **状态：进行中（第 4 步 · 显示接管）。** 内核已能通过 `fastboot boot` 启动并拿到 USB shell，
> **但屏幕还没点亮**（DSI 控制器未使能，卡在 DRM bridge 链装配）。目前**不提供安装器**，也不写任何分区。

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
| 显示 | ❌ 未点亮 | DPU card 与 DSI connector 已建立，但屏幕黑：DSI 控制器从未使能 |
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
