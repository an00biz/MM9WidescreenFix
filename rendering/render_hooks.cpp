// The render object's hooks: Blit, Scale and Fill onto the screen are mapped from 800x600 to the
// real screen; the screen reports 800x600 to the game; copies from the screen are redirected to
// real-resolution copies; the loading screen is redrawn whole each step.
#include "rendering/render_hooks.h"
#include "core/addresses.h"
#include "rendering/black_bars.h"
#include "settings/config.h"
#include "core/cshell_hud.h"
#include "diagnostics/diagnostics.h"
#include "diagnostics/screen_access.h"
#include "input/input.h"
#include "layout/layout.h"
#include "core/log.h"
#include "core/patching.h"
#include "rendering/renderer.h"
#include "rendering/driver_state.h"
#include "rendering/screen_copies.h"
#include "media/sound.h"

void UpdateBeforeDraw()
{
    static DWORD lastUpdate = 0;
    ApplyMasterVolume(VOLUME_IN_MENUS);
    InstallInputHookOnce();
    if (g_screen.known) UpdateDrawMapping();

    DWORD now = GetTickCount();
    if (g_screen.known && now - lastUpdate < 100) return;
    lastUpdate = now;
    LoadConfig(false);
    UpdateScreenSize();
    UpdateDrawMapping();
    UpdateScreenAccessHooks();
}

// --- solid color surfaces ------------------------------------------------------------------------

struct SolidColorSurface { DWORD color; int flags; void* surface; };
static const int MAX_SOLID_COLORS = 32;
static SolidColorSurface g_solidColors[MAX_SOLID_COLORS];
static int g_solidColorCount = 0;

void* GetSolidColorSurface(void* renderer, DWORD color, int flags)
{
    for (int i = 0; i < g_solidColorCount; i++)
        if (g_solidColors[i].color == color && g_solidColors[i].flags == flags)
            return g_solidColors[i].surface;
    if (g_solidColorCount >= MAX_SOLID_COLORS)
        return NULL;

    void* surface = Renderer::CreateSurface(renderer, 8, 8);
    if (surface == NULL)
        return NULL;
    Renderer::Fill(renderer, surface, color, NULL, flags);
    SolidColorSurface entry = {color, flags, surface};
    g_solidColors[g_solidColorCount++] = entry;
    return surface;
}

// --- scaled drawing ----------------------------------------------------------------------------

// Draws src (srcRect of it, NULL = all) scaled into the screen rect out.
// Empty draws are skipped: the party portraits' 4x75 experience bar is drawn with a source height
// of 0 while it is empty. 1:1 that costs nothing, but scaled the Direct3D renderer takes ~2.5 ms
// each (4 a frame halved the frame rate as soon as the HUD was bigger than 1.0). Source rects
// reaching outside the picture are clipped to it, the target shrunk to match: below one pixel of
// experience the bar asks for the row just below its picture, which is also slow to stretch.
static char DrawScaled(void* renderer, void* screen, const Rect& out, void* src, const Rect* srcRect)
{
    if ((srcRect != NULL && IsEmpty(*srcRect)) || IsEmpty(out))
        return 1;
    int w = 0, h = 0;
    Renderer::GetSize(renderer, src, w, h);
    Rect source = srcRect != NULL ? *srcRect : MakeRect(0, 0, w, h);
    Rect target = out;

    bool reachesOutside = source.x < 0 || source.y < 0 || source.x + source.w > w || source.y + source.h > h;
    if (w > 0 && h > 0 && reachesOutside) {
        float scaleX = (float)target.w / source.w, scaleY = (float)target.h / source.h;
        int left   = source.x < 0 ? 0 : source.x;
        int top    = source.y < 0 ? 0 : source.y;
        int right  = source.x + source.w > w ? w : source.x + source.w;
        int bottom = source.y + source.h > h ? h : source.y + source.h;
        if (right <= left || bottom <= top)
            return 1;
        int targetLeft   = target.x + Round((left - source.x) * scaleX);
        int targetTop    = target.y + Round((top - source.y) * scaleY);
        int targetRight  = target.x + Round((right - source.x) * scaleX);
        int targetBottom = target.y + Round((bottom - source.y) * scaleY);
        source = MakeRect(left, top, right - left, bottom - top);
        target = MakeRect(targetLeft, targetTop, targetRight - targetLeft, targetBottom - targetTop);
        if (IsEmpty(target))
            return 1;
    }
    return Renderer::Scale(renderer, screen, target, src, &source);
}

// --- Blit --------------------------------------------------------------------------------------

