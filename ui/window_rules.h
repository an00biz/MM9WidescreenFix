// Draw hooks on window classes: the gameplay HUD (zone layout, mouse input converted for it),
// windows with an [UI] alignment rule, windows that follow the HUD size, and the mouse cursor.
#pragma once

// The vtable slots about to be replaced hold the expected functions.
bool HasExpectedHudAndCursorCode();
void InstallHudHooks();
void InstallCursorHooks();
// The window classes named in [UI] align_* / loot_right and [HUD] windows (read at game start).
void InstallWindowRules();
