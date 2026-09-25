# -*- coding: utf-8 -*-
"""
build_voice_map.py

用途
====
这个工具用于比较《BALDR FORCE EXE》（BFE）与《BALDR FORCE Standard Edition》
（BFSE）的 NEXAS BIN 剧情脚本，并把 BFSE 后来增加的主角语音标签映射回
BFE 的“逻辑文本记录号”。

最重要的设计原则
================
1. 只读取 BIN，不修改任何游戏文件。
2. 模糊匹配只在离线生成映射时使用。
3. 正式 ASI 运行时不应该做模糊文本匹配。
4. 最终映射键使用：脚本文件名 + BFE 文本记录号。
5. 低置信度结果宁可进入人工复核，也不自动写入最终映射。

为什么这样做
============
BFE 的旧汉化脚本本身已经经过修改。如果我们再次重编译或直接改写 BIN，
很容易碰到 NEXAS 脚本跳转地址、长度、控制流等问题。

因此正确路线是：

    原始日文 BFE BIN + BFSE BIN
                ↓
        本工具离线建立映射
                ↓
    脚本名 + BFE记录号 -> VoiceTag
                ↓
        ASI 在运行时只改内存字符串
                ↓
    在原台词前临时加 @vXXXX

这样磁盘上的汉化 BIN 可以完全不碰。
"""

from __future__ import annotations

import argparse
import bisect
import csv
import json
import re
import struct
import sys
import unicodedata
from collections import Counter, defaultdict
from dataclasses import dataclass
from difflib import SequenceMatcher
from pathlib import Path
from typing import Iterable


# -----------------------------------------------------------------------------
# 已确认的 NEXAS 文本记录结构
# -----------------------------------------------------------------------------
#
# 当前 BFE / BFSE BIN 中，普通剧情文本记录可以识别为：
#
#     0B 00
#     XX XX XX XX      <- little-endian uint32，字符串长度（包含末尾 00）
#     [CP932 字符串]
#     00
#
# 这里把开头两个字节单独定义出来，扫描 BIN 时用它寻找候选记录。
TEXT_OPCODE = b"\x0B\x00"

# 防止随机二进制被误认为一个超大字符串。
# 正常剧情文本远小于 64 KiB，因此这个上限足够宽松。
MAX_STRING_SIZE = 65536

# @v 后面的部分就是语音资源键。
# 已确认例子包括：
#
#     @v100000
#     @v500000
#     @vTR0001
#
# 这里允许英文字母、数字和下划线，避免以后遇到其它同引擎命名时丢数据。
VOICE_TAG_RE = re.compile(r"@v([A-Za-z0-9_]+)")

# NEXAS 文本中的控制标签，例如：
#
#     @v500000
#     @f1A0101
#     @m4
#     @a1
#     @h00
#     @w1830
#
# 离线比较正文时要把这些控制标签去掉，否则仅仅因为 SE 多了 @v 就会导致
# 本来完全相同的两句台词被判定为不同。
CONTROL_TAG_RE = re.compile(r"@[A-Za-z][A-Za-z0-9_]*")

# 当前研究已确认 BFSE 的主角剧情语音主体集中在：
#
#     5xxxxx
#     6xxxxx
#     7xxxxx
#     TRxxxx
#
# 这个正则只是默认值。命令行可以用 --voice-pattern 覆盖，避免把研究结论
# 永久写死在工具里。
DEFAULT_PROTAGONIST_PATTERN = r"^(?:[567]\d{5}|TR\d+)$"


@dataclass
class TextRecord:
    """一条从 BIN 中成功识别出的剧情文本记录。"""

    # 逻辑记录号：这是当前脚本中第几条 0x000B 文本记录。
    # ASI 最终最需要的就是这个编号，而不是文件偏移。
    index: int

    # 这条记录在 BIN 文件中的字节偏移，仅用于研究与排错。
    offset: int

    # 原始 CP932 文本，里面仍然保留 @v、@f 等控制标签。
    raw_text: str

    # 去掉控制标签并做 Unicode 规范化后的正文，用于比较。
    clean_text: str

    # 这条文本里出现的所有 @v 语音键。
    voice_tags: list[str]


@dataclass
class Anchor:
    """BFE 与 BFSE 之间的一条高可信“完全一致且唯一”的顺序锚点。"""

    se_index: int
    bfe_index: int


@dataclass
class ContextEvidence:
    """
    一条候选映射周围的“前后文对齐证据”。

    这里故意把上下文证据单独保存，而不是只返回 True / False。
    原因是人工复核时最重要的不是“程序说它对”，而是能看见程序为什么认为它对。
    因此 CSV 会同时写出前侧分数、后侧分数、强匹配数量和具体对齐位置。
    """

    previous_score: float | None
    next_score: float | None
    previous_strong_matches: int
    next_strong_matches: int
    previous_available: int
    next_available: int
    previous_pairs: str
    next_pairs: str
    passed: bool
    reason: str


@dataclass
class CandidateResult:
    """一条 BFSE 主角语音在 BFE 中的最终候选映射结果。"""

    script: str
    voice_tag: str
    grade: str
    status: str
    reason: str
    similarity: float
    second_similarity: float
    bfse_index: int
    bfe_index: int | None
    bfse_offset: int
    bfe_offset: int | None
    expected_bfe_index: float | None
    anchor_before: str
    anchor_after: str
    bfse_raw: str
    bfse_clean: str
    bfe_raw: str
    bfe_clean: str
    bfse_prev: str
    bfse_next: str
    bfe_prev: str
    bfe_next: str
    voice3_verified: str

    # 以下字段记录“候选台词周围的剧情是否真的能够对上”。
    # 从 v0.1-dev2 开始，A/B 自动通过不再只看目标台词本身；v0.1-dev3 进一步允许连续的全年龄化改写。
    context_previous_score: float | None
    context_next_score: float | None
    context_previous_strong_matches: int
    context_next_strong_matches: int
    context_previous_pairs: str
    context_next_pairs: str
    context_passed: bool
    context_reason: str

    # 人工结论与机器等级分开保存。
    # 这样既能保留“机器原来认为 C”，又能记录“用户试听后确认通过”。
    manual_decision: str = ""
    manual_note: str = ""


# -----------------------------------------------------------------------------
# 文本基础处理
# -----------------------------------------------------------------------------

def normalize_text(text: str) -> str:
    """
    把游戏文本转换成“只用于离线比较”的规范正文。

    注意：
    这个函数绝不会写回 BIN。

    例如：

        @v500000@f1A0101「……ああ聞いているよ、月菜」

    会变成：

        「......ああ聞いているよ、月菜」

    这里采用 NFKC 的原因是：
    日文脚本可能存在全角/兼容字符差异。NFKC 可以消除一部分“视觉相同但编码
    不完全相同”的无意义差异，让相似度更符合人工直觉。
    """

    # 先删除 @v / @f / @w 等 NEXAS 控制标签。
    text = CONTROL_TAG_RE.sub("", text)

    # Unicode 兼容规范化。
    text = unicodedata.normalize("NFKC", text)

    # 空格、Tab、换行通常不决定一句剧情台词是否是同一句，因此比较时去掉。
    text = re.sub(r"\s+", "", text)

    return text


def text_similarity(left: str, right: str) -> float:
    """
    返回 0~100 的字符串相似度。

    使用 Python 标准库 difflib，因此这个工具不要求用户安装第三方包。
    对本项目来说，真正需要模糊比较的只是少量 SE 改写台词，速度足够。
    """

    if not left or not right:
        return 0.0

    return SequenceMatcher(
        None,
        left,
        right,
        autojunk=False,
    ).ratio() * 100.0


def meaningful_character_count(text: str) -> int:
    """
    统计一句话里真正有语义的字母/数字数量。

    为什么需要这个值：
    
        「............」
        「............っ」

    这类文本用普通字符串相似度计算会非常高，因为绝大部分字符都是相同的
    省略号。但对语音映射来说，它们恰恰是最容易误配的喘息/短句。

    Unicode 分类中，L* 表示字母（汉字、假名也属于这一类），N* 表示数字。
    标点、括号、省略号不会计入。
    """

    count = 0

    for char in text:
        category = unicodedata.category(char)
        if category.startswith("L") or category.startswith("N"):
            count += 1

    return count


# -----------------------------------------------------------------------------
# BIN 解析
# -----------------------------------------------------------------------------

