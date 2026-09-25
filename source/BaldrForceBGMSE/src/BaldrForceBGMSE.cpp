// ============================================================================
// BaldrForceBGMSE.cpp
// ============================================================================
//
// 目标：
//   在 2003 年原版《BALDR FORCE》(BFE) 中，继续使用 BFE 自己的“BGM 编号/切曲逻辑”，
//   但把实际播放资源和循环起点替换成 Standard Edition (BFSE) 的同编号 BGM。
//
// 这份源码刻意写了非常详细的中文行内注释。
// 可以把整个插件理解成三个彼此独立的小动作：
//
//   1. BFE 从 Table.pac 读取每条 BGMInfo 时：
//      只把 +0x40 的“循环起点”改成 BFSE 数值；
//      前 0x40 字节的 BFE 文件名保持原样。
//
//   2. BFE 真正准备打开某首 BGM WAV 时：
//      根据“这条 BGMInfo 在 30 项数组里的编号”，临时把文件名参数换成 BFSE 文件名。
//      这样资源加载器会从当前 BGM.pac 中寻找 BFSE 的 WAV 名称。
//
//   3. 如果 INI 选择 Local 模式：
//      只在游戏打开 BGM.pac 时，把路径改到“ASI 同目录\BFSE_BGM.pac”。
//      如果选择 Game 模式，则完全不碰 BGM.pac 路径，交给游戏/Ultimate ASI Loader 的
//      update 虚拟覆盖系统去决定最终读到哪个 BGM.pac。
//
// 为什么“不直接把整个 BGMInfo 0x44 覆盖成 BFSE”？
// ----------------------------------------------------
// 这是为了最大限度保留 BFE/汉化版可能使用的曲名显示。
// BFSE 与 BFE 的资源文件名不同，而用户已经确认汉化版还会显示汉化过的 BGM 名字。
// 当前反汇编已经证明真正播放时，BGM loader 只需要“资源文件名指针”；循环点则由后续播放函数
// 从 BGMInfo +0x40 单独读取。因此我们可以：
//
//   - 内存表里的 BFE 名字完全不改；
//   - 只改循环点；
//   - 只有进入资源加载函数的一瞬间才换成 BFSE 文件名。
//
// 这样比整条覆盖更保守，也更适合汉化版。
//
// 关于 +0x40 循环点：
// -------------------
// 已经不是“推测”，而是从 BFE 播放代码确认：
//
//   - 值 < 0（实际是 -1） -> 走非循环播放函数 0x004AD980；
//   - 值 >= 0            -> 走循环播放函数   0x004ADA10；
//   - 循环函数把该值保存到声音对象 +0x34，并设置 +0x30 = 1；
//   - 流读到文件尾时，0x004AD3FC 再读取 +0x34，执行：
//         loopValue * bytesPerSecond / 100
//     换算成重新读取的字节位置。
//
// 所以单位是 1/100 秒（centisecond，厘秒）：1600 = 16.00 秒。
// BFSE 的循环点必须和 BFSE WAV 一起移植，否则即使能播放，也会在错误位置循环。
//
// ============================================================================

#include "BgmTable.generated.h"

// ----------------------------------------------------------------------------
// 最小基础类型
// ----------------------------------------------------------------------------
// 不包含 windows.h，也不链接 CRT/VC Runtime。
// 这样生成的 ASI 是一个非常独立的 32 位 DLL，适合老游戏绿色使用。
typedef unsigned char      u8;
typedef unsigned short     u16;
typedef unsigned int       u32;
typedef signed int         s32;
typedef void*              Handle;
typedef void*              ModuleHandle;

// ----------------------------------------------------------------------------
// Win32 API 函数指针类型
// ----------------------------------------------------------------------------
typedef int (__stdcall *VirtualProtectFn)(void* address, u32 size, u32 newProtect, u32* oldProtect);
typedef int (__stdcall *FlushInstructionCacheFn)(Handle process, const void* address, u32 size);
typedef u32 (__stdcall *GetModuleFileNameAFn)(ModuleHandle module, char* buffer, u32 size);
typedef u32 (__stdcall *GetPrivateProfileStringAFn)(const char* section, const char* key, const char* defaultValue, char* out, u32 outSize, const char* fileName);
typedef Handle (__stdcall *CreateFileAFn)(const char* fileName, u32 desiredAccess, u32 shareMode, void* securityAttributes, u32 creationDisposition, u32 flagsAndAttributes, Handle templateFile);
typedef int (__stdcall *WriteFileFn)(Handle file, const void* buffer, u32 bytesToWrite, u32* bytesWritten, void* overlapped);
typedef int (__stdcall *CloseHandleFn)(Handle object);

