/*
    BaldrForceGamepad.c
    ================================================================
    BALDR FORCE 2003 PC 版专用现代手柄输入修复 ASI
    版本：v0.1-test7

    这个源文件故意写了非常多中文注释。
    目标不是让代码“看起来短”，而是让只学过一天 C 语言的人也能顺着看懂：
    1. 游戏原本怎样读输入；
    2. 我们在什么位置插入自己的逻辑；
    3. 为什么 D-Pad 是步行、左摇杆是跑步；
    4. 为什么菜单中两者又会变成完全相同；
    5. 右摇杆和 LT/RT 怎样变成游戏自己的鼠标输入。

    v0.1-test7 起，插件完全不再接管“方向右打开剧情快捷菜单/完整菜单”。
    test1~test6 对该功能的所有实验均判定失败并从运行时代码移除；
    Right 在所有场景都只保留游戏原版方向语义。

    ----------------------------------------------------------------
    重要：本插件只支持下面这一份 EXE 基线。

    文件：BaldrForce.2003.exe
    大小：987,136 bytes
    SHA-256：5b65ecb1512b0cbf72cacdb5e981aa04f9c569698c6cab2f8877abf2d2cd42c5
    架构：PE32 / x86
    ImageBase：0x00400000

    代码会在安装 Hook 前再次核对五个关键地址的机器码。
    只要机器码不同，就不会继续打补丁，避免误写其他版本 EXE。
    ================================================================
*/

/*
    我们不包含 windows.h，也不链接 C/C++ 运行库。

    原因：
    - 这是一个很小的 x86 ASI；
    - 游戏本身已经导入了我们需要的大部分 Win32 API；
    - 剩余 API 可以通过游戏现有的 GetProcAddress 动态取得；
    - 这样生成的 ASI 几乎没有额外运行时依赖。

    因此下面先定义几个最基本的 Windows 风格类型。
*/
typedef unsigned char       u8;
typedef unsigned short      u16;
typedef unsigned int        u32;
typedef signed short        s16;
typedef signed int          s32;
typedef unsigned long       DWORD;
typedef int                 BOOL;
typedef void*               HANDLE;
typedef void*               HMODULE;
typedef void*               LPVOID;
typedef const char*         LPCSTR;
typedef char*               LPSTR;
typedef unsigned long*      LPDWORD;
typedef unsigned long       SIZE_T;

typedef struct POINT_ {
    long x;
    long y;
} POINT_;

#ifndef NULL
#define NULL ((void*)0)
#endif

#define TRUE  1
#define FALSE 0

/* DLL_PROCESS_ATTACH 是 Windows 在 DLL/ASI 第一次被装入进程时给 DllMain 的 reason。 */
#define DLL_PROCESS_ATTACH 1

/* VirtualAlloc / VirtualProtect 要用到的内存标志。 */
#define MEM_COMMIT              0x00001000u
#define MEM_RESERVE             0x00002000u
#define PAGE_EXECUTE_READWRITE  0x00000040u

/* CreateFileA 要用到的参数。这里只用于写一个简单日志文件。 */
#define GENERIC_WRITE           0x40000000u
#define FILE_SHARE_READ         0x00000001u
#define CREATE_ALWAYS           2u
#define FILE_ATTRIBUTE_NORMAL   0x00000080u
#define FILE_END                2u
#define INVALID_HANDLE_VALUE    ((HANDLE)(s32)-1)

/*
    __stdcall / __cdecl / __fastcall / __thiscall 都是 x86 调用约定。

    - WINAPI API 通常是 __stdcall；
    - 游戏 0x411920 是普通 __cdecl；
    - 本版不再 Hook 剧情菜单对象，因此不再需要 __thiscall / __fastcall 包装。
*/
#define STDCALL    __stdcall
#define CDECL      __cdecl
#define FASTCALL   __fastcall
#define THISCALL   __thiscall

/* MSVC 在使用浮点时可能要求这个符号。这里虽然主体尽量用整数，也保留它以避免链接器找 CRT。 */
int _fltused = 0;

/* ================================================================
   1. 这份 2003 EXE 中已经存在的 Win32 API IAT 地址
   ================================================================ */

/*
    IAT 可以理解成“游戏保存系统函数地址的表”。
    例如 0x004BE10C 这个位置里存的是 LoadLibraryA 的真正函数地址。

    我们直接复用游戏自己的表，就不需要让 ASI 静态导入 kernel32/user32。
*/
typedef HMODULE (STDCALL *FnLoadLibraryA)(LPCSTR);
typedef HMODULE (STDCALL *FnGetModuleHandleA)(LPCSTR);
typedef LPVOID  (STDCALL *FnGetProcAddress)(HMODULE, LPCSTR);
typedef LPVOID  (STDCALL *FnVirtualAlloc)(LPVOID, SIZE_T, DWORD, DWORD);
typedef BOOL    (STDCALL *FnVirtualFree)(LPVOID, SIZE_T, DWORD);
typedef HANDLE  (STDCALL *FnCreateThread)(LPVOID, SIZE_T, LPVOID, LPVOID, DWORD, LPDWORD);
typedef DWORD   (STDCALL *FnGetModuleFileNameA)(HMODULE, LPSTR, DWORD);
typedef HANDLE  (STDCALL *FnCreateFileA)(LPCSTR, DWORD, DWORD, LPVOID, DWORD, DWORD, HANDLE);
typedef BOOL    (STDCALL *FnWriteFile)(HANDLE, const void*, DWORD, LPDWORD, LPVOID);
typedef BOOL    (STDCALL *FnCloseHandle)(HANDLE);
typedef DWORD   (STDCALL *FnSetFilePointer)(HANDLE, long, long*, DWORD);
typedef BOOL    (STDCALL *FnGetCursorPos)(POINT_*);
typedef BOOL    (STDCALL *FnSetCursorPos)(int, int);

#define GAME_IAT_LoadLibraryA        (*(FnLoadLibraryA*)0x004BE10C)
#define GAME_IAT_GetModuleHandleA    (*(FnGetModuleHandleA*)0x004BE1AC)
#define GAME_IAT_GetProcAddress      (*(FnGetProcAddress*)0x004BE108)
#define GAME_IAT_VirtualAlloc        (*(FnVirtualAlloc*)0x004BE044)
#define GAME_IAT_VirtualFree         (*(FnVirtualFree*)0x004BE048)
#define GAME_IAT_CreateThread        (*(FnCreateThread*)0x004BE084)
#define GAME_IAT_GetModuleFileNameA  (*(FnGetModuleFileNameA*)0x004BE174)
#define GAME_IAT_CreateFileA         (*(FnCreateFileA*)0x004BE05C)
#define GAME_IAT_WriteFile           (*(FnWriteFile*)0x004BE070)
#define GAME_IAT_CloseHandle         (*(FnCloseHandle*)0x004BE064)
#define GAME_IAT_SetFilePointer      (*(FnSetFilePointer*)0x004BE078)
#define GAME_IAT_GetCursorPos        (*(FnGetCursorPos*)0x004BE204)
#define GAME_IAT_SetCursorPos        (*(FnSetCursorPos*)0x004BE208)

/*
    下面这些 API 并没有出现在游戏的静态导入表中。
    初始化时会借用游戏已有的 GetModuleHandleA + GetProcAddress 去找它们。
*/
typedef BOOL  (STDCALL *FnVirtualProtect)(LPVOID, SIZE_T, DWORD, LPDWORD);
typedef BOOL  (STDCALL *FnFlushInstructionCache)(HANDLE, const void*, SIZE_T);
typedef HANDLE(STDCALL *FnGetCurrentProcess)(void);
typedef DWORD (STDCALL *FnGetTickCount)(void);
typedef DWORD (STDCALL *FnGetPrivateProfileStringA)(LPCSTR, LPCSTR, LPCSTR, LPSTR, DWORD, LPCSTR);

static FnVirtualProtect            g_VirtualProtect = NULL;
static FnFlushInstructionCache     g_FlushInstructionCache = NULL;
static FnGetCurrentProcess         g_GetCurrentProcess = NULL;
static FnGetTickCount              g_GetTickCount = NULL;
static FnGetPrivateProfileStringA  g_GetPrivateProfileStringA = NULL;

/* ================================================================
   2. SDL3 最小动态接口
   ================================================================ */