def parse_bin(path: Path) -> list[TextRecord]:
    """
    读取一个 BIN，并按已确认的 0x000B 文本结构提取所有正常文本记录。

    为什么不能简单搜索所有 CP932 字符串：
    BIN 中还包含脚本控制数据、跳转、变量等二进制结构。只有遵循已经确认的
    0x000B + 长度结构，记录号才有稳定意义。
    """

    data = path.read_bytes()
    records: list[TextRecord] = []
    position = 0

    while True:
        # 从当前位置开始寻找下一处文本 opcode。
        hit = data.find(TEXT_OPCODE, position)
        if hit < 0:
            break

        # opcode 后必须至少还有 4 字节长度字段。
        if hit + 6 > len(data):
            break

        string_size = struct.unpack_from("<I", data, hit + 2)[0]

        # 长度异常时，这一处很可能只是普通二进制中碰巧出现 0B 00。
        if string_size <= 0 or string_size > MAX_STRING_SIZE:
            position = hit + 1
            continue

        string_start = hit + 6
        string_end = string_start + string_size

        # 长度不能越过文件末尾。
        if string_end > len(data):
            position = hit + 1
            continue

        raw_bytes = data[string_start:string_end]

        # 正常字符串长度包含末尾的 C 字符串结束符 00。
        if not raw_bytes.endswith(b"\x00"):
            position = hit + 1
            continue

        try:
            # 日文原始脚本使用 CP932 / Windows Shift-JIS 系编码。
            raw_text = raw_bytes[:-1].decode("cp932")
        except UnicodeDecodeError:
            # 解码失败时不要强行替换字符，否则可能把随机二进制误判成文本。
            position = hit + 1
            continue

        records.append(
            TextRecord(
                index=len(records),
                offset=hit,
                raw_text=raw_text,
                clean_text=normalize_text(raw_text),
                voice_tags=VOICE_TAG_RE.findall(raw_text),
            )
        )

        # 已经完整消费这条记录，可以直接跳到记录末尾继续找下一条。
        position = string_end

    return records


# -----------------------------------------------------------------------------
# 文件清单
# -----------------------------------------------------------------------------

def collect_bin_files(folder: Path) -> dict[str, Path]:
    """
    收集目录第一层的全部 BIN，并用小写文件名作为比较键。

    这是必要的，因为实际 BFE 数据里存在 `.BIN` 大写扩展名，而 BFSE 对应文件
    可能是 `.bin`。Windows 文件系统通常不区分大小写，我们的离线工具也应该
    按同样规则处理。
    """

    result: dict[str, Path] = {}

    for path in folder.iterdir():
        if not path.is_file():
            continue
        if path.suffix.lower() != ".bin":
            continue

        key = path.name.lower()

        if key in result:
            raise RuntimeError(
                f"目录中存在大小写不同但逻辑同名的 BIN：{result[key].name} / {path.name}"
            )

        result[key] = path

    return result


# -----------------------------------------------------------------------------
# 顺序锚点
# -----------------------------------------------------------------------------

def build_monotonic_anchors(
    bfe_records: list[TextRecord],
    bfse_records: list[TextRecord],
) -> tuple[list[Anchor], dict[str, list[int]], dict[str, list[int]]]:
    """
    建立“完全相同 + 两边都只出现一次”的文本锚点，并保证锚点顺序单调。

    为什么还要做“单调”筛选：
    即使一句文本在两边都唯一，SE 也理论上可能移动过演出顺序。最终 ASI 映射
    依赖剧情流程顺序，因此用于夹住局部搜索范围的锚点必须保持前后顺序一致。

    这里使用“最长递增子序列”（LIS）：
    - 先按 BFSE 记录号排序；
    - 再找 BFE 记录号最长的递增序列；
    - 得到不会相互交叉的一组可靠锚点。
    """

    bfe_positions: dict[str, list[int]] = defaultdict(list)
    bfse_positions: dict[str, list[int]] = defaultdict(list)

    for record in bfe_records:
        bfe_positions[record.clean_text].append(record.index)

    for record in bfse_records:
        bfse_positions[record.clean_text].append(record.index)

    # 只有正文非空，并且在两边都恰好出现一次，才有资格成为唯一锚点候选。
    pairs: list[tuple[int, int]] = []

    for text, se_list in bfse_positions.items():
        if not text:
            continue

        bfe_list = bfe_positions.get(text, [])

        if len(se_list) == 1 and len(bfe_list) == 1:
            pairs.append((se_list[0], bfe_list[0]))

    pairs.sort(key=lambda item: item[0])

    if not pairs:
        return [], dict(bfe_positions), dict(bfse_positions)

    # tails[k] 表示：长度为 k+1 的递增序列，目前能得到的最小末尾 BFE 索引。
    tails: list[int] = []

    # tail_pair_index[k] 保存上面 tails[k] 对应的是 pairs 中哪一项。
    tail_pair_index: list[int] = []

    # predecessor[i] 用于最终从尾部反向还原整条 LIS。
    predecessor = [-1] * len(pairs)

    for i, (_, bfe_index) in enumerate(pairs):
        position = bisect.bisect_left(tails, bfe_index)

        if position == len(tails):
            tails.append(bfe_index)
            tail_pair_index.append(i)
        else:
            tails[position] = bfe_index
            tail_pair_index[position] = i

        if position > 0:
            predecessor[i] = tail_pair_index[position - 1]

    # 从最后一个节点向前回溯，得到完整单调锚点序列。
    current = tail_pair_index[-1]
    reversed_result: list[Anchor] = []

    while current != -1:
        se_index, bfe_index = pairs[current]
        reversed_result.append(Anchor(se_index=se_index, bfe_index=bfe_index))
        current = predecessor[current]

    anchors = list(reversed(reversed_result))

    return anchors, dict(bfe_positions), dict(bfse_positions)


def find_anchor_bounds(
    se_index: int,
    anchors: list[Anchor],
    bfe_count: int,
) -> tuple[int, int, Anchor | None, Anchor | None, float | None]:
    """
    找到 BFSE 某条记录前后的最近锚点，并推算它在 BFE 中大概应处于哪里。

    这个“预计位置”只用于解决重复短句和排序候选，绝不会直接作为自动映射。
    """

    if bfe_count == 0:
        return 0, -1, None, None, None

    se_anchor_indices = [anchor.se_index for anchor in anchors]
    insert_at = bisect.bisect_left(se_anchor_indices, se_index)

    before = anchors[insert_at - 1] if insert_at > 0 else None
    after = anchors[insert_at] if insert_at < len(anchors) else None

    low = before.bfe_index + 1 if before else 0
    high = after.bfe_index - 1 if after else bfe_count - 1

    # 极少数异常情况下前后锚点夹出的区间可能为空。
    # 这时宁可退回全脚本搜索，不把错误范围强行套给候选。
    if high < low:
        low = 0
        high = bfe_count - 1

    expected: float | None = None

    if before and after and after.se_index != before.se_index:
        # 两边都有锚点时做线性插值。
        fraction = (se_index - before.se_index) / (after.se_index - before.se_index)
        expected = before.bfe_index + fraction * (after.bfe_index - before.bfe_index)
    elif before:
        # 只有前锚点时，暂时假设后续记录数量大体平行。
        expected = before.bfe_index + (se_index - before.se_index)
    elif after:
        # 只有后锚点时同理向前推。
        expected = after.bfe_index - (after.se_index - se_index)

    return low, high, before, after, expected


# -----------------------------------------------------------------------------
# 辅助函数
# -----------------------------------------------------------------------------

def record_text(records: list[TextRecord], index: int) -> str:
    """安全读取某一条正文；越界时返回空字符串。"""

    if 0 <= index < len(records):
        return records[index].clean_text
    return ""


def record_raw(records: list[TextRecord], index: int) -> str:
    """安全读取某一条原始文本；越界时返回空字符串。"""

    if 0 <= index < len(records):
        return records[index].raw_text
    return ""


def anchor_label(anchor: Anchor | None) -> str:
    """把锚点转换成适合写入 CSV 的短文本。"""

    if anchor is None:
        return ""
    return f"BFSE#{anchor.se_index}->BFE#{anchor.bfe_index}"


def load_voice3_keys(path: Path | None) -> set[str] | None:
    """
    可选读取 Voice3 文件清单。

    用户以后可以把 GARbro 解包后的文件名整理成一个 txt/csv 清单，例如：

        TR0001.wav
        500000.wav
        500002.wav

    工具会取文件名主体（stem），用于验证一个 @v key 是否真的存在于 Voice3。
    当前没有清单时返回 None，结果表中会写“未提供清单”。
    """

    if path is None:
        return None

    keys: set[str] = set()

    with path.open("r", encoding="utf-8-sig", errors="replace") as handle:
        for line in handle:
            line = line.strip()
            if not line:
                continue

            # 兼容一行里只有文件名，也兼容 CSV 第一列是文件名的简单情况。
            first_cell = line.split(",", 1)[0].strip().strip('"')
            keys.add(Path(first_cell).stem.upper())

    return keys


# -----------------------------------------------------------------------------
# 人工复核结论导入
# -----------------------------------------------------------------------------