// BFE 自己的两个函数类型。
// __thiscall 的 this 放 ECX，其余参数走栈。
typedef int (__thiscall *TableReadFn)(void* self, void* destination, u32 size);
typedef int (__thiscall *BgmLoadFn)(void* self, void* archiveContext, const char* resourceName);

// ----------------------------------------------------------------------------
// 固定常量：只针对本轮确认的 2003 BFE 基线
// ----------------------------------------------------------------------------
static const u32 kPageExecuteReadWrite = 0x40u;
static const u32 kGenericWrite = 0x40000000u;
static const u32 kFileShareRead = 0x00000001u;
static const u32 kFileShareWrite = 0x00000002u;
static const u32 kCreateAlways = 2u;
static const u32 kFileAttributeNormal = 0x00000080u;
static Handle const kInvalidHandleValue = (Handle)(s32)-1;

// EXE 里的地址全部保存成 RVA，而不是写死 0x004xxxxx。
// 运行时实际地址 = 当前 EXE ImageBase + RVA。
static const u32 kBgmInfoReadCallRva = 0x0006144Cu;   // VA 0x0046144C
static const u32 kTableReadFunctionRva = 0x000A9EC0u; // VA 0x004A9EC0

static const u32 kBgmLoadCall1Rva = 0x00063892u;      // VA 0x00463892
static const u32 kBgmLoadCall2Rva = 0x000639B6u;      // VA 0x004639B6
static const u32 kBgmLoadFunctionRva = 0x000AD7F0u;   // VA 0x004AD7F0

static const u32 kBgmTableRva = 0x00321010u;          // VA 0x00721010
static const u32 kBgmTableBytes = 30u * 0x44u;        // 30 条，正好到 VA 0x00721808

// KERNEL32!CreateFileA 在当前 BFE 主 EXE 的 IAT 槽位。
// Game 模式不会修改它；只有 Local 模式才把这个槽位换成我们的 Hook。
static const u32 kCreateFileAIatRva = 0x000BE05Cu;

// ----------------------------------------------------------------------------
// 全局运行状态
// ----------------------------------------------------------------------------
static ModuleHandle gThisModule = 0;
static u8* gExeBase = 0;
static int gInitialized = 0;
static int gSourceLocal = 0;

static VirtualProtectFn gVirtualProtect = 0;
static FlushInstructionCacheFn gFlushInstructionCache = 0;
static GetModuleFileNameAFn gGetModuleFileNameA = 0;
static GetPrivateProfileStringAFn gGetPrivateProfileStringA = 0;
static CreateFileAFn gCreateFileA_Direct = 0;
static WriteFileFn gWriteFile = 0;
static CloseHandleFn gCloseHandle = 0;

static CreateFileAFn gCreateFileA_Chain = 0;
static TableReadFn gOriginalTableRead = 0;
static BgmLoadFn gOriginalBgmLoad = 0;

static Handle gLog = kInvalidHandleValue;
static char gIniPath[1024];
static char gLocalBgmPath[1024];
static char gLogPath[1024];

// 30 位 bit mask：某个编号第一次真正进入资源加载器时记一条日志。
// 这样测试版能确认不同 BGM 确实走 BFSE 名称，又不会每次循环/切场景都刷屏。
static u32 gSeenBgmMask = 0u;
static u32 gLoopPatchedMask = 0u;
static int gRedirectLogged = 0;
static int gRedirectFailLogged = 0;

// ----------------------------------------------------------------------------
// 不依赖 CRT 的基础字符串/内存函数
// ----------------------------------------------------------------------------
static u32 StringLength(const char* text)
{
    if (!text)
        return 0u;

    u32 length = 0u;
    while (text[length] != '\0')
        ++length;
    return length;
}

static char ToLowerAscii(char c)
{
    if (c >= 'A' && c <= 'Z')
        return (char)(c - 'A' + 'a');
    return c;
}

static int StringEqualsIgnoreCase(const char* a, const char* b)
{
    if (!a || !b)
        return 0;

    u32 i = 0u;
    while (a[i] && b[i])
    {
        if (ToLowerAscii(a[i]) != ToLowerAscii(b[i]))
            return 0;
        ++i;
    }
    return a[i] == '\0' && b[i] == '\0';
}

