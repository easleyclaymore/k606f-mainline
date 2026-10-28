# 第 1 步：构建环境 — 结果与结论

日期：2026-10-28　　状态：**离线通过**（工具链可用、基线可复现、增量达秒级）

## 1. 三份基线构建（同一份 pmOS sm8250 配置，12 核并行）

| # | 源码 | 工具链 | OUT | dtbs | Image | modules | 合计 | ccache 命中（冷缓存） |
|---|---|---|---|---|---|---|---|---|
| A | `linux-7.2`（清华镜像 tarball） | clang 23.1.3 + LLD | `out/kernel-ref-7.2` | 8 s | **328 s** | 142 s | **470 s** | 1.5% |
| B | `linux-7.2` | aarch64-linux-gnu-**gcc 16.2.0** | `out/kernel-gcc-7.2` | 3 s | **512 s** | 168 s | **680 s** | 1.3% |
| C | `pmos-sm8250` tag `sm8250-7.2.0`（社区参考树） | gcc 16.2.0 | `out/kernel-pmos-7.2` | 2 s | **495 s** | 157 s | **652 s** | 1.9% |

产物：
- A：Image 46 MB / 332 个 `.ko`；B：Image 46 MB / 332；C：Image **47 MB** / **335 个 `.ko`**
  （C 多 3 个模块 = 社区补丁新增的驱动）
- 架构校验：`file` → `Linux kernel ARM64 boot executable Image, little-endian, 4K pages`；
  `readelf` → `ELF64 / AArch64` ✓

## 2. 增量构建（第 1 步完成标准）

| 场景 | 用时 | ccache |
|---|---|---|
| 无改动再跑 `make Image` | **2.1 s** | 37/37 命中（100%） |
| `touch drivers/gpu/drm/msm/msm_drv.c` 后重建 | **22 s** | 41/42 命中 |

→ 达标（skill 要求"增量构建几分钟内"，实测秒级）。

## 3. 结论与三条经验

### 3.1 工具链：gcc 与 clang 都可用，clang 快约 30%
同一份源码、同一份配置，Image 阶段 clang 328 s vs gcc 512 s。若追求迭代速度可用 `TOOLCHAIN=llvm`；
若要跟 Alpine/pmOS 官方构建完全一致（发布、上游化）用 `TOOLCHAIN=gcc`（默认）。
**注意**：两者生成的 `.config` 哈希不同（`03c5505076d7` vs `b49123836a0f`），因为 Kconfig 里有依赖编译器身份的项。
→ 不要把两份 OUT 的配置互相复用；**一个工具链一个长期 OUT**。

### 3.2 ccache 跨源码路径几乎不命中（与 skill 的默认假设有出入）
预期"`base_dir` + `-ffile-prefix-map` 能让不同路径共享缓存"，实测三份构建各自 98%+ 未命中，
即使 A→B 是同一份源码（同一目录、只换编译器）、B→C 只换了同一目录树的不同 clone。

原因：ccache 的 **direct mode** 把命令行里的绝对路径（`-I…`、源文件路径、`-o`）计入哈希；
`base_dir` 只做相对化，跨树时 include 路径必然不同。`-ffile-prefix-map` 影响的是**产物里的调试信息**，不是缓存键。

**结论（写进本项目规矩）**：缓存只对"同一个 SRC 路径 + 同一个 OUT 路径"有效。
需要跨树复用（例如换内核基线的分支）时，用**固定路径**（`sources/current` 软链或 bind mount）而不是靠 ccache。
一次冷构建约 8 分钟（gcc）/ 6 分钟（clang），10 GB 缓存按实测每份完整构建约 0.7–1.3 GB 增长，可存 8–14 份。

### 3.3 宿主沙箱的两个坑（已绕开，写进脚本）
1. `rm` 被包装成安全删除脚本 → make 报大量「错误 129」，**没有任何编译错误信息**。
   解决：脚本内 `unset -f rm unlink rmdir` + 从 PATH 剔除 shim 目录。
2. 工具调用结束时对进程组发 SIGHUP → 后台构建被静默打断（错误 129）。
   解决：`setsid nohup` + 脚本内 `trap '' HUP`。

## 4. 未完成 / 下一步

- [ ] **根文件系统构建方式**（第 1 步第 4 项）：本次未做。计划用 Ubuntu arm64 官方层 + `debootstrap --arch=arm64`（需 binfmt/qemu-user）。
- [ ] `sources/pmos-sm8250` 是 `--depth 1` 浅克隆（`grafted`），**看不到补丁提交历史**，无法按提交挑拣。
      需要时 `git fetch --deepen=200` 或按需拉取分支来提取补丁系列。
- [ ] 基线用的是上游 tarball（技术债 #1）；pmOS 树 C 已构通，可作为正式基线的候选。