/*
    不要求编译机安装 SDL3 SDK。
    我们只声明自己真正会用到的几个 ABI 类型和函数指针。

    运行时：
      游戏目录\SDL3.dll
             ↓
      LoadLibraryA("SDL3.dll")
             ↓
      GetProcAddress 逐个取得函数

    如果 SDL3.dll 不存在，插件仍会安装键盘步行/跑步相关 Hook；
    只是 SDL3 手柄部分会自动停用，不让整个游戏因为缺 DLL 启动失败。
*/
typedef u32 SDL_InitFlags;
typedef u32 SDL_JoystickID;
typedef struct SDL_Gamepad SDL_Gamepad;

typedef u8            SDLBool;
typedef SDLBool       (CDECL *FnSDL_InitSubSystem)(SDL_InitFlags);
typedef void          (CDECL *FnSDL_SetMainReady)(void);
typedef void          (CDECL *FnSDL_UpdateGamepads)(void);
typedef SDL_JoystickID*(CDECL *FnSDL_GetGamepads)(int*);
typedef SDL_Gamepad*  (CDECL *FnSDL_OpenGamepad)(SDL_JoystickID);
typedef void          (CDECL *FnSDL_CloseGamepad)(SDL_Gamepad*);
typedef SDLBool       (CDECL *FnSDL_GamepadConnected)(SDL_Gamepad*);
typedef s16           (CDECL *FnSDL_GetGamepadAxis)(SDL_Gamepad*, int);
typedef SDLBool       (CDECL *FnSDL_GetGamepadButton)(SDL_Gamepad*, int);
typedef void          (CDECL *FnSDL_free)(void*);
typedef const char*   (CDECL *FnSDL_GetError)(void);

#define SDL_INIT_GAMEPAD 0x00002000u

/* SDL_GamepadAxis。INVALID=-1，所以 LEFTX 从 0 开始顺排。 */
#define SDL_GAMEPAD_AXIS_LEFTX          0
#define SDL_GAMEPAD_AXIS_LEFTY          1
#define SDL_GAMEPAD_AXIS_RIGHTX         2
#define SDL_GAMEPAD_AXIS_RIGHTY         3
#define SDL_GAMEPAD_AXIS_LEFT_TRIGGER   4
#define SDL_GAMEPAD_AXIS_RIGHT_TRIGGER  5

/* SDL_GamepadButton 中 D-Pad 四方向的固定枚举值。 */
/*
    SDL3 标准手柄按钮编号。

    这里特意把 Xbox 风格常用按钮全部写出来，而不是只声明 D-Pad。
    原因是 v0.1-test1 实机发现：旧 DirectInput 能让 A/B/X/Y 等部分按钮碰巧工作，
    但 START 没有进入游戏，导致某些菜单无法退出。

    test2 起，我们不再依赖“旧 DInput 恰好看见哪些按钮”，而是自己按照 XIDI
    已经验证可用的 1~12 顺序生成一份标准物理按钮位图：

      1  A      -> bit 0
      2  B      -> bit 1
      3  X      -> bit 2
      4  Y      -> bit 3
      5  LB     -> bit 4
      6  RB     -> bit 5
      7  LT     -> bit 6（保留槽位，但恒为 0；LT 独占鼠标右键）
      8  RT     -> bit 7（保留槽位，但恒为 0；RT 独占鼠标左键）
      9  BACK   -> bit 8
      10 START  -> bit 9（槽位保留，但 test4 起恒为 0；START 独占键盘 Esc 语义）
      11 L3     -> bit 10
      12 R3     -> bit 11

    为什么 7/8/10 三个槽位必须“留空而不能删除”：
    XIDI 已经证明这个游戏的物理按钮编号约定就是上面的 1~12。
    如果因为某个物理键改作专用功能就把后续编号整体向前压缩，
    BACK/L3/R3 等原版映射都会错位。

    test5 的三个专用规则是：
      - LT / RT 不进入游戏手柄映射，只做鼠标右键 / 左键；
      - START 不进入旧手柄第 10 键映射；
      - START 一方面在统一输入层补 Esc 的 0x1000，另一方面在 0x40C700 直接响应 DIK_ESCAPE，
        从而覆盖练习模式这种绕过统一逻辑输入、直接查询键盘的老代码。

    因此插件仍保持 1~12 的“物理槽位坐标系”，只是第 7、8、10 槽永远不置位。
*/
#define SDL_GAMEPAD_BUTTON_SOUTH        0
#define SDL_GAMEPAD_BUTTON_EAST         1
#define SDL_GAMEPAD_BUTTON_WEST         2
#define SDL_GAMEPAD_BUTTON_NORTH        3
#define SDL_GAMEPAD_BUTTON_BACK         4
#define SDL_GAMEPAD_BUTTON_START        6
#define SDL_GAMEPAD_BUTTON_LEFT_STICK   7
#define SDL_GAMEPAD_BUTTON_RIGHT_STICK  8
#define SDL_GAMEPAD_BUTTON_LEFT_SHOULDER 9
#define SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER 10
#define SDL_GAMEPAD_BUTTON_DPAD_UP      11
#define SDL_GAMEPAD_BUTTON_DPAD_DOWN    12
#define SDL_GAMEPAD_BUTTON_DPAD_LEFT    13
#define SDL_GAMEPAD_BUTTON_DPAD_RIGHT   14

static HMODULE                    g_sdlModule = NULL;
static FnSDL_InitSubSystem        g_SDL_InitSubSystem = NULL;
static FnSDL_SetMainReady         g_SDL_SetMainReady = NULL;
static FnSDL_UpdateGamepads       g_SDL_UpdateGamepads = NULL;
static FnSDL_GetGamepads          g_SDL_GetGamepads = NULL;
static FnSDL_OpenGamepad          g_SDL_OpenGamepad = NULL;
static FnSDL_CloseGamepad         g_SDL_CloseGamepad = NULL;
static FnSDL_GamepadConnected     g_SDL_GamepadConnected = NULL;
static FnSDL_GetGamepadAxis       g_SDL_GetGamepadAxis = NULL;
static FnSDL_GetGamepadButton     g_SDL_GetGamepadButton = NULL;
static FnSDL_free                 g_SDL_free = NULL;
static FnSDL_GetError             g_SDL_GetError = NULL;

static SDL_Gamepad* g_gamepad = NULL;
static int g_sdlReady = 0;
static int g_sdlInitTried = 0;
static DWORD g_nextGamepadSearchTick = 0;

/* ================================================================
   3. 游戏地址和输入位定义
   ================================================================ */

/* 游戏自己的统一输入对象。 */
#define ADDR_INPUT_STATE          0x007CF218u

/* DirectInput 原始键盘 256-byte 状态表。每个按下的键其最高位 0x80 为 1。 */
#define ADDR_RAW_KEYBOARD         0x00504A7Cu

/*
    游戏对外公开、供 UI 消费的鼠标坐标与按钮状态。
    主循环在 0x457160 中先调用 0x40C750 采集实体鼠标，再调用 0x40C710
    把内部鼠标状态复制到这三个地址。
*/
#define ADDR_MOUSE_X              0x007CF340u
#define ADDR_MOUSE_Y              0x007CF344u
#define ADDR_MOUSE_BUTTONS        0x007CF348u

/*
    游戏真正的“鼠标采集层”内部状态。

    0x40C750 每帧直接写：
      0x504A60 = 客户区鼠标 X
      0x504A64 = 客户区鼠标 Y
      0x504A74 = 鼠标按钮当前态 + 本帧刚按下态

    test3 把 LT/RT 写在 0x7CF348 的较晚阶段，实机证明某些原版消费路径看不到。
    test4 改为在 0x40C750 自己完成实体鼠标采集后，直接合并到这组“源状态”里，
    然后让原版 0x40C710 自己复制到 0x7CF340/344/348。这样 LT/RT 与真实鼠标处在完全同一层。
*/
#define ADDR_NATIVE_MOUSE_X       0x00504A60u
#define ADDR_NATIVE_MOUSE_Y       0x00504A64u
#define ADDR_NATIVE_MOUSE_BUTTONS 0x00504A74u

/*
    v0.1-test7 删除剧情 Right 菜单功能。

    test1~test6 曾逆向并使用过“メニューショートカット”、Message.ani、
    右侧小图标栏和 0x483CA0 等地址，但实机始终无法得到稳定、无副作用的
    Right -> 菜单体验。按照最终需求，这些地址不再进入运行时代码。

    历史地址与失败结论仍保留在《完整接档说明.md》中，方便未来如果另开研究线时复查；
    正式插件本体不再读取、修改或 Hook 这些剧情菜单对象。
*/

