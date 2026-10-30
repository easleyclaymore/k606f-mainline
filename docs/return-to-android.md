# 回到 Android

只要 Linux 是用 `fastboot boot` 临时启动的，**回到 Android 就是直接重启**：什么都没写进分区。

需要额外处理的只有一种情况：

## 刷过 dtbo 的情况

第 3 步为了让自有设备树能叠上去，需要先清空 `dtbo_a`（原厂 dtbo 依赖大量 vendor 符号，自洽 DTB 叠不上去）。
清空之后 Android 会因为缺 dtbo 起不来，**必须刷回备份**：

```sh
fastboot flash dtbo_a <备份目录>/dtbo_a.img
fastboot reboot
```

- 备份镜像的 sha256 应与备份时的记录一致，校验通过再刷。
- 两个槽都有 dtbo：`dtbo_a` 和 `dtbo_b`。只刷活动槽通常就够，但建议两个都保持完整。
- 同理，凡是 `erase` 过的原厂分区，恢复方式都是「刷回备份」，不要指望系统自愈。

## 检查设备当前状态

```sh
fastboot getvar current-slot      # 当前活动槽（示例：a）
fastboot getvar slot-count        # A/B 槽数量
adb devices                       # Android 起来后应能看到设备
```

## 如果 Android 起不来

1. 长按电源键关机，再按 `Power + Vol-` 进 fastboot；
2. 确认活动槽，把该槽的 `boot` 与 `dtbo` 从备份刷回；
3. 仍有问题就检查是否 `erase` 或写过 `vbmeta`（需要一并刷回备份）。

> 以上操作的前提是**你有一份校验过的完整分区备份**。没有备份又清过原厂分区，恢复只能靠官方固件重刷。
