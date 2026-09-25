# INI 设置说明

> 适用版本：`BaldrForceCN Runtime v1.0.4-crashfix1`

`BaldrForceCN.ini` 必须与 `BaldrForceCN.asi` 放在同一目录。文件不存在时，程序会自动使用默认值，不影响汉化加载。

## 完整配置

```ini
[BaldrForceCN]
FontFace=
EnableLog=1
```

本版故意只允许这两个选项。过去曾经出现过 `EnableFontHook / ForceCharset / ContextBytes` 等内部参数，但这些已经证明是汉化架构的一部分，不再允许通过 INI 改动。

## FontFace

`FontFace=` 留空时，Runtime 保持已经实机验证过的默认规则：小字号按宋体方向，大字号按黑体方向；不会因为重新加入 INI 而改变 v1.0.1 的默认视觉效果。

要换字体时填写 Windows 已安装的字体族名称，例如：

```ini
FontFace=Microsoft YaHei
```

也可以填写中文字体名。随包 INI 使用 UTF-16 LE 保存，Windows 的 `GetPrivateProfileStringW` 可以直接读取 Unicode 字体名；如果用第三方编辑器改成其它编码，建议使用英文字体族名以避免系统 ANSI 代码页造成字体名乱码。

自定义字体会作用到两处：游戏通过 `CreateFontIndirectW` 创建的字体；Runtime 自己用于 GBK `GetGlyphOutlineW` 的字形 DC。它不会改变游戏贴图字体或 HELLMODE 图片资源。

如果指定字体没有某个汉字，本版也不会因此退回原版 CP932 renderer；取字失败会走安全 fallback/空白字形，优先保证不崩溃。

## EnableLog

```ini
EnableLog=1
```

表示创建 `BaldrForceCN.log`，每次启动覆盖旧日志。日志在 ASI 同目录，UTF-8 BOM。

```ini
EnableLog=0
```

表示本次运行不创建或覆盖日志。已有旧日志文件不会被主动删除，因此判断是否关闭日志时不要只看目录里是否还存在旧文件。

排查崩溃、乱码、资源漏项时必须改回 `1`，因为增强 VEH、脚本事件、字形 fallback 等诊断都写入同一日志。
