// vxshadercheck: compiles every HLSL shader with the D3D compiler (Windows CI), so shader
// errors are caught at build/test time instead of at the first launch on a Windows machine.
#include <cstdio>
#if defined(_WIN32)
#include <d3dcompiler.h>
#include "app/shaders.h"

static int compile(const char* name, const char* src, const char* entry, const char* target) {
    ID3DBlob* code = nullptr; ID3DBlob* errs = nullptr;
    HRESULT hr = D3DCompile(src, strlen(src), name, nullptr, nullptr, entry, target, D3DCOMPILE_ENABLE_STRICTNESS, 0, &code, &errs);
    if (FAILED(hr)) { printf("FAIL %-16s %s/%s\n%s\n", name, entry, target, errs ? (const char*)errs->GetBufferPointer() : "(no message)"); return 1; }
    printf(" ok  %-16s %s/%s (%zu bytes)\n", name, entry, target, (size_t)code->GetBufferSize());
    code->Release(); if (errs) errs->Release();
    return 0;
}

int main() {
    using namespace vx::shaders;
    int f = 0;
    f += compile("terrain", terrainVS, "vs_main", "vs_5_0");
    f += compile("terrain-opaque", terrainFS, "fs_opaque", "ps_5_0");
    f += compile("terrain-cutout", terrainFS, "fs_cutout", "ps_5_0");
    f += compile("entity", entityVS, "vs_main", "vs_5_0");
    f += compile("lines", linesVS, "vs_main", "vs_5_0");
    f += compile("lines", linesFS, "fs_main", "ps_5_0");
    f += compile("far", farVS, "vs_main", "vs_5_0");
    f += compile("far", farFS, "fs_main", "ps_5_0");
    f += compile("ui", uiVS, "vs_main", "vs_5_0");
    f += compile("ui", uiFS, "fs_main", "ps_5_0");
    printf("%d shader(s) failed\n", f);
    return f ? 1 : 0;
}
#else
int main() { printf("vxshadercheck: HLSL checks run on Windows only (Metal shaders are compiled when the game starts)\n"); return 0; }
#endif
