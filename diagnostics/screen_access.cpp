// Times the render driver's LockScreen / UnlockScreen and BlitFromScreen, the only two ways the
// engine reaches the pixels of the screen, and logs who asked for them. The hooks sit in
// lithtech.exe's table of driver functions (filled by d3d.ren, read through the pointer at
// 0x00494A6C) and only measure: every call goes on to the driver unchanged.
#include <stdio.h>
#include <string.h>
#include "diagnostics/screen_access.h"
#include "diagnostics/diagnostics.h"
#include "rendering/driver_state.h"
#include "core/addresses.h"
#include "core/cshell_hud.h"
#include "core/log.h"
#include "core/patching.h"
#include "settings/config.h"

typedef int  (__cdecl *LockScreenFn)(int left, int top, int right, int bottom, void** bits, long* pitch);
typedef void (__cdecl *UnlockScreenFn)();
typedef void (__cdecl *BlitFromScreenFn)(void* request);

static LockScreenFn     g_driverLockScreen = NULL;
static UnlockScreenFn   g_driverUnlockScreen = NULL;
static BlitFromScreenFn g_driverBlitFromScreen = NULL;
typedef void* (__cdecl *CreateSurfaceFn)(int w, int h);
typedef int   (__cdecl *OptimizeSurfaceFn)(void* surface, DWORD transparentColor);
typedef void  (__cdecl *BlitToScreenFn)(void* request);
static CreateSurfaceFn   g_driverCreateSurface = NULL;
static OptimizeSurfaceFn g_driverOptimizeSurface = NULL;
static BlitToScreenFn    g_driverBlitToScreen = NULL;
static void DescribeDriverSurface(const void* surface, char* out, int size);

static DWORD GetImageEnd(DWORD base)
{
    BYTE* image = (BYTE*)base;
    return base + ((IMAGE_NT_HEADERS*)(image + ((IMAGE_DOS_HEADER*)image)->e_lfanew))->OptionalHeader.SizeOfImage;
}

// --- who asked -------------------------------------------------------------------------------------

// The address right after a call instruction: E8 rel32, or FF /2 (call [mem] / call reg).
static bool FollowsCall(DWORD address)
{
    const BYTE* b = (const BYTE*)address;
    if (IsBadReadPtr(b - 6, 6)) return false;
    bool callRelative = b[-5] == 0xE8;
    bool callMemory32 = b[-6] == 0xFF && (b[-5] & 0x38) == 0x10;   // FF 15 / FF 9x disp32
    bool callMemory8  = b[-3] == 0xFF && (b[-2] & 0x38) == 0x10;   // FF 5x disp8
    bool callRegister = b[-2] == 0xFF && (b[-1] & 0xF8) == 0xD0;   // FF D0..D7
    return callRelative || callMemory32 || callMemory8 || callRegister;
}

static const int CHAIN_LENGTH = 6;
struct CallChain { DWORD addresses[CHAIN_LENGTH]; };

// The first return addresses on the stack that lead back into lithtech.exe or the original
// cshell: the engine function that locked the screen, then the game code that called it.
static CallChain GetCallChain(const DWORD* stack)
{
    DWORD exeStart = (DWORD)GetModuleHandleA(NULL), exeEnd = GetImageEnd(exeStart);
    DWORD shellStart = g_originalBase, shellEnd = LoadedAddress(VA_VTABLES_START);
    CallChain chain;
    memset(&chain, 0, sizeof(chain));
    int found = 0;
    for (int i = 0; i < 1024 && found < CHAIN_LENGTH; i++) {
        if (IsBadReadPtr(stack + i, 4)) break;
        DWORD value = stack[i];
        bool inExe = value > exeStart && value < exeEnd;
        bool inShell = value > shellStart && value < shellEnd;
        if ((inExe || inShell) && FollowsCall(value)) chain.addresses[found++] = value;
    }
    return chain;
}

// --- statistics ----------------------------------------------------------------------------------

// ACCESS_OUTSIDE_SCENE: a game draw onto the screen while d3d.ren is not drawing 2D through
// Direct3D (outside Start3D / StartOptimized2D), so it goes as a DirectDraw blit.
// ACCESS_MOVED_INTO_SCENE: such a draw that ScopedSceneDraw moved into the Direct3D 2D mode.
enum ScreenAccess { ACCESS_LOCK, ACCESS_COPY, ACCESS_OUTSIDE_SCENE, ACCESS_MOVED_INTO_SCENE, ACCESS_KIND_COUNT };
static const char* ACCESS_NAMES[ACCESS_KIND_COUNT] = {"lock", "copy", "outside-scene draw", "moved-into-scene draw"};

