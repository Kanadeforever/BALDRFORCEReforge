// BaldrForceVoiceSE.cpp
//
// 《BALDR FORCE EXE》2003 → Standard Edition 主角剧情语音运行时移植插件（v0.1.1-test1 前后20条定位测试版）。
//
// 这个文件故意写了非常详细的行内注释：
//   1. 游戏原来怎样读 Voice1/Voice2；
//   2. 我们怎样在不改磁盘脚本的情况下挂入 Voice3.pac；
//   3. 我们怎样只改游戏已经复制到栈上的临时文本；
//   4. 为什么读档、跳转、回看不会让“第几句”计数错位。
//
// 重要原则：
//   - 不修改任何 .bin；
//   - 不重新打包 Chapter.pac；
//   - 不自己播放 WAV；
//   - 不自己实现 PAC 读取器；
//   - 尽量让原游戏自己的资源系统、@v 控制码解释器和声音系统完成工作。

// 这里不包含 windows.h。
// 原因是正式发行版需要生成一个“无 CRT、无导入表”的轻量 Win32 ASI，
// 这样不会依赖用户机器上的某个 VC Runtime，也方便与旧游戏环境共存。
// 因为不用 windows.h，所以后面只手工定义本文件真正需要的几个基本类型和 API 函数类型。

typedef unsigned char  u8;
typedef unsigned short u16;
typedef unsigned int   u32;
typedef signed int     s32;

typedef void* Handle;

// -----------------------------------------------------------------------------
// 自动生成的剧情语音映射表
// -----------------------------------------------------------------------------
//
// 这个头文件不是手工维护的。
// source/tools/generate_voice_table.py 会读取：
//   - BFE 原脚本；
//   - 已审核的 BFE -> BFSE VoiceTag 映射；
// 然后把每条语音转换为：
//
//   脚本执行流 FNV-1a 哈希 + 0x000B 记录在执行流中的静态字节偏移 -> VoiceTag
//
// 运行时完全不需要模糊比较中文/日文文本。
#include "VoiceMap.generated.h"

// -----------------------------------------------------------------------------
// 本版本只支持已经分析过的 2003 原版 EXE。
// -----------------------------------------------------------------------------
//
// PE 默认 ImageBase 是 0x00400000。
// 下面保存的是 RVA（相对模块基址），而不是把绝对 VA 写死。
// 这样即使系统把主 EXE 整体搬到别的基址，我们仍可以用：
//
//   实际模块基址 + RVA
//
// 找到同一条指令。
static const u32 kRvaScriptCallSite = 0x000AB5D2u; // 0x004AB5D2：0x0B handler 中的 CALL 0x004ACC20
static const u32 kRvaScriptCopy     = 0x000ACC20u; // 0x004ACC20：从当前脚本流复制字符串的原函数
static const u32 kRvaPacCallSite    = 0x000612BBu; // 0x004612BB：资源初始化循环中的 CALL 0x004AE330
static const u32 kRvaOpenPac        = 0x000AE330u; // 0x004AE330：原游戏自己的 PAC 打开/挂载函数

// 原版文本解释器里的 @v 处理入口。
// test1 已经通过实机证明：数字 VoiceTag（例如 500000）可以直接由原版 @v 解释器播放；
// 但 SE 新增的 TR0001 这类字母 VoiceTag 不行。
// 原版 @v 只读取十进制数字，SE 则把 @v 扩展成了“6 字节资源键”，因此 test2 只在这里
// 增加一个非常小的兼容分支：遇到 @vTRxxxx 时按 SE 的规则构造 TRxxxx.wav，
// 然后重新回到原版既有的语音播放流程。
static const u32 kRvaVoiceCommandEntry       = 0x0009EA40u; // 0x0049EA40：原版 @v 参数解析入口
static const u32 kRvaVoiceNumericContinue    = 0x0009EA46u; // 0x0049EA46：原版数字 VoiceTag 继续点
static const u32 kRvaVoiceAfterAvailability  = 0x0009EAF9u; // 0x0049EAF9：跳过角色语音可用性判断后继续

// 0x0B handler 在栈上给文本本体预留 0x1000 字节。
// 长度字段位于文本地址前 4 字节。
// 我们只在这块临时缓冲里前插 @v，不扩大这块槽，也不碰源 BIN。
static const u32 kTemporaryTextCapacity = 0x1000u;

