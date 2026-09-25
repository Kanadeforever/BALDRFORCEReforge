@echo off  
chcp 65001 >nul
setlocal EnableExtensions

REM ============================================================================  
REM BALDR FORCE 全模块总编译脚本。  
REM 依次调用 source\ 下四个模块自己的 build.bat，正式产物统一进入包根 release\。  
REM 各模块脚本负责清理自己的临时文件，本脚本不产生任何中间文件。  
REM
REM 用法：  
REM   build_all.bat             编译全部模块  
REM   build_all.bat BGMSE       只编译 BGMSE / CN / GAMEPAD / VOICESE 之一  
REM ============================================================================  

set "ROOT=%~dp0"
cd /d "%ROOT%" || exit /b 1

set "ONLY=%~1"
set "FAILED=0"
set "BUILT=0"

if not exist "%ROOT%release" mkdir "%ROOT%release"

echo ============================================================  
echo [环境自检]  
call :probe clang.exe
call :probe lld-link.exe
call :probe python.exe
echo ============================================================  

call :run "BaldrForceBGMSE"   "%ROOT%source\BaldrForceBGMSE\build.bat"      "BGMSE"
call :run "BaldrForceCN"      "%ROOT%source\BaldrForceCN_Runtime\build.bat" "CN"
call :run "BaldrForceGamepad" "%ROOT%source\BaldrForceGamepad\build.bat"    "GAMEPAD"
call :run "BaldrForceVoiceSE" "%ROOT%source\BaldrForceVoiceSE\build.bat"    "VOICESE"

echo.
echo ============================================================  
if "%FAILED%"=="0" (
  echo [全部通过] 成功编译 %BUILT% 个模块。  
) else (
  echo [存在失败] 成功 %BUILT% 个，失败 %FAILED% 个。请向上翻看第一条报错。  
)
echo [产物目录] %ROOT%release  
dir /b "%ROOT%release"
echo ============================================================  
if not "%FAILED%"=="0" exit /b 1
exit /b 0

REM ----------------------------------------------------------------------------  
REM :run "显示名" "脚本绝对路径" "子命令别名"  
REM 注意：模块脚本会 cd 到自己的目录，所以每次调用前后都要切回包根，  
REM 并且必须用绝对路径调用，否则第二次调用会找不到脚本。  
REM ----------------------------------------------------------------------------  
:run
set "M_NAME=%~1"
set "M_SCRIPT=%~2"
set "M_ALIAS=%~3"

if not defined ONLY goto :run_go
if /i "%ONLY%"=="%M_ALIAS%" goto :run_go
echo [跳过] %M_NAME%（本次只编译 %ONLY%）  
exit /b 0

:run_go
cd /d "%ROOT%"

if not exist "%M_SCRIPT%" (
  echo [失败] 找不到 %M_SCRIPT%  
  set /a FAILED+=1
  exit /b 0
)

echo.
echo ============================================================  
echo [开始] %M_NAME%  
echo ============================================================  
call "%M_SCRIPT%"
set "RC=%errorlevel%"
cd /d "%ROOT%"

if "%RC%"=="0" (
  echo [通过] %M_NAME%  
  set /a BUILT+=1
) else (
  echo [失败] %M_NAME% 返回 %RC%  
  set /a FAILED+=1
)
exit /b 0

REM ----------------------------------------------------------------------------  
REM :probe 可执行名 —— 依次检查 PATH、MSYS2 clang64、LLVM 官方默认目录。  
REM ----------------------------------------------------------------------------  
:probe
set "FOUND="
for %%p in (%~1) do if not defined FOUND set "FOUND=%%~$PATH:p"
if defined FOUND (
  echo   [PATH] %~1  
  exit /b 0
)
if exist "C:\msys64\clang64\bin\%~1" (
  echo   [备用] %~1  位于 C:\msys64\clang64\bin  
  exit /b 0
)
if exist "C:\Program Files\LLVM\bin\%~1" (
  echo   [备用] %~1  位于 C:\Program Files\LLVM\bin  
  exit /b 0
)
echo   [缺失] %~1  --  依赖它的模块会构建失败  
exit /b 0
