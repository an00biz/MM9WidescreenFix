// Adds HUD Scale, FOV and FOV Type to Options -> Game Options. The slider ramps and the list's
// frame are painted into the screens' background pictures, so they are copied from there:
//   ramp:  OptionsGameBG.pcx, under the Turn Speed slider
//   frame: OptionsVideoBG.pcx, the Display Adapter box (pieces cut inside its plain black inside)
#include <stdlib.h>
#include "options/options_game.h"
#include "core/addresses.h"
#include "camera/camera.h"
#include "settings/config.h"
#include "core/cshell_hud.h"
#include "layout/layout.h"
#include "core/log.h"
#include "options/options_controls.h"
#include "core/patching.h"
#include "rendering/renderer.h"

// Layout, 800x600 screen space. The sliders sit under Turn Speed, with its x and size (405x30).
const int SLIDER_X = 281, SLIDER_W = 405, SLIDER_H = 30;
const int HUD_LABEL_Y = 402, HUD_SLIDER_Y = 424, FOV_LABEL_Y = 462, FOV_SLIDER_Y = 484;
const int LABEL_X = 282, LABEL_W = 260;
// The Turn Speed ramp in OptionsGameBG.pcx (the slider's rect 31,102 405x30 in the picture,
// trimmed 1 left, 2 top, 2 bottom), and where the copy goes relative to a slider.
const int RAMP_SRC_X = 32, RAMP_SRC_Y = 104, RAMP_W = 404, RAMP_H = 26, RAMP_DX = 1, RAMP_DY = 2;
// FOV Type box: change these to move / resize it. Left edge and top / bottom edge of the gold
// frame (173 px wide: 513..685; top and bottom at least ~90 px apart).
const int BOX_LEFT = 513, BOX_TOP = 201, BOX_BOTTOM = 309;
// Its source: the Display Adapter box in OptionsVideoBG.pcx (frame about x 33..495, y 105..257).
// Copied in 4 pieces: left and right (the scroll arrows), each split into a top part (header, up
// arrow) and a bottom part (down arrow, bottom edge); the cuts run through the plain black inside.
// Margin around the frame: 1 px left, 0 right, 1 top, 1 bottom.
const int BOX_SRC_Y = 104, BOX_SRC_H = 154;           // source rows 104..257
const int BOX_BOTTOM_ROWS = 37;                        // taken from the bottom: down arrow + edge
const int BOX_LEFT_SRC_X = 32, BOX_LEFT_W = 117, BOX_RIGHT_SRC_X = 439, BOX_RIGHT_W = 57;
const int BOX_X = BOX_LEFT - 1, BOX_Y = BOX_TOP - 1;
const int BOX_H = BOX_BOTTOM - BOX_TOP + 2;            // on screen, margins included

enum SliderKind { NO_SLIDER, HUD_SLIDER, FOV_SLIDER };
static const char* FOV_TYPE_NAMES[3] = {"Original", "Original Fixed", "Correct"};   // FovMode order

typedef char (__fastcall *ClickedFn)(void* screen, void* edx, void* control);
static MethodFn  g_unhookedInit = NULL;
static ClickedFn g_unhookedClicked = NULL;

static void* g_openScreen = NULL;      // the Game Options screen while it is open
static void* g_hudSlider = NULL;
static void* g_fovSlider = NULL;
static void* g_fovTypeList = NULL;
static void* g_videoBackground = NULL; // OptionsVideoBG.pcx, loaded once
static int   g_doubleClickedSlider = NO_SLIDER;   // double-clicked, button still down
static int   g_sliderToReset = NO_SLIDER;         // ... released: reset on the next frame

static void ForgetScreen()
{
    g_openScreen = NULL;
    g_hudSlider = NULL;
    g_fovSlider = NULL;
    g_fovTypeList = NULL;
}

// At or below 800x600 the HUD size can't change; the slider still needs a range.
static float GetHudSliderMin()
{
    if (g_screen.hudScaleMax - g_screen.hudScaleMin < 0.01f)
        return g_screen.hudScaleMax - 0.01f;
    else
        return g_screen.hudScaleMin;
}