// -----------------------------------------------------------------------------
// Windows API 的最小函数类型定义
// -----------------------------------------------------------------------------
//
// 正式发行版已经删除测试日志和运行时 CSV，因此这里只保留“安装内存补丁”真正需要的两个 API。
// __stdcall 是 32 位 Windows API 常用调用约定：参数从右到左压栈，返回时由被调用者清理参数。
typedef int (__stdcall *VirtualProtectFn)(void* address, u32 size, u32 newProtect, u32* oldProtect);
typedef int (__stdcall *FlushInstructionCacheFn)(Handle process, const void* address, u32 size);

// VirtualProtect 用的 PAGE_EXECUTE_READWRITE。
// 安装 Hook 前临时把目标机器码页改成可写；写完后会恢复原保护属性。
static const u32 kPageExecuteReadWrite = 0x40u;

// -----------------------------------------------------------------------------
// 全局运行状态
// -----------------------------------------------------------------------------
//
// 这里只保存真正影响功能的少量状态。
// 正式版不再维护日志计数、覆盖率数组、CSV 路径等测试数据。
static VirtualProtectFn gVirtualProtect = 0;
static FlushInstructionCacheFn gFlushInstructionCache = 0;
static int gApisResolved = 0;
static int gInitialized = 0;

// Voice3 只应在原版成功挂载 Voice2 后追加一次。
// 资源初始化循环里即使未来重复看到 Voice2，这个标志也能避免重复挂载。
static int gVoice3Attempted = 0;

// 裸汇编 TR 兼容分支不能把 EXE 绝对地址写死。
// 初始化时根据主 EXE 实际基址计算两个返回位置，再由汇编间接跳转。
static u8* gVoiceNumericContinue = 0;
static u8* gVoiceAfterAvailability = 0;

// -----------------------------------------------------------------------------
// 原游戏函数的函数指针类型
// -----------------------------------------------------------------------------
//
// 原函数使用 __thiscall：
//   ECX = this 指针
//   其余参数在栈上
//
// 我们的 Hook 用 __fastcall：
//   ECX = 第一个参数（正好继续接 this）
//   EDX = 第二个参数（我们不用）
//   后续参数仍在栈上
//
// 这样可以非常自然地接住原 __thiscall CALL，而不用写裸汇编桥。
typedef int (__thiscall *ScriptCopyFn)(void* self, char* destination, u32 length);
typedef int (__thiscall *OpenPacFn)(void* self, const char* path);

static ScriptCopyFn gOriginalScriptCopy = 0;
static OpenPacFn gOriginalOpenPac = 0;

// -----------------------------------------------------------------------------
// 一些完全不依赖 CRT 的基础小函数
// -----------------------------------------------------------------------------

extern "C" void* __cdecl memcpy(void* destination, const void* source, u32 size)
{
    // clang 在优化某些很小的字符数组复制时，可能自动把循环重新识别成 memcpy。
    // 本项目又明确使用 /nodefaultlib，不允许偷偷依赖 CRT，所以这里自己提供一个最小实现。
    //
    // 它做的事情非常朴素：
    //   1. 把目标地址和源地址都看成“一个字节一个字节”的数组；
    //   2. 从第 0 个字节复制到第 size-1 个字节；
    //   3. 最后返回目标地址。
    //
    // 这里只用于编译器自动生成的普通非重叠复制，不承担 memmove 的重叠语义。
    u8* out = (u8*)destination;
    const u8* in = (const u8*)source;

    for (u32 i = 0; i < size; ++i)
        out[i] = in[i];

    return destination;
}


static u32 ReadU32(const void* address)
{
    // x86 允许非 4 字节对齐读取，所以这里直接把地址当成 u32 指针。
    // 本项目所有目标都是 32 位 x86；如果以后移植到别的平台，应重新审查这一点。
    return *(const u32*)address;
}


static int StringEquals(const char* left, const char* right)
{
    // 不调用 strcmp，避免引入 C Runtime。
    // 两边逐字节比较；只有同时遇到 '\0' 才算完全相等。
    if (!left || !right)
        return 0;

    while (*left && *right)
    {
        if (*left != *right)
            return 0;
        ++left;
        ++right;
    }

    return *left == *right;
}

static u32 StringLength(const char* text)
{
    // 不调用 strlen，同样是为了完全摆脱 CRT。
    u32 length = 0;
    if (!text)
        return 0;

    while (text[length] != '\0')
        ++length;

    return length;
}

