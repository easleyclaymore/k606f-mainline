#!/system/bin/sh
# 只读取证与备份：GPT、启动分区、persist、固件清单。全部只读 dd。
set -u
DST=/sdcard/YTK606F-backup
mkdir -p "$DST/gpt" "$DST/images" "$DST/firmware" "$DST/meta"
cd "$DST" || exit 1
LOG="$DST/meta/backup.log"
: > "$LOG"
say() { echo "$@" | tee -a "$LOG"; }

say "== 时间 =="
date -Iseconds 2>/dev/null || date

say ""
say "== 块设备 LUN =="
for d in /dev/block/sd?; do
  [ -e "$d" ] || continue
  sz=$(blockdev --getsize64 "$d" 2>/dev/null)
  say "$d size=$sz"
done

say ""
say "== by-name 映射（分区 -> 实际设备）=="
ls -l /dev/block/by-name/ | sed 's/  */ /g' | tee -a "$LOG"

say ""
say "== 每个 LUN 的 GPT（主 34 扇区 + 备份 34 扇区）=="
for d in /dev/block/sd?; do
  [ -e "$d" ] || continue
  n=$(basename "$d")
  secs=$(blockdev --getsz "$d" 2>/dev/null) || continue
  dd if="$d" of="$DST/gpt/${n}_gpt_primary.bin" bs=512 count=34 2>/dev/null
  off=$((secs-34))
  dd if="$d" of="$DST/gpt/${n}_gpt_backup.bin" bs=512 skip=$off count=34 2>/dev/null
  echo "${n} sectors=$secs" >> "$DST/gpt/summary.txt"
  say "$n sectors=$secs primary=$(wc -c < "$DST/gpt/${n}_gpt_primary.bin") backup=$(wc -c < "$DST/gpt/${n}_gpt_backup.bin")"
done

say ""
say "== 启动相关分区整分区镜像 =="
for p in vbmeta_a vbmeta_b vbmeta_system_a vbmeta_system_b vbmeta_vendor_a \
         boot_a boot_b dtbo_a dtbo_b persist metadata; do
  src=/dev/block/by-name/$p
  [ -e "$src" ] || { say "skip $p (不存在)"; continue; }
  size=$(blockdev --getsize64 "$src" 2>/dev/null)
  case "$p" in
    boot_*|dtbo_*|persist|metadata) out="$DST/images/$p.img" ;;
    *) out="$DST/images/$p.img" ;;
  esac
  dd if="$src" of="$out" bs=1M 2>/dev/null
  got=$(wc -c < "$out")
  say "$p size=$size backed=$got"
done

say ""
say "== super 只读信息（不备份整块）=="
sup=/dev/block/by-name/super
if [ -e "$sup" ]; then
  say "super size=$(blockdev --getsize64 "$sup")"
  dd if="$sup" of="$DST/meta/super_gpt_head.bin" bs=512 count=34 2>/dev/null
fi

say ""
say "== 固件清单（/vendor/firmware 全量 sha256）=="
find /vendor/firmware -type f 2>/dev/null | sort | while read -r f; do
  sha256sum "$f" 2>/dev/null
done > "$DST/meta/firmware-sha256.txt"
wc -l "$DST/meta/firmware-sha256.txt" | tee -a "$LOG"
du -sh /vendor/firmware 2>/dev/null | tee -a "$LOG"

say ""
say "== 需要的固件副本 =="
for f in a650_gmu.bin a650_sqe.fw a650_zap.mdt a650_zap.elf a650_zap.b00 a650_zap.b01 a650_zap.b02 \
         goodix_firmware.bin goodix_cfg_group.bin; do
  [ -f "/vendor/firmware/$f" ] && cp -f "/vendor/firmware/$f" "$DST/firmware/" && say "copied $f"
done
if [ -d /vendor/firmware/wlan ]; then
  mkdir -p "$DST/firmware/wlan"
  cp -rf /vendor/firmware/wlan/. "$DST/firmware/wlan/" 2>/dev/null && say "copied wlan/ dir: $(du -sh "$DST/firmware/wlan" | cut -f1)"
fi

say ""
say "== 备份清单与哈希 =="
cd "$DST" && find . -type f ! -name 'sha256sums.txt' -exec sha256sum {} \; | sort -k2 > "$DST/meta/sha256sums.txt"
say "files=$(wc -l < "$DST/meta/sha256sums.txt")"
du -sh "$DST" | tee -a "$LOG"
chmod -R a+r "$DST"
say "DONE"