static void PaintBackgroundArt(void* screen)
{
    void* renderer = Renderer::Get();
    void* background = GetScreenBackground(screen);
    if (renderer == NULL || background == NULL) return;
    BackgroundPainter painter(renderer, background);

    void* ramp = painter.CopyPart(background, MakeRect(RAMP_SRC_X, RAMP_SRC_Y, RAMP_W, RAMP_H));
    if (ramp != NULL) {
        painter.Blit(SLIDER_X + RAMP_DX, HUD_SLIDER_Y + RAMP_DY, ramp, MakeRect(0, 0, RAMP_W, RAMP_H));
        painter.Blit(SLIDER_X + RAMP_DX, FOV_SLIDER_Y + RAMP_DY, ramp, MakeRect(0, 0, RAMP_W, RAMP_H));
        painter.DeleteCopy(ramp);
    }

    if (g_videoBackground == NULL || g_videoBackground == background) return;
    int maxTopRows = BOX_SRC_H - BOX_BOTTOM_ROWS;     // never taller than the original box
    int topRows = BOX_H - BOX_BOTTOM_ROWS < maxTopRows ? BOX_H - BOX_BOTTOM_ROWS : maxTopRows;
    int bottomSrcY = BOX_SRC_Y + maxTopRows, bottomY = BOX_Y + topRows;
    painter.Blit(BOX_X, BOX_Y, g_videoBackground, MakeRect(BOX_LEFT_SRC_X, BOX_SRC_Y, BOX_LEFT_W, topRows));
    painter.Blit(BOX_X, bottomY, g_videoBackground, MakeRect(BOX_LEFT_SRC_X, bottomSrcY, BOX_LEFT_W, BOX_BOTTOM_ROWS));
    painter.Blit(BOX_X + BOX_LEFT_W, BOX_Y, g_videoBackground, MakeRect(BOX_RIGHT_SRC_X, BOX_SRC_Y, BOX_RIGHT_W, topRows));
    painter.Blit(BOX_X + BOX_LEFT_W, bottomY, g_videoBackground,
                 MakeRect(BOX_RIGHT_SRC_X, bottomSrcY, BOX_RIGHT_W, BOX_BOTTOM_ROWS));
}

// FOV Type: header label and a list box inside the copied frame, laid out like the Display
// Adapter list (x + 2, header 27 px, scroll column 21 px on the right).
static void* CreateFovTypeList(void* screen, void* font)
{
    CreateLabel(screen, BOX_LEFT + 5, BOX_TOP + 3, 150, "FOV Type", font);
    int listTop = BOX_TOP + 27;
    void* list = CreateListBox(BOX_LEFT + 2, listTop, 149, BOX_BOTTOM - 2 - listTop, font);
    if (list == NULL)
        return NULL;
    for (int i = 0; i < 3; i++) AddListItem(list, FOV_TYPE_NAMES[i]);
    SetListSelection(list, g_config.fovMode);
    AddChild(screen, list);
    return list;
}

// Replaces the screen's "call Init": the game's controls first, then the fix's.
static void __fastcall HookInit(void* screen, void*)
{
    g_unhookedInit(screen, 0);
    ForgetScreen();
    g_doubleClickedSlider = NO_SLIDER;
    g_sliderToReset = NO_SLIDER;
    if (!g_enabled) return;

    void* font = GetOptionsFont();
    float hudMin = GetHudSliderMin();
    CreateLabel(screen, LABEL_X, HUD_LABEL_Y, LABEL_W, "HUD Scale", font);
    g_hudSlider = CreateSlider(screen, 0x30, SLIDER_X, HUD_SLIDER_Y, hudMin, g_screen.hudScaleMax, g_screen.hudScale);
    CreateLabel(screen, LABEL_X, FOV_LABEL_Y, LABEL_W, "FOV", font);
    g_fovSlider = CreateSlider(screen, 0x31, SLIDER_X, FOV_SLIDER_Y, g_config.fovScaleMin, g_config.fovScaleMax,
                               g_config.fovScale);
    g_fovTypeList = CreateFovTypeList(screen, font);

    if (g_videoBackground == NULL)
        g_videoBackground = LoadPicture("interfacescreens\\optionsscreen\\OptionsVideoBG.pcx", GetOptionsColor(0x14));
    PaintBackgroundArt(screen);
    g_openScreen = screen;
    Log("game options: HUD Scale %.2f (%.2f..%.2f), FOV %.2f, FOV Type %s%s", g_screen.hudScale, hudMin,
        g_screen.hudScaleMax, g_config.fovScale, FOV_TYPE_NAMES[g_config.fovMode],
        g_videoBackground != NULL ? "" : " (no OptionsVideoBG.pcx)");
}

