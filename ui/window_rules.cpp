// Patches the Draw (and destructor) of window classes so the right mapping applies however the
// game draws them. A vtable slot gives the hook no hint which rule it serves, so each rule slot
// has its own small function (a template instance) that passes its number on.
#include "ui/window_rules.h"
#include "core/addresses.h"
#include "rendering/black_bars.h"
#include "settings/config.h"
#include "core/cshell_hud.h"
#include "diagnostics/diagnostics.h"
#include "input/input.h"
#include "layout/layout.h"
#include "core/log.h"
#include "core/patching.h"
#include "rendering/render_hooks.h"
#include "rendering/renderer.h"
#include "media/sound.h"

typedef char  (__fastcall *PointFn)(void* self, void* edx, POINT* point);
typedef void* (__fastcall *DestructorFn)(void* self, void* edx, int flags);
typedef Rect* (__fastcall *GetRectFn)(void* self, void* edx, Rect* out);

static const int MAX_RULES = 8;

// --- windows with an alignment rule ------------------------------------------------------------

struct AlignedWindowRule { DWORD windowClass; int align; MethodFn unhookedDraw; DestructorFn unhookedDestructor; };
static AlignedWindowRule g_alignedRules[MAX_RULES];
static int g_alignedRuleCount = 0;

static char DrawAlignedWindow(int rule, void* window)
{
    const AlignedWindowRule& r = g_alignedRules[rule];
    if (!g_enabled)
        return r.unhookedDraw(window, 0);
    if (g_dump.active) Log("  [window %08lX drawn, aligned %s]", r.windowClass, AlignName(r.align));
    ScopedAlignment alignment(r.align);
    NoteAlignedWindowDrawn(window, r.windowClass, r.align);
    return r.unhookedDraw(window, 0);
}

static void* DestroyAlignedWindow(int rule, void* window, int flags)
{
    ForgetAlignedWindow(window);
    return g_alignedRules[rule].unhookedDestructor(window, 0, flags);
}

template <int N> static char  __fastcall HookAlignedDraw(void* window, void*) { return DrawAlignedWindow(N, window); }
template <int N> static void* __fastcall HookAlignedDestructor(void* window, void*, int flags) { return DestroyAlignedWindow(N, window, flags); }

#define RULE_HOOKS(hook) {(void*)hook<0>, (void*)hook<1>, (void*)hook<2>, (void*)hook<3>, \
                          (void*)hook<4>, (void*)hook<5>, (void*)hook<6>, (void*)hook<7>}
static void* const ALIGNED_DRAW_HOOKS[MAX_RULES] = RULE_HOOKS(HookAlignedDraw);
static void* const ALIGNED_DESTRUCTOR_HOOKS[MAX_RULES] = RULE_HOOKS(HookAlignedDestructor);

// --- windows that follow the HUD size (dialogue, combat history, real-time combat log) ---------

struct HudWindowHook { DWORD windowClass; int column, row; MethodFn unhookedDraw; };
static HudWindowHook g_hudWindowHooks[MAX_RULES];
static int g_hudWindowHookCount = 0;

// While the window draws, everything it draws uses its own mapping.
static char DrawHudWindow(int rule, void* window)
{
    const HudWindowHook& r = g_hudWindowHooks[rule];
    if (!g_enabled || g_hudDrawDepth > 0)
        return r.unhookedDraw(window, 0);
    UpdateBeforeDraw();
    Rect rect = MakeRect(0, 0, 800, 600);
    GetMethod<GetRectFn>(window, CONTROL_GET_RECT)(window, 0, &rect);
    Mapping mapping = GetHudWindowMapping(rect, r.column, r.row);
    if (g_dump.active)
        Log("  [window %08lX follows the HUD size: rect (%d,%d %dx%d) scale %.3f at %.0f,%.0f]", r.windowClass,
            rect.x, rect.y, rect.w, rect.h, mapping.scaleX, mapping.offsetX, mapping.offsetY);

    ScopedDrawMapping drawMapping(mapping);
    ScopedHudWindowDraw collectArea(window, mapping);
    return r.unhookedDraw(window, 0);
}

template <int N> static char __fastcall HookHudWindowDraw(void* window, void*) { return DrawHudWindow(N, window); }
static void* const HUD_WINDOW_DRAW_HOOKS[MAX_RULES] = RULE_HOOKS(HookHudWindowDraw);

// A vtable of the original whose Draw is in the original's code.
static bool IsWindowClass(DWORD windowClass)
{
    if (windowClass < VA_VTABLES_START || windowClass >= VA_VTABLES_END)
        return false;
    DWORD draw = *(DWORD*)(LoadedAddress(windowClass) + CONTROL_DRAW);
    return draw >= LoadedAddress(VA_CODE_START) && draw < LoadedAddress(VA_VTABLES_START);
}

