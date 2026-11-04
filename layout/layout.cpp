// The screen size and scales, the mapping in use for each draw (4:3 block, HUD zones, windows
// following the HUD size), the HUD size control, and the mapping for each mouse point.
#include <string.h>
#include "layout/layout.h"
#include "core/addresses.h"
#include "settings/config.h"
#include "rendering/renderer.h"
#include "rendering/black_bars.h"
#include "core/log.h"

ScreenState g_screen = {800, 600, false, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f};
Mapping     g_drawMapping = {1.0f, 1.0f, 0.0f, 0.0f};
int         g_hudDrawDepth = 0;
HudZone     g_hudZone = {-1, -1};
int         g_hudWindowSlot = -1;

static int     g_forcedMappingDepth = 0;
static Mapping g_forcedMapping;
static int     g_currentAlign = ALIGN_NONE;    // alignment of the window being drawn

static void* g_openAlignedWindow = NULL;
static int   g_openAlignedWindowAlign = ALIGN_NONE;

// Screen areas drawn by the windows that follow the HUD size, for mouse input over them.
struct HudWindowArea {
    void*   window;
    Mapping mapping;
    Rect    area;
    bool    hasArea;
    DWORD   drawnAt;      // tick
    DWORD   drawOrder;    // higher = drawn later = on top
};
static const int MAX_HUD_WINDOW_AREAS = 16;
static HudWindowArea g_hudWindowAreas[MAX_HUD_WINDOW_AREAS];
static DWORD g_hudWindowDrawCount = 0;
static Rect  g_collectedArea;            // area of the window being drawn so far
static bool  g_haveCollectedArea = false;

// --- scales --------------------------------------------------------------------------------

// The largest scale at which 800x600 still fits on the screen.
static float FitScale()
{
    float fitX = g_screen.width / 800.0f, fitY = g_screen.height / 600.0f;
    return fitX < fitY ? fitX : fitY;
}

// A size setting ([HUD] size, [UI] scale) -> scale: 1.0 = real pixels .. as large as fits; 0 = max.
// Below 800x600 nothing can be smaller than "fits", so the range shrinks to that one value.
static float ScaleFromSize(float size)
{
    float fit = FitScale();
    float smallest = fit < 1.0f ? fit : 1.0f;
    if (size <= 0.0f)
        return fit;
    else
        return Clamp(size, smallest, fit);
}

void UpdateHudScale()
{
    float fit = FitScale();
    g_screen.hudScaleMax = fit;
    g_screen.hudScaleMin = fit < 1.0f ? fit : 1.0f;
    g_screen.hudScale = ScaleFromSize(g_config.hudSize);
}

void UpdateScreenSize()
{
    void* renderer = Renderer::Get();
    if (renderer == NULL) return;

    Rect screen = Renderer::GetRealScreenRect(renderer);
    if (!IsEmpty(screen)) {
        if (screen.w != g_screen.width || screen.h != g_screen.height || !g_screen.known)
            Log("screen %dx%d", screen.w, screen.h);
        g_screen.width = screen.w;
        g_screen.height = screen.h;
        g_screen.known = true;
    }

    UpdateHudScale();
    if (g_config.uiStretched) {
        g_screen.uiScaleX = g_screen.width / 800.0f;
        g_screen.uiScaleY = g_screen.height / 600.0f;
    } else {
        g_screen.uiScaleX = ScaleFromSize(g_config.uiScale);
        g_screen.uiScaleY = g_screen.uiScaleX;
    }
}

void SetHudSize(float size)
{
    if (!g_screen.known) return;
    bool isMax = size >= g_screen.hudScaleMax - 0.005f;
    if (size < g_screen.hudScaleMin) size = g_screen.hudScaleMin;
    g_config.hudSize = isMax ? 0.0f : size;
    UpdateHudScale();
    SaveHudSize(g_config.hudSize, isMax);
    Log("hud size %.2f (range %.2f .. %.2f)%s", g_screen.hudScale, g_screen.hudScaleMin,
        g_screen.hudScaleMax, isMax ? " = max" : "");
}