// -----------------------------------------------------------------------------
// 从 PEB 取得已加载模块，并手工解析导出表
// -----------------------------------------------------------------------------
//
// 为什么做这件事：
//   普通 DLL 会在导入表里写 VirtualProtect / WriteFile 等函数。
//   这样链接时要依赖 kernel32.lib 等 Windows SDK 文件。
//   本正式版希望自身完全便携，所以不产生标准导入表，而是运行时从已经加载的
//   Windows 模块导出表中找到这些函数。
//
// 这不是“绕过系统 API”；仍然调用正常的 Windows API，只是不用链接器导入表。

__declspec(naked) static void* GetPeb()
{
    // 32 位 Windows 的线程环境块 TEB 位于 FS 段。
    // FS:[0x30] 保存当前进程的 PEB 指针。
    __asm
    {
        mov eax, fs:[0x30]
        ret
    }
}

static u8* GetExeBase()
{
    u8* peb = (u8*)GetPeb();
    if (!peb)
        return 0;

    // PEB + 0x0C -> PEB_LDR_DATA*。
    u8* ldr = *(u8**)(peb + 0x0C);
    if (!ldr)
        return 0;

    // PEB_LDR_DATA + 0x0C -> InLoadOrderModuleList 的 LIST_ENTRY 头。
    u8* listHead = ldr + 0x0C;
    u8* firstEntry = *(u8**)listHead;
    if (!firstEntry || firstEntry == listHead)
        return 0;

    // InLoadOrder 列表第一个条目就是主 EXE。
    // LDR_DATA_TABLE_ENTRY + 0x18 是 DllBase。
    return *(u8**)(firstEntry + 0x18);
}

static void* FindExportInModule(u8* moduleBase, const char* wantedName)
{
    if (!moduleBase || !wantedName)
        return 0;

    // 所有正常 PE 文件从 "MZ" 开始。
    if (moduleBase[0] != 'M' || moduleBase[1] != 'Z')
        return 0;

    // DOS 头 + 0x3C 是 NT Headers 的文件内偏移。
    u32 peOffset = ReadU32(moduleBase + 0x3C);
    u8* nt = moduleBase + peOffset;

    // NT Headers 从 "PE\0\0" 开始。
    if (ReadU32(nt) != 0x00004550u)
        return 0;

    // PE32 OptionalHeader 从 NT Headers + 24 开始。
    u8* optional = nt + 24;
    if (*(u16*)optional != 0x010Bu)
        return 0;

    // PE32 OptionalHeader + 0x60 是 DataDirectory[0]，即导出表 RVA/Size。
    u32 exportRva  = ReadU32(optional + 0x60);
    u32 exportSize = ReadU32(optional + 0x64);
    if (!exportRva || !exportSize)
        return 0;

    u8* exportDirectory = moduleBase + exportRva;

    // IMAGE_EXPORT_DIRECTORY 中我们只需要以下字段：
    // +0x18 NumberOfNames
    // +0x1C AddressOfFunctions
    // +0x20 AddressOfNames
    // +0x24 AddressOfNameOrdinals
    u32 numberOfNames = ReadU32(exportDirectory + 0x18);
    u32 functionsRva  = ReadU32(exportDirectory + 0x1C);
    u32 namesRva      = ReadU32(exportDirectory + 0x20);
    u32 ordinalsRva   = ReadU32(exportDirectory + 0x24);

    u32* functions = (u32*)(moduleBase + functionsRva);
    u32* names = (u32*)(moduleBase + namesRva);
    u16* ordinals = (u16*)(moduleBase + ordinalsRva);

    for (u32 i = 0; i < numberOfNames; ++i)
    {
        const char* name = (const char*)(moduleBase + names[i]);
        if (!StringEquals(name, wantedName))
            continue;

        u32 functionRva = functions[ordinals[i]];

        // 如果函数 RVA 落在导出目录本身范围内，它不是机器码地址，
        // 而是类似 "KERNELBASE.VirtualProtect" 的 forwarder 字符串。
        // 我们不在这里递归解析 forwarder，而是让外层继续扫描其它已加载模块；
        // Windows 10/11 的 KERNELBASE 通常会提供真正实现。
        if (functionRva >= exportRva && functionRva < exportRva + exportSize)
            return 0;

        return moduleBase + functionRva;
    }

    return 0;
}

