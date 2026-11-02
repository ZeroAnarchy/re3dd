#pragma once
// .r3tx (2026-11-02): an HD background or mask atlas of pack\<scene>\ as GPU block data, so a camera change copies blocks
// into video memory instead of decoding a 2560x1920 PNG on the CPU (67-112 ms here, 204-458 ms on an i5-11300H laptop).
//   colour  DXT5 blocks with scaled YCoCg (van Waveren / Castano 2007): luma Y in the alpha block, Co and Cg in red and green
//           (times 1, 2 or 4 per block), that block scale in blue (5-bit s-1); a pixel shader turns it back into RGB
//   alpha   (atlases) the mask alpha as BC4 / DXT5-alpha blocks, 8 bytes per 4x4; the DLL uploads it as a DXT5 texture
//           whose alpha blocks are these (no ATI1: its D3D9 pitch differs between drivers)
// Written by tools\r3tx (r3tx_pack.exe) from the PNG next to it, which stays the source. The header keeps the PNG's size and
// the CRC of its last chunk before IEND: when the PNG next to it no longer matches, the DLL loads the PNG instead.
#include <cstdint>
#include <cstring>

namespace r3tx {
constexpr uint32_t kMagic = 0x58543352;   // "R3TX"
constexpr uint32_t kVersion = 1;
enum Format : uint32_t { YCoCg = 1, YCoCgAlpha = 2 };
struct Header {
    uint32_t magic, version, width, height, format;
    uint32_t sourceSize, sourceCrc;        // the PNG it was made from (staleness)
    uint32_t colourBytes, alphaBytes;      // block data after the header: colour, then alpha
    uint32_t reserved[3];
};
static_assert(sizeof(Header) == 48, "r3tx header");

inline uint32_t BlocksWide(uint32_t w) { return (w + 3) / 4; }
inline uint32_t BlocksHigh(uint32_t h) { return (h + 3) / 4; }
inline bool Valid(const Header& h, uint64_t fileSize) {
    if (h.magic != kMagic || h.version != kVersion || (h.format != YCoCg && h.format != YCoCgAlpha)) return false;
    if (!h.width || !h.height || h.width > 8192 || h.height > 8192 || (h.width & 3) || (h.height & 3)) return false;
    const uint64_t blocks = uint64_t(BlocksWide(h.width)) * BlocksHigh(h.height);
    if (h.colourBytes != blocks * 16 || h.alphaBytes != (h.format == YCoCgAlpha ? blocks * 8 : 0)) return false;
    return fileSize == sizeof(Header) + uint64_t(h.colourBytes) + h.alphaBytes;
}
// PNG tail: ... [len][type][data][crc of the last data chunk] [0000 0000 "IEND" AE426082]. The CRC before IEND changes with
// the image data; with the file size it tells a regenerated PNG from the one the .r3tx was made from.
inline bool PngTail(const uint8_t* last16, uint32_t& crc) {
    if (memcmp(last16 + 4, "\0\0\0\0IEND", 8) != 0) return false;
    crc = uint32_t(last16[0]) << 24 | uint32_t(last16[1]) << 16 | uint32_t(last16[2]) << 8 | last16[3];
    return true;
}

// CPU decoding (the DLL's fallback without DXT5 or pixel shaders, the tool's verify mode). BGRA output.
inline void AlphaBlock(const uint8_t* b, uint8_t out[16]) {
    const int a0 = b[0], a1 = b[1]; int pal[8] = {a0, a1};
    if (a0 > a1) for (int i = 1; i < 7; ++i) pal[1 + i] = ((7 - i) * a0 + i * a1) / 7;
    else { for (int i = 1; i < 5; ++i) pal[1 + i] = ((5 - i) * a0 + i * a1) / 5; pal[6] = 0; pal[7] = 255; }
    uint64_t bits = 0; for (int i = 0; i < 6; ++i) bits |= uint64_t(b[2 + i]) << (8 * i);
    for (int i = 0; i < 16; ++i) out[i] = uint8_t(pal[(bits >> (3 * i)) & 7]);
}
// DXT5 colour block (always the four-colour palette); RGB in 0..255, endpoints expanded by bit replication like the encoder
inline int E5(int v) { return (v << 3) | (v >> 2); }
inline int E6(int v) { return (v << 2) | (v >> 4); }
inline void ColourBlock(const uint8_t* b, uint8_t out[16][3]) {
    const int c0 = b[0] | b[1] << 8, c1 = b[2] | b[3] << 8; int pal[4][3];
    const int e[2][3] = {{E5(c0 >> 11), E6((c0 >> 5) & 63), E5(c0 & 31)}, {E5(c1 >> 11), E6((c1 >> 5) & 63), E5(c1 & 31)}};
    for (int c = 0; c < 3; ++c) { pal[0][c] = e[0][c]; pal[1][c] = e[1][c]; pal[2][c] = (2 * e[0][c] + e[1][c]) / 3; pal[3][c] = (e[0][c] + 2 * e[1][c]) / 3; }
    const uint32_t idx = uint32_t(b[4]) | uint32_t(b[5]) << 8 | uint32_t(b[6]) << 16 | uint32_t(b[7]) << 24;
    for (int i = 0; i < 16; ++i) for (int c = 0; c < 3; ++c) out[i][c] = uint8_t(pal[(idx >> (2 * i)) & 3][c]);
}
inline uint8_t Clamp8(int v) { return uint8_t(v < 0 ? 0 : v > 255 ? 255 : v); }
// one 16-byte YCoCg block (+ an optional 8-byte alpha block) into 4 rows of a BGRA image
inline void DecodeBlock(const uint8_t* colour, const uint8_t* alpha, uint8_t* dst, size_t pitch) {
    uint8_t y[16], a[16], c[16][3]; AlphaBlock(colour, y); ColourBlock(colour + 8, c);
    if (alpha) AlphaBlock(alpha, a); else memset(a, 255, 16);
    for (int i = 0; i < 16; ++i) {
        const float s = 1.0f / (c[i][2] / 8.0f + 1.0f), co = (c[i][0] - 128) * s, cg = (c[i][1] - 128) * s, Y = y[i];
        uint8_t* p = dst + size_t(i / 4) * pitch + size_t(i % 4) * 4;
        p[0] = Clamp8(int(Y - co - cg + 0.5f)); p[1] = Clamp8(int(Y + cg + 0.5f)); p[2] = Clamp8(int(Y + co - cg + 0.5f)); p[3] = a[i];
    }
}
} // namespace r3tx
