#pragma once
// Controls round 2026-11-09 (menu agent): binding model of the 3DD menu (F5 / Start). Pure logic, no game memory, no
// D3D: the menu (mod_menu_ui.h), the game side (mod_menu.h) and the offline test (qa/ctl-2026-11-09/menu/tools) use it.
// Report: runtime/staging/ctl-2026-11-09/menu/REPORT.ru.txt.
//
// Native bindings = the game's own tables in the application object ([534988] list, vtable 50A6B0):
//   +2B28 int key[14][16]          DirectInput scan codes (DIK), <= 0 = empty; bio3.ini [Keyboard] Key1..KeyE "200,72"
//   +2EA8 {int ofs,val}[14][16]    DIJOYSTATE offset + value, ofs < 0 = empty; bio3.ini [Gamepad] Key1..KeyE "4,-500"
// Action i (Key1..KeyE, exe tables 50A5D0 / 50A640) sets the PSX raw bit kNative[i].raw in the pad word 65C858.
// Pad test of the game (input object vt+88 = 410090): axis (DIDFT 1|2): value > v (v > 0) or value < v (v < 0), range
// -1000..1000 (DIPROP_RANGE at 4078B0); button (DIDFT 4|8): state byte != 0, v is ignored (default KeyB "57,0" is a
// button too); POV (DIDFT 0x10): POV == v exactly.
// GOG dinput.dll (goginput.ini onelayout=TRUE) presents every pad as "Gamepad" in one layout: lX/lY left stick (and the
// d-pad: dpadtoanalog=TRUE), lRx/lRy right stick, buttons 0 A/Cross 1 B/Circle 2 X/Square 3 Y/Triangle 4 LB/L1 5 RB/R1
// 6 LT/L2 7 RT/R2 8 Back/Share 9 Start/Options 10 LS/L3 11 RS/R3 (DIJOYSTATE offset 48 + button).
#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace re3menu {

constexpr int kActions = 14, kSlots = 16;
struct PadBind { int32_t ofs = -1, val = 0; bool Valid() const { return ofs >= 0; } };
static_assert(sizeof(PadBind) == 8, "game layout {int ofs,int val}");
struct Bindings { int32_t key[kActions][kSlots]; PadBind pad[kActions][kSlots]; };

struct NativeAction { const char* ini; uint16_t raw; };
// exe 50A5D0 (keyboard) / 50A640 (gamepad): ini key and PSX raw bit, index = table slot
static const NativeAction kNative[kActions] = {
    {"Key1", 0x1000}, {"Key2", 0x4000}, {"Key3", 0x8000}, {"Key4", 0x2000}, {"Key5", 0x0020}, {"Key6", 0x0040}, {"Key7", 0x0010},
    {"Key8", 0x0004}, {"Key9", 0x0008}, {"KeyA", 0x0002}, {"KeyB", 0x0100}, {"KeyC", 0x0800}, {"KeyD", 0x0080}, {"KeyE", 0x0001}};
// menu order (KeyE = PS L2: no table maps raw 0x0001 to an action, not shown)
static const int kMenuOrder[] = {0, 1, 2, 3, 12, 5, 8, 9, 7, 11, 6, 4, 10};
constexpr int kMenuActions = int(sizeof(kMenuOrder) / sizeof(kMenuOrder[0]));

// exe default table 50A308 (4 per action; the loader 405590 uses it when a Key line has no valid code)
static const int32_t kDefaultKeys[kActions][4] = {
    {200, 72, -1, -1}, {208, 80, -1, -1}, {203, 75, -1, -1}, {205, 77, -1, -1}, {-1, -1, -1, -1}, {47, -1, -1, -1}, {50, -1, -1, -1},
    {48, -1, -1, -1}, {31, -1, -1, -1}, {45, -1, -1, -1}, {16, -1, -1, -1}, {44, -1, -1, -1}, {57, 46, 28, -1}, {-1, -1, -1, -1}};
