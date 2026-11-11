#pragma once
// fx 2026-11-08 options: keys for the two native option menus of the GOG build (both exist, neither has a working key on
// the user's PC). Report: runtime/staging/fx-2026-11-08/options/REPORT.ru.txt, evidence qa/fx-2026-11-08/options.
//
// 1. PC "OPTION" window = Win32 property sheet "Setting Up Resident Evil 3" (Video / Audio / Keyboard / Controls (pad) /
//    Others; RT_DIALOG 145..152, PropertySheetA at 40123F). The original Readme says F5; the GOG exe binds it to F1 only
//    (RT_ACCELERATOR 113: F1 -> WM_COMMAND 40002, Alt+F4 / Esc -> 40001 quit box) and the window procedure 404760 has no
//    WM_KEYDOWN case, so F5 does nothing with or without the mod. On the user's PC F1 is a global hotkey of another program
//    (RegisterHotKey(F1) -> ERROR_HOTKEY_ALREADY_REGISTERED, the NVIDIA overlay answers "Screenshot saved"), so F1 never
//    reaches the game either. options_key posts the same WM_COMMAND 40002 the F1 accelerator sends (game thread, message
//    pump); the dialog is modal inside the game's window procedure (the game is frozen while it is open) and saves to
//    bio3.ini next to the exe itself on OK.
// 2. Legacy PSX Options menu (bio19/prog/bin/option.bin = program 4 of the task table 517970, entry 449640: KEY CONFIG
//    TYPE A/B/C, VIBRATION, SOUND, SCREEN/MONITOR ADJUST, EXIT) - dead code in the PC build:
//    - the gameplay controller 42B175 (byte A587A1: 1 request, 2 fade, 3 fade-in) runs program 4 in task slot 1 when the
//      request bit A5C880.00100000 (flag 0:0B) is set: 42B294 -> 42AF30(1,4) -> 42B2BF clears the bit; on exit the program
//      (44A0C0) reloads Core00u.tim and requests the room camera again (A628B6=2);
//    - nothing sets that bit: no writer in the EXE code, no room SCD (131 rooms) sets/checks flag 0:0B, and the title
//      sub-state 4 (4514B0 -> 451590 -> program 4) is never entered (the title has EASY/HARD/LOAD GAME/EPILOGUE/EXIT).
//    The request below is the native status-screen request (42B7A9..42B8C2: same guards, A587A1 = 1 published last) with
//    the options bit instead of the inventory bit A61F5C.0200. The options-only path returns A5CBD0 = 0 (A352AC stays 0,
//    42B34F; 44996A), so the low 24 bits of A5CBD0 are put back after the menu closed.
//
// pilot.ini: options_key = virtual key for the PC OPTION window (default 116 = F5 as in the Readme; 0 = off, native: F5
//   does nothing); options_legacy_key = virtual key for the PSX menu in gameplay (default 0 = off; e.g. 117 = F6;
//   F10 does not work: it is a system key, WM_SYSKEYDOWN).
//   QA: pilot.command options-open (PSX menu, debug_warp=1 only).
// RE3_MERCE.exe: both off (keys stay free; its game loop / window are not verified).
#include <windows.h>
#include <cstdint>
#include <cstring>
#include <string>

namespace re3options {

inline volatile LONG keyRequest = 0;       // legacy PSX menu, set from the window procedure (game thread, message pump)
inline volatile LONG commandRequest = 0;   // pilot.command options-open
inline volatile LONG pcRequest = 0;        // PC OPTION window
inline HWND keyWindow = nullptr;           // window that received the key
inline int optionsKey = -1, legacyKey = -1;   // -1 = not read yet

// controls round 2026-11-09: options_key now opens the 3DD menu (mod_menu.h); the PC window opens from its row
// "Game settings window", or from options_key when the menu is off (pilot.ini menu=0 / checks failed): pcRequest.
inline bool OnKeyDown(HWND w, WPARAM wp, LPARAM lp) {
    (void)w;
    const bool repeat = (lp & (1l << 30)) != 0;
    if (legacyKey > 0 && wp == static_cast<WPARAM>(legacyKey)) { if (!repeat) InterlockedExchange(&keyRequest, 1); return true; }
    return false;
}
// the game's main window (class "Resident Evil 3", wndproc 410670 -> 404760) of this process
inline HWND GameWindow() {
    HWND h = nullptr;
    while ((h = FindWindowExA(nullptr, h, "Resident Evil 3", nullptr)) != nullptr) {
        DWORD pid = 0; GetWindowThreadProcessId(h, &pid);
        if (pid == GetCurrentProcessId()) return h;
    }
    return nullptr;
}
inline void RequestFromCommand() { InterlockedExchange(&commandRequest, 1); }

template<class Log>
struct Runtime {
    enum Stage { Idle, Published, Running };
    Stage stage = Idle;
    bool ready = false, checked = false;
    unsigned waitFrames = 0, stageFrames = 0, refusedLogged = 0, requests = 0;
    uint32_t savedFlags2 = 0;   // A5CBD0 at the request
    const char* source = "";