/*
    v0.1-test7 只保留五个已经有实机价值、且彼此职责清晰的 Hook：

    - 0x40C700：原生键盘扫描码查询。START 在这里直接等价 DIK_ESCAPE；
    - 0x40C710：原生鼠标 getter。LT/RT 在最终 UI 输出层补鼠标按钮；
    - 0x40C750：原生鼠标采集。右摇杆与 LT/RT 在源状态层合并；
    - 0x411920：游戏统一输入。负责 SDL3 按钮、战斗外 D-Pad/左摇杆方向、键盘固定方案；
    - 0x41866A：原版跑步判定 Gate。只让左摇杆/方向键直接跑，D-Pad/8246 保留原版双击跑。

    0x483CA0 剧情菜单 Hook 已在 test7 完全删除。
*/
#define ADDR_KEY_QUERY            0x0040C700u
#define ADDR_MOUSE_GET            0x0040C710u
#define ADDR_MOUSE_POLL           0x0040C750u
#define ADDR_INPUT_PROCESS        0x00411920u
#define ADDR_RUN_GATE             0x0041866Au

/* 跑步判定 Hook 的三个回去位置。 */
#define ADDR_RUN_ORIGINAL_CONTINUE 0x0041866Fu
#define ADDR_RUN_FORCE_ACTION      0x004186B8u

/* 游戏统一 16-bit 输入里，低四位固定是方向。 */
#define INPUT_UP                  0x0001u
#define INPUT_DOWN                0x0002u
#define INPUT_LEFT                0x0004u
#define INPUT_RIGHT               0x0008u
#define INPUT_DIRECTION_MASK      0x000Fu

/*
    0x1000 是键盘 Esc 对应的统一逻辑输入位。

    静态证据来自原版默认键盘映射表：
      DIK_ESCAPE = 0x01 -> 逻辑 0x1000。

    test2/test3 曾尝试让 SDL3 START 继续扮演 XIDI 的“第 10 个物理按钮”，
    但实机证明这个旧手柄语义并不能覆盖练习模式等所有需要“退出/返回”的场景。
    test4 曾只把 START 并入 0x1000，但实机证明这还不等价真正的 Esc。
    test5 同时 Hook 0x40C700 的 DIK_ESCAPE 查询，才覆盖练习模式这种直接读键盘的代码。
*/
#define INPUT_ESCAPE              0x1000u

/* 游戏鼠标位：低位是“按住”，向左移 2 bit 是“本帧刚按下”。 */
#define MOUSE_LEFT_HELD           0x0001u
#define MOUSE_RIGHT_HELD          0x0002u
#define MOUSE_LEFT_PRESSED        0x0004u
#define MOUSE_RIGHT_PRESSED       0x0008u

/* DirectInput 键盘扫描码。 */
#define DIK_ESCAPE    0x01u
#define DIK_NUMPAD1   0x4Fu
#define DIK_NUMPAD2   0x50u
#define DIK_NUMPAD3   0x51u
#define DIK_NUMPAD4   0x4Bu
#define DIK_NUMPAD6   0x4Du
#define DIK_NUMPAD7   0x47u
#define DIK_NUMPAD8   0x48u
#define DIK_NUMPAD9   0x49u
#define DIK_UP        0xC8u
#define DIK_DOWN      0xD0u
#define DIK_LEFT      0xCBu
#define DIK_RIGHT     0xCDu

/*
    跑步判定只需要两种模式。

    ORIGINAL：
      保留游戏 2003 PC 版原始移动语义。
      这正是 D-Pad 和数字小键盘需要的行为：
      - 第一次按住方向：步行；
      - 松开后，在原版允许的时间窗口内再次按同一方向并保持：进入跑步。

    FORCE_RUN：
      左摇杆或键盘方向键正在给方向。
      这两种输入模拟后续主机版的现代语义，不要求双击，直接进入游戏自己的跑步动作 2。

    这里特意没有 FORCE_WALK。test2 曾错误地把 D-Pad/数字小键盘锁成“永远步行”，
    导致原版的双击跑步也被一起禁掉。test3 起恢复原版双击跑步。
*/
#define MOVE_MODE_ORIGINAL  0
#define MOVE_MODE_FORCE_RUN 1

static volatile s32 g_moveMode = MOVE_MODE_ORIGINAL;

/* ================================================================
   4. 配置、日志和插件全局状态
   ================================================================ */

static HMODULE g_asiModule = NULL;
static volatile s32 g_initStarted = 0;
static int g_hooksInstalled = 0;

static char g_iniPath[520];
static char g_logPath[520];

/*
    鼠标灵敏度使用“千分比”保存，避免为了 1.25 这样的数值引入复杂浮点运行库。

    例：
      1.00 -> 1000
      1.50 -> 1500
      0.50 ->  500
*/
static s32 g_mouseSensitivityPermille = 1000;

/* 右摇杆和扳机使用固定死区/阈值。以后如果实机有需求，再暴露到 INI。 */
static s32 g_leftStickDeadzone = 8000;
static s32 g_rightStickDeadzone = 8000;
static s32 g_triggerThreshold = 8000;

/*
    鼠标按钮要记住上一帧，才能从 LT/RT 生成“本帧刚按下”的原版鼠标边沿。

    g_gamepadMouseButtons 保存“这一帧由手柄贡献的鼠标按钮位”：
      bit0 = RT 作为鼠标左键正在按住；
      bit1 = LT 作为鼠标右键正在按住；
      bit2 = RT 本帧刚按下；
      bit3 = LT 本帧刚按下。

    为什么要单独保存一份？
    因为 test4 只把按钮 OR 到 0x504A74，实机仍然没有点击效果。
    test5 会同时在 0x40C750“源状态”层和 0x40C710“getter 输出”层合并同一份按钮位，
    避免某些 UI 在我们写源状态之前/之后读取而漏掉 LT/RT。
*/
static int g_prevLT = 0;
static int g_prevRT = 0;
static u32 g_gamepadMouseButtons = 0;
static DWORD g_lastMouseMoveTick = 0;

/* v0.1-test7：不再维护任何剧情 Right 菜单状态。 */

/* ================================================================
   5. 不依赖 CRT 的小工具函数
   ================================================================ */

/* 返回字符串长度。等价于 strlen，但自己写，避免链接 CRT。 */
static u32 StrLenA(const char* s) {
    u32 n = 0;
    if (!s) return 0;
    while (s[n] != '\0') ++n;
    return n;
}

/* 把 src 拷贝到 dst，并保证最后有 '\0'。 */
static void CopyText(char* dst, u32 cap, const char* src) {
    u32 i = 0;
    if (!dst || cap == 0) return;
    if (!src) {
        dst[0] = '\0';
        return;
    }
    while (i + 1 < cap && src[i] != '\0') {
        dst[i] = src[i];
        ++i;
    }
    dst[i] = '\0';
}

/* 判断两个字节数组是否完全相同。用于核对 EXE 的 Hook 点签名。 */
static int BytesEqual(const u8* a, const u8* b, u32 size) {
    u32 i;
    for (i = 0; i < size; ++i) {
        if (a[i] != b[i]) return 0;
    }
    return 1;
}

/*
    把 ASI 自己的路径扩展名换成 .ini 或 .log。

    例：
      C:\Games\BaldrForceGamepad.asi
    变成：
      C:\Games\BaldrForceGamepad.ini
*/
static void BuildSiblingPath(char* outPath, u32 cap, const char* extensionWithDot) {
    char temp[520];
    u32 len;
    s32 i;

    temp[0] = '\0';
    if (GAME_IAT_GetModuleFileNameA) {
        GAME_IAT_GetModuleFileNameA(g_asiModule, temp, (DWORD)(sizeof(temp) - 1));
        temp[sizeof(temp) - 1] = '\0';
    }

    len = StrLenA(temp);
    i = (s32)len - 1;

    /* 从文件名末尾往前找点号；遇到目录分隔符就停止。 */
    while (i >= 0) {
        if (temp[i] == '.') {
            temp[i] = '\0';
            break;
        }
        if (temp[i] == '\\' || temp[i] == '/') break;
        --i;
    }

    CopyText(outPath, cap, temp);
    len = StrLenA(outPath);

    i = 0;
    while (extensionWithDot[i] != '\0' && len + 1 < cap) {
        outPath[len++] = extensionWithDot[i++];
    }
    outPath[len] = '\0';
}

