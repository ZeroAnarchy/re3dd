#pragma once
// Controls round 2026-11-09 (menu agent): the 3DD menu in the game. Report: runtime/staging/ctl-2026-11-09/menu/REPORT.ru.txt.
// Included by proxy.cpp after fps60_integration.h / glock18c.h (uses log, root, gameBase, gameDevice, gogWrapper, mutex,
// QuadState, keepDeviceHooks, gpuFlushPending, desiredEnabled, toggleRequested, fps60*, model*, re3extra::*, sfxhits,
// moviePlaying, re3options). Declared early in proxy.cpp (pilotWndProc, releasePilotDeviceResources) and mod_hooks.h.
//
// Open / close: pilot.ini options_key (VK, default F5) or menu_pad (default "57,128" = Start under the GOG pad layout);
// the request is taken in AfterInputPoll (first call in mod_hooks.h: that frame's game input is cleared) and the menu runs
// at the frame's flip (call 40BC84 -> 410FE0, chained before fps60's hook): the finished frame is copied (StretchRect of
// render target 0), then a modal loop on the game thread draws it + the menu and flips through the native flip until the
// menu closes. The game is frozen like with its own modal "Setting Up" dialog (sound keeps playing); afterwards the
// virtual vblank clock is re-based (system object [5349BC] vt+0C = 420950 -> 4209E0) so the game does not run the missed vblanks,
// and the game input stays cleared until every key / button is released (no phantom presses). Refused while a movie plays.
// Keys in the loop: window messages (WM_KEYDOWN; the scan code = the game's DirectInput code); pad: the game's own
// DirectInput wrapper object ([input+1424], vtable 50A7E4: +50 poll joysticks 407A90, +24 value at a DIJOYSTATE offset
// 407D40, +30 DIDFT type 407E70).
// Bindings: the application object (vtable 50A6B0, list [534988]/+2C, hwnd +28) tables +2B28 / +2EA8 (mod_input.h) are
// edited in place and applied / saved with the game's own functions, as its Setting Up dialog does on OK (404C83..404CA4):
// 405810 keyboard -> input object, 405760 save [Keyboard]; 405A20 pad -> input object, 405940 save [Gamepad] (bio3.ini
// next to the exe, ".\bio3.ini"). Mod keys (pilot.ini): options_key/menu_pad, hd_key/hd_pad (F8), fps60_key/fps60_pad (F7),
// reload_key/reload_pad (R / Y) -> re3menu::onReload (wired at merge to re3reload::Request).
// One-time pad layout change (pilot.ini pad_layout=2 afterwards): the game's [Gamepad] KeyB (PS Select, raw 0100) on Start
// moves to Back (button 8, "56,128"); Start opens this menu. GOG dinput.dll (goginput.ini onelayout, HID remaps per VID/PID):
// Xbox 360/One/Series Start (HID 7) and DualSense/DualShock 4 Options (HID 9) are unified button 9 = DIJOYSTATE offset 57.
// Fix 2026-11-11b (pad_layout=3): Back / Share toggles the HD graphics by default (hd_pad "56,128"); KeyB leaves Back once
// (mod_input.h BackToHd) and has no pad button by default any more.
// Mod pad buttons that a [Gamepad] action also uses: a default one (no pilot.ini line) is off for the run, logged.
// Keyboard rows left empty: the loader 405590 would give them the exe default again -> pilot.ini menu_keys_cleared (bits
// Key1..KeyE), emptied again at startup while the bio3.ini line is still empty. Pad edits need a pad (405860 loads
// [<pad name>] only with one). Other modules: see the merge points below (all null = no-ops in this tree).
// pilot.ini menu=0: no 3DD menu, options_key opens the game's Setting Up window as in fx 2026-11-08.
// RE3_MERCE.exe: the same code through exe_map.h (all signatures verified on disk by qa/.../menu/tools/menu_tests.cpp).
// Round 7 (2026-11-10): the rows "Exit to title screen" and "Exit to desktop" of the first page. Both are the game's own
// paths (mod_menu_exit.h: the listings and the byte checks; proxy.cpp tells which call polled the input, re3menu::pollReturn):
//  - title = the game's soft reset: bit 200000h of [A5C880], set at the game loop's input poll (4238C6), where the game
//    tests it and runs 424B50 - as it does itself after a game over and when the title's attract demo ends;
//  - desktop = WM_CLOSE posted to the game window = what "Yes" of the game's quit box sends (509620 -> 404892
//    DestroyWindow -> 4088A0 PostQuitMessage), handled by the window thread at the game's next own yield.
// A row is carried out after the menu loop has ended (ExitTick), never from inside it; one log line MENU_EXIT each.
#include "mod_menu_ui.h"
#include "mod_menu_exit.h"   // round 7

