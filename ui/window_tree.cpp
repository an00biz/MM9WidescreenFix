// Re-implements the container's "draw all children" (0x10084F40) to place each child: HUD pieces
// in their zone, windows with an alignment rule noted for the black bars, and the window tree
// written to the log for [HUD] dump. Also re-implements the controls' hit tests, 1:1 plus a trace.
#include <stdio.h>
#include "ui/window_tree.h"
#include "core/addresses.h"
#include "rendering/black_bars.h"
#include "settings/config.h"
#include "core/cshell_hud.h"
#include "diagnostics/diagnostics.h"
#include "layout/layout.h"
#include "core/log.h"
#include "options/options_game.h"
#include "core/patching.h"
#include "rendering/renderer.h"

typedef Rect* (__fastcall *GetRectFn)(void* self, void* edx, Rect* out);
typedef int   (__fastcall *GetIntFn)(void* self, void* edx);

static Rect GetControlRect(void* control)
{
    Rect r = MakeRect(0, 0, 0, 0);
    GetMethod<GetRectFn>(control, CONTROL_GET_RECT)(control, 0, &r);
    return r;
}

static void DrawControl(void* control) { GetMethod<MethodFn>(control, CONTROL_DRAW)(control, 0); }
static bool IsHidden(void* control)    { return GetMethod<MethodFn>(control, CONTROL_IS_HIDDEN)(control, 0) != 0; }
static DWORD GetWindowClass(void* control) { return LinkedAddress((DWORD)GetVtable(control)); }

// [HUD] dump outside the HUD: the window tree, three levels deep.
static void DumpWindow(void* window, int alignRule)
{
    Rect r = GetControlRect(window);
    char alignment[16] = "";
    if (alignRule != ALIGN_NONE) _snprintf(alignment, sizeof(alignment), " [%s]", AlignName(alignRule));
    Log("%*swindow vt=%08lX rect=(%d,%d %dx%d)%s", g_dump.depth * 2, "", GetWindowClass(window), r.x, r.y, r.w, r.h, alignment);
    g_dump.depth++;
    DrawControl(window);
    g_dump.depth--;
}

// A HUD child: a leaf (a single piece) is drawn in the zone of its rect; a container's children
// decide their own zones.
static void DrawHudChild(void* child)
{
    bool isLeaf = (DWORD)GetVtable(child)[CONTROL_ADD_CHILD / 4] != LoadedAddress(VA_CONTAINER_ADD_CHILD);
    Rect r = GetControlRect(child);
    HudZone zone = GetZoneOfPiece(r);
    if (g_dump.active)
        Log("%*s%s vt=%08lX rect=(%d,%d %dx%d)%s%s", g_dump.depth * 2, "", isLeaf ? "leaf" : "cont", GetWindowClass(child),
            r.x, r.y, r.w, r.h, isLeaf ? " group=" : "", isLeaf ? GetZoneName(zone) : "");

    if (isLeaf) {
        HudZone saved = g_hudZone;
        g_hudZone = zone;
        DrawControl(child);
        g_hudZone = saved;
    } else {
        g_dump.depth++;
        DrawControl(child);
        g_dump.depth--;
    }
}

static void DrawChild(void* child)
{
    bool isHud = g_hudDrawDepth > 0;
    int alignRule = g_enabled && !isHud ? FindAlignRule(GetWindowClass(child)) : ALIGN_NONE;
    if (alignRule != ALIGN_NONE && !IsHidden(child)) NoteAlignedWindowInTree();

    if (g_dump.active && !isHud && g_dump.depth < 3)
        DumpWindow(child, alignRule);
    else if (isHud && g_enabled)
        DrawHudChild(child);
    else
        DrawControl(child);
}

// The children are a circular list of {child, next} nodes at +0x38. A child whose byte at +4 is
// set is skipped, as in the original.
static char __fastcall HookDrawChildren(void* container, void*)
{
    if (IsHidden(container))
        return 1;
    if (g_enabled) BeforeGameOptionsDraw(container);

    void** node = *(void***)((char*)container + 0x38);
    void* first = node != NULL ? node[0] : NULL;
    BeginWindowTree();
    if (first != NULL) {
        void* child = first;
        do {
            if (*((char*)child + 4) == 0) DrawChild(child);
            node = (void**)node[1];
            child = node[0];
        } while (child != first);
    }
    EndWindowTree(container);
    return 1;
}

// --- hit tests -----------------------------------------------------------------------------------

// The point is inside the control's rect, edges included (as in the original).
static bool HitTest(void* control, const POINT* point)
{
    Rect r = GetControlRect(control);
    bool isInside = point->x >= r.x && point->y >= r.y && point->x <= r.x + r.w && point->y <= r.y + r.h;
    if (isInside && g_config.mouseLog && GetTickCount() <= g_hitTraceUntil)
        Log("   hit: control vt=%08lX rect=(%d,%d %dx%d) point (%ld,%ld)  block x %.0f%s", GetWindowClass(control),
            r.x, r.y, r.w, r.h, point->x, point->y, g_drawMapping.offsetX, g_hudDrawDepth > 0 ? " [in HUD draw]" : "");
    return isInside;
}

static char __fastcall HookControlHitTest(void* control, void*, POINT* point) { return HitTest(control, point); }

// An image control without an id (-1) is never hit.
static char __fastcall HookImageControlHitTest(void* control, void*, POINT* point)
{
    if (GetMethod<GetIntFn>(control, CONTROL_GET_ID)(control, 0) == -1)
        return 0;
    else
        return HitTest(control, point);
}

bool HasExpectedWindowTreeCode()
{
    // push esi; mov esi,ecx; mov eax,[esi]; call [eax+0ACh]
    return BytesMatch(VA_CONTAINER_DRAW_CHILDREN, "\x56\x8B\xF1\x8B\x06\xFF\x90\xAC\x00\x00\x00", 11)
        && BytesMatch(VA_CONTROL_HIT_TEST, "\x8B\x01\x83\xEC\x10\x8D\x54\x24\x00\x53\x56\x57", 12)
        && BytesMatch(VA_IMAGE_CONTROL_HIT_TEST, "\x83\xEC\x10\x56\x8B\xF1\x8B\x06\xFF\x50\x3C\x83", 12);
}

void InstallWindowTreeHooks()
{
    WriteJump(LoadedAddress(VA_CONTAINER_DRAW_CHILDREN), (void*)HookDrawChildren);
    WriteJump(LoadedAddress(VA_CONTROL_HIT_TEST), (void*)HookControlHitTest);
    WriteJump(LoadedAddress(VA_IMAGE_CONTROL_HIT_TEST), (void*)HookImageControlHitTest);
}