// GOG bio3.ini [Gamepad] without KeyB (PS Select; GOG has it on Start = 57): Start opens the 3DD menu and, since the fix of
// 2026-11-11b, Back (56) toggles the HD graphics (kMod below) - the controls round had moved KeyB there
static const PadBind kDefaultPad[kActions][2] = {
    {{4, -500}, {}}, {{4, 500}, {}}, {{0, -500}, {}}, {{0, 500}, {}}, {{}, {}}, {{50, 128}, {}}, {{54, 128}, {}},
    {{52, 128}, {}}, {{53, 128}, {}}, {{55, 128}, {}}, {{}, {}}, {{49, 128}, {}}, {{48, 128}, {}}, {{}, {}}};
constexpr int32_t kPadStart = 57, kPadBack = 56;

inline void DefaultKeyboard(Bindings& b) {
    for (int a = 0; a < kActions; ++a) for (int s = 0; s < kSlots; ++s) b.key[a][s] = s < 4 ? kDefaultKeys[a][s] : -1;
}
inline void DefaultPad(Bindings& b) {
    for (int a = 0; a < kActions; ++a) for (int s = 0; s < kSlots; ++s) b.pad[a][s] = s < 2 ? kDefaultPad[a][s] : PadBind{};
}

// ---- code the menu calls / relies on (ResidentEvil3.exe RVAs; RE3_MERCE.exe through exe_map.h). Checked once at startup
// (mod_menu.h VerifyCode) and offline against both exes on disk (qa/ctl-2026-11-09/menu/tools/menu_tests.cpp).
struct CodeSig { uint32_t rva; uint8_t bytes[24]; size_t n; const char* what; };
static const CodeSig kMenuCodeSigs[] = {
    {0x5810, {0x53, 0x55, 0x8b, 0xd9, 0x56, 0x57, 0xbf, 0xd4, 0xa5, 0x50, 0x00, 0x8d, 0xb3, 0x28, 0x2b, 0x00, 0x00, 0x8b, 0x8b, 0x00, 0x01, 0x00, 0x00}, 23, "405810 keyboard table +2B28 -> input object"},
    {0x5760, {0x81, 0xec, 0x14, 0x04, 0x00, 0x00, 0x53, 0x55, 0x56, 0x57, 0xbb, 0xd0, 0xa5, 0x50, 0x00, 0x8d, 0xa9, 0x28, 0x2b, 0x00, 0x00}, 21, "405760 save [Keyboard]"},
    {0x5a20, {0x51, 0x53, 0x55, 0x8b, 0xd9, 0x56, 0x57, 0xbf, 0x44, 0xa6, 0x50, 0x00, 0x8d, 0xb3, 0xa8, 0x2e, 0x00, 0x00, 0x8b, 0x8b, 0x00, 0x01, 0x00, 0x00}, 24, "405A20 pad table +2EA8 -> input object"},
    {0x5940, {0x81, 0xec, 0x24, 0x08, 0x00, 0x00, 0x8d, 0x94, 0x24, 0x24, 0x04, 0x00, 0x00, 0x56, 0x8b, 0xf1, 0x52, 0x8b, 0x8e, 0x00, 0x01, 0x00, 0x00}, 23, "405940 save [<pad name>]"},
    {0x209e0, {0x83, 0xec, 0x08, 0x53, 0x56, 0x57, 0x8b, 0xf9, 0x8b, 0x47, 0x04, 0x85, 0xc0}, 13, "4209E0 vblank clock re-base (system [5349BC] vt+0C -> 420950 jmp)"},
};

// ---- mod actions (pilot.ini: keyboard = Windows virtual key, gamepad = "ofs,val" like bio3.ini) ----
enum ModAction { ModMenu = 0, ModHd, ModFps60, ModReload, kModActions };
struct ModBinding { int vk = 0; PadBind pad; };
struct ModInfo { const char* keyIni; const char* padIni; int vk; PadBind pad; };
static const ModInfo kMod[kModActions] = {
    {"options_key", "menu_pad", VK_F5, {kPadStart, 128}},   // options_key: fx 2026-11-08 key, now opens this menu
    {"hd_key", "hd_pad", VK_F8, {kPadBack, 128}},           // fix 2026-11-11b: Back / Share (was the game's PS Select)
    {"fps60_key", "fps60_pad", VK_F7, {}},
    {"reload_key", "reload_pad", 'R', {51, 128}}};          // Y / Triangle: the face button free in the default layout

