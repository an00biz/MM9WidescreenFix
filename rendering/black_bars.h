// Black bars beside full-screen menus. Those menus cover only the 4:3 block; the rest of the
// screen keeps the engine's clear color, which is the fog color on foggy maps (grey bars). Each
// window tree's draws are watched to tell a full-screen menu from gameplay.
#pragma once
#include "core/geometry.h"

// What the current window tree draws (fed by the draw hooks).
void NoteDrawnPiece(const Rect& designRect, bool isHud);
void NoteWholeScreenFill();
void NoteScreenBackdropDrawn();    // a captured screen drawn back whole (frozen 3D view behind the loot window)
void NoteHudDrawn();
void NoteAlignedWindowInTree();    // a window with an alignment rule (the loot window) is open

// Around the game's "draw all children" of a container; the outermost one is a whole tree.
void BeginWindowTree();
void EndWindowTree(void* root);

// Paints the screen outside the 4:3 block black.
void PaintBlackBars();