// The game's slider stops a little short of its ends (about 2.5%, the handle's width): values
// within 3% of an end are taken as that end.
static float SnapToEnds(float value, float min, float max)
{
    float position = (value - min) / (max - min);
    if (position >= 0.97f)
        return max;
    else if (position <= 0.03f)
        return min;
    else
        return value;
}

static void ApplySettings()
{
    if (g_hudSlider != NULL)
        SetHudSize(SnapToEnds(GetSliderValue(g_hudSlider), GetHudSliderMin(), g_screen.hudScaleMax));
    if (g_fovTypeList != NULL) {
        int mode = GetListSelection(g_fovTypeList);
        g_config.fovMode = mode >= FOV_ORIGINAL && mode <= FOV_CORRECT ? mode : FOV_HOR_PLUS;
        SaveFovMode(g_config.fovMode);
    }
    if (g_fovSlider != NULL) {
        float scale = SnapToEnds(GetSliderValue(g_fovSlider), g_config.fovScaleMin, g_config.fovScaleMax);
        if (scale > 0.97f && scale < 1.03f) scale = 1.0f;      // easy to get back to the default
        SetFovScale(scale);                                    // also sets the camera's FOV again
    } else {
        ReapplyCameraFov();
    }
    Log("game options applied: HUD size %.2f%s, FOV scale %.2f, FOV type %s", g_screen.hudScale,
        g_config.hudSize <= 0 ? " (max)" : "", g_config.fovScale, FOV_TYPE_NAMES[g_config.fovMode]);
}

static void RestoreDefaults()
{
    if (g_hudSlider != NULL) SetSliderValue(g_hudSlider, g_screen.hudScaleMax);
    if (g_fovSlider != NULL) SetSliderValue(g_fovSlider, 1.0f);
    if (g_fovTypeList != NULL) SetListSelection(g_fovTypeList, FOV_HOR_PLUS);
}

static char __fastcall HookClicked(void* screen, void*, void* control)
{
    if (screen == g_openScreen && control != NULL) {
        int id = GetControlId(control);
        if (id == GAME_OPTIONS_DEFAULTS) RestoreDefaults();
        if (id == GAME_OPTIONS_APPLY) ApplySettings();
        if (id == GAME_OPTIONS_APPLY || id == GAME_OPTIONS_CANCEL) ForgetScreen();   // the screen closes
    }
    return g_unhookedClicked(screen, 0, control);
}

// Applied on the frame after the button is released, so the slider's own click handling can't
// undo it.
void BeforeGameOptionsDraw(void* screen)
{
    bool isOpenScreen = screen == g_openScreen && GetVtable(screen) == (void**)LoadedAddress(VA_GAME_OPTIONS_VTABLE);
    if (!isOpenScreen || g_sliderToReset == NO_SLIDER) return;
    if (g_sliderToReset == HUD_SLIDER && g_hudSlider != NULL) SetSliderValue(g_hudSlider, g_screen.hudScaleMax);
    if (g_sliderToReset == FOV_SLIDER && g_fovSlider != NULL) SetSliderValue(g_fovSlider, 1.0f);
    g_sliderToReset = NO_SLIDER;
}

