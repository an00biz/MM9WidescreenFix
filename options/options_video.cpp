// Adds a Resolution list to Options -> Video Options. The list holds the modes the renderer reports
// for the current display adapter and bit depth (the engine's own list, the same the Display
// Adapter list is built from), with Windows' list as the fallback. Its scroll bar is the Keyboard
// Setup screen's (up / down buttons and a handle); its box is built from the Display Adapter box
// in OptionsVideoBG.pcx, the screen's own background.
#include <stdio.h>
#include <string.h>
#include "options/options_video.h"
#include "core/addresses.h"
#include "settings/config.h"
#include "core/cshell_hud.h"
#include "core/log.h"
#include "options/options_controls.h"
#include "core/patching.h"
#include "rendering/renderer.h"

#if __cplusplus >= 201103L || (defined(_MSC_VER) && _MSC_VER >= 1600)
static_assert(sizeof(RenderMode) == 0x214, "RenderMode must match the engine's record");
#endif

// The box on screen (800x600 space), the frame's outer (dark) edges: light frame lines at x 488 /
// 744 and y 336 (top), 361 (under the header), 503 (bottom).
const int BOX_LEFT = 487, BOX_RIGHT = 745, BOX_TOP = 335, BOX_BOTTOM = 504;
// The Display Adapter box in OptionsVideoBG.pcx (picture x = screen x - 250): outer edges x
// 282..745, y 104..257; header line at y 130; scroll column line at x 725; inside, rows alternate
// black / grey (8,8,8) in 20 px bands. Copied: a left part (199 px wide) and a right part (60 px,
// the scroll column) of the header rows 104..131, of a plain black band (rows 174..193) repeated
// over the list area, and of the bottom edge rows 255..257; the grey bands are painted on top.
// The picture's scroll arrows are left out: real scroll buttons are drawn there.
const int SRC_LEFT_X = 282 - OPTIONS_BACKGROUND_X, LEFT_W = 199;
const int SRC_RIGHT_X = 686 - OPTIONS_BACKGROUND_X, RIGHT_W = 60;
const int SRC_HEADER_Y = 104, HEADER_H = 28, SRC_BAND_Y = 174, BAND_H = 20, SRC_EDGE_Y = 255, EDGE_H = 3;
const int LIST_X = BOX_LEFT + 3, LIST_Y = BOX_TOP + HEADER_H, LIST_W = 724 - LIST_X;
const int LIST_H = (BOX_BOTTOM - 2) - LIST_Y;
const int SCROLL_X = 727, SCROLL_W = 16;

typedef char  (__fastcall *ClickedFn)(void* screen, void* edx, void* control);
typedef void* (__fastcall *ScrollPartConstructorFn)(void* self, void* edx, void* owner, void* renderer, int minusOne);
typedef char  (__fastcall *ScrollBarInitFn)(void* self, void* edx, int x, int y, int w, int h, int zero, void* up, void* down, void* handle);
typedef char  (__fastcall *ScrollButtonInitFn)(void* self, void* edx, POINT* position, void* normal, void* pressed, void* highlight, int zero);
typedef char  (__fastcall *ScrollHandleInitFn)(void* self, void* edx, void* picture, int zero1, int zero2, int zero3);
typedef char  (__fastcall *AddChildFn)(void* self, void* edx, void* child);
typedef RenderMode* (*GetRenderModesFn)();
typedef void  (*FreeRenderModesFn)(RenderMode* list);
typedef int   (*RunConsoleFn)(const char* command);

static MethodFn  g_unhookedInit = NULL;
static ClickedFn g_unhookedClicked = NULL;
static void* g_openScreen = NULL;
static void* g_resolutionList = NULL;

struct Resolution { int w, h; char text[16]; };
static const int MAX_RESOLUTIONS = 128;
static Resolution g_resolutions[MAX_RESOLUTIONS];
static int g_resolutionCount = 0;

static RenderMode* GetScreenMode(void* screen) { return (RenderMode*)((char*)screen + VIDEO_OPTIONS_MODE); }