static void* FindLoadedExport(const char* wantedName)
{
    u8* peb = (u8*)GetPeb();
    if (!peb)
        return 0;

    u8* ldr = *(u8**)(peb + 0x0C);
    if (!ldr)
        return 0;

    u8* listHead = ldr + 0x0C;
    u8* entry = *(u8**)listHead;

    // 正常进程模块远少于 256 个。加上硬上限，可以在链表损坏时避免无限循环。
    for (u32 guard = 0; entry && entry != listHead && guard < 256u; ++guard)
    {
        u8* moduleBase = *(u8**)(entry + 0x18);
        void* found = FindExportInModule(moduleBase, wantedName);
        if (found)
            return found;

        // LIST_ENTRY 的第一个指针就是 Flink，即下一项。
        entry = *(u8**)entry;
    }

    return 0;
}

static int ResolveApis()
{
    if (gApisResolved)
        return gVirtualProtect != 0;

    // 正式版只需要修改代码页和刷新指令缓存，所以只解析这两个 API。
    // 不再解析 CreateFileA / WriteFile，确保运行时不会生成日志或覆盖率文件。
    gVirtualProtect = (VirtualProtectFn)FindLoadedExport("VirtualProtect");
    gFlushInstructionCache = (FlushInstructionCacheFn)FindLoadedExport("FlushInstructionCache");

    gApisResolved = 1;
    return gVirtualProtect != 0;
}

// -----------------------------------------------------------------------------
// FNV-1a 与映射表二分查找
// -----------------------------------------------------------------------------

static u32 Fnv1a32(const u8* data, u32 size)
{
    // FNV-1a 的每一步都只有 XOR + 乘法，非常适合稳定复现离线生成器的结果。
    u32 value = 0x811C9DC5u;
    for (u32 i = 0; i < size; ++i)
    {
        value ^= data[i];
        value *= 0x01000193u;
    }
    return value;
}

static const VoiceMapEntry* FindVoiceEntry(u32 scriptHash, u32 recordOffset)
{
    // VoiceMap.generated.h 已按 (scriptHash, recordOffset) 排序。
    // 二分查找最多只需大约 log2(5891) ≈ 13 次比较，比线性扫 5000 多项稳定得多。
    u32 left = 0;
    u32 right = kVoiceMapCount;

    while (left < right)
    {
        u32 middle = left + (right - left) / 2u;
        const VoiceMapEntry* item = &kVoiceMap[middle];

        if (item->scriptHash < scriptHash ||
            (item->scriptHash == scriptHash && item->recordOffset < recordOffset))
        {
            left = middle + 1u;
        }
        else
        {
            right = middle;
        }
    }

    if (left >= kVoiceMapCount)
        return 0;

    const VoiceMapEntry* item = &kVoiceMap[left];
    if (item->scriptHash == scriptHash && item->recordOffset == recordOffset)
        return item;

    return 0;
}

// -----------------------------------------------------------------------------
// 在临时字符串前插 @v 标签
// -----------------------------------------------------------------------------

static int AlreadyHasVoicePrefix(const char* text)
{
    // 这是双保险：当前确定映射来自原 BFE，本来不会带主角 @v。
    // 但如果以后用户又叠加了别的补丁，看到开头已经是 @v 或 @h00@v 就不要重复插入。
    if (!text)
        return 0;

    if (text[0] == '@' && text[1] == 'v')
        return 1;

    if (text[0] == '@' && text[1] == 'h' && text[2] == '0' && text[3] == '0' &&
        text[4] == '@' && text[5] == 'v')
        return 1;

    return 0;
}

