/* Windows helper run inside the Gryphfield prefix: watches the launcher and game processes and
 * prints one JSON event per line on stdout for gryphfield-engine. It only reads process and
 * window state; it never touches the game's files or memory.
 *
 *   gryphfield-watch.exe once                 print {"event":"processes",...} and exit
 *   gryphfield-watch.exe gryphlink [--start-timeout S]
 *   gryphfield-watch.exe direct    [--start-timeout S]
 *
 * gryphlink: ends when no launcher or game process is left (after one was seen).
 * direct:    ends when Endfield.exe exits (after it was seen).
 * Either mode emits {"event":"failed","reason":...} if nothing appears within the start timeout.
 *
 * Build: x86_64-w64-mingw32-gcc -O2 -Wall -Wextra -municode gryphfield-watch.c -o gryphfield-watch.exe */
#include <windows.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>

#define POLL_MS 500
#define STABLE_MS 30000

struct procs { DWORD launcher, games, game, unity; };

static void emit(const char *json)
{
    fputs(json, stdout);
    fputc('\n', stdout);
    fflush(stdout);
}

static void scan(struct procs *p)
{
    PROCESSENTRY32W entry;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);

    memset(&entry, 0, sizeof(entry));
    entry.dwSize = sizeof(entry);

    memset(p, 0, sizeof(*p));
    if (snap == INVALID_HANDLE_VALUE) return;
    if (Process32FirstW(snap, &entry))
    {
        do
        {
            if (!_wcsicmp(entry.szExeFile, L"Launcher.exe")) p->launcher = entry.th32ProcessID;
            else if (!_wcsicmp(entry.szExeFile, L"Games.exe")) p->games = entry.th32ProcessID;
            else if (!_wcsicmp(entry.szExeFile, L"Endfield.exe")) p->game = entry.th32ProcessID;
            else if (!_wcsicmp(entry.szExeFile, L"UnityCrashHandler64.exe")) p->unity = entry.th32ProcessID;
        } while (Process32NextW(snap, &entry));
    }
    CloseHandle(snap);
}

struct windows { DWORD pid; int normal, minimized; };

static BOOL CALLBACK window_proc(HWND hwnd, LPARAM lp)
{
    struct windows *w = (struct windows *)lp;
    DWORD pid = 0;
    RECT r;

    GetWindowThreadProcessId(hwnd, &pid);
    if (pid != w->pid || !IsWindowVisible(hwnd)) return TRUE;
    if (IsIconic(hwnd)) { w->minimized++; return TRUE; }
    /* ignore tiny helper windows; the launcher's main window is about 1300x750 */
    if (GetWindowRect(hwnd, &r) && (r.right - r.left) >= 400 && (r.bottom - r.top) >= 300) w->normal++;
    return TRUE;
}

/* 1 = a full-size window is shown, 0 = only minimized or hidden windows remain */
static int launcher_shown(DWORD pid)
{
    struct windows w = { pid, 0, 0 };
    EnumWindows(window_proc, (LPARAM)&w);
    return w.normal > 0;
}

static void print_once(void)
{
    struct procs p;
    char line[256];

    scan(&p);
    snprintf(line, sizeof(line),
             "{\"event\":\"processes\",\"gryphlink\":%s,\"endfield\":%s,\"unity\":%s}",
             (p.launcher || p.games) ? "true" : "false", p.game ? "true" : "false",
             p.unity ? "true" : "false");
    emit(line);
}

int wmain(int argc, WCHAR **argv)
{
    int direct, i;
    DWORD start_timeout = 120, started = GetTickCount(), unity_since = 0;
    BOOL seen_launcher = FALSE, launcher_was_shown = FALSE, minimized_sent = FALSE;
    BOOL seen_game = FALSE, unity_sent = FALSE, stable_sent = FALSE;
    HANDLE game = NULL;
    DWORD game_pid = 0;
    struct procs p;
    char line[256];

    if (argc < 2) { fputs("usage: gryphfield-watch.exe once|gryphlink|direct [--start-timeout S]\n", stderr); return 2; }
    if (!_wcsicmp(argv[1], L"once")) { print_once(); return 0; }
    direct = !_wcsicmp(argv[1], L"direct");
    if (!direct && _wcsicmp(argv[1], L"gryphlink")) { fputs("unknown mode\n", stderr); return 2; }
    for (i = 2; i + 1 < argc; i++)
        if (!wcscmp(argv[i], L"--start-timeout")) start_timeout = wcstoul(argv[++i], NULL, 10);

    for (;;)
    {
        scan(&p);
        DWORD now = GetTickCount();

        if (!direct && (p.launcher || p.games))
        {
            if (!seen_launcher) { seen_launcher = TRUE; emit("{\"event\":\"launcher_started\"}"); }
            if (p.games)
            {
                if (launcher_shown(p.games)) launcher_was_shown = TRUE, minimized_sent = FALSE;
                else if (launcher_was_shown && !minimized_sent)
                {
                    minimized_sent = TRUE;
                    emit("{\"event\":\"launcher_minimized\"}");
                }
            }
        }

        if (p.game && p.game != game_pid)
        {
            if (game) CloseHandle(game);
            game_pid = p.game;
            /* keep a handle so the exit code can be read after the game exits */
            game = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, game_pid);
            seen_game = TRUE;
            unity_sent = stable_sent = FALSE;
            unity_since = 0;
            snprintf(line, sizeof(line), "{\"event\":\"game_started\",\"pid\":%lu}", (unsigned long)game_pid);
            emit(line);
        }

        if (game_pid)
        {
            if (p.unity)
            {
                if (!unity_sent) { unity_sent = TRUE; unity_since = now; emit("{\"event\":\"unity_started\"}"); }
                else if (!stable_sent && now - unity_since >= STABLE_MS)
                {
                    stable_sent = TRUE;
                    emit("{\"event\":\"unity_stable\"}");
                }
            }
            else if (unity_sent && !stable_sent) unity_since = now;   /* restart the 30 s window */

            if (!p.game || p.game != game_pid)
            {
                DWORD code = 0;
                if (game && WaitForSingleObject(game, 2000) == WAIT_OBJECT_0 && GetExitCodeProcess(game, &code))
                    snprintf(line, sizeof(line), "{\"event\":\"game_exited\",\"exit_code\":%lu}", (unsigned long)code);
                else
                    snprintf(line, sizeof(line), "{\"event\":\"game_exited\",\"exit_code\":null}");
                emit(line);
                if (game) CloseHandle(game);
                game = NULL;
                game_pid = 0;
                if (direct) return 0;
                continue;
            }
        }

        if (direct ? !seen_game : !seen_launcher)
        {
            if (now - started >= start_timeout * 1000)
            {
                emit(direct ? "{\"event\":\"failed\",\"reason\":\"Endfield.exe did not start\"}"
                            : "{\"event\":\"failed\",\"reason\":\"GRYPHLINK did not start\"}");
                return 1;
            }
        }
        else if (!direct && !p.launcher && !p.games && !p.game)
            return 0;   /* launcher session over */

        Sleep(POLL_MS);
    }
}
