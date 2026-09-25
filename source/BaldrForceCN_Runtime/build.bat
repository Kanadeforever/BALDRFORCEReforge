@echo off
chcp 65001 >nul
setlocal EnableExtensions
cd /d "%~dp0"

REM BaldrForceCN v1.0.4-crashfix1 - MSVC C 编译 + Clang 资源汇编的备用构建路线。  
REM C 文件仍由 cl.exe 编译；embedded_assets.S 必须由 clang.exe 处理，因为 MASM 没有兼容的 .incbin 工作流。  
REM 最终仍由 32 位 link.exe 链接，/SAFESEH:NO 允许纯数据汇编对象加入 DLL。  
set "OUT=%CD%\build"
if not exist "%OUT%" mkdir "%OUT%"
if not exist "%CD%\release" mkdir "%CD%\release"

where cl >nul 2>nul
if errorlevel 1 (
  echo [失败] 未找到 cl.exe，请使用 Visual Studio x86 Native Tools 环境。  
  exit /b 1
)
where link >nul 2>nul
if errorlevel 1 (
  echo [失败] 未找到 link.exe。  
  exit /b 1
)
where clang >nul 2>nul
if errorlevel 1 (
  echo [失败] v1.0.4-crashfix1 内置资源需要 clang.exe 处理 embedded_assets.S。  
  exit /b 1
)

echo [1/3] 使用 MSVC 编译 BaldrForceCN.c...  
cl /nologo /c /O2 /GS- /Zl /TC /utf-8 /W4 /Fo"%OUT%\BaldrForceCN.obj" "%CD%\source\BaldrForceCN.c"
if errorlevel 1 exit /b 1

echo [2/3] 使用 Clang 把内置资源编译为 Win32 COFF 数据对象...  
clang --target=i686-pc-windows-msvc -c "%CD%\source\embedded_assets.S" -o "%OUT%\embedded_assets.obj"
if errorlevel 1 exit /b 1

echo [3/3] 链接 PE32 BaldrForceCN.asi...  
link /nologo /dll /machine:x86 /nodefaultlib /entry:DllMainCRTStartup /subsystem:windows /SAFESEH:NO /opt:ref /opt:icf /export:InitializeASI /Brepro /implib:"%OUT%\BaldrForceCN.lib" /out:"%CD%\release\BaldrForceCN.asi" "%OUT%\BaldrForceCN.obj" "%OUT%\embedded_assets.obj"
if errorlevel 1 exit /b 1

echo [完成] release\BaldrForceCN.asi  
exit /b 0