// ---- offsets ----
enum class PadKind { None, Axis, Pov, Button };
inline PadKind KindOf(int32_t ofs) {
    if (ofs < 0) return PadKind::None;
    if (ofs < 32) return (ofs & 3) ? PadKind::None : PadKind::Axis;
    if (ofs < 48) return (ofs & 3) ? PadKind::None : PadKind::Pov;
    return ofs < 80 ? PadKind::Button : PadKind::None;
}
// same physical input (game semantics): button by offset, axis by offset + direction, POV by offset + angle
inline bool SamePad(PadBind a, PadBind b) {
    if (!a.Valid() || !b.Valid() || a.ofs != b.ofs) return false;
    switch (KindOf(a.ofs)) {
    case PadKind::Button: return true;
    case PadKind::Axis: return (a.val < 0) == (b.val < 0) && a.val != 0 && b.val != 0;
    default: return a.val == b.val;
    }
}

// ---- names ----
struct KeyName { uint8_t dik; const wchar_t* en; const wchar_t* ru; };
static const KeyName kKeyNames[] = {
    {0x01, L"Esc", L"Esc"}, {0x0C, L"-", L"-"}, {0x0D, L"=", L"="}, {0x0E, L"Backspace", L"Backspace"}, {0x0F, L"Tab", L"Tab"},
    {0x1A, L"[", L"["}, {0x1B, L"]", L"]"}, {0x1C, L"Enter", L"Enter"}, {0x1D, L"Left Ctrl", L"\u041b\u0435\u0432\u044b\u0439 Ctrl"}, {0x27, L";", L";"},
    {0x28, L"'", L"'"}, {0x29, L"`", L"`"}, {0x2A, L"Left Shift", L"\u041b\u0435\u0432\u044b\u0439 Shift"}, {0x2B, L"\\", L"\\"}, {0x33, L",", L","},
    {0x34, L".", L"."}, {0x35, L"/", L"/"}, {0x36, L"Right Shift", L"\u041f\u0440\u0430\u0432\u044b\u0439 Shift"}, {0x37, L"Num *", L"Num *"},
    {0x38, L"Left Alt", L"\u041b\u0435\u0432\u044b\u0439 Alt"}, {0x39, L"Space", L"\u041f\u0440\u043e\u0431\u0435\u043b"}, {0x3A, L"Caps Lock", L"Caps Lock"}, {0x45, L"Num Lock", L"Num Lock"},
    {0x46, L"Scroll Lock", L"Scroll Lock"}, {0x47, L"Num 7", L"Num 7"}, {0x48, L"Num 8", L"Num 8"}, {0x49, L"Num 9", L"Num 9"},
    {0x4A, L"Num -", L"Num -"}, {0x4B, L"Num 4", L"Num 4"}, {0x4C, L"Num 5", L"Num 5"}, {0x4D, L"Num 6", L"Num 6"},
    {0x4E, L"Num +", L"Num +"}, {0x4F, L"Num 1", L"Num 1"}, {0x50, L"Num 2", L"Num 2"}, {0x51, L"Num 3", L"Num 3"},
    {0x52, L"Num 0", L"Num 0"}, {0x53, L"Num .", L"Num ."}, {0x56, L"\\ (102)", L"\\ (102)"}, {0x57, L"F11", L"F11"}, {0x58, L"F12", L"F12"},
    {0x9C, L"Num Enter", L"Num Enter"}, {0x9D, L"Right Ctrl", L"\u041f\u0440\u0430\u0432\u044b\u0439 Ctrl"}, {0xB5, L"Num /", L"Num /"},
    {0xB7, L"Print Screen", L"Print Screen"}, {0xB8, L"Right Alt", L"\u041f\u0440\u0430\u0432\u044b\u0439 Alt"}, {0xC5, L"Pause", L"Pause"},
    {0xC7, L"Home", L"Home"}, {0xC8, L"\u2191", L"\u2191"}, {0xC9, L"Page Up", L"Page Up"}, {0xCB, L"\u2190", L"\u2190"},
    {0xCD, L"\u2192", L"\u2192"}, {0xCF, L"End", L"End"}, {0xD0, L"\u2193", L"\u2193"}, {0xD1, L"Page Down", L"Page Down"},
    {0xD2, L"Insert", L"Insert"}, {0xD3, L"Delete", L"Delete"}, {0xDB, L"Left Win", L"\u041b\u0435\u0432\u044b\u0439 Win"}, {0xDC, L"Right Win", L"\u041f\u0440\u0430\u0432\u044b\u0439 Win"},
    {0xDD, L"Menu", L"Menu"}};
