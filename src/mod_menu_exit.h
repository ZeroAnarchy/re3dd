#pragma once
// Round 7 (2026-11-10), agent "exit": the 3DD menu rows "Exit to title screen" and "Exit to desktop" - the exe facts they
// rest on, as data the game side (mod_menu.h) checks in memory at install and the offline test checks in both exe files
// (qa/r7-2026-11-10/exit/tools/menu_tests.cpp). Report: runtime/staging/r7-2026-11-10/exit/REPORT.ru.txt, the listings
// were read with qa/r7-2026-11-10/exit/tools/exit_rx.py / exit_verify.py (static, the game was not started).
// Addresses below are ResidentEvil3.exe (GOG, base 400000); RE3_MERCE.exe has the same code at the addresses exe_map.h
// gives (data at the same addresses), the GOG quit cave 509620 at the same address in both.
//
// EXIT TO DESKTOP = what "Yes" of the game's own quit box does.
//   Esc / Alt+F4 -> accelerator -> WM_COMMAND 40001 -> window procedure 404760 -> 40495D: push 0 / push 0 / push 10h
//   (WM_CLOSE) / push hwnd / jmp 509620 (GOG cave): [5323C8] = 1, arrow cursor, MessageBoxA("Are you sure you want to
//   quit?", MB_YESNO | MB_ICONQUESTION); IDYES -> 50964A: SendMessageA(hwnd, WM_CLOSE, 0, 0) with the four pushed
//   arguments; anything else -> 408870 (cursor off) and DefWindowProc.
//   WM_CLOSE (404760 switch 404B24[404B48[10h - 2] = 5] = 404892): 4106E0(0) ([53498C] = 0), DestroyWindow(hwnd).
//   WM_DESTROY (case 0 = 4048AC): window object vt+4 = 4088A0: 410500 (window off the list [534988]), PostQuitMessage(0).
//   The message pump 408450 returns 0 on WM_QUIT (4084CB), the main loop 410280 then leaves (4102E5..41030C): 4134E0
//   ends every task thread (kill flag + SetEvent; a parked thread ends itself in 4133F0), WinMain 403610 runs 408510
//   (display teardown app vt+20 = 4043D0 -> 40AFF0) and returns: the process ends.
//   The row posts WM_CLOSE to the game window (hwnd of the window object, [app+4+28]) after the menu loop, from the main
//   game loop's input poll: the window thread takes it at the game's next own yield (VSync 420AE0), i.e. outside a frame.
//   It must NOT be handled while the menu loop holds a frame open: 40AFF0 spins until the "frame open" flag
//   [renderer+10CC10] is clear, and the killed game task would never clear it (the process would hang). Hence: posted only
//   after the menu loop, and the menu cannot open again once it is posted.
//
// EXIT TO TITLE = the game's own soft reset.
//   The game loop 4237B0 (task thread 1) tests the system flags right after its input poll: 4238C6 call 42CAD0;
//   4238CB mov eax,[A5C880]; test eax,200000h; jz; 4238D7 call 424B50; jmp 4237DA (next frame). Writers of that bit:
//     42BDF0  the gameplay process 42B0E0 when it ends (game over: diedemo.bin 44CA10 cleared the game-active bit 8000000h;
//             after the results of a finished game) - then 42AFD0 ends its task;
//     42CE09  the input poll itself when the title's attract demo (Pdemo0x.dat, started by title phase 4517B0 after 30 s)
//             ends or is interrupted - i.e. from the middle of live gameplay, with the gameplay task parked;
//     442CA8  ending.bin, 44994D option.bin (dead in the PC build).
//   424B50: pad tables 42D1C0, sound tables 4304A0, display off 4235D0(0), VSync, screen cleared 4231E0, 424C60(1, 0),
//   task slots 0..2 killed 42B000 (CloseTh -> 4135E0), sound sequences stopped 4275F0 (429C60), game state cleared 4242C0
//   (A5CBD0.. 5A90h bytes, flags &= 88h, fades off), [A5C858] = FFFFFFFF, slot 0 = program 0 (title.bin 44F760; its
//   loader 44F740 clears the title object A3F680). The title then plays roopne.str and shows its menu, as after a game over.
//   The row sets bit 200000h of [A5C880] in the hook right after 42CAD0 of the call at 4238C6 (return address 4238CB) -
//   the same place and thread the demo path sets it - so 424B50 runs before any task of the next frame.
//   Task slots (PSX threads, here OS threads parked in ChangeTh): 80h bytes each from A59124: +0 state (0 free), +4 entry,
//   +8 thread id (42AF90). Slot 0 = title 44F760 -> gameplay 42B0E0 (set by 450ABE) -> ending 442C80; slot 1 = overlay
//   tasks: the programs of the table 517970 (+4 entry: 2 mem_card.bin 44CEC0 = save screen, 3 diedemo.bin 44B8E0 = game
//   over), the status screen 45E2F0, the door / room change 42C750, the start loader 469400. The game itself opens the
//   status screen only while slot 1 is free (42B7A9: 42B0C0(1) == 0) - the row is offered under the same condition.
#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include "mod_menu_ui.h"

