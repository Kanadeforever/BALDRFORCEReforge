# -*- coding: utf-8 -*-
"""
verify_regression.py

用途
====
这个脚本不是游戏运行时组件，而是开发阶段的“回归测试工具”。

为什么需要它
============
本项目现在已经有一批用户亲自人工确认过的结果：

1. 一组“确认通过”的语音映射；
2. 一组“确认不通过”的错误映射。

以后无论怎样调整自动匹配算法，都不能把这些已知事实破坏掉。
因此每次重新生成映射后，都应该自动检查：

- 人工不通过项不能重新被机器判成 A/B；
- 人工不通过项不能出现在最终 JSON；
- 人工通过项必须仍然出现在最终 JSON；
- 所有人工复核键都必须能在本轮候选里找到。

如果任意一条失败，本脚本返回非 0，BAT 也会立即报错。
"""

from __future__ import annotations

import argparse
import csv
import json
from pathlib import Path


def read_decision_keys(path: Path) -> set[tuple[str, str]]:
    """
    从人工复核 CSV 中读取 `(脚本名, VoiceTag)`。

    文件名统一转成小写、VoiceTag 统一转成大写，原因是 Windows 文件名通常
    不区分大小写，而 VoiceTag 本身也是资源键，不应该因为大小写格式不同导致
    回归检查误报。
    """

    result: set[tuple[str, str]] = set()

    with path.open("r", encoding="utf-8-sig", newline="") as handle:
        reader = csv.DictReader(handle)

        for row in reader:
            script = (row.get("脚本") or "").strip()
            voice_tag = (row.get("VoiceTag") or "").strip()

            # 人工 CSV 可以有空白分隔行，空行不是数据，直接忽略。
            if not script or not voice_tag:
                continue

            result.add((script.lower(), voice_tag.upper()))

    return result


def read_candidates(path: Path) -> dict[tuple[str, str], dict[str, str]]:
    """把完整候选 CSV 按 `(脚本, VoiceTag)` 建成快速查询表。"""

    result: dict[tuple[str, str], dict[str, str]] = {}

    with path.open("r", encoding="utf-8-sig", newline="") as handle:
        reader = csv.DictReader(handle)

        for row in reader:
            script = (row.get("脚本") or "").strip()
            voice_tag = (row.get("VoiceTag") or "").strip()

            if not script or not voice_tag:
                continue

            key = (script.lower(), voice_tag.upper())

            if key in result:
                raise RuntimeError(
                    "完整候选中出现重复的脚本+VoiceTag："
                    f"{script} / {voice_tag}"
                )

            result[key] = row

    return result


def read_final_mapping(path: Path) -> set[tuple[str, int, str]]:
    """
    把最终 JSON 展开成集合：

        (脚本名, BFE记录号, VoiceTag)

    这样后面可以非常直接地判断某条人工结论有没有真正进入最终映射。
    """

    data = json.loads(path.read_text(encoding="utf-8"))
    result: set[tuple[str, int, str]] = set()

    for script, record_map in data.items():
        for record_index_text, tags in record_map.items():
            record_index = int(record_index_text)

            for tag in tags:
                result.add(
                    (
                        script.lower(),
                        record_index,
                        str(tag).upper(),
                    )
                )

    return result


def candidate_final_key(row: dict[str, str]) -> tuple[str, int, str] | None:
    """把候选 CSV 的一行转换成最终 JSON 中使用的三元键。"""

    script = (row.get("脚本") or "").strip().lower()
    voice_tag = (row.get("VoiceTag") or "").strip().upper()
    bfe_index_text = (row.get("BFE记录号") or "").strip()

    if not script or not voice_tag or not bfe_index_text:
        return None

    return script, int(bfe_index_text), voice_tag


def run(args: argparse.Namespace) -> int:
    release_dir = Path(args.release).resolve()
    accept_path = Path(args.accept).resolve()
    reject_path = Path(args.reject).resolve()

    candidates_path = release_dir / "完整候选.csv"
    final_path = release_dir / "最终映射_主线推荐.json"

    required_paths = [
        accept_path,
        reject_path,
        candidates_path,
        final_path,
    ]

    for path in required_paths:
        if not path.is_file():
            print(f"[失败] 缺少回归测试所需文件：{path}")
            return 1

    accepted_keys = read_decision_keys(accept_path)
    rejected_keys = read_decision_keys(reject_path)
    candidates = read_candidates(candidates_path)
    final_mapping = read_final_mapping(final_path)

    errors: list[str] = []

    # ------------------------------------------------------------------
    # 1. 检查人工通过项
    # ------------------------------------------------------------------
    for key in sorted(accepted_keys):
        row = candidates.get(key)

        if row is None:
            errors.append(
                f"人工通过项在本轮候选中消失：{key[0]} / {key[1]}"
            )
            continue

        final_key = candidate_final_key(row)

        if final_key is None:
            errors.append(
                f"人工通过项没有 BFE 记录号：{key[0]} / {key[1]}"
            )
            continue

        if final_key not in final_mapping:
            errors.append(
                f"人工通过项没有进入最终主线映射：{key[0]} / {key[1]}"
            )

    # ------------------------------------------------------------------
    # 2. 检查人工不通过项
    # ------------------------------------------------------------------
    for key in sorted(rejected_keys):
        row = candidates.get(key)

        if row is None:
            errors.append(
                f"人工不通过项在本轮候选中消失：{key[0]} / {key[1]}"
            )
            continue

        grade = (row.get("等级") or "").strip().upper()

        # 这是这轮算法修改最重要的回归条件：
        # 已知错误样本不能再次被机器自动评成 A/B。
        if grade in {"A", "B"}:
            errors.append(
                "人工不通过项重新被机器判成自动通过等级："
                f"{key[0]} / {key[1]} / grade={grade}"
            )

        final_key = candidate_final_key(row)

        if final_key is not None and final_key in final_mapping:
            errors.append(
                f"人工不通过项错误进入最终映射：{key[0]} / {key[1]}"
            )

    if errors:
        print("[失败] 回归测试未通过：")

        for item in errors:
            print(f"  - {item}")

        print(f"[统计] 错误数量：{len(errors)}")
        return 1

    print("[成功] 回归测试全部通过。")
    print(f"[确认通过样本] {len(accepted_keys)} 条，全部保留在最终主线映射。")
    print(f"[确认不通过样本] {len(rejected_keys)} 条，全部未被机器判成 A/B。")
    print("[确认不通过样本] 0 条进入最终主线映射。")

    return 0


def build_argument_parser() -> argparse.ArgumentParser:
    """建立命令行参数。"""

    parser = argparse.ArgumentParser(
        description="验证 BaldrForceVoiceMap 的人工正/负样本没有发生回归"
    )

    parser.add_argument(
        "--release",
        required=True,
        help="build_voice_map.py 生成的 release 目录",
    )
    parser.add_argument(
        "--accept",
        required=True,
        help="人工确认通过 CSV",
    )
    parser.add_argument(
        "--reject",
        required=True,
        help="人工确认不通过 CSV",
    )

    return parser


def main() -> int:
    parser = build_argument_parser()
    args = parser.parse_args()
    return run(args)


if __name__ == "__main__":
    raise SystemExit(main())
