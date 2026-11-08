// [HUD] perf, the driver side: every time the engine locks the screen or copies from it (d3d.ren's
// LockScreen and BlitFromScreen). Without a DirectDraw wrapper each of these makes modern Windows
// copy the back buffer from the graphics card and back, so its cost grows with the resolution.
// They are counted and timed per call chain, and the slow ones written to the log once a second.
#pragma once

// A game draw (perf) made while d3d.ren's Direct3D 2D mode was off: counted per call chain in
// the screen access lines, as moved into that mode (ScopedSceneDraw) or left outside.
void NoteDrawOutsideScene(void* engineSurface, double ms, bool moved);

// Writes "slow: <what> - <call chain>" to the log: the return addresses on the stack that lead
// back into lithtech.exe and the original cshell, nearest first.
void LogSlowCall(const char* what);
// A draw call (perf) slower than this is logged on its own with LogSlowCall.
const double SLOW_CALL_MS = 10.0;

// Called from the draw path: while [HUD] perf is on, puts the timing hooks into the render
// driver's function table (again after the renderer was reloaded, e.g. by a resolution change).
void UpdateScreenAccessHooks();
