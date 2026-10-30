# 构建

本仓**不含内核源码树**，只放配置、板级设备树、面板驱动和脚本。构建需要自备一棵主线内核树。

## 1. 主机要求

| 项 | 要求 |
|---|---|
| 主机 | x86_64 Linux（本项目在 Arch/CachyOS 上验证） |
| 交叉编译 | 以下任选其一：`aarch64-linux-gnu-gcc`（推荐，`TOOLCHAIN=gcc`）或 `clang` + `lld`（`TOOLCHAIN=llvm`） |
| 其他工具 | `bc`、`dtc`、`flex`、`bison`、`ccache`（强烈建议）、`cpio` |
| 磁盘 | 内核源码约 1.5 GiB，构建输出约 8 GiB，ccache 另需 5–10 GiB |

```sh
# Arch
sudo pacman -S --needed base-devel bc dtc flex bison ccache cpio aarch64-linux-gnu-gcc
# Debian/Ubuntu
sudo apt install build-essential bc device-tree-compiler flex bison ccache cpio gcc-aarch64-linux-gnu
```

## 2. 拿内核源码

内核基线是主线 Linux 7.2，取自 postmarketOS 的 sm8250 树：

```sh
git clone https://gitlab.postmarketos.org/soc/qualcomm-sm8250/linux.git
cd linux && git checkout sm8250-7.2.0
```

> 该仓库在国内经常拉不动，可以换镜像或先浅克隆（`--depth 1`）。

## 3. 应用本仓的改动

`kernel/` 下的三个文件是全部内核侧改动：

| 文件 | 作用 |
|---|---|
| `kernel/sm8250-lenovo-k606f.dts` | 板级设备树 |
| `kernel/panel-lenovo-k606f.c` | 双 DSI 视频模式面板驱动（`panel-simple` 不支持 dual-DSI） |
| `kernel/k606f-kernel.patch` | 上述改动 + DSI PHY 7nm PLL 修复的合并 patch |

```sh
cd <内核树>
git am /path/to/k606f-mainline/kernel/k606f-kernel.patch
```

> 现阶段改动以 patch + 两个源文件的形式提供，尚未拆成独立内核仓（见项目技术债记录）。

## 4. 构建

```sh
export SRC=<内核树>
export OUT=<构建输出目录>
TOOLCHAIN=gcc SRC=$SRC OUT=$OUT CFG=<本仓>/config/k606f.config \
  <本仓>/tools/build-kernel.sh Image modules dtbs
```

- `CFG` 可选：`config/k606f.config`（普通内核）或 `config/k606f-probe.config`（显示驱动做成模块，便于 `insmod` 调试）。
- 想加快重复构建：给 `ccache` 一个长期目录，并保证 `OUT` 不每次换路径。

## 5. 产物

- `Image` —— 内核
- `*.dtb` —— 设备树，其中 `sm8250-lenovo-k606f.dtb` 是本机用的
- 模块（`k606f-probe.config` 下会产出 `msm.ko` 及其依赖）

打包启动镜像见 `docs/install.md`。
