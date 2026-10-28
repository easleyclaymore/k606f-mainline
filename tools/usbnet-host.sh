#!/bin/bash
# 主机侧：给探针的 USB NCM 网卡配 IP，等它通，然后取 dmesg。
set -uo pipefail
HOST_IP=${HOST_IP:-172.16.42.2}
DEV_IP=${DEV_IP:-172.16.42.1}
PW=${PW:-qq}

find_if() {
  ip -o link | grep -oE "(enp0s[0-9a-zA-Z]+u[0-9]+|usb[0-9]+)" | head -n1
}

IF=$(find_if)
if [ -z "$IF" ]; then
  echo "等 USB 网卡枚举（最多 60 秒）…"
  for i in $(seq 1 60); do sleep 1; IF=$(find_if); [ -n "$IF" ] && break; done
fi
[ -z "$IF" ] && { echo "FAIL: 没有 USB 网卡"; exit 1; }
echo "网卡: $IF"

echo "$PW" | sudo -S -p '' ip addr add "$HOST_IP/24" dev "$IF" 2>/dev/null
echo "$PW" | sudo -S -p '' ip link set "$IF" up 2>/dev/null

for i in $(seq 1 60); do
  if ping -c1 -W1 "$DEV_IP" >/dev/null 2>&1; then echo "✓ ping 通 ($i s)"; break; fi
  sleep 1
done

echo "=== 设备 dmesg（尾部）==="
curl -s --max-time 5 "http://$DEV_IP/dmesg.txt" | tail -n 40 || echo "(http 不可用)"
echo "=== 设备 uptime ==="
curl -s --max-time 5 "http://$DEV_IP/uptime.txt" || true
echo
echo "=== telnet 手动入口 ==="
echo "  telnet $DEV_IP"