/* 写一整行 ASCII 日志。日志本身主要用于排错，不参与游戏逻辑。 */
static void LogLine(const char* text) {
    HANDLE file;
    DWORD written = 0;
    const char crlf[2] = {'\r', '\n'};

    if (!text || !g_logPath[0]) return;

    /*
        第一次初始化时日志文件已经用 CREATE_ALWAYS 清空。
        后续每行重新打开后把文件指针放到末尾，再追加文字。
    */
    file = GAME_IAT_CreateFileA(g_logPath, GENERIC_WRITE, FILE_SHARE_READ, NULL,
                               3u /* OPEN_EXISTING */, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE || file == NULL) return;

    GAME_IAT_SetFilePointer(file, 0, NULL, FILE_END);
    GAME_IAT_WriteFile(file, text, (DWORD)StrLenA(text), &written, NULL);
    GAME_IAT_WriteFile(file, crlf, 2, &written, NULL);
    GAME_IAT_CloseHandle(file);
}

/* 启动时清空旧日志，保证用户每次只看到本轮结果。 */
static void ResetLog(void) {
    HANDLE file;
    BuildSiblingPath(g_logPath, (u32)sizeof(g_logPath), ".log");
    file = GAME_IAT_CreateFileA(g_logPath, GENERIC_WRITE, FILE_SHARE_READ, NULL,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file != INVALID_HANDLE_VALUE && file != NULL) {
        GAME_IAT_CloseHandle(file);
    }
}

/*
    把类似 "1.25" 的十进制字符串解析为千分数 1250。
    最多取小数后三位，多余小数直接忽略。
*/
static s32 ParseDecimalPermille(const char* s, s32 defaultValue) {
    s32 whole = 0;
    s32 frac = 0;
    s32 fracDigits = 0;
    int seenDigit = 0;
    int afterDot = 0;

    if (!s) return defaultValue;

    while (*s == ' ' || *s == '\t') ++s;

    while (*s) {
        char c = *s++;
        if (c >= '0' && c <= '9') {
            seenDigit = 1;
            if (!afterDot) {
                if (whole < 100000) whole = whole * 10 + (c - '0');
            } else if (fracDigits < 3) {
                frac = frac * 10 + (c - '0');
                ++fracDigits;
            }
        } else if (c == '.' && !afterDot) {
            afterDot = 1;
        } else {
            break;
        }
    }

    if (!seenDigit) return defaultValue;

    while (fracDigits < 3) {
        frac *= 10;
        ++fracDigits;
    }

    return whole * 1000 + frac;
}

/* 从 INI 读取字符串。API 不存在时直接返回默认值。 */
static void ReadIniText(const char* section, const char* key, const char* defaultText,
                        char* out, u32 outCap) {
    if (!g_GetPrivateProfileStringA || !g_iniPath[0]) {
        CopyText(out, outCap, defaultText);
        return;
    }
    g_GetPrivateProfileStringA(section, key, defaultText, out, outCap, g_iniPath);
}

/* 读取配置并做合理范围保护，防止用户误填一个极端数字。 */
static void LoadConfig(void) {
    char buf[64];
    s32 sensitivity;

    BuildSiblingPath(g_iniPath, (u32)sizeof(g_iniPath), ".ini");

    ReadIniText("Mouse", "Sensitivity", "1.00", buf, (u32)sizeof(buf));
    sensitivity = ParseDecimalPermille(buf, 1000);
    if (sensitivity < 100) sensitivity = 100;     /* 最低 0.10 倍 */
    if (sensitivity > 5000) sensitivity = 5000; /* 最高 5.00 倍 */
    g_mouseSensitivityPermille = sensitivity;

}

/* ================================================================
   6. SDL3 载入、初始化和热插拔
   ================================================================ */

/*
    这个函数只负责“找到 SDL3.dll 中的函数地址”，不初始化手柄子系统。
    SDL 官方要求 SDL_InitSubSystem 尽量在主线程调用，所以真正初始化会延后到游戏输入 Hook。
*/
static int LoadSDL3Functions(void) {
    if (g_sdlModule) return 1;

    g_sdlModule = GAME_IAT_LoadLibraryA("SDL3.dll");
    if (!g_sdlModule) {
        LogLine("[SDL3] 未找到 SDL3.dll；键盘修复继续工作，SDL3 手柄功能停用。");
        return 0;
    }

    g_SDL_InitSubSystem = (FnSDL_InitSubSystem)GAME_IAT_GetProcAddress(g_sdlModule, "SDL_InitSubSystem");
    g_SDL_SetMainReady = (FnSDL_SetMainReady)GAME_IAT_GetProcAddress(g_sdlModule, "SDL_SetMainReady");
    g_SDL_UpdateGamepads = (FnSDL_UpdateGamepads)GAME_IAT_GetProcAddress(g_sdlModule, "SDL_UpdateGamepads");
    g_SDL_GetGamepads = (FnSDL_GetGamepads)GAME_IAT_GetProcAddress(g_sdlModule, "SDL_GetGamepads");
    g_SDL_OpenGamepad = (FnSDL_OpenGamepad)GAME_IAT_GetProcAddress(g_sdlModule, "SDL_OpenGamepad");
    g_SDL_CloseGamepad = (FnSDL_CloseGamepad)GAME_IAT_GetProcAddress(g_sdlModule, "SDL_CloseGamepad");
    g_SDL_GamepadConnected = (FnSDL_GamepadConnected)GAME_IAT_GetProcAddress(g_sdlModule, "SDL_GamepadConnected");
    g_SDL_GetGamepadAxis = (FnSDL_GetGamepadAxis)GAME_IAT_GetProcAddress(g_sdlModule, "SDL_GetGamepadAxis");
    g_SDL_GetGamepadButton = (FnSDL_GetGamepadButton)GAME_IAT_GetProcAddress(g_sdlModule, "SDL_GetGamepadButton");
    g_SDL_free = (FnSDL_free)GAME_IAT_GetProcAddress(g_sdlModule, "SDL_free");
    g_SDL_GetError = (FnSDL_GetError)GAME_IAT_GetProcAddress(g_sdlModule, "SDL_GetError");

    if (!g_SDL_InitSubSystem || !g_SDL_UpdateGamepads || !g_SDL_GetGamepads ||
        !g_SDL_OpenGamepad || !g_SDL_CloseGamepad || !g_SDL_GamepadConnected ||
        !g_SDL_GetGamepadAxis || !g_SDL_GetGamepadButton || !g_SDL_free) {
        LogLine("[SDL3] 缺少必要导出；请使用官方 x86 SDL3 运行库。");
        g_sdlModule = NULL;
        return 0;
    }

    LogLine("[SDL3] 运行库已载入；手柄子系统将在游戏主线程初始化。");
    return 1;
}

/* 第一次进入游戏输入 Hook 时才执行，因此处于游戏主线程。 */
static int EnsureSDLInitializedOnMainThread(void) {
    if (g_sdlReady) return 1;
    if (g_sdlInitTried) return 0;

    g_sdlInitTried = 1;
    if (!g_sdlModule || !g_SDL_InitSubSystem) return 0;

    /* SDL 被嵌入到已有 Win32 游戏中；如果运行库提供 SDL_SetMainReady，就先声明宿主 main 已就绪。 */
    if (g_SDL_SetMainReady) g_SDL_SetMainReady();

    if (!g_SDL_InitSubSystem(SDL_INIT_GAMEPAD)) {
        LogLine("[SDL3] SDL_INIT_GAMEPAD 初始化失败。");
        return 0;
    }

    g_sdlReady = 1;
    LogLine("[SDL3] 手柄子系统已在游戏线程初始化。");
    return 1;
}

/*
    找到第一只 SDL Gamepad。
    没有设备时不会每帧疯狂枚举，而是大约 1 秒再尝试一次。
*/
static void FindGamepadIfNeeded(DWORD now) {
    SDL_JoystickID* ids;
    int count = 0;

    if (!g_sdlReady) return;

    if (g_gamepad) {
        if (g_SDL_GamepadConnected(g_gamepad)) return;

        g_SDL_CloseGamepad(g_gamepad);
        g_gamepad = NULL;
        LogLine("[SDL3] 手柄已断开。");
    }

    if ((s32)(now - g_nextGamepadSearchTick) < 0) return;
    g_nextGamepadSearchTick = now + 1000u;

    ids = g_SDL_GetGamepads(&count);
    if (!ids) return;

    if (count > 0 && ids[0] != 0) {
        g_gamepad = g_SDL_OpenGamepad(ids[0]);
        if (g_gamepad) {
            LogLine("[SDL3] 已打开第一只可用手柄。");
        }
    }

    g_SDL_free(ids);
}

