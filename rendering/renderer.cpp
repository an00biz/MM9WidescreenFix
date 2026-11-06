// The fix's own calls to the render object, made through its unhooked functions while
// g_ownCallDepth tells the hooks to stay out of the way.
#include "rendering/renderer.h"
#include "core/cshell_hud.h"
#include "settings/config.h"

RendererFunctions g_unhookedRenderer;
int g_ownCallDepth = 0;

bool AreDrawHooksActive() { return g_enabled && g_ownCallDepth == 0; }

// The engine takes non-const rects: hand it a copy, NULL stays NULL.
static Rect* CopyOrNull(const Rect* rect, Rect& copy)
{
    if (rect == NULL)
        return NULL;
    copy = *rect;
    return &copy;
}

namespace Renderer {

void* Get()
{
    if (g_originalBase == 0)
        return NULL;
    else
        return *(void**)LoadedAddress(VA_RENDERER_PTR);
}

void* GetScreen(void* renderer) { return g_unhookedRenderer.getScreen(renderer, 0); }

bool IsScreen(void* renderer, void* surface) { return surface != NULL && surface == GetScreen(renderer); }

Rect GetRealScreenRect(void* renderer)
{
    OwnCallScope own;
    Rect screen = MakeRect(0, 0, 800, 600);
    g_unhookedRenderer.getScreenRect(renderer, 0, &screen);
    return screen;
}

void GetSize(void* renderer, void* surface, int& w, int& h)
{
    OwnCallScope own;
    g_unhookedRenderer.getSize(renderer, 0, surface, &w, &h);
}

char Blit(void* renderer, void* dest, int x, int y, void* src, const Rect* srcRect)
{
    OwnCallScope own;
    Rect srcCopy;
    return g_unhookedRenderer.blit(renderer, 0, dest, x, y, src, CopyOrNull(srcRect, srcCopy));
}

char Scale(void* renderer, void* dest, const Rect& dstRect, void* src, const Rect* srcRect)
{
    OwnCallScope own;
    Rect dstCopy = dstRect, srcCopy;
    return g_unhookedRenderer.scale(renderer, 0, dest, &dstCopy, src, CopyOrNull(srcRect, srcCopy));
}

char Fill(void* renderer, void* dest, DWORD color, const Rect* rect, int flags)
{
    OwnCallScope own;
    Rect copy;
    return g_unhookedRenderer.fill(renderer, 0, dest, color, CopyOrNull(rect, copy), flags);
}

void* CreateSurface(void* renderer, int w, int h)
{
    OwnCallScope own;
    return g_unhookedRenderer.createSurface(renderer, 0, w, h, 0);
}

void DeleteSurface(void* renderer, void* surface)
{
    if (surface == NULL) return;
    OwnCallScope own;
    g_unhookedRenderer.deleteSurface(renderer, 0, surface);
}

}