def load_manual_decision_file(
    path: Path | None,
    default_decision: str,
) -> dict[tuple[str, str], tuple[str, str]]:
    """
    读取人工复核 CSV，并把它转换成：

        (小写脚本名, 大写VoiceTag) -> (人工结论, 备注)

    这个函数故意只依赖两列：`脚本` 与 `VoiceTag`。
    其它列可以很多、也可以来自旧版工具，因为真正定位一条复核记录只需要这两个键。

    `default_decision` 的意义：
    用户可能直接上传“这里只包含不通过项”的 CSV，而不是每一行都填好“人工结论”。
    这种情况下，只要该行有脚本和 VoiceTag，就按调用者指定的默认结论处理。
    """

    if path is None:
        return {}

    if not path.is_file():
        raise FileNotFoundError(f"人工复核文件不存在：{path}")

    result: dict[tuple[str, str], tuple[str, str]] = {}

    with path.open("r", encoding="utf-8-sig", newline="") as handle:
        reader = csv.DictReader(handle)

        if reader.fieldnames is None:
            raise RuntimeError(f"人工复核 CSV 没有表头：{path}")

        required = {"脚本", "VoiceTag"}
        missing = required - set(reader.fieldnames)

        if missing:
            raise RuntimeError(
                f"人工复核 CSV 缺少必要列 {sorted(missing)}：{path}"
            )

        for row in reader:
            script = (row.get("脚本") or "").strip()
            voice_tag = (row.get("VoiceTag") or "").strip()

            # 用户表格里允许存在空白分隔行；空行直接忽略。
            if not script or not voice_tag:
                continue

            written_decision = (row.get("人工结论") or "").strip()
            note = (row.get("人工备注") or "").strip()

            decision = written_decision or default_decision
            key = (script.lower(), voice_tag.upper())

            if key in result:
                raise RuntimeError(
                    "人工复核 CSV 出现重复键："
                    f"{script} / {voice_tag}"
                )

            result[key] = (decision, note)

    return result


def apply_manual_decisions(
    candidates: list[CandidateResult],
    accepted: dict[tuple[str, str], tuple[str, str]],
    rejected: dict[tuple[str, str], tuple[str, str]],
) -> tuple[int, int]:
    """
    把人工通过/不通过结论附加到候选对象上。

    注意：这里**不覆盖机器等级**。

    例如某条机器仍然判为 C，但用户已经试听确认：

        grade = C
        manual_decision = 人工确认通过

    这样以后回看报告时，既知道机器为什么不敢自动通过，也知道人类最终作了什么决定。
    正式 JSON 是否收录，则由 `candidate_is_final()` 统一判断。
    """

    overlap = set(accepted) & set(rejected)

    if overlap:
        sample = sorted(overlap)[:5]
        raise RuntimeError(
            "同一条语音同时出现在人工通过与人工不通过文件中："
            f"{sample}"
        )

    by_key = {
        (candidate.script.lower(), candidate.voice_tag.upper()): candidate
        for candidate in candidates
    }

    applied_accept = 0
    applied_reject = 0

    for key, (decision, note) in accepted.items():
        candidate = by_key.get(key)

        if candidate is None:
            raise RuntimeError(
                "人工通过记录在本次候选中不存在："
                f"{key[0]} / {key[1]}"
            )

        candidate.manual_decision = decision or "人工确认通过"
        candidate.manual_note = note
        applied_accept += 1

    for key, (decision, note) in rejected.items():
        candidate = by_key.get(key)

        if candidate is None:
            raise RuntimeError(
                "人工不通过记录在本次候选中不存在："
                f"{key[0]} / {key[1]}"
            )

        candidate.manual_decision = decision or "人工确认不通过"
        candidate.manual_note = note
        applied_reject += 1

    return applied_accept, applied_reject


def candidate_is_manual_accept(candidate: CandidateResult) -> bool:
    """判断一条候选是否已经被人工明确确认通过。"""

    return candidate.manual_decision in {
        "通过",
        "人工确认通过",
        "接受",
    }


def candidate_is_manual_reject(candidate: CandidateResult) -> bool:
    """判断一条候选是否已经被人工明确确认不通过。"""

    return candidate.manual_decision in {
        "不通过",
        "人工确认不通过",
        "舍弃",
    }


def candidate_is_final(candidate: CandidateResult) -> bool:
    """
    判断一条候选能否进入正式 ASI 映射。

    优先级非常明确：

    1. 人工明确“不通过” -> 永远排除；
    2. 人工明确“通过”   -> 收录；
    3. 没有人工结论       -> 只有机器 A/B 才收录。
    """

    if candidate_is_manual_reject(candidate):
        return False

    if candidate_is_manual_accept(candidate):
        return candidate.bfe_index is not None

    return (
        candidate.grade in {"A", "B"}
        and candidate.bfe_index is not None
    )


# -----------------------------------------------------------------------------
# 前后文序列对齐
# -----------------------------------------------------------------------------

def context_record_weight(text: str) -> float:
    """
    计算一条“上下文文本”在对齐时应该有多大权重。

    初学者可以把它理解成：

        一大段真正的剧情句子，比单独一个“……”更适合拿来认路。

    `meaningful_character_count()` 会忽略标点，只统计真正的文字和数字。
    这里把权重限制在 0.25 ~ 1.0：

    - 很短的喘息/省略号不会完全消失，但影响很小；
    - 8 个以上有效字符的正常句子按完整权重计算。
    """

    meaningful = meaningful_character_count(text)
    return max(0.25, min(1.0, meaningful / 8.0))


def align_context_side(
    bfse_records: list[TextRecord],
    bfe_records: list[TextRecord],
    bfse_index: int,
    bfe_index: int,
    side: str,
    bfse_window: int = 5,
    bfe_window: int = 8,
) -> tuple[float | None, int, int, str]:
    """
    对齐目标台词某一侧的剧情上下文。

    为什么不能只比较“紧挨着的前一句 / 后一句”：
    ------------------------------------------------
    BFE 和 BFSE 之间确实存在删减、插入和分级改写。例如 BFSE 可能删掉一条
    成人内容，而 BFE 仍然保留它。这样真正对应的下一句就可能从 `+1` 变成
    `+2` 或 `+3`。

    所以这里不是死板地做：

        BFSE 前1  <-> BFE 前1
        BFSE 前2  <-> BFE 前2

    而是在一个很小的局部窗口里做“保持顺序的动态规划对齐”。

    可以把它想象成两排卡片：

        BFSE: A  B     C  D
        BFE : A  B  X  C  D

    中间多出来的 X 可以跳过，但 A/B/C/D 的先后顺序不能乱。

    返回值：
    --------
    1. 上下文覆盖分数，0~100；没有这一侧上下文时返回 None。
    2. “强匹配”数量。强匹配要求相似度 >= 90%，而且必须包含足够有效文字。
    3. BFSE 这一侧实际拿了多少条上下文记录。
    4. 具体对齐对，写入 CSV 供人工追溯。
    """

    if side not in {"previous", "next"}:
        raise ValueError(f"未知上下文方向：{side}")

    # 根据方向截取目标台词附近的小窗口。
    # BFSE 取 5 条，BFE 取 8 条，是为了允许 BFE 多出少量删减前内容。
    if side == "previous":
        se_start = max(0, bfse_index - bfse_window)
        bfe_start = max(0, bfe_index - bfe_window)

        se_context = bfse_records[se_start:bfse_index]
        bfe_context = bfe_records[bfe_start:bfe_index]

        se_base = se_start
        bfe_base = bfe_start
    else:
        se_start = bfse_index + 1
        bfe_start = bfe_index + 1

        se_context = bfse_records[
            se_start:min(len(bfse_records), se_start + bfse_window)
        ]
        bfe_context = bfe_records[
            bfe_start:min(len(bfe_records), bfe_start + bfe_window)
        ]

        se_base = se_start
        bfe_base = bfe_start

    # 如果目标已经在脚本最开头/最末尾，某一侧可能天然没有上下文。
    # 这种情况不是失败，因此用 None 表示“这一侧不可用”。
    if not se_context:
        return None, 0, 0, ""

    # BFSE 有上下文而 BFE 完全没有，说明候选位置很可疑。
    if not bfe_context:
        return 0.0, 0, len(se_context), ""

    se_count = len(se_context)
    bfe_count = len(bfe_context)

    # 先把所有“BFSE局部句子 vs BFE局部句子”的相似度算好。
    # 后面的动态规划只查这个表，不重复做昂贵的字符串比较。
    similarities: list[list[float]] = []

    for se_record in se_context:
        row: list[float] = []

        for bfe_record in bfe_context:
            row.append(
                text_similarity(
                    se_record.clean_text,
                    bfe_record.clean_text,
                )
            )

        similarities.append(row)

    # dp[i][j]：
    #   使用 BFSE 前 i 条、BFE 前 j 条上下文时，能够获得的最佳对齐收益。
    #
    # action[i][j]：
    #   为了得到这个最佳结果，最后一步做了什么。
    #
    # 这样最后可以倒着走，把真正采用了哪些句子对重新找出来。
    dp = [
        [0.0] * (bfe_count + 1)
        for _ in range(se_count + 1)
    ]
    action: list[list[str | None]] = [
        [None] * (bfe_count + 1)
        for _ in range(se_count + 1)
    ]

    for se_pos in range(1, se_count + 1):
        for bfe_pos in range(1, bfe_count + 1):
            similarity = similarities[se_pos - 1][bfe_pos - 1]

            # 小于 40% 的文本基本不能提供定位价值，所以不产生正收益。
            # 但 40~100% 之间仍保留连续变化，避免把 89% / 90% 硬切成两个世界。
            weight = context_record_weight(
                se_context[se_pos - 1].clean_text
            )
            match_benefit = max(0.0, similarity - 40.0) * weight

            choices = [
                # BFE 多了一句：跳过 BFE 当前句。
                (dp[se_pos][bfe_pos - 1], "skip_bfe"),

                # BFSE 多了一句：跳过 BFSE 当前句。
                (dp[se_pos - 1][bfe_pos], "skip_bfse"),

                # 两句按顺序配在一起。
                (
                    dp[se_pos - 1][bfe_pos - 1] + match_benefit,
                    "match",
                ),
            ]

            best_value, best_action = max(
                choices,
                key=lambda item: item[0],
            )

            dp[se_pos][bfe_pos] = best_value
            action[se_pos][bfe_pos] = best_action

    # 从右下角反向还原真正采用的匹配对。
    se_pos = se_count
    bfe_pos = bfe_count
    matched_pairs: list[tuple[int, int, float]] = []

    while se_pos > 0 and bfe_pos > 0:
        current_action = action[se_pos][bfe_pos]

        if current_action == "match":
            similarity = similarities[se_pos - 1][bfe_pos - 1]

            matched_pairs.append(
                (
                    se_base + se_pos - 1,
                    bfe_base + bfe_pos - 1,
                    similarity,
                )
            )

            se_pos -= 1
            bfe_pos -= 1
        elif current_action == "skip_bfe":
            bfe_pos -= 1
        else:
            se_pos -= 1

    matched_pairs.reverse()

    # “覆盖分数”故意除以 BFSE 上下文总条数，而不是只除以已经匹配上的条数。
    # 这意味着：如果只有 1 条碰巧对上、其余 4 条完全不对，分数不会虚高。
    score = (
        sum(pair[2] for pair in matched_pairs)
        / se_count
    )

    strong_matches = 0
    pair_labels: list[str] = []

    for se_absolute, bfe_absolute, similarity in matched_pairs:
        se_text = bfse_records[se_absolute].clean_text
        bfe_text = bfe_records[bfe_absolute].clean_text

        # 只有真正包含文字/数字的正常句子，才允许成为“强上下文锚点”。
        # 纯「……」即使 100% 相同，也不应该帮一个候选自动过审。
        if (
            similarity >= 90.0
            and meaningful_character_count(se_text) >= 4
            and meaningful_character_count(bfe_text) >= 4
        ):
            strong_matches += 1

        pair_labels.append(
            f"BFSE#{se_absolute}<->BFE#{bfe_absolute}:{similarity:.1f}%"
        )

    return (
        score,
        strong_matches,
        se_count,
        " | ".join(pair_labels),
    )


