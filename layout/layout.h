// Where the game's 800x600 design space lands on the real screen: menus, dialogs and the cursor
// as one 4:3 block, HUD pieces in zones anchored to the screen edges, windows that follow the HUD
// size; and the way back for mouse points.
#pragma once
#include <windows.h>
#include "core/geometry.h"
#include "layout/hud_zones.h"


struct ScreenState {
    int   width, height;
    bool  known;                       // size taken from a draw call (never from the window proc)
    float hudScale;                    // the HUD size in use ...
    float hudScaleMin, hudScaleMax;    // ... and its range on this screen
    float uiScaleX, uiScaleY;          // the 4:3 block's scale
};
extern ScreenState g_screen;
extern Mapping     g_drawMapping;      // used right now by non-HUD draws
extern int         g_hudDrawDepth;     // > 0 while the HUD draws: its pieces use the zone layout
extern HudZone     g_hudZone;          // zone of the HUD piece being drawn
extern int         g_hudWindowSlot;    // window following the HUD size being drawn, -1 = none

void UpdateScreenSize();               // asks the render object; also refreshes the scales
void UpdateHudScale();
void UpdateDrawMapping();              // after a change of config, alignment or forced mapping

Mapping GetBlockMapping(int align);    // the 4:3 block with an Align
// A draw call's design rect -> screen rect, with the mapping that applies right now.
Rect    MapDesignRect(const Rect& designRect);
// The cursor may go anywhere on the real screen: that area in 800x600 coordinates, for every
// alignment and every HUD zone.
Rect    GetCursorClampRect();

// Mouse points: the mapping of the topmost window following the HUD size that recently drew
// under the point, else the 4:3 block (following an open aligned window).
Mapping GetMouseMappingAt(int x, int y);
int     GetMouseAlignment();
// The window with an alignment rule that is open (mouse input and the cursor follow it).
void    NoteAlignedWindowDrawn(void* window, DWORD windowClass, int align);
void    ForgetAlignedWindow(void* window);

// HUD size: 1.0 = original pixel size up to as large as fits; saved to the ini.
void SetHudSize(float size);
void StepHudSize(int notches);

// While alive, non-HUD draws use this mapping (a window following the HUD size, the cursor).
class ScopedDrawMapping {
public:
    explicit ScopedDrawMapping(const Mapping& mapping);
    ~ScopedDrawMapping();
private:
    Mapping savedMapping;
};

// While alive, the 4:3 block uses this alignment (a window with an alignment rule).
class ScopedAlignment {
public:
    explicit ScopedAlignment(int align);
    ~ScopedAlignment();
private:
    int savedAlign;
};

// While alive, a window following the HUD size draws: the screen area it covers is collected, so
// mouse points over it can later be converted with its mapping.
class ScopedHudWindowDraw {
public:
    ScopedHudWindowDraw(void* window, const Mapping& mapping);
    ~ScopedHudWindowDraw();
private:
    int     slot;
    Mapping mapping;
    int     savedSlot;
    bool    savedHaveArea;
    Rect    savedArea;
};