namespace re3menu {
// ---- merge points: wired by the orchestrator (other agents' modules), all null in this tree = harmless no-ops.
// An option row whose setter is null is saved to pilot.ini and marked "after restart".
inline void (*onReload)() = nullptr;                  // re3reload::Request (the "Reload weapon" key / pad button)
inline void (*onReloadKeyChanged)(int vk) = nullptr;  // re3reload::reloadKey = vk: the menu owns pilot.ini reload_key; keeps the
                                                      // reload module from still catching the old key after a rebind
inline void (*onInputSuspended)(bool on) = nullptr;   // re3modern::SetSuspended: true from the open request until every key /
                                                      // button used in the menu is released after it (the stick is read directly)
inline void (*onMenuFrame)() = nullptr;               // not called since round 6 (the menu no longer forces the 4:3 frame)
// round 6: re3widescreen::MenuView - the crop window shown now (top row and row count of the 240-row frame) = where the
// menu is laid out; false = the whole 4:3 frame is shown
inline bool (*onMenuView)(float& top, float& rows) = nullptr;
inline void (*onSetControlScheme)(int) = nullptr;     // re3modern::SetScheme (pilot.ini control_scheme 0 classic, 1 modern)
inline void (*onSetModernStickRun)(int) = nullptr;    // optional, modern_stick_run (0/1); null = after restart
inline void (*onSetWidescreen)(int) = nullptr;        // re3widescreen::SetMode (pilot.ini widescreen 0 off, 1 crop centred, 2 follow)
inline void (*onSetWidescreenSmooth)(int) = nullptr;  // round 4: re3widescreen::SetSmooth (pilot.ini widescreen_smooth 0..3)
inline void (*onSetWidescreenSpeed)(int) = nullptr;   // round 5: re3widescreen::SetSpeed (pilot.ini widescreen_speed 0..4)
inline void (*onSetWidescreenDelay)(int) = nullptr;   // round 5: re3widescreen::SetDelay (pilot.ini widescreen_delay 0..5)
inline void (*onSetWidescreenPeek)(int) = nullptr;    // round 6: re3widescreen::SetPeek (pilot.ini widescreen_peek 0/1: right stick)
inline void (*onSetDodgeAlways)(int) = nullptr;       // round 6 (test row): glock::dodgeAlways (pilot.ini glock_dodge_always 0/1)
inline void (*onSetPlayerMovies)(int) = nullptr;      // player_movies (0/1): the movie module's enabled flag; null = after restart
inline void (*onSetGlockKneelBudget)(int) = nullptr;  // glock_kneel_budget (0/1): glock18c.h kneelBudget; null = after restart

namespace detail {
using ThisFn = void(__fastcall*)(void*, void*);
using DiwValueFn = int(__fastcall*)(void*, void*, int, int);   // thiscall (offset, joystick)
using FlipFn = int(__fastcall*)(void*, void*, int);            // 410FE0 thiscall (int) or fps60FlipHook

struct Config { bool loaded = false, enabled = true; int language = 0; ModBinding mod[kModActions]; bool explicitPad[kModActions]{}; };
inline Config cfg;
inline bool checked = false, ready = false, merce = false, modal = false, closeRequested = false, nativeAfterClose = false;
inline const char* closeReason = "";
inline uint8_t* app = nullptr; inline uint8_t* input = nullptr; inline uint8_t* diw = nullptr;
inline uint8_t* sys = nullptr;   // QA 2026-11-09: system object [5349BC] (vtable 50AC90): task yield, vblank clock
inline uintptr_t flipCall = 0; inline FlipFn flipNext = nullptr;
inline volatile LONG openRequest = 0, reloadRequest = 0;
inline const char* openSource = "";
inline int armed = 0; inline DWORD suppressUntil = 0; inline bool padPrev[kModActions]{};
inline IDirect3DTexture9* capture = nullptr; inline UINT capW = 0, capH = 0; inline D3DFORMAT capFmt = D3DFMT_UNKNOWN;
inline Gfx gfx; inline Menu menu;
inline int optRunning[O_Count]{}; inline int optValue[O_Count]{};
inline unsigned opens = 0;
inline std::string Ini() { return root + "\\pilot.ini"; }

inline void LoadConfig() {
    const std::string ini = Ini();
    cfg.enabled = GetPrivateProfileIntA("pilot", "menu", 1, ini.c_str()) != 0;
    cfg.language = int(GetPrivateProfileIntA("pilot", "menu_language", 0, ini.c_str()));
    for (int m = 0; m < kModActions; ++m) {
        cfg.mod[m].vk = int(GetPrivateProfileIntA("pilot", kMod[m].keyIni, UINT(kMod[m].vk), ini.c_str()));
        char text[64] = "";
        GetPrivateProfileStringA("pilot", kMod[m].padIni, "\x01", text, sizeof(text), ini.c_str());
        cfg.explicitPad[m] = !(text[0] == '\x01' && !text[1]);
        cfg.mod[m].pad = cfg.explicitPad[m] ? ParsePad(text) : kMod[m].pad;
    }
    cfg.loaded = true;
}
inline void EnsureConfig() { if (!cfg.loaded) LoadConfig(); }

// ---- game objects ----
// QA 2026-11-09 (first game run: "input object (vtable 50AC04) not found"): the list holds the application's WINDOW
// sub-object at app+4 (constructor 403690: [app] = 50A6BC, [app+4] = 50A6B0). The game's methods (403910, 405810, 405A20)
// take the object base: input at base+100 (403B29, = [5349C0] from 403E73), tables at base+2B28 / +2EA8. Return the base.
inline uint8_t* FindApp() {
    auto* p = *reinterpret_cast<uint8_t**>(gameBase + 0x134988);   // window object list (4104D0), nodes = window sub-objects
    for (int i = 0; p && i < 16; ++i, p = *reinterpret_cast<uint8_t**>(p + 0x2c))
        if (*reinterpret_cast<uintptr_t*>(p) == gameBase + 0x10a6b0 && *reinterpret_cast<uintptr_t*>(p - 4) == gameBase + 0x10a6bc)
            return p - 4;   // application base (vtables 50A6BC at +0, 50A6B0 at +4)
    return nullptr;
}
inline const char* VerifyCode() {
    for (const auto& s : kMenuCodeSigs) if (memcmp(reinterpret_cast<const void*>(gameBase + s.rva), s.bytes, s.n)) {
        static char why[64]; sprintf_s(why, "code %x differs", 0x400000 + s.rva); return why;
    }
    const uint8_t* call = reinterpret_cast<const uint8_t*>(gameBase + 0xbc84);   // 40BC84 call 410FE0 (flip) in 40B9C0
    if (call[0] != 0xe8) return "flip call 40BC84 differs";
    const uintptr_t target = reinterpret_cast<uintptr_t>(call + 5) + *reinterpret_cast<const int32_t*>(call + 1);
    if (target != gameBase + 0x10fe0 && target != reinterpret_cast<uintptr_t>(&fps60FlipHook)) return "flip call 40BC84 has a foreign target";
    return nullptr;
}
inline const char* FindObjects() {
    app = FindApp();
    if (!app) return "application object not found";
    input = *reinterpret_cast<uint8_t**>(app + 0x100);
    if (!input || *reinterpret_cast<uintptr_t*>(input) != gameBase + 0x10ac04) return "input object (vtable 50AC04) not found";
    if (input != *reinterpret_cast<uint8_t**>(gameBase + 0x1349c0)) return "input object differs from [5349C0]";   // 403E73 copies base+100 there
    diw = *reinterpret_cast<uint8_t**>(input + 0x1424);
    if (!diw || *reinterpret_cast<uintptr_t*>(diw) != gameBase + 0x10a7e4) return "DirectInput object (vtable 50A7E4) not found";
    // QA 2026-11-09 (game runs): the input vtable 50AC04 has only 35 slots (0..88); the next vtable 50AC90 belongs to the SYSTEM
    // object [5349BC] (main loop 410280 = +18, task switcher at +5BC, vblank clock +5A8..+5B8). The window belongs to the MAIN
    // thread; the game (and this menu loop at the flip) runs in task 1, and the main thread pumps the window's messages only
    // while the game task yields to it: system vt+1C = 410320 (current task -> +8C8, switch to task 0 = main loop, which
    // pumps and switches back to +8C8). Without it the keys never reach the open menu (window thread waits, "hung").
    // system vt+0C = 420950 -> 4209E0: vblank clock re-based (the menu agent called it as "input vt+98" = past the end of
    // 50AC04 with this = input, which would have written input+5A8.. and called through [input+4]).
    sys = *reinterpret_cast<uint8_t**>(gameBase + 0x1349bc);
    if (!sys || *reinterpret_cast<uintptr_t*>(sys) != gameBase + 0x10ac90) return "system object [5349BC] (vtable 50AC90) not found";
    if (reinterpret_cast<uintptr_t>((*reinterpret_cast<void***>(sys))[0x0c / 4]) != uintptr_t(gameBase + 0x20950)) return "system vt+0C is not the vblank re-base 420950";
    if (reinterpret_cast<uintptr_t>((*reinterpret_cast<void***>(sys))[0x1c / 4]) != uintptr_t(gameBase + 0x10320)) return "system vt+1C is not the task yield 410320";
    const int joysticks = *reinterpret_cast<int*>(diw + 0xa0d74);
    if (joysticks < 0 || joysticks > 10) return "joystick count out of range";
    return nullptr;
}
inline void* Slot(uint8_t* object, unsigned offset) { return (*reinterpret_cast<void***>(object))[offset / 4]; }
inline int PadCount() { return diw ? *reinterpret_cast<int*>(diw + 0xa0d74) : 0; }
inline int PadValue(int ofs) { return reinterpret_cast<DiwValueFn>(Slot(diw, 0x24))(diw, nullptr, ofs, 0); }
inline int PadType(int ofs) { return reinterpret_cast<DiwValueFn>(Slot(diw, 0x30))(diw, nullptr, ofs, 0); }
inline void PadRefresh() { reinterpret_cast<ThisFn>(Slot(diw, 0x50))(diw, nullptr); }
// the game's own test (410090)
inline bool PadActive(PadBind b) {
    if (!b.Valid() || PadCount() <= 0) return false;
    const int type = PadType(b.ofs) & 0xff, value = PadValue(b.ofs);
    if (type & 3) return b.val > 0 ? value > b.val : b.val < 0 ? value < b.val : false;
    if (type & 0x0c) return value != 0;
    if (type & 0x10) return value == b.val;
    return false;
}
inline Bindings& Native() { return *reinterpret_cast<Bindings*>(app + 0x2b28); }
inline void ApplySave(bool keyboard, bool pad) {
    if (keyboard) { reinterpret_cast<ThisFn>(gameBase + 0x5810)(app, nullptr); reinterpret_cast<ThisFn>(gameBase + 0x5760)(app, nullptr); }
    if (pad) {
        reinterpret_cast<ThisFn>(gameBase + 0x5a20)(app, nullptr);
        if (PadCount() > 0) reinterpret_cast<ThisFn>(gameBase + 0x5940)(app, nullptr);
        else {   // the game saves the pad section only with a pad (section = its name "Gamepad", GOG 509520): same text
            for (int a = 0; a < kActions; ++a) WritePrivateProfileStringA("Gamepad", kNative[a].ini, FormatPads(Native().pad[a]).c_str(), ".\\bio3.ini");
        }
    }
    log("MENU_SAVE bio3.ini%s%s (applied live: 405810/405A20)", keyboard ? " [Keyboard]" : "", pad ? " [Gamepad]" : "");
}
inline void ZeroInput() {
    for (uint32_t rva : {0x65c858u, 0x65c85cu, 0x65c860u, 0x65c864u, 0x65c868u}) *reinterpret_cast<uint32_t*>(gameBase + rva) = 0;
}
inline bool InputHeld() { return *reinterpret_cast<uint32_t*>(gameBase + 0x65c858) || *reinterpret_cast<uint32_t*>(gameBase + 0x65c864); }
// this poll's edges only (65C85C raw pressed, 65C860, 65C868 action pressed); held words stay -> next poll has no false edges
inline void ZeroPressed() { for (uint32_t rva : {0x65c85cu, 0x65c860u, 0x65c868u}) *reinterpret_cast<uint32_t*>(gameBase + rva) = 0; }
inline unsigned RoomCode() { return (((*reinterpret_cast<uint16_t*>(gameBase + 0x661fa6) + 1) & 15) << 8) | (*reinterpret_cast<uint16_t*>(gameBase + 0x661fa8) & 0xff); }

// ---- options ----
inline int Toggled(volatile LONG& counter) { return (InterlockedCompareExchange(&counter, 0, 0) & 1) ? 1 : 0; }
inline int OptLiveValue(int id) {
    switch (id) {
    case O_Enabled: return (desiredEnabled ? 1 : 0) ^ Toggled(toggleRequested);
    case O_Fps60: return (fps60Enabled ? 1 : 0) ^ Toggled(fps60ToggleRequested);
    default: return optValue[id];
    }
}
inline void LoadOptions(bool startup) {
    const std::string ini = Ini();
    for (int i = 0; i < O_Count; ++i) {
        optValue[i] = int(GetPrivateProfileIntA("pilot", kOptions[i].key, UINT(kOptions[i].def), ini.c_str()));
        if (kOptions[i].kind != OptNumber) optValue[i] = (std::max)(kOptions[i].lo, (std::min)(kOptions[i].hi, optValue[i]));
    }
    // fix 2026-11-11: three rows are one choice over two pilot.ini keys each (written back by OptSet)
    auto key = [&](const char* name, int def) { return int(GetPrivateProfileIntA("pilot", name, UINT(def), ini.c_str())); };
    optValue[O_Models] = key("models", 1) ? (key("characters", 1) ? 2 : 1) : 0;
    optValue[O_Scheme] = key("control_scheme", 0) ? (key("modern_stick_run", 0) ? 2 : 1) : 0;
    optValue[O_HitSounds] = key("hit_sounds", 1) ? (std::max)(0, (std::min)(200, key("hit_sound_volume", 100))) : 0;
    optValue[O_BloodBright] = (std::max)(40, (std::min)(160, optValue[O_BloodBright]));
    optValue[O_VoicePack] = voicePackIndex + 1;   // voice packs 2026-11-11: pilot.ini voice_pack=<folder> -> 0 original / pack number
    // the modules' own startup defaults where the released pilot.ini relies on them
    if (startup) for (int i = 0; i < O_Count; ++i) optRunning[i] = optValue[i];
    optRunning[O_Enabled] = desiredEnabled ? 1 : 0; optRunning[O_Fps60] = fps60Enabled ? 1 : 0;
    optRunning[O_Models] = modelActive ? (std::max)(1, optRunning[O_Models]) : 0;
}
// Fix 2026-11-11 (user: "dependent options must turn grey when their parent option blocks them"): the row that blocks this
// one by its value as it is set now (S_Count = none). What each needs was read off the modules: the follow rows work in
// the "follows Jill" crop only and the right-stick look in either crop (widescreen.h); everything HD needs the master
// switch; the HD effects are drawn by the model renderer (extra_model_integration.h active() = models on), the water / acid
// pages are textures only; wounds are made on HD enemies; shadows and the flash light need the GPU path of the models; the
// hit sounds play in HD mode only (sfx_hits.h soundOn); movie_fit matters for the player's own movies (the game's are 2:1).
inline Str OptBlocked(int id) {
    const bool hd = OptLiveValue(O_Enabled) != 0; const int models = optValue[O_Models];
    const Str needModels = !hd ? S_NeedsHd : models ? S_Count : S_NeedsModels;
    switch (id) {
    case O_WideSmooth: case O_WideSpeed: case O_WideDelay: return optValue[O_Widescreen] == 2 ? S_Count : S_NeedsFollow;
    case O_WidePeek: return optValue[O_Widescreen] ? S_Count : S_NeedsWide;
    case O_Models: case O_Effects: case O_HitSounds: return hd ? S_Count : S_NeedsHd;
    case O_Fps60: case O_Motion: case O_Face: case O_Blood: case O_Fire: case O_Muzzle: case O_Cloud: case O_GpuModels: return needModels;
    case O_Slime: return needModels != S_Count ? needModels : optValue[O_GpuModels] ? S_Count : S_NeedsGpu;   // fx 2026-11-11e
    case O_Wounds: return needModels != S_Count ? needModels : models == 2 ? S_Count : S_NeedsEnemies;
    case O_Shadows: case O_FlashLight: case O_FireLight: case O_Leather: return needModels != S_Count ? needModels : optValue[O_GpuModels] ? S_Count : S_NeedsGpu;
    case O_ShadowEdges: case O_ShadowMasks: return needModels != S_Count ? needModels : !optValue[O_GpuModels] ? S_NeedsGpu : optValue[O_Shadows] ? S_Count : S_NeedsShadows;   // fix 2026-11-11d
    case O_BloodBright: return needModels != S_Count ? needModels : (optValue[O_Blood] || optValue[O_Wounds]) ? S_Count : S_NeedsBlood;   // fx 2026-11-11e: it sets the wounds too
    case O_MovieFit: return optValue[O_PlayerMovies] ? S_Count : S_NeedsOwnMovies;
    case O_VoicePack: return voicePacks.empty() ? S_NoVoicePacks : S_Count;   // voice packs 2026-11-11: nothing in 3DD-Voice
    default: return S_Count;
    }
}
inline OptStatus OptState(int id) {
    switch (id) {
    case O_Enabled: case O_Language: case O_Effects: case O_BloodBright: return OptLive;
    case O_VoicePack: return voiceHooksReady && voiceDubReady ? OptLive : OptUnavailable;   // voice packs 2026-11-11: from the next clip load
    case O_Shadows: case O_FlashLight: case O_FireLight: case O_ShadowEdges: case O_ShadowMasks: case O_Leather: case O_Slime: return modelGpuEnabled ? OptLive : OptUnavailable;   // lighting round 2026-11-10 (GPU path of the HD models)   // r7 fx pages: nothing to install, always live
    case O_Fps60: return modelActive ? OptLive : OptUnavailable;
    case O_Motion: case O_Wounds: return modelActive ? OptLive : OptRestart;   // fix 2026-11-11: wounds are a live switch (the shot hooks are always there)
    case O_Blood: case O_Muzzle: case O_Cloud: case O_HitSounds: return optRunning[id] ? OptLive : OptRestart;
    case O_Fire: return optRunning[id] == 1 ? OptLive : OptRestart;   // fx 2026-11-11f: Original / HD only (fire_fx=2 is clamped to 1 at load)
    case O_PlayerMovies: return onSetPlayerMovies ? OptLive : OptRestart;   // live = from the next movie (none plays while open)
    case O_MovieFit: return OptLive;   // player_movies.h reads pilot.ini movie_fit at every movie open (merge 2026-11-09)
    case O_Scheme: return onSetControlScheme && onSetModernStickRun ? OptLive : OptRestart;
    case O_GlockKneel: return onSetGlockKneelBudget ? OptLive : OptRestart;
    case O_Widescreen: return onSetWidescreen ? OptLive : OptRestart;
    case O_WideSmooth: return onSetWidescreenSmooth ? OptLive : OptRestart;
    case O_WideSpeed: return onSetWidescreenSpeed ? OptLive : OptRestart;
    case O_WideDelay: return onSetWidescreenDelay ? OptLive : OptRestart;
    case O_WidePeek: return onSetWidescreenPeek ? OptLive : OptRestart;
    case O_DodgeAlways: return onSetDodgeAlways ? OptLive : OptRestart;
    default: return OptRestart;
    }
}
inline void OptSet(int id, int v) {
    const std::string ini = Ini(); const OptionDef& o = kOptions[id];
    const int before = OptLiveValue(id);
    const bool live = OptState(id) == OptLive;
    bool applied = false;
    switch (id) {
    case O_Enabled: if (before != v) InterlockedIncrement(&toggleRequested); applied = true; break;
    case O_Fps60: if (before != v) InterlockedIncrement(&fps60ToggleRequested); applied = true; break;   // fps60OnRealFrame writes fps60=
    case O_Motion: if (live) { modelMotionSmoothing = v != 0; applied = true; } break;
    case O_Blood: if (live) { re3extra::blood::enabled = v != 0; applied = true; } break;
    case O_Muzzle: if (live) { re3extra::muzzle::enabled = v != 0; applied = true; } break;
    case O_Cloud: if (live) { re3extra::nemesis_cloud::enabled = v != 0; applied = true; } break;
    case O_Effects: re3extra::fxpages::enabled = v != 0; applied = true; break;
    case O_Shadows: lightShadowMode = (std::max)(0, (std::min)(2, v)); applied = true; break;   // lighting round 2026-11-10
    case O_FlashLight: lightFlashOn = v != 0; applied = true; break;
    case O_FireLight: lightFireOn = v != 0; applied = true; break;   // fx 2026-11-11f
    case O_Leather: modelRenderer.leatherOn = v != 0; applied = true; break;   // fx 2026-11-11e
    case O_Slime: re3extra::nemesis_cloud::armModeSetting = v ? 2 : 0; applied = true; break;   // fx 2026-11-11e (nemesis_cloud_arm=1, the old gas, only from pilot.ini at start)
    case O_ShadowEdges: lightShadowEdges = (std::max)(0, (std::min)(2, v)); applied = true; break;   // fix 2026-11-11d
    case O_ShadowMasks: lightMaskShadowOn = v != 0; applied = true; break;   // fix 2026-11-11d   // r7 fx pages (fx_pages.h)
    case O_HitSounds: if (live) { sfxhits::config.enabled = v != 0; if (v) sfxhits::config.volumePct = (std::max)(0, (std::min)(200, v)); applied = true; } break;   // fix 2026-11-11: off / volume in one row
    case O_Wounds: if (live) { modelWoundsEnabled = v != 0; applied = true; } break;
    case O_BloodBright: re3extra::blood::poolBrightness = re3wound::bloodBrightness = float((std::max)(40, (std::min)(160, v))) / 100.f; applied = true; break;   // fx 2026-11-11e: wounds too
    case O_Fire: if (optRunning[id] == 1 && v <= 1) { re3extra::fire::fireMode = unsigned(v); applied = true; } break;
    case O_Scheme: if (onSetControlScheme && onSetModernStickRun) { onSetControlScheme(v ? 1 : 0); if (v) onSetModernStickRun(v == 2 ? 1 : 0); applied = true; } break;   // fix 2026-11-11: one row
    case O_GlockKneel: if (onSetGlockKneelBudget) { onSetGlockKneelBudget(v); applied = true; } break;
    case O_PlayerMovies: if (onSetPlayerMovies) { onSetPlayerMovies(v); applied = true; } break;
    case O_MovieFit: applied = true; break;   // written to pilot.ini below, read at the next movie open
    case O_Widescreen: if (onSetWidescreen) { onSetWidescreen(v); applied = true; } break;
    case O_WideSmooth: if (onSetWidescreenSmooth) { onSetWidescreenSmooth(v); applied = true; } break;
    case O_WideSpeed: if (onSetWidescreenSpeed) { onSetWidescreenSpeed(v); applied = true; } break;
    case O_WideDelay: if (onSetWidescreenDelay) { onSetWidescreenDelay(v); applied = true; } break;
    case O_WidePeek: if (onSetWidescreenPeek) { onSetWidescreenPeek(v); applied = true; } break;
    case O_DodgeAlways: if (onSetDodgeAlways) { onSetDodgeAlways(v); applied = true; } break;
    case O_Language: cfg.language = v; applied = true; break;
    case O_VoicePack: if (live) { voiceSetPack(v); v = voicePackIndex + 1; applied = true; } break;   // voice packs 2026-11-11 (writes voice_pack=)
    default: break;
    }
    optValue[id] = v;
    auto put = [&](const char* name, int value) { WritePrivateProfileStringA("pilot", name, std::to_string(value).c_str(), ini.c_str()); };
    switch (id) {   // fix 2026-11-11: the merged rows write their two keys (the second only when it has a meaning)
    case O_Fps60: case O_VoicePack: break;
    case O_Models: put("models", v ? 1 : 0); if (v) put("characters", v == 2 ? 1 : 0); break;
    case O_Scheme: put("control_scheme", v ? 1 : 0); if (v) put("modern_stick_run", v == 2 ? 1 : 0); break;
    case O_HitSounds: put("hit_sounds", v ? 1 : 0); if (v) put("hit_sound_volume", v); break;
    default: put(o.key, v); break;
    }
    log("MENU_OPTION %s=%d (was %d) %s", o.key, v, before, applied ? "applied live" : "saved (after restart)");
}

// ---- emptied keyboard rows: the loader 405590 gives an action without a key its exe default on the next start; the mod
// keeps the list (pilot.ini menu_keys_cleared, bit = Key1..KeyE) and empties those rows again at startup while their
// bio3.ini line is still empty (a key set there later, by hand or in the Setting Up window, wins)
inline uint32_t clearedSaved = 0xffffffffu;
inline void SaveClearedMask() {
    const uint32_t mask = ClearedKeysMask(Native());
    if (mask == clearedSaved) return;
    char text[16]; sprintf_s(text, "%u", mask);
    WritePrivateProfileStringA("pilot", "menu_keys_cleared", text, Ini().c_str());
    clearedSaved = mask;
    log("MENU_SAVE pilot.ini menu_keys_cleared=%u (keyboard rows left empty, kept empty at the next start)", mask);
}
inline void RestoreClearedKeys() {
    const uint32_t mask = GetPrivateProfileIntA("pilot", "menu_keys_cleared", 0, Ini().c_str());
    clearedSaved = mask;
    if (!mask) return;
    static char text[kActions][1024]; const char* lines[kActions];
    for (int a = 0; a < kActions; ++a) { GetPrivateProfileStringA("Keyboard", kNative[a].ini, "", text[a], sizeof(text[a]), ".\\bio3.ini"); lines[a] = text[a]; }
    const uint32_t cleared = ApplyClearedKeys(Native(), mask, lines);
    if (cleared) reinterpret_cast<ThisFn>(gameBase + 0x5810)(app, nullptr);   // keyboard table -> input object (as the dialog's OK)
    if (cleared != mask) {   // a row got a key outside the menu: forget it
        char value[16]; sprintf_s(value, "%u", cleared);
        WritePrivateProfileStringA("pilot", "menu_keys_cleared", value, Ini().c_str());
        clearedSaved = cleared;
    }
    log("MENU_KEYS_CLEARED pilot.ini mask=%x: %x emptied again (bio3.ini line empty, loader default removed, 405810), %x dropped (line has a key)",
        mask, cleared, mask & ~cleared);
}

// ---- round 7: the exit rows (facts and byte checks: mod_menu_exit.h) ----
enum ExitKind { ExitNone, ExitTitle, ExitDesktop };
inline char exitTitleWhy[160] = "not checked", exitDesktopWhy[160] = "not checked";
inline const char* exitTitleOff = exitTitleWhy;      // nullptr = the exe facts of the row hold (Install)
inline const char* exitDesktopOff = exitDesktopWhy;
inline ExitKind exitAfterClose = ExitNone;           // chosen in the open menu
inline ExitKind exitPending = ExitNone;              // waits for the game loop's input poll (ExitTick)
inline unsigned exitPolls = 0;
inline DWORD quitPosted = 0;                         // tick (| 1) of the posted WM_CLOSE: the menu stays shut while the game closes
inline const char* ExitReason(ExitKind kind) { return kind == ExitTitle ? "exit to title screen" : "exit to desktop"; }
// the running exe for the checks of mod_menu_exit.h
struct MemoryImage : exitfacts::Image {
    const uint8_t* At(uint32_t rva, size_t n) const override {
        return rva + n <= 0x667000u ? reinterpret_cast<const uint8_t*>(static_cast<uintptr_t>(gameBase) + rva) : nullptr;
    }
    uint32_t Code(uint32_t rva) const override {
        const uintptr_t address = gameBase + rva;   // exe_map.h; a miss resolves to its sink
        return address == reinterpret_cast<uintptr_t>(re3exe::gSink) ? 0xffffffffu : uint32_t(address - static_cast<uintptr_t>(gameBase));
    }
    uint32_t LinkBase() const override { return uint32_t(static_cast<uintptr_t>(gameBase)); }   // no relocations: loaded where it was linked
};
inline exitfacts::GameState ReadGameState() {
    const uintptr_t b = static_cast<uintptr_t>(gameBase);   // data: the same addresses in both exes
    exitfacts::GameState g;
    g.flags = *reinterpret_cast<const uint32_t*>(b + exitfacts::kFlags);
    g.slot0State = *reinterpret_cast<const uint16_t*>(b + exitfacts::kSlot0State); g.slot0Entry = *reinterpret_cast<const uint32_t*>(b + exitfacts::kSlot0Entry);
    g.slot1State = *reinterpret_cast<const uint16_t*>(b + exitfacts::kSlot1State); g.slot1Entry = *reinterpret_cast<const uint32_t*>(b + exitfacts::kSlot1Entry);
    g.gameplayEntry = uint32_t(gameBase + exitfacts::kGameplayEntry);
    g.memCardEntry = *reinterpret_cast<const uint32_t*>(b + exitfacts::kProgramTable + exitfacts::kProgramMemCard * 12 + 4);
    return g;
}
inline HWND GameWindowOfApp() { return app ? *reinterpret_cast<HWND*>(app + 4 + 0x28) : nullptr; }   // 404771: hwnd of the window object
inline bool FrameOpen() {   // 40B9C0 .. 40BC95: [renderer + 10CC10], renderer = [5349C4]
    const auto* renderer = *reinterpret_cast<uint8_t* const*>(static_cast<uintptr_t>(gameBase) + 0x1349c4);
    return renderer && *reinterpret_cast<const volatile LONG*>(renderer + 0x10cc10) != 0;
}
inline void VerifyExit(const char* exe) {
    const MemoryImage image;
    const char* why = exitfacts::VerifyTitle(image);
    if (why) strcpy_s(exitTitleWhy, why);
    exitTitleOff = why ? exitTitleWhy : nullptr;
    why = exitfacts::VerifyDesktop(image);
    if (!why && !GameWindowOfApp()) why = "the window object has no window";
    if (why) strcpy_s(exitDesktopWhy, why);
    exitDesktopOff = why ? exitDesktopWhy : nullptr;
    if (exitTitleOff) log("MENU_EXIT_CHECK row \"Exit to title screen\" off (%s): %s", exe, exitTitleOff);
    if (exitDesktopOff) log("MENU_EXIT_CHECK row \"Exit to desktop\" off (%s): %s", exe, exitDesktopOff);
    if (!exitTitleOff && !exitDesktopOff)
        log("MENU_EXIT_CHECK ok (%s): title = soft-reset bit 200000 of [A5C880] at the game loop's poll 4238CB -> 424B50; desktop = WM_CLOSE to window %p -> "
            "404892 DestroyWindow -> 4088A0 PostQuitMessage", exe, static_cast<void*>(GameWindowOfApp()));
}
// After the menu loop, at an input poll of the game: carries out the exit chosen in the menu. Only at the poll of the game
// loop (call 4238C6, return 4238CB): the game tests its soft-reset bit right after it, every task thread is parked and no
// frame is open. The movie player polls too (42FB60): not there.
inline void ExitTick(DWORD now) {
    if (quitPosted && now - quitPosted > 15000) {
        quitPosted = 0;
        log("MENU_EXIT desktop: the game still runs 15 s after WM_CLOSE was posted; the menu can be opened again");
    }
    if (exitPending == ExitNone) return;
    const bool gameLoop = pollReturn == gameBase + exitfacts::kMainLoopPollReturn;
    const bool frameOpen = !exitDesktopOff && FrameOpen();   // the flag's code is among the desktop row's checks; never set at this poll
    if (!gameLoop || frameOpen) {
        if (++exitPolls >= 150) {
            log("MENU_EXIT %s dropped: 150 input polls without one of the game loop outside a frame (last poll returns to %p, frame open %d)",
                exitPending == ExitTitle ? "title" : "desktop", reinterpret_cast<void*>(pollReturn), frameOpen ? 1 : 0);
            exitPending = ExitNone;
        }
        return;
    }
    const ExitKind kind = exitPending; exitPending = ExitNone;
    const exitfacts::GameState g = ReadGameState();
    if (kind == ExitTitle) {
        const Str blocked = exitfacts::TitleBlocked(g);   // the state the row was offered in, checked again
        if (blocked != S_Count) {
            log("MENU_EXIT title refused: %s (A5C880=%08x slot0=%u/%08x slot1=%u/%08x)", exitfacts::BlockedName(blocked), g.flags, g.slot0State, g.slot0Entry,
                g.slot1State, g.slot1Entry);
            return;
        }
        // the bit the game sets itself at 42CE09 (demo end, inside this poll) and 42BDF0; tested at 4238D0 right after this hook
        *reinterpret_cast<volatile uint32_t*>(static_cast<uintptr_t>(gameBase) + exitfacts::kFlags) = g.flags | exitfacts::kFlagSoftReset;
        log("MENU_EXIT title: soft reset requested the game's way (A5C880 %08x -> %08x at the game loop's poll 4238CB, next 4238D0 -> 424B50 -> title program), "
            "room R%03X, slot1=%u/%08x, %u polls after the menu", g.flags, g.flags | exitfacts::kFlagSoftReset, RoomCode(), g.slot1State, g.slot1Entry, exitPolls);
        return;
    }
    const HWND window = GameWindowOfApp(); DWORD process = 0;
    const DWORD thread = window && IsWindow(window) ? GetWindowThreadProcessId(window, &process) : 0;
    if (!thread || process != GetCurrentProcessId()) { log("MENU_EXIT desktop refused: %p is not a window of this process", static_cast<void*>(window)); return; }
    if (!PostMessageA(window, WM_CLOSE, 0, 0)) { log("MENU_EXIT desktop refused: PostMessage(WM_CLOSE) failed, error %lu", GetLastError()); return; }
    quitPosted = now | 1;
    log("MENU_EXIT desktop: WM_CLOSE posted to the game window %p (window thread %lu, game thread %lu) = \"Yes\" of the game's quit box: 404892 DestroyWindow -> "
        "4088A0 PostQuitMessage -> 410280 ends the tasks -> WinMain returns; A5C880=%08x room R%03X, %u polls after the menu",
        static_cast<void*>(window), thread, GetCurrentThreadId(), g.flags, RoomCode(), exitPolls);
}

// ---- menu host ----
struct GameHost : Host {
    Bindings& Native() override { return detail::Native(); }
    ModBinding& Mod(int m) override { return cfg.mod[m]; }
    void CommitNative(bool keyboard, bool pad) override {
        ApplySave(keyboard, pad);
        if (keyboard) SaveClearedMask();
    }
    void CommitMod(int m) override {
        const std::string ini = Ini();
        WritePrivateProfileStringA("pilot", kMod[m].keyIni, std::to_string(cfg.mod[m].vk).c_str(), ini.c_str());
        WritePrivateProfileStringA("pilot", kMod[m].padIni, FormatPad(cfg.mod[m].pad).c_str(), ini.c_str());
        cfg.explicitPad[m] = true;
        log("MENU_SAVE pilot.ini %s=%d %s=%s", kMod[m].keyIni, cfg.mod[m].vk, kMod[m].padIni, FormatPad(cfg.mod[m].pad).c_str());
        if (m == ModReload && onReloadKeyChanged) onReloadKeyChanged(cfg.mod[m].vk);
    }
    bool PadPresent() override { return PadCount() > 0; }
    int OptGet(int id) override { return OptLiveValue(id); }
    int OptRunning(int id) override { return optRunning[id]; }
    OptStatus OptState(int id) override { return detail::OptState(id); }
    Str OptBlocked(int id) override { return detail::OptBlocked(id); }   // fix 2026-11-11
    Str OptReason(int id) override { return id == O_Fps60 ? S_NeedsModels : S_Unavailable; }
    // voice packs 2026-11-11: the voice-over row = original + every offered pack (pack.ini name, name_ru in a Russian menu)
    int OptChoiceCount(int id) override { return id == O_VoicePack ? 1 + int(voicePacks.size()) : Host::OptChoiceCount(id); }
    std::wstring OptChoiceText(int id, int v, bool ru) override {
        if (id != O_VoicePack || v <= 0 || v > int(voicePacks.size())) return Host::OptChoiceText(id, v, ru);
        const auto& p = voicePacks[size_t(v - 1)].info; return ru && !p.nameRu.empty() ? p.nameRu : p.name;
    }
    void OptSet(int id, int value) override { detail::OptSet(id, value); }
    bool NativeWindowAvailable() override { return !merce; }
    void OpenNativeWindow() override { nativeAfterClose = true; closeRequested = true; closeReason = "game settings window"; }
    void Close() override { closeRequested = true; closeReason = "closed"; }
    void Note(const char* line) override { log("%s", line); }
    // round 7: the exit rows. The state is read while the menu loop holds the game (nothing changes under it) and again
    // when the exit is carried out (ExitTick).
    Str ExitTitleBlocked() override { return exitTitleOff ? S_Unavailable : exitfacts::TitleBlocked(ReadGameState()); }
    Str ExitDesktopBlocked() override { return exitDesktopOff ? S_Unavailable : S_Count; }
    void ExitToTitle() override { exitAfterClose = ExitTitle; closeRequested = true; closeReason = ExitReason(ExitTitle); }
    void ExitToDesktop() override {
        // every row of the menu is saved when it is changed, except a 60 fps switch still waiting for the frame hook
        // (fps60OnRealFrame writes fps60=): the game may close before its next frame, so it is written here
        if (Toggled(fps60ToggleRequested)) WritePrivateProfileStringA("pilot", "fps60", OptLiveValue(O_Fps60) ? "1" : "0", Ini().c_str());
        exitAfterClose = ExitDesktop; closeRequested = true; closeReason = ExitReason(ExitDesktop);
    }
};
inline GameHost host;

// ---- pad events in the menu ----
// Round 6: the logic is PadReader (mod_menu_ui.h, driven offline by menu_tests.cpp); here the game's DirectInput wrapper as
// its source. debug_warp=1 (harness runs, nobody presses the pad there): pilot.stick "pad <ms> <ofs> <value> [<ofs>
// <value>]..." read by the open menu overrides those DIJOYSTATE offsets for <ms> - e.g. "pad 150 49 128" = B,
// "pad 200 4 1000" = d-pad / left stick down, "pad 600 16 -1000" = right stick up.
inline int qaPadOfs[4]{-1, -1, -1, -1}, qaPadValue[4]{}; inline DWORD qaPadUntil = 0, qaPadCheck = 0;
struct GamePad : PadSource {
    int Count() override { return PadCount(); }
    int Type(int ofs) override { return PadType(ofs); }
    int Value(int ofs) override {
        if (qaPadUntil) for (int i = 0; i < 4; ++i) if (qaPadOfs[i] == ofs) return qaPadValue[i];
        return PadValue(ofs);
    }
    void Refresh() override { PadRefresh(); }
};
inline GamePad gamePad; inline PadReader padReader;
inline void QaPadInject(DWORD now) {
    if (qaPadUntil && int(now - qaPadUntil) >= 0) { qaPadUntil = 0; log("MENU_QA pad injection ends"); }
    if (int(now - qaPadCheck) < 0) return;
    qaPadCheck = now + 40;
    const std::string path = root + "\\pilot.stick";
    if (GetFileAttributesA(path.c_str()) == INVALID_FILE_ATTRIBUTES) return;
    std::ifstream in(path); std::string what; int ms = 0;
    if (!(in >> what) || what != "pad" || !(in >> ms)) return;   // other pilot.stick commands wait for the game
    for (int i = 0; i < 4; ++i) { qaPadOfs[i] = -1; if (!(in >> qaPadOfs[i] >> qaPadValue[i])) qaPadOfs[i] = -1; }
    in.close(); DeleteFileA(path.c_str());
    qaPadUntil = (now + DWORD((std::max)(10, (std::min)(ms, 5000)))) | 1;
    log("MENU_QA pad injection %d ms: %d=%d %d=%d %d=%d %d=%d", ms, qaPadOfs[0], qaPadValue[0], qaPadOfs[1], qaPadValue[1], qaPadOfs[2], qaPadValue[2], qaPadOfs[3], qaPadValue[3]);
}
inline void PadScanInit() {
    char text[16] = "";
    GetPrivateProfileStringA("general", "dpadtoanalog", "", text, sizeof(text), ".\\goginput.ini");
    padReader.Init(gamePad, !_stricmp(text, "TRUE") || !strcmp(text, "1"));
    static std::string logged;
    if (padReader.Seen() != logged) {
        logged = padReader.Seen();
        log("MENU_PAD inputs (DIJOYSTATE offset:type)%s; goginput.ini dpadtoanalog=%s: the POV hat is %s", logged.c_str(), text[0] ? text : "(not set)",
            padReader.HatSkipped() ? "not scanned (the d-pad arrives on lX/lY)" : "scanned");
    }
}
inline void PadPoll(DWORD now, bool baseline) { padReader.Poll(gamePad, menu, host, now, baseline, closeRequested); }
inline bool PadAnyActive() { return padReader.AnyActive(); }

// ---- drawing ----
inline void ReleaseCapture() { if (capture) { capture->Release(); capture = nullptr; } capW = capH = 0; }
inline bool CaptureFrame() {
    IDirect3DDevice9* d = gameDevice; if (!d) return false;
    IDirect3DSurface9* rt = nullptr; if (FAILED(d->GetRenderTarget(0, &rt)) || !rt) return false;
    D3DSURFACE_DESC desc{}; rt->GetDesc(&desc);
    HRESULT hr = S_OK;
    if (!capture || capW != desc.Width || capH != desc.Height || capFmt != desc.Format) {
        ReleaseCapture();
        hr = d->CreateTexture(desc.Width, desc.Height, 1, D3DUSAGE_RENDERTARGET, desc.Format, D3DPOOL_DEFAULT, &capture, nullptr);
        if (SUCCEEDED(hr)) { capW = desc.Width; capH = desc.Height; capFmt = desc.Format; }
    }
    IDirect3DSurface9* dst = nullptr;
    if (SUCCEEDED(hr)) hr = capture->GetSurfaceLevel(0, &dst);
    if (SUCCEEDED(hr)) hr = d->StretchRect(rt, nullptr, dst, nullptr, D3DTEXF_NONE);
    if (dst) dst->Release();
    rt->Release();
    if (FAILED(hr)) { log("MENU_CAPTURE_FAILED hr=%08lx %ux%u format=%u", hr, desc.Width, desc.Height, unsigned(desc.Format)); ReleaseCapture(); return false; }
    return true;
}
inline bool DrawFrame(bool withMenu, DWORD now) {
    std::lock_guard<std::recursive_mutex> guard(mutex);
    IDirect3DDevice9* d = gameDevice;
    if (!d || !capture) return false;
    auto dd = gogWrapper;   // the GOG wrapper's deferred vertex queue goes down first (as drawQuad)
    if (dd) { auto cs = reinterpret_cast<CRITICAL_SECTION*>(dd + 0x181fb0); EnterCriticalSection(cs); reinterpret_cast<void(__cdecl*)()>(dd + 0x45b30)(); LeaveCriticalSection(cs); }
    const bool ownScene = dd && !*reinterpret_cast<uint32_t*>(dd + 0x181fe4);
    if (ownScene && FAILED(d->BeginScene())) return false;
    QuadState state;
    if (FAILED(state.capture(d))) { if (ownScene) d->EndScene(); return false; }
    D3DVIEWPORT9 old{}; d->GetViewport(&old);
    IDirect3DSurface9* rt = nullptr; D3DSURFACE_DESC desc{};
    if (SUCCEEDED(d->GetRenderTarget(0, &rt)) && rt) { rt->GetDesc(&desc); rt->Release(); }
    if (!desc.Width) { desc.Width = capW; desc.Height = capH; }
    D3DVIEWPORT9 full{0, 0, desc.Width, desc.Height, 0.f, 1.f}; d->SetViewport(&full);
    gfx.Begin(d, float(desc.Width), float(desc.Height));
    gfx.Frame(capture);
    if (withMenu) {
        // round 6 (user: "with widescreen on, the format must not change to 4:3 while the F5 menu is open"): while the crop
        // is shown the menu is laid out inside the crop window of the frame; widescreen.h holds that window while modal
        float top = 0, rows = 0;
        if (onMenuView && onMenuView(top, rows) && rows >= 60.f && rows < 240.f) {
            const float k = float(desc.Height) / 240.f;
            gfx.View(0, top * k, float(desc.Width), rows * k);
        }
        menu.Draw(gfx, now);
    }
    d->SetViewport(&old); state.restore(d);
    if (ownScene) d->EndScene();
    return true;
}

// ---- the modal loop at the flip ----
inline std::vector<UINT> keysDown;
inline unsigned windowMessages = 0;   // QA 2026-11-09: any window message while the menu is open
inline bool qaTrace = false;   // pilot.ini debug_warp=1 at open: MENU_LOOP / MENU_WNDMSG / MENU_KEY diagnostic lines
inline unsigned keyMessages = 0;   // QA 2026-11-09: key messages seen by the open menu (MENU_KEY lines, first 40)
inline void Pump() {
    MSG m;
    while (PeekMessageA(&m, nullptr, 0, 0, PM_REMOVE)) {
        if (m.message == WM_QUIT) { PostQuitMessage(int(m.wParam)); closeRequested = true; closeReason = "WM_QUIT"; return; }
        DispatchMessageA(&m);
    }
}
// QA 2026-11-09 (debug_warp=1 only): pilot.command "capture" while the menu is open captures the menu frame (the game's
// frame hook, which takes pilot.command, does not run while the menu loop holds the game task); other commands wait
inline DWORD qaNextCheck = 0;
inline bool QaCaptureRequested(DWORD now) {
    if (int(now - qaNextCheck) < 0) return false;
    qaNextCheck = now + 250;
    if (!GetPrivateProfileIntA("pilot", "debug_warp", 0, Ini().c_str())) return false;
    const std::string path = root + "\\pilot.command";
    std::ifstream in(path); std::string command; if (!in || !(in >> command) || command != "capture") return false;
    in.close(); DeleteFileA(path.c_str());
    return true;
}
// QA 2026-11-09: let the main (window) thread pump its messages once (system vt+1C = 410320, see FindObjects); only from the
// game task thread, never from the window's own thread
inline DWORD windowThread = 0; inline unsigned yields = 0;
inline void YieldToWindowThread() {
    if (!windowThread) { const HWND gw = re3options::GameWindow(); windowThread = gw ? GetWindowThreadProcessId(gw, nullptr) : 0; }
    if (!windowThread || windowThread == GetCurrentThreadId() || !sys) return;
    reinterpret_cast<int(__fastcall*)(void*, void*)>(Slot(sys, 0x1c))(sys, nullptr);
    ++yields;
}
inline bool KeysReleased() {
    for (UINT vk : keysDown) if (GetAsyncKeyState(int(vk)) & 0x8000) return false;
    return true;
}
inline int Modal(void* display, void* edx, int argument) {
    const DWORD t0 = GetTickCount();
    { std::lock_guard<std::recursive_mutex> guard(mutex); keepDeviceHooks(); }
    gpuFlushPending();
    bool ok = false;
    { std::lock_guard<std::recursive_mutex> guard(mutex); ok = CaptureFrame(); }
    if (!ok) { log("MENU_OPEN refused: frame copy failed"); suppressUntil = GetTickCount() + 3000; return flipNext(display, edx, argument); }
    modal = true; closeRequested = false; nativeAfterClose = false; closeReason = ""; keysDown.clear(); ++opens;
    exitAfterClose = ExitNone;   // round 7
    LoadOptions(false);
    menu.Open(&host, MenuRussian(cfg.language));
    PadScanInit(); PadPoll(t0, true);
    log("MENU_OPEN #%u (%s) room R%03X frame %ux%u format=%u pad=%d lang=%s", opens, openSource, RoomCode(), capW, capH, unsigned(capFmt), PadCount(), menu.ru ? "ru" : "en");
    {   // round 7: what the two exit rows offer in this state (one line per open: QA and bug reports read it)
        const exitfacts::GameState g = ReadGameState();
        log("MENU_EXIT_ROWS title: %s, desktop: %s (A5C880=%08x slot0=%u/%08x slot1=%u/%08x)", exitTitleOff ? "exe check failed" : exitfacts::BlockedName(exitfacts::TitleBlocked(g)),
            exitDesktopOff ? "exe check failed" : "available", g.flags, g.slot0State, g.slot0Entry, g.slot1State, g.slot1Entry);
    }
    qaTrace = GetPrivateProfileIntA("pilot", "debug_warp", 0, Ini().c_str()) != 0;
    if (opens == 1) {   // QA 2026-11-09: the game window belongs to the main thread, this loop runs in the game task thread
        const HWND gw = re3options::GameWindow();
        log("MENU_THREADS game window %p owned by thread %lu, menu loop thread %lu (window messages via the task yield 410320)",
            static_cast<void*>(gw), gw ? GetWindowThreadProcessId(gw, nullptr) : 0ul, GetCurrentThreadId());
    }
    int result = 0; DWORD lastDraw = 0, lastFlip = 0; bool first = true; unsigned flips = 0;
    DWORD beat = t0; unsigned beats = 0; const unsigned yields0 = yields; bool qaCapture = false;
    float viewKey = -2.f;   // round 6: the crop window the menu was last drawn in (changes when the widescreen row is changed)
    for (;;) {
        Pump();
        YieldToWindowThread();   // the main thread pumps the window's messages (keys -> OnWindowMessage) and comes back
        const DWORD now = GetTickCount();
        if (qaTrace && now - beat >= 2000 && beats < 3) {   // QA 2026-11-09: a few heartbeat lines per open
            beat = now; ++beats;
            log("MENU_LOOP #%u %.1f s flips=%u keys=%u window_messages=%u yields=%u page=%d", opens, (now - t0) / 1000.0, flips, keyMessages, windowMessages, yields - yields0, int(menu.CurrentPage()));
        }
        if (qaTrace) QaPadInject(now);
        if (!closeRequested) { PadPoll(now, false); menu.Tick(now); }
        if (closeRequested) break;
        if (!gameDevice || !capture) { closeRequested = true; closeReason = "device lost"; break; }
        {
            float top = 0, rows = 0;
            const float key = onMenuView && onMenuView(top, rows) ? top * 1000.f + rows : -1.f;
            if (key != viewKey) { viewKey = key; menu.Invalidate(); }
        }
        const bool animate = menu.Capturing() && now - lastDraw >= 40;
        if (QaCaptureRequested(now)) qaCapture = true;
        if (first || menu.Dirty() || animate || qaCapture || now - lastDraw >= 500) {
            if (now - lastFlip < 15) { Sleep(1); continue; }
            if (first && qaTrace) log("MENU_LOOP #%u first draw", opens);
            if (!DrawFrame(true, now)) { closeRequested = true; closeReason = "draw failed"; break; }
            if (qaCapture) { qaCapture = false; std::lock_guard<std::recursive_mutex> guard(mutex); captureFrame(); }   // the frame with the menu, before the flip
            menu.Drawn(); result = flipNext(display, edx, argument); ++flips; lastDraw = lastFlip = GetTickCount();
            if (first && qaTrace) log("MENU_LOOP #%u first flip after %u ms", opens, unsigned(lastFlip - t0));
            first = false;
            if ((flips & 63) == 0) gfx.Trim();
            continue;
        }
        Sleep(4);
    }
    // closing: the plain frame, then wait (max 0.8 s) until the keys / buttons used in the menu are released
    if (gameDevice && capture && DrawFrame(false, GetTickCount())) { result = flipNext(display, edx, argument); ++flips; }
    for (const DWORD wait = GetTickCount(); GetTickCount() - wait < 800;) {
        if (!KeysReleased()) { Pump(); YieldToWindowThread(); Sleep(5); continue; }
        if (PadCount() > 0) { PadPoll(GetTickCount(), true); if (PadAnyActive()) { Pump(); YieldToWindowThread(); Sleep(5); continue; } }
        break;
    }
    { std::lock_guard<std::recursive_mutex> guard(mutex); gfx.ReleaseTextures(); ReleaseCapture(); }
    reinterpret_cast<ThisFn>(Slot(sys, 0x0c))(sys, nullptr);   // system vt+0C = 420950 -> 4209E0: vblank clock re-based, no catch-up (QA 2026-11-09: was input vt+98)
    for (int m = 0; m < kModActions; ++m) padPrev[m] = PadActive(cfg.mod[m].pad);
    suppressUntil = GetTickCount() + 3000;
    modal = false;
    log("MENU_CLOSE #%u %s after %.1f s, %u flips", opens, closeReason, (GetTickCount() - t0) / 1000.0, flips);
    if (nativeAfterClose) { re3options::keyWindow = re3options::GameWindow(); InterlockedExchange(&re3options::pcRequest, 1); }
    // round 7: an exit chosen in the menu is carried out at the game loop's next input poll (ExitTick), outside this loop
    // and outside the frame it holds open - and only when the menu closed for it (not for a lost focus or device)
    if (exitAfterClose != ExitNone) {
        if (!strcmp(closeReason, ExitReason(exitAfterClose))) { exitPending = exitAfterClose; exitPolls = 0; }
        else log("MENU_EXIT %s cancelled: the menu was closed by \"%s\" first", exitAfterClose == ExitTitle ? "title" : "desktop", closeReason);
        exitAfterClose = ExitNone;
    }
    return result;
}
inline int __fastcall FlipHook(void* display, void* edx, int argument) {
    if (armed && !modal && !fps60Pass) { armed = 0; return Modal(display, edx, argument); }
    return flipNext(display, edx, argument);
}

// QA 2026-11-09 (game run): the main loop 408450 runs TranslateAcceleratorA (call [50A230] at 4084A3) before dispatching:
// the game's table turns Esc and Alt+F4 into WM_COMMAND 40001 (its quit dialog) and F1 into 40002 (Setting Up), so Esc
// never reached the open menu and opened the quit dialog over it. While the menu is open no accelerator applies.
inline void* nativeTranslateAccelerator = nullptr;
inline int WINAPI TranslateAcceleratorHook(HWND w, HACCEL table, LPMSG m) {
    if (modal) return 0;
    return reinterpret_cast<int(WINAPI*)(HWND, HACCEL, LPMSG)>(nativeTranslateAccelerator)(w, table, m);
}
inline const char* HookAccelerators() {
    const uint8_t* call = reinterpret_cast<const uint8_t*>(gameBase + 0x84a3);
    if (call[0] != 0xff || call[1] != 0x15) return "TranslateAcceleratorA call 4084A3 differs";
    void** slot = *reinterpret_cast<void** const*>(call + 2);
    if (reinterpret_cast<uintptr_t>(slot) != 0x50a230 || !*slot) return "TranslateAcceleratorA import slot 50A230 differs";
    if (*slot == reinterpret_cast<void*>(&TranslateAcceleratorHook)) return nullptr;
    DWORD protection;
    if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &protection)) return "import slot not writable";
    nativeTranslateAccelerator = *slot; *slot = reinterpret_cast<void*>(&TranslateAcceleratorHook);
    VirtualProtect(slot, sizeof(void*), protection, &protection);
    return nullptr;
}

