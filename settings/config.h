// The settings from cshell_hud.ini: their in-memory form, loading (re-read while the game runs)
// and saving the values the game changes (HUD size, FOV).
#pragma once
#include <windows.h>
#include "core/geometry.h"
#include "settings/key_bindings.h"

enum Align     { ALIGN_NONE = -1, ALIGN_CENTER = 0, ALIGN_LEFT = 1, ALIGN_RIGHT = 2 };
enum FovMode   { FOV_ORIGINAL = 0, FOV_HOR_PLUS = 1, FOV_CORRECT = 2 };
enum VideoMode { VIDEO_ORIGINAL = 0, VIDEO_FIT = 1, VIDEO_STRETCH = 2 };
// HUD zones: the screen is split into 3 columns and up to 3 rows, each anchored to its own edge.
enum HudColumn { COLUMN_LEFT = 0, COLUMN_CENTER = 1, COLUMN_RIGHT = 2 };
enum HudRow    { ROW_TOP = 0, ROW_MIDDLE = 1, ROW_BOTTOM = 2 };

// A window class is named by its vtable address in the original as linked (e.g. 100C13A0).
struct AlignRule      { DWORD windowClass; int align; };
struct HudWindowRule  { DWORD windowClass; int column, row; };   // -1: from the window's rect

const int MAX_ALIGN_RULES = 32, MAX_HUD_WINDOW_RULES = 16, MAX_BOTTOM_RIGHT_PINS = 8;

struct Config {
    // [HUD]
    bool  enable;              // read once, at game start (g_enabled)
    float hudSize;             // 1.0 = original pixel size; 0 = "max", as large as fits
    float hudSizeStep;         // per wheel notch
    float marginX, marginY;    // gap between the HUD and the screen edges, screen pixels
    // Zone borders in the 800x600 HUD. Columns split at x splitLeft / splitRight; the centre
    // column has rows split at y splitTop / splitBottom. The side columns have only a top and a
    // bottom row, split at splitSideLeft / splitSideRight (party portraits and gold on the left,
    // compass and hired NPC portraits on the right). On the right the 3rd NPC portrait reaches
    // as low as the Identify Monster window, so there pieces below splitSideRightLow always go to
    // the bottom, and pieces between the two splits only when they start left of npcColumnX.
    float splitLeft, splitRight, splitTop, splitBottom;
    float splitSideLeft, splitSideRight, splitSideRightLow, npcColumnX;
    // Pieces always pinned to the bottom-right corner (turn-based hourglass and hand, which sit
    // where the 3rd NPC portrait would): same top-left corner, no bigger than the box.
    Rect  bottomRightPins[MAX_BOTTOM_RIGHT_PINS];
    int   bottomRightPinCount;
    // Windows that follow the HUD size (dialogue, combat history, real-time combat log).
    HudWindowRule hudWindows[MAX_HUD_WINDOW_RULES];
    int   hudWindowCount;
    bool  hudMouse;            // convert mouse input for the HUD's corner layout
    bool  dump, mouseLog, perf;   // troubleshooting logs

    // [UI] menus, dialogs, cursor
    float uiScale;             // size of the 4:3 block, like hudSize
    bool  uiStretched;         // mode=stretched: the whole screen, uiScale and align ignored
    int   uiAlign;             // Align for screens without a rule
    AlignRule alignRules[MAX_ALIGN_RULES];
    int   alignRuleCount;
    bool  blackBars;           // paint the sides of full-screen menus black
    bool  barsLog;             // log what the full-screen menu detection sees

    // [FOV]
    int   fovMode;             // FovMode
    float fovScale;            // multiplier on the view size (the tangent of the half angles)
    float fovScaleMin, fovScaleMax;

    // [VIDEO] and [SOUND]
    int   videoMode;           // VideoMode, read at game start
    int   cameraNudge;         // start a full-screen 3D view this many pixels above / left of the screen
    bool  applyVolume;         // apply the saved master volume at game start

    // [KEYS]
    KeyBinding hudSizeKey, hudSizeResetKey, fovKey, fovResetKey;
    float fovStep;
    bool  fovWheelInverted;
};

extern Config g_config;
// The fix is on: [HUD] enable at game start, and the original dll is the expected version.
extern bool g_enabled;
// [HUD] dump went from 0 to 1: dump the next HUD frame to the log.
extern bool g_dumpRequested;

// atStart: read unconditionally and take "enable"; otherwise only when the file changed
// (checked at most twice a second).
void LoadConfig(bool atStart);
void SaveHudSize(float size, bool isMax);
void SaveFovScale(float scale);
void SaveFovMode(int mode);

// The [UI] alignment rule for a window class, ALIGN_NONE if it has none.
int FindAlignRule(DWORD windowClass);
const char* AlignName(int align);     // "center", "left", "right", "" for ALIGN_NONE
const char* FovModeName(int mode);    // as written in the ini: "original", "hor+", "correct"
