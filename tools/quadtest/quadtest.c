/*
 * quadtest: draw a full-window quad with Direct3D 11 in the ways ANGLE and Chromium do, read the
 * backbuffer back, and report whether both triangles were drawn. Used to find why GRYPHLINK is
 * half drawn (its bottom-left triangle stays black) on wined3d's Vulkan renderer over MoltenVK.
 *
 * Every case clears the backbuffer to black and draws red over the whole window, then checks a
 * pixel in the bottom-left triangle (BL), the top-right triangle (TR) and the centre.
 *   quadtest.exe [frames]      prints one PASS/FAIL line per case; exit code = failures
 */
#define COBJMACROS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <stdio.h>
#include <string.h>

#define W 256
#define H 256

typedef HRESULT (WINAPI *compile_fn)(const void *, SIZE_T, const char *, const D3D_SHADER_MACRO *,
                                     ID3DInclude *, const char *, const char *, UINT, UINT,
                                     ID3DBlob **, ID3DBlob **);

static const char shader_src[] =
    "struct VSIn { float2 pos : POSITION; float2 tex : TEXCOORD0; };\n"
    "struct VSOut { float4 pos : SV_POSITION; float2 tex : TEXCOORD0; };\n"
    "VSOut vs(VSIn i) { VSOut o; o.pos = float4(i.pos, 0.0, 1.0); o.tex = i.tex; return o; }\n"
    "float4 ps(VSOut i) : SV_TARGET { return float4(1.0, 0.0, 0.0, 1.0); }\n";

struct vertex { float x, y, u, v; };

/* ANGLE's SwapChain11::copyOffscreenToBackbuffer order: BL, TL, BR, TR as a strip */
static const struct vertex strip[4] = {
    {-1.0f, -1.0f, 0.0f, 1.0f}, {-1.0f, 1.0f, 0.0f, 0.0f},
    { 1.0f, -1.0f, 1.0f, 1.0f}, { 1.0f, 1.0f, 1.0f, 0.0f},
};
/* the same quad as a triangle list: (BL, TL, BR) (BR, TL, TR) */
static const struct vertex list[6] = {
    {-1.0f, -1.0f, 0.0f, 1.0f}, {-1.0f, 1.0f, 0.0f, 0.0f}, { 1.0f, -1.0f, 1.0f, 1.0f},
    { 1.0f, -1.0f, 1.0f, 1.0f}, {-1.0f, 1.0f, 0.0f, 0.0f}, { 1.0f, 1.0f, 1.0f, 0.0f},
};
static const struct vertex junk = {0.0f, 0.0f, 0.0f, 0.0f};

static ID3D11Device *dev;
static ID3D11DeviceContext *ctx;
static IDXGISwapChain *swapchain;
static ID3D11RenderTargetView *rtv;
static ID3D11Texture2D *backbuffer, *staging;
static int failures;

static ID3D11Buffer *make_buffer(UINT bind, D3D11_USAGE usage, UINT size, const void *data)
{
    D3D11_BUFFER_DESC desc = {0};
    D3D11_SUBRESOURCE_DATA init = {0};
    ID3D11Buffer *buffer = NULL;

    desc.ByteWidth = size;
    desc.Usage = usage;
    desc.BindFlags = bind;
    desc.CPUAccessFlags = usage == D3D11_USAGE_DYNAMIC ? D3D11_CPU_ACCESS_WRITE : 0;
    init.pSysMem = data;
    if (FAILED(ID3D11Device_CreateBuffer(dev, &desc, data ? &init : NULL, &buffer)))
    {
        printf("CreateBuffer failed\n");
        ExitProcess(100);
    }
    return buffer;
}

static void map_write(ID3D11Buffer *buffer, D3D11_MAP type, UINT offset, const void *data, UINT size)
{
    D3D11_MAPPED_SUBRESOURCE map;

    if (FAILED(ID3D11DeviceContext_Map(ctx, (ID3D11Resource *)buffer, 0, type, 0, &map)))
    {
        printf("Map failed\n");
        ExitProcess(101);
    }
    memcpy((BYTE *)map.pData + offset, data, size);
    ID3D11DeviceContext_Unmap(ctx, (ID3D11Resource *)buffer, 0);
}