/* ================================================================
   7. 方向输入：键盘、D-Pad、左摇杆
   ================================================================ */

/* 判断某个 DirectInput 扫描码现在是否按住。 */
static int RawKeyDown(u32 scanCode) {
    const volatile u8* keyboard = (const volatile u8*)ADDR_RAW_KEYBOARD;
    return (keyboard[scanCode & 0xFFu] & 0x80u) != 0;
}

/*
    数字小键盘使用“原版移动语义”。
    也就是说：第一次按住是步行；按原版节奏双击同方向并保持第二下，会进入跑步。

    除了用户明确要求的 8/2/4/6，也保留原版 1/3/7/9 四个对角方向，
    这样使用完整数字键盘时不会丢失原游戏原本就有的斜向移动能力。
*/
static u16 GetNumpadWalkDirection(void) {
    u16 d = 0;

    if (RawKeyDown(DIK_NUMPAD8)) d |= INPUT_UP;
    if (RawKeyDown(DIK_NUMPAD2)) d |= INPUT_DOWN;
    if (RawKeyDown(DIK_NUMPAD4)) d |= INPUT_LEFT;
    if (RawKeyDown(DIK_NUMPAD6)) d |= INPUT_RIGHT;

    if (RawKeyDown(DIK_NUMPAD7)) d |= (INPUT_UP | INPUT_LEFT);
    if (RawKeyDown(DIK_NUMPAD9)) d |= (INPUT_UP | INPUT_RIGHT);
    if (RawKeyDown(DIK_NUMPAD1)) d |= (INPUT_DOWN | INPUT_LEFT);
    if (RawKeyDown(DIK_NUMPAD3)) d |= (INPUT_DOWN | INPUT_RIGHT);

    return d;
}

/* 方向键用于“强制跑步”。同时按两个方向自然形成对角线。 */
static u16 GetArrowRunDirection(void) {
    u16 d = 0;
    if (RawKeyDown(DIK_UP))    d |= INPUT_UP;
    if (RawKeyDown(DIK_DOWN))  d |= INPUT_DOWN;
    if (RawKeyDown(DIK_LEFT))  d |= INPUT_LEFT;
    if (RawKeyDown(DIK_RIGHT)) d |= INPUT_RIGHT;
    return d;
}

/*
    处理一个摇杆轴的死区。

    例如 X=5000，小于 8000 死区，就当作 0；
    X=20000，则保留为正方向。
    这里只需要判断方向，不需要把数值做得特别复杂。
*/
static s32 ApplyDeadzone(s32 value, s32 deadzone) {
    if (value > -deadzone && value < deadzone) return 0;
    return value;
}

/*
    D-Pad 使用原版 PC 移动语义：普通按住=步行，双击同方向并保持=跑步。
    这里只负责把十字键转换成游戏的方向 bit；是否触发跑步继续交给原版双击检测。
*/
static u16 GetSDLPadWalkDirection(void) {
    u16 d = 0;
    if (!g_gamepad) return 0;

    if (g_SDL_GetGamepadButton(g_gamepad, SDL_GAMEPAD_BUTTON_DPAD_UP))    d |= INPUT_UP;
    if (g_SDL_GetGamepadButton(g_gamepad, SDL_GAMEPAD_BUTTON_DPAD_DOWN))  d |= INPUT_DOWN;
    if (g_SDL_GetGamepadButton(g_gamepad, SDL_GAMEPAD_BUTTON_DPAD_LEFT))  d |= INPUT_LEFT;
    if (g_SDL_GetGamepadButton(g_gamepad, SDL_GAMEPAD_BUTTON_DPAD_RIGHT)) d |= INPUT_RIGHT;
    return d;
}

/* 左摇杆始终属于“强制跑步来源”。 */
static u16 GetSDLStickRunDirection(void) {
    s32 x, y;
    u16 d = 0;
    if (!g_gamepad) return 0;

    x = ApplyDeadzone((s32)g_SDL_GetGamepadAxis(g_gamepad, SDL_GAMEPAD_AXIS_LEFTX), g_leftStickDeadzone);
    y = ApplyDeadzone((s32)g_SDL_GetGamepadAxis(g_gamepad, SDL_GAMEPAD_AXIS_LEFTY), g_leftStickDeadzone);

    if (x < 0) d |= INPUT_LEFT;
    if (x > 0) d |= INPUT_RIGHT;
    if (y < 0) d |= INPUT_UP;
    if (y > 0) d |= INPUT_DOWN;
    return d;
}

/*
    把 SDL3 手柄转换成游戏认识的“物理按钮 1~12 位图”。

    这一层只负责普通手柄按钮，不负责三个“专用输入”：
      - LT：鼠标右键；
      - RT：鼠标左键；
      - START：键盘 Esc。

    所以物理槽位 7、8、10 都故意留空。
    注意“留空”和“删除编号”完全不同：L3/R3 仍然必须待在第 11/12 槽，
    这样原版手柄映射表和 XIDI 已验证的编号体系不会因为专用键而整体错位。
*/
static u32 GetSDLPhysicalButtonMask(void) {
    u32 buttons = 0;

    if (!g_gamepad) return 0;

    /* A/B/X/Y -> 物理按钮 1/2/3/4。 */
    if (g_SDL_GetGamepadButton(g_gamepad, SDL_GAMEPAD_BUTTON_SOUTH)) buttons |= (1u << 0);
    if (g_SDL_GetGamepadButton(g_gamepad, SDL_GAMEPAD_BUTTON_EAST))  buttons |= (1u << 1);
    if (g_SDL_GetGamepadButton(g_gamepad, SDL_GAMEPAD_BUTTON_WEST))  buttons |= (1u << 2);
    if (g_SDL_GetGamepadButton(g_gamepad, SDL_GAMEPAD_BUTTON_NORTH)) buttons |= (1u << 3);

    /* LB/RB -> 物理按钮 5/6。 */
    if (g_SDL_GetGamepadButton(g_gamepad, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER))  buttons |= (1u << 4);
    if (g_SDL_GetGamepadButton(g_gamepad, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER)) buttons |= (1u << 5);

    /* bit 6 / bit 7：对应 XIDI 的 LT / RT，恒为 0；两只扳机独占鼠标。 */

    /* BACK 仍是第 9 个物理按钮。 */
    if (g_SDL_GetGamepadButton(g_gamepad, SDL_GAMEPAD_BUTTON_BACK)) buttons |= (1u << 8);

    /*
        bit 9：对应 XIDI 的第 10 键 START，test4 起也恒为 0。
        START 会在 InputProcessHook 中直接 OR 到 INPUT_ESCAPE(0x1000)，
        因而等价真正的键盘 Esc，不再受旧手柄映射表限制。
    */

    /* L3/R3 继续保持原来的第 11/12 物理槽位。 */
    if (g_SDL_GetGamepadButton(g_gamepad, SDL_GAMEPAD_BUTTON_LEFT_STICK))  buttons |= (1u << 10);
    if (g_SDL_GetGamepadButton(g_gamepad, SDL_GAMEPAD_BUTTON_RIGHT_STICK)) buttons |= (1u << 11);

    return buttons;
}

/*
    用游戏自己的 joystick 映射表，把上面的“物理按钮位图”转换成逻辑动作位。

    输入对象 +0xB8 开始保存 20 个 WORD 映射槽。
    其中第 8~16 号槽（偏移 +0xC8..+0xD8）正好对应逻辑动作：
      0x0010, 0x0020, 0x0040, ... , 0x1000

    每个 WORD 不是按钮编号，而是“允许哪些物理按钮触发本动作”的 bit mask。
    例如目标 EXE 的默认 +0xD2 = 0x0200，代表第 10 个物理按钮触发逻辑 0x0200；
    这正好就是 XIDI 的 START 第 10 键，因此 test2 能自然恢复那些菜单的 Start 语义。
*/
static u16 MapSDLButtonsThroughGameConfig(void* state) {
    const volatile u16* joystickMap;
    u32 physicalButtons;
    u16 logical = 0;
    u32 i;

    if (!state || !g_gamepad) return 0;

    physicalButtons = GetSDLPhysicalButtonMask();
    if (physicalButtons == 0) return 0;

    /* +0xC8 就是第一个“动作按钮映射 WORD”。 */
    joystickMap = (const volatile u16*)((const u8*)state + 0xC8);

    for (i = 0; i < 9u; ++i) {
        if ((physicalButtons & (u32)joystickMap[i]) != 0) {
            logical = (u16)(logical | (u16)(0x0010u << i));
        }
    }

    return logical;
}

