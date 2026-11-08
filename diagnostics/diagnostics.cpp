// Troubleshooting output: HUD dump state, per-second draw-call statistics, cursor API calls, and
// the first few crashes with registers and the return addresses on the stack.
#include <stdio.h>
#include <string.h>
#include "diagnostics/diagnostics.h"
#include "core/cshell_hud.h"
#include "settings/config.h"
#include "layout/layout.h"
#include "core/log.h"
#include "core/patching.h"
#include "rendering/renderer.h"
#include "diagnostics/screen_access.h"
#include "rendering/driver_state.h"

DumpState g_dump = {false, 0, 0};
DWORD g_hitTraceUntil = 0;

void UpdateDumpAtHudDraw()
{
    if (g_dumpRequested && !g_dump.active) {
        g_dumpRequested = false;
        g_dump.active = true;
        g_dump.hudDrawsSeen = 0;
    }
    if (!g_dump.active) return;

    g_dump.hudDrawsSeen++;
    if (g_dump.hudDrawsSeen > 1) {
        g_dump.active = false;
        g_dump.hudDrawsSeen = 0;
        Log("---- end of dump ----");
    } else {
        Log("---- dump: screen %dx%d hud scale %.3f ui scale %.3f (windows drawn after the HUD come first "
            "next frame) ----", g_screen.width, g_screen.height, g_screen.hudScale, g_screen.uiScaleX);
    }
}

// --- [HUD] perf --------------------------------------------------------------------------------

static const char* PERF_NAMES[PERF_OPERATION_COUNT] = {"blit", "scale", "fill", "read-back", "create", "delete"};

// Totals per source surface and operation, for the most expensive list.
// For each source also how the driver would have drawn it: how many calls of all could be told
// (known), and how many of those went with each DrawState flag.
struct PerfSource { void* surface; int operation, calls, w, h; double ms; int known, texture, reupload, textured2d, inScene; };
static const int MAX_PERF_SOURCES = 64;

static int        g_perfCalls[PERF_OPERATION_COUNT];
static double     g_perfMs[PERF_OPERATION_COUNT];
static DWORD      g_perfPeriodStart = 0;
static PerfSource g_perfSources[MAX_PERF_SOURCES];
static int        g_perfSourceCount = 0;

double GetMilliseconds()
{
    static LARGE_INTEGER frequency = {{0, 0}};
    if (frequency.QuadPart == 0) QueryPerformanceFrequency(&frequency);
    LARGE_INTEGER counter;
    QueryPerformanceCounter(&counter);
    return (double)counter.QuadPart * 1000.0 / (double)frequency.QuadPart;
}

static void AddDrawState(PerfSource& source, int drawState)
{
    if ((drawState & DRAW_KNOWN) == 0) return;
    source.known++;
    if (drawState & DRAW_TEXTURE)     source.texture++;
    if (drawState & DRAW_REUPLOAD)    source.reupload++;
    if (drawState & DRAW_TEXTURED_2D) source.textured2d++;
    if (drawState & DRAW_IN_SCENE)    source.inScene++;
}

static void DescribeDrawPath(const PerfSource& source, char* out, int size);

static void AddToSource(int operation, void* surface, double ms, int drawState)
{
    int i = 0;
    while (i < g_perfSourceCount && !(g_perfSources[i].surface == surface && g_perfSources[i].operation == operation))
        i++;
    if (i == MAX_PERF_SOURCES) return;   // table full

    PerfSource& source = g_perfSources[i];
    if (i == g_perfSourceCount) {
        g_perfSourceCount++;
        memset(&source, 0, sizeof(source));
        source.surface = surface;
        source.operation = operation;
        void* renderer = Renderer::Get();
        if (renderer != NULL) Renderer::GetSize(renderer, surface, source.w, source.h);
    }
    source.calls++;
    source.ms += ms;
    AddDrawState(source, drawState);
}

// " | texture 100% re-upload 0% d3d-2d 100% in-scene 100%": the share of the source's calls
static void DescribeDrawPath(const PerfSource& source, char* out, int size)
{
    if (source.known == 0) {
        _snprintf(out, size, " | path ?");
    } else {
        int n = source.known;
        _snprintf(out, size, " | texture %d%% re-upload %d%% d3d-2d %d%% in-scene %d%%", source.texture * 100 / n,
                  source.reupload * 100 / n, source.textured2d * 100 / n, source.inScene * 100 / n);
    }
    out[size - 1] = '\0';
}