static int InjectVoicePrefix(char* destination, const VoiceMapEntry* entry)
{
    if (!destination || !entry)
        return 0;

    if (AlreadyHasVoicePrefix(destination))
        return 0;

    // 0x0B 临时记录布局：
    //   destination - 8 : opcode DWORD（低位为 0x0B）
    //   destination - 4 : 当前字符串字节长度（包含末尾 NUL）
    //   destination     : 文本本体
    u32* lengthField = (u32*)(destination - 4);
    u32 currentLength = *lengthField;

    // 正常字符串至少含一个结尾 NUL，所以长度不能是 0。
    // 也绝不能超过临时槽容量；异常时宁可不播放，也不越界写内存。
    if (currentLength == 0u || currentLength > kTemporaryTextCapacity)
        return 0;

    u32 tagLength = StringLength(entry->voiceTag);

    // test1 对 TR 独白直接注入了：
    //     @h00@vTR0001
    // 实机结果证明原版解释器并不认识 SE 新增的 @h，也不支持字母开头的 @v 参数，
    // 因此画面会漏出 "00TR0001"，同时没有声音。
    //
    // test2 改为只注入：
    //     @vTR0001
    // 然后由下面新增的“TR @v 兼容 Hook”负责：
    //   1. 吃掉完整的 TR0001；
    //   2. 构造 TR0001.wav；
    //   3. 跳过原版仅针对既有角色的语音可用性检查；
    //   4. 回到原版声音播放流程。
    //
    // 数字标签仍然完全走原版，不经过特殊处理。
    u32 prefixLength = 2u + tagLength;

    if (currentLength + prefixLength > kTemporaryTextCapacity)
    {
        // 目标临时槽没有足够空间时直接放弃这一句。
        // 正式版宁可少播一条，也绝不越界覆盖游戏栈内存。
        return 0;
    }

    // 从尾巴往后搬，包括末尾 '\0'。
    // 必须倒序搬；如果从前往后，刚写到后面的字节会覆盖还没复制的原文本。
    for (u32 i = currentLength; i > 0u; --i)
        destination[i - 1u + prefixLength] = destination[i - 1u];

    u32 cursor = 0;

    destination[cursor++] = '@';
    destination[cursor++] = 'v';

    for (u32 i = 0; i < tagLength; ++i)
        destination[cursor++] = entry->voiceTag[i];

    // 长度字段包含末尾 NUL，所以直接加 prefixLength 即可。
    *lengthField = currentLength + prefixLength;
    return 1;
}

// -----------------------------------------------------------------------------
// 剧情文本 CALL Hook
// -----------------------------------------------------------------------------

static int __fastcall HookScriptCopy(void* self, void* /*unusedEdx*/, char* destination, u32 length)
{
    // 第一步：在调用原字符串复制函数之前，读取 ScriptReader 当前状态。
    // 这里不能等复制之后再读“当前游标”，因为原函数执行完后游标已经向后移动，
    // 我们就无法准确知道刚刚复制的是执行流里的哪一条 0x000B 文本记录。
    u8* state = self ? *(u8**)((u8*)self + 8) : 0;

    u8* streamBase = 0;
    u32 streamSize = 0;
    u32 cursorBeforeCopy = 0;
    u32 recordOffset = 0;
    u32 scriptHash = 0;
    const VoiceMapEntry* entry = 0;

    if (state)
    {
        // 这些字段已经通过原版反汇编确认：
        //   state+0x0C  = 当前脚本执行流长度
        //   state+0x114 = 当前读取游标
        //   state+0x128 = 当前脚本执行流基址
        streamSize = ReadU32(state + 0x0C);
        cursorBeforeCopy = ReadU32(state + 0x114);
        streamBase = *(u8**)(state + 0x128);

        // 当前 CALL 发生时，opcode 0x0B 和后面的 4 字节字符串长度已经被解释器消费，
        // 因此游标正好指向字符串第一个字节。往前退 6 字节，就是这条记录在执行流中的起点。
        if (streamBase && streamSize > 0u && streamSize <= 16u * 1024u * 1024u &&
            cursorBeforeCopy >= 6u && cursorBeforeCopy <= streamSize)
        {
            recordOffset = cursorBeforeCopy - 6u;

            // 在真正查映射前做三项结构校验：
            //   1. 记录没有越过执行流末尾；
            //   2. 开头确实是 0B 00；
            //   3. 记录中的长度字段与这次 CALL 传进来的 length 完全相同。
            // 任意一项不符，都说明当前数据不是我们确认过的剧情文本，正式版直接保持原样。
            if (recordOffset + 6u <= streamSize &&
                streamBase[recordOffset] == 0x0Bu &&
                streamBase[recordOffset + 1u] == 0x00u &&
                ReadU32(streamBase + recordOffset + 2u) == length)
            {
                // 只哈希“真正执行流”，而不是整个 BIN。
                // 旧汉化追加的 BFET 位于执行流之后，所以原版日文脚本和旧汉化脚本会得到同一个身份哈希。
                scriptHash = Fnv1a32(streamBase, streamSize);

                // 映射键是：脚本执行流哈希 + 记录静态偏移。
                // 这两个量都不依赖台词当前显示的是日文还是汉化后的中文。
                entry = FindVoiceEntry(scriptHash, recordOffset);
            }
        }
    }

    // 第二步：无论有没有找到主角语音映射，都必须先调用游戏原来的字符串复制函数。
    // 这也是与既有汉化 ASI 共存的关键：如果另一个插件已经接管 0x004ACC20，
    // 我们这里保存的目标仍然是游戏当前地址上的原函数入口，文本会先按既有流程复制到临时缓冲。
    int result = gOriginalScriptCopy ? gOriginalScriptCopy(self, destination, length) : 0;

    // 复制失败或当前记录没有安全映射，就原样返回，不改变任何文本。
    if (!result || !entry)
        return result;

    // 第三步：只修改“这一次已经复制到栈上的临时文本”。
    // InjectVoicePrefix 会在最前面插入 @v<VoiceTag>，并同步更新 destination 前 4 字节的长度字段。
    // 它不会写回 BIN，也不会改变脚本解释器的源游标。
    InjectVoicePrefix(destination, entry);

    return result;
}

