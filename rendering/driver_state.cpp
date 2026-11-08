// Reads lithtech.exe's driver table and the original d3d.ren's state, and moves game draws made
// outside d3d.ren's Direct3D 2D mode into it (driver_state.h says why).
#include <string.h>
#include "rendering/driver_state.h"
#include "core/addresses.h"
#include "core/cshell_hud.h"
#include "core/log.h"
#include "settings/config.h"

DWORD g_drawsMovedIntoScene = 0;

static DWORD ExeAddress(DWORD linked) { return (DWORD)GetModuleHandleA(NULL) + (linked - EXE_IMAGE_BASE); }

// mov eax,[494A6Ch] ... call [eax+0E4h] in the engine's LockSurface, mov eax,[49DE3Ch] in its
// surface-to-surface draw: the table pointer and the table are where addresses.h says.
bool HasExpectedDriverTableCode()
{
    return memcmp((void*)ExeAddress(EXE_LOCK_SURFACE_TABLE_LOAD), "\xA1\x6C\x4A\x49\x00", 5) == 0
        && memcmp((void*)ExeAddress(EXE_LOCK_SURFACE_SCREEN_CALL), "\xFF\x90\xE4\x00\x00\x00", 6) == 0
        && memcmp((void*)ExeAddress(EXE_BLIT_FROM_SCREEN_LOAD), "\xA1\x3C\xDE\x49\x00", 5) == 0;
}

// Checked once, logged once.
static bool IsExpectedExe()
{
    static int expected = -1;        // -1 = not checked yet
    if (expected < 0) {
        expected = HasExpectedDriverTableCode() ? 1 : 0;
        if (!expected) Log("render driver: lithtech.exe is not the expected version - driver state unknown");
    }
    return expected == 1;
}

BYTE* GetDriverTable() { return IsExpectedExe() ? *(BYTE**)ExeAddress(EXE_RENDER_TABLE_PTR) : NULL; }

// The original's BlitToScreen at base: sub esp,0Ch; mov eax,[back buffer] ... mov edx,[textured
// 2D] ... mov edx,[in scene], with the globals at their places in the original.
static bool IsOriginalDriverAt(DWORD base)
{
    const BYTE* code = (const BYTE*)(base + D3D_BLIT_TO_SCREEN);
    if (IsBadReadPtr(code, 0x34)) return false;
    return memcmp(code, "\x83\xEC\x0C\xA1", 4) == 0 && *(const DWORD*)(code + 0x04) == base + D3D_BACK_BUFFER_PTR
        && code[0x24] == 0x8B && code[0x25] == 0x15 && *(const DWORD*)(code + 0x26) == base + D3D_TEXTURED_2D
        && code[0x2E] == 0x8B && code[0x2F] == 0x15 && *(const DWORD*)(code + 0x30) == base + D3D_IN_SCENE;
}

static bool IsLoadedModule(DWORD base)
{
    HMODULE module = NULL;
    return GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                              (LPCSTR)base, &module) != 0 && (DWORD)module == base;
}

// Found through the BlitToScreen slot of the table; the base is kept, since the [HUD] perf
// diagnostics put their own function into that slot.
DWORD GetOriginalDriverBase()
{
    static DWORD knownBase = 0;
    BYTE* table = GetDriverTable();
    if (table == NULL) return 0;
    DWORD slotBase = *(const DWORD*)(table + RENDER_BLIT_TO_SCREEN) - D3D_BLIT_TO_SCREEN;
    if (slotBase != knownBase && IsOriginalDriverAt(slotBase))
        knownBase = slotBase;
    else if (knownBase != 0 && !(IsLoadedModule(knownBase) && IsOriginalDriverAt(knownBase)))
        knownBase = 0;
    return knownBase;
}

int GetDrawState(void* engineSurface)
{
    DWORD base = GetOriginalDriverBase();
    if (base == 0 || engineSurface == NULL || IsBadReadPtr(engineSurface, ENGINE_SURFACE_FLAGS + 4)) return 0;

    int state = DRAW_KNOWN;
    const BYTE* driverSurface = *(const BYTE* const*)((const BYTE*)engineSurface + ENGINE_SURFACE_DRIVER);
    DWORD flags = *(const DWORD*)((const BYTE*)engineSurface + ENGINE_SURFACE_FLAGS);
    if (driverSurface != NULL && !IsBadReadPtr(driverSurface + D3D_SURFACE_TEXTURES, 4)
        && *(const DWORD*)(driverSurface + D3D_SURFACE_TEXTURES) != 0)
        state |= DRAW_TEXTURE;
    if ((flags & 2) != 0 && (flags & 4) != 0) state |= DRAW_REUPLOAD;
    if (*(const DWORD*)(base + D3D_TEXTURED_2D) != 0) state |= DRAW_TEXTURED_2D;
    if (*(const DWORD*)(base + D3D_IN_SCENE) != 0) state |= DRAW_IN_SCENE;
    return state;
}

// --- moving a draw into the Direct3D 2D mode -----------------------------------------------------

// The engine's own Start3D / StartOptimized2D / EndOptimized2D / End3D: no arguments, LT_OK = 0.
typedef int (__cdecl *ClientCallFn)();
static bool CallClient(int slot) { return GetLtClientFunction<ClientCallFn>(slot)() == 0; }

ScopedSceneDraw::ScopedSceneDraw(void* src)
{
    started3d = false;
    startedOptimized2d = false;
    if (!g_config.sceneDraws || GetLtClient() == NULL) return;
    int state = GetDrawState(src);
    bool canMove = (state & DRAW_KNOWN) != 0 && (state & DRAW_TEXTURE) != 0 && (state & DRAW_TEXTURED_2D) == 0;
    if (!canMove) return;

    if ((state & DRAW_IN_SCENE) == 0) {
        if (!CallClient(LTCLIENT_START_3D)) return;
        started3d = true;
    }
    startedOptimized2d = CallClient(LTCLIENT_START_OPTIMIZED_2D);
    if (startedOptimized2d) g_drawsMovedIntoScene++;
}

ScopedSceneDraw::~ScopedSceneDraw()
{
    if (startedOptimized2d) CallClient(LTCLIENT_END_OPTIMIZED_2D);
    if (started3d) CallClient(LTCLIENT_END_3D);
}
