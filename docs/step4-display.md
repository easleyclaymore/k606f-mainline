# 第 4 步：显示接管（进行中）

日期：2026-10-28　　状态：**准备中**（simpledrm 已出画面；原生 DPU+DSI 尚未接管）

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

## 两个已知风险

1. **双 DSI split 是最大的不确定项**：主线 `msm` 的双链路支持主要面向 bonded 宽屏；
   我们是"每链路各 1080"的 split 模式，需要确认 `dsi_manager` 的接线方式。
   参考：xiaomi-elish 用的是 `dual_cphy_video` 且已在主线跑通，可对照其 DTS 写法。
2. **面板无 init 序列**：现在靠 ABL 点亮。`fastboot boot` 场景 ABL 先点过屏，所以 simpledrm 有画面；
   等到冷启动（从分区启动）时，如果面板没被初始化过，可能全黑——这需要面板驱动自己完成上电时序，
   或在 initramfs 里保留一次"点屏"（技术债）。
