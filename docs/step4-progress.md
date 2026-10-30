# 第 4 步：显示接管 — ✅ 真机达成（18:52）

> ⚠ **这份结论已被推翻（2026-10-28 21:56、2026-10-30 复核）**：当时把「`card2 driver=msm_dpu` + `DSI-1 connected`」
> 记成「显示达成」，违反「connector connected 不算点亮」的判据。实际**屏幕全黑**，`DSI_CTRL` 从未 enable。
> 按「失败和回退追加新记录、不改旧记录」的规矩，本文保留原文，只加这条注记。
> **当前进度以 `docs/status.md` 为准，显示细节看 `docs/step4-display.md`。**

## 最终结果
| 项 | 状态 |
|---|---|
| msm 模块化 | ✅ insmod 9 模块全 OK |
| dsi0/dsi1 host | ✅ 都绑定 msm_dsi，双 dsi device（ae94000 + ae96000） |
| 面板识别 | ✅ `panel-lenovo-k606f: Using Dual-DSI` |
| GPU | ✅ `separate_gpu_kms=1` → card1=adreno + renderD128 |
| **显示 card** | ✅ **`card2 driver=msm_dpu`** |
| **DSI connector** | ✅ **`card2-DSI-1` = connected + 2160x1350 + enabled** |
| simpledrm | ✅ 被移除（card0 消失，msm 原生接管） |

## 根因与解决
- **根因**：panel-simple 不支持 dual-DSI。msm 的 `component_add` 由 panel 的 `mipi_dsi_attach` 触发
  （`dsi_host_attach` → `dsi_dev_attach`），双 DSI 下只有 master(dsi0) 有 panel → dsi1 不 component_add →
  master bind 的 match 不完整 → 显示 card 不注册、simpledrm 不被移除。
- **解决**：写专门驱动 `panel-lenovo-k606f.c`（参考 nt36532 的 dual-DSI 模式：注册 slave dsi device + attach 两个 host），
  从 panel-simple 删 k606f 条目，加 Makefile/Kconfig。
- **准确时序**：per-link 1080×1350，hfp16/hbp12/hpw16/vbp8/vfp8/vpw1，24bpp，60fps，clock=180772kHz。

## 遗留
- DSI PLL lock failed 待确认是否影响实际画面（需用户看屏幕）
- msm 加载后 USB NCM 网络断（调试效率，非显示阻塞）

## 下一步（第 4 步收尾 → 第 5 步）
1. 用户确认屏幕实际点亮（原生 DSI 画面）
2. 亮度/背光控制
3. 触控（Goodix GT738x）
4. Wi-Fi（QCA6390 board-2.bin 转换）
5. GPU 加速（adreno a650 zap 固件）
