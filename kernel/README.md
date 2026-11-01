# 内核改动说明

本目录是 YT-K606F 相对上游 [postmarketOS sm8250 内核](https://gitlab.postmarketos.org/soc/qualcomm-sm8250/linux)（tag `sm8250-7.2.0`，commit `71f4068cb`）的全部改动。

## 文件

> 快照基线：本目录对应内核树本地提交 `e3391aab7`（双 DSI 链路、与 bootloader 工作态
> 逐位对齐的调查状态，见下"调查状态"）。之后的单链 bisect 实验未包含在内。

| 文件 | 说明 |
|---|---|
| `k606f-kernel.patch` | 全部改动合并的 `git diff` patch（含新增文件），可直接 `git apply` 到上游树 |
| `sm8250-lenovo-k606f.dts` | 板级设备树（以 `sm8250-lenovo-q706f.dts` 为骨架） |
| `panel-lenovo-k606f.c` | 双 DSI 视频模式面板驱动（放入 `drivers/gpu/drm/panel/`） |
| `psa8331.c` | PSA8331A（SN65DSI86 + PS8331）MIPI-DSI→eDP 桥驱动（放入 `drivers/gpu/drm/bridge/`） |

## 改动清单

1. **板级 dts** `arch/arm64/boot/dts/qcom/sm8250-lenovo-k606f.dts`（新增）
   - 双 DSI split（无 DSC），面板 2160×1350@60（per-link 1080×1350）
   - `qcom,dual-dsi-mode` + `qcom,sync-dual-dsi` + `qcom,master-dsi`（master 在 dsi0）
   - dsi1 补 `assigned-clock-parents = <&mdss_dsi0_phy ...>`（slave 用 master 的 PHY PLL，对照 elish）
   - PM8150L LPG PWM 背光（`pwm-backlight`，channel 3 = base 0xbc00）
   - 面板 `reset-gpios = <&tlmm 75 GPIO_ACTIVE_LOW>`
2. **面板驱动** `drivers/gpu/drm/panel/panel-lenovo-k606f.c`（新增）
   - `panel-simple` 不支持 dual-DSI，故自写；参考 `panel-novatek-nt36532.c` 的 dual-DSI 注册模式
   - 显式注册并 attach 第二个 DSI device（`dsi[1]`），这是 msm `component_add` 能齐集的必要条件
   - 不设 `MIPI_DSI_MODE_NO_EOT_PACKET`（bootloader 工作态实测 TX_EOT_APPEND=1，见"调查状态"）
3. **eDP 桥驱动** `drivers/gpu/drm/bridge/psa8331.c`（新增）
   - 同一条 I2C 总线上两颗芯片：TI SN65DSI86 @0x2C（真正的 DSI→eDP 桥）+ PS8331 @0x33（端口 mux）
   - 分步上电（21..25 子步）+ 59 条 eDP 初始化表 + semi-auto 链路训练（0x96=0x09 轮询 0x01）
   - 调试用 sysfs `power_stage`（0/1/2/3/4/6），正式化前应移除
4. **DSI PHY 7nm PLL 修复** `drivers/gpu/drm/msm/dsi/phy/dsi_phy_7nm.c`
   - `dsi_pll_enable/disable_pll_bias` 里同时置/清 `DIGTOP_PWRDN_B`，否则 PHY digital top 掉电、PLL 锁不上
   - 上游对应补丁：Dmitry Baryshkov（freedreno）
   - 另：clk_trail/clk_pre 硬编码 -1 对齐厂商固定值（待改成 DTS 覆盖）
5. **DSI host/controller 对齐 bootloader 的修正**（`dsi_host.c`/`dsi_manager.c`/`dsi.c`）
   - 显式写 `HS_TIMER_CTRL=0x4ea60`（主线从不设置，video mode 下误判 HS_TX_TIMEOUT）
   - bonded 双链：两台控制器延迟启动、背靠背写 CTRL.ENABLE（调查机制，见下）
   - dsi1 的 byte/pclk 时钟源改挂 dsi0 的 PHY PLL（与 bootloader 工作态一致）
6. **Kconfig/Makefile/panel-simple** —— 挂入新面板驱动

## 调查状态（2026-11-01）

显示链路当前状态：DSI 控制器使能 ✅、eDP 桥上电+配置+链路训练 ✅（与 bootloader 工作态
逐字节一致）、DP 侧稳定输出 ✅——但屏幕仍黑。SN65DSI86 报 CHA/CHB INVALID_LENGTH_ERR
（收到的视频行长度与编程值不匹配），且每次引导随机一条链报错。已排除：EOT 包缺失
（已修）、跨 PLL 频漂（时钟域已统一）、PHY timing、桥配置、DPU 拆分配置、pclk/byte
速率比（实测精确 4/3）。下一步是单链 bisect 与 panic 现场取证。内核树里有调查用探针
（`[K606F]` 打印）与实验机制，正式化前应清理。

## 应用方式

```sh
git clone https://gitlab.postmarketos.org/soc/qualcomm-sm8250/linux.git
cd linux && git checkout sm8250-7.2.0
git apply /path/to/k606f-kernel.patch
# 或手动复制 dts / panel 驱动到对应位置
```