inline void MigratePadLayout() {
    const std::string ini = Ini();
    const int layout = int(GetPrivateProfileIntA("pilot", "pad_layout", 0, ini.c_str()));
    if (layout >= 3) return;
    if (PadCount() <= 0) return;   // [Gamepad] is loaded only with a pad (405860): try again next run
    const PadBind menuPad = cfg.mod[ModMenu].pad;
    int moved = 0, removed = 0;
    if (layout < 2) FreeMenuPad(Native(), menuPad, moved, removed);
    // fix 2026-11-11b, layout 3: Back / Share = the HD toggle, the game's KeyB (PS Select) leaves it
    const int back = BackToHd(Native(), cfg.mod);
    if (moved || removed || (back & 1)) ApplySave(false, true);
    if (back & 2) {
        WritePrivateProfileStringA("pilot", kMod[ModHd].padIni, FormatPad(cfg.mod[ModHd].pad).c_str(), ini.c_str());
        cfg.explicitPad[ModHd] = true;
    }
    WritePrivateProfileStringA("pilot", "pad_layout", "3", ini.c_str());
    if (layout < 2) log("MENU_PAD_LAYOUT %s %s: %d moved to Back (56,128), %d removed (game actions on the menu button)", FormatPad(menuPad).c_str(),
                        moved || removed ? "freed" : "already free", moved, removed);
    log("MENU_PAD_LAYOUT 3 Back / Share = HD toggle: KeyB (PS Select) %s, hd_pad %s (now %s)", (back & 1) ? "left Back" : "was not on Back",
        (back & 2) ? "set to Back" : "kept", FormatPad(cfg.mod[ModHd].pad).c_str());
}

