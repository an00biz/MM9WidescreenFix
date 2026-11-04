// The original cshell.dll loaded in memory, address conversion into it, and the folder that holds
// the fix's own files (cshell_hud.ini, cshell_hud.log).
#pragma once
#include <windows.h>
#include "core/addresses.h"

extern HMODULE g_originalDll;    // cshell_orig.dll
extern DWORD   g_originalBase;   // where it was loaded

// An address of the original as linked (addresses.h) -> the same place in the loaded original.
inline DWORD LoadedAddress(DWORD linked) { return g_originalBase + (linked - VA_IMAGE_BASE); }
// ... and back, for the log.
inline DWORD LinkedAddress(DWORD loaded) { return loaded - g_originalBase + VA_IMAGE_BASE; }

// "<folder of cshell_orig.dll>\<fileName>"
void GetFixFilePath(const char* fileName, char* out);

// The engine's client interface (ILTClient) and one of its functions, by vtable offset.
inline void* GetLtClient() { return *(void**)LoadedAddress(VA_LTCLIENT_PTR); }
template <typename Fn> inline Fn GetLtClientFunction(int slot) { return *(Fn*)((char*)GetLtClient() + slot); }
