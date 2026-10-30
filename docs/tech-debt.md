# 技术债表（唯一一份）

| # | 临时做法 | 位置 | 为什么暂用 | 影响/风险 | 退出条件 | 关联 |
|---|---|---|---|---|---|---|
| 1 | 用**上游 7.2 tarball** 做基线构建，而不是 pmOS sm8250 分支 | `sources/linux-7.2` | gitlab.postmarketos.org 直连约 10 KB/s，拉不动 | 基线不含 sm8250 社区补丁，只能验证工具链，不能代表最终基线 | pmOS 树拉取完成后，用同一份配置重建并对比 | 第 1 步 |
| 2 | ~~工具链用 LLVM=1~~ **已清**：装了 `aarch64-linux-gnu-gcc 16.2.0`，默认 gcc，`TOOLCHAIN=llvm` 保留作对照 | `tools/build-kernel.sh` | 用户提供了按需提权 | — | 已还：`out/kernel-gcc-7.2` 用 gcc 构通过 | 第 1 步 |
| 3 | ~~手动解包 ccache/bc/dtc~~ **已清**：系统已装 `ccache 4.14.1`/`bc`/`dtc`，脚本优先用系统版 | `toolchain/`（保留但不再使用） | — | — | 已还；`toolchain/` 可删 | 第 1 步 |
| 4 | 产品配置将关闭 `DEBUG_INFO`（基线暂沿用 pmOS 配置的 `=y`） | `out/kernel-ref-7.2/.config` | 基线要「原样」；调试信息让构建变慢、产物变大 | 产物大、构建慢；发布包会带 dwarf | 自建板级配置时改 `DEBUG_INFO_NONE` | 第 1 步 |
| 5 | `sources/pmos-sm8250` 是 `--depth 1` 浅克隆（grafted） | `sources/pmos-sm8250` | 直连太慢，先求能构 | 看不到补丁提交历史，无法按提交挑拣或做 `diff` 基线 | 需要提取补丁时 `git fetch --deepen` 或改全量克隆 | 第 1/3 步 |
| 6 | 基线构建用上游 tarball（`sources/linux-7.2`），非社区树 | `out/kernel-ref-7.2`、`out/kernel-gcc-7.2` | 第 1 步只求验证工具链 | 不含 sm8250 社区补丁 | 正式基线改用 `sources/pmos-sm8250`（已构通，见 `docs/step1-build-report.md`） | 第 1 步 |
| 7 | 探针/调试镜像带 `clk_ignore_unused pd_ignore_unused` | `out/boot-probe.img` cmdline | 借用固件画面路线需要，先求跑通 | 掩盖资源回收问题，做不了休眠/省电 | 原生显示接管后去掉，把资源写进真正的消费者 | 第 3/4 步 |
| 8 | 清空 `dtbo_a` 才能启动自有 DTB | `fastboot flash dtbo_a empty-dtbo.img` | 原厂 dtbo[11] 依赖上百个 vendor 符号，自洽 DTB 叠不上去 | Android 与 Linux 需要不同的 dtbo，切换时要跟着切 | 做出与 vendor overlay 兼容的符号表后去掉 | 第 3/5 步 |
| 9 | DT 里缺 `vdd-hba` 供电（UFS） | `sm8250-lenovo-k606f.dts` | 先用「假定常开」绕过 | UFS 供电依赖 bootloader 遗留状态，休眠/重上电风险 | 从原厂 DT 取真实供电映射补上 | 第 4 步 |
| 10 | 探针 initramfs 的 telnetd 跑几分钟后失联（gadget 仍在，内核活着） | `tools/build-initramfs.sh` | 只用于调试 | 调试窗口有限，每次失效要等超时重启 | 改成 inetd 模式或周期性重启 telnetd | 第 3/4 步 |
| 11 | `aux_bridge` 以 `-ENODEV` 失败但仍在 msm 组件链里 | DTS 关掉 DP/type-C 桥后仍存在 | 尚未找到创建者 | 可能挡住 msm 注册 DRM 设备 | 找到并禁用其父设备，或按上游补丁修 | 第 4 步 |
| 12 | `bringup.sh` 复用 `OUT` 变量给内核构建和 initramfs 构建，`OUT=kernel-msmmod` 时 initramfs 脚本会覆盖内核产物目录 | `tools/bringup.sh` | 第 3/4 步引入模块版内核时暴露 | 内核目录被 initramfs staging 覆盖，`Image`/`dtb` 丢失 | 已修：调用 `build-initramfs.sh` 时显式 `OUT=$W/out/initramfs` 隔离 | 第 4 步 |
| 13 | `build-initramfs.sh` 里「模块拷贝」「模块加载」两段被上一轮 patch 重复插入 | `tools/build-initramfs.sh` | 编辑脚本时未查重 | init 里 insmod 跑两遍、cpio 里重复拷模块 | 已删重复块 | 第 4 步 |
| 14 | 并行启动多个 `build-kernel.sh` 会破坏 OUT 目录（`init/.main.o.d: Not a directory`） | `tools/build-kernel.sh` | 后台任务被 SIGTERM 后误重发 | flock 未挡住第二次构建，两个进程同时写 | 确保同一 OUT 只起一个构建进程；flock 前先检查 | 第 4 步 |
| 15 | 取证用的 `~/amp/fdt/fdt.dts` 是自写 `fdtdump.py` 生成的，**非字符串属性解析错位**：3 cell 的 GPIO 属性、`#size-cells` 等被渲染成 `"', '', '', 'e', …"` 乱码 | `~/amp/fdt/fdt.dts`、`~/amp/fdt/fdtdump.py` | 当时图快，自写 dumper | **凡从该文件读 GPIO 编号 / 中断号 / cell 数的结论都不可信**；面板时序、分辨率为字节数组与单值，已用 dtc 复核无误，其余未复核 | 一律改用 `dtc -I dtb -O dts`（正确版 `~/amp/fdt/fdt-clean.dts`，2026-10-30）；`fdtdump.py` 作废，不要再生成新结论 | 第 2/4 步 |
| 16 | 板级 dts 里触控与音频还是 q706f 骨架抄来的：`goodix,gt9916` 挂在 SPI13、4×`cirrus,cs35l45` | `kernel/sm8250-lenovo-k606f.dts` | 建 dts 时以 q706f 为骨架，这两项当时未取证 | 触控总线与型号都错（真身 GT738x on `i2c@a94000` addr `0x5d`）；音频功放型号错（厂商 defconfig 无 cs35l45）。不影响当前显示排查，但会在第 6 步集中爆 | 真值已由本机 FDT 给出；**第 4 步点亮通过后一次性改**，避免和显示排查混变量 | 第 4/6 步 |
| 17 | `kernel/` 不是独立 git 仓：内核改动以 `kernel/k606f-kernel.patch` + 两个未跟踪源文件的形式放在**集成仓**里，没有上游历史 | `kernel/`（`public/k606f-mainline` 内） | 第 3/4 步改动只有「板级 dts + 面板驱动 + 一个 patch」，先求能构能跑 | 偏离 skill 的目录纪律（内核仓独立、保留上游历史）；patch 与源文件双份存在，容易不一致；上游更新时无法 rebase | 改动规模超过「一个 patch + 2 个文件」，或需要向上游提补丁时，拆成 `kernel/` 独立仓（基于 pmOS sm8250 tag，保留历史） | 第 3/4 步 |

规则：加临时做法和登记在这里必须在同一次改动里完成。功能「跑通」不等于债还清。
