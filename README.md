# BALDRFORCEReforge

BALDR FORCE EXE （2003 原版 clean Win32/x86 EXE）的 MOD 合集。四个模块各自是独立的 ASI 插件，
可以单独加载，也可以组合加载。

| 模块 | 版本 | 作用 |
|---|---|---|
| BaldrForceCN | v1.0.4-crashfix1 | 中文汉化运行时（GBK renderer + BFET 文本映射 + 原汉化资源提取覆盖） |
| BaldrForceGamepad | v0.1-test7 | 用 SDL3 提供现代手柄支持 |
| BaldrForceVoiceSE | v0.1.1-test1 | 把 BFSE 的剧情语音接进 BFE |
| BaldrForceBGMSE | v0.1-test1 | 把 BFSE 的新版 BGM 移植进 BFE |

四个模块都以同一个目标程序为基线：

    BaldrForce.exe
    987,136 bytes
    SHA256 5b65ecb1512b0cbf72cacdb5e981aa04f9c569698c6cab2f8877abf2d2cd42c5

## 一键构建

在项目根用 **cmd** 运行：

    build_all.bat

只编单个模块：

    build_all.bat CN

可选参数：BGMSE / CN / GAMEPAD / VOICESE。

依赖只有 clang 与 lld-link（本机位于 C:\msys64\clang64\bin，脚本自动探测）
以及 Python 3。不需要 Visual Studio，全部构建走 cmd 批处理。

所有产物统一进入项目根的 release 目录。

## 目录

    build_all.bat          总编译脚本
    release\               全部编译产物，固定在项目根
    docs\                  文档，按模块分目录，入口见 docs/README.md
    source\<模块>\         build.bat / src / tools / data / templete

每个模块的 templete 目录存放需要随 ASI 一起分发的文件（INI 等），构建时自动复制到 release。

## 文档

入口见 [docs/README.md](docs/README.md)。每个模块三篇：完整接档说明、构建与发布、更新记录。

## 许可

MIT，见 [LICENSE](LICENSE)。
