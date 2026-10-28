# 分区表与启动链（本机实测）

> 来源：`private/backups/2026-10-28/`（GPT 头、boot/dtbo/vbmeta 整分区镜像、`meta/partitions.md`）
> 全部只读采集，未写入任何分区。采集时间 2026-10-28。

## 0. 三条必须先记住的事实

1. **所有 LUN 的逻辑扇区是 4096 字节，不是 512**。GPT 里的 LBA 一律按 4 KiB 计。
   任何手写 `dd` 都要 `bs=4096`，或者按字节算偏移；用 512 会整体错位（第一次采集就踩了）。
2. **物理 A/B 双槽 + super 动态分区**，不带 VAB 快照。当前 `current-slot: a`、`slot-count: 2`。
3. **B 槽的固件比 A 槽旧一代**（vbmeta_system：A = 14.0.494 / 2023-02，B = 14.0.384 / 2022-10），
   且 B 槽所有分区的类型 GUID 都是同一个通用值 `77036cd4-…`（出厂占位式分区）。
   → B 槽可以当 Linux 落点，但不能当"原厂同版备份"，恢复只认 A 槽。

## 1. LUN 一览

| LUN | 容量 | 说明 |
|---|---|---|
| sda | 253 679 894 528 B（236.3 GiB） | 主盘：super、userdata、recovery、vm-system、persist |
| sdb | 8 MiB | 小分区（`ALIGN_TO_128K` 类） |
| sdc | 8 MiB | 同上 |
| sdd | 32 MiB | `cdt`、`ddr` 等 |
| sde | 2 GiB | 启动链全在这里：xbl/abl/tz/hyp/modem/boot/dtbo/vbmeta |
| sdf | 32 MiB | `ALIGN_TO_128K_2` 等 |

## 2. sda：与我们相关的分区

| 分区 | 起始 LBA(4K) | 大小 | 用途 / 备注 |
|---|---|---|---|
| persist | 8 | 32 MiB | **必须备份**：校准、传感器、MAC 等本机唯一数据 |
| lenovocust | 8968 | 200 MiB | 联想定制区（含 countrycode 等） |
| recovery_a / _b | 64264 / 89864 | 100 MiB ×2 | 原厂 recovery（**不要当 Linux 落点**，bootloader 只当 ramdisk） |
| vbmeta_system_a / _b | 115464 / 115480 | 64 KiB ×2 | AVB 链：system 侧 |
| metadata | 115496 | 16 MiB | 双系统切换元数据（AOSP 定义） |
| vm-system_a / _b | 119592 / 152360 | 128 MiB ×2 | 联想预置的 Linux 虚机镜像（不可用作本项目根） |
| super | 217896 | 6144 MiB | 动态分区（system/vendor/product/odm） |
| userdata | 1790760 | 229.4 GiB | 可用空间都在这（当前用了约 19 GiB） |

`userdata` 起始字节 = 1790760 × 4096 = **7 334 952 960**（约 6.83 GiB 偏移）。
给 Linux 分区只能从 **userdata 尾部**截，不要动 super。

## 3. sde：启动链

| 分区 | 起始 LBA(4K) | 大小 | 备注 |
|---|---|---|---|
| aop_a / tz_a / hyp_a | 6 / 134 / 1158 | 0.1~1 MiB | 固件组件 |
| modem_a | 3206 | 395 MiB | modem 固件（`/vendor/firmware_mnt` 就是它） |
| abl_a | 113798 | 1 MiB | **ABL = 真正干活的 bootloader（fastboot 实现）** |
| boot_a | 130566 | 96 MiB | Android 内核 + ramdisk + DTB（v2 头） |
| vbmeta_a | 155450 | 64 KiB | AVB 根 |
| dtbo_a | 155466 | 24 MiB | 12 个 overlay，`androidboot.dtbo_idx=11` |
| vm-linux_a | 162138 | 32 MiB | 联想虚机内核位（空/占位） |
| boot_b / vbmeta_b / dtbo_b | 301434 / 326318 / 326334 | 96 MiB / 64 KiB / 24 MiB | B 槽（旧一代固件） |

## 4. boot 镜像头（原厂 A 槽，`unpack_bootimg`）

