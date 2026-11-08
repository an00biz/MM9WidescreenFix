// The 3D view rect during in-game cutscenes, and the camera FOV for wide screens ([FOV] mode).
#include <math.h>
#include "camera/camera.h"
#include "core/addresses.h"
#include "settings/config.h"
#include "core/cshell_hud.h"
#include "layout/layout.h"
#include "core/log.h"
#include "core/patching.h"
#include "rendering/renderer.h"

// --- 3D view rect ------------------------------------------------------------------------------
// The in-game cutscene code uses hardcoded 800x600 rects (the full view and the letterbox): they
// are stretched to the real screen, so the letterbox bars keep their share.

typedef int (*SetCameraRectFn)(void* camera, int fullscreen, int left, int top, int right, int bottom);
typedef int (*GetCameraRectFn)(void* camera, int* fullscreen, int* left, int* top, int* right, int* bottom);

static bool IsCutsceneCode(DWORD caller)
{
    return caller >= LoadedAddress(VA_CUTSCENE_CODE_START) && caller < LoadedAddress(VA_CUTSCENE_CODE_END);
}

// The game's camera object holds the engine camera at +4.
static void* GetEngineCamera(void* gameCamera) { return *(void**)((char*)gameCamera + 4); }

// Replaces the game's "set 3D view rect" (rect in screen pixels, or 800x600 from cutscenes).
static char __fastcall HookSetCameraRect(void* gameCamera, void*, Rect* rect)
{
    DWORD caller = RETURN_ADDRESS();
    Rect view = *rect;
    bool active = g_enabled && g_screen.known;
    if (active && IsCutsceneCode(caller)) {
        float scaleX = g_screen.width / 800.0f, scaleY = g_screen.height / 600.0f;
        int left = Round(view.x * scaleX), top = Round(view.y * scaleY);
        view = MakeRect(left, top, Round((view.x + view.w) * scaleX) - left, Round((view.y + view.h) * scaleY) - top);
    }

    // The stock renderer draws the world 1 px down-right (a line of clear color at the top and
    // left edge): a full-screen view starts [VIDEO] camera_nudge pixels above / left of the screen.
    int nudge = g_config.cameraNudge;
    bool isFullScreen = view.x == 0 && view.y == 0 && view.w == g_screen.width && view.h == g_screen.height;
    if (active && nudge > 0 && isFullScreen) {
        static int loggedW = 0, loggedH = 0, loggedNudge = 0;
        if (loggedW != view.w || loggedH != view.h || loggedNudge != nudge) {
            Log("camera rect nudged by -%d px: (%d,%d)-(%d,%d)", nudge, -nudge, -nudge, view.w, view.h);
            loggedW = view.w;
            loggedH = view.h;
            loggedNudge = nudge;
        }
        view = MakeRect(-nudge, -nudge, view.w + nudge, view.h + nudge);
    }
    GetLtClientFunction<SetCameraRectFn>(LTCLIENT_SET_CAMERA_RECT)(GetEngineCamera(gameCamera), 0,
                                                                   view.x, view.y, view.x + view.w, view.y + view.h);
    return 1;
}

// Replaces the game's "get 3D view rect"; the cutscene code gets back the 800x600 rect it set.
static Rect* __fastcall HookGetCameraRect(void* gameCamera, void*, Rect* out)
{
    DWORD caller = RETURN_ADDRESS();
    int fullscreen = 0, left = 0, top = 0, right = 0, bottom = 0;
    GetLtClientFunction<GetCameraRectFn>(LTCLIENT_GET_CAMERA_RECT)(GetEngineCamera(gameCamera), &fullscreen,
                                                                   &left, &top, &right, &bottom);
    if (g_enabled && g_screen.known && IsCutsceneCode(caller)) {
        float scaleX = 800.0f / g_screen.width, scaleY = 600.0f / g_screen.height;
        left = Round(left * scaleX);
        right = Round(right * scaleX);
        top = Round(top * scaleY);
        bottom = Round(bottom * scaleY);
    }
    *out = MakeRect(left, top, right - left, bottom - top);
    return out;
}

