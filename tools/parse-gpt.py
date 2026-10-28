#!/usr/bin/env python3
"""解析备份出的 GPT 头（主/备份），支持 512 与 4096 逻辑扇区。
用法：parse-gpt.py <file> [...]"""
import struct, sys, uuid


def parse(path):
    d = open(path, "rb").read()
    off = d.find(b"EFI PART")
    if off < 0:
        print(f"{path}: 未找到 GPT 头")
        return
    ss = off                      # 头在 LBA1，所以文件内偏移即逻辑扇区大小
    rev, hsz, crc, _rsv, cur, bak, first_use, last_use, guid, pel, n_part, psz, _ecrc = \
        struct.unpack_from("<IIIIQQQQ16sQIII", d, off + 8)
    print(f"## {path.split('/')[-1]}  (逻辑扇区 {ss} 字节)")
    print(f"- 当前 LBA {cur}，备份头 LBA {bak}，可用 {first_use}..{last_use}；分区项 {n_part}×{psz}B @LBA{pel}")
    print(f"- 磁盘 GUID `{uuid.UUID(bytes_le=guid)}`")
    print()
    print("| # | 分区名 | 起始 LBA | 结束 LBA | 扇区数 | 大小 | 类型 GUID |")
    print("|---|---|---|---|---|---|---|")
    base = pel * ss
    for i in range(n_part):
        e = d[base + i * psz: base + (i + 1) * psz]
        if len(e) < 128:
            break
        tguid = uuid.UUID(bytes_le=e[0:16])
        if tguid.int == 0:
            continue
        flba, llba, _attr = struct.unpack_from("<QQQ", e, 32)
        name = e[56:128].decode("utf-16-le").rstrip("\x00")
        size = (llba - flba + 1) * ss
        print(f"| {i+1} | {name} | {flba} | {llba} | {llba-flba+1} | {size/2**20:.1f} MiB | `{tguid}` |")
    print()


if __name__ == "__main__":
    for p in sys.argv[1:]:
        parse(p)
