// The game's own interface controls (labels, sliders, list boxes), made and read by the options
// screens the fix extends, and painting extra art into an options screen's background picture.
#pragma once
#include <windows.h>
#include "core/geometry.h"

// The options panel's background picture starts at this screen x (800x600 space).
const int OPTIONS_BACKGROUND_X = 250;

void* NewGameObject(int size);                // the game's operator new
DWORD GetOptionsColor(int index);
void* GetOptionsFont();
void* LoadPicture(const char* file, DWORD colorKey);
void  AddChild(void* parent, void* child);    // nothing for a NULL child

// Each made control is added to the screen, except the list box (the caller adds it).
void* CreateLabel(void* screen, int x, int y, int w, const char* text, void* font);
// Same size and handle as the game's own option sliders (405x30).
void* CreateSlider(void* screen, int id, int x, int y, float min, float max, float value);
void* CreateListBox(int x, int y, int w, int h, void* font);

int   GetControlId(void* control);
float GetSliderValue(void* slider);
void  SetSliderValue(void* slider, float value);
void  AddListItem(void* list, const char* text);
int   GetListSelection(void* list);
void  SetListSelection(void* list, int index);

// The background picture of an options screen.
void* GetScreenBackground(void* screen);

// Paints into an options screen's background picture once, when the screen opens (drawing the
// pieces every frame cost a lot of FPS). Positions are in 800x600 screen space.
class BackgroundPainter {
public:
    BackgroundPainter(void* renderer, void* background);
    void Blit(int x, int y, void* src, const Rect& srcRect);
    void Fill(DWORD color, const Rect& rect);
    // A copy of part of a picture: a piece of the background itself must be copied before it
    // is drawn back into it. Free with DeleteCopy.
    void* CopyPart(void* picture, const Rect& part);
    void  DeleteCopy(void* copy);
private:
    void* renderer;
    void* background;
};