inline void Install() {
    checked = true; EnsureConfig();
    merce = re3exe::gRunning == re3exe::Exe::Merce;
    LoadOptions(true);
    const char* why = !cfg.enabled ? "pilot.ini menu=0" : VerifyCode();
    if (!why) why = FindObjects();
    if (!why) {
        flipCall = gameBase + 0xbc84; auto* call = reinterpret_cast<uint8_t*>(flipCall);
        flipNext = reinterpret_cast<FlipFn>(reinterpret_cast<uintptr_t>(call + 5) + *reinterpret_cast<int32_t*>(call + 1));
        DWORD protection;
        if (!VirtualProtect(call, 5, PAGE_EXECUTE_READWRITE, &protection)) why = "flip call not writable";
        else {
            *reinterpret_cast<int32_t*>(call + 1) = int32_t(reinterpret_cast<uint8_t*>(&FlipHook) - (call + 5));
            VirtualProtect(call, 5, protection, &protection); FlushInstructionCache(GetCurrentProcess(), call, 5);
        }
    }
    ready = why == nullptr;
    if (ready) {
        VerifyExit(merce ? "RE3_MERCE.exe" : "ResidentEvil3.exe");   // round 7: the exit rows' exe facts, a row is greyed when its bytes differ
        if (const char* accel = HookAccelerators()) log("MENU_ACCELERATORS not hooked: %s (Esc in the open menu may open the game's quit dialog)", accel);
        MigratePadLayout();
        RestoreClearedKeys();
        if (PadCount() > 0) {   // mod pad buttons a game action also uses ([Gamepad] of this pad is loaded now)
            ModBinding before[kModActions]; for (int m = 0; m < kModActions; ++m) before[m] = cfg.mod[m];
            PadConflict conflicts[kModActions]; const int n = ResolveModPadConflicts(Native(), cfg.mod, cfg.explicitPad, conflicts, kModActions);
            for (int i = 0; i < n && i < kModActions; ++i)
                log("MENU_PAD_CONFLICT %s=%s is also bio3.ini [Gamepad] %s: %s", kMod[conflicts[i].mod].padIni, FormatPad(before[conflicts[i].mod].pad).c_str(),
                    kNative[conflicts[i].action].ini, conflicts[i].dropped ? "default button off for this run" : "kept (set in pilot.ini), both act");
        }
        for (int m = 0; m < kModActions; ++m) padPrev[m] = PadActive(cfg.mod[m].pad);
    }
    char keys[256] = "";
    for (int m = 0; m < kModActions; ++m) {
        char part[64]; sprintf_s(part, "%s%s=%d/%s", m ? " " : "", kMod[m].keyIni, cfg.mod[m].vk, FormatPad(cfg.mod[m].pad).c_str()); strcat_s(keys, part);
    }
    char wired[240];
    sprintf_s(wired, "reload=%u reload_key=%u suspend=%u view=%u scheme=%u stick_run=%u widescreen=%u smooth=%u speed=%u delay=%u peek=%u movies=%u glock=%u dodge=%u",
              onReload != nullptr, onReloadKeyChanged != nullptr, onInputSuspended != nullptr, onMenuView != nullptr, onSetControlScheme != nullptr,
              onSetModernStickRun != nullptr, onSetWidescreen != nullptr, onSetWidescreenSmooth != nullptr, onSetWidescreenSpeed != nullptr,
              onSetWidescreenDelay != nullptr, onSetWidescreenPeek != nullptr, onSetPlayerMovies != nullptr, onSetGlockKneelBudget != nullptr,
              onSetDodgeAlways != nullptr);
    if (ready) log("MENU_INSTALL ready (%s) flip 40BC84 -> menu -> %s, app=%p pads=%d lang=%s keys: %s; wired: %s", merce ? "RE3_MERCE.exe" : "ResidentEvil3.exe",
                   reinterpret_cast<uintptr_t>(flipNext) == gameBase + 0x10fe0 ? "410FE0" : "fps60 flip hook", static_cast<void*>(app), PadCount(),
                   MenuRussian(cfg.language) ? "ru" : "en", keys, wired);
    else log("MENU_INSTALL off: %s; %s; keys: %s; wired: %s", why, merce ? "Mercenaries: options_key does nothing" : "options_key opens the game's Setting Up window",
             keys, wired);
}
// other modules (modern controls) are told while the game must not see input: open request .. all released after close
inline bool suspendNoted = false;
inline void NoteSuspension() {
    const bool blocked = modal || armed || suppressUntil;
    if (blocked == suspendNoted) return;
    suspendNoted = blocked;
    if (onInputSuspended) onInputSuspended(blocked);
}
inline void Trigger(int m, const char* source) {
    switch (m) {
    case ModMenu: openSource = source; InterlockedExchange(&openRequest, 1); break;   // taken (or refused) in AfterInputPoll
    case ModHd: InterlockedIncrement(&toggleRequested); break;
    case ModFps60: InterlockedIncrement(&fps60ToggleRequested); break;
    case ModReload: InterlockedExchange(&reloadRequest, 1); break;
    default: break;
    }
}
}  // namespace detail

