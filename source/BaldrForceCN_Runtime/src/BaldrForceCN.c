/*
 * BaldrForceCN Runtime ASI v1.0.4-crashfix1
 * Win32/x86, no CRT, no third-party dependencies.
 *
 * Runtime model:
 *   - Game loads recovered localized Update.pac content; its historical old-VFS name was BFE.PAC.
 *   - This ASI independently parses Update.pac at startup.
 *   - 300 script BFET tails are parsed into JA(CP932) -> ZH(GBK) mappings.
 *   - Main translation path hooks the engine script byte-copy routine used by opcode 0x0B.
 *   - 34,184/34,184 BFET source strings are proven to be encoded as opcode 0x0B + length + CP932 bytes.
 *   - The hook now writes the original localization's raw GBK bytes directly.
 *   - 211 verified EXE-hardcoded UI/system strings are merged into this ASI and also written as raw GBK.
 *   - 168 old-localization encoding-normalization rows convert same-visible CP932 text to the exact GBK bytes.
 *   - WideSweep's 871 aligned candidates are reviewed in-tree: 662 real static strings are patched,
 *     while 209 structure/pointer false positives are retained as evidence but never written to memory.
 *   - A later clean-vs-old-runtime sweep contributes 144 additional verified static strings.
 *   - Localized Update.pac DAT payloads are overlaid at the engine resource-open boundary before parsing.
 *   - Final v1.0.4-crashfix1 loose-resource policy embeds only the 9 files that the user confirmed differ
 *     byte-for-byte from the clean original: 3 BMP\Hell, 3 Dat\Cpu and 3 Dat\Waza files.
 *   - When the game opens one of those exact paths, the ASI returns a real delete-on-close Windows file
 *     handle backed by the embedded bytes; every other file path still falls back to the original game.
 *   - The renderer follows the recovered old-localization byte-consumption rule: every lead byte
 *     0x80..0xFE consumes the following byte, then the pair is rendered through the GDI glyph path.
 *   - Earlier provenance/suffix tracking remains only as a compatibility aid for copied/formatted text;
 *     it no longer decides whether the main renderer consumes one or two bytes.
 *   - Win32 codepage IAT hooks remain only as a compatibility fallback for later conversions.
 *   - GetProcAddress is patched so dynamically resolved APIs are also intercepted.
 *   - CreateFontIndirectW is always adjusted to GB2312 charset when the hook is available.
 *   - BaldrForceCN.ini is optional again and exposes only FontFace / EnableLog; all architecture-critical settings stay fixed.
 *
 * Evidence basis (2026-09-19):
 *   - recovered VFS name BFE.PAC, 344 PACw entries
 *   - 301 .bin, 300 valid BFET tails, 34,184 mappings
 *   - BFET source offsets map exactly into CP932 script strings
 *   - BFET target offsets map exactly into GBK translations
 */

/* ---------- basic Win32/x86 declarations (intentionally no windows.h / CRT) ---------- */
typedef unsigned char  BYTE;
typedef unsigned short WORD;
typedef unsigned short WCHAR;
typedef unsigned int   UINT;
typedef unsigned long  DWORD;
typedef signed long    LONG;
typedef unsigned long  ULONG;
typedef unsigned long  SIZE_T;
typedef int            BOOL;
typedef void*          PVOID;
typedef void*          HANDLE;
typedef void*          HMODULE;
typedef void*          HINSTANCE;
typedef void*          HFONT;
typedef void*          HDC;
typedef void*          HGDIOBJ;
typedef void*          FARPROC;
typedef const char*    LPCSTR;
typedef const char*    LPCCH;
typedef char*          LPSTR;
typedef const WCHAR*   LPCWSTR;
typedef WCHAR*         LPWSTR;
typedef DWORD*         LPDWORD;
typedef BOOL*          LPBOOL;
typedef unsigned long  ULONG_PTR;
typedef signed short   SHORT;

#ifdef _MSC_VER
# define STDCALL __stdcall
# define FASTCALL __fastcall
# define CDECL __cdecl
# define THISCALL __thiscall
# define EXPORT __declspec(dllexport)
#else
# define STDCALL __attribute__((stdcall))
# define FASTCALL __attribute__((fastcall))
# define CDECL __attribute__((cdecl))
# define THISCALL __attribute__((thiscall))
# define EXPORT __declspec(dllexport)
#endif

#define TRUE 1
#define FALSE 0
#define NULL ((void*)0)
#define DLL_PROCESS_ATTACH 1
#define INVALID_HANDLE_VALUE ((HANDLE)(LONG)-1)
#define GENERIC_READ 0x80000000UL
#define GENERIC_WRITE 0x40000000UL
#define FILE_SHARE_READ 0x00000001UL
#define FILE_SHARE_WRITE 0x00000002UL
#define FILE_SHARE_DELETE 0x00000004UL
#define CREATE_NEW 1UL
#define CREATE_ALWAYS 2UL
#define OPEN_EXISTING 3UL
#define FILE_ATTRIBUTE_HIDDEN 0x00000002UL
#define FILE_ATTRIBUTE_NORMAL 0x00000080UL
#define FILE_ATTRIBUTE_TEMPORARY 0x00000100UL
#define FILE_FLAG_DELETE_ON_CLOSE 0x04000000UL
#define FILE_BEGIN 0UL
#define INVALID_SET_FILE_POINTER 0xFFFFFFFFUL
#define PAGE_EXECUTE_READWRITE 0x40UL
#define MEM_COMMIT 0x1000UL
#define MEM_RESERVE 0x2000UL
#define PAGE_NOACCESS 0x01UL
#define PAGE_GUARD 0x100UL
#define HEAP_ZERO_MEMORY 0x00000008UL
#define ERROR_INSUFFICIENT_BUFFER 122UL
#define CP_ACP 0U
#define CP_SHIFTJIS 932U
#define CP_GBK 936U
#define GB2312_CHARSET 134U
#define WC_NO_BEST_FIT_CHARS 0x00000400UL
#define GGO_BITMAP 1U
#define GDI_ERROR 0xFFFFFFFFUL
#define FW_NORMAL 400
#define OUT_DEFAULT_PRECIS 0U
#define CLIP_DEFAULT_PRECIS 0U
#define NONANTIALIASED_QUALITY 3U
#define DEFAULT_PITCH 0U
#define MAX_PATH_W 520
#define BUCKET_COUNT 65536U
#define INVALID_INDEX 0xFFFFFFFFUL

/*
 * Windows 异常代码。诊断版只记录真正值得调查的 CPU/内存异常，
 * 不记录普通 C++/调试器等软件异常，避免把日志刷满。
 */
#define EXCEPTION_ACCESS_VIOLATION       0xC0000005UL
#define EXCEPTION_IN_PAGE_ERROR          0xC0000006UL
#define EXCEPTION_ILLEGAL_INSTRUCTION    0xC000001DUL
#define EXCEPTION_INT_DIVIDE_BY_ZERO     0xC0000094UL
#define EXCEPTION_INT_OVERFLOW           0xC0000095UL
#define EXCEPTION_STACK_OVERFLOW         0xC00000FDUL
#define EXCEPTION_CONTINUE_SEARCH        0L
#define MAXIMUM_EXCEPTION_PARAMETERS     15U
#define SIZE_OF_80387_REGISTERS          80U
#define MAXIMUM_SUPPORTED_EXTENSION      512U

/* ---------- structures ---------- */
typedef struct _LOGFONTW_MIN {
    LONG lfHeight, lfWidth, lfEscapement, lfOrientation, lfWeight;
    BYTE lfItalic, lfUnderline, lfStrikeOut, lfCharSet;
    BYTE lfOutPrecision, lfClipPrecision, lfQuality, lfPitchAndFamily;
    WCHAR lfFaceName[32];
} LOGFONTW_MIN;

typedef struct _LOGFONTA_MIN {
    LONG lfHeight, lfWidth, lfEscapement, lfOrientation, lfWeight;
    BYTE lfItalic, lfUnderline, lfStrikeOut, lfCharSet;
    BYTE lfOutPrecision, lfClipPrecision, lfQuality, lfPitchAndFamily;
    char lfFaceName[32];
} LOGFONTA_MIN;

typedef struct _TEXTMETRICA_MIN {
    LONG tmHeight,tmAscent,tmDescent,tmInternalLeading,tmExternalLeading;
    LONG tmAveCharWidth,tmMaxCharWidth,tmWeight,tmOverhang,tmDigitizedAspectX,tmDigitizedAspectY;
    BYTE tmFirstChar,tmLastChar,tmDefaultChar,tmBreakChar,tmItalic,tmUnderlined,tmStruckOut,tmPitchAndFamily,tmCharSet;
} TEXTMETRICA_MIN;

typedef struct _POINT_MIN { LONG x,y; } POINT_MIN;
typedef struct _FIXED_MIN { WORD fract; SHORT value; } FIXED_MIN;
typedef struct _MAT2_MIN { FIXED_MIN eM11,eM12,eM21,eM22; } MAT2_MIN;
typedef struct _GLYPHMETRICS_MIN {
    UINT gmBlackBoxX,gmBlackBoxY;
    POINT_MIN gmptGlyphOrigin;
    SHORT gmCellIncX,gmCellIncY;
} GLYPHMETRICS_MIN;

typedef struct _MEMORY_BASIC_INFORMATION_MIN {
    PVOID BaseAddress;
    PVOID AllocationBase;
    DWORD AllocationProtect;
    SIZE_T RegionSize;
    DWORD State;
    DWORD Protect;
    DWORD Type;
} MEMORY_BASIC_INFORMATION_MIN;

/*
 * 下面三个结构严格按照 32 位 Windows 的异常上下文布局定义。
 * 我们故意不包含 windows.h，因此必须把 VEH 回调真正会读取的字段自己写出来。
 *
 * FLOATING_SAVE_AREA_MIN 看起来很长，但它位于 CONTEXT 的通用寄存器之前；
 * 如果省略它，EIP/ESP 等字段偏移就会整体错位，日志会显示完全假的寄存器值。
 */
typedef struct _FLOATING_SAVE_AREA_MIN {
    DWORD ControlWord;
    DWORD StatusWord;
    DWORD TagWord;
    DWORD ErrorOffset;
    DWORD ErrorSelector;
    DWORD DataOffset;
    DWORD DataSelector;
    BYTE RegisterArea[SIZE_OF_80387_REGISTERS];
    DWORD Cr0NpxState;
} FLOATING_SAVE_AREA_MIN;

typedef struct _CONTEXT_X86_MIN {
    DWORD ContextFlags;
    DWORD Dr0,Dr1,Dr2,Dr3,Dr6,Dr7;
    FLOATING_SAVE_AREA_MIN FloatSave;
    DWORD SegGs,SegFs,SegEs,SegDs;
    DWORD Edi,Esi,Ebx,Edx,Ecx,Eax;
    DWORD Ebp,Eip,SegCs,EFlags,Esp,SegSs;
    BYTE ExtendedRegisters[MAXIMUM_SUPPORTED_EXTENSION];
} CONTEXT_X86_MIN;

typedef struct _EXCEPTION_RECORD_MIN {
    DWORD ExceptionCode;
    DWORD ExceptionFlags;
    struct _EXCEPTION_RECORD_MIN* ExceptionRecord;
    PVOID ExceptionAddress;
    DWORD NumberParameters;
    ULONG_PTR ExceptionInformation[MAXIMUM_EXCEPTION_PARAMETERS];
} EXCEPTION_RECORD_MIN;

typedef struct _EXCEPTION_POINTERS_MIN {
    EXCEPTION_RECORD_MIN* ExceptionRecord;
    CONTEXT_X86_MIN* ContextRecord;
} EXCEPTION_POINTERS_MIN;

typedef struct _UNICODE_STRING_MIN {
    WORD Length;
    WORD MaximumLength;
    WCHAR* Buffer;
} UNICODE_STRING_MIN;

typedef struct _LIST_ENTRY_MIN {
    struct _LIST_ENTRY_MIN* Flink;
    struct _LIST_ENTRY_MIN* Blink;
} LIST_ENTRY_MIN;

/* ---------- API function types ---------- */
typedef HANDLE (STDCALL *PFN_CreateThread)(PVOID, SIZE_T, DWORD (STDCALL *)(PVOID), PVOID, DWORD, LPDWORD);
typedef BOOL   (STDCALL *PFN_CloseHandle)(HANDLE);
typedef HANDLE (STDCALL *PFN_CreateFileW)(LPCWSTR,DWORD,DWORD,PVOID,DWORD,DWORD,HANDLE);
typedef HANDLE (STDCALL *PFN_CreateFileA)(LPCSTR,DWORD,DWORD,PVOID,DWORD,DWORD,HANDLE);
typedef DWORD  (STDCALL *PFN_GetFileSize)(HANDLE,LPDWORD);
typedef BOOL   (STDCALL *PFN_ReadFile)(HANDLE,PVOID,DWORD,LPDWORD,PVOID);
typedef BOOL   (STDCALL *PFN_WriteFile)(HANDLE,const void*,DWORD,LPDWORD,PVOID);
typedef BOOL   (STDCALL *PFN_FlushFileBuffers)(HANDLE);
typedef DWORD  (STDCALL *PFN_SetFilePointer)(HANDLE,LONG,LONG*,DWORD);
typedef HANDLE (STDCALL *PFN_GetProcessHeap)(void);
typedef PVOID  (STDCALL *PFN_HeapAlloc)(HANDLE,DWORD,SIZE_T);
typedef BOOL   (STDCALL *PFN_HeapFree)(HANDLE,DWORD,PVOID);
typedef BOOL   (STDCALL *PFN_VirtualProtect)(PVOID,SIZE_T,DWORD,LPDWORD);
typedef PVOID  (STDCALL *PFN_VirtualAlloc)(PVOID,SIZE_T,DWORD,DWORD);
typedef SIZE_T (STDCALL *PFN_VirtualQuery)(const void*,MEMORY_BASIC_INFORMATION_MIN*,SIZE_T);
typedef BOOL   (STDCALL *PFN_FlushInstructionCache)(HANDLE,const void*,SIZE_T);
typedef HANDLE (STDCALL *PFN_GetCurrentProcess)(void);
typedef DWORD  (STDCALL *PFN_GetModuleFileNameW)(HMODULE,LPWSTR,DWORD);
/*
 * Windows 自带的 INI 读取 API。这里用 W 版而不是 A 版，是为了让 FontFace 可以直接写
 * Unicode 字体名；例如“微软雅黑”不需要依赖系统当前 ANSI 代码页。
 */
typedef DWORD  (STDCALL *PFN_GetPrivateProfileStringW)(LPCWSTR,LPCWSTR,LPCWSTR,LPWSTR,DWORD,LPCWSTR);
typedef void   (STDCALL *PFN_SetLastError)(DWORD);
typedef void   (STDCALL *PFN_Sleep)(DWORD);
typedef int    (STDCALL *PFN_MultiByteToWideChar)(UINT,DWORD,LPCCH,int,LPWSTR,int);
typedef int    (STDCALL *PFN_WideCharToMultiByte)(UINT,DWORD,LPCWSTR,int,LPSTR,int,LPCCH,LPBOOL);
typedef FARPROC(STDCALL *PFN_GetProcAddress)(HMODULE,LPCSTR);
typedef LONG   (STDCALL *PFN_VectoredExceptionHandler)(EXCEPTION_POINTERS_MIN*);
typedef PVOID  (STDCALL *PFN_AddVectoredExceptionHandler)(ULONG,PFN_VectoredExceptionHandler);
typedef HFONT  (STDCALL *PFN_CreateFontIndirectW)(const LOGFONTW_MIN*);
typedef HFONT  (STDCALL *PFN_CreateFontW)(int,int,int,int,int,DWORD,DWORD,DWORD,DWORD,DWORD,DWORD,DWORD,DWORD,LPCWSTR);
typedef HFONT  (STDCALL *PFN_CreateFontIndirectA)(const LOGFONTA_MIN*);
typedef DWORD  (STDCALL *PFN_GetGlyphOutlineA)(HDC,UINT,UINT,GLYPHMETRICS_MIN*,DWORD,PVOID,const MAT2_MIN*);
typedef BOOL   (STDCALL *PFN_GetTextMetricsA)(HDC,TEXTMETRICA_MIN*);
typedef HDC    (STDCALL *PFN_CreateCompatibleDC)(HDC);
typedef BOOL   (STDCALL *PFN_DeleteDC)(HDC);
typedef HGDIOBJ(STDCALL *PFN_SelectObject)(HDC,HGDIOBJ);
typedef BOOL   (STDCALL *PFN_DeleteObject)(HGDIOBJ);
typedef DWORD  (STDCALL *PFN_GetGlyphOutlineW)(HDC,UINT,UINT,GLYPHMETRICS_MIN*,DWORD,PVOID,const MAT2_MIN*);
typedef int    (STDCALL *PFN_GetTextFaceW)(HDC,int,LPWSTR);
typedef LPSTR  (STDCALL *PFN_lstrcpyA)(LPSTR,LPCSTR);
typedef LPSTR  (STDCALL *PFN_lstrcatA)(LPSTR,LPCSTR);
typedef int    (STDCALL *PFN_wvsprintfA)(LPSTR,LPCSTR,PVOID);
typedef int    (THISCALL *PFN_EngineOpenResource)(PVOID,PVOID,LPCSTR,DWORD);
/*
 * The original 2003 resource-stream opener begins by resetting the stream object.
 * Our DAT overlay must do the same thing before replacing its backing buffer; otherwise
 * a reused stream object could keep stale pointers or ownership state from the previous file.
 */
typedef int    (THISCALL *PFN_EngineStreamReset)(PVOID);

/* ---------- globals ---------- */
static volatile LONG g_started = 0;
/*
 * Windows passes the DLL module handle to DllMain when the ASI is loaded.
 * We keep that handle because GetModuleFileNameW(NULL, ...) asks for the GAME EXE path,
 * while GetModuleFileNameW(g_self_module, ...) asks for the ASI file itself.  The user
 * explicitly requires BaldrForceCN.log to live beside BaldrForceCN.asi, even when an ASI
 * loader puts the plugin in a subdirectory.
 */
static HINSTANCE g_self_module = NULL;
static HANDLE g_heap = NULL;
static HANDLE g_log = INVALID_HANDLE_VALUE;
static BYTE* g_pac = NULL;
static DWORD g_pac_size = 0;

static PFN_CreateThread pCreateThread = NULL;
static PFN_CloseHandle pCloseHandle = NULL;
static PFN_CreateFileW pCreateFileW = NULL;
static PFN_CreateFileA g_sys_createfilea = NULL;
static PFN_GetFileSize pGetFileSize = NULL;
static PFN_ReadFile pReadFile = NULL;
static PFN_WriteFile pWriteFile = NULL;
static PFN_FlushFileBuffers pFlushFileBuffers = NULL;
static PFN_SetFilePointer pSetFilePointer = NULL;
static PFN_GetProcessHeap pGetProcessHeap = NULL;
static PFN_HeapAlloc pHeapAlloc = NULL;
static PFN_HeapFree pHeapFree = NULL;
static PFN_VirtualProtect pVirtualProtect = NULL;
static PFN_VirtualAlloc pVirtualAlloc = NULL;
static PFN_VirtualQuery pVirtualQuery = NULL;
static PFN_FlushInstructionCache pFlushInstructionCache = NULL;
static PFN_GetCurrentProcess pGetCurrentProcess = NULL;
static PFN_GetModuleFileNameW pGetModuleFileNameW = NULL;
static PFN_GetPrivateProfileStringW pGetPrivateProfileStringW = NULL;
static PFN_SetLastError pSetLastError = NULL;
static PFN_Sleep pSleep = NULL;
static PFN_AddVectoredExceptionHandler pAddVectoredExceptionHandler = NULL;

static PFN_MultiByteToWideChar g_sys_mbtowc = NULL;
static PFN_WideCharToMultiByte g_sys_wctomb = NULL;
static PFN_CreateFontIndirectW g_sys_font = NULL;
static PFN_CreateFontW g_gdi_createfont = NULL;
static PFN_CreateFontIndirectA g_gdi_font_a = NULL;
static PFN_GetGlyphOutlineA g_gdi_glyph_a = NULL;
static PFN_GetTextMetricsA g_gdi_metrics_a = NULL;
static PFN_CreateCompatibleDC g_gdi_createdc = NULL;
static PFN_DeleteDC g_gdi_deletedc = NULL;
static PFN_SelectObject g_gdi_select = NULL;
static PFN_DeleteObject g_gdi_deleteobj = NULL;
static PFN_GetGlyphOutlineW g_gdi_glyph = NULL;
static PFN_GetTextFaceW g_gdi_textface = NULL;
static PFN_GetProcAddress g_orig_getproc = NULL;
static PFN_lstrcpyA g_orig_lstrcpyA = NULL;
static PFN_lstrcatA g_orig_lstrcatA = NULL;
static PFN_wvsprintfA g_orig_wvsprintfA = NULL;
static PFN_EngineOpenResource g_orig_engine_open_resource = NULL;
static PFN_EngineStreamReset g_engine_stream_reset = NULL;

static int g_enable_font_hook = 1;
static int g_enable_wctomb_hook = 1;
static int g_enable_log = 1;
static int g_force_charset = GB2312_CHARSET;
/*
 * FontFace 是 v1.0.4 唯一允许用户改变的视觉参数。
 * 空字符串表示完全沿用此前实机通过的字体选择逻辑；非空时，普通游戏字体 Hook 和
 * Runtime 自己的 GBK 字形生成器都会使用这个 Unicode 字体名。
 */
static WCHAR g_force_face[32];
static DWORD g_context_bytes = 128;
static DWORD g_ambiguous_fallbacks = 0;
static DWORD g_context_resolved = 0;
static DWORD g_exact_script_resolved = 0;
static DWORD g_hits_ja = 0;
static DWORD g_hits_gbk = 0;
static DWORD g_script_hook_hits = 0;
static DWORD g_script_hook_replaced = 0;
/*
 * 这两个计数器服务于 v1.0.4 的“双长度语义”修复。
 *
 * 两份 v1.0.3 实机崩溃日志证明：
 *   - 脚本源流的读取偏移必须按原始日文 count 前进；
 *   - 运行时 value object 的 dest-4 byte_count 必须反映实际写入的中文 GBK 长度+NUL。
 *
 * runtime_count_updated 统计已经把运行时对象长度更新为中文长度的次数；
 * length_delta_seen 统计其中中日长度不同的次数。它们只用于日志/诊断，不参与其它决策。
 */
static DWORD g_script_runtime_count_updated = 0;
static DWORD g_script_length_delta_seen = 0;
static DWORD g_crash_scene_marker_hits = 0;
static DWORD g_gbk_copy_propagations = 0;
static DWORD g_gbk_format_propagations = 0;
static PVOID g_script_copy_target = NULL;
static BYTE g_glyph_tmp[4096];

/*
 * ---------- 崩溃诊断状态 ----------
 *
 * v1.0.3 的两份实机异常已经把八木泽名片崩溃和 Backlog 截断崩溃分成两个根因。
 * 本版在修复两条根因的同时继续保存最近 128 次脚本复制事件，并在真正的 CPU 异常发生时
 * 把异常地址、寄存器、栈顶和这些事件一起写进 BaldrForceCN.log。
 *
 * 这里全部使用固定大小数组，不在异常回调里 HeapAlloc。原因是：发生访问冲突时堆本身也可能处于
 * 不稳定状态；诊断代码越少依赖复杂运行库，越不容易把原始现场盖掉。
 */
typedef struct _SCRIPT_DIAG_EVENT {
    DWORD sequence;       /* 全局事件序号，帮助确认真实先后顺序。 */
    DWORD cur;            /* 调用脚本复制函数时 state+0x114 的原始读取偏移。 */
    DWORD count;          /* clean 脚本要求复制的原始字节数（通常含结尾 NUL）。 */
    DWORD dest;           /* 目标 value object 字符缓冲地址，只保存数值，不在崩溃时解引用。 */
    DWORD source_abs;     /* 命中 BFET 时，源日文字符串在当前脚本 payload 内的偏移。 */
    DWORD ja_hash;        /* 命中 BFET 时的日文 FNV/hash；未命中则为 0。 */
    DWORD zh_hash;        /* 命中 BFET 时的中文 GBK hash；未命中则为 0。 */
    DWORD ja_len;         /* 日文字节数，不含结尾 NUL。 */
    DWORD zh_len;         /* 中文 GBK 字节数，不含结尾 NUL。 */
    DWORD mapping_index;  /* 当前 .bin 内的 BFET 序号；例如 jyosyo05.bin #64。 */
    DWORD state_addr;     /* 脚本解释器 state 指针的数值快照。 */
    DWORD body_addr;      /* 当前脚本 body 起点；和 cur/source_abs 一起可还原现场。 */
    DWORD limit;          /* 当前脚本可读上限 state+0x0C。 */
    DWORD post_cur;       /* 按旧汉化语义推进后的 state+0x114，正常应为 cur+count。 */
    DWORD value_type;     /* 若目标对象可识别，保存 dest-8 的 type；通常 0x0B。 */
    DWORD value_count;    /* 保存 dest-4 的“当前运行时 byte_count”。
                         * 事件刚建立时它等于原脚本 count；命中翻译并写入中文后，
                         * v1.0.4 会把它更新成中文实际长度+NUL。这样崩溃日志可以同时
                         * 对照 source_count 与 value_count，判断是哪一层长度出了问题。 */
    DWORD flags;
    /*
     * flags 位定义：
     * bit0  = 命中 BFET；
     * bit1  = 命中 after.bin direct-entry；
     * bit2  = 目标确认为 opcode 0x0B value object；
     * bit3  = 中文比原日文长（zh_len + 1 > count）；
     * bit4  = 如果后续代码按原 count 再复制中文，会刚好截在 GBK 双字节中间；
     * bit5  = 中文自身的 「/」 数量不平衡；
     * bit6  = 日文和中文的 「/」 个数发生变化。
     *
     * bit3/bit4 是 v1.0.3 诊断阶段特别增加的字段。用户实机已经看到
     * “「………什么……」”末尾变成方块，随后打开 Backlog 崩溃；静态审计证明
     * jyosyo05.bin #64 正是 20 -> 26 字节，如果按原 count=21 二次复制，
     * 第 21 个字节会停在一个 GBK 汉字的首字节上。
     */
    char script_name[32]; /* 固定快照；异常时不需要再访问 PAC 表。 */
    BYTE ja_preview[32];  /* 日文源串前 32 字节的原始十六进制证据。 */
    BYTE zh_preview[32];  /* 中文目标串前 32 字节的原始十六进制证据。 */
    BYTE ja_preview_len;
    BYTE zh_preview_len;
    BYTE reserved0;
    BYTE reserved1;
} SCRIPT_DIAG_EVENT;