void StepHudSize(int notches) { SetHudSize(g_screen.hudScale + notches * g_config.hudSizeStep); }

// --- the 4:3 block ---------------------------------------------------------------------------

Mapping GetBlockMapping(int align)
{
    float blockWidth = 800.0f * g_screen.uiScaleX;
    Mapping m;
    m.scaleX = g_screen.uiScaleX;
    m.scaleY = g_screen.uiScaleY;
    m.offsetY = g_screen.height * 0.5f - 300.0f * g_screen.uiScaleY;
    if (align == ALIGN_LEFT)
        m.offsetX = 0.0f;
    else if (align == ALIGN_RIGHT)
        m.offsetX = g_screen.width - blockWidth;
    else
        m.offsetX = (g_screen.width - blockWidth) * 0.5f;
    return m;
}

void UpdateDrawMapping()
{
    if (g_forcedMappingDepth > 0)
        g_drawMapping = g_forcedMapping;
    else if (g_currentAlign != ALIGN_NONE)
        g_drawMapping = GetBlockMapping(g_currentAlign);
    else
        g_drawMapping = GetBlockMapping(g_config.uiAlign);
}

ScopedDrawMapping::ScopedDrawMapping(const Mapping& mapping)
{
    savedMapping = g_forcedMapping;
    g_forcedMapping = mapping;
    g_forcedMappingDepth++;
    UpdateDrawMapping();
}

ScopedDrawMapping::~ScopedDrawMapping()
{
    g_forcedMappingDepth--;
    g_forcedMapping = savedMapping;
    UpdateDrawMapping();
}

ScopedAlignment::ScopedAlignment(int align)
{
    savedAlign = g_currentAlign;
    g_currentAlign = align;
    UpdateDrawMapping();
}

ScopedAlignment::~ScopedAlignment()
{
    g_currentAlign = savedAlign;
    UpdateDrawMapping();
}

// --- design rect -> screen rect ----------------------------------------------------------------

Rect MapDesignRect(const Rect& in)
{
    bool isHud = g_hudDrawDepth > 0;
    NoteDrawnPiece(in, isHud);

    float left, right, top, bottom;
    if (isHud) {
        HudZone zone = g_hudZone.column >= 0 ? g_hudZone : GetZoneOfPiece(in);
        left   = HudToScreenX((float)in.x, zone.column);
        right  = HudToScreenX((float)(in.x + in.w), zone.column);
        top    = HudToScreenY((float)in.y, zone.row);
        bottom = HudToScreenY((float)(in.y + in.h), zone.row);
    } else {
        left   = g_drawMapping.offsetX + in.x * g_drawMapping.scaleX;
        right  = g_drawMapping.offsetX + (in.x + in.w) * g_drawMapping.scaleX;
        top    = g_drawMapping.offsetY + in.y * g_drawMapping.scaleY;
        bottom = g_drawMapping.offsetY + (in.y + in.h) * g_drawMapping.scaleY;
    }
    Rect out = MakeRect(Round(left), Round(top), Round(right) - Round(left), Round(bottom) - Round(top));

    if (g_hudWindowSlot >= 0 && !isHud && !IsEmpty(out)) {
        g_collectedArea = g_haveCollectedArea ? Union(g_collectedArea, out) : out;
        g_haveCollectedArea = true;
    }
    return out;
}

Rect GetCursorClampRect()
{
    float hudScale = g_screen.hudScale;
    Mapping block = GetBlockMapping(ALIGN_CENTER);
    float left  = -(g_screen.width / g_screen.uiScaleX - 800.0f);
    float right = g_screen.width / g_screen.uiScaleX;
    float top    = -block.offsetY / block.scaleY;
    float bottom = (g_screen.height - block.offsetY) / block.scaleY;
    for (int zone = 0; zone < 3; zone++) {
        float zoneX = HudToScreenX(0.0f, zone), zoneY = HudToScreenY(0.0f, zone);
        if (-zoneX / hudScale < left)                    left = -zoneX / hudScale;
        if ((g_screen.width - zoneX) / hudScale > right) right = (g_screen.width - zoneX) / hudScale;
        if (-zoneY / hudScale < top)                     top = -zoneY / hudScale;
        if ((g_screen.height - zoneY) / hudScale > bottom) bottom = (g_screen.height - zoneY) / hudScale;
    }
    int x = (int)floorf(left), y = (int)floorf(top);
    return MakeRect(x, y, (int)ceilf(right) - x, (int)ceilf(bottom) - y);
}

