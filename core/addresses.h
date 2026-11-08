// Addresses inside the original cshell.dll as linked (preferred base 0x10000000) and inside
// lithtech.exe, plus the vtable offsets of the engine objects the fix calls or patches.
// Convert with LoadedAddress() before use: the original is normally relocated.
#pragma once
#include <windows.h>

const DWORD VA_IMAGE_BASE = 0x10000000;

// --- 2D render object (support\lithrender.cpp) ---
const DWORD VA_RENDERER_PTR    = 0x101735F4;   // global pointer to the render object
const DWORD VA_RENDERER_VTABLE = 0x100C9664;
enum RendererSlot {                            // (arguments) -> result
    RENDERER_CREATE_SURFACE  = 0x04,           // (w, h, flags) -> surface
    RENDERER_DELETE_SURFACE  = 0x08,           // (surface)
    RENDERER_BLIT            = 0x0C,           // (dest, x, y, src, srcRect*)
    RENDERER_SCALE           = 0x10,           // (dest, dstRect*, src, srcRect*)
    RENDERER_FILL            = 0x14,           // (dest, color, rect*, flags)
    RENDERER_GET_SIZE        = 0x2C,           // (surface, int* w, int* h)
    RENDERER_GET_SCREEN_RECT = 0x50,           // (Rect* out) -> {0, 0, W, H}
    RENDERER_GET_SCREEN      = 0x58            // () -> screen surface
};

// --- interface controls and windows (every window class derives from the control class) ---
enum ControlSlot {
    CONTROL_DESTRUCTOR = 0x00,                 // (flags)
    CONTROL_GET_RECT   = 0x04,                 // (Rect* out) -> rect on the 800x600 screen
    CONTROL_DRAW       = 0x08,                 // ()
    CONTROL_GET_ID     = 0x3C,                 // () -> id given at creation, -1 = none
    CONTROL_IS_HIDDEN  = 0xAC,                 // () -> char
    CONTROL_ADD_CHILD  = 0xD0                  // (child) - only containers have their own
};
const DWORD VA_CONTAINER_DRAW_CHILDREN = 0x10084F40;
const DWORD VA_CONTAINER_ADD_CHILD     = 0x10084AC0;   // in a vtable's ADD_CHILD slot: "is a container"
const DWORD VA_CONTROL_HIT_TEST        = 0x100010A0;   // point inside the control's rect
const DWORD VA_IMAGE_CONTROL_HIT_TEST  = 0x10012C10;   // the same, for image controls
// Window classes are identified by their vtable; all vtables lie in this range of the original.
const DWORD VA_VTABLES_START = 0x100BC000;
const DWORD VA_VTABLES_END   = 0x100CD000;
const DWORD VA_CODE_START    = 0x10001000;

// --- gameplay HUD (mmixhud.cpp) ---
const DWORD VA_HUD_VTABLE = 0x100BF3F0;
// HUD mouse handlers: 0x20 button down, 0x24 button up, 0x28..0x30 the other mouse events
const int HUD_MOUSE_SLOTS[5] = {0x20, 0x24, 0x28, 0x2C, 0x30};

// --- mouse ---
const DWORD VA_WNDPROC_MOUSE        = 0x1008E844;   // window proc: lParam -> global mouse point (31 bytes)
const DWORD VA_MOUSE_X              = 0x100DF798;   // global mouse point read by the whole interface
const DWORD VA_MOUSE_Y              = 0x100DF79C;
const DWORD VA_IMPORT_SCREEN_TO_CLIENT = 0x100BC180; // cshell's import slot for ScreenToClient
const DWORD VA_CURSOR_VTABLE        = 0x100C8A70;   // mouse cursor: arrow
const DWORD VA_ANIMATED_CURSOR_VTABLE = 0x100C8ABC; // mouse cursor: animated wand
const int   CURSOR_DRAW_SLOT        = 0x30;
// mouse cursor MoveTo: "if (x < 0) x = 0" and "if (y < 0) y = 0" (each a jge)
const DWORD VA_CURSOR_CLAMP_X       = 0x1008DB9F;
const DWORD VA_CURSOR_CLAMP_Y       = 0x1008DBAA;

