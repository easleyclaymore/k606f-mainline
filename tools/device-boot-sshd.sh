#!/system/bin/sh
# 开机自启 Termux → ~/.bashrc 里的 sshd 起来。
# 限制：Android 12 在锁屏状态下拦截后台启动 Activity，所以只能"等解锁后再拉起"。
#       本脚本每 15 秒试一次，最多试 40 次（约 10 分钟）；一旦 sshd 监听就停，并把 Termux 丢到后台。
LOG=/data/local/tmp/termux-sshd-boot.log
log() { echo "$(date '+%F %T') $*" >> "$LOG"; }
listening() { netstat -ltn 2>/dev/null | grep -q ':8022 '; }

(
  i=0
  while [ "$(getprop sys.boot_completed)" != "1" ] && [ $i -lt 120 ]; do sleep 2; i=$((i+1)); done
  log "boot_completed=1"
  sleep 8

  n=0
  while [ $n -lt 40 ]; do
    if listening; then
      log "8022 已监听（第 $n 次检查）"
      # 若 Termux 在前台，切到后台；否则不动
      if dumpsys window 2>/dev/null | grep -q "mCurrentFocus.*com.termux"; then
        input keyevent KEYCODE_HOME >/dev/null 2>&1
        log "Termux 在前台，已切到后台"
      fi
      exit 0
    fi
    n=$((n+1))
    am start -n com.termux/.HomeActivity --user 0 >/dev/null 2>&1
    sleep 15
  done
  log "警告：10 分钟内未能拉起 sshd"
) &
