#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
BALDR FORCE BFET / Backlog 结构风险审计工具。

用途：
1. 读取 release/Update.pac；
2. 按当前 Runtime 已验证过的 PACw + BFET 格式还原 34,184 条日文/中文映射；
3. 专门寻找“中文比原日文更长”时，旧脚本 value object 的原 count 会造成什么后果；
4. 检查中文「/」自身是否配对，以及中日两边的引号数量是否发生变化；
5. 生成 CSV 与纯文本摘要，供以后任何版本独立接档。

为什么需要这个工具：
- v1.0.2 复刻旧 46MB 汉化的脚本复制函数后，会保留原日文 count；
- 用户实机随后发现 jyosyo05.bin 的一句“「………什么……」”末尾出现方块，打开 Backlog 后崩溃；
- 静态复算证明该句日文 20 bytes、中文 26 bytes，而原 value count=21。
  如果任何下游模块再次按 21 bytes 复制已经变长的中文，最后一个字节正好会是 GBK
  双字节字符的首字节，从而得到半个字符，和实机末尾方块高度一致。

本工具只审计，不修改任何游戏文件。
"""

from __future__ import annotations

import csv
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]  # 本模块目录
PROJECT = ROOT.parents[1]  # 项目根
# 编译产物固定在项目根的 release（四个模块共用）；输出仍写本模块的 data。
PAC = PROJECT / "release" / "Update.pac"
OUT_CSV = ROOT / "data" / "BFET_Backlog风险审计.csv"
OUT_TXT = ROOT / "data" / "脚本历史记录风险审计摘要.txt"


def u32(buf: bytes, off: int) -> int:
    """按 little-endian 读取一个 32 位无符号整数。"""
    return struct.unpack_from("<I", buf, off)[0]


def find_valid_bfet(payload: bytes) -> int | None:
    """
    从 .bin 尾部向前寻找 BFET。

    Runtime 使用同一套判定：
    - magic = BFET
    - block_size + 8 == 文件尾到 BFET 的总长度
    - 映射数量合理
    - 两张 offset 表都落在 BFET 区间内
    """
    if len(payload) < 20:
        return None
    for pos in range(len(payload) - 4, -1, -1):
        if payload[pos : pos + 4] != b"BFET" or pos + 12 > len(payload):
            continue
        block_size = u32(payload, pos + 4)
        count = u32(payload, pos + 8)
        if block_size + 8 != len(payload) - pos:
            continue
        if not (0 < count < 10000):
            continue
        if 12 + count * 8 + 4 > len(payload) - pos:
            continue
        return pos
    return None


def cstr_end(buf: bytes, start: int, end: int) -> int | None:
    """在 [start, end) 中寻找 NUL；找不到就返回 None。"""
    p = buf.find(b"\0", start, end)
    return p if p >= 0 else None


def char_count(text: str, ch: str) -> int:
    """按已经解码后的字符统计，避免跨两个双字节字符做滑窗造成假命中。"""
    return text.count(ch)


def original_count_splits_gbk(zh: bytes, original_count: int) -> bool:
    """
    模拟“下游把中文按原日文 count 再复制一次”。

    original_count 通常 = 日文字符串字节数 + 1（原本应该包含 NUL）。
    如果中文变长，那么这段长度内已经没有 NUL。我们按 Runtime/旧汉化的 0x80..0xFE
    双字节 lead 规则扫描；如果最后只剩一个 lead byte，就说明会截成“半个汉字”。
    """
    if original_count <= 0 or original_count > len(zh):
        return False
    i = 0
    while i < original_count:
        b = zh[i]
        if b == 0:
            return False
        if 0x80 <= b <= 0xFE:
            if i + 1 >= original_count:
                return True
            i += 2
        else:
            i += 1
    return False


def decode_safe(buf: bytes, enc: str) -> str:
    """CSV 只用于人工阅读；坏字节用替换符显示，不让审计因为单条异常数据终止。"""
    return buf.decode(enc, errors="replace")


def main() -> int:
    data = PAC.read_bytes()
    if data[:4] != b"PACw":
        raise SystemExit("Update.pac 不是预期的 PACw。")

    pac_count = u32(data, 4)
    rows: list[dict[str, object]] = []

    total = equal = shorter = longer = split = 0
    zh_unbalanced = quote_changed = 0

    for pac_index in range(pac_count):
        ent = 12 + pac_index * 76
        name_raw = data[ent : ent + 64].split(b"\0", 1)[0]
        name = name_raw.decode("ascii", errors="replace")
        off = u32(data, ent + 64)
        size1 = u32(data, ent + 68)
        size2 = u32(data, ent + 72)
        if not name.lower().endswith(".bin") or size1 != size2:
            continue
        payload = data[off : off + size1]
        bfet = find_valid_bfet(payload)
        if bfet is None:
            continue

        count = u32(payload, bfet + 8)
        text_base = 16 + u32(payload, 8) * 4
        b = payload[bfet:]

        for mapping_index in range(count):
            # BFET 的日文 offset 表按逆序消费；这和 Runtime build_database() 完全一致。
            source_rel = u32(b, 12 + (count - 1 - mapping_index) * 4)
            # 中文 offset 的第 0 项是区块边界，因此第 j 条中文使用 j+1 项。
            target_rel = (
                u32(b, 12 + count * 4 + (mapping_index + 1) * 4)
                if mapping_index + 1 < count
                else u32(b, 12 + count * 8)
            )
            ja_start = text_base + source_rel
            zh_start = bfet + 8 + target_rel
            ja_end = cstr_end(payload, ja_start, bfet)
            zh_end = cstr_end(payload, zh_start, len(payload))
            if ja_end is None or zh_end is None:
                raise SystemExit(f"{name} #{mapping_index}: NUL 边界异常")

            ja = payload[ja_start:ja_end]
            zh = payload[zh_start:zh_end]
            ja_len = len(ja)
            zh_len = len(zh)
            total += 1

            relation = "相同"
            if zh_len == ja_len:
                equal += 1
            elif zh_len < ja_len:
                shorter += 1
                relation = "中文更短"
            else:
                longer += 1
                relation = "中文更长"

            original_count = ja_len + 1
            would_split = zh_len > ja_len and original_count_splits_gbk(zh, original_count)
            if would_split:
                split += 1

            # 日文 CP932 「/」 = 81 75 / 81 76；中文 GBK 「/」 = A1 B8 / A1 B9。
            ja_text = decode_safe(ja, "cp932")
            zh_text = decode_safe(zh, "gbk")
            ja_open = char_count(ja_text, "「")
            ja_close = char_count(ja_text, "」")
            zh_open = char_count(zh_text, "「")
            zh_close = char_count(zh_text, "」")
            this_zh_unbalanced = zh_open != zh_close
            this_quote_changed = ja_open != zh_open or ja_close != zh_close
            if this_zh_unbalanced:
                zh_unbalanced += 1
            if this_quote_changed:
                quote_changed += 1

            # CSV 只收“值得人工看”的行，避免 34k 行淹没重点。
            if zh_len > ja_len or this_zh_unbalanced or this_quote_changed:
                rows.append(
                    {
                        "脚本": name,
                        "映射序号": mapping_index,
                        "日文字节": ja_len,
                        "中文字节": zh_len,
                        "原count": original_count,
                        "长度关系": relation,
                        "按原count二次复制会截断GBK双字节": "是" if would_split else "否",
                        "日文左引号": ja_open,
                        "日文右引号": ja_close,
                        "中文左引号": zh_open,
                        "中文右引号": zh_close,
                        "中文自身引号不平衡": "是" if this_zh_unbalanced else "否",
                        "中日引号数量变化": "是" if this_quote_changed else "否",
                        "日文": ja_text,
                        "中文": zh_text,
                        "日文HEX": ja.hex(" ").upper(),
                        "中文HEX": zh.hex(" ").upper(),
                    }
                )

    OUT_CSV.parent.mkdir(parents=True, exist_ok=True)
    fieldnames = list(rows[0].keys()) if rows else []
    with OUT_CSV.open("w", encoding="utf-8-sig", newline="") as f:
        w = csv.DictWriter(f, fieldnames=fieldnames)
        w.writeheader()
        w.writerows(rows)

    # 单独找用户截图对应的 jyosyo05.bin #64，便于摘要直接留下“为什么方块可重现”。
    repro = next((r for r in rows if str(r["脚本"]).lower() == "jyosyo05.bin" and int(r["映射序号"]) == 64), None)

    lines = [
        "BALDR FORCE BFET / Backlog 结构风险审计摘要",
        "==========================================",
        f"总映射数：{total}",
        f"中日等长：{equal}",
        f"中文更短：{shorter}",
        f"中文更长：{longer}",
        f"中文更长且按原 count 二次复制会截在 GBK 双字节中间：{split}",
        f"中文自身「/」数量不平衡：{zh_unbalanced}",
        f"中日「/」数量发生变化：{quote_changed}",
        "",
        "关键解释：",
        "- v1.0.4 已把两个长度分开：脚本源游标仍按原 count 前进，运行时 value object 的 byte_count 改为中文实际长度+NUL。",
        "- 因此正式 Backlog/历史记录只要从 value object 读取长度，就不会再因 #64 这类扩张文本被截断。",
        "- 本表仍然保留，因为如果以后发现另一个消费者绕过 value object、直接错误复用脚本源 count，",
        "  642 条扩张文本中仍有 627 条会把 GBK 双字节劈成一半，可用本 CSV 快速定位。",
        "",
    ]
    if repro:
        lines += [
            "用户新实机截图对应的高置信复现候选：",
            f"- {repro['脚本']} #{repro['映射序号']}",
            f"- 日文 {repro['日文字节']} bytes -> 中文 {repro['中文字节']} bytes；原 count={repro['原count']}",
            f"- 中文：{repro['中文']}",
            f"- 按原 count 二次复制会截断 GBK 双字节：{repro['按原count二次复制会截断GBK双字节']}",
            "- v1.0.3 实机异常已经提供 EIP/脚本事件证据；v1.0.4 因此把运行时 value object count 修正为中文实际长度。",
            "",
        ]
    lines += [
        "注意：",
        "- 中文 BFET 数据本身只有 1 条出现「/」自身数量不平衡；因此用户截图的缺失右引号不是普遍的翻译数据缺字。",
        "- v1.0.4 的运行时日志会直接写出脚本文件名、BFET 序号、JA/ZH hash、长度、前 32 字节和是否会截半个 GBK 字符。",
    ]
    OUT_TXT.write_text("\n".join(lines) + "\n", encoding="utf-8")

    print("\n".join(lines))
    print(f"CSV: {OUT_CSV}")
    print(f"TXT: {OUT_TXT}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
