// Reads [KEYS] binding text into KeyBinding records and matches them against input messages.
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include "settings/key_bindings.h"

struct NamedModifier { const char* name; int family, state; };
static const NamedModifier MODIFIERS[] = {
    {"lctrl", CTRL_KEYS, LEFT_KEY}, {"rctrl", CTRL_KEYS, RIGHT_KEY}, {"ctrl", CTRL_KEYS, EITHER_KEY},
    {"lcontrol", CTRL_KEYS, LEFT_KEY}, {"rcontrol", CTRL_KEYS, RIGHT_KEY}, {"control", CTRL_KEYS, EITHER_KEY},
    {"lalt", ALT_KEYS, LEFT_KEY}, {"ralt", ALT_KEYS, RIGHT_KEY}, {"alt", ALT_KEYS, EITHER_KEY},
    {"lshift", SHIFT_KEYS, LEFT_KEY}, {"rshift", SHIFT_KEYS, RIGHT_KEY}, {"shift", SHIFT_KEYS, EITHER_KEY}};

struct NamedTrigger { const char* name; int trigger; UINT buttonMessage; int xButton; int virtualKey; };
static const NamedTrigger TRIGGERS[] = {
    {"wheel", TRIGGER_WHEEL, 0, 0, 0}, {"mousewheel", TRIGGER_WHEEL, 0, 0, 0},
    {"mbutton", TRIGGER_MOUSE_BUTTON, WM_MBUTTONDOWN, 0, 0},
    {"wheelclick", TRIGGER_MOUSE_BUTTON, WM_MBUTTONDOWN, 0, 0},
    {"middleclick", TRIGGER_MOUSE_BUTTON, WM_MBUTTONDOWN, 0, 0},
    {"xbutton1", TRIGGER_MOUSE_BUTTON, WM_XBUTTONDOWN, XBUTTON1, 0},
    {"xbutton2", TRIGGER_MOUSE_BUTTON, WM_XBUTTONDOWN, XBUTTON2, 0},
    {"home", TRIGGER_KEY, 0, 0, VK_HOME}, {"end", TRIGGER_KEY, 0, 0, VK_END},
    {"insert", TRIGGER_KEY, 0, 0, VK_INSERT}, {"delete", TRIGGER_KEY, 0, 0, VK_DELETE},
    {"pageup", TRIGGER_KEY, 0, 0, VK_PRIOR}, {"pagedown", TRIGGER_KEY, 0, 0, VK_NEXT},
    {"space", TRIGGER_KEY, 0, 0, VK_SPACE}, {"tab", TRIGGER_KEY, 0, 0, VK_TAB},
    {"backspace", TRIGGER_KEY, 0, 0, VK_BACK}, {"enter", TRIGGER_KEY, 0, 0, VK_RETURN}};

static const int COUNT_OF_MODIFIERS = sizeof(MODIFIERS) / sizeof(MODIFIERS[0]);
static const int COUNT_OF_TRIGGERS  = sizeof(TRIGGERS) / sizeof(TRIGGERS[0]);

bool IsOffValue(const char* text)
{
    return text[0] == '\0' || _stricmp(text, "none") == 0 || _stricmp(text, "off") == 0 || strcmp(text, "0") == 0;
}

static void CopyWithoutSpaces(const char* text, char* out, int size)
{
    int length = 0;
    for (const char* c = text; *c != '\0' && length < size - 1; c++)
        if (*c != ' ' && *c != '\t') out[length++] = *c;
    out[length] = '\0';
}

static bool ParseModifier(const char* token, KeyBinding& binding)
{
    for (int i = 0; i < COUNT_OF_MODIFIERS; i++) {
        if (_stricmp(token, MODIFIERS[i].name) != 0) continue;
        binding.modifier[MODIFIERS[i].family] = MODIFIERS[i].state;
        return true;
    }
    return false;
}