/*
 * 32 条在多崩溃场景下太短：一句剧情会伴随多个临时值/控制串复制。
 * 扩到 128 条仍只有几十 KB，却可以覆盖“出问题前一整段对话”，便于一次日志直接定位。
 */
#define SCRIPT_DIAG_RING 128U

/*
 * 渲染事件环形缓冲用于区分“脚本对象已经坏了”与“脚本正常、最后死在字形路径”。
 * type=1 表示普通 0x80..0xFE DBCS 分类，type=2 表示低层 1bpp mask 构建。
 */
typedef struct _GLYPH_DIAG_EVENT {
    DWORD sequence;
    DWORD type;
    DWORD code;
    DWORD arg1;
    DWORD arg2;
} GLYPH_DIAG_EVENT;
#define GLYPH_DIAG_RING 64U
static GLYPH_DIAG_EVENT g_glyph_diag[GLYPH_DIAG_RING];
static DWORD g_glyph_diag_sequence = 0;

static SCRIPT_DIAG_EVENT g_script_diag[SCRIPT_DIAG_RING];
static DWORD g_script_diag_sequence = 0;
static volatile LONG g_exception_handler_busy = 0;
static DWORD g_exception_log_count = 0;
static PVOID g_vectored_handler_cookie = NULL;
static BYTE* g_diag_main_base = NULL;
static DWORD g_diag_main_size = 0;
static BYTE* g_diag_self_base = NULL;
static DWORD g_diag_self_size = 0;
/*
 * BFET 结构风险统计只用于诊断，不参与翻译选择。
 * 这些计数让日志一启动就告诉我们“长度扩张/引号结构”是否可能影响 Backlog 等二次消费者。
 */
static DWORD g_bfet_len_equal = 0;
static DWORD g_bfet_zh_shorter = 0;
static DWORD g_bfet_zh_longer = 0;
static DWORD g_bfet_original_count_dbcs_split = 0;
static DWORD g_bfet_zh_quote_unbalanced = 0;
static DWORD g_bfet_quote_count_changed = 0;
static DWORD g_runtime_expansion_diag_logs = 0;

static WCHAR g_base_dir[MAX_PATH_W];
/* Directory that contains BaldrForceCN.asi.  Only the log is anchored here; game resources
 * intentionally keep their existing game-directory layout so test9 changes one variable at a time. */
static WCHAR g_asi_dir[MAX_PATH_W];
static WCHAR g_update_path[MAX_PATH_W];
static WCHAR g_log_path[MAX_PATH_W];
static WCHAR g_ini_path[MAX_PATH_W];

/* Translation entry. All byte pointers refer into g_pac and stay valid for process lifetime. */
typedef struct _TRANS_ENTRY {
    DWORD hash_ja;
    DWORD hash_zh_mb;
    DWORD hash_zh_w;
    const BYTE* ja;
    DWORD ja_len;
    const BYTE* zh_mb;
    DWORD zh_mb_len;
    WCHAR* zh_w;
    DWORD zh_w_len;
    const BYTE* payload;
    DWORD payload_size;
    DWORD source_abs;
    DWORD source_rel;
    DWORD text_base;
    /*
     * 诊断字段：script_name 直接指向 Update.pac 的 PACw 文件名表。
     * g_pac 在整个进程生命周期都不会释放，所以这个指针在崩溃时仍然有效。
     * mapping_index 是这个 .bin 自己的 BFET 映射序号；以后日志可直接写成
     * “jyosyo05.bin #64”，不需要再用 hash 猜到底是哪一句。
     */
    const char* script_name;
    DWORD mapping_index;
    DWORD next_ja;
    DWORD next_zh_mb;
    DWORD next_zh_w;
} TRANS_ENTRY;

static TRANS_ENTRY* g_trans = NULL;
static DWORD g_trans_count = 0;
static DWORD* g_head_ja = NULL;
static DWORD* g_head_zh_mb = NULL;
static DWORD* g_head_zh_w = NULL;
static WCHAR* g_wide_pool = NULL;

/* Direct GBK strings, currently used for after.bin (no BFET). */
typedef struct _DIRECT_ENTRY {
    DWORD hash_mb;
    DWORD hash_w;
    const BYTE* mb;
    DWORD mb_len;
    WCHAR* w;
    DWORD w_len;
    DWORD next_mb;
    DWORD next_w;
} DIRECT_ENTRY;
static DIRECT_ENTRY* g_direct = NULL;
static DWORD g_direct_count = 0;
static DWORD* g_head_direct_mb = NULL;
static DWORD* g_head_direct_w = NULL;
static WCHAR* g_direct_wide_pool = NULL;

/*
 * after.bin is a localization-added script stored inside Update.pac/BFE.PAC and has no BFET tail.
 * Its 9 direct GBK strings are indexed in g_direct.  The first time one of those strings is actually
 * consumed by opcode 0x0B, we log it once.  That is stronger evidence than merely seeing after.bin
 * inside the PAC: it proves the running game reached and executed text from that added script.
 */
static DWORD g_after_bin_direct_runtime_hits = 0;

#include "ui_patch_data_gbk.h"
#include "legacy_static_patch_data_gbk.h"
#include "encoding_normalization_gbk.h"
#include "late_static_patch_data_gbk.h"

/* ---------- localized content overlays ----------
 * Old-localization oracle:
 *   - direct DAT payloads live uncompressed in the recovered localized Update.pac;
 *   - the old MoleBox/STEELBOX VFS has 311 loose files besides the two PACs; the user compared
 *     those 311 against the clean original and found exactly 9 byte-different files;
 *   - v1.0.2 embeds exactly those 9 old-localization files and lets all 302 byte-identical files
 *     fall through to the clean game's originals.
 *
 * The important rule is “exact old bytes, exact original path”.  We do not reinterpret GRP/ANI/SPR/WAZ
 * formats here.  The clean 2003 game receives the same bytes through an ordinary Windows file handle
 * and continues through its original parser.
 */
#define RESOURCE_OVERLAY_MAX 16U
typedef struct _RESOURCE_OVERLAY_ENTRY {
    const char* name;
    const BYTE* data;
    DWORD size;
    DWORD hits;
} RESOURCE_OVERLAY_ENTRY;
static RESOURCE_OVERLAY_ENTRY g_resource_overlays[RESOURCE_OVERLAY_MAX];
static DWORD g_resource_overlay_count=0;
static DWORD g_resource_overlay_hits=0;
static DWORD g_embedded_asset_open_failures=0;
static DWORD g_embedded_temp_serial=0;

/*
 * 渲染计数既供字体代码自身统计，也供崩溃 VEH 输出阶段快照，因此必须在诊断函数之前定义。
 * 这里只移动定义位置，不改变任何计数更新逻辑。
 */
static DWORD g_gbk_mask_builds=0;
static DWORD g_gbk_mask_failures=0;
static DWORD g_gbk_renderer_hits=0;
static DWORD g_gbk_glyph_builds=0;
static DWORD g_gbk_glyph_failures=0;
/* ANSI 取字失败后由 Unicode 路径救回的次数，用来确认八木泽崩溃修复是否实际命中。 */
static DWORD g_gbk_outline_ansi_failures=0;
static DWORD g_gbk_outline_unicode_fallbacks=0;
static DWORD g_gbk_outline_hard_failures=0;


/*
 * 这些渲染计数原本定义在字体实现附近。崩溃 VEH 也需要读取它们，所以把定义提前到
 * 全局状态区；后面的字体代码仍然直接使用同一组变量，没有任何运行时语义变化。
 */

static PVOID g_engine_open_resource_site=NULL;


/* ---------- ASI-embedded loose-resource manifest ----------
 *
 * embedded_assets.S uses .incbin to place each original file byte-for-byte in the ASI .rdata section.
 * Every pair below is therefore just two addresses: the first byte and one-past-the-last byte.
 * Keeping start/end instead of a hand-written size prevents a stale length constant from truncating a file.
 */
extern const BYTE g_embedded_hell_font_grp_start[], g_embedded_hell_font_grp_end[];
extern const BYTE g_embedded_hell_menumsg_grp_start[], g_embedded_hell_menumsg_grp_end[];
extern const BYTE g_embedded_hell_menu03_grp_start[], g_embedded_hell_menu03_grp_end[];
extern const BYTE g_embedded_cpu_genha_start[], g_embedded_cpu_genha_end[];
extern const BYTE g_embedded_cpu_kaira_start[], g_embedded_cpu_kaira_end[];
extern const BYTE g_embedded_cpu_zako_s_t01_start[], g_embedded_cpu_zako_s_t01_end[];
extern const BYTE g_embedded_neko05_waz_start[], g_embedded_neko05_waz_end[];
extern const BYTE g_embedded_tooru_waz_start[], g_embedded_tooru_waz_end[];
extern const BYTE g_embedded_yagisawa_waz_start[], g_embedded_yagisawa_waz_end[];

typedef struct _EMBEDDED_LOOSE_ASSET {
    /* path 是游戏原本请求的相对路径。只做大小写/斜杠宽容匹配，不改文件名。 */
    const char* path;
    /* begin/end 指向 ASI .rdata 中用 .incbin 放进去的原始文件字节。 */
    const BYTE* begin;
    const BYTE* end;
    /* hits 只用于日志；每个资源第一次真正被游戏打开时记录一次。 */
    DWORD hits;
} EMBEDDED_LOOSE_ASSET;

/*
 * 发行版只列出 9 个“用户已经与 clean 原版做过二进制对比、确认不同”的 loose 文件。
 * 这张表是白名单：不在表里的 302 个 loose 文件绝不会被 ASI 接管，而是直接回到原版。
 * 这样既完整保留旧汉化确实修改过的字节，也避免 test12 那种把整个 Hell 目录一起覆盖的冗余。
 */
static EMBEDDED_LOOSE_ASSET g_embedded_assets[] = {
    {"Bmp\\Hell\\Font.grp",         g_embedded_hell_font_grp_start,       g_embedded_hell_font_grp_end,       0},
    {"Bmp\\Hell\\MenuMsg.grp",      g_embedded_hell_menumsg_grp_start,    g_embedded_hell_menumsg_grp_end,    0},
    {"Bmp\\Hell\\hell_Menu03.grp",  g_embedded_hell_menu03_grp_start,     g_embedded_hell_menu03_grp_end,     0},
    {"Dat\\Cpu\\genha.cpu",          g_embedded_cpu_genha_start,           g_embedded_cpu_genha_end,           0},
    {"Dat\\Cpu\\kaira.cpu",          g_embedded_cpu_kaira_start,           g_embedded_cpu_kaira_end,           0},
    {"Dat\\Cpu\\zako_s_t01.cpu",     g_embedded_cpu_zako_s_t01_start,      g_embedded_cpu_zako_s_t01_end,      0},
    {"Dat\\Waza\\neko05.waz",        g_embedded_neko05_waz_start,          g_embedded_neko05_waz_end,          0},
    {"Dat\\Waza\\TOORU.WAZ",         g_embedded_tooru_waz_start,           g_embedded_tooru_waz_end,           0},
    {"Dat\\Waza\\YAGISAWA.WAZ",      g_embedded_yagisawa_waz_start,        g_embedded_yagisawa_waz_end,        0}
};
#define EMBEDDED_ASSET_COUNT ((DWORD)(sizeof(g_embedded_assets)/sizeof(g_embedded_assets[0])))


/* ---------- tiny runtime helpers ---------- */
static DWORD u32(const void* p) { const BYTE* b=(const BYTE*)p; return (DWORD)b[0] | ((DWORD)b[1]<<8) | ((DWORD)b[2]<<16) | ((DWORD)b[3]<<24); }
static WORD u16(const void* p) { const BYTE* b=(const BYTE*)p; return (WORD)(b[0] | ((WORD)b[1]<<8)); }
static void mem_copy(void* d,const void* s,DWORD n){ BYTE* a=(BYTE*)d; const BYTE* b=(const BYTE*)s; while(n--) *a++=*b++; }
static void mem_zero(void* d,DWORD n){ BYTE* a=(BYTE*)d; while(n--)*a++=0; }
static int mem_equal(const void* a,const void* b,DWORD n){ const BYTE*x=(const BYTE*)a,*y=(const BYTE*)b; while(n--) if(*x++!=*y++) return 0; return 1; }
static DWORD c_len_bounded(const BYTE* s,DWORD max){ DWORD n=0; while(n<max && s[n]) ++n; return n; }
static DWORD a_len(const char* s){ DWORD n=0; if(!s)return 0; while(s[n])++n; return n; }
static DWORD w_len(const WCHAR* s){ DWORD n=0; if(!s)return 0; while(s[n])++n; return n; }
static char lower_a(char c){ if(c>='A'&&c<='Z') return (char)(c+32); return c; }
static WCHAR lower_w(WCHAR c){ if(c>='A'&&c<='Z') return (WCHAR)(c+32); return c; }
static int a_eq(const char*a,const char*b){ while(*a&&*b){ if(lower_a(*a)!=lower_a(*b))return 0; ++a;++b;} return *a==*b; }
static int w_eq_ascii_n(const WCHAR* w,WORD wchar_count,const char* a){ DWORD i=0,al=a_len(a); if((DWORD)wchar_count!=al)return 0; for(i=0;i<al;i++) if(lower_w(w[i])!=(WCHAR)lower_a(a[i]))return 0; return 1; }
static DWORD fnv_bytes(const BYTE* p,DWORD n){ DWORD h=2166136261UL; while(n--){ h^=*p++; h*=16777619UL; } return h; }
static DWORD fnv_wide(const WCHAR* p,DWORD n){ DWORD h=2166136261UL; while(n--){ WORD w=*p++; h^=(BYTE)w;h*=16777619UL; h^=(BYTE)(w>>8);h*=16777619UL;} return h; }
static int has_cjk(const WCHAR* p,DWORD n){ DWORD i; for(i=0;i<n;i++){ WORD c=p[i]; if((c>=0x3400&&c<=0x9FFF)||c>=0xF900) return 1;} return 0; }

static void* get_peb(void){
#ifdef _MSC_VER
    void* p;
    __asm {
        mov eax, fs:[0x30]
        mov p, eax
    }
    return p;
#else
    void* p; __asm__("movl %%fs:0x30,%0":"=r"(p)); return p;
#endif
}

/* ---------- loaded-module and export resolver ---------- */
static PVOID find_loaded_module(const char* ascii_name){
    BYTE* peb=(BYTE*)get_peb();
    BYTE* ldr=*(BYTE**)(peb+0x0C);
    LIST_ENTRY_MIN* head=(LIST_ENTRY_MIN*)(ldr+0x14);
    LIST_ENTRY_MIN* cur=head->Flink;
    while(cur && cur!=head){
        BYTE* ent=(BYTE*)cur-8;
        PVOID base=*(PVOID*)(ent+0x18);
        UNICODE_STRING_MIN* bn=(UNICODE_STRING_MIN*)(ent+0x2C);
        if(bn->Buffer && w_eq_ascii_n(bn->Buffer,(WORD)(bn->Length/2),ascii_name)) return base;
        cur=cur->Flink;
    }
    return NULL;
}

static PVOID resolve_export_ordinal(PVOID module,DWORD ordinal);
static PVOID resolve_export(PVOID module,const char* name){
    BYTE* b=(BYTE*)module; DWORD peoff,exrva,exsz,nname,i; BYTE* ex;
    if(!b || u16(b)!=0x5A4D) return NULL;
    peoff=u32(b+0x3C); if(u32(b+peoff)!=0x00004550) return NULL;
    if(u16(b+peoff+24)!=0x10B) return NULL;
    exrva=u32(b+peoff+24+96); exsz=u32(b+peoff+24+100); if(!exrva||!exsz)return NULL;
    ex=b+exrva; nname=u32(ex+24);
    {
        DWORD* names=(DWORD*)(b+u32(ex+32));
        WORD* ords=(WORD*)(b+u32(ex+36));
        DWORD* funcs=(DWORD*)(b+u32(ex+28));
        for(i=0;i<nname;i++){
            const char* nm=(const char*)(b+names[i]);
            if(a_eq(nm,name)){
                DWORD rva=funcs[ords[i]];
                if(rva>=exrva && rva<exrva+exsz){
                    const char* fwd=(const char*)(b+rva); char mod[80]; char fn[100]; DWORD m=0,k=0;
                    while(fwd[m] && fwd[m]!='.' && m<70){mod[m]=fwd[m];m++;} mod[m]=0;
                    if(fwd[m]!='.')return NULL; m++;
                    while(fwd[m] && k<95) fn[k++]=fwd[m++]; fn[k]=0;
                    { DWORD ml=a_len(mod); if(ml<4 || !(lower_a(mod[ml-4])=='.'&&lower_a(mod[ml-3])=='d'&&lower_a(mod[ml-2])=='l'&&lower_a(mod[ml-1])=='l')){mod[ml++]='.';mod[ml++]='d';mod[ml++]='l';mod[ml++]='l';mod[ml]=0;} }
                    { PVOID fm=find_loaded_module(mod); if(!fm)return NULL; if(fn[0]=='#'){ DWORD o=0,j=1; while(fn[j]>='0'&&fn[j]<='9'){o=o*10+(fn[j]-'0');j++;} return resolve_export_ordinal(fm,o);} return resolve_export(fm,fn); }
                }
                return b+rva;
            }
        }
    }
    return NULL;
}
static PVOID resolve_export_ordinal(PVOID module,DWORD ordinal){
    BYTE* b=(BYTE*)module; DWORD peoff,exrva,exsz,baseord,nfunc,rva; BYTE* ex; DWORD* funcs;
    if(!b || u16(b)!=0x5A4D)return NULL; peoff=u32(b+0x3C); if(u32(b+peoff)!=0x00004550)return NULL;
    exrva=u32(b+peoff+24+96);exsz=u32(b+peoff+24+100);if(!exrva)return NULL;ex=b+exrva;
    baseord=u32(ex+16);nfunc=u32(ex+20);if(ordinal<baseord||ordinal>=baseord+nfunc)return NULL;
    funcs=(DWORD*)(b+u32(ex+28));rva=funcs[ordinal-baseord];
    if(rva>=exrva&&rva<exrva+exsz)return NULL; return b+rva;
}

static int resolve_system_apis(void){
    PVOID k=find_loaded_module("kernel32.dll");
    PVOID g=find_loaded_module("gdi32.dll");
    PVOID u=find_loaded_module("user32.dll");
    if(!k) return 0;
#define R(name,type) p##name=(type)resolve_export(k,#name)
    R(CreateThread,PFN_CreateThread); R(CloseHandle,PFN_CloseHandle); R(CreateFileW,PFN_CreateFileW);
    R(GetFileSize,PFN_GetFileSize); R(ReadFile,PFN_ReadFile); R(WriteFile,PFN_WriteFile); R(SetFilePointer,PFN_SetFilePointer);
    pFlushFileBuffers=(PFN_FlushFileBuffers)resolve_export(k,"FlushFileBuffers");
    R(GetProcessHeap,PFN_GetProcessHeap); R(HeapAlloc,PFN_HeapAlloc); R(HeapFree,PFN_HeapFree);
    R(VirtualProtect,PFN_VirtualProtect); R(VirtualAlloc,PFN_VirtualAlloc); R(VirtualQuery,PFN_VirtualQuery); R(FlushInstructionCache,PFN_FlushInstructionCache);
    R(GetCurrentProcess,PFN_GetCurrentProcess); R(GetModuleFileNameW,PFN_GetModuleFileNameW); R(SetLastError,PFN_SetLastError); R(Sleep,PFN_Sleep);
    pGetPrivateProfileStringW=(PFN_GetPrivateProfileStringW)resolve_export(k,"GetPrivateProfileStringW");
    pAddVectoredExceptionHandler=(PFN_AddVectoredExceptionHandler)resolve_export(k,"AddVectoredExceptionHandler");
#undef R
    g_sys_createfilea=(PFN_CreateFileA)resolve_export(k,"CreateFileA");
    g_sys_mbtowc=(PFN_MultiByteToWideChar)resolve_export(k,"MultiByteToWideChar");
    g_sys_wctomb=(PFN_WideCharToMultiByte)resolve_export(k,"WideCharToMultiByte");
    g_orig_lstrcpyA=(PFN_lstrcpyA)resolve_export(k,"lstrcpyA");
    g_orig_lstrcatA=(PFN_lstrcatA)resolve_export(k,"lstrcatA");
    if(u)g_orig_wvsprintfA=(PFN_wvsprintfA)resolve_export(u,"wvsprintfA");
    if(g){
        g_sys_font=(PFN_CreateFontIndirectW)resolve_export(g,"CreateFontIndirectW");
        g_gdi_createfont=(PFN_CreateFontW)resolve_export(g,"CreateFontW");
        g_gdi_font_a=(PFN_CreateFontIndirectA)resolve_export(g,"CreateFontIndirectA");
        g_gdi_glyph_a=(PFN_GetGlyphOutlineA)resolve_export(g,"GetGlyphOutlineA");
        g_gdi_metrics_a=(PFN_GetTextMetricsA)resolve_export(g,"GetTextMetricsA");
        g_gdi_createdc=(PFN_CreateCompatibleDC)resolve_export(g,"CreateCompatibleDC");
        g_gdi_deletedc=(PFN_DeleteDC)resolve_export(g,"DeleteDC");
        g_gdi_select=(PFN_SelectObject)resolve_export(g,"SelectObject");
        g_gdi_deleteobj=(PFN_DeleteObject)resolve_export(g,"DeleteObject");
        g_gdi_glyph=(PFN_GetGlyphOutlineW)resolve_export(g,"GetGlyphOutlineW");
        g_gdi_textface=(PFN_GetTextFaceW)resolve_export(g,"GetTextFaceW");
    }
    if(!pCreateFileW||!pCloseHandle||!pGetFileSize||!pReadFile||!pWriteFile||!pSetFilePointer||!pGetProcessHeap||!pHeapAlloc||!pVirtualProtect||!pGetModuleFileNameW||!g_sys_mbtowc||!g_sys_wctomb) return 0;
    g_heap=pGetProcessHeap(); return g_heap!=NULL;
}

