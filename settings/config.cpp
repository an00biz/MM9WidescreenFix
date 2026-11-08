// Loads cshell_hud.ini into g_config (again whenever the file changes) and writes back the
// values the game changes. A missing file or key gives the same defaults as the shipped ini.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "settings/config.h"
#include "core/cshell_hud.h"
#include "core/log.h"

Config g_config;
bool   g_enabled = true;
bool   g_dumpRequested = false;

static FILETIME g_configWriteTime = {0, 0};
static DWORD    g_lastConfigCheck = 0;

// --- reading -------------------------------------------------------------------------------

static void ReadText(const char* path, const char* section, const char* key, const char* fallback,
                     char* out, int size)
{
    GetPrivateProfileStringA(section, key, fallback, out, size, path);
}

static float ReadFloat(const char* path, const char* section, const char* key, float fallback)
{
    char fallbackText[32], text[64];
    _snprintf(fallbackText, sizeof(fallbackText), "%g", fallback);
    ReadText(path, section, key, fallbackText, text, sizeof(text));
    return (float)atof(text);
}

static int ReadInt(const char* path, const char* section, const char* key, int fallback)
{
    return GetPrivateProfileIntA(section, key, fallback, path);
}

static bool ReadFlag(const char* path, const char* section, const char* key, bool fallback)
{
    return ReadInt(path, section, key, fallback ? 1 : 0) != 0;
}

// A size setting: a number, or "max" (stored as 0) for as large as fits.
static float ReadSize(const char* path, const char* section, const char* key)
{
    char text[32];
    ReadText(path, section, key, "max", text, sizeof(text));
    if (text[0] == '\0' || _stricmp(text, "max") == 0)
        return 0.0f;
    else
        return (float)atof(text);
}

static void ReadKeyBinding(const char* path, const char* key, const char* fallback, KeyBinding& binding)
{
    char text[64];
    ReadText(path, "KEYS", key, fallback, text, sizeof(text));
    if (!ParseKeyBinding(text, binding) && !IsOffValue(text))
        Log("keys: can't read %s=%s (binding off)", key, text);
}

static int ParseAlign(const char* text)
{
    if (_stricmp(text, "left") == 0)
        return ALIGN_LEFT;
    else if (_stricmp(text, "right") == 0)
        return ALIGN_RIGHT;
    else
        return ALIGN_CENTER;
}

static void AddAlignRule(Config& c, DWORD windowClass, int align)
{
    if (c.alignRuleCount >= MAX_ALIGN_RULES) return;
    c.alignRules[c.alignRuleCount].windowClass = windowClass;
    c.alignRules[c.alignRuleCount].align = align;
    c.alignRuleCount++;
}

// [UI] align_left / align_right / align_center = window classes separated by spaces, commas or ;
static void ReadAlignRules(Config& c, const char* path, const char* key, int align)
{
    char text[512];
    ReadText(path, "UI", key, "", text, sizeof(text));
    for (char* token = strtok(text, " ,;"); token != NULL; token = strtok(NULL, " ,;")) {
        DWORD windowClass = strtoul(token, NULL, 16);
        if (windowClass != 0) AddAlignRule(c, windowClass, align);
    }
}

// [HUD] windows = class[:anchor] list; anchor = top / middle / bottom and/or left / center / right
static void ReadHudWindowRules(Config& c, const char* path)
{
    char text[512];
    ReadText(path, "HUD", "windows", "100C0F04:bottom-center,100C98E0,100C0198", text, sizeof(text));
    c.hudWindowCount = 0;
    for (char* token = strtok(text, " ,;"); token != NULL && c.hudWindowCount < MAX_HUD_WINDOW_RULES;
         token = strtok(NULL, " ,;")) {
        DWORD windowClass = strtoul(token, NULL, 16);
        if (windowClass == 0) continue;

        HudWindowRule& rule = c.hudWindows[c.hudWindowCount++];
        rule.windowClass = windowClass;
        rule.column = -1;
        rule.row = -1;
        char* anchor = strchr(token, ':');
        if (anchor == NULL) continue;

        _strlwr(anchor);
        if (strstr(anchor, "left"))
            rule.column = COLUMN_LEFT;
        else if (strstr(anchor, "center") || strstr(anchor, "centre"))
            rule.column = COLUMN_CENTER;
        else if (strstr(anchor, "right"))
            rule.column = COLUMN_RIGHT;
        if (strstr(anchor, "top"))
            rule.row = ROW_TOP;
        else if (strstr(anchor, "middle"))
            rule.row = ROW_MIDDLE;
        else if (strstr(anchor, "bottom"))
            rule.row = ROW_BOTTOM;
    }
}