static DWORD GetDrawFunction(DWORD windowClass) { return LinkedAddress(*(DWORD*)(LoadedAddress(windowClass) + CONTROL_DRAW)); }

static void InstallAlignedWindows()
{
    for (int i = 0; i < g_config.alignRuleCount && g_alignedRuleCount < MAX_RULES; i++) {
        DWORD windowClass = g_config.alignRules[i].windowClass;
        if (!IsWindowClass(windowClass)) {
            Log("align: %08lX is not a window class ID - ignored", windowClass);
            continue;
        }
        int n = g_alignedRuleCount++;
        AlignedWindowRule& rule = g_alignedRules[n];
        rule.windowClass = windowClass;
        rule.align = g_config.alignRules[i].align;
        Log("align: window %08lX (draw %08lX) patched", windowClass, GetDrawFunction(windowClass));
        rule.unhookedDraw = (MethodFn)PatchVtableSlot(windowClass, CONTROL_DRAW, ALIGNED_DRAW_HOOKS[n]);
        rule.unhookedDestructor = (DestructorFn)PatchVtableSlot(windowClass, CONTROL_DESTRUCTOR, ALIGNED_DESTRUCTOR_HOOKS[n]);
    }
}

static void InstallHudWindows()
{
    for (int i = 0; i < g_config.hudWindowCount && g_hudWindowHookCount < MAX_RULES; i++) {
        const HudWindowRule& rule = g_config.hudWindows[i];
        if (FindAlignRule(rule.windowClass) != ALIGN_NONE) {
            Log("hud windows: %08lX already has an [UI] alignment - ignored", rule.windowClass);
            continue;
        }
        if (!IsWindowClass(rule.windowClass)) {
            Log("hud windows: %08lX is not a window class ID - ignored", rule.windowClass);
            continue;
        }
        int n = g_hudWindowHookCount++;
        HudWindowHook& hook = g_hudWindowHooks[n];
        hook.windowClass = rule.windowClass;
        hook.column = rule.column;
        hook.row = rule.row;
        Log("hud windows: %08lX (draw %08lX) follows the HUD size", rule.windowClass, GetDrawFunction(rule.windowClass));
        hook.unhookedDraw = (MethodFn)PatchVtableSlot(rule.windowClass, CONTROL_DRAW, HUD_WINDOW_DRAW_HOOKS[n]);
    }
}

void InstallWindowRules()
{
    InstallAlignedWindows();
    InstallHudWindows();
}

// --- the gameplay HUD --------------------------------------------------------------------------

static MethodFn g_unhookedHudDraw;
static PointFn  g_unhookedHudMouse[5];

static char __fastcall HookHudDraw(void* hud, void*)
{
    if (!g_enabled)
        return g_unhookedHudDraw(hud, 0);
    UpdateBeforeDraw();
    UpdateDumpAtHudDraw();
    if (g_dump.active) Log("  [HUD]");
    NoteHudDrawn();
    ApplyMasterVolume(VOLUME_IN_GAME_WORLD);

    HudZone saved = g_hudZone;
    g_hudZone.column = -1;
    g_hudDrawDepth++;
    char result = g_unhookedHudDraw(hud, 0);
    g_hudDrawDepth--;
    g_hudZone = saved;
    if (g_dump.active) Log("  [/HUD]");
    return result;
}

// Mouse points arrive converted for the 4:3 block; the HUD needs them for its zone layout, so
// they are converted again from the real point.
static char ConvertHudMouse(int handler, void* hud, POINT* point)
{
    if (!g_enabled || point == NULL || !g_config.hudMouse)
        return g_unhookedHudMouse[handler](hud, 0, point);

    float realX, realY;
    if (g_mouse.haveReal) {
        realX = (float)g_mouse.realX;
        realY = (float)g_mouse.realY;
    } else {
        realX = g_drawMapping.offsetX + point->x * g_drawMapping.scaleX;
        realY = g_drawMapping.offsetY + point->y * g_drawMapping.scaleY;
    }
    POINT hudPoint = ScreenToHudPoint(realX, realY);
    char handled = g_unhookedHudMouse[handler](hud, 0, &hudPoint);

    if (g_config.mouseLog && handler < 2)
        Log("   HUD mouse %s: in (%ld,%ld) real (%.0f,%.0f) -> hud (%ld,%ld) handled=%d", handler == 0 ? "down" : "up",
            point->x, point->y, realX, realY, hudPoint.x, hudPoint.y, (int)handled);
    return handled;
}

