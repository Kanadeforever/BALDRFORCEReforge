#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
把 BFE/BFSE 两份 BGMInfo.DAT 解析为：
1. C++ 编译期使用的 BFSE BGM 元数据表；
2. 供人工核对的中文命名 CSV。

这个工具故意不碰 BGM.pac。
原因是当前插件的设计目标是“让 BFE 按原来的 0~29 BGM 编号播放 BFSE 同编号曲目”，
真正需要从 BFSE 迁移的只有：
    - 资源文件名；
    - 循环起点。

BGMInfo.DAT 已经通过 EXE 反汇编确认：
    30 条记录 × 0x44 字节 + 9 字节相同表尾。
每条 0x44：
    +0x00..+0x3F = CP932 文件名缓冲区；
    +0x40..+0x43 = int32 循环起点，单位 1/100 秒；-1 表示非循环。

源码注释写得比较啰嗦是有意的：这个项目要求任何单独拿到源码的人都能接档，
并且尽量让刚学编程的人也能看懂每一步在验证什么。
"""

from __future__ import annotations

import csv
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DATA = ROOT / "data"
SRC = ROOT / "src"

BFE_DAT = DATA / "BFE_BGMInfo.DAT"
BFSE_DAT = DATA / "BFSE_BGMInfo.DAT"
HEADER_OUT = SRC / "BgmTable.generated.h"
CSV_OUT = DATA / "BGM信息对照.csv"

RECORD_SIZE = 0x44
NAME_SIZE = 0x40
RECORD_COUNT = 30
BODY_SIZE = RECORD_SIZE * RECORD_COUNT
EXPECTED_TOTAL_SIZE = BODY_SIZE + 9
EXPECTED_TAIL = b"av\x00av\x00av\x00"


def read_dat(path: Path) -> bytes:
    """读取并严格验证一份 BGMInfo.DAT。"""
    data = path.read_bytes()

    # 2049 = 30 * 68 + 9。尺寸不对时立即失败，避免把别的版本误当成当前基线。
    if len(data) != EXPECTED_TOTAL_SIZE:
        raise SystemExit(
            f"[失败] {path.name} 大小应为 {EXPECTED_TOTAL_SIZE} 字节，实际为 {len(data)} 字节"
        )

    # 两版文件末尾都确认是 av\0av\0av\0。
    # 30 条记录加载循环不会读取这 9 字节，但保留校验可以帮助发现输入文件版本变化。
    tail = data[BODY_SIZE:]
    if tail != EXPECTED_TAIL:
        raise SystemExit(
            f"[失败] {path.name} 表尾不是已确认的 av\\0av\\0av\\0：{tail.hex(' ')}"
        )

    return data


def decode_name(record: bytes) -> str:
    """把固定 0x40 字节文件名字段按 NUL 截断，再用 CP932 解码。"""
    raw = record[:NAME_SIZE].split(b"\x00", 1)[0]
    return raw.decode("cp932")


def parse_records(data: bytes) -> list[dict[str, object]]:
    """把 30 条固定记录拆成 Python 字典，方便后续输出。"""
    records: list[dict[str, object]] = []

    for index in range(RECORD_COUNT):
        start = index * RECORD_SIZE
        raw = data[start : start + RECORD_SIZE]

        # +0x40 是有符号 int32：-1 代表“不使用循环播放入口”。
        loop_cs = struct.unpack_from("<i", raw, 0x40)[0]

        records.append(
            {
                "index": index,
                "raw": raw,
                "name": decode_name(raw),
                "loop_cs": loop_cs,
            }
        )

    return records


def bytes_as_cpp(raw: bytes) -> str:
    """把二进制写成 C++ 十六进制数组；这样不会依赖源文件字符编码。"""
    return ", ".join(f"0x{value:02X}" for value in raw)


def main() -> None:
    bfe_data = read_dat(BFE_DAT)
    bfse_data = read_dat(BFSE_DAT)

    bfe = parse_records(bfe_data)
    bfse = parse_records(bfse_data)

    # 额外确认两边都是 30 项，并且编号位置可以一一配对。
    if len(bfe) != RECORD_COUNT or len(bfse) != RECORD_COUNT:
        raise SystemExit("[失败] BGM 记录数不是 30，停止生成")

    # 生成 C++ 头文件。
    # 运行时真正需要两类信息：
    #   1. BFSE 资源名：BGM loader 打开 BFSE_BGM.pac / BGM.pac 中的对应 WAV；
    #   2. BFSE 循环起点：BFE 的流式播放对象在到达文件末尾后从这个位置重新读取。
    lines: list[str] = []
    lines.append("#pragma once")
    lines.append("")
    lines.append("// 本文件由 tools/generate_bgm_table.py 自动生成，请不要手工编辑。")
    lines.append("// 每条记录仍保留原始 0x44 字节，便于运行时直接取文件名和 +0x40 循环点。")
    lines.append("")
    lines.append("static const unsigned int kBfseBgmRecordCount = 30u;")
    lines.append("static const unsigned int kBgmRecordSize = 0x44u;")
    lines.append("static const unsigned int kBgmNameSize = 0x40u;")
    lines.append("")
    lines.append("static const unsigned char kBfseBgmInfo[30][0x44] =")
    lines.append("{")

    for item in bfse:
        index = int(item["index"])
        name = str(item["name"])
        loop_cs = int(item["loop_cs"])
        raw = bytes(item["raw"])

        # 注释只用于人类阅读；真正二进制内容使用十六进制数组，避免 CP932/UTF-8 编码混淆。
        lines.append(
            f"    // #{index + 1:02d}  {name}  loop={loop_cs} (1/100 秒；-1=非循环)"
        )
        lines.append(f"    {{ {bytes_as_cpp(raw)} }},")

    lines.append("};")
    lines.append("")
    HEADER_OUT.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")

    # 生成中文名 CSV，方便不看十六进制的人快速比较 30 首曲目的资源名和循环点。
    with CSV_OUT.open("w", encoding="utf-8-sig", newline="") as f:
        writer = csv.writer(f)
        writer.writerow(
            [
                "编号",
                "BFE资源文件名",
                "BFE循环点(1/100秒)",
                "BFE循环点(秒)",
                "BFSE资源文件名",
                "BFSE循环点(1/100秒)",
                "BFSE循环点(秒)",
            ]
        )

        for old, new in zip(bfe, bfse):
            old_loop = int(old["loop_cs"])
            new_loop = int(new["loop_cs"])
            writer.writerow(
                [
                    int(old["index"]) + 1,
                    old["name"],
                    old_loop,
                    "非循环" if old_loop < 0 else f"{old_loop / 100:.2f}",
                    new["name"],
                    new_loop,
                    "非循环" if new_loop < 0 else f"{new_loop / 100:.2f}",
                ]
            )

    looped = sum(1 for item in bfse if int(item["loop_cs"]) >= 0)
    no_loop = RECORD_COUNT - looped

    print(f"[成功] BFE/BFSE 均验证为 {RECORD_COUNT} 条 × 0x44 + 9 字节表尾")
    print(f"[成功] BFSE 循环曲目={looped}，非循环曲目={no_loop}")
    print(f"[输出] {HEADER_OUT}")
    print(f"[输出] {CSV_OUT}")


if __name__ == "__main__":
    main()
