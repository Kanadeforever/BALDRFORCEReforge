@echo off  
chcp 65001 >nul
setlocal EnableExtensions
cd /d "%~dp0"

REM ============================================================================  
REM BaldrForceBGMSE 一键构建。  
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


REM 第 1 步：重新解析两版 BGMInfo.DAT，生成 BFSE 30 条资源名/循环点静态表与中文对照 CSV。  
python tools\generate_bgm_table.py
if errorlevel 1 goto :fail

REM 第 2 步：准备干净的临时编译目录和包根 release 目录。  
if exist build rmdir /s /q build
mkdir build
if not exist "..\..\release" mkdir "..\..\release"

REM 第 3 步：编译 32 位 x86 C++；/Zl 与后面的 /nodefaultlib 配合，禁止悄悄引入 CRT。  
REM -fno-builtin-strlen 是硬要求：新版 Clang 会把循环识别成 strlen 并生成 CRT 调用，  
REM 而本工程用 /nodefaultlib，链接期没有 strlen 可用，会直接报 undefined symbol。  
"%CLANG%" --driver-mode=cl --target=i686-pc-windows-msvc /nologo /c /O2 /GS- /GR- /Zl /W4 /EHs-c- -fno-builtin-strlen /Fo"build\BaldrForceBGMSE.obj" "src\BaldrForceBGMSE.cpp"
if errorlevel 1 goto :fail

REM 第 4 步：链接成 PE32 ASI；/timestamp:0 让相同输入得到稳定哈希。  
"%LLD%" /dll /machine:x86 /nodefaultlib /timestamp:0 /entry:DllMainCRTStartup@12 /def:BaldrForceBGMSE.def /out:"..\..\release\BaldrForceBGMSE.asi" /implib:"build\BaldrForceBGMSE.lib" "build\BaldrForceBGMSE.obj"
if errorlevel 1 goto :fail

REM 第 5 步：验证 PE32/i386、DLL、入口、空标准导入表和 InitializeASI 导出。  
python tools\verify_build.py "..\..\release\BaldrForceBGMSE.asi"
if errorlevel 1 goto :fail

REM 第 6 步：链接器偶尔会在 release 顺带生成导入库/exp/pdb，这些都不是玩家文件。  
if exist "..\..\release\BaldrForceBGMSE.lib" del /q "..\..\release\BaldrForceBGMSE.lib"
if exist "..\..\release\BaldrForceBGMSE.exp" del /q "..\..\release\BaldrForceBGMSE.exp"
if exist "..\..\release\BaldrForceBGMSE.pdb" del /q "..\..\release\BaldrForceBGMSE.pdb"

REM 第 7 步：把 templete\ 里的随包文件（INI 等）复制到 ASI 同目录，随 ASI 一起分发。  
if exist "templete" (
  echo [随包] 复制 templete 内容到 ..\..\release\  
  xcopy /y /q /i "templete\*" "..\..\release\" >nul
  if errorlevel 2 goto :fail
) else (
  echo [提示] templete 目录不存在；本次不复制随包文件。  
)

REM 第 8 步：删除中间文件与 Python 缓存，正式产物只留在包根 release。  
call :cleanup

echo [成功] 已生成 ..\..\release\BaldrForceBGMSE.asi  
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
