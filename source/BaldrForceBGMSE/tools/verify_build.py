#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""验证 BaldrForceBGMSE.asi 的最小 PE32 发行条件。"""

from __future__ import annotations

import struct
import sys
from pathlib import Path


def u16(data: bytes, offset: int) -> int:
    return struct.unpack_from("<H", data, offset)[0]


def u32(data: bytes, offset: int) -> int:
    return struct.unpack_from("<I", data, offset)[0]


def rva_to_offset(data: bytes, pe: int, rva: int) -> int:
    """把 PE RVA 转成文件偏移；导出表解析需要这一步。"""
    section_count = u16(data, pe + 6)
    optional_size = u16(data, pe + 20)
    section_table = pe + 24 + optional_size

    for i in range(section_count):
        base = section_table + i * 40
        virtual_size = u32(data, base + 8)
        virtual_address = u32(data, base + 12)
        raw_size = u32(data, base + 16)
        raw_pointer = u32(data, base + 20)
        span = max(virtual_size, raw_size)
        if virtual_address <= rva < virtual_address + span:
            return raw_pointer + (rva - virtual_address)

    raise ValueError(f"RVA 0x{rva:08X} 不在任何节中")


def c_string(data: bytes, offset: int) -> str:
    end = data.index(b"\0", offset)
    return data[offset:end].decode("ascii")


def export_names(data: bytes, pe: int, optional: int) -> set[str]:
    export_rva = u32(data, optional + 0x60)
    export_size = u32(data, optional + 0x64)
    if not export_rva or not export_size:
        return set()

    exp = rva_to_offset(data, pe, export_rva)
    number_of_names = u32(data, exp + 0x18)
    names_rva = u32(data, exp + 0x20)
    names_off = rva_to_offset(data, pe, names_rva)

    result: set[str] = set()
    for i in range(number_of_names):
        name_rva = u32(data, names_off + i * 4)
        result.add(c_string(data, rva_to_offset(data, pe, name_rva)))
    return result


def main() -> None:
    if len(sys.argv) != 2:
        raise SystemExit("用法：python verify_build.py <BaldrForceBGMSE.asi>")

    path = Path(sys.argv[1])
    data = path.read_bytes()

    if data[:2] != b"MZ":
        raise SystemExit("[失败] 缺少 MZ 头")

    pe = u32(data, 0x3C)
    if data[pe : pe + 4] != b"PE\0\0":
        raise SystemExit("[失败] 缺少 PE 签名")

    machine = u16(data, pe + 4)
    characteristics = u16(data, pe + 22)
    optional = pe + 24
    magic = u16(data, optional)
    entry_rva = u32(data, optional + 0x10)

    # PE32 数据目录从 OptionalHeader + 0x60 开始；Import 是第二项，也就是 +0x68/+0x6C。
    import_rva = u32(data, optional + 0x68)
    import_size = u32(data, optional + 0x6C)

    names = export_names(data, pe, optional)

    if machine != 0x014C:
        raise SystemExit(f"[失败] Machine=0x{machine:04X}，不是 i386")
    if magic != 0x010B:
        raise SystemExit(f"[失败] OptionalHeader Magic=0x{magic:04X}，不是 PE32")
    if (characteristics & 0x2000) == 0:
        raise SystemExit("[失败] PE 没有 DLL 标志")
    if entry_rva == 0:
        raise SystemExit("[失败] AddressOfEntryPoint 为 0")
    if import_rva != 0 or import_size != 0:
        raise SystemExit(
            f"[失败] Standard Import Directory 非空：RVA=0x{import_rva:08X} Size=0x{import_size:X}"
        )
    if "InitializeASI" not in names:
        raise SystemExit(f"[失败] 没有导出 InitializeASI；当前导出={sorted(names)}")

    print("[验证] Machine = i386 (0x014C)")
    print("[验证] OptionalHeader = PE32 (0x010B)")
    print("[验证] DLL 标志存在")
    print(f"[验证] Entry RVA = 0x{entry_rva:08X}")
    print("[验证] Standard Import Directory 为空")
    print(f"[验证] 导出 = {', '.join(sorted(names))}")
    print("[成功] ASI 静态结构验证通过")


if __name__ == "__main__":
    main()
