@echo off  
REM ============================================================================  
REM BaldrForceGamepad 一键构建脚本。  
REM 这个脚本只做四件事：找到 Clang/lld、编译 32 位对象、链接 ASI、复制运行配置。  
REM 所有临时文件都放在模块内 _build，正式玩家文件只进入包根 release\。  
REM ============================================================================  
setlocal EnableExtensions

REM 把当前目录切换到这个 build.bat 自己所在的目录。  
REM 这样用户无论从哪个目录双击脚本，相对路径都不会跑错。  
cd /d "%~dp0" || goto :fail

REM 切换控制台为 UTF-8，确保下面的中文提示在现代 Windows 终端里正常显示。  
chcp 65001 >nul

REM ----------------------------------------------------------------------------  
REM 第一步：寻找工具链。先查 PATH，再查本机已知的 LLVM 安装位置。  
REM MSYS2 的 clang64 自带 clang.exe 和 lld-link.exe，是常见的无 PATH 安装位置。  
REM ----------------------------------------------------------------------------  
set "CLANG="
for %%p in (clang.exe) do if not defined CLANG set "CLANG=%%~$PATH:p"
if not defined CLANG if exist "C:\msys64\clang64\bin\clang.exe" set "CLANG=C:\msys64\clang64\bin\clang.exe"
if not defined CLANG if exist "C:\Program Files\LLVM\bin\clang.exe" set "CLANG=C:\Program Files\LLVM\bin\clang.exe"
if not defined CLANG (
    echo [错误] 找不到 clang.exe，请安装 LLVM/Clang，或把它加入 PATH。  
    exit /b 1
)

set "LLD="
for %%p in (lld-link.exe) do if not defined LLD set "LLD=%%~$PATH:p"
if not defined LLD if exist "C:\msys64\clang64\bin\lld-link.exe" set "LLD=C:\msys64\clang64\bin\lld-link.exe"
if not defined LLD if exist "C:\Program Files\LLVM\bin\lld-link.exe" set "LLD=C:\Program Files\LLVM\bin\lld-link.exe"
if not defined LLD (
    echo [错误] 找不到 lld-link.exe，请安装 LLVM/LLD，或把它加入 PATH。  
    exit /b 1
)

REM clang.exe 依赖同目录的 DLL，把它的目录放到 PATH 最前面最稳。  
for %%d in ("%CLANG%") do set "PATH=%%~dpd;%PATH%"
echo [工具链] clang    = %CLANG%  
echo [工具链] lld-link = %LLD%  

REM 每次构建都删除旧 _build，避免旧 .obj 混入新版本。  
if exist _build rmdir /s /q _build
mkdir _build >nul 2>nul

REM release 是最终玩家产物目录（在包根）；如果还不存在就创建。  
if not exist "..\..\release" mkdir "..\..\release" >nul 2>nul

echo [1/4] 编译 x86 对象文件...  
REM -target i686-pc-windows-msvc 和 -m32 明确要求生成 32 位 Windows 对象。  
REM -ffreestanding / -fno-builtin 表示源码自己处理底层运行环境，不依赖普通 C 运行库。  
REM -Wall -Wextra 用来尽可能提前暴露危险的类型或未使用代码问题。  
"%CLANG%" -target i686-pc-windows-msvc -m32 -O2 -ffreestanding -fno-builtin -fno-stack-protector -fno-ident -Wall -Wextra -c "src\BaldrForceGamepad.c" -o "_build\BaldrForceGamepad.obj" || goto :fail

echo [2/4] 链接 PE32 ASI...  
REM /nodefaultlib 禁止自动链接 MSVC CRT，让发布的 ASI 不要求额外 VC 运行库。  
REM /entry:DllMain 使用源码自己的最小 DLL 入口。  
REM /machine:x86 再次从链接阶段锁定 32 位，避免误生成 x64。  
REM /timestamp:0 固定 PE 时间戳，让相同源码与工具链重复构建得到稳定哈希。  
"%LLD%" /dll /machine:x86 /nodefaultlib /timestamp:0 /entry:DllMain /subsystem:windows,6.0 /out:"..\..\release\BaldrForceGamepad.asi" "_build\BaldrForceGamepad.obj" || goto :fail

REM lld-link 有时会顺便生成导入库、exp 或 pdb。  
REM 这些都不是玩家运行需要的文件，所以发布目录里统一删除。  
if exist "..\..\release\BaldrForceGamepad.lib" del /q "..\..\release\BaldrForceGamepad.lib"
if exist "..\..\release\BaldrForceGamepad.exp" del /q "..\..\release\BaldrForceGamepad.exp"
if exist "..\..\release\BaldrForceGamepad.pdb" del /q "..\..\release\BaldrForceGamepad.pdb"

echo [3/4] 复制随包文件...  
REM INI 必须和 ASI 同目录。随包文件模板放在 templete\，每次构建都覆盖到包根 release。  
if exist "templete" (
    echo [随包] 复制 templete 内容到 ..\..\release\  
    xcopy /y /q /i "templete\*" "..\..\release\" >nul
    if errorlevel 2 goto :fail
) else (
    echo [提示] templete 目录不存在；本次只生成 ASI。  
)

REM SDL3 是运行时动态加载的第三方 DLL，不参与编译。  
REM 如果开发者已经把核验过的 x86 SDL3.dll 放到固定目录，就顺便复制；否则只给提示。  
if exist "third_party\SDL3\SDL3.dll" (
    copy /y "third_party\SDL3\SDL3.dll" "..\..\release\SDL3.dll" >nul || goto :fail
    echo [通过] 已把 third_party\SDL3\SDL3.dll 复制到 release。  
) else (
    echo [提示] third_party\SDL3\SDL3.dll 不存在；本次只生成 ASI 和 INI。  
    echo [提示] 实机测试现代手柄前，请把官方 32 位 x86 SDL3.dll 放到游戏目录。  
)

echo [4/4] 构建完成。  
echo [输出] ..\..\release\BaldrForceGamepad.asi  
echo [输出] ..\..\release\BaldrForceGamepad.ini  
call :cleanup
exit /b 0

:cleanup
if exist _build rmdir /s /q _build
exit /b 0

:fail
call :cleanup
echo [失败] 构建中止，请查看上方第一条报错。  
exit /b 1
