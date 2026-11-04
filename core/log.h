// The fix's log file, cshell_hud.log, next to cshell_orig.dll.
#pragma once

#if defined(__GNUC__) || defined(__clang__)
#define PRINTF_FORMAT __attribute__((format(printf, 1, 2)))
#else
#define PRINTF_FORMAT
#endif

// printf-style line; the file is created on the first call.
void Log(const char* format, ...) PRINTF_FORMAT;