bool HasExpectedCameraRectCode()
{
    return BytesMatch(VA_GET_CAMERA_RECT, "\x83\xEC\x14\x8B\x49\x04\x8D\x44\x24\x0C", 10)
        && BytesMatch(VA_SET_CAMERA_RECT, "\x8B\x44\x24\x04\x56\x57\x8B\x49\x04\x8B\x50\x04", 12);
}

void InstallCameraRectHooks()
{
    WriteJump(LoadedAddress(VA_SET_CAMERA_RECT), (void*)HookSetCameraRect);
    WriteJump(LoadedAddress(VA_GET_CAMERA_RECT), (void*)HookGetCameraRect);
}

// --- field of view -----------------------------------------------------------------------------
// The game keeps the horizontal FOV (90 deg) and sets fovY = fovX * h / w: a linear ratio instead
// of the tangent relation, so wider screens see less vertically ("Vert-") and the picture is
// stretched upwards (12% at 4:3, 19% at 16:9). Every FOV the game sets or reads passes here:
//   game x, y   what the game would set at 800x600 for the same view (the 3D view rects are
//               800x600 rects scaled to the screen, so they depend only on W/H)
//   hor+        vertical as at 800x600, horizontal widened by the screen aspect / (4/3)
//   correct     the same, but the vertical derived from the horizontal with the tangent
// Reads (the FOV wobble effects) get the exact inverse, so they keep working.

typedef int (*SetFovFn)(void* camera, float x, float y);
typedef int (*GetFovFn)(void* camera, float* x, float* y);
static const float MAX_HALF_ANGLE_TANGENT = 11.43f;   // 85 degrees
static const float DEGREES_PER_RADIAN = 57.29578f;

// (W/H) / (4/3)
static float GetAspectOver4By3()
{
    if (Renderer::Get() != NULL) UpdateScreenSize();
    if (g_screen.known)
        return (float)g_screen.width / g_screen.height * 0.75f;
    else
        return 1.0f;
}

static void GameFovToEngine(float x, float y, float* engineX, float* engineY)
{
    *engineX = x;
    *engineY = y;
    if (!g_enabled || g_config.fovMode == FOV_ORIGINAL) return;

    float aspect = GetAspectOver4By3(), scale = g_config.fovScale;
    float yAt800x600 = y * aspect;
    float tanX = tanf(x * 0.5f) * aspect * scale;
    float tanY;
    if (g_config.fovMode == FOV_CORRECT)
        tanY = tanf(yAt800x600 * 0.5f / 0.75f) * 0.75f * scale;
    else
        tanY = tanf(yAt800x600 * 0.5f) * scale;
    if (tanX > MAX_HALF_ANGLE_TANGENT) tanX = MAX_HALF_ANGLE_TANGENT;
    if (tanY > MAX_HALF_ANGLE_TANGENT) tanY = MAX_HALF_ANGLE_TANGENT;
    *engineX = 2.0f * atanf(tanX);
    *engineY = 2.0f * atanf(tanY);
}

static void EngineFovToGame(float x, float y, float* gameX, float* gameY)
{
    *gameX = x;
    *gameY = y;
    if (!g_enabled || g_config.fovMode == FOV_ORIGINAL) return;

    float aspect = GetAspectOver4By3(), scale = g_config.fovScale;
    float yAt800x600;
    if (g_config.fovMode == FOV_CORRECT)
        yAt800x600 = 0.75f * 2.0f * atanf(tanf(y * 0.5f) / scale / 0.75f);
    else
        yAt800x600 = 2.0f * atanf(tanf(y * 0.5f) / scale);
    *gameX = 2.0f * atanf(tanf(x * 0.5f) / (aspect * scale));
    *gameY = yAt800x600 / aspect;
}

static int SetCameraFov(void* camera, float engineX, float engineY)
{
    return GetLtClientFunction<SetFovFn>(LTCLIENT_SET_CAMERA_FOV)(camera, engineX, engineY);
}