/* ---------- path + logging ---------- */
static void w_copy(WCHAR*d,const WCHAR*s,DWORD cap){DWORD i=0;if(!cap)return;while(i+1<cap&&s&&s[i]){d[i]=s[i];i++;}d[i]=0;}
static void w_append_ascii(WCHAR*d,const char*s,DWORD cap){DWORD n=w_len(d),i=0;while(n+1<cap&&s[i])d[n++]=(WCHAR)(BYTE)s[i++];d[n]=0;}
static void w_append_hex8(WCHAR*d,DWORD v,DWORD cap){
    /* Append exactly eight hexadecimal digits.  Fixed width keeps temporary names deterministic and short. */
    static const char*h="0123456789ABCDEF";DWORD n=w_len(d),i;
    if(n+8>=cap)return;
    for(i=0;i<8;i++)d[n++]=(WCHAR)h[(v>>(28-i*4))&15];
    d[n]=0;
}
static int path_suffix_eq(LPCSTR full,const char* wanted){
    /*
     * The game may pass "Bmp\\Hell\\MenuMsg.grp", ".\\Bmp/Hell/MenuMsg.grp", or an absolute path.
     * We therefore compare only the end of the path, ignore ASCII case, and treat '/' and '\\' equally.
     * No locale function is used: every path component involved here is plain ASCII.
     */
    DWORD fl,wl,i;
    if(!full||!wanted)return 0;
    fl=a_len(full);wl=a_len(wanted);
    if(fl<wl)return 0;
    full+=fl-wl;
    for(i=0;i<wl;i++){
        char a=full[i],b=wanted[i];
        if(a=='/')a='\\';if(b=='/')b='\\';
        if(lower_a(a)!=lower_a(b))return 0;
    }
    return 1;
}
static void trim_to_directory(WCHAR* path,DWORD n){
    DWORD i;
    /*
     * GetModuleFileNameW returns a full file path.  Walk backwards to the final slash,
     * then keep the slash itself as the last character.  Keeping the slash lets the rest
     * of the code append a simple ASCII filename without needing another separator check.
     */
    i=n;
    while(i>0&&path[i-1]!='\\'&&path[i-1]!='/')i--;
    if(i>0)path[i]=0;
    else path[0]=0;
}
static void make_paths(void){
    DWORD n;

    /*
     * Update.pac stays beside BaldrForce.exe because the clean engine already expects its PAC there.
     * v1.0.4 brings back BaldrForceCN.ini only for two safe user-facing choices: FontFace and EnableLog.
     * Encoding/codepage/hook behavior is still fixed in code.  The INI follows the ASI directory, not the EXE.
     * Only the 9 loose files proven byte-different from the clean original are linked into the ASI.
     */
    n=pGetModuleFileNameW(NULL,g_base_dir,MAX_PATH_W-1);
    if(n>=MAX_PATH_W)n=MAX_PATH_W-1;
    g_base_dir[n]=0;
    trim_to_directory(g_base_dir,n);

    /*
     * The log path is different on purpose.  Ask Windows for THIS DLL/ASI path so the log
     * follows BaldrForceCN.asi if the user moves the ASI into an ASI-loader subdirectory.
     * DllMain normally sets g_self_module before the worker thread starts.  The NULL fallback
     * is only defensive for unusual manual loaders that skip the normal DLL attach callback.
     */
    n=pGetModuleFileNameW(g_self_module,g_asi_dir,MAX_PATH_W-1);
    if(n==0){
        w_copy(g_asi_dir,g_base_dir,MAX_PATH_W);
    }else{
        if(n>=MAX_PATH_W)n=MAX_PATH_W-1;
        g_asi_dir[n]=0;
        trim_to_directory(g_asi_dir,n);
    }

    w_copy(g_update_path,g_base_dir,MAX_PATH_W);w_append_ascii(g_update_path,"Update.pac",MAX_PATH_W);
    w_copy(g_log_path,g_asi_dir,MAX_PATH_W);w_append_ascii(g_log_path,"BaldrForceCN.log",MAX_PATH_W);
    w_copy(g_ini_path,g_asi_dir,MAX_PATH_W);w_append_ascii(g_ini_path,"BaldrForceCN.ini",MAX_PATH_W);
}
static void log_raw(const char*s,DWORD n){DWORD wr;if(g_log!=INVALID_HANDLE_VALUE&&pWriteFile)pWriteFile(g_log,s,n,&wr,NULL);}
static void log_s(const char*s){log_raw(s,a_len(s));}
static void log_nl(void){log_raw("\r\n",2);}
static void log_dec(DWORD v){char b[16];DWORD n=0,i;if(v==0){log_raw("0",1);return;}while(v&&n<15){b[n++]=(char)('0'+v%10);v/=10;}for(i=0;i<n/2;i++){char t=b[i];b[i]=b[n-1-i];b[n-1-i]=t;}log_raw(b,n);}
static void log_hex32(DWORD v){char b[10];DWORD i;static const char*h="0123456789ABCDEF";b[0]='0';b[1]='x';for(i=0;i<8;i++)b[2+i]=h[(v>>(28-i*4))&15];log_raw(b,10);}
/*
 * 把一小段原始机器码按“AA BB CC”的形式写入日志。
 * 这个辅助函数只用于失败诊断：如果固定 RVA 上的字节和预期不一样，用户只需
 * 回传日志，我们就能直接看到真实字节，不必再让用户额外开十六进制编辑器。
 * n 在当前调用处最多只有 6，所以这里不需要复杂的缓冲区或动态内存。
 */
static void log_hex_bytes(const BYTE*p,DWORD n){DWORD i;static const char*h="0123456789ABCDEF";char b[3];if(!p)return;for(i=0;i<n;i++){if(i)log_raw(" ",1);b[0]=h[(p[i]>>4)&15];b[1]=h[p[i]&15];b[2]=0;log_raw(b,2);}}
static void log_kv(const char*k,DWORD v){log_s(k);log_dec(v);log_nl();}

/*
 * 把最多 n 个字节完整输出为十六进制。诊断日志故意输出原始字节而不是尝试在异常现场转码，
 * 因为“错误恰好发生在半个 GBK 字符”时，任何自动解码都会把最关键的坏字节隐藏掉。
 */
static void diag_log_hex_block(const BYTE* p,DWORD n){
    DWORD i;static const char*h="0123456789ABCDEF";char b[2];
    if(!p){log_s("<空>");return;}
    for(i=0;i<n;i++){if(i)log_raw(" ",1);b[0]=h[(p[i]>>4)&15];b[1]=h[p[i]&15];log_raw(b,2);}
}

/* 复制 PAC 文件名到固定 32 字节事件快照，避免崩溃时再依赖临时对象。 */
static void diag_copy_script_name(char out[32],const char* in){
    DWORD i=0;if(!out)return;if(in)while(i<31&&in[i]){out[i]=in[i];i++;}out[i]=0;
}

/* 复制最多 32 字节文本前缀。这里只做原始字节快照，不读到调用者给定长度之外。 */
static BYTE diag_copy_preview(BYTE out[32],const BYTE* in,DWORD n){
    DWORD i,m=n<32U?n:32U;for(i=0;i<m;i++)out[i]=in[i];for(;i<32U;i++)out[i]=0;return (BYTE)m;
}

/*
 * 统计 GBK 文本中的某个双字节字符，但只在“字符边界”比较。
 * 不能简单逐字节滑窗搜索 A1 B8：例如前一个汉字的第二字节恰好是 A1、后一个汉字
 * 的第一字节恰好是 B8 时，滑窗会产生一个根本不存在的假「。这里按当前旧汉化
 * 0x80..0xFE lead 规则逐字符前进，因此不会跨两个字符误报。
 */
static DWORD diag_count_gbk_pair(const BYTE* s,DWORD n,BYTE a,BYTE b){
    DWORD i=0,c=0;if(!s)return 0;
    while(i<n){BYTE x=s[i];if(x>=0x80&&x<=0xFE&&i+1<n){if(x==a&&s[i+1]==b)c++;i+=2;}else i++;}
    return c;
}

/*
 * CP932 的 lead byte 范围和 GBK 不同：0x81..0x9F、0xE0..0xFC。
 * 日文 BFET 源串用这个规则走字符边界，再统计 Shift-JIS 的「=81 75、」=81 76。
 */
static DWORD diag_count_cp932_pair(const BYTE* s,DWORD n,BYTE a,BYTE b){
    DWORD i=0,c=0;if(!s)return 0;
    while(i<n){BYTE x=s[i];int lead=((x>=0x81&&x<=0x9F)||(x>=0xE0&&x<=0xFC));if(lead&&i+1<n){if(x==a&&s[i+1]==b)c++;i+=2;}else i++;}
    return c;
}

/*
 * 模拟“把已经变长的 GBK 中文按原脚本 count 再复制一次”。
 * 返回 1 表示 count 的最后一个有效字节正好是 0x80..0xFE 的首字节，下一半被截掉。
 * 这种状态会在画面上表现为方块/问号，也很容易让 Backlog 的后续字符串扫描越界或错位。
 */
static int diag_original_count_splits_gbk(const BYTE* zh,DWORD zh_len,DWORD count){
    DWORD i=0;if(!zh||count==0||count>zh_len)return 0;
    while(i<count){BYTE a=zh[i];if(a==0)return 0;if(a>=0x80&&a<=0xFE){if(i+1>=count)return 1;i+=2;}else i++;}
    return 0;
}

/* 下面两个函数的主体定义在后面；诊断器需要先知道它们的函数签名。 */
static int readable_context(const BYTE* ptr,const BYTE** lo,const BYTE** hi);
static BYTE* main_image_base(DWORD* out_size);

/*
 * 返回一个已经加载 PE 模块的 SizeOfImage。
 * 这里只读 MZ/PE 头，不扫描 section；用途只是判断“异常地址属于主 EXE / 本 ASI / 其它模块”。
 * 如果头部不符合 32 位 PE，就返回 0，诊断器宁可少报也不制造假的 RVA。
 */
static DWORD image_size_from_loaded_base(const BYTE* base){
    DWORD peoff;
    if(!base||u16(base)!=0x5A4D)return 0;
    peoff=u32(base+0x3C);
    if(u32(base+peoff)!=0x4550||u16(base+peoff+24)!=0x10B)return 0;
    return u32(base+peoff+24+56);
}

/*
 * 把一个运行时地址写成“绝对地址 + 所属模块 RVA”。
 * 例如 0x00415ABC 会额外打印 主程序RVA=0x00015ABC。
 * 以后拿到日志后可以直接回到反汇编，不必先猜 ASLR/基址。
 */
static void log_address_with_module(const char* label,DWORD address){
    log_s(label);log_hex32(address);
    if(g_diag_main_base&&address>=(DWORD)(ULONG_PTR)g_diag_main_base&&address<(DWORD)(ULONG_PTR)(g_diag_main_base+g_diag_main_size)){
        log_s("，主程序RVA=");log_hex32(address-(DWORD)(ULONG_PTR)g_diag_main_base);
    }else if(g_diag_self_base&&address>=(DWORD)(ULONG_PTR)g_diag_self_base&&address<(DWORD)(ULONG_PTR)(g_diag_self_base+g_diag_self_size)){
        log_s("，CN ASI RVA=");log_hex32(address-(DWORD)(ULONG_PTR)g_diag_self_base);
    }else{
        MEMORY_BASIC_INFORMATION_MIN mbi;
        if(pVirtualQuery&&address&&pVirtualQuery((const void*)(ULONG_PTR)address,&mbi,sizeof(mbi))>=sizeof(mbi)&&mbi.AllocationBase){
            DWORD module_base=(DWORD)(ULONG_PTR)mbi.AllocationBase;
            DWORD module_size=image_size_from_loaded_base((const BYTE*)mbi.AllocationBase);
            log_s("，其它映像/分配基址=");log_hex32(module_base);
            if(module_size&&address>=module_base&&address<module_base+module_size){log_s("，模块RVA=");log_hex32(address-module_base);}
            else log_s("，无法确认PE模块RVA");
        }else log_s("，无法通过 VirtualQuery 归属模块");
    }
    log_nl();
}

/*
 * 保存一次脚本复制事件。这里只写固定数组，不写日志。
 * “先保存、出错时统一 dump”有两个好处：
 *   1. 正常剧情不会每一句都刷日志；
 *   2. 真崩溃时仍能看到出事前最近 128 次脚本复制到底处理了什么。
 */
static SCRIPT_DIAG_EVENT* diag_begin_script_event(DWORD cur,DWORD count,BYTE* dest,BYTE* state,BYTE* body,DWORD limit){
    DWORD seq=++g_script_diag_sequence;
    SCRIPT_DIAG_EVENT* e=&g_script_diag[(seq-1U)%SCRIPT_DIAG_RING];
    e->sequence=seq;e->cur=cur;e->count=count;e->dest=(DWORD)(ULONG_PTR)dest;e->source_abs=0;
    e->ja_hash=0;e->zh_hash=0;e->ja_len=0;e->zh_len=0;e->mapping_index=INVALID_INDEX;
    e->state_addr=(DWORD)(ULONG_PTR)state;e->body_addr=(DWORD)(ULONG_PTR)body;e->limit=limit;e->post_cur=cur;
    e->value_type=0;e->value_count=0;e->flags=0;
    e->script_name[0]=0;e->ja_preview_len=0;e->zh_preview_len=0;e->reserved0=e->reserved1=0;
    /*
     * 这里只保存 dest 的数值，不提前读取 dest-8。真正验证 0x0B 时再设置 bit2；
     * 如果 dest 本身已经坏掉，诊断器不会因为“想多记一点信息”而抢先制造新的访问冲突。
     */
    return e;
}

/*
 * 异常发生时输出最近脚本事件。顺序从最旧到最新，方便直接看出“标题卡之后是否真的进入下一句”。
 * 事件结构里只存整数，不保存临时指针，所以这里不会因为脚本缓冲已经释放而再次访问非法内存。
 */
static void diag_log_recent_script_events(void){
    DWORD total=g_script_diag_sequence;
    DWORD n=total<SCRIPT_DIAG_RING?total:SCRIPT_DIAG_RING;
    DWORD first=(total>=n)?(total-n+1U):1U;
    DWORD seq;
    log_s("[崩溃] 最近脚本复制事件数=");log_dec(n);log_s("（最多保留128条）");log_nl();
    for(seq=first;seq<=total;seq++){
        SCRIPT_DIAG_EVENT* e=&g_script_diag[(seq-1U)%SCRIPT_DIAG_RING];
        if(e->sequence!=seq)continue;
        log_s("[崩溃][脚本] #");log_dec(e->sequence);
        log_s(" cur=");log_hex32(e->cur);log_s(" count=");log_dec(e->count);log_s(" post_cur=");log_hex32(e->post_cur);
        log_s(" limit=");log_hex32(e->limit);log_s(" state=");log_hex32(e->state_addr);log_s(" body=");log_hex32(e->body_addr);
        log_s(" dest=");log_hex32(e->dest);log_s(" objType=");log_hex32(e->value_type);log_s(" objCount=");log_dec(e->value_count);
        log_s(" flags=");log_hex32(e->flags);
        if(e->flags&1U){
            log_s(" file=");log_s(e->script_name[0]?e->script_name:"<未知>");
            log_s(" map#=");if(e->mapping_index==INVALID_INDEX)log_s("?");else log_dec(e->mapping_index);
            log_s(" source_off=");log_hex32(e->source_abs);
            log_s(" ja_hash=");log_hex32(e->ja_hash);log_s(" zh_hash=");log_hex32(e->zh_hash);
            log_s(" len=");log_dec(e->ja_len);log_s("->");log_dec(e->zh_len);
            if(e->flags&8U)log_s(" 中文更长");
            if(e->flags&16U)log_s(" 原count会截断GBK双字节");
            if(e->flags&32U)log_s(" 中文引号不平衡");
            if(e->flags&64U)log_s(" 中日引号数量变化");
        }
        log_nl();
        if(e->flags&1U){
            log_s("[崩溃][脚本字节] JA前缀(");log_dec(e->ja_preview_len);log_s(")：");diag_log_hex_block(e->ja_preview,e->ja_preview_len);log_nl();
            log_s("[崩溃][脚本字节] ZH前缀(");log_dec(e->zh_preview_len);log_s(")：");diag_log_hex_block(e->zh_preview,e->zh_preview_len);log_nl();
        }
    }
}

/*
 * 在不冒险跨页的前提下，输出 ESP 起始的前 32 个 DWORD。
 * VirtualQuery 先告诉我们当前内存页是否已提交且可读，再用 RegionSize 限制读取数量。
 * 这只是原始栈值，不尝试“智能回溯”，因为没有符号/栈帧信息时硬猜调用栈反而容易误导。
 */
static void diag_log_stack(DWORD esp){
    const BYTE* lo;const BYTE* hi;const DWORD* p;DWORD available,n,i;
    if(!esp||!readable_context((const BYTE*)(ULONG_PTR)esp,&lo,&hi)){log_s("[崩溃] ESP 所在内存不可安全读取。\r\n");return;}
    (void)lo;
    available=(DWORD)(hi-(const BYTE*)(ULONG_PTR)esp);
    n=available/4U;if(n>32U)n=32U;p=(const DWORD*)(ULONG_PTR)esp;
    log_s("[崩溃] 栈顶DWORD：");
    for(i=0;i<n;i++){if(i)log_s(" ");log_hex32(p[i]);}
    log_nl();
}

/*
 * 输出一个地址附近的原始内存。只读取 VirtualQuery 已确认可读的当前 region，绝不跨区。
 * EIP 附近机器码能直接告诉下一轮反编译“究竟是哪条指令炸了”；故障地址可读时也能看到对象头。
 */
static void diag_log_memory_window(const char* label,DWORD address,DWORD before,DWORD after){
    const BYTE*lo;const BYTE*hi;DWORD a,b;
    if(!address||!readable_context((const BYTE*)(ULONG_PTR)address,&lo,&hi)){log_s(label);log_s("：不可读\r\n");return;}
    a=address-(DWORD)(ULONG_PTR)lo<before?(DWORD)(ULONG_PTR)lo:address-before;
    b=(DWORD)(ULONG_PTR)hi-address<after?(DWORD)(ULONG_PTR)hi:address+after;
    log_s(label);log_s("：起点=");log_hex32(a);log_s(" 当前=");log_hex32(address);log_s(" 字节=");
    diag_log_hex_block((const BYTE*)(ULONG_PTR)a,b-a);log_nl();
}

/*
 * 从 ESP 的原始 DWORD 里筛出“看起来像主程序/CN ASI 内代码地址”的值。
 * 这不是符号化调用栈，但即使编译器省略 EBP，也常能直接看到 2~10 层返回地址。
 */
static void diag_log_stack_code_candidates(DWORD esp){
    const BYTE*lo;const BYTE*hi;const DWORD*p;DWORD n,i,v;
    if(!esp||!readable_context((const BYTE*)(ULONG_PTR)esp,&lo,&hi))return;
    (void)lo;n=(DWORD)(hi-(const BYTE*)(ULONG_PTR)esp)/4U;if(n>64U)n=64U;p=(const DWORD*)(ULONG_PTR)esp;
    log_s("[崩溃] 栈中疑似代码返回地址：\r\n");
    for(i=0;i<n;i++){
        v=p[i];
        if((g_diag_main_base&&v>=(DWORD)(ULONG_PTR)g_diag_main_base&&v<(DWORD)(ULONG_PTR)(g_diag_main_base+g_diag_main_size))||
           (g_diag_self_base&&v>=(DWORD)(ULONG_PTR)g_diag_self_base&&v<(DWORD)(ULONG_PTR)(g_diag_self_base+g_diag_self_size))){
            log_s("[崩溃][栈候选] ESP+");log_hex32(i*4U);log_s(" = ");log_address_with_module("",v);
        }
    }
}

/*
 * 传统 EBP frame chain 仍然值得尝试。每层读取 [EBP]=上一帧、[EBP+4]=返回地址；
 * 任何不可读、倒退、跨度过大都会立即停止，避免损坏栈让诊断器自身继续走飞。
 */
static void diag_log_ebp_chain(DWORD ebp){
    DWORD frame=ebp,i;
    log_s("[崩溃] EBP帧链（最多16层）：\r\n");
    for(i=0;i<16U;i++){
        const BYTE*lo;const BYTE*hi;const DWORD*p;DWORD next,ret;
        if(!frame||!readable_context((const BYTE*)(ULONG_PTR)frame,&lo,&hi)||(DWORD)(hi-(const BYTE*)(ULONG_PTR)frame)<8U)break;
        p=(const DWORD*)(ULONG_PTR)frame;next=p[0];ret=p[1];
        log_s("[崩溃][帧] #");log_dec(i);log_s(" EBP=");log_hex32(frame);log_s(" RET=");log_address_with_module("",ret);
        if(next<=frame||next-frame>0x100000UL)break;frame=next;
    }
}

static void diag_note_glyph(DWORD type,DWORD code,DWORD arg1,DWORD arg2){
    DWORD seq=++g_glyph_diag_sequence;GLYPH_DIAG_EVENT*e=&g_glyph_diag[(seq-1U)%GLYPH_DIAG_RING];
    e->sequence=seq;e->type=type;e->code=code;e->arg1=arg1;e->arg2=arg2;
}

static void diag_log_recent_glyph_events(void){
    DWORD total=g_glyph_diag_sequence,n=total<GLYPH_DIAG_RING?total:GLYPH_DIAG_RING,first=(total>=n)?(total-n+1U):1U,seq;
    log_s("[崩溃] 最近字形/DBCS事件数=");log_dec(n);log_nl();
    for(seq=first;seq<=total;seq++){
        GLYPH_DIAG_EVENT*e=&g_glyph_diag[(seq-1U)%GLYPH_DIAG_RING];if(e->sequence!=seq)continue;
        log_s("[崩溃][字形] #");log_dec(e->sequence);log_s(" 类型=");log_dec(e->type);log_s(" code=");log_hex32(e->code);
        log_s(" arg1=");log_dec(e->arg1);log_s(" arg2=");log_dec(e->arg2);log_nl();
    }
}


/*
 * 把 ExceptionRecord 的参数全部输出。Access Violation 通常只有 2 个参数，
 * In-page Error 可能有 3 个；其它异常也可能携带附加信息。逐项保留可以避免下一轮
 * 只看到“异常代码”却丢掉 Windows 已经给出的原始上下文。
 */
static void diag_log_exception_parameters(const EXCEPTION_RECORD_MIN* er){
    DWORD i,n;if(!er)return;n=er->NumberParameters;if(n>MAXIMUM_EXCEPTION_PARAMETERS)n=MAXIMUM_EXCEPTION_PARAMETERS;
    log_s("[崩溃] ExceptionParameters 数量=");log_dec(n);log_nl();
    for(i=0;i<n;i++){log_s("[崩溃][参数] #");log_dec(i);log_s(" = ");log_hex32((DWORD)er->ExceptionInformation[i]);log_nl();}
}

/*
 * 把常用寄存器当成“可能的对象/字符串指针”各自尝试读取一个小窗口。
 * readable_context 会先做 VirtualQuery，因此坏指针只会得到“不可读”，不会为了诊断再制造访问冲突。
 * 对老游戏逆向而言，这往往能直接看到 ECX=this、ESI/EDI=脚本对象等结构头，比只有寄存器数值快得多。
 */
static void diag_log_register_memory(const CONTEXT_X86_MIN* c){
    if(!c)return;
    diag_log_memory_window("[崩溃][寄存器内存] EAX",c->Eax,0U,48U);
    diag_log_memory_window("[崩溃][寄存器内存] EBX",c->Ebx,0U,48U);
    diag_log_memory_window("[崩溃][寄存器内存] ECX",c->Ecx,0U,48U);
    diag_log_memory_window("[崩溃][寄存器内存] EDX",c->Edx,0U,48U);
    diag_log_memory_window("[崩溃][寄存器内存] ESI",c->Esi,0U,48U);
    diag_log_memory_window("[崩溃][寄存器内存] EDI",c->Edi,0U,48U);
    diag_log_memory_window("[崩溃][寄存器内存] EBP",c->Ebp,0U,48U);
}

/*
 * 最近脚本事件已经保存了 dest 数值。异常时再对最后 12 个目标对象做“仍然可读才 dump”的现场检查：
 *   dest-8 = value type；dest-4 = 当前运行时 byte_count；dest = 当前字符缓冲。
 * 这对 Backlog 特别关键：source count 与运行时对象 count 是两个概念。v1.0.4 正常情况下
 * 会让 dest-4 等于实际中文长度+NUL；如果以后又出现不一致，日志会把对象现场直接留下。
 * 旧对象若已释放，VirtualQuery/可读范围检查会让该项安全跳过，而不是解引用悬空指针。
 */
static void diag_log_recent_script_objects(void){
    DWORD total=g_script_diag_sequence,n=total<12U?total:12U,first=(total>=n)?(total-n+1U):1U,seq;
    log_s("[崩溃] 最近脚本目标对象现场（最多12条，只读仍可访问的对象）：\r\n");
    for(seq=first;seq<=total;seq++){
        SCRIPT_DIAG_EVENT*e=&g_script_diag[(seq-1U)%SCRIPT_DIAG_RING];const BYTE*lo;const BYTE*hi;DWORD start,avail,dumpn;
        if(e->sequence!=seq||e->dest<8U)continue;
        start=e->dest-8U;
        if(!readable_context((const BYTE*)(ULONG_PTR)start,&lo,&hi)){log_s("[崩溃][对象] #");log_dec(seq);log_s(" dest=");log_hex32(e->dest);log_s(" 当前不可读\r\n");continue;}
        (void)lo;avail=(DWORD)(hi-(const BYTE*)(ULONG_PTR)start);dumpn=avail<80U?avail:80U;
        log_s("[崩溃][对象] #");log_dec(seq);log_s(" dest=");log_hex32(e->dest);log_s(" 字节(dest-8起)=");
        diag_log_hex_block((const BYTE*)(ULONG_PTR)start,dumpn);log_nl();
    }
}

/*
 * 把本 ASI 的关键运行计数冻结在崩溃现场。多个潜在崩溃点时，这能快速判断故障发生在
 * “脚本还没替换 / 已替换但尚未渲染 / 已进入字形 / 资源刚打开”等哪一阶段。
 */
static void diag_log_runtime_counters(void){
    log_s("[崩溃] 运行计数：script_hook_hits=");log_dec(g_script_hook_hits);
    log_s(" replaced=");log_dec(g_script_hook_replaced);log_s(" runtime_count_updated=");log_dec(g_script_runtime_count_updated);
    log_s(" length_delta=");log_dec(g_script_length_delta_seen);log_s(" after_hits=");log_dec(g_after_bin_direct_runtime_hits);log_nl();
    log_s("[崩溃] 渲染计数：renderer_hits=");log_dec(g_gbk_renderer_hits);log_s(" glyph_builds=");log_dec(g_gbk_glyph_builds);
    log_s(" glyph_failures=");log_dec(g_gbk_glyph_failures);log_s(" mask_builds=");log_dec(g_gbk_mask_builds);log_s(" mask_failures=");log_dec(g_gbk_mask_failures);
    log_s(" ansi_outline_failures=");log_dec(g_gbk_outline_ansi_failures);log_s(" unicode_fallbacks=");log_dec(g_gbk_outline_unicode_fallbacks);
    log_s(" hard_outline_failures=");log_dec(g_gbk_outline_hard_failures);log_nl();
    log_s("[崩溃] 资源计数：DAT_hits=");log_dec(g_resource_overlay_hits);log_s(" embedded_open_failures=");log_dec(g_embedded_asset_open_failures);log_nl();
}

/*
 * 第一机会 VEH：只记录，不修复、不跳过、不吞异常。
 * 返回 EXCEPTION_CONTINUE_SEARCH 后，Windows 会继续走游戏原本的 SEH/崩溃流程，
 * 因此诊断版不会把“本来应该崩”的状态伪装成继续运行。
 */
