# 第 3 步：首次启动（fastboot boot 探针）

日期：2026-10-28　　状态：**真机通过**（内核、UFS、USB 网络、simpledrm 画面、telnet shell 全部真机验证）

## 目标与判据

| 判据 | 结果 |
|---|---|
| ABL 接受并交接（设备离开 fastboot） | ✅ |
| 内核启动、initramfs 的 `/init` 执行 | ✅（init 里的 USB gadget 配置成功，主机侧枚举到 0x1d6b:0x0104 "K606F probe"） |
| 主机能连设备 shell | ✅ telnet 172.16.42.1:23 进入 BusyBox ash，`uname -a` = `7.2.0-g71f4068cb254 aarch64` |
| 超时自动回 Android | ✅（60 秒短超时版本验证过：到期 `reboot -f`，Android 正常启动） |

## 产物

| 文件 | 说明 |
|---|---|
| `sources/pmos-sm8250/arch/arm64/boot/dts/qcom/sm8250-lenovo-k606f.dts` | 板级 dts（以同厂 `q706f` 为骨架） |
| `tools/build-initramfs.sh` | 生成 BusyBox initramfs（USB NCM + telnetd + httpd + 超时重启） |
| `tools/usbnet-host.sh` | 主机侧配 IP、ping、拉 dmesg |
| `out/boot-probe.img` | v2 头启动镜像（kernel + initramfs + dtb） |

## 关键坑（按踩到的顺序）

### 1. `fastboot boot` 报 `Load Error` 其实是会话状态坏了
现象：`Booting FAILED (remote: 'Failed to load/authenticate boot image: Load Error')`，
但**用原厂 boot_a.img 也一样失败**。
原因：fastboot 会话放了十几分钟后状态不可靠（skill 里记过类似现象）。
解决：`fastboot reboot-bootloader` 重新开会话，同一条命令立刻 `OKAY`。
→ **规矩：每次 `fastboot boot` 前先 `fastboot reboot-bootloader`。**

### 2. 【最关键】`mkbootimg` 默认 `--base 0x10000000`，本机要求 `0x0`
现象：`fastboot boot` 回 `OKAY`，设备 10 秒后直接回到 Fastboot，没有任何其它线索。
对比原厂镜像头才发现加载地址整体偏了 0x10000000：

| 字段 | 原厂 | 我们的（错） | 修正后 |
|---|---|---|---|
| kernel load | `0x8000` | `0x10008000` | `0x8000` |
| ramdisk load | `0x1000000` | `0x11000000` | `0x1000000` |
| tags | `0x100` | `0x10000100` | `0x100` |
| dtb addr | `0x1f00000` | `0x11f00000` | `0x1f00000` |

解决：`mkbootimg … --base 0x0`。
→ **教训：做启动镜像时先把原厂镜像头逐字段抄下来，不要信工具默认值。**

### 3. 板级 DTB 的身份与 dtbo 叠加
- 原厂 boot 镜像里的 DTB 是**基础树**（`compatible="qcom,kona"`、`board-id=<0 0>`、`memory` 节点留空待 ABL 填），
  真正的板级信息来自 **dtbo[11] 叠加**（9582 行，需要 `spmi_bus`/`mdss_mdp`/`tlmm` 等上百个 vendor 符号）。
- 我们的 DTB 是自洽的，ABL 无法把那个 overlay 叠上去 → 用 **空 dtbo**（合法头 + 0 entry）或 `fastboot erase dtbo_a` 绕开。
- 实测：**dtbo_a 被清空后 Android 仍能正常启动**（15:07 验证）。所以 dtbo 可以作为双系统的「切换开关」：
  Android 用原厂 dtbo，Linux 用空 dtbo，二者随镜像集一起切换。

### 4. Alpine 的 busybox-static **没有编译 telnetd/httpd/nc**
现象：ping 通了，但 23/80 端口全关。
排查：`strings busybox | grep ^telnetd$` 为空 —— Alpine 版把这些 applet 关掉了。
解决：换 **Ubuntu ports 的 `busybox-static`（1.37.0，1.9 MB，含 telnetd/httpd/ifconfig）**，
放在 `tools/vendor/busybox-aarch64-static`。同时把 `ip addr add` 改成 `ifconfig`（Ubuntu 版也未必带 `ip`）。

### 4b. busybox 的 `ip` 不支持 `-o`
initramfs 里用 `ip -o link | grep usb[0-9]` 找不到网卡 → 设备侧没配 IP → 主机侧 `NO-CARRIER`。
解决：直接看 `/sys/class/net/usb0`。

## 首次启动实测结果（15:31）

```
Linux (none) 7.2.0-g71f4068cb254 #1 SMP PREEMPT ... aarch64 GNU/Linux
Hardware name: Lenovo Yoga Tab 13 (YT-K606F) (DT)
ufshcd-qcom 1d84000.ufshc -> scsi host0: ufshcd      # UFS 起来了，/sys/block 有 sda..sdf
Console: switching to colour frame buffer device 270x84
simple-framebuffer 9c000000.framebuffer: [drm] fb0: simpledrmdrmfb
```

失败项（第 4 步的工作）：`DSI PLL(0) lock failed, status=0x00000000` —— 原生 DSI/面板接管尚未做，
画面目前来自 simpledrm。完整日志：`private/step3/dmesg-7.2.0-firstboot.txt`。

收尾时已把 `dtbo_a` 刷回原厂（哈希 `ada89e08…` 与备份一致），Android 正常启动、`chrootDebian` 完好。

## 下一步

1. 等新 initramfs（已修正网卡检测）跑起来，验证 telnet / http 能取到 dmesg。
2. 有了 shell 之后：确认 UFS 是否枚举（`/sys/block/sda`）、PMIC/稳压器是否正常、
   把 `dmesg` 全量存证，作为第 4 步显示接管的基线。
3. dtbo_a 目前是空的；交付给 Android 使用前需要把 `private/backups/.../dtbo_a.img` 刷回。
