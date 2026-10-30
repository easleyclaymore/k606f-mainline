# YT-K606F 主线 Linux 项目（AGENTS.md）

> 给下一个接手的人/agent。**不写当前进度**（进度看 `docs/status.md`）。
> 最后更新：2026-10-28

## 1. 设备与目标

| 项 | 值 |
|---|---|
| 机型 | Lenovo YT-K606F = **Yoga Tab 13** / 国行 YOGA Pad Pro，13" 2160×1350@60 |
| SoC | SM8250-AC（骁龙 870，kona），8 GB RAM / 256 GB UFS 3.1 |
| 出厂 | Android 12（SDK 31），内核 **4.19.136**（= 联想官方开源包 `yt_k606f_s100101_210209_row` 的版本，权威源） |
| 设备当前 | **已 OTA 到最新系统**：Android 12（SDK 31），内核 **4.19.157-perf+**。⚠ 这个版本号**不是出厂版本**，别把它当出厂证据；厂商开源包对应的是 4.19.136 |
| 连接 | `ssh p109`（地址/账号见私有工作区，公钥已配）；root：`su -c '…' </dev/null`（Magisk） |
| Bootloader | **已解锁**（`verifiedbootstate=orange`、`flash.locked=0`），物理 A/B 槽 |

**目标**：跑原生主线内核 + Ubuntu/KDE 桌面，与 Android 双系统共存。

## 2. 已确认的决策（2026-10-28，用户确认，不再重复问）

1. **路线 A：主线内核 + 双系统**。保留 Android 作为退路与充电环境；chroot 那套（Termux + Debian + XFCE）继续保留当兜底。
2. 工作区放桌面：`~/Desktop/YT-K606F-mainline/`。
3. 双系统方式：**A/B 活动槽镜像集轮换**（不改槽属性、不碰 `persist`/`frp`/`modemst`）。
4. 首版范围：内核 + 显示 + 触控 + Wi-Fi + GPU + Plasma 为 v0.1；音频/休眠/HDMI/传感器进 v0.2。
5. 桌面目标：Ubuntu + **KDE Plasma**（先 GNOME 过验收，再换 KDE）。
6. **分区方案（2026-10-28 用户确认）**：根 **60 GiB** + home 放最后取剩余空间（便于扩容）。
   ⚠ 待解决前提：`userdata` 占 229.4 GiB 且已有数据（含现有 Debian chroot 约 7 GiB），
   要给 Linux 让出空间必须缩小 `userdata` —— **能否无损缩小取决于它的文件系统类型**（f2fs 基本不支持离线缩容）。
7. **固件分发（2026-10-28 用户确认）**：**随项目分发**。需附逐文件清单（来源路径、sha256），
   并写明"权利人提出异议即删除"。本机唯一数据（MAC、蓝牙地址、校准）仍然不进仓库。
8. 待定：无人值守授权边界（见第 3 节）。

## 3. 授权边界（2026-10-28 用户授权，带前置条件）

用户授权：**允许** `fastboot flash` / `fastboot erase` / 写活动槽 / 改槽属性 / 格式化分区。

**前置条件（用户原话"如果你能确定刷机后能恢复"）**：必须先用真机演练证明恢复闭环，成立之后才动这些命令。
证明标准（缺一不可）：

1. **写入闭环**：`fastboot flash` 写一个分区 → 读回 → 与备份 **sha256 完全一致**。
   （第一次演练用 `boot_b` 写入与备份**逐字节相同**的镜像，内容不变，风险≈0。）
2. **回退闭环**：把 `boot_a` 写坏（或写一个已知非启动镜像）→ 从 Fastboot 用备份 `boot_a.img` 刷回 → 系统正常启动。
   这一步才真正证明"变砖可自救"。
3. **进 Fastboot 的物理路径**：确认按键组合（Power + Vol-，或 Power+Vol+/Vol- 三键菜单）在系统完全起不来时可用。
4. **备份完整性**：`private/backups/2026-10-28/meta/sha256sums.txt` 校验通过（已通过）。

