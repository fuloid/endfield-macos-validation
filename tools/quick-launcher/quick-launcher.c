/*
 * Quick-Launcher.exe: start Arknights: Endfield directly on Windows, skipping the GRYPHLINK UI.
 *
 * Put it in the GRYPHLINK folder, next to Launcher.exe. It starts the newest
 * .\<version>\Games.exe suspended, then launches .\games\Arknights Endfield\Endfield.exe with
 * Games.exe as its parent process, which satisfies the game's launcher check (OneChannelLC.dll).
 * Extra arguments are passed to the game. The suspended Games.exe is closed when the game exits.
 *
 * The manifest requests administrator rights, so the one UAC prompt for Quick-Launcher covers
 * both Games.exe and the game: they inherit its elevated token.
 *
 * Derived from mary-ext/crossover-wine-endfield tools/direct-launch.c
 * (MIT License, Copyright (c) 2026 Endfield_FineWine contributors).
 *
 * Build: x86_64-w64-mingw32-windres quick-launcher.rc -O coff -o quick-launcher.res
 *        x86_64-w64-mingw32-gcc -O2 -Wall -Wextra -municode -mwindows quick-launcher.c \
 *            quick-launcher.res -o Quick-Launcher.exe
 */

#include <windows.h>
#include <strsafe.h>
#include <wchar.h>

#define TITLE L"Endfield Quick Launcher"
#define CMDLINE_MAX 32768

static void fail(DWORD err, const WCHAR *fmt, ...)
{
    WCHAR msg[1024], sys[512] = L"";
    va_list args;
    size_t len;

    va_start(args, fmt);
    StringCchVPrintfW(msg, ARRAYSIZE(msg), fmt, args);
    va_end(args);

    if (err)
    {
        if (FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, err, 0,
                           sys, ARRAYSIZE(sys), NULL))
        {
            len = wcslen(sys);
            while (len && (sys[len - 1] == '\r' || sys[len - 1] == '\n')) sys[--len] = 0;
        }
        len = wcslen(msg);
        StringCchPrintfW(msg + len, ARRAYSIZE(msg) - len, L"\n\nError %lu: %ls", err, sys);
    }

    MessageBoxW(NULL, msg, TITLE, MB_OK | MB_ICONERROR);
}

/* Sets the last error on failure so callers can report it. */
static BOOL file_exists(const WCHAR *path)
{
    DWORD attrs = GetFileAttributesW(path);
    if (attrs == INVALID_FILE_ATTRIBUTES) return FALSE;
    if (!(attrs & FILE_ATTRIBUTE_DIRECTORY)) return TRUE;
    SetLastError(ERROR_FILE_NOT_FOUND);
    return FALSE;
}

/* Pack a.b[.c[.d]] into 16-bit fields for numeric version comparison (1.10.0 > 1.9.2). */
static BOOL parse_version(const WCHAR *name, ULONGLONG *ver)
{
    const WCHAR *p = name;
    int i;

    *ver = 0;
    for (i = 0; i < 4; i++)
    {
        unsigned long part;
        WCHAR *end;

        if (*p < '0' || *p > '9') return FALSE;
        part = wcstoul(p, &end, 10);
        if (part > 0xffff) return FALSE;
        *ver |= (ULONGLONG)part << (16 * (3 - i));
        p = end;
        if (!*p) return i >= 1;
        if (*p++ != '.') return FALSE;
    }
    return FALSE;
}

/* The newest <root>\<version>\Games.exe. */
static BOOL find_launcher(const WCHAR *root, WCHAR *out, size_t size)
{
    ULONGLONG best = 0, ver;
    WIN32_FIND_DATAW fd;
    WCHAR pattern[MAX_PATH], path[MAX_PATH];
    HANDLE find;

    *out = 0;
    if (FAILED(StringCchPrintfW(pattern, ARRAYSIZE(pattern), L"%ls\\*", root))) return FALSE;
    if ((find = FindFirstFileW(pattern, &fd)) == INVALID_HANDLE_VALUE) return FALSE;
    do
    {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
        if (!parse_version(fd.cFileName, &ver)) continue;
        if (*out && ver <= best) continue;
        if (FAILED(StringCchPrintfW(path, ARRAYSIZE(path), L"%ls\\%ls\\Games.exe", root, fd.cFileName))) continue;
        if (!file_exists(path)) continue;
        if (FAILED(StringCchCopyW(out, size, path))) continue;
        best = ver;
    } while (FindNextFileW(find, &fd));
    FindClose(find);
    return *out != 0;
}