struct AccessSite {
    int       access;
    CallChain chain;
    int       w, h;          // the area of the screen asked for
    int       calls;
    double    ms, maxMs;
};
static const int MAX_SITES = 32;
static AccessSite g_sites[MAX_SITES];
static int        g_siteCount = 0;
static DWORD      g_periodStart = 0;
static int        g_lockSite = -1;          // the lock now held, waiting for its unlock
static double     g_lockMs = 0.0;           // ... and what its LockScreen took

static int FindSite(int access, const CallChain& chain, int w, int h)
{
    for (int i = 0; i < g_siteCount; i++) {
        const AccessSite& site = g_sites[i];
        if (site.access == access && site.w == w && site.h == h && memcmp(&site.chain, &chain, sizeof(chain)) == 0)
            return i;
    }
    if (g_siteCount == MAX_SITES) return -1;
    AccessSite& site = g_sites[g_siteCount];
    memset(&site, 0, sizeof(site));
    site.access = access;
    site.chain = chain;
    site.w = w;
    site.h = h;
    return g_siteCount++;
}

static void AddSample(int index, double ms)
{
    if (index < 0) return;
    AccessSite& site = g_sites[index];
    site.calls++;
    site.ms += ms;
    if (ms > site.maxMs) site.maxMs = ms;
}

// " addr (where) < addr (where) ..." appended to line
static void AppendChain(const CallChain& chain, char* line, int size)
{
    int length = (int)strlen(line);
    for (int i = 0; i < CHAIN_LENGTH && chain.addresses[i] != 0 && length >= 0 && length < size; i++) {
        char where[160];
        DescribeAddress(chain.addresses[i], where, sizeof(where));
        length += _snprintf(line + length, size - length, "%s %s", i == 0 ? "" : " <", where);
    }
    line[size - 1] = '\0';
}

static void LogSite(const AccessSite& site, double seconds)
{
    char line[1280];
    _snprintf(line, sizeof(line), "     %s %dx%d: %d/s, %.1f ms/s, max %.2f ms -",
              ACCESS_NAMES[site.access], site.w, site.h,
              (int)(site.calls / seconds + 0.5), site.ms / seconds, site.maxMs);
    line[sizeof(line) - 1] = '\0';
    AppendChain(site.chain, line, sizeof(line));
    Log("%s", line);
}

void LogSlowCall(const char* what)
{
    DWORD stackTop = 0;
    CallChain chain = GetCallChain(&stackTop);
    char line[1280];
    _snprintf(line, sizeof(line), "slow: %s -", what);
    line[sizeof(line) - 1] = '\0';
    AppendChain(chain, line, sizeof(line));
    Log("%s", line);
}

// Once a second, when the screen was touched at all: totals, then the costliest call chains.
static void LogPeriodIfDue()
{
    DWORD now = GetTickCount();
    if (g_periodStart == 0) g_periodStart = now;
    if (now - g_periodStart < 1000 || g_lockSite >= 0) return;
    double seconds = (now - g_periodStart) / 1000.0;
    g_periodStart = now;
    if (g_siteCount == 0) return;

    int calls[ACCESS_KIND_COUNT] = {0, 0, 0, 0};
    double ms[ACCESS_KIND_COUNT] = {0.0, 0.0, 0.0, 0.0};
    for (int i = 0; i < g_siteCount; i++) {
        calls[g_sites[i].access] += g_sites[i].calls;
        ms[g_sites[i].access] += g_sites[i].ms;
    }
    Log("screen access (per second): locks %d (%.1f ms), copies from the screen %d (%.1f ms), "
        "draws outside the scene %d (%.1f ms), moved into it %d (%.1f ms)",
        (int)(calls[ACCESS_LOCK] / seconds + 0.5), ms[ACCESS_LOCK] / seconds,
        (int)(calls[ACCESS_COPY] / seconds + 0.5), ms[ACCESS_COPY] / seconds,
        (int)(calls[ACCESS_OUTSIDE_SCENE] / seconds + 0.5), ms[ACCESS_OUTSIDE_SCENE] / seconds,
        (int)(calls[ACCESS_MOVED_INTO_SCENE] / seconds + 0.5), ms[ACCESS_MOVED_INTO_SCENE] / seconds);
    for (int listed = 0; listed < 8; listed++) {
        int most = -1;
        for (int i = 0; i < g_siteCount; i++)
            if (g_sites[i].calls > 0 && (most < 0 || g_sites[i].ms > g_sites[most].ms)) most = i;
        if (most < 0) break;
        LogSite(g_sites[most], seconds);
        g_sites[most].calls = 0;          // listed
    }
    g_siteCount = 0;
}

