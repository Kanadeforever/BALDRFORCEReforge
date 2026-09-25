@echo off  
chcp 65001 >nul
setlocal EnableExtensions
cd /d "%~dp0"

REM ============================================================================  
REM BaldrForceVoiceSE 一键构建。  
REM 正式产物只进入包根 release\；中间目录在构建结束后一律删除。  
REM ============================================================================  

REM 第 0 步：解析工具链。先查 PATH，再查本机已知的 LLVM 安装位置。  
REM 注意：MSYS2 的 clang64 不提供 clang-cl.exe，所以统一用 clang.exe 的 cl 驱动模式。  
set "CLANG="
for %%p in (clang.exe) do if not defined CLANG set "CLANG=%%~$PATH:p"
if not defined CLANG if exist "C:\msys64\clang64\bin\clang.exe" set "CLANG=C:\msys64\clang64\bin\clang.exe"
if not defined CLANG if exist "C:\Program Files\LLVM\bin\clang.exe" set "CLANG=C:\Program Files\LLVM\bin\clang.exe"
if not defined CLANG (
  echo [失败] 找不到 clang.exe。请安装 LLVM/Clang，或把它的 bin 目录加入 PATH。  
  exit /b 1
)
set "LLD="
for %%p in (lld-link.exe) do if not defined LLD set "LLD=%%~$PATH:p"
if not defined LLD if exist "C:\msys64\clang64\bin\lld-link.exe" set "LLD=C:\msys64\clang64\bin\lld-link.exe"
if not defined LLD if exist "C:\Program Files\LLVM\bin\lld-link.exe" set "LLD=C:\Program Files\LLVM\bin\lld-link.exe"
if not defined LLD (
  echo [失败] 找不到 lld-link.exe。请安装 LLVM/LLD，或把它的 bin 目录加入 PATH。  
  exit /b 1
)
REM clang.exe 依赖同目录的 DLL，把它的目录放到 PATH 最前面最稳。  
for %%d in ("%CLANG%") do set "PATH=%%~dpd;%PATH%"
echo [工具链] clang    = %CLANG%  
echo [工具链] lld-link = %LLD%  


REM 第 1 步：从当前确认映射与 BFE 脚本重新生成正式版静态 VoiceMap。  
REM 输入：data\BFE（必须正好 301 个脚本）+ data\剧情语音映射_当前确定.json  
REM 本测试版沿用正式生成器：只更新 src\VoiceMap.generated.h，不生成运行时覆盖率或审计 CSV。  
python tools\generate_voice_table.py
if errorlevel 1 goto :fail

REM 第 2 步：准备干净的临时编译目录与包根 release 目录。  
if exist build rmdir /s /q build
mkdir build
if not exist "..\..\release" mkdir "..\..\release"

REM 第 3 步：清理旧测试阶段曾经放在 release 里的 CSV，保证 release 目录只有运行时 ASI。  
if exist "..\..\release\剧情语音映射审计.csv" del /q "..\..\release\剧情语音映射审计.csv"
if exist "..\..\release\全游戏剧情语音覆盖清单.csv" del /q "..\..\release\全游戏剧情语音覆盖清单.csv"
if exist "..\..\release\待人工复核_主线45条.csv" del /q "..\..\release\待人工复核_主线45条.csv"
if exist "..\..\release\待人工复核_全部.csv" del /q "..\..\release\待人工复核_全部.csv"

REM 第 4 步：编译 32 位 x86 C++。/nodefaultlib 路线要求源码不能偷偷依赖 CRT。  
REM /O2 让每次剧情文本触发时的 FNV-1a 与二分查找保持足够轻量。  
REM -fno-builtin-strlen 与 BGMSE 同理：禁止 Clang 合成 CRT 的 strlen 调用。  
"%CLANG%" --driver-mode=cl --target=i686-pc-windows-msvc /nologo /c /O2 /GS- /GR- /Zl /W4 /EHs-c- -fno-builtin-strlen /Fo"build\BaldrForceVoiceSE.obj" "src\BaldrForceVoiceSE.cpp"
if errorlevel 1 goto :fail

REM 第 5 步：用 lld-link 生成 PE32 DLL，并直接使用 .asi 扩展名。  
REM /nodefaultlib 是硬约束；如果源码意外依赖 CRT，链接会失败而不是悄悄增加运行库要求。  
REM /timestamp:0 固定 PE 时间戳，使相同源码与工具链重复构建时得到稳定哈希。  
"%LLD%" /dll /machine:x86 /nodefaultlib /timestamp:0 /entry:DllMainCRTStartup@12 /def:BaldrForceVoiceSE.def /out:"..\..\release\BaldrForceVoiceSE.asi" /implib:"build\BaldrForceVoiceSE.lib" "build\BaldrForceVoiceSE.obj"
if errorlevel 1 goto :fail

REM 第 6 步：静态验证最终文件确实是 i386 / PE32 / DLL、无标准导入表，并导出两个 ASI 入口名。  
python tools\verify_build.py "..\..\release\BaldrForceVoiceSE.asi"
if errorlevel 1 goto :fail

REM 第 7 步：链接器偶尔会在 release 顺带生成导入库/exp/pdb，这些都不是玩家文件。  
if exist "..\..\release\BaldrForceVoiceSE.lib" del /q "..\..\release\BaldrForceVoiceSE.lib"
if exist "..\..\release\BaldrForceVoiceSE.exp" del /q "..\..\release\BaldrForceVoiceSE.exp"
if exist "..\..\release\BaldrForceVoiceSE.pdb" del /q "..\..\release\BaldrForceVoiceSE.pdb"

REM 第 8 步：删除临时目标文件和 Python 缓存；正式交付物始终只留在包根 release。  
call :cleanup

echo [成功] 已生成 ..\..\release\BaldrForceVoiceSE.asi  
exit /b 0

:cleanup
if exist build rmdir /s /q build
for /d /r %%d in (__pycache__) do @if exist "%%d" rmdir /s /q "%%d"
for /r %%f in (*.pyc) do @if exist "%%f" del /q "%%f"
exit /b 0

:fail
call :cleanup
echo [失败] 构建中止，请查看上方第一条报错。  
exit /b 1