inline std::wstring KeyNameDik(int dik, bool ru) {
    if (dik <= 0 || dik > 0xff) return std::wstring();
    static const char kRow1[] = "1234567890", kRowQ[] = "QWERTYUIOP", kRowA[] = "ASDFGHJKL", kRowZ[] = "ZXCVBNM";
    if (dik >= 0x02 && dik <= 0x0B) return std::wstring(1, wchar_t(kRow1[dik - 0x02]));
    if (dik >= 0x10 && dik <= 0x19) return std::wstring(1, wchar_t(kRowQ[dik - 0x10]));
    if (dik >= 0x1E && dik <= 0x26) return std::wstring(1, wchar_t(kRowA[dik - 0x1E]));
    if (dik >= 0x2C && dik <= 0x32) return std::wstring(1, wchar_t(kRowZ[dik - 0x2C]));
    if (dik >= 0x3B && dik <= 0x44) return L"F" + std::to_wstring(dik - 0x3B + 1);
    for (const auto& k : kKeyNames) if (k.dik == dik) return ru ? k.ru : k.en;
    wchar_t name[64] = L"";   // anything else: the keyboard layout's own name
    if (GetKeyNameTextW(LONG(((dik & 0x7f) << 16) | ((dik & 0x80) ? (1 << 24) : 0)), name, 64) > 0) return name;
    wchar_t code[24]; swprintf_s(code, ru ? L"\u041a\u043b\u0430\u0432\u0438\u0448\u0430 %d" : L"Key %d", dik); return code;
}
// DirectInput code of a WM_KEYDOWN (scan code, extended flag) = what the game's DirectInput sees
inline int DikFromKeyMessage(WPARAM vk, LPARAM lp) {
    int scan = int((lp >> 16) & 0xff); const bool ext = (lp & (1 << 24)) != 0;
    if (vk == VK_PAUSE) return 0xC5;
    if (vk == VK_NUMLOCK) return 0x45;
    if (!scan) scan = int(MapVirtualKeyW(UINT(vk), MAPVK_VK_TO_VSC));
    return (scan & 0x7f) | (ext ? 0x80 : 0);
}
inline int DikFromVk(int vk) {
    if (vk <= 0) return 0;
    if (vk == VK_PAUSE) return 0xC5;
    if (vk == VK_NUMLOCK) return 0x45;
    switch (vk) {   // extended keys (MapVirtualKey drops the E0 prefix of most of them)
    case VK_UP: return 0xC8; case VK_DOWN: return 0xD0; case VK_LEFT: return 0xCB; case VK_RIGHT: return 0xCD;
    case VK_HOME: return 0xC7; case VK_END: return 0xCF; case VK_PRIOR: return 0xC9; case VK_NEXT: return 0xD1;
    case VK_INSERT: return 0xD2; case VK_DELETE: return 0xD3; case VK_DIVIDE: return 0xB5; case VK_RCONTROL: return 0x9D;
    case VK_RMENU: return 0xB8; case VK_LWIN: return 0xDB; case VK_RWIN: return 0xDC; case VK_APPS: return 0xDD; case VK_SNAPSHOT: return 0xB7;
    default: break;
    }
    const UINT ex = MapVirtualKeyW(UINT(vk), 4 /*MAPVK_VK_TO_VSC_EX*/);
    if (!ex) return 0;
    return int(ex & 0x7f) | (((ex >> 8) & 0xff) == 0xE0 ? 0x80 : 0);
}
inline std::wstring KeyNameVk(int vk, bool ru) {
    if (vk <= 0) return std::wstring();
    if (vk >= VK_F1 && vk <= VK_F24) return L"F" + std::to_wstring(vk - VK_F1 + 1);
    const int dik = DikFromVk(vk);
    if (dik) return KeyNameDik(dik, ru);
    wchar_t code[24]; swprintf_s(code, L"VK %d", vk); return code;
}
inline std::wstring PadName(PadBind b, bool ru) {
    if (!b.Valid()) return std::wstring();
    static const wchar_t* const kButtonsEn[12] = {L"A / Cross", L"B / Circle", L"X / Square", L"Y / Triangle", L"LB / L1", L"RB / R1",
                                                  L"LT / L2", L"RT / R2", L"Back / Share", L"Start / Options", L"LS / L3", L"RS / R3"};
    static const wchar_t* const kButtonsRu[12] = {L"A / \u041a\u0440\u0435\u0441\u0442", L"B / \u041a\u0440\u0443\u0433", L"X / \u041a\u0432\u0430\u0434\u0440\u0430\u0442", L"Y / \u0422\u0440\u0435\u0443\u0433\u043e\u043b\u044c\u043d\u0438\u043a", L"LB / L1", L"RB / R1",
                                                  L"LT / L2", L"RT / R2", L"Back / Share", L"Start / Options", L"LS / L3", L"RS / R3"};
    const bool neg = b.val < 0;
    switch (KindOf(b.ofs)) {
    case PadKind::Button: {
        const int n = (b.ofs - 48);
        if (n < 12) return ru ? kButtonsRu[n] : kButtonsEn[n];
        return (ru ? L"\u041a\u043d\u043e\u043f\u043a\u0430 " : L"Button ") + std::to_wstring(n + 1);
    }
    case PadKind::Axis:
        switch (b.ofs) {
        case 0: return std::wstring(ru ? L"\u041a\u0440\u0435\u0441\u0442./\u043b.\u0441\u0442\u0438\u043a " : L"D-pad/L-stick ") + (neg ? L"\u2190" : L"\u2192");
        case 4: return std::wstring(ru ? L"\u041a\u0440\u0435\u0441\u0442./\u043b.\u0441\u0442\u0438\u043a " : L"D-pad/L-stick ") + (neg ? L"\u2191" : L"\u2193");
        case 12: return std::wstring(ru ? L"\u041f\u0440. \u0441\u0442\u0438\u043a " : L"R-stick ") + (neg ? L"\u2190" : L"\u2192");
        case 16: return std::wstring(ru ? L"\u041f\u0440. \u0441\u0442\u0438\u043a " : L"R-stick ") + (neg ? L"\u2191" : L"\u2193");
        case 8: return std::wstring(ru ? L"\u041e\u0441\u044c Z " : L"Z axis ") + (neg ? L"\u2212" : L"+");
        case 20: return std::wstring(ru ? L"\u041e\u0441\u044c Rz " : L"Rz axis ") + (neg ? L"\u2212" : L"+");
        default: return (ru ? L"\u041f\u043e\u043b\u0437\u0443\u043d\u043e\u043a " : L"Slider ") + std::to_wstring((b.ofs - 24) / 4 + 1) + (neg ? L" \u2212" : L" +");
        }
    case PadKind::Pov: {
        const wchar_t* dir = b.val == 0 ? L"\u2191" : b.val == 9000 ? L"\u2192" : b.val == 18000 ? L"\u2193" : b.val == 27000 ? L"\u2190" : nullptr;
        std::wstring s = ru ? L"\u041a\u0440\u0435\u0441\u0442\u043e\u0432\u0438\u043d\u0430" : L"D-pad";
        if ((b.ofs - 32) / 4) s += L" " + std::to_wstring((b.ofs - 32) / 4 + 1);
        if (dir) return s + L" " + dir;
        return s + L" " + std::to_wstring(b.val / 100) + L"\u00b0";
    }
    default: break;
    }
    wchar_t code[32]; swprintf_s(code, L"%d,%d", b.ofs, b.val); return code;
}

