@echo off  
chcp 65001 >nul
setlocal EnableExtensions
cd /d "%~dp0"

REM ============================================================================  
REM BaldrForceCN Runtime 一键构建。  
REM 走纯 Clang/LLVM 路线：C 逻辑和 embedded_assets.S 都由 clang.exe 编译，再由  
REM lld-link.exe 链接成 PE32 ASI。不需要 Visual Studio 环境，也不需要 CRT。  
REM 正式产物只进入包根 release\；中间目录在构建结束后一律删除。  
REM ============================================================================  
set "OUT=%CD%\build"

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


REM 第 1 步：准备干净的临时目录与包根 release 目录。  
if exist "%OUT%" rmdir /s /q "%OUT%"
mkdir "%OUT%"
if not exist "..\..\release" mkdir "..\..\release"

echo [1/3] 编译 src\BaldrForceCN.c...  
"%CLANG%" --target=i686-pc-windows-msvc -O2 -ffreestanding -fno-builtin -fno-stack-protector -fno-exceptions -fno-asynchronous-unwind-tables -Wall -Wextra -Werror -c "%CD%\src\BaldrForceCN.c" -o "%OUT%\BaldrForceCN.obj"
if errorlevel 1 goto :fail

REM 第 2 步：编译内置资源。embedded_assets.S 用 .incbin 把 9 个差异资源原字节放进 ASI。  
REM 该文件里的 .incbin 是裸相对路径 assets\...，所以先切到 src：这样无论汇编器按  
REM “工作目录”还是“源文件所在目录”解析，结果都指向 src\assets\。  
echo [2/3] 编译 src\embedded_assets.S...  
pushd "%CD%\src"
"%CLANG%" --target=i686-pc-windows-msvc -c "embedded_assets.S" -o "%OUT%\embedded_assets.obj"
set "RC=%errorlevel%"
popd
if not "%RC%"=="0" goto :fail

echo [3/3] 链接 PE32 BaldrForceCN.asi...  
"%LLD%" /dll /machine:x86 /nodefaultlib /entry:DllMainCRTStartup /subsystem:windows /safeseh:no /opt:ref /opt:icf /export:InitializeASI /Brepro /implib:"%OUT%\BaldrForceCN.lib" /out:"..\..\release\BaldrForceCN.asi" "%OUT%\BaldrForceCN.obj" "%OUT%\embedded_assets.obj"
if errorlevel 1 goto :fail

REM 第 3 步：链接器偶尔会在 release 顺带生成导入库/exp/pdb，这些都不是玩家文件。  
if exist "..\..\release\BaldrForceCN.lib" del /q "..\..\release\BaldrForceCN.lib"
if exist "..\..\release\BaldrForceCN.exp" del /q "..\..\release\BaldrForceCN.exp"
if exist "..\..\release\BaldrForceCN.pdb" del /q "..\..\release\BaldrForceCN.pdb"

REM 第 4 步：Chapter.pac / Update.pac 不内嵌进 ASI，而是随 ASI 一起复制到包根 release。  
REM 它们放在 src\assets\ 只是源文件位置；embedded_assets.S 没有引用它们，所以内嵌内容不变。  
if not exist "src\assets\Chapter.pac" (
  echo [失败] 找不到 src\assets\Chapter.pac  
  goto :fail
)
if not exist "src\assets\Update.pac" (
  echo [失败] 找不到 src\assets\Update.pac  
  goto :fail
)
copy /y "src\assets\Chapter.pac" "..\..\release\Chapter.pac" >nul || goto :fail
copy /y "src\assets\Update.pac" "..\..\release\Update.pac" >nul || goto :fail
echo [随包] 已复制 Chapter.pac / Update.pac 到 ..\..\release\  

REM 第 5 步：把 templete\ 里的随包文件（INI 等）复制到 ASI 同目录，随 ASI 一起分发。  
if exist "templete" (
  echo [随包] 复制 templete 内容到 ..\..\release\  
  xcopy /y /q /i "templete\*" "..\..\release\" >nul
  if errorlevel 2 goto :fail
) else (
  echo [提示] templete 目录不存在；本次不复制随包文件。  
)

call :cleanup
echo [完成] 已生成 ..\..\release\BaldrForceCN.asi  
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