// clang 在 /O2 下有时会把某些简单复制自动优化成 memcpy 调用。
// 我们明确实现最小 memcpy/memset，避免 /nodefaultlib 构建因为编译器自动优化而偷偷需要 CRT。
// 这两个函数不是“业务逻辑”，只是给编译器一个本地可用的字节复制/填充实现。
extern "C" void* __cdecl memcpy(void* destination, const void* source, u32 size)
{
    u8* dst = (u8*)destination;
    const u8* src = (const u8*)source;
    for (u32 i = 0u; i < size; ++i)
        dst[i] = src[i];
    return destination;
}

extern "C" void* __cdecl memset(void* destination, int value, u32 size)
{
    u8* dst = (u8*)destination;
    for (u32 i = 0u; i < size; ++i)
        dst[i] = (u8)value;
    return destination;
}

static void CopyString(char* destination, u32 capacity, const char* source)
{
    if (!destination || capacity == 0u)
        return;

    u32 i = 0u;
    if (source)
    {
        while (source[i] && i + 1u < capacity)
        {
            destination[i] = source[i];
            ++i;
        }
    }
    destination[i] = '\0';
}

static void AppendString(char* destination, u32 capacity, const char* source)
{
    u32 used = StringLength(destination);
    if (used >= capacity)
        return;
    CopyString(destination + used, capacity - used, source);
}

static void AppendUnsignedDecimal(char* destination, u32 capacity, u32 value)
{
    // 先把个位、十位……倒着放到临时数组，再反向追加到输出。
    char reversed[16];
    u32 count = 0u;

    do
    {
        reversed[count++] = (char)('0' + (value % 10u));
        value /= 10u;
    }
    while (value && count < sizeof(reversed));

    while (count)
    {
        char one[2];
        one[0] = reversed[--count];
        one[1] = '\0';
        AppendString(destination, capacity, one);
    }
}

static void AppendSignedDecimal(char* destination, u32 capacity, s32 value)
{
    if (value < 0)
    {
        AppendString(destination, capacity, "-");
        // 当前数据最小只有 -1，不会遇到 INT_MIN 溢出问题。
        AppendUnsignedDecimal(destination, capacity, (u32)(-value));
    }
    else
    {
        AppendUnsignedDecimal(destination, capacity, (u32)value);
    }
}

static int BaseNameEqualsIgnoreCase(const char* path, const char* wanted)
{
    if (!path || !wanted)
        return 0;

    // 找最后一个 '\\' 或 '/'，后面才是真正文件名。
    const char* base = path;
    for (const char* p = path; *p; ++p)
    {
        if (*p == '\\' || *p == '/')
            base = p + 1;
    }

    return StringEqualsIgnoreCase(base, wanted);
}

static s32 ReadS32(const void* address)
{
    // x86 允许非对齐读取，但这里仍用逐字节方式拼出来，让含义更直观。
    const u8* p = (const u8*)address;
    u32 value =
        ((u32)p[0]) |
        ((u32)p[1] << 8) |
        ((u32)p[2] << 16) |
        ((u32)p[3] << 24);
    return (s32)value;
}

static void WriteS32(void* address, s32 value)
{
    u8* p = (u8*)address;
    u32 v = (u32)value;
    p[0] = (u8)(v & 0xFFu);
    p[1] = (u8)((v >> 8) & 0xFFu);
    p[2] = (u8)((v >> 16) & 0xFFu);
    p[3] = (u8)((v >> 24) & 0xFFu);
}

// ----------------------------------------------------------------------------
// 从 PEB 取得主 EXE 与已加载模块
// ----------------------------------------------------------------------------
// 由于这个 ASI 使用 /nodefaultlib，也没有标准 Import Directory，
// 初始化时必须自己找到 KERNEL32 导出的少量 API。
struct ListEntry32
{
    ListEntry32* Flink;
    ListEntry32* Blink;
};

static u8* GetPeb()
{
    u8* peb = 0;
#if defined(_M_IX86)
    __asm
    {
        mov eax, fs:[0x30]
        mov peb, eax
    }
#endif
    return peb;
}

static u8* GetMainExeBase()
{
    u8* peb = GetPeb();
    if (!peb)
        return 0;
    return *(u8**)(peb + 0x08);
}

