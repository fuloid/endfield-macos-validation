/* A real PE TLS template, including on MinGW builds that use emutls for __thread. */
#include <windows.h>
#include <winternl.h>
#include <stdint.h>

static DWORD tls_value __attribute__((section(".tls$BBB"), used)) = 0x12345678;

__declspec(dllexport) DWORD tls_check(void)
{
    HMODULE module;
    IMAGE_NT_HEADERS *nt;
    IMAGE_TLS_DIRECTORY *dir;
    void **slots = *(void ***)((BYTE *)NtCurrentTeb() + 0x58);
    DWORD *value, index;

    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                            GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            (const char *)&tls_value, &module)) return 0;
    nt = (IMAGE_NT_HEADERS *)((BYTE *)module + ((IMAGE_DOS_HEADER *)module)->e_lfanew);
    if (!nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_TLS].VirtualAddress) return 0;
    dir = (IMAGE_TLS_DIRECTORY *)((BYTE *)module +
             nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_TLS].VirtualAddress);
    if ((uintptr_t)&tls_value < dir->StartAddressOfRawData ||
        (uintptr_t)(&tls_value + 1) > dir->EndAddressOfRawData) return 0;
    index = *(DWORD *)(uintptr_t)dir->AddressOfIndex;
    if (!slots || !slots[index]) return 0;
    value = (DWORD *)((BYTE *)slots[index] + (uintptr_t)&tls_value - dir->StartAddressOfRawData);
    if (*value != 0x12345678) return 0;
    *value += 1;
    return 1;
}