// -----------------------------------------------------------------------------
// 原版 @v 的 TRxxxx 兼容入口
// -----------------------------------------------------------------------------
//
// test1 实机日志已经证明：
//   - @v500000 能正常播放；
//   - @vTR0001 不会播放，而且 "TR0001" 会漏进正文；
//   - @h00 也不是原版支持的控制码，"00" 同样会漏进正文。
//
// 对原版 0x0049EA40 的反汇编确认了原因：
//   原版 @v 只循环接受 '0'..'9'，然后把读到的数字后面加上 ".wav"。
// 对 BFSE 对应函数的反汇编则确认：
//   SE 会把 @v 后面的前两个字符无条件接收，再继续接收后四个字符，
//   因而可以处理 TR0001、KI0001 等字母资源键。
//
// 当前剧情映射里需要字母兼容的只有 TRxxxx。
// 所以我们不移植整个 SE 文本解释器，而只在原版 @v 参数入口加一个极小分支：
//   - 如果不是 "TR"，马上执行原版被覆盖的两条指令并回到原版；
//   - 如果是 "TR"，把 6 字节键复制到原版自己的栈上临时文件名缓冲区，追加 ".wav"，
//     再把文本游标前移 6 字节，最后跳回原版播放流程。
//
// 这里用 naked 的原因：
//   我们必须继续使用“原解析函数当前那一帧”的 ESP/EBP 和局部缓冲区，
//   普通 C++ 函数一旦建立自己的栈帧，这些偏移就不再对应原游戏。
__declspec(naked) static void HookVoiceCommandEntry()
{
    __asm
    {
        // 原版 0x0049EA40 被覆盖的第一条指令本来是：
        //     mov bl, [eax + ecx + 2]
        // eax = 当前文本基址，ecx = 当前 @ 字符的游标。
        mov bl, byte ptr [eax + ecx + 2]

        // 只接管 "TRxxxx"。
        // 其它情况必须尽可能保持原版行为。
        cmp bl, 'T'
        jne normal_original_path
        cmp byte ptr [eax + ecx + 3], 'R'
        jne normal_original_path

        // 保存 EBX。
        // 原函数把 EBX 当成非易失寄存器，我们不能因为自己的辅助复制破坏它。
        push ebx

        // EDX 指向 @v 后面的 6 字节资源键，例如 "TR0001"。
        lea edx, [eax + ecx + 2]

        // push ebx 让 ESP 比原函数低了 4 字节。
        // 原函数语音文件名临时缓冲区是 [原 ESP + 0xA4]，
        // 因此此刻同一地址就是 [当前 ESP + 0xA8]。
        lea ebx, [esp + 0xA8]

        // 复制 6 字节 VoiceTag。
        // 前 4 字节一次复制，后 2 字节再复制。
        mov eax, dword ptr [edx]
        mov dword ptr [ebx], eax
        mov ax, word ptr [edx + 4]
        mov word ptr [ebx + 4], ax

        // 在第 6 字节后直接追加 ".wav\0"。
        // 0x7661772E 按 x86 little-endian 写到内存正好是：2E 77 61 76 = ".wav"。
        mov dword ptr [ebx + 6], 0x7661772E
        mov byte ptr [ebx + 10], 0

        // 原版正常 @v 在进入播放判断前，还会把刚构造出的文件名保存到 this+0x60。
        // 这份副本可能被“重播上一句语音”等后续逻辑使用，所以 TR 兼容分支也必须同步维护，
        // 不能只保证眼前这一声能播放。这里一共复制 11 字节：
        //     TR0001.wav + 末尾 NUL
        mov eax, dword ptr [ebx]
        mov dword ptr [ebp + 0x60], eax
        mov eax, dword ptr [ebx + 4]
        mov dword ptr [ebp + 0x64], eax
        mov ax, word ptr [ebx + 8]
        mov word ptr [ebp + 0x68], ax
        mov al, byte ptr [ebx + 10]
        mov byte ptr [ebp + 0x6A], al

        // SE 的 @h00 本质上允许这类特殊台词不依赖“当前角色是否有语音”的旧检查。
        // 我们不把 @h00 塞给原版，而是在 TR 分支中直接越过那段角色可用性判断。
        // 原函数公共尾部仍会再 +2，从而正常吃掉 "@v" 本身。
        add dword ptr [ebp + 0xE8], 6

        pop ebx

        // 这里仍然保留原版的“当前模式”和“语音忙碌状态”检查，
        // 只跳过原版不适用于 TR 独白的角色语音可用性判断。
        jmp dword ptr [gVoiceAfterAvailability]

normal_original_path:
        // 恢复第二条被覆盖指令：xor edx, edx。
        // 然后回到 0x0049EA46，数字 VoiceTag 继续 100% 使用原版逻辑。
        xor edx, edx
        jmp dword ptr [gVoiceNumericContinue]
    }
}

