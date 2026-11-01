#!/bin/bash
# 盯 EDL（9008）：深刷线/按键组合有没有把设备拉进高通 EDL 模式。
#   tools/watch-edl.sh            # 盯 5 分钟
#   DURATION=1800 tools/watch-edl.sh
# 判据：出现 05c6:9008（Qualcomm HS-USB QDLoader 9008）就说明 SoC 的 PBL 跑起来了。
set -uo pipefail
DURATION=${DURATION:-300}
end=$((SECONDS+DURATION))
echo "盯 EDL $DURATION 秒（期望 05c6:9008）…"
seen=0
while [ $SECONDS -lt $end ]; do
  if lsusb 2>/dev/null | grep -qiE "05c6:9008|QDLoader|Qualcomm HS-USB"; then
    echo "[$(date +%H:%M:%S)] ✓ 进入 EDL："; lsusb | grep -iE "05c6|QDLoader"
    seen=1; break
  fi
  if lsusb 2>/dev/null | grep -qiE "18d1:d00d|fastboot"; then
    echo "[$(date +%H:%M:%S)] ✓ 出现 fastboot（比 EDL 更好）"; lsusb | grep -iE "18d1"
    seen=1; break
  fi
  sleep 1
done
[ $seen -eq 0 ] && { echo "[$(date +%H:%M:%S)] 没有出现 9008/fastboot"; exit 1; }
exit 0
