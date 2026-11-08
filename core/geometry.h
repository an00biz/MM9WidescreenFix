// Rectangles and rounding for the code that maps the game's 800x600 design space to the screen.
#pragma once
#include <math.h>

// A rectangle as the engine passes it: left, top, width, height (four ints in a row).
struct Rect { int x, y, w, h; };

// A mapping from the 800x600 design space to the screen: screen = offset + design * scale, per
// axis (the two scales differ only for [UI] mode=stretched).
struct Mapping { float scaleX, scaleY, offsetX, offsetY; };

inline Rect MakeRect(int x, int y, int w, int h)
{
    Rect r = {x, y, w, h};
    return r;
}

inline int  Round(float v)              { return (int)floorf(v + 0.5f); }
inline bool IsEmpty(const Rect& r)      { return r.w <= 0 || r.h <= 0; }
inline bool Contains(const Rect& r, int x, int y) { return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h; }
inline bool CoversDesignScreen(const Rect& r)     { return r.x <= 0 && r.y <= 0 && r.x + r.w >= 800 && r.y + r.h >= 600; }

// The smallest rectangle holding both.
inline Rect Union(const Rect& a, const Rect& b)
{
    int left   = a.x < b.x ? a.x : b.x;
    int top    = a.y < b.y ? a.y : b.y;
    int right  = a.x + a.w > b.x + b.w ? a.x + a.w : b.x + b.w;
    int bottom = a.y + a.h > b.y + b.h ? a.y + a.h : b.y + b.h;
    return MakeRect(left, top, right - left, bottom - top);
}

inline float Clamp(float v, float low, float high)
{
    if (v < low)
        return low;
    else if (v > high)
        return high;
    else
        return v;
}