static LONG STDCALL CrashDiagnosticVEH(EXCEPTION_POINTERS_MIN* ep){
    EXCEPTION_RECORD_MIN* er;CONTEXT_X86_MIN* c;DWORD code,address;
    if(!ep||!ep->ExceptionRecord||!ep->ContextRecord)return EXCEPTION_CONTINUE_SEARCH;
    er=ep->ExceptionRecord;c=ep->ContextRecord;code=er->ExceptionCode;address=(DWORD)(ULONG_PTR)er->ExceptionAddress;
    if(code!=EXCEPTION_ACCESS_VIOLATION&&code!=EXCEPTION_IN_PAGE_ERROR&&code!=EXCEPTION_ILLEGAL_INSTRUCTION&&
       code!=EXCEPTION_INT_DIVIDE_BY_ZERO&&code!=EXCEPTION_INT_OVERFLOW&&code!=EXCEPTION_STACK_OVERFLOW)return EXCEPTION_CONTINUE_SEARCH;

    /* 防止诊断代码自己遇到异常时无限递归。 */
    if(g_exception_handler_busy)return EXCEPTION_CONTINUE_SEARCH;
    g_exception_handler_busy=1;
    g_exception_log_count++;
    log_s("\r\n[崩溃] ===== 捕获到严重异常（仅记录，不拦截） =====\r\n");
    log_s("[崩溃] 序号=");log_dec(g_exception_log_count);log_s("，异常代码=");log_hex32(code);log_s("，标志=");log_hex32(er->ExceptionFlags);log_nl();
    log_address_with_module("[崩溃] ExceptionAddress=",address);
    log_address_with_module("[崩溃] EIP=",c->Eip);
    log_s("[崩溃] EAX=");log_hex32(c->Eax);log_s(" EBX=");log_hex32(c->Ebx);log_s(" ECX=");log_hex32(c->Ecx);log_s(" EDX=");log_hex32(c->Edx);log_nl();
    log_s("[崩溃] ESI=");log_hex32(c->Esi);log_s(" EDI=");log_hex32(c->Edi);log_s(" EBP=");log_hex32(c->Ebp);log_s(" ESP=");log_hex32(c->Esp);log_nl();
    log_s("[崩溃] EFlags=");log_hex32(c->EFlags);log_s(" ContextFlags=");log_hex32(c->ContextFlags);log_s("，最近脚本事件总序号=");log_dec(g_script_diag_sequence);log_nl();
    log_s("[崩溃] 段寄存器 CS=");log_hex32(c->SegCs);log_s(" SS=");log_hex32(c->SegSs);log_s(" DS=");log_hex32(c->SegDs);log_s(" ES=");log_hex32(c->SegEs);log_s(" FS=");log_hex32(c->SegFs);log_s(" GS=");log_hex32(c->SegGs);log_nl();
    diag_log_exception_parameters(er);

    if((code==EXCEPTION_ACCESS_VIOLATION||code==EXCEPTION_IN_PAGE_ERROR)&&er->NumberParameters>=2U){
        log_s("[崩溃] 内存访问类型=");
        if(er->ExceptionInformation[0]==0)log_s("读取");else if(er->ExceptionInformation[0]==1)log_s("写入");else if(er->ExceptionInformation[0]==8)log_s("执行");else log_s("未知");
        log_s("，目标地址=");log_hex32((DWORD)er->ExceptionInformation[1]);
        if(code==EXCEPTION_IN_PAGE_ERROR&&er->NumberParameters>=3U){log_s("，底层状态=");log_hex32((DWORD)er->ExceptionInformation[2]);}
        log_nl();
    }
    diag_log_memory_window("[崩溃] EIP附近机器码",c->Eip,16U,48U);
    if((code==EXCEPTION_ACCESS_VIOLATION||code==EXCEPTION_IN_PAGE_ERROR)&&er->NumberParameters>=2U)
        diag_log_memory_window("[崩溃] 故障目标附近",(DWORD)er->ExceptionInformation[1],32U,64U);
    diag_log_register_memory(c);
    diag_log_runtime_counters();
    diag_log_stack(c->Esp);
    diag_log_stack_code_candidates(c->Esp);
    diag_log_ebp_chain(c->Ebp);
    diag_log_recent_script_events();
    diag_log_recent_script_objects();
    diag_log_recent_glyph_events();
    log_s("[崩溃] ===== 异常记录结束，继续交给游戏/Windows 原处理链 =====\r\n");
    /*
     * 游戏很可能在我们返回 CONTINUE_SEARCH 后立刻被 Windows 终止。
     * 主动 FlushFileBuffers 可以尽量保证刚写的异常现场已经落盘，而不是只停留在文件系统缓存里。
     * 如果 API 因极旧系统不可用，就保持普通 WriteFile 行为；这不影响原异常继续传播。
     */
    if(pFlushFileBuffers&&g_log!=INVALID_HANDLE_VALUE)pFlushFileBuffers(g_log);
    g_exception_handler_busy=0;
    return EXCEPTION_CONTINUE_SEARCH;
}

/*
 * 安装 VEH 前先固定主 EXE 与 ASI 的基址/SizeOfImage。
 * Handler 的返回值 cookie 只需保存到进程结束；本 ASI 不主动卸载，因此无需 RemoveVectoredExceptionHandler。
 */
static void install_crash_diagnostics(void){
    DWORD main_size=0;
    g_diag_main_base=main_image_base(&main_size);g_diag_main_size=main_size;
    g_diag_self_base=(BYTE*)g_self_module;g_diag_self_size=image_size_from_loaded_base(g_diag_self_base);
    if(!pAddVectoredExceptionHandler){log_s("[警告] 系统未解析到 AddVectoredExceptionHandler，崩溃地址诊断不可用。\r\n");return;}
    g_vectored_handler_cookie=pAddVectoredExceptionHandler(1,CrashDiagnosticVEH);
    if(g_vectored_handler_cookie){
        log_s("[成功] 已安装增强版只记录不拦截的崩溃诊断 VEH；主程序基址=");log_hex32((DWORD)(ULONG_PTR)g_diag_main_base);
        log_s("，CN ASI 基址=");log_hex32((DWORD)(ULONG_PTR)g_diag_self_base);log_nl();
    }else log_s("[警告] AddVectoredExceptionHandler 返回空，崩溃地址诊断不可用。\r\n");
}

/* ---------- optional, deliberately tiny INI configuration ----------
 *
 * v1.0.0~v1.0.3 removed the INI because the old file exposed architecture switches that users could
 * turn off and thereby break the localization.  v1.0.4 brings the file back, but only for two options
 * that do not change parsing, encoding or Hook topology:
 *
 *   FontFace = optional Unicode font family name. Empty means use the proven default Song/Hei behavior.
 *   EnableLog = 1 writes BaldrForceCN.log; 0 disables normal and crash-diagnostic file logging.
 *
 * If the INI is missing, malformed, or the Windows INI API cannot be resolved, defaults are used.
 * The runtime therefore remains drop-in compatible with the previous no-INI build.
 */
static void load_runtime_config(void){
    WCHAR tmp[64];
    DWORD n,i;

    /* Start from the exact behavior that already passed the user's v1.0.1 functional tests. */
    g_enable_log=1;
    g_force_face[0]=0;

    if(!pGetPrivateProfileStringW)return;

    /*
     * EnableLog accepts the simple values 0 or 1.  Anything except an explicit leading '0' stays enabled,
     * which is safer for troubleshooting: a typo should not silently erase crash diagnostics.
     */
    mem_zero(tmp,sizeof(tmp));
    pGetPrivateProfileStringW((LPCWSTR)L"BaldrForceCN",(LPCWSTR)L"EnableLog",(LPCWSTR)L"1",tmp,64,g_ini_path);
    if(tmp[0]=='0')g_enable_log=0;

    /*
     * LOGFONT has room for 31 visible WCHARs plus NUL.  Copy at most that many so an excessively long
     * INI value can never overflow our fixed-size structure.  Leading/trailing spaces are left to the
     * Windows INI parser; users should write the family name exactly as Windows exposes it.
     */
    mem_zero(tmp,sizeof(tmp));
    n=pGetPrivateProfileStringW((LPCWSTR)L"BaldrForceCN",(LPCWSTR)L"FontFace",(LPCWSTR)L"",tmp,64,g_ini_path);
    if(n){
        for(i=0;i<31U&&tmp[i];i++)g_force_face[i]=tmp[i];
        g_force_face[i]=0;
    }
}


/* ---------- PAC/BFET parsing ---------- */
static int bounds(DWORD off,DWORD size,DWORD total){ return off<=total && size<=total-off; }
static DWORD find_valid_bfet(const BYTE* p,DWORD sz){
    DWORD pos;if(sz<20)return INVALID_INDEX; pos=sz-4;
    while(1){
        if(p[pos]=='B'&&pos+12<=sz&&p[pos+1]=='F'&&p[pos+2]=='E'&&p[pos+3]=='T'){
            DWORD bs=u32(p+pos+4),n=u32(p+pos+8);
            if(bs+8==sz-pos && n>0 && n<10000 && 12+n*8+4<=sz-pos) return pos;
        }
        if(pos==0)break;pos--;
    }
    return INVALID_INDEX;
}
static const BYTE* cstr_end(const BYTE* s,const BYTE* end){const BYTE*p=s;while(p<end&&*p)p++;return p;}

static int load_file(LPCWSTR path,BYTE** out,DWORD* outsz){
    HANDLE h;DWORD sz,rd=0;BYTE*b;
    h=pCreateFileW(path,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);if(h==INVALID_HANDLE_VALUE)return 0;
    sz=pGetFileSize(h,NULL);if(sz==0xFFFFFFFFUL||sz<12){pCloseHandle(h);return 0;}
    b=(BYTE*)pHeapAlloc(g_heap,0,sz);if(!b){pCloseHandle(h);return 0;}
    if(!pReadFile(h,b,sz,&rd,NULL)||rd!=sz){pHeapFree(g_heap,0,b);pCloseHandle(h);return 0;}
    pCloseHandle(h);*out=b;*outsz=sz;return 1;
}

static int is_bin_name(const BYTE* n){DWORD l=c_len_bounded(n,64);if(l<4)return 0;return lower_a((char)n[l-4])=='.'&&lower_a((char)n[l-3])=='b'&&lower_a((char)n[l-2])=='i'&&lower_a((char)n[l-1])=='n';}
static int is_after_name(const BYTE*n){return lower_a((char)n[0])=='a'&&lower_a((char)n[1])=='f'&&lower_a((char)n[2])=='t'&&lower_a((char)n[3])=='e'&&lower_a((char)n[4])=='r'&&n[5]=='.'&&lower_a((char)n[6])=='b'&&lower_a((char)n[7])=='i'&&lower_a((char)n[8])=='n'&&n[9]==0;}

static int pac_name_eq(const BYTE* n,const char* a){
    DWORD i=0;if(!n||!a)return 0;
    while(i<64&&n[i]&&a[i]){if(lower_a((char)n[i])!=lower_a(a[i]))return 0;i++;}
    return i<64&&n[i]==0&&a[i]==0;
}
static int find_uncompressed_pac_entry(const char* name,const BYTE** data,DWORD* size){
    DWORD cnt,i;if(!g_pac||g_pac_size<12)return 0;cnt=u32(g_pac+4);
    if(cnt==0||cnt>4096||!bounds(12,cnt*76,g_pac_size))return 0;
    for(i=0;i<cnt;i++){
        const BYTE*e=g_pac+12+i*76;DWORD off,sz1,sz2;
        if(!pac_name_eq(e,name))continue;
        off=u32(e+64);sz1=u32(e+68);sz2=u32(e+72);
        if(sz1!=sz2||!bounds(off,sz2,g_pac_size))return 0;
        if(data)*data=g_pac+off;if(size)*size=sz2;return 1;
    }
    return 0;
}
static int init_resource_overlays(void){
    static const char* names[]={
        "CGInfo.DAT","ChapterFlowInfo.DAT","DatabaseInfo.DAT","EquipmentInfo.DAT",
        "FlgInfo.DAT","FlowInfo.DAT","ItemInfo.DAT","ReplayInfo.DAT",
        "SceneInfo.DAT","VisualInfo.DAT"
    };
    DWORD i;g_resource_overlay_count=0;
    for(i=0;i<(DWORD)(sizeof(names)/sizeof(names[0]))&&g_resource_overlay_count<RESOURCE_OVERLAY_MAX;i++){
        const BYTE*d=NULL;DWORD n=0;if(find_uncompressed_pac_entry(names[i],&d,&n)){
            RESOURCE_OVERLAY_ENTRY*e=&g_resource_overlays[g_resource_overlay_count++];
            e->name=names[i];e->data=d;e->size=n;e->hits=0;
        }
    }
    log_kv("可用汉化 DAT 覆盖数：",g_resource_overlay_count);
    return g_resource_overlay_count>0;
}
static RESOURCE_OVERLAY_ENTRY* lookup_resource_overlay(LPCSTR name){
    DWORD i;if(!name)return NULL;
    for(i=0;i<g_resource_overlay_count;i++)if(a_eq(name,g_resource_overlays[i].name))return &g_resource_overlays[i];
    return NULL;
}


/* First pass returns total translations and direct GBK count; also counts wchar pool sizes. */
static int count_database(DWORD* total_trans,DWORD* total_w,DWORD* total_direct,DWORD* total_direct_w,DWORD* bfet_files){
    DWORD cnt,i; *total_trans=*total_w=*total_direct=*total_direct_w=*bfet_files=0;
    if(!g_pac||g_pac_size<12||g_pac[0]!='P'||g_pac[1]!='A'||g_pac[2]!='C')return 0;
    cnt=u32(g_pac+4); if(cnt==0||cnt>4096||!bounds(12,cnt*76,g_pac_size))return 0;
    for(i=0;i<cnt;i++){
        const BYTE* e=g_pac+12+i*76; DWORD off=u32(e+64),dec=u32(e+68),enc=u32(e+72),bp;
        const BYTE* p; DWORD psz;
        if(!is_bin_name(e)||dec!=enc||!bounds(off,enc,g_pac_size))continue;
        p=g_pac+off;psz=enc;bp=find_valid_bfet(p,psz);
        if(bp!=INVALID_INDEX){
            DWORD n=u32(p+bp+8),j; const BYTE* b=p+bp; (*bfet_files)++;
            if(12+n*8+4>psz-bp)return 0;
            *total_trans += n;
            for(j=0;j<n;j++){
                DWORD to=(j+1<n)?u32(b+12+n*4+(j+1)*4):u32(b+12+n*8);
                DWORD ta=bp+8+to; int w;
                if(ta>=psz)return 0;
                {const BYTE* z=cstr_end(p+ta,p+psz);if(z>=p+psz)return 0;w=g_sys_mbtowc(CP_GBK,0,(const char*)(p+ta),-1,NULL,0);if(w<=0)return 0;*total_w+=(DWORD)w;}
            }
        } else if(is_after_name(e)){
            DWORD tb,j=0; if(psz<20)continue; tb=16+u32(p+8)*4;if(tb>=psz)continue;
            while(tb+j<psz){const BYTE*s=p+tb+j;const BYTE*en=cstr_end(s,p+psz);DWORD l=(DWORD)(en-s);if(l>=2&&l<=4096){int w=g_sys_mbtowc(CP_GBK,0,(const char*)s,-1,NULL,0);if(w>0){WCHAR temp[8];int tw=g_sys_mbtowc(CP_GBK,0,(const char*)s,-1,temp,8);if((tw>0&&has_cjk(temp,(DWORD)(tw<8?tw:8)))||w>8){(*total_direct)++;*total_direct_w+=(DWORD)w;}}}if(en>=p+psz)break;j=(DWORD)(en-(p+tb))+1;}
        }
    }
    return *total_trans>0;
}

static void init_heads(DWORD* h){DWORD i;for(i=0;i<BUCKET_COUNT;i++)h[i]=INVALID_INDEX;}

static int build_database(void){
    DWORD total=0,totalw=0,dirn=0,dirw=0,bfiles=0,cnt,i,ti=0,di=0,wpos=0,dwpos=0;
    if(!count_database(&total,&totalw,&dirn,&dirw,&bfiles))return 0;
    g_trans=(TRANS_ENTRY*)pHeapAlloc(g_heap,HEAP_ZERO_MEMORY,total*sizeof(TRANS_ENTRY));
    g_wide_pool=(WCHAR*)pHeapAlloc(g_heap,HEAP_ZERO_MEMORY,totalw*sizeof(WCHAR));
    g_head_ja=(DWORD*)pHeapAlloc(g_heap,0,BUCKET_COUNT*sizeof(DWORD));
    g_head_zh_mb=(DWORD*)pHeapAlloc(g_heap,0,BUCKET_COUNT*sizeof(DWORD));
    g_head_zh_w=(DWORD*)pHeapAlloc(g_heap,0,BUCKET_COUNT*sizeof(DWORD));
    if(!g_trans||!g_wide_pool||!g_head_ja||!g_head_zh_mb||!g_head_zh_w)return 0;
    init_heads(g_head_ja);init_heads(g_head_zh_mb);init_heads(g_head_zh_w);
    if(dirn){
        g_direct=(DIRECT_ENTRY*)pHeapAlloc(g_heap,HEAP_ZERO_MEMORY,dirn*sizeof(DIRECT_ENTRY));
        g_direct_wide_pool=(WCHAR*)pHeapAlloc(g_heap,HEAP_ZERO_MEMORY,dirw*sizeof(WCHAR));
        g_head_direct_mb=(DWORD*)pHeapAlloc(g_heap,0,BUCKET_COUNT*sizeof(DWORD));g_head_direct_w=(DWORD*)pHeapAlloc(g_heap,0,BUCKET_COUNT*sizeof(DWORD));
        if(!g_direct||!g_direct_wide_pool||!g_head_direct_mb||!g_head_direct_w)return 0;init_heads(g_head_direct_mb);init_heads(g_head_direct_w);
    }
    cnt=u32(g_pac+4);
    for(i=0;i<cnt;i++){
        const BYTE* e=g_pac+12+i*76;DWORD off=u32(e+64),dec=u32(e+68),enc=u32(e+72),bp;const BYTE*p;DWORD psz;
        if(!is_bin_name(e)||dec!=enc||!bounds(off,enc,g_pac_size))continue;p=g_pac+off;psz=enc;bp=find_valid_bfet(p,psz);
        if(bp!=INVALID_INDEX){
            const BYTE*b=p+bp;DWORD n=u32(b+8),textbase=16+u32(p+8)*4,j;
            if(textbase>=bp)return 0;
            for(j=0;j<n;j++){
                DWORD so=u32(b+12+(n-1-j)*4);DWORD to=(j+1<n)?u32(b+12+n*4+(j+1)*4):u32(b+12+n*8);DWORD sa=textbase+so,ta=bp+8+to;
                const BYTE*je;const BYTE*ze;DWORD jl,zl;int wl;TRANS_ENTRY*t;DWORD buck;
                if(sa>=bp||ta>=psz)return 0;je=cstr_end(p+sa,p+bp);ze=cstr_end(p+ta,p+psz);if(je>=p+bp||ze>=p+psz)return 0;jl=(DWORD)(je-(p+sa));zl=(DWORD)(ze-(p+ta));
                wl=g_sys_mbtowc(CP_GBK,0,(const char*)(p+ta),-1,g_wide_pool+wpos,(int)(totalw-wpos));if(wl<=0)return 0;
                t=&g_trans[ti];t->ja=p+sa;t->ja_len=jl;t->zh_mb=p+ta;t->zh_mb_len=zl;t->zh_w=g_wide_pool+wpos;t->zh_w_len=(DWORD)(wl-1);t->payload=p;t->payload_size=psz;t->source_abs=sa;t->source_rel=so;t->text_base=textbase;
                t->script_name=(const char*)e;t->mapping_index=j;
                t->hash_ja=fnv_bytes(t->ja,t->ja_len);t->hash_zh_mb=fnv_bytes(t->zh_mb,t->zh_mb_len);t->hash_zh_w=fnv_wide(t->zh_w,t->zh_w_len);
                /*
                 * 启动期顺手完成全 34,184 条映射的结构风险统计。
                 * 这里不修改任何字符串，只回答“中文长度变化后，如果别的子系统仍信任原 count，会发生什么”。
                 */
                if(t->zh_mb_len==t->ja_len)g_bfet_len_equal++;
                else if(t->zh_mb_len<t->ja_len)g_bfet_zh_shorter++;
                else{
                    g_bfet_zh_longer++;
                    if(diag_original_count_splits_gbk(t->zh_mb,t->zh_mb_len,t->ja_len+1U))g_bfet_original_count_dbcs_split++;
                }
                {
                    DWORD jo=diag_count_cp932_pair(t->ja,t->ja_len,0x81,0x75),jc=diag_count_cp932_pair(t->ja,t->ja_len,0x81,0x76);
                    DWORD zo=diag_count_gbk_pair(t->zh_mb,t->zh_mb_len,0xA1,0xB8),zc=diag_count_gbk_pair(t->zh_mb,t->zh_mb_len,0xA1,0xB9);
                    if(zo!=zc)g_bfet_zh_quote_unbalanced++;
                    if(jo!=zo||jc!=zc)g_bfet_quote_count_changed++;
                }
                buck=t->hash_ja&(BUCKET_COUNT-1);t->next_ja=g_head_ja[buck];g_head_ja[buck]=ti;
                buck=t->hash_zh_mb&(BUCKET_COUNT-1);t->next_zh_mb=g_head_zh_mb[buck];g_head_zh_mb[buck]=ti;
                buck=t->hash_zh_w&(BUCKET_COUNT-1);t->next_zh_w=g_head_zh_w[buck];g_head_zh_w[buck]=ti;
                wpos+=(DWORD)wl;ti++;
            }
        } else if(is_after_name(e)&&g_direct){
            DWORD tb,j=0;if(psz<20)continue;tb=16+u32(p+8)*4;if(tb>=psz)continue;
            while(tb+j<psz){const BYTE*s=p+tb+j;const BYTE*en=cstr_end(s,p+psz);DWORD l=(DWORD)(en-s);if(l>=2&&l<=4096){int wl=g_sys_mbtowc(CP_GBK,0,(const char*)s,-1,g_direct_wide_pool+dwpos,(int)(dirw-dwpos));if(wl>0&&has_cjk(g_direct_wide_pool+dwpos,(DWORD)(wl-1))){DIRECT_ENTRY*d=&g_direct[di];DWORD buck;d->mb=s;d->mb_len=l;d->w=g_direct_wide_pool+dwpos;d->w_len=(DWORD)(wl-1);d->hash_mb=fnv_bytes(s,l);d->hash_w=fnv_wide(d->w,d->w_len);buck=d->hash_mb&(BUCKET_COUNT-1);d->next_mb=g_head_direct_mb[buck];g_head_direct_mb[buck]=di;buck=d->hash_w&(BUCKET_COUNT-1);d->next_w=g_head_direct_w[buck];g_head_direct_w[buck]=di;dwpos+=(DWORD)wl;di++;}}
                if(en>=p+psz)break;j=(DWORD)(en-(p+tb))+1;}
        }
    }
    g_trans_count=ti;g_direct_count=di;
    log_kv("BFET 文件数：",bfiles);log_kv("映射条目数：",g_trans_count);log_kv("直接 GBK 字符串数：",g_direct_count);
    log_kv("BFET 中日长度相同：",g_bfet_len_equal);
    log_kv("BFET 中文更短：",g_bfet_zh_shorter);
    log_kv("BFET 中文更长：",g_bfet_zh_longer);
    log_kv("若按原 count 二次复制会截断 GBK 双字节：",g_bfet_original_count_dbcs_split);
    log_kv("中文自身「/」数量不平衡：",g_bfet_zh_quote_unbalanced);
    log_kv("中日「/」数量发生变化：",g_bfet_quote_count_changed);
    return ti==total;
}

