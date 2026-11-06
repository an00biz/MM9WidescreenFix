// The game's window tree: its "draw all children" (re-implemented, so each child's draw can be
// placed on the screen) and the controls' hit tests (re-implemented to trace them).
#pragma once

// The code about to be replaced is the expected version (checked before anything is patched).
bool HasExpectedWindowTreeCode();
void InstallWindowTreeHooks();