static DWORD pixel(const D3D11_MAPPED_SUBRESOURCE *map, int x, int y)
{
    return *(const DWORD *)((const BYTE *)map->pData + y * map->RowPitch + x * 4) & 0xffffff;
}

static void begin(void)
{
    static const float black[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    D3D11_VIEWPORT vp = {0.0f, 0.0f, W, H, 0.0f, 1.0f};

    ID3D11DeviceContext_OMSetRenderTargets(ctx, 1, &rtv, NULL);
    ID3D11DeviceContext_RSSetViewports(ctx, 1, &vp);
    ID3D11DeviceContext_ClearRenderTargetView(ctx, rtv, black);
}

static void check(const char *name)
{
    D3D11_MAPPED_SUBRESOURCE map;
    DWORD bl, tr, centre;
    int ok;

    ID3D11DeviceContext_CopyResource(ctx, (ID3D11Resource *)staging, (ID3D11Resource *)backbuffer);
    if (FAILED(ID3D11DeviceContext_Map(ctx, (ID3D11Resource *)staging, 0, D3D11_MAP_READ, 0, &map)))
    {
        printf("FAIL %-34s could not read the backbuffer\n", name);
        failures++;
        return;
    }
    /* B8G8R8A8: red is 0xff0000 */
    bl = pixel(&map, W / 8, H - H / 8);
    tr = pixel(&map, W - W / 8, H / 8);
    centre = pixel(&map, W / 2 + 3, H / 2 + 7);
    ID3D11DeviceContext_Unmap(ctx, (ID3D11Resource *)staging, 0);
    ok = bl == 0xff0000 && tr == 0xff0000;
    printf("%s %-34s BL %06lx TR %06lx centre %06lx\n", ok ? "PASS" : "FAIL", name, bl, tr, centre);
    if (!ok) failures++;
    IDXGISwapChain_Present(swapchain, 0, 0);
}

static void set_vb(ID3D11Buffer *vb, UINT offset)
{
    UINT stride = sizeof(struct vertex);
    ID3D11DeviceContext_IASetVertexBuffers(ctx, 0, 1, &vb, &stride, &offset);
}

static void run_cases(int frame)
{
    char name[64];
    ID3D11Buffer *vb, *ib;
    struct vertex big[16];
    static const WORD idx16[4] = {0, 1, 2, 3};
    static const WORD idx16_list[6] = {0, 1, 2, 2, 1, 3};
    int i;

#define NAME(s) (snprintf(name, sizeof(name), "%s #%d", s, frame), name)

    /* 1: immutable buffer, strip */
    vb = make_buffer(D3D11_BIND_VERTEX_BUFFER, D3D11_USAGE_IMMUTABLE, sizeof(strip), strip);
    begin(); set_vb(vb, 0);
    ID3D11DeviceContext_IASetPrimitiveTopology(ctx, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    ID3D11DeviceContext_Draw(ctx, 4, 0);
    check(NAME("strip immutable"));
    ID3D11Buffer_Release(vb);

    /* 2: immutable buffer, list */
    vb = make_buffer(D3D11_BIND_VERTEX_BUFFER, D3D11_USAGE_IMMUTABLE, sizeof(list), list);
    begin(); set_vb(vb, 0);
    ID3D11DeviceContext_IASetPrimitiveTopology(ctx, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D11DeviceContext_Draw(ctx, 6, 0);
    check(NAME("list immutable"));
    ID3D11Buffer_Release(vb);

    /* 3: dynamic buffer, Map(DISCARD) every draw, as ANGLE's copy to the backbuffer does */
    vb = make_buffer(D3D11_BIND_VERTEX_BUFFER, D3D11_USAGE_DYNAMIC, sizeof(strip), NULL);
    for (i = 0; i < 3; i++)
    {
        map_write(vb, D3D11_MAP_WRITE_DISCARD, 0, strip, sizeof(strip));
        begin(); set_vb(vb, 0);
        ID3D11DeviceContext_IASetPrimitiveTopology(ctx, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
        ID3D11DeviceContext_Draw(ctx, 4, 0);
        snprintf(name, sizeof(name), "strip dynamic discard %d #%d", i, frame);
        check(name);
    }
    ID3D11Buffer_Release(vb);

    /* 4: streaming buffer: junk, then the quad appended with NO_OVERWRITE, drawn at a start vertex */
    for (i = 0; i < 16; i++) big[i] = junk;
    vb = make_buffer(D3D11_BIND_VERTEX_BUFFER, D3D11_USAGE_DYNAMIC, sizeof(big), NULL);
    map_write(vb, D3D11_MAP_WRITE_DISCARD, 0, big, sizeof(big));
    map_write(vb, D3D11_MAP_WRITE_NO_OVERWRITE, 5 * sizeof(struct vertex), strip, sizeof(strip));
    begin(); set_vb(vb, 0);
    ID3D11DeviceContext_IASetPrimitiveTopology(ctx, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    ID3D11DeviceContext_Draw(ctx, 4, 5);
    check(NAME("strip dynamic start vertex 5"));

    /* 5: the same data reached through the vertex buffer offset instead */
    begin(); set_vb(vb, 5 * sizeof(struct vertex));
    ID3D11DeviceContext_Draw(ctx, 4, 0);
    check(NAME("strip dynamic buffer offset 80"));
    ID3D11Buffer_Release(vb);

    /* 6: indexed strip and indexed list, 16-bit indices */
    vb = make_buffer(D3D11_BIND_VERTEX_BUFFER, D3D11_USAGE_IMMUTABLE, sizeof(strip), strip);
    ib = make_buffer(D3D11_BIND_INDEX_BUFFER, D3D11_USAGE_IMMUTABLE, sizeof(idx16), idx16);
    begin(); set_vb(vb, 0);
    ID3D11DeviceContext_IASetIndexBuffer(ctx, ib, DXGI_FORMAT_R16_UINT, 0);
    ID3D11DeviceContext_IASetPrimitiveTopology(ctx, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    ID3D11DeviceContext_DrawIndexed(ctx, 4, 0, 0);
    check(NAME("strip indexed16"));
    ID3D11Buffer_Release(ib);
    ib = make_buffer(D3D11_BIND_INDEX_BUFFER, D3D11_USAGE_IMMUTABLE, sizeof(idx16_list), idx16_list);
    begin(); set_vb(vb, 0);
    ID3D11DeviceContext_IASetIndexBuffer(ctx, ib, DXGI_FORMAT_R16_UINT, 0);
    ID3D11DeviceContext_IASetPrimitiveTopology(ctx, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D11DeviceContext_DrawIndexed(ctx, 6, 0, 0);
    check(NAME("list indexed16"));
    ID3D11Buffer_Release(ib);
    ID3D11Buffer_Release(vb);

    /* 7: default-usage buffer filled with UpdateSubresource */
    vb = make_buffer(D3D11_BIND_VERTEX_BUFFER, D3D11_USAGE_DEFAULT, sizeof(strip), NULL);
    ID3D11DeviceContext_UpdateSubresource(ctx, (ID3D11Resource *)vb, 0, NULL, strip, 0, 0);
    begin(); set_vb(vb, 0);
    ID3D11DeviceContext_IASetPrimitiveTopology(ctx, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    ID3D11DeviceContext_Draw(ctx, 4, 0);
    check(NAME("strip UpdateSubresource"));
    ID3D11Buffer_Release(vb);
#undef NAME
}

int main(int argc, char **argv)
{
    int frames = argc > 1 ? atoi(argv[1]) : 2, i;
    HMODULE compiler;
    compile_fn compile;
    ID3DBlob *vs_blob = NULL, *ps_blob = NULL, *errors = NULL;
    ID3D11VertexShader *vs;
    ID3D11PixelShader *ps;
    ID3D11InputLayout *layout;
    ID3D11RasterizerState *rs;
    D3D11_RASTERIZER_DESC rs_desc = {0};
    DXGI_SWAP_CHAIN_DESC sc = {0};
    D3D11_TEXTURE2D_DESC tex_desc;
    D3D_FEATURE_LEVEL level;
    WNDCLASSA wc = {0};
    HWND hwnd;
    static const D3D11_INPUT_ELEMENT_DESC elements[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8, D3D11_INPUT_PER_VERTEX_DATA, 0},
    };

    setvbuf(stdout, NULL, _IONBF, 0);
    wc.lpfnWndProc = DefWindowProcA;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.lpszClassName = "quadtest";
    RegisterClassA(&wc);
    hwnd = CreateWindowA("quadtest", "quadtest", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                         100, 100, W + 40, H + 60, NULL, NULL, wc.hInstance, NULL);

    sc.BufferDesc.Width = W;
    sc.BufferDesc.Height = H;
    sc.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    sc.SampleDesc.Count = 1;
    sc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sc.BufferCount = 1;
    sc.OutputWindow = hwnd;
    sc.Windowed = TRUE;
    sc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    if (FAILED(D3D11CreateDeviceAndSwapChain(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, NULL, 0,
                                             D3D11_SDK_VERSION, &sc, &swapchain, &dev, &level, &ctx)))
    {
        printf("D3D11CreateDeviceAndSwapChain failed\n");
        return 100;
    }
    printf("feature level %#x\n", level);

    if (!(compiler = LoadLibraryA("d3dcompiler_47.dll")) ||
        !(compile = (compile_fn)(void *)GetProcAddress(compiler, "D3DCompile")))
    {
        printf("d3dcompiler_47.dll unavailable\n");
        return 100;
    }
    if (FAILED(compile(shader_src, sizeof(shader_src) - 1, "quad", NULL, NULL, "vs", "vs_4_0", 0, 0, &vs_blob, &errors)) ||
        FAILED(compile(shader_src, sizeof(shader_src) - 1, "quad", NULL, NULL, "ps", "ps_4_0", 0, 0, &ps_blob, &errors)))
    {
        printf("shader compile failed: %s\n", errors ? (const char *)ID3D10Blob_GetBufferPointer(errors) : "?");
        return 100;
    }
    ID3D11Device_CreateVertexShader(dev, ID3D10Blob_GetBufferPointer(vs_blob), ID3D10Blob_GetBufferSize(vs_blob), NULL, &vs);
    ID3D11Device_CreatePixelShader(dev, ID3D10Blob_GetBufferPointer(ps_blob), ID3D10Blob_GetBufferSize(ps_blob), NULL, &ps);
    ID3D11Device_CreateInputLayout(dev, elements, 2, ID3D10Blob_GetBufferPointer(vs_blob), ID3D10Blob_GetBufferSize(vs_blob), &layout);

    /* no culling, like ANGLE's copy: a dropped triangle must not be a winding question */
    rs_desc.FillMode = D3D11_FILL_SOLID;
    rs_desc.CullMode = D3D11_CULL_NONE;
    rs_desc.DepthClipEnable = TRUE;
    ID3D11Device_CreateRasterizerState(dev, &rs_desc, &rs);

    IDXGISwapChain_GetBuffer(swapchain, 0, &IID_ID3D11Texture2D, (void **)&backbuffer);
    ID3D11Device_CreateRenderTargetView(dev, (ID3D11Resource *)backbuffer, NULL, &rtv);
    ID3D11Texture2D_GetDesc(backbuffer, &tex_desc);
    tex_desc.Usage = D3D11_USAGE_STAGING;
    tex_desc.BindFlags = 0;
    tex_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    tex_desc.MiscFlags = 0;
    ID3D11Device_CreateTexture2D(dev, &tex_desc, NULL, &staging);

    ID3D11DeviceContext_IASetInputLayout(ctx, layout);
    ID3D11DeviceContext_VSSetShader(ctx, vs, NULL, 0);
    ID3D11DeviceContext_PSSetShader(ctx, ps, NULL, 0);
    ID3D11DeviceContext_RSSetState(ctx, rs);

    for (i = 0; i < frames; i++) run_cases(i);
    printf("failures %d\n", failures);
    DestroyWindow(hwnd);
    return failures;
}
