// [KEYS] bindings through a message hook, and mouse points converted to 800x600 in the game's
// window proc (patched) and in cshell's ScreenToClient import.
#include <string.h>
#include "input/input.h"
#include "core/addresses.h"
#include "camera/camera.h"
#include "settings/config.h"
#include "core/cshell_hud.h"
#include "diagnostics/diagnostics.h"
#include "core/log.h"
#include "options/options_game.h"
#include "core/patching.h"

MouseState g_mouse = {0, 0, false, {1.0f, 1.0f, 0.0f, 0.0f}, false};

// --- [KEYS] bindings ---------------------------------------------------------------------------

static HHOOK g_messageHook = NULL;
static bool  g_ignoreNextAltRelease = false;   // an Alt binding was used

// Whole notches from a wheel message; the remainder is kept for the next message.
static int TakeWheelNotches(int& accumulated, const MSG* msg)
{
    accumulated += (short)HIWORD(msg->wParam);
    int notches = accumulated / WHEEL_DELTA;
    accumulated -= notches * WHEEL_DELTA;
    return notches;
}

static bool IsWheelBinding(const KeyBinding& binding) { return binding.trigger == TRIGGER_WHEEL && AreModifiersHeld(binding); }
static bool IsButtonBinding(const KeyBinding& binding) { return binding.trigger == TRIGGER_MOUSE_BUTTON && AreModifiersHeld(binding); }

// The binding the message triggered (and its action done), NULL if none.
static const KeyBinding* RunBinding(const MSG* msg)
{
    static int hudWheel = 0, fovWheel = 0;
    const Config& c = g_config;
    if (msg->message == WM_MOUSEWHEEL) {
        if (IsWheelBinding(c.hudSizeKey)) {
            int notches = TakeWheelNotches(hudWheel, msg);
            if (notches != 0) StepHudSize(notches);
            return &c.hudSizeKey;
        } else if (IsWheelBinding(c.fovKey)) {
            int notches = TakeWheelNotches(fovWheel, msg);
            if (notches != 0) StepFovScale(c.fovWheelInverted ? -notches : notches);
            return &c.fovKey;
        }
    } else if (IsPressedBy(c.hudSizeResetKey, msg) && AreModifiersHeld(c.hudSizeResetKey)) {
        SetHudSize(g_screen.hudScaleMax);
        return &c.hudSizeResetKey;
    } else if (IsPressedBy(c.fovResetKey, msg) && AreModifiersHeld(c.fovResetKey)) {
        SetFovScale(1.0f);
        return &c.fovResetKey;
    }
    return NULL;
}

// The release of a mouse button used by a binding.
static bool IsBindingButtonRelease(const MSG* msg)
{
    bool isRelease = msg->message == WM_MBUTTONUP || msg->message == WM_XBUTTONUP;
    return isRelease && (IsButtonBinding(g_config.hudSizeResetKey) || IsButtonBinding(g_config.fovResetKey));
}

static LRESULT CALLBACK HookMessages(int code, WPARAM removal, LPARAM message)
{
    bool isRemoved = code == HC_ACTION && removal == PM_REMOVE && message != 0;
    if (!isRemoved || !g_enabled)
        return CallNextHookEx(g_messageHook, code, removal, message);

    MSG* msg = (MSG*)message;
    bool isAltRelease = (msg->message == WM_SYSKEYUP || msg->message == WM_KEYUP) && msg->wParam == VK_MENU;
    const KeyBinding* used = RunBinding(msg);
    if (used != NULL) {
        if (used->modifier[ALT_KEYS] != KEYS_UP) g_ignoreNextAltRelease = true;
        msg->message = WM_NULL;                  // the game never sees it
    } else if (IsBindingButtonRelease(msg)) {
        msg->message = WM_NULL;
    } else if (isAltRelease && g_ignoreNextAltRelease) {
        // Releasing Alt alone opens the system menu (the window freezes until the next click).
        g_ignoreNextAltRelease = false;
        msg->message = WM_NULL;
    }
    return CallNextHookEx(g_messageHook, code, removal, message);
}

void InstallInputHookOnce()
{
    static bool tried = false;
    if (tried || !g_enabled) return;
    tried = true;
    g_messageHook = SetWindowsHookExA(WH_GETMESSAGE, HookMessages, NULL, GetCurrentThreadId());
    if (g_messageHook != NULL)
        Log("mouse wheel / key bindings active (thread %lu)", GetCurrentThreadId());
    else
        Log("could not hook the mouse wheel (thread %lu)", GetCurrentThreadId());
}

// --- mouse points ------------------------------------------------------------------------------

static void LogMouseMessage(DWORD msg, int realX, int realY, int x, int y)
{
    DWORD now = GetTickCount();
    static DWORD lastMoveLog = 0, lastHoverSample = 0;
    if (msg == WM_MOUSEMOVE && now - lastMoveLog > 100) {
        lastMoveLog = now;
        POINT cursor;
        GetCursorPos(&cursor);
        RECT clip;
        GetClipCursor(&clip);
        Log("move: real (%d,%d) -> ui (%d,%d)  os cursor (%ld,%ld)  clip (%ld,%ld,%ld,%ld)", realX, realY, x, y,
            cursor.x, cursor.y, clip.left, clip.top, clip.right, clip.bottom);
    }

    // Hit tests are traced for a moment after each click, and briefly every 0.7 s while hovering.
    bool isClick = msg == WM_LBUTTONDOWN || msg == WM_RBUTTONDOWN;
    if (isClick) {
        g_hitTraceUntil = now + 200;
        Log("mouse %s: real (%d,%d) -> ui (%d,%d)  block x %.0f", msg == WM_LBUTTONDOWN ? "L-down" : "R-down",
            realX, realY, x, y, g_drawMapping.offsetX);
    } else if (msg == WM_MOUSEMOVE && now - lastHoverSample > 700) {
        lastHoverSample = now;
        g_hitTraceUntil = now + 40;
        Log("hover sample: real (%d,%d) -> ui (%d,%d)  block x %.0f", realX, realY, x, y, g_drawMapping.offsetX);
    }
}