static void LogPerfPeriod(double seconds)
{
    char line[512];
    int length = _snprintf(line, sizeof(line), "perf (per second):");
    for (int op = 0; op < PERF_OPERATION_COUNT && length > 0 && length < (int)sizeof(line); op++) {
        if (op == PERF_DELETE)
            length += _snprintf(line + length, sizeof(line) - length, "  %s %d", PERF_NAMES[op], (int)(g_perfCalls[op] / seconds));
        else
            length += _snprintf(line + length, sizeof(line) - length, "  %s %d (%.1f ms)", PERF_NAMES[op],
                                (int)(g_perfCalls[op] / seconds), g_perfMs[op] / seconds);
    }
    line[sizeof(line) - 1] = '\0';
    Log("%s", line);

    for (int listed = 0; listed < 6; listed++) {      // the most expensive sources by time
        int most = -1;
        for (int i = 0; i < g_perfSourceCount; i++)
            if (g_perfSources[i].calls > 0 && (most < 0 || g_perfSources[i].ms > g_perfSources[most].ms)) most = i;
        if (most < 0) break;
        PerfSource& source = g_perfSources[most];
        char path[96];
        DescribeDrawPath(source, path, sizeof(path));
        Log("     %-9s src %p %dx%d: %d/s, %.2f ms/s%s", PERF_NAMES[source.operation], source.surface,
            source.w, source.h, (int)(source.calls / seconds), source.ms / seconds, path);
        source.calls = 0;                              // listed
    }
}

static void AddPerfSample(int operation, double start, void* surface, int drawState)
{
    double ms = GetMilliseconds() - start;
    g_perfCalls[operation]++;
    g_perfMs[operation] += ms;
    if (surface != NULL) AddToSource(operation, surface, ms, drawState);

    DWORD now = GetTickCount();
    if (now - g_perfPeriodStart < 1000) return;
    if (g_perfPeriodStart != 0) LogPerfPeriod((now - g_perfPeriodStart) / 1000.0);
    memset(g_perfCalls, 0, sizeof(g_perfCalls));
    memset(g_perfMs, 0, sizeof(g_perfMs));
    g_perfSourceCount = 0;
    g_perfPeriodStart = now;
}

PerfTimer::PerfTimer(int perfOperation, void* sourceSurface)
{
    active = g_config.perf && g_ownCallDepth == 0;
    operation = perfOperation;
    source = sourceSurface;
    drawState = active && sourceSurface != NULL ? GetDrawState(sourceSurface) : 0;
    movedBefore = g_drawsMovedIntoScene;
    start = active ? GetMilliseconds() : 0.0;
}

PerfTimer::~PerfTimer()
{
    if (!active) return;
    double ms = GetMilliseconds() - start;
    // Blits and scales of a game picture while d3d.ren is outside its 2D-through-Direct3D mode.
    bool drawnOutsideScene = (drawState & DRAW_KNOWN) != 0 && (drawState & DRAW_TEXTURED_2D) == 0
                             && (operation == PERF_BLIT || operation == PERF_SCALE);
    if (drawnOutsideScene) NoteDrawOutsideScene(source, ms, g_drawsMovedIntoScene != movedBefore);
    if (ms >= SLOW_CALL_MS) {
        int w = 0, h = 0;
        void* renderer = Renderer::Get();
        if (renderer != NULL && source != NULL) Renderer::GetSize(renderer, source, w, h);
        PerfSource state;
        memset(&state, 0, sizeof(state));
        AddDrawState(state, drawState);
        char path[96], what[256];
        DescribeDrawPath(state, path, sizeof(path));
        _snprintf(what, sizeof(what), "%s src %p %dx%d %.2f ms%s", PERF_NAMES[operation], source, w, h, ms, path);
        what[sizeof(what) - 1] = '\0';
        LogSlowCall(what);
    }
    AddPerfSample(operation, start, source, drawState);
}

// --- addresses and crashes ---------------------------------------------------------------------

static HMODULE GetModuleOf(DWORD address)
{
    HMODULE module = NULL;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       (LPCSTR)address, &module);
    return module;
}

static DWORD GetImageSize(HMODULE module)
{
    BYTE* image = (BYTE*)module;
    return ((IMAGE_NT_HEADERS*)(image + ((IMAGE_DOS_HEADER*)image)->e_lfanew))->OptionalHeader.SizeOfImage;
}

void DescribeAddress(DWORD address, char* out, int size)
{
    HMODULE module = GetModuleOf(address);
    if (module == g_originalDll) {
        _snprintf(out, size, "%08lX (cshell_orig VA %08lX)", address, LinkedAddress(address));
    } else if (module != NULL) {
        char path[MAX_PATH] = "?";
        GetModuleFileNameA(module, path, MAX_PATH);
        const char* slash = strrchr(path, '\\');
        _snprintf(out, size, "%08lX (%s+%lX)", address, slash != NULL ? slash + 1 : path, address - (DWORD)module);
    } else {
        _snprintf(out, size, "%08lX", address);
    }
    out[size - 1] = '\0';
}

