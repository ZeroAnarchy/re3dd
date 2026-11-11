#pragma once
// Controls round 2026-11-09 (Glock dodge rate, modern controls, manual reload, F5 mod menu, player movies, widescreen
// crop): ONE production hook right after the game's input poll 42CAD0 (calls at 4238C6 and 42FB60). Before this round
// the hook was installed only with debug_warp=1; now it is installed for every run, the diagnostic part stays gated.
// At this point the poll has just written the pad words: 65C858 raw PSX bits held, 65C85C/65C860 raw pressed,
// 65C864 game action bits held (1 forward, 2 turn right, 4 backward, 8 turn left, 0x40 fire, 0x80 action, 0x100 aim ...),
// 65C868 action bits pressed this frame (raw -> action table 517DAC + key type [6628BD] * 32).
// Each feature adds ONE call below in this order: menu (may swallow all input), reload, modern controls, widescreen.
#include <cstdint>
#include "exe_map.h"
#include "controls_modern.h" // controls round 2026-11-09: modern (camera-relative) controls

namespace re3menu { void AfterInputPoll(uintptr_t base); }   // menu: mod_menu.h (included later by proxy.cpp)
namespace re3modhooks {
inline void AfterInputPoll(const re3exe::Base& base) {
    re3menu::AfterInputPoll(base);   // menu (F5 / Start): first, may clear this frame's game input
    (void)base;
    re3reload::AfterInputPoll(base, log, root);   // reload (reload.h, controls round 2026-11-09)
    re3modern::Tick(base, root, log);   // modern controls (controls_modern.h): pilot.ini control_scheme=1, no-op for 0
}
}  // namespace re3modhooks