static int UnicodeModuleNameEqualsAscii(const u16* wide, u16 byteLength, const char* ascii)
{
    if (!wide || !ascii)
        return 0;

    u32 wideChars = (u32)byteLength / 2u;
    u32 asciiLen = StringLength(ascii);
    if (wideChars != asciiLen)
        return 0;

    for (u32 i = 0u; i < asciiLen; ++i)
    {
        u16 wc = wide[i];
        char ac = ascii[i];

        if (wc >= 'A' && wc <= 'Z')
            wc = (u16)(wc - 'A' + 'a');
        ac = ToLowerAscii(ac);

        if (wc != (u16)(u8)ac)
            return 0;
    }
    return 1;
}

static u8* FindLoadedModule(const char* moduleName)
{
    u8* peb = GetPeb();
    if (!peb)
        return 0;

    // 32 位 PEB：+0x0C 是 PEB_LDR_DATA*。
    u8* ldr = *(u8**)(peb + 0x0C);
    if (!ldr)
        return 0;

    // PEB_LDR_DATA +0x14 是 InMemoryOrderModuleList。
    ListEntry32* head = (ListEntry32*)(ldr + 0x14);
    ListEntry32* link = head->Flink;

    while (link && link != head)
    {
        // link 指向 LDR_DATA_TABLE_ENTRY.InMemoryOrderLinks（偏移 +0x08），
        // 所以减 8 就回到整个条目的开头。
        u8* entry = (u8*)link - 0x08;
        u8* dllBase = *(u8**)(entry + 0x18);

        // BaseDllName 是 +0x2C 的 UNICODE_STRING：
        //   +0x00 = Length (字节)
        //   +0x04 = Buffer
        u16 nameBytes = *(u16*)(entry + 0x2C);
        const u16* wideName = *(const u16**)(entry + 0x30);

        if (dllBase && UnicodeModuleNameEqualsAscii(wideName, nameBytes, moduleName))
            return dllBase;

        link = link->Flink;
    }

    return 0;
}

static void* FindExportInModule(u8* moduleBase, const char* exportName, int allowForwarder);

static void* ResolveForwarder(const char* forwarder)
{
    // 常见转发格式："KERNELBASE.VirtualProtect"。
    // 我们只需要处理“模块名.函数名”，本插件用到的 Win32 API 都属于这种普通情况。
    if (!forwarder)
        return 0;

    char module[96];
    char function[128];
    u32 dot = 0xFFFFFFFFu;
    u32 length = StringLength(forwarder);

    for (u32 i = 0u; i < length; ++i)
    {
        if (forwarder[i] == '.')
        {
            dot = i;
            break;
        }
    }

    if (dot == 0xFFFFFFFFu || dot == 0u || dot + 1u >= length)
        return 0;

    u32 m = 0u;
    while (m < dot && m + 5u < sizeof(module))
    {
        module[m] = forwarder[m];
        ++m;
    }
    module[m] = '\0';

    // PEB 中模块名带 .DLL；转发字符串通常省略扩展名。
    AppendString(module, sizeof(module), ".dll");
    CopyString(function, sizeof(function), forwarder + dot + 1u);

    // 本项目不需要按 ordinal 转发；遇到 #123 就明确返回失败。
    if (function[0] == '#')
        return 0;

    u8* targetModule = FindLoadedModule(module);
    if (!targetModule)
        return 0;

    return FindExportInModule(targetModule, function, 0);
}

static int StringsEqualExact(const char* a, const char* b)
{
    if (!a || !b)
        return 0;
    u32 i = 0u;
    while (a[i] && b[i])
    {
        if (a[i] != b[i])
            return 0;
        ++i;
    }
    return a[i] == '\0' && b[i] == '\0';
}

