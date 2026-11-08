// The game's 2D render object: its original (unhooked) functions, and the fix's own drawing calls
// to them, which the render hooks let through unchanged.
#pragma once
#include <windows.h>
#include "core/geometry.h"

// The engine's objects are C++ classes, their methods __thiscall (object in ecx). They are called
// and hooked here as __fastcall functions, whose first two arguments travel in ecx and edx: the
// object, and an unused edx.
typedef char  (__fastcall *BlitFn)(void* renderer, void* edx, void* dest, int x, int y, void* src, Rect* srcRect);
typedef char  (__fastcall *ScaleFn)(void* renderer, void* edx, void* dest, Rect* dstRect, void* src, Rect* srcRect);
typedef char  (__fastcall *FillFn)(void* renderer, void* edx, void* dest, DWORD color, Rect* rect, int flags);
typedef char  (__fastcall *GetSizeFn)(void* renderer, void* edx, void* surface, int* w, int* h);
typedef Rect* (__fastcall *GetScreenRectFn)(void* renderer, void* edx, Rect* out);
typedef void* (__fastcall *GetScreenFn)(void* renderer, void* edx);
typedef void* (__fastcall *CreateSurfaceFn)(void* renderer, void* edx, int w, int h, int flags);
typedef char  (__fastcall *DeleteSurfaceFn)(void* renderer, void* edx, void* surface);
// A method without arguments, e.g. a control's Draw or IsHidden.
typedef char  (__fastcall *MethodFn)(void* self, void* edx);

// The render object's functions as they were before the hooks replaced them.
struct RendererFunctions {
    BlitFn          blit;
    ScaleFn         scale;
    FillFn          fill;
    GetSizeFn       getSize;
    GetScreenRectFn getScreenRect;
    GetScreenFn     getScreen;
    CreateSurfaceFn createSurface;
    DeleteSurfaceFn deleteSurface;
};
extern RendererFunctions g_unhookedRenderer;

// > 0 while the fix itself calls the render object: the hooks then let every call through.
extern int g_ownCallDepth;
struct OwnCallScope {
    OwnCallScope()  { g_ownCallDepth++; }
    ~OwnCallScope() { g_ownCallDepth--; }
};
// The fix is on and the call comes from the game.
bool AreDrawHooksActive();

// An object's vtable, and one of its methods by byte offset.
inline void** GetVtable(void* object) { return *(void***)object; }
template <typename Fn> inline Fn GetMethod(void* object, int slot) { return (Fn)GetVtable(object)[slot / 4]; }

// The fix's own calls. Rects are copied before they reach the engine.
namespace Renderer {
    void* Get();                                  // NULL until the engine has made it
    void* GetScreen(void* renderer);
    bool  IsScreen(void* renderer, void* surface);
    Rect  GetRealScreenRect(void* renderer);
    void  GetSize(void* renderer, void* surface, int& w, int& h);
    char  Blit(void* renderer, void* dest, int x, int y, void* src, const Rect* srcRect);
    char  Scale(void* renderer, void* dest, const Rect& dstRect, void* src, const Rect* srcRect);
    char  Fill(void* renderer, void* dest, DWORD color, const Rect* rect, int flags);
    void* CreateSurface(void* renderer, int w, int h);
    void  DeleteSurface(void* renderer, void* surface);
}
