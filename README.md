# Lenovo YT-K606F (Yoga Tab 13) 主线 Linux 移植

把主线 Linux 内核 + 桌面发行版移植到联想 YT-K606F（Yoga Tab 13 / 国行 YOGA Pad Pro）平板，与 Android 双系统共存。目标是在这台骁龙 870（SM8250-AC）设备上原生运行 Ubuntu + KDE Plasma。

> 状态：**进行中**（第 4 步「点亮与内存桌面」——显示接管）。详见 [`docs/status.md`](docs/status.md) 与 [`docs/worklog.md`](docs/worklog.md)。

## 设备

| 项 | 值 |
|---|---|
| 机型 | Lenovo YT-K606F = Yoga Tab 13 / YOGA Pad Pro，13" 2160×1350@60 |
| SoC | SM8250-AC（骁龙 870，kona），8 GB RAM / 256 GB UFS 3.1 |
| 出厂 | Android 12（SDK 31），vendor 内核 4.19.157-perf+ |
| 内核基线 | 主线 Linux 7.2（基于 [postmarketOS sm8250 树](https://gitlab.postmarketos.org/soc/qualcomm-sm8250/linux)，tag `sm8250-7.2.0`） |
| 显示 | 双 DSI split，无 DSC，面板无初始化序列（video 模式） |

## 当前进度

| 阶段 | 状态 |
|---|---|
| 0 立项 / 1 构建环境 / 2 取证 / 3 首次启动 | ✅ 完成 |
| 4 点亮与内存桌面 | 🔶 进行中（显示接管：msm_dpu card 已注册、DSI connector `connected`，黑屏根因定位中） |
| 5 安装到存储 ~ 8 维护 | ⬜ 未开始 |

关键里程碑：
- 主线内核 7.2.0 通过 `fastboot boot` 首次启动，UFS 枚举 + USB NCM shell（`docs/step3-firstboot.md`）。
- 显示：`CONFIG_DRM_MSM=m` 模块化加载，自写 `panel-lenovo-k606f` 双 DSI 面板驱动，DPU 显示 card 与 DSI connector 已建立（`docs/step4-display.md`）。

## 目录结构

```
├── AGENTS.md            项目规范、决策记录、授权边界（给下一个接手者）
├── docs/                状态表、工作日志、技术债、各步骤文档
├── tools/               构建 / 启动 / 取证脚本（build-kernel.sh 等）
├── config/              内核配置（k606f.config / k606f-probe.config）
├── kernel/              内核改动：patch + 板级 dts + 面板驱动
└── (私有)                原始日志、本机备份、设备唯一数据不入仓
```

## 内核改动

见 [`kernel/README.md`](kernel/README.md)。核心是：

- `kernel/sm8250-lenovo-k606f.dts` —— 板级设备树（以 q706f 为骨架）
- `kernel/panel-lenovo-k606f.c` —— 双 DSI 视频模式面板驱动（`panel-simple` 不支持 dual-DSI）
- `kernel/k606f-kernel.patch` —— 全部改动合并的 git patch（含 DSI PHY 7nm PLL 修复）

## 快速开始

构建与启动流程见 [`AGENTS.md`](AGENTS.md) 与 `tools/` 下的脚本。核心命令：

```sh
# 构建（模块版内核，显示驱动做成模块手动加载）
TOOLCHAIN=gcc SRC=$PWD/sources/pmos-sm8250 OUT=$PWD/out/kernel-msmmod \
  CFG=$PWD/sources/k606f-probe.config tools/build-kernel.sh Image modules dtbs

# 打启动镜像 + fastboot 临时启动（不写入分区）
tools/bringup.sh
```

## 免责声明

- 解锁 bootloader 会清空设备数据、可能影响保修；刷写分区操作不当可能导致设备无法启动。
- 动手前务必完整备份分区并校验，确认有恢复手段（本项目流程见 `docs/recovery-rehearsal.md`）。
- 本仓库只提供方法、配置与代码，不提供刷机工具、镜像或固件。内容按「原样」提供，使用风险自负。

## 许可

MIT（见 LICENSE）。
