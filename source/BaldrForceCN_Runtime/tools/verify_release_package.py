#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
BaldrForceCN v1.0.4-crashfix1 发行包离线验证器。

这个脚本专门解决一个现实问题：维护环境里不一定随时有固定 SHA256 的 clean 2003
BaldrForce.exe，但我们仍然应该在发布前检查“我们自己生成的包”有没有被弄坏。

因此它只检查本项目能够独立证明的内容，不读取、也不修改游戏 EXE：

1. release 目录是否仍然只有 ASI + INI + Update.pac + Chapter.pac 四个文件；
2. 两个 PAC 是否还是旧 46MB 汉化 oracle 的固定字节；
3. ASI 是否还是 32 位 PE DLL、没有导入表、并导出 InitializeASI；
4. 9 个经用户全量二进制对比确认与原版不同的 loose 资源是否保持固定大小/SHA256，
   并且每份原始 payload 在最终 ASI 中恰好出现一次；
5. 313 文件 VFS 发行前审计是否仍然是 11 COVERED + 302 PASS + 0 REVIEW；
6. v1.0.2 同点崩溃反证及 test12 实机通过证据是否仍随包保存；
7. 关键文档是否已经升级到 v1.0.4-crashfix1；
8. VEH 只记录诊断、最近 128 次脚本事件和 EXCEPTION_CONTINUE_SEARCH 语义是否仍存在。

完整 clean-EXE 地址/签名验证仍由 tools/verify_static.py 负责。两个验证器职责不同：
- verify_release_package.py：任何维护机器都能跑，验证发行包自身；
- verify_static.py <BaldrForce.exe>：有固定 clean 2003 EXE 时再跑，额外验证所有 RVA/签名。