    // VA of ResidentEvil3.exe (base 400000) -> address in the running process (data RVAs are identical in both exes)
    static uint8_t* At(uintptr_t base, uint32_t va) { return reinterpret_cast<uint8_t*>(base + (va - 0x400000)); }
    static uint32_t D(uintptr_t b, uint32_t va) { return *reinterpret_cast<uint32_t*>(At(b, va)); }
    static uint16_t W(uintptr_t b, uint32_t va) { return *reinterpret_cast<uint16_t*>(At(b, va)); }
    static uint8_t B(uintptr_t b, uint32_t va) { return *At(b, va); }

    // the exact native consumer and program table entry (bytes of the shipped GOG exe)
    static bool VerifyConsumer(uintptr_t base) {
        static const uint8_t test[] = {0xf7, 0x05, 0x80, 0xc8, 0xa5, 0x00, 0x00, 0x00, 0x10, 0x00, 0x74, 0x29,   // 42B294 test A5C880,100000; je
                                       0x6a, 0x04, 0x6a, 0x01, 0xc6, 0x05, 0x75, 0xc8, 0xa5, 0x00, 0x00,        // push 4; push 1; mov A5C875,0
                                       0xe8};                                                                  // call 42AF30
        static const uint8_t clear[] = {0x25, 0xff, 0xff, 0xef, 0xff, 0xa3, 0x80, 0xc8, 0xa5, 0x00};            // 42B2BF and eax,FFEFFFFF; mov A5C880,eax
        static const uint8_t controller[] = {0xa0, 0xa1, 0x87, 0xa5, 0x00, 0x48, 0x74, 0x29};                   // 42B179 mov al,[A587A1]; dec eax; je
        if (memcmp(At(base, 0x42b294), test, sizeof(test)) || memcmp(At(base, 0x42b2bf), clear, sizeof(clear)) ||
            memcmp(At(base, 0x42b179), controller, sizeof(controller))) return false;
        const uint8_t* call = At(base, 0x42b2ab);
        if (reinterpret_cast<uintptr_t>(call + 5) + *reinterpret_cast<const int32_t*>(call + 1) != reinterpret_cast<uintptr_t>(At(base, 0x42af30))) return false;
        return D(base, 0x517970 + 4 * 12 + 4) == base + (0x449640 - 0x400000);   // program 4 entry = option.bin 449640
    }

    // the guards of the native status-screen request 42B7A9..42B85A plus the idle-QA exclusions of
    // static3d/qa-control/options-native-request.txt; returns nullptr when the request may be published
    static const char* Blocked(uintptr_t b) {
        if (W(b, 0xa591a4)) return "task slot 1 busy";                                   // 42B0C0(1)
        if (D(b, 0xa5c880) & 0x115b0480u) return "A5C880 busy (demo/overlay/door/options/save/movie/ui/scenario)";
        if (D(b, 0xa61f5c) & 0x10400308u) return "A61F5C busy (event/loading/inventory/actor)";
        if (W(b, 0xa61faa) != B(b, 0xa5cbee)) return "camera change";                     // [A5A760+784A] vs [A5A760+248E]
        if (B(b, 0xa628b6)) return "camera transition";
        if (B(b, 0xa5cd21)) return "A5CD21 busy";                                       // [A5A760+25C1]
        if (B(b, 0xa587a1) || B(b, 0xa587a0)) return "controller or room request busy";
        if (B(b, 0xa4cfdb) & 0x0e) return "A4CFDB busy";
        if (B(b, 0xa57468) & 0x80) return "message text";
        if (D(b, 0xa5cbd0) & 0xff000000u) return "update domains masked";
        if (D(b, 0xa61d44) || D(b, 0xa61d48)) return "interaction/dialog controller";
        if (B(b, 0xa628c4)) return "fade";
        if (D(b, 0xa5cbd4) & 0x40000000u) return "diagnostic pause";
        if (B(b, 0xa5cbf8) != 1) return "player not in normal control";                 // player routine (+4)
        if (W(b, 0xa61fa6) > 6) return "no stage";
        return nullptr;
    }

