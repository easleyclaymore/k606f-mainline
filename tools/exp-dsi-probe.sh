#!/bin/bash
# 一次跑完「显示探针」实验：重启到 fastboot → 擦 dtbo_a → fastboot boot 探针镜像
# → 等 USB 网络 → 取 dmesg/msm-load → 设备自动重启后刷回 dtbo_a → 回 Android
#
#   tools/exp-dsi-probe.sh            # 跑完整流程
#   tools/exp-dsi-probe.sh --dry-run  # 只打印将执行的步骤
#
# 前置：① 平板已开 USB 调试（adb devices 能看到）；② out/boot-probe.img 已构好；
#       ③ private/backups/2026-10-28/images/dtbo_a.img 在，且 sha256 与清单一致。
# 退出后设备一定回到 Android —— 若中途失败，脚本会提示恢复命令，不会静默放弃。
set -uo pipefail
trap '' HUP

W=$(cd "$(dirname "$0")/.." && pwd)
IMG=${IMG:-$W/out/boot-probe.img}
TIMEOUT=${TIMEOUT:-300}
PW=${PW:-请填你的sudo密码}
DRY=0
[ "${1:-}" = "--dry-run" ] && DRY=1

DTBO=$W/private/backups/2026-10-28/images/dtbo_a.img
LOG=$W/private/logs/$(date +%Y%m%d-%H%M)-dsi-probe
step() { printf '\n\033[1m== %s ==\033[0m\n' "$*"; }
die()  { printf '\n!! %s\n' "$*" >&2; exit 1; }

step "0/7 预检"
[ -f "$IMG" ] || die "没有 $IMG"
[ -f "$DTBO" ] || die "没有 dtbo 备份 $DTBO"
WANT=$(grep "images/dtbo_a.img" "$W/private/backups/2026-10-28/meta/sha256sums.txt" | awk '{print $1}')
GOT=$(sha256sum "$DTBO" | awk '{print $1}')
[ "$WANT" = "$GOT" ] || die "dtbo 备份哈希不符：want=$WANT got=$GOT"
echo "  镜像  $(sha256sum "$IMG" | awk '{print $1}')  $(du -h "$IMG" | cut -f1)"
echo "  dtbo  备份哈希 OK ($GOT)"
echo "  日志  $LOG"
if [ "$DRY" = "1" ]; then
  cat <<EOF

将执行：
  adb reboot bootloader → fastboot reboot-bootloader → fastboot erase dtbo_a
  fastboot boot $IMG
  等 USB 网卡 → 配 172.16.42.2/24 → 轮询 http://172.16.42.1/{msm-load,dmesg}.txt（最多 ${TIMEOUT}s）
  设备自动重启后 → fastboot flash dtbo_a 备份 → fastboot reboot
EOF
  exit 0
fi

mkdir -p "$LOG"

step "1/7 进 fastboot"
if fastboot devices 2>/dev/null | grep -q fastboot; then
  echo "  设备已在 fastboot（跳过 adb）"
  fastboot devices > "$LOG/fastboot.txt"
else
  adb devices | grep -qw device || die "既不在 fastboot，adb 也看不到设备：请手动进 fastboot（Power+Vol-）或打开 USB 调试"
  adb reboot bootloader >/dev/null 2>&1 || true
  sleep 12
  fastboot reboot-bootloader >/dev/null 2>&1 || true
  sleep 8
  fastboot devices > "$LOG/fastboot.txt"
  grep -q fastboot "$LOG/fastboot.txt" || die "设备不在 fastboot"
fi
SER=$(awk '{print $1}' "$LOG/fastboot.txt" | head -1)
echo "  serial $SER"

step "2/7 擦 dtbo_a（让 ABL 没有 overlay 可叠；退出前会刷回）"
fastboot -s "$SER" erase dtbo_a | tee -a "$LOG/fastboot.txt"

step "3/7 临时启动探针镜像（不写分区）"
fastboot -s "$SER" boot "$IMG" 2>&1 | tee "$LOG/boot.txt" | tail -2

step "4/7 等 USB 网卡并配地址"
PW="$PW" "$W/tools/usbnet-host.sh" > "$LOG/usbnet.txt" 2>&1 || true
tail -3 "$LOG/usbnet.txt"

step "5/7 采集（最多 ${TIMEOUT}s）"
mkdir -p "$LOG/http"
ok=0
for i in $(seq 1 $((TIMEOUT / 3))); do
  for f in msm-load dmesg ipaddr base-load; do
    curl -fsS --max-time 3 "http://172.16.42.1/$f.txt" -o "$LOG/http/$f.txt" 2>/dev/null && touch "$LOG/http/$f.ok"
  done
  if grep -q "automsm done" "$LOG/http/msm-load.txt" 2>/dev/null; then ok=1; echo "  自动 insmod 完成（第 $((i*3)) 秒）"; break; fi
  sleep 3
done
[ "$ok" = "1" ] || echo "  ⚠ 没等到 automsm done，可能是手动模式镜像或网络没起来"

step "6/7 恢复 dtbo_a"
# 探针超时后设备自己 reboot，落在 fastboot（因为 dtbo 被擦过，Android 起不来）
for i in $(seq 1 40); do
  fastboot devices | grep -q fastboot && break
  sleep 3
done
if fastboot devices | grep -q fastboot; then
  fastboot flash dtbo_a "$DTBO" | tee -a "$LOG/fastboot.txt"
  step "7/7 回到 Android"
  fastboot reboot | tee -a "$LOG/fastboot.txt"
else
  echo "  ⚠ 设备不在 fastboot，请手动：fastboot flash dtbo_a $DTBO && fastboot reboot"
fi

step "摘要"
grep -c "K606F" "$LOG/http/dmesg.txt" 2>/dev/null | sed 's/^/  探针命中行数: /'
grep "K606F" "$LOG/http/dmesg.txt" 2>/dev/null | tail -25
echo
echo "  完整证据：$LOG"
