/* Windows helper run inside the Wine prefix: list top-level windows, or click a point given
 * relative to the largest visible window of a process, the way a user would. It never touches
 * the target's files or memory; input goes through Wine's normal input path.
 *
 *   launcher_click.exe list
 *   launcher_click.exe click <process-exe-name> <rel-x> <rel-y> [--post|--cursor]
 *   launcher_click.exe fit <process-exe-name>   (move, not resize, so the window ends on screen)
 * Fractions include the window's shadow margins. --post avoids focus/cursor changes;
 * --cursor uses SetCursorPos instead of an absolute SendInput move.
 *
 * Build: x86_64-w64-mingw32-gcc -O2 -Wall -municode launcher_click.c -o launcher_click.exe */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>

struct find { const WCHAR *exe; HWND best; LONG area; };

static void process_name(HWND hwnd, WCHAR *name, DWORD len)
{
    DWORD pid = 0, size = MAX_PATH;
    WCHAR path[MAX_PATH] = L"?";
    HANDLE process;

    GetWindowThreadProcessId(hwnd, &pid);
    if ((process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid)))
    {
        if (!QueryFullProcessImageNameW(process, 0, path, &size)) wcscpy(path, L"?");
        CloseHandle(process);
    }
    const WCHAR *base = wcsrchr(path, L'\\');
    wcsncpy(name, base ? base + 1 : path, len - 1);
    name[len - 1] = 0;
}

static BOOL CALLBACK list_proc(HWND hwnd, LPARAM lp)
{
    WCHAR title[256], cls[128], exe[MAX_PATH];
    RECT r;
    DWORD pid = 0;
    (void)lp;
    if (!IsWindowVisible(hwnd)) return TRUE;
    GetWindowTextW(hwnd, title, 256);
    GetClassNameW(hwnd, cls, 128);
    GetWindowRect(hwnd, &r);
    GetWindowThreadProcessId(hwnd, &pid);
    process_name(hwnd, exe, MAX_PATH);
    wprintf(L"%p pid=%lu exe=%ls rect=%ld,%ld %ldx%ld class=%ls title=%ls\n", hwnd, pid, exe,
            r.left, r.top, r.right - r.left, r.bottom - r.top, cls, title);
    return TRUE;
}

static BOOL CALLBACK find_proc(HWND hwnd, LPARAM lp)
{
    struct find *f = (struct find *)lp;
    WCHAR exe[MAX_PATH];
    RECT r;
    if (!IsWindowVisible(hwnd)) return TRUE;
    process_name(hwnd, exe, MAX_PATH);
    if (_wcsicmp(exe, f->exe)) return TRUE;
    if (!GetWindowRect(hwnd, &r)) return TRUE;
    if (IsIconic(hwnd)) {
        WINDOWPLACEMENT placement = {0};
        placement.length = sizeof(placement);
        if (!GetWindowPlacement(hwnd, &placement)) return TRUE;
        r = placement.rcNormalPosition;
    }
    LONG area = (r.right - r.left) * (r.bottom - r.top);
    if (area > f->area) { f->area = area; f->best = hwnd; }
    return TRUE;
}

/* Resolve dynamically so older Wine builds can fall back to system DPI awareness. */
static void dpi_aware(void)
{
    typedef BOOL (WINAPI *DpiContextFn)(HANDLE);
    union { FARPROC raw; DpiContextFn fn; } api;
    api.raw = GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetProcessDpiAwarenessContext");
    if (api.raw && api.fn((HANDLE)(LONG_PTR)-4)) return;
    SetProcessDPIAware();
}

static int fraction(const WCHAR *s, double *v)
{
    WCHAR *end;
    *v = wcstod(s, &end);
    return end != s && !*end && *v >= 0 && *v < 1;
}

static int post_mouse(HWND target, UINT message, WPARAM buttons, LPARAM pos)
{
    SetLastError(0);
    if (PostMessageW(target, message, buttons, pos)) return 0;
    fwprintf(stderr, L"PostMessage 0x%x failed: %lu\n", message, GetLastError());
    return 1;
}

