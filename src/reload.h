#pragma once
// Controls round 2026-11-09, item 3: manual reload of the equipped weapon at any moment (also with rounds left), through
// the game's own reload. Report: runtime/staging/ctl-2026-11-09/reload/REPORT.ru.txt; static evidence and the offline test:
// qa/ctl-2026-11-09/reload (rl_dis.py, rl_items.py, rl_plw.py, rl_sigs.py, test_reload_logic.cpp).
//
// Native reload (ResidentEvil3.exe RVAs; gameBase+RVA, translated for RE3_MERCE.exe by exe_map.h; no code is patched here):
// - Inventory [A623E4]: slots of 4 bytes {id, count, word flags}, +128 equipped slot (FF none), +129 its id, +12A slots.
//   Flags &3: 1 counted, 2 M4A1 sub-counter (bits 14-15 = rounds fired inside the current unit: 4DEC10(1) subtracts 0x4000
//   per round and takes one unit when the field wraps to 0, then sets it back to 3 -> one shown unit = 3 rounds; a reload
//   only adds units and never touches the field), 3 infinite (4DEC10 answers FF, nothing is counted).
// - Attributes 52AC50 + 4*id {type 1 weapon / 2 ammo, capacity, ?, slot flags}; reload rows 52B300 {weapon, ammo, 0, 0}
//   FF-terminated, looked up by 4DCBC0; combine rows 52AF00 {type, a, b, result, ...} 8 bytes (type 0 = load/merge),
//   looked up by 4DCB50. glock18c.h relocates both tables (operands 4DCBC2/4DCBC7, 4DCB52/4DCB5B) and adds the Glock
//   rows 53/54 <- 15 with capacity 19: the pointers are read from those operands, so the Glock reloads to 19.
// - 4DEB60(apply): reload row of the equipped id, first stack of its ammo (4DE9D0(ammo, 0)); apply = 0 only answers
//   0 (possible) / FF; apply = 1 moves take = min(capacity - loaded, stack) (4DEBD4: capacity of the slot item), loaded +=
//   take, 4DEA10(stack slot, take, 0) (count -= take unless infinite; 0 -> 4DEA60 removes the slot and shifts the later
//   slots down, the equipped index follows) and sets player+D3 bit 4 (casings to drop).
// - Auto reload in the aim state 464640 (table 52A508[player+6] via 464340 for every weapon but 0B/0F; 0F = 465200 ->
//   52A534): fire pressed with an empty gun (4DEC10(0) == FFFF) -> 4DEB60(0) == 0 -> word [player+6] = 4 (state 4, sub 0).
//   State 4 = 464AA0 (M4A1 AUTO: 4657A0) plays upper-body animation 7 (0A) of the PLW with its own frame events: sound
//   nibble 0xF000 -> 427B20(0x10100 | n-1, parts+344), 0x400 casings, 0x200 -> 4DEB60(1); at its end word [6] = 1 (aim).
//   The reload state never checks that the gun is empty, so starting it with rounds left keeps them (loaded += take).
//   PLW section 5 (upper body) animation 7 has a reload event for 02 03 04 05 0D 0E 10 11 12 13 (and 0F anim 0A) of Jill
//   (PL00) and Carlos (PL08) - exactly the weapons of the reload table. Jill's grenade launchers 06-09 and the rocket
//   launcher 0A have a 1-frame stub, the Mine Throwers 0C/14 27 frames without any event, the Gatling 0B none.
// - Grenade launcher 06/07/08/09 (grenade/flame/acid/freeze loaded): no reload row, the game only loads it in the
//   inventory (combine type 0, same arithmetic, capacity 250 = the whole stack up to 250; another round type is the type-4
//   swap, which we never do). Here: the same arithmetic as 4DEB60 for the round type in the launcher (combine row
//   {0, weapon, ammo}), weapon count += take and the native 4DEA10 for the stack, no animation (there is none), the
//   launcher's own mechanical sounds of its shot animation (A_06 bank events S4 / S5 -> 427B20(0x10103 / 0x10104)).
// - Mine Thrower 0C (and its enhanced 14 "M. Thrower E"): never (the user's rule; the inventory loads it only when empty,
//   4DAB8E). Knife 01, rocket launcher 0A, Gatling 0B, infinite slots: nothing to reload.
// Player A5CBF4: +4 routine (1 normal), +5 sub-routine (0 stand, 1 walk, 2 run, 3 back, 4 turn, 5 weapon, 6 quick turn,
//   7 = the 0x20701 move the native takes instead of aiming while +0 bit 31 is set and life > 20), +6/+7 weapon state/sub.
//   The stand/walk/run/back/turn controllers (52A238[0..4]) enter the weapon routine on aim with dword [+4] = 0x501 (raise,
//   state 0 -> 1 at the end of the raise). The weapon routine 463C60 sets A61F5C |= 0x140 every frame (cleared by the
//   stand controller), so that bit is "player busy", not "game busy".
// When: the guards of the native status-screen request (42B7A9..42B85A, as re3options::Blocked; the inventory can be
//   opened there: no event, door, menu, movie, message, fade, camera change, hit cooldown A5CD21 = player+12D) without the
//   A61F5C bit 0x100, plus the player in routine 1 with sub-routine 0..4 (raised first, like the aim press: state 0 ->
//   1 -> 4 -> back to 1 -> lowered if aim is not held) or 5 in the aim state 1 / lowering 3 (state 4 right away); a raise
//   (0) or a shot (2) in progress is waited for. Not during damage (routine 2), death, the 0x20701 move, stairs and other
//   sub-routines, nor with +0 bit 31 set. A request waits reload_wait_frames polls for such a state, then it is dropped.
// pilot.ini: reload_enabled=1, reload_key=82 (VK 'R', 0 = no key), reload_from_idle=1 (0 = only while aiming),
//   reload_wait_frames=12 (1..60), reload_gl_sound=1. Debug: pilot.command reload (debug_warp=1).
#include <windows.h>
#include <cstdarg>
#include <cstdint>
#include <cstring>
#include <string>
#include "exe_map.h"