static void* FindExportInModule(u8* moduleBase, const char* exportName, int allowForwarder)
{
    if (!moduleBase || !exportName)
        return 0;

    if (*(u16*)moduleBase != 0x5A4Du) // 'MZ'
        return 0;

    u32 peOffset = *(u32*)(moduleBase + 0x3C);
    u8* nt = moduleBase + peOffset;
    if (*(u32*)nt != 0x00004550u) // 'PE\0\0'
        return 0;

    u8* optional = nt + 24u;
    if (*(u16*)optional != 0x010Bu) // PE32
        return 0;

    u32 exportRva = *(u32*)(optional + 0x60);
    u32 exportSize = *(u32*)(optional + 0x64);
    if (!exportRva || !exportSize)
        return 0;

    u8* directory = moduleBase + exportRva;
    u32 numberOfFunctions = *(u32*)(directory + 0x14);
    u32 numberOfNames = *(u32*)(directory + 0x18);
    u32 functionsRva = *(u32*)(directory + 0x1C);
    u32 namesRva = *(u32*)(directory + 0x20);
    u32 ordinalsRva = *(u32*)(directory + 0x24);

    u32* functions = (u32*)(moduleBase + functionsRva);
    u32* names = (u32*)(moduleBase + namesRva);
    u16* ordinals = (u16*)(moduleBase + ordinalsRva);

    for (u32 i = 0u; i < numberOfNames; ++i)
    {
        const char* name = (const char*)(moduleBase + names[i]);
        if (!StringsEqualExact(name, exportName))
            continue;

        u16 ordinalIndex = ordinals[i];
        if ((u32)ordinalIndex >= numberOfFunctions)
            return 0;

        u32 functionRva = functions[ordinalIndex];
        if (!functionRva)
            return 0;

        // 函数 RVA 如果落在导出目录本身内部，它不是机器码，而是一段转发字符串。
        if (functionRva >= exportRva && functionRva < exportRva + exportSize)
        {
            if (!allowForwarder)
                return 0;
            return ResolveForwarder((const char*)(moduleBase + functionRva));
        }

        return moduleBase + functionRva;
    }

    return 0;
}

static void* ResolveKernel32(const char* exportName)
{
    u8* kernel32 = FindLoadedModule("kernel32.dll");
    if (!kernel32)
        return 0;
    return FindExportInModule(kernel32, exportName, 1);
}

static int ResolveApis()
{
    gVirtualProtect = (VirtualProtectFn)ResolveKernel32("VirtualProtect");
    gFlushInstructionCache = (FlushInstructionCacheFn)ResolveKernel32("FlushInstructionCache");
    gGetModuleFileNameA = (GetModuleFileNameAFn)ResolveKernel32("GetModuleFileNameA");
    gGetPrivateProfileStringA = (GetPrivateProfileStringAFn)ResolveKernel32("GetPrivateProfileStringA");
    gCreateFileA_Direct = (CreateFileAFn)ResolveKernel32("CreateFileA");
    gWriteFile = (WriteFileFn)ResolveKernel32("WriteFile");
    gCloseHandle = (CloseHandleFn)ResolveKernel32("CloseHandle");

    return gVirtualProtect && gFlushInstructionCache && gGetModuleFileNameA &&
           gGetPrivateProfileStringA && gCreateFileA_Direct && gWriteFile && gCloseHandle;
}

// ----------------------------------------------------------------------------
// 路径与日志
// ----------------------------------------------------------------------------
static void BuildSiblingPath(char* out, u32 capacity, const char* fileName)
{
    if (!out || capacity == 0u)
        return;

    out[0] = '\0';

    if (gGetModuleFileNameA && gThisModule)
        gGetModuleFileNameA(gThisModule, out, capacity);

    u32 length = StringLength(out);
    u32 cut = length;

    while (cut > 0u)
    {
        char c = out[cut - 1u];
        if (c == '\\' || c == '/')
            break;
        --cut;
    }

    // cut 指向“最后一个斜杠后的第一个字符”。
    // 把这里变成 NUL，就留下 ASI 所在目录和末尾斜杠。
    if (cut < capacity)
        out[cut] = '\0';
    else
        out[0] = '\0';

    AppendString(out, capacity, fileName);
}

static void OpenLog()
{
    if (!gCreateFileA_Direct)
        return;

    BuildSiblingPath(gLogPath, sizeof(gLogPath), "BaldrForceBGMSE.log");

    gLog = gCreateFileA_Direct(
        gLogPath,
        kGenericWrite,
        kFileShareRead | kFileShareWrite,
        0,
        kCreateAlways,
        kFileAttributeNormal,
        0
    );
}

static void LogLine(const char* text)
{
    if (gLog == kInvalidHandleValue || !gWriteFile || !text)
        return;

    u32 written = 0u;
    u32 length = StringLength(text);
    if (length)
        gWriteFile(gLog, text, length, &written, 0);

    static const char newline[] = "\r\n";
    gWriteFile(gLog, newline, 2u, &written, 0);
}