static int post_click(HWND hwnd, POINT pt)
{
    HWND target = hwnd;
    POINT client;
    /* Descend through native child windows; Qt WebEngine may render in the parent. */
    for (int depth = 0; depth < 32; ++depth) {
        client = pt;
        if (!ScreenToClient(target, &client)) {
            fwprintf(stderr, L"ScreenToClient failed: %lu\n", GetLastError());
            return 1;
        }
        HWND child = ChildWindowFromPointEx(target, client,
                                            CWP_SKIPINVISIBLE | CWP_SKIPDISABLED | CWP_SKIPTRANSPARENT);
        if (!child || child == target) break;
        target = child;
    }
    client = pt;
    if (!ScreenToClient(target, &client) || client.x < -32768 || client.x > 32767 ||
        client.y < -32768 || client.y > 32767) return 1;
    LPARAM pos = MAKELPARAM(client.x, client.y);
    wprintf(L"posting hwnd=%p client=%ld,%ld\n", target, client.x, client.y);
    if (post_mouse(target, WM_MOUSEMOVE, 0, pos)) return 1;
    Sleep(50);
    if (post_mouse(target, WM_LBUTTONDOWN, MK_LBUTTON, pos)) return 1;
    Sleep(90);
    if (post_mouse(target, WM_LBUTTONUP, 0, pos)) return 1;
    wprintf(L"posted click client=%ld,%ld hwnd=%p; delivery is not proof of UI response\n",
            client.x, client.y, target);
    return 0;
}

static int send_click(HWND hwnd, POINT pt, int sx, int sy, int sw, int sh, int cursor)
{
    SetForegroundWindow(hwnd);
    Sleep(300);
    if (GetForegroundWindow() != hwnd) {
        fwprintf(stderr, L"refusing click: target did not become foreground\n");
        return 1;
    }
    INPUT in = {0};
    in.type = INPUT_MOUSE;
    /* Pixel centres in physical virtual-desktop coordinates. */
    in.mi.dx = (LONG)(((pt.x - sx + 0.5) * 65536.0) / sw);
    in.mi.dy = (LONG)(((pt.y - sy + 0.5) * 65536.0) / sh);
    in.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK;
    SetLastError(0);
    if (cursor ? !SetCursorPos(pt.x, pt.y) : SendInput(1, &in, sizeof(in)) != 1) {
        fwprintf(stderr, L"mouse move failed: %lu (zero may mean no error detail)\n", GetLastError());
        return 1;
    }
    Sleep(50);
    POINT actual;
    if (!GetCursorPos(&actual)) return 1;
    wprintf(L"cursor requested=%ld,%ld actual=%ld,%ld\n", pt.x, pt.y, actual.x, actual.y);
    if (labs(actual.x - pt.x) > 1 || labs(actual.y - pt.y) > 1 ||
        GetAncestor(WindowFromPoint(actual), GA_ROOT) != hwnd || GetForegroundWindow() != hwnd) {
        fwprintf(stderr, L"refusing click: cursor mismatch or target is obscured\n");
        return 1;
    }
    ZeroMemory(&in.mi, sizeof(in.mi));
    in.mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
    if (SendInput(1, &in, sizeof(in)) != 1) return 1;
    Sleep(90);
    in.mi.dwFlags = MOUSEEVENTF_LEFTUP;
    if (SendInput(1, &in, sizeof(in)) != 1) return 1;
    return 0;
}

