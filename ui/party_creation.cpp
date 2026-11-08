// The party creation screen selects a party member through four invisible buttons (ids 1021..1024)
// over the portrait column. When the screen is made, each button copies its rect from a
// placeholder picture (CharacterCreation\PortraitSample.pcx). When that picture doesn't load, the
// buttons stay 0x0 and clicks on the portraits do nothing; the portraits shown later are other
// pictures, and the buttons never learn their size. A bug of the original game: only 1-4 and Tab
// work. Before each draw of the screen the buttons get the rects of the portraits shown.
#include "ui/party_creation.h"
#include "core/addresses.h"
#include "core/cshell_hud.h"
#include "core/log.h"
#include "core/patching.h"
#include "rendering/renderer.h"
#include "settings/config.h"

typedef Rect* (__fastcall *GetRectFn)(void* self, void* edx, Rect* out);
typedef char  (__fastcall *SetRectFn)(void* self, void* edx, const Rect* rect);
typedef int   (__fastcall *GetIntFn)(void* self, void* edx);

static MethodFn g_unhookedDraw = NULL;

static bool IsSameRect(const Rect& a, const Rect& b) { return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h; }

// The portrait a control is the button of, NULL for any other control.
static void* GetPortraitOfButton(void* screen, void* control)
{
    if (GetVtable(control) != (void**)LoadedAddress(VA_BUTTON_VTABLE))
        return NULL;
    int index = GetMethod<GetIntFn>(control, CONTROL_GET_ID)(control, 0) - PORTRAIT_BUTTON_FIRST_ID;
    if (index < 0 || index > 3)
        return NULL;
    else
        return *(void**)((char*)screen + PARTY_CREATION_PORTRAITS + index * 4);
}

// The button takes the portrait's rect, the way the screen gave it the placeholder's.
static void FitButtonToPortrait(void* button, void* portrait)
{
    Rect shown = MakeRect(0, 0, 0, 0);
    GetMethod<GetRectFn>(portrait, CONTROL_GET_RECT)(portrait, 0, &shown);
    const Rect& current = *(const Rect*)((char*)button + BUTTON_RECT);
    if (IsEmpty(shown) || IsSameRect(shown, current)) return;

    Log("party creation: portrait button %d rect (%d,%d %dx%d) -> (%d,%d %dx%d)",
        GetMethod<GetIntFn>(button, CONTROL_GET_ID)(button, 0) - PORTRAIT_BUTTON_FIRST_ID + 1,
        current.x, current.y, current.w, current.h, shown.x, shown.y, shown.w, shown.h);
    GetMethod<SetRectFn>(button, BUTTON_SET_RECT)(button, 0, &shown);
}

// The screen's children: a circular list of {child, next} nodes at +0x38.
static void FitPortraitButtons(void* screen)
{
    void** node = *(void***)((char*)screen + 0x38);
    void* first = node != NULL ? node[0] : NULL;
    if (first == NULL) return;
    void* child = first;
    do {
        void* portrait = GetPortraitOfButton(screen, child);
        if (portrait != NULL) FitButtonToPortrait(child, portrait);
        node = (void**)node[1];
        child = node[0];
    } while (child != first);
}

static char __fastcall HookDraw(void* screen, void*)
{
    if (g_enabled) FitPortraitButtons(screen);
    return g_unhookedDraw(screen, 0);
}

bool InstallPartyCreationFix()
{
    bool isExpected = IsVtableSlot(VA_PARTY_CREATION_VTABLE, CONTROL_DRAW, VA_PARTY_CREATION_DRAW)
        && IsVtableSlot(VA_BUTTON_VTABLE, BUTTON_SET_RECT, VA_BUTTON_SET_RECT)
        && IsVtableSlot(VA_IMAGE_CONTROL_VTABLE, CONTROL_GET_RECT, VA_IMAGE_CONTROL_GET_RECT);
    if (!isExpected) {
        Log("party creation: unexpected code - portrait clicks not fixed");
        return false;
    }
    g_unhookedDraw = (MethodFn)PatchVtableSlot(VA_PARTY_CREATION_VTABLE, CONTROL_DRAW, (void*)HookDraw);
    return true;
}
