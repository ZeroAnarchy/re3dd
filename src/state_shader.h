#pragma once

// D3D9 screen-space composition for the background state pilot.
// The caller supplies an XYZRHW quad with RHW=1 and TEXCOORD0 in original
// normalized screen coordinates. No half-texel correction is added here.
// Each H is a row-major 3x3 mapping screen UV to source-image UV:
//     sourceUV = (H * float3(screenUV, 1)).xy / (...).z
// The mask's actual alpha channel is sampled at untransformed screen UV;
// alpha 0 selects the base and alpha 1 selects the variant. PNGs have a
// top-left origin. Background RGB is treated as the game's existing LDR RGB.

#include <windows.h>
#include <d3d9.h>
#include <d3dcompiler.h>
#include <cmath>
#include <cstring>

namespace re3_background_shader_detail {

static const char kSource[] = R"HLSL(
sampler2D baseImage    : register(s0);
sampler2D variantImage : register(s1);
sampler2D stateAlpha   : register(s2);
float4 baseRow0 : register(c0);
float4 baseRow1 : register(c1);
float4 baseRow2 : register(c2);
float4 variantRow0 : register(c3);
float4 variantRow1 : register(c4);
float4 variantRow2 : register(c5);

float2 projectUV(float2 uv, float4 row0, float4 row1, float4 row2)
{
    float3 p = float3(uv, 1.0);
    float w = dot(row2.xyz, p);
    // Valid pilot homographies do not cross their projective horizon.
    // Guard malformed/extreme input against NaNs without changing valid UVs.
    w = (abs(w) < 1.0e-8) ? ((w < 0.0) ? -1.0e-8 : 1.0e-8) : w;
    return float2(dot(row0.xyz, p), dot(row1.xyz, p)) / w;
}

float4 main(float2 screenUV : TEXCOORD0) : COLOR0
{
    float2 baseUV = projectUV(screenUV, baseRow0, baseRow1, baseRow2);
    float2 variantUV = projectUV(screenUV, variantRow0, variantRow1, variantRow2);
    float4 baseColor = tex2D(baseImage, baseUV);
    float4 variantColor = tex2D(variantImage, variantUV);
    float weight = saturate(tex2D(stateAlpha, screenUV).a);
    return lerp(baseColor, variantColor, weight);
}
)HLSL";

// Scaled YCoCg DXT5 (pack\*\*.r3tx, r3tx_format.h): luma in alpha, chroma in red/green divided by the block scale in blue.
// The background variant: single scenes only (composite scenes stay PNG), the same projection, no state alpha.
static const char kYcocgSource[] = R"HLSL(
sampler2D variantImage : register(s1);
float4 variantRow0 : register(c3);
float4 variantRow1 : register(c4);
float4 variantRow2 : register(c5);

float2 projectUV(float2 uv, float4 row0, float4 row1, float4 row2)
{
    float3 p = float3(uv, 1.0);
    float w = dot(row2.xyz, p);
    w = (abs(w) < 1.0e-8) ? ((w < 0.0) ? -1.0e-8 : 1.0e-8) : w;
    return float2(dot(row0.xyz, p), dot(row1.xyz, p)) / w;
}

float4 main(float2 screenUV : TEXCOORD0) : COLOR0
{
    float4 c = tex2D(variantImage, projectUV(screenUV, variantRow0, variantRow1, variantRow2));
    float s = 1.0 / (31.875 * c.b + 1.0);
    float co = (c.r - 0.5019608) * s;
    float cg = (c.g - 0.5019608) * s;
    return float4(c.a + co - cg, c.a + cg, c.a - co - cg, 1.0);
}
)HLSL";

// Mask atlas: YCoCg colour in s0, the mask alpha in the alpha blocks of the DXT5 texture in s1.
static const char kMaskYcocgSource[] = R"HLSL(
sampler2D colourImage : register(s0);
sampler2D alphaImage  : register(s1);

float4 main(float2 uv : TEXCOORD0) : COLOR0
{
    float4 c = tex2D(colourImage, uv);
    float s = 1.0 / (31.875 * c.b + 1.0);
    float co = (c.r - 0.5019608) * s;
    float cg = (c.g - 0.5019608) * s;
    return float4(c.a + co - cg, c.a + cg, c.a - co - cg, tex2D(alphaImage, uv).a);
}
)HLSL";

inline void ReportCompilerErrors(ID3DBlob* errors)
{
    if (errors && errors->GetBufferPointer()) {
        OutputDebugStringA("RE3 background shader: ");
        OutputDebugStringA(static_cast<const char*>(errors->GetBufferPointer()));
    }
}

} // namespace re3_background_shader_detail

enum class BackgroundShaderKind { Png, Ycocg, MaskYcocg };