// ---- ini text in the game's format (savers 405760 ",%d" / 405940 ",%d,%d", leading comma dropped) ----
inline std::string FormatKeys(const int32_t* keys) {
    std::string s; char part[16];
    for (int i = 0; i < kSlots; ++i) if (keys[i] > 0) { sprintf_s(part, ",%d", keys[i]); s += part; }
    return s.empty() ? s : s.substr(1);
}
inline std::string FormatPads(const PadBind* pads) {
    std::string s; char part[32];
    for (int i = 0; i < kSlots; ++i) if (pads[i].Valid()) { sprintf_s(part, ",%d,%d", pads[i].ofs, pads[i].val); s += part; }
    return s.empty() ? s : s.substr(1);
}
inline std::string FormatPad(PadBind b) { if (!b.Valid()) return std::string(); char t[32]; sprintf_s(t, "%d,%d", b.ofs, b.val); return t; }
inline PadBind ParsePad(const char* text) {
    PadBind b; if (!text) return b;
    while (*text == ' ') ++text;
    if (!*text) return b;
    char* end = nullptr; const long ofs = strtol(text, &end, 10);
    if (end == text || ofs < 0 || ofs >= 80) return b;
    long val = 128;
    if (*end == ',') { const char* v = end + 1; char* e2 = nullptr; const long parsed = strtol(v, &e2, 10); if (e2 != v) val = parsed; }
    b.ofs = int32_t(ofs); b.val = int32_t(val);
    if (KindOf(b.ofs) == PadKind::None || (KindOf(b.ofs) == PadKind::Axis && b.val == 0)) return PadBind{};
    return b;
}