// -----------------------------------------------------------------------------
// PAC 打开 CALL Hook
// -----------------------------------------------------------------------------

static int __fastcall HookOpenPac(void* self, void* /*unusedEdx*/, const char* path)
{
    // 先完全照原游戏行为打开当前包。
    // Voice1、Voice2 以及其它资源包的结果都必须保持不变。
    int originalResult = gOriginalOpenPac ? gOriginalOpenPac(self, path) : 0;

    // 原版资源初始化中 Voice1 和 Voice2 使用同一个 PAC 管理对象；
    // BFSE 的 Voice1 / Voice2 / Voice3 也同样挂在这个对象上。
    // 因此当“原版 Voice2 已经成功挂载”这一事实成立时，
    // 只额外让同一个 self 再打开一次 Voice3.pac，就能让原版 @v 播放链自然找到新增主角剧情语音。
    if (!gVoice3Attempted && originalResult && path && StringEquals(path, "Voice2.pac"))
    {
        gVoice3Attempted = 1;

        static const char voice3Path[] = "Voice3.pac";
        if (gOriginalOpenPac)
            gOriginalOpenPac(self, voice3Path);
    }

    // 这里故意返回“原始 Voice1/Voice2 请求”的结果，而不是 Voice3 的结果。
    // 即使用户忘了放 Voice3.pac，最坏也只是新增主角语音不播放，
    // 不能因为插件附加资源缺失而破坏原版自己的资源初始化。
    return originalResult;
}

// -----------------------------------------------------------------------------
// CALL 指令补丁
// -----------------------------------------------------------------------------

static int PatchRelativeCall(u8* callSite, void* expectedTarget, void* hookTarget)
{
    if (!callSite || !expectedTarget || !hookTarget || !gVirtualProtect)
        return 0;

    // x86 near CALL rel32 的第一个字节必须是 E8。
    if (callSite[0] != 0xE8u)
        return 0;

    s32 oldRelative = *(s32*)(callSite + 1);
    u8* actualTarget = callSite + 5 + oldRelative;

    // 只有“当前仍然 CALL 到我们已经逆向确认的原函数”才允许改写。
    // 如果用户拿错 EXE，或别的插件已经改了这个 CALL，本插件直接拒绝盲写。
    if (actualTarget != (u8*)expectedTarget)
        return 0;

    u32 oldProtect = 0;
    if (!gVirtualProtect(callSite, 5u, kPageExecuteReadWrite, &oldProtect))
        return 0;

    s32 newRelative = (s32)((u8*)hookTarget - (callSite + 5));
    callSite[0] = 0xE8u;
    *(s32*)(callSite + 1) = newRelative;

    // 写完后恢复原来的页面保护。
    u32 ignored = 0;
    gVirtualProtect(callSite, 5u, oldProtect, &ignored);

    // 修改的是正在执行的机器码。通知 CPU 刷新指令缓存是正确做法。
    // -1 是 Win32 的“当前进程”伪句柄，不需要再调用 GetCurrentProcess。
    if (gFlushInstructionCache)
        gFlushInstructionCache((Handle)(-1), callSite, 5u);

    return 1;
}

