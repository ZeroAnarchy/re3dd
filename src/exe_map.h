#pragma once
// RE3_MERCE.exe (Mercenaries) is ResidentEvil3.exe's code base built separately: same section layout, .rdata/.data/
// .bss identical but code pointers, code moved piecewise. All exe addresses in this runtime are written as
// gameBase+<ResidentEvil3.exe RVA>; re3exe::Base::operator+ translates the RVA for the running exe: identity for
// ResidentEvil3.exe (and for headers/data in both), the verified code ranges of exe_map_merce_data.h
// (qa/fix-2026-10-25b/merce-port/make_exe_map.py, check_coverage.py) for RE3_MERCE.exe. A code RVA outside them
// resolves to a zeroed sink: every call/slot/byte guard then fails and that feature stays native (EXE_MAP_MISS).
#include <windows.h>
#include <cstdint>
#include <type_traits>
#include "exe_map_merce_data.h"

namespace re3exe {
enum class Exe { Unknown, Main, Merce };
inline Exe gRunning = Exe::Main;
inline void (*gMissLog)(uint32_t rva) = nullptr;
inline volatile LONG gMisses = 0;
alignas(16) inline uint8_t gSink[0x10000];
inline uintptr_t Translate(uintptr_t base, uint32_t rva) {
    if (gRunning != Exe::Merce || rva < 0x1000 || (rva >= 0x10a000 && rva < 0x667000)) return base + rva;
    size_t lo = 0, hi = sizeof(kMerceCode) / sizeof(kMerceCode[0]);
    while (lo < hi) { const size_t mid = (lo + hi) / 2; if (kMerceCode[mid].hi <= rva) lo = mid + 1; else hi = mid; }
    if (lo < sizeof(kMerceCode) / sizeof(kMerceCode[0]) && kMerceCode[lo].lo <= rva) return base + rva + kMerceCode[lo].delta;
    if (InterlockedIncrement(&gMisses) <= 64 && gMissLog) gMissLog(rva);
    return reinterpret_cast<uintptr_t>(gSink);
}
struct Base {
    uintptr_t value = 0;
    Base(uintptr_t v = 0) : value(v) {}
    operator uintptr_t() const { return value; }
    template<class T> uintptr_t operator+(T rva) const {
        static_assert(std::is_integral<T>::value, "gameBase + RVA");
        return Translate(value, static_cast<uint32_t>(rva));
    }
};
// Which exe is loaded: PE header identity (link time stamp, entry, .text size, image size); RE3_MERCE.exe also needs
// the FNV-1a 64 of its file. ResidentEvil3.exe keeps the old behaviour (header only, file hash just reported).
inline Exe Detect(uintptr_t base, uint64_t* fileHash) {
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < 0 || dos->e_lfanew > 0x800) return Exe::Unknown;
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->FileHeader.NumberOfSections < 1) return Exe::Unknown;
    const auto* text = IMAGE_FIRST_SECTION(nt);
    const uint32_t stamp = nt->FileHeader.TimeDateStamp, entry = nt->OptionalHeader.AddressOfEntryPoint,
        size = nt->OptionalHeader.SizeOfImage, textSize = text->Misc.VirtualSize;
    uint64_t hash = 0xcbf29ce484222325ull; bool hashed = false;
    wchar_t path[MAX_PATH]; const DWORD n = GetModuleFileNameW(reinterpret_cast<HMODULE>(base), path, MAX_PATH);
    if (n && n < MAX_PATH) {
        HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
        if (file != INVALID_HANDLE_VALUE) {
            static uint8_t chunk[1 << 16]; DWORD got = 0; hashed = true;
            while (ReadFile(file, chunk, sizeof(chunk), &got, nullptr) && got) for (DWORD i = 0; i < got; ++i) hash = (hash ^ chunk[i]) * 0x100000001b3ull;
            CloseHandle(file);
        }
    }
    if (fileHash) *fileHash = hashed ? hash : 0;
    if (stamp == 0x39d81b32 && entry == 0xef59c && size == 0x667000 && textSize == 0x10847a) return Exe::Main;
    if (stamp == 0x39d819f2 && entry == 0xef60c && size == 0x667000 && textSize == 0x1084ea && hashed &&
        hash == 0x470ce52d7955fee1ull) return Exe::Merce;
    return Exe::Unknown;
}
} // namespace re3exe