template <int N> static char __fastcall HookHudMouse(void* hud, void*, POINT* point) { return ConvertHudMouse(N, hud, point); }
static void* const HUD_MOUSE_HOOKS[5] = {(void*)HookHudMouse<0>, (void*)HookHudMouse<1>, (void*)HookHudMouse<2>,
                                         (void*)HookHudMouse<3>, (void*)HookHudMouse<4>};

void InstallHudHooks()
{
    g_unhookedHudDraw = (MethodFn)PatchVtableSlot(VA_HUD_VTABLE, CONTROL_DRAW, (void*)HookHudDraw);
    for (int i = 0; i < 5; i++)
        g_unhookedHudMouse[i] = (PointFn)PatchVtableSlot(VA_HUD_VTABLE, HUD_MOUSE_SLOTS[i], HUD_MOUSE_HOOKS[i]);
}

// --- the mouse cursor ----------------------------------------------------------------------------
// The cursor is drawn outside any window. It is drawn at the [UI] size around the real mouse
// point, which was converted with the mapping that held under it (otherwise it would sit 240 px
// off while the loot window is open, or over a window following the HUD size).

static MethodFn g_unhookedCursorDraw[2];

static char DrawCursor(int cursor, void* self)
{
    if (!g_enabled)
        return g_unhookedCursorDraw[cursor](self, 0);
    Mapping m = GetBlockMapping(GetMouseAlignment());
    if (g_mouse.haveMapping) {
        float gameX = (float)*(int*)LoadedAddress(VA_MOUSE_X), gameY = (float)*(int*)LoadedAddress(VA_MOUSE_Y);
        m.scaleX = g_screen.uiScaleX;
        m.scaleY = g_screen.uiScaleY;
        m.offsetX = g_mouse.mapping.offsetX + gameX * (g_mouse.mapping.scaleX - g_screen.uiScaleX);
        m.offsetY = g_mouse.mapping.offsetY + gameY * (g_mouse.mapping.scaleY - g_screen.uiScaleY);
    }
    ScopedDrawMapping cursorMapping(m);
    return g_unhookedCursorDraw[cursor](self, 0);
}

static char __fastcall HookArrowCursorDraw(void* self, void*)    { return DrawCursor(0, self); }
static char __fastcall HookAnimatedCursorDraw(void* self, void*) { return DrawCursor(1, self); }

void InstallCursorHooks()
{
    g_unhookedCursorDraw[0] = (MethodFn)PatchVtableSlot(VA_CURSOR_VTABLE, CURSOR_DRAW_SLOT, (void*)HookArrowCursorDraw);
    g_unhookedCursorDraw[1] = (MethodFn)PatchVtableSlot(VA_ANIMATED_CURSOR_VTABLE, CURSOR_DRAW_SLOT, (void*)HookAnimatedCursorDraw);
    // The cursor may go left of / above the 4:3 block (negative 800x600 coordinates): jge -> jmp.
    static const unsigned char JMP_SHORT = 0xEB;
    WriteCode(LoadedAddress(VA_CURSOR_CLAMP_X), &JMP_SHORT, 1);
    WriteCode(LoadedAddress(VA_CURSOR_CLAMP_Y), &JMP_SHORT, 1);
}

bool HasExpectedHudAndCursorCode()
{
    return IsVtableSlot(VA_HUD_VTABLE, CONTROL_DRAW, 0x1001CD50)
        && IsVtableSlot(VA_HUD_VTABLE, 0x20, 0x100844E0) && IsVtableSlot(VA_HUD_VTABLE, 0x24, 0x10084620)
        && IsVtableSlot(VA_HUD_VTABLE, 0x28, 0x10084580) && IsVtableSlot(VA_HUD_VTABLE, 0x2C, 0x100846C0)
        && IsVtableSlot(VA_HUD_VTABLE, 0x30, 0x10084760)
        && IsVtableSlot(VA_HUD_VTABLE, CONTROL_ADD_CHILD, VA_CONTAINER_ADD_CHILD)
        && IsVtableSlot(VA_CURSOR_VTABLE, CURSOR_DRAW_SLOT, 0x1008DC40)
        && IsVtableSlot(VA_ANIMATED_CURSOR_VTABLE, CURSOR_DRAW_SLOT, 0x1008E0D0)
        // jge +7; mov dword ptr [ecx+44h / 48h], 0
        && BytesMatch(VA_CURSOR_CLAMP_X, "\x7D\x07\xC7\x41\x44", 5)
        && BytesMatch(VA_CURSOR_CLAMP_Y, "\x7D\x07\xC7\x41\x48", 5);
}
