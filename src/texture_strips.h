#pragma once
// Strip upload of large replacement images (2026-10-31). The game is a 32-bit process without the large-address flag
// (2 GB). loadReplacement used to decode a whole image into one SYSTEMMEM staging texture: 85 MB in a single block for
// a 4096x4096 albedo with its mip chain. After 15-45 minutes of play the address space no longer had such a block, the
// load failed with E_OUTOFMEMORY and the Grave Digger (creature-em30) and Tyrell stayed native for the rest of the session.
// Here rows of every mip level are decoded/filtered into small SYSTEMMEM surfaces (<= 4 MB for level 0) and copied
// into the DEFAULT texture with UpdateSurface, strip after strip. Texels are identical to the old path: each level is
// the same 2x2 box filter of the level above, and a strip of 2^(levels-1)*k rows holds whole rows of every level.
#include <d3d9.h>
#include <wincodec.h>
#include <algorithm>
#include <cstdint>
#include <cstddef>

namespace re3strips {
constexpr size_t kStripBytes = 4u * 1024 * 1024; // level-0 rows per strip; images up to this size keep the old path
using TableFn = void** (*)(void* object);        // original vtable of a D3D object (the proxy patches shared vtables)

// one row of level n+1 from rows 2y and 2y+1 of level n (lw x lh); the loader's box filter
inline void BoxRow(const uint8_t* row0, const uint8_t* row1, uint8_t* dst, UINT lw, UINT nw) {
    for (UINT x = 0; x < nw; ++x) {
        const size_t x0 = size_t((std::min)(2 * x, lw - 1)) * 4, x1 = size_t((std::min)(2 * x + 1, lw - 1)) * 4;
        for (UINT c = 0; c < 4; ++c) dst[size_t(x) * 4 + c] = uint8_t((row0[x0 + c] + row0[x1 + c] + row1[x0 + c] + row1[x1 + c] + 2) / 4);
    }
}
// mip chains need a power-of-two height (strip rows map exactly onto rows of every level); widths may be anything
inline bool Eligible(UINT w, UINT h, UINT levels) { return size_t(w) * h * 4 > kStripBytes && (levels == 1 || !(h & (h - 1))); }
// level-0 rows per strip: a power of two, at least 2^(levels-1), at most kStripBytes when possible, at most h for chains
inline UINT StripRows(UINT w, UINT h, UINT levels) {
    UINT rows = 1u << (levels - 1);
    while (size_t(rows) * 2 * w * 4 <= kStripBytes && rows * 2 <= h) rows *= 2;
    return levels > 1 ? (std::min)(rows, h) : rows;
}

inline HRESULT Upload(IDirect3DDevice9* d, void** deviceTable, TableFn table, IWICBitmapSource* source, UINT w, UINT h, UINT levels, IDirect3DTexture9* out) {
    using CreateOffscreenFn = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*, UINT, UINT, D3DFORMAT, D3DPOOL, IDirect3DSurface9**, HANDLE*);
    using UpdateSurfaceFn = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*, IDirect3DSurface9*, const RECT*, IDirect3DSurface9*, const POINT*);
    using GetSurfaceFn = HRESULT(STDMETHODCALLTYPE*)(IDirect3DTexture9*, UINT, IDirect3DSurface9**);
    using LockFn = HRESULT(STDMETHODCALLTYPE*)(IDirect3DSurface9*, D3DLOCKED_RECT*, const RECT*, DWORD);
    using UnlockFn = HRESULT(STDMETHODCALLTYPE*)(IDirect3DSurface9*);
    using ReleaseFn = ULONG(STDMETHODCALLTYPE*)(IUnknown*);
    if (levels < 1 || levels > 16) return E_INVALIDARG;
    const UINT rows = StripRows(w, h, levels);
    IDirect3DSurface9* strip[16] = {};
    IDirect3DSurface9* level[16] = {};
    HRESULT hr = S_OK;
    for (UINT l = 0; SUCCEEDED(hr) && l < levels; ++l) {
        hr = ((CreateOffscreenFn)deviceTable[36])(d, (std::max)(1u, w >> l), (std::max)(1u, rows >> l), D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, &strip[l], nullptr);
        if (SUCCEEDED(hr)) hr = ((GetSurfaceFn)table(out)[18])(out, l, &level[l]);
    }
    for (UINT y0 = 0; SUCCEEDED(hr) && y0 < h; y0 += rows) {
        const UINT count = (std::min)(rows, h - y0);
        D3DLOCKED_RECT lr[16] = {};
        UINT locked = 0;
        for (UINT l = 0; SUCCEEDED(hr) && l < levels; ++l) {
            hr = ((LockFn)table(strip[l])[13])(strip[l], &lr[l], nullptr, 0);
            if (FAILED(hr)) break;
            locked = l + 1;
            if (!l) {
                const WICRect rect{0, INT(y0), INT(w), INT(count)};
                hr = source->CopyPixels(&rect, UINT(lr[0].Pitch), UINT(lr[0].Pitch) * count, (BYTE*)lr[0].pBits);
                continue;
            }
            const UINT lw = (std::max)(1u, w >> (l - 1)), nw = (std::max)(1u, w >> l), nh = count >> l;
            for (UINT y = 0; y < nh; ++y)
                BoxRow((const uint8_t*)lr[l - 1].pBits + size_t(2 * y) * lr[l - 1].Pitch, (const uint8_t*)lr[l - 1].pBits + size_t(2 * y + 1) * lr[l - 1].Pitch,
                       (uint8_t*)lr[l].pBits + size_t(y) * lr[l].Pitch, lw, nw);
        }
        for (UINT l = 0; l < locked; ++l) ((UnlockFn)table(strip[l])[14])(strip[l]);
        for (UINT l = 0; SUCCEEDED(hr) && l < levels; ++l) {
            const RECT from{0, 0, LONG((std::max)(1u, w >> l)), LONG(count >> l)};
            const POINT to{0, LONG(y0 >> l)};
            hr = ((UpdateSurfaceFn)deviceTable[30])(d, strip[l], &from, level[l], &to);
        }
    }
    for (UINT l = 0; l < levels; ++l) {
        if (strip[l]) ((ReleaseFn)table(strip[l])[2])(strip[l]);
        if (level[l]) ((ReleaseFn)table(level[l])[2])(level[l]);
    }
    return hr;
}
} // namespace re3strips
