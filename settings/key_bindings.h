// [KEYS] bindings - "<modifiers>+<trigger>", e.g. "LCtrl+Wheel", "LAlt+MButton", "Shift+F5" -
// read from text and matched against the game's input messages.
#pragma once
#include <windows.h>

enum ModifierFamily { CTRL_KEYS, ALT_KEYS, SHIFT_KEYS, MODIFIER_FAMILY_COUNT };
enum ModifierState  { KEYS_UP, LEFT_KEY, RIGHT_KEY, EITHER_KEY };   // which key of a family must be down
enum TriggerKind    { TRIGGER_NONE, TRIGGER_WHEEL, TRIGGER_MOUSE_BUTTON, TRIGGER_KEY };

struct KeyBinding {
    int  modifier[MODIFIER_FAMILY_COUNT];   // ModifierState per family; unlisted families must be up
    int  trigger;                      // TriggerKind
    UINT buttonMessage;                // TRIGGER_MOUSE_BUTTON: WM_MBUTTONDOWN or WM_XBUTTONDOWN ...
    int  xButton;                      // ... and for the latter XBUTTON1 / XBUTTON2
    int  virtualKey;                   // TRIGGER_KEY
};

// "none", "off", "0" or nothing: the binding is turned off.
bool IsOffValue(const char* text);
// False for an off value and for text that is not a binding; the binding is then off.
bool ParseKeyBinding(const char* text, KeyBinding& binding);

bool AreModifiersHeld(const KeyBinding& binding);
// The message is the binding's mouse-button press or key press (modifiers not checked).
bool IsPressedBy(const KeyBinding& binding, const MSG* msg);