def evaluate_context(
    bfse_records: list[TextRecord],
    bfe_records: list[TextRecord],
    bfse_index: int,
    bfe_index: int,
) -> ContextEvidence:
    """
    综合前后两侧的局部序列，判断这个候选位置是否真的“处于同一段剧情”。

    v0.1-dev1 的教训：
    -----------------
    只看目标台词会把以下情况误认为同一句：

        「............」

    甚至一条很长的句子，也可能在完全不同的剧情段落中出现近似文本。
    用户人工复核已经明确要求：**前后文必须参与定位**。

    当前自动通过标准故意偏保守：

    - 正常情况下前、后两侧都要有足够的上下文覆盖；
    - 每一侧至少要找到一个真正有文字内容的 >=90% 强匹配；
    - 两侧合计至少 3 个强匹配；
    - 如果目标正好位于脚本边缘，只剩一侧可用，则该侧要求更高。

    没过这个门槛不代表一定错误，只代表不能由机器自动写进正式映射。
    """

    (
        previous_score,
        previous_strong,
        previous_available,
        previous_pairs,
    ) = align_context_side(
        bfse_records,
        bfe_records,
        bfse_index,
        bfe_index,
        "previous",
    )

    (
        next_score,
        next_strong,
        next_available,
        next_pairs,
    ) = align_context_side(
        bfse_records,
        bfe_records,
        bfse_index,
        bfe_index,
        "next",
    )

    has_previous = previous_available > 0
    has_next = next_available > 0

    passed = False
    reason = ""

    if has_previous and has_next:
        # 普通情况：目标前后都有剧情，因此两侧都必须能自洽。
        passed = (
            previous_score is not None
            and next_score is not None
            and previous_score >= 65.0
            and next_score >= 65.0
            and previous_strong >= 1
            and next_strong >= 1
            and previous_strong + next_strong >= 3
        )

        if passed:
            reason = (
                "前后两侧局部剧情均通过序列对齐："
                f"前侧={previous_score:.1f}%/{previous_strong}个强锚点，"
                f"后侧={next_score:.1f}%/{next_strong}个强锚点"
            )
        else:
            reason = (
                "前后文不足以共同定位："
                f"前侧={previous_score or 0.0:.1f}%/{previous_strong}个强锚点，"
                f"后侧={next_score or 0.0:.1f}%/{next_strong}个强锚点"
            )

    elif has_previous or has_next:
        # 脚本开头或结尾可能只有一侧上下文。
        # 因为缺少另一侧互相验证，所以剩下这一侧必须更强。
        side_score = previous_score if has_previous else next_score
        side_strong = previous_strong if has_previous else next_strong
        side_available = previous_available if has_previous else next_available

        required_strong = 1 if side_available <= 1 else 2

        passed = (
            side_score is not None
            and side_score >= 80.0
            and side_strong >= required_strong
        )

        side_name = "前侧" if has_previous else "后侧"

        if passed:
            reason = (
                f"目标位于脚本边缘，仅有{side_name}上下文；"
                f"该侧={side_score:.1f}%/{side_strong}个强锚点，达到边缘规则"
            )
        else:
            reason = (
                f"目标位于脚本边缘且只有{side_name}上下文；"
                f"该侧={side_score or 0.0:.1f}%/{side_strong}个强锚点，不足以自动定位"
            )

    else:
        reason = "目标前后都没有可用于定位的文本上下文"

    return ContextEvidence(
        previous_score=previous_score,
        next_score=next_score,
        previous_strong_matches=previous_strong,
        next_strong_matches=next_strong,
        previous_available=previous_available,
        next_available=next_available,
        previous_pairs=previous_pairs,
        next_pairs=next_pairs,
        passed=passed,
        reason=reason,
    )


def evaluate_storyline_continuity(
    candidate_reason: str,
    similarity: float,
    expected_bfe_index: float | None,
    chosen_bfe_index: int | None,
    anchor_before: Anchor | None,
    anchor_after: Anchor | None,
    context: ContextEvidence,
) -> tuple[bool, str]:
    """
    判断一条原本因为“前后文文字不够像”而被降级的候选，是否其实只是
    BFE 与 BFSE 的全年龄化/分级改写。

    为什么还需要这一层
    ====================
    v0.1-dev2 把前后文条件收得很紧：前后两侧都要有较高文字相似度。
    这样确实能挡住“碰巧有一句一样、但其实处在完全不同剧情”的错误映射，
    但也会把真正的全年龄改写一起挡掉。

    用户人工复核后明确了更准确的原则：

        - 允许前后文有轻度甚至中度措辞改写；
        - 真正不能接受的是前后剧情已经完全不是同一个位置。

    因此这里不再只问“每一句有多像”，而是同时看四件事：

        1. 目标前后是否都有稳定的唯一剧情锚点；
        2. 这些锚点推算出来的 BFE 位置，是否与当前候选位置接近；
        3. 前后文序列是否仍能连续对齐；
        4. 目标台词本身是完全相同、轻微改写，还是重度改写。

    这是一层“补充自动通过规则”，不是放宽到随便猜。
    已经人工确认不通过的样本仍然由人工结论最高优先级排除；
    回归测试也会确保它们不能重新进入最终映射。
    """

    # 没有候选位置或没有预计位置时，无法判断“位置是否吻合”。
    if chosen_bfe_index is None or expected_bfe_index is None:
        return False, "缺少候选位置或预计位置，不能用剧情连续性自动放行"

    # 必须同时存在前、后两个剧情锚点。
    # 只有单侧锚点时，某个重复短句仍可能被错误吸附到别处。
    if anchor_before is None or anchor_after is None:
        return False, "缺少双侧剧情锚点，不能用剧情连续性自动放行"

    # 当前候选位置与两侧锚点线性推算出的预计位置最多允许相差 2 条记录。
    # 这给少量删句/插句留出空间，同时阻止跨很远剧情段落的错误匹配。
    position_distance = abs(chosen_bfe_index - expected_bfe_index)

    if position_distance > 2.0:
        return False, (
            "候选位置与剧情锚点推算位置相差过大："
            f"{position_distance:.2f} 条记录"
        )

    previous_score = context.previous_score or 0.0
    next_score = context.next_score or 0.0
    strong_total = (
        context.previous_strong_matches
        + context.next_strong_matches
    )

    # --------------------------------------------------------------
    # 规则 A：目标正文完全一致，而且在同脚本中是唯一匹配。
    # --------------------------------------------------------------
    # 这类候选已经非常强；这里仍然要求上下文不能“整体崩掉”。
    # 如果两侧至少都有 40% 的连续覆盖，或者一侧已经 >=85%，
    # 再加上双侧锚点与位置吻合，就可以认为是同一剧情位置。
    exact_unique = candidate_reason.startswith(
        "同脚本内 BFE/BFSE 正文唯一且完全一致"
    )

    if exact_unique:
        if (
            (previous_score >= 40.0 and next_score >= 40.0)
            or max(previous_score, next_score) >= 85.0
        ):
            return True, (
                "正文唯一完全一致，且双侧剧情锚点推算位置吻合；"
                f"前文={previous_score:.1f}%、后文={next_score:.1f}%、"
                f"位置差={position_distance:.2f}"
            )

    # --------------------------------------------------------------
    # 规则 B：重复正文，但已经被前后唯一锚点夹定。
    # --------------------------------------------------------------
    # 重复短句天生比唯一长句危险，所以额外要求两侧上下文都至少 50%，
    # 并且前后合计要有至少 3 个真正包含文字的强锚点。
    repeated_but_anchored = candidate_reason.startswith(
        "重复正文，但被前后两个唯一锚点夹定"
    )

    if repeated_but_anchored:
        if (
            previous_score >= 50.0
            and next_score >= 50.0
            and strong_total >= 3
        ):
            return True, (
                "重复正文已被双侧锚点夹定，且局部上下文仍连续；"
                f"前文={previous_score:.1f}%、后文={next_score:.1f}%、"
                f"强锚点={strong_total}"
            )

    # --------------------------------------------------------------
    # 规则 C：目标台词只有轻微措辞差异。
    # --------------------------------------------------------------
    # 90% 以上通常就是标点、注音、措辞轻改。
    # 只要两侧上下文都至少 50%，并有足够强锚点，就按全年龄改写通过。
    if (
        similarity >= 90.0
        and previous_score >= 50.0
        and next_score >= 50.0
        and strong_total >= 3
    ):
        return True, (
            "目标台词高度相似，双侧剧情锚点与局部上下文共同确认是同一位置；"
            f"相似度={similarity:.1f}%、前文={previous_score:.1f}%、"
            f"后文={next_score:.1f}%、强锚点={strong_total}"
        )

    # --------------------------------------------------------------
    # 规则 D：目标台词可能被全年龄化大幅改写，但剧情序列非常稳定。
    # --------------------------------------------------------------
    # 即使正文只有 40~80% 相似，只要前后两侧都维持 >=70% 的连续覆盖，
    # 合计至少 5 个强锚点，并且位置与双侧锚点推算一致，就说明“剧情位置”
    # 本身几乎没有歧义。典型例子就是成人版与全年龄版对同一段对白的改写。
    if (
        similarity >= 40.0
        and previous_score >= 70.0
        and next_score >= 70.0
        and strong_total >= 5
    ):
        return True, (
            "目标台词改写较大，但前后剧情序列高度连续，符合全年龄化改写特征；"
            f"相似度={similarity:.1f}%、前文={previous_score:.1f}%、"
            f"后文={next_score:.1f}%、强锚点={strong_total}、"
            f"位置差={position_distance:.2f}"
        )

    return False, (
        "剧情连续性仍不足以自动确认："
        f"相似度={similarity:.1f}%、前文={previous_score:.1f}%、"
        f"后文={next_score:.1f}%、强锚点={strong_total}、"
        f"位置差={position_distance:.2f}"
    )