/* ---------- lookup / context disambiguation ---------- */
static int readable_context(const BYTE* ptr,const BYTE** lo,const BYTE** hi){MEMORY_BASIC_INFORMATION_MIN mbi;if(!pVirtualQuery||pVirtualQuery(ptr,&mbi,sizeof(mbi))<sizeof(mbi))return 0;if(mbi.State!=MEM_COMMIT||(mbi.Protect&PAGE_GUARD)||(mbi.Protect&0xFF)==PAGE_NOACCESS)return 0;*lo=(const BYTE*)mbi.BaseAddress;*hi=*lo+mbi.RegionSize;return 1;}
static DWORD context_score(const TRANS_ENTRY*t,const BYTE*runtime){
    const BYTE*rlo,*rhi;DWORD pre=0,post=0,lim=g_context_bytes;const BYTE*orig=t->payload+t->source_abs;
    if(!readable_context(runtime,&rlo,&rhi))return 0;
    while(pre<lim && t->source_abs>pre && runtime>rlo+pre){if(orig[-(LONG)(pre+1)]!=runtime[-(LONG)(pre+1)])break;pre++;}
    while(post<lim && t->source_abs+t->ja_len+1+post<t->payload_size && runtime+t->ja_len+1+post<rhi){if(orig[t->ja_len+1+post]!=runtime[t->ja_len+1+post])break;post++;}
    return pre+post;
}
static const TRANS_ENTRY* lookup_ja(const BYTE*s,DWORD len){
    DWORD h=fnv_bytes(s,len),idx=g_head_ja?h&(BUCKET_COUNT-1):0,cur,first=INVALID_INDEX,best=INVALID_INDEX,bscore=0,btie=0;const TRANS_ENTRY* f=NULL;
    if(!g_head_ja)return NULL;cur=g_head_ja[idx];
    while(cur!=INVALID_INDEX){TRANS_ENTRY*t=&g_trans[cur];if(t->hash_ja==h&&t->ja_len==len&&mem_equal(t->ja,s,len)){if(first==INVALID_INDEX){first=cur;f=t;}else if(f->zh_w_len==t->zh_w_len&&mem_equal(f->zh_w,t->zh_w,t->zh_w_len*2)){}else{DWORD sc=context_score(t,s);if(best==INVALID_INDEX){best=first;bscore=context_score(&g_trans[first],s);btie=0;}if(sc>bscore){best=cur;bscore=sc;btie=0;}else if(sc==bscore)btie=1;}}cur=t->next_ja;}
    if(first==INVALID_INDEX)return NULL;if(best!=INVALID_INDEX&&bscore>=4&&!btie){g_context_resolved++;return &g_trans[best];}if(best!=INVALID_INDEX){g_ambiguous_fallbacks++;}return &g_trans[first];
}
static DWORD script_identity_score(const TRANS_ENTRY*t,const BYTE*body,DWORD cur,DWORD limit){
    const BYTE*lo,*hi;DWORD score=0,n;
    if(t->source_abs==cur||t->source_rel==cur)score+=0x10000UL;
    if(!body||!readable_context(body,&lo,&hi))return score;
    n=limit;if(n>128)n=128;if(n>(DWORD)(hi-body))n=(DWORD)(hi-body);
    if(n){
        DWORD n0=n;if(n0>t->payload_size)n0=t->payload_size;
        if(n0>=16&&mem_equal(body,t->payload,n0))score+=0x40000UL+n0;
        if(t->text_base<t->payload_size){DWORD avail=t->payload_size-t->text_base,nt=n;if(nt>avail)nt=avail;if(nt>=16&&mem_equal(body,t->payload+t->text_base,nt))score+=0x30000UL+nt;}
    }
    return score;
}
static const TRANS_ENTRY* lookup_ja_script(const BYTE*s,DWORD len,const BYTE*body,DWORD cur,DWORD limit){
    DWORD h,idx,ent,best=INVALID_INDEX,bscore=0,tie=0;const TRANS_ENTRY*first=NULL;
    if(!g_head_ja)return NULL;h=fnv_bytes(s,len);idx=h&(BUCKET_COUNT-1);ent=g_head_ja[idx];
    while(ent!=INVALID_INDEX){TRANS_ENTRY*t=&g_trans[ent];if(t->hash_ja==h&&t->ja_len==len&&mem_equal(t->ja,s,len)){
        DWORD sc=script_identity_score(t,body,cur,limit);if(!first)first=t;
        if(sc>bscore){bscore=sc;best=ent;tie=0;}else if(sc==bscore&&sc!=0&&best!=INVALID_INDEX){TRANS_ENTRY*b=&g_trans[best];if(!(b->zh_w_len==t->zh_w_len&&mem_equal(b->zh_w,t->zh_w,t->zh_w_len*2)))tie=1;}
    }ent=t->next_ja;}
    if(best!=INVALID_INDEX&&bscore>=0x10000UL&&!tie){g_exact_script_resolved++;if(g_exact_script_resolved==1){log_s("[命中] 精确 BFET 脚本身份/偏移解析器已启用\r\n");}return &g_trans[best];}
    /* Exact identity was unavailable; keep the older context-based resolver as fallback. */
    return lookup_ja(s,len);
}
static const TRANS_ENTRY* lookup_zh_mb(const BYTE*s,DWORD len){DWORD h,cur;if(!g_head_zh_mb)return NULL;h=fnv_bytes(s,len);cur=g_head_zh_mb[h&(BUCKET_COUNT-1)];while(cur!=INVALID_INDEX){TRANS_ENTRY*t=&g_trans[cur];if(t->hash_zh_mb==h&&t->zh_mb_len==len&&mem_equal(t->zh_mb,s,len))return t;cur=t->next_zh_mb;}return NULL;}
static const TRANS_ENTRY* lookup_zh_w(const WCHAR*s,DWORD len){DWORD h,cur;if(!g_head_zh_w)return NULL;h=fnv_wide(s,len);cur=g_head_zh_w[h&(BUCKET_COUNT-1)];while(cur!=INVALID_INDEX){TRANS_ENTRY*t=&g_trans[cur];if(t->hash_zh_w==h&&t->zh_w_len==len&&mem_equal(t->zh_w,s,len*2))return t;cur=t->next_zh_w;}return NULL;}
static const DIRECT_ENTRY* lookup_direct_mb(const BYTE*s,DWORD len){DWORD h,cur;if(!g_head_direct_mb)return NULL;h=fnv_bytes(s,len);cur=g_head_direct_mb[h&(BUCKET_COUNT-1)];while(cur!=INVALID_INDEX){DIRECT_ENTRY*d=&g_direct[cur];if(d->hash_mb==h&&d->mb_len==len&&mem_equal(d->mb,s,len))return d;cur=d->next_mb;}return NULL;}
static const DIRECT_ENTRY* lookup_direct_w(const WCHAR*s,DWORD len){DWORD h,cur;if(!g_head_direct_w)return NULL;h=fnv_wide(s,len);cur=g_head_direct_w[h&(BUCKET_COUNT-1)];while(cur!=INVALID_INDEX){DIRECT_ENTRY*d=&g_direct[cur];if(d->hash_w==h&&d->w_len==len&&mem_equal(d->w,s,len*2))return d;cur=d->next_w;}return NULL;}

/* ---------- raw-GBK mixed renderer adapter ----------
 *
 * Goal for this test line:
 *   - translated data stays byte-for-byte GBK, as in the old localization;
 *   - untranslated Japanese continues through the original CP932 renderer;
 *   - only strings we know/register as GBK, plus a conservative GBK fallback,
 *     enter the GDI glyph path.
 *
 * The packing below follows the high-word==0 path recovered from old .zeas
 * function 0x007DC200: GB2312_CHARSET, Song/Ming face for small sizes, Hei
 * face for large sizes, GGO_BITMAP -> 2bpp square glyph with outline/shadow.
 */
#define GBK_RANGE_COUNT 8192U
#define GBK_CACHE_SIZE 8192U
#define GBK_SUFFIX_TABLE_SIZE 524288U

typedef struct _GBK_RANGE { const BYTE* p; DWORD n; } GBK_RANGE;
typedef struct _GBK_SUFFIX_ENTRY { DWORD hash; const BYTE* p; DWORD n; } GBK_SUFFIX_ENTRY;
typedef struct _GBK_CACHE_ENTRY { WORD code; BYTE size; BYTE used; WORD cellw; BYTE* bits; } GBK_CACHE_ENTRY;
static GBK_RANGE g_gbk_ranges[GBK_RANGE_COUNT];
static GBK_SUFFIX_ENTRY* g_gbk_suffixes=NULL;
static DWORD g_gbk_suffix_count=0;
static DWORD g_gbk_suffix_drops=0;
static DWORD g_gbk_range_next=0;
static BYTE* g_known_gbk=NULL;             /* 65536-byte code bitmap */
static DWORD g_known_gbk_count=0;
static GBK_CACHE_ENTRY* g_gbk_cache=NULL;
static HDC g_gbk_dc[65];
static HFONT g_gbk_font[65];
/*
 * 第二套 DC 只服务于 Unicode GetGlyphOutlineW 路径。正常情况下仍优先沿用已经实机验证的
 * ANSI/GB2312 路径；只有用户明确指定 FontFace，或 ANSI 取字失败时才使用这套 DC。
 * 这样既保留 v1.0.1 的既有字形外观，又能避免某一个中文字形失败后掉回原版 CP932 路径。
 */
static HDC g_gbk_dc_w[65];
static HFONT g_gbk_font_w[65];
static BYTE g_gbk_matrix[8192];
static BYTE g_gbk_mask[8192];
/*
 * 极端情况下连 GDI Unicode fallback 都失败时，仍要给游戏一个“合法但空白”的字形缓冲，
 * 不能返回 NULL。返回 NULL 会让汇编 stub 回到原版 CP932 renderer，而用户第一次崩溃的
 * EDI=0xFFFFFFFF / EIP=0x0048A974 已证明那条 fallback 对已经确认的 GBK 双字节并不安全。
 */
static BYTE g_gbk_safe_blank[4096];
static volatile BYTE g_renderer_custom_pair=0;
static volatile DWORD g_renderer_last_code=0;
/*
 * IMPORTANT: the two old 1bpp battle/information blitters do NOT consume the same state as
 * the normal renderer at 0x00489726.  The old Chinese executable has a separate low-level
 * pipeline.  Two things happen BEFORE the final mask is drawn:
 *
 *   1) Three caller loops change the Shift-JIS "is this a double-byte lead byte?" cutoff from
 *      0xA0 to 0xFF.  After the preceding high-bit test, this makes every byte 0x80..0xFE take
 *      the two-byte path.  That is exactly what raw GBK needs in these old UI/battle loops.
 *   2) The DBCS/single-byte glyph lookup functions save the CURRENT character into a dedicated
 *      global.  Later the 1bpp blitter reads that saved character to build a GDI mask.
 *
 * test10 restored only step (2) at the function entry.  Its real-machine trace proved the
 * upstream tokenizer was still Shift-JIS: GBK "相马透" (CF E0 C2 ED CD B8) arrived as
 * CF | E0C2 | ED20 | CD | B8 instead of CFE0 | C2ED | CDB8.  test11 therefore reproduces the
 * old localization's exact byte-routing edits AND its exact state writes, at the original
 * instruction locations, with clean-2003 byte validation before any patch is written.
 */
static volatile DWORD g_lowlevel_current_code=0;
static PVOID g_renderer_parse_site=NULL,g_renderer_width_site=NULL,g_renderer_late_site=NULL;
static PVOID g_renderer_draw_site_a=NULL,g_renderer_draw_site_b=NULL;

static int gbk_trail(BYTE b){return b>=0x40&&b<=0xFE&&b!=0x7F;}
static void register_gbk_range(const BYTE*p,DWORD n){
    DWORD i;if(!p||n<2)return;
    for(i=0;i<GBK_RANGE_COUNT;i++)if(g_gbk_ranges[i].p==p){g_gbk_ranges[i].n=n;return;}
    g_gbk_ranges[g_gbk_range_next].p=p;g_gbk_ranges[g_gbk_range_next].n=n;
    g_gbk_range_next=(g_gbk_range_next+1U)&(GBK_RANGE_COUNT-1U);
}
static int ptr_in_gbk_range(const BYTE*p){
    DWORD i;ULONG_PTR q=(ULONG_PTR)p;
    for(i=0;i<GBK_RANGE_COUNT;i++){ULONG_PTR a=(ULONG_PTR)g_gbk_ranges[i].p;DWORD n=g_gbk_ranges[i].n;if(a&&q>=a&&q+1<a+n)return 1;}
    return 0;
}
static DWORD gbk_suffix_hash(const BYTE*s,DWORD n){
    DWORD h=2166136261UL,i;for(i=0;i<n;i++){h^=s[i];h*=16777619UL;}h^=n*0x9E3779B1UL;return h?h:1;
}
static void add_gbk_suffix(const BYTE*s,DWORD n){
    DWORD h,i;if(!g_gbk_suffixes||!s||n<2)return;h=gbk_suffix_hash(s,n);
    for(i=0;i<GBK_SUFFIX_TABLE_SIZE;i++){GBK_SUFFIX_ENTRY*e=&g_gbk_suffixes[(h+i)&(GBK_SUFFIX_TABLE_SIZE-1U)];
        if(!e->p){e->hash=h;e->p=s;e->n=n;g_gbk_suffix_count++;return;}
        if(e->hash==h&&e->n==n&&mem_equal(e->p,s,n))return;
    }g_gbk_suffix_drops++;
}
static void add_gbk_suffixes(const BYTE*s,DWORD n){
    DWORD i=0;if(!s)return;while(i+1<n){BYTE a=s[i];if(a<0x80){i++;continue;}if(a>=0x81&&a<=0xFE&&gbk_trail(s[i+1])){add_gbk_suffix(s+i,n-i);i+=2;}else i++;}
}
static int is_known_gbk_suffix(const BYTE*p){
    const BYTE*lo,*hi,*e;DWORD n,h,i;if(!g_gbk_suffixes||!p||!readable_context(p,&lo,&hi))return 0;
    e=p;while(e<hi&&*e&&((DWORD)(e-p)<4096U))e++;if(e>=hi||*e!=0)return 0;n=(DWORD)(e-p);if(n<2)return 0;h=gbk_suffix_hash(p,n);
    for(i=0;i<GBK_SUFFIX_TABLE_SIZE;i++){GBK_SUFFIX_ENTRY*x=&g_gbk_suffixes[(h+i)&(GBK_SUFFIX_TABLE_SIZE-1U)];if(!x->p)return 0;if(x->hash==h&&x->n==n&&mem_equal(x->p,p,n))return 1;}return 0;
}
static int string_has_known_gbk(const BYTE*s){
    const BYTE*lo,*hi,*p,*e;if(!s||!readable_context(s,&lo,&hi))return 0;if(ptr_in_gbk_range(s))return 1;
    e=s;while(e<hi&&*e&&((DWORD)(e-s)<4096U))e++;if(e>=hi||*e!=0)return 0;
    p=s;while(p+1<e){BYTE a=*p;if(a<0x80){p++;continue;}if(a>=0x81&&a<=0xFE&&gbk_trail(p[1])){if(is_known_gbk_suffix(p))return 1;p+=2;}else p++;}return 0;
}
static void note_gbk_stream(const BYTE*s,DWORD n){
    DWORD i=0;if(!g_known_gbk||!s)return;
    while(i<n){BYTE a=s[i];if(a<0x80){i++;continue;}if(a>=0x81&&a<=0xFE&&i+1<n&&gbk_trail(s[i+1])){WORD c=(WORD)(((WORD)a<<8)|s[i+1]);if(!g_known_gbk[c]){g_known_gbk[c]=1;g_known_gbk_count++;}i+=2;}else i++;}
}
static int build_known_gbk_codes(void){
    DWORD i;if(!g_heap)return 0;
    g_known_gbk=(BYTE*)pHeapAlloc(g_heap,HEAP_ZERO_MEMORY,65536UL);
    g_gbk_cache=(GBK_CACHE_ENTRY*)pHeapAlloc(g_heap,HEAP_ZERO_MEMORY,sizeof(GBK_CACHE_ENTRY)*GBK_CACHE_SIZE);
    g_gbk_suffixes=(GBK_SUFFIX_ENTRY*)pHeapAlloc(g_heap,HEAP_ZERO_MEMORY,sizeof(GBK_SUFFIX_ENTRY)*GBK_SUFFIX_TABLE_SIZE);
    if(!g_known_gbk||!g_gbk_cache||!g_gbk_suffixes)return 0;
    for(i=0;i<g_trans_count;i++){note_gbk_stream(g_trans[i].zh_mb,g_trans[i].zh_mb_len);add_gbk_suffixes(g_trans[i].zh_mb,g_trans[i].zh_mb_len);}
    for(i=0;i<g_direct_count;i++){note_gbk_stream(g_direct[i].mb,g_direct[i].mb_len);add_gbk_suffixes(g_direct[i].mb,g_direct[i].mb_len);}
    for(i=0;i<UI_PATCH_COUNT;i++){note_gbk_stream(g_ui_patches[i].dst,g_ui_patches[i].dst_len);add_gbk_suffixes(g_ui_patches[i].dst,g_ui_patches[i].dst_len);}
    for(i=0;i<LEGACY_STATIC_PATCH_COUNT;i++){note_gbk_stream(g_legacy_static_patches[i].dst,g_legacy_static_patches[i].dst_len);add_gbk_suffixes(g_legacy_static_patches[i].dst,g_legacy_static_patches[i].dst_len);}
    for(i=0;i<LATE_STATIC_PATCH_COUNT;i++){note_gbk_stream(g_late_static_patches[i].dst,g_late_static_patches[i].dst_len);add_gbk_suffixes(g_late_static_patches[i].dst,g_late_static_patches[i].dst_len);}
    log_kv("已知 GBK 双字节编码数：",g_known_gbk_count);log_kv("已知 GBK 汉化后缀数：",g_gbk_suffix_count);log_kv("GBK 后缀表丢弃数：",g_gbk_suffix_drops);return 1;
}
static int CDECL RendererClassifyGBK(const BYTE*p){
    BYTE a,b;WORD c;
    g_renderer_custom_pair=0;
    if(!p)return 0;
    a=p[0];
    /* Exact old-localization byte-consumption rule recovered from the unpacked
       runtime renderer: every leading byte 0x80..0xFE is consumed together
       with the following byte.  The original localization did NOT use a
       per-string GBK/CP932 provenance test here.  Our gbk1-gbk3 selective
       classification therefore left most copied/formatted GBK buffers on the
       original CP932 path and produced the observed half-width-kana mojibake. */
    if(a<0x80||a>0xFE)return 0;
    b=p[1];
    c=(WORD)(((WORD)a<<8)|b);
    g_renderer_custom_pair=1;
    g_renderer_last_code=(DWORD)c;
    diag_note_glyph(1U,(DWORD)c,0U,0U);
    g_gbk_renderer_hits++;
    if(g_gbk_renderer_hits==1){
        log_s("[命中] 旧汉化全局 0x80..0xFE DBCS 渲染器已启用，字符码=");
        log_hex32((DWORD)c);log_nl();
    }
    return 1;
}
static HDC get_gbk_dc(DWORD size){
    LOGFONTA_MIN lf;
    HDC dc;
    HFONT f;
    HGDIOBJ old;
    DWORD i;

    /*
     * 这是已经在 test11~v1.0.1 实机通过的 ANSI/GB2312 主路径。
     * 没有用户自定义 FontFace 时仍优先走它，避免为了修一个缺字而无谓改变全部中文字形外观。
     */
    if(size>64||size<1||!g_gdi_font_a||!g_gdi_createdc||!g_gdi_select)return NULL;
    if(g_gbk_dc[size])return g_gbk_dc[size];

    mem_zero(&lf,sizeof(lf));
    lf.lfHeight=(LONG)size;
    lf.lfWeight=(LONG)(size>=14?700:200);
    lf.lfCharSet=GB2312_CHARSET;
    lf.lfQuality=NONANTIALIASED_QUALITY;
    lf.lfPitchAndFamily=2;

    /*
     * 旧汉化使用 GB2312 字符集：小字号偏宋体，大字号偏黑体。
     * 这里保留它的字节写法，仅作为“默认 ANSI 主路径”；Unicode fallback 另有 W 版字体。
     */
    if(size<22){
        static const BYTE song[]={0xCB,0xCE,0xCC,0xE5,0};
        for(i=0;i<5;i++)lf.lfFaceName[i]=(char)song[i];
    }else{
        static const BYTE hei[]={0xBA,0xDA,0xCC,0xE5,0};
        for(i=0;i<5;i++)lf.lfFaceName[i]=(char)hei[i];
    }

    dc=g_gdi_createdc(NULL);
    if(!dc)return NULL;
    f=g_gdi_font_a(&lf);
    if(!f){
        if(g_gdi_deletedc)g_gdi_deletedc(dc);
        return NULL;
    }
    old=g_gdi_select(dc,(HGDIOBJ)f);
    (void)old;
    g_gbk_dc[size]=dc;
    g_gbk_font[size]=f;
    return dc;
}

static HDC get_gbk_dc_unicode(DWORD size){
    HDC dc;
    HFONT f;
    HGDIOBJ old;
    const WCHAR* face;
    static const WCHAR song_w[]={0x5B8B,0x4F53,0}; /* “宋体” */
    static const WCHAR hei_w[]={0x9ED1,0x4F53,0};  /* “黑体” */

    /*
     * Unicode DC 有两个用途：
     *   1. 用户在 BaldrForceCN.ini 里指定 FontFace 时，确保字体名不受系统 ACP 影响；
     *   2. 默认 ANSI GetGlyphOutlineA 对某个 GBK 字形失败时，用 GetGlyphOutlineW 安全救回。
     */
    if(size>64||size<1||!g_gdi_createfont||!g_gdi_createdc||!g_gdi_select)return NULL;
    if(g_gbk_dc_w[size])return g_gbk_dc_w[size];

    if(g_force_face[0])face=g_force_face;
    else face=(size<22)?song_w:hei_w;

    dc=g_gdi_createdc(NULL);
    if(!dc)return NULL;

    f=g_gdi_createfont(
        (int)size,                 /* 字体高度，与旧汉化字号保持一致。 */
        0,                         /* 宽度 0：交给 GDI 按字体本身决定。 */
        0,0,                       /* 不旋转字体。 */
        (int)(size>=14?700:200),   /* 保留旧路径的粗细规则。 */
        0,0,0,                     /* 非斜体、非下划线、非删除线。 */
        GB2312_CHARSET,            /* 中文字符集提示仍固定为 134。 */
        OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS,
        NONANTIALIASED_QUALITY,
        DEFAULT_PITCH,
        face
    );
    if(!f){
        if(g_gdi_deletedc)g_gdi_deletedc(dc);
        return NULL;
    }

    old=g_gdi_select(dc,(HGDIOBJ)f);
    (void)old;
    g_gbk_dc_w[size]=dc;
    g_gbk_font_w[size]=f;
    return dc;
}

static int gbk_code_to_unicode(WORD code,WCHAR* out){
    BYTE mb[2];
    int n;

    if(!out||!g_sys_mbtowc)return 0;

    /*
     * Runtime 的 WORD 按“高字节=GBK lead、低字节=trail”保存，例如“相”的 CF E0 对应 0xCFE0。
     * MultiByteToWideChar 需要重新拆成原始两个字节，再按 CP936 转为一个 Unicode 字符。
     */
    mb[0]=(BYTE)(code>>8);
    mb[1]=(BYTE)(code&0xFFU);
    n=g_sys_mbtowc(CP_GBK,0,(LPCCH)mb,2,out,1);
    return n==1;
}

static DWORD query_gbk_outline(WORD code,DWORD size,GLYPHMETRICS_MIN* gm,BYTE* buffer,DWORD buffer_size,const MAT2_MIN* m,HDC* out_dc){
    DWORD need=GDI_ERROR;
    HDC dc=NULL;
    WCHAR wc=0;

    if(out_dc)*out_dc=NULL;

    /*
     * 如果用户明确指定了字体，就直接使用 Unicode 字体/Unicode 字符取字。
     * 这样“微软雅黑”等字体名不依赖当前 Windows 的 ANSI 系统区域设置。
     */
    if(g_force_face[0]&&g_gdi_glyph&&gbk_code_to_unicode(code,&wc)){
        dc=get_gbk_dc_unicode(size);
        if(dc){
            need=g_gdi_glyph(dc,(UINT)wc,GGO_BITMAP,gm,buffer_size,buffer,m);
            if(need!=GDI_ERROR){
                if(out_dc)*out_dc=dc;
                return need;
            }
        }
    }

    /*
     * 默认情况先走旧汉化风格的 ANSI/GB2312 路径。绝大多数已经验证通过的文字仍走这里，
     * 因此本修复不会把整个游戏的中文字形重新换一套实现。
     */
    dc=get_gbk_dc(size);
    if(dc&&g_gdi_glyph_a){
        need=g_gdi_glyph_a(dc,(UINT)code,GGO_BITMAP,gm,buffer_size,buffer,m);
        if(need!=GDI_ERROR){
            if(out_dc)*out_dc=dc;
            return need;
        }
        g_gbk_outline_ansi_failures++;
    }

    /*
     * 关键修复：ANSI 取字失败后绝不能把已经确认是 GBK 的双字节送回原版 CP932 renderer。
     * 改用 CP936->Unicode + GetGlyphOutlineW 再试一次。第一次八木泽崩溃正是在旧 fallback 后
     * 以 EDI=0xFFFFFFFF 进入 0x0048A974，因此这里必须把编码边界封死。
     */
    if(g_gdi_glyph&&gbk_code_to_unicode(code,&wc)){
        dc=get_gbk_dc_unicode(size);
        if(dc){
            mem_zero(gm,sizeof(*gm));
            mem_zero(buffer,buffer_size);
            need=g_gdi_glyph(dc,(UINT)wc,GGO_BITMAP,gm,buffer_size,buffer,m);
            if(need!=GDI_ERROR){
                g_gbk_outline_unicode_fallbacks++;
                if(g_gbk_outline_unicode_fallbacks<=8U){
                    log_s("[诊断] ANSI 中文字形失败后已由 Unicode fallback 恢复，GBK字符码=");
                    log_hex32((DWORD)code);log_s("，字号=");log_dec(size);log_nl();
                }
                if(out_dc)*out_dc=dc;
                return need;
            }
        }
    }

    g_gbk_outline_hard_failures++;
    if(g_gbk_outline_hard_failures<=16U){
        log_s("[警告] GBK 字形的 ANSI/Unicode 两条 GDI 路径均失败；将返回安全空白字形而不回退 CP932。字符码=");
        log_hex32((DWORD)code);log_s("，字号=");log_dec(size);log_nl();
    }
    return GDI_ERROR;
}

static BYTE* make_safe_blank_glyph(DWORD size,DWORD cellw){
    DWORD packrow,outsz;
    BYTE* out;

    if(size>64)size&=0x3FU;
    if(size>=2)size-=2;
    if(size<12)size=12;
    if(size>64)size=64;
    if(cellw<4)cellw=4;
    if(cellw>128)cellw=128;

    packrow=(cellw+3U)>>2;
    outsz=(size+3U)*packrow+1U;

    /*
     * 正常情况下仍从进程堆分配一个和真实字形同尺寸的全 0 缓冲，便于缓存长期持有。
     * 如果连堆分配都失败，再退到静态 4096-byte 全 0 数组。这个数组大于本项目最大字形尺寸，
     * 所以游戏后续读取不会越界；最坏结果只是该字符暂时显示为空白，而不是崩溃。
     */
    out=(BYTE*)pHeapAlloc(g_heap,HEAP_ZERO_MEMORY,outsz);
    if(out)return out;
    mem_zero(g_gbk_safe_blank,sizeof(g_gbk_safe_blank));
    return g_gbk_safe_blank;
}

