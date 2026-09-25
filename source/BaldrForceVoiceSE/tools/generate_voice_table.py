# -*- coding: utf-8 -*-
"""
generate_voice_table.py

用途
====
把已经人工/自动审核完成的剧情语音映射转换成 ASI 编译期静态表。

当前版本与历史测试设施
======================
早期测试阶段曾经额外生成运行时覆盖表和多个审计 CSV，用于确认完整游戏到底走到了哪些脚本。
这些运行时审计设施已经完成任务。v0.1.1-test1 仍沿用 v0.1.0 的精简运行时，只保留真正影响游戏功能的 VoiceMap：

    BFE 脚本执行流哈希 + 文本记录静态偏移 -> BFSE VoiceTag

生成器仍然会检查 301 个 BFE 脚本、哈希碰撞、记录边界和多 VoiceTag 冲突，
但不会再向 release 目录生成 CSV，也不会为运行时加入覆盖率元数据。

重要安全原则
============
- 工具只读取 BIN，不写回任何游戏文件；
- v0.1.1-test1 只把“用户人工确认通过”和“前后各 20 条上下文能够唯一重新定位”的条目加入 VoiceMap；
- 前后 20 条仍无法唯一定位的 9 条主线，以及未确认 Replay，继续排除；
- 同一个 BFE 文本记录如果被两个 VoiceTag 同时指向，当前版本整条排除；
- 运行时仍然不进行模糊文本比较。
"""

from __future__ import annotations

import json
import struct
from pathlib import Path


# ---------------------------------------------------------------------------
# 固定目录
# ---------------------------------------------------------------------------

# __file__ 是当前 Python 文件自己的路径。
# parents[1] 就是 source 目录。
ROOT = Path(__file__).resolve().parents[1]
DATA_DIR = ROOT / "data"

# C++ 编译时直接 include 的自动生成头文件。
HEADER_OUT = ROOT / "src" / "VoiceMap.generated.h"

# 当前已经审核通过的“脚本名 -> 文本记录号 -> [VoiceTag...]”映射。
MAP_PATH = DATA_DIR / "剧情语音映射_当前确定.json"

# BFE 原始脚本。生成器需要它们来把逻辑记录号转换成稳定的执行流字节偏移。
BFE_DIR = DATA_DIR / "BFE"

TEXT_OPCODE = b"\x0B\x00"
MAX_STRING_SIZE = 65536


# ---------------------------------------------------------------------------
# 基础二进制工具
# ---------------------------------------------------------------------------

def fnv1a32(data: bytes) -> int:
    """计算 32 位 FNV-1a；C++ 运行时会使用完全相同的逐字节算法。"""
    value = 0x811C9DC5
    for byte in data:
        value ^= byte
        value = (value * 0x01000193) & 0xFFFFFFFF
    return value


def script_stream_range(data: bytes) -> tuple[int, int]:
    """
    读取当前已经确认的 NEXAS/BALDR FORCE BIN 头部，返回真正执行流的位置和长度。

    结构：
        stream_start = 16 + (u32@0x08 * 4)
        stream_size  = u32@0x0C

    旧汉化追加的 BFET 位于执行流结尾之后，所以只哈希这段执行流可以同时兼容：
        - 原始日文 BFE BIN；
        - 旧汉化追加 BFET 的 BFE BIN。
    """
    if len(data) < 16:
        raise ValueError("文件小于 16 字节，不符合当前已确认的 BFE BIN 结构")

    table_count = struct.unpack_from("<I", data, 0x08)[0]
    stream_size = struct.unpack_from("<I", data, 0x0C)[0]
    stream_start = 16 + table_count * 4
    stream_end = stream_start + stream_size

    if stream_end > len(data):
        raise ValueError(
            f"脚本头声明的执行流结尾 {stream_end} 超过文件实际大小 {len(data)}"
        )

    return stream_start, stream_size