    void Tick(uintptr_t base, bool mainExe, const std::string& ini, Log log) {
        if (!checked) {
            checked = true;
            ready = mainExe && VerifyConsumer(base);
            legacyKey = ready ? static_cast<int>(GetPrivateProfileIntA("pilot", "options_legacy_key", 0, ini.c_str())) : 0;
            optionsKey = mainExe ? static_cast<int>(GetPrivateProfileIntA("pilot", "options_key", VK_F5, ini.c_str())) : 0;   // Mercenaries: keys stay free
            log("OPTIONS_MENU options_key=%02x -> 3DD menu (Setting Up window via its row / menu=0, WM_COMMAND 40002) legacy_ready=%u legacy_key=%02x (option.bin via controller A587A1)",
                optionsKey, ready, legacyKey);
        }
        if (InterlockedExchange(&pcRequest, 0)) {
            HWND game = GameWindow(); char cls[64] = "";
            if (keyWindow) GetClassNameA(keyWindow, cls, sizeof(cls));
            if (game && IsWindowEnabled(game) && PostMessageA(game, WM_COMMAND, 40002, 0))
                log("OPTIONS_PC_WINDOW requested (WM_COMMAND 40002 to %p, key window class '%s')", static_cast<void*>(game), cls);
            else log("OPTIONS_PC_WINDOW refused: game window %p enabled=%d (dialog already open?)", static_cast<void*>(game), game ? IsWindowEnabled(game) : 0);
        }
        const bool key = InterlockedExchange(&keyRequest, 0) != 0, command = InterlockedExchange(&commandRequest, 0) != 0;
        if ((key || command) && stage == Idle && !waitFrames) { waitFrames = 45; refusedLogged = 0; source = command ? "command" : "key"; }
        if (!ready) { if (waitFrames) { log("OPTIONS_REQUEST %s refused: exe not verified", source); waitFrames = 0; } return; }
        switch (stage) {
        case Idle:
            if (!waitFrames) return;
            if (const char* why = Blocked(base)) {
                if (--waitFrames == 0)
                    log("OPTIONS_REQUEST %s refused: %s (A5C880=%08x A61F5C=%08x A5CBD0=%08x A587A1=%u player=%u slot1=%u)", source, why,
                        D(base, 0xa5c880), D(base, 0xa61f5c), D(base, 0xa5cbd0), B(base, 0xa587a1), B(base, 0xa5cbf8), W(base, 0xa591a4));
                return;
            }
            waitFrames = 0; savedFlags2 = D(base, 0xa5cbd0);
            // the native status-screen request order: request flag, controller byte last (42B8C2). A587E4 is not written:
            // only the inventory/save paths copy it to A352AC, the options-only path ignores it (A5CBD0 is put back below).
            *reinterpret_cast<uint32_t*>(At(base, 0xa5c880)) = D(base, 0xa5c880) | 0x00100000u;
            *At(base, 0xa587a1) = 1;
            stage = Published; stageFrames = 0; ++requests;
            log("OPTIONS_REQUEST %s published #%u room R%X%02X A5C880=%08x A5CBD0=%08x", source, requests,
                (W(base, 0xa61fa6) + 1) & 15, W(base, 0xa61fa8) & 0xff, D(base, 0xa5c880), savedFlags2);
            return;
        case Published:
            ++stageFrames;
            if (!(D(base, 0xa5c880) & 0x00100000u)) {   // 42B2BF: program 4 scheduled and the request bit cleared
                stage = Running; stageFrames = 0;
                log("OPTIONS_TASK running slot1=%u A587A1=%u", W(base, 0xa591a4), B(base, 0xa587a1));
            } else if (stageFrames > 90 && B(base, 0xa587a1) == 1 && !(D(base, 0xa61f5c) & 0x200)) {
                // the controller never took it (still state 1, e.g. no gameplay loop): withdraw our own two writes
                *reinterpret_cast<uint32_t*>(At(base, 0xa5c880)) = D(base, 0xa5c880) & ~0x00100000u;
                *At(base, 0xa587a1) = 0;
                stage = Idle;
                log("OPTIONS_REQUEST withdrawn: controller did not consume it in 90 frames");
            }
            return;
        case Running:
            ++stageFrames;
            if (W(base, 0xa591a4) == 0 && B(base, 0xa587a1) == 0) {
                const uint32_t now = D(base, 0xa5cbd0), lost = savedFlags2 & 0x00ffffffu & ~now;
                if (lost) *reinterpret_cast<uint32_t*>(At(base, 0xa5cbd0)) = now | lost;
                log("OPTIONS_TASK closed after %u frames A5CBD0 %08x -> %08x (restored %08x) A5C880=%08x room R%X%02X", stageFrames,
                    savedFlags2, now, lost, D(base, 0xa5c880), (W(base, 0xa61fa6) + 1) & 15, W(base, 0xa61fa8) & 0xff);
                stage = Idle;
            }
            return;
        }
    }
};

}  // namespace re3options
