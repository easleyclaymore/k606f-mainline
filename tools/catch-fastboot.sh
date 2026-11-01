#!/bin/bash
# 蹲守 fastboot 窗口：设备如果处于"启动失败 → 落回 fastboot → 超时又重启"的循环里，
# 每次只有很短的时间窗。这个脚本高频轮询，一旦看到设备立刻把 dtbo_a 刷回备份并重启。
#
#   tools/catch-fastboot.sh              # 蹲 10 分钟
#   DURATION=3600 tools/catch-fastboot.sh
#
# 为什么值得试：14:33 的 dmesg 里见过 `18d1:d00d` 出现几秒又消失（device 13/14），
# 说明它可能偶尔会开一次 fastboot 窗口 —— 抓到就能把 dtbo_a 写回去。
set -uo pipefail
trap '' HUP

W=$(cd "$(dirname "$0")/.." && pwd)
SER=${SER:-请填你的设备序列号}
DURATION=${DURATION:-600}
IMG=${IMG:-$W/private/backups/2026-10-28/images/dtbo_a.img}
WANT=$(grep "images/dtbo_a.img" "$W/private/backups/2026-10-28/meta/sha256sums.txt" | awk '{print $1}')
LOG=$W/private/logs/$(date +%Y%m%d-%H%M)-catch-fastboot
mkdir -p "$LOG"

[ -f "$IMG" ] || { echo "没有 $IMG" >&2; exit 1; }
GOT=$(sha256sum "$IMG" | awk '{print $1}')
[ "$WANT" = "$GOT" ] || { echo "dtbo 备份哈希不符，拒绝使用" >&2; exit 1; }

printf '蹲守 %ss，serial=%s\ndtbo 备份 %s (sha256 OK)\n日志 %s\n' \
  "$DURATION" "$SER" "$GOT" "$LOG"

end=$((SECONDS + DURATION))
hits=0
while [ $SECONDS -lt $end ]; do
  if timeout 3 fastboot devices 2>/dev/null | grep -q "$SER"; then
    hits=$((hits+1))
    echo "[$(date +%H:%M:%S)] 第 $hits 次看到设备，立刻刷 dtbo_a"
    {
      date -Iseconds
      fastboot -s "$SER" getvar current-slot 2>&1 | tail -2
      fastboot -s "$SER" flash dtbo_a "$IMG" 2>&1
      fastboot -s "$SER" reboot 2>&1
    } | tee -a "$LOG/flash.log"
    if grep -q "OKAY" "$LOG/flash.log"; then
      echo "[$(date +%H:%M:%S)] 刷写返回 OKAY —— 请确认 Android 是否正常启动"
      exit 0
    fi
    echo "[$(date +%H:%M:%S)] 这一下没成功，继续蹲"
    sleep 2
  fi
  sleep 0.3
done

echo "[$(date +%H:%M:%S)] 蹲守结束，未抓到可用窗口（命中 $hits 次）"
exit 1
