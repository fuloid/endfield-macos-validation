/* Checks two Wine patches without a game:
 *   0017: the version resource of a builtin DLL (kernel32.dll) is readable, unless
 *         CX_HIDE_BUILTIN_VERSION_RESOURCES=1 hides it;
 *   0018: a child process is started (Wine execs it through a link named after the exe).
 * Prints "version-size=<n> error=<n>" and "child exit=<n>".
 */
#include <windows.h>
#include <stdio.h>
#include <string.h>

int wmain(int argc, wchar_t **argv)
{
    WCHAR self[MAX_PATH], cmdline[MAX_PATH + 16];
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    DWORD handle, size, code = 1;

    if (argc > 1 && !wcscmp(argv[1], L"child")) return 42;

    SetLastError(0);
    size = GetFileVersionInfoSizeW(L"kernel32.dll", &handle);
    printf("version-size=%lu error=%lu\n", (unsigned long)size, (unsigned long)GetLastError());

    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    if (!GetModuleFileNameW(NULL, self, MAX_PATH)) return 2;
    swprintf(cmdline, ARRAYSIZE(cmdline), L"\"%ls\" child", self);
    if (!CreateProcessW(self, cmdline, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) return 2;
    WaitForSingleObject(pi.hProcess, 30000);
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    printf("child exit=%lu\n", (unsigned long)code);
    fflush(stdout);
    return code == 42 ? 0 : 1;
}