static void LogBgmIndex(const char* prefix, u32 index, s32 loopCs)
{
    char line[256];
    line[0] = '\0';
    AppendString(line, sizeof(line), prefix);
    AppendString(line, sizeof(line), " #");
    AppendUnsignedDecimal(line, sizeof(line), index + 1u);
    AppendString(line, sizeof(line), "，循环点=");
    AppendSignedDecimal(line, sizeof(line), loopCs);
    AppendString(line, sizeof(line), " (1/100秒)");
    LogLine(line);
}

// ----------------------------------------------------------------------------
// 机器码补丁辅助
// ----------------------------------------------------------------------------
static u8* RelativeCallTarget(u8* callSite)
{
    if (!callSite || callSite[0] != 0xE8u)
        return 0;

    s32 displacement = *(s32*)(callSite + 1u);
    return callSite + 5u + displacement;
}

static int ValidateCall(u32 callRva, u32 expectedTargetRva)
{
    u8* site = gExeBase + callRva;
    u8* expected = gExeBase + expectedTargetRva;
    return site[0] == 0xE8u && RelativeCallTarget(site) == expected;
}

static int PatchRelativeCall(u32 callRva, void* replacement)
{
    u8* site = gExeBase + callRva;
    u32 oldProtection = 0u;

    if (!gVirtualProtect(site, 5u, kPageExecuteReadWrite, &oldProtection))
        return 0;

    site[0] = 0xE8u;
    *(s32*)(site + 1u) = (s32)((u8*)replacement - (site + 5u));

    u32 ignored = 0u;
    gVirtualProtect(site, 5u, oldProtection, &ignored);
    gFlushInstructionCache((Handle)(s32)-1, site, 5u);
    return 1;
}

static int PatchPointer(void** slot, void* replacement)
{
    u32 oldProtection = 0u;
    if (!gVirtualProtect(slot, 4u, kPageExecuteReadWrite, &oldProtection))
        return 0;

    *slot = replacement;

    u32 ignored = 0u;
    gVirtualProtect(slot, 4u, oldProtection, &ignored);
    gFlushInstructionCache((Handle)(s32)-1, slot, 4u);
    return 1;
}

// ----------------------------------------------------------------------------
// BGMInfo：只替换循环点，不替换 BFE/汉化版文件名字段
// ----------------------------------------------------------------------------
static s32 BfseLoopPoint(u32 index)
{
    if (index >= kBfseBgmRecordCount)
        return -1;
    return ReadS32(kBfseBgmInfo[index] + 0x40u);
}

static int BgmIndexFromRecordPointer(const void* pointer, u32* outIndex)
{
    if (!pointer || !outIndex)
        return 0;

    const u8* begin = gExeBase + kBgmTableRva;
    const u8* end = begin + kBgmTableBytes;
    const u8* p = (const u8*)pointer;

    if (p < begin || p >= end)
        return 0;

    u32 delta = (u32)(p - begin);
    if ((delta % 0x44u) != 0u)
        return 0;

    u32 index = delta / 0x44u;
    if (index >= kBfseBgmRecordCount)
        return 0;

    *outIndex = index;
    return 1;
}

static int __fastcall HookTableRead(void* self, void* /*unusedEdx*/, void* destination, u32 size)
{
    // 第一步永远先调用 BFE 原函数，让 Table.pac 按原逻辑把这一条记录读进内存。
    int result = gOriginalTableRead ? gOriginalTableRead(self, destination, size) : 0;

    if (!result || size != 0x44u)
        return result;

    u32 index = 0u;
    if (!BgmIndexFromRecordPointer(destination, &index))
        return result;

    // 关键策略：只写最后 4 字节循环点。
    // destination 前 0x40 字节仍是 BFE/汉化版从自己的 BGMInfo.dat 读到的原始名字，
    // 所以即使某处 UI 未来引用这里，也不会被 BFSE 英文资源名污染。
    s32 loopCs = BfseLoopPoint(index);
    WriteS32((u8*)destination + 0x40u, loopCs);

    u32 bit = 1u << index;
    if ((gLoopPatchedMask & bit) == 0u)
    {
        gLoopPatchedMask |= bit;
        LogBgmIndex("[BGMInfo] 已替换", index, loopCs);
    }

    return result;
}