// Return addresses into the original's code or into this dll, from the top of the stack.
static void LogStackReturnAddresses(const DWORD* stack)
{
    DWORD originalCodeEnd = g_originalBase + (VA_VTABLES_START - VA_IMAGE_BASE);
    HMODULE self = GetModuleOf((DWORD)LogStackReturnAddresses);
    DWORD selfEnd = (DWORD)self + GetImageSize(self);
    int found = 0;
    for (int i = 0; i < 2048 && found < 16; i++) {
        if (IsBadReadPtr(stack + i, 4)) break;
        DWORD value = stack[i];
        bool inOriginal = value >= g_originalBase && value < originalCodeEnd;
        bool inSelf = value >= (DWORD)self && value < selfEnd;
        if (!inOriginal && !inSelf) continue;
        char where[160];
        DescribeAddress(value, where, sizeof(where));
        Log("    stack[%d] %s", i, where);
        found++;
    }
}

// Only logs: the game's own handling continues unchanged.
static LONG CALLBACK LogCrash(EXCEPTION_POINTERS* exception)
{
    static LONG crashesLogged = 0;
    DWORD code = exception->ExceptionRecord->ExceptionCode;
    bool isCrash = code == EXCEPTION_ACCESS_VIOLATION || code == EXCEPTION_ILLEGAL_INSTRUCTION
                   || code == EXCEPTION_STACK_OVERFLOW || code == EXCEPTION_INT_DIVIDE_BY_ZERO;
    if (!isCrash || InterlockedIncrement(&crashesLogged) > 3)
        return EXCEPTION_CONTINUE_SEARCH;

    const CONTEXT* c = exception->ContextRecord;
    char where[160];
    DescribeAddress((DWORD)exception->ExceptionRecord->ExceptionAddress, where, sizeof(where));
    Log("!!! exception %08lX at %s", code, where);
    if (code == EXCEPTION_ACCESS_VIOLATION)
        Log("    %s address %08lX", exception->ExceptionRecord->ExceptionInformation[0] ? "write" : "read",
            (DWORD)exception->ExceptionRecord->ExceptionInformation[1]);
    Log("    eax=%08lX ebx=%08lX ecx=%08lX edx=%08lX esi=%08lX edi=%08lX ebp=%08lX esp=%08lX",
        c->Eax, c->Ebx, c->Ecx, c->Edx, c->Esi, c->Edi, c->Ebp, c->Esp);
    Log("    state: hudDraw=%d ownCalls=%d zone=%d,%d screen=%dx%d mouse=%d,%d", g_hudDrawDepth, g_ownCallDepth,
        g_hudZone.column, g_hudZone.row, g_screen.width, g_screen.height,
        *(int*)LoadedAddress(VA_MOUSE_X), *(int*)LoadedAddress(VA_MOUSE_Y));
    LogStackReturnAddresses((const DWORD*)c->Esp);
    return EXCEPTION_CONTINUE_SEARCH;
}

void InstallCrashLogger() { AddVectoredExceptionHandler(1, LogCrash); }

// --- cursor API ----------------------------------------------------------------------------------

typedef BOOL (WINAPI *SetCursorPosFn)(int, int);
typedef BOOL (WINAPI *ClipCursorFn)(const RECT*);
static SetCursorPosFn g_unhookedSetCursorPos;
static ClipCursorFn   g_unhookedClipCursor;

static BOOL WINAPI HookSetCursorPos(int x, int y)
{
    if (g_config.mouseLog) {
        // The engine re-centres the cursor every frame in mouse-look mode: log each caller 3 times.
        static DWORD callers[16];
        static int timesLogged[16];
        static int callerCount = 0;
        DWORD caller = RETURN_ADDRESS();
        int i = 0;
        while (i < callerCount && callers[i] != caller) i++;
        if (i == callerCount && callerCount < 16) {
            callers[callerCount] = caller;
            timesLogged[callerCount] = 0;
            callerCount++;
        }
        if (i < 16 && timesLogged[i]++ < 3) {
            char where[160];
            DescribeAddress(caller, where, sizeof(where));
            Log("SetCursorPos(%d,%d) from %s", x, y, where);
        }
    }
    return g_unhookedSetCursorPos(x, y);
}

static BOOL WINAPI HookClipCursor(const RECT* r)
{
    if (g_config.mouseLog) {
        char where[160];
        DescribeAddress(RETURN_ADDRESS(), where, sizeof(where));
        if (r != NULL)
            Log("ClipCursor(%ld,%ld,%ld,%ld) from %s", r->left, r->top, r->right, r->bottom, where);
        else
            Log("ClipCursor(NULL) from %s", where);
    }
    return g_unhookedClipCursor(r);
}

void InstallCursorApiLogging()
{
    HMODULE user32 = GetModuleHandleA("user32.dll");
    g_unhookedSetCursorPos = (SetCursorPosFn)(void*)GetProcAddress(user32, "SetCursorPos");
    g_unhookedClipCursor   = (ClipCursorFn)(void*)GetProcAddress(user32, "ClipCursor");
    HMODULE callers[2] = {g_originalDll, GetModuleHandleA(NULL)};
    for (int i = 0; i < 2; i++) {
        PatchImport(callers[i], "USER32.dll", "SetCursorPos", (void*)HookSetCursorPos);
        PatchImport(callers[i], "USER32.dll", "ClipCursor", (void*)HookClipCursor);
    }
}