// --- the hooks -----------------------------------------------------------------------------------

// The saved driver function still belongs to a loaded module (the renderer can be reloaded).
static bool IsLoaded(void* function)
{
    HMODULE module = NULL;
    return function != NULL
        && GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                              (LPCSTR)function, &module) != 0;
}

static int __cdecl HookLockScreen(int left, int top, int right, int bottom, void** bits, long* pitch)
{
    if (!IsLoaded((void*)g_driverLockScreen)) return 0;      // the engine treats 0 as "lock failed"
    DWORD stackTop = 0;              // the scan starts at this frame and goes up the stack
    CallChain chain = GetCallChain(&stackTop);
    double start = GetMilliseconds();
    int result = g_driverLockScreen(left, top, right, bottom, bits, pitch);
    g_lockMs = GetMilliseconds() - start;
    g_lockSite = result != 0 ? FindSite(ACCESS_LOCK, chain, right - left, bottom - top) : -1;
    return result;
}

// The driver copies the back buffer back to the card here: the unlock is counted with its lock.
static void __cdecl HookUnlockScreen()
{
    if (!IsLoaded((void*)g_driverUnlockScreen)) return;
    double start = GetMilliseconds();
    g_driverUnlockScreen();
    double ms = GetMilliseconds() - start;
    int site = g_lockSite;
    g_lockSite = -1;
    AddSample(site, g_lockMs + ms);
    LogPeriodIfDue();
}

// The request's source rect (left, top, right, bottom on the screen) is at +0x0C.
static void __cdecl HookBlitFromScreen(void* request)
{
    if (!IsLoaded((void*)g_driverBlitFromScreen)) return;
    int w = 0, h = 0;
    const int* const* sourceRect = request != NULL ? (const int* const*)((char*)request + 0x0C) : NULL;
    if (sourceRect != NULL && !IsBadReadPtr(sourceRect, 4) && *sourceRect != NULL && !IsBadReadPtr(*sourceRect, 16)) {
        w = (*sourceRect)[2] - (*sourceRect)[0];
        h = (*sourceRect)[3] - (*sourceRect)[1];
    }
    DWORD stackTop = 0;
    CallChain chain = GetCallChain(&stackTop);
    double start = GetMilliseconds();
    g_driverBlitFromScreen(request);
    AddSample(FindSite(ACCESS_COPY, chain, w, h), GetMilliseconds() - start);
    LogPeriodIfDue();
}

void NoteDrawOutsideScene(void* engineSurface, double ms, bool moved)
{
    int w = 0, h = 0;
    const BYTE* surface = (const BYTE*)engineSurface;
    if (surface != NULL && !IsBadReadPtr(surface, ENGINE_SURFACE_DRIVER + 4)) {
        char size[32];
        DescribeDriverSurface(*(const void* const*)(surface + ENGINE_SURFACE_DRIVER), size, sizeof(size));
        sscanf(size, "%dx%d", &w, &h);
    }
    DWORD stackTop = 0;
    int access = moved ? ACCESS_MOVED_INTO_SCENE : ACCESS_OUTSIDE_SCENE;
    AddSample(FindSite(access, GetCallChain(&stackTop), w, h), ms);
    LogPeriodIfDue();
}

// --- slow driver calls -----------------------------------------------------------------------------

// Making surfaces and their textures, and drawing a surface onto the screen: each one slower than
// this is logged with its call chain (they happen once per new picture, so there is no summary).
static const double SLOW_DRIVER_CALL_MS = 5.0;


// "w x h" of a d3d.ren surface, "?" when it can't be read.
static void DescribeDriverSurface(const void* surface, char* out, int size)
{
    const BYTE* s = (const BYTE*)surface;
    if (s != NULL && !IsBadReadPtr(s, D3D_SURFACE_WIDTH + 4))
        _snprintf(out, size, "%dx%d", *(const int*)(s + D3D_SURFACE_WIDTH), *(const int*)(s + D3D_SURFACE_HEIGHT));
    else
        _snprintf(out, size, "?");
    out[size - 1] = '\0';
}