# -----------------------------------------------------------------------------
# 单条语音映射
# -----------------------------------------------------------------------------

def classify_voice_event(
    script_name: str,
    voice_tag: str,
    se_record: TextRecord,
    bfe_records: list[TextRecord],
    bfse_records: list[TextRecord],
    anchors: list[Anchor],
    bfe_positions: dict[str, list[int]],
    bfse_positions: dict[str, list[int]],
    voice3_keys: set[str] | None,
) -> CandidateResult:
    """
    为一条 BFSE 主角语音寻找 BFE 对应台词并分级。

    等级定义：

    A = 确定
        同脚本内唯一完全一致，或重复句被两侧可靠锚点唯一夹定。

    B = 高可信
        正文存在轻微改写，但相似度 >= 90%，并且顺序/候选唯一性足够可靠。

    C = 人工复核
        有明显候选，但属于重复短句、分级改写、较低相似度等情况。

    D = 舍弃候选
        找不到可靠对应，默认不进入最终映射。

    没有人工结论时，最终 JSON 只自动写 A/B；人工确认结果会在主流程末尾叠加。
    """

    se_index = se_record.index
    clean = se_record.clean_text

    low, high, before, after, expected = find_anchor_bounds(
        se_index,
        anchors,
        len(bfe_records),
    )

    # 用于最终写表的变量先给安全默认值。
    chosen_index: int | None = None
    similarity = 0.0
    second_similarity = 0.0
    grade = "D"
    status = "舍弃候选"
    reason = "没有找到可靠的 BFE 对应台词"

    bfe_exact_positions = bfe_positions.get(clean, [])
    bfse_same_positions = bfse_positions.get(clean, [])

    # ------------------------------------------------------------------
    # 情况 1：同脚本内，两边正文都只出现一次，而且完全一致。
    # ------------------------------------------------------------------
    # 这是最可靠的自动映射，不要求它一定进入单调锚点 LIS。
    # 即使 SE 调整了这一小段顺序，唯一完全一致仍然足够说明它是哪一句。
    if clean and len(bfe_exact_positions) == 1 and len(bfse_same_positions) == 1:
        chosen_index = bfe_exact_positions[0]
        similarity = 100.0
        grade = "A"
        status = "自动接受"
        reason = "同脚本内 BFE/BFSE 正文唯一且完全一致"

    # ------------------------------------------------------------------
    # 情况 2：正文完全一致，但至少一边存在重复句。
    # ------------------------------------------------------------------
    elif clean and bfe_exact_positions:
        # 先只看前后单调锚点夹出的 BFE 区间。
        inside = [index for index in bfe_exact_positions if low <= index <= high]

        # 如果区间内恰好只剩一个完全一致候选，并且前后两边都有锚点，
        # 就说明重复句已经被剧情顺序唯一定位。
        if (
            len(inside) == 1
            and before is not None
            and after is not None
        ):
            chosen_index = inside[0]
            similarity = 100.0
            grade = "A"
            status = "自动接受"
            reason = "重复正文，但被前后两个唯一锚点夹定为单一 BFE 位置；不是仅凭短句正文判断"

        else:
            # 无法唯一夹定时，只选择“最值得人工查看”的一个候选，绝不自动接受。
            pool = inside if inside else bfe_exact_positions

            if expected is not None:
                chosen_index = min(pool, key=lambda index: abs(index - expected))
            else:
                chosen_index = pool[0]

            similarity = 100.0
            grade = "C"
            status = "人工复核"
            reason = "正文完全一致但存在多个可能位置，不能仅凭短句/重复句自动决定"

    # ------------------------------------------------------------------
    # 情况 3：没有完全一致正文，需要做模糊匹配。
    # ------------------------------------------------------------------
    else:
        # 搜索范围优先限制在前后锚点之间。
        # 左右各放宽 3 条，避免锚点附近的一点结构调整把真正候选排除。
        search_low = max(0, low - 3)
        search_high = min(len(bfe_records) - 1, high + 3)

        local_indices = list(range(search_low, search_high + 1))

        if not local_indices:
            local_indices = list(range(len(bfe_records)))

        scored: list[tuple[float, int]] = []

        for bfe_index in local_indices:
            score = text_similarity(clean, bfe_records[bfe_index].clean_text)
            scored.append((score, bfe_index))

        scored.sort(reverse=True)

        if scored:
            similarity, chosen_index = scored[0]
            second_similarity = scored[1][0] if len(scored) > 1 else 0.0

        text_length = len(clean)
        semantic_length = meaningful_character_count(clean)
        has_both_anchors = before is not None and after is not None

        # 高于 90% 是当前项目已经验证非常有效的“轻微改写”阈值。
        # 但极短句即使 90% 也可能撞到别处，所以还要求正文至少 6 个字符。
        if (
            chosen_index is not None
            and similarity >= 90.0
            and text_length >= 6
            # 至少包含 4 个真正的文字/数字，避免省略号和喘息声造成虚高相似度。
            and semantic_length >= 4
            and (
                # 最佳候选明显领先第二候选，说明不是多个近似句并列。
                similarity - second_similarity >= 2.0
                # 或者它处于前后两个可靠剧情锚点之间。
                or has_both_anchors
            )
        ):
            grade = "B"
            status = "自动接受"
            reason = "BFSE 台词存在轻微改写，但同脚本局部最佳相似度 >= 90% 且顺序可信"

        elif chosen_index is not None and similarity >= 90.0:
            grade = "C"
            status = "人工复核"
            reason = "相似度 >= 90%，但文本过短或候选不够唯一，需要人工确认"

        elif chosen_index is not None and similarity >= 70.0 and has_both_anchors:
            # 这类通常就是分级改写，例如一句里替换了敏感词。
            grade = "C"
            status = "人工复核"
            reason = "相似度低于 90%，但被前后剧情锚点夹住，疑似 BFSE 分级改写"

        else:
            # 为了让“舍弃候选.csv”仍有人工参考价值，再做一次全脚本搜索。
            # 注意：全局找到更像的句子也不会自动升级为 B，因为它已经违反局部剧情顺序。
            global_scored: list[tuple[float, int]] = []

            for bfe_index, bfe_record in enumerate(bfe_records):
                score = text_similarity(clean, bfe_record.clean_text)
                global_scored.append((score, bfe_index))

            global_scored.sort(reverse=True)

            if global_scored and global_scored[0][0] > similarity:
                similarity, chosen_index = global_scored[0]
                second_similarity = global_scored[1][0] if len(global_scored) > 1 else 0.0
                reason = "局部顺序范围没有可靠对应；表中仅附全脚本最佳候选供人工参考"

                # 如果全局候选非常像，至少送进人工复核，而不是直接丢弃。
                if similarity >= 90.0:
                    grade = "C"
                    status = "人工复核"
                    reason = "局部顺序不吻合，但全脚本存在 >=90% 的高相似候选，需要人工判断是否发生大范围移动"

    # ------------------------------------------------------------------
    # 前后文定位验证
    # ------------------------------------------------------------------
    #
    # 这是 v0.1-dev2 最重要的变化。
    #
    # 即使目标正文 100% 一样，如果周围剧情完全对不上，也不能认为它是同一句。
    # 因此只要已经找到了 BFE 候选位置，就必须再做一次上下文序列对齐。
    if chosen_index is not None:
        context = evaluate_context(
            bfse_records,
            bfe_records,
            se_index,
            chosen_index,
        )
    else:
        context = ContextEvidence(
            previous_score=None,
            next_score=None,
            previous_strong_matches=0,
            next_strong_matches=0,
            previous_available=0,
            next_available=0,
            previous_pairs="",
            next_pairs="",
            passed=False,
            reason="没有 BFE 候选位置，无法验证前后文",
        )

    # A/B 代表“机器允许直接进入正式映射”。
    # v0.1-dev2 只允许通过严格的文字上下文门槛；v0.1-dev3 在这里增加
    # “剧情连续性”补充判断，允许全年龄版对同一剧情位置做连续措辞改写。
    storyline_passed = False
    storyline_reason = ""

    if not context.passed and chosen_index is not None:
        storyline_passed, storyline_reason = evaluate_storyline_continuity(
            candidate_reason=reason,
            similarity=similarity,
            expected_bfe_index=expected,
            chosen_bfe_index=chosen_index,
            anchor_before=before,
            anchor_after=after,
            context=context,
        )

    if grade in {"A", "B"} and not context.passed and not storyline_passed:
        grade = "C"
        status = "人工复核"
        reason += "；目标正文虽可匹配，但前后剧情不能共同稳定定位，已禁止自动通过"

    elif grade in {"A", "B"} and storyline_passed:
        reason += f"；{storyline_reason}"

    # 原本的 C/D 也可能只是因为 BFSE 做了较大幅度的全年龄化改写。
    # 如果剧情连续性证据已经足够强，就把它提升到 B。
    # 这里尤其能恢复类似“成人版敏感措辞 -> 全年龄替换措辞”的同位置对白。
    elif grade in {"C", "D"} and storyline_passed:
        grade = "B"
        status = "自动接受"
        reason += f"；{storyline_reason}"

    # ------------------------------------------------------------------
    # Voice3 文件清单验证状态
    # ------------------------------------------------------------------
    if voice3_keys is None:
        voice3_verified = "未提供Voice3清单"
    elif voice_tag.upper() in voice3_keys:
        voice3_verified = "存在"
    else:
        voice3_verified = "不存在"

        # 如果用户已经提供 Voice3 权威清单，而某标签根本不在 Voice3 中，
        # 即使文本匹配很好，也不要自动写入最终映射。
        if grade in {"A", "B"}:
            grade = "C"
            status = "人工复核"
            reason += "；但 Voice3 文件清单中未找到对应资源"

    # ------------------------------------------------------------------
    # 组装上下文，方便人工听音和核验。
    # ------------------------------------------------------------------
    if chosen_index is not None:
        chosen_record = bfe_records[chosen_index]
        bfe_offset = chosen_record.offset
        bfe_raw = chosen_record.raw_text
        bfe_clean = chosen_record.clean_text
        bfe_prev = record_text(bfe_records, chosen_index - 1)
        bfe_next = record_text(bfe_records, chosen_index + 1)
    else:
        bfe_offset = None
        bfe_raw = ""
        bfe_clean = ""
        bfe_prev = ""
        bfe_next = ""

    return CandidateResult(
        script=script_name,
        voice_tag=voice_tag,
        grade=grade,
        status=status,
        reason=reason,
        similarity=similarity,
        second_similarity=second_similarity,
        bfse_index=se_index,
        bfe_index=chosen_index,
        bfse_offset=se_record.offset,
        bfe_offset=bfe_offset,
        expected_bfe_index=expected,
        anchor_before=anchor_label(before),
        anchor_after=anchor_label(after),
        bfse_raw=se_record.raw_text,
        bfse_clean=se_record.clean_text,
        bfe_raw=bfe_raw,
        bfe_clean=bfe_clean,
        bfse_prev=record_text(bfse_records, se_index - 1),
        bfse_next=record_text(bfse_records, se_index + 1),
        bfe_prev=bfe_prev,
        bfe_next=bfe_next,
        voice3_verified=voice3_verified,
        context_previous_score=context.previous_score,
        context_next_score=context.next_score,
        context_previous_strong_matches=context.previous_strong_matches,
        context_next_strong_matches=context.next_strong_matches,
        context_previous_pairs=context.previous_pairs,
        context_next_pairs=context.next_pairs,
        context_passed=context.passed,
        context_reason=context.reason,
    )