脚本只读项目文件。任何一项 FAIL 都表示不应发布当前目录。
"""

from __future__ import annotations

import csv
import hashlib
import struct
from collections import Counter
from pathlib import Path


# 项目根目录就是 tools 的上一级。这样不论从哪里执行脚本，都能找到 release/source/docs。
ROOT = Path(__file__).resolve().parents[1]

# 两个 PAC 的字节已经从旧 46MB 汉化 VFS 独立恢复并多轮固定。
# 这里同时固定大小和 SHA256，避免“文件名一样但内容被换掉”的假通过。
EXPECTED_PACS = {
    "Update.pac": (
        23_984_401,
        "7f5f66e03d23869aeb2c8de771f4d34907478a85cfa8982e9da9c64612c4114e",
    ),
    "Chapter.pac": (
        8_129_004,
        "565e48a1a272feb9c99223f0a5dac8e3a450dea4a5ce05e8e81e8e400b9e960d",
    ),
}

# v1.0.4-crashfix1 继续只内嵌这些已经有明确旧汉化真值/实机证据的 loose 资源。
# 不把 313 文件全塞进 ASI，是为了避免把仍为日文或纯游戏数据的资源错误覆盖 clean 版。
EXPECTED_EMBEDDED_ASSETS = {
    'BMP/Hell/Font.grp': (1036825, '391e8b1eebb4304bb3426a564f248e347e564d5c9e9f20a169f05b7522a49d87'),
    'BMP/Hell/MenuMsg.grp': (277855, '2ee492082f3bb677178a20086beed9693b397e9709bf682a10a266f9b7e75bf6'),
    'BMP/Hell/hell_Menu03.grp': (94201, 'd4232bc336894b9dc4ff8b081ab7bf609f31e102a8b48b9310d3cf3263d01df1'),
    'Dat/Cpu/genha.cpu': (2648, '00a9a0ec65c96d61ce2b54ac327ae5f2dda2ac8c893ac026764ec036ccd7186b'),
    'Dat/Cpu/kaira.cpu': (1496, '830640626d5abdb27574109cdcae4ae251093cba85c1418495a26e3948b3db38'),
    'Dat/Cpu/zako_s_t01.cpu': (638, '25a9f7d9c0ca9a4d234480b27cb1abdd95ff2517680c863b5568c4d6d144fb87'),
    'Dat/Waza/neko05.waz': (32534, '5b44d6725fe6c8d73e6a05c23a1e2da64a6e5bce340bbe0c29ef29853d5a5256'),
    'Dat/Waza/TOORU.WAZ': (349982, '7f3b5b46284c4727cc49627c43ff9d1fb4b34534f4921106a084122f7916825c'),
    'Dat/Waza/YAGISAWA.WAZ': (42194, 'ce25737bc3469600503ef957140e9020b5747be4b46d17830b19757268d082ca'),
}

# 313 文件全集审计的固定分组。只要未来新增“需要人工复核”的文件，这里就会 FAIL，
# 强迫维护者先弄清资源用途，再决定是否修改正式发行。
EXPECTED_AUDIT_GROUPS = {
    "ASI内嵌差异loose资源": 9,
    "CPU/AI（原版一致）": 80,
    "PAC": 2,
    "WAZ（原版一致）": 79,
    "HELLMODE资源（原版一致）": 13,
    "地图数据": 5,
    "字体": 1,
    "战斗语音路由": 24,
    "战斗部件图": 2,
    "机体图形/动画": 26,
    "语音WAV": 72,
}


def sha256_bytes(data: bytes) -> str:
    """计算一块内存数据的 SHA256。"""
    return hashlib.sha256(data).hexdigest()


def sha256_file(path: Path) -> str:
    """分块读取大文件，避免一次把 20~30MB PAC 全塞进额外 Python 临时对象。"""
    h = hashlib.sha256()
    with path.open("rb") as f:
        while True:
            chunk = f.read(1024 * 1024)
            if not chunk:
                break
            h.update(chunk)
    return h.hexdigest()


def u16(data: bytes, offset: int) -> int:
    """从 bytes 的指定位置读取一个 little-endian 16 位无符号整数。"""
    return struct.unpack_from("<H", data, offset)[0]


def u32(data: bytes, offset: int) -> int:
    """从 bytes 的指定位置读取一个 little-endian 32 位无符号整数。"""
    return struct.unpack_from("<I", data, offset)[0]


def parse_pe(data: bytes) -> dict:
    """
    读取本验证需要的最小 PE 信息。

    我们不用 pefile 等第三方库，是为了让源码包只靠 Python 标准库就能离线验证。
    返回 image base、section、data directory 等信息，后面的 RVA 转文件偏移和导出表解析会用到。
    """
    if data[:2] != b"MZ":
        raise ValueError("MZ header missing")

    pe_offset = u32(data, 0x3C)
    if data[pe_offset : pe_offset + 4] != b"PE\0\0":
        raise ValueError("PE signature missing")

    machine = u16(data, pe_offset + 4)
    section_count = u16(data, pe_offset + 6)
    optional_size = u16(data, pe_offset + 20)
    characteristics = u16(data, pe_offset + 22)

    optional = pe_offset + 24
    magic = u16(data, optional)
    if magic != 0x10B:
        # 本项目只支持 PE32/x86。PE32+ 的字段偏移不同，不要硬解析后给出假结果。
        raise ValueError(f"unexpected optional-header magic 0x{magic:04X}")

    image_base = u32(data, optional + 28)
    directory_base = optional + 96
    directories = [
        (u32(data, directory_base + i * 8), u32(data, directory_base + i * 8 + 4))
        for i in range(16)
    ]

    sections = []
    section_table = optional + optional_size
    for i in range(section_count):
        off = section_table + i * 40
        name = data[off : off + 8].split(b"\0", 1)[0].decode("ascii", "replace")
        virtual_size = u32(data, off + 8)
        virtual_address = u32(data, off + 12)
        raw_size = u32(data, off + 16)
        raw_offset = u32(data, off + 20)
        sections.append(
            {
                "name": name,
                "va": virtual_address,
                "vs": virtual_size,
                "raw": raw_offset,
                "raw_size": raw_size,
            }
        )

    return {
        "machine": machine,
        "magic": magic,
        "characteristics": characteristics,
        "image_base": image_base,
        "directories": directories,
        "sections": sections,
    }


def rva_to_offset(pe: dict, rva: int) -> int:
    """把 PE 运行时 RVA 转成磁盘文件偏移，只接受真正落在 section raw data 里的地址。"""
    for section in pe["sections"]:
        start = section["va"]
        span = max(section["vs"], section["raw_size"])
        if start <= rva < start + span:
            delta = rva - start
            if delta >= section["raw_size"]:
                raise ValueError(f"RVA 0x{rva:X} has no raw file bytes")
            return section["raw"] + delta
    raise ValueError(f"RVA 0x{rva:X} is not mapped by any section")


def read_export_names(data: bytes, pe: dict) -> set[str]:
    """
    从 PE export directory 中读取所有导出名字。

    InitializeASI 是 ASI loader 找到入口的关键导出。只看 DLL 标志还不够；如果链接时忘了
    /export:InitializeASI，游戏会得到一个“格式正确但无法初始化”的 ASI，所以这里单独验证。
    """
    export_rva, export_size = pe["directories"][0]
    if export_rva == 0 or export_size == 0:
        return set()

    export_off = rva_to_offset(pe, export_rva)
    number_of_names = u32(data, export_off + 24)
    names_rva = u32(data, export_off + 32)
    names_off = rva_to_offset(pe, names_rva)

    result: set[str] = set()
    for i in range(number_of_names):
        name_rva = u32(data, names_off + i * 4)
        name_off = rva_to_offset(pe, name_rva)
        end = data.find(b"\0", name_off)
        if end < 0:
            raise ValueError("unterminated export name")
        result.add(data[name_off:end].decode("ascii", "replace"))
    return result


def report(label: str, ok: bool, detail: str = "") -> bool:
    """统一打印 PASS/FAIL；同时把布尔值返回给主流程累计最终结果。"""
    suffix = f": {detail}" if detail else ""
    print(f"[{'PASS' if ok else 'FAIL'}] {label}{suffix}")
    return ok


def main() -> int:
    ok = True

    # ------------------------------------------------------------------
    # 1. release 布局与两个 PAC
    # ------------------------------------------------------------------
    release = ROOT / "release"
    release_files = sorted(
        p.relative_to(release).as_posix()
        for p in release.rglob("*")
        if p.is_file()
    )
    ok &= report(
        "release only contains four runtime files",
        release_files == ["BaldrForceCN.asi", "BaldrForceCN.ini", "Chapter.pac", "Update.pac"],
        ", ".join(release_files),
    )

    for name, (expected_size, expected_hash) in EXPECTED_PACS.items():
        path = release / name
        exists = path.is_file()
        ok &= report(f"{name} exists", exists)
        if not exists:
            continue
        actual_hash = sha256_file(path)
        ok &= report(f"{name} size", path.stat().st_size == expected_size, str(path.stat().st_size))
        ok &= report(f"{name} SHA256", actual_hash == expected_hash, actual_hash)

    # ------------------------------------------------------------------
    # 2. ASI 格式、导入表、导出表
    # ------------------------------------------------------------------
    asi_path = release / "BaldrForceCN.asi"
    if not asi_path.is_file():
        ok &= report("BaldrForceCN.asi exists", False)
        asi_data = b""
    else:
        asi_data = asi_path.read_bytes()
        ok &= report("BaldrForceCN.asi exists", True)
        try:
            pe = parse_pe(asi_data)
            is_dll = bool(pe["characteristics"] & 0x2000)
            ok &= report(
                "ASI is PE32 / i386 / DLL",
                pe["machine"] == 0x14C and pe["magic"] == 0x10B and is_dll,
                f"machine=0x{pe['machine']:04X}",
            )

            import_rva, import_size = pe["directories"][1]
            ok &= report(
                "ASI import directory is empty",
                import_rva == 0 and import_size == 0,
                f"RVA=0x{import_rva:X}, size={import_size}",
            )

            export_names = read_export_names(asi_data, pe)
            ok &= report(
                "InitializeASI export exists",
                "InitializeASI" in export_names,
                ", ".join(sorted(export_names)),
            )
        except Exception as exc:  # noqa: BLE001 - verifier must turn malformed PE into a clean FAIL.
            ok &= report("ASI PE parsing", False, str(exc))

        print(f"[INFO] ASI size: {len(asi_data)}")
        print(f"[INFO] ASI SHA256: {sha256_bytes(asi_data)}")

    # ------------------------------------------------------------------
    # 3. 9 个原始 loose 差异资产与最终 ASI 内嵌字节
    # ------------------------------------------------------------------
    for rel, (expected_size, expected_hash) in EXPECTED_EMBEDDED_ASSETS.items():
        source_asset = ROOT / "source" / "assets" / Path(rel)
        exists = source_asset.is_file()
        ok &= report(f"source asset exists: {rel}", exists)
        if not exists:
            continue

        payload = source_asset.read_bytes()
        actual_hash = sha256_bytes(payload)
        ok &= report(
            f"source asset size/hash: {rel}",
            len(payload) == expected_size and actual_hash == expected_hash,
            f"{len(payload)} / {actual_hash}",
        )

        # .incbin 的目标就是把整个原文件逐字节放进 ASI。
        # “恰好一次”同时能发现漏嵌入（0 次）和错误重复嵌入（2 次以上）。
        if asi_data:
            count = asi_data.count(payload)
            ok &= report(f"ASI contains embedded asset exactly once: {rel}", count == 1, str(count))

    # ------------------------------------------------------------------
    # 4. 313 文件 VFS 全集审计输出
    # ------------------------------------------------------------------
    audit_path = ROOT / "data" / "旧汉化VFS资源审计.csv"
    if not audit_path.is_file():
        ok &= report("VFS audit CSV exists", False)
    else:
        rows = list(csv.DictReader(audit_path.open("r", encoding="utf-8-sig", newline="")))
        status_counts = Counter(row["audit_result"] for row in rows)
        group_counts = Counter(row["group"] for row in rows)
        total_bytes = sum(int(row["size"]) for row in rows)

        ok &= report("VFS audit covers 313 files", len(rows) == 313, str(len(rows)))
        ok &= report("VFS audit covers 45,599,439 bytes", total_bytes == 45_599_439, str(total_bytes))
        ok &= report(
            "VFS audit disposition is 11 COVERED + 302 PASS + 0 REVIEW",
            status_counts["COVERED"] == 11
            and status_counts["PASS"] == 302
            and status_counts["REVIEW"] == 0,
            repr(dict(status_counts)),
        )
        ok &= report(
            "VFS audit group counts match frozen classification",
            dict(group_counts) == EXPECTED_AUDIT_GROUPS,
            repr(dict(group_counts)),
        )

    # ------------------------------------------------------------------
    # 5. 版本、实机证据与文档同步检查
    # ------------------------------------------------------------------
    source_text = (ROOT / "source" / "BaldrForceCN.c").read_text("utf-8")
    ok &= report("after.bin runtime hit logging",
                 "[命中] after.bin 直接 GBK 脚本文本已执行（运行时已确认）" in source_text and
                 "g_after_bin_direct_runtime_hits" in source_text)
    ok &= report(
        "script hook separates source count from runtime value-object byte_count",
        "*(DWORD*)(state+0x114)=cur+count" in source_text
        and "*(DWORD*)(dest-4)=outn+1U" in source_text
        and "g_script_runtime_count_updated" in source_text
        and "运行时value object count已更新为27" in source_text,
    )
    ok &= report(
        "script-copy bounds check present",
        "if(count>limit-cur)" in source_text,
    )
    ok &= report(
        "source version marker is v1.0.4-crashfix1",
        "BaldrForceCN 中文运行时 ASI v1.0.4-crashfix1" in source_text,
    )
    ok &= report(
        "crash VEH is present and continues original exception search",
        "AddVectoredExceptionHandler" in source_text
        and "CrashDiagnosticVEH" in source_text
        and "EXCEPTION_CONTINUE_SEARCH" in source_text
        and "[崩溃] ===== 捕获到严重异常（仅记录，不拦截） =====" in source_text,
    )
    ok &= report(
        "128-entry detailed script diagnostic ring is present",
        "#define SCRIPT_DIAG_RING 128U" in source_text
        and "diag_begin_script_event" in source_text
        and "diag_log_recent_script_events" in source_text
        and "script_name[32]" in source_text
        and "mapping_index" in source_text
        and "value_count" in source_text
        and "post_cur" in source_text,
    )
    ok &= report(
        "64-entry glyph/DBCS diagnostic ring is present",
        "#define GLYPH_DIAG_RING 64U" in source_text
        and "diag_note_glyph" in source_text
        and "diag_log_recent_glyph_events" in source_text,
    )
    ok &= report(
        "enhanced crash context logging is present",
        "diag_log_exception_parameters" in source_text
        and "diag_log_register_memory" in source_text
        and "diag_log_stack_code_candidates" in source_text
        and "diag_log_ebp_chain" in source_text
        and "diag_log_recent_script_objects" in source_text
        and "diag_log_runtime_counters" in source_text
        and "pFlushFileBuffers" in source_text,
    )
    ok &= report(
        "Backlog #64 runtime diagnostic is present",
        "0x482ACBB0UL" in source_text
        and "0x6AA94A29UL" in source_text
        and "[诊断][Backlog复现句]" in source_text,
    )
    # v1.0.4-crashfix1 延续中文日志，因此必须验证当前运行时代码里不再残留旧英文等级标签。
    # 这里检查源码而不是历史 evidence 日志；历史日志属于测试证据，必须保持原样。
    chinese_log_prefixes = ["[信息]", "[成功]", "[警告]", "[失败]", "[命中]", "[追踪]", "[诊断]", "[崩溃]"]
    old_log_prefixes = ["[INFO]", "[OK]", "[WARN]", "[FAIL]", "[HIT]", "[TRACE]", "[DIAG]"]
    ok &= report(
        "runtime log prefixes are Simplified Chinese",
        all(prefix in source_text for prefix in chinese_log_prefixes)
        and not any(prefix in source_text for prefix in old_log_prefixes),
    )
    ok &= report(
        "runtime log writes UTF-8 BOM",
        r'log_raw("\xEF\xBB\xBF",3)' in source_text,
    )
    ok &= report(
        "Unicode glyph fallback prevents confirmed GBK pairs from falling back to CP932",
        "query_gbk_outline" in source_text
        and "GetGlyphOutlineW" in source_text
        and "make_safe_blank_glyph" in source_text
        and "GBK 字形的 ANSI/Unicode 两条 GDI 路径均失败" in source_text,
    )
    ok &= report(
        "optional INI exposes only FontFace and EnableLog",
        "load_runtime_config" in source_text
        and "FontFace" in source_text
        and "EnableLog" in source_text
        and "g_enable_log" in source_text,
    )

    ini_path = ROOT / "release" / "BaldrForceCN.ini"
    if not ini_path.is_file():
        ok &= report("BaldrForceCN.ini exists", False)
    else:
        ini_text = ini_path.read_text("utf-16")
        ok &= report(
            "BaldrForceCN.ini contains only the supported user settings",
            "FontFace=" in ini_text and "EnableLog=1" in ini_text
            and "EnableWideCharToMultiByte" not in ini_text
            and "ForceCharset" not in ini_text
            and "ContextBytes" not in ini_text,
        )

    evidence_files = [
        "test12_HELLMODE中文图片已恢复.png",
        "test12_TORU练习招式继续正确.png",
        "test12_Information继续正确中文.png",
        "v1.0.2_八木泽名片场景同点崩溃日志.txt",
        "v1.0.2_Backlog前扩张文本末尾方块_打开Backlog崩溃.png",
        "v1.0.3_第一次_八木泽名片崩溃.log",
        "v1.0.3_第二次_Backlog崩溃.log",
    ]
    missing_evidence = [
        name for name in evidence_files if not (ROOT / "evidence" / name).is_file()
    ]
    ok &= report(
        "test12 final real-machine evidence is present",
        not missing_evidence,
        ", ".join(missing_evidence),
    )

    backlog_audit = ROOT / "tools" / "audit_bfet_backlog_risks.py"
    backlog_csv = ROOT / "data" / "BFET_Backlog风险审计.csv"
    backlog_summary = ROOT / "data" / "脚本历史记录风险审计摘要.txt"
    ok &= report("Backlog BFET audit tool is present", backlog_audit.is_file())
    ok &= report("Backlog BFET audit outputs are present", backlog_csv.is_file() and backlog_summary.is_file())
    if backlog_summary.is_file():
        backlog_text = backlog_summary.read_text("utf-8", errors="replace")
        ok &= report(
            "Backlog audit frozen counts match",
            all(x in backlog_text for x in [
                "总映射数：34184", "中日等长：3909", "中文更短：29633", "中文更长：642",
                "GBK 双字节中间：627", "中文自身「/」数量不平衡：1", "中日「/」数量发生变化：6",
            ]),
        )

    main_readme = (ROOT / "说明.md").read_text("utf-8")
    handoff = (ROOT / "docs" / "完整接档说明.md").read_text("utf-8")
    resource_doc = (ROOT / "docs" / "资源覆盖说明.md").read_text("utf-8")
    all_docs = "\n".join(
        p.read_text("utf-8", errors="replace")
        for p in ROOT.rglob("*.md")
        if "tools/研究工具" not in p.as_posix()
    )

    ok &= report("top-level README identifies v1.0.4-crashfix1", "v1.0.4-crashfix1" in main_readme.splitlines()[0])
    ok &= report("handoff identifies v1.0.4-crashfix1", "截至 v1.0.4-crashfix1" in handoff.splitlines()[0])
    ok &= report(
        "resource doc records HELLMODE real-machine pass",
        "HELLMODE" in resource_doc and "test12" in resource_doc and "实机" in resource_doc and "恢复" in resource_doc,
    )

    stale_phrases = [
        "HELLMODE Bmp\\Hell 图片资产        test12 当前待验",
        "唯一尚待本轮实机确认的是 HELLMODE",
        "实机验证 test12 HELLMODE / Trial 图片说明是否恢复",
    ]
    stale_found = [phrase for phrase in stale_phrases if phrase in all_docs]
    ok &= report("no stale pre-release HELLMODE status remains in docs", not stale_found, " | ".join(stale_found))

    print("RESULT:", "PASS" if ok else "FAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
