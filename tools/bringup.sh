#!/bin/bash
# 一条命令：源码 → dtb + initramfs → v2 启动镜像 → （可选）fastboot boot
#
#   tools/bringup.sh                # 只构建，产出 out/boot-probe.img
#   tools/bringup.sh --boot         # 构建并推到设备启动
#   tools/bringup.sh --boot --restore-dtbo   # 启动前把 dtbo_a 刷成原厂（默认刷空）
#
# 关键点（踩过的坑写在这里，别改）：
#   * mkbootimg 必须 --base 0x0，否则加载地址整体偏 0x10000000，ABL 回 OKAY 但不跳转
#   * fastboot 会话放久了会 Load Error，boot 前一律先 reboot-bootloader
#   * 本机 ABL 会尝试把 dtbo[11] 叠到我们的 DTB 上（依赖上百个 vendor 符号，必然失败），
#     所以启动 Linux 前要把 dtbo_a 刷成"空 dtbo"
set -euo pipefail
trap '' HUP

W=$(cd "$(dirname "$0")/.." && pwd)
SRC=${SRC:-$W/sources/pmos-sm8250}
OUT=${OUT:-$W/out/kernel-pmos-7.2}
DTB=$OUT/arch/arm64/boot/dts/qcom/sm8250-lenovo-k606f.dtb
IMAGE=$OUT/arch/arm64/boot/Image
TIMEOUT=${TIMEOUT:-300}
BOOT=0
RESTORE_DTBO=0
for a in "$@"; do
  case "$a" in
    --boot) BOOT=1 ;;
    --restore-dtbo) RESTORE_DTBO=1 ;;
    --build-only) BOOT=0 ;;
    *) echo "未知参数: $a" >&2; exit 1 ;;
  esac
done

step() { printf '\n\033[1m== %s ==\033[0m\n' "$*"; }

step "1/4 编译设备树"
TOOLCHAIN=${TOOLCHAIN:-gcc} SRC="$SRC" OUT="$OUT" \
  CFG=${CFG:-$W/sources/pmos-sm8250.config} "$W/tools/build-kernel.sh" dtbs >/dev/null
ls -l "$DTB" | awk '{print "  dtb:", $5, "bytes"}'

step "2/4 生成 initramfs"
# 注意：这里必须显式隔离 OUT。build-initramfs.sh 的 OUT 是它的 staging 目录，
# 若沿用内核构建的 OUT（如 out/kernel-msmmod），会把它当成 initramfs 组装目录覆盖掉内核产物。
TIMEOUT="$TIMEOUT" OUT="$W/out/initramfs" "$W/tools/build-initramfs.sh" | tail -1

step "3/4 打包启动镜像（--base 0x0）"
mkbootimg \
  --kernel "$IMAGE" \
  --ramdisk "$W/out/initramfs.cpio.gz" \
  --dtb "$DTB" \
  --header_version 2 --pagesize 4096 --base 0x0 \
  --os_version 12.0.0 --os_patch_level 2023-02 \
  --cmdline "console=ttyMSM0,115200n8 earlycon loglevel=8 ignore_loglevel panic=10 clk_ignore_unused pd_ignore_unused ${EXTRA_CMDLINE:-}" \
  --output "$W/out/boot-probe.img"
unpack_bootimg --boot_img "$W/out/boot-probe.img" --out /tmp/bringup-check >/dev/null 2>&1 || true
KADDR=$(unpack_bootimg --boot_img "$W/out/boot-probe.img" --out /tmp/bringup-check 2>/dev/null | awk -F': ' '/kernel load address/{print $2}')
[ "$KADDR" = "0x00008000" ] || { echo "!! kernel load address = $KADDR，应为 0x00008000（--base 0x0 没生效？）" >&2; exit 1; }

cat > "$W/out/boot-probe.identity" <<EOF
date            $(date -Iseconds)
kernel_image    $(sha256sum "$IMAGE" | cut -d' ' -f1)
dtb             $(sha256sum "$DTB" | cut -d' ' -f1)
initramfs       $(sha256sum "$W/out/initramfs.cpio.gz" | cut -d' ' -f1)
boot_img        $(sha256sum "$W/out/boot-probe.img" | cut -d' ' -f1)
kernel_version  $(strings "$IMAGE" | grep -m1 -oE '^[0-9]+\.[0-9]+\.[0-9]+[^ ]*' || true)
timeout_s       $TIMEOUT
EOF
sed -n '1,7p' "$W/out/boot-probe.identity" | sed 's/^/  /'

if [ "$BOOT" = "0" ]; then
  step "4/4 跳过启动（加 --boot 才会推上去）"
  echo "  产物: $W/out/boot-probe.img"
  exit 0
fi

step "4/4 推送到设备启动"
adb reboot bootloader 2>/dev/null || true
sleep 12
fastboot reboot-bootloader >/dev/null 2>&1 || true
sleep 8
fastboot devices | grep -q fastboot || { echo "!! 设备不在 fastboot" >&2; exit 1; }

if [ "$RESTORE_DTBO" = "1" ]; then
  echo "  恢复原厂 dtbo_a"
  fastboot flash dtbo_a "$W/private/backups/2026-10-28/images/dtbo_a.img" >/dev/null
else
  # 必须是 erase：只写 32 字节的空 dtbo 头，分区尾部仍留着原厂数据，ABL 会拒绝交接（实测）
  echo "  擦除 dtbo_a（让 ABL 没有 overlay 可叠）"
  fastboot erase dtbo_a >/dev/null
fi

fastboot boot "$W/out/boot-probe.img" 2>&1 | tail -2

echo
echo "等 12 秒后自动配网卡并取 dmesg…"
sleep 12
"$W/tools/usbnet-host.sh" || true
echo
echo "进 shell:  socat - TCP:172.16.42.1:23"
echo "探针将在 ${TIMEOUT} 秒后自动重启（届时回到 fastboot）"
