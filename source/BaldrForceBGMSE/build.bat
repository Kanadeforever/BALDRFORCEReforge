@echo off  
chcp 65001 >nul  
setlocal  
cd /d "%~dp0"  
  
REM 第 1 步：重新解析两版 BGMInfo.DAT，生成 BFSE 30 条资源名/循环点静态表与中文对照 CSV。  
python tools\generate_bgm_table.py  
if errorlevel 1 goto :fail  
  
REM 第 2 步：准备临时编译目录和统一 release 目录。  
if exist build rmdir /s /q build  
mkdir build  
if not exist ..\release mkdir ..\release  
  
REM 第 3 步：编译 32 位 x86 C++；/Zl 与后面的 /nodefaultlib 配合，禁止悄悄引入 CRT。  
clang-cl --target=i686-pc-windows-msvc /nologo /c /O2 /GS- /GR- /Zl /W4 /EHs-c- /Fo"build\BaldrForceBGMSE.obj" "src\BaldrForceBGMSE.cpp"  
if errorlevel 1 goto :fail  
  
REM 第 4 步：链接成 PE32 ASI；/timestamp:0 让相同输入得到稳定哈希。  
lld-link /dll /machine:x86 /nodefaultlib /timestamp:0 /entry:DllMainCRTStartup@12 /def:BaldrForceBGMSE.def /out:"..\release\BaldrForceBGMSE.asi" /implib:"build\BaldrForceBGMSE.lib" "build\BaldrForceBGMSE.obj"  
if errorlevel 1 goto :fail  
  
REM 第 5 步：验证 PE32/i386、DLL、入口、空标准导入表和 InitializeASI 导出。  
python tools\verify_build.py "..\release\BaldrForceBGMSE.asi"  
if errorlevel 1 goto :fail  
  
REM 第 6 步：删除中间文件与 Python 缓存，正式产物只留在 release。  
if exist build rmdir /s /q build  
if exist tools\__pycache__ rmdir /s /q tools\__pycache__  
  
echo [成功] 已生成 BaldrForceBGMSE v0.1-test1。  
exit /b 0  
  
:fail  
echo [失败] 构建中止，请查看上方第一条报错。  
exit /b 1  
