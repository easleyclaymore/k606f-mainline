# 内核改动说明

本目录是 YT-K606F 相对上游 [postmarketOS sm8250 内核](https://gitlab.postmarketos.org/soc/qualcomm-sm8250/linux)（tag `sm8250-7.2.0`，commit `71f4068cb`）的全部改动。

## 文件

| 文件 | 说明 |
|---|---|
| `k606f-kernel.patch` | 全部改动合并的 `git diff` patch（含新增文件），可直接 `git apply` 到上游树 |
| `sm8250-lenovo-k606f.dts` | 板级设备树（以 `sm8250-lenovo-q706f.dts` 为骨架） |
| `panel-lenovo-k606f.c` | 双 DSI 视频模式面板驱动（放入 `drivers/gpu/drm/panel/`） |

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
3. **DSI PHY 7nm PLL 修复** `drivers/gpu/drm/msm/dsi/phy/dsi_phy_7nm.c`
   - `dsi_pll_enable/disable_pll_bias` 里同时置/清 `DIGTOP_PWRDN_B`，否则 PHY digital top 掉电、PLL 锁不上
   - 上游对应补丁：Dmitry Baryshkov（freedreno）
4. **Kconfig/Makefile/panel-simple** —— 挂入新面板驱动

## 应用方式

```sh
git clone https://gitlab.postmarketos.org/soc/qualcomm-sm8250/linux.git
cd linux && git checkout sm8250-7.2.0
git apply /path/to/k606f-kernel.patch
# 或手动复制 dts / panel 驱动到对应位置
```