static void SetKey(KeyBinding& binding, int virtualKey)
{
    binding.trigger = TRIGGER_KEY;
    binding.virtualKey = virtualKey;
}

// A named trigger, a letter or digit, F1..F24, Num0..Num9 or 0xNN (a Windows virtual-key code).
static bool ParseTrigger(const char* token, KeyBinding& binding)
{
    for (int i = 0; i < COUNT_OF_TRIGGERS; i++) {
        if (_stricmp(token, TRIGGERS[i].name) != 0) continue;
        binding.trigger       = TRIGGERS[i].trigger;
        binding.buttonMessage = TRIGGERS[i].buttonMessage;
        binding.xButton       = TRIGGERS[i].xButton;
        binding.virtualKey    = TRIGGERS[i].virtualKey;
        return true;
    }

    int length = (int)strlen(token);
    bool isFunctionKey = toupper((unsigned char)token[0]) == 'F' && (length == 2 || length == 3)
                         && atoi(token + 1) >= 1 && atoi(token + 1) <= 24;
    bool isNumpadKey = _strnicmp(token, "num", 3) == 0 && length == 4 && isdigit((unsigned char)token[3]);
    bool isKeyCode = token[0] == '0' && toupper((unsigned char)token[1]) == 'X';

    if (length == 1 && isalnum((unsigned char)token[0]))
        SetKey(binding, toupper((unsigned char)token[0]));
    else if (isFunctionKey)
        SetKey(binding, VK_F1 + atoi(token + 1) - 1);
    else if (isNumpadKey)
        SetKey(binding, VK_NUMPAD0 + token[3] - '0');
    else if (isKeyCode)
        SetKey(binding, (int)strtol(token, NULL, 16) & 0xFF);
    return binding.trigger != TRIGGER_NONE;
}

bool ParseKeyBinding(const char* text, KeyBinding& binding)
{
    memset(&binding, 0, sizeof(binding));
    char compact[64];
    CopyWithoutSpaces(text, compact, sizeof(compact));
    if (IsOffValue(compact))
        return false;

    // Modifiers in any order, then exactly one trigger, which must come last.
    char* token = strtok(compact, "+");
    while (token != NULL) {
        char* next = strtok(NULL, "+");
        if (!ParseModifier(token, binding)) {
            if (next != NULL)
                return false;
            else
                return ParseTrigger(token, binding);
        }
        token = next;
    }
    return false;   // modifiers only
}

static bool IsKeyDown(int virtualKey) { return (GetAsyncKeyState(virtualKey) & 0x8000) != 0; }

bool AreModifiersHeld(const KeyBinding& binding)
{
    static const int KEYS[MODIFIER_FAMILY_COUNT][2] = {
        {VK_LCONTROL, VK_RCONTROL}, {VK_LMENU, VK_RMENU}, {VK_LSHIFT, VK_RSHIFT}};
    for (int family = 0; family < MODIFIER_FAMILY_COUNT; family++) {
        bool left = IsKeyDown(KEYS[family][0]), right = IsKeyDown(KEYS[family][1]);
        bool held;
        switch (binding.modifier[family]) {
        case KEYS_UP:    held = !left && !right; break;
        case LEFT_KEY:  held = left;            break;
        case RIGHT_KEY: held = right;           break;
        default:        held = left || right;   break;
        }
        if (!held) return false;
    }
    return true;
}

bool IsPressedBy(const KeyBinding& binding, const MSG* msg)
{
    if (binding.trigger == TRIGGER_MOUSE_BUTTON && msg->message == binding.buttonMessage)
        return binding.buttonMessage != WM_XBUTTONDOWN || HIWORD(msg->wParam) == binding.xButton;
    else if (binding.trigger == TRIGGER_KEY && (msg->message == WM_KEYDOWN || msg->message == WM_SYSKEYDOWN))
        return (int)msg->wParam == binding.virtualKey;
    else
        return false;
}