static BYTE* build_gbk_glyph_uncached(WORD code,DWORD rawh,DWORD cellw){
    DWORD size=rawh,rowbytes,stride,packrow,outsz,need,x,y,drawW,drawH;
    LONG ox,voff;
    HDC dc;
    GLYPHMETRICS_MIN gm;
    TEXTMETRICA_MIN tm;
    MAT2_MIN m;
    BYTE* out;

    if(size>64)size&=0x3FU;
    if(size>=2)size-=2;
    if(size<12)size=12;
    if(size>64)size=64;
    if(cellw<4)cellw=4;
    if(cellw>128)cellw=128;
    packrow=(cellw+3U)>>2;
    if(!packrow)return make_safe_blank_glyph(size,cellw);

    m.eM11.fract=0;m.eM11.value=1;
    m.eM12.fract=0;m.eM12.value=0;
    m.eM21.fract=0;m.eM21.value=0;
    m.eM22.fract=0;m.eM22.value=1;
    mem_zero(&gm,sizeof(gm));
    mem_zero(&tm,sizeof(tm));
    mem_zero(g_glyph_tmp,sizeof(g_glyph_tmp));
    mem_zero(g_gbk_matrix,sizeof(g_gbk_matrix));

    need=query_gbk_outline(code,size,&gm,g_glyph_tmp,sizeof(g_glyph_tmp),&m,&dc);
    if(need==GDI_ERROR||!dc||!g_gdi_metrics_a){
        g_gbk_glyph_failures++;
        return make_safe_blank_glyph(size,cellw);
    }
    if(!g_gdi_metrics_a(dc,&tm)){
        g_gbk_glyph_failures++;
        return make_safe_blank_glyph(size,cellw);
    }

    rowbytes=((gm.gmBlackBoxX+31U)&~31U)>>3;
    stride=(gm.gmBlackBoxX+5U)&~3U;
    if(!stride||stride*(gm.gmBlackBoxY+3U)>sizeof(g_gbk_matrix)){
        g_gbk_glyph_failures++;
        return make_safe_blank_glyph(size,cellw);
    }

    for(y=0;y<gm.gmBlackBoxY;y++){
        for(x=0;x<gm.gmBlackBoxX;x++){
            if(g_glyph_tmp[y*rowbytes+(x>>3)]&(BYTE)(0x80U>>(x&7))){
                DWORD r0=y*stride,r1=(y+1U)*stride;
                g_gbk_matrix[r1+x+1U]=3;
                if(!g_gbk_matrix[r0+x+1U])g_gbk_matrix[r0+x+1U]=2;
                if(!g_gbk_matrix[r1+x])g_gbk_matrix[r1+x]=2;
                if(!g_gbk_matrix[r0+x])g_gbk_matrix[r0+x]=2;
                if(!g_gbk_matrix[r0+x+2U])g_gbk_matrix[r0+x+2U]=2;
            }
        }
    }

    if(gm.gmBlackBoxY){
        for(y=gm.gmBlackBoxY;y>0;y--){
            for(x=gm.gmBlackBoxX;x>0;x--){
                if(g_gbk_matrix[y*stride+x]==3){
                    if(!g_gbk_matrix[(y+1U)*stride+x])g_gbk_matrix[(y+1U)*stride+x]=1;
                    if(!g_gbk_matrix[y*stride+x+1U])g_gbk_matrix[y*stride+x+1U]=1;
                    if(x&&!g_gbk_matrix[(y+1U)*stride+x-1U])g_gbk_matrix[(y+1U)*stride+x-1U]=1;
                    if(!g_gbk_matrix[(y+1U)*stride+x+1U])g_gbk_matrix[(y+1U)*stride+x+1U]=1;
                }
            }
        }
    }

    drawW=gm.gmBlackBoxX+2U;
    drawH=gm.gmBlackBoxY+2U;
    outsz=(size+3U)*packrow+1U;
    out=(BYTE*)pHeapAlloc(g_heap,HEAP_ZERO_MEMORY,outsz);
    if(!out)return make_safe_blank_glyph(size,cellw);

    ox=gm.gmptGlyphOrigin.x;
    voff=tm.tmAscent-gm.gmptGlyphOrigin.y;

    /*
     * 旧 .zeas normal（high-word == 0）路径的垂直定位规则：
     * GetTextMetricsA 得到 ascent，再用 ascent-glyphOriginY 算实际原点，最后从 size-voff 向上打包。
     * 这就是 test11 之后一直保持正常的 gbk4 位置关系，本轮只改“取不到字时怎么办”，不改排版数学。
     */
    for(y=0;y<drawH;y++){
        LONG rr=(LONG)size-voff-(LONG)y;
        LONG xmax=(LONG)drawW+ox;
        if(rr<0||rr>=(LONG)(size+3U)||xmax<=0)continue;
        for(x=0;x<(DWORD)xmax;x++){
            if((LONG)x>=ox){
                LONG mx=(LONG)x-ox;
                BYTE v;
                if(mx<0||mx>=(LONG)stride)continue;
                v=g_gbk_matrix[y*stride+(DWORD)mx];
                if(v&&(x>>2)<packrow)out[(DWORD)rr*packrow+(x>>2)]|=(BYTE)(v<<((x&3U)*2U));
            }
        }
    }

    g_gbk_glyph_builds++;
    return out;
}

static BYTE* CDECL RendererGetGBKGlyph(DWORD code,DWORD rawh,DWORD cellw){
    DWORD size=rawh,h,i;if(size>64)size&=0x3FU;if(size>=2)size-=2;if(size<12)size=12;if(size>64)size=64;if(cellw>128)cellw=128;
    h=(((DWORD)(WORD)code*2654435761UL)^(size*40503UL)^(cellw*97UL))&(GBK_CACHE_SIZE-1U);
    for(i=0;i<GBK_CACHE_SIZE;i++){GBK_CACHE_ENTRY*e=&g_gbk_cache[(h+i)&(GBK_CACHE_SIZE-1U)];if(!e->used){BYTE*b=build_gbk_glyph_uncached((WORD)code,rawh,cellw);if(!b)return NULL;e->used=1;e->code=(WORD)code;e->size=(BYTE)size;e->cellw=(WORD)cellw;e->bits=b;return b;}if(e->code==(WORD)code&&e->size==(BYTE)size&&e->cellw==(WORD)cellw)return e->bits;}
    return build_gbk_glyph_uncached((WORD)code,rawh,cellw);
}

/* Old .zeas high-word != 0 path used by the two low-level glyph blitters
 * (clean 2003: 0x00415840 and 0x00415CF1).  The high word is the caller's
 * 1bpp row stride.  This path is intentionally NOT the outlined 2bpp glyph
 * above: it produces the exact bit mask consumed by BT [esi],edx. */
static BYTE* CDECL RendererGetGBKMask(DWORD code,DWORD rawh,DWORD rowstride){
    DWORD size=rawh,rowbytes,stride,need,x,y;
    LONG ox,voff,xmax;
    HDC dc;
    GLYPHMETRICS_MIN gm;
    TEXTMETRICA_MIN tm;
    MAT2_MIN m;

    if(size>64)size&=0x3FU;
    if(size<12)size=12;
    if(size>64)size=64;
    if(rowstride<1||rowstride>256)return NULL;

    diag_note_glyph(2U,(DWORD)(WORD)code,rawh,rowstride);

    m.eM11.fract=0;m.eM11.value=1;
    m.eM12.fract=0;m.eM12.value=0;
    m.eM21.fract=0;m.eM21.value=0;
    m.eM22.fract=0;m.eM22.value=1;
    mem_zero(&gm,sizeof(gm));
    mem_zero(&tm,sizeof(tm));
    mem_zero(g_glyph_tmp,sizeof(g_glyph_tmp));
    mem_zero(g_gbk_matrix,sizeof(g_gbk_matrix));
    mem_zero(g_gbk_mask,sizeof(g_gbk_mask));

    /*
     * 低层 1bpp 路径和普通 2bpp renderer 共享同一套“ANSI 失败 -> Unicode fallback”取字逻辑。
     * 这保证 Information/练习模式等 test11 已修好的特殊路径不会因为某一个缺字再次掉回 CP932。
     */
    need=query_gbk_outline((WORD)code,size,&gm,g_glyph_tmp,sizeof(g_glyph_tmp),&m,&dc);
    if(need==GDI_ERROR||!dc||!g_gdi_metrics_a){
        g_gbk_mask_failures++;
        /*
         * g_gbk_mask 已经清零。返回这个合法缓冲意味着“本字符暂时空白”，而不是返回 NULL 让
         * 汇编 stub 重新走原版字形指针逻辑。对已经确认是 GBK 的字符，安全优先于错误回退。
         */
        return g_gbk_mask;
    }
    if(!g_gdi_metrics_a(dc,&tm)){
        g_gbk_mask_failures++;
        return g_gbk_mask;
    }

    rowbytes=((gm.gmBlackBoxX+31U)>>5)<<2;
    stride=(gm.gmBlackBoxX+5U)&~3U;
    if(!stride||stride*(gm.gmBlackBoxY+1U)>sizeof(g_gbk_matrix)){
        g_gbk_mask_failures++;
        return g_gbk_mask;
    }

    for(y=0;y<gm.gmBlackBoxY;y++){
        for(x=0;x<gm.gmBlackBoxX;x++){
            if(g_glyph_tmp[y*rowbytes+(x>>3)]&(BYTE)(0x80U>>(x&7)))g_gbk_matrix[y*stride+x]=1;
        }
    }

    ox=gm.gmptGlyphOrigin.x;
    voff=tm.tmAscent-gm.gmptGlyphOrigin.y;
    if(voff<0||(DWORD)voff*rowstride>=sizeof(g_gbk_mask)){
        g_gbk_mask_failures++;
        return g_gbk_mask;
    }

    for(y=0;y<gm.gmBlackBoxY;y++){
        LONG rr=voff+(LONG)y;
        BYTE* dst;
        if(rr<0||(DWORD)rr*rowstride>=sizeof(g_gbk_mask))continue;
        dst=g_gbk_mask+(DWORD)rr*rowstride;
        xmax=(LONG)gm.gmBlackBoxX+ox;
        if(xmax<=0)continue;
        for(x=0;x<(DWORD)xmax;x++){
            if((LONG)x>=ox){
                LONG mx=(LONG)x-ox;
                if(mx>=0&&mx<(LONG)stride&&g_gbk_matrix[y*stride+(DWORD)mx]){
                    DWORD bi=x>>3;
                    if(bi<rowstride)dst[bi]|=(BYTE)(1U<<(x&7U));
                }
            }
        }
    }

    /*
     * 只打印最前面的 12 个低层字符。这个数量足够判断 lookup Hook 与 mask builder
     * 是否正在按不同 GBK code 逐字前进，同时又不会让长时间战斗把日志刷得非常大。
     * 这些追踪信息只观察状态，不改变任何字形、指针或绘制行为。
     */
    if(g_gbk_mask_builds<12){
        log_s("[追踪] 同步低层字形掩码 #");log_dec(g_gbk_mask_builds+1);
        log_s(" 字符码=");log_hex32((DWORD)(WORD)code);log_s("，行步长=");log_dec(rowstride);log_nl();
    }
    g_gbk_mask_builds++;
    return g_gbk_mask;
}

/* ---------- internal script-string hook ----------
 * 2003 engine evidence:
 *   Script opcode 0x0B stores a string as: WORD 0x000B, DWORD byte_count, raw CP932 bytes including NUL.
 *   All 34,184 BFET sources point exactly at such raw string bytes.
 *   Function signature below is the engine routine at 0x004ACC20 in the supplied 2003 EXE.
 *
 * Original routine ABI is __thiscall: ECX=self, stack=(dest,count), RET 8.
 * A Win32 __fastcall function is ABI-compatible here: ECX=self, EDX ignored, stack=(dest,count), RET 8.
 * We reimplement the tiny original routine instead of trampolining it, then modify only opcode-0x0B values.
 */
static int FASTCALL Hook_ScriptCopyBytes(PVOID self,PVOID unused_edx,BYTE* dest,DWORD count){
    BYTE* state;
    BYTE* body;
    DWORD cur;
    DWORD limit;
    DWORD i;
    const BYTE* src;
    const TRANS_ENTRY* t;
    SCRIPT_DIAG_EVENT* diag_event;

    (void)unused_edx;

    /*
     * 先取得 clean 2003 脚本流的三个核心字段：
     *   state+0x114 = 当前读取偏移；
     *   state+0x0C  = 当前脚本可读上限；
     *   state+0x128 = 当前脚本主体起点。
     *
     * 这一步不做翻译，只是在复刻原复制函数开始工作前必须具备的状态。
     */
    if(!self||!dest)return 0;
    state=*(BYTE**)((BYTE*)self+8);
    if(!state)return 0;
    cur=*(DWORD*)(state+0x114);
    limit=*(DWORD*)(state+0x0C);
    body=*(BYTE**)(state+0x128);
    if(!body||cur>limit)return 0;

    /*
     * 诊断版先把“这一笔复制请求”记进固定环形缓冲。
     * 即使后面不是 0x0B、没有命中翻译，发生崩溃时也能知道最后一次脚本复制停在什么偏移。
     */
    diag_event=diag_begin_script_event(cur,count,dest,state,body,limit);

    /*
     * v1.0.1 只有 cur>=limit 的检查，然后仍可能复制 count 个字节。
     * 如果坏脚本/坏状态让 count 越过 limit，就会直接读出脚本缓冲区。
     * 旧游戏正常数据不应该触发这里，所以加上严格边界检查只会把潜在越界变成安全失败。
     */
    if(count>limit-cur){
        log_s("[失败] 脚本复制请求越过当前脚本边界；已拒绝潜在越界读取。偏移=");
        log_hex32(cur);log_s("，长度=");log_dec(count);log_s("，上限=");log_hex32(limit);log_nl();
        return 0;
    }

    src=body+cur;

    /*
     * 旧 46MB 汉化运行时的真实函数（运行时主模块约 0x004ACB40）先把脚本游标增加“原始 count”。
     * 这非常关键：无论中文最终比日文长还是短，脚本解释器消费的仍是原脚本里的那一段字节。
     */
    *(DWORD*)(state+0x114)=cur+count;
    if(diag_event)diag_event->post_cur=cur+count;
    g_script_hook_hits++;

    /*
     * opcode 0x0B 的 value object 布局已经在 34,184 条 BFET 源字符串上验证过：
     *   dest-8 : type = 0x0000000B
     *   dest-4 : 当前运行时 byte_count（包含结尾 NUL；进入 Hook 时最初等于原脚本 count）
     *   dest   : 最多 0x1000 字节的字符缓冲区
     *
     * 注意这里检查 src[count-1]，而不是先把日文复制进 dest 再检查 dest[count-1]。
     * 这样命中翻译时可以真正复刻旧汉化的 strcpy 语义，不会在中文结尾后残留一份刚复制进去的日文尾巴。
     */
    if(count>0 && count<=0x1000 && src[count-1]==0 && *(DWORD*)(dest-8)==0x0B && *(DWORD*)(dest-4)==count){
        if(diag_event){diag_event->flags|=4U;diag_event->value_type=*(DWORD*)(dest-8);diag_event->value_count=*(DWORD*)(dest-4);}
        t=lookup_ja_script(src,count-1,body,cur,limit);
        if(t){
            DWORD outn=t->zh_mb_len;
            /* 把本次命中的稳定数字身份写回刚才的诊断事件；异常时不再需要访问 t 指针。 */
            if(diag_event){
                DWORD jo,jc,zo,zc;
                diag_event->flags|=1U;diag_event->source_abs=t->source_abs;diag_event->ja_hash=t->hash_ja;diag_event->zh_hash=t->hash_zh_mb;
                diag_event->ja_len=t->ja_len;diag_event->zh_len=t->zh_mb_len;diag_event->mapping_index=t->mapping_index;
                diag_copy_script_name(diag_event->script_name,t->script_name);
                diag_event->ja_preview_len=diag_copy_preview(diag_event->ja_preview,t->ja,t->ja_len);
                diag_event->zh_preview_len=diag_copy_preview(diag_event->zh_preview,t->zh_mb,t->zh_mb_len);
                if(outn+1U>count){
                    diag_event->flags|=8U;
                    if(diag_original_count_splits_gbk(t->zh_mb,t->zh_mb_len,count))diag_event->flags|=16U;
                }
                jo=diag_count_cp932_pair(t->ja,t->ja_len,0x81,0x75);jc=diag_count_cp932_pair(t->ja,t->ja_len,0x81,0x76);
                zo=diag_count_gbk_pair(t->zh_mb,t->zh_mb_len,0xA1,0xB8);zc=diag_count_gbk_pair(t->zh_mb,t->zh_mb_len,0xA1,0xB9);
                if(zo!=zc)diag_event->flags|=32U;
                if(jo!=zo||jc!=zc)diag_event->flags|=64U;
            }
            if(outn<0x1000){
                /*
                 * 【v1.0.4 Backlog 修复：分离“脚本源长度”和“运行时字符串长度”】
                 *
                 * 两份 v1.0.3 崩溃日志已经把两个概念彻底区分开：
                 *   - state+0x114 的脚本游标必须继续按原始日文 count 前进，否则下一条 opcode 会读错位置；
                 *   - dest-4 是已经构造出来的 value object 的 byte_count，Backlog/历史记录等二次消费者会读它。
                 *
                 * jyosyo05.bin #64 是最直接的证据：日文 20 bytes、中文 26 bytes、原 count=21。
                 * v1.0.3 保留 dest-4=21 后，主画面把中文写进了 dest，但 Backlog 再次消费对象时会按 21 bytes
                 * 截断，恰好切在 GBK 双字节中间；画面出现缺右引号/方块，随后 Backlog 崩溃。
                 *
                 * 因此正确做法是：上面已经把脚本游标按原 count 推进；这里把运行时对象的 byte_count
                 * 更新为实际中文长度+NUL。两套长度各管各的，不能再混成一个字段。
                 */
                mem_copy(dest,t->zh_mb,outn);
                dest[outn]=0;
                *(DWORD*)(dest-4)=outn+1U;
                if(diag_event)diag_event->value_count=outn+1U;
                register_gbk_range(dest,outn+1);
                g_script_hook_replaced++;
                g_script_runtime_count_updated++;
                if(outn+1!=count)g_script_length_delta_seen++;

                /*
                 * 只把前 24 次“中文比原脚本源更长”的现场直接写日志。
                 * 这些信息现在主要用来验证：源脚本 count 仍保持原值，而 value object count 已经改成中文长度。
                 * 如果以后还有另一个系统绕过 value object、直接错误复用原 count，这些记录仍能快速定位。
                 */
                if(outn+1U>count&&g_runtime_expansion_diag_logs<24U){
                    g_runtime_expansion_diag_logs++;
                    log_s("[诊断][扩张文本] 脚本=");log_s(t->script_name?t->script_name:"<未知>");
                    log_s("，映射#=");log_dec(t->mapping_index);log_s("，日/中=");log_dec(t->ja_len);log_s("->");log_dec(t->zh_mb_len);
                    log_s("，原count=");log_dec(count);
                    if(diag_original_count_splits_gbk(t->zh_mb,t->zh_mb_len,count))log_s("，若按原count二次复制：会截在GBK双字节中间");
                    else log_s("，若按原count二次复制：会截断但不在双字节中间");
                    log_s("，JAhash=");log_hex32(t->hash_ja);log_s("，ZHhash=");log_hex32(t->hash_zh_mb);log_nl();
                }

                /*
                 * 用户新截图已经把一个具体问题钉到 jyosyo05.bin #64：
                 *   JA 20 bytes: @f1A0121「………な」
                 *   ZH 26 bytes: @f1A0121「………什么……」
                 * 原 count=21。若任何后续系统只复制这 21 个中文字节，就会停在一个 GBK
                 * 双字节首字节上，画面末尾出现方块；随后打开 Backlog 崩溃与这个现象高度一致。
                 * v1.0.4 不修改“源脚本 count”，但已经在上面把运行时 value object 的 byte_count
                 * 更新为中文实际长度+NUL。这里的诊断只负责标记这个已知复现句，不再做额外特殊处理。
                 */
                if(t->hash_ja==0x482ACBB0UL&&t->ja_len==20U&&t->hash_zh_mb==0x6AA94A29UL&&t->zh_mb_len==26U){
                    log_s("[诊断][Backlog复现句] 命中 jyosyo05.bin #64：20->26；脚本源count仍为21，运行时value object count已更新为27。\r\n");
                }

                if(g_script_hook_replaced==1){
                    log_s("[命中] 操作码 0x0B BFET 替换已启用；脚本游标保留原count，运行时对象byte_count使用中文实际长度。日文字节数=");
                    log_dec(count-1);log_s(" -> 原始 GBK 字节数=");log_dec(outn);log_nl();
                }else if(g_script_hook_replaced==10||g_script_hook_replaced==100||g_script_hook_replaced==1000){
                    log_s("脚本替换次数：");log_dec(g_script_hook_replaced);
                    log_s("；其中中日长度不同且已更新运行时byte_count：");log_dec(g_script_length_delta_seen);log_nl();
                }

                /*
                 * 这是用户当前稳定崩溃点的标题卡：
                 * “国连军治安维持局信息管理处第一小队队长 ... 八木泽 宗次”。
                 * 它自己的中日长度恰好同为 71 字节，所以真正危险的是紧随其后的短译文。
                 * 记录一次命中，方便实机日志确认已经进入 v1.0.4 的双长度复制语义与字形安全回退测试区间。
                 */
                if(t->hash_ja==0xF7F56B7AUL&&t->ja_len==71&&t->hash_zh_mb==0x62BB16C5UL&&t->zh_mb_len==71){
                    g_crash_scene_marker_hits++;
                    if(g_crash_scene_marker_hits==1){
                        log_s("[诊断] 已命中八木泽名片复现标题卡；该条本身71->71。若本版仍异常，重点检查中文字形 Unicode fallback 与崩溃EIP。\r\n");
                    }
                }
                return 1;
            }
        }else{
            const DIRECT_ENTRY* d=lookup_direct_mb(src,count-1);
            if(d&&d->mb_len<0x1000){
                DWORD outn=d->mb_len;
                if(diag_event)diag_event->flags|=2U;
                mem_copy(dest,d->mb,outn);
                dest[outn]=0;
                /*
                 * direct-entry（主要是 after.bin）同样已经成为运行时字符串对象。
                 * 所以后续消费者看到的 byte_count 也必须和实际 GBK 缓冲一致；脚本游标仍然早已按原 count 前进。
                 */
                *(DWORD*)(dest-4)=outn+1U;
                if(diag_event)diag_event->value_count=outn+1U;
                register_gbk_range(dest,outn+1);
                g_script_hook_replaced++;
                g_script_runtime_count_updated++;
                if(outn+1!=count)g_script_length_delta_seen++;

                /*
                 * after.bin 没有 BFET；它的 direct-entry 本身就是旧汉化 GBK。
                 * 这里只改变当前 value object 的真实字符串长度，不改变脚本源流的读取位置。
                 */
                g_after_bin_direct_runtime_hits++;
                if(g_after_bin_direct_runtime_hits==1){
                    log_s("[命中] after.bin 直接 GBK 脚本文本已执行（运行时已确认）\r\n");
                }
                return 1;
            }
        }
    }

    /*
     * 没有命中 BFET/direct-entry 时，行为就是原版的 rep movsb：按原 count 原样复制。
     * 因为脚本游标已经在上面增加过 count，这里绝不能再改游标。
     */
    for(i=0;i<count;i++)dest[i]=src[i];
    return 1;
}

static BYTE* main_image_base(DWORD* out_size){
    BYTE* peb=(BYTE*)get_peb();BYTE* base=*(BYTE**)(peb+8);DWORD peoff;
    if(out_size)*out_size=0;if(!base||u16(base)!=0x5A4D)return NULL;peoff=u32(base+0x3C);
    if(u32(base+peoff)!=0x4550||u16(base+peoff+24)!=0x10B)return NULL;
    if(out_size)*out_size=u32(base+peoff+24+56);return base;
}
static DWORD apply_ui_gbk_patches(void){
    DWORD imgsz=0,i,patched=0,miss=0,old,dummy;BYTE*base=main_image_base(&imgsz);
    if(!base)return 0;
    for(i=0;i<UI_PATCH_COUNT;i++){const UI_PATCH_ENTRY*e=&g_ui_patches[i];BYTE*p;if(e->rva+e->src_len+1>imgsz){miss++;continue;}p=base+e->rva;
        if(p[e->src_len]!=0||fnv_bytes(p,e->src_len)!=e->src_hash){miss++;continue;}if(!pVirtualProtect(p,e->src_len+1,PAGE_EXECUTE_READWRITE,&old)){miss++;continue;}
        mem_zero(p,e->src_len+1);mem_copy(p,e->dst,e->dst_len);pVirtualProtect(p,e->src_len+1,old,&dummy);register_gbk_range(p,e->dst_len+1);patched++;}
    if(pFlushInstructionCache&&pGetCurrentProcess)pFlushInstructionCache(pGetCurrentProcess(),base,imgsz);
    log_kv("已安装 UI GBK 补丁数：",patched);log_kv("UI 源字节不匹配数：",miss);return patched;
}
static DWORD apply_legacy_static_gbk_patches(void){
    DWORD imgsz=0,i,patched=0,miss=0,padfail=0,old,dummy;BYTE*base=main_image_base(&imgsz);
    if(!base)return 0;
    for(i=0;i<LEGACY_STATIC_PATCH_COUNT;i++){const LEGACY_STATIC_PATCH_ENTRY*e=&g_legacy_static_patches[i];BYTE*p;DWORD j;
        if(e->rva+e->write_len>imgsz){miss++;continue;}p=base+e->rva;
        if(p[e->src_len]!=0||fnv_bytes(p,e->src_len)!=e->src_hash){miss++;continue;}
        if(e->dst_len>e->src_len){for(j=e->src_len;j<e->write_len;j++)if(p[j]!=0){padfail++;break;}if(j<e->write_len)continue;}
        if(!pVirtualProtect(p,e->write_len,PAGE_EXECUTE_READWRITE,&old)){miss++;continue;}
        mem_zero(p,e->write_len);mem_copy(p,e->dst,e->dst_len);pVirtualProtect(p,e->write_len,old,&dummy);register_gbk_range(p,e->dst_len+1);patched++;
    }
    if(pFlushInstructionCache&&pGetCurrentProcess)pFlushInstructionCache(pGetCurrentProcess(),base,imgsz);
    log_kv("WideSweep 实际文本 GBK 补丁数：",patched);log_kv("WideSweep 源字节不匹配数：",miss);log_kv("WideSweep 填充区拒绝数：",padfail);log_kv("WideSweep 排除的结构候选数：",209);return patched;
}