def parse_text_records(data: bytes, stream_start: int, stream_size: int) -> list[tuple[int, int]]:
    """
    扫描脚本执行流里的 0x000B 文本记录。

    返回：
        [(逻辑文本记录号, 执行流内字节偏移), ...]

    这里的“逻辑文本记录号”与离线 BFE↔BFSE 映射器使用的定义保持一致。
    """
    stream_end = stream_start + stream_size
    records: list[tuple[int, int]] = []
    position = stream_start

    while True:
        hit = data.find(TEXT_OPCODE, position, stream_end)
        if hit < 0:
            break

        if hit + 6 > stream_end:
            break

        string_size = struct.unpack_from("<I", data, hit + 2)[0]
        if string_size <= 0 or string_size > MAX_STRING_SIZE:
            position = hit + 1
            continue

        string_start = hit + 6
        string_end = string_start + string_size
        if string_end > stream_end:
            position = hit + 1
            continue

        raw = data[string_start:string_end]
        if not raw.endswith(b"\x00"):
            position = hit + 1
            continue

        try:
            raw[:-1].decode("cp932")
        except UnicodeDecodeError:
            position = hit + 1
            continue

        # hit 是整个文件内的偏移；减掉 stream_start 后才是运行时状态对象看到的执行流偏移。
        records.append((len(records), hit - stream_start))
        position = string_end

    return records


def collect_bfe_files() -> dict[str, Path]:
    """按 Windows 大小写无关语义收集 BFE 目录中的所有 BIN。"""
    result: dict[str, Path] = {}

    for path in BFE_DIR.iterdir():
        if not path.is_file() or path.suffix.lower() != ".bin":
            continue

        key = path.name.lower()
        if key in result:
            raise RuntimeError(f"存在大小写冲突文件：{result[key]} / {path}")
        result[key] = path

    return result


def c_string(text: str) -> str:
    """把 ASCII 文本安全转义成 C/C++ 字符串字面量。"""
    return '"' + text.replace("\\", "\\\\").replace('"', '\\"') + '"'


# ---------------------------------------------------------------------------
# 主生成流程
# ---------------------------------------------------------------------------