在上述 1–3 全部有真机证据之前，默认仍是"只读 + `fastboot boot` 临时镜像"。

**状态：2026-10-28 已全部验证通过**（证据见 `docs/recovery-rehearsal.md`）：
1. ✅ `boot_b` 写回→读回 sha256 = `579421e9…`，与备份一致。
2. ✅ 把 `boot_a`（活动槽）写坏 → 重启失败 → 自动回 Fastboot → 刷回备份 → Android 正常启动，读回 `b0480183…`。
3. ✅ 失败启动后设备自动落回 Fastboot（槽位未被改动，`slot_suffix` 始终 `_a`）。

→ **即日起可自行执行 `fastboot flash` / `erase` / 写活动槽**，无需逐次询问。

- 任何会清数据的操作（格式化 userdata、重建分区表）**仍然要单独确认**：设备能恢复 ≠ 数据能恢复。
- 设备同一时刻只有一个操作者；每条设备命令带 serial。

## 4. 环境事实（2026-10-28 实测）

- 主机：Arch/CachyOS，12 核 / 31 GB / `/home` 剩 222 GB。
- **没有 aarch64-linux-gnu-gcc，也没有 sudo 权限**（sudo 要密码）。内核用 **LLVM=1（clang 23.1.3 + LLD）** 交叉编译。
- `ccache`、`bc`、`dtc` 由本地包解包到 `toolchain/`（无 root），通过 `toolchain/bin/` 包装脚本使用（内部设 `LD_LIBRARY_PATH`）。
- ccache：`out/ccache`，10 GB，`base_dir` 指向工作区，`hash_dir=false`。
- 网络：gitlab.postmarketos.org **极慢/不稳**；代理 `127.0.0.1:7897`（Clash 混合端口）可用；kernel 源码优先用清华镜像。

## 5. 目录

```
~/Desktop/YT-K606F-mainline/            私有工作区（只放记录 + 内部脚本，不放代码副本）
├── AGENTS.md              本文件
├── docs/                  状态表、工作日志、技术债、放弃方案、各功能文档
├── tools/                 env.sh（环境）、build-kernel.sh（构建）
├── sources/               参考源码（只读）：linux-7.2 上游、pmos-sm8250 配置
├── toolchain/             无 root 解包的工具（ccache/bc/dtc）；系统装好后可删
├── out/                   构建输出（含 ccache 与 kernel-ref-7.2）
├── private/               原始日志、备份、本机唯一数据（不进任何仓库）
├── 权威数据/              厂商开源包、运行时 FDT、评估/核验报告（只读取证，不改）
└── public/k606f-mainline/ 集成仓（独立 git，公开）：配置、dts/面板驱动、脚本、文档
```

⚠ 与 skill 推荐结构的偏差（已登记技术债 #17）：`kernel/` 目前不是独立 git 仓，改动以 `kernel/k606f-kernel.patch`
形式放在集成仓里；等改动规模超过「一个 patch + 2 个文件」时拆成独立内核仓并保留上游历史。

## 6. 代码规矩

- 内核改动将来进 `kernel/`（独立 git，保留上游历史）；板级 dts **每个数值取本机原厂证据**，不从参考设备抄。
- 临时的快速做法必须登记到 `docs/tech-debt.md`，与改动同一个提交。
- 提交署名：AI 只加 `Co-Authored-By:`；不代签 `Signed-off-by`/`Tested-by`。
- **推送、建仓、发版、改仓库可见性、重写历史，每一项都先问用户**（本地提交可以自己做）。
- 取证类结论必须带版本信息：源码提交 / 镜像哈希 / 场景 / 证据文件路径。
- **改完文档跑一次 `tools/check-docs.sh`**（查状态词合法、技术债表完整、AGENTS.md 必备条目、出厂版本没被 OTA 版本顶替）。
- 上机前在工作日志写实验四句话：只改了什么 / 结果为 A 排除了什么 / 结果为 B 排除了什么 / 用哪个机器指标判定；上机后当天补结果与证据路径。
- 不进公开仓：序列号、MAC、蓝牙地址、校准数据、固件原始镜像（未获授权时）、本机备份。