// [HUD] bottom_right = x,y,w,h;x,y,w,h;...
static void ReadBottomRightPins(Config& c, const char* path)
{
    char text[256];
    ReadText(path, "HUD", "bottom_right", "700,470,80,80", text, sizeof(text));
    c.bottomRightPinCount = 0;
    const char* at = text;
    while (*at != '\0' && c.bottomRightPinCount < MAX_BOTTOM_RIGHT_PINS) {
        Rect pin;
        int consumed = 0;
        if (sscanf(at, "%d,%d,%d,%d%n", &pin.x, &pin.y, &pin.w, &pin.h, &consumed) == 4 && consumed > 0) {
            c.bottomRightPins[c.bottomRightPinCount++] = pin;
            at += consumed;
        } else {
            at++;
        }
        while (*at == ';' || *at == ' ') at++;
    }
}

static void ReadHudSection(Config& c, const char* path)
{
    c.enable      = ReadFlag(path, "HUD", "enable", true);
    c.hudSize     = ReadSize(path, "HUD", "size");
    c.hudSizeStep = ReadFloat(path, "HUD", "size_step", 0.05f);
    if (c.hudSizeStep < 0.01f) c.hudSizeStep = 0.01f;
    c.marginX = ReadFloat(path, "HUD", "margin_x", 0);
    c.marginY = ReadFloat(path, "HUD", "margin_y", 0);

    c.splitLeft         = ReadFloat(path, "HUD", "split_left", 266);
    c.splitRight        = ReadFloat(path, "HUD", "split_right", 533);
    c.splitTop          = ReadFloat(path, "HUD", "split_top", 200);
    c.splitBottom       = ReadFloat(path, "HUD", "split_bottom", 400);
    c.splitSideLeft     = ReadFloat(path, "HUD", "split_side_left", 530);
    c.splitSideRight    = ReadFloat(path, "HUD", "split_side_right", 400);
    c.splitSideRightLow = ReadFloat(path, "HUD", "split_side_right_low", 530);
    c.npcColumnX        = ReadFloat(path, "HUD", "npc_column_x", 690);
    ReadBottomRightPins(c, path);
    ReadHudWindowRules(c, path);

    c.hudMouse = ReadFlag(path, "HUD", "mouse", true);
    c.dump     = ReadFlag(path, "HUD", "dump", false);
    c.mouseLog = ReadFlag(path, "HUD", "mouse_log", false);
    c.perf     = ReadFlag(path, "HUD", "perf", false);
}

static void ReadUiSection(Config& c, const char* path)
{
    char text[32];
    c.uiScale = ReadSize(path, "UI", "scale");
    ReadText(path, "UI", "align", "center", text, sizeof(text));
    c.uiAlign = ParseAlign(text);
    ReadText(path, "UI", "mode", "fit", text, sizeof(text));
    c.uiStretched = _strnicmp(text, "stretch", 7) == 0;
    c.blackBars = ReadFlag(path, "UI", "black_bars", true);
    c.barsLog   = ReadFlag(path, "UI", "bars_log", false);
    c.sceneDraws = ReadFlag(path, "UI", "scene_draws", true);

    c.alignRuleCount = 0;
    ReadAlignRules(c, path, "align_left", ALIGN_LEFT);
    ReadAlignRules(c, path, "align_right", ALIGN_RIGHT);
    ReadAlignRules(c, path, "align_center", ALIGN_CENTER);
    if (ReadFlag(path, "UI", "loot_right", true)) AddAlignRule(c, VA_LOOT_WINDOW_VTABLE, ALIGN_RIGHT);
}

static void ReadFovSection(Config& c, const char* path)
{
    char text[32];
    ReadText(path, "FOV", "mode", "hor+", text, sizeof(text));
    if (_stricmp(text, "original") == 0)
        c.fovMode = FOV_ORIGINAL;
    else if (_stricmp(text, "correct") == 0)
        c.fovMode = FOV_CORRECT;
    else
        c.fovMode = FOV_HOR_PLUS;

    // The range must hold the default 1.0 and be at least 0.1 wide.
    c.fovScaleMin = Clamp(ReadFloat(path, "FOV", "min_fov_scale", 0.5f), 0.2f, 1.0f);
    c.fovScaleMax = Clamp(ReadFloat(path, "FOV", "max_fov_scale", 1.7f), 1.0f, 5.0f);
    if (c.fovScaleMax - c.fovScaleMin < 0.1f) {
        c.fovScaleMin = 0.5f;
        c.fovScaleMax = 1.7f;
    }
    c.fovScale = Clamp(ReadFloat(path, "FOV", "scale", 1.0f), c.fovScaleMin, c.fovScaleMax);
}