def main() -> int:
    if not MAP_PATH.is_file():
        raise FileNotFoundError(MAP_PATH)
    if not BFE_DIR.is_dir():
        raise FileNotFoundError(BFE_DIR)

    mapping = json.loads(MAP_PATH.read_text(encoding="utf-8"))
    files = collect_bfe_files()

    # 用户已经确认 BFE 实际是 301 个 BIN。
    # 如果以后数据目录不完整，直接失败，避免生成“看似成功但少脚本”的覆盖版。
    if len(files) != 301:
        raise RuntimeError(f"BFE 脚本数量不是已确认的 301：实际 {len(files)}")

    # scripts 保存全 301 个 BFE 脚本的信息。
    scripts: list[dict] = []
    script_by_lower_name: dict[str, dict] = {}
    hash_to_name: dict[int, str] = {}

    for key, path in sorted(files.items()):
        data = path.read_bytes()
        stream_start, stream_size = script_stream_range(data)
        script_hash = fnv1a32(data[stream_start:stream_start + stream_size])
        records = parse_text_records(data, stream_start, stream_size)

        # 301 个 BFE 执行流在此前调查中已经确认零碰撞。
        # 生成时仍然重新检查，防止输入文件被替换或损坏。
        old_name = hash_to_name.get(script_hash)
        if old_name is not None and old_name.lower() != path.name.lower():
            raise RuntimeError(
                f"FNV-1a 脚本哈希碰撞：0x{script_hash:08X}: {old_name} / {path.name}"
            )
        hash_to_name[script_hash] = path.name

        info = {
            "script": path.name,
            "script_hash": script_hash,
            "stream_start": stream_start,
            "stream_size": stream_size,
            "records": records,
            "voice_count": 0,
        }
        scripts.append(info)
        script_by_lower_name[key] = info

    entries: list[dict] = []
    conflicts: list[tuple[str, int, list[str]]] = []

    for script_name, record_map in mapping.items():
        info = script_by_lower_name.get(script_name.lower())
        if info is None:
            raise RuntimeError(f"映射引用了不存在的 BFE 脚本：{script_name}")

        offsets = {record_index: offset for record_index, offset in info["records"]}

        for record_index_text, tags in record_map.items():
            record_index = int(record_index_text)

            # 当前已知有 4 个 ren47c 记录被两个 BFSE VoiceTag 同时指向。
            # 在人工判定之前，宁可少播，也绝不把两个声音塞给同一句 BFE 台词。
            if len(tags) != 1:
                conflicts.append((script_name, record_index, list(tags)))
                continue

            if record_index not in offsets:
                raise RuntimeError(
                    f"映射记录号超出脚本实际文本记录：{script_name} #{record_index}"
                )

            voice_tag = tags[0]
            if len(voice_tag) > 7:
                raise RuntimeError(f"VoiceTag 太长，当前 C++ 固定数组容不下：{voice_tag}")

            entries.append(
                {
                    "script": info["script"],
                    "script_hash": info["script_hash"],
                    "record_index": record_index,
                    "record_offset": offsets[record_index],
                    "voice_tag": voice_tag,
                    # 历史字段名保留为 needsH00；正式版不会注入 @h00。
                    # 该字段只保留“这是 TR 字母型 VoiceTag”的离线来源信息。
                    "needs_h00": voice_tag.upper().startswith("TR"),
                }
            )
            info["voice_count"] += 1

    # VoiceMap 运行时用二分查找，所以按 (脚本哈希, 记录偏移) 排序。
    entries.sort(key=lambda item: (item["script_hash"], item["record_offset"]))

    seen_keys: set[tuple[int, int]] = set()
    for item in entries:
        key = (item["script_hash"], item["record_offset"])
        if key in seen_keys:
            raise RuntimeError(f"重复运行时键：{key}")
        seen_keys.add(key)

    # 正式版运行时不再需要全脚本覆盖表。
    # 这里仍然保留 scripts 列表，是为了在生成阶段确认 301 个输入脚本都可解析、哈希零碰撞。

    # -----------------------------------------------------------------------
    # 生成 C++ 头文件
    # -----------------------------------------------------------------------
    HEADER_OUT.parent.mkdir(parents=True, exist_ok=True)
    with HEADER_OUT.open("w", encoding="utf-8", newline="\n") as f:
        f.write("// 这是 generate_voice_table.py 自动生成的文件。\n")
        f.write("// 不要手工编辑；修改离线映射或 BFE 脚本后应重新运行生成器。\n")
        f.write("// v0.1.1-test1 只包含当前已确认或经前后20条上下文唯一定位的剧情语音映射。\n\n")

        # 下面这些注释会直接进入自动生成的 C++ 头文件。
        # 这样即使接档者只打开 VoiceMap.generated.h，也能理解每个字段的意义，
        # 不需要先反查 Python 生成器。
        f.write("// 每一项表示：某个 BFE 脚本执行流中的某一条 0x000B 文本，应在运行时临时追加哪个 VoiceTag。\n")
        f.write("struct VoiceMapEntry {\n")
        f.write("    unsigned int scriptHash;      // BFE 脚本真正执行流的 32 位 FNV-1a；旧汉化尾部 BFET 不参与哈希。\n")
        f.write("    unsigned int recordOffset;    // 这条 0x000B 文本记录相对执行流起点的字节偏移；运行时用它精确定位。\n")
        f.write("    unsigned short recordIndex;   // 离线人工审计用的逻辑文本序号；正式运行时查找不依赖它。\n")
        f.write("    unsigned char needsH00;       // 历史字段名；1 表示 TRxxxx，保留给离线审计理解其特殊兼容来源。\n")
        f.write("    char voiceTag[8];             // 不含 @v 和 .wav 的资源键，例如 500000 或 TR0001；末尾由 NUL 自动补齐。\n")
        f.write("};\n\n")

        f.write("static const VoiceMapEntry kVoiceMap[] = {\n")
        for item in entries:
            f.write(
                "    { 0x%08Xu, 0x%08Xu, %du, %du, %s }, // %s\n"
                % (
                    item["script_hash"],
                    item["record_offset"],
                    item["record_index"],
                    1 if item["needs_h00"] else 0,
                    c_string(item["voice_tag"]),
                    item["script"],
                )
            )
        f.write("};\n\n")
        f.write(
            "static const unsigned int kVoiceMapCount = "
            "(unsigned int)(sizeof(kVoiceMap) / sizeof(kVoiceMap[0]));\n\n"
        )

    mapped_scripts = sum(1 for item in scripts if item["voice_count"] > 0)

    print(f"[完成] BFE 全脚本身份表：{len(scripts)} 个")
    print(f"[完成] 有已确认主角语音映射的脚本：{mapped_scripts} 个")
    print(f"[完成] 编入 ASI 的唯一安全语音映射：{len(entries)} 条")
    print(f"[安全排除] 多 VoiceTag 冲突记录：{len(conflicts)} 条")
    for script, index, tags in conflicts:
        print(f"  - {script} #{index}: {', '.join(tags)}")
    print(f"[输出] {HEADER_OUT}")
    print("[精简运行时] 不生成运行时覆盖 CSV，也不向 release 目录输出审计 CSV")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
