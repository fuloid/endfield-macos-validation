/* Does starting a child process still work once dxgi.dll is loaded? Under D3DMetal, GRYPHLINK's
 * Launcher.exe (whose crash reporter loads dxgi) faulted inside NtCreateUserProcess on a real Mac.
 * Each step prints one key=value line, flushed, so a crash shows how far it got. Exit 0 when all
 * three process starts work, 1 when one fails. */
#define COBJMACROS
#include <windows.h>
#include <dxgi.h>
#include <stdio.h>

static int spawn(const char *label)
{
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    WCHAR cmd[] = L"cmd.exe /c exit 7";
    DWORD code = 0, wait;

    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);

    printf("%s=starting\n", label);
    fflush(stdout);
    if (!CreateProcessW(NULL, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi))
    {
        printf("%s=failed error=%lu\n", label, GetLastError());
        fflush(stdout);
        return 0;
    }
    wait = WaitForSingleObject(pi.hProcess, 20000);
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    printf("%s=%s exit=%lu\n", label, wait == WAIT_OBJECT_0 && code == 7 ? "ok" : "bad", code);
    fflush(stdout);
    return wait == WAIT_OBJECT_0 && code == 7;
}

int main(void)
{
    HRESULT (WINAPI *create_factory)(REFIID, void **);
    IDXGIFactory1 *factory = NULL;
    IDXGIAdapter1 *adapter = NULL;
    DXGI_ADAPTER_DESC1 desc;
    HMODULE dxgi;
    HRESULT hr;
    int ok = 1;

    setvbuf(stdout, NULL, _IONBF, 0);
    ok &= spawn("spawn_before_dxgi");

    dxgi = LoadLibraryA("dxgi.dll");
    printf("dxgi_loaded=%s\n", dxgi ? "yes" : "no");
    if (!dxgi) return 1;
    ok &= spawn("spawn_after_load");

    create_factory = (void *)GetProcAddress(dxgi, "CreateDXGIFactory1");
    hr = create_factory ? create_factory(&IID_IDXGIFactory1, (void **)&factory) : E_NOINTERFACE;
    printf("factory_hr=0x%08lx\n", (unsigned long)hr);
    if (SUCCEEDED(hr) && SUCCEEDED(IDXGIFactory1_EnumAdapters1(factory, 0, &adapter))
        && SUCCEEDED(IDXGIAdapter1_GetDesc1(adapter, &desc)))
        printf("adapter=%ls vendor=0x%04x\n", desc.Description, desc.VendorId);
    else
        printf("adapter=none\n");
    ok &= spawn("spawn_after_factory");

    if (adapter) IDXGIAdapter1_Release(adapter);
    if (factory) IDXGIFactory1_Release(factory);
    printf("result=%s\n", ok ? "ok" : "failed");
    return ok ? 0 : 1;
}