// The camera's main FOV change; logged whenever the game's FOV changes.
static int __cdecl HookSetFovMain(void* camera, float x, float y)
{
    static float loggedX = -1, loggedY = -1;
    float engineX, engineY;
    GameFovToEngine(x, y, &engineX, &engineY);
    if (x != loggedX || y != loggedY) {
        Log("fov: game %.1f x %.1f deg -> %.1f x %.1f deg (screen %dx%d)", x * DEGREES_PER_RADIAN,
            y * DEGREES_PER_RADIAN, engineX * DEGREES_PER_RADIAN, engineY * DEGREES_PER_RADIAN,
            g_screen.width, g_screen.height);
        loggedX = x;
        loggedY = y;
    }
    return SetCameraFov(camera, engineX, engineY);
}

static int __cdecl HookSetFov(void* camera, float x, float y)
{
    float engineX, engineY;
    GameFovToEngine(x, y, &engineX, &engineY);
    return SetCameraFov(camera, engineX, engineY);
}

static int __cdecl HookGetFov(void* camera, float* x, float* y)
{
    int result = GetLtClientFunction<GetFovFn>(LTCLIENT_GET_CAMERA_FOV)(camera, x, y);
    if (x != NULL && y != NULL) EngineFovToGame(*x, *y, x, y);
    return result;
}

// "call [reg+slot]" (FF /2 with a 32-bit displacement, 6 bytes) -> "call hook; nop"
struct FovCallSite { DWORD address; unsigned char modRm; DWORD slot; void* hook; };

bool InstallFovHooks()
{
    const FovCallSite sites[5] = {
        {VA_FOV_SET_MAIN,     0x92, LTCLIENT_SET_CAMERA_FOV, (void*)HookSetFovMain},   // call [edx+2ACh]
        {VA_FOV_SET_WOBBLE_1, 0x90, LTCLIENT_SET_CAMERA_FOV, (void*)HookSetFov},       // call [eax+2ACh]
        {VA_FOV_SET_WOBBLE_2, 0x90, LTCLIENT_SET_CAMERA_FOV, (void*)HookSetFov},
        {VA_FOV_GET_WOBBLE_1, 0x90, LTCLIENT_GET_CAMERA_FOV, (void*)HookGetFov},       // call [eax+2A8h]
        {VA_FOV_GET_WOBBLE_2, 0x90, LTCLIENT_GET_CAMERA_FOV, (void*)HookGetFov}};
    for (int i = 0; i < 5; i++) {
        const unsigned char* code = (const unsigned char*)LoadedAddress(sites[i].address);
        if (code[0] != 0xFF || code[1] != sites[i].modRm || *(const DWORD*)(code + 2) != sites[i].slot)
            return false;
    }
    for (int i = 0; i < 5; i++) {
        DWORD address = LoadedAddress(sites[i].address);
        WriteCall(address, sites[i].hook);
        static const unsigned char NOP = 0x90;
        WriteCode(address + 5, &NOP, 1);
    }
    return true;
}

// --- [FOV] scale from the keys and the Game Options slider -----------------------------------------

void ReapplyCameraFov()
{
    void* camera = *(void**)LoadedAddress(VA_CAMERA_OBJECT);
    if (camera != NULL && GetEngineCamera(camera) != NULL)
        ((MethodFn)LoadedAddress(VA_CAMERA_APPLY_FOV))(camera, 0);
}

void SetFovScale(float scale)
{
    scale = Clamp(scale, g_config.fovScaleMin, g_config.fovScaleMax);
    if (scale > 0.995f && scale < 1.005f) scale = 1.0f;
    g_config.fovScale = scale;
    SaveFovScale(scale);
    ReapplyCameraFov();
    Log("fov scale %.2f (range %.2f .. %.2f)", scale, g_config.fovScaleMin, g_config.fovScaleMax);
}

void StepFovScale(int notches) { SetFovScale(g_config.fovScale + notches * g_config.fovStep); }