static char MapBlit(void* renderer, void* dest, int x, int y, void* src, Rect* srcRect)
{
    if (!AreDrawHooksActive())
        return g_unhookedRenderer.blit(renderer, 0, dest, x, y, src, srcRect);

    if (!Renderer::IsScreen(renderer, dest)) {
        if (Renderer::IsScreen(renderer, src)) {
            Rect copied = srcRect != NULL ? *srcRect : MakeRect(0, 0, 800, 600);
            Rect at = MakeRect(x, y, copied.w, copied.h);
            return CopyFromScreen(renderer, dest, &at, srcRect);
        }
        ForgetScreenCopy(renderer, dest);
        return g_unhookedRenderer.blit(renderer, 0, dest, x, y, src, srcRect);
    }

    UpdateBeforeDraw();
    int w = 0, h = 0;
    if (srcRect != NULL) {
        w = srcRect->w;
        h = srcRect->h;
    } else {
        Renderer::GetSize(renderer, src, w, h);
    }
    Rect designRect = MakeRect(x, y, w, h);
    ScreenCopy* copy = FindScreenCopy(src);
    if (copy != NULL)
        return DrawScreenCopy(renderer, dest, *copy, designRect, srcRect);

    ScopedSceneDraw scene(src);
    Rect out = MapDesignRect(designRect);
    if (g_dump.active && g_hudDrawDepth > 0 && g_hudZone.column < 0)
        Log("   loose blit  (%d,%d %dx%d) -> (%d,%d %dx%d)", x, y, w, h, out.x, out.y, out.w, out.h);
    if (out.w == w && out.h == h)
        return Renderer::Blit(renderer, dest, out.x, out.y, src, srcRect);
    else
        return DrawScaled(renderer, dest, out, src, srcRect);
}

// Loading screen (menus.cpp, 10034A80). The boat, at every step, puts back what was under it
// from a saved copy of the screen, saves the screen under its new place and draws itself. With
// page flipping the back buffers don't hold the same picture, so those copies hold stale pixels
// (a piece of the previous screen, an older boat) and leave trails. The background is a plain
// picture, so the whole picture is drawn again before each step instead.
static void* g_loadingBackground = NULL;

static char __fastcall HookBlit(void* renderer, void*, void* dest, int x, int y, void* src, Rect* srcRect)
{
    DWORD caller = RETURN_ADDRESS();
    PerfTimer timer(PERF_BLIT, src);
    bool isGameDrawOnScreen = g_enabled && g_ownCallDepth == 0 && Renderer::IsScreen(renderer, dest);

    // The loading screen is drawn on its own, without a window tree: black bars before its picture.
    if (isGameDrawOnScreen && caller == LoadedAddress(VA_LOADING_BACKGROUND_BLIT_RET)) {
        g_loadingBackground = src;
        UpdateBeforeDraw();
        if (g_config.blackBars && g_screen.known) PaintBlackBars();
    }
    // The whole picture, not just the boat's old place: each back buffer still holds the boat
    // from two or three steps ago, which a partial restore misses.
    if (isGameDrawOnScreen && caller == LoadedAddress(VA_LOADING_BOAT_RESTORE_RET) && g_loadingBackground != NULL) {
        UpdateBeforeDraw();
        if (g_config.blackBars && g_screen.known) PaintBlackBars();
        return MapBlit(renderer, dest, 0, 0, g_loadingBackground, NULL);
    }
    return MapBlit(renderer, dest, x, y, src, srcRect);
}

// --- Scale and Fill ----------------------------------------------------------------------------

static char __fastcall HookScale(void* renderer, void*, void* dest, Rect* dstRect, void* src, Rect* srcRect)
{
    PerfTimer timer(PERF_SCALE, src);
    if (!AreDrawHooksActive())
        return g_unhookedRenderer.scale(renderer, 0, dest, dstRect, src, srcRect);

    if (!Renderer::IsScreen(renderer, dest)) {
        if (Renderer::IsScreen(renderer, src))
            return CopyFromScreen(renderer, dest, dstRect, srcRect);
        ForgetScreenCopy(renderer, dest);
        return g_unhookedRenderer.scale(renderer, 0, dest, dstRect, src, srcRect);
    }
    if (dstRect == NULL)
        return g_unhookedRenderer.scale(renderer, 0, dest, dstRect, src, srcRect);

    UpdateBeforeDraw();
    ScreenCopy* copy = FindScreenCopy(src);
    if (copy != NULL)
        return DrawScreenCopy(renderer, dest, *copy, *dstRect, srcRect);
    ScopedSceneDraw scene(src);
    return DrawScaled(renderer, dest, MapDesignRect(*dstRect), src, srcRect);
}

// A filled rectangle on part of the screen is very slow with the Direct3D renderer (~2.5 ms each,
// it seems to lock the back buffer); a scaled blit costs ~0.1 ms. So partial fills are drawn as a
// one-color surface scaled to the rectangle. Whole-screen fills (clears) are fast and stay fills.
static char __fastcall HookFill(void* renderer, void*, void* dest, DWORD color, Rect* rect, int flags)
{
    PerfTimer timer(PERF_FILL, NULL);
    if (!AreDrawHooksActive() || rect == NULL || !Renderer::IsScreen(renderer, dest))
        return g_unhookedRenderer.fill(renderer, 0, dest, color, rect, flags);

    UpdateBeforeDraw();
    bool wholeScreen = CoversDesignScreen(*rect) && g_hudDrawDepth == 0;
    Rect out;
    if (wholeScreen) {
        NoteWholeScreenFill();
        out = MakeRect(0, 0, g_screen.width, g_screen.height);
    } else {
        out = MapDesignRect(*rect);
    }

    void* solid = NULL;
    if (!wholeScreen && !IsEmpty(out)) solid = GetSolidColorSurface(renderer, color, flags);
    if (solid != NULL) {
        ScopedSceneDraw scene(solid);
        return Renderer::Scale(renderer, dest, out, solid, NULL);
    }
    else
        return Renderer::Fill(renderer, dest, color, &out, flags);
}

