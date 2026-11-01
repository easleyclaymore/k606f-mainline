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

# --- 自动上机模式：cmdline 带 k606f.automsm=1 时自动 insmod msm 并把 dmesg 挂到 httpd ---
# 目的：把「telnet 进去手敲 insmod」变成可复现的一步操作，宿主侧只靠 HTTP 取证据。
if grep -q "k606f.automsm=1" /proc/cmdline 2>/dev/null; then
  (
    dmesg > /tmp/dmesg.txt 2>/dev/null
    sleep 3
    : > /tmp/msm-load.txt
    $BB insmod /lib/modules/msm.ko >> /tmp/msm-load.txt 2>&1
    echo "insmod rc=$?" >> /tmp/msm-load.txt
    for i in 1 2 3 4 5 6 7 8 9 10; do
      sleep 3
      dmesg > /tmp/dmesg.txt 2>/dev/null
    done
    echo "automsm done" >> /tmp/msm-load.txt
  ) &
fi

# --- B 阶段脚本化模式：cmdline 带 k606f.blmode=N ---
# 背景：msm 一加载 USB 常掉，之后没法交互；且日志必须能取回来。所以整条实验脚本化 +
# 结果写进 dtbo_b 分区（boot Android 后 dd 读回），人只需要看屏幕。
#   blmode=1：桥 + msm（msm 自己触发上电）→ 写"引导程序那 9 个值"
#   blmode=2：**不加载 msm**，只上电 + 写那 9 个值
#   blmode=3：**先上电（stage 1/2/3）再 insmod msm** —— B7 的观察：桥先上电时
#              dsi_err_worker 会停；反过来（msm 先起 DSI）会一直报 status=5
if grep -q "k606f.blmode=" /proc/cmdline 2>/dev/null; then
  (
    MODE=$($BB sed -n 's/.*k606f.blmode=\([0-9]*\).*/\1/p' /proc/cmdline)
    L=/tmp/blmode.txt
    : > $L
    echo "mode=$MODE" >> $L

    # B57：增量持久化——每次快照后把当前日志写入 dtbo_b，崩溃也有现场
    flush() {
      SAVE=/tmp/save-inc.txt
      echo "K606FLOG1 inc $(date +%H:%M:%S)" > $SAVE
      $BB cat $L >> $SAVE 2>/dev/null
      echo "--- dmesg tail ---" >> $SAVE
      $BB dmesg | $BB tail -n 300 >> $SAVE 2>/dev/null
      for p in /sys/class/block/sd*; do
        if $BB grep -q "PARTNAME=dtbo_b" "$p/uevent" 2>/dev/null; then
          $BB dd if=$SAVE of=/dev/$($BB basename "$p") bs=4096 seek=1 conv=fsync 2>/dev/null
          break
        fi
      done
    }
    # B58 起：读上次 panic 的 ramoops 现场（探针内核与上次共享保留区）
    $BB mkdir -p /sys/fs/pstore 2>/dev/null
    $BB mount -t pstore none /sys/fs/pstore 2>/dev/null
    echo "=== 上次 panic 现场（pstore）===" >> $L
    for f in /sys/fs/pstore/console-ramoops* /sys/fs/pstore/dmesg-ramoops*; do
      [ -e "$f" ] || continue
      echo "--- $f ---" >> $L
      $BB tail -c 8000 "$f" >> $L 2>/dev/null
    done
    $BB umount /sys/fs/pstore 2>/dev/null
    # B63：k606f.panic=1 = 捕获链自检（主动 sysrq panic，验证 ramoops+双 boot 全链路）
    if $BB grep -q "k606f.panic=1" /proc/cmdline 2>/dev/null; then
      echo "=== 自检：触发 sysrq panic ===" >> $L
      flush
      echo 1 > /proc/sys/kernel/sysrq 2>/dev/null
      echo c > /proc/sysrq-trigger 2>/dev/null
      $BB sleep 5
    fi
    echo "=== ramoops 诊断 ===" >> $L
    $BB dmesg | $BB grep -iE "ramoops|pstore|reserved-memory" >> $L 2>/dev/null
    $BB ls /sys/bus/platform/devices/ 2>/dev/null | $BB grep -i ramoops >> $L 2>&1
    $BB ls /proc/device-tree/reserved-memory/ >> $L 2>&1
    sleep 2

    stage() { echo $1 > /sys/bus/i2c/devices/1-0033/power_stage; echo "stage$1 rc=$?" >> $L; }
    dsi_cnt() { $BB dmesg | $BB grep -c dsi_err_worker 2>/dev/null; }
    snap() {
      echo "--- $1 (dsi_err_worker=$2) ---" >> $L
      # 两颗芯片都全量 dump：DSI86(0x2C) 才是做 DSI->eDP 的桥（0x0A=0x82 PLL锁、
      # 0x96=0x01 训练完成），0x33 只是端口 mux。B38 之前只 dump 0x33，白忙一场。
      echo "DSI86(0x2C):" >> $L
      /bin/i2cdump8 1 0x2c 0 0xff 2>/dev/null | $BB grep -vE '^#' >> $L
      echo "MUX(0x33):" >> $L
      /bin/i2cdump8 1 0x33 0 0xff 2>/dev/null | $BB grep -vE '^#' >> $L
    }


    # B62：后台 flush 循环——主流程阻塞在 insmod 里也能每 2s 落一次盘，
    # 崩点前最后 2s 内的 dmesg 一定在 dtbo_b 里
    ( while true; do flush; $BB sleep 0.3; done ) &
    FLUSH_PID=$!

    $BB insmod /lib/modules/psa8331.ko >> $L 2>&1
    echo "bridge rc=$?" >> $L

    if [ "$MODE" = "3" ]; then
      # 关键：在 DSI 起来之前把桥上电并进入 MIPI->eDP 模式
      for st in 1 2 3 6; do stage $st; $BB sleep 1; done
      echo "pre-power done, dsi_err=$(dsi_cnt)" >> $L
    fi

    # 在 msm 重新配置之前，dump ABL 留下的 DSI 控制器+PHY 寄存器（ABL 的配置能点亮 Tux，
    # 是 ground truth）。之后 msm 跑起来再 dump 一次，diff 就是差异。
    # B48 起：加入 DPU 侧 INTF1/2（0xae6b800/0xae6c000，两条 DSI 的时序引擎）、
    # PP0/1（0xae71000/0xae71800）、LM0/1（0xae45000/0xae46000）。
    # bonded 双 DSI 的"每链半幅"拆分就写在 INTF 时序里，ABL 期即真值。
    echo "--- ABL DSI config (before msm) ---" >> $L
    for bc in 0xae94000:96 0xae96000:96 0xae94400:96 0xae96400:96 0xae94900:96 0xae96900:96 0xae6b800:176 0xae6c000:176 0xae71000:64 0xae71800:64 0xae45000:200 0xae46000:200 0xaf02098:16 0xaf02110:16 0xaf0212c:8 0xaf020b0:8 0xae012f4:2 0xae012f8:2 0xae013f0:2; do
      b=${bc%%:*}; c=${bc##*:}
      echo "== $b ==" >> $L
      /bin/devmem $b $c >> $L 2>&1
    done

    $BB insmod /lib/modules/msm.ko >> $L 2>&1
    echo "msm rc=$?" >> $L
    flush
    $BB sleep 5
    echo "--- msm 配置后的 DSI 寄存器 ---" >> $L
    for bc in 0xae94000:96 0xae96000:96 0xae94400:96 0xae96400:96 0xae94900:96 0xae96900:96 0xae6b800:176 0xae6c000:176 0xae71000:64 0xae71800:64 0xae45000:200 0xae46000:200 0xaf02098:16 0xaf02110:16 0xaf0212c:8 0xaf020b0:8 0xae012f4:2 0xae012f8:2 0xae013f0:2; do
      b=${bc%%:*}; c=${bc##*:}
      echo "== $b ==" >> $L
      /bin/devmem $b $c >> $L 2>&1
    done

    snap "before" "$(dsi_cnt)"
    flush

    # B38 起：59 条 eDP 表 + TPS1 已改写到 DSI86(0x2C)，不再需要这里手写"BL 那 9 个值"
    #（那 9 个值其实是 0x33 mux 的寄存器，不是 DSI86 的，写进去是南辕北辙）。
    $BB sleep 3

    snap "after" "$(dsi_cnt)"
    flush
    $BB sleep 5
    snap "after+5s" "$(dsi_cnt)"
    flush

    # ---- 把真画面送上屏（人眼判据）----
    # 三个必须做的准备，缺一个结果就不可信：
    #   ① 静默控制台（dmesg -n 1）：否则内核任何一条 printk 都会让 fbcon 重绘整屏；
    #   ② drmpaint 自己再解绑一次 fbcon（/sys/class/vtconsole/*/bind），双保险；
    #   ③ 写入按整行对齐分块（line_length 的整数倍），避免 dirty 区落在一行中间画出半帧。
    # 判据：屏幕出现 8 条彩条 + 正中一个每秒 +1 的超大数字 + 顶部横扫白块。
    #   数字在跳 / 白块在动 → 显示链路真的在工作；一直只有背光 → eDP 输出端确实没工作。
    $BB dmesg -n 1 2>/dev/null
    echo "console loglevel 已设为 1（dmesg -n 1 rc=$?）" >> $L
    echo "vtconsole: $($BB ls /sys/class/vtconsole 2>/dev/null | $BB tr '\n' ' ')" >> $L
    for v in /sys/class/vtconsole/vtcon*; do
      [ -e "$v" ] || continue
      echo "  $($BB basename $v) name=$($BB cat $v/name 2>/dev/null) bind=$($BB cat $v/bind 2>/dev/null)" >> $L
    done
    echo "dri: $($BB ls /dev/dri 2>/dev/null | $BB tr '\n' ' ')" >> $L
    echo "gfx: $($BB ls /sys/class/graphics 2>/dev/null | $BB tr '\n' ' ')" >> $L
    for g in /sys/class/graphics/fb*; do
      [ -e "$g" ] || continue
      echo "$($BB basename $g): name=$($BB cat $g/name 2>/dev/null) size=$($BB cat $g/virtual_size 2>/dev/null) stride=$($BB cat $g/stride 2>/dev/null) bpp=$($BB cat $g/bits_per_pixel 2>/dev/null)" >> $L
    done

    DEV=""
    [ -e /dev/fb0 ] && DEV=/dev/fb0

    # 优先走 DRM（card1）：SETCRTC 会真正把 CRTC 打开并扫描我们自己的帧缓冲，
    # 不依赖 fbcon/fbdev 的状态。B26 的教训：解绑 fbcon 之后 fbdev 客户端会把
    # CRTC 恢复成关闭（fb0/blank=4 = POWERDOWN），此时往 fb0 写什么都不显示。
    echo "--- 先试 DRM 路径 ---" >> $L
    echo "画之前: fb0/blank=$(cat /sys/class/graphics/fb0/blank 2>/dev/null)" >> $L
    for f in /sys/class/drm/*/status /sys/class/drm/*/enabled; do
      [ -e "$f" ] || continue
      echo "  $f = $(cat "$f" 2>/dev/null)" >> $L
    done
    /bin/drmpaint "" bars 4 >> $L 2>&1
    DRM_RC=$?
    echo "drmpaint(DRM 诊断) rc=$DRM_RC" >> $L
    echo "画之后: fb0/blank=$(cat /sys/class/graphics/fb0/blank 2>/dev/null)" >> $L

    if [ "$DRM_RC" -eq 0 ]; then
      snap "after-drm-setcrtc" "$(dsi_cnt)"
      flush
      # ---- B44：桥(SN65DSI86) DSI 接收诊断 ----
      # 0xF0-0xF3 是桥的 DSI 接口错误（SOT/EOT/contention/timeout 等，粘滞），
      # 0x12 是 DSI 时钟频率范围（桥检测到的 DSI CLK）。清除错误标志后重读，
      # 看 msm 的 DSI 流到底有没有被桥收到、有没有协议错误。
      echo "=== 桥 DSI 诊断：清除 0xF0-F3 前 ===" >> $L
      /bin/i2cdump8 1 0x2c 0x12 0x12 2>/dev/null | $BB grep -vE '^#' >> $L
      /bin/i2cdump8 1 0x2c 0xf0 0xf7 2>/dev/null | $BB grep -vE '^#' >> $L
      /bin/i2cdump8 w 1 0x2c 0xf0 0xff >> $L 2>&1
      /bin/i2cdump8 w 1 0x2c 0xf1 0xff >> $L 2>&1
      /bin/i2cdump8 w 1 0x2c 0xf2 0xff >> $L 2>&1
      /bin/i2cdump8 w 1 0x2c 0xf3 0xff >> $L 2>&1
      # 0xF4 AUX 命令状态 / 0xF5 IRQ 状态 / 0xF6 DP 同步 / 0xF7：也是粘滞
      #（Android 工作态 0xF5=0x20、0xF6=0x40 同样非零，说明这些位不能当"当前状态"读）
      /bin/i2cdump8 w 1 0x2c 0xf4 0xff >> $L 2>&1
      /bin/i2cdump8 w 1 0x2c 0xf5 0xff >> $L 2>&1
      /bin/i2cdump8 w 1 0x2c 0xf6 0xff >> $L 2>&1
      /bin/i2cdump8 w 1 0x2c 0xf7 0xff >> $L 2>&1
      $BB sleep 3
      echo "=== 桥 DSI 诊断：清除后 +3s（残留=新错误）===" >> $L
      /bin/i2cdump8 1 0x2c 0xf0 0xf7 2>/dev/null | $BB grep -vE '^#' >> $L
      # B47：经桥 AUX 读面板 DPCD 0x200（SINK_STATUS，bit0=1 表示 TCON 收到稳定同步）。
      # Android 工作态健康签名：0x200=0x81。这给出"DP 侧真在出帧"的机器证据。
      aux_read() {
        /bin/i2cdump8 w 1 0x2c 0x74 0x00 >> $L 2>&1
        /bin/i2cdump8 w 1 0x2c 0x75 0x02 >> $L 2>&1
        /bin/i2cdump8 w 1 0x2c 0x76 0x00 >> $L 2>&1
        /bin/i2cdump8 w 1 0x2c 0x77 0x02 >> $L 2>&1
        /bin/i2cdump8 w 1 0x2c 0x78 0x01 >> $L 2>&1
        $BB sleep 1
        echo "  DPCD 0x200-0x201 (SINK_STATUS):" >> $L
        /bin/i2cdump8 1 0x2c 0x78 0x7b 2>/dev/null | $BB grep -vE '^#' >> $L
        /bin/i2cdump8 1 0x2c 0xf4 0xf4 2>/dev/null | $BB grep -vE '^#' >> $L
      }
      aux_read
      /bin/drmpaint "" bars 300 >> $L 2>&1 &
      # B46：延迟重读。B45 里 CHB 在 +3s 还重锁过一次、洪流 20s 后才停，
      # 只有"清除后长时间保持 0x00"才能证明链路真的稳定（Android 健康态签名=全 0）。
      $BB sleep 30
      echo "=== 桥 DSI 诊断：清除后 +33s（dsi_err=$(dsi_cnt)）===" >> $L
      /bin/i2cdump8 1 0x2c 0xf0 0xf7 2>/dev/null | $BB grep -vE '^#' >> $L
      aux_read
      $BB sleep 60
      echo "=== 桥 DSI 诊断：清除后 +93s（dsi_err=$(dsi_cnt)）===" >> $L
      /bin/i2cdump8 1 0x2c 0xf0 0xf7 2>/dev/null | $BB grep -vE '^#' >> $L
      aux_read
      # B51：commit 之后的 DPU 状态——INTF timing engine 到底谁在跑（此前只在
      # insmod+5s dump 过，那是 commit 前的状态，post_kickoff 还没执行）
      echo "=== DPU 晚期状态（SETCRTC 后）===" >> $L
      for bc in 0xae6b800:12 0xae6c000:12 0xae012f4:2 0xae013f0:2 0xae71000:8 0xae71800:8; do
        b=${bc%%:*}; c=${bc##*:}
        echo "== $b ==" >> $L
        /bin/devmem $b $c >> $L 2>&1
      done
    elif [ -n "$DEV" ]; then
      echo "--- DRM 失败，退回 fbdev（先强制 unblank）---" >> $L
      echo 0 > /sys/class/graphics/fb0/blank 2>>$L
      echo "unblank rc=$?，blank=$(cat /sys/class/graphics/fb0/blank 2>/dev/null)" >> $L
      /bin/drmpaint "$DEV" bars 4 >> $L 2>&1
      echo "drmpaint(fbdev 诊断, $DEV) rc=$?" >> $L
      snap "after-fbpaint" "$(dsi_cnt)"
      /bin/drmpaint "$DEV" bars 300 >> $L 2>&1 &
    else
      echo "既没有可用的 DRM 卡也没有 /dev/fb0" >> $L
    fi

    $BB dmesg > /tmp/dmesg-blmode.txt 2>/dev/null
    $BB grep -E "K606F|engine_start|dsi_bind" /tmp/dmesg-blmode.txt > /tmp/k606f-events.txt 2>/dev/null
    echo "blmode done $(date +%H:%M:%S)" >> $L

    # ---- 持久化日志：dtbo_b 第 4 KiB 起（首 4 KiB 不动，分区头不受影响）----
    SAVE=/tmp/save.txt
    : > $SAVE
    for p in /sys/class/block/sd*; do
      if $BB grep -q "PARTNAME=dtbo_b" "$p/uevent" 2>/dev/null; then
        DEV=/dev/$($BB basename "$p")
        echo "K606FLOG1 $(date +%H:%M:%S)" > $SAVE
        cat $L >> $SAVE
        echo "--- k606f events ---" >> $SAVE
        $BB cat /tmp/k606f-events.txt >> $SAVE 2>/dev/null
        echo "--- dmesg tail ---" >> $SAVE
        $BB tail -n 400 /tmp/dmesg-blmode.txt >> $SAVE 2>/dev/null
        $BB dd if=$SAVE of=$DEV bs=4096 seek=1 conv=fsync 2>/dev/null
        echo "saved to $DEV ($($BB wc -c < $SAVE) bytes)" >> $L
        break
      fi
    done
    [ -e "$DEV" ] || echo "no dtbo_b block device found" >> $L
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

# 上机用小工具（可选）：out/probe-bin/* -> /bin/
# 目前只有 i2cdump8（寄存器 dump），源码 tools/i2cdump8.c，静态 aarch64 编译：
#   aarch64-linux-gnu-gcc -static -O2 -o out/probe-bin/i2cdump8 tools/i2cdump8.c
if [ -d "$W/out/probe-bin" ]; then
  cp "$W"/out/probe-bin/* "$OUT/bin/" 2>/dev/null || true
  chmod 755 "$OUT"/bin/* 2>/dev/null || true
fi

# 内核模块（可选）：out/kmod-probe/*.ko -> /lib/modules
if [ -d "$W/out/kmod-probe" ]; then
  mkdir -p "$OUT/lib/modules"
  cp "$W"/out/kmod-probe/*.ko "$OUT/lib/modules/" 2>/dev/null || true
fi

cd "$OUT" && find . | cpio -o -H newc 2>/dev/null | gzip -9 > "$W/out/initramfs.cpio.gz"
ls -lh "$W/out/initramfs.cpio.gz"
