# 状态表

> 状态词只有：待调研 / 有源码线索 / 离线通过 / 真机启动 / 真机通过 / 真机失败（发布前另用 冷启动通过 / 可发布）
> 「离线通过」= 本地构建/静态验证通过，**没有**真机结果。
> 「真机通过」只由硬件指标或人眼判断，connector connected / fb0 / 背光寄存器正常**都不算点亮**。
> 最后更新：2026-11-01 13:55（彩条里程碑）

## 当前

> 每次上机一个编号（**B1、B2…**），见 [`docs/bringup-rounds.md`](bringup-rounds.md)。
> 用编号说话，避免"上一轮"产生歧义。

- **里程碑（2026-11-01 13:15）**：**SN65DSI86 内置彩条在屏幕上出图了！**（用户照片确认，
  `private/logs/20261101-1300-B72/colorbar-evidence.jpg`）——写 `0x3C=0x10`（COLOR_BAR_EN）
  即可让桥独立于 DSI 输入直接输出测试图案。**68 轮以来首次有像素上屏。**
  **bisect 最终裁决：eDP 输出侧（桥→PS8331→面板）完全正常，故障锁定 DPU 像素数据通路**
  （B75 单链证据：DSI 包协议零错误、桥 f0-f7 清后全 0、DPCD 0x200=0x81 稳定，但像素为黑）。
  0x3C 已恢复 0x07。
- **下一步（唯一）**：**查 DSI `adjusted_mode` 为什么被切成 1080（B79）**。B77/B78 两个新证据：
  ① INTF 输入端**有变化数据**（MISR CTRL=0x81000301、两次 SIG 不同）→ 不是空帧；
  ② 桥 `0x10`/`0x20` **运行中可写**（0x80→0x20、1080→2160 均成功且保持），写 2160 后 CHA 立刻
  报 INVALID_LENGTH → **反证 DSI 实际只发 1080/行**（`TOTAL=1124`、`ACTIVE_H=(1108,28)` 反推 mode=1080/1124）。
  **结论：B56-B78 的"单链 2160"从未真正生效，DSI 一直只发半宽**——之前所有"改桥配置"的轮次都建立在
  "DSI 发 2160"的错觉上。B79：在 `dsi_mgr_bridge_mode_set`/`dpu_encoder` 打点确认 `adjusted_mode.hdisplay`
  （2160 还是 1080），定位切半发生在 DRM tile / DPU split / panel get_modes 哪一层。
- **B 阶段进度（截至 B78）**：
  - **已验证通过的段**：背光 ✅ / 桥上电+配置+训练 ✅ / eDP 输出 ✅（彩条证实）/
    PS8331 mux ✅ / 面板供电+复位+TCON ✅（DPCD 0x200=0x81）/ DPU INTF 有像素流 ✅（MISR）
  - **当前真正缺口**：DSI 只发 1080 半宽，而 panel 是 2160 → 桥/TCON 收到的行宽与面板不匹配。
  - 双链"随机坏链"可能同源：两链各 1080 但桥/tile 配置与实际不符。
- - **设备状态**：**2026-10-31 23:35 已回到 Android**（电量 99%）。`userdata` / `boot_a/b` /
  `vbmeta_*` / `super` 全程未触碰；每轮 dtbo_a 均已刷回。
- **当前阻塞**：无。B47 需要用户人眼 5 分钟（唯一人工环节）。
- **下一轮上机协议**：① 探针定时 **300 s**；② 判据看 fastboot 出现的**时刻**（秒级＝崩，接近定时点＝活着）；
  ③ 一轮只改一个变量；④ 崩溃后紧接的那次引导不可信；⑤ **提交内核后必须重新同步全部 .ko 到
  `out/kmod-probe/`**（否则 vermagic 不匹配，B40 教训）。
- **调试通道（今天升级）**：
  - **USB NCM + telnet**（`tools/k606f-telnet.sh`）—— 但 `msm` 加载后经常掉，不能依赖。
  - **持久化日志**：探针把结果写进 `dtbo_b` 第 4 KiB 起，回 Android 后
    `adb shell su -c 'dd if=/dev/block/by-name/dtbo_b bs=4096 skip=1 count=48'` 读回。
    `k606f.blmode=1/2/3` 三种脚本化模式见 `tools/build-initramfs.sh`。
  - **人眼通道**：`tools/drmpaint.c`（DRM）与 `/dev/fb0` 直写，都能独立上屏。
- **操作者 / 运行中任务**：无。设备停在 Android。


## 子系统

