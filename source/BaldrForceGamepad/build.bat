@echo off
REM ============================================================================  
REM BaldrForceGamepad 一键构建脚本。  
REM 这个脚本只做四件事：找到 Clang/lld、编译 32 位对象、链接 ASI、复制运行配置。  
REM 所有临时文件都放在 source\_build，正式玩家文件只进入包根 release\。  
REM ============================================================================  
setlocal EnableExtensions

REM 把当前目录切换到这个 build.bat 自己所在的 source 目录。  
REM 这样用户无论从哪个目录双击脚本，相对路径都不会跑错。  
cd /d "%~dp0" || exit /b 1

REM 切换控制台为 UTF-8，确保下面的中文提示在现代 Windows 终端里正常显示。  
chcp 65001 >nul

REM ----------------------------------------------------------------------------  
REM 第一步：寻找 clang.exe。  
REM 先查 PATH；如果 PATH 里没有，再尝试 LLVM 官方安装器常见的默认目录。  
REM ----------------------------------------------------------------------------  
set "CLANG=clang.exe"
where clang.exe >nul 2>nul
if errorlevel 1 (
    set "CLANG=C:\Program Files\LLVM\bin\clang.exe"
    if not exist "%CLANG%" (
        echo [错误] 找不到 clang.exe，请安装 LLVM/Clang，或把它加入 PATH。  
        exit /b 1
    )
)

REM ----------------------------------------------------------------------------  
REM 第二步：寻找 lld-link.exe。  
REM lld-link 是 LLVM 的 Windows PE 链接器，用来把 .obj 链接成 32 位 .asi DLL。  
REM ----------------------------------------------------------------------------  
set "LLD=lld-link.exe"
where lld-link.exe >nul 2>nul
if errorlevel 1 (
    set "LLD=C:\Program Files\LLVM\bin\lld-link.exe"
    if not exist "%LLD%" (
        echo [错误] 找不到 lld-link.exe，请安装 LLVM/Clang，或把它加入 PATH。  
        exit /b 1
    )
)

REM 每次构建都删除旧 _build，避免旧 .obj 混入新版本。  
if exist _build rmdir /s /q _build
mkdir _build >nul 2>nul

REM release 是最终玩家产物目录；如果还不存在就创建。  
if not exist "..\release" mkdir "..\release" >nul 2>nul

echo [1/4] 编译 x86 对象文件...  
REM -target i686-pc-windows-msvc 和 -m32 明确要求生成 32 位 Windows 对象。  
REM -ffreestanding / -fno-builtin 表示源码自己处理底层运行环境，不依赖普通 C 运行库。  
REM -Wall -Wextra 用来尽可能提前暴露危险的类型或未使用代码问题。  
"%CLANG%" -target i686-pc-windows-msvc -m32 -O2 -ffreestanding -fno-builtin -fno-stack-protector -fno-ident -Wall -Wextra -c "src\BaldrForceGamepad.c" -o "_build\BaldrForceGamepad.obj" || exit /b 1

echo [2/4] 链接 PE32 ASI...  
REM /nodefaultlib 禁止自动链接 MSVC CRT，让发布的 ASI 不要求额外 VC 运行库。  
REM /entry:DllMain 使用源码自己的最小 DLL 入口。  
REM /machine:x86 再次从链接阶段锁定 32 位，避免误生成 x64。  
"%LLD%" /dll /machine:x86 /nodefaultlib /entry:DllMain /subsystem:windows,6.0 /out:"..\release\BaldrForceGamepad.asi" "_build\BaldrForceGamepad.obj" || exit /b 1

REM lld-link 有时会顺便生成导入库、exp 或 pdb。  
REM 这些都不是玩家运行需要的文件，所以发布目录里统一删除。  
if exist "..\release\BaldrForceGamepad.lib" del /q "..\release\BaldrForceGamepad.lib"
if exist "..\release\BaldrForceGamepad.exp" del /q "..\release\BaldrForceGamepad.exp"
if exist "..\release\BaldrForceGamepad.pdb" del /q "..\release\BaldrForceGamepad.pdb"

echo [3/4] 复制配置...  
REM INI 必须和 ASI 同目录，所以每次构建都把 source\config 的最新版本覆盖到 release。  
copy /y "config\BaldrForceGamepad.ini" "..\release\BaldrForceGamepad.ini" >nul || exit /b 1

REM SDL3 是运行时动态加载的第三方 DLL，不参与编译。  
REM 如果开发者已经把核验过的 x86 SDL3.dll 放到固定目录，就顺便复制；否则只给提示。  
if exist "third_party\SDL3\SDL3.dll" (
    copy /y "third_party\SDL3\SDL3.dll" "..\release\SDL3.dll" >nul || exit /b 1
    echo [通过] 已把 third_party\SDL3\SDL3.dll 复制到 release。  
) else (
    echo [提示] source\third_party\SDL3\SDL3.dll 不存在；本次只生成 ASI 和 INI。  
    echo [提示] 实机测试现代手柄前，请把官方 32 位 x86 SDL3.dll 放到游戏目录。  
)

echo [4/4] 构建完成。  
echo [输出] ..\release\BaldrForceGamepad.asi  
echo [输出] ..\release\BaldrForceGamepad.ini  
exit /b 0