// ----------------------------------------------------------------------------
// BGM 资源名：只在真正加载 WAV 的瞬间替换成 BFSE 名称
// ----------------------------------------------------------------------------
static int __fastcall HookBgmLoad(void* self, void* /*unusedEdx*/, void* archiveContext, const char* resourceName)
{
    u32 index = 0u;
    if (BgmIndexFromRecordPointer(resourceName, &index))
    {
        // kBfseBgmInfo[index] 的前 0x40 字节就是以 NUL 结尾的 BFSE CP932 文件名。
        // BFE 原 loader 在 0x004AD7F0 只把这个参数当 C 字符串交给 PAC 文件读取器，
        // 并不会读取后面的 +0x40，因此传入整条记录起点完全安全。
        const char* bfseName = (const char*)kBfseBgmInfo[index];

        u32 bit = 1u << index;
        if ((gSeenBgmMask & bit) == 0u)
        {
            gSeenBgmMask |= bit;
            LogBgmIndex("[BGM资源] 首次请求 BFSE 曲目", index, BfseLoopPoint(index));
        }

        return gOriginalBgmLoad ? gOriginalBgmLoad(self, archiveContext, bfseName) : 0;
    }

    // 理论上两个被 Hook 的 CALL 都会传 BGMInfo 表指针。
    // 如果未来遇到不同版本导致参数不在表中，宁可原样交回 BFE，也不猜编号。
    return gOriginalBgmLoad ? gOriginalBgmLoad(self, archiveContext, resourceName) : 0;
}

// ----------------------------------------------------------------------------
// Local 模式：把“BGM.pac”的文件打开请求重定向到 ASI 同目录 BFSE_BGM.pac
// ----------------------------------------------------------------------------
static Handle __stdcall HookCreateFileA(
    const char* fileName,
    u32 desiredAccess,
    u32 shareMode,
    void* securityAttributes,
    u32 creationDisposition,
    u32 flagsAndAttributes,
    Handle templateFile)
{
    if (fileName && BaseNameEqualsIgnoreCase(fileName, "BGM.pac"))
    {
        if (!gRedirectLogged)
        {
            gRedirectLogged = 1;
            LogLine("[BGM包] Local 模式捕获 BGM.pac，改读 ASI 同目录 BFSE_BGM.pac。");
        }

        Handle redirected = gCreateFileA_Chain(
            gLocalBgmPath,
            desiredAccess,
            shareMode,
            securityAttributes,
            creationDisposition,
            flagsAndAttributes,
            templateFile
        );

        if (redirected != kInvalidHandleValue)
            return redirected;

        // 测试版采用“失败后回退原路径”而不是直接让游戏启动失败。
        // 日志会明确告诉用户本地包没有打开成功，这样既安全又方便诊断路径错误。
        if (!gRedirectFailLogged)
        {
            gRedirectFailLogged = 1;
            LogLine("[警告] BFSE_BGM.pac 打开失败，已回退原 BGM.pac；此时 BFSE 文件名很可能找不到。");
        }
    }

    return gCreateFileA_Chain(
        fileName,
        desiredAccess,
        shareMode,
        securityAttributes,
        creationDisposition,
        flagsAndAttributes,
        templateFile
    );
}

// ----------------------------------------------------------------------------
// 初始化
// ----------------------------------------------------------------------------
static void LoadConfiguration()
{
    BuildSiblingPath(gIniPath, sizeof(gIniPath), "BaldrForceBGMSE.ini");
    BuildSiblingPath(gLocalBgmPath, sizeof(gLocalBgmPath), "BFSE_BGM.pac");

    char mode[32];
    mode[0] = '\0';

    // 默认 Game：不接管 BGM.pac 文件路径。
    // 这正好配合 Ultimate ASI Loader 的 update 虚拟覆盖：
    // update 里如果已经有 BFSE BGM.pac，游戏仍按原来的 "BGM.pac" 路径打开即可。
    gGetPrivateProfileStringA(
        "BGMSE",
        "Source",
        "Game",
        mode,
        sizeof(mode),
        gIniPath
    );

    gSourceLocal = StringEqualsIgnoreCase(mode, "Local") || StringEqualsIgnoreCase(mode, "1");
}

