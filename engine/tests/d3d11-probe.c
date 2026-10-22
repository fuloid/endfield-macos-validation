// Harmless D3D11 fixture: create a real device/swapchain and present frames.
#define COBJMACROS
#include <windows.h>
#include <d3d11.h>
#include <stdio.h>

int main(void)
{
    WNDCLASSW wc = {0};
    HWND window;
    ID3D11Device *device = NULL;
    ID3D11DeviceContext *context = NULL;
    IDXGISwapChain *swap = NULL;
    ID3D11Texture2D *back = NULL;
    ID3D11RenderTargetView *target = NULL;
    DXGI_SWAP_CHAIN_DESC desc = {0};
    D3D_FEATURE_LEVEL level;
    HRESULT hr;
    unsigned int frames = 0;
    setvbuf(stdout, NULL, _IONBF, 0);
    puts("stage=window");
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.lpszClassName = L"GryphfieldD3D11Probe";
    if (!RegisterClassW(&wc)) return 1;
    window = CreateWindowW(wc.lpszClassName, L"Gryphfield D3D11 verification", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                           100, 100, 400, 300, NULL, NULL, wc.hInstance, NULL);
    if (!window) return 1;
    desc.BufferDesc.Width = 400;
    desc.BufferDesc.Height = 300;
    desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2;
    desc.OutputWindow = window;
    desc.Windowed = TRUE;
    desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    puts("stage=device-and-swapchain");
    hr = D3D11CreateDeviceAndSwapChain(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, NULL, 0, D3D11_SDK_VERSION,
                                     &desc, &swap, &device, &level, &context);
    printf("device_hresult=0x%08lx\n", (unsigned long)hr);
    fflush(stdout);
    if (FAILED(hr)) return 2; // clean device refusal, distinct from crash/timeout
    printf("feature_level=0x%x\n", level);
    puts("stage=get-buffer");
    hr = IDXGISwapChain_GetBuffer(swap, 0, &IID_ID3D11Texture2D, (void **)&back);
    if (SUCCEEDED(hr)) {
        puts("stage=render-target");
        hr = ID3D11Device_CreateRenderTargetView(device, (ID3D11Resource *)back, NULL, &target);
    }
    if (FAILED(hr)) return 3;
    puts("stage=present");
    for (frames = 0; frames < 240; frames++) {
        MSG msg;
        float color[] = {frames / 240.0f, 0.15f, 0.35f, 1.0f};
        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
        ID3D11DeviceContext_ClearRenderTargetView(context, target, color);
        hr = IDXGISwapChain_Present(swap, 1, 0);
        if (FAILED(hr)) { printf("present_hresult=0x%08lx\n", (unsigned long)hr); return 4; }
        Sleep(25);
        if (frames % 60 == 0) printf("frame=%u\n", frames);
    }
    printf("presented_frames=%u\n", frames);
    puts("stage=release");
    ID3D11RenderTargetView_Release(target);
    ID3D11Texture2D_Release(back);
    IDXGISwapChain_Release(swap);
    ID3D11DeviceContext_Release(context);
    ID3D11Device_Release(device);
    DestroyWindow(window);
    puts("stage=complete");
    return 0;
}