static void ReadOtherSections(Config& c, const char* path)
{
    char text[32];
    ReadText(path, "VIDEO", "mode", "fit", text, sizeof(text));
    if (_stricmp(text, "original") == 0)
        c.videoMode = VIDEO_ORIGINAL;
    else if (_stricmp(text, "stretch") == 0)
        c.videoMode = VIDEO_STRETCH;
    else
        c.videoMode = VIDEO_FIT;
    c.cameraNudge = ReadInt(path, "VIDEO", "camera_nudge", 1);
    c.applyVolume = ReadFlag(path, "SOUND", "apply_volume", true);

    ReadKeyBinding(path, "hud_size",  "LCtrl+Wheel",   c.hudSizeKey);
    ReadKeyBinding(path, "hud_reset", "LCtrl+MButton", c.hudSizeResetKey);
    ReadKeyBinding(path, "fov",       "LAlt+Wheel",    c.fovKey);
    ReadKeyBinding(path, "fov_reset", "LAlt+MButton",  c.fovResetKey);
    c.fovStep = ReadFloat(path, "KEYS", "fov_step", 0.05f);
    if (c.fovStep < 0.01f) c.fovStep = 0.01f;
    c.fovWheelInverted = ReadFlag(path, "KEYS", "fov_wheel_invert", false);
}

static void LogConfig(const Config& c)
{
    Log("cfg: enable=%d(start) hud.size=%.3f(0=max) margin=%.0f,%.0f splitX=%.0f,%.0f splitY=%.0f,%.0f "
        "mouse=%d dump=%d ui.scale=%.3f(0=max)", (int)g_enabled, c.hudSize, c.marginX, c.marginY,
        c.splitLeft, c.splitRight, c.splitTop, c.splitBottom, (int)c.hudMouse, (int)c.dump, c.uiScale);
    Log("     fov: %s scale=%.3f", FovModeName(c.fovMode), c.fovScale);
    for (int i = 0; i < c.alignRuleCount; i++)
        Log("     window %08lX -> %s", c.alignRules[i].windowClass, AlignName(c.alignRules[i].align));
}

// The file changed since the last load (or must be read anyway at start).
static bool HasConfigChanged(const char* path, bool atStart)
{
    WIN32_FILE_ATTRIBUTE_DATA attributes;
    if (!GetFileAttributesExA(path, GetFileExInfoStandard, &attributes)) {
        if (atStart) Log("no cshell_hud.ini - defaults");
        return atStart;
    }
    if (!atStart && CompareFileTime(&attributes.ftLastWriteTime, &g_configWriteTime) == 0)
        return false;
    g_configWriteTime = attributes.ftLastWriteTime;
    return true;
}

void LoadConfig(bool atStart)
{
    DWORD now = GetTickCount();
    if (!atStart && now - g_lastConfigCheck < 500) return;
    g_lastConfigCheck = now;

    char path[MAX_PATH];
    GetFixFilePath("cshell_hud.ini", path);
    if (!HasConfigChanged(path, atStart)) return;

    Config c;
    memset(&c, 0, sizeof(c));
    ReadHudSection(c, path);
    ReadUiSection(c, path);
    ReadFovSection(c, path);
    ReadOtherSections(c, path);

    if (c.dump && !g_config.dump) g_dumpRequested = true;   // each time dump goes 0 -> 1
    if (atStart) g_enabled = c.enable;
    g_config = c;
    LogConfig(c);
}

// --- writing -------------------------------------------------------------------------------

static void SaveSetting(const char* section, const char* key, const char* value)
{
    char path[MAX_PATH];
    GetFixFilePath("cshell_hud.ini", path);
    WritePrivateProfileStringA(section, key, value, path);
}

void SaveHudSize(float size, bool isMax)
{
    char text[32];
    if (isMax)
        strcpy(text, "max");
    else
        _snprintf(text, sizeof(text), "%.2f", size);
    SaveSetting("HUD", "size", text);
}

void SaveFovScale(float scale)
{
    char text[32];
    _snprintf(text, sizeof(text), "%.2f", scale);
    SaveSetting("FOV", "scale", text);
}

void SaveFovMode(int mode) { SaveSetting("FOV", "mode", FovModeName(mode)); }

// --- lookups -------------------------------------------------------------------------------

int FindAlignRule(DWORD windowClass)
{
    for (int i = 0; i < g_config.alignRuleCount; i++)
        if (g_config.alignRules[i].windowClass == windowClass)
            return g_config.alignRules[i].align;
    return ALIGN_NONE;
}

const char* AlignName(int align)
{
    switch (align) {
    case ALIGN_CENTER: return "center";
    case ALIGN_LEFT:   return "left";
    case ALIGN_RIGHT:  return "right";
    default:           return "";
    }
}

const char* FovModeName(int mode)
{
    switch (mode) {
    case FOV_ORIGINAL: return "original";
    case FOV_CORRECT:  return "correct";
    default:           return "hor+";
    }
}
