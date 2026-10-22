/* Compiles quadtest's shaders with Microsoft's d3dcompiler_47 (run on Windows) and writes
 * quadtest_shaders.h with the bytecode, so the test does not depend on Wine's HLSL compiler. */
#include <windows.h>
#include <d3dcompiler.h>
#include <stdio.h>

typedef HRESULT (WINAPI *compile_fn)(const void *, SIZE_T, const char *, const D3D_SHADER_MACRO *,
                                     ID3DInclude *, const char *, const char *, UINT, UINT,
                                     ID3DBlob **, ID3DBlob **);

static const char shader_src[] =
    "struct VSIn { float2 pos : POSITION; float2 tex : TEXCOORD0; };\n"
    "struct VSOut { float4 pos : SV_POSITION; float2 tex : TEXCOORD0; };\n"
    "VSOut vs(VSIn i) { VSOut o; o.pos = float4(i.pos, 0.0, 1.0); o.tex = i.tex; return o; }\n"
    "float4 ps(VSOut i) : SV_TARGET { return float4(1.0, 0.0, 0.0, 1.0); }\n"
    "Texture2D tex : register(t0);\n"
    "SamplerState smp : register(s0);\n"
    "float4 ps_tex(VSOut i) : SV_TARGET { return tex.Sample(smp, i.tex); }\n";

static void emit(FILE *out, compile_fn compile, const char *entry, const char *target, const char *name)
{
    ID3DBlob *blob = NULL, *errors = NULL;
    const unsigned char *p;
    SIZE_T i, n;

    if (FAILED(compile(shader_src, sizeof(shader_src) - 1, "quad", NULL, NULL, entry, target, 0, 0, &blob, &errors)))
    {
        fprintf(stderr, "%s %s: %s\n", entry, target, errors ? (const char *)errors->lpVtbl->GetBufferPointer(errors) : "?");
        ExitProcess(1);
    }
    p = blob->lpVtbl->GetBufferPointer(blob);
    n = blob->lpVtbl->GetBufferSize(blob);
    fprintf(out, "/* %s %s, %u bytes */\nstatic const unsigned char %s[] = {", entry, target, (unsigned)n, name);
    for (i = 0; i < n; i++) fprintf(out, "%s0x%02x,", i % 16 ? " " : "\n    ", p[i]);
    fprintf(out, "\n};\n\n");
}

int main(void)
{
    HMODULE lib = LoadLibraryA("d3dcompiler_47.dll");
    compile_fn compile = lib ? (compile_fn)(void *)GetProcAddress(lib, "D3DCompile") : NULL;
    FILE *out;

    if (!compile) return 1;
    if (!(out = fopen("quadtest_shaders.h", "w"))) return 1;
    fprintf(out, "/* Generated on Windows by Microsoft's d3dcompiler_47 from quadtest.c's shader_src (see genshaders.c). */\n\n");
    emit(out, compile, "vs", "vs_4_0", "vs_4_0_code");
    emit(out, compile, "ps", "ps_4_0", "ps_4_0_code");
    emit(out, compile, "vs", "vs_4_0_level_9_3", "vs_4_0_level_9_3_code");
    emit(out, compile, "ps", "ps_4_0_level_9_3", "ps_4_0_level_9_3_code");
    emit(out, compile, "ps_tex", "ps_4_0", "ps_tex_4_0_code");
    emit(out, compile, "ps_tex", "ps_4_0_level_9_3", "ps_tex_4_0_level_9_3_code");
    fclose(out);
    return 0;
}