static void* __cdecl HookCreateSurface(int w, int h)
{
    if (!IsLoaded((void*)g_driverCreateSurface)) return NULL;
    double start = GetMilliseconds();
    void* surface = g_driverCreateSurface(w, h);
    double ms = GetMilliseconds() - start;
    if (ms >= SLOW_DRIVER_CALL_MS) {
        char what[96];
        _snprintf(what, sizeof(what), "driver CreateSurface %dx%d %.2f ms", w, h, ms);
        what[sizeof(what) - 1] = '\0';
        LogSlowCall(what);
    }
    return surface;
}

static int __cdecl HookOptimizeSurface(void* surface, DWORD transparentColor)
{
    if (!IsLoaded((void*)g_driverOptimizeSurface)) return 0;
    double start = GetMilliseconds();
    int result = g_driverOptimizeSurface(surface, transparentColor);
    double ms = GetMilliseconds() - start;
    if (ms >= SLOW_DRIVER_CALL_MS) {
        char size[32], what[128];
        DescribeDriverSurface(surface, size, sizeof(size));
        _snprintf(what, sizeof(what), "driver OptimizeSurface %s %.2f ms (result %d)", size, ms, result);
        what[sizeof(what) - 1] = '\0';
        LogSlowCall(what);
    }
    return result;
}

// The request's first field is the driver surface to draw.
static void __cdecl HookBlitToScreen(void* request)
{
    if (!IsLoaded((void*)g_driverBlitToScreen)) return;
    double start = GetMilliseconds();
    g_driverBlitToScreen(request);
    double ms = GetMilliseconds() - start;
    if (ms >= SLOW_DRIVER_CALL_MS) {
        const void* surface = request != NULL && !IsBadReadPtr(request, 4) ? *(const void* const*)request : NULL;
        char size[32], what[128];
        DescribeDriverSurface(surface, size, sizeof(size));
        _snprintf(what, sizeof(what), "driver BlitToScreen %s %.2f ms", size, ms);
        what[sizeof(what) - 1] = '\0';
        LogSlowCall(what);
    }
}

// --- DirectX calls made by the driver ---------------------------------------------------------------

// The DirectDraw / Direct3D methods d3d.ren draws a frame with, hooked in their COM vtables (shared
// by every object of the class). Calls slower than SLOW_DIRECTX_CALL_MS are logged with their call
// chain; the time spent in each method is summed and written once a second with the other totals.
static const double SLOW_DIRECTX_CALL_MS = 3.0;

enum DirectXMethod {
    DX_BEGIN_SCENE, DX_END_SCENE, DX_DRAW_PRIMITIVE, DX_SET_TEXTURE,
    DX_SURFACE_BLT, DX_SURFACE_LOCK, DX_SURFACE_UNLOCK, DX_SURFACE_FLIP, DX_CREATE_SURFACE,
    DX_METHOD_COUNT
};
static const char* DX_NAMES[DX_METHOD_COUNT] = {
    "BeginScene", "EndScene", "DrawPrimitive", "SetTexture", "Blt", "Lock", "Unlock", "Flip", "CreateSurface"
};
static const int DEVICE_BEGIN_SCENE = 0x14, DEVICE_END_SCENE = 0x18, DEVICE_DRAW_PRIMITIVE = 0x64,
                 DEVICE_SET_TEXTURE = 0x8C;                       // IDirect3DDevice7
static const int SURFACE_BLT = 0x14, SURFACE_FLIP = 0x2C, SURFACE_LOCK = 0x64, SURFACE_UNLOCK = 0x80;   // IDirectDrawSurface7
static const int DIRECTDRAW_CREATE_SURFACE = 0x18;                // IDirectDraw7

static void* g_dxOriginal[DX_METHOD_COUNT];
static int    g_dxCalls[DX_METHOD_COUNT];
static double g_dxMs[DX_METHOD_COUNT];
static DWORD  g_dxPeriodStart = 0;

