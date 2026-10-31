#pragma once
#include <cstddef>
#include <cstdint>

namespace re3prop {
// 4323A0/432510 copy the face's TPAGE word at +6 to the GT3/GT4 packet.
// Its low byte contains ABR at bits 5..6; the high byte is a vertex index.
// One HD part can use one native ABR only. A mixed-mode part stays native.
// The caller verifies the two spans are accessible before calling this helper.
inline bool UniformNativeAbr(const uint8_t* triangles, size_t triangleCount,
                             const uint8_t* quads, size_t quadCount, unsigned& abr) {
    if ((!triangleCount && !quadCount) || (triangleCount && !triangles) || (quadCount && !quads)) return false;
    const uint8_t* first = triangleCount ? triangles : quads;
    const unsigned candidate = (first[6] >> 5) & 3u;
    for (size_t n = 0; n < triangleCount; ++n)
        if (((triangles[n * 12 + 6] >> 5) & 3u) != candidate) return false;
    for (size_t n = 0; n < quadCount; ++n)
        if (((quads[n * 16 + 6] >> 5) & 3u) != candidate) return false;
    abr = candidate;
    return true;
}
} // namespace re3prop
