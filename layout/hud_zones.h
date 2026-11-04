// The HUD's zone layout. The 800x600 HUD is cut into 3 columns and up to 3 rows (config.h); each
// zone keeps its pieces' design positions relative to its own screen edge or the screen centre,
// scaled by the HUD size, so every piece sticks to its nearest edge or corner.
#pragma once
#include <windows.h>
#include "core/geometry.h"

// A zone (HudColumn, HudRow); column -1 means "decide for each draw call from its own rect".
struct HudZone { int column, row; };

HudZone GetZoneOfPiece(const Rect& designRect);
const char* GetZoneName(HudZone zone);    // "TL" .. "BR"

// A HUD x / y in a zone -> screen pixels.
float HudToScreenX(float x, int column);
float HudToScreenY(float y, int row);
// A real screen point -> the HUD design point whose zone's on-screen image holds it.
POINT ScreenToHudPoint(float x, float y);

// A whole window anchored like a HUD piece (by the zone of its rect's centre; column / row -1 =
// from the rect) and scaled by the HUD size, but kept in one piece.
Mapping GetHudWindowMapping(const Rect& designRect, int column, int row);
