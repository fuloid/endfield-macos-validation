/* Exercise child processes, DXGI factory creation and adapter enumeration separately.
 * Each step prints key=value lines, flushed, so a crash shows how far it got.
 * Exit 0 only when all three process starts, the factory and the adapter succeed. */
#define COBJMACROS
#include <windows.h>
#include <dxgi.h>
#include <stdio.h>

static ULONGLONG t0;

static void stamp(const char *label)   /* elapsed milliseconds since main() began */
{
    printf("%s_ms=%llu\n", label, (unsigned long long)(GetTickCount64() - t0));
    fflush(stdout);
}

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
    stamp(label);
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
    t0 = GetTickCount64();
    ok &= spawn("spawn_before_dxgi");

    dxgi = LoadLibraryA("dxgi.dll");
    printf("dxgi_loaded=%s\n", dxgi ? "yes" : "no");
    stamp("dxgi_loaded");
    if (!dxgi)
    {
        printf("factory=failed\nadapter=skipped\nresult=failed\n");
        return 1;
    }
    ok &= spawn("spawn_after_load");

    create_factory = (void *)GetProcAddress(dxgi, "CreateDXGIFactory1");
    printf("factory=starting\n");
    hr = create_factory ? create_factory(&IID_IDXGIFactory1, (void **)&factory) : E_NOINTERFACE;
    printf("factory_hr=0x%08lx\n", (unsigned long)hr);
    stamp("factory");
    printf("factory=%s\n", SUCCEEDED(hr) && factory ? "ok" : "failed");
    if (SUCCEEDED(hr) && factory)
    {
        printf("adapter=starting\n");
        hr = IDXGIFactory1_EnumAdapters1(factory, 0, &adapter);
        printf("adapter_hr=0x%08lx\n", (unsigned long)hr);
        if (SUCCEEDED(hr) && adapter)
        {
            hr = IDXGIAdapter1_GetDesc1(adapter, &desc);
            printf("adapter_desc_hr=0x%08lx\n", (unsigned long)hr);
            if (SUCCEEDED(hr))
                printf("adapter_name=%ls vendor=0x%04x\n", desc.Description, desc.VendorId);
        }
        printf("adapter=%s\n", SUCCEEDED(hr) && adapter ? "ok" : "failed");
        ok &= SUCCEEDED(hr) && adapter != NULL;
    }
    else
    {
        printf("adapter=skipped\n");
        ok = 0;
    }
    ok &= spawn("spawn_after_factory");

    if (adapter) IDXGIAdapter1_Release(adapter);
    if (factory) IDXGIFactory1_Release(factory);
    printf("result=%s\n", ok ? "ok" : "failed");
    return ok ? 0 : 1;
}