/* ================================================================
   8. 右摇杆鼠标 + LT/RT 鼠标键 + START=Esc 原生查询
   ================================================================ */

/*
    把摇杆轴映射成每帧鼠标像素增量。

    算式概念是：
      摇杆比例 × 900 像素/秒 × Sensitivity × 本帧毫秒数

    全部使用 32-bit 整数，避免 x86 无 CRT 构建时引入 64-bit 除法帮助函数。
*/
static s32 AxisToMouseDelta(s32 raw, s32 deadzone, DWORD deltaMs) {
    s32 sign = 1;
    s32 mag;
    s32 normalized;
    s32 sensitivityScaled;
    s32 result;

    if (raw < 0) {
        sign = -1;
        raw = -raw;
    }

    mag = raw;
    if (mag <= deadzone) return 0;
    if (mag > 32767) mag = 32767;

    /* 去掉死区后重新拉伸到 0..32767，避免刚越过死区时突然跳得很快。 */
    normalized = (mag - deadzone) * 32767 / (32767 - deadzone);

    /* 先除后乘，所有中间值都留在 32-bit 安全范围。 */
    sensitivityScaled = normalized * g_mouseSensitivityPermille / 32767;
    result = sensitivityScaled * 900 / 1000;
    result = result * (s32)deltaMs / 1000;

    return result * sign;
}

/*
    右摇杆仍然复用 Windows 光标位置，因为原版 0x40C750 本来就是通过 GetCursorPos
    取得鼠标坐标。这样游戏自己的窗口/客户区换算保持原样，不需要插件重新猜比例。

    test5 已彻底删除“自动隐藏光标”功能：这里仅移动坐标，绝不再修改任何光标精灵可见性。
*/
static void UpdateGamepadMouseCursor(DWORD now) {
    POINT_ p;
    s32 rx, ry;
    s32 dx, dy;
    DWORD dt;

    if (!g_gamepad) return;

    rx = (s32)g_SDL_GetGamepadAxis(g_gamepad, SDL_GAMEPAD_AXIS_RIGHTX);
    ry = (s32)g_SDL_GetGamepadAxis(g_gamepad, SDL_GAMEPAD_AXIS_RIGHTY);

    if (g_lastMouseMoveTick == 0) g_lastMouseMoveTick = now;
    dt = now - g_lastMouseMoveTick;
    if (dt > 50u) dt = 50u;
    g_lastMouseMoveTick = now;

    dx = AxisToMouseDelta(rx, g_rightStickDeadzone, dt);
    dy = AxisToMouseDelta(ry, g_rightStickDeadzone, dt);

    if ((dx != 0 || dy != 0) && GAME_IAT_GetCursorPos && GAME_IAT_SetCursorPos) {
        if (GAME_IAT_GetCursorPos(&p)) {
            GAME_IAT_SetCursorPos((int)(p.x + dx), (int)(p.y + dy));
        }
    }
}

/*
    根据当前 SDL3 扳机状态构造“游戏原生鼠标按钮位”。

    BALDR FORCE 在 0x40C750 中使用的按钮位已经由机器码确认：
      bit 0 = 左键按住
      bit 1 = 右键按住
      bit 2 = 左键本帧刚按下
      bit 3 = 右键本帧刚按下

    用户要求：
      RT = 鼠标左键
      LT = 鼠标右键

    这里不把 LT/RT 送进 joystick 按钮表，它们只属于鼠标。
*/
static u32 BuildGamepadMouseButtonMask(void) {
    int lt = 0;
    int rt = 0;
    u32 mask = 0;

    if (g_gamepad && g_SDL_GetGamepadAxis) {
        lt = ((s32)g_SDL_GetGamepadAxis(g_gamepad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER) > g_triggerThreshold);
        rt = ((s32)g_SDL_GetGamepadAxis(g_gamepad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) > g_triggerThreshold);
    }

    if (rt) {
        mask |= MOUSE_LEFT_HELD;
        if (!g_prevRT) mask |= MOUSE_LEFT_PRESSED;
    }

    if (lt) {
        mask |= MOUSE_RIGHT_HELD;
        if (!g_prevLT) mask |= MOUSE_RIGHT_PRESSED;
    }

    g_prevRT = rt;
    g_prevLT = lt;
    g_gamepadMouseButtons = mask;
    return mask;
}

/*
    0x40C700 是游戏自己的“查询一个 DirectInput 键盘扫描码是否正在按下”的函数。

    关键实机结论：test4 仅在统一 16-bit 输入中 OR 0x1000，START 仍无法退出练习模式。
    反汇编随后确认练习模式在 0x42E990 直接：
        push DIK_ESCAPE(0x01)
        call 0x40C700
    它完全绕过统一输入层。

    因此 test5 在真正的键盘查询层补 START：
    - 如果游戏问的是 DIK_ESCAPE，而且 SDL START 正在按住，直接返回 1；
    - 其他所有按键完全交给原函数。

    这里会在查询 Esc 时顺手刷新一次 SDL3，避免 START 状态只在别的 Hook 中更新、
    导致练习模式恰好先查询 Esc 时读到旧一帧状态。
*/
typedef int (CDECL *FnKeyQuery)(u32 scanCode);
static FnKeyQuery g_originalKeyQuery = NULL;

static int CDECL KeyQueryHook(u32 scanCode) {
    if (scanCode == DIK_ESCAPE) {
        DWORD now = g_GetTickCount ? g_GetTickCount() : 0;

        if (g_sdlReady && g_SDL_UpdateGamepads) {
            g_SDL_UpdateGamepads();
            FindGamepadIfNeeded(now);
        }

        if (g_gamepad && g_SDL_GetGamepadButton &&
            g_SDL_GetGamepadButton(g_gamepad, SDL_GAMEPAD_BUTTON_START)) {
            return 1;
        }
    }

    return g_originalKeyQuery(scanCode);
}

/*
    0x40C710 是游戏的原生鼠标 getter：
      参数1 -> 返回 X
      参数2 -> 返回 Y
      参数3 -> 返回按钮位

    test4 只在 0x40C750 之后写 0x504A74，实机仍然没有 LT/RT 点击。
    所以 test5 再在 getter 的“最终输出”处补一次同样的按钮位。

    注意：这里只 OR 按钮，不改真实鼠标本身，也不伪造 Windows SendInput。
*/
typedef void (CDECL *FnMouseGet)(s32* outX, s32* outY, u32* outButtons);
static FnMouseGet g_originalMouseGet = NULL;

static void CDECL MouseGetHook(s32* outX, s32* outY, u32* outButtons) {
    g_originalMouseGet(outX, outY, outButtons);
    if (outButtons) {
        *outButtons |= g_gamepadMouseButtons;
    }
}

/* 0x40C750 原生鼠标采集 Hook。 */
typedef void (CDECL *FnMousePoll)(void);
static FnMousePoll g_originalMousePoll = NULL;

static void CDECL MousePollHook(void) {
    DWORD now = g_GetTickCount ? g_GetTickCount() : 0;
    u32 padButtons;

    /*
        鼠标采集发生得很早，所以在这里刷新 SDL3：
        - 右摇杆移动会更及时；
        - LT/RT 的鼠标按钮边沿也和实体鼠标处在同一帧。
    */
    if (g_sdlReady && g_SDL_UpdateGamepads) {
        g_SDL_UpdateGamepads();
        FindGamepadIfNeeded(now);
    }

    if (now != 0) {
        UpdateGamepadMouseCursor(now);
    }

    /* 先让游戏完整采集真实鼠标。 */
    g_originalMousePoll();

    /*
        再构造手柄鼠标按钮，并合并到原生源状态。
        同一份 g_gamepadMouseButtons 还会由 MouseGetHook 合并到 getter 输出。
    */
    padButtons = BuildGamepadMouseButtonMask();
    *(volatile u32*)ADDR_NATIVE_MOUSE_BUTTONS |= padButtons;
}

/* ================================================================
   9. Hook 通用工具

   ================================================================ */

/*
    在 target 写入 x86 的 E9 rel32 跳转。
    rel32 = 目标地址 - “E9 指令后面的地址”。
*/
static void WriteRelativeJump(u8* target, const void* destination, u32 overwriteLength) {
    s32 rel;
    u32 i;

    target[0] = 0xE9;
    rel = (s32)((const u8*)destination - (target + 5));
    *(s32*)(target + 1) = rel;

    /* 如果覆盖长度大于 5，多出来的字节填 NOP，避免残留半条旧指令。 */
    for (i = 5; i < overwriteLength; ++i) target[i] = 0x90;
}