int wmain(int argc, WCHAR **argv)
{
    dpi_aware();
    if (argc == 2 && !wcscmp(argv[1], L"list")) {
        EnumWindows(list_proc, 0);
        return 0;
    }
    if (argc == 3 && !wcscmp(argv[1], L"fit")) {
        /* Move (never resize) the window so its right/bottom edges are on screen, like a user
         * dragging a window that is larger than the display (e.g. 1024x768 CI desktops). */
        struct find f = { argv[2], NULL, 0 };
        EnumWindows(find_proc, (LPARAM)&f);
        if (!f.best) {
            fwprintf(stderr, L"no visible window for %ls\n", argv[2]);
            return 1;
        }
        RECT r;
        if (!GetWindowRect(f.best, &r)) return 1;
        int sx = GetSystemMetrics(SM_XVIRTUALSCREEN), sy = GetSystemMetrics(SM_YVIRTUALSCREEN);
        int sw = GetSystemMetrics(SM_CXVIRTUALSCREEN), sh = GetSystemMetrics(SM_CYVIRTUALSCREEN);
        LONG w = r.right - r.left, h = r.bottom - r.top, x = r.left, y = r.top;
        if (x + w > sx + sw) x = sx + sw - w;
        if (y + h > sy + sh) y = sy + sh - h;
        if (w <= sw && x < sx) x = sx;
        if (h <= sh && y < sy) y = sy;
        if (!SetWindowPos(f.best, NULL, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE)) {
            fwprintf(stderr, L"SetWindowPos failed: %lu\n", GetLastError());
            return 1;
        }
        Sleep(300);
        GetWindowRect(f.best, &r);
        wprintf(L"fit window=%ld,%ld %ldx%ld virtual=%d,%d %dx%d\n", r.left, r.top,
                r.right - r.left, r.bottom - r.top, sx, sy, sw, sh);
        return 0;
    }
    int post = argc == 6 && !wcscmp(argv[5], L"--post");
    int cursor = argc == 6 && !wcscmp(argv[5], L"--cursor");
    if ((argc != 5 && !post && !cursor) || wcscmp(argv[1], L"click")) {
        fwprintf(stderr, L"usage: list | fit <exe> | click <exe> <rel-x> <rel-y> [--post|--cursor]\n");
        return 2;
    }
    double rx, ry;
    if (!fraction(argv[3], &rx) || !fraction(argv[4], &ry)) {
        fwprintf(stderr, L"coordinates must be finite fractions in [0,1)\n");
        return 2;
    }
    struct find f = { argv[2], NULL, 0 };
    EnumWindows(find_proc, (LPARAM)&f);
    if (!f.best) {
        fwprintf(stderr, L"no visible window for %ls\n", argv[2]);
        return 1;
    }
    if (IsIconic(f.best)) {
        ShowWindowAsync(f.best, SW_RESTORE);
        if (!PostMessageW(f.best, WM_SYSCOMMAND, SC_RESTORE, 0)) {
            fwprintf(stderr, L"restore request failed: %lu\n", GetLastError());
            return 1;
        }
        for (int attempt = 0; attempt < 40 && IsIconic(f.best); ++attempt) Sleep(50);
        if (IsIconic(f.best)) {
            fwprintf(stderr, L"refusing click: window remained minimized\n");
            return 1;
        }
        Sleep(200); /* Allow restored layout to settle before reading its rectangle. */
    }
    RECT r;
    if (!GetWindowRect(f.best, &r)) return 1;
    POINT pt = { r.left + (LONG)(rx * (r.right - r.left)),
                 r.top + (LONG)(ry * (r.bottom - r.top)) };
    int sx = GetSystemMetrics(SM_XVIRTUALSCREEN), sy = GetSystemMetrics(SM_YVIRTUALSCREEN);
    int sw = GetSystemMetrics(SM_CXVIRTUALSCREEN), sh = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    wprintf(L"target=%ld,%ld window=%ld,%ld %ldx%ld virtual=%d,%d %dx%d\n",
            pt.x, pt.y, r.left, r.top, r.right-r.left, r.bottom-r.top, sx, sy, sw, sh);
    if (post || pt.x < sx || pt.y < sy || pt.x >= sx+sw || pt.y >= sy+sh)
        return post_click(f.best, pt);
    return send_click(f.best, pt, sx, sy, sw, sh, cursor);
}