# -----------------------------------------------------------------------------
# CSV / JSON 输出
# -----------------------------------------------------------------------------
CSV_FIELDS = [
    "脚本",
    "VoiceTag",
    "等级",
    "状态",
    "相似度",
    "第二候选相似度",
    "判定理由",
    "BFSE记录号",
    "BFE记录号",
    "BFSE偏移",
    "BFE偏移",
    "预计BFE位置",
    "前锚点",
    "后锚点",
    "Voice3验证",
    "前文上下文分数",
    "后文上下文分数",
    "前文强锚点数",
    "后文强锚点数",
    "上下文是否通过",
    "上下文判定说明",
    "前文对齐明细",
    "后文对齐明细",
    "BFSE原始文本",
    "BFSE正文",
    "BFE原始文本",
    "BFE正文",
    "BFSE前一句",
    "BFSE后一句",
    "BFE前一句",
    "BFE后一句",
    "人工结论",
    "人工备注",
]


def candidate_to_row(candidate: CandidateResult) -> dict[str, object]:
    """把内部对象转换成适合 CSV 的中文字段。"""

    return {
        "脚本": candidate.script,
        "VoiceTag": candidate.voice_tag,
        "等级": candidate.grade,
        "状态": candidate.status,
        "相似度": f"{candidate.similarity:.2f}",
        "第二候选相似度": f"{candidate.second_similarity:.2f}",
        "判定理由": candidate.reason,
        "BFSE记录号": candidate.bfse_index,
        "BFE记录号": "" if candidate.bfe_index is None else candidate.bfe_index,
        "BFSE偏移": f"0x{candidate.bfse_offset:08X}",
        "BFE偏移": "" if candidate.bfe_offset is None else f"0x{candidate.bfe_offset:08X}",
        "预计BFE位置": "" if candidate.expected_bfe_index is None else f"{candidate.expected_bfe_index:.2f}",
        "前锚点": candidate.anchor_before,
        "后锚点": candidate.anchor_after,
        "Voice3验证": candidate.voice3_verified,
        "前文上下文分数": "" if candidate.context_previous_score is None else f"{candidate.context_previous_score:.2f}",
        "后文上下文分数": "" if candidate.context_next_score is None else f"{candidate.context_next_score:.2f}",
        "前文强锚点数": candidate.context_previous_strong_matches,
        "后文强锚点数": candidate.context_next_strong_matches,
        "上下文是否通过": "是" if candidate.context_passed else "否",
        "上下文判定说明": candidate.context_reason,
        "前文对齐明细": candidate.context_previous_pairs,
        "后文对齐明细": candidate.context_next_pairs,
        "BFSE原始文本": candidate.bfse_raw,
        "BFSE正文": candidate.bfse_clean,
        "BFE原始文本": candidate.bfe_raw,
        "BFE正文": candidate.bfe_clean,
        "BFSE前一句": candidate.bfse_prev,
        "BFSE后一句": candidate.bfse_next,
        "BFE前一句": candidate.bfe_prev,
        "BFE后一句": candidate.bfe_next,
        # 如果已经导入人工结论，就把它原样写出来；否则保持空白，方便继续复核。
        "人工结论": candidate.manual_decision,
        "人工备注": candidate.manual_note,
    }


def write_candidates_csv(path: Path, candidates: Iterable[CandidateResult]) -> None:
    """写 UTF-8 BOM CSV，方便 Windows Excel 直接打开不乱码。"""

    with path.open("w", encoding="utf-8-sig", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=CSV_FIELDS)
        writer.writeheader()

        for candidate in candidates:
            writer.writerow(candidate_to_row(candidate))


