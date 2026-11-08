// cshell_hud - Might & Magic IX interface scaling, a wrapper for cshell.dll (v1.2).
//
// The engine loads "cshell.dll" (this dll). It loads the untouched original (renamed to
// cshell_orig.dll), forwards its 3 exports, and patches pointers and a few code spots in the
// original's memory:
//   * The 2D render object reports an 800x600 screen to the game, so every widget, dialog and
//     menu lays itself out exactly as at 800x600 (no centring offsets, no 800x600 clamps).
//   * Every 2D draw onto the screen is mapped from that 800x600 space to the real screen:
//       - the HUD: each piece sticks to its nearest screen edge or corner, scaled by [HUD] size
//         (1.0 = real pixels .. max = as large as fits; Left Ctrl + wheel changes it live)
//       - dialogue, combat history, real-time combat log: whole windows anchored the same way
//         and scaled with the HUD size; mouse input over them follows
//       - everything else (dialogs, popups, menus, cursor): one 4:3 block scaled by [UI] scale
//   * Mouse points are converted back into the 800x600 space; points going to the HUD are
//     converted again for its zone layout.
//   * Pictures the game draws outside the renderer's Direct3D 2D mode (the item popup, between
//     frames) are drawn inside it: without a DirectDraw wrapper each such draw stalled the frame.
//   * 3D view: cutscene rects follow the real screen, the FOV is corrected for wide screens
//     ([FOV] mode, hor+ by default), Bink videos keep their aspect ratio.
//   * Options -> Game Options gets HUD Scale and FOV sliders and an FOV Type list; Options ->
//     Video Options gets a Resolution list.
//
// Build: run build.bat in an x86 Developer Command Prompt (map.md lists the modules).
//
// Install, in the folder holding the game's cshell.dll (next to lithtech.exe or in data\):
// rename cshell.dll to cshell_orig.dll, copy this cshell.dll and cshell_hud.ini there.
// The log is cshell_hud.log in the same folder.
#include <stdio.h>
#include <string.h>
#include "core/cshell_hud.h"
#include "settings/config.h"
#include "diagnostics/diagnostics.h"
#include "core/install.h"
#include "core/log.h"

HMODULE g_originalDll = NULL;
DWORD   g_originalBase = 0;
static char g_fixFolder[MAX_PATH];     // with a trailing backslash

void GetFixFilePath(const char* fileName, char* out)
{
    _snprintf(out, MAX_PATH, "%s%s", g_fixFolder, fileName);
    out[MAX_PATH - 1] = '\0';
}

typedef void (*GetClientShellFunctionsFn)(void* create, void* destroy);
typedef int  (*GetClientShellVersionFn)();
typedef void (*SetInstanceHandleFn)(HINSTANCE instance);
static GetClientShellFunctionsFn g_originalGetFunctions;
static GetClientShellVersionFn   g_originalGetVersion;
static SetInstanceHandleFn       g_originalSetInstance;

// The engine loads a temporary copy of cshell.dll, so this dll's own path says nothing. The
// original is looked for next to lithtech.exe, then in its data folder; the ini and the log live
// in the folder where it was found.
static bool FindAndLoadOriginal(char* path)
{
    char exeFolder[MAX_PATH];
    GetModuleFileNameA(NULL, exeFolder, MAX_PATH);
    char* slash = strrchr(exeFolder, '\\');
    if (slash != NULL)
        slash[1] = '\0';
    else
        exeFolder[0] = '\0';

    static const char* SUBFOLDERS[3] = {"", "data\\", "Data\\"};
    for (int i = 0; i < 3 && g_originalDll == NULL; i++) {
        _snprintf(g_fixFolder, MAX_PATH, "%s%s", exeFolder, SUBFOLDERS[i]);
        g_fixFolder[MAX_PATH - 1] = '\0';
        GetFixFilePath("cshell_orig.dll", path);
        if (GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES) g_originalDll = LoadLibraryA(path);
    }
    return g_originalDll != NULL;
}

// Loads and patches the original on the first call into any export.
static bool EnsureLoaded()
{
    if (g_originalDll != NULL)
        return true;

    char path[MAX_PATH];
    if (!FindAndLoadOriginal(path)) {
        MessageBoxA(NULL, "cshell_orig.dll not found next to lithtech.exe or in its data folder.\n"
                          "Rename the original cshell.dll to cshell_orig.dll.", "MM9 interface fix", MB_ICONERROR);
        return false;
    }
    g_originalBase = (DWORD)g_originalDll;
    g_originalGetFunctions = (GetClientShellFunctionsFn)(void*)GetProcAddress(g_originalDll, "GetClientShellFunctions");
    g_originalGetVersion   = (GetClientShellVersionFn)(void*)GetProcAddress(g_originalDll, "GetClientShellVersion");
    g_originalSetInstance  = (SetInstanceHandleFn)(void*)GetProcAddress(g_originalDll, "SetInstanceHandle");
    Log("MM9 interface fix v1.2 - loaded %s at %08lX", path, g_originalBase);

    LoadConfig(true);
    InstallCrashLogger();
    InstallHooks();
    return g_originalGetFunctions != NULL && g_originalGetVersion != NULL && g_originalSetInstance != NULL;
}

extern "C" void GetClientShellFunctions(void* create, void* destroy)
{
    if (EnsureLoaded()) g_originalGetFunctions(create, destroy);
}

extern "C" int GetClientShellVersion()
{
    if (EnsureLoaded())
        return g_originalGetVersion();
    else
        return 0;
}

// The original gets its own module handle, so it finds its own resources.
extern "C" void SetInstanceHandle(HINSTANCE)
{
    if (EnsureLoaded()) g_originalSetInstance((HINSTANCE)g_originalDll);
}

BOOL WINAPI DllMain(HINSTANCE, DWORD, LPVOID) { return TRUE; }
