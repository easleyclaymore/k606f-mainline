# 调查记录（内部工作文档镜像）

这两份是我们移植过程中的**原始调查记录**，未经润色，按时间顺序追加。
供其他做同类移植的人参考：哪些路走过、哪些假设被否掉、证据长什么样。

| 文件 | 内容 |
|---|---|
| [`display-bringup.md`](display-bringup.md) | 显示接管的全部技术调查：DSI→eDP 桥链路、逐字节寄存器对齐、EOT 修复、随机坏链、单链 bisect |
| [`bringup-rounds.md`](bringup-rounds.md) | 每一次上机（B1-B67）的编号台账：改了什么变量、结果、证据路径 |

## 怎么读

- `display-bringup.md` 的**「当前结论摘要」**小节是最新状态（已确认事实 + 已否定假设全表）；
  更早的章节凡标 ⚠ 已被推翻，保留原文是刻意的（失败记录不删）。
- `bringup-rounds.md` 每行一次上机：**只记改变了哪一个变量**、机器可复核的结果、
  人眼观察单独记。B 编号可以在两份文档间互相索引。
- 配套的内核改动与桥驱动见 [`../../kernel/`](../../kernel/)，上机脚本见 [`../../tools/`](../../tools/)。

## 当前状态（2026-11-01）

双 DSI 链路：配置层与 bootloader 工作态逐位对齐后，屏幕仍黑（桥报 CHA/CHB
INVALID_LENGTH_ERR，随机坏链）——已排除清单见 display-bringup.md 的摘要表。
单链 bisect 进行中：单链内核在 display probe 静默挂死（非崩溃），取证设施
（增量日志落盘、ramoops/pstore、双 boot 协议）已建成。
