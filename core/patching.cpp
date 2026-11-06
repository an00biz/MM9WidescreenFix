// Writes into the loaded original and lithtech.exe: pointers, vtable and import slots, x86 jumps
// and calls; memory protection is lifted for each write and restored after.
#include <string.h>
#include "core/patching.h"
#include "core/cshell_hud.h"

void* PatchPointer(DWORD address, void* value)
{
    void** slot = (void**)address;
    DWORD oldProtection;
    VirtualProtect(slot, 4, PAGE_EXECUTE_READWRITE, &oldProtection);
    void* previous = *slot;
    *slot = value;
    VirtualProtect(slot, 4, oldProtection, &oldProtection);
    return previous;
}

void* PatchVtableSlot(DWORD vtable, int slot, void* hook) { return PatchPointer(LoadedAddress(vtable) + slot, hook); }

bool IsVtableSlot(DWORD vtable, int slot, DWORD expectedFunction)
{
    return *(DWORD*)(LoadedAddress(vtable) + slot) == LoadedAddress(expectedFunction);
}

void WriteCode(DWORD address, const void* bytes, int count)
{
    DWORD oldProtection;
    VirtualProtect((void*)address, count, PAGE_EXECUTE_READWRITE, &oldProtection);
    memcpy((void*)address, bytes, count);
    VirtualProtect((void*)address, count, oldProtection, &oldProtection);
    FlushInstructionCache(GetCurrentProcess(), (void*)address, count);
}

// E8 call / E9 jmp: opcode, then the target relative to the next instruction.
static void WriteRelative(DWORD address, unsigned char opcode, void* target)
{
    unsigned char bytes[5] = {opcode};
    *(DWORD*)(bytes + 1) = (DWORD)target - (address + 5);
    WriteCode(address, bytes, 5);
}

void WriteJump(DWORD address, void* target) { WriteRelative(address, 0xE9, target); }
void WriteCall(DWORD address, void* target) { WriteRelative(address, 0xE8, target); }

bool CallsFunction(DWORD callSite, DWORD function)
{
    const unsigned char* code = (const unsigned char*)LoadedAddress(callSite);
    return code[0] == 0xE8 && LoadedAddress(callSite) + 5 + *(const int*)(code + 1) == LoadedAddress(function);
}

bool BytesMatch(DWORD linkedAddress, const void* bytes, int count)
{
    return memcmp((const void*)LoadedAddress(linkedAddress), bytes, count) == 0;
}

void* PatchImport(HMODULE module, const char* dll, const char* function, void* hook)
{
    BYTE* image = (BYTE*)module;
    IMAGE_NT_HEADERS* headers = (IMAGE_NT_HEADERS*)(image + ((IMAGE_DOS_HEADER*)image)->e_lfanew);
    IMAGE_DATA_DIRECTORY imports = headers->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (imports.VirtualAddress == 0)
        return NULL;

    void* target = (void*)GetProcAddress(GetModuleHandleA(dll), function);
    IMAGE_IMPORT_DESCRIPTOR* descriptor = (IMAGE_IMPORT_DESCRIPTOR*)(image + imports.VirtualAddress);
    for (; descriptor->Name != 0; descriptor++) {
        if (_stricmp((char*)(image + descriptor->Name), dll) != 0) continue;
        IMAGE_THUNK_DATA* thunk = (IMAGE_THUNK_DATA*)(image + descriptor->FirstThunk);
        for (; thunk->u1.Function != 0; thunk++) {
            void* current = (void*)thunk->u1.Function;
            if (current == hook)
                return NULL;
            if (current == target)
                return PatchPointer((DWORD)&thunk->u1.Function, hook);
        }
    }
    return NULL;
}
