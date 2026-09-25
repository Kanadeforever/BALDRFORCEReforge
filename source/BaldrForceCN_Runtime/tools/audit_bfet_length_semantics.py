#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
BFET 中日文本长度差异审计工具。

用途：
    这个脚本只读取 release/Update.pac，不修改任何游戏文件。
    它把 300 个带 BFET 尾部的脚本逐个解析，统计：
      1. 日文原字符串和中文 GBK 字符串长度完全相同的条数；
      2. 中文比日文短的条数；
      3. 中文比日文长的条数；
      4. 最极端的缩短/增长案例；
      5. 当前 v1.0.1 稳定崩溃复现点 jyosyo05.bin 的两条关键字符串。

为什么要做这个统计：
    v1.0.0/v1.0.1 的 Hook 在替换文本后会把 value object 的 byte_count
    从“原日文长度+1”改成“中文长度+1”。后来反编译旧 46MB 汉化运行时发现，
    旧汉化从来不改这个 byte_count；它只 strcpy 中文，再让脚本游标按原始 count 前进。

    如果只有几条翻译长度不同，这个错误可能只是单点风险；但实际 34,184 条里绝大多数
    长度都不同，所以必须把它当成系统性语义差异处理，而不是只给某一句打补丁。

运行：
    python tools/audit_bfet_length_semantics.py

输出：
    只打印统计，不创建文件。
"""

from __future__ import annotations

import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]  # 本模块目录
PROJECT = ROOT.parents[1]  # 项目根
# 编译产物固定在项目根的 release（四个模块共用），不再放在模块内。
PAC = PROJECT / "release" / "Update.pac"


def u32(data: bytes, offset: int) -> int:
    """从 data[offset:offset+4] 读取 little-endian uint32。"""
    return struct.unpack_from("<I", data, offset)[0]


def cstr_end(data: bytes, start: int, end: int) -> int:
    """在 [start, end) 中寻找 C 字符串结尾 NUL；找不到就抛异常。"""
    pos = data.find(b"\0", start, end)
    if pos < 0:
        raise ValueError(f"字符串 0x{start:X} 在边界 0x{end:X} 前没有 NUL")
    return pos


def find_valid_bfet(payload: bytes) -> int | None:
    """从文件尾向前找与本项目 Runtime 相同规则的有效 BFET 尾部。"""
    if len(payload) < 20:
        return None
    for pos in range(len(payload) - 4, -1, -1):
        if payload[pos : pos + 4] != b"BFET" or pos + 12 > len(payload):
            continue
        block_size = u32(payload, pos + 4)
        count = u32(payload, pos + 8)
        if (
            block_size + 8 == len(payload) - pos
            and 0 < count < 10000
            and 12 + count * 8 + 4 <= len(payload) - pos
        ):
            return pos
    return None


def main() -> int:
    data = PAC.read_bytes()
    if data[:4] != b"PACw":
        raise SystemExit("Update.pac 不是预期的 PACw 容器")

    count = u32(data, 4)
    equal = shorter = longer = total = 0
    max_short = (0, None)
    max_long = (0, None)
    crash_rows: list[tuple] = []

    for entry_index in range(count):
        entry = 12 + entry_index * 76
        raw_name = data[entry : entry + 64].split(b"\0", 1)[0]
        name = raw_name.decode("ascii", "replace")
        offset = u32(data, entry + 64)
        size1 = u32(data, entry + 68)
        size2 = u32(data, entry + 72)

        # 只有未压缩 .bin 才是当前 Runtime 的 BFET 脚本来源。
        if not name.lower().endswith(".bin") or size1 != size2:
            continue
        payload = data[offset : offset + size1]
        bfet = find_valid_bfet(payload)
        if bfet is None:
            continue

        mapping_count = u32(payload, bfet + 8)
        text_base = 16 + u32(payload, 8) * 4

        for mapping_index in range(mapping_count):
            # 这两张 offset 表的方向与 Runtime 的 build_database() 完全一致。
            source_rel = u32(payload, bfet + 12 + (mapping_count - 1 - mapping_index) * 4)
            if mapping_index + 1 < mapping_count:
                target_rel = u32(payload, bfet + 12 + mapping_count * 4 + (mapping_index + 1) * 4)
            else:
                target_rel = u32(payload, bfet + 12 + mapping_count * 8)

            source_abs = text_base + source_rel
            target_abs = bfet + 8 + target_rel
            source_end = cstr_end(payload, source_abs, bfet)
            target_end = cstr_end(payload, target_abs, len(payload))

            ja = payload[source_abs:source_end]
            zh = payload[target_abs:target_end]
            ja_len = len(ja)
            zh_len = len(zh)
            delta = zh_len - ja_len
            total += 1

            if delta == 0:
                equal += 1
            elif delta < 0:
                shorter += 1
                if -delta > max_short[0]:
                    max_short = (-delta, (name, mapping_index, ja_len, zh_len, ja, zh))
            else:
                longer += 1
                if delta > max_long[0]:
                    max_long = (delta, (name, mapping_index, ja_len, zh_len, ja, zh))

            # 用户当前稳定崩溃位置：名片标题卡，以及它后面紧接的一句旁白。
            if name.lower() == "jyosyo05.bin" and mapping_index in (49, 50):
                crash_rows.append((mapping_index, ja_len, zh_len, ja, zh))

    print("BFET 映射总数：", total)
    print("长度相同：      ", equal)
    print("中文更短：      ", shorter)
    print("中文更长：      ", longer)
    print("长度不同合计：  ", shorter + longer)

    if max_short[1]:
        name, idx, ja_len, zh_len, ja, zh = max_short[1]
        print(f"最大缩短：{max_short[0]} 字节，{name} #{idx}，{ja_len}->{zh_len}")
        print("  JA:", ja.decode("cp932", "replace"))
        print("  ZH:", zh.decode("gbk", "replace"))
    if max_long[1]:
        name, idx, ja_len, zh_len, ja, zh = max_long[1]
        print(f"最大增长：{max_long[0]} 字节，{name} #{idx}，{ja_len}->{zh_len}")
        print("  JA:", ja.decode("cp932", "replace"))
        print("  ZH:", zh.decode("gbk", "replace"))

    print("\n当前崩溃复现点：")
    for idx, ja_len, zh_len, ja, zh in crash_rows:
        print(f"  jyosyo05.bin #{idx}: {ja_len}->{zh_len}")
        print("    JA:", ja.decode("cp932", "replace"))
        print("    ZH:", zh.decode("gbk", "replace"))

    # 固定断言，防止未来换 PAC 或 parser 后统计悄悄变掉。
    assert total == 34184
    assert (equal, shorter, longer) == (3909, 29633, 642)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
