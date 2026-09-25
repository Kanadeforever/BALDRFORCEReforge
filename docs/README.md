# 文档导航

每个模块三篇文档，职责不重叠：

| 文档 | 内容 |
|---|---|
| 完整接档说明.md | 项目目标、逆向结论、地址与结构、已成功方案、已失败与已证伪方案、运行架构、使用与配置、测试要点、当前阻塞项 |
| 构建与发布.md | 目录结构、依赖与工具链、一键构建、构建产物与静态验证、工具说明、发布清单、构建约定 |
| 更新记录.md | 版本历史与关键变更 |

- [BaldrForceBGMSE](BaldrForceBGMSE/完整接档说明.md) —— 移植 BFSE 新版 BGM
- [BaldrForceCN_Runtime](BaldrForceCN_Runtime/完整接档说明.md) —— 中文汉化运行时
- [BaldrForceGamepad](BaldrForceGamepad/完整接档说明.md) —— SDL3 现代手柄支持
- [BaldrForceVoiceSE](BaldrForceVoiceSE/完整接档说明.md) —— 移植 BFSE 剧情语音

## 全局规范

### 目录

    build_all.bat                    总编译脚本
    release\                         全部编译产物，固定在项目根
    docs\<模块>\                    文档
    source\<模块>\                   build.bat / src / tools / data（有随包文件的模块另有 templete）

release 永远位于项目根，任何模块的构建产物都写到这里，不在模块内部另建 release。

### 构建

- 总入口 build_all.bat，在项目根用 cmd 运行；后面跟 BGMSE / CN / GAMEPAD / VOICESE 可只编一个模块
- 模块脚本先切到自身目录，再把产物写到上两级的 release
- 构建脚本会自行清理 build、_build、__pycache__、*.pyc，并从 release 删除误生成的 .lib/.exp/.pdb
- 四个模块的构建都是确定性的：相同源码与工具链重复构建得到相同 SHA256

### 工具链

- clang 22.1.7 与 lld-link，本机位于 C:\msys64\clang64\bin
- 脚本探测顺序：PATH、C:\msys64\clang64\bin、C:\Program Files\LLVM\bin；
  并把 clang 所在目录插到 PATH 最前（clang 依赖同目录的 DLL）
- MSYS2 的 clang64 不提供 clang-cl.exe，因此 BGMSE 与 VoiceSE 使用 clang.exe 的 cl 驱动模式
- BaldrForceCN 走纯 clang + lld-link 路线，不需要 Visual Studio 或 MSVC
- 四个模块统一 /nodefaultlib、PE32/i386、无 CRT

### 随包文件

需要随 ASI 一起分发的文件放在模块的 templete 目录，构建时复制到 release。
目前 BGMSE / CN / Gamepad 三个模块各有 templete（内含该模块的 INI）；VoiceSE 没有随包文件，
因此没有该目录，它的 release 里只有 ASI 一个文件。
BaldrForceCN 另外会把 Chapter.pac 与 Update.pac 复制到 release（这两个文件不内嵌进 ASI）。

### 文档与脚本写作

- .md 使用 LF 与 UTF-8 无 BOM
- .bat 使用 CRLF 与 UTF-8 无 BOM
- .bat 中 REM 与 ECHO 后面有文字的行，行尾统一补两个半角空格，
  避免行尾中日文字符与换行相邻时被 cmd 误解析

## 项目级已知问题

- BaldrForceCN 的 tools 下四个校验脚本（verify_static.py、verify_release_package.py、
  audit_bfet_length_semantics.py、audit_bfet_backlog_risks.py）仍在使用目录重组前的旧路径，
  当前无法运行；其中引用的 evidence 目录也不存在。主构建流程不依赖它们。
- .gitignore 排除了 release 目录，以及 BaldrForceVoiceSE 的 data 下 BFE 与 BFSE 目录。
  因此直接克隆后无法构建 VoiceSE，且仓库里没有编译产物。
- BaldrForceGamepad 需要手工放置 third_party 下的 x86 SDL3.dll，否则 release 中不会有 SDL3.dll。