// Return addresses of the GetScreenRect calls that must see the real screen size:
//   menus.cpp "screen size != autoexec resolution -> set the video mode again" (with 800x600 it
//   sets the mode forever, a stack overflow after the intro)
const DWORD VA_SCREEN_RECT_FROM_MODE_CHECK = 0x100376E0;
//   the mouse cursor's rectangle, which the cursor is clamped to (and the Windows cursor warped back)
const DWORD VA_SCREEN_RECT_FROM_CURSOR_1   = 0x100A4064;
const DWORD VA_SCREEN_RECT_FROM_CURSOR_2   = 0x1009DB4D;

// --- loading screen (menus.cpp) ---
const DWORD VA_LOADING_BACKGROUND_BLIT_RET = 0x10034D1B;   // the background picture's blit (in 10034CF0)
const DWORD VA_LOADING_BOAT_RESTORE_RET    = 0x10034ACD;   // the boat's "put back what was under me" blit

// --- camera and 3D view ---
const DWORD VA_LTCLIENT_PTR     = 0x100DFC4C;   // ILTClient, the engine's client interface
const DWORD VA_SET_CAMERA_RECT  = 0x10090D60;   // camera: set 3D view rect -> ILTClient SetCameraRect
const DWORD VA_GET_CAMERA_RECT  = 0x10090DA0;   // camera: get 3D view rect <- ILTClient GetCameraRect
const DWORD VA_CUTSCENE_CODE_START = 0x10091E00; // in-game cutscene camera code, with hardcoded
const DWORD VA_CUTSCENE_CODE_END   = 0x10092160; //   800x600 rects (full 0,0,800,600, letterbox 0,100,800,400)
const DWORD VA_CAMERA_OBJECT    = 0x100DF7D0;   // global camera object
const DWORD VA_CAMERA_APPLY_FOV = 0x10090E00;   // camera: set its FOV again
// "call [reg+0x2AC]" (SetCameraFOV) and "call [reg+0x2A8]" (GetCameraFOV), 6 bytes each
const DWORD VA_FOV_SET_MAIN     = 0x10090E5B;   // fovY = fovX * h / w (every normal FOV change)
const DWORD VA_FOV_SET_WOBBLE_1 = 0x100918F3;   // FOV wobble effects: read the FOV, nudge it, set it
const DWORD VA_FOV_SET_WOBBLE_2 = 0x10091AAE;
const DWORD VA_FOV_GET_WOBBLE_1 = 0x10091809;
const DWORD VA_FOV_GET_WOBBLE_2 = 0x100919C4;
enum LtClientSlot {
    LTCLIENT_START_3D          = 0x02C,         // () -> LT_OK: BeginScene
    LTCLIENT_START_OPTIMIZED_2D = 0x038,        // () -> LT_OK: 2D drawn as Direct3D textures (in 3D only)
    LTCLIENT_END_OPTIMIZED_2D  = 0x03C,         // ()
    LTCLIENT_END_3D            = 0x040,         // (): EndScene
    LTCLIENT_GET_RENDER_MODES  = 0x044,         // () -> RenderMode list
    LTCLIENT_FREE_RENDER_MODES = 0x048,         // (list)
    LTCLIENT_SET_MASTER_VOLUME = 0x0C4,         // (int volume)
    LTCLIENT_GET_VAR_FLOAT     = 0x1DC,         // (console variable) -> float
    LTCLIENT_RUN_CONSOLE       = 0x248,         // (command text)
    LTCLIENT_GET_CAMERA_FOV    = 0x2A8,         // (camera, float* x, float* y)
    LTCLIENT_SET_CAMERA_FOV    = 0x2AC,         // (camera, float x, float y)
    LTCLIENT_GET_CAMERA_RECT   = 0x2B0,         // (camera, int* fullscreen, int* l, int* t, int* r, int* b)
    LTCLIENT_SET_CAMERA_RECT   = 0x2B4          // (camera, int fullscreen, int l, int t, int r, int b)
};

// --- videos and sound ---
const DWORD VA_MOVIE_FULLSCREEN_JE = 0x100337EC;  // movie start: "fullscreen" only if the screen asks (je)
const DWORD EXE_BINK_BLIT          = 0x00402111;  // lithtech.exe, Bink fullscreen: surface->Blt(...)
const DWORD EXE_IMAGE_BASE         = 0x00400000;
const DWORD VA_MASTER_VOLUME_VAR   = 0x100DFDD0;  // console variable "MasterVolume"