static DWORD apply_late_static_gbk_patches(void){
    DWORD imgsz=0,i,patched=0,miss=0,padfail=0,old,dummy;BYTE*base=main_image_base(&imgsz);
    if(!base)return 0;
    for(i=0;i<LATE_STATIC_PATCH_COUNT;i++){const LATE_STATIC_PATCH_ENTRY*e=&g_late_static_patches[i];BYTE*p;DWORD j;
        if(e->rva+e->write_len>imgsz){miss++;continue;}p=base+e->rva;
        if(p[e->src_len]!=0||fnv_bytes(p,e->src_len)!=e->src_hash){miss++;continue;}
        if(e->dst_len>e->src_len){for(j=e->src_len;j<e->write_len;j++)if(p[j]!=0){padfail++;break;}if(j<e->write_len)continue;}
        if(!pVirtualProtect(p,e->write_len,PAGE_EXECUTE_READWRITE,&old)){miss++;continue;}
        mem_zero(p,e->write_len);mem_copy(p,e->dst,e->dst_len);pVirtualProtect(p,e->write_len,old,&dummy);register_gbk_range(p,e->dst_len+1);patched++;
    }
    if(pFlushInstructionCache&&pGetCurrentProcess)pFlushInstructionCache(pGetCurrentProcess(),base,imgsz);
    log_kv("后续确认静态 GBK 补丁数：",patched);
    log_kv("后续静态源字节不匹配数：",miss);
    log_kv("后续静态填充区拒绝数：",padfail);
    return patched;
}

static DWORD apply_encoding_normalization_patches(void){
    DWORD imgsz=0,i,patched=0,miss=0,old,dummy;BYTE*base=main_image_base(&imgsz);
    if(!base)return 0;
    for(i=0;i<ENC_NORM_PATCH_COUNT;i++){const ENC_NORM_PATCH_ENTRY*e=&g_enc_norm_patches[i];BYTE*p;
        if(e->rva+e->len+1>imgsz){miss++;continue;}p=base+e->rva;
        if(p[e->len]!=0||fnv_bytes(p,e->len)!=e->src_hash){miss++;continue;}
        if(!pVirtualProtect(p,e->len+1,PAGE_EXECUTE_READWRITE,&old)){miss++;continue;}
        mem_copy(p,e->dst,e->len);pVirtualProtect(p,e->len+1,old,&dummy);register_gbk_range(p,e->len+1);patched++;
    }
    if(pFlushInstructionCache&&pGetCurrentProcess)pFlushInstructionCache(pGetCurrentProcess(),base,imgsz);
    log_kv("仅编码 CP932->GBK 规范化补丁数：",patched);
    log_kv("仅编码源字节不匹配数：",miss);
    return patched;
}

static BYTE* find_unique_pattern(const BYTE* pat,DWORD n,DWORD* matches){
    DWORD imgsz=0,peoff,nsec,optsz,shoff,si,c=0;BYTE*base=main_image_base(&imgsz);BYTE*found=NULL;
    if(matches)*matches=0;if(!base||n==0)return NULL;
    peoff=u32(base+0x3C);nsec=u16(base+peoff+6);optsz=u16(base+peoff+20);shoff=peoff+24+optsz;
    for(si=0;si<nsec;si++){
        BYTE*sh=base+shoff+si*40;DWORD vsize=u32(sh+8),rva=u32(sh+12),rawsz=u32(sh+16),chars=u32(sh+36),span,i;
        if(!(chars&0x20000000UL))continue;span=vsize>rawsz?vsize:rawsz;if(rva>=imgsz)continue;if(span>imgsz-rva)span=imgsz-rva;if(span<n)continue;
        for(i=0;i<=span-n;i++){if(mem_equal(base+rva+i,pat,n)){found=base+rva+i;c++;if(c>1){if(matches)*matches=c;return NULL;}}}
    }
    if(matches)*matches=c;return c==1?found:NULL;
}
static void emit8(BYTE**p,BYTE v){*(*p)++=v;}
static void emit32(BYTE**p,DWORD v){*(DWORD*)(*p)=v;*p+=4;}
static void patch_rel32(BYTE*at,const BYTE*to){*(DWORD*)at=(DWORD)(to-(at+4));}
static int patch_jmp(BYTE*t,DWORD n,BYTE*stub){DWORD old,dummy,i,disp;if(n<5||!pVirtualProtect(t,n,PAGE_EXECUTE_READWRITE,&old))return 0;disp=(DWORD)(stub-(t+5));t[0]=0xE9;*(DWORD*)(t+1)=disp;for(i=5;i<n;i++)t[i]=0x90;pVirtualProtect(t,n,old,&dummy);if(pFlushInstructionCache&&pGetCurrentProcess)pFlushInstructionCache(pGetCurrentProcess(),t,n);return 1;}
static BYTE* make_parse_stub(BYTE*t){
    BYTE*s=(BYTE*)pVirtualAlloc(NULL,224,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE),*p=s,*jcustom,*j_single1,*j_single2,*custom;
    if(!s)return NULL;
    /* Preserve EAX/ECX around the classifier. */
    emit8(&p,0x51);emit8(&p,0x50);emit8(&p,0x50);
    emit8(&p,0xE8);emit32(&p,(DWORD)((BYTE*)(PVOID)RendererClassifyGBK-(p+4)));
    emit8(&p,0x83);emit8(&p,0xC4);emit8(&p,0x04);emit8(&p,0x85);emit8(&p,0xC0);
    emit8(&p,0x0F);emit8(&p,0x85);jcustom=p;emit32(&p,0);

    /* Old localization's non-DBCS path: bytes below 0x80 and 0xFF are single.
       Jump directly to the clean renderer's single-byte block (t+0x76), rather
       than re-entering the original CP932 E0..FF lead-byte test. */
    emit8(&p,0x58);emit8(&p,0x59);
    emit8(&p,0x80);emit8(&p,0xF9);emit8(&p,0x80);
    emit8(&p,0x0F);emit8(&p,0x82);j_single1=p;emit32(&p,0);
    emit8(&p,0x80);emit8(&p,0xF9);emit8(&p,0xFE);
    emit8(&p,0x0F);emit8(&p,0x87);j_single2=p;emit32(&p,0);
    /* Defensive fallback: classifier and range test should agree. */
    emit8(&p,0xE9);emit32(&p,(DWORD)((t+0x76)-(p+4)));
    patch_rel32(j_single1,t+0x76);patch_rel32(j_single2,t+0x76);

    custom=p;patch_rel32(jcustom,custom);
    emit8(&p,0x58);emit8(&p,0x59);
    /* Mirror old localized 0x4895D0 path: CH=lead, CL=trail, preserve the
       renderer's scratch/base pointers, consume exactly two bytes. */
    emit8(&p,0x8A);emit8(&p,0xE9);                                  /* mov ch,cl */
    emit8(&p,0x8B);emit8(&p,0xBE);emit32(&p,0x00010404UL);           /* mov edi,[esi+10404] */
    emit8(&p,0x8A);emit8(&p,0x48);emit8(&p,0x01);                   /* mov cl,[eax+1] */
    emit8(&p,0x89);emit8(&p,0x4C);emit8(&p,0x24);emit8(&p,0xFC);    /* [esp-4]=ecx */
    emit8(&p,0x8B);emit8(&p,0x96);emit32(&p,0x0001040CUL);           /* mov edx,[esi+1040c] */
    emit8(&p,0x89);emit8(&p,0x54);emit8(&p,0x24);emit8(&p,0xF8);    /* [esp-8]=edx */
    emit8(&p,0x8B);emit8(&p,0x96);emit32(&p,0x00010408UL);           /* mov edx,[esi+10408] */
    emit8(&p,0x8B);emit8(&p,0x8E);emit32(&p,0x00000904UL);           /* mov ecx,[esi+904] */
    emit8(&p,0x8D);emit8(&p,0x2C);emit8(&p,0x12);                   /* lea ebp,[edx+edx] */
    emit8(&p,0x03);emit8(&p,0xCF);                                  /* add ecx,edi */
    emit8(&p,0x83);emit8(&p,0xC0);emit8(&p,0x02);                   /* add eax,2 */
    emit8(&p,0xE9);emit32(&p,(DWORD)((t+0x93)-(p+4)));              /* common block */
    return s;
}
static BYTE* make_width_stub(BYTE*t){
    BYTE*s=(BYTE*)pVirtualAlloc(NULL,96,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE),*p=s,*jc,*custom;if(!s)return NULL;
    emit8(&p,0x80);emit8(&p,0x3D);emit32(&p,(DWORD)(ULONG_PTR)&g_renderer_custom_pair);emit8(&p,0x00);emit8(&p,0x0F);emit8(&p,0x85);jc=p;emit32(&p,0);
    emit8(&p,0x8A);emit8(&p,0x44);emit8(&p,0x24);emit8(&p,0x14);emit8(&p,0x3C);emit8(&p,0x80);emit8(&p,0xE9);emit32(&p,(DWORD)((t+6)-(p+4)));
    custom=p;patch_rel32(jc,custom);emit8(&p,0x8B);emit8(&p,0x86);emit32(&p,0x00010408UL);emit8(&p,0x8D);emit8(&p,0x44);emit8(&p,0x00);emit8(&p,0x03);emit8(&p,0xE9);emit32(&p,(DWORD)((t+0x35)-(p+4)));return s;
}
static BYTE* make_late_stub(BYTE*t){
    BYTE*s=(BYTE*)pVirtualAlloc(NULL,192,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE),*p=s,*jf,*jfail,*j1,*j2,*no,*fallback;if(!s)return NULL;
    emit8(&p,0x80);emit8(&p,0x3D);emit32(&p,(DWORD)(ULONG_PTR)&g_renderer_custom_pair);emit8(&p,0x00);emit8(&p,0x0F);emit8(&p,0x84);jf=p;emit32(&p,0);
    emit8(&p,0x0F);emit8(&p,0xB7);emit8(&p,0x44);emit8(&p,0x24);emit8(&p,0xFC);emit8(&p,0x8B);emit8(&p,0x54);emit8(&p,0x24);emit8(&p,0xF8);
    emit8(&p,0x55);emit8(&p,0x52);emit8(&p,0x50);emit8(&p,0xE8);emit32(&p,(DWORD)((BYTE*)(PVOID)RendererGetGBKGlyph-(p+4)));emit8(&p,0x83);emit8(&p,0xC4);emit8(&p,0x0C);emit8(&p,0x85);emit8(&p,0xC0);emit8(&p,0x0F);emit8(&p,0x84);jfail=p;emit32(&p,0);emit8(&p,0x89);emit8(&p,0xC2);
    emit8(&p,0x8B);emit8(&p,0x4C);emit8(&p,0x24);emit8(&p,0x18);emit8(&p,0x3B);emit8(&p,0x4C);emit8(&p,0x24);emit8(&p,0x10);emit8(&p,0x0F);emit8(&p,0x84);j1=p;emit32(&p,0);
    emit8(&p,0x8B);emit8(&p,0x44);emit8(&p,0x24);emit8(&p,0xF8);emit8(&p,0x2B);emit8(&p,0xC1);emit8(&p,0x3B);emit8(&p,0x44);emit8(&p,0x24);emit8(&p,0x10);emit8(&p,0x0F);emit8(&p,0x85);j2=p;emit32(&p,0);
    emit8(&p,0x8D);emit8(&p,0x45);emit8(&p,0x03);emit8(&p,0xC1);emit8(&p,0xE8);emit8(&p,0x02);emit8(&p,0x0F);emit8(&p,0xAF);emit8(&p,0xC1);emit8(&p,0x03);emit8(&p,0xD0);
    no=p;patch_rel32(j1,no);patch_rel32(j2,no);emit8(&p,0x89);emit8(&p,0xD1);
    fallback=p;patch_rel32(jf,fallback);patch_rel32(jfail,fallback);emit8(&p,0x8B);emit8(&p,0x44);emit8(&p,0x24);emit8(&p,0x44);emit8(&p,0x83);emit8(&p,0xF8);emit8(&p,0x08);emit8(&p,0xE9);emit32(&p,(DWORD)((t+7)-(p+4)));return s;
}
static int patch_call6(BYTE*t,BYTE*stub){DWORD old,dummy,disp;if(!pVirtualProtect(t,6,PAGE_EXECUTE_READWRITE,&old))return 0;disp=(DWORD)(stub-(t+5));t[0]=0xE8;*(DWORD*)(t+1)=disp;t[5]=0x90;pVirtualProtect(t,6,old,&dummy);if(pFlushInstructionCache&&pGetCurrentProcess)pFlushInstructionCache(pGetCurrentProcess(),t,6);return 1;}
/*
 * Replace a small, already-validated instruction sequence with another sequence of exactly the
 * same length.  This helper is deliberately boring: no scanning, no guessing, no trampolines.
 * The caller supplies the bytes that MUST currently be present.  If even one byte differs, we
 * refuse to write.  This is important because these five edits sit inside old hand-written text
 * loops; writing them to another executable version would be unsafe.
 */
static int patch_exact_bytes(BYTE*t,const BYTE*expected,const BYTE*replacement,DWORD n){
    DWORD old,dummy;
    if(!t||!expected||!replacement||!n)return 0;
    if(!mem_equal(t,expected,n))return 0;
    if(!pVirtualProtect(t,n,PAGE_EXECUTE_READWRITE,&old))return 0;
    mem_copy(t,replacement,n);
    pVirtualProtect(t,n,old,&dummy);
    if(pFlushInstructionCache&&pGetCurrentProcess)pFlushInstructionCache(pGetCurrentProcess(),t,n);
    return 1;
}

/*
 * Reproduce the OLD 46MB localization's low-level DBCS routing/state edits on the fixed clean
 * 2003 executable.  These are not speculative patches: the old runtime image and the clean
 * disassembly can be compared instruction-for-instruction.
 *
 * Why the three A0 -> FF changes matter:
 *
 *     mov al,[text]
 *     test al,80h        ; ASCII (< 0x80) still stays single-byte
 *     je   single
 *     cmp  al,A0h        ; clean Shift-JIS logic
 *
 * In the old localization only the immediate A0 is changed to FF.  Because the high bit was
 * already proven set, every 0x80..0xFE byte now falls through to the two-byte branch.  A GBK
 * sequence such as CF E0 C2 ED CD B8 is therefore grouped as CFE0 / C2ED / CDB8 instead of the
 * clean engine's Shift-JIS grouping CF / E0C2 / ED20 / CD / B8 observed in test10.
 *
 * Why the two lookup-body changes matter:
 *
 * The old localization stores the packed current WORD/BYTE into its 0x7DDFF0 state.  We perform
 * the same store into g_lowlevel_current_code.  The later mask hooks at 0x415840/0x415CF1 then
 * feed this exact code to RendererGetGBKMask().  We patch the body where the old executable did,
 * rather than grabbing the function argument at entry, so the control-flow semantics now match
 * the oracle exactly.
 *
 * Safety rule: ALL FIVE clean byte sequences are checked first.  If any one does not match, no
 * patch is written at all.  This prevents a half-installed routing/state combination.
 */
static int install_lowlevel_oldlocalization_patches(void){
    static const BYTE route_expected[]={0x3C,0xA0};                 /* cmp al,0A0h */
    static const BYTE route_replacement[]={0x3C,0xFF};              /* cmp al,0FFh */
    static const BYTE dbcs_expected[]={
        0x66,0x3D,0xFD,0x81,0x73,0x14,0x25,0xFF,
        0xFF,0x00,0x00,0x2D,0x40,0x81,0x00,0x00
    };
    static const BYTE single_expected[]={0x3C,0x7F,0x77,0x12,0x25,0xFF,0x00,0x00,0x00};
    BYTE dbcs_replacement[16]={0x25,0xFF,0xFF,0x00,0x00,0xA3,0,0,0,0,0x90,0xB8,0,0,0,0};
    BYTE single_replacement[9]={0x0F,0xB6,0xC0,0xA3,0,0,0,0,0x90};
    DWORD imgsz=0,state=(DWORD)(ULONG_PTR)&g_lowlevel_current_code;
    BYTE*base=main_image_base(&imgsz),*r1,*r2,*r3,*db,*sb;

    if(!base||imgsz<=0x000170C1UL){
        log_s("[警告] 主模块映像过小或不可用，无法安装旧汉化低层补丁。\r\n");
        return 0;
    }

    /* Convert the five fixed clean-2003 RVAs to actual loaded addresses.  ASLR is not assumed
       away here: base is the module's real runtime ImageBase, and the constants are RVAs. */
    r1=base+0x000154ADUL;
    r2=base+0x00015B2DUL;
    r3=base+0x000170BFUL;
    db=base+0x000155E2UL;
    sb=base+0x0001572DUL;

    /* Validate every target before touching any page.  The diagnostic dump is intentionally
       small but complete enough to identify another executable/version from BaldrForceCN.log. */
    if(!mem_equal(r1,route_expected,2)||!mem_equal(r2,route_expected,2)||
       !mem_equal(r3,route_expected,2)||!mem_equal(db,dbcs_expected,16)||
       !mem_equal(sb,single_expected,9)){
        log_s("[警告] 旧汉化低层补丁校验失败；未修改任何路由/状态字节。\r\n");
        log_s("[诊断] 路由 0x4154AD：");log_hex_bytes(r1,2);log_nl();
        log_s("[诊断] 路由 0x415B2D：");log_hex_bytes(r2,2);log_nl();
        log_s("[诊断] 路由 0x4170BF：");log_hex_bytes(r3,2);log_nl();
        log_s("[诊断] DBCS  0x4155E2：");log_hex_bytes(db,16);log_nl();
        log_s("[诊断] 单字节 0x41572D：");log_hex_bytes(sb,9);log_nl();
        return 0;
    }

    /* Insert this ASI's state address into the old-localization-equivalent A3 instructions.
       A3 <addr32> means: mov dword ptr [absolute_address],eax. */
    mem_copy(dbcs_replacement+6,&state,4);
    mem_copy(single_replacement+4,&state,4);

    /* Each call repeats its expected-byte check.  That makes the tiny write helper safe even if
       this function is later reused elsewhere.  All five prechecks above have already succeeded. */
    if(!patch_exact_bytes(r1,route_expected,route_replacement,2)||
       !patch_exact_bytes(r2,route_expected,route_replacement,2)||
       !patch_exact_bytes(r3,route_expected,route_replacement,2)||
       !patch_exact_bytes(db,dbcs_expected,dbcs_replacement,16)||
       !patch_exact_bytes(sb,single_expected,single_replacement,9)){
        log_s("[失败] 旧汉化低层补丁通过校验后写入失败。\r\n");
        return 0;
    }

    log_s("[成功] 旧汉化低层 DBCS 路由补丁 @ 0x004154AD / 0x00415B2D / 0x004170BF\r\n");
    log_s("[成功] 旧汉化字形查找状态补丁 @ 0x004155E2 / 0x0041572D\r\n");
    return 1;
}

static BYTE* make_draw_mask_stub(void){
    BYTE*s=(BYTE*)pVirtualAlloc(NULL,160,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE),*p=s,*jf,*done;
    if(!s)return NULL;

    /*
     * test7/test8 gated this path with g_renderer_custom_pair and used g_renderer_last_code.
     * That state belongs to the NORMAL renderer and is unrelated to these low-level blitters.
     * The old localization instead uses the character saved inside 0x4155B0 / 0x415710 after the caller has already grouped the raw bytes.
     *
     * Load that synchronized character.  A zero value is treated as \"capture unavailable\" and
     * falls back to the untouched bitmap pointer so a signature/capture failure cannot crash text.
     */
    emit8(&p,0xA1);emit32(&p,(DWORD)(ULONG_PTR)&g_lowlevel_current_code); /* mov eax,[code] */
    emit8(&p,0x85);emit8(&p,0xC0);                                     /* test eax,eax */
    emit8(&p,0x0F);emit8(&p,0x84);jf=p;emit32(&p,0);                    /* jz fallback */

    /* RendererGetGBKMask(current_code, [ebp-4], [ebp+98]). */
    emit8(&p,0xFF);emit8(&p,0xB5);emit32(&p,0x00000098UL);             /* push [ebp+98] row stride */
    emit8(&p,0xFF);emit8(&p,0x75);emit8(&p,0xFC);                      /* push [ebp-4] height */
    emit8(&p,0x50);                                                     /* push eax current code */
    emit8(&p,0xE8);emit32(&p,(DWORD)((BYTE*)(PVOID)RendererGetGBKMask-(p+4)));
    emit8(&p,0x83);emit8(&p,0xC4);emit8(&p,0x0C);                      /* caller removes 3 args */
    emit8(&p,0x85);emit8(&p,0xC0);                                     /* mask pointer != NULL? */
    emit8(&p,0x74);BYTE*jz8=p;emit8(&p,0x00);                          /* no -> fallback */
    emit8(&p,0x8B);emit8(&p,0xF0);                                     /* mov esi,eax mask */
    emit8(&p,0xC3);                                                     /* return to blitter */

    done=p;
    *jz8=(BYTE)(done-(jz8+1));
    patch_rel32(jf,done);

    /* Original instruction replaced by CALL+NOP: mov esi,[ebp+8C]. */
    emit8(&p,0x8B);emit8(&p,0xB5);emit32(&p,0x0000008CUL);
    emit8(&p,0xC3);
    return s;
}
static int install_gbk_draw_mask_hooks(void){
    static const BYTE asig[]={0x8B,0xB5,0x8C,0x00,0x00,0x00,0x66,0x8B,0x9D,0x9C,0x00,0x00,0x00,0x8B,0x4D,0xFC};
    static const BYTE bsig[]={0x8B,0xB5,0x8C,0x00,0x00,0x00,0x8B,0x4D,0xFC,0x8B,0x9D,0x9C,0x00,0x00,0x00};
    DWORD ma=0,mb=0;BYTE*a=find_unique_pattern(asig,sizeof(asig),&ma),*b=find_unique_pattern(bsig,sizeof(bsig),&mb),*sa,*sb;
    if(!a||!b){log_s("[警告] 低层字形掩码签名 A/B 匹配数=");log_dec(ma);log_s("/");log_dec(mb);log_nl();return 0;}
    sa=make_draw_mask_stub();sb=make_draw_mask_stub();if(!sa||!sb)return 0;if(!patch_call6(a,sa)||!patch_call6(b,sb))return 0;
    g_renderer_draw_site_a=a;g_renderer_draw_site_b=b;log_s("[成功] 旧汉化低层字形掩码 Hook @ ");log_hex32((DWORD)(ULONG_PTR)a);log_s(" / ");log_hex32((DWORD)(ULONG_PTR)b);log_nl();return 1;
}

static int install_gbk_renderer_hook(void){
    static const BYTE psig[]={0x80,0xF9,0x80,0x72,0x36,0x80,0xF9,0x9F,0x77,0x31,0x8B,0x4C,0x24,0x14};
    static const BYTE wsig[]={0x8A,0x44,0x24,0x14,0x3C,0x80,0x72,0x10,0x3C,0x9F,0x77,0x0C};
    static const BYTE lsig[]={0x8B,0x44,0x24,0x44,0x83,0xF8,0x08,0x0F,0x87};
    DWORD m1=0,m2=0,m3=0;BYTE*p1=find_unique_pattern(psig,sizeof(psig),&m1),*p2=find_unique_pattern(wsig,sizeof(wsig),&m2),*p3=find_unique_pattern(lsig,sizeof(lsig),&m3);BYTE*s1,*s2,*s3;
    if(!p1||!p2||!p3){log_s("[警告] 渲染器签名 解析/宽度/后段 匹配数=");log_dec(m1);log_s("/");log_dec(m2);log_s("/");log_dec(m3);log_nl();return 0;}
    s1=make_parse_stub(p1);s2=make_width_stub(p2);s3=make_late_stub(p3);if(!s1||!s2||!s3)return 0;
    if(!patch_jmp(p1,5,s1)||!patch_jmp(p2,6,s2)||!patch_jmp(p3,7,s3))return 0;
    g_renderer_parse_site=p1;g_renderer_width_site=p2;g_renderer_late_site=p3;log_s("[成功] 旧汉化全局 DBCS/GDI 渲染器 Hook @ ");log_hex32((DWORD)(ULONG_PTR)p1);log_s(" / ");log_hex32((DWORD)(ULONG_PTR)p2);log_s(" / ");log_hex32((DWORD)(ULONG_PTR)p3);log_nl();return 1;
}