static int FindSliderAt(int x, int y)
{
    if (x < SLIDER_X || x >= SLIDER_X + SLIDER_W)
        return NO_SLIDER;
    else if (g_fovSlider != NULL && y >= FOV_SLIDER_Y && y < FOV_SLIDER_Y + SLIDER_H)
        return FOV_SLIDER;
    else if (g_hudSlider != NULL && y >= HUD_SLIDER_Y && y < HUD_SLIDER_Y + SLIDER_H)
        return HUD_SLIDER;
    else
        return NO_SLIDER;
}

// A double-click on a slider resets it to its default (HUD Scale max, FOV 1.0). The game's window
// may not get WM_LBUTTONDBLCLK, so two quick presses on the same slider count too.
void TrackSliderDoubleClick(DWORD msg, int x, int y)
{
    static DWORD lastPress = 0;
    static int lastPressX = -9999, lastPressY = -9999, lastPressSlider = NO_SLIDER;
    bool isPress = msg == WM_LBUTTONDOWN || msg == WM_LBUTTONDBLCLK;
    if (g_openScreen == NULL || (!isPress && msg != WM_LBUTTONUP)) return;

    if (msg == WM_LBUTTONUP) {
        if (g_doubleClickedSlider != NO_SLIDER) g_sliderToReset = g_doubleClickedSlider;
        g_doubleClickedSlider = NO_SLIDER;
        return;
    }

    DWORD now = GetTickCount();
    int slider = FindSliderAt(x, y);
    bool isSecondPress = lastPress != 0 && slider == lastPressSlider && now - lastPress <= GetDoubleClickTime()
                         && abs(x - lastPressX) <= 4 && abs(y - lastPressY) <= 4;
    if (slider != NO_SLIDER && (msg == WM_LBUTTONDBLCLK || isSecondPress)) {
        g_doubleClickedSlider = slider;
        lastPress = 0;
    } else {
        lastPress = slider != NO_SLIDER ? now : 0;
        lastPressSlider = slider;
        lastPressX = x;
        lastPressY = y;
    }
}

bool InstallGameOptions()
{
    bool isExpected = CallsFunction(VA_GAME_OPTIONS_INIT_CALL, VA_GAME_OPTIONS_INIT)
        && IsVtableSlot(VA_GAME_OPTIONS_VTABLE, SCREEN_CONTROL_CLICKED, VA_GAME_OPTIONS_CLICKED)
        && IsVtableSlot(VA_GAME_OPTIONS_VTABLE, CONTROL_ADD_CHILD, VA_CONTAINER_ADD_CHILD)
        && BytesMatch(VA_SLIDER_CONSTRUCTOR, "\x8B\x44\x24\x04\x56\x8B\xF1\x50", 8)
        && BytesMatch(VA_LIST_CONSTRUCTOR, "\x8B\x44\x24\x0C\x8B\x54\x24\x04", 8)
        && BytesMatch(VA_MAKE_LABEL, "\x53\x56\x57\x68\x04\x03\x00\x00", 8)
        && BytesMatch(VA_LOAD_PICTURE, "\x81\xEC\x04\x01\x00\x00\x53\x55", 8)
        && BytesMatch(VA_GET_COLOR, "\x8B\x44\x24\x04", 4)
        && BytesMatch(VA_CAMERA_APPLY_FOV, "\x83\xEC\x24\x8D\x44\x24\x14\x56", 8);
    if (!isExpected) {
        Log("game options: unexpected code - not changed");
        return false;
    }
    g_unhookedInit = (MethodFn)LoadedAddress(VA_GAME_OPTIONS_INIT);
    WriteCall(LoadedAddress(VA_GAME_OPTIONS_INIT_CALL), (void*)HookInit);
    g_unhookedClicked = (ClickedFn)PatchVtableSlot(VA_GAME_OPTIONS_VTABLE, SCREEN_CONTROL_CLICKED, (void*)HookClicked);
    Log("game options: HUD Scale / FOV / FOV Type added");
    return true;
}
