// Tells full-screen menus from gameplay by what a window tree draws, and paints the screen beside
// the 4:3 block black for them. [UI] bars_log=1 logs what the detection sees.
#include "rendering/black_bars.h"
#include "core/addresses.h"
#include "core/cshell_hud.h"
#include "settings/config.h"
#include "diagnostics/diagnostics.h"
#include "layout/layout.h"
#include "core/log.h"
#include "rendering/render_hooks.h"
#include "rendering/renderer.h"

// A big piece drawn in this tree (kept for the bars log only).
struct BigPiece {
    Rect rect;
    int  treeDepth, hudDrawDepth, hudWindowSlot;
    char kind;    // 'h' HUD, 'm' menu, 's' screen backdrop, 'f' whole-screen fill
};
static const int MAX_BIG_PIECES = 16;
static const double DESIGN_AREA = 800.0 * 600.0;

static int      g_treeDepth = 0;
static double   g_coveredArea = 0;        // big non-HUD pieces drawn in this tree, design units
static bool     g_wholePictureSeen = false;   // a whole 800x600 picture (a menu background)
static bool     g_hudSeen = false;        // the gameplay HUD was drawn: a gameplay frame
static bool     g_alignedWindowSeen = false;  // a window over the 3D view (loot window)
static bool     g_backdropSeen = false;
static BigPiece g_bigPieces[MAX_BIG_PIECES];
static int      g_bigPieceCount = 0;
static int      g_drawCount = 0;

static void NoteBigPiece(const Rect& r, char kind)
{
    g_drawCount++;
    if (!g_config.barsLog || r.w < 200 || r.h < 200 || g_bigPieceCount >= MAX_BIG_PIECES) return;
    BigPiece& piece = g_bigPieces[g_bigPieceCount++];
    piece.rect = r;
    piece.treeDepth = g_treeDepth;
    piece.hudDrawDepth = g_hudDrawDepth;
    piece.hudWindowSlot = g_hudWindowSlot;
    piece.kind = kind;
}

void NoteDrawnPiece(const Rect& r, bool isHud)
{
    if (isHud) {
        NoteBigPiece(r, 'h');
        return;
    }
    bool isBig = r.w >= 200 && r.h >= 200;
    if (g_treeDepth > 0 && g_hudWindowSlot < 0 && isBig) g_coveredArea += (double)r.w * r.h;
    NoteBigPiece(r, 'm');
    if (g_hudWindowSlot < 0 && CoversDesignScreen(r)) g_wholePictureSeen = true;
}

void NoteWholeScreenFill()     { NoteBigPiece(MakeRect(0, 0, 800, 600), 'f'); }
void NoteHudDrawn()            { g_hudSeen = true; }
void NoteAlignedWindowInTree() { g_alignedWindowSeen = true; }

void NoteScreenBackdropDrawn()
{
    NoteBigPiece(MakeRect(0, 0, 800, 600), 's');
    g_backdropSeen = true;
}

void BeginWindowTree()
{
    if (g_treeDepth == 0) g_coveredArea = 0;
    g_treeDepth++;
}

static void LogTree(void* root)
{
    static DWORD lastLog = 0;
    if (GetTickCount() - lastLog <= 2000) return;
    lastLog = GetTickCount();
    Log("bars check: root %08lX cover %.0f%% draws %d big pieces %d", LinkedAddress((DWORD)GetVtable(root)),
        g_coveredArea * 100.0 / DESIGN_AREA, g_drawCount, g_bigPieceCount);
    for (int i = 0; i < g_bigPieceCount; i++) {
        const BigPiece& piece = g_bigPieces[i];
        Log("   %c (%d,%d %dx%d) depth %d hud %d hudwin %d", piece.kind, piece.rect.x, piece.rect.y,
            piece.rect.w, piece.rect.h, piece.treeDepth, piece.hudDrawDepth, piece.hudWindowSlot);
    }
}

// Gameplay draws the HUD, the loot window or a captured 3D view; a menu covers the block.
static bool IsFullScreenMenu(void* root)
{
    bool isGameplay = g_hudSeen || g_alignedWindowSeen || g_backdropSeen;
    bool isLootWindow = GetVtable(root) == (void**)LoadedAddress(VA_LOOT_WINDOW_VTABLE);
    bool coversBlock = g_wholePictureSeen || g_coveredArea >= 0.75 * DESIGN_AREA;
    return !isGameplay && !isLootWindow && coversBlock;
}

void EndWindowTree(void* root)
{
    g_treeDepth--;
    if (g_treeDepth > 0) return;

    if (g_config.barsLog) LogTree(root);
    if (g_enabled && g_config.blackBars && g_screen.known && IsFullScreenMenu(root)) {
        static DWORD lastLog = 0;
        if (g_dump.active || GetTickCount() - lastLog > 60000) {
            Log("black bars: full-screen menu %08lX (%s)", LinkedAddress((DWORD)GetVtable(root)),
                g_wholePictureSeen ? "whole 800x600 picture" : "big pieces");
            lastLog = GetTickCount();
        }
        UpdateBeforeDraw();
        PaintBlackBars();
    }

    // The next check sees what is drawn after this root and in the next one.
    g_bigPieceCount = 0;
    g_drawCount = 0;
    g_wholePictureSeen = false;
    g_hudSeen = false;
    g_alignedWindowSeen = false;
    g_backdropSeen = false;
}

static int ClampInt(int v, int low, int high)
{
    if (v < low)
        return low;
    else if (v > high)
        return high;
    else
        return v;
}

void PaintBlackBars()
{
    void* renderer = Renderer::Get();
    if (renderer == NULL) return;
    void* screen = Renderer::GetScreen(renderer);
    if (screen == NULL) return;
    void* black = GetSolidColorSurface(renderer, 0, 0);
    if (black == NULL) return;

    const Mapping& m = g_drawMapping;
    int left   = ClampInt(Round(m.offsetX), 0, g_screen.width);
    int top    = ClampInt(Round(m.offsetY), 0, g_screen.height);
    int right  = ClampInt(Round(m.offsetX + 800.0f * m.scaleX), 0, g_screen.width);
    int bottom = ClampInt(Round(m.offsetY + 600.0f * m.scaleY), 0, g_screen.height);
    Rect bars[4] = {
        MakeRect(0, 0, g_screen.width, top),                         // above
        MakeRect(0, bottom, g_screen.width, g_screen.height - bottom),   // below
        MakeRect(0, top, left, bottom - top),                        // left
        MakeRect(right, top, g_screen.width - right, bottom - top)}; // right
    for (int i = 0; i < 4; i++)
        if (!IsEmpty(bars[i])) Renderer::Scale(renderer, screen, bars[i], black, NULL);
}