static void NoteDirectXCall(int method, double start)
{
    double ms = GetMilliseconds() - start;
    g_dxCalls[method]++;
    g_dxMs[method] += ms;
    // Flip waits for the screen refresh every frame by design: only its total is of interest.
    if (ms >= SLOW_DIRECTX_CALL_MS && method != DX_SURFACE_FLIP) {
        char what[96];
        _snprintf(what, sizeof(what), "DirectX %s %.2f ms", DX_NAMES[method], ms);
        what[sizeof(what) - 1] = '\0';
        LogSlowCall(what);
    }

    DWORD now = GetTickCount();
    if (g_dxPeriodStart == 0) g_dxPeriodStart = now;
    if (now - g_dxPeriodStart < 1000) return;
    double seconds = (now - g_dxPeriodStart) / 1000.0;
    g_dxPeriodStart = now;
    char line[640];
    int length = _snprintf(line, sizeof(line), "directx (per second):");
    for (int i = 0; i < DX_METHOD_COUNT && length > 0 && length < (int)sizeof(line); i++)
        length += _snprintf(line + length, sizeof(line) - length, "  %s %d (%.1f ms)", DX_NAMES[i],
                            (int)(g_dxCalls[i] / seconds + 0.5), g_dxMs[i] / seconds);
    line[sizeof(line) - 1] = '\0';
    Log("%s", line);
    memset(g_dxCalls, 0, sizeof(g_dxCalls));
    memset(g_dxMs, 0, sizeof(g_dxMs));
}

typedef HRESULT (__stdcall *Method0Fn)(void* self);
typedef HRESULT (__stdcall *Method1Fn)(void* self, DWORD a);
typedef HRESULT (__stdcall *Method2Fn)(void* self, DWORD a, DWORD b);
typedef HRESULT (__stdcall *Method3Fn)(void* self, DWORD a, DWORD b, DWORD c);
typedef HRESULT (__stdcall *Method4Fn)(void* self, DWORD a, DWORD b, DWORD c, DWORD d);
typedef HRESULT (__stdcall *Method5Fn)(void* self, DWORD a, DWORD b, DWORD c, DWORD d, DWORD e);

static HRESULT __stdcall HookBeginScene(void* self)
{
    double start = GetMilliseconds();
    HRESULT result = ((Method0Fn)g_dxOriginal[DX_BEGIN_SCENE])(self);
    NoteDirectXCall(DX_BEGIN_SCENE, start);
    return result;
}

static HRESULT __stdcall HookEndScene(void* self)
{
    double start = GetMilliseconds();
    HRESULT result = ((Method0Fn)g_dxOriginal[DX_END_SCENE])(self);
    NoteDirectXCall(DX_END_SCENE, start);
    return result;
}

static HRESULT __stdcall HookDrawPrimitive(void* self, DWORD type, DWORD format, DWORD vertices, DWORD count, DWORD flags)
{
    double start = GetMilliseconds();
    HRESULT result = ((Method5Fn)g_dxOriginal[DX_DRAW_PRIMITIVE])(self, type, format, vertices, count, flags);
    NoteDirectXCall(DX_DRAW_PRIMITIVE, start);
    return result;
}

static HRESULT __stdcall HookSetTexture(void* self, DWORD stage, DWORD texture)
{
    double start = GetMilliseconds();
    HRESULT result = ((Method2Fn)g_dxOriginal[DX_SET_TEXTURE])(self, stage, texture);
    NoteDirectXCall(DX_SET_TEXTURE, start);
    return result;
}

static HRESULT __stdcall HookSurfaceBlt(void* self, DWORD dstRect, DWORD src, DWORD srcRect, DWORD flags, DWORD effects)
{
    double start = GetMilliseconds();
    HRESULT result = ((Method5Fn)g_dxOriginal[DX_SURFACE_BLT])(self, dstRect, src, srcRect, flags, effects);
    NoteDirectXCall(DX_SURFACE_BLT, start);
    return result;
}

static HRESULT __stdcall HookSurfaceLock(void* self, DWORD rect, DWORD description, DWORD flags, DWORD event)
{
    double start = GetMilliseconds();
    HRESULT result = ((Method4Fn)g_dxOriginal[DX_SURFACE_LOCK])(self, rect, description, flags, event);
    NoteDirectXCall(DX_SURFACE_LOCK, start);
    return result;
}

static HRESULT __stdcall HookSurfaceUnlock(void* self, DWORD rect)
{
    double start = GetMilliseconds();
    HRESULT result = ((Method1Fn)g_dxOriginal[DX_SURFACE_UNLOCK])(self, rect);
    NoteDirectXCall(DX_SURFACE_UNLOCK, start);
    return result;
}

static HRESULT __stdcall HookSurfaceFlip(void* self, DWORD target, DWORD flags)
{
    double start = GetMilliseconds();
    HRESULT result = ((Method2Fn)g_dxOriginal[DX_SURFACE_FLIP])(self, target, flags);
    NoteDirectXCall(DX_SURFACE_FLIP, start);
    return result;
}

