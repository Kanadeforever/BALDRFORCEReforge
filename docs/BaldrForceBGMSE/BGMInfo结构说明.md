# BGMInfo 结构说明

## 1. 文件尺寸

两版 `BGMInfo.DAT` 都是：

```text
2049 bytes
```

分解为：

```text
30 × 0x44 = 2040 bytes
+ 9 bytes
```

最后 9 字节两版完全相同：

```hex
61 76 00 61 76 00 61 76 00
```

即：

```text
av\0av\0av\0
```

BFE 读取 BGMInfo 的循环从 `0x00721010` 开始，每次读取 `0x44`，结束地址是 `0x00721808`，正好只读取 30 条记录。因此这 9 字节表尾不属于 30 条运行时 BGMInfo，本插件不修改它。

## 2. 每条 0x44 结构

当前确认：

```cpp
struct BGMInfoEntry
{
    char  resourceName[0x40]; // +0x00，CP932，NUL 结尾
    int32 loopStartCs;        // +0x40，1/100 秒；-1 = 非循环
};
```

`resourceName` 是 PAC 内真正 WAV 资源名。

例如 BFSE：

```text
#05  05：the real world.wav
#13  13：Red Zone.wav
#26  26：irony of fate.wav
#27  27：Leviathan.wav
```

## 3. 循环点证据

BFE BGM 播放控制函数中，曲目编号先换算成：

```text
0x00721010 + index * 0x44
```

随后直接读取：

```text
record + 0x40
```

### 值小于 0

调用：

```text
0x004AD980
```

该函数设置声音对象：

```text
object + 0x30 = 0
```

也就是不启用循环回卷。

### 值大于等于 0

调用：

```text
0x004ADA10
```

该函数：

```text
object + 0x30 = 1
object + 0x34 = BGMInfo +0x40 的值
```

当流读到文件尾部时，BFE 在：

```text
0x004AD3F7
```

检查：

```text
object + 0x30 == 1
```

成立后在：

```text
0x004AD3FC
```

读取：

```text
object + 0x34
```

并计算：

```text
loopValue * bytesPerSecond / 100
```

作为重新读取 WAV 数据的字节位置。

机器码使用常数：

```text
0x51EB851F
```

配合乘法和右移实现整数除以 100。

因此 `loopValue` 的单位就是 **1/100 秒**。

例如：

```text
1548 -> 15.48 秒
1600 -> 16.00 秒
3999 -> 39.99 秒
5458 -> 54.58 秒
-1   -> 不循环
```

## 4. 为什么必须移植 BFSE 循环点

BFE/BFSE 的很多循环点不同，例如：

```text
编号   BFE      BFSE
#02      0       1548
#06   1600       3999
#15      0       5458
#20   2494        922
#26   4914       4909
#27   3454       4140
#29     -1         -1
#30     -1         -1
```

如果只把 BFSE WAV 放进去、仍沿用 BFE 循环点，绝大部分曲目会在错误时间回卷，甚至把 BFSE 的前奏/衔接结构切错。

所以本插件把“文件名 + 循环点”视为同一首 BFSE BGM 的配套元数据。

## 5. 为什么运行时只改 +0x40

虽然 BFSE 的完整 0x44 记录都已经解析，但插件不把整条记录写回 BFE 表。

原因是：

- 前 0x40 是资源名字；
- 汉化版可能还存在 BGM 名称显示；
- 真正加载 BGM 时，`0x004AD7F0` 只把传入记录起点当成 NUL 结尾字符串使用；
- 循环控制则由播放函数稍后从 BFE 表的 +0x40 独立读取。

因此最稳方案是：

```text
BFE 表：保留原名字，只换 BFSE loopStartCs
资源加载瞬间：临时把字符串参数换成 BFSE resourceName
```

这样对汉化版影响最小。
