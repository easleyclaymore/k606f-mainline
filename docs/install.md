# 安装与启动

> **现状（第 4 步，显示尚未点亮）**：只支持 `fastboot boot` **临时启动** —— 不写任何分区，
> 重启即回 Android。安装到存储（分区、A/B 槽轮换、切换工具）属于第 5 步，**还没有做**，
> 本仓也还没有安装器。请不要照本文尝试写入分区。

## 前提

- Bootloader 已解锁（`verifiedbootstate=orange`、`fastboot getvar unlocked` 为 `yes`）。
- 主机已装 `fastboot`（`android-tools`）与 `adb`。
- **已完整备份设备分区**并校验。没有备份不要往下走。

```sh
fastboot devices                        # 应看到一台设备
fastboot getvar unlocked                # 期望 yes
```

## 一、打启动镜像

内核构建完成后（见 `docs/build.md`），把 `Image`、`dtb` 和 initramfs 打成 Android boot 镜像：

```sh
TOOLCHAIN=gcc SRC=<内核树> OUT=<输出目录> CFG=<本仓>/config/k606f-probe.config \
  <本仓>/tools/bringup.sh
```

产物是一个 boot 镜像（含内核 + 设备树 + 最小 initramfs）。

## 二、临时启动（不写分区）

```sh
fastboot boot <boot.img>
```

- `fastboot boot` 只在内存里跑，**掉电或重启就恢复原系统**，是最安全的验证方式。
- 启动后通过 USB 网络进 shell（initramfs 里带 telnetd），详见项目内部记录。
- 想反复调试：关机重进 fastboot 再来一次即可，不会改变设备上的任何分区。

## 三、风险与恢复

| 操作 | 风险 | 恢复 |
|---|---|---|
| `fastboot boot` | 极低（不写盘） | 重启 |
| `fastboot flash boot_a/boot_b` | 中：写错槽可能起不来 | 用备份镜像刷回；高通 ABL 在启动失败后通常自动落回 fastboot |
| `fastboot erase/flash dtbo_a` | **高**：Android 会因为缺 dtbo 起不来 | 必须刷回备份的 `dtbo_a`，见 `docs/return-to-android.md` |
| 格式化 userdata / 重建分区表 | **数据不可恢复** | 备份无用，只能重装系统 |

**通用恢复手段**：`Power + Vol-` 进 fastboot；设备完全起不来时，短接或按键组合进 EDL 是高通最后手段（需要签名 firehose，本项目不提供）。

## 四、将来（第 5 步）会补的内容

- 分区方案（根 60 GiB + home 取剩余空间）与 `userdata` 缩小/重建的取舍。
- A/B 活动槽镜像集轮换：Linux 与 Android 各占一套槽，切换只改活动槽。
- 安装器与切换工具，以及「按公开文档从零安装」的验收记录。