// --- the list of resolutions ---------------------------------------------------------------------

static void AddResolution(int w, int h)
{
    if (w < 640 || h < 480 || g_resolutionCount >= MAX_RESOLUTIONS) return;
    for (int i = 0; i < g_resolutionCount; i++)
        if (g_resolutions[i].w == w && g_resolutions[i].h == h) return;
    g_resolutions[g_resolutionCount].w = w;
    g_resolutions[g_resolutionCount].h = h;
    g_resolutionCount++;
}

static bool IsSmaller(const Resolution& a, const Resolution& b) { return a.w < b.w || (a.w == b.w && a.h < b.h); }

static void SortResolutions()
{
    for (int i = 1; i < g_resolutionCount; i++)
        for (int j = i; j > 0 && IsSmaller(g_resolutions[j], g_resolutions[j - 1]); j--) {
            Resolution swap = g_resolutions[j];
            g_resolutions[j] = g_resolutions[j - 1];
            g_resolutions[j - 1] = swap;
        }
}

// The index of a resolution, -1 when it's not listed.
static int FindResolution(int w, int h)
{
    for (int i = 0; i < g_resolutionCount; i++)
        if (g_resolutions[i].w == w && g_resolutions[i].h == h)
            return i;
    return -1;
}

// The renderer's modes for the same render dll, device and bit depth; how many were found.
static int AddRendererModes(const RenderMode& current)
{
    RenderMode* modes = GetLtClientFunction<GetRenderModesFn>(LTCLIENT_GET_RENDER_MODES)();
    int found = 0;
    for (const RenderMode* mode = modes; mode != NULL; mode = mode->next) {
        bool sameDevice = strcmp(mode->renderDll, current.renderDll) == 0 && strcmp(mode->internalName, current.internalName) == 0;
        bool sameDepth = current.bitDepth == 0 || mode->bitDepth == current.bitDepth;
        if (!sameDevice || !sameDepth) continue;
        AddResolution(mode->width, mode->height);
        found++;
    }
    if (modes != NULL) GetLtClientFunction<FreeRenderModesFn>(LTCLIENT_FREE_RENDER_MODES)(modes);
    return found;
}

static void CollectResolutions(const RenderMode& current)
{
    g_resolutionCount = 0;
    int fromRenderer = AddRendererModes(current);
    bool usedWindowsList = g_resolutionCount < 2;   // the renderer told nothing useful
    if (usedWindowsList) {
        DEVMODEA display;
        memset(&display, 0, sizeof(display));
        display.dmSize = sizeof(display);
        for (DWORD i = 0; EnumDisplaySettingsA(NULL, i, &display); i++)
            if (current.bitDepth == 0 || (int)display.dmBitsPerPel == current.bitDepth)
                AddResolution(display.dmPelsWidth, display.dmPelsHeight);
    }
    AddResolution(current.width, current.height);
    SortResolutions();
    for (int i = 0; i < g_resolutionCount; i++)
        _snprintf(g_resolutions[i].text, sizeof(g_resolutions[i].text), "%dx%d", g_resolutions[i].w, g_resolutions[i].h);
    Log("video options: %d resolutions (%s) for \"%s\" %d bpp, current %dx%d", g_resolutionCount,
        !usedWindowsList && fromRenderer > 0 ? "renderer" : "Windows", current.description, current.bitDepth,
        current.width, current.height);
}

// --- controls ----------------------------------------------------------------------------------

struct ScrollPictures { void *upNormal, *upPressed, *upHighlight, *downNormal, *downPressed, *downHighlight, *handle; };

static void* LoadScrollPicture(const char* name)
{
    char path[MAX_PATH];
    _snprintf(path, sizeof(path), "InterfaceScreens\\OptionsScreen\\%s", name);
    path[sizeof(path) - 1] = '\0';
    return LoadPicture(path, 0);
}

