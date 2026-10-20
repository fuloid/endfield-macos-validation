/* Compare Wine's normal Nt entry point with a direct syscall, without a game.
 * Extract the syscall number from the loaded ntdll, never from a Windows version table.
 */
#include <windows.h>
#include <winternl.h>
#include <stdio.h>
#include <string.h>

typedef NTSTATUS (WINAPI *query_fn)(HANDLE, PROCESSINFOCLASS, void *, ULONG, ULONG *);
static query_fn normal_query, direct_query;

static DWORD WINAPI check_thread(void *unused)
{
    PROCESS_BASIC_INFORMATION normal, direct;
    ULONG normal_size = 0, direct_size = 0;
    NTSTATUS a, b;
    (void)unused;
    memset(&normal, 0, sizeof(normal));
    memset(&direct, 0, sizeof(direct));
    a = normal_query(GetCurrentProcess(), ProcessBasicInformation,
                     &normal, sizeof(normal), &normal_size);
    b = direct_query(GetCurrentProcess(), ProcessBasicInformation,
                     &direct, sizeof(direct), &direct_size);
    printf("thread=%lu normal=%08lx direct=%08lx normal_pid=%llu direct_pid=%llu\n",
           GetCurrentThreadId(), (unsigned long)a, (unsigned long)b,
           (unsigned long long)normal.UniqueProcessId,
           (unsigned long long)direct.UniqueProcessId);
    fflush(stdout);
    return a != 0 || b != a || normal_size != direct_size ||
           normal.UniqueProcessId != GetCurrentProcessId() ||
           direct.UniqueProcessId != normal.UniqueProcessId ||
           direct.PebBaseAddress != normal.PebBaseAddress;
}

int main(void)
{
    unsigned char code[] = {0x4c,0x8b,0xd1,0xb8,0,0,0,0,0x0f,0x05,0xc3};
    const unsigned char *entry;
    void *memory;
    DWORD old_protect, failures, result;
    HANDLE thread;
    normal_query = (query_fn)(void *)GetProcAddress(GetModuleHandleW(L"ntdll.dll"),
                                                   "NtQueryInformationProcess");
    entry = (const unsigned char *)(void *)normal_query;
    if (!entry || memcmp(entry, code, 4))
    {
        fprintf(stderr, "Unsupported ntdll thunk; refusing to guess syscall number\n");
        return 2;
    }
    memcpy(code + 4, entry + 4, 4);
    printf("NtQueryInformationProcess syscall=%lu\n", *(const DWORD *)(entry + 4));
    memory = VirtualAlloc(NULL, sizeof(code), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!memory) return 2;
    memcpy(memory, code, sizeof(code));
    if (!VirtualProtect(memory, sizeof(code), PAGE_EXECUTE_READ, &old_protect)) return 2;
    FlushInstructionCache(GetCurrentProcess(), memory, sizeof(code));
    direct_query = (query_fn)memory;
    failures = check_thread(NULL);
    thread = CreateThread(NULL, 0, check_thread, NULL, 0, NULL);
    if (!thread) return 2;
    if (WaitForSingleObject(thread, 10000) != WAIT_OBJECT_0 ||
        !GetExitCodeThread(thread, &result)) return 2;
    failures += result;
    CloseHandle(thread);
    VirtualFree(memory, 0, MEM_RELEASE);
    printf("%s direct syscall on main and worker threads\n", failures ? "FAIL" : "PASS");
    return failures ? 1 : 0;
}
