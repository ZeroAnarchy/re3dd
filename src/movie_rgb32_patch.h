// movie_rgb32_patch.h - proposal for the RE3 pilot runtime (d3d9 proxy), video-v14, 2026-10-22.
//
// Problem: ResidentEvil3.exe creates the FMV texture through its texture helper 0x4138D0 with the
// format mask 2 ("16-bit RGB, no alpha"). With the GOG ddraw.dll the first enumerated match is
// R5G6B5, the movie system-memory surface gets the same format, and GOG dshow.dll converts every
// decoded BGRA frame to 565 by plain bit truncation (dshow.dll 0x100C2A82, no dithering).
// Result: 32 grey levels on R/B, banding in dark gradients - in the original 320x160 movies too,
// but much more visible on the 1920x960 remaster.
//
// Fix: request mask 8 ("32-bit RGB, no alpha") instead. GOG ddraw.dll enumerates X8R8G8B8 as its
// 5th texture format (harness: movie_probe --caps with PROBE_TEXFMT=1), the chooser 0x413740
// accepts it for mask 8 (32-bit case, alpha mask 0), the movie surface becomes 32-bit, and
// dshow.dll's Update copies the BGRA frame 1:1 (memcpy branch 0x100C29BD). Max texture size of
// the wrapper is 2048x2048 without the POW2 cap, so a 1920x960 movie fits exactly.
//
// Patch sites (function 0x411BA0, "create movie texture + surface + sample"):
//   0x411BD2: 6A 02   push 2   ; first try: movie size
//   0x411BE7: 6A 02   push 2   ; fallback: 256x256
// Only the immediate bytes 0x411BD3 / 0x411BE8 change 02 -> 08. The image has no relocations
// (ImageBase 0x400000, no ASLR), so gameBase is normally 0x400000; it is still taken as argument.
// Cost: the movie surface + texture grow from 2 to 4 bytes/pixel (+3.7 MB + 3.7 MB at 1920x960).
//
// Integrated 2026-10-23 (runtime v14 movies): proxy.cpp installGameHooks() applies it right after the image-size
// check, before the opening movie; pilot.ini movie_rgb32 (default 1, the launcher writes 1) = 0 keeps mask 2.
// Log line MOVIE_RGB32 applied/skipped/disabled. Revert = movie_rgb32=0 and restart (or RevertRgb32Patch).
#pragma once
#include <windows.h>
#include <cstdint>
#include <cstring>
#include "exe_map.h"

namespace re3movie {

// expected bytes around each site (from ResidentEvil3.exe SHA256 f6ab367f...35123ec)
struct Site { uint32_t rva; uint8_t before[8]; };
static const Site kSites[2] = {
    // 8b bc 24 90 00 00 00 | 6a 02 ...   (mov edi,[esp+90h]; push 2)
    {0x11BD2, {0x8b, 0xbc, 0x24, 0x90, 0x00, 0x00, 0x00, 0x6a}},
    // 85 c0 75 1d | 6a 02 50 68  ... (test eax,eax; jne; push 2; push eax; push 100h)
    {0x11BE7, {0x00, 0x00, 0x85, 0xc0, 0x75, 0x1d, 0x6a, 0x02}},
};

inline bool SiteMatches(const uint8_t* p, int which, uint8_t immediate) {
  if (which == 0) return std::memcmp(p - 7, kSites[0].before, 8) == 0 && p[1] == immediate;
  return std::memcmp(p - 6, kSites[1].before, 6) == 0 && p[0] == 0x6a && p[1] == immediate && p[2] == 0x50 && p[3] == 0x68;
}

// Returns true when both sites were verified (holding the original 02 or an earlier 08) and now
// hold `mask` (08 = patch, 02 = revert). Nothing is written unless both sites verify.
inline bool ApplyRgb32Patch(re3exe::Base imageBase, uint8_t mask = 0x08) {
  uint8_t* site[2];
  for (int i = 0; i < 2; i++) {
    site[i] = reinterpret_cast<uint8_t*>(imageBase + kSites[i].rva);
    MEMORY_BASIC_INFORMATION mbi = {};
    if (!VirtualQuery(site[i] - 8, &mbi, sizeof mbi) || mbi.State != MEM_COMMIT) return false;
    if (!SiteMatches(site[i], i, 0x02) && !SiteMatches(site[i], i, 0x08)) return false;
  }
  for (int i = 0; i < 2; i++) {
    DWORD old = 0, unused = 0;
    if (!VirtualProtect(site[i] + 1, 1, PAGE_EXECUTE_READWRITE, &old)) return false;
    site[i][1] = mask;
    VirtualProtect(site[i] + 1, 1, old, &unused);
    FlushInstructionCache(GetCurrentProcess(), site[i], 2);
  }
  return true;
}

inline bool RevertRgb32Patch(re3exe::Base imageBase) { return ApplyRgb32Patch(imageBase, 0x02); }

}  // namespace re3movie