// --- mouse -------------------------------------------------------------------------------------

void NoteAlignedWindowDrawn(void* window, DWORD windowClass, int align)
{
    if (g_openAlignedWindow != window && g_config.mouseLog)
        Log("aligned window %08lX opened (obj %p)", windowClass, window);
    g_openAlignedWindow = window;
    g_openAlignedWindowAlign = align;
}

void ForgetAlignedWindow(void* window)
{
    if (g_openAlignedWindow != window) return;
    g_openAlignedWindow = NULL;
    g_openAlignedWindowAlign = ALIGN_NONE;
    if (g_config.mouseLog) Log("aligned window closed (obj %p)", window);
}

int GetMouseAlignment()
{
    // Still open? Hidden windows are skipped by the game's own draw loop.
    if (g_openAlignedWindow != NULL && GetMethod<MethodFn>(g_openAlignedWindow, CONTROL_IS_HIDDEN)(g_openAlignedWindow, 0) == 0)
        return g_openAlignedWindowAlign;
    else
        return g_config.uiAlign;
}

Mapping GetMouseMappingAt(int x, int y)
{
    DWORD now = GetTickCount();
    int topmost = -1;
    for (int i = 0; i < MAX_HUD_WINDOW_AREAS; i++) {
        const HudWindowArea& entry = g_hudWindowAreas[i];
        bool drawnRecently = entry.window != NULL && entry.hasArea && now - entry.drawnAt <= 300;
        if (!drawnRecently || !Contains(entry.area, x, y)) continue;
        if (topmost < 0 || entry.drawOrder > g_hudWindowAreas[topmost].drawOrder) topmost = i;
    }
    if (topmost >= 0)
        return g_hudWindowAreas[topmost].mapping;
    else
        return GetBlockMapping(GetMouseAlignment());
}

// The window's own slot, else the one drawn longest ago (or a free one).
static int FindHudWindowSlot(void* window)
{
    for (int i = 0; i < MAX_HUD_WINDOW_AREAS; i++)
        if (g_hudWindowAreas[i].window == window)
            return i;

    DWORD now = GetTickCount();
    int stalest = 0;
    for (int i = 1; i < MAX_HUD_WINDOW_AREAS; i++) {
        const HudWindowArea& entry = g_hudWindowAreas[i];
        if (entry.window == NULL || now - entry.drawnAt > now - g_hudWindowAreas[stalest].drawnAt) stalest = i;
    }
    g_hudWindowAreas[stalest].window = window;
    g_hudWindowAreas[stalest].hasArea = false;
    return stalest;
}

ScopedHudWindowDraw::ScopedHudWindowDraw(void* window, const Mapping& windowMapping)
{
    slot = FindHudWindowSlot(window);
    mapping = windowMapping;
    savedSlot = g_hudWindowSlot;
    savedHaveArea = g_haveCollectedArea;
    savedArea = g_collectedArea;
    g_hudWindowSlot = slot;
    g_haveCollectedArea = false;
}

ScopedHudWindowDraw::~ScopedHudWindowDraw()
{
    HudWindowArea& entry = g_hudWindowAreas[slot];
    entry.mapping = mapping;
    entry.drawnAt = GetTickCount();
    entry.drawOrder = ++g_hudWindowDrawCount;
    entry.hasArea = g_haveCollectedArea;
    if (g_haveCollectedArea) entry.area = g_collectedArea;

    g_hudWindowSlot = savedSlot;
    g_haveCollectedArea = savedHaveArea;
    g_collectedArea = savedArea;
}
