// Input: the [KEYS] bindings, seen before the game and swallowed, and mouse points converted from
// the real screen into the game's 800x600 space.
#pragma once
#include "layout/layout.h"

// The last mouse point from the window proc, before and after conversion.
struct MouseState {
    int     realX, realY;     // client pixels
    bool    haveReal;
    Mapping mapping;          // the mapping it was converted with
    bool    haveMapping;
};
extern MouseState g_mouse;

// A message hook on the game thread for the bindings; called from the draw path.
void InstallInputHookOnce();

// The window proc's mouse code is the expected version (checked before anything is patched).
bool HasExpectedMouseCode();
// The window proc and cshell's ScreenToClient import convert mouse points.
void InstallMouseHooks();
