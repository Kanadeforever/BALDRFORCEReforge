# -*- coding: utf-8 -*-
"""
build_review_sheet.py

用途
====
把 `build_voice_map.py` 生成的“仍未解决的 C / D 候选”整理成一张真正适合
人工一次性核验的 CSV。

为什么需要单独做这张表
======================
普通候选 CSV 已经有很多机器诊断字段，但人工真正判断“这是不是同一段剧情”时，
最需要同时看到：

1. BFSE 目标台词；
2. BFE 候选台词；
3. BFSE 目标前后各 20 条；
4. BFE 候选前后各 20 条；
5. VoiceTag / WAV 文件名；
6. 机器为什么没有自动通过。

因此这个工具不会重新做匹配，也不会修改任何 BIN。
它只是把现有候选和原始脚本重新排版成更容易人工阅读的表格。

重要原则
========
- 这个工具只读 BIN。
- 这个工具不改变 `build_voice_map.py` 的任何判定。
- CSV 中的“人工结论”默认留空，方便用户填写“通过 / 不通过”。
- 当前项目要求“一次性把所有待确认项给出来”，因此会同时包含主线与 Replay。
"""

from __future__ import annotations

import argparse
import csv
from pathlib import Path

# 直接复用主映射工具已经验证过的 BIN 解析器。
# 这样两个工具不会因为各自复制了一份解析代码而逐渐产生行为差异。
from build_voice_map import collect_bin_files, parse_bin


# 2026-09-20 的人工复核发现：全年龄版与成人版有些段落会一次改写十几条甚至几十条。
# 如果只显示前后 5 条，目标句可能正好落在整个“改写块”中央，看不到改写块两端真正相同的剧情锚点。
# 因此人工复核展示窗口扩展为前后各 20 条。
# 注意：这里只扩大“给人看”的范围，不提高 build_voice_map.py 的自动接受权限。
# 原因是更宽的窗口适合人根据剧情结构判断，却不应该让机器因此更激进地自动写入正式映射。
CONTEXT_RADIUS = 20


OUTPUT_FIELDS = [
    "范围",
    "脚本",
    "VoiceTag",
    "建议试听文件",
    "机器等级",
    "相似度",
    "第二候选相似度",
    "BFSE记录号",
    "BFE记录号",
    "预计BFE位置",
    "判定理由",
    "上下文判定说明",
    "BFSE目标台词",
    "BFE候选台词",
    "BFSE前20条",
    "BFSE后20条",
    "BFE前20条",
    "BFE后20条",
    "前文对齐明细",
    "后文对齐明细",
    "人工结论",
    "人工备注",
]


def read_candidates(path: Path) -> list[dict[str, str]]:
    """
    读取主映射工具生成的 `完整候选.csv`。

    这里只保留：

        - 没有人工结论；
        - 机器等级为 C 或 D。

    原因很简单：A/B 已经可以自动进入映射；以前已经人工确认过的项目也不需要
    再让用户重复劳动。
    """

    rows: list[dict[str, str]] = []

    with path.open("r", encoding="utf-8-sig", newline="") as handle:
        reader = csv.DictReader(handle)

        for row in reader:
            grade = (row.get("等级") or "").strip().upper()
            manual = (row.get("人工结论") or "").strip()

            if grade not in {"C", "D"}:
                continue

            if manual:
                continue

            script = (row.get("脚本") or "").strip()
            voice_tag = (row.get("VoiceTag") or "").strip()

            if not script or not voice_tag:
                continue

            rows.append(row)

    return rows


def format_context(records, center_index: int, start_delta: int, end_delta: int) -> str:
    """
    把目标附近的一段文本整理到一个 CSV 单元格中。

    例如目标记录号是 100，要求前 20 条时：

        #95  台词……
        #96  台词……
        #97  台词……
        #98  台词……
        #99  台词……

    使用换行而不是把所有台词挤成一长串，是为了让 Excel / LibreOffice 打开时
    更容易人工逐行看剧情顺序。
    """

    lines: list[str] = []

    start = center_index + start_delta
    end = center_index + end_delta

    for index in range(start, end + 1):
        # 脚本开头/结尾可能没有足够的 20 条上下文，越界直接跳过即可。
        if index < 0 or index >= len(records):
            continue

        text = records[index].clean_text
        lines.append(f"#{index}  {text}")

    return "\n".join(lines)