namespace re3reload {

// ---------------------------------------------------------------- pure logic (offline test: test_reload_logic.cpp)
namespace logic {
constexpr unsigned kSlotsMax = 0x4a;        // slots end before +128
constexpr unsigned kAttributeIds = 0xac;    // 52AC50 .. 52AF00 (combine table) / 4
constexpr unsigned kReloadRowsMax = 64, kCombineRowsMax = 0x90;
enum class Kind : uint8_t { None, Animated, Instant };
enum class Refuse : uint8_t { Ok, NoInventory, NoWeapon, MineThrower, Infinite, NotReloadable, Full, NoAmmo };
inline const char* RefuseName(Refuse r) {
    switch (r) {
    case Refuse::Ok: return "ok";
    case Refuse::NoInventory: return "no inventory";
    case Refuse::NoWeapon: return "no weapon equipped";
    case Refuse::MineThrower: return "Mine Thrower (never reloaded)";
    case Refuse::Infinite: return "infinite ammo";
    case Refuse::NotReloadable: return "weapon without ammo (knife/rocket/gatling)";
    case Refuse::Full: return "magazine full";
    case Refuse::NoAmmo: return "no matching ammo";
    }
    return "?";
}
struct Tables { const uint8_t* attributes; const uint8_t* reload; const uint8_t* combine; };
struct Plan {
    Refuse refuse = Refuse::NoInventory; Kind kind = Kind::None;
    uint8_t weapon = 0, slot = 0xff, ammo = 0, ammoSlot = 0xff, loaded = 0, capacity = 0, stack = 0, take = 0;
    uint16_t flags = 0;
};
inline bool IsMineThrower(uint8_t id) { return id == 0x0c || id == 0x14; }   // "Mine Thrower", "M. Thrower E" (52AC50 names)
inline uint8_t AttrType(const Tables& t, uint8_t id) { return id < kAttributeIds ? t.attributes[id * 4] : 0; }
inline uint8_t Capacity(const Tables& t, uint8_t id) { return id < kAttributeIds ? t.attributes[id * 4 + 1] : 0; }
// 4DCBC0: the reload row of a weapon -> its ammo id, -1 none
inline int ReloadAmmo(const Tables& t, uint8_t weapon) {
    if (!t.reload) return -1;
    for (unsigned i = 0; i < kReloadRowsMax && t.reload[i * 4] != 0xff; ++i) if (t.reload[i * 4] == weapon) return t.reload[i * 4 + 1];
    return -1;
}
// the inventory load row {0, weapon, ammo} of a weapon (grenade launcher: the round type already in it), -1 none
inline int CombineAmmo(const Tables& t, uint8_t weapon) {
    if (!t.combine) return -1;
    for (unsigned i = 0; i < kCombineRowsMax && t.combine[i * 8 + 1] != 0xff; ++i) {
        const uint8_t* row = t.combine + i * 8;
        if (row[0] == 0 && row[1] == weapon && row[2] != weapon && AttrType(t, row[2]) == 2) return row[2];
    }
    return -1;
}
// 4DE9D0(id, 0): the first slot holding id, 0xff none
inline uint8_t FirstSlot(const uint8_t* inv, uint8_t id) {
    const unsigned slots = inv[0x12a] < kSlotsMax ? inv[0x12a] : kSlotsMax;
    for (unsigned i = 0; i < slots; ++i) if (inv[i * 4] == id) return uint8_t(i);
    return 0xff;
}
inline Plan Decide(const Tables& t, const uint8_t* inv) {
    Plan p;
    if (!inv || !t.attributes) return p;
    const uint8_t slot = inv[0x128], id = inv[0x129];
    if (slot == 0xff || slot >= inv[0x12a] || slot >= kSlotsMax || inv[slot * 4] != id || !id || AttrType(t, id) != 1) { p.refuse = Refuse::NoWeapon; return p; }
    p.weapon = id; p.slot = slot; p.loaded = inv[slot * 4 + 1]; p.capacity = Capacity(t, id);
    std::memcpy(&p.flags, inv + slot * 4 + 2, 2);
    if (IsMineThrower(id)) { p.refuse = Refuse::MineThrower; return p; }
    if ((p.flags & 3) == 3) { p.refuse = Refuse::Infinite; return p; }
    int ammo = ReloadAmmo(t, id);
    if (ammo >= 0) p.kind = Kind::Animated;
    else if ((ammo = CombineAmmo(t, id)) >= 0) p.kind = Kind::Instant;
    else { p.refuse = Refuse::NotReloadable; return p; }
    p.ammo = uint8_t(ammo);
    if (p.loaded >= p.capacity) { p.refuse = Refuse::Full; return p; }
    p.ammoSlot = FirstSlot(inv, p.ammo);
    if (p.ammoSlot == 0xff || !inv[p.ammoSlot * 4 + 1]) { p.refuse = Refuse::NoAmmo; return p; }
    p.stack = inv[p.ammoSlot * 4 + 1];
    const unsigned room = unsigned(p.capacity) - p.loaded;
    p.take = uint8_t(room < p.stack ? room : p.stack);   // 4DEBD4..4DEBE7
    p.refuse = Refuse::Ok;
    return p;
}
// Models of the native helpers for the offline test (the runtime calls the game's own).
inline void RemoveSlot(uint8_t* inv, uint8_t slot) {   // 4DEA60
    const unsigned slots = inv[0x12a];
    unsigned i = slot;
    for (; i + 1 < slots; ++i) std::memcpy(inv + i * 4, inv + (i + 1) * 4, 4);
    std::memset(inv + i * 4, 0, 4);
    if (inv[0x128] != 0xff && inv[0x128] >= slot) --inv[0x128];
}
inline uint8_t TakeFromSlot(uint8_t* inv, uint8_t slot, uint8_t n) {   // 4DEA10(slot, n, 0)
    uint8_t* s = inv + slot * 4;
    if ((s[2] & 3) != 3) s[1] = uint8_t(s[1] - n);
    if (!s[1]) { RemoveSlot(inv, slot); return 0; }
    return s[1];
}
inline uint8_t NativeReload(const Tables& t, uint8_t* inv) {   // 4DEB60(1): what the R event of animation 7 does
    if (inv[0x128] == 0xff) return 0xff;
    const int ammo = ReloadAmmo(t, inv[0x129]); if (ammo < 0) return 0xff;
    const uint8_t from = FirstSlot(inv, uint8_t(ammo)); if (from == 0xff) return 0xff;
    uint8_t* w = inv + inv[0x128] * 4;
    uint8_t take = uint8_t(Capacity(t, w[0]) - w[1]);
    if (take >= inv[from * 4 + 1]) take = inv[from * 4 + 1];
    w[1] = uint8_t(w[1] + take);
    TakeFromSlot(inv, from, take);
    return 0;
}
inline void InstantReload(uint8_t* inv, const Plan& p) {   // the grenade launcher path below, with TakeFromSlot = 4DEA10
    inv[p.slot * 4 + 1] = uint8_t(inv[p.slot * 4 + 1] + p.take);
    TakeFromSlot(inv, p.ammoSlot, p.take);
}

// Code signatures (ResidentEvil3.exe RVA, bytes; ">rva" = rel32 to that RVA, "??" = any; glock18c.h operand patches masked)
struct Sig { uint32_t rva; const char* bytes; };
struct PtrSig { uint32_t rva, target; };   // dword at rva (data, same RVA in both exes) == address of code target
enum SigGroup : unsigned { kCore = 1, kIdle = 2, kLauncher = 4 };
struct GroupSig { unsigned group; Sig sig; };
inline const GroupSig kSigs[] = {
    {kCore, {0xdeb60, "51 a1 e4 23 a6 00 80 b8 28 01 00 00 ff 75 04 0c ff 59 c3 8a 80 29 01 00 00 50 68 e0 5e a3 00 e8 >dcbc0 83 c4 08 83 f8 ff 75 04 0a c0 59 c3 8a 48 01 6a 00 51 e8 >de9d0"}},   // 4DEB60
    {kCore, {0xdebc4, "8d 34 91 8d 14 81 33 c0 8a 06 8a 4e 01 8a 52 01 8a 04 85 51 ac 52 00 2a c1 3a c2 88 44 24 0c"}},   // capacity - loaded vs stack
    {kCore, {0xdebfc, "e8 >dea10 83 c4 0c 80 0d c7 cc a5 00 04"}},   // 4DEA10(stack, take, 0)
    {kCore, {0xdcbc0, "8a 0d ?? ?? ?? ?? b8 ?? ?? ?? ?? 80 f9 ff 74 13 8a 54 24 08 3a ca 74 0e"}},   // 4DCBC0 reload row lookup
    {kCore, {0xde9d0, "8a 44 24 08 56 8b 35 e4 23 a6 00 57 88 44 24 10"}},   // 4DE9D0 first slot
    {kCore, {0xdea10, "8b 44 24 04 8b 15 e4 23 a6 00 8b c8 56 81 e1 ff 00 00 00 8d 34 8a 8a 4c 8a 02 80 e1 03 80 f9 03 74 0c 8a 54 24 0c 8a 4e 01 2a ca 88 4e 01 8a 4e 01 84 c9 77 11 8a 4c 24 10 84 c9 75 09 50 e8 >dea60"}},   // 4DEA10
    {kCore, {0x647d3, "f6 05 68 c8 a5 00 40 74 34 6a 00 e8 >deb60 83 c4 04 84 c0 75 08 66 c7 46 06 04 00"}},   // aim: empty -> state 4
    {kCore, {0x64aa0, "56 8b 74 24 08 8a 46 07 84 c0 75 15 b8 07 00 07 00 c6 46 07 01 89 86 c8 00 00 00 89 86 a0 01 00 00"}},   // state 4 anim 7
    {kCore, {0x64ba2, "8b 96 e4 00 00 00 f6 42 01 02 74 11 6a 01 e8 >deb60 83 c4 04 80 8e d3 00 00 00 04"}},   // R event
    {kCore, {0x657a0, "56 8b 74 24 08 8a 46 07 84 c0 75 15 b8 0a 00 07 00 c6 46 07 01 89 86 c8 00 00 00 89 86 a0 01 00 00"}},   // 0F anim 0A
    {kCore, {0x65877, "8b 8e e4 00 00 00 f6 41 01 02 74 11 6a 01 e8 >deb60 83 c4 04 80 8e d3 00 00 00 04"}},   // 0F R event
    {kCore, {0x63cf4, "a1 5c 1f a6 00 8b 4c 24 10 0d 40 01 00 00 a3 5c 1f a6 00"}},   // weapon routine: A61F5C |= 0x140
    {kCore, {0x2b7a9, "6a 01 e8 >2b0c0 83 c4 04 85 c0 0f 85 >2b8ea a1 80 c8 a5 00 85 c3 0f 85 >2b8ea 66 0f b6 8e 8e 24 00 00 66 39 8e 4a 78 00 00 0f 85 >2b8ea 8a 0d b6 28 a6 00 84 c9 0f 85 >2b8ea 8a 8e c1 25 00 00 84 c9 0f 85 >2b8ea 8a 0d a1 87 a5 00 84 c9 0f 85 >2b8ea 8a 0d a0 87 a5 00 84 c9 0f 85 >2b8ea f6 05 db cf a4 00 0e 0f 85 >2b8ea f6 05 68 74 a5 00 80 0f 85 >2b8ea f7 05 d0 cb a5 00 00 00 00 01 0f 85 >2b8ea a9 00 00 03 00 0f 85 >2b8ea a1 5c 1f a6 00 a9 08 01 00 00 0f 85 >2b8ea"}},   // status-screen guards
    {kIdle, {0x6000f, "f6 c5 01 53 ba 01 05 00 00 74 25 8a 58 46 84 db 74 1e 8b 18 89 50 04"}},   // stand: aim -> dword [+4] = 0x501
    {kLauncher, {0xdcb50, "8a 0d ?? ?? ?? ?? 53 80 f9 ff b8 ?? ?? ?? ??"}},   // 4DCB50 combine lookup (table operand at +0B)
    {kLauncher, {0x64b17, "8b 8e e4 00 00 00 83 c4 18 66 8b 01 f6 c4 f0 74 28 8b 96 08 01 00 00 25 ff ff 00 00 c1 e8 0c 81 c2 44 03 00 00 48 6a 00 6a 00 0d 00 01 01 00 52 50 e8 >27b20"}},   // frame sound call form
    {kLauncher, {0x27b20, "8b 0d 94 9d a5 00 81 f9 94 9d a5 00 0f 84 >27bee"}},   // 427B20
};
inline const PtrSig kPtrSigs[] = {{0x12a518, 0x64aa0}, {0x12a544, 0x657a0}};   // 52A508[4], 52A534[4]: the reload states
// Matches one signature; at = the bytes, siteVa = their address in the process (or the image VA in a file test),
// vaOf(rva) = the address the running exe has for a ResidentEvil3.exe RVA.
template<class VaOf> inline bool Match(const Sig& s, const uint8_t* at, uintptr_t siteVa, VaOf vaOf) {
    const char* p = s.bytes; size_t k = 0;
    auto hex = [](char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1; };
    while (*p) {
        while (*p == ' ') ++p;
        if (!*p) break;
        if (*p == '>') {
            ++p; uint32_t rva = 0; int d;
            while ((d = hex(*p)) >= 0) { rva = rva * 16 + uint32_t(d); ++p; }
            int32_t rel; std::memcpy(&rel, at + k, 4);
            if (siteVa + k + 4 + uintptr_t(intptr_t(rel)) != vaOf(rva)) return false;
            k += 4; continue;
        }
        if (p[0] == '?' && p[1] == '?') { p += 2; ++k; continue; }
        const int hi = hex(p[0]), lo = hex(p[1]);
        if (hi < 0 || lo < 0 || at[k] != uint8_t(hi * 16 + lo)) return false;
        p += 2; ++k;
    }
    return true;
}
inline size_t SigLength(const Sig& s) {
    size_t n = 0;
    for (const char* p = s.bytes; *p;) {
        while (*p == ' ') ++p;
        if (!*p) break;
        if (*p == '>') { ++p; while ((*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'f')) ++p; n += 4; } else { p += 2; ++n; }
    }
    return n;
}
}  // namespace logic

#ifndef RE3RELOAD_LOGIC_ONLY
// ---------------------------------------------------------------- runtime (game thread)
using LogFn = void (*)(const char*, ...);
enum Source : LONG { kNone = 0, kKey = 1, kCommand = 2, kApi = 3 };
inline volatile LONG requestSource = kNone;
// pilot.ini reload_key, read at the first poll (-1 = not yet: the key is not taken). A key binding of the F5 mod menu can
// overwrite it after RELOAD_INIT (0 = no keyboard key here) and call Request() for its own keys / pad buttons.
inline int reloadKey = -1;
// The reload action (keyboard key below, the F5 mod menu bindings, pilot.command reload). Game thread or window procedure.
inline void Request(LONG source = kApi) { InterlockedExchange(&requestSource, source); }
// pilotWndProc, WM_KEYDOWN: true = our key (swallowed)
inline bool OnKeyDown(WPARAM wp, LPARAM lp) {
    if (reloadKey <= 0 || wp != static_cast<WPARAM>(reloadKey)) return false;
    if (!(lp & (1l << 30))) Request(kKey);
    return true;
}
inline const char* SourceName(LONG s) { return s == kKey ? "key" : s == kCommand ? "command" : "menu"; }

namespace detail {
inline bool Accessible(const void* p, size_t bytes, bool writable = false) {
    uintptr_t at = reinterpret_cast<uintptr_t>(p);
    if (!at || bytes > UINTPTR_MAX - at) return false;
    const uintptr_t end = at + bytes;
    while (at < end) {
        MEMORY_BASIC_INFORMATION info{};
        if (!VirtualQuery(reinterpret_cast<void*>(at), &info, sizeof(info)) || info.State != MEM_COMMIT ||
            (info.Protect & (PAGE_GUARD | PAGE_NOACCESS))) return false;
        const DWORD access = info.Protect & 0xff;
        if (writable && access != PAGE_READWRITE && access != PAGE_WRITECOPY && access != PAGE_EXECUTE_READWRITE &&
            access != PAGE_EXECUTE_WRITECOPY) return false;
        const uintptr_t next = reinterpret_cast<uintptr_t>(info.BaseAddress) + info.RegionSize;
        if (next <= at) return false;
        at = next < end ? next : end;
    }
    return true;
}
// data (same RVA in both exes): VA of ResidentEvil3.exe -> address
inline uint8_t* Data(uintptr_t base, uint32_t va) { return reinterpret_cast<uint8_t*>(base + (va - 0x400000)); }
inline uint32_t D(uintptr_t b, uint32_t va) { uint32_t v; std::memcpy(&v, Data(b, va), 4); return v; }
inline uint16_t W(uintptr_t b, uint32_t va) { uint16_t v; std::memcpy(&v, Data(b, va), 2); return v; }
inline uint8_t B(uintptr_t b, uint32_t va) { return *Data(b, va); }
// The native status-screen guards 42B7A9..42B85A (+ the exclusions of re3options::Blocked) without A61F5C bit 0x100,
// which the weapon routine itself sets every frame (463CFD). nullptr = the game runs free gameplay.
inline const char* GameBlocked(uintptr_t b) {
    if (W(b, 0xa591a4)) return "task slot 1 busy";                                   // 42B0C0(1)
    // fix 2026-11-11b (user: "the tactical reload does not work in Mercenaries"): the mask came from the options request
    // (options_menu.h) with its bit 80h "special scenario" - that is the Mercenaries flag, set for the whole of RE3_MERCE.exe
    // (title 20000080h, gameplay 08001080h / 28001080h in the user's log; widescreen.h s.merc), so every request was refused there
    if (D(b, 0xa5c880) & 0x115b0400u) return "A5C880 busy (demo/overlay/door/options/save/movie/ui)";
    if (D(b, 0xa61f5c) & 0x10400208u) return "A61F5C busy (event/loading/inventory)";
    if (W(b, 0xa61faa) != B(b, 0xa5cbee)) return "camera change";                     // [A5A760+784A] vs [A5A760+248E]
    if (B(b, 0xa628b6)) return "camera transition";
    if (B(b, 0xa5cd21)) return "hit cooldown (player+12D)";                          // [A5A760+25C1]
    if (B(b, 0xa587a1) || B(b, 0xa587a0)) return "controller or room request busy";
    if (B(b, 0xa4cfdb) & 0x0e) return "A4CFDB busy";
    if (B(b, 0xa57468) & 0x80) return "message text";
    if (D(b, 0xa5cbd0) & 0xff000000u) return "update domains masked (inventory/menu)";
    if (D(b, 0xa61d44) || D(b, 0xa61d48)) return "interaction/dialog controller";
    if (B(b, 0xa628c4)) return "fade";
    if (D(b, 0xa5cbd4) & 0x40000000u) return "diagnostic pause";
    return nullptr;
}
}  // namespace detail

struct Runtime {
    enum Phase { Idle, Waiting, Raising, Reloading };
    bool checked = false, ready = false, idleOk = false, launcherOk = false;
    bool enabled = true, fromIdle = true, launcherSound = true;
    unsigned waitFrames = 12, raiseFrames = 60, logged = 0;
    Phase phase = Idle; LONG source = kNone; unsigned frames = 0;
    logic::Plan plan; const char* how = "";
    uint8_t soundDelay = 0, soundWeapon = 0;   // grenade launcher: the second click S5 this many polls later, if still in hand
    uintptr_t base = 0;

