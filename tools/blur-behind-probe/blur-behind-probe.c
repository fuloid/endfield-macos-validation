/* Checks patch 0023: DwmEnableBlurBehindWindow succeeds and marks the window with the property
 * __wine_dwm_content_alpha, which winemac reads to composite the window with its content's alpha,
 * and disabling it removes the mark. The window is set up like GRYPHLINK's (layered, LWA_ALPHA 255,
 * empty blur region). Prints "blur-behind enable=<hr> prop=<p> disable=<hr> prop=<p>".
 */
#include <windows.h>
#include <dwmapi.h>
#include <stdio.h>

static void pump(DWORD ms)
{
    DWORD end = GetTickCount() + ms;
    MSG msg;

    while ((LONG)(end - GetTickCount()) > 0)
    {
        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) DispatchMessageW(&msg);
        Sleep(20);
    }
}

int main(void)
{
    DWM_BLURBEHIND bb = {0};
    HANDLE prop_on, prop_off;
    HRESULT on, off;
    HWND hwnd;

    hwnd = CreateWindowExW(WS_EX_LAYERED, L"STATIC", L"blur-behind probe", WS_POPUP | WS_VISIBLE,
                           100, 100, 240, 160, NULL, NULL, NULL, NULL);
    if (!hwnd)
    {
        printf("CreateWindowEx failed: %lu\n", GetLastError());
        return 1;
    }
    SetLayeredWindowAttributes(hwnd, 0, 255, LWA_ALPHA);
    pump(300);

    bb.dwFlags = DWM_BB_ENABLE | DWM_BB_BLURREGION;
    bb.fEnable = TRUE;
    bb.hRgnBlur = CreateRectRgn(0, 0, -1, -1);
    on = DwmEnableBlurBehindWindow(hwnd, &bb);
    prop_on = GetPropW(hwnd, L"__wine_dwm_content_alpha");
    pump(500);

    bb.dwFlags = DWM_BB_ENABLE;
    bb.fEnable = FALSE;
    off = DwmEnableBlurBehindWindow(hwnd, &bb);
    prop_off = GetPropW(hwnd, L"__wine_dwm_content_alpha");
    pump(300);

    printf("blur-behind enable=%08lx prop=%p disable=%08lx prop=%p\n", (unsigned long)on, prop_on,
           (unsigned long)off, prop_off);
    DeleteObject(bb.hRgnBlur);
    DestroyWindow(hwnd);
    return on == S_OK && prop_on && off == S_OK && !prop_off ? 0 : 1;
}