// No d3dcompiler import library is required. Windows 10/11 supply the compiler
// in the system directory; use an absolute path to avoid loading a local DLL.
// Caller owns the returned shader and must Release it before releasing device.
// D3D9 shader objects survive Reset; recreate only for a new device instance.
inline IDirect3DPixelShader9* CreateBackgroundShader(
    IDirect3DDevice9* device, HRESULT* result = nullptr,
    BackgroundShaderKind kind = BackgroundShaderKind::Png)
{
    if (result) *result = E_INVALIDARG;
    if (!device) return nullptr;

    D3DCAPS9 caps{};
    HRESULT hr = device->GetDeviceCaps(&caps);
    if (FAILED(hr)) {
        if (result) *result = hr;
        return nullptr;
    }
    if (caps.PixelShaderVersion < D3DPS_VERSION(2, 0)) {
        if (result) *result = D3DERR_NOTAVAILABLE;
        return nullptr;
    }

    wchar_t path[MAX_PATH]{};
    const UINT count = GetSystemDirectoryW(path, MAX_PATH);
    const wchar_t suffix[] = L"\\d3dcompiler_47.dll";
    if (!count || count >= MAX_PATH || count + _countof(suffix) > MAX_PATH) {
        if (result) *result = HRESULT_FROM_WIN32(
            count ? ERROR_INSUFFICIENT_BUFFER : GetLastError());
        return nullptr;
    }
    memcpy(path + count, suffix, sizeof(suffix));
    HMODULE compiler = LoadLibraryExW(path, nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!compiler) {
        if (result) *result = HRESULT_FROM_WIN32(GetLastError());
        return nullptr;
    }

    using Compile = HRESULT (WINAPI *)(LPCVOID, SIZE_T, LPCSTR,
        const D3D_SHADER_MACRO*, ID3DInclude*, LPCSTR, LPCSTR, UINT, UINT,
        ID3DBlob**, ID3DBlob**);
    const auto compile = reinterpret_cast<Compile>(GetProcAddress(compiler, "D3DCompile"));
    if (!compile) {
        if (result) *result = HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND);
        FreeLibrary(compiler);
        return nullptr;
    }

    ID3DBlob* bytecode = nullptr;
    ID3DBlob* errors = nullptr;
    IDirect3DPixelShader9* shader = nullptr;
    const char* profile = caps.PixelShaderVersion >= D3DPS_VERSION(3, 0)
        ? "ps_3_0" : "ps_2_0";
    using namespace re3_background_shader_detail;
    const char* source = kind == BackgroundShaderKind::Ycocg ? kYcocgSource
        : kind == BackgroundShaderKind::MaskYcocg ? kMaskYcocgSource : kSource;
    const size_t length = kind == BackgroundShaderKind::Ycocg ? sizeof(kYcocgSource) - 1
        : kind == BackgroundShaderKind::MaskYcocg ? sizeof(kMaskYcocgSource) - 1 : sizeof(kSource) - 1;
    hr = compile(source, length,
        "RE3 pilot background composition", nullptr, nullptr, "main", profile,
        D3DCOMPILE_OPTIMIZATION_LEVEL3,
        0, &bytecode, &errors);
    re3_background_shader_detail::ReportCompilerErrors(errors);
    if (SUCCEEDED(hr)) {
        hr = device->CreatePixelShader(
            static_cast<const DWORD*>(bytecode->GetBufferPointer()), &shader);
    }
    // Blob methods reside in the compiler DLL; release them before unloading it.
    if (errors) errors->Release();
    if (bytecode) bytecode->Release();
    FreeLibrary(compiler);
    if (result) *result = hr;
    return SUCCEEDED(hr) ? shader : nullptr;
}

// Changes pixel shader, c0-c5, textures s0-s2 and those three sampler states.
// Caller captures/restores a D3DSBT_ALL state block, sets FVF/vertex shader and
// opaque draw/depth/viewport states, and restores that block even on failure.
// Inputs must remain alive through the draw. Hbase/Hvariant each contain 9
// finite floats; normalized source-image UV means no texture-size adjustment.
inline HRESULT SetBackgroundShader(
    IDirect3DDevice9* device, IDirect3DPixelShader9* shader,
    const float* Hbase, const float* Hvariant,
    IDirect3DTexture9* baseTexture, IDirect3DTexture9* variantTexture,
    IDirect3DTexture9* alphaTexture)
{
    if (!device || !shader || !Hbase || !Hvariant || !baseTexture ||
        !variantTexture || !alphaTexture) return E_INVALIDARG;

    float rows[6][4]{};
    for (unsigned row = 0; row < 3; ++row) {
        for (unsigned col = 0; col < 3; ++col) {
            const float b = Hbase[row * 3 + col];
            const float v = Hvariant[row * 3 + col];
            if (!std::isfinite(b) || !std::isfinite(v)) return E_INVALIDARG;
            rows[row][col] = b;
            rows[row + 3][col] = v;
        }
    }

    HRESULT hr = device->SetPixelShaderConstantF(0, &rows[0][0], 6);
    if (FAILED(hr)) return hr;
    IDirect3DTexture9* const textures[3] = {
        baseTexture, variantTexture, alphaTexture
    };
    struct SamplerSetting { D3DSAMPLERSTATETYPE state; DWORD value; };
    const SamplerSetting settings[] = {
        { D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP },
        { D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP },
        { D3DSAMP_MINFILTER, D3DTEXF_LINEAR },
        { D3DSAMP_MAGFILTER, D3DTEXF_LINEAR },
        { D3DSAMP_MIPFILTER, D3DTEXF_NONE },
        { D3DSAMP_SRGBTEXTURE, FALSE }
    };
    for (DWORD slot = 0; slot < 3; ++slot) {
        hr = device->SetTexture(slot, textures[slot]);
        if (FAILED(hr)) return hr;
        for (const auto& setting : settings) {
            hr = device->SetSamplerState(slot, setting.state, setting.value);
            if (FAILED(hr)) return hr;
        }
    }
    return device->SetPixelShader(shader);
}

inline void ReleaseBackgroundShader(IDirect3DPixelShader9*& shader)
{
    if (shader) {
        shader->Release();
        shader = nullptr;
    }
}
