// Options -> Game Options: "HUD Scale" and "FOV" sliders and an "FOV Type" list, made from the
// game's own controls. Values come from the ini when the screen opens; Apply saves and applies them.
#pragma once
#include <windows.h>

// False when the screen's code is not the expected version (the screen is left unchanged).
bool InstallGameOptions();

// Before an options screen draws its children (called for every container).
void BeforeGameOptionsDraw(void* screen);
// Mouse messages in 800x600 space: a double-click on one of the sliders resets it.
void TrackSliderDoubleClick(DWORD msg, int x, int y);
