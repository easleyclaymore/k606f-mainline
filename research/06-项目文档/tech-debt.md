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
| 18 | ~~文档两份同步副本~~ **已清（2026-10-30）**：按 skill 拆分职责 —— 内部记录（状态表/工作日志/技术债/放弃方案/各步骤文档/分区与恢复记录）只在工作区 `docs/`；集成仓 `docs/` 只留对外文档（`build.md`、`install.md`、`return-to-android.md`），并已 `git rm` 掉 10 份内部文档 | `docs/`（工作区）、`public/k606f-mainline/docs/` | 早期集成仓是从工作区整目录推上去的 | （已消除）此前两边靠手工 cp 同步，容易漂移且接手者不知道信哪份；公开仓还带着调试通道、备份路径等内部细节 | 已还：`tools/check-docs.sh` 的第 3 项会阻止内部记录再混进集成仓 | 全程 |

| 19 | `tools/` 脚本在工作区与集成仓各有一份，且已发生漂移（2026-10-31 核查：`bringup.sh`/`build-kernel.sh` 两边一致；**`build-initramfs.sh` 工作区版多出整个 B 阶段 `blmode` 探针块，集成仓版没有**——B19–B44 的探针镜像全部用工作区版构建） | `tools/`、`public/k606f-mainline/tools/` | 集成仓推上去时整目录复制，之后两边都直接改 | 两份会漂移，且"哪份是实际路径"已反转：**上机探针脚本（build-initramfs/bringup）以工作区为准，对外构建/安装文档引用的脚本以集成仓为准**，别再互相拷 | 拆分职责后删掉另一份：上机编排/采集脚本只留工作区（不对外），对外构建/打包/安装脚本只留集成仓 | 第 4/5 步 |
| 20 | msm 源码树里带着 `[K606F]` 探针（`dsi.c`/`dsi_manager.c`/`dsi_host.c`/`dpu1/`共 **23 处** `pr_info`——B50-B52 又加了 split_config/engine_start 打点），构建出来的内核版本号带 `-dirty` | `sources/pmos-sm8250`（已随本地提交序列入库） | 用来一次性看清 bridge 链装配与 DSI 使能路径，比开 `drm.debug=0x1f` 省事且不会被 debug 掩码过滤 | 日志噪声；版本号 dirty 会让「镜像对应哪个源码状态」变模糊；不适合长期带着 | B 阶段（eDP 桥）实现并验证后，把探针整理成 `private/patches/dsi-probe.patch` 存档，源码树回干净状态 | 第 4 步 |
| 21 | **把新驱动编成 builtin（`CONFIG_DRM_PSA8331=y`）直接上机**，结果整机在 init 之前挂死、USB 不枚举、按键无响应（100% 电量也救不回） | 已改：`CONFIG_DRM_PSA8331=m`，且 probe 里 GPIO 用 `GPIOD_ASIS` 不主动拉线 | 当时想让桥在开机时就绪，省掉手动 insmod | builtin probe 一旦出问题会把唯一的调试通道一起关掉，等于设备失联；这次代价是设备从 14:47 起不可达 | 规则化：新驱动**一律先编成模块**手动 insmod；每次上机先用已知好镜像确认设备能起，再试新镜像 | 第 4 步 |
| 22 | `tools/bringup.sh` 的默认 `OUT`/`CFG` 与真实上机镜像不一致：默认取 `out/kernel-pmos-7.2`（msm **内建**、内核 `7.2.0-g71f4068`），而真机验证过的镜像来自 `out/kernel-msmmod` + `sources/k606f-probe.config`（msm `=m`、内核 `73131af`） | `tools/bringup.sh` 第 1、3 步 | 脚本是基线阶段写的，后来调试改成另一个 OUT/CFG，但只靠命令行传参，没回写默认值 | **直接跑 `tools/bringup.sh` 会静默打出一个内核完全不同的镜像**，ramdisk 里的 `.ko` 与新内核版本不匹配（insmod 会全部失败），而且构建不报任何错；2026-10-31 预检时靠比对 `kernel_image` 哈希才发现 | 把 `OUT`/`CFG` 的默认值改成当前上机路径，或在打包后校验 `kernel_version` 与 ramdisk 模块的 vermagic 一致；另加一条：每次上机镜像归档到 `private/images/`（同名 `out/boot-probe.img` 会覆盖，10-30 的已知好镜像已被覆盖丢失） | 第 4 步 |
| 23 | 板级 dts 里 4 个 `cs35l45/cs35l41` 功放节点与 `audio_1p8_supply`/`avdd_1p3_supply` 等 regulator 是从别的板子抄来的：**原厂 FDT 里没有 cs35l45**，且 `audio_1p8_supply` 抢了桥的 `boost-nbyp-en`(gpio49)。当前处置：`&i2c5` 改 `disabled`（原厂就是 disabled）、该 regulator 去掉 `gpio`/`pinctrl` 只保留 `always-on` | `sources/pmos-sm8250/.../sm8250-lenovo-k606f.dts`（`&i2c5`、`audio_1p8_supply`） | 为了先把 gpio12(桥的 `mipi-to-edp-en`) 和 gpio49 让出来做显示，音频不在 v0.1 范围 | 本机音频链路目前完全是错的（总线、地址、使能脚都可能不对），`&i2c5` 关掉后功放不会被探测 | 做音频（v0.2）时按原厂 FDT 重建：确认功放到底挂在哪条总线/是否用 SPI、`audio_1p8_supply` 的真实使能脚 | 第 6 步（音频） |
| 24 | 桥驱动里加了调试用 sysfs 开关 `/sys/bus/i2c/devices/1-0033/power_stage`（0/1/2/3/4 与 21..25 子步），可无视 DRM 直接分步上电 | `sources/pmos-sm8250/drivers/gpu/drm/bridge/psa8331.c`（`psa8331_power_stage` + `DEVICE_ATTR_WO(power_stage)`） | 桥上电会整机复位且没有任何日志可读；分步 + 宿主侧 ping 是唯一能定位到具体动作的办法 | 暴露一个能直接驱动电源序列的写接口，正式镜像里不该有 | 桥稳住、能出画面后删掉 sysfs 与子步，只留 `pre_enable` 里那条固定序列 | 第 4 步 |
| 25 | `tools/build-initramfs.sh` 里的 `k606f.blmode` 脚本块被重复插入两次（编辑失误），**实验会并发跑两遍**：两条 `insmod` + 两组寄存器写抢同一条 I²C | `tools/build-initramfs.sh`（init 生成处） | 一次 python 替换命中了两处相同锚点 | B19 就是在重复状态下跑的，该轮结果不可信 | 已去重；`check-docs.sh` 第 10 项会检查「关键块唯一」，重复即报错 | 第 4 步 |
| 26 | `dsi_phy_7nm.c` 里把 `clk_trail`/`clk_pre` 硬编码各减 1 去对齐厂商固定值（B41），**对所有 7nm PHY 面板生效**，不是 per-board 的 | `sources/pmos-sm8250/drivers/gpu/drm/msm/dsi/phy/dsi_phy_7nm.c`（`dsi_phy_hw_v4_0_enable`） | 厂商 `qcom,mdss-dsi-panel-phy-timings` 是固定值，主线 v4 公式算出来各 +1；为了快速对齐厂商 ground truth 先硬编码 | 会影响其它 SM8250 面板（如 elish）的 PHY timing；B41 已证明它**不是** status=5 的根因 | 改造成 DTS 属性覆盖（在面板节点加 `qcom,mdss-dsi-panel-phy-timings`，PHY 驱动解析后覆盖计算值）；在此之前保留硬编码（对齐厂商总没错） | 第 4 步 |
| 27 | `dsi_host.c` 里的 **engine_deferred/pending_ctrl_data 双引擎同步启动机制**（B50 引入）：bonded 时 power_on 只做配置，CTRL.ENABLE 由 `msm_dsi_host_engine_start()` 背靠背写 | `sources/pmos-sm8250/drivers/gpu/drm/msm/dsi/dsi_host.c`、`dsi_manager.c` | 追查桥 INVALID_LENGTH 时的实验机制（B51 实测间隔 3µs→B55 亚µs） | 上游没有这套语义；若后续证明启动相位不是根因，应整体回退 | 单链 bisect（B56+）出结论后决定去留；保留期间每次改 dsi enable 路径都要回归验证 | 第 4 步 |
| 28 | **psa8331.c 的 eDP 表混入了厂商注释掉的 ASSR 变体表 7 条**（0x64/0x74-0x78/0xEE=一次 DPCD 写事务 + 延时），厂商活跃表 `dsi86_init[]` 没有它们；"缺这段训练锁不上"的 B28 结论是在地址 bug 时代得出的，已作废 | `sources/pmos-sm8250/drivers/gpu/drm/bridge/psa8331.c`（`psa8331_edp_init` 中段） | 照抄厂商表时合并了两份 | 桥多收一次未知 DPCD 写；0x64/0x76 的"差异"实为残留 | 单链/双链出图后做一次"去掉这 7 条"的对照轮（ABL 工作态没有它们的残留），通过即删 | 第 4 步 |
| 29 | **内核树当前处于"单链 bisect 崩溃态"**（B56-B60：dts 去 bonded、桥单通道、dsi1 禁用——这个配置在 insmod msm 探测内必崩），不是可交付状态 | `sources/pmos-sm8250`（提交 `800d81d92`→`a1a60a014`） | 单链 bisect 崩点已钉死但 panic 文本未取得（ramoops 卡点） | 直接 `fastboot boot` 会崩在 msm 探测；接手者若需要"能起的双链内核"，回退到 `e3391aab7`（B55 态）或 `bfb561f31`（B44 态） | ramoops probe 修好 → 双 boot 拿 panic → 修单链崩溃 → bisect 出结论后，要么固化单链要么回退双链 | 第 4 步 |
| 30 | **面板驱动是 builtin（`CONFIG_DRM_PANEL_LENOVO_K606F=y`）**，与"新驱动一律先编成模块"的规矩不符——它是 #21 事故（builtin 桥驱动挂死失联 17h）之前建面板时就定下的，事故后修了 psa8331/msm 却没回头动它 | `sources/k606f-probe.config:8565` | 60 轮从未因它崩过（probe 极简：无 IO、无时序），风险一直为账面值；改 `=m` 要连带调 blmode 的 insmod 顺序 | panel probe 若出问题会跟 builtin 桥一样带走 USB 通道；且违反自家规矩 | 显示点亮、进入第 5 步前，把面板改 `=m` 并验证 insmod 顺序（panel 先于 msm），顺手回归一次 | 第 4/5 步 |

**下一批要还的（2026-10-31 按影响排序）**——链路跑通后按这个顺序回来收敛，"跑通"不等于债还清：

1. **#21 builtin 驱动**：唯一会「让设备彻底失联」的规矩，已付过 17 小时代价；正式镜像里要再验证一次"模块化 + 可恢复"。
2. **#24 `power_stage` sysfs 调试开关**：能直接驱动电源序列的写接口不该出现在正式镜像；显示稳定后删，改由 DRM 回调驱动。
3. **#22 `bringup.sh` / `build-kernel.sh` 默认路径**：已静默污染过一次构建目录（丢掉 `CONFIG_DRM_PSA8331`、混进上游对象），换人最容易再踩。
4. **#23 板级 dts 抄来的 cs35l45 节点**：会抢 GPIO 且与实际硬件不符，音频开工前必须清。
5. **#25 `blmode` 块重复插入**：已修，但要加"脚本块唯一性"的自动检查，否则同类编辑失误还会再来。
6. **#20 msm 源码树里的 `[K606F]` 探针**：只影响日志噪音，发布前清。

#1–#19 已还或只影响内部构建效率，不影响真机结论，按需处理。

规则：加临时做法和登记在这里必须在同一次改动里完成。功能「跑通」不等于债还清。