def build_row(
    source_row: dict[str, str],
    bfe_records,
    bfse_records,
) -> dict[str, str]:
    """把一条机器候选转换成适合人工复核的最终行。"""

    script = (source_row.get("脚本") or "").strip()
    voice_tag = (source_row.get("VoiceTag") or "").strip()

    bfse_index = int(source_row["BFSE记录号"])

    bfe_index_text = (source_row.get("BFE记录号") or "").strip()
    bfe_index = int(bfe_index_text) if bfe_index_text else None

    # Replay 单独标出来。
    # 它们目前没有新增 VoiceTag，但用户要求这次把全部待确认项一次给完，
    # 所以仍然放进同一张总表中。
    scope = "Replay" if script.lower().startswith("replay") else "主线"

    bfse_before = format_context(
        bfse_records,
        bfse_index,
        -CONTEXT_RADIUS,
        -1,
    )
    bfse_after = format_context(
        bfse_records,
        bfse_index,
        1,
        CONTEXT_RADIUS,
    )

    if bfe_index is None:
        bfe_target = ""
        bfe_before = ""
        bfe_after = ""
    else:
        bfe_target = bfe_records[bfe_index].clean_text
        bfe_before = format_context(
            bfe_records,
            bfe_index,
            -CONTEXT_RADIUS,
            -1,
        )
        bfe_after = format_context(
            bfe_records,
            bfe_index,
            1,
            CONTEXT_RADIUS,
        )

    # `VoiceTag.wav` 是用户用 GARbro 解包 Voice3 后最直观的试听文件名。
    # 如果某些资源以后发现不是 WAV，也只影响这个提示列，不影响映射本身。
    suggested_audio = f"{voice_tag}.wav"

    return {
        "范围": scope,
        "脚本": script,
        "VoiceTag": voice_tag,
        "建议试听文件": suggested_audio,
        "机器等级": source_row.get("等级", ""),
        "相似度": source_row.get("相似度", ""),
        "第二候选相似度": source_row.get("第二候选相似度", ""),
        "BFSE记录号": source_row.get("BFSE记录号", ""),
        "BFE记录号": source_row.get("BFE记录号", ""),
        "预计BFE位置": source_row.get("预计BFE位置", ""),
        "判定理由": source_row.get("判定理由", ""),
        "上下文判定说明": source_row.get("上下文判定说明", ""),
        "BFSE目标台词": source_row.get("BFSE正文", ""),
        "BFE候选台词": bfe_target,
        "BFSE前20条": bfse_before,
        "BFSE后20条": bfse_after,
        "BFE前20条": bfe_before,
        "BFE后20条": bfe_after,
        "前文对齐明细": source_row.get("前文对齐明细", ""),
        "后文对齐明细": source_row.get("后文对齐明细", ""),
        "人工结论": "",
        "人工备注": "",
    }


def write_csv(path: Path, rows: list[dict[str, str]]) -> None:
    """使用 UTF-8 BOM 输出 CSV，保证 Windows Excel 直接打开时不乱码。"""

    path.parent.mkdir(parents=True, exist_ok=True)

    with path.open("w", encoding="utf-8-sig", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=OUTPUT_FIELDS)
        writer.writeheader()
        writer.writerows(rows)


def run(args: argparse.Namespace) -> int:
    bfe_dir = Path(args.bfe).resolve()
    bfse_dir = Path(args.bfse).resolve()
    candidates_path = Path(args.candidates).resolve()
    output_path = Path(args.out).resolve()

    if not bfe_dir.is_dir():
        print(f"[错误] BFE 目录不存在：{bfe_dir}")
        return 1

    if not bfse_dir.is_dir():
        print(f"[错误] BFSE 目录不存在：{bfse_dir}")
        return 1

    if not candidates_path.is_file():
        print(f"[错误] 完整候选 CSV 不存在：{candidates_path}")
        return 1

    bfe_files = collect_bin_files(bfe_dir)
    bfse_files = collect_bin_files(bfse_dir)
    source_rows = read_candidates(candidates_path)

    # 缓存已经解析过的脚本。
    # 同一个脚本可能有很多语音候选，如果每一行都重新读取 BIN，会浪费大量时间。
    bfe_cache = {}
    bfse_cache = {}

    output_rows: list[dict[str, str]] = []

    for source_row in source_rows:
        script = source_row["脚本"].strip()
        key = script.lower()

        # BFSE 候选一定来自 BFSE 脚本；如果找不到，说明输入数据被破坏。
        if key not in bfse_files:
            raise RuntimeError(f"BFSE 中找不到候选脚本：{script}")

        if key not in bfse_cache:
            bfse_cache[key] = parse_bin(bfse_files[key])

        bfse_records = bfse_cache[key]

        # 极少数 BFSE 独有脚本在 BFE 中可能不存在。
        # 这种 D 候选仍然需要出现在总表中，因此用空列表表示没有 BFE 上下文。
        if key in bfe_files:
            if key not in bfe_cache:
                bfe_cache[key] = parse_bin(bfe_files[key])
            bfe_records = bfe_cache[key]
        else:
            bfe_records = []

        output_rows.append(
            build_row(
                source_row,
                bfe_records,
                bfse_records,
            )
        )

    # 人工核验时先看主线，再看 Replay；同一范围内按脚本和 BFSE 记录号排序。
    output_rows.sort(
        key=lambda row: (
            1 if row["范围"] == "Replay" else 0,
            row["脚本"].lower(),
            int(row["BFSE记录号"]),
            row["VoiceTag"],
        )
    )

    write_csv(output_path, output_rows)

    main_count = sum(row["范围"] == "主线" for row in output_rows)
    replay_count = sum(row["范围"] == "Replay" for row in output_rows)

    print(f"[完成] 一次性人工确认总数：{len(output_rows)}")
    print(f"[主线] {main_count}")
    print(f"[Replay] {replay_count}")
    print(f"[输出] {output_path}")

    return 0


def build_argument_parser() -> argparse.ArgumentParser:
    """建立命令行参数。"""

    parser = argparse.ArgumentParser(
        description="把 BFE/BFSE 未决语音候选整理成一次性人工确认 CSV"
    )

    parser.add_argument("--bfe", required=True, help="BFE 原始 BIN 目录")
    parser.add_argument("--bfse", required=True, help="BFSE BIN 目录")
    parser.add_argument(
        "--candidates",
        required=True,
        help="build_voice_map.py 生成的完整候选.csv",
    )
    parser.add_argument("--out", required=True, help="最终人工确认 CSV 输出路径")

    return parser


def main() -> int:
    parser = build_argument_parser()
    args = parser.parse_args()
    return run(args)


if __name__ == "__main__":
    raise SystemExit(main())
