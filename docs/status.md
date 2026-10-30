# 状态表

> 状态词只有：待调研 / 有源码线索 / 离线通过 / 真机启动 / 真机通过 / 真机失败（发布前另用 冷启动通过 / 可发布）
> 「离线通过」= 本地构建/静态验证通过，**没有**真机结果。
> 「真机通过」只由硬件指标或人眼判断，connector connected / fb0 / 背光寄存器正常**都不算点亮**。
> 最后更新：2026-10-30 13:55

## 当前

- **下一步（唯一）**：显示接管黑屏根因——`DSI_CTRL=0x20040000`（DSI 控制器从未 enable），
  `drm_atomic` 的 bridge pre_enable 执行了但没走到 `dsi_mgr_bridge`。用 `drm.debug=0x1f` 抓
  `dsi_mgr_bridge_pre_enable` 的 DBG，确认是「`master_dsi_link_id` 判断错」还是「bridge 没 attach 进 encoder 链」。
  → 2026-10-30 补充：**eDP 桥（PSA8331）已在本机 FDT 确认存在**，但它排在 DSI enable **之后**，
  解释不了 `DSI_CTRL` 没使能。**A 阶段（修 DSI enable）不变，不要抢跑写桥驱动**。
- **设备状态**：已恢复 Android（`dtbo_a` 已刷回备份，`adb devices` 可见），未留任何临时启动状态。
- **当前阻塞**：无硬件阻塞。主机侧调试通道（USB NCM + telnet）可用，只等下一次上机窗口抓 DBG。
- **调试通道**：USB NCM 网络已稳定（见下），可反复加载 msm 抓日志。
- **操作者 / 运行中任务**：无（主机侧文档整理中，未接设备，无构建、无上机任务）。

## 子系统

| 子系统 | 状态 | 说明 / 下一步 |
|---|---|---|
| 立项评估 | 离线通过 | `~/WorkBuddy/2026-10-28-13-20-50/YT-K606F-mainline-feasibility.md` |
| 工作区与资料池 | 离线通过 | 桌面工作区；上游 7.2 tarball + pmOS 配置 + pmOS 树。**厂商开源包 `yt_k606f_s100101_210209_row`（2026-10-29 核验）：不含板级 dtsi 与固件，只提供 ODM 驱动参考**（`权威数据/厂商开源包价值评估报告.md`） |
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
| 显示：msm 模块化 + panel 驱动 | **真机启动** | `panel-lenovo-k606f` 双 DSI 驱动 + `CONFIG_DRM_MSM=m`，insmod 后 card2=msm_dpu、DSI-1 connector connected |
| 显示：点亮（第 4 步） | **真机失败** | 试过、屏幕黑：DSI_CTRL 未 enable，bridge pre_enable 未走到 dsi_mgr_bridge（`docs/step4-display.md`）。下一步见「当前」 |
| 显示链路拓扑 | 有源码线索 | **2026-10-30 本机 FDT 确认**：`/soc/i2c@984000/eDP-MUX@66` = `eDP-MUX-8331`（PSA8331），DSI → 桥 → eDP 玻璃；活动面板就是 `qcom,mdss_dsi_dual_sim_video`。厂商树有对应驱动 `techpack/display/msm/dsi/dsi86-edp-mux.c`（`docs/step4-display.md` 新增章节） |
| 触控（GT738x） | 有源码线索 | **2026-10-30 本机 FDT 定性**：`goodix,gt738x` @ `i2c@a94000`（qupv3_se13_i2c）addr `0x5d`，reset 38 / report 25 / irq 39，供电 `vtouch`（`pm8150_l16`）；厂商包有完整下游 I2C 驱动 `goodix_gt738x/`。**现有 dts 的「SPI13 + gt9916」是错的**（技术债 #16） |
| Wi-Fi（QCA6390） | 待调研 | `bdwlan.e01–e09` + `amss20.bin` 已导出 |
| 音频（wcd938x + wsa881x） | 待调研 | UCM 要自写 |
| GPU（a650） | 待调研 | `a650_zap/gmu/sqe` 已导出 |
| 充电控制 | 待调研 | **首版不做**（`docs/abandoned.md` #1）：smb5 无上游驱动；只读电量可走 bq27xxx |
| 相机 | 待调研 | **不做**（`docs/abandoned.md` #2）：SM8250 无上游 ISP |
| 开源 | 离线通过 | GitHub `easleyclaymore/k606f-mainline`（public），脱敏后推送；发布验收按第 7 步走，尚未做 |