/*
    安装最普通的“入口 Hook”：
    1. 分配一小块可执行内存做 trampoline；
    2. 把原函数开头被覆盖的字节复制过去；
    3. trampoline 末尾跳回原函数剩余部分；
    4. 原函数入口改成跳到我们的 Hook。
*/
static void* InstallTrampolineHook(u32 targetAddress, const void* hook, u32 stolenLength) {
    u8* target = (u8*)targetAddress;
    u8* trampoline;
    DWORD oldProtect = 0;
    DWORD tempProtect = 0;
    u32 i;

    trampoline = (u8*)GAME_IAT_VirtualAlloc(NULL, stolenLength + 5u,
                                            MEM_COMMIT | MEM_RESERVE,
                                            PAGE_EXECUTE_READWRITE);
    if (!trampoline) return NULL;

    for (i = 0; i < stolenLength; ++i) trampoline[i] = target[i];
    WriteRelativeJump(trampoline + stolenLength, target + stolenLength, 5);

    if (!g_VirtualProtect(target, stolenLength, PAGE_EXECUTE_READWRITE, &oldProtect)) return NULL;
    WriteRelativeJump(target, hook, stolenLength);
    g_VirtualProtect(target, stolenLength, oldProtect, &tempProtect);

    if (g_FlushInstructionCache && g_GetCurrentProcess) {
        g_FlushInstructionCache(g_GetCurrentProcess(), target, stolenLength);
    }

    return trampoline;
}

/* 单独给跑步判定安装 5-byte JMP；它不需要普通 trampoline。 */
static int InstallDirectJump(u32 targetAddress, const void* hook, u32 overwriteLength) {
    u8* target = (u8*)targetAddress;
    DWORD oldProtect = 0;
    DWORD tempProtect = 0;

    if (!g_VirtualProtect(target, overwriteLength, PAGE_EXECUTE_READWRITE, &oldProtect)) return 0;
    WriteRelativeJump(target, hook, overwriteLength);
    g_VirtualProtect(target, overwriteLength, oldProtect, &tempProtect);

    if (g_FlushInstructionCache && g_GetCurrentProcess) {
        g_FlushInstructionCache(g_GetCurrentProcess(), target, overwriteLength);
    }
    return 1;
}

/* ================================================================
   10. 统一输入 Hook：核心功能的中心
   ================================================================ */

typedef void (CDECL *FnInputProcess)(void* state, u16 logicalMask);
static FnInputProcess g_originalInputProcess = NULL;

/*
    游戏每帧已经把键盘/旧 DirectInput 手柄转换成 logicalMask 后，会调用 0x411920。
    我们就在这里把“现代来源”重新整理成游戏能理解的低四方向 bit。
*/
static void CDECL InputProcessHook(void* state, u16 logicalMask) {
    u16 numpadOriginal = 0;
    u16 arrowRun = 0;
    u16 dpadOriginal = 0;
    u16 stickRun = 0;
    u16 originalDir;
    u16 runDir;
    u16 chosenDir = 0;
    u16 sdlLogicalButtons = 0;
    DWORD now = g_GetTickCount ? g_GetTickCount() : 0;

    /* 只处理主输入对象，万一同一个函数还有别的对象使用，就保持原样。 */
    if ((u32)state == ADDR_INPUT_STATE) {
        /* SDL 初始化必须发生在游戏线程，因此放在这里。 */
        if (EnsureSDLInitializedOnMainThread()) {
            g_SDL_UpdateGamepads();
            FindGamepadIfNeeded(now);
        }

        /* 键盘固定方案：不再受游戏设置中的方向键/小键盘切换影响。 */
        numpadOriginal = GetNumpadWalkDirection();
        arrowRun = GetArrowRunDirection();

        /* SDL 手柄存在时，D-Pad 和左摇杆分别保留“走/跑”的来源身份。 */
        if (g_gamepad) {
            dpadOriginal = GetSDLPadWalkDirection();
            stickRun = GetSDLStickRunDirection();

            /*
                把 SDL3 的普通按钮 A/B/X/Y/LB/RB/BACK/L3/R3
                通过游戏自己的 joystick 映射表合并到逻辑动作。

                LT/RT 不在这里进入游戏动作：它们独占原生鼠标右键/左键。
                START 也不再进入旧手柄物理按钮表：test3/test4 实机证明“第 10 个手柄按钮”
                和单独补 0x1000 都无法退出练习模式。test5 还会在 0x40C700 直接响应 DIK_ESCAPE。
            */
            sdlLogicalButtons = MapSDLButtonsThroughGameConfig(state);
            logicalMask = (u16)(logicalMask | sdlLogicalButtons);

            /*
                START = Esc。
                这里使用“当前按住”而不是只发一帧脉冲，行为与键盘 Esc 完全同级；
                0x411920 会自己计算 just-pressed / released / repeat，插件不用重复造边沿状态。
            */
            if (g_SDL_GetGamepadButton &&
                g_SDL_GetGamepadButton(g_gamepad, SDL_GAMEPAD_BUTTON_START)) {
                logicalMask = (u16)(logicalMask | INPUT_ESCAPE);
            }
        }

        originalDir = (u16)(numpadOriginal | dpadOriginal);
        runDir = (u16)(arrowRun | stickRun);

        /*
            优先级：直接跑来源 > 原版移动来源。

            - 左摇杆 / 方向键：明确要求“直接跑”，所以设置 FORCE_RUN。
            - D-Pad / 数字小键盘：只替换方向来源，不改变原版跑步判定，
              因此保持 ORIGINAL，让 0x41866A 后面的原版双击检测自己决定这一帧是否起跑。

            例如玩家同时推左摇杆并按 D-Pad，左摇杆的“明确直接跑”意图优先。
        */
        if (runDir != 0) {
            chosenDir = runDir;
            g_moveMode = MOVE_MODE_FORCE_RUN;
        } else if (originalDir != 0) {
            chosenDir = originalDir;
            g_moveMode = MOVE_MODE_ORIGINAL;
        } else {
            g_moveMode = MOVE_MODE_ORIGINAL;
        }

        /*
            只要 SDL 手柄已打开，就清掉原版 DInput 产生的方向。
            否则同一只手柄可能同时被老 DInput 和 SDL3 各读一次，来源身份就会混乱。

            键盘正在使用我们固定的数字键/方向键时也清掉原方向，确保游戏设置无法反向覆盖它。
        */
        if (g_gamepad || numpadOriginal != 0 || arrowRun != 0) {
            logicalMask = (u16)((logicalMask & ~INPUT_DIRECTION_MASK) | chosenDir);
        }

        /*
            v0.1-test7 起，Right 不再被插件用于剧情菜单。
            因此这里不保存 Right 的按下沿、不延迟触发、不消费方向；
            logicalMask 中的 INPUT_RIGHT 会原样进入游戏自己的输入历史与后续逻辑。
        */

        /*
            0x411920 会先读取输入历史环形缓冲中的“上一份当前值”。
            我们把这帧最终方向也写进当前历史槽，保证边沿/连发/双击历史都看到同一个结果。
        */
        {
            volatile u32* historyIndex = (volatile u32*)((u8*)state + 0xE4);
            volatile u16* historyBase = (volatile u16*)((u8*)state + 0xE8);
            u32 index = (*historyIndex) & 31u;
            historyBase[index] = logicalMask;
        }

    }

    /* 最后继续执行游戏原版 0x411920 的全部边沿/连发逻辑。 */
    g_originalInputProcess(state, logicalMask);
}

/* ================================================================
   11. 跑步判定 Hook：真正实现“走/跑共存”
   ================================================================ */

/*
    0x41866A 原始 5 bytes：
        A0 1C F2 7C 00
        mov al, byte ptr [0x7CF21C]

    它后面就是 PC 版的“松开后 2..7 帧内再次按相同方向 -> 跑步”检测。

    我们在这里做两路分流：
    - FORCE_RUN：左摇杆/方向键直接去原版 0x4186B8，执行 action 2（跑步）；
    - ORIGINAL：D-Pad/数字小键盘以及其他未接管来源，补执行被覆盖的
      mov al,[0x7CF21C]，然后回 0x41866F，继续游戏原本的“双击同方向 -> 跑步”逻辑。

    test3 特别恢复了 ORIGINAL 路径给 D-Pad/数字小键盘。
    因此它们不再被永久锁成步行，而是完整保留原版“单按走、双击跑”。

    这里用 naked 函数，是因为不能让编译器自动生成 push ebp 等函数序言；
    我们需要原封不动地接管 CPU 当前寄存器现场。
*/
__declspec(naked) static void RunGateHook(void) {
    __asm {
        cmp dword ptr [g_moveMode], MOVE_MODE_FORCE_RUN
        je force_run

        /*
            非“直接跑”来源全部走原版判定。
            这里包括 D-Pad 和数字小键盘，所以它们的双击跑步能力会完整保留。
        */
        mov al, byte ptr [ADDR_INPUT_STATE + 4]
        push ADDR_RUN_ORIGINAL_CONTINUE
        ret

    force_run:
        /* 走到原版“调用 action 2”的路径，完全复用游戏自己的跑步动作。 */
        push ADDR_RUN_FORCE_ACTION
        ret
    }
}

