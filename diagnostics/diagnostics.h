// Troubleshooting output for the log: one-frame HUD dump ([HUD] dump), draw-call timings
// ([HUD] perf), mouse traces ([HUD] mouse_log) and a crash logger.
#pragma once
#include <windows.h>

// [HUD] dump: one frame of the HUD layout, from one HUD draw to the next.
struct DumpState {
    bool active;
    int  hudDrawsSeen;
    int  depth;          // indentation of the window tree in the dump
};
extern DumpState g_dump;
// Called at each HUD draw: starts a requested dump, or ends the one in progress.
void UpdateDumpAtHudDraw();

// [HUD] mouse_log: hit tests are logged until this tick (set by clicks and hover samples).
extern DWORD g_hitTraceUntil;

// [HUD] perf: counts and times one render call, from construction to destruction. Calls made
// by the fix itself are not counted.
enum PerfOperation { PERF_BLIT, PERF_SCALE, PERF_FILL, PERF_READ_BACK, PERF_CREATE, PERF_DELETE, PERF_OPERATION_COUNT };
class PerfTimer {
public:
    PerfTimer(int operation, void* sourceSurface);
    ~PerfTimer();
private:
    bool   active;
    int    operation;
    void*  source;
    double start;
};

// "address (cshell_orig VA ...)" or "address (module+offset)"
void DescribeAddress(DWORD address, char* out, int size);

void InstallCrashLogger();
// Logs who moves or clips the Windows cursor (active while [HUD] mouse_log=1).
void InstallCursorApiLogging();