static ScrollPictures LoadScrollPictures()
{
    ScrollPictures p;
    p.upHighlight   = LoadScrollPicture("OptionsScrollerUp-highlight.pcx");
    p.upPressed     = LoadScrollPicture("OptionsScrollerUp-Pressed.pcx");
    p.upNormal      = LoadScrollPicture("OptionsScrollerUp-Normal.pcx");
    p.downHighlight = LoadScrollPicture("OptionsScrollerDown-Highlight.pcx");
    p.downPressed   = LoadScrollPicture("OptionsScrollerDown-Pressed.pcx");
    p.downNormal    = LoadScrollPicture("OptionsScrollerDown-normal.pcx");
    p.handle        = LoadScrollPicture("OptionsScrollerHandle-normal.pcx");
    return p;
}

// A scroll button's height; 16 when the picture is missing or odd.
static int GetButtonHeight(void* picture)
{
    int w = 0, h = 0;
    void* renderer = Renderer::Get();
    if (picture != NULL && renderer != NULL) Renderer::GetSize(renderer, picture, w, h);
    if (h <= 0 || h > 40)
        return 16;
    else
        return h;
}

static void InitScrollButton(void* button, DWORD constructor, void* bar, int y, void* normal, void* pressed, void* highlight)
{
    ((ScrollPartConstructorFn)LoadedAddress(constructor))(button, 0, bar, Renderer::Get(), -1);
    *((BYTE*)button + 0x60) = 1;    // set after construction, as Keyboard Setup does
    POINT position = {SCROLL_X, y};
    GetMethod<ScrollButtonInitFn>(button, SCROLL_BUTTON_INIT)(button, 0, &position, normal, pressed, highlight, 0);
}

// A scroll bar for the list, like Keyboard Setup's: buttons at the top and bottom of the scroll
// column, the handle's track between them. NULL if it could not be made.
static void* CreateScrollBar(void* list)
{
    ScrollPictures pictures = LoadScrollPictures();
    int upY = LIST_Y;
    int downY = BOX_BOTTOM - 2 - GetButtonHeight(pictures.downNormal);
    int trackY = upY + GetButtonHeight(pictures.upNormal);
    void* bar = NewGameObject(0xA0);
    void* up = NewGameObject(0x64);
    void* down = NewGameObject(0x64);
    void* handle = NewGameObject(0x94);
    if (bar == NULL || up == NULL || down == NULL || handle == NULL)
        return NULL;

    ((ScrollPartConstructorFn)LoadedAddress(VA_SCROLL_BAR_CONSTRUCTOR))(bar, 0, list, Renderer::Get(), -1);
    InitScrollButton(up, VA_SCROLL_UP_CONSTRUCTOR, bar, upY, pictures.upNormal, pictures.upPressed, pictures.upHighlight);
    InitScrollButton(down, VA_SCROLL_DOWN_CONSTRUCTOR, bar, downY, pictures.downNormal, pictures.downPressed,
                     pictures.downHighlight);
    ((ScrollPartConstructorFn)LoadedAddress(VA_SCROLL_HANDLE_CONSTRUCTOR))(handle, 0, bar, Renderer::Get(), -1);
    *((BYTE*)handle + 0x90) = 1;    // set after construction, as Keyboard Setup does
    GetMethod<ScrollHandleInitFn>(handle, SCROLL_HANDLE_INIT)(handle, 0, pictures.handle, 0, 0, 0);
    *((BYTE*)bar + 0x9C) = 1;
    GetMethod<ScrollBarInitFn>(bar, SCROLL_BAR_INIT)(bar, 0, SCROLL_X, trackY, SCROLL_W, downY - trackY, 0, up, down, handle);
    return bar;
}

// Scrolls the list so the selected row is in the middle: first visible row at +0x74, number of
// visible rows at +0x7C.
static void ScrollToSelection(void* list, int selected)
{
    int visibleRows = *(int*)((char*)list + 0x7C);
    if (visibleRows <= 0 || g_resolutionCount <= visibleRows) return;
    int top = selected - visibleRows / 2;
    if (top > g_resolutionCount - visibleRows) top = g_resolutionCount - visibleRows;
    if (top < 0) top = 0;
    *(int*)((char*)list + 0x74) = top;
}