/* Quote arguments using CommandLineToArgvW rules. */
static BOOL append_arg(WCHAR *cmd, size_t size, const WCHAR *arg)
{
    size_t len = wcslen(cmd);
    const WCHAR *p;

#define PUT(c) do { if (len + 1 >= size) return FALSE; cmd[len++] = (c); } while (0)
    if (len) PUT(' ');
    PUT('"');
    for (p = arg;; p++)
    {
        unsigned int backslashes = 0;
        while (*p == '\\') { backslashes++; p++; }
        /* Backslashes before a quote, including the closing one, are doubled. */
        if (!*p || *p == '"') backslashes *= 2;
        while (backslashes--) PUT('\\');
        if (!*p) break;
        if (*p == '"') PUT('\\');
        PUT(*p);
    }
    PUT('"');
#undef PUT
    cmd[len] = 0;
    return TRUE;
}

/* Kill the launcher when this process exits, even if terminated externally.
 * Silent breakaway keeps the game out of the launcher's job. */
static HANDLE create_kill_job(void)
{
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION info;
    HANDLE job = CreateJobObjectW(NULL, NULL);

    if (!job) return NULL;
    ZeroMemory(&info, sizeof(info));
    info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE |
                                            JOB_OBJECT_LIMIT_SILENT_BREAKAWAY_OK;
    if (!SetInformationJobObject(job, JobObjectExtendedLimitInformation, &info, sizeof(info)))
    {
        CloseHandle(job);
        return NULL;
    }
    return job;
}

/* Folder containing this executable, without a trailing backslash. */
static BOOL exe_dir(WCHAR *out, DWORD size)
{
    DWORD len = GetModuleFileNameW(NULL, out, size);
    WCHAR *slash;

    if (!len || len >= size) return FALSE;
    if (!(slash = wcsrchr(out, L'\\'))) return FALSE;
    *slash = 0;
    return TRUE;
}