// ---- the game's loaders, for the offline round-trip test and the startup fix below ----
// 405660: the line split at ',', each piece at most 15 characters (a longer one ends the list), atoi (405700), max numbers
inline int ParseIniNumbers(const char* text, int32_t* out, int max) {
    int n = 0;
    if (!text || !*text) return 0;
    for (const char* p = text; n < max;) {
        const char* comma = strchr(p, ',');
        const size_t len = comma ? size_t(comma - p) : strlen(p);
        if (len > 15) break;
        char piece[16]; memcpy(piece, p, len); piece[len] = 0;
        out[n++] = int32_t(atoi(piece));
        if (!comma) break;
        p = comma + 1;
    }
    return n;
}
// 405590 per action: up to 16 codes > 0; a line without one gets the exe default (50A308, 4 codes) - an emptied action
// comes back on the next start unless the mod clears it again (pilot.ini menu_keys_cleared, see ClearedKeysMask)
inline void LoadKeysLine(const char* text, Bindings& b, int a) {
    int32_t numbers[16]; const int n = ParseIniNumbers(text, numbers, 16);
    int w = 0;
    for (int s = 0; s < kSlots; ++s) b.key[a][s] = -1;
    for (int i = 0; i < n && w < kSlots; ++i) if (numbers[i] > 0) b.key[a][w++] = numbers[i];
    if (!w) for (int s = 0; s < 4; ++s) b.key[a][s] = kDefaultKeys[a][s];
}
// 405860 per action (section = the pad's name, "Gamepad" under GOG): up to 16 pairs with ofs >= 0, no default
inline void LoadPadsLine(const char* text, Bindings& b, int a) {
    int32_t numbers[32]; const int n = ParseIniNumbers(text, numbers, 32);
    int w = 0;
    for (int s = 0; s < kSlots; ++s) b.pad[a][s] = PadBind{};
    for (int i = 0; i < n && w < kSlots; i += 2) if (numbers[i] >= 0) b.pad[a][w++] = PadBind{numbers[i], i + 1 < n ? numbers[i + 1] : 0};
}
inline bool KeysLineEmpty(const char* text) {
    int32_t numbers[16]; const int n = ParseIniNumbers(text, numbers, 16);
    for (int i = 0; i < n; ++i) if (numbers[i] > 0) return false;
    return true;
}
inline bool HasDefaultKeys(int a) { return kDefaultKeys[a][0] > 0; }
// actions the menu left without a keyboard key although the exe has a default for them (bit = action index)
inline uint32_t ClearedKeysMask(const Bindings& b) {
    uint32_t mask = 0;
    for (int a = 0; a < kActions; ++a) {
        bool any = false;
        for (int s = 0; s < kSlots; ++s) if (b.key[a][s] > 0) any = true;
        if (!any && HasDefaultKeys(a)) mask |= 1u << a;
    }
    return mask;
}
// startup: an action in the mask whose bio3.ini line is still empty was given the default by the loader -> empty again.
// lines[a] = the [Keyboard] KeyN text. Returns the actions cleared now (the rest of the mask is stale: a key was set).
inline uint32_t ApplyClearedKeys(Bindings& b, uint32_t mask, const char* const* lines) {
    uint32_t cleared = 0;
    for (int a = 0; a < kActions; ++a) {
        if (!(mask & (1u << a)) || !HasDefaultKeys(a) || !KeysLineEmpty(lines[a])) continue;
        for (int s = 0; s < kSlots; ++s) b.key[a][s] = -1;
        cleared |= 1u << a;
    }
    return cleared;
}

