// The HUD's zone layout: which zone a piece belongs to, where a zone lands on the screen, and the
// way back from a screen point to a HUD point. Zone borders and pins come from [HUD] (config.h).
#include "layout/hud_zones.h"
#include "settings/config.h"
#include "layout/layout.h"

static int GetZoneColumn(float centerX)
{
    if (centerX < g_config.splitLeft)
        return COLUMN_LEFT;
    else if (centerX > g_config.splitRight)
        return COLUMN_RIGHT;
    else
        return COLUMN_CENTER;
}

static float GetSideSplit(int column)
{
    if (column == COLUMN_LEFT)
        return g_config.splitSideLeft;
    else
        return g_config.splitSideRight;
}

// left < 0: a whole window, placed by its centre's y only.
static int GetZoneRow(float centerY, int column, float left = -1.0f)
{
    if (column == COLUMN_LEFT || (column == COLUMN_RIGHT && left < 0.0f))
        return centerY > GetSideSplit(column) ? ROW_BOTTOM : ROW_TOP;

    if (column == COLUMN_RIGHT) {
        bool belowEverything = centerY > g_config.splitSideRightLow;
        bool belowSplitLeftOfNpcs = centerY > g_config.splitSideRight && left < g_config.npcColumnX;
        return belowEverything || belowSplitLeftOfNpcs ? ROW_BOTTOM : ROW_TOP;
    }

    if (centerY < g_config.splitTop)
        return ROW_TOP;
    else if (centerY > g_config.splitBottom)
        return ROW_BOTTOM;
    else
        return ROW_MIDDLE;
}

static bool IsInPinBox(float x, float y)
{
    for (int i = 0; i < g_config.bottomRightPinCount; i++) {
        const Rect& pin = g_config.bottomRightPins[i];
        if (x >= pin.x && y >= pin.y && x < pin.x + pin.w && y < pin.y + pin.h)
            return true;
    }
    return false;
}

// A drawn piece with the same top-left corner as a pin box and no bigger (the 3rd NPC's health
// bar lies inside the box too, but starts elsewhere and stays with its portrait). A piece
// without width is judged by its corner alone.
static bool IsPinnedPiece(const Rect& piece)
{
    if (piece.w <= 0)
        return IsInPinBox((float)piece.x, (float)piece.y);
    for (int i = 0; i < g_config.bottomRightPinCount; i++) {
        const Rect& pin = g_config.bottomRightPins[i];
        if (piece.x == pin.x && piece.y == pin.y && piece.w <= pin.w && piece.h <= pin.h)
            return true;
    }
    return false;
}

HudZone GetZoneOfPiece(const Rect& r)
{
    HudZone zone = {COLUMN_RIGHT, ROW_BOTTOM};
    if (IsPinnedPiece(r))
        return zone;
    zone.column = GetZoneColumn(r.x + r.w * 0.5f);
    zone.row = GetZoneRow(r.y + r.h * 0.5f, zone.column, (float)r.x);
    return zone;
}

const char* GetZoneName(HudZone zone)
{
    static const char* NAMES[3][3] = {{"TL", "TC", "TR"}, {"ML", "MC", "MR"}, {"BL", "BC", "BR"}};
    return NAMES[zone.row][zone.column];
}

// Each zone keeps its design position relative to its own screen edge (or the centre).
float HudToScreenX(float x, int column)
{
    if (column == COLUMN_LEFT)
        return x * g_screen.hudScale + g_config.marginX;
    else if (column == COLUMN_RIGHT)
        return g_screen.width - (800.0f - x) * g_screen.hudScale - g_config.marginX;
    else
        return g_screen.width * 0.5f + (x - 400.0f) * g_screen.hudScale;
}

float HudToScreenY(float y, int row)
{
    if (row == ROW_TOP)
        return y * g_screen.hudScale + g_config.marginY;
    else if (row == ROW_BOTTOM)
        return g_screen.height - (600.0f - y) * g_screen.hudScale - g_config.marginY;
    else
        return g_screen.height * 0.5f + (y - 300.0f) * g_screen.hudScale;
}

