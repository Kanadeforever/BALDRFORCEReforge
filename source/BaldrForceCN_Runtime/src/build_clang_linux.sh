#!/usr/bin/env bash
# 这个脚本用于 Linux/容器中的 Win32 交叉编译复核，不会运行 BaldrForce.exe。
# v1.0.4-crashfix1 延续 v1.0.2 最终差异资源内嵌架构：同时编译 C 逻辑和 embedded_assets.S；后者用 .incbin 把旧汉化资源原字节放进 ASI。
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$ROOT/build"
CLANG="${CLANG:-/usr/local/swift/usr/bin/clang}"
LLD="${LLD:-/usr/local/swift/usr/bin/lld-link}"
mkdir -p "$OUT" "$ROOT/release"

# .incbin 的相对路径以当前工作目录为基准，因此先切换到项目根目录。
cd "$ROOT"

# 第一步：把主 C 逻辑编译成 32 位 Windows COFF。-Werror 让所有警告都阻止发布。
"$CLANG" --target=i686-pc-windows-msvc -O2 -ffreestanding -fno-builtin -fno-stack-protector -fno-exceptions -fno-asynchronous-unwind-tables -Wall -Wextra -Werror -c "source/BaldrForceCN.c" -o "$OUT/BaldrForceCN.obj"

# 第二步：让 Clang 集成汇编器执行 .incbin。这里没有代码逻辑，只有只读资源字节和边界符号。
"$CLANG" --target=i686-pc-windows-msvc -c "source/embedded_assets.S" -o "$OUT/embedded_assets.obj"

# 第三步：链接为 Win32/x86 DLL/ASI。汇编数据对象没有 SAFESEH 表，所以显式关闭 SAFESEH 要求。
"$LLD" /dll /machine:x86 /nodefaultlib /entry:DllMainCRTStartup /subsystem:windows /safeseh:no /opt:ref /opt:icf /export:InitializeASI /timestamp:0 /implib:"$OUT/BaldrForceCN.lib" /out:"$ROOT/release/BaldrForceCN.asi" "$OUT/BaldrForceCN.obj" "$OUT/embedded_assets.obj"