/* ================================================================
   12. 目标 EXE 验证和初始化
   ================================================================ */


/* 用 GetProcAddress 找到没有静态导入的系统 API。 */
static int ResolveSystemFunctions(void) {
    HMODULE kernel32;
    kernel32 = GAME_IAT_GetModuleHandleA("kernel32.dll");
    if (!kernel32) return 0;

    g_VirtualProtect = (FnVirtualProtect)GAME_IAT_GetProcAddress(kernel32, "VirtualProtect");
    g_FlushInstructionCache = (FnFlushInstructionCache)GAME_IAT_GetProcAddress(kernel32, "FlushInstructionCache");
    g_GetCurrentProcess = (FnGetCurrentProcess)GAME_IAT_GetProcAddress(kernel32, "GetCurrentProcess");
    g_GetTickCount = (FnGetTickCount)GAME_IAT_GetProcAddress(kernel32, "GetTickCount");
    g_GetPrivateProfileStringA = (FnGetPrivateProfileStringA)GAME_IAT_GetProcAddress(kernel32, "GetPrivateProfileStringA");

    return g_VirtualProtect && g_GetTickCount && g_GetPrivateProfileStringA;
}

/*
    精确核对五个 Hook 点原始机器码。

    任何一个入口不匹配，都说明当前 EXE 不是我们逆向并验证的 2003 基线，
    此时宁可整个插件停止，也不能猜地址继续写内存。
*/
static int VerifyTargetExecutable(void) {
    static const u8 sigKey[6]       = {0x8B,0x4C,0x24,0x04,0x33,0xC0};
    static const u8 sigMouseGet[6]  = {0x8B,0x44,0x24,0x04,0x85,0xC0};
    static const u8 sigMousePoll[7] = {0x83,0xEC,0x08,0x8D,0x44,0x24,0x00};
    static const u8 sigInput[5]     = {0x51,0x53,0x55,0x56,0x57};
    static const u8 sigRun[5]       = {0xA0,0x1C,0xF2,0x7C,0x00};

    if (!BytesEqual((const u8*)ADDR_KEY_QUERY, sigKey, 6)) return 0;
    if (!BytesEqual((const u8*)ADDR_MOUSE_GET, sigMouseGet, 6)) return 0;
    if (!BytesEqual((const u8*)ADDR_MOUSE_POLL, sigMousePoll, 7)) return 0;
    if (!BytesEqual((const u8*)ADDR_INPUT_PROCESS, sigInput, 5)) return 0;
    if (!BytesEqual((const u8*)ADDR_RUN_GATE, sigRun, 5)) return 0;
    return 1;
}

/* 安装五个 Hook；全部成功后才允许把插件标记为已就绪。 */
static int InstallAllHooks(void) {
    void* keyTrampoline;
    void* mouseGetTrampoline;
    void* mousePollTrampoline;
    void* inputTrampoline;

    /* 0x40C700 前两条完整指令一共 6 bytes。 */
    keyTrampoline = InstallTrampolineHook(ADDR_KEY_QUERY, KeyQueryHook, 6);
    if (!keyTrampoline) return 0;
    g_originalKeyQuery = (FnKeyQuery)keyTrampoline;

    /* 0x40C710 前两条完整指令一共 6 bytes。 */
    mouseGetTrampoline = InstallTrampolineHook(ADDR_MOUSE_GET, MouseGetHook, 6);
    if (!mouseGetTrampoline) return 0;
    g_originalMouseGet = (FnMouseGet)mouseGetTrampoline;

    /* 0x40C750 前两条完整指令一共 7 bytes。 */
    mousePollTrampoline = InstallTrampolineHook(ADDR_MOUSE_POLL, MousePollHook, 7);
    if (!mousePollTrampoline) return 0;
    g_originalMousePoll = (FnMousePoll)mousePollTrampoline;

    inputTrampoline = InstallTrampolineHook(ADDR_INPUT_PROCESS, InputProcessHook, 5);
    if (!inputTrampoline) return 0;
    g_originalInputProcess = (FnInputProcess)inputTrampoline;

    if (!InstallDirectJump(ADDR_RUN_GATE, RunGateHook, 5)) return 0;

    return 1;
}

/*
    真正初始化在线程里执行，避免在 Windows Loader Lock 中调用 LoadLibrary(SDL3.dll)。
*/
static DWORD STDCALL InitializeThread(void* unused) {
    (void)unused;

    /*
        先验证目标。注意：验证成功后才使用该 EXE 的固定 IAT 地址做进一步工作。
        这份插件本来就是单版本专用，因此不尝试“猜测兼容地址”。
    */
    if (!VerifyTargetExecutable()) {
        return 0;
    }

    ResetLog();
    LogLine("[BaldrForceGamepad v0.1-test7] 目标 EXE 特征验证通过。");

    if (!ResolveSystemFunctions()) {
        LogLine("[失败] 无法解析所需 Win32 API。");
        return 0;
    }

    LoadConfig();
    LogLine("[配置] 已读取 BaldrForceGamepad.ini。");

    /* SDL 只在这里 LoadLibrary；真正 SDL_Init 延后到游戏主线程。 */
    LoadSDL3Functions();

    if (!InstallAllHooks()) {
        LogLine("[失败] Hook 安装失败，停止初始化。");
        return 0;
    }

    g_hooksInstalled = 1;
    LogLine("[成功] 原生键盘查询、鼠标 getter/采集、统一输入、步行/跑步 Hook 已安装。");
    LogLine("[规则] 战斗：D-Pad/数字小键盘=原版单按步行+双击跑步；左摇杆/方向键=直接跑步。");
    LogLine("[规则] 战斗外：D-Pad 与左摇杆进入同一套原生方向逻辑。");
    LogLine("[规则] 剧情菜单：插件不再接管 Right；所有方向右行为完全交还游戏原版。 ");
    LogLine("[规则] 手柄按钮：A/B/X/Y/LB/RB/BACK/L3/R3 走原版映射；START 同时进入统一 Esc 位与 0x40C700 原生 DIK_ESCAPE 查询；LT/RT 只走原生鼠标。");
    LogLine("[规则] 鼠标：右摇杆控制指针；LT/RT 同时注入 0x40C750 源状态与 0x40C710 getter 输出；test7 无光标隐藏功能。");
    return 0;
}

/*
    防止某些 ASI Loader 同时走 DllMain 和 InitializeASI 时重复创建初始化线程。
    这里不要求非常复杂的锁，因为进程加载早期实际只会发生一次；
    即使两个入口非常接近，先写入 g_initStarted 的那个负责初始化。
*/
static void StartInitializationOnce(void) {
    DWORD threadId = 0;
    HANDLE thread;

    if (g_initStarted) return;
    g_initStarted = 1;

    if (!VerifyTargetExecutable()) return;

    thread = GAME_IAT_CreateThread(NULL, 0, (LPVOID)InitializeThread, NULL, 0, &threadId);
    if (thread && thread != INVALID_HANDLE_VALUE) {
        GAME_IAT_CloseHandle(thread);
    }
}

/*
    一些 ASI Loader 会主动查找 InitializeASI 导出；支持它可以提高兼容性。
    函数名保持英文是源码/API 约定，不属于文档文件名。
*/
__declspec(dllexport) void CDECL InitializeASI(void) {
    StartInitializationOnce();
}

/*
    标准 DLL 入口。
    这里只保存模块句柄并启动很小的初始化线程，不在 Loader Lock 里初始化 SDL。
*/
BOOL STDCALL DllMain(HMODULE module, DWORD reason, LPVOID reserved) {
    (void)reserved;

    if (reason == DLL_PROCESS_ATTACH) {
        g_asiModule = module;
        StartInitializationOnce();
    }
    return TRUE;
}