static int PatchVoiceCommandParser(u8* patchSite, void* hookTarget)
{
    // 原版 0x0049EA40 的前 6 字节必须精确是：
    //   8A 5C 08 02    mov bl, [eax+ecx+2]
    //   33 D2          xor edx, edx
    //
    // 只有完全匹配才允许覆盖，避免把不同 EXE 或已经被其它插件修改过的位置写坏。
    static const u8 expected[6] = { 0x8A, 0x5C, 0x08, 0x02, 0x33, 0xD2 };

    if (!patchSite || !hookTarget || !gVirtualProtect)
        return 0;

    for (u32 i = 0; i < 6u; ++i)
    {
        if (patchSite[i] != expected[i])
            return 0;
    }

    u32 oldProtect = 0;
    if (!gVirtualProtect(patchSite, 6u, kPageExecuteReadWrite, &oldProtect))
        return 0;

    // E9 rel32 需要 5 字节；第 6 字节补 NOP，刚好覆盖两条原指令。
    s32 relative = (s32)((u8*)hookTarget - (patchSite + 5));
    patchSite[0] = 0xE9u;
    *(s32*)(patchSite + 1) = relative;
    patchSite[5] = 0x90u;

    u32 ignored = 0;
    gVirtualProtect(patchSite, 6u, oldProtect, &ignored);

    if (gFlushInstructionCache)
        gFlushInstructionCache((Handle)(-1), patchSite, 6u);

    return 1;
}

static void InitializeCore()
{
    // ASI Loader 可能既靠 LoadLibrary 触发 DllMain，又显式调用 InitializeASI。
    // 所以初始化必须幂等：第二次进来直接返回，不能把 CALL 再补一次。
    if (gInitialized)
        return;
    gInitialized = 1;

    if (!ResolveApis())
        return;

    u8* exeBase = GetExeBase();
    if (!exeBase)
        return;

    gOriginalScriptCopy = (ScriptCopyFn)(exeBase + kRvaScriptCopy);
    gOriginalOpenPac = (OpenPacFn)(exeBase + kRvaOpenPac);

    // 给裸汇编 TR 分支准备两个“回到原游戏”的地址。
    gVoiceNumericContinue = exeBase + kRvaVoiceNumericContinue;
    gVoiceAfterAvailability = exeBase + kRvaVoiceAfterAvailability;

    // 每个补丁函数都会先验证目标机器码/旧 CALL 目标。
    // 如果用户拿了不受支持的 EXE，对应补丁会安静地拒绝写入，而不是盲目覆盖未知代码。
    PatchRelativeCall(
        exeBase + kRvaScriptCallSite,
        (void*)gOriginalScriptCopy,
        (void*)&HookScriptCopy
    );

    PatchRelativeCall(
        exeBase + kRvaPacCallSite,
        (void*)gOriginalOpenPac,
        (void*)&HookOpenPac
    );

    PatchVoiceCommandParser(
        exeBase + kRvaVoiceCommandEntry,
        (void*)&HookVoiceCommandEntry
    );
}

// -----------------------------------------------------------------------------
// ASI / DLL 入口
// -----------------------------------------------------------------------------

extern "C" void __stdcall InitializeASI(void)
{
    // 给支持 InitializeASI 导出的 ASI Loader 使用。
    InitializeCore();
}

extern "C" int __stdcall DllMainCRTStartup(void* /*module*/, u32 reason, void* /*reserved*/)
{
    // reason == 1 表示 DLL_PROCESS_ATTACH，也就是 ASI 第一次被装入游戏进程。
    if (reason == 1u)
    {
        // 正式发行版只做内存 Hook 初始化，不创建日志、不生成 CSV，也不写任何额外运行时文件。
        InitializeCore();
    }

    // DLL 入口返回非 0 表示允许加载。
    // 即使版本校验导致某个 Hook 没有安装，我们也不让游戏因此启动失败。
    return 1;
}
