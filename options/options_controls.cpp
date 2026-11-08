// Makes and reads the game's interface controls through its own constructors and vtables.
#include "options/options_controls.h"
#include "core/addresses.h"
#include "core/cshell_hud.h"
#include "rendering/renderer.h"

typedef void* (__cdecl    *GameNewFn)(int size, const char* file, int line);
typedef DWORD (__fastcall *GetColorFn)(void* assets, void* edx, int index);
typedef void* (__fastcall *GetFontFn)(void* assets, void* edx, int id);
typedef void* (__cdecl    *LoadPictureFn)(const char* file, int one, DWORD colorKey, float scale);
typedef void* (__cdecl    *MakeLabelFn)(int fontId, Rect* rect, const char* text, void* font, DWORD color, int minusOne, int one);
typedef void* (__fastcall *SliderConstructorFn)(void* self, void* edx, int id);
typedef char  (__fastcall *SliderInitFn)(void* self, void* edx, void* handle, int zero, float min, float max, int x, int y, int w, int h);
typedef void* (__fastcall *ListConstructorFn)(void* self, void* edx, void* renderer, int four, int zero);
typedef char  (__fastcall *ListInitFn)(void* self, void* edx, int x, int y, int w, int h, void* font, DWORD color,
                                       int minusOne, DWORD textColor, DWORD selectedColor, int zero);
typedef char  (__fastcall *AddChildFn)(void* self, void* edx, void* child);
typedef int   (__fastcall *GetIntFn)(void* self, void* edx);
typedef char  (__fastcall *SetIntFn)(void* self, void* edx, int value);
typedef float (__fastcall *GetFloatFn)(void* self, void* edx);
typedef char  (__fastcall *SetFloatFn)(void* self, void* edx, float value);
typedef char  (__fastcall *AddListItemFn)(void* self, void* edx, const char* text, int zero1, int zero2);

static const int OPTIONS_FONT_ID = 0x41;

void* NewGameObject(int size) { return ((GameNewFn)LoadedAddress(VA_GAME_NEW))(size, "cshell_hud.cpp", __LINE__); }

DWORD GetOptionsColor(int index)
{
    return ((GetColorFn)LoadedAddress(VA_GET_COLOR))((void*)LoadedAddress(VA_ASSETS), 0, index);
}

void* GetOptionsFont()
{
    return ((GetFontFn)LoadedAddress(VA_GET_FONT))((void*)LoadedAddress(VA_ASSETS), 0, OPTIONS_FONT_ID);
}

void* LoadPicture(const char* file, DWORD colorKey)
{
    return ((LoadPictureFn)LoadedAddress(VA_LOAD_PICTURE))(file, 1, colorKey, 1.0f);
}

void AddChild(void* parent, void* child)
{
    if (child != NULL) GetMethod<AddChildFn>(parent, CONTROL_ADD_CHILD)(parent, 0, child);
}

void* CreateLabel(void* screen, int x, int y, int w, const char* text, void* font)
{
    Rect rect = MakeRect(x, y, w, 20);
    void* label = ((MakeLabelFn)LoadedAddress(VA_MAKE_LABEL))(OPTIONS_FONT_ID, &rect, text, font, GetOptionsColor(0), -1, 1);
    AddChild(screen, label);
    return label;
}

void* CreateSlider(void* screen, int id, int x, int y, float min, float max, float value)
{
    void* slider = NewGameObject(0x84);
    if (slider == NULL)
        return NULL;
    ((SliderConstructorFn)LoadedAddress(VA_SLIDER_CONSTRUCTOR))(slider, 0, id);
    void* handle = LoadPicture("interfacescreens\\optionsscreen\\OptionsSliderHandle.pcx", GetOptionsColor(0x14));
    GetMethod<SliderInitFn>(slider, SLIDER_INIT)(slider, 0, handle, 0, min, max, x, y, 405, 30);
    SetSliderValue(slider, Clamp(value, min, max));
    AddChild(screen, slider);
    return slider;
}

void* CreateListBox(int x, int y, int w, int h, void* font)
{
    void* list = NewGameObject(0xAC);
    if (list == NULL)
        return NULL;
    ((ListConstructorFn)LoadedAddress(VA_LIST_CONSTRUCTOR))(list, 0, Renderer::Get(), 4, 0);
    *((BYTE*)list + 0x98) = 1;   // set after construction, as the game's own option screens do
    GetMethod<ListInitFn>(list, LIST_INIT)(list, 0, x, y, w, h, font, GetOptionsColor(0), -1,
                                           GetOptionsColor(0), GetOptionsColor(5), 0);
    return list;
}

int   GetControlId(void* control)                { return GetMethod<GetIntFn>(control, CONTROL_GET_ID)(control, 0); }
float GetSliderValue(void* slider)               { return GetMethod<GetFloatFn>(slider, SLIDER_GET_VALUE)(slider, 0); }
void  SetSliderValue(void* slider, float value)  { GetMethod<SetFloatFn>(slider, SLIDER_SET_VALUE)(slider, 0, value); }
void  AddListItem(void* list, const char* text)  { GetMethod<AddListItemFn>(list, LIST_ADD)(list, 0, text, 0, 0); }
int   GetListSelection(void* list)               { return GetMethod<GetIntFn>(list, LIST_GET_SELECTED)(list, 0); }
void  SetListSelection(void* list, int index)    { GetMethod<SetIntFn>(list, LIST_SET_SELECTED)(list, 0, index); }

void* GetScreenBackground(void* screen) { return *(void**)((char*)screen + SCREEN_BACKGROUND); }

BackgroundPainter::BackgroundPainter(void* painterRenderer, void* backgroundPicture)
{
    renderer = painterRenderer;
    background = backgroundPicture;
}

void BackgroundPainter::Blit(int x, int y, void* src, const Rect& srcRect)
{
    Renderer::Blit(renderer, background, x - OPTIONS_BACKGROUND_X, y, src, &srcRect);
}

void BackgroundPainter::Fill(DWORD color, const Rect& rect)
{
    Rect inPicture = MakeRect(rect.x - OPTIONS_BACKGROUND_X, rect.y, rect.w, rect.h);
    Renderer::Fill(renderer, background, color, &inPicture, 1);
}

void* BackgroundPainter::CopyPart(void* picture, const Rect& part)
{
    void* copy = Renderer::CreateSurface(renderer, part.w, part.h);
    if (copy != NULL) Renderer::Blit(renderer, copy, 0, 0, picture, &part);
    return copy;
}

void BackgroundPainter::DeleteCopy(void* copy) { Renderer::DeleteSurface(renderer, copy); }