// --- lithtech.exe: the render driver's function table (filled by d3d.ren's RenderDLLSetup) ---
const DWORD EXE_RENDER_TABLE_PTR = 0x00494A6C;    // -> the table, once a renderer is loaded
// The engine's "lock a surface" (0x0040AD45): for the screen it asks the driver to lock all of it,
// mov eax,[494A6Ch] ... call [eax+0E4h]
const DWORD EXE_LOCK_SURFACE_TABLE_LOAD = 0x0040AD6D;
const DWORD EXE_LOCK_SURFACE_SCREEN_CALL = 0x0040AD80;
// The engine's surface-to-surface draw, source = the screen: mov eax,[49DE3Ch] (BlitFromScreen)
const DWORD EXE_BLIT_FROM_SCREEN_LOAD = 0x0040BB7B;
// The engine's surface record (HSURFACE): the driver's surface at +0x0C, flags at +0x1C
// (2 = optimize asked for, 4 = changed since the driver's texture was made).
const int ENGINE_SURFACE_DRIVER = 0x0C;
const int ENGINE_SURFACE_FLAGS  = 0x1C;
// The original d3d.ren, offsets from its image base: BlitToScreen (in the table at
// RENDER_BLIT_TO_SCREEN), the two globals it checks before drawing a surface as a texture, and
// the texture list of its surfaces (non-NULL once optimized).
const DWORD D3D_BLIT_TO_SCREEN   = 0x0001C820;
const DWORD D3D_BACK_BUFFER_PTR  = 0x00094258;
const DWORD D3D_TEXTURED_2D      = 0x00095B34;
const DWORD D3D_IN_SCENE         = 0x00095B38;
const int   D3D_SURFACE_TEXTURES = 0x84;
const DWORD D3D_DIRECTDRAW_PTR   = 0x00094264;  // IDirectDraw7*
const DWORD D3D_DEVICE_PTR       = 0x00095CF8;  // IDirect3DDevice7*
const int   D3D_SURFACE_HEIGHT   = 0x0C;      // d3d.ren surface: height and width (GetSurfaceInfo)
const int   D3D_SURFACE_WIDTH    = 0x10;
enum RenderTableSlot {                            // all __cdecl
    RENDER_CREATE_SURFACE   = 0xC8,               // (w, h) -> driver surface
    RENDER_OPTIMIZE_SURFACE = 0xDC,               // (driver surface, transparent color) -> int: makes its textures
    RENDER_LOCK_SCREEN      = 0xE4,               // (left, top, right, bottom, void** bits, long* pitch) -> int
    RENDER_UNLOCK_SCREEN    = 0xE8,               // ()
    RENDER_BLIT_TO_SCREEN   = 0xEC,               // (request*): draws a surface onto the screen
    RENDER_BLIT_FROM_SCREEN = 0xFC                // (request*): copies part of the screen into a surface
};

// --- Start Game -> party creation screen (mmixcreatechar.cpp) ---
const DWORD VA_PARTY_CREATION_VTABLE = 0x100BE9BC;
const DWORD VA_PARTY_CREATION_DRAW   = 0x10015E80;
const int   PARTY_CREATION_PORTRAITS = 0xEC;          // the 4 portrait pictures (image controls)
const int   PORTRAIT_BUTTON_FIRST_ID = 1021;          // invisible buttons over them: ids 1021..1024
const DWORD VA_BUTTON_VTABLE         = 0x100BEB24;    // invisible button, shared by many menus
const int   BUTTON_SET_RECT          = 0xD0;          // (Rect*) - the rect at +0x34
const DWORD VA_BUTTON_SET_RECT       = 0x10085400;
const int   BUTTON_RECT              = 0x34;
const DWORD VA_IMAGE_CONTROL_VTABLE  = 0x100C1CD4;
const DWORD VA_IMAGE_CONTROL_GET_RECT = 0x10030450;

// --- windows with a default rule ---
const DWORD VA_LOOT_WINDOW_VTABLE = 0x100C13A0;   // chest + inventory (treasuremenu.cpp)