inline bool IsOpen() { return detail::modal; }
// true while the game must not see input (menu requested / open / keys still held after it): other modules check it
inline bool InputBlocked() { return detail::modal || detail::armed || detail::suppressUntil; }

void AfterInputPoll(uintptr_t base) {
    using namespace detail;
    (void)base;
    if (!checked) Install();
    const DWORD now = GetTickCount();
    if (ready) ExitTick(now);   // round 7: an exit chosen in the menu (first: the game tests its soft-reset bit right after this hook)
    if (ready) for (int m = 0; m < kModActions; ++m) {   // mod pad buttons (this poll's pad state, the game's own test 410090)
        const bool active = PadActive(cfg.mod[m].pad);
        if (active && !padPrev[m] && !armed && !suppressUntil) Trigger(m, "pad");
        padPrev[m] = active;
    }
    if (InterlockedExchange(&reloadRequest, 0)) {   // key (window message) or pad above; the reload module runs after this hook
        if (onReload && !suppressUntil && !armed && !modal) onReload();
        else { static unsigned unwired = 0; if (!onReload && ++unwired <= 3) log("MENU_RELOAD key: re3menu::onReload not wired"); }
    }
    if (!ready) {   // no 3DD menu: options_key opens the game's Setting Up window (fx 2026-11-08 behaviour), Mercenaries nothing
        if (InterlockedExchange(&openRequest, 0) && !merce) { re3options::keyWindow = re3options::GameWindow(); InterlockedExchange(&re3options::pcRequest, 1); }
        return;
    }
    if (InterlockedExchange(&openRequest, 0) && !armed) {
        const char* why = exitPending != ExitNone || quitPosted ? "the game is exiting (round 7)" :   // never a menu loop while WM_CLOSE is on its way
                          moviePlaying ? "movie playing" : !gameDevice ? "no device" : !gogWrapper ? "GOG wrapper not verified" : nullptr;
        if (why) log("MENU_OPEN refused (%s): %s", openSource, why);
        else armed = 8;
    }
    if (armed) {
        ZeroInput();
        if (--armed == 0) { log("MENU_OPEN refused (%s): no frame flip within 8 input polls", openSource); suppressUntil = now + 3000; }
    } else if (suppressUntil) {   // after the menu: the game sees nothing until every key / button is released (max 3 s)
        if (!InputHeld()) suppressUntil = 0;
        else if (int(now - suppressUntil) >= 0) {   // still held: handed back without this poll's edges (no phantom press)
            ZeroPressed(); suppressUntil = 0;
            static unsigned logged = 0; if (++logged <= 8) log("MENU_INPUT held 3 s after the menu: given back as held, not pressed");
        } else ZeroInput();
    }
    NoteSuspension();
}
// Round 6: a game action or a mod key is on the right stick's up / down axis (DIJOYSTATE lRy, offset 16): the widescreen
// peek (proxy.cpp gPeekStick) leaves the stick to it. Checked at every present: 14 x 16 slots.
bool RightStickBound() {
    using namespace detail;
    if (!ready || !app || PadCount() <= 0) return false;
    const Bindings& b = Native();
    for (int a = 0; a < kActions; ++a) for (int s = 0; s < kSlots; ++s) if (b.pad[a][s].ofs == 16) return true;
    for (int m = 0; m < kModActions; ++m) if (cfg.mod[m].pad.ofs == 16) return true;
    return false;
}
// pilotWndProc (proxy.cpp): menu input while it is open, rebindable mod keys otherwise; true = handled (result set)
bool OnWindowMessage(HWND w, UINT msg, WPARAM wp, LPARAM lp, LRESULT& result) {
    using namespace detail;
    (void)w; result = 0;
    if (modal) {
        if (++windowMessages <= 12 && qaTrace) log("MENU_WNDMSG %04X wp=%X thread %lu", msg, unsigned(wp), GetCurrentThreadId());   // QA 2026-11-09
        switch (msg) {
        case WM_KEYDOWN: case WM_SYSKEYDOWN: {
            const bool repeat = (lp & (1l << 30)) != 0;
            if (++keyMessages <= 40 && qaTrace) log("MENU_KEY vk=%02X repeat=%d window thread %lu, this thread %lu", unsigned(wp), repeat ? 1 : 0,
                                         GetWindowThreadProcessId(w, nullptr), GetCurrentThreadId());
            if (std::find(keysDown.begin(), keysDown.end(), UINT(wp)) == keysDown.end()) keysDown.push_back(UINT(wp));
            if (!closeRequested) menu.OnKeyDown(UINT(wp), DikFromKeyMessage(wp, lp), repeat, GetTickCount());
            return true;
        }
        case WM_KEYUP: case WM_SYSKEYUP: case WM_CHAR: case WM_SYSCHAR: case WM_DEADCHAR: case WM_SYSDEADCHAR:
        case WM_CONTEXTMENU: case WM_LBUTTONDOWN: case WM_LBUTTONUP: case WM_RBUTTONDOWN: case WM_RBUTTONUP: case WM_MBUTTONDOWN: case WM_MBUTTONUP:
            return true;
        case WM_SYSCOMMAND: if ((wp & 0xfff0) == SC_KEYMENU) return true; break;
        case WM_ACTIVATEAPP: if (!wp) { closeRequested = true; closeReason = "focus lost"; } break;
        case WM_KILLFOCUS: closeRequested = true; closeReason = "focus lost"; break;
        default: break;
        }
        return false;
    }
    if (msg != WM_KEYDOWN && msg != WM_SYSKEYDOWN) return false;
    EnsureConfig();
    for (int m = 0; m < kModActions; ++m) {
        if (!cfg.mod[m].vk || int(wp) != cfg.mod[m].vk) continue;
        if (!(lp & (1l << 30))) Trigger(m, "key");
        return true;
    }
    return false;
}
// debug mailbox command "menu-open" (debug_warp=1, proxy.cpp): the same request as F5 / Start, for harness QA
void RequestOpen(const char* source) { detail::Trigger(ModMenu, source); }
// releasePilotDeviceResources (proxy.cpp): device reset / rebuild
void ReleaseDevice() {
    using namespace detail;
    gfx.ReleaseTextures(); ReleaseCapture();
    if (modal) { closeRequested = true; closeReason = "device reset"; }
}
}  // namespace re3menu