// --- the box -----------------------------------------------------------------------------------

static void PaintBox(void* screen)
{
    void* renderer = Renderer::Get();
    void* background = GetScreenBackground(screen);
    if (renderer == NULL || background == NULL) return;
    BackgroundPainter painter(renderer, background);

    // The Display Adapter box of this same picture, copied first.
    const Rect copied = MakeRect(SRC_LEFT_X, SRC_HEADER_Y, SRC_RIGHT_X + RIGHT_W - SRC_LEFT_X, SRC_EDGE_Y + EDGE_H - SRC_HEADER_Y);
    void* box = painter.CopyPart(background, copied);
    if (box == NULL) return;
    const int leftX = BOX_LEFT, rightX = BOX_RIGHT + 1 - RIGHT_W;
    const int leftSrcX = SRC_LEFT_X - copied.x, rightSrcX = SRC_RIGHT_X - copied.x;

    // header: frame top, header text area, the line under it
    painter.Blit(leftX, BOX_TOP, box, MakeRect(leftSrcX, SRC_HEADER_Y - copied.y, LEFT_W, HEADER_H));
    painter.Blit(rightX, BOX_TOP, box, MakeRect(rightSrcX, SRC_HEADER_Y - copied.y, RIGHT_W, HEADER_H));
    // list area: the plain black band repeated
    int edgeY = BOX_BOTTOM + 1 - EDGE_H;
    for (int y = LIST_Y; y < edgeY; y += BAND_H) {
        int h = edgeY - y < BAND_H ? edgeY - y : BAND_H;
        painter.Blit(leftX, y, box, MakeRect(leftSrcX, SRC_BAND_Y - copied.y, LEFT_W, h));
        painter.Blit(rightX, y, box, MakeRect(rightSrcX, SRC_BAND_Y - copied.y, RIGHT_W, h));
    }
    // bottom edge
    painter.Blit(leftX, edgeY, box, MakeRect(leftSrcX, SRC_EDGE_Y - copied.y, LEFT_W, EDGE_H));
    painter.Blit(rightX, edgeY, box, MakeRect(rightSrcX, SRC_EDGE_Y - copied.y, RIGHT_W, EDGE_H));

    // Grey bands in the Display Adapter box's rhythm, rows from the top of the list area:
    // 24..41, 62..81, 102..121, 142..161; the last row above the bottom edge stays black.
    static const int BANDS[4][2] = {{24, 18}, {62, 20}, {102, 20}, {142, 20}};
    for (int i = 0; i < 4; i++) {
        int y = LIST_Y + BANDS[i][0];
        int h = y + BANDS[i][1] > edgeY - 1 ? edgeY - 1 - y : BANDS[i][1];
        if (h > 0) painter.Fill(0x080808, MakeRect(LIST_X, y, LIST_W, h));
    }
    painter.DeleteCopy(box);
}

// --- the screen --------------------------------------------------------------------------------

static void* CreateResolutionList(void* screen, const RenderMode& current)
{
    void* font = GetOptionsFont();
    CreateLabel(screen, BOX_LEFT + 6, BOX_TOP + 4, 200, "Resolution", font);
    void* list = CreateListBox(LIST_X, LIST_Y, LIST_W, LIST_H, font);
    if (list == NULL)
        return NULL;
    void* scrollBar = CreateScrollBar(list);
    for (int i = 0; i < g_resolutionCount; i++) AddListItem(list, g_resolutions[i].text);
    int selected = FindResolution(current.width, current.height);
    if (selected < 0) selected = 0;
    SetListSelection(list, selected);
    AddChild(screen, list);
    if (scrollBar != NULL) {
        AddChild(screen, scrollBar);
        AddChild(list, scrollBar);       // the list scrolls with the bar
    }
    ScrollToSelection(list, selected);
    return list;
}