def write_final_mapping(
    path: Path,
    candidates: list[CandidateResult],
    exclude_replay: bool = False,
) -> None:
    """
    输出正式 ASI 应该消费的最小映射。

    写入“最终可用”的候选：

    - 没有人工结论时，只收机器 A/B；
    - 人工确认通过时，即使机器原本是 C，也收录；
    - 人工确认不通过时，无论机器原来是什么等级，都排除。

    JSON 结构：

        {
          "jyosyo01.bin": {
            "62": ["500000"]
          }
        }

    值使用数组，是为了保留“一条文本理论上可能需要多个语音标签”的扩展能力。
    """

    result: dict[str, dict[str, list[str]]] = {}

    for candidate in candidates:
        if not candidate_is_final(candidate):
            continue
        if exclude_replay and candidate.script.lower().startswith("replay"):
            continue
        if candidate.bfe_index is None:
            continue

        script_map = result.setdefault(candidate.script.lower(), {})
        key = str(candidate.bfe_index)
        tags = script_map.setdefault(key, [])

        if candidate.voice_tag not in tags:
            tags.append(candidate.voice_tag)

    # 排序只为了让 diff 和人工查看更稳定。
    ordered: dict[str, dict[str, list[str]]] = {}

    for script in sorted(result):
        ordered[script] = {
            key: result[script][key]
            for key in sorted(result[script], key=lambda value: int(value))
        }

    with path.open("w", encoding="utf-8") as handle:
        json.dump(ordered, handle, ensure_ascii=False, indent=2)


def write_script_stats(path: Path, rows: list[dict[str, object]]) -> None:
    """写每个脚本的文件级统计。"""

    fields = [
        "脚本",
        "BFE存在",
        "BFSE存在",
        "BFE文本记录",
        "BFSE文本记录",
        "主角语音候选",
        "A级",
        "B级",
        "C级",
        "D级",
        "单调唯一锚点",
    ]

    with path.open("w", encoding="utf-8-sig", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=fields)
        writer.writeheader()
        writer.writerows(rows)


# -----------------------------------------------------------------------------
# 主流程
# -----------------------------------------------------------------------------

def run(args: argparse.Namespace) -> int:
    bfe_dir = Path(args.bfe).resolve()
    bfse_dir = Path(args.bfse).resolve()
    output_dir = Path(args.out).resolve()

    if not bfe_dir.is_dir():
        print(f"[错误] BFE 目录不存在：{bfe_dir}")
        return 1

    if not bfse_dir.is_dir():
        print(f"[错误] BFSE 目录不存在：{bfse_dir}")
        return 1

    output_dir.mkdir(parents=True, exist_ok=True)

    voice_pattern = re.compile(args.voice_pattern, re.IGNORECASE)
    voice3_keys = load_voice3_keys(Path(args.voice3_list).resolve()) if args.voice3_list else None

    # 人工结论是“开发阶段的已知事实”，不是运行时逻辑。
    # 通过/不通过文件都可以省略；省略时工具纯靠机器规则生成候选。
    manual_accept = load_manual_decision_file(
        Path(args.manual_accept).resolve() if args.manual_accept else None,
        "人工确认通过",
    )
    manual_reject = load_manual_decision_file(
        Path(args.manual_reject).resolve() if args.manual_reject else None,
        "人工确认不通过",
    )

    bfe_files = collect_bin_files(bfe_dir)
    bfse_files = collect_bin_files(bfse_dir)

    common_names = sorted(set(bfe_files) & set(bfse_files))
    bfe_only = sorted(set(bfe_files) - set(bfse_files))
    bfse_only = sorted(set(bfse_files) - set(bfe_files))

    print(f"[文件] BFE BIN：{len(bfe_files)}")
    print(f"[文件] BFSE BIN：{len(bfse_files)}")
    print(f"[文件] 共同脚本：{len(common_names)}")
    print(f"[文件] BFE 独有：{len(bfe_only)}")
    print(f"[文件] BFSE 独有：{len(bfse_only)}")

    all_candidates: list[CandidateResult] = []
    script_stats: list[dict[str, object]] = []

    # 为了让“BFSE 独有且带主角语音”的情况也能进入报告，遍历 BFSE 全部文件。
    for script_key in sorted(bfse_files):
        bfse_path = bfse_files[script_key]
        bfse_records = parse_bin(bfse_path)

        voice_events: list[tuple[TextRecord, str]] = []

        for record in bfse_records:
            for voice_tag in record.voice_tags:
                if voice_pattern.fullmatch(voice_tag):
                    voice_events.append((record, voice_tag))

        # 没有主角语音时仍然保留脚本统计，但不做逐条匹配。
        if script_key in bfe_files:
            bfe_records = parse_bin(bfe_files[script_key])
        else:
            bfe_records = []

        if bfe_records:
            anchors, bfe_positions, bfse_positions = build_monotonic_anchors(
                bfe_records,
                bfse_records,
            )
        else:
            anchors = []
            bfe_positions = {}
            bfse_positions = {}

        local_candidates: list[CandidateResult] = []

        for se_record, voice_tag in voice_events:
            if not bfe_records:
                # BFSE 独有脚本，没有任何 BFE 对应文件时直接 D。
                if voice3_keys is None:
                    voice3_verified = "未提供Voice3清单"
                elif voice_tag.upper() in voice3_keys:
                    voice3_verified = "存在"
                else:
                    voice3_verified = "不存在"

                candidate = CandidateResult(
                    script=bfse_path.name,
                    voice_tag=voice_tag,
                    grade="D",
                    status="舍弃候选",
                    reason="BFSE 脚本在 BFE 中不存在，无法建立运行时记录号映射",
                    similarity=0.0,
                    second_similarity=0.0,
                    bfse_index=se_record.index,
                    bfe_index=None,
                    bfse_offset=se_record.offset,
                    bfe_offset=None,
                    expected_bfe_index=None,
                    anchor_before="",
                    anchor_after="",
                    bfse_raw=se_record.raw_text,
                    bfse_clean=se_record.clean_text,
                    bfe_raw="",
                    bfe_clean="",
                    bfse_prev=record_text(bfse_records, se_record.index - 1),
                    bfse_next=record_text(bfse_records, se_record.index + 1),
                    bfe_prev="",
                    bfe_next="",
                    voice3_verified=voice3_verified,
                    context_previous_score=None,
                    context_next_score=None,
                    context_previous_strong_matches=0,
                    context_next_strong_matches=0,
                    context_previous_pairs="",
                    context_next_pairs="",
                    context_passed=False,
                    context_reason="BFE 中不存在同名脚本，无法建立前后文定位证据",
                )
            else:
                candidate = classify_voice_event(
                    script_name=bfe_files[script_key].name,
                    voice_tag=voice_tag,
                    se_record=se_record,
                    bfe_records=bfe_records,
                    bfse_records=bfse_records,
                    anchors=anchors,
                    bfe_positions=bfe_positions,
                    bfse_positions=bfse_positions,
                    voice3_keys=voice3_keys,
                )

            local_candidates.append(candidate)
            all_candidates.append(candidate)

        grade_counts = Counter(candidate.grade for candidate in local_candidates)

        script_stats.append(
            {
                "脚本": bfse_path.name,
                "BFE存在": "是" if script_key in bfe_files else "否",
                "BFSE存在": "是",
                "BFE文本记录": len(bfe_records),
                "BFSE文本记录": len(bfse_records),
                "主角语音候选": len(local_candidates),
                "A级": grade_counts.get("A", 0),
                "B级": grade_counts.get("B", 0),
                "C级": grade_counts.get("C", 0),
                "D级": grade_counts.get("D", 0),
                "单调唯一锚点": len(anchors),
            }
        )

    # BFE 独有脚本也写到脚本统计中，让 301/280 的文件关系能完整保留下来。
    for script_key in bfe_only:
        bfe_records = parse_bin(bfe_files[script_key])
        script_stats.append(
            {
                "脚本": bfe_files[script_key].name,
                "BFE存在": "是",
                "BFSE存在": "否",
                "BFE文本记录": len(bfe_records),
                "BFSE文本记录": 0,
                "主角语音候选": 0,
                "A级": 0,
                "B级": 0,
                "C级": 0,
                "D级": 0,
                "单调唯一锚点": 0,
            }
        )

    script_stats.sort(key=lambda row: str(row["脚本"]).lower())

    # ------------------------------------------------------------------
    # 最终冲突审计
    # ------------------------------------------------------------------
    # 正式 ASI 在“同一脚本 + 同一 BFE 记录号”上不应该同时收到两个不同 VoiceTag。
    # 如果自动算法出现这种情况，说明至少有一条短句/重复句被压到了错误位置。
    # 与其猜哪一条对，不如把该目标上的全部候选一起降级到 C，让人工看上下文/试听。
    target_map: dict[tuple[str, int], list[CandidateResult]] = defaultdict(list)

    for candidate in all_candidates:
        if candidate.grade not in {"A", "B"}:
            continue
        if candidate.bfe_index is None:
            continue
        target_map[(candidate.script.lower(), candidate.bfe_index)].append(candidate)

    for target_candidates in target_map.values():
        distinct_tags = {candidate.voice_tag for candidate in target_candidates}

        if len(distinct_tags) <= 1:
            continue

        for candidate in target_candidates:
            candidate.grade = "C"
            candidate.status = "人工复核"
            candidate.reason += "；冲突审计发现多个不同 VoiceTag 指向同一 BFE 记录，已禁止自动写入"

    # ------------------------------------------------------------------
    # 导入人工复核结论
    # ------------------------------------------------------------------
    # 机器冲突审计完成后再附加人工结论，避免机器规则覆盖人类已经确认的结果。
    manual_accept_count, manual_reject_count = apply_manual_decisions(
        all_candidates,
        manual_accept,
        manual_reject,
    )

    # “自动接受”只表示机器 A/B 且没有被人工否决。
    auto_accepted = [
        candidate
        for candidate in all_candidates
        if candidate.grade in {"A", "B"}
        and not candidate_is_manual_reject(candidate)
    ]

    manual_accepted = [
        candidate
        for candidate in all_candidates
        if candidate_is_manual_accept(candidate)
    ]

    manual_rejected = [
        candidate
        for candidate in all_candidates
        if candidate_is_manual_reject(candidate)
    ]

    # 人工复核表只留下“仍未解决”的 C。
    review = [
        candidate
        for candidate in all_candidates
        if candidate.grade == "C"
        and not candidate.manual_decision
    ]

    # D 仍然保留作研究参考；人工明确不通过的条目另有单独表。
    rejected = [candidate for candidate in all_candidates if candidate.grade == "D"]

    final_candidates = [
        candidate
        for candidate in all_candidates
        if candidate_is_final(candidate)
    ]

    grade_counts = Counter(candidate.grade for candidate in all_candidates)

    # 写所有机器可读/人工可读结果。
    write_candidates_csv(output_dir / "完整候选.csv", all_candidates)
    write_candidates_csv(output_dir / "自动接受.csv", auto_accepted)
    write_candidates_csv(output_dir / "人工确认通过.csv", manual_accepted)
    write_candidates_csv(output_dir / "人工确认不通过.csv", manual_rejected)
    write_candidates_csv(output_dir / "人工复核.csv", review)
    write_candidates_csv(output_dir / "舍弃候选.csv", rejected)

    # 给用户实际复核时，最方便的是把“仍未解决的主线 C/D”放在一张表里。
    # Replay 当前仍作为第二阶段处理，因此这里主动排除 Replay。
    pending_main = [
        candidate
        for candidate in all_candidates
        if not candidate.script.lower().startswith("replay")
        and not candidate.manual_decision
        and candidate.grade in {"C", "D"}
    ]
    write_candidates_csv(output_dir / "待人工处理_主线.csv", pending_main)
    # 推荐的正式映射先排除 Replay。原因不是 Replay 不重要，而是当前 Replay
    # 脚本与 BFE/BFSE 主线差异特别大；让只有少数行成功的“半残 Replay 语音”
    # 混入正式版反而更难测试。与此同时仍输出一份包含所有自动接受项的实验映射。
    write_final_mapping(
        output_dir / "最终映射.json",
        all_candidates,
        exclude_replay=True,
    )
    write_final_mapping(
        output_dir / "最终映射_主线推荐.json",
        all_candidates,
        exclude_replay=True,
    )
    write_final_mapping(
        output_dir / "最终映射_含Replay实验.json",
        all_candidates,
        exclude_replay=False,
    )
    write_script_stats(output_dir / "脚本统计.csv", script_stats)

    non_replay_candidates = [
        candidate
        for candidate in all_candidates
        if not candidate.script.lower().startswith("replay")
    ]
    replay_candidates = [
        candidate
        for candidate in all_candidates
        if candidate.script.lower().startswith("replay")
    ]

    non_replay_final = [
        candidate
        for candidate in non_replay_candidates
        if candidate_is_final(candidate)
    ]
    replay_final = [
        candidate
        for candidate in replay_candidates
        if candidate_is_final(candidate)
    ]

    non_replay_grades = Counter(candidate.grade for candidate in non_replay_candidates)
    replay_grades = Counter(candidate.grade for candidate in replay_candidates)

    summary = {
        "BFE_BIN总数": len(bfe_files),
        "BFSE_BIN总数": len(bfse_files),
        "共同脚本数": len(common_names),
        "BFE独有脚本数": len(bfe_only),
        "BFSE独有脚本数": len(bfse_only),
        "主角语音候选总数": len(all_candidates),
        "A_确定": grade_counts.get("A", 0),
        "B_高可信": grade_counts.get("B", 0),
        "C_人工复核": grade_counts.get("C", 0),
        "D_舍弃候选": grade_counts.get("D", 0),
        "机器自动接受数_含Replay": len(auto_accepted),
        "人工确认通过数": manual_accept_count,
        "人工确认不通过数": manual_reject_count,
        "最终可用映射数_含Replay": len(final_candidates),
        "仍待人工复核数_C级": len(review),
        "主线待人工处理总数_C+D且未有人工结论": len(pending_main),
        "主线候选数": len(non_replay_candidates),
        "主线A_确定": non_replay_grades.get("A", 0),
        "主线B_高可信": non_replay_grades.get("B", 0),
        "主线C_人工复核": non_replay_grades.get("C", 0),
        "主线D_舍弃候选": non_replay_grades.get("D", 0),
        "主线机器自动A+B数": non_replay_grades.get("A", 0) + non_replay_grades.get("B", 0),
        "主线最终可用映射数_含人工通过": len(non_replay_final),
        "Replay候选数": len(replay_candidates),
        "ReplayA_确定": replay_grades.get("A", 0),
        "ReplayB_高可信": replay_grades.get("B", 0),
        "ReplayC_人工复核": replay_grades.get("C", 0),
        "ReplayD_舍弃候选": replay_grades.get("D", 0),
        "Replay最终可用映射数_含人工通过": len(replay_final),
        "唯一VoiceTag总数": len({candidate.voice_tag for candidate in all_candidates}),
        "主线唯一VoiceTag数": len({candidate.voice_tag for candidate in non_replay_candidates}),
        "Replay唯一VoiceTag数": len({candidate.voice_tag for candidate in replay_candidates}),
        "默认语音筛选正则": args.voice_pattern,
        "Voice3清单": args.voice3_list or "未提供",
        "人工通过文件": args.manual_accept or "未提供",
        "人工不通过文件": args.manual_reject or "未提供",
        "BFE独有脚本": [bfe_files[key].name for key in bfe_only],
        "BFSE独有脚本": [bfse_files[key].name for key in bfse_only],
    }

    with (output_dir / "统计摘要.json").open("w", encoding="utf-8") as handle:
        json.dump(summary, handle, ensure_ascii=False, indent=2)

    print()
    print("[结果]")
    print(f"主角语音候选：{len(all_candidates)}")
    print(f"A 确定：{grade_counts.get('A', 0)}")
    print(f"B 高可信：{grade_counts.get('B', 0)}")
    print(f"C 人工复核：{grade_counts.get('C', 0)}")
    print(f"D 舍弃候选：{grade_counts.get('D', 0)}")
    print(f"机器自动接受（含 Replay）：{len(auto_accepted)}")
    print(f"人工确认通过：{manual_accept_count}")
    print(f"人工确认不通过：{manual_reject_count}")
    print(f"最终可用映射（含 Replay）：{len(final_candidates)}")
    print(f"仍待人工复核（C级）：{len(review)}")
    print(f"主线待人工处理（未决 C+D）：{len(pending_main)}")
    print(
        "主线机器 A/B："
        f"{non_replay_grades.get('A', 0) + non_replay_grades.get('B', 0)} / "
        f"{len(non_replay_candidates)}"
    )
    print(
        "主线最终可用（含人工通过）："
        f"{len(non_replay_final)} / {len(non_replay_candidates)}"
    )
    print(
        "主线待人工："
        f"C={non_replay_grades.get('C', 0)} / D={non_replay_grades.get('D', 0)}"
    )
    print(f"输出目录：{output_dir}")

    return 0