static int InstallHooks()
{
    // 先把三个直接 CALL 全部验证一遍，全部正确才开始写机器码。
    // 这样如果拿错 EXE，不会出现“前两个已改、第三个失败”的半补丁状态。
    if (!ValidateCall(kBgmInfoReadCallRva, kTableReadFunctionRva))
    {
        LogLine("[失败] BGMInfo 读取 CALL 签名不匹配；当前 EXE 不是已确认 BFE 基线。");
        return 0;
    }

    if (!ValidateCall(kBgmLoadCall1Rva, kBgmLoadFunctionRva) ||
        !ValidateCall(kBgmLoadCall2Rva, kBgmLoadFunctionRva))
    {
        LogLine("[失败] BGM loader CALL 签名不匹配；停止安装，避免错误版本写坏代码。");
        return 0;
    }

    gOriginalTableRead = (TableReadFn)(gExeBase + kTableReadFunctionRva);
    gOriginalBgmLoad = (BgmLoadFn)(gExeBase + kBgmLoadFunctionRva);

    if (!PatchRelativeCall(kBgmInfoReadCallRva, (void*)&HookTableRead))
    {
        LogLine("[失败] 无法安装 BGMInfo 读取 Hook。");
        return 0;
    }

    if (!PatchRelativeCall(kBgmLoadCall1Rva, (void*)&HookBgmLoad) ||
        !PatchRelativeCall(kBgmLoadCall2Rva, (void*)&HookBgmLoad))
    {
        LogLine("[失败] 无法安装 BGM 资源名替换 Hook。");
        return 0;
    }

    LogLine("[成功] 已安装 BGMInfo 循环点替换与两处 BGM 资源名替换 Hook。");

    if (gSourceLocal)
    {
        // IAT 槽位里可能已经是 Ultimate ASI Loader/别的模块的链式 Hook。
        // 所以这里不是强行写成 kernel32!CreateFileA，而是保存“当前槽位值”，
        // Hook 中继续调用它，尽量与已有虚拟文件系统共存。
        void** slot = (void**)(gExeBase + kCreateFileAIatRva);
        gCreateFileA_Chain = (CreateFileAFn)(*slot);

        if (!gCreateFileA_Chain || !PatchPointer(slot, (void*)&HookCreateFileA))
        {
            LogLine("[失败] Local 模式无法接管 CreateFileA IAT；BGMInfo Hook 已装，但本地 BGM 包不会自动重定向。");
            return 0;
        }

        LogLine("[模式] Local：游戏请求 BGM.pac 时改读 ASI 同目录 BFSE_BGM.pac。");
    }
    else
    {
        LogLine("[模式] Game：不改 BGM.pac 路径，直接读取游戏看到的 BGM.pac（可跟随 update 虚拟覆盖）。");
    }

    return 1;
}

static int InitializeCore()
{
    if (gInitialized)
        return 1;
    gInitialized = 1;

    gExeBase = GetMainExeBase();
    if (!gExeBase)
        return 0;

    if (!ResolveApis())
        return 0;

    OpenLog();
    LogLine("[启动] BaldrForceBGMSE v0.1-test1 初始化开始。");
    LogLine("[基线] 目标为 2003 BFE x86；BFSE BGM 编号 0~29 按相同编号映射。");
    LogLine("[循环] BGMInfo +0x40 已确认是 1/100 秒循环起点；-1 为非循环。");
    LogLine("[保护] BFE/汉化版 BGMInfo 前 0x40 字节名字不会被覆盖。");

    LoadConfiguration();

    int ok = InstallHooks();
    if (ok)
        LogLine("[完成] 初始化成功，等待游戏读取 BGMInfo.dat 和 BGM.pac。");
    else
        LogLine("[失败] 初始化未完整成功，请保留本日志用于接档。");

    return ok;
}

// ----------------------------------------------------------------------------
// ASI 导出与 DLL 入口
// ----------------------------------------------------------------------------
extern "C" __declspec(dllexport) void __stdcall InitializeASI()
{
    InitializeCore();
}

extern "C" int __stdcall DllMainCRTStartup(void* module, u32 reason, void* /*reserved*/)
{
    // DLL_PROCESS_ATTACH = 1。
    if (reason == 1u)
    {
        gThisModule = module;
        InitializeCore();
    }
    else if (reason == 0u)
    {
        // DLL_PROCESS_DETACH：测试日志正常关闭。
        if (gLog != kInvalidHandleValue && gCloseHandle)
        {
            gCloseHandle(gLog);
            gLog = kInvalidHandleValue;
        }
    }

    return 1;
}
