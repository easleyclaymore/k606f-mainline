# 恢复能力演练记录（写刷 / 变砖自救）

日期：2026-10-28　　结论：**通过与刷写相关的全部三项**，可据此放开 `fastboot flash` 类操作。

## 为什么先做这个

用户授权"能确定恢复就允许自行刷写"。**恢复能力不能靠假设，必须真机演练**。本文就是演练证据。

## 三项判据与结果

| # | 判据 | 做法 | 结果 |
|---|---|---|---|
| 1 | 写入闭环：写进去 = 备份字节 | `fastboot flash boot_b boot_b.img`（镜像与原内容完全相同）→ 启动后读回 `sha256` | ✅ 读回 `579421e9…`，与备份逐字节一致 |
| 2 | 变砖自救：启动不了还能刷回来 | 把 `boot_a`（**活动槽**）头部清零 → 重启 → 自动回到 Fastboot → `fastboot flash boot_a boot_a.img` → 重启 | ✅ Android 正常启动，`sys.boot_completed=1`；`boot_a` 读回 `b0480183…`，与备份一致 |
| 3 | 进 Fastboot 的路径 | 观察失败启动后的落点 | ✅ 设备**自动回到 Fastboot**（无需按键）；物理按键组合未单独测试（已被自动回退覆盖） |

## 过程中的观测（都值得记住）

1. **`fastboot boot` 一个坏镜像不会变砖**：返回 `Booting OKAY` 后约 10 秒，设备停在 Fastboot。
   这正是 skill 里"`fastboot boot` 回 OKAY 却停在 Fastboot"的现象——本例原因是**启动镜像头无效**，不是设备树身份不匹配。
   → 对我们意义很大：第 3/4 步大量用 `fastboot boot` 迭代，这条路是安全的。
2. **坏 `boot_a` 启动失败后 ABL 自动回到 Fastboot**，没有反复重启，也没有自动切到 B 槽
   （`ro.boot.slot_suffix` 前后都是 `_a`，槽属性未被改动）。
3. **写入速度**：96 MiB 镜像 `Sending` 约 2.2 s（USB），`Writing` 仅 0.13 s（UFS）。
   → `fastboot boot` 一次往返约 12 秒，适合做快速迭代。
4. **数据无影响**：`/data` 19 G / 229 G，`/data/SubSystem/chrootDebian` 完好。
5. **没有验证**的部分：写启动分区是否重置 ABL 的重试计数（未取 `getvar` 的相关项）；
   vbmeta 链在自签镜像下的行为（要等我们自己造镜像时才测）。

## 备份与恢复的口令式清单（下次照做）

```bash
cd ~/Desktop/YT-K606F-mainline/private/backups/2026-10-28/images
sha256sum -c ../meta/sha256sums.txt          # 先验备份完整
adb reboot bootloader && fastboot devices
fastboot flash boot_a boot_a.img             # 恢复活动槽内核
fastboot reboot
```

## 结论

- 可以放开：`fastboot flash`（boot/dtbo/vbmeta 相关分区）、`fastboot erase`、写活动槽。
- **仍然需要单独确认**：`fastboot format userdata`、重建分区表这类**会清数据**的操作——
  设备能恢复 ≠ 数据能恢复。
