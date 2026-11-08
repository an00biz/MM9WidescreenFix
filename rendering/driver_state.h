// The render driver's state as the 2D hooks need it: whether the engine's driver table and the
// loaded d3d.ren are the expected ones, how d3d.ren would draw a surface onto the screen right now,
// and moving a draw made outside its Direct3D 2D mode into that mode.
//
// d3d.ren draws a picture onto the screen as a Direct3D texture only between the game's
// Start3D / StartOptimized2D and EndOptimized2D / End3D (cshell does this once a frame around the
// interface). A draw made outside that bracket goes as a DirectDraw blit into the 3D back buffer;
// without a DirectDraw wrapper modern Windows then waits for the graphics card, ~2 ms per draw
// at 1920x1080 and more at higher resolutions. The game's item popup draws its text and tiles
// like that dozens of times a second.
#pragma once
#include <windows.h>

// lithtech.exe is the expected version: the driver table can be read through its pointer.
bool HasExpectedDriverTableCode();
// The engine's table of render driver functions (filled by d3d.ren), NULL when unknown.
BYTE* GetDriverTable();
// The image base of the loaded d3d.ren when it is the original one, else 0.
DWORD GetOriginalDriverBase();

// How the original d3d.ren would draw a surface onto the screen right now (DRAW_* flags);
// 0 when it can't be told (another renderer, or no renderer yet).
enum DrawState {
    DRAW_KNOWN       = 1,
    DRAW_TEXTURE     = 2,    // the surface has the driver's textures (optimized)
    DRAW_REUPLOAD    = 4,    // ... but changed since: the engine makes them again before drawing
    DRAW_TEXTURED_2D = 8,    // the driver draws 2D through Direct3D (inside StartOptimized2D)
    DRAW_IN_SCENE    = 16    // ... and is inside Start3D (BeginScene)
};
int GetDrawState(void* engineSurface);

// How many draws ScopedSceneDraw moved into the Direct3D 2D mode so far.
extern DWORD g_drawsMovedIntoScene;

// While alive, around one game draw of src onto the screen: when [UI] scene_draws is on, src has
// textures and d3d.ren is outside its Direct3D 2D mode, the mode is switched on (Start3D when
// needed, StartOptimized2D) and off again afterwards, so src is drawn as a texture.
class ScopedSceneDraw {
public:
    explicit ScopedSceneDraw(void* src);
    ~ScopedSceneDraw();
private:
    bool started3d, startedOptimized2d;
};
