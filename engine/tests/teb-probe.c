/* Check native-TSD mirrors, loader TLS growth, signal/SEH return and thread startup. */
#include <windows.h>
#include <winternl.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#define DLL_COUNT 80

typedef DWORD (*tls_check_fn)(void);
static HMODULE modules[DLL_COUNT];
static tls_check_fn checks[DLL_COUNT];
static HANDLE ready, proceed;
static void *peb;
static int variant;
static volatile LONG seh_count;

static void *gs_self(void)
{
    void *value;
    __asm__ volatile("movq %%gs:0x30,%0" : "=r" (value));
    return value;
}

static void *gs_tls(void)
{
    void *value;
    __asm__ volatile("movq %%gs:0x58,%0" : "=r" (value));
    return value;
}

static void *gs_peb(void)
{
    void *value;
    __asm__ volatile("movq %%gs:0x60,%0" : "=r" (value));
    return value;
}

static int views(const char *label)
{
    BYTE *teb = (BYTE *)NtCurrentTeb();
    void *tls = *(void **)(teb + 0x58), *tsd = *(void **)(teb + 0x16b8);
    int ok = gs_self() == teb && gs_tls() == tls && gs_peb() == peb &&
             *(void **)(teb + 0x60) == peb && (!variant || (tsd && tsd != teb));

    printf("teb_view=%s result=%s teb=%p tls=%p peb=%p tsd=%p\n",
           label, ok ? "ok" : "failed", teb, tls, peb, tsd);
    return ok;
}

EXCEPTION_DISPOSITION seh_handler(EXCEPTION_RECORD *record, void *frame,
                                  CONTEXT *context, void *dispatch)
{
    (void)frame;
    (void)dispatch;
    if (record->ExceptionCode == EXCEPTION_ILLEGAL_INSTRUCTION)
        context->Rip += 2; /* ud2 */
    else if (record->ExceptionCode != 0xe0424242)
        return ExceptionContinueSearch;
    InterlockedIncrement(&seh_count);
    return ExceptionContinueExecution;
}

extern void seh_roundtrip(void);
__asm__(".text\n"
        ".p2align 4\n"
        ".globl seh_roundtrip\n"
        ".seh_proc seh_roundtrip\n"
        "seh_roundtrip:\n"
        "subq $40,%rsp\n"
        ".seh_stackalloc 40\n"
        ".seh_handler seh_handler,@except\n"
        ".seh_endprologue\n"
        "xorl %r9d,%r9d\n"
        "xorl %r8d,%r8d\n"
        "xorl %edx,%edx\n"
        "movl $0xe0424242,%ecx\n"
        "call RaiseException\n"
        "ud2\n"
        "addq $40,%rsp\n"
        "ret\n"
        ".seh_endproc\n");

static DWORD WINAPI worker(void *arg)
{
    unsigned int i;
    int ok = views(arg ? "existing-thread-start" : "new-thread-start");
    LONG before;

    if (arg)
    {
        SetEvent(ready);
        if (WaitForSingleObject(proceed, 30000) != WAIT_OBJECT_0) return 1;
        ok &= views("existing-thread-after-tls-growth");
    }
    for (i = 0; i < DLL_COUNT; i++) ok &= checks[i]() == 1;
    before = seh_count;
    seh_roundtrip();
    ok &= seh_count == before + 2;
    ok &= views("thread-after-seh");
    printf("thread_tls=%s result=%s\n", arg ? "existing" : "new", ok ? "ok" : "failed");
    return !ok;
}

static int finish_thread(HANDLE thread)
{
    DWORD code;
    if (!thread || WaitForSingleObject(thread, 30000) != WAIT_OBJECT_0 ||
        !GetExitCodeThread(thread, &code)) return 0;
    CloseHandle(thread);
    return code == 0;
}

int main(int argc, char **argv)
{
    NTSTATUS (WINAPI *query)(HANDLE, PROCESSINFOCLASS, void *, ULONG, ULONG *);
    PROCESS_BASIC_INFORMATION info;
    TIME_ZONE_INFORMATION zone;
    FILETIME utc, local;
    HANDLE existing, fresh;
    WCHAR fixture[MAX_PATH], temp[MAX_PATH], dir[MAX_PATH], path[MAX_PATH], *slash;
    void *old_tls;
    unsigned int i, loaded = 0;
    int ok, grew = 0;

    setvbuf(stdout, NULL, _IONBF, 0);
    variant = argc > 1 && !strcmp(argv[1], "--variant");
    query = (void *)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQueryInformationProcess");
    if (!query || query(GetCurrentProcess(), ProcessBasicInformation, &info, sizeof(info), NULL)) return 2;
    peb = info.PebBaseAddress;
    ok = views("main-start");
    GetSystemTimeAsFileTime(&utc);
    ok &= GetTimeZoneInformation(&zone) != TIME_ZONE_ID_INVALID;
    ok &= FileTimeToLocalFileTime(&utc, &local) != 0;
    ok &= views("main-after-localtime");
    seh_roundtrip();
    ok &= seh_count == 2;
    ok &= views("main-after-seh");
    if (!GetModuleFileNameW(NULL, fixture, MAX_PATH) || !(slash = wcsrchr(fixture, L'\\'))) return 2;
    wcscpy(slash + 1, L"teb-tls.dll");
    if (!GetTempPathW(MAX_PATH, temp) || !GetTempFileNameW(temp, L"teb", 0, dir)) return 2;
    DeleteFileW(dir);
    if (!CreateDirectoryW(dir, NULL)) return 2;
    ready = CreateEventW(NULL, TRUE, FALSE, NULL);
    proceed = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!ready || !proceed) return 2;
    existing = CreateThread(NULL, 0, worker, (void *)1, 0, NULL);
    if (!existing || WaitForSingleObject(ready, 10000) != WAIT_OBJECT_0) return 2;
    old_tls = gs_tls();
    for (i = 0; i < DLL_COUNT; i++)
    {
        swprintf(path, MAX_PATH, L"%ls\\tls-%02u.dll", dir, i);
        if (!CopyFileW(fixture, path, FALSE) || !(modules[i] = LoadLibraryW(path))) break;
        loaded++;
        checks[i] = (void *)GetProcAddress(modules[i], "tls_check");
        if (!checks[i]) break;
        ok &= checks[i]() == 1;
        ok &= views("main-tls-load");
        grew |= old_tls != gs_tls();
    }
    if (loaded != DLL_COUNT || !checks[DLL_COUNT - 1])
    {
        printf("tls_load=failed count=%u error=%lu\n", loaded, GetLastError());
        return 2;
    }
    ok &= grew;
    printf("tls_array_grew=%s modules=%u\n", grew ? "yes" : "no", loaded);
    SetEvent(proceed);
    ok &= finish_thread(existing);
    fresh = CreateThread(NULL, 0, worker, NULL, 0, NULL);
    ok &= finish_thread(fresh);
    for (i = 0; i < loaded; i++)
    {
        ok &= FreeLibrary(modules[i]) != 0;
        ok &= views("main-tls-unload");
        swprintf(path, MAX_PATH, L"%ls\\tls-%02u.dll", dir, i);
        ok &= DeleteFileW(path) != 0;
    }
    ok &= RemoveDirectoryW(dir) != 0;
    CloseHandle(ready);
    CloseHandle(proceed);
    printf("seh_roundtrips=%ld\n", seh_count);
    printf("%s TEB mirrors, TLS growth, localtime, SEH and CreateThread\n", ok ? "PASS" : "FAIL");
    return !ok;
}