namespace re3menu {
namespace exitfacts {

// ---- data (RVA; .rdata / .data sit at the same addresses in both exes) ----
constexpr uint32_t kFlags = 0x65c880;                    // A5C880 system flags
constexpr uint32_t kFlagSoftReset = 0x00200000u;         // tested at 4238D0 -> 424B50
constexpr uint32_t kFlagDemo = 0x10000000u;              // title attract demo plays (451887)
constexpr uint32_t kFlagGameActive = 0x08000000u;        // set when the game start has loaded (42C1F5), cleared by diedemo.bin (44CA21)
constexpr uint32_t kFlagDeath = 0x00020000u;             // the player died, game over requested (4638C5)
constexpr uint32_t kFlagGameOver = 0x00000040u;          // diedemo.bin started (42BA8F)
constexpr uint32_t kFlagSaveRequest = 0x00080000u;       // save screen requested (42B2C9 -> program 2)
constexpr uint32_t kSlot0State = 0x659124, kSlot0Entry = 0x659128;   // word, dword
constexpr uint32_t kSlot1State = 0x6591a4, kSlot1Entry = 0x6591a8;
constexpr uint32_t kProgramTable = 0x117970;             // 517970: {name, entry, loader} x 12 bytes
constexpr int kProgramMemCard = 2, kProgramDieDemo = 3;
constexpr uint32_t kWindowVtable = 0x10a6b0;             // window object vtable: +4 = WM_DESTROY handler 4088A0
constexpr uint32_t kCloseSwitchIndex = 0x4b48, kCloseSwitchTable = 0x4b24;   // window procedure switch (msg - 2), in .text
// ---- code (RVA of ResidentEvil3.exe; exe_map.h translates for RE3_MERCE.exe) ----
constexpr uint32_t kGameplayEntry = 0x2b0e0;             // 42B0E0 the gameplay process (task slot 0 while a game runs)
constexpr uint32_t kMainLoopPollReturn = 0x238cb;        // return address of the input poll call 4238C6 in the game loop

// A byte pattern ("83 ec 08 e8 ?? ?? ?? ??": ?? = any byte) at a code RVA. raw: not translated (the GOG caves at the end
// of .text are outside exe_map.h and at the same address in both exes).
struct Pattern { uint32_t rva; const char* bytes; const char* what; bool raw; };
// A rel32 call / jmp at a code RVA that must reach a target RVA (both translated unless raw).
struct Branch { uint32_t at; uint32_t target; const char* what; bool rawAt, rawTarget; };
// An absolute code pointer stored at an RVA: imm32 of a push, a switch table entry, a vtable slot.
struct Pointer { uint32_t at; uint32_t target; const char* what; bool code; };   // code: 'at' is a (translated) code RVA

static const Pattern kTitlePatterns[] = {
    {0x238c6, "e8", "4238C6 call 42CAD0 (input poll) in the game loop", false},
    {0x238cb, "a1 80 c8 a5 00 a9 00 00 20 00 74 0a e8 ?? ?? ?? ?? e9 f9 fe ff ff", "4238CB test [A5C880],200000h -> call 424B50 -> next frame", false},
    {0x24b50, "83 ec 08 e8 ?? ?? ?? ?? e8 ?? ?? ?? ?? 6a 00 66 c7 44 24 04 00 00 66 c7 44 24 06 00 00 66 c7 44 24 08 40 01 66 c7 44 24 0a e0 01 e8",
     "424B50 soft reset: tables, display off", false},
    {0x24b9c, "6a 00 6a 01 e8 ?? ?? ?? ?? 6a 00 e8 ?? ?? ?? ?? 6a 01 e8 ?? ?? ?? ?? 6a 02 e8 ?? ?? ?? ?? e8 ?? ?? ?? ?? e8 ?? ?? ?? ?? 6a 00 e8",
     "424B9C soft reset: task slots 0..2 killed, sound off, state cleared", false},
    {0x24bcb, "6a 00 6a 00 c7 05 58 c8 a5 00 ff ff ff ff e8 ?? ?? ?? ?? 83 c4 40 c3", "424BCB soft reset: slot 0 = program 0 (title)", false},
    {0x2bdf0, "81 0d 80 c8 a5 00 00 00 20 00 e8", "42BDF0 gameplay process end sets the soft-reset bit", false},
    {0x2cdfe, "a1 80 c8 a5 00 88 0d bd 28 a6 00 0d 00 00 20 00 c6 05 ee 87 a5 00 00 a3 80 c8 a5 00", "42CDFE demo end sets the soft-reset bit in the input poll", false},
    {0x2b0e0, "51 53 55 56 57 be 60 a7 a5 00 e8", "42B0E0 gameplay process", false},
    {0x50abe, "68 ?? ?? ?? ?? e8", "450ABE title -> gameplay process", false},
    {0x2af90, "8b 44 24 04 8b 4c 24 08 c1 e0 07 89 88 28 91 a5 00 66 c7 80 24 91 a5 00 02 00 c3", "42AF90 task slot layout (A59124 + slot * 80h)", false},
    {0x2b000, "56 8b 74 24 08 c1 e6 07 81 c6 24 91 a5 00 66 83 3e 00 74 1b 66 c7 06 00 00 e8", "42B000 kill a task slot", false},
    {0x2ba8a, "25 ff ff fd ff 0c 40 a3 80 c8 a5 00", "42BA8A game over started (bit 40h)", false},
    {0x2c1f0, "a1 80 c8 a5 00 0d 00 00 00 08", "42C1F0 game active (bit 8000000h)", false},
    {0x4ca19, "a1 80 c8 a5 00 83 c4 08 25 ff f5 ff 77", "44CA19 diedemo.bin end clears game active", false},
    {0x638bd, "a1 80 c8 a5 00 80 c9 80 0d 00 00 02 00 a3 80 c8 a5 00", "4638BD player death (bit 20000h)", false},
    {0x51884, "80 e4 ef 0d 00 00 00 10 a3 80 c8 a5 00", "451884 title starts the attract demo (bit 10000000h)", false},
    {0x2b2c9, "f7 05 80 c8 a5 00 00 00 08 00 74 34 6a 02 6a 01 e8", "42B2C9 save screen request -> program 2", false},
    {0x2b7a9, "6a 01 e8 ?? ?? ?? ?? 83 c4 04 85 c0 0f 85", "42B7A9 the game's own guard: status screen only with task slot 1 free", false},
    {0x2b0c0, "8b 44 24 04 33 c9 c1 e0 07 66 8b 88 24 91 a5 00 8b c1 c3", "42B0C0 state word of a task slot", false},
};
static const Branch kTitleBranches[] = {
    {0x238d7, 0x24b50, "4238D7 call 424B50", false, false},
    {0x24b53, 0x2d1c0, "424B53 call 42D1C0", false, false},
    {0x24b58, 0x304a0, "424B58 call 4304A0", false, false},
    {0x24b7b, 0x235d0, "424B7B call 4235D0", false, false},
    {0x24ba0, 0x24c60, "424BA0 call 424C60", false, false},
    {0x24ba7, 0x2b000, "424BA7 call 42B000", false, false},
    {0x24bae, 0x2b000, "424BAE call 42B000", false, false},
    {0x24bb5, 0x2b000, "424BB5 call 42B000", false, false},
    {0x24bba, 0x275f0, "424BBA call 4275F0", false, false},
    {0x24bbf, 0x242c0, "424BBF call 4242C0", false, false},
    {0x24bd9, 0x2af30, "424BD9 call 42AF30", false, false},
    {0x2bdfa, 0x2afd0, "42BDFA call 42AFD0", false, false},
    {0x50ac3, 0x2b040, "450AC3 call 42B040", false, false},
    {0x2b2d9, 0x2af30, "42B2D9 call 42AF30", false, false},
    {0x2b7ab, 0x2b0c0, "42B7AB call 42B0C0", false, false},
};
static const Pointer kTitlePointers[] = {
    {0x50abf, kGameplayEntry, "450ABE push 42B0E0", true},
};
static const Pattern kDesktopPatterns[] = {
    {0x4771, "8b 5e 28", "404771 hwnd = [window object + 28h]", false},
    {0x4783, "8d 45 fe 83 f8 1a 0f 87 ?? ?? ?? ?? 33 c9 8a 88 48 4b 40 00 ff 24 8d 24 4b 40 00", "404783 window procedure switch (msg - 2)", false},
    {0x495d, "6a 00 6a 00 6a 10 53 e9", "40495D WM_COMMAND 40001 -> quit box with WM_CLOSE prepared", false},
    {0x109620, "e8 ?? ?? ?? ?? 6a 24 68 24 44 51 00 68 c7 3b 51 00 53 ff 15 0c a2 50 00 83 f8 06 74 0d 83 c4 10 e8 ?? ?? ?? ?? e9 ?? ?? ?? ?? ff 15 94 a2 50 00 e9",
     "509620 quit box: IDYES -> SendMessageA(hwnd, WM_CLOSE, 0, 0)", true},
    {0x4892, "6a 00 e8 ?? ?? ?? ?? 83 c4 04 53 ff 15 14 a2 50 00 5f 5e 5d 33 c0 5b c2 0c 00", "404892 WM_CLOSE -> DestroyWindow(hwnd)", false},
    {0x48ac, "8b 06 8b ce ff 50 04", "4048AC WM_DESTROY -> window object vt+4", false},
    {0x88a0, "e8 ?? ?? ?? ?? 6a 00 ff 15 50 a2 50 00 c3", "4088A0 PostQuitMessage(0)", false},
    {0x84cb, "83 7f 40 12 75 97 5f 5e 5d 33 c0 5b c3", "4084CB message pump returns 0 on WM_QUIT", false},
    {0x102e5, "ff 52 04 85 c0 74 20", "4102E5 main loop leaves when the pump returns 0", false},
    {0x1030c, "8b cf e8", "41030C main loop end: task threads ended (4134E0)", false},
    {0xaff3, "8b 86 10 cc 10 00 85 c0 74 0a", "40AFF3 display teardown waits for the frame-open flag", false},
    {0xbc95, "c7 86 10 cc 10 00 00 00 00 00", "40BC95 frame function clears the frame-open flag after its flip", false},
};
static const Branch kDesktopBranches[] = {
    {0x4964, 0x109620, "404964 jmp 509620", false, true},
    {0x109650, 0x496a, "509650 jmp 40496A", true, false},
    {0x4894, 0x106e0, "404894 call 4106E0", false, false},
    {0x1030e, 0x134e0, "41030E call 4134E0", false, false},
};
static const Pointer kDesktopPointers[] = {
    {kCloseSwitchTable + 5 * 4, 0x4892, "switch case 5 = 404892 (WM_CLOSE)", true},
    {kCloseSwitchTable + 0 * 4, 0x48ac, "switch case 0 = 4048AC (WM_DESTROY)", true},
    {kWindowVtable + 4, 0x88a0, "window vtable +4 = 4088A0", false},
};

// The exe as the checker sees it: bytes at an RVA (nullptr = not there), a code RVA translated for the exe (0xffffffff =
// not mapped), the image base the absolute pointers were linked for (400000h, also the load address: no relocations).
struct Image {
    virtual ~Image() {}
    virtual const uint8_t* At(uint32_t rva, size_t n) const = 0;
    virtual uint32_t Code(uint32_t rva) const = 0;
    virtual uint32_t LinkBase() const { return 0x400000u; }
};

inline bool Match(const uint8_t* p, const char* pattern) {
    if (!p) return false;
    for (const char* s = pattern; *s;) {
        while (*s == ' ') ++s;
        if (!*s) break;
        if (s[0] == '?' && s[1] == '?') { ++p; s += 2; continue; }
        unsigned v = 0;
        for (int i = 0; i < 2; ++i) {
            const char c = s[i]; v <<= 4;
            if (c >= '0' && c <= '9') v |= unsigned(c - '0'); else if (c >= 'a' && c <= 'f') v |= unsigned(c - 'a' + 10); else return false;
        }
        if (*p != uint8_t(v)) return false;
        ++p; s += 2;
    }
    return true;
}
inline size_t Length(const char* pattern) {
    size_t n = 0;
    for (const char* s = pattern; *s;) { while (*s == ' ') ++s; if (!*s) break; ++n; s += 2; }
    return n;
}
// nullptr = everything matches; else the first difference ("what" of the table row), text kept in a static buffer
template<size_t NP, size_t NB, size_t NQ>
inline const char* Verify(const Image& exe, const Pattern (&patterns)[NP], const Branch (&branches)[NB], const Pointer (&pointers)[NQ]) {
    static char why[160];
    for (const Pattern& s : patterns) {
        const uint32_t rva = s.raw ? s.rva : exe.Code(s.rva);
        if (rva == 0xffffffffu || !Match(exe.At(rva, Length(s.bytes)), s.bytes)) { sprintf_s(why, "%s differs", s.what); return why; }
    }
    for (const Branch& b : branches) {
        const uint32_t at = b.rawAt ? b.at : exe.Code(b.at), target = b.rawTarget ? b.target : exe.Code(b.target);
        const uint8_t* p = at == 0xffffffffu ? nullptr : exe.At(at, 5);
        if (!p || target == 0xffffffffu || (p[0] != 0xe8 && p[0] != 0xe9) || at + 5 + uint32_t(*reinterpret_cast<const int32_t*>(p + 1)) != target) {
            sprintf_s(why, "%s differs", b.what); return why;
        }
    }
    for (const Pointer& q : pointers) {
        const uint32_t at = q.code ? exe.Code(q.at) : q.at, target = exe.Code(q.target);
        const uint8_t* p = at == 0xffffffffu ? nullptr : exe.At(at, 4);
        if (!p || target == 0xffffffffu || *reinterpret_cast<const uint32_t*>(p) != exe.LinkBase() + target) { sprintf_s(why, "%s differs", q.what); return why; }
    }
    return nullptr;
}
// the window procedure's switch sends WM_CLOSE (10h) to case 5 and WM_DESTROY (2) to case 0; program table entries exist
inline const char* VerifyDesktop(const Image& exe) {
    if (const char* why = Verify(exe, kDesktopPatterns, kDesktopBranches, kDesktopPointers)) return why;
    const uint32_t index = exe.Code(kCloseSwitchIndex);
    const uint8_t* map = index == 0xffffffffu ? nullptr : exe.At(index, 0x1b);
    if (!map || map[WM_CLOSE - 2] != 5 || map[WM_DESTROY - 2] != 0) return "window procedure switch index 404B48 differs";
    return nullptr;
}
inline const char* VerifyTitle(const Image& exe) {
    if (const char* why = Verify(exe, kTitlePatterns, kTitleBranches, kTitlePointers)) return why;
    for (int program : {0, kProgramMemCard, kProgramDieDemo}) {
        const uint8_t* entry = exe.At(kProgramTable + uint32_t(program) * 12 + 4, 4);
        const uint32_t va = entry ? *reinterpret_cast<const uint32_t*>(entry) : 0;
        if (va < exe.LinkBase() + 0x1000 || va >= exe.LinkBase() + 0x10a000) return "program table 517970 differs";
    }
    return nullptr;
}

// ---- when "Exit to title screen" works: the game state as plain numbers (read by mod_menu.h, scripted by the test) ----
struct GameState {
    uint32_t flags = 0;                                  // [A5C880]
    uint16_t slot0State = 0, slot1State = 0;             // [A59124], [A591A4] (0 = free)
    uint32_t slot0Entry = 0, slot1Entry = 0;             // [A59128], [A591A8]
    uint32_t gameplayEntry = 0, memCardEntry = 0;        // 42B0E0 in this exe; program 2 of the table 517970
};
// S_Count = the soft reset may be requested; else the reason shown on the greyed row. In this order:
//   no game:     task slot 0 is not the gameplay process (title screen, boot screens, ending / credits), or the title's
//                attract demo plays (its own end is the same reset, with the key type restored first);
//   not now:     the game has not become active yet (its start is still loading), the player died, diedemo.bin runs or
//                has ended, or the soft reset is already requested - the game goes back to the title by itself;
//   save screen: mem_card.bin runs in task slot 1 or is requested (a save may be in progress);
//   not now:     any other task in slot 1 (status screen, door / room change, start loader, a puzzle or text overlay):
//                the row is offered only in plain gameplay, where the game itself would open the status screen (42B7A9).
inline Str TitleBlocked(const GameState& g) {
    if (!g.slot0State || !g.gameplayEntry || g.slot0Entry != g.gameplayEntry) return S_ExitNoGame;
    if (g.flags & kFlagDemo) return S_ExitNoGame;
    if (!(g.flags & kFlagGameActive) || (g.flags & (kFlagDeath | kFlagGameOver | kFlagSoftReset))) return S_ExitNotNow;
    if ((g.flags & kFlagSaveRequest) || (g.slot1State && g.memCardEntry && g.slot1Entry == g.memCardEntry)) return S_ExitSaveScreen;
    // slot 0 suspended: bit 40h of its state word (42B080, set by the overlay program that runs; 7Fh = the scheduler's
    // mark of the task it is running, 42AF02 - not a suspension)
    if (g.slot1State || ((g.slot0State & 0x40) && g.slot0State != 0x7f)) return S_ExitNotNow;
    return S_Count;
}
inline const char* BlockedName(Str s) {
    return s == S_Count ? "available" : s == S_ExitNoGame ? "no game runs" : s == S_ExitNotNow ? "not in plain gameplay (starting, ending or an overlay task in slot 1)" :
           s == S_ExitSaveScreen ? "save screen" : "exe check failed";
}

}  // namespace exitfacts
}  // namespace re3menu
