# -*- coding: utf-8 -*-
"""
verify_build.py

对刚编译出来的 BaldrForceVoiceSE.asi 做最基础但很关键的 PE 静态检查。

为什么构建后还要检查：
    “编译器说成功”只代表链接器生成了一个文件，并不能保证它真的是旧游戏需要的 32 位 DLL，
    也不能保证我们没有不小心引入 C Runtime / Windows SDK 导入表。

本工具会确认：
    1. 文件是 PE；
    2. Machine == 0x014C，也就是 i386 / 32 位 x86；
    3. OptionalHeader 是 PE32（0x10B），而不是 PE32+ / x64；
    4. IMAGE_FILE_DLL 标志存在；
    5. 标准 Import Directory 为 0；
    6. 导出表同时包含 InitializeASI 和 _InitializeASI@0；
    7. 入口点不是 0。

工具只读取 ASI，不会修改它。
"""

from __future__ import annotations

import argparse
import struct
from pathlib import Path


def u16(data: bytes, offset: int) -> int:
    """从 byte 数组指定位置读取一个 little-endian 16 位无符号整数。"""
    return struct.unpack_from("<H", data, offset)[0]


def u32(data: bytes, offset: int) -> int:
    """从 byte 数组指定位置读取一个 little-endian 32 位无符号整数。"""
    return struct.unpack_from("<I", data, offset)[0]


def rva_to_offset(data: bytes, pe: int, rva: int) -> int:
    """
    把 PE 运行时使用的 RVA 转换成磁盘文件偏移。

    PE 的节表记录了每个 section：
        VirtualAddress     -> 装载后的 RVA 起点
        PointerToRawData   -> 文件中的起点
        SizeOfRawData      -> 文件中实际占用大小
        VirtualSize        -> 内存中逻辑大小

    找到包含目标 RVA 的节以后，用 RVA - VirtualAddress 加回 PointerToRawData 即可。
    """
    number_of_sections = u16(data, pe + 6)
    optional_size = u16(data, pe + 20)
    section_table = pe + 24 + optional_size

    for index in range(number_of_sections):
        section = section_table + index * 40
        virtual_size = u32(data, section + 8)
        virtual_address = u32(data, section + 12)
        raw_size = u32(data, section + 16)
        raw_pointer = u32(data, section + 20)

        span = max(virtual_size, raw_size)
        if virtual_address <= rva < virtual_address + span:
            return raw_pointer + (rva - virtual_address)

    raise ValueError(f"RVA 0x{rva:08X} 不属于任何 PE 节")


def read_c_string(data: bytes, offset: int) -> str:
    """读取 PE 导出表中的 ASCII NUL 结尾字符串。"""
    end = data.find(b"\x00", offset)
    if end < 0:
        raise ValueError("导出名没有 NUL 结束符")
    return data[offset:end].decode("ascii")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("asi", type=Path)
    args = parser.parse_args()

    data = args.asi.read_bytes()

    if len(data) < 0x100 or data[:2] != b"MZ":
        raise SystemExit("[失败] 不是有效 MZ/PE 文件")

    pe = u32(data, 0x3C)
    if data[pe:pe + 4] != b"PE\x00\x00":
        raise SystemExit("[失败] 找不到 PE\\0\\0 签名")

    machine = u16(data, pe + 4)
    characteristics = u16(data, pe + 22)
    optional = pe + 24
    magic = u16(data, optional)
    entry_rva = u32(data, optional + 16)

    # PE32 的 DataDirectory 从 OptionalHeader + 0x60 开始。
    export_rva = u32(data, optional + 0x60)
    export_size = u32(data, optional + 0x64)
    import_rva = u32(data, optional + 0x68)
    import_size = u32(data, optional + 0x6C)

    problems: list[str] = []

    if machine != 0x014C:
        problems.append(f"Machine 不是 i386：0x{machine:04X}")
    if magic != 0x010B:
        problems.append(f"OptionalHeader 不是 PE32：0x{magic:04X}")
    if not (characteristics & 0x2000):
        problems.append("IMAGE_FILE_DLL 标志不存在")
    if entry_rva == 0:
        problems.append("AddressOfEntryPoint 为 0")
    if import_rva != 0 or import_size != 0:
        problems.append(
            f"标准导入表不为空：RVA=0x{import_rva:08X}, size=0x{import_size:X}"
        )
    if export_rva == 0 or export_size == 0:
        problems.append("没有导出表")

    exported_names: set[str] = set()

    if export_rva:
        export_offset = rva_to_offset(data, pe, export_rva)
        number_of_names = u32(data, export_offset + 24)
        names_rva = u32(data, export_offset + 32)
        names_offset = rva_to_offset(data, pe, names_rva)

        for index in range(number_of_names):
            name_rva = u32(data, names_offset + index * 4)
            name_offset = rva_to_offset(data, pe, name_rva)
            exported_names.add(read_c_string(data, name_offset))

    for required in ("InitializeASI", "_InitializeASI@0"):
        if required not in exported_names:
            problems.append(f"缺少导出：{required}")

    if problems:
        print("[失败] ASI 静态验证未通过：")
        for problem in problems:
            print(f"  - {problem}")
        return 1

    print("[成功] ASI 静态验证通过")
    print("  Machine: i386 / PE32")
    print("  DLL: yes")
    print("  Standard Import Directory: empty")
    print(f"  Entry RVA: 0x{entry_rva:08X}")
    print("  Exports: InitializeASI, _InitializeASI@0")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