```
magic ANDROID!  header version 2   page size 4096
kernel     41 650 192 B   (0x027b8810)
ramdisk     1 634 623 B   (A 槽；B 槽 1 149 334 B)
dtb         1 601 400 B   @ 0x01f00000
cmdline     earlycon=msm_geni_serial,0xa90000 androidboot.hardware=qcom … console=ttyMSM0,115200n8
```

**关键**：原厂用 **v2 头 + 内嵌 DTB**，没有 init_boot / vendor_boot。
→ 我们的 RAM 测试镜像和持久启动镜像都按 v2 造（kernel + ramdisk + DTB），这与 skill 里
「v2 做 RAM 测试、持久按原厂结构」的建议一致，而且这里原厂本身就是 v2，省掉三件套的复杂度。

## 5. dtbo（`dtbo_a.img`）

- 格式：**大端** DTBO 表（`magic 0xd7b7ab1e`），`header 32 B`、`entry 32 B`、**12 个 entry**、page 4096
- 每个 entry 的 `id/rev` 全是 `0`，说明**只按索引选**（`androidboot.dtbo_idx=11` → 用第 12 个）
- 大小分布：168–260 KiB，第 11 个 224 826 B @ offset 2 636 921

→ 做板级 dts 时，必须知道 ABL 会把 **dtbo[11]** 叠到我们的 DTB 上（skill 里「符号表 + 叠加失败静默拒绝」那个坑）。
本机已导出运行时合并后的 FDT（`~/amp/fdt/fdt.dts`），可作为"叠加结果"的正对照。

## 6. AVB 链（`avbtool info_image`）

| 分区 | 算法 | 公钥 sha1 | rollback index | flags | 子链 |
|---|---|---|---|---|---|
| vbmeta_a / _b | SHA256_RSA4096 | `c2aefc2d…` | 0 | **0** | chain → `vbmeta_system`（rollback loc 2） |
| vbmeta_system_a | SHA256_RSA2048 | `cdbb7717…` | 1675209600（2023-02-01） | 0 | 属性：fingerprint 14.0.494 |
| vbmeta_system_b | 同上 | 同上 | 1659312000（2022-10-17） | 0 | 属性：fingerprint 14.0.384 |

- `flags = 0`：没有 `HASHTREE_DISABLED`、也没有 `VERIFICATION_DISABLED`——**靠解锁状态绕过校验**，不是靠 flag。
- 自签镜像时 rollback index 不能低于本机现有值（A 槽 1675209600），否则会被拒。

## 7. fastboot 只读查询结果（进 bootloader 后取值，未做任何写入）

| 变量 | 值 | 用途 |
|---|---|---|
| `max-download-size` | **805306368（768 MiB）** | `fastboot boot` 能推多大镜像——我们的 RAM 镜像约 45–60 MB，余量充足 |
| `unlocked` | **yes** | 已解锁，测试密钥可被接受 |
| `current-slot` | a | 当前活动槽 |
| `slot-count` | 2 | 物理 A/B |
| `product` | kona | — |
| `hw-revision` | 20001 | 与 DTB 里 `qcom,msm-id = <0x164 0x20001>` 的第二个值一致 |
| `variant` | SM8 UFS | — |
| `partition-size:boot_a` | 0x6000000（96 MiB） | 与 GPT 一致 |
| `partition-size:dtbo_a` | 0x1800000（24 MiB） | — |
| `partition-size:super` | 0x180000000（6 GiB） | — |
| `partition-type:boot_a` | raw | 不是 ext4，直接写镜像 |
| `off-mode-charge` | 1 | **插电自动开机**，关机充电会反复自启（skill 里那个坑本机成立） |
| `has-slot:boot` | yes | — |
| `battery-voltage` | 4160 mV | — |

## 8. 做 DTB 时必须匹配的身份值（来自运行时 FDT）

```
compatible   qcom,kona-mtp / qcom,kona / qcom,mtp     ← ABL 会改写成平台通用名
qcom,msm-id  = <0x164 0x20001>
qcom,board-id = <0x3010008 0x0>
```

ABL 在跳转前按这两个值校验；不匹配就静默停在 Fastboot（`fastboot boot` 仍回 OKAY）。
