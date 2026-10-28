# 状态表

> 状态词只有：待调研 / 有源码线索 / 离线通过 / 真机启动 / 真机通过 / 真机失败
> 「离线通过」= 本地构建/静态验证通过，**没有**真机结果。
> 最后更新：2026-10-28 14:15

## 当前

- **下一步（唯一）**：把 pmOS sm8250 树（tag `sm8250-7.2.0`）检出完成，用**同一份配置 + gcc** 构一次，
  与本机基线对比差异（ccache 应大量命中），据此确认"社区参考内核可复现"。
- **阻塞**：gitlab.postmarketos.org 的 blob 拉取仍在进行（373 MB/约 1.5 GB），走 Clash 代理。
- **设备状态**：正常在 Android（fastboot 只读查询后已 `fastboot reboot` 回系统），未写入任何分区。
  操作者：agent。USB 已连接，`adb devices` 可见设备（序列号略）。

## 子系统

| 子系统 | 状态 | 说明 / 下一步 |
|---|---|---|
| 立项评估 | 离线通过 | `~/WorkBuddy/2026-10-28-13-20-50/YT-K606F-mainline-feasibility.md` |
| 工作区与资料池 | 离线通过 | 桌面工作区；上游 7.2 tarball + pmOS 配置 + pmOS 树（部分） |
| 构建环境（gcc 交叉） | 离线通过 | `aarch64-linux-gnu-gcc 16.2.0` + ccache 4.14.1，系统安装 |
| 构建环境（LLVM 对照） | 离线通过 | clang 23.1.3 + LLD，`TOOLCHAIN=llvm` |
| 基准内核（clang） | 离线通过 | `out/kernel-ref-7.2`：Image 46 MB、332 模块、Image 328 s |
| 基准内核（gcc） | 离线通过 | `out/kernel-gcc-7.2`：512 s + 168 s，332 模块 |
| pmOS sm8250 内核树 | 离线通过 | tag `sm8250-7.2.0`（`71f4068`）已检出；gcc 构通：Image 495 s + 模块 157 s，335 `.ko` |
| 原厂取证（FDT/固件/分区/AVB） | 离线通过 | `docs/partitions.md`、`private/backups/2026-10-28/` |
| fastboot 只读信息 | 离线通过 | `max-download-size` 768 MiB、`unlocked: yes`、`current-slot a` |
| 恢复能力演练 | **真机通过** | 写回读回一致；`boot_a` 写坏→自动回 Fastboot→刷回正常（`docs/recovery-rehearsal.md`） |
| 分区备份 | 离线通过（部分） | GPT×6 LUN、boot_a/b、dtbo_a/b、vbmeta×4、persist、metadata；**super/userdata 未备份**（不需要） |
| 板级 dts | 离线通过 | `sm8250-lenovo-k606f.dts`（以 q706f 为骨架，已编出 dtb） |
| 显示接管（第 4 步） | **真机通过** | msm_dpu card2 + DSI-1 connected+2160x1350+enabled（`docs/step4-progress.md`） |
| 首次启动（fastboot boot） | **真机通过** | 内核 7.2.0 启动、UFS 枚举、USB NCM + telnet shell、simpledrm 出画面（`docs/step3-firstboot.md`） |
| 显示（DPU + 双 DSI） | 待调研 | 无 DSC、面板无初始化序列 |
| 触控（GT738x） | 待调研 | 固件已导出 `goodix_firmware.bin` |
| Wi-Fi（QCA6390） | 待调研 | `bdwlan.e01–e09` + `amss20.bin` 已导出 |
| 音频（wcd938x + wsa881x） | 待调研 | UCM 要自写 |
| GPU（a650） | 待调研 | `a650_zap/gmu/sqe` 已导出 |
| 充电控制 | 放弃（首版） | smb5 无上游驱动；只读电量可行 |
| 相机 | 放弃 | SM8250 无上游 ISP |
