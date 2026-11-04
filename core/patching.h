// Changing code and pointers in loaded modules: vtable slots, import slots, jumps and calls,
// and checking that the bytes about to be patched are the expected ones.
#pragma once
#include <windows.h>

// Inside a hook: the address its caller returns to, which tells which call site called it.
#ifdef _MSC_VER
#include <intrin.h>
#pragma intrinsic(_ReturnAddress)
#define RETURN_ADDRESS() ((DWORD)_ReturnAddress())
#else
#define RETURN_ADDRESS() ((DWORD)__builtin_return_address(0))
#endif

// Writes a pointer-sized value; returns the previous one.
void* PatchPointer(DWORD address, void* value);
// A slot (byte offset) of a vtable of the original, given as linked; returns the previous function.
void* PatchVtableSlot(DWORD vtable, int slot, void* hook);
// The slot holds the given function of the original (both as linked).
bool  IsVtableSlot(DWORD vtable, int slot, DWORD expectedFunction);

void WriteCode(DWORD address, const void* bytes, int count);
void WriteJump(DWORD address, void* target);        // jmp rel32 (5 bytes)
void WriteCall(DWORD address, void* target);        // call rel32 (5 bytes)
// The "call rel32" at a linked address of the original calls the given linked function.
bool CallsFunction(DWORD callSite, DWORD function);
// The bytes at a linked address of the original are these.
bool BytesMatch(DWORD linkedAddress, const void* bytes, int count);

// Points module's import of dll!function at the hook; returns the previous pointer, or NULL
// when not found or already hooked.
void* PatchImport(HMODULE module, const char* dll, const char* function, void* hook);