int wmain(int argc, WCHAR **argv)
{
    static const WCHAR *game_dirs[] = { L"Arknights Endfield", L"Arknight Endfield" };
    static WCHAR cmd[CMDLINE_MAX];
    WCHAR root[MAX_PATH], launcher[MAX_PATH], game_dir[MAX_PATH], game[MAX_PATH];
    WCHAR lcmd[MAX_PATH + 3] = L"";
    STARTUPINFOW lsi;
    STARTUPINFOEXW si;
    PROCESS_INFORMATION lpi, pi;
    LPPROC_THREAD_ATTRIBUTE_LIST attrs = NULL;
    HANDLE job = NULL;
    DWORD exit_code = 1;
    SIZE_T size = 0;
    BOOL found = FALSE;
    int i;

    ZeroMemory(&lsi, sizeof(lsi));
    lsi.cb = sizeof(lsi);
    ZeroMemory(&si, sizeof(si));
    si.StartupInfo.cb = sizeof(si);
    ZeroMemory(&lpi, sizeof(lpi));
    ZeroMemory(&pi, sizeof(pi));

    if (!exe_dir(root, ARRAYSIZE(root)))
    {
        fail(GetLastError(), L"Couldn't find the folder Quick-Launcher.exe is in.");
        return 1;
    }
    if (!find_launcher(root, launcher, ARRAYSIZE(launcher)))
    {
        fail(0, L"Couldn't find %ls\\<version>\\Games.exe.\n\n"
                L"Put Quick-Launcher.exe in the GRYPHLINK folder, next to Launcher.exe.", root);
        return 1;
    }
    for (i = 0; i < (int)ARRAYSIZE(game_dirs) && !found; i++)
    {
        if (FAILED(StringCchPrintfW(game_dir, ARRAYSIZE(game_dir), L"%ls\\games\\%ls", root, game_dirs[i])) ||
            FAILED(StringCchPrintfW(game, ARRAYSIZE(game), L"%ls\\Endfield.exe", game_dir)))
            continue;
        found = file_exists(game);
    }
    if (!found)
    {
        fail(0, L"Game not found:\n%ls\\games\\Arknights Endfield\\Endfield.exe\n\n"
                L"Install the game with GRYPHLINK first.", root);
        return 1;
    }

    if (!append_arg(lcmd, ARRAYSIZE(lcmd), launcher))
    {
        fail(ERROR_FILENAME_EXCED_RANGE, L"Launcher path is too long:\n%ls", launcher);
        return 1;
    }
    append_arg(cmd, ARRAYSIZE(cmd), game);
    for (i = 1; i < argc; i++)
    {
        if (!append_arg(cmd, ARRAYSIZE(cmd), argv[i]))
        {
            fail(0, L"Game command line is too long.");
            return 1;
        }
    }

    if (!(job = create_kill_job()))
    {
        fail(GetLastError(), L"Couldn't create the launcher job object.");
        return 1;
    }

    if (!CreateProcessW(launcher, lcmd, NULL, NULL, FALSE, CREATE_SUSPENDED, NULL, NULL, &lsi, &lpi))
    {
        fail(GetLastError(), L"Couldn't start the launcher suspended:\n%ls", launcher);
        goto done;
    }
    if (!AssignProcessToJobObject(job, lpi.hProcess))
    {
        fail(GetLastError(), L"Couldn't assign the launcher to its job object.");
        goto done;
    }

    if (!InitializeProcThreadAttributeList(NULL, 1, 0, &size) && GetLastError() != ERROR_INSUFFICIENT_BUFFER)
    {
        fail(GetLastError(), L"Couldn't size the process attribute list.");
        goto done;
    }
    if (!(attrs = HeapAlloc(GetProcessHeap(), 0, size)))
    {
        fail(ERROR_NOT_ENOUGH_MEMORY, L"Couldn't allocate the process attribute list.");
        goto done;
    }
    if (!InitializeProcThreadAttributeList(attrs, 1, 0, &size))
    {
        fail(GetLastError(), L"Couldn't initialize the process attribute list.");
        HeapFree(GetProcessHeap(), 0, attrs);
        attrs = NULL;
        goto done;
    }
    if (!UpdateProcThreadAttribute(attrs, 0, PROC_THREAD_ATTRIBUTE_PARENT_PROCESS,
                                   &lpi.hProcess, sizeof(lpi.hProcess), NULL, NULL))
    {
        fail(GetLastError(), L"Couldn't set the launcher as the parent process.");
        goto done;
    }
    si.lpAttributeList = attrs;

    if (!CreateProcessW(game, cmd, NULL, NULL, FALSE, EXTENDED_STARTUPINFO_PRESENT, NULL,
                        game_dir, &si.StartupInfo, &pi))
    {
        fail(GetLastError(), L"Couldn't start the game:\n%ls", game);
        goto done;
    }

    WaitForSingleObject(pi.hProcess, INFINITE);
    if (!GetExitCodeProcess(pi.hProcess, &exit_code)) exit_code = 1;

done:
    if (attrs)
    {
        DeleteProcThreadAttributeList(attrs);
        HeapFree(GetProcessHeap(), 0, attrs);
    }
    if (pi.hThread) CloseHandle(pi.hThread);
    if (pi.hProcess) CloseHandle(pi.hProcess);
    /* Also covers AssignProcessToJobObject failure. */
    if (lpi.hProcess) TerminateProcess(lpi.hProcess, 0);
    if (lpi.hThread) CloseHandle(lpi.hThread);
    if (lpi.hProcess) CloseHandle(lpi.hProcess);
    CloseHandle(job);
    return exit_code;
}
