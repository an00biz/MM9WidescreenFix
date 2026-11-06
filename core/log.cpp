// Writes the fix's log, cshell_hud.log; every line is flushed at once so a crash loses nothing.
#include <stdio.h>
#include <stdarg.h>
#include "core/log.h"
#include "core/cshell_hud.h"

static FILE* g_logFile = NULL;

void Log(const char* format, ...)
{
    if (g_logFile == NULL) {
        char path[MAX_PATH];
        GetFixFilePath("cshell_hud.log", path);
        g_logFile = fopen(path, "w");
        if (g_logFile == NULL) return;
    }

    va_list args;
    va_start(args, format);
    vfprintf(g_logFile, format, args);
    va_end(args);

    fputc('\n', g_logFile);
    fflush(g_logFile);
}
