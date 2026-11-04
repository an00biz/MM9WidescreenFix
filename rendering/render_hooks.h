// Hooks on the 2D render object. The game is told the screen is 800x600, so every widget, dialog
// and menu lays itself out as at 800x600; every draw onto the screen is then mapped to the real
// screen (layout.h), and unscaled blits become scaled ones.
#pragma once
#include <windows.h>

// The render object's vtable and the GetScreenRect call sites are the expected version.
bool HasExpectedRendererCode();
void InstallRenderHooks();

// Called from the draw path before mapping: reloads the config when it changed, takes the screen
// size (at most 10 times a second) and refreshes the current mapping.
void UpdateBeforeDraw();

// An 8x8 surface of one color, made once per color (and kept). NULL when the table is full.
void* GetSolidColorSurface(void* renderer, DWORD color, int flags);