static float ScreenToHudX(float x, int column)
{
    if (column == COLUMN_LEFT)
        return (x - g_config.marginX) / g_screen.hudScale;
    else if (column == COLUMN_RIGHT)
        return 800.0f - (g_screen.width - g_config.marginX - x) / g_screen.hudScale;
    else
        return (x - g_screen.width * 0.5f) / g_screen.hudScale + 400.0f;
}

static float ScreenToHudY(float y, int row)
{
    if (row == ROW_TOP)
        return (y - g_config.marginY) / g_screen.hudScale;
    else if (row == ROW_BOTTOM)
        return 600.0f - (g_screen.height - g_config.marginY - y) / g_screen.hudScale;
    else
        return (y - g_screen.height * 0.5f) / g_screen.hudScale + 300.0f;
}

// A zone's design span (from..to on one axis).
struct ZoneSpan { float from, to; int zone; };

// The zone whose span, as drawn on screen, holds the screen coordinate (or comes nearest).
static int FindZoneAt(float screenValue, const ZoneSpan* spans, int count, bool isX)
{
    int best = 0;
    float bestDistance = 1e9f;
    for (int i = 0; i < count; i++) {
        float low  = isX ? HudToScreenX(spans[i].from, spans[i].zone) : HudToScreenY(spans[i].from, spans[i].zone);
        float high = isX ? HudToScreenX(spans[i].to, spans[i].zone)   : HudToScreenY(spans[i].to, spans[i].zone);
        float distance = 0.0f;
        if (screenValue < low)
            distance = low - screenValue;
        else if (screenValue > high)
            distance = screenValue - high;
        if (distance < bestDistance) {
            bestDistance = distance;
            best = i;
        }
    }
    return spans[best].zone;
}

static int FindColumnAtScreenX(float x)
{
    ZoneSpan spans[3] = {{0, g_config.splitLeft, COLUMN_LEFT},
                         {g_config.splitLeft, g_config.splitRight, COLUMN_CENTER},
                         {g_config.splitRight, 800, COLUMN_RIGHT}};
    return FindZoneAt(x, spans, 3, true);
}

static int FindRowAtScreenY(float y, int column)
{
    if (column == COLUMN_CENTER) {
        ZoneSpan spans[3] = {{0, g_config.splitTop, ROW_TOP},
                             {g_config.splitTop, g_config.splitBottom, ROW_MIDDLE},
                             {g_config.splitBottom, 600, ROW_BOTTOM}};
        return FindZoneAt(y, spans, 3, false);
    } else {
        float split = GetSideSplit(column);
        ZoneSpan spans[2] = {{0, split, ROW_TOP}, {split, 600, ROW_BOTTOM}};
        return FindZoneAt(y, spans, 2, false);
    }
}

POINT ScreenToHudPoint(float x, float y)
{
    int column = FindColumnAtScreenX(x);
    POINT hud;
    hud.x = Round(ScreenToHudX(x, column));
    hud.y = Round(ScreenToHudY(y, FindRowAtScreenY(y, column)));

    // Right column: its rows also depend on x (see Config), so take the row whose own rule
    // places the point there.
    if (column == COLUMN_RIGHT) {
        float asTop = ScreenToHudY(y, ROW_TOP), asBottom = ScreenToHudY(y, ROW_BOTTOM);
        if (IsInPinBox((float)hud.x, asBottom))
            hud.y = Round(asBottom);
        else if (GetZoneRow(asTop, COLUMN_RIGHT, (float)hud.x) == ROW_TOP && !IsInPinBox((float)hud.x, asTop))
            hud.y = Round(asTop);
        else if (GetZoneRow(asBottom, COLUMN_RIGHT, (float)hud.x) == ROW_BOTTOM)
            hud.y = Round(asBottom);
    }
    return hud;
}

Mapping GetHudWindowMapping(const Rect& r, int column, int row)
{
    if (column < 0) column = GetZoneColumn(r.x + r.w * 0.5f);
    if (row < 0) row = GetZoneRow(r.y + r.h * 0.5f, column);
    Mapping m;
    m.scaleX = g_screen.hudScale;
    m.scaleY = g_screen.hudScale;
    m.offsetX = HudToScreenX(0.0f, column);
    m.offsetY = HudToScreenY(0.0f, row);
    return m;
}