// ---- editing (the menu's conflict rule: an input belongs to one action; it is moved, never duplicated) ----
inline int CountKeys(const Bindings& b, int a) { int n = 0; for (int s = 0; s < kSlots; ++s) if (b.key[a][s] > 0) ++n; return n; }
inline int CountPads(const Bindings& b, int a) { int n = 0; for (int s = 0; s < kSlots; ++s) if (b.pad[a][s].Valid()) ++n; return n; }
inline int FindKey(const Bindings& b, int dik, int* slot = nullptr) {
    for (int a = 0; a < kActions; ++a) for (int s = 0; s < kSlots; ++s) if (b.key[a][s] == dik) { if (slot) *slot = s; return a; }
    return -1;
}
inline int FindPad(const Bindings& b, PadBind p, int* slot = nullptr) {
    for (int a = 0; a < kActions; ++a) for (int s = 0; s < kSlots; ++s) if (SamePad(b.pad[a][s], p)) { if (slot) *slot = s; return a; }
    return -1;
}
inline void CompactKeys(Bindings& b, int a) {
    int w = 0; for (int s = 0; s < kSlots; ++s) if (b.key[a][s] > 0) b.key[a][w++] = b.key[a][s];
    for (; w < kSlots; ++w) b.key[a][w] = -1;
}
inline void CompactPads(Bindings& b, int a) {
    int w = 0; for (int s = 0; s < kSlots; ++s) if (b.pad[a][s].Valid()) b.pad[a][w++] = b.pad[a][s];
    for (; w < kSlots; ++w) b.pad[a][w] = PadBind{};
}
inline void RemoveKey(Bindings& b, int a, int dik) { for (int s = 0; s < kSlots; ++s) if (b.key[a][s] == dik) b.key[a][s] = -1; CompactKeys(b, a); }
inline void RemovePad(Bindings& b, int a, PadBind p) { for (int s = 0; s < kSlots; ++s) if (SamePad(b.pad[a][s], p)) b.pad[a][s] = PadBind{}; CompactPads(b, a); }
inline bool AppendKey(Bindings& b, int a, int dik) {
    CompactKeys(b, a);
    for (int s = 0; s < kSlots; ++s) if (b.key[a][s] <= 0) { b.key[a][s] = dik; return true; }
    return false;
}
inline bool AppendPad(Bindings& b, int a, PadBind p) {
    CompactPads(b, a);
    for (int s = 0; s < kSlots; ++s) if (!b.pad[a][s].Valid()) { b.pad[a][s] = p; return true; }
    return false;
}
// bindings a game action needs to be usable: arrows / stick are expected; nothing to enforce, the menu shows "-"