static HRESULT __stdcall HookCreateDirectDrawSurface(void* self, DWORD description, DWORD surface, DWORD outer)
{
    double start = GetMilliseconds();
    HRESULT result = ((Method3Fn)g_dxOriginal[DX_CREATE_SURFACE])(self, description, surface, outer);
    NoteDirectXCall(DX_CREATE_SURFACE, start);
    return result;
}

// One slot of a COM object's vtable; the first original seen is kept (the vtables don't change).
static void HookComSlot(void* object, int slot, void* hook, int method)
{
    if (object == NULL || IsBadReadPtr(object, 4)) return;
    void** vtable = *(void***)object;
    if (IsBadReadPtr(vtable + slot / 4, 4) || vtable[slot / 4] == hook) return;
    g_dxOriginal[method] = vtable[slot / 4];
    PatchPointer((DWORD)(vtable + slot / 4), hook);
}

// The device, DirectDraw and the back buffer of the original d3d.ren (surfaces share one vtable).
static void UpdateDirectXHooks()
{
    DWORD base = GetOriginalDriverBase();
    if (base == 0) return;
    void* device = *(void**)(base + D3D_DEVICE_PTR);
    void* directDraw = *(void**)(base + D3D_DIRECTDRAW_PTR);
    void* backBuffer = *(void**)(base + D3D_BACK_BUFFER_PTR);
    if (device == NULL || directDraw == NULL || backBuffer == NULL) return;
    HookComSlot(device, DEVICE_BEGIN_SCENE, (void*)HookBeginScene, DX_BEGIN_SCENE);
    HookComSlot(device, DEVICE_END_SCENE, (void*)HookEndScene, DX_END_SCENE);
    HookComSlot(device, DEVICE_DRAW_PRIMITIVE, (void*)HookDrawPrimitive, DX_DRAW_PRIMITIVE);
    HookComSlot(device, DEVICE_SET_TEXTURE, (void*)HookSetTexture, DX_SET_TEXTURE);
    HookComSlot(backBuffer, SURFACE_BLT, (void*)HookSurfaceBlt, DX_SURFACE_BLT);
    HookComSlot(backBuffer, SURFACE_LOCK, (void*)HookSurfaceLock, DX_SURFACE_LOCK);
    HookComSlot(backBuffer, SURFACE_UNLOCK, (void*)HookSurfaceUnlock, DX_SURFACE_UNLOCK);
    HookComSlot(backBuffer, SURFACE_FLIP, (void*)HookSurfaceFlip, DX_SURFACE_FLIP);
    HookComSlot(directDraw, DIRECTDRAW_CREATE_SURFACE, (void*)HookCreateDirectDrawSurface, DX_CREATE_SURFACE);
}

// --- installing ----------------------------------------------------------------------------------

// Puts the hook into one slot of the driver table, saving what the driver put there.
static void HookSlot(BYTE* table, int slot, void* hook, void** driverFunction)
{
    void* current = *(void**)(table + slot);
    if (current == hook || current == NULL) return;
    *driverFunction = current;
    PatchPointer((DWORD)(table + slot), hook);
}

void UpdateScreenAccessHooks()
{
    if (!g_config.perf) return;
    static bool announced = false;
    if (!announced && GetDriverTable() != NULL) {
        announced = true;
        Log("perf: timing screen locks and copies from the screen, and how surfaces are drawn");
    }
    BYTE* table = GetDriverTable();
    if (table == NULL) return;
    HookSlot(table, RENDER_LOCK_SCREEN, (void*)HookLockScreen, (void**)&g_driverLockScreen);
    HookSlot(table, RENDER_UNLOCK_SCREEN, (void*)HookUnlockScreen, (void**)&g_driverUnlockScreen);
    HookSlot(table, RENDER_BLIT_FROM_SCREEN, (void*)HookBlitFromScreen, (void**)&g_driverBlitFromScreen);
    HookSlot(table, RENDER_CREATE_SURFACE, (void*)HookCreateSurface, (void**)&g_driverCreateSurface);
    HookSlot(table, RENDER_OPTIMIZE_SURFACE, (void*)HookOptimizeSurface, (void**)&g_driverOptimizeSurface);
    HookSlot(table, RENDER_BLIT_TO_SCREEN, (void*)HookBlitToScreen, (void**)&g_driverBlitToScreen);
    UpdateDirectXHooks();
}
