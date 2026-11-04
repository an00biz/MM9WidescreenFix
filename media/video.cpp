// Bink videos: the engine can stretch them to the whole screen but ignores the aspect ratio, so
// the target rect is shrunk to keep it, centred, and the bars around it painted black.
#include <ddraw.h>
#include <string.h>
#include "media/video.h"
#include "core/addresses.h"
#include "settings/config.h"
#include "core/cshell_hud.h"
#include "core/geometry.h"
#include "core/log.h"
#include "core/patching.h"
#include "rendering/renderer.h"

typedef HRESULT (__stdcall *DirectDrawBlitFn)(void* surface, RECT* dst, void* src, RECT* srcRect, DWORD flags, void* effects);
static const int DIRECTDRAW_SURFACE_BLT = 5;   // IDirectDrawSurface::Blt, in the COM vtable

static RECT FitKeepingAspect(const RECT& area, int videoW, int videoH)
{
    int areaW = area.right - area.left, areaH = area.bottom - area.top;
    float scaleX = (float)areaW / videoW, scaleY = (float)areaH / videoH;
    float scale = scaleX < scaleY ? scaleX : scaleY;
    int w = Round(videoW * scale), h = Round(videoH * scale);
    RECT fitted;
    fitted.left = area.left + (areaW - w) / 2;
    fitted.top = area.top + (areaH - h) / 2;
    fitted.right = fitted.left + w;
    fitted.bottom = fitted.top + h;
    return fitted;
}

// Called by lithtech.exe in place of surface->Blt(dst, dstRect, src, srcRect, flags, effects).
static HRESULT __stdcall HookVideoBlit(void* dst, RECT* dstRect, void* src, RECT* srcRect, DWORD flags, void* effects)
{
    DirectDrawBlitFn blit = (DirectDrawBlitFn)GetVtable(dst)[DIRECTDRAW_SURFACE_BLT];
    if (g_config.videoMode != VIDEO_FIT || dstRect == NULL || srcRect == NULL)
        return blit(dst, dstRect, src, srcRect, flags, effects);
    int videoW = srcRect->right - srcRect->left, videoH = srcRect->bottom - srcRect->top;
    bool isEmpty = dstRect->right <= dstRect->left || dstRect->bottom <= dstRect->top || videoW <= 0 || videoH <= 0;
    if (isEmpty)
        return blit(dst, dstRect, src, srcRect, flags, effects);

    const RECT& area = *dstRect;
    RECT fitted = FitKeepingAspect(area, videoW, videoH);
    DDBLTFX black;
    memset(&black, 0, sizeof(black));
    black.dwSize = sizeof(black);
    RECT bars[4] = {{area.left, area.top, area.right, fitted.top},          // above
                    {area.left, fitted.bottom, area.right, area.bottom},    // below
                    {area.left, fitted.top, fitted.left, fitted.bottom},    // left
                    {fitted.right, fitted.top, area.right, fitted.bottom}}; // right
    for (int i = 0; i < 4; i++)
        if (bars[i].right > bars[i].left && bars[i].bottom > bars[i].top)
            blit(dst, &bars[i], NULL, NULL, DDBLT_COLORFILL | DDBLT_WAIT, &black);
    return blit(dst, &fitted, src, srcRect, flags, effects);
}

bool HasExpectedMovieCode() { return BytesMatch(VA_MOVIE_FULLSCREEN_JE, "\x74\x05\xBA\x01", 4); }   // je +5; mov edx,1...

// The video's fullscreen flag is forced on, and lithtech.exe's Blt call goes through the hook.
void InstallVideoHook()
{
    if (g_config.videoMode == VIDEO_ORIGINAL) return;
    DWORD blitCall = (DWORD)GetModuleHandleA(NULL) + (EXE_BINK_BLIT - EXE_IMAGE_BASE);
    // mov ecx,[eax]; push edx; push eax; call [ecx+14h]
    if (memcmp((void*)blitCall, "\x8B\x08\x52\x50\xFF\x51\x14", 7) != 0) {
        Log("videos: lithtech.exe is not the expected version - videos left alone");
        return;
    }

    static const unsigned char NOPS[2] = {0x90, 0x90};
    WriteCode(LoadedAddress(VA_MOVIE_FULLSCREEN_JE), NOPS, 2);
    // push edx; push eax; call HookVideoBlit (the surface pointer in eax becomes its first argument)
    unsigned char call[7] = {0x52, 0x50, 0xE8, 0, 0, 0, 0};
    *(DWORD*)(call + 3) = (DWORD)HookVideoBlit - (blitCall + 7);
    WriteCode(blitCall, call, 7);
    Log("videos: %s", g_config.videoMode == VIDEO_STRETCH ? "stretched to the screen" : "scaled to fit, aspect kept");
}