// ---- mod pad buttons against the game's [Gamepad] (startup): a mod button that a game action also uses would do both.
// A default mod button (no line in pilot.ini) gives way - off for this run, logged; one the player chose is kept and
// reported. The menu button itself is handled by the one-time pad layout change (Start freed). Returns the conflicts.
// One-time pad layout change (pilot.ini pad_layout=2): game actions on the menu button leave it; the GOG default KeyB
// (PS Select, "57,0" = Start) moves to Back (56) when Back is free, anything else on it is removed.
inline void FreeMenuPad(Bindings& b, PadBind menuPad, int& moved, int& removed) {
    moved = removed = 0;
    if (!menuPad.Valid()) return;
    const PadBind back{kPadBack, 128};
    for (int a = 0; a < kActions; ++a) {
        bool changed = false;
        for (int s = 0; s < kSlots; ++s) {
            if (!SamePad(b.pad[a][s], menuPad)) continue;
            if (a == 10 && !SamePad(menuPad, back) && FindPad(b, back) < 0) { b.pad[a][s] = back; ++moved; }
            else { b.pad[a][s] = PadBind{}; ++removed; }
            changed = true;
        }
        if (changed) CompactPads(b, a);
    }
}
// Fix 2026-11-11b (pilot.ini pad_layout=3; user: "make Back / Share toggle the HD mode by default instead of the useless
// function it has now"): Back leaves the game's KeyB (PS Select, where the layout-2 change above had put it) and becomes
// the HD toggle - unless the player uses Back for something else (another mod action, another game action) or has put the
// HD toggle on another button. Returns bit 1 = KeyB lost Back ([Gamepad] changed), bit 2 = the HD toggle got Back.
inline int BackToHd(Bindings& b, ModBinding* mods) {
    const PadBind back{kPadBack, 128};
    for (int m = 0; m < kModActions; ++m) if (m != ModHd && SamePad(mods[m].pad, back)) return 0;
    if (mods[ModHd].pad.Valid() && !SamePad(mods[ModHd].pad, back)) return 0;
    for (int a = 0; a < kActions; ++a) if (a != 10) for (int s = 0; s < kSlots; ++s) if (SamePad(b.pad[a][s], back)) return 0;
    int result = 0;
    if (FindPad(b, back) == 10) { RemovePad(b, 10, back); result |= 1; }
    if (!mods[ModHd].pad.Valid()) { mods[ModHd].pad = back; result |= 2; }
    return result;
}
struct PadConflict { int mod; int action; bool dropped; };
inline int ResolveModPadConflicts(const Bindings& b, ModBinding* mods, const bool* explicitPad, PadConflict* out, int max) {
    int n = 0;
    for (int m = 0; m < kModActions; ++m) {
        if (m == ModMenu || !mods[m].pad.Valid()) continue;
        const int a = FindPad(b, mods[m].pad);
        if (a < 0) continue;
        const bool drop = !explicitPad[m];
        if (drop) mods[m].pad = PadBind{};
        if (n < max) out[n] = PadConflict{m, a, drop};
        ++n;
    }
    return n;
}

// ---- language: same rule as the installer (Get-UICulture ru/uk/be/kk); pilot.ini menu_language 0 auto, 1 en, 2 ru ----
inline bool WindowsRussian() {
    const LANGID id = GetUserDefaultUILanguage(); const int primary = PRIMARYLANGID(id);
    return primary == LANG_RUSSIAN || primary == LANG_UKRAINIAN || primary == LANG_BELARUSIAN || primary == LANG_KAZAK;
}
inline bool MenuRussian(int setting) { return setting == 2 ? true : setting == 1 ? false : WindowsRussian(); }

}  // namespace re3menu