// Called from the window proc (patched) for every message, in place of its "lParam -> global
// mouse point" code. Must never call into the engine: the window proc also runs during video
// mode switches.
static void __fastcall ConvertWindowMouse(DWORD lParam, DWORD msg)
{
    int realX = (short)LOWORD(lParam), realY = (short)HIWORD(lParam);
    int x = realX, y = realY;
    // Mouse messages carry client pixels, except the wheel's, which are screen pixels.
    bool hasClientPoint = msg >= WM_MOUSEMOVE && msg <= WM_XBUTTONDBLCLK && msg != WM_MOUSEWHEEL;
    if (hasClientPoint && g_enabled && g_screen.known) {
        g_mouse.realX = realX;
        g_mouse.realY = realY;
        g_mouse.haveReal = true;
        g_mouse.mapping = GetMouseMappingAt(realX, realY);
        g_mouse.haveMapping = true;
        x = Round((realX - g_mouse.mapping.offsetX) / g_mouse.mapping.scaleX);
        y = Round((realY - g_mouse.mapping.offsetY) / g_mouse.mapping.scaleY);
    }

    if (g_enabled) TrackSliderDoubleClick(msg, x, y);
    if (g_config.mouseLog) LogMouseMessage(msg, realX, realY, x, y);
    *(int*)LoadedAddress(VA_MOUSE_X) = x;
    *(int*)LoadedAddress(VA_MOUSE_Y) = y;
}

typedef BOOL (WINAPI *ScreenToClientFn)(HWND, LPPOINT);
static ScreenToClientFn g_unhookedScreenToClient;

static BOOL WINAPI HookScreenToClient(HWND window, LPPOINT point)
{
    BOOL result = g_unhookedScreenToClient(window, point);
    if (result && point != NULL && g_enabled && g_screen.known) {
        Mapping m = GetMouseMappingAt(point->x, point->y);
        point->x = Round((point->x - m.offsetX) / m.scaleX);
        point->y = Round((point->y - m.offsetY) / m.scaleY);
    }
    return result;
}

// The window proc's original 31 bytes at VA_WNDPROC_MOUSE (ebp = message):
static void GetOriginalMouseCode(unsigned char* code)
{
    static const unsigned char ORIGINAL[31] = {
        0x8B, 0x44, 0x24, 0x24,            // mov eax,[esp+24h]   lParam
        0x0F, 0xBF, 0xC8,                  // movsx ecx,ax
        0xC1, 0xE8, 0x10,                  // shr eax,10h
        0x0F, 0xBF, 0xD0,                  // movsx edx,ax
        0x81, 0xFD, 0x03, 0x02, 0x00, 0x00,   // cmp ebp,203h
        0x89, 0x0D, 0, 0, 0, 0,            // mov [mouseX],ecx
        0x89, 0x15, 0, 0, 0, 0};           // mov [mouseY],edx
    memcpy(code, ORIGINAL, 31);
    *(DWORD*)(code + 21) = LoadedAddress(VA_MOUSE_X);
    *(DWORD*)(code + 27) = LoadedAddress(VA_MOUSE_Y);
}

// ... replaced by: call ConvertWindowMouse(lParam, message), load the converted point into
// ecx / edx as the original did, and redo its cmp (the flags are used right after).
static void GetNewMouseCode(unsigned char* code)
{
    static const unsigned char REPLACEMENT[31] = {
        0x8B, 0x4C, 0x24, 0x24,            // mov ecx,[esp+24h]
        0x8B, 0xD5,                        // mov edx,ebp
        0xE8, 0, 0, 0, 0,                  // call ConvertWindowMouse
        0x8B, 0x0D, 0, 0, 0, 0,            // mov ecx,[mouseX]
        0x8B, 0x15, 0, 0, 0, 0,            // mov edx,[mouseY]
        0x81, 0xFD, 0x03, 0x02, 0x00, 0x00,   // cmp ebp,203h
        0x90, 0x90};
    DWORD at = LoadedAddress(VA_WNDPROC_MOUSE);
    memcpy(code, REPLACEMENT, 31);
    *(DWORD*)(code + 7)  = (DWORD)ConvertWindowMouse - (at + 11);
    *(DWORD*)(code + 13) = LoadedAddress(VA_MOUSE_X);
    *(DWORD*)(code + 19) = LoadedAddress(VA_MOUSE_Y);
}

bool HasExpectedMouseCode()
{
    unsigned char original[31];
    GetOriginalMouseCode(original);
    return BytesMatch(VA_WNDPROC_MOUSE, original, 31);
}

void InstallMouseHooks()
{
    unsigned char replacement[31];
    GetNewMouseCode(replacement);
    WriteCode(LoadedAddress(VA_WNDPROC_MOUSE), replacement, 31);
    g_unhookedScreenToClient = (ScreenToClientFn)PatchPointer(LoadedAddress(VA_IMPORT_SCREEN_TO_CLIENT),
                                                              (void*)HookScreenToClient);
}