static int install_script_string_hook(void){
    static const BYTE sig[]={0x8B,0x41,0x08,0x56,0x8B,0x90,0x14,0x01,0x00,0x00,0x8B,0x70,0x0C,0x3B,0xD6,0x72,0x06,0x33,0xC0,0x5E,0xC2,0x08,0x00};
    DWORD matches=0,old,dummy,disp;BYTE*t=find_unique_pattern(sig,(DWORD)sizeof(sig),&matches);
    if(!t){log_s("[警告] 脚本复制函数签名匹配数：");log_dec(matches);log_nl();return 0;}
    if(!pVirtualProtect(t,5,PAGE_EXECUTE_READWRITE,&old)){log_s("[失败] 脚本复制 Hook 的 VirtualProtect 调用失败\r\n");return 0;}
    disp=(DWORD)((BYTE*)(PVOID)Hook_ScriptCopyBytes-(t+5));t[0]=0xE9;*(DWORD*)(t+1)=disp;
    pVirtualProtect(t,5,old,&dummy);if(pFlushInstructionCache&&pGetCurrentProcess)pFlushInstructionCache(pGetCurrentProcess(),t,5);
    g_script_copy_target=t;log_s("[成功] 脚本操作码 0x0B 复制 Hook @ ");log_hex32((DWORD)(ULONG_PTR)t);log_nl();return 1;
}

/* ---------- exact old-content feed hooks ---------- */
static EMBEDDED_LOOSE_ASSET* lookup_embedded_asset(LPCSTR path){
    /* Search the tiny 9-row manifest linearly.  File opens are infrequent, so a hash table would add complexity only. */
    DWORD i;if(!path)return NULL;
    for(i=0;i<EMBEDDED_ASSET_COUNT;i++)if(path_suffix_eq(path,g_embedded_assets[i].path))return &g_embedded_assets[i];
    return NULL;
}
static HANDLE open_embedded_asset_as_temp_file(EMBEDDED_LOOSE_ASSET*e){
    /*
     * Return a REAL Windows file handle instead of a fake pointer-shaped HANDLE.
     *
     * Why this design is deliberately simple:
     *   - BaldrForce is an old engine and may call GetFileSize, ReadFile, SetFilePointer, GetFileType, etc.
     *   - A fake memory handle would require us to hook every one of those APIs perfectly.
     *   - A hidden FILE_FLAG_DELETE_ON_CLOSE file lets Windows implement all ordinary file semantics for us.
     *
     * The bytes still live permanently inside BaldrForceCN.asi.  The temporary file is created only when the game
     * actually asks for an embedded path, is hidden/temporary, and is automatically deleted when its handle closes
     * (also when Windows tears down the process after a crash).  Nothing is left as a user-managed runtime asset.
     */
    WCHAR tmp[MAX_PATH_W];HANDLE h=INVALID_HANDLE_VALUE;DWORD attempt,serial,size,written=0,pos;
    if(!e||!pCreateFileW||!pWriteFile||!pSetFilePointer||!pCloseHandle)return INVALID_HANDLE_VALUE;
    size=(DWORD)(e->end-e->begin);
    for(attempt=0;attempt<64;attempt++){
        serial=++g_embedded_temp_serial;
        w_copy(tmp,g_asi_dir,MAX_PATH_W);
        w_append_ascii(tmp,"BaldrForceCN.$embedded.",MAX_PATH_W);
        w_append_hex8(tmp,serial,MAX_PATH_W);
        w_append_ascii(tmp,".tmp",MAX_PATH_W);
        h=pCreateFileW(tmp,GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
                       NULL,CREATE_NEW,FILE_ATTRIBUTE_HIDDEN|FILE_ATTRIBUTE_TEMPORARY|FILE_FLAG_DELETE_ON_CLOSE,NULL);
        if(h!=INVALID_HANDLE_VALUE)break;
    }
    if(h==INVALID_HANDLE_VALUE)return INVALID_HANDLE_VALUE;
    if(!pWriteFile(h,e->begin,size,&written,NULL)||written!=size){pCloseHandle(h);return INVALID_HANDLE_VALUE;}
    pos=pSetFilePointer(h,0,NULL,FILE_BEGIN);
    if(pos==INVALID_SET_FILE_POINTER){pCloseHandle(h);return INVALID_HANDLE_VALUE;}
    return h;
}
static HANDLE STDCALL Hook_CreateFileA(LPCSTR path,DWORD access,DWORD share,PVOID sa,DWORD disp,DWORD flags,HANDLE templ){
    EMBEDDED_LOOSE_ASSET*e=lookup_embedded_asset(path);
    if(e){
        HANDLE h=open_embedded_asset_as_temp_file(e);
        if(h!=INVALID_HANDLE_VALUE){
            e->hits++;
            if(e->hits==1){
                log_s("[命中] 内嵌汉化资源：");log_s(e->path);log_s("，字节数=");
                log_dec((DWORD)(e->end-e->begin));log_nl();
            }
            return h;
        }
        g_embedded_asset_open_failures++;
        if(g_embedded_asset_open_failures<=8){
            log_s("[警告] 内嵌汉化资源无法创建关闭时自动删除的临时后备句柄，回退原版：");
            log_s(e->path);log_nl();
        }
    }
    /* Every path not explicitly present in the 9-row v1.0.0 differential-resource manifest is untouched. */
    return g_sys_createfilea?g_sys_createfilea(path,access,share,sa,disp,flags,templ):INVALID_HANDLE_VALUE;
}

static int FASTCALL Hook_EngineOpenResource(PVOID self,PVOID unused_edx,PVOID manager,LPCSTR name,DWORD arg3){
    RESOURCE_OVERLAY_ENTRY*e=lookup_resource_overlay(name);
    BYTE*o=(BYTE*)self;
    (void)unused_edx;

    if(e&&o){
        /*
         * This is deliberately a byte-for-byte model of the clean game's memory-stream
         * constructor at 0x004A9C50, not a new custom file format.  The steps are:
         *   1. Reset the existing stream object exactly like the original opener does.
         *   2. Tell the object that the stream length is e->size bytes.
         *   3. Point begin/current/end at the exact DAT bytes inside localized Update.pac.
         *   4. Set mode=2 (memory stream) and owned=0, so the game must NOT free g_pac memory.
         *
         * The localized DAT bytes therefore enter the normal game parser unchanged.
         * Nothing below interprets or rewrites the DAT record structure.
         */
        if(g_engine_stream_reset)g_engine_stream_reset(self);
        *(DWORD*)(o+0x18)=e->size;
        *(DWORD*)(o+0x1C)=0;
        *(const BYTE**)(o+0x38)=e->data;
        *(const BYTE**)(o+0x3C)=e->data;
        *(const BYTE**)(o+0x40)=e->data+e->size;
        *(DWORD*)(o+0x44)=2;
        *(DWORD*)(o+0x48)=0;

        e->hits++;g_resource_overlay_hits++;
        if(e->hits==1){
            log_s("[命中] 汉化 DAT 覆盖：");
            log_s(e->name);
            log_s("，字节数=");
            log_dec(e->size);
            log_nl();
        }
        return 1;
    }

    /* Unknown resources are handled by the untouched original 2003 function. */
    return g_orig_engine_open_resource?g_orig_engine_open_resource(self,manager,name,arg3):0;
}

/*
 * Install the internal DAT resource-overlay hook.
 *
 * Why an inline trampoline is required:
 * - The DAT loader is a C++ __thiscall method inside BaldrForce.exe, not a Win32 import.
 * - We still need the original method for every resource that is not one of our verified DAT files.
 * - Therefore we copy the six overwritten prologue bytes to a tiny executable trampoline, append
 *   a jump back to original+6, then redirect the original entry to Hook_EngineOpenResource.
 *
 * The signature below is the verified 2003 entry at 0x004A9D40.  If a different EXE does not
 * contain it exactly once, installation fails safely and the game keeps its normal resource path.
 */
static int install_resource_overlay_hook(void){
    static const BYTE sig[]={
        0x53,0x55,0x56,0x57,0x8B,0xF1,0xE8,0xF5,0x00,0x00,0x00,
        0x8B,0x5C,0x24,0x18,0x8B,0x6C,0x24,0x14
    };
    DWORD matches=0;
    BYTE*t=find_unique_pattern(sig,(DWORD)sizeof(sig),&matches);
    BYTE*tr;
    DWORD back_disp;
    DWORD imgsz=0;
    BYTE*base=main_image_base(&imgsz);

    if(!t||!base){
        log_s("[警告] 引擎 DAT 打开函数签名匹配数：");
        log_dec(matches);
        log_nl();
        return 0;
    }

    /* 0x004A9E40 is the reset routine called by the original opener before it fills the stream. */
    if(imgsz<=0x000A9E40UL){
        log_s("[警告] 主模块映像过小，无法定位引擎流重置辅助函数\r\n");
        return 0;
    }
    g_engine_stream_reset=(PFN_EngineStreamReset)(base+0x000A9E40UL);

    /* Six bytes are enough to cover whole x86 instructions: push/push/push/push/mov esi,ecx. */
    tr=(BYTE*)pVirtualAlloc(NULL,32,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE);
    if(!tr){
        log_s("[失败] 无法分配引擎 DAT 打开函数跳板\r\n");
        return 0;
    }
    mem_copy(tr,t,6);
    tr[6]=0xE9;
    back_disp=(DWORD)((t+6)-(tr+11));
    *(DWORD*)(tr+7)=back_disp;
    g_orig_engine_open_resource=(PFN_EngineOpenResource)tr;

    if(!patch_jmp(t,6,(BYTE*)(PVOID)Hook_EngineOpenResource)){
        g_orig_engine_open_resource=NULL;
        log_s("[失败] 无法补丁引擎 DAT 打开函数\r\n");
        return 0;
    }

    g_engine_open_resource_site=t;
    log_s("[成功] 汉化 DAT 资源覆盖 Hook @ ");
    log_hex32((DWORD)(ULONG_PTR)t);
    log_nl();
    return 1;
}

/* ---------- hooks ---------- */
static LPSTR STDCALL Hook_lstrcpyA(LPSTR dst,LPCSTR src){
    int known=string_has_known_gbk((const BYTE*)src);LPSTR r=g_orig_lstrcpyA?g_orig_lstrcpyA(dst,src):dst;
    if(known&&dst){DWORD n=c_len_bounded((const BYTE*)dst,4095);register_gbk_range((const BYTE*)dst,n+1);g_gbk_copy_propagations++;if(g_gbk_copy_propagations==1)log_s("[命中] lstrcpyA 已传播 GBK 来源标记\r\n");}return r;
}
static LPSTR STDCALL Hook_lstrcatA(LPSTR dst,LPCSTR src){
    int known=string_has_known_gbk((const BYTE*)dst)||string_has_known_gbk((const BYTE*)src);LPSTR r=g_orig_lstrcatA?g_orig_lstrcatA(dst,src):dst;
    if(known&&dst){DWORD n=c_len_bounded((const BYTE*)dst,4095);register_gbk_range((const BYTE*)dst,n+1);g_gbk_copy_propagations++;if(g_gbk_copy_propagations==1)log_s("[命中] lstrcatA 已传播 GBK 来源标记\r\n");}return r;
}
static int STDCALL Hook_wvsprintfA(LPSTR dst,LPCSTR fmt,PVOID args){
    int known=string_has_known_gbk((const BYTE*)fmt),r=g_orig_wvsprintfA?g_orig_wvsprintfA(dst,fmt,args):0;
    if(known&&dst&&r>=0){DWORD n=c_len_bounded((const BYTE*)dst,4095);register_gbk_range((const BYTE*)dst,n+1);g_gbk_format_propagations++;if(g_gbk_format_propagations==1)log_s("[命中] wvsprintfA 已传播 GBK 来源标记\r\n");}return r;
}

static int CDECL Hook_wsprintfA(LPSTR dst,LPCSTR fmt,...){
    PVOID args=(PVOID)((BYTE*)&fmt+4);int known=string_has_known_gbk((const BYTE*)fmt),r=g_orig_wvsprintfA?g_orig_wvsprintfA(dst,fmt,args):0;
    if(known&&dst&&r>=0){DWORD n=c_len_bounded((const BYTE*)dst,4095);register_gbk_range((const BYTE*)dst,n+1);g_gbk_format_propagations++;if(g_gbk_format_propagations==1)log_s("[命中] wsprintfA/wvsprintfA 已传播 GBK 来源标记\r\n");}return r;
}

static int STDCALL Hook_MultiByteToWideChar(UINT cp,DWORD flags,LPCCH src,int cb,LPWSTR out,int cch){
    DWORD len;int has_null=0;const TRANS_ENTRY*t;const DIRECT_ENTRY*d;
    if(!src)return g_sys_mbtowc(cp,flags,src,cb,out,cch);
    if(cb<0){len=c_len_bounded((const BYTE*)src,65535);has_null=1;}else{len=(DWORD)cb;if(len&&src[len-1]==0){len--;has_null=1;}}
    if(len==0)return g_sys_mbtowc(cp,flags,src,cb,out,cch);
    t=lookup_ja((const BYTE*)src,len);
    if(t){DWORD need=t->zh_w_len+(has_null?1:0);g_hits_ja++;if(cch==0)return (int)need;if((DWORD)cch<need){if(pSetLastError)pSetLastError(ERROR_INSUFFICIENT_BUFFER);return 0;}mem_copy(out,t->zh_w,t->zh_w_len*2);if(has_null)out[t->zh_w_len]=0;return (int)need;}
    /* If already-GBK translated text is passed again through CP932/ACP, preserve it. */
    if(cp==CP_SHIFTJIS||cp==CP_ACP){t=lookup_zh_mb((const BYTE*)src,len);if(t){g_hits_gbk++;return g_sys_mbtowc(CP_GBK,flags,src,cb,out,cch);}d=lookup_direct_mb((const BYTE*)src,len);if(d){g_hits_gbk++;return g_sys_mbtowc(CP_GBK,flags,src,cb,out,cch);}}
    return g_sys_mbtowc(cp,flags,src,cb,out,cch);
}

static int STDCALL Hook_WideCharToMultiByte(UINT cp,DWORD flags,LPCWSTR src,int cch,LPSTR out,int cb,LPCCH defc,LPBOOL used){
    DWORD len;int has_null=0;if(!src||!g_enable_wctomb_hook)return g_sys_wctomb(cp,flags,src,cch,out,cb,defc,used);
    if(cch<0){len=w_len(src);has_null=1;}else{len=(DWORD)cch;if(len&&src[len-1]==0){len--;has_null=1;}}
    if((cp==CP_SHIFTJIS||cp==CP_ACP)&&len){if(lookup_zh_w(src,len)||lookup_direct_w(src,len)){return g_sys_wctomb(CP_GBK,flags,src,cch,out,cb,defc,used);}}
    (void)has_null;return g_sys_wctomb(cp,flags,src,cch,out,cb,defc,used);
}

static HFONT STDCALL Hook_CreateFontIndirectW(const LOGFONTW_MIN* in){LOGFONTW_MIN lf;DWORD i;if(!g_sys_font)return NULL;if(!in||!g_enable_font_hook)return g_sys_font(in);mem_copy(&lf,in,sizeof(lf));lf.lfCharSet=(BYTE)g_force_charset;if(g_force_face[0]){for(i=0;i<31&&g_force_face[i];i++)lf.lfFaceName[i]=g_force_face[i];lf.lfFaceName[i]=0;}return g_sys_font(&lf);}

static FARPROC STDCALL Hook_GetProcAddress(HMODULE mod,LPCSTR name){
    if((ULONG_PTR)name>0xFFFFUL){if(a_eq(name,"CreateFileA")&&g_sys_createfilea)return (FARPROC)Hook_CreateFileA;if(a_eq(name,"MultiByteToWideChar"))return (FARPROC)Hook_MultiByteToWideChar;if(a_eq(name,"WideCharToMultiByte")&&g_enable_wctomb_hook)return (FARPROC)Hook_WideCharToMultiByte;if(a_eq(name,"CreateFontIndirectW")&&g_enable_font_hook&&g_sys_font)return (FARPROC)Hook_CreateFontIndirectW;if(a_eq(name,"lstrcpyA")&&g_orig_lstrcpyA)return (FARPROC)Hook_lstrcpyA;if(a_eq(name,"lstrcatA")&&g_orig_lstrcatA)return (FARPROC)Hook_lstrcatA;if(a_eq(name,"wvsprintfA")&&g_orig_wvsprintfA)return (FARPROC)Hook_wvsprintfA;if(a_eq(name,"wsprintfA")&&g_orig_wvsprintfA)return (FARPROC)Hook_wsprintfA;}
    return g_orig_getproc?g_orig_getproc(mod,name):NULL;
}

/* ---------- IAT patcher ---------- */
static DWORD patch_main_iat(void){
    BYTE* peb=(BYTE*)get_peb();BYTE* base=*(BYTE**)(peb+8);DWORD peoff,imp_rva,imgsz,patched=0;BYTE*desc;
    if(!base||u16(base)!=0x5A4D)return 0;peoff=u32(base+0x3C);if(u32(base+peoff)!=0x4550||u16(base+peoff+24)!=0x10B)return 0;imgsz=u32(base+peoff+24+56);imp_rva=u32(base+peoff+24+96+8);if(!imp_rva||imp_rva>=imgsz)return 0;desc=base+imp_rva;
    while(u32(desc)||u32(desc+12)||u32(desc+16)){
        DWORD oft=u32(desc),ft=u32(desc+16),j=0;if(!ft||ft>=imgsz){desc+=20;continue;}if(!oft)oft=ft;
        while(1){DWORD nv=u32(base+oft+j*4);PVOID*slot=(PVOID*)(base+ft+j*4);const char*nm;PVOID repl=NULL;DWORD old;
            if(!nv)break;if(!(nv&0x80000000UL)&&nv<imgsz){nm=(const char*)(base+nv+2);
                if(a_eq(nm,"CreateFileA")&&g_sys_createfilea)repl=(PVOID)Hook_CreateFileA;
                else if(a_eq(nm,"MultiByteToWideChar"))repl=(PVOID)Hook_MultiByteToWideChar;
                else if(a_eq(nm,"WideCharToMultiByte")&&g_enable_wctomb_hook)repl=(PVOID)Hook_WideCharToMultiByte;
                else if(a_eq(nm,"GetProcAddress")){repl=(PVOID)Hook_GetProcAddress;if(!g_orig_getproc)g_orig_getproc=(PFN_GetProcAddress)(*slot);}
                else if(a_eq(nm,"CreateFontIndirectW")&&g_enable_font_hook&&g_sys_font)repl=(PVOID)Hook_CreateFontIndirectW;
                else if(a_eq(nm,"lstrcpyA")){repl=(PVOID)Hook_lstrcpyA;if(!g_orig_lstrcpyA)g_orig_lstrcpyA=(PFN_lstrcpyA)(*slot);}
                else if(a_eq(nm,"lstrcatA")){repl=(PVOID)Hook_lstrcatA;if(!g_orig_lstrcatA)g_orig_lstrcatA=(PFN_lstrcatA)(*slot);}
                else if(a_eq(nm,"wvsprintfA")){repl=(PVOID)Hook_wvsprintfA;if(!g_orig_wvsprintfA)g_orig_wvsprintfA=(PFN_wvsprintfA)(*slot);}
                else if(a_eq(nm,"wsprintfA")&&g_orig_wvsprintfA){repl=(PVOID)Hook_wsprintfA;}
                if(repl&&*slot!=repl){if(pVirtualProtect(slot,4,PAGE_EXECUTE_READWRITE,&old)){*slot=repl;{DWORD dummy;pVirtualProtect(slot,4,old,&dummy);}patched++;}}
            }j++;if(j>10000)break;
        }desc+=20;if((DWORD)(desc-base)>=imgsz)break;
    }
    if(pFlushInstructionCache&&pGetCurrentProcess)pFlushInstructionCache(pGetCurrentProcess(),base,imgsz);return patched;
}

/* ---------- initialization ---------- */
static DWORD STDCALL init_thread(PVOID unused){
    DWORD hooks; (void)unused;
    if(!resolve_system_apis())return 0;
    make_paths();

    /*
     * 配置必须在创建日志之前读取，因为 EnableLog=0 的语义就是“本次启动完全不创建/覆盖日志文件”。
     * INI 不存在时 load_runtime_config() 会保留默认值，因此仍然和 v1.0.3 一样开箱即用。
     */
    load_runtime_config();
    if(g_enable_log){
        g_log=pCreateFileW(g_log_path,GENERIC_WRITE,FILE_SHARE_READ,NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);
    }else{
        g_log=INVALID_HANDLE_VALUE;
    }

    /* 日志内容统一使用 UTF-8。这里先写 BOM，让 Windows 记事本等工具无需猜测编码即可直接显示中文。 */
    if(g_log!=INVALID_HANDLE_VALUE)log_raw("\xEF\xBB\xBF",3);
    log_s("BaldrForceCN 中文运行时 ASI v1.0.4-crashfix1\r\n");
    log_s("[信息] 架构：Win32/x86；1017 条已汉化静态文本 + 168 条编码规范化 + 汉化 DAT 覆盖 + 9 个内嵌差异资源文件 + 旧汉化 DBCS/GDI 渲染器 + Unicode 字形安全回退 + 旧汉化低层字节路由/状态同步 + BFET 双长度修复 + 增强诊断\r\n");
    log_s("[信息] BaldrForceCN.ini 仅提供 FontFace 与 EnableLog；其余关键运行时配置仍固定内置。\r\n");
    if(g_force_face[0])log_s("[信息] 字体：已启用 BaldrForceCN.ini 中的自定义 FontFace；普通字体 Hook 与 GBK 字形生成器都会使用它。\r\n");
    else log_s("[信息] 字体：未指定 FontFace，沿用已实机验证的默认宋体/黑体规则。\r\n");
    log_s("[信息] 日志：EnableLog=1；日志位于 BaldrForceCN.asi 同目录。\r\n");
    install_crash_diagnostics();
    log_kv("内嵌汉化差异资源数：",EMBEDDED_ASSET_COUNT);
    if(!load_file(g_update_path,&g_pac,&g_pac_size)){log_s("[失败] 无法打开 Update.pac\r\n");return 0;}
    log_kv("Update.pac 字节数：",g_pac_size);
    if(!build_database()){log_s("[失败] BFET 数据库构建失败\r\n");return 0;}
    if(!build_known_gbk_codes()){log_s("[失败] GBK 字符码映射构建失败\r\n");return 0;}
    apply_ui_gbk_patches();
    apply_legacy_static_gbk_patches();
    apply_late_static_gbk_patches();
    apply_encoding_normalization_patches();

    /*
     * Build the DAT overlay table only after Update.pac is loaded.  The entries point directly
     * into g_pac, whose allocation intentionally stays alive for the entire game process.
     */
    if(!init_resource_overlays())log_s("[警告] Update.pac 中未找到汉化 DAT 覆盖资源。\r\n");
    else if(!install_resource_overlay_hook())log_s("[警告] 已找到汉化 DAT 覆盖资源，但未能 Hook 2003 引擎资源打开函数。\r\n");

    if(!install_gbk_renderer_hook())log_s("[警告] 原始 GBK 渲染适配器不可用；汉化 GBK 文本仍可能显示乱码。\r\n");
    /* Install the old localization's three DBCS-routing edits and two lookup-state writes before
       the mask hooks.  test10 proved that capturing lookup arguments after clean Shift-JIS byte
       splitting is too late: the bytes must first be grouped with the old 0x80..0xFE rule. */
    if(!install_lowlevel_oldlocalization_patches())log_s("[警告] 旧汉化低层路由/状态同步不可用；战斗/阅览信息特殊文本仍可能按 Shift-JIS 错误拆分。\r\n");
    if(!install_gbk_draw_mask_hooks())log_s("[警告] 低层字形掩码 Hook 不可用；字形图层或位置仍可能异常。\r\n");
    if(!install_script_string_hook())log_s("[警告] 内部脚本 Hook 不可用；当前仅剩 IAT 兜底路径。\r\n");
    hooks=patch_main_iat();log_kv("已安装 IAT Hook 数：",hooks);
    if(hooks==0)log_s("[警告] 未找到兼容导入项；该 EXE 需要版本适配或内联 Hook。\r\n");
    else log_s("[成功] 运行时初始化完成\r\n");
    log_s("[信息] 已将确认与干净原版不同的 9 个旧汉化独立资源文件内嵌进 BaldrForceCN.asi；其它独立资源全部回退原版。\r\n");
    log_s("[信息] after.bin 保留在 Update.pac 中；首次实际执行其中的直接 GBK 文本时会输出一条 [命中] 日志。\r\n");
    /* Repatch once after startup in case a loader restored IAT entries. */
    if(pSleep){pSleep(1500);hooks=patch_main_iat();if(hooks){log_s("延迟 IAT 再补丁数：");log_dec(hooks);log_nl();}}
    return 0;
}

static void start_runtime(void){
    PVOID k;if(g_started)return;g_started=1;k=find_loaded_module("kernel32.dll");if(k)pCreateThread=(PFN_CreateThread)resolve_export(k,"CreateThread");
    if(pCreateThread){HANDLE th=pCreateThread(NULL,0,init_thread,NULL,0,NULL);if(th){PVOID c=resolve_export(k,"CloseHandle");if(c)((PFN_CloseHandle)c)(th);return;}}
    init_thread(NULL);
}

EXPORT void STDCALL InitializeASI(void){start_runtime();}
BOOL STDCALL DllMainCRTStartup(HINSTANCE h,DWORD reason,PVOID reserved){
    (void)reserved;
    if(reason==DLL_PROCESS_ATTACH){
        /* Save our own module handle BEFORE the worker thread builds paths.  This is what makes
           BaldrForceCN.log follow BaldrForceCN.asi instead of always following BaldrForce.exe. */
        g_self_module=h;
        start_runtime();
    }
    return TRUE;
}
