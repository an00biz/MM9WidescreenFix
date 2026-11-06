// Keeps real-resolution copies of what the game copies from the screen, and draws them in place
// of the game's (800x600-sized) surfaces.
#include "rendering/screen_copies.h"
#include "rendering/black_bars.h"
#include "diagnostics/diagnostics.h"
#include "layout/layout.h"
#include "rendering/render_hooks.h"
#include "rendering/renderer.h"

static const int MAX_SCREEN_COPIES = 64;
static ScreenCopy g_copies[MAX_SCREEN_COPIES];
static int        g_copyCount = 0;

static Rect GetRealScreen() { return MakeRect(0, 0, g_screen.width, g_screen.height); }

ScreenCopy* FindScreenCopy(void* gameSurface)
{
    for (int i = 0; i < g_copyCount; i++)
        if (g_copies[i].gameSurface == gameSurface)
            return &g_copies[i];
    return NULL;
}

void ForgetScreenCopy(void* renderer, void* gameSurface)
{
    ScreenCopy* copy = FindScreenCopy(gameSurface);
    if (copy == NULL) return;
    Renderer::DeleteSurface(renderer, copy->surface);
    *copy = g_copies[--g_copyCount];
}

// The copy for a game surface, sized w x h (an existing one of another size is replaced);
// NULL when the table is full or the surface can't be made.
static ScreenCopy* GetCopyOfSize(void* renderer, void* gameSurface, int w, int h)
{
    ScreenCopy* copy = FindScreenCopy(gameSurface);
    if (copy != NULL && (copy->w != w || copy->h != h)) {
        ForgetScreenCopy(renderer, gameSurface);
        copy = NULL;
    }
    if (copy != NULL || g_copyCount >= MAX_SCREEN_COPIES)
        return copy;

    void* surface = Renderer::CreateSurface(renderer, w, h);
    if (surface == NULL)
        return NULL;
    copy = &g_copies[g_copyCount++];
    copy->gameSurface = gameSurface;
    copy->surface = surface;
    copy->w = w;
    copy->h = h;
    return copy;
}

char CopyFromScreen(void* renderer, void* dest, const Rect* dstRect, const Rect* srcRect)
{
    PerfTimer timer(PERF_READ_BACK, dest);
    UpdateBeforeDraw();
    Rect designSrc = srcRect != NULL ? *srcRect : MakeRect(0, 0, 800, 600);
    bool wholeScreen = CoversDesignScreen(designSrc) && g_hudDrawDepth == 0;
    Rect realSrc = wholeScreen ? GetRealScreen() : MapDesignRect(designSrc);
    if (IsEmpty(realSrc))
        return 1;

    void* screen = Renderer::GetScreen(renderer);
    char result = 1;
    ScreenCopy* copy = GetCopyOfSize(renderer, dest, realSrc.w, realSrc.h);
    if (copy != NULL) {
        copy->wholeScreen = wholeScreen;
        copy->designW = designSrc.w;
        copy->designH = designSrc.h;
        result = Renderer::Blit(renderer, copy->surface, 0, 0, screen, &realSrc);
    }

    // Keep the game's own surface roughly right too (used only if drawn somewhere else).
    Rect gameDst = dstRect != NULL ? *dstRect : MakeRect(0, 0, designSrc.w, designSrc.h);
    Rect part = MakeRect(realSrc.x, realSrc.y, realSrc.w < gameDst.w ? realSrc.w : gameDst.w,
                         realSrc.h < gameDst.h ? realSrc.h : gameDst.h);
    Renderer::Blit(renderer, dest, gameDst.x, gameDst.y, screen, &part);
    return result;
}

// srcRect is a part of the game surface (800x600 units): the same part of the copy.
static Rect ToCopyPixels(const ScreenCopy& copy, const Rect* srcRect)
{
    if (srcRect == NULL || copy.designW <= 0 || copy.designH <= 0)
        return MakeRect(0, 0, copy.w, copy.h);
    float scaleX = (float)copy.w / copy.designW, scaleY = (float)copy.h / copy.designH;
    int left = Round(srcRect->x * scaleX), top = Round(srcRect->y * scaleY);
    return MakeRect(left, top, Round((srcRect->x + srcRect->w) * scaleX) - left,
                    Round((srcRect->y + srcRect->h) * scaleY) - top);
}

char DrawScreenCopy(void* renderer, void* screen, const ScreenCopy& copy, const Rect& designRect, const Rect* srcRect)
{
    Rect out, copySrc;
    if (copy.wholeScreen) {
        bool drawnWhole = CoversDesignScreen(designRect) && (srcRect == NULL || CoversDesignScreen(*srcRect));
        if (drawnWhole) {
            out = GetRealScreen();
            NoteScreenBackdropDrawn();
        } else {
            out = MapDesignRect(designRect);
        }
        copySrc = out;               // a screen capture: the same place on screen
    } else {
        out = MapDesignRect(designRect);
        copySrc = ToCopyPixels(copy, srcRect);
    }

    if (out.w == copySrc.w && out.h == copySrc.h)
        return Renderer::Blit(renderer, screen, out.x, out.y, copy.surface, &copySrc);
    else
        return Renderer::Scale(renderer, screen, out, copy.surface, &copySrc);
}