| 子系统 | 状态 | 说明 / 下一步 |
|---|---|---|
| 立项评估 | 离线通过 | `~/WorkBuddy/2026-10-28-13-20-50/YT-K606F-mainline-feasibility.md` |
| 工作区与资料池 | 离线通过 | 桌面工作区；上游 7.2 tarball + pmOS 配置 + pmOS 树。**厂商开源包 `yt_k606f_s100101_210209_row`（2026-10-29 核验）：不含板级 dtsi 与固件，只提供 ODM 驱动参考**（结论见 `docs/abandoned.md` #3） |
| 构建环境（gcc 交叉） | 离线通过 | `aarch64-linux-gnu-gcc 16.2.0` + ccache 4.14.1 |
| 构建环境（LLVM 对照） | 离线通过 | clang 23.1.3 + LLD，`TOOLCHAIN=llvm` |
| 基准内核（clang） | 离线通过 | `out/kernel-ref-7.2`：Image 46 MB、332 模块 |
| 基准内核（gcc） | 离线通过 | `out/kernel-gcc-7.2`：512 s + 168 s，332 模块 |
| pmOS sm8250 内核树 | 离线通过 | tag `sm8250-7.2.0`（`71f4068`）已检出；gcc 构通，335 `.ko` |
| 原厂取证（FDT/固件/分区/AVB） | 离线通过 | `docs/partitions.md`、`private/backups/2026-10-28/` |
| 恢复能力演练 | **真机通过** | 写回读回一致；`boot_a` 写坏→自动回 Fastboot→刷回正常（`docs/recovery-rehearsal.md`） |
| 分区备份 | 离线通过 | 已备份 GPT×6 LUN、boot_a/b、dtbo_a/b、vbmeta×4、persist、metadata；**userdata 未备份** |
| 首次启动（fastboot boot） | **真机通过** | 内核 7.2.0 启动、UFS 枚举、USB NCM + telnet shell、simpledrm 出画面（`docs/step3-firstboot.md`） |
| USB NCM 网络（调试通道） | **真机通过** | host 侧 NM 持久化 unmanaged（`/etc/NetworkManager/conf.d/99-unmanaged-usb.conf`），msm 加载后网络持续稳定 |
| 板级 dts | 离线通过 | `sm8250-lenovo-k606f.dts`（以 q706f 为骨架，已编出 dtb） |
| 显示：msm 模块化 + panel 驱动 | **真机启动** | `panel-lenovo-k606f` 双 DSI 驱动 + `CONFIG_DRM_MSM=m`；insmod 后 msm 拿到的 DRM 卡是 **`card1`**（`card0` 被 simpledrm 占了），DSI-1 connector connected |
| 显示：DSI 控制器使能 | **真机通过** | 2026-10-30：两个 DSI 都 `DSI_CTRL=0x1f1`（ENABLE=1）、`CLK_CTRL=0x23f`、`fb0=msmdrmfb`；2026-10-31 B22 补上关键一条：桥先上电时 **`dsi_err_worker` 停在 10 不再增长**，说明 DSI 有接收方 |
| 显示：eDP 桥 PSA8331（B 阶段） | **真机通过** | 仅指"桥这一层"：probe 三行、16 根 GPIO 全到手、零 warning；1p2/1p8/refclk/`mipi-to-edp-en`/TPS1 分步全部存活；**256 字节寄存器全图与 Android 工作态 0 差异**（B23，`private/logs/20261031-1235-B23/`）。**不含像素输出**，那条见下一行 |
| 显示：点亮 / 像素输出 | **真机失败** | 背光已由我们的驱动点亮（B7，人眼确认）。像素验证已多轮完成：DRM modeset 软件侧成功（B28 `SETCRTC crtc=91` 上屏 pattern=bars）、**人眼=亮屏无彩条**（B47）——除 B7 的 ABL 残帧外，像素从未到玻璃；根因追查见 `docs/step4-display.md` B44-B60 |
| 显示链路拓扑 | 有源码线索 | **2026-10-30 本机 FDT 确认**：`/soc/i2c@984000/eDP-MUX@66` = `eDP-MUX-8331`（PSA8331），DSI → 桥 → eDP 玻璃；活动面板就是 `qcom,mdss_dsi_dual_sim_video`。厂商树有对应驱动 `techpack/display/msm/dsi/dsi86-edp-mux.c`（`docs/step4-display.md` 新增章节） |
| 触控（GT738x） | 有源码线索 | **2026-10-30 本机 FDT 定性**：`goodix,gt738x` @ `i2c@a94000`（qupv3_se13_i2c）addr `0x5d`，reset 38 / report 25 / irq 39，供电 `vtouch`（`pm8150_l16`）；厂商包有完整下游 I2C 驱动 `goodix_gt738x/`。**现有 dts 的「SPI13 + gt9916」是错的**（技术债 #16） |
| Wi-Fi（QCA6390） | 待调研 | `bdwlan.e01–e09` + `amss20.bin` 已导出 |
| 音频（wcd938x + wsa881x） | 待调研 | UCM 要自写 |
| GPU（a650） | 待调研 | `a650_zap/gmu/sqe` 已导出 |
| 充电控制 | 待调研 | **首版不做**（`docs/abandoned.md` #1）：smb5 无上游驱动；只读电量可走 bq27xxx |
| 相机 | 待调研 | **不做**（`docs/abandoned.md` #2）：SM8250 无上游 ISP |
| 开源 | 离线通过 | GitHub `easleyclaymore/k606f-mainline`（public），脱敏后推送；发布验收按第 7 步走，尚未做 |
