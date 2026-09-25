@echo off  
chcp 65001 >nul  
setlocal  
cd /d "%~dp0"  
  
REM 第 1 步：从当前确认映射与 BFE 脚本重新生成正式版静态 VoiceMap。  
REM 本测试版沿用正式生成器：只更新 src\VoiceMap.generated.h，不生成运行时覆盖率或审计 CSV。  
python tools\generate_voice_table.py  
if errorlevel 1 goto :fail  
  
REM 第 2 步：建立临时编译目录与统一 release 目录。  
if not exist build mkdir build  
if not exist ..\release mkdir ..\release  
  
REM 清理旧测试阶段曾经放在 release 里的 CSV，保证本测试包 release 目录只有运行时 ASI。  
if exist "..\release\剧情语音映射审计.csv" del /q "..\release\剧情语音映射审计.csv"  
if exist "..\release\全游戏剧情语音覆盖清单.csv" del /q "..\release\全游戏剧情语音覆盖清单.csv"  
if exist "..\release\待人工复核_主线45条.csv" del /q "..\release\待人工复核_主线45条.csv"  
if exist "..\release\待人工复核_全部.csv" del /q "..\release\待人工复核_全部.csv"  
  
REM 第 3 步：编译 32 位 x86 C++。/nodefaultlib 路线要求源码不能偷偷依赖 CRT。  
REM /O2 让每次剧情文本触发时的 FNV-1a 与二分查找保持足够轻量。  
clang-cl --target=i686-pc-windows-msvc /nologo /c /O2 /GS- /GR- /Zl /W4 /EHs-c- /Fo"build\BaldrForceVoiceSE.obj" "src\BaldrForceVoiceSE.cpp"  
if errorlevel 1 goto :fail  
  
REM 第 4 步：用 lld-link 生成 PE32 DLL，并直接使用 .asi 扩展名。  
REM /nodefaultlib 是硬约束；如果源码意外依赖 CRT，链接会失败而不是悄悄增加运行库要求。  
REM /timestamp:0 固定 PE 时间戳，使相同源码与工具链重复构建时得到稳定哈希。  
lld-link /dll /machine:x86 /nodefaultlib /timestamp:0 /entry:DllMainCRTStartup@12 /def:BaldrForceVoiceSE.def /out:"..\release\BaldrForceVoiceSE.asi" /implib:"build\BaldrForceVoiceSE.lib" "build\BaldrForceVoiceSE.obj"  
if errorlevel 1 goto :fail  
  
REM 第 5 步：静态验证最终文件确实是 i386 / PE32 / DLL、无标准导入表，并导出两个 ASI 入口名。  
python tools\verify_build.py "..\release\BaldrForceVoiceSE.asi"  
if errorlevel 1 goto :fail  
  
REM 第 6 步：删除临时目标文件和 Python 缓存；正式交付物始终只留在 release。  
if exist build rmdir /s /q build  
if exist tools\__pycache__ rmdir /s /q tools\__pycache__  
  
echo [成功] 已生成 v0.1.1-test1 release\BaldrForceVoiceSE.asi  
exit /b 0  
  
:fail  
echo [失败] 构建中止，请查看上方第一条报错。  
exit /b 1  