static void __fastcall HookInit(void* screen, void*)
{
    g_unhookedInit(screen, 0);
    g_openScreen = NULL;
    g_resolutionList = NULL;
    if (!g_enabled) return;

    const RenderMode& current = *GetScreenMode(screen);
    CollectResolutions(current);
    g_resolutionList = CreateResolutionList(screen, current);
    if (g_resolutionList == NULL) return;
    PaintBox(screen);
    g_openScreen = screen;
}

// The selected resolution goes into the screen's mode; the game's Apply saves it and switches.
static void ApplyResolution(void* screen)
{
    int selected = GetListSelection(g_resolutionList);
    if (selected < 0 || selected >= g_resolutionCount) return;
    int w = g_resolutions[selected].w, h = g_resolutions[selected].h;

    RunConsoleFn runConsole = GetLtClientFunction<RunConsoleFn>(LTCLIENT_RUN_CONSOLE);
    char command[64];
    _snprintf(command, sizeof(command), "ScreenWidth %d", w);
    runConsole(command);
    _snprintf(command, sizeof(command), "ScreenHeight %d", h);
    runConsole(command);

    RenderMode* mode = GetScreenMode(screen);
    if (mode->width == w && mode->height == h) return;
    mode->width = w;
    mode->height = h;
    *((BYTE*)screen + VIDEO_OPTIONS_CHANGED) = 1;
    Log("video options: resolution %dx%d", w, h);
}

// Restore Default keeps the resolution: the current one is selected again.
static void SelectCurrentResolution(void* screen)
{
    int current = FindResolution(GetScreenMode(screen)->width, GetScreenMode(screen)->height);
    if (current >= 0) SetListSelection(g_resolutionList, current);
}

static char __fastcall HookClicked(void* screen, void*, void* control)
{
    if (screen == g_openScreen && control != NULL) {
        int id = GetControlId(control);
        if (id == VIDEO_OPTIONS_APPLY) ApplyResolution(screen);
        if (id == VIDEO_OPTIONS_DEFAULTS) SelectCurrentResolution(screen);
        if (id == VIDEO_OPTIONS_APPLY || id == VIDEO_OPTIONS_CANCEL) {   // the screen closes
            g_openScreen = NULL;
            g_resolutionList = NULL;
        }
    }
    return g_unhookedClicked(screen, 0, control);
}

// The scroll handle saves the screen under itself every frame (a read-back from the graphics
// card, ~4 ms each) to restore it on the next. Pointless: the option screens redraw their whole
// background every frame. Its "save background" reports nothing saved, so nothing is restored.
static char __fastcall SaveNoBackground(void*, void*) { return 0; }

bool InstallVideoOptions()
{
    bool isExpected = CallsFunction(VA_VIDEO_OPTIONS_INIT_CALL, VA_VIDEO_OPTIONS_INIT)
        && IsVtableSlot(VA_VIDEO_OPTIONS_VTABLE, SCREEN_CONTROL_CLICKED, VA_VIDEO_OPTIONS_CLICKED)
        && BytesMatch(VA_SCROLL_BAR_CONSTRUCTOR, "\x8B\x44\x24\x0C\x8B\x54\x24\x04\x56\x8B\xF1", 11);
    if (!isExpected) {
        Log("video options: unexpected code - not changed");
        return false;
    }
    g_unhookedInit = (MethodFn)LoadedAddress(VA_VIDEO_OPTIONS_INIT);
    WriteCall(LoadedAddress(VA_VIDEO_OPTIONS_INIT_CALL), (void*)HookInit);
    g_unhookedClicked = (ClickedFn)PatchVtableSlot(VA_VIDEO_OPTIONS_VTABLE, SCREEN_CONTROL_CLICKED, (void*)HookClicked);
    Log("video options: Resolution added");
    if (IsVtableSlot(VA_SCROLL_HANDLE_VTABLE, SPRITE_SAVE_BACKGROUND, VA_SPRITE_SAVE_BACKGROUND))
        PatchVtableSlot(VA_SCROLL_HANDLE_VTABLE, SPRITE_SAVE_BACKGROUND, (void*)SaveNoBackground);
    return true;
}
