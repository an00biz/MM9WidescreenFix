// Copies the game takes FROM the screen (the save-under behind moving things and dialogs, screen
// captures). The engine can't scale such a copy (it comes out as garbage), so each game surface
// that receives one gets a copy of the fix's own at real resolution with the real pixels, and
// when the game draws its surface back onto the screen, that copy is drawn instead.
#pragma once
#include "core/geometry.h"

struct ScreenCopy {
    void* gameSurface;      // the game's surface the copy belongs to
    void* surface;          // the fix's surface with the real pixels ...
    int   w, h;             // ... and its size
    int   designW, designH; // the size of the copied part in 800x600
    bool  wholeScreen;      // a capture of the whole screen: drawn back to the same place
};

ScreenCopy* FindScreenCopy(void* gameSurface);
void ForgetScreenCopy(void* renderer, void* gameSurface);   // the game surface got other content
// The game copies srcRect of the screen into dest at dstRect (NULL = whole / at 0,0).
char CopyFromScreen(void* renderer, void* dest, const Rect* dstRect, const Rect* srcRect);
// The game draws a surface with a screen copy onto the screen at designRect.
char DrawScreenCopy(void* renderer, void* screen, const ScreenCopy& copy, const Rect& designRect, const Rect* srcRect);
