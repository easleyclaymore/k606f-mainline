#!/usr/bin/env bash
# 文档自检：把「纪律」变成可执行的检查，免得靠人记。
# 用法：tools/check-docs.sh          在集成仓根目录跑
# 退出码：0 全过；1 有问题（逐条打印）
#
# 检查项对应 android-mainline-port skill：
#   references/discipline.md §1 文档齐备、§2 技术债表、§5 署名与推送
#   references/verification.md 状态词表

set -uo pipefail
cd "$(dirname "$0")/.." || exit 1

fail=0
err() { printf '  ✗ %s\n' "$1"; fail=1; }
ok()  { printf '  ✓ %s\n' "$1"; }

echo "1) 必需文档"
for f in AGENTS.md README.md docs/status.md docs/worklog.md docs/tech-debt.md docs/abandoned.md; do
  [ -s "$f" ] && ok "$f" || err "缺 $f"
done

echo "2) status.md 状态词合法（待调研/有源码线索/离线通过/真机启动/真机通过/真机失败）"
bad=$(awk -F'|' '
  /^\|/ && NF >= 3 {
    s = $3
    gsub(/^[ \t*]+|[ \t*]+$/, "", s)
    gsub(/\*\*/, "", s)
    if (s == "" || s ~ /^-+$/ || s == "状态" || s == "项") next
    if (s !~ /^(待调研|有源码线索|离线通过|真机启动|真机通过|真机失败)$/) print NR": "s
  }' docs/status.md)
if [ -z "$bad" ]; then ok "全部合法"; else printf '%s\n' "$bad" | while read -r l; do err "非标准状态词 → $l"; done; fail=1; fi

echo "3) status.md 必备要素"
for k in "下一步" "设备状态" "当前阻塞" "操作者"; do
  grep -q "$k" docs/status.md && ok "含「$k」" || err "缺「$k」"
done

echo "4) tech-debt.md 表格列数一致、表内无插入文字"
cols=$(awk -F'|' '/^\|/{print NF-2}' docs/tech-debt.md | sort -u | tr '\n' ' ')
case "$cols" in
  "7 "|"") ok "统一 7 列" ;;
  *) err "列数不一致：$cols" ;;
esac
# 表头与最后一行表格之间夹着的非表格行会把表格截断
inter=$(awk '
  { line[NR]=$0 }
  /^\|/ { if (first==0) first=NR; last=NR }
  END { for (i=first+1; i<last; i++)
          if (line[i] !~ /^\|/ && line[i] !~ /^[ \t]*$/ && line[i] !~ /^#/ ) print i": "line[i] }
' docs/tech-debt.md)
if [ -z "$inter" ]; then ok "表格连续"; else printf '%s\n' "$inter" | while read -r l; do err "表格被截断 → $l"; done; fail=1; fi

echo "5) AGENTS.md 必备条目"
for k in "授权边界" "Co-Authored-By" "先问用户" "不进公开仓" "出厂"; do
  grep -q "$k" AGENTS.md && ok "含「$k」" || err "缺「$k」"
done

echo "6) 出厂内核版本未与 OTA 版本混淆"
if grep -q "4\.19\.136" AGENTS.md; then ok "AGENTS.md 记了出厂 4.19.136"; else err "AGENTS.md 未记出厂内核 4.19.136"; fi
grep -q "不是出厂" AGENTS.md || grep -q "不是出厂版本" README.md && ok "已标注 4.19.157 非出厂" \
  || err "未标注 4.19.157-perf+ 不是出厂版本"

echo
if [ "$fail" -eq 0 ]; then echo "全部通过"; else echo "有未通过项，见上"; fi
exit "$fail"