    logic::Tables Tables() const {
        logic::Tables t{detail::Data(base, 0x52ac50), nullptr, nullptr};
        uint32_t reload = 0, combine = 0;
        std::memcpy(&reload, reinterpret_cast<const void*>(re3exe::Base(base) + 0xdcbc7), 4);   // mov eax, table (4DCBC6)
        if (detail::Accessible(reinterpret_cast<void*>(uintptr_t(reload)), logic::kReloadRowsMax * 4)) t.reload = reinterpret_cast<const uint8_t*>(uintptr_t(reload));
        if (launcherOk) {
            std::memcpy(&combine, reinterpret_cast<const void*>(re3exe::Base(base) + 0xdcb5b), 4);  // mov eax, table (4DCB5A)
            if (detail::Accessible(reinterpret_cast<void*>(uintptr_t(combine)), 8)) t.combine = reinterpret_cast<const uint8_t*>(uintptr_t(combine));
        }
        return t;
    }
    uint8_t* Inventory() const {
        uint8_t* inv = nullptr; std::memcpy(&inv, detail::Data(base, 0xa623e4), 4);
        return detail::Accessible(inv, 0x12b, true) ? inv : nullptr;
    }
    static uint8_t* Player(uintptr_t b) { return detail::Data(b, 0xa5cbf4); }
    bool Verify(unsigned group, LogFn log) const {
        const re3exe::Base gb(base); bool ok = true;
        auto vaOf = [&](uint32_t rva) { return uintptr_t(gb + rva); };
        for (const auto& g : logic::kSigs) {
            if (!(g.group & group)) continue;
            const uintptr_t at = gb + g.sig.rva;
            const auto* bytes = reinterpret_cast<const uint8_t*>(at);
            if (!detail::Accessible(bytes, logic::SigLength(g.sig)) || !logic::Match(g.sig, bytes, at, vaOf)) {
                log("RELOAD_SIGNATURE mismatch at %x (group %u)", g.sig.rva, g.group); ok = false;
            }
        }
        if (group & logic::kCore)
            for (const auto& s : logic::kPtrSigs) {
                uint32_t v = 0; std::memcpy(&v, detail::Data(base, 0x400000 + s.rva), 4);
                if (v != uint32_t(gb + s.target)) { log("RELOAD_SIGNATURE mismatch: %x -> %08x, expected the state %x", s.rva, v, s.target); ok = false; }
            }
        return ok;
    }
    void Init(uintptr_t b, LogFn log, const std::string& rootDir) {
        checked = true; base = b;
        const std::string ini = rootDir + "\\pilot.ini";
        auto get = [&](const char* key, int def) { return int(GetPrivateProfileIntA("pilot", key, def, ini.c_str())); };
        enabled = get("reload_enabled", 1) != 0;
        fromIdle = get("reload_from_idle", 1) != 0;
        launcherSound = get("reload_gl_sound", 1) != 0;
        const int wait = get("reload_wait_frames", 12); waitFrames = unsigned(wait < 1 ? 1 : wait > 60 ? 60 : wait);
        const char* exe = re3exe::gRunning == re3exe::Exe::Merce ? "RE3_MERCE" : "ResidentEvil3";
        if (!enabled) { reloadKey = 0; log("RELOAD_INIT off (pilot.ini reload_enabled=0) exe=%s", exe); return; }
        ready = Verify(logic::kCore, log);
        idleOk = ready && Verify(logic::kIdle, log);
        launcherOk = ready && Verify(logic::kLauncher, log);
        reloadKey = ready ? get("reload_key", 0x52) : 0;
        if (reloadKey < 0 || reloadKey > 0xfe) reloadKey = 0;
        const logic::Tables t = Tables();
        log("RELOAD_INIT %s exe=%s%s key=%02x from_idle=%u(%s) wait=%u launcher=%s sound=%u reload_table=%p combine_table=%p "
            "(native state 4 = 464AA0/4657A0, check 4DEB60(0), Mine Thrower 0C/14 never)",
            ready ? "ready" : "OFF: code signature mismatch", exe, re3exe::gSteam ? " (Steam)" : "", reloadKey, unsigned(fromIdle),
            idleOk ? "ok" : "off", waitFrames, launcherOk ? "instant" : "off", unsigned(launcherSound), static_cast<const void*>(t.reload),
            static_cast<const void*>(t.combine));
    }
    void Say(LogFn log, const char* fmt, ...) {
        if (logged >= 400) return;
        if (++logged == 400) { log("RELOAD log limit reached (400 lines)"); return; }
        char line[512]; va_list args; va_start(args, fmt); _vsnprintf_s(line, sizeof(line), _TRUNCATE, fmt, args); va_end(args);
        log("%s", line);
    }
    void Refuse(LogFn log, const char* why, const uint8_t* player) {
        Say(log, "RELOAD refused (%s): %s; weapon %02x loaded %u/%u ammo %02x stack %u; player %u/%u/%u/%u", SourceName(source), why,
            plan.weapon, plan.loaded, plan.capacity, plan.ammo, plan.stack, player[4], player[5], player[6], player[7]);
        phase = Idle;
    }
    void LauncherClick(uint8_t* player, uint32_t sound) const {
        uint8_t* parts = nullptr; std::memcpy(&parts, player + 0x108, 4);
        if (!detail::Accessible(parts, 0x354)) return;
        // as the frame events of the reload/shot animations do it (464B48): 427B20(0x10100 | n-1, parts + 0x344, 0, 0)
        reinterpret_cast<void(__cdecl*)(uint32_t, void*, int, int)>(re3exe::Base(base) + 0x27b20)(sound, parts + 0x344, 0, 0);
    }
    void Instant(LogFn log, uint8_t* inv, uint8_t* player, const char* where) {
        const uint8_t loaded = inv[plan.slot * 4 + 1];
        inv[plan.slot * 4 + 1] = uint8_t(loaded + plan.take);
        // the native 4DEA10(slot, n, 0): count -= n unless infinite, 0 -> 4DEA60 removes the stack (later slots move down)
        const int left = reinterpret_cast<int(__cdecl*)(int, int, int)>(re3exe::Base(base) + 0xdea10)(plan.ammoSlot, plan.take, 0) & 0xff;
        if (launcherSound) { LauncherClick(player, 0x10103); soundDelay = 10; soundWeapon = plan.weapon; }
        Say(log, "RELOAD done (%s): launcher %02x loaded %u -> %u / %u, rounds %02x stack %u -> %u (slot %u), instant (no native animation), %s",
            SourceName(source), plan.weapon, loaded, inv[inv[0x128] * 4 + 1], plan.capacity, plan.ammo, plan.stack, left, plan.ammoSlot, where);
        phase = Idle;
    }
    void StartAnimated(LogFn log, uint8_t* player, const char* where) {
        // the native check (reload row + a stack of the ammo) must agree: the state-4 R event uses the same function
        const int native = reinterpret_cast<int(__cdecl*)(int)>(re3exe::Base(base) + 0xdeb60)(0) & 0xff;
        if (native) { Refuse(log, "native check 4DEB60(0) says no", player); return; }
        const uint16_t reload = 4; std::memcpy(player + 6, &reload, 2);   // what the aim state does at 4647EA
        phase = Reloading; frames = 0; how = where;
    }
    void Track(LogFn log, uint8_t* inv, uint8_t* player) {
        const uint8_t slot = inv[0x128];
        const bool same = slot != 0xff && slot < inv[0x12a] && inv[0x129] == plan.weapon && inv[slot * 4] == plan.weapon;
        const uint8_t now = same ? inv[slot * 4 + 1] : 0;
        const bool inState = player[4] == 1 && player[5] == 5 && player[6] == 4;
        if (same && now != plan.loaded) {
            const uint8_t stackSlot = logic::FirstSlot(inv, plan.ammo);
            Say(log, "RELOAD done (%s): weapon %02x (player+46 %02x) loaded %u -> %u / %u, ammo %02x stack %u -> %u%s, native reload state 4, %s",
                SourceName(source), plan.weapon, player[0x46], plan.loaded, now, plan.capacity, plan.ammo, plan.stack,
                stackSlot == 0xff ? 0u : unsigned(inv[stackSlot * 4 + 1]), stackSlot == 0xff ? " (used up)" : "", how);
            phase = Idle; return;
        }
        if (!same || !inState || ++frames > 150) {
            Say(log, "RELOAD interrupted (%s): weapon %02x loaded %u unchanged, player %u/%u/%u/%u after %u polls%s", SourceName(source),
                plan.weapon, now, player[4], player[5], player[6], player[7], frames, same ? "" : " (weapon changed)");
            phase = Idle;
        }
    }
    void Serve(LogFn log, uint8_t* inv, uint8_t* player) {
        plan = logic::Decide(Tables(), inv);
        if (plan.refuse != logic::Refuse::Ok) { Refuse(log, logic::RefuseName(plan.refuse), player); return; }
        // the animation is player+46's, the arithmetic the equipped item's (4DEB60 reads +129): they must be the same gun
        // (the Glock items 53/54 are in hand as the M92F 03, glock18c.h alias)
        const uint8_t hand = player[0x46];
        if (hand != plan.weapon && !((plan.weapon == 0x53 || plan.weapon == 0x54) && hand == 3)) {
            Refuse(log, "the weapon in hand (player+46) is not the equipped item", player); return;
        }
        const unsigned limit = phase == Raising ? raiseFrames : waitFrames;
        const char* busy = detail::GameBlocked(base);
        uint32_t flags0 = 0; std::memcpy(&flags0, player, 4);
        if (!busy) {
            const uint8_t routine = player[4], sub = player[5], state = player[6];
            if (player[8] < 15 || (player[0x47] & 0x80) || !player[0x46]) busy = "player not active";
            else if (routine != 1) busy = "player routine (damage/death/event)";
            else if (sub == 5) {
                if (state == 1 || state == 3) {   // aiming, or lowering the gun: the reload state takes over
                    if (plan.kind == logic::Kind::Instant) Instant(log, inv, player, state == 1 ? "aiming" : "lowering");
                    else StartAnimated(log, player, phase == Raising ? "raised for it" : state == 1 ? "from aim" : "from lowering");
                    return;
                }
                if (state == 4) { Refuse(log, "a reload is already running", player); return; }
                busy = state == 0 ? "raising the gun" : state == 2 ? "shot in progress" : "weapon state busy";
            } else if (phase == Raising) {
                Refuse(log, "the raise was interrupted", player); return;
            } else if (sub <= 4) {   // stand / walk / run / back / turn
                if (plan.kind == logic::Kind::Instant) { Instant(log, inv, player, "not aiming"); return; }
                if (!fromIdle || !idleOk) busy = "not aiming (reload_from_idle=0)";
                // +0 bit 31: the stand/walk controllers and the raise (464370, first 5 frames) take the 0x20701 move instead
                else if (flags0 & 0x80000000u) busy = "player +0 bit 31 (an aim press would not raise the gun now)";
                else {   // the aim press of the stand/walk/run/back/turn controllers: raise, then state 1 -> 4 above
                    const uint32_t raise = 0x501; std::memcpy(player + 4, &raise, 4);
                    phase = Raising; frames = 0; return;
                }
            } else busy = "player sub-routine (stairs/quick turn/push/...)";
        }
        if (++frames > limit) Refuse(log, busy ? busy : "busy", player);
    }
    void Tick(uintptr_t b, LogFn log, const std::string& rootDir) {
        if (!checked) Init(b, log, rootDir);
        const LONG request = InterlockedExchange(&requestSource, kNone);
        if (!ready) {
            if (request && logged < 8) { ++logged; log("RELOAD refused (%s): feature off (see RELOAD_INIT)", SourceName(request)); }
            return;
        }
        uint8_t* player = Player(base);
        uint8_t* inv = Inventory();
        if (soundDelay && --soundDelay == 0 && launcherSound && inv && inv[0x129] == soundWeapon && !detail::GameBlocked(base)) LauncherClick(player, 0x10104);
        if (request) {
            if (phase == Idle) { phase = Waiting; frames = 0; source = request; }
            else Say(log, "RELOAD request (%s) ignored: one in progress (phase %d)", SourceName(request), int(phase));
        }
        if (phase == Idle) return;
        if (!inv) { plan = logic::Plan{}; Refuse(log, "no inventory", player); return; }
        if (phase == Reloading) Track(log, inv, player);
        else Serve(log, inv, player);
    }
};
inline Runtime runtime;
// mod_hooks.h, after the input poll (game thread)
inline void AfterInputPoll(const re3exe::Base& base, LogFn log, const std::string& rootDir) { runtime.Tick(base, log, rootDir); }
#endif  // RE3RELOAD_LOGIC_ONLY

}  // namespace re3reload