// --- game objects and assets ---
const DWORD VA_GAME_NEW     = 0x1008D320;   // operator new(size, file, line)
const DWORD VA_MAKE_LABEL   = 0x10030B50;   // (id, rect*, text, font, color, -1, 1) -> label
const DWORD VA_LOAD_PICTURE = 0x100A3F00;   // (file, 1, color key, 1.0f) -> surface
const DWORD VA_ASSETS       = 0x100DF8A8;   // asset manager
const DWORD VA_GET_COLOR    = 0x100A1A70;   // asset manager: (index) -> color
const DWORD VA_GET_FONT     = 0x100A2050;   // asset manager: (id) -> font
const DWORD VA_SLIDER_CONSTRUCTOR = 0x10031500;   // slider, 0x84 bytes: (id)
const DWORD VA_LIST_CONSTRUCTOR   = 0x100380E0;   // list box, 0xAC bytes: (renderer, 4, 0)
const DWORD VA_SCROLL_BAR_CONSTRUCTOR    = 0x10002D50;   // 0xA0 bytes: (list, renderer, -1)
const DWORD VA_SCROLL_UP_CONSTRUCTOR     = 0x10002E50;   // 0x64 bytes: (bar, renderer, -1)
const DWORD VA_SCROLL_DOWN_CONSTRUCTOR   = 0x10002F30;   // 0x64 bytes: (bar, renderer, -1)
const DWORD VA_SCROLL_HANDLE_CONSTRUCTOR = 0x10002F60;   // 0x94 bytes: (bar, renderer, -1)
const DWORD VA_SCROLL_HANDLE_VTABLE      = 0x100BC894;
const DWORD VA_SPRITE_SAVE_BACKGROUND    = 0x10087A80;   // the scroll handle's slot 0xE8
enum OptionsControlSlot {
    SLIDER_INIT       = 0x110,   // (handle, 0, min, max, x, y, w, h)
    SLIDER_SET_VALUE  = 0x114,
    SLIDER_GET_VALUE  = 0x118,
    LIST_INIT         = 0x0EC,   // (x, y, w, h, font, color, -1, color, selected color, 0)
    LIST_ADD          = 0x0F0,   // (text, 0, 0)
    LIST_SET_SELECTED = 0x114,
    LIST_GET_SELECTED = 0x118,
    SCROLL_BAR_INIT    = 0x110,  // (x, y, w, h, 0, up, down, handle)
    SCROLL_BUTTON_INIT = 0x0D8,  // (POINT*, normal, pressed, highlight, 0)
    SCROLL_HANDLE_INIT = 0x0D0,  // (surface, 0, 0, 0)
    SPRITE_SAVE_BACKGROUND = 0x0E8
};
const int SCREEN_CONTROL_CLICKED = 0x5C;   // options screen vtable: (control) clicked
const int SCREEN_BACKGROUND      = 0x70;   // options screen: its background picture

// --- Options -> Game Options screen (OptionsMenus.cpp) ---
const DWORD VA_GAME_OPTIONS_INIT_CALL = 0x1003D317;   // "call Init" in the screen's create function
const DWORD VA_GAME_OPTIONS_INIT      = 0x1003D320;   // labels, checkboxes, sliders, crosshair buttons
const DWORD VA_GAME_OPTIONS_VTABLE    = 0x100C51B0;
const DWORD VA_GAME_OPTIONS_CLICKED   = 0x1003DFB0;
enum GameOptionsControl { GAME_OPTIONS_DEFAULTS = 0xB, GAME_OPTIONS_APPLY = 0xC, GAME_OPTIONS_CANCEL = 0xD };

// --- Options -> Video Options screen ---
const DWORD VA_VIDEO_OPTIONS_INIT_CALL = 0x1003EEA9;
const DWORD VA_VIDEO_OPTIONS_INIT      = 0x1003EED0;
const DWORD VA_VIDEO_OPTIONS_VTABLE    = 0x100C55D0;
const DWORD VA_VIDEO_OPTIONS_CLICKED   = 0x1003FC60;
enum VideoOptionsControl { VIDEO_OPTIONS_APPLY = 0, VIDEO_OPTIONS_CANCEL = 1, VIDEO_OPTIONS_DEFAULTS = 2 };
const int VIDEO_OPTIONS_MODE    = 0x1E4;   // the RenderMode the screen shows
const int VIDEO_OPTIONS_CHANGED = 0x614;   // set: Apply saves autoexec.cfg and switches the mode

// The engine's render mode record (0x214 bytes), a linked list from LTCLIENT_GET_RENDER_MODES.
struct RenderMode {
    DWORD       unknown;
    char        renderDll[256];
    char        internalName[128];
    char        description[128];
    int         width, height, bitDepth;
    RenderMode* next;
};
