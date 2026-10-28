#!/bin/bash
# 生成最小 BusyBox initramfs：USB NCM + telnet + 超时自动重启回原系统。
set -euo pipefail
W=$(cd "$(dirname "$0")/.." && pwd)
OUT=${OUT:-$W/out/initramfs}
BB=${BB:-$W/tools/vendor/busybox-aarch64-static}
TIMEOUT=${TIMEOUT:-600}

rm -rf "$OUT"
mkdir -p "$OUT"/{bin,sbin,etc,proc,sys,dev,tmp,usr/bin,usr/sbin}
cp "$BB" "$OUT/bin/busybox"
chmod 755 "$OUT/bin/busybox"

# 常用 applet 的软链
for a in sh ls cat mount umount echo sleep dmesg dmesg cat ip ifconfig udhcpc \
         telnetd httpd mkdir ln rm ps top free df grep sed awk vi reboot poweroff \
         halt sync mdev lsmod modprobe insmod find head tail cut date true false; do
  ln -sf busybox "$OUT/bin/$a"
done
ln -sf ../bin/busybox "$OUT/sbin/init"

cat > "$OUT/init" <<'INIT'
#!/bin/busybox sh
# K606F 首次启动探针
export PATH=/bin:/sbin:/usr/bin:/usr/sbin
BB=/bin/busybox

$BB mount -t proc proc /proc
$BB mount -t sysfs sysfs /sys
$BB mount -t devtmpfs devtmpfs /dev 2>/dev/null || true
$BB mount -t tmpfs tmpfs /tmp 2>/dev/null || true
mkdir -p /dev/pts && $BB mount -t devpts devpts /dev/pts 2>/dev/null || true

echo
echo "=========================================================="
echo " K606F mainline probe"
echo " kernel: $($BB uname -a 2>/dev/null || cat /proc/version)"
echo " uptime: $(cut -d' ' -f1 /proc/uptime) s"
echo "=========================================================="
$BB dmesg | $BB tail -n 40

# ---- USB gadget: NCM ----
udc_setup() {
  $BB mount -t configfs none /sys/kernel/config 2>/dev/null || true
  [ -d /sys/kernel/config/usb_gadget ] || { echo "no configfs usb_gadget"; return 1; }
  cd /sys/kernel/config/usb_gadget || return 1
  $BB mkdir -p g1 && cd g1 || return 1
  echo 0x1d6b > idVendor
  echo 0x0104 > idProduct
  echo 0x0100 > bcdDevice
  echo 0x0200 > bcdUSB
  $BB mkdir -p strings/0x409
  echo "k606f-probe" > strings/0x409/serialnumber
  echo "mainline"    > strings/0x409/manufacturer
  echo "K606F probe"  > strings/0x409/product
  $BB mkdir -p configs/c.1/strings/0x409
  echo "NCM" > configs/c.1/strings/0x409/configuration
  echo 250   > configs/c.1/MaxPower
  $BB mkdir -p functions/ncm.0
  echo "02:1d:6b:60:6f:01" > functions/ncm.0/dev_addr 2>/dev/null || true
  echo "02:1d:6b:60:6f:02" > functions/ncm.0/host_addr 2>/dev/null || true
  [ -L configs/c.1/ncm.0 ] || $BB ln -s functions/ncm.0 configs/c.1/
  UDC=$(ls /sys/class/udc 2>/dev/null | head -n1)
  echo "UDC=$UDC"
  [ -n "$UDC" ] && echo "$UDC" > UDC
  return 0
}

udc_setup
# 等设备侧网卡出现（busybox 的 ip 不支持 -o，直接看 sysfs）
for i in $(seq 1 30); do
  [ -e /sys/class/net/usb0 ] && break
  $BB sleep 1
done
$BB ls /sys/class/net > /tmp/netifs.txt 2>/dev/null
IF=""
for c in usb0 usb1 eth0; do [ -e /sys/class/net/$c ] && IF=$c && break; done
echo "usb iface: ${IF:-none}"
if [ -n "$IF" ]; then
  $BB ifconfig "$IF" up 2>/dev/null
  $BB ifconfig "$IF" 172.16.42.1 netmask 255.255.255.0 2>/dev/null
  $BB ifconfig -a > /tmp/ipaddr.txt 2>/dev/null
fi

# ---- 先把网络服务拉起来（先于模块加载，避免 msm/DPU 使能时钟把 USB 弄掉后没法调试）----
$BB telnetd -l /bin/sh -p 23 2>/dev/null && echo "telnetd on :23"
$BB httpd -p 80 -h /tmp 2>/dev/null && echo "httpd on :80"

# 只自动加载基础模块（不含 msm）；msm 留给宿主机 telnet 进去后手动 insmod，
# 以精确定位「msm 加载是否导致 USB 网络掉线」。基础模块写在 /tmp/base-load.txt。
if [ -d /lib/modules ]; then
  (
    : > /tmp/base-load.txt
    for m in drm_dp_aux_bus drm_exec drm_gpuvm gpu-sched cec drm_display_helper mdt_loader ubwc_config; do
      [ -f /lib/modules/$m.ko ] || continue
      if $BB insmod /lib/modules/$m.ko 2>>/tmp/base-load.txt; then
        echo "OK   $m" >> /tmp/base-load.txt
      else
        echo "FAIL $m" >> /tmp/base-load.txt
      fi
    done
    echo "base modules done" >> /tmp/base-load.txt
  ) &
fi

echo
echo ">>> 就绪：宿主机配 172.16.42.2/24，然后 telnet 172.16.42.1"
echo ">>> 手动加载 msm： insmod /lib/modules/msm.ko （观察 USB 是否掉线）"
echo ">>> ${TIMEOUT} 秒后自动重启回 Android"
echo

$BB sleep ${TIMEOUT}
$BB sync
echo "probe done, rebooting"
$BB reboot -f
INIT
sed -i "s/\${TIMEOUT}/$TIMEOUT/" "$OUT/init"
chmod 755 "$OUT/init"

# 内核模块（可选）：out/kmod-probe/*.ko -> /lib/modules
if [ -d "$W/out/kmod-probe" ]; then
  mkdir -p "$OUT/lib/modules"
  cp "$W"/out/kmod-probe/*.ko "$OUT/lib/modules/" 2>/dev/null || true
fi

cd "$OUT" && find . | cpio -o -H newc 2>/dev/null | gzip -9 > "$W/out/initramfs.cpio.gz"
ls -lh "$W/out/initramfs.cpio.gz"