// --- surfaces and the screen size ----------------------------------------------------------------

static void* __fastcall HookCreateSurface(void* renderer, void*, int w, int h, int flags)
{
    PerfTimer timer(PERF_CREATE, NULL);
    return g_unhookedRenderer.createSurface(renderer, 0, w, h, flags);
}

static char __fastcall HookDeleteSurface(void* renderer, void*, void* surface)
{
    PerfTimer timer(PERF_DELETE, NULL);
    if (surface != NULL) ForgetScreenCopy(renderer, surface);
    if (surface != NULL && surface == g_loadingBackground) g_loadingBackground = NULL;
    return g_unhookedRenderer.deleteSurface(renderer, 0, surface);
}

// The game sees an 800x600 screen, except the callers that need the real one.
static Rect* __fastcall HookGetScreenRect(void* renderer, void*, Rect* out)
{
    DWORD caller = RETURN_ADDRESS();
    if (!AreDrawHooksActive() || out == NULL || caller == LoadedAddress(VA_SCREEN_RECT_FROM_MODE_CHECK))
        return g_unhookedRenderer.getScreenRect(renderer, 0, out);

    bool isCursorRect = caller == LoadedAddress(VA_SCREEN_RECT_FROM_CURSOR_1)
                        || caller == LoadedAddress(VA_SCREEN_RECT_FROM_CURSOR_2);
    if (isCursorRect && g_screen.known)
        *out = GetCursorClampRect();
    else
        *out = MakeRect(0, 0, 800, 600);
    return out;
}

static char __fastcall HookGetSize(void* renderer, void*, void* surface, int* w, int* h)
{
    if (!AreDrawHooksActive() || !Renderer::IsScreen(renderer, surface))
        return g_unhookedRenderer.getSize(renderer, 0, surface, w, h);
    if (w != NULL) *w = 800;
    if (h != NULL) *h = 600;
    return 1;
}

bool HasExpectedRendererCode()
{
    const DWORD vtable = VA_RENDERER_VTABLE;
    // each GetScreenRect caller: call [eax+50h] right before its return address
    return BytesMatch(VA_SCREEN_RECT_FROM_MODE_CHECK - 3, "\xFF\x50\x50", 3)
        && BytesMatch(VA_SCREEN_RECT_FROM_CURSOR_1 - 3, "\xFF\x50\x50", 3)
        && BytesMatch(VA_SCREEN_RECT_FROM_CURSOR_2 - 3, "\xFF\x50\x50", 3)
        && IsVtableSlot(vtable, RENDERER_CREATE_SURFACE, 0x1008BDC0) && IsVtableSlot(vtable, RENDERER_DELETE_SURFACE, 0x1008BED0)
        && IsVtableSlot(vtable, RENDERER_BLIT, 0x1009C880) && IsVtableSlot(vtable, RENDERER_SCALE, 0x1009C900)
        && IsVtableSlot(vtable, RENDERER_FILL, 0x1008C390) && IsVtableSlot(vtable, RENDERER_GET_SIZE, 0x1008C950)
        && IsVtableSlot(vtable, RENDERER_GET_SCREEN_RECT, 0x1008C6C0) && IsVtableSlot(vtable, RENDERER_GET_SCREEN, 0x1008C720);
}

void InstallRenderHooks()
{
    const DWORD vtable = VA_RENDERER_VTABLE;
    RendererFunctions& f = g_unhookedRenderer;
    f.getScreen     = (GetScreenFn)*(void**)(LoadedAddress(vtable) + RENDERER_GET_SCREEN);
    f.blit          = (BlitFn)         PatchVtableSlot(vtable, RENDERER_BLIT,            (void*)HookBlit);
    f.scale         = (ScaleFn)        PatchVtableSlot(vtable, RENDERER_SCALE,           (void*)HookScale);
    f.fill          = (FillFn)         PatchVtableSlot(vtable, RENDERER_FILL,            (void*)HookFill);
    f.getSize       = (GetSizeFn)      PatchVtableSlot(vtable, RENDERER_GET_SIZE,        (void*)HookGetSize);
    f.createSurface = (CreateSurfaceFn)PatchVtableSlot(vtable, RENDERER_CREATE_SURFACE,  (void*)HookCreateSurface);
    f.deleteSurface = (DeleteSurfaceFn)PatchVtableSlot(vtable, RENDERER_DELETE_SURFACE,  (void*)HookDeleteSurface);
    f.getScreenRect = (GetScreenRectFn)PatchVtableSlot(vtable, RENDERER_GET_SCREEN_RECT, (void*)HookGetScreenRect);
}