def build_argument_parser() -> argparse.ArgumentParser:
    """建立命令行参数。"""

    parser = argparse.ArgumentParser(
        description="BFE ↔ BFSE 主角剧情语音离线映射生成器（只读 BIN，不修改游戏文件）"
    )

    parser.add_argument(
        "--bfe",
        required=True,
        help="BFE 原始日文 BIN 所在目录",
    )

    parser.add_argument(
        "--bfse",
        required=True,
        help="BFSE BIN 所在目录",
    )

    parser.add_argument(
        "--out",
        required=True,
        help="结果输出目录",
    )

    parser.add_argument(
        "--voice-pattern",
        default=DEFAULT_PROTAGONIST_PATTERN,
        help=(
            "主角语音 VoiceTag 正则。默认识别 5xxxxx / 6xxxxx / 7xxxxx / TRxxxx。"
        ),
    )

    parser.add_argument(
        "--voice3-list",
        default="",
        help=(
            "可选：Voice3 解包文件名清单。提供后会核验每个 VoiceTag 是否真实存在。"
        ),
    )

    parser.add_argument(
        "--manual-accept",
        default="",
        help=(
            "可选：人工确认通过 CSV。至少需要“脚本”和“VoiceTag”两列。"
        ),
    )

    parser.add_argument(
        "--manual-reject",
        default="",
        help=(
            "可选：人工确认不通过 CSV。至少需要“脚本”和“VoiceTag”两列；"
            "即使人工结论单元格为空，只要出现在此文件中也按不通过处理。"
        ),
    )

    return parser


def main() -> int:
    parser = build_argument_parser()
    args = parser.parse_args()
    return run(args)


if __name__ == "__main__":
    raise SystemExit(main())
