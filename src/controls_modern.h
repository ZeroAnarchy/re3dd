#pragma once
// Controls round 2026-11-09, item 2: modern (camera-relative) movement as an option instead of tank controls.
// pilot.ini [pilot] control_scheme = 0 tank (default, native, nothing is touched) / 1 modern. Live switch: SetScheme(int)
// (the F5 mod menu calls it and saves pilot.ini itself); Scheme() returns the current one.
//
// How it works (one call per input poll from mod_hooks.h, right after 42CAD0 has written the pad words):
//  - direction: the left stick from the game's own DirectInput state (360 degrees, analog dead zone with hysteresis), else
//    the raw pad direction bits of 65C858 (keyboard arrows / d-pad: 8 directions);
//  - the direction is turned into a world direction with the CURRENT fixed camera (rotation 51C398, the lookat 426B10
//    builds from the room camera eye/target): up = away from the camera (screen up on steep cameras), right = screen right;
//  - in free movement (player routine 1, sub-state idle/walk/run/back/turn/back-into-wall, aim not held) the player's yaw
//    (player+6E, 4096 = 360 degrees) is turned toward that direction with a rate limit and the native FORWARD action bit is
//    synthesized (backward/turn bits cleared); the native walk/run/collision/animation code then moves her. A large angle
//    first turns in place (native turn bit, so the native turn animation plays), then walks; the native run button runs.
//  - stairs (sub 9) and walking/running into an obstacle or pushing (sub 10/15): no yaw write; FORWARD only while the
//    input points roughly along her facing, else nothing (she stops, the next tick steers again).
//  - camera cuts: the camera basis is locked while the input stays within modern_lock_angle of the input at the time of
//    the cut; release or a bigger change picks up the new camera (no ping-pong at camera borders). Round 2 (09.11, user:
//    "at a camera cut she turns sharply; she should keep going until I release the button and press it again"): the
//    default angle is 180 = only a release picks up the new camera, a release is input absent for more than 4 ticks
//    (a camera cut can drop the pad bits for a tick) and a guard (Blocked) resets the lock only after 45 ticks (1.5 s:
//    an event, not the one-tick flags around a cut).
//  - everything else (aim held, weapon states, quick turn, damage, events, doors, menus, inventory, movies, fades, message
//    text, cutscenes) passes through untouched: only bits 1/2/4/8/10/20 (and 200 with modern_stick_run) of 65C864/65C868
//    are rewritten, and only in the states above.
// Static facts (ResidentEvil3.exe; RE3_MERCE.exe through exe_map, checked by qa/ctl-2026-11-09/modern-controls/
// merce_check.py): see REPORT.ru.txt in runtime/staging/ctl-2026-11-09/modern-controls.
#include <windows.h>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include "exe_map.h"

namespace re3modern {
using Log = void (*)(const char*, ...);

// ------------------------------------------------------------------------------------------------------------------
// Pure logic (no game memory): offline-tested by qa/ctl-2026-11-09/modern-controls/test_modern.cpp
// ------------------------------------------------------------------------------------------------------------------
constexpr double kPi = 3.14159265358979323846;
constexpr uint32_t kForward = 0x01, kTurnRight = 0x02, kBackward = 0x04, kTurnLeft = 0x08, kUpAlias = 0x10, kDownAlias = 0x20;
constexpr uint32_t kDirectionMask = 0x3f;   // action bits 0..5 = raw up/right/down/left/up/down (table 517DAC, all key types)
constexpr uint32_t kRun = 0x200, kAimMask = 0x500;   // run = raw 0x40 (type 0) or 0x80 (types 1/2); aim 0x100 / 0x400
constexpr uint32_t kRawUp = 0x1000, kRawRight = 0x2000, kRawDown = 0x4000, kRawLeft = 0x8000;   // 65C858 (bio3.ini Key1..Key4)

struct Vec2 { double x = 0, z = 0; };
inline double Length(Vec2 v) { return std::sqrt(v.x * v.x + v.z * v.z); }
inline Vec2 Normalized(Vec2 v) { const double l = Length(v); return l > 1e-9 ? Vec2{v.x / l, v.z / l} : Vec2{}; }

inline int WrapYaw(int v) { return v & 0xfff; }
// signed shortest difference target - current in (-2048, 2048]
inline int YawDiff(int target, int current) { const int d = (target - current) & 0xfff; return d > 2048 ? d - 4096 : d; }
// 456020 (enemies face the player with it): yaw = -atan2(dz, dx) in 4096 units, dx == 0 -> 0x400 / 0xC00
inline int YawFromDirection(Vec2 d) {
    const double a = -std::atan2(d.z, d.x) * 4096.0 / (2.0 * kPi);
    return WrapYaw(static_cast<int>(std::lround(a)));
}
// RotMatrixY(yaw) applied to the walk root motion (local +X) in 455E90: world step = (cos, -sin) * speed
inline Vec2 FacingFromYaw(int yaw) {
    const double a = static_cast<double>(yaw & 0xfff) * 2.0 * kPi / 4096.0;
    return Vec2{std::cos(a), -std::sin(a)};
}
inline int DegreesToYaw(double degrees) { return static_cast<int>(std::lround(degrees * 4096.0 / 360.0)); }

// The fixed camera: rows of the world->view rotation (PSX GTE, 4096 = 1.0). Row 0 = screen right, row 1 = screen down,
// row 2 = view direction. Ground forward = (view direction - screen down) on XZ: away from a flat camera, screen-up on
// a top-down one (426B10 builds Rx(pitch) * Ry(yaw); straight down it leaves yaw identity -> +Z up, +X right).
struct CameraBasis { Vec2 right, forward; bool valid = false; };
inline CameraBasis BasisFromMatrix(const int16_t m[9]) {
    CameraBasis b;
    const Vec2 forward{static_cast<double>(m[6]) - m[3], static_cast<double>(m[8]) - m[5]};
    const Vec2 right{static_cast<double>(m[0]), static_cast<double>(m[2])};
    if (Length(forward) < 200.0 || Length(right) < 200.0) return b;   // degenerate (not a room camera)
    b.forward = Normalized(forward); b.right = Normalized(right); b.valid = true;
    return b;
}
inline bool SameBasis(const CameraBasis& a, const CameraBasis& b) {
    return a.valid && b.valid && a.forward.x * b.forward.x + a.forward.z * b.forward.z > 0.9995 &&
           a.right.x * b.right.x + a.right.z * b.right.z > 0.9995;
}
// stick: x right, y up (away), unit length
inline Vec2 WorldDirection(const CameraBasis& b, double sx, double sy) {
    return Normalized(Vec2{sx * b.right.x + sy * b.forward.x, sx * b.right.z + sy * b.forward.z});
}

struct Stick { double x = 0, y = 0, magnitude = 0; bool active = false; };
inline double AngleDegrees(const Stick& a, const Stick& b) {
    double c = a.x * b.x + a.y * b.y; c = c > 1 ? 1 : c < -1 ? -1 : c;
    return std::acos(c) * 180.0 / kPi;
}
// keyboard arrows / d-pad through 65C858: 8 directions, opposite keys cancel
inline Stick FromRawBits(uint32_t raw) {
    Stick s;
    const int x = ((raw & kRawRight) ? 1 : 0) - ((raw & kRawLeft) ? 1 : 0), y = ((raw & kRawUp) ? 1 : 0) - ((raw & kRawDown) ? 1 : 0);
    if (!x && !y) return s;
    const double l = std::sqrt(static_cast<double>(x * x + y * y));
    s.x = x / l; s.y = y / l; s.magnitude = 1; s.active = true;
    return s;
}

struct Settings {
    int turnRate = 256;          // yaw units per 30 Hz tick (256 = 22.5 degrees: 180 degrees in 8 ticks, 0.27 s)
    int deadzoneOn = 250;        // analog engage radius (DirectInput range +-1000, set by the game in 4078B0)
    int deadzoneOff = 180;       // ... release radius (hysteresis)
    double lockAngle = 180;      // degrees the input may change after a camera cut before the new camera is used (180 = only a release)
    double walkGate = 100;       // |angle| up to which she walks while turning; above: turn in place first
    double stairsGate = 75;      // stairs (sub 9): keep climbing while the input is within this of her facing
    double pushGate = 60;        // into obstacle / pushing (sub 10, 15)
    bool analog = true;          // 0 = only the 8 digital directions
    bool stickRun = false;       // 1 = stick at >= runThreshold also runs (off: with goginput dpadtoanalog the d-pad is full deflection)
    int runThreshold = 900;
    bool turnAnimation = true;   // in-place turns set the native turn bit (native turn animation) on top of the yaw write
};

// analog stick with a radial dead zone + hysteresis; lx/ly = DIJOYSTATE lX/lY (lY negative = up)
struct AnalogFilter {
    bool engaged = false;
    bool Update(int lx, int ly, const Settings& s, Stick& out) {
        const double m = std::sqrt(static_cast<double>(lx) * lx + static_cast<double>(ly) * ly);
        if (engaged) { if (m < s.deadzoneOff) engaged = false; }
        else if (m >= s.deadzoneOn) engaged = true;
        if (!engaged || m < 1) { out = Stick{}; return false; }
        out.x = lx / m; out.y = -ly / m; out.magnitude = m > 1000 ? 1.0 : m / 1000.0; out.active = true;
        return true;
    }
};

// The game's default [Gamepad] bindings turn lY/lX beyond -+500 into raw up/down/left/right (bio3.ini Key1..Key4 =
// 4,-500 / 4,500 / 0,-500 / 0,500). A stick clearly past them (600) without any raw bit on that axis means the pad words
// were cleared after the poll (a mod menu swallowing input) or the bindings were moved elsewhere: do not steer then.
inline bool SwallowedStick(int lx, int ly, uint32_t raw) {
    return (std::abs(ly) >= 600 && !(raw & (kRawUp | kRawDown))) || (std::abs(lx) >= 600 && !(raw & (kRawLeft | kRawRight)));
}

// camera-cut lock: the basis used to turn the stick into a world direction
struct CameraLock {
    bool active = false;
    CameraBasis basis;
    Stick input;
    enum Event { None, Started, Followed, Kept, Relocked };
    Event Update(const CameraBasis& current, const Stick& stick, double thresholdDegrees) {
        if (!active) { active = true; basis = current; input = stick; return Started; }
        if (SameBasis(basis, current)) { basis = current; input = stick; return Followed; }
        if (AngleDegrees(stick, input) > thresholdDegrees) { basis = current; input = stick; return Relocked; }
        return Kept;
    }
    void Reset() { active = false; }
};

enum class Klass : uint8_t { Pass, Steer, ForwardOnly };
// routine/sub = player[4]/[5]; held = 65C864. Handlers 52A238[sub]: 0 idle 45FF50, 1 walk 460300, 2 run 460590,
// 3 back 460820, 4 turn 460CF0, 9 stairs 461750, 10 walk into obstacle/push 4619A0, 14 back into obstacle 461D30,
// 15 run into obstacle/push 4620D0; 5 weapon, 6 quick turn, 7/8/11-13 no input handler.
inline Klass Classify(uint8_t routine, uint8_t sub, uint32_t held) {
    if (routine != 1 || (held & kAimMask)) return Klass::Pass;
    switch (sub) {
    case 0: case 1: case 2: case 3: case 4: case 14: return Klass::Steer;
    case 9: case 10: case 15: return Klass::ForwardOnly;
    default: return Klass::Pass;
    }
}
inline const char* KlassName(Klass k) { return k == Klass::Steer ? "steer" : k == Klass::ForwardOnly ? "forward-only" : "pass"; }

struct Decision { bool writeYaw = false; int yaw = 0; uint32_t bits = 0; };
inline Decision Decide(Klass k, uint8_t sub, int yaw, int target, const Settings& s) {
    Decision d;
    const int diff = YawDiff(target, yaw);
    if (k == Klass::ForwardOnly) {
        const double gate = sub == 9 ? s.stairsGate : s.pushGate;
        if (std::abs(diff) <= DegreesToYaw(gate)) d.bits = kForward | kUpAlias;
        return d;
    }
    if (k != Klass::Steer) return d;
    const int step = diff > s.turnRate ? s.turnRate : diff < -s.turnRate ? -s.turnRate : diff;
    d.writeYaw = step != 0; d.yaw = WrapYaw(yaw + step);
    if (std::abs(diff) <= DegreesToYaw(s.walkGate)) d.bits = kForward | kUpAlias;       // walk (run = native button) while turning
    else if (s.turnAnimation) d.bits = diff > 0 ? kTurnRight : kTurnLeft;                // turn in place: right turn adds yaw (460DBA)
    return d;
}

// ------------------------------------------------------------------------------------------------------------------
// Game glue
// ------------------------------------------------------------------------------------------------------------------
inline volatile LONG gScheme = -1;   // -1 = not read from pilot.ini yet
inline volatile LONG gSchemeLogged = -1;
inline void SetScheme(int scheme) { InterlockedExchange(&gScheme, scheme == 1 ? 1 : 0); }
inline int Scheme() { const LONG s = gScheme; return s == 1 ? 1 : 0; }
// The stick is read from DirectInput directly, so a mod menu that only clears the pad words while it is open would not
// stop it: such a menu calls SetSuspended(true) while open (false when closed). Without that call the heuristic in
// Tick still stops when the stick is clearly past the game's +-500 bindings but no raw direction bit came through.
inline volatile LONG gSuspended = 0;
inline void SetSuspended(bool on) { InterlockedExchange(&gSuspended, on ? 1 : 0); }
// Live pilot.ini modern_stick_run from the F5 menu (merge 2026-11-09): -1 = keep the value read at startup.
inline volatile LONG gStickRunRequest = -1;
inline void SetStickRun(int on) { InterlockedExchange(&gStickRunRequest, on ? 1 : 0); }

namespace detail {
struct Site { uint32_t rva; const char* hex; const char* what; };
// verified on disk (qa/ctl-2026-11-09/modern-controls/re3x.py; RE3_MERCE.exe: merce_check.py)
inline const Site kCoreSites[] = {
    {0x2ce5b, "8b1564c8a50033f689156cc8a500893564c8a500", "poll 42CE5B: 65C86C = previous 65C864, 65C864 rebuilt"},
    {0x2cf1e, "33e833d623e823d633ff84c9892d5cc8a500891568c8a500893d60c8a500", "poll 42CF1E: 65C868 = held & ~previous"},
    {0x2cc68, "33c0a0bd28a6008bf8a158c8a500c1e70581c7ac7d5100", "poll 42CC68: raw 65C858 -> action table 517DAC + type*32"},
    {0x5fec0, "8b0d68c8a5008b1564c8a500568b74240833c0518a46055256ff148538a25200", "45FEC0: handler 52A238[sub](player, 65C864, 65C868)"},
    {0x55e9e, "668b4e6e", "455E9E: movement rotates by yaw player+6E"},
    {0x55f06, "8b46348b4e3c", "455F06: position X +34 / Z +3C"},
    {0x56050, "0f9cc2c1e20b03c25ef7d825ff0f0000", "456050: yaw = -atan2(dz, dx) & 0xFFF"},
    {0x60400, "c4a252006601466e", "460400: turn right adds to yaw +6E"},
    {0x26b1b, "b908000000be78c35100bf98c35100f3a5", "426B1B: camera lookat writes 51C398"},
};
inline const Site kAnalogSites[] = {
    {0x7aa5, "8db1540100008b06508b08ff51648b068d5eb0536a50", "407AA5: GetDeviceState(0x50) into input manager +104 + i*10158"},
    {0x78ef, "c744243818fcffffc744243ce8030000", "4078EF: axis range -1000..1000"},
    {0xfdbd, "898e24140000", "40FDBD: pad +1424 = input manager"},
    {0x3e73, "8b8600010000a3c0495300", "403E73: [5349C0] = pad object"},
};
struct HandlerSlot { uint32_t index, rva; };
inline const HandlerSlot kHandlers[] = {{0, 0x5ff50}, {1, 0x60300}, {2, 0x60590}, {3, 0x60820}, {4, 0x60cf0}, {9, 0x61750}, {10, 0x619a0}, {14, 0x61d30}, {15, 0x620d0}};

inline int HexValue(char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1; }
inline bool Matches(uintptr_t at, const char* hex) {
    const auto* p = reinterpret_cast<const uint8_t*>(at);
    for (size_t i = 0; hex[2 * i] && hex[2 * i + 1]; ++i) if (p[i] != static_cast<uint8_t>(HexValue(hex[2 * i]) * 16 + HexValue(hex[2 * i + 1]))) return false;
    return true;
}
inline bool Readable(const void* p, size_t n) {
    if (!p) return false;
    MEMORY_BASIC_INFORMATION mbi{};
    if (!VirtualQuery(p, &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT || (mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS))) return false;
    if (!(mbi.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY))) return false;
    return reinterpret_cast<uintptr_t>(p) + n <= reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
}
template<class T> inline T& At(const re3exe::Base& b, uint32_t rva) { return *reinterpret_cast<T*>(b + rva); }

// The options-menu guard list (options_menu.h Blocked) without its camera-change tests (cuts are handled here) and
// without A61F5C bit 100 (set by the stairs motion 4617E4 and other player animations, which the sub-state test covers).
inline const char* Blocked(const re3exe::Base& b) {
    if (At<uint16_t>(b, 0x6591a4)) return "task slot 1 busy (inventory/map/files/options)";
    // fix 2026-11-11b (user: "the modern controls do not work in Mercenaries"): without bit 80h of the options mask - the
    // Mercenaries flag, set for the whole of RE3_MERCE.exe (see reload.h GameBlocked)
    if (At<uint32_t>(b, 0x65c880) & 0x115b0400u) return "A5C880 busy (demo/overlay/door/options/save/movie/ui)";
    if (At<uint32_t>(b, 0x661f5c) & 0x10400208u) return "A61F5C busy (event/loading/inventory)";
    if (At<uint8_t>(b, 0x65cd21)) return "A5CD21 door guard";
    if (At<uint8_t>(b, 0x6587a1) || At<uint8_t>(b, 0x6587a0)) return "controller or room request";
    if (At<uint8_t>(b, 0x64cfdb) & 0x0e) return "A4CFDB busy";
    if (At<uint8_t>(b, 0x657468) & 0x80) return "message text";
    if (At<uint32_t>(b, 0x65cbd0) & 0xff000000u) return "update domains masked";
    if (At<uint32_t>(b, 0x661d44) || At<uint32_t>(b, 0x661d48)) return "interaction/dialog controller";
    if (At<uint8_t>(b, 0x6628c4)) return "fade";
    if (At<uint32_t>(b, 0x65cbd4) & 0x40000000u) return "pause";
    if (At<uint8_t>(b, 0x65cbf8) != 1) return "player not in routine 1";
    if (At<uint16_t>(b, 0x661fa6) > 6) return "no stage";
    return nullptr;
}

struct Runtime {
    bool initialized = false, coreOk = false, analogCode = false, debug = false, analogLogged = false;
    std::string ini, stickFile;
    Settings settings;
    AnalogFilter filter;
    CameraLock lock;
    uint32_t room = 0xffffffffu;
    const uint8_t* pad = nullptr; const uint8_t* manager = nullptr; bool padOk = false, managerOk = false;
    // debug_warp=1 only: pilot.stick injection + MODERN_QA lines
    int injectFrames = 0, injectTotal = 0, injectX = 0, injectY = 0; uint32_t injectRaw = 0; bool injectAnalog = false;
    int injectRFrames = 0, injectRX = 0, injectRY = 0;   // round 6: "rstick" = the right stick (widescreen peek)
    const uint8_t* rightPad = nullptr; const uint8_t* rightManager = nullptr; bool rightOk = false;
    unsigned tick = 0; Klass lastKlass = Klass::Pass; const char* lastBlock = nullptr; CameraLock::Event lastEvent = CameraLock::None;
    bool lastSwallowed = false;
    unsigned releaseTicks = 0, blockTicks = 0;   // round 2: grace before a missing input / a guard drops the camera lock
    const char* loggedBlock = nullptr; unsigned blockLines = 0;   // fix 2026-11-11b: a guard that holds with a direction held is named in the log
};
constexpr unsigned kReleaseGrace = 4, kBlockGrace = 45;
inline Runtime& State() { static Runtime r; return r; }

inline int IniInt(const Runtime& r, const char* key, int fallback) { return static_cast<int>(GetPrivateProfileIntA("pilot", key, fallback, r.ini.c_str())); }

// joystick 0 lX/lY exactly as the game reads them for bio3.ini [Gamepad] Key1..Key4 (410090 -> 407D40 -> 407B10)
inline bool ReadAnalog(const re3exe::Base& b, Runtime& r, int& lx, int& ly, const char** why) {
    if (!r.analogCode) { *why = "code mismatch"; return false; }
    const auto* pad = *reinterpret_cast<const uint8_t* const*>(b + 0x1349c0);
    if (pad != r.pad) { r.pad = pad; r.manager = nullptr; r.managerOk = false;
        r.padOk = Readable(pad, 0x1428) && *reinterpret_cast<const uint32_t*>(pad) == static_cast<uint32_t>(b + 0x10ac04); }
    if (!r.padOk) { *why = "pad object (vtable 50AC04) not found"; return false; }
    const auto* manager = *reinterpret_cast<const uint8_t* const*>(pad + 0x1424);
    if (manager != r.manager) { r.manager = manager;
        r.managerOk = Readable(manager, 0x110) && Readable(manager + 0xa0d74, 4) &&
                      *reinterpret_cast<const uint32_t*>(manager) == static_cast<uint32_t>(b + 0x10a7e4); }
    if (!r.managerOk) { *why = "input manager (vtable 50A7E4) not found"; return false; }
    if (*reinterpret_cast<const int32_t*>(manager + 0xa0d74) < 1) { *why = "no joystick enumerated"; return false; }
    const auto* state = reinterpret_cast<const int32_t*>(manager + 0x104);   // DIJOYSTATE of joystick 0
    lx = state[0] < -1000 ? -1000 : state[0] > 1000 ? 1000 : state[0];
    ly = state[1] < -1000 ? -1000 : state[1] > 1000 ? 1000 : state[1];
    return true;
}

// Round 6 (09.11 night): joystick 0 lRx/lRy (DIJOYSTATE +0C/+10: the right stick under the GOG pad layout) for the
// widescreen peek (proxy.cpp -> re3widescreen::gPeekStick). The same objects as ReadAnalog, checked on their own (the
// right stick does not need the modern-controls code sites). debug_warp=1: pilot.stick "rstick <lRx> <lRy> <polls>".
inline bool ReadRightStick(const re3exe::Base& b, int& rx, int& ry) {
    auto& r = State();
    if (!r.initialized) return false;
    if (r.injectRFrames > 0) { rx = r.injectRX; ry = r.injectRY; return true; }
    const auto* pad = *reinterpret_cast<const uint8_t* const*>(b + 0x1349c0);
    const auto* manager = Readable(pad, 0x1428) ? *reinterpret_cast<const uint8_t* const*>(pad + 0x1424) : nullptr;
    if (pad != r.rightPad || manager != r.rightManager) {
        r.rightPad = pad; r.rightManager = manager;
        r.rightOk = manager && *reinterpret_cast<const uint32_t*>(pad) == static_cast<uint32_t>(b + 0x10ac04) && Readable(manager, 0x130) &&
                    Readable(manager + 0xa0d74, 4) && *reinterpret_cast<const uint32_t*>(manager) == static_cast<uint32_t>(b + 0x10a7e4);
    }
    if (!r.rightOk || *reinterpret_cast<const int32_t*>(manager + 0xa0d74) < 1) return false;
    const auto* state = reinterpret_cast<const int32_t*>(manager + 0x104);   // DIJOYSTATE of joystick 0
    rx = state[3] < -1000 ? -1000 : state[3] > 1000 ? 1000 : state[3];
    ry = state[4] < -1000 ? -1000 : state[4] > 1000 ? 1000 : state[4];
    return true;
}

inline void Init(const re3exe::Base& b, const std::string& folder, Log log) {
    auto& r = State(); r.initialized = true;
    r.ini = folder + "\\pilot.ini"; r.stickFile = folder + "\\pilot.stick";
    if (gScheme < 0) InterlockedCompareExchange(&gScheme, IniInt(r, "control_scheme", 0) == 1 ? 1 : 0, -1);
    auto& s = r.settings;
    s.turnRate = IniInt(r, "modern_turn_rate", 256); if (s.turnRate < 32) s.turnRate = 32; if (s.turnRate > 2048) s.turnRate = 2048;
    s.deadzoneOn = IniInt(r, "modern_deadzone", 250); if (s.deadzoneOn < 50) s.deadzoneOn = 50; if (s.deadzoneOn > 900) s.deadzoneOn = 900;
    s.deadzoneOff = s.deadzoneOn * 7 / 10;
    s.lockAngle = IniInt(r, "modern_lock_angle", 180); if (s.lockAngle < 10) s.lockAngle = 10; if (s.lockAngle > 180) s.lockAngle = 180;
    s.analog = IniInt(r, "modern_analog", 1) != 0;
    s.stickRun = IniInt(r, "modern_stick_run", 0) != 0;
    s.turnAnimation = IniInt(r, "modern_turn_anim", 1) != 0;
    r.debug = IniInt(r, "debug_warp", 0) != 0;
    const char* exe = re3exe::gRunning == re3exe::Exe::Merce ? "RE3_MERCE" : re3exe::gRunning == re3exe::Exe::Main ? "ResidentEvil3" : "unknown";
    const char* failed = nullptr;
    for (const auto& site : kCoreSites) if (!Matches(b + site.rva, site.hex)) { failed = site.what; break; }
    if (!failed) for (const auto& h : kHandlers)
        if (At<uint32_t>(b, 0x12a238 + 4 * h.index) != static_cast<uint32_t>(b + h.rva)) { failed = "handler table 52A238"; break; }
    r.coreOk = failed == nullptr && re3exe::gRunning != re3exe::Exe::Unknown;
    r.analogCode = r.coreOk && s.analog;
    for (const auto& site : kAnalogSites) if (r.analogCode && !Matches(b + site.rva, site.hex)) { r.analogCode = false; log("MODERN_CONTROLS analog code differs at %s: 8-direction digital only", site.what); }
    int lx = 0, ly = 0; const char* why = s.analog ? "" : "modern_analog=0";
    const bool analog = s.analog && ReadAnalog(b, r, lx, ly, &why);
    r.analogLogged = analog;
    if (!r.coreOk) log("MODERN_CONTROLS scheme=%d exe=%s DISABLED (stays tank): %s", Scheme(), exe, failed ? failed : "unknown exe");
    else log("MODERN_CONTROLS scheme=%d (pilot.ini control_scheme: 0 tank, 1 modern; live switch from the mod menu) exe=%s code verified; "
             "analog=%s%s%s (DIJOYSTATE joystick 0 lX/lY via [5349C0]+1424 -> +104, range +-1000; joysticks=%d) turn_rate=%d deadzone=%d "
             "lock_angle=%d stick_run=%d turn_anim=%d%s", Scheme(), exe, analog ? "yes" : "no (", analog ? "" : why, analog ? "" : ") -> 8-direction digital",
             r.managerOk ? *reinterpret_cast<const int32_t*>(r.manager + 0xa0d74) : 0, s.turnRate, s.deadzoneOn,
             static_cast<int>(s.lockAngle), s.stickRun ? 1 : 0, s.turnAnimation ? 1 : 0, r.debug ? " debug=pilot.stick+MODERN_QA" : "");
    gSchemeLogged = Scheme();
}

// debug_warp=1: pilot.stick = "stick <lX> <lY> <frames>" | "raw <hex 65C858 bits> <frames>" | "scheme <0|1>". The stick also
// produces the raw/action bits the game's +-500 bindings would (so native states see it), the raw form ORs pad bits.
inline void DebugInput(const re3exe::Base& b, Runtime& r, Log log) {
    if (GetFileAttributesA(r.stickFile.c_str()) != INVALID_FILE_ATTRIBUTES) {
        std::ifstream in(r.stickFile); std::string what; in >> what;
        if (what == "scheme") { int s = 0; in >> s; SetScheme(s); log("MODERN_QA scheme request %d", s); }
        else if (what == "rstick") {   // round 6: the right stick for <polls> input polls
            int frames = 0; in >> r.injectRX >> r.injectRY >> frames;
            r.injectRFrames = frames < 0 ? 0 : frames > 3000 ? 3000 : frames;
            log("MODERN_QA inject rstick x=%d y=%d polls=%d", r.injectRX, r.injectRY, r.injectRFrames);
        }
        else if (what == "stick" || what == "raw") {
            int frames = 0;
            if (what == "stick") { in >> r.injectX >> r.injectY >> frames; r.injectAnalog = true; r.injectRaw = 0; }
            else { in >> std::hex >> r.injectRaw >> std::dec >> frames; r.injectAnalog = false; }
            r.injectFrames = r.injectTotal = frames < 0 ? 0 : frames > 600 ? 600 : frames;
            const auto* p = reinterpret_cast<const uint8_t*>(b + 0x65cbf4); const auto* m = reinterpret_cast<const int16_t*>(b + 0x11c398);
            log("MODERN_QA inject %s x=%d y=%d raw=%x frames=%d start pos=%d,%d yaw=%03x sub=%u room=R%X%02X cam=%d camera rows right=%d,%d,%d "
                "down=%d,%d,%d view=%d,%d,%d scheme=%d", what.c_str(), r.injectX, r.injectY, r.injectRaw, r.injectFrames,
                *reinterpret_cast<const int32_t*>(p + 0x34), *reinterpret_cast<const int32_t*>(p + 0x3c), *reinterpret_cast<const uint16_t*>(p + 0x6e) & 0xfff,
                p[5], (At<uint16_t>(b, 0x661fa6) + 1) & 15, At<uint16_t>(b, 0x661fa8) & 0xff, At<int16_t>(b, 0x661faa),
                m[0], m[1], m[2], m[3], m[4], m[5], m[6], m[7], m[8], Scheme());
        } else log("MODERN_QA pilot.stick ignored: '%s'", what.c_str());
        in.close(); DeleteFileA(r.stickFile.c_str());
    }
    if (!r.injectFrames) return;
    uint32_t raw = r.injectRaw;
    if (r.injectAnalog) raw |= (r.injectY <= -500 ? kRawUp : 0) | (r.injectY >= 500 ? kRawDown : 0) | (r.injectX <= -500 ? kRawLeft : 0) | (r.injectX >= 500 ? kRawRight : 0);
    const auto* table = reinterpret_cast<const uint16_t*>(b + 0x117dac + At<uint8_t>(b, 0x6628bd) * 32u);
    uint32_t mapped = 0; for (unsigned i = 0; i < 16; ++i) if (raw & table[i]) mapped |= 1u << i;
    At<uint32_t>(b, 0x65c858) |= raw; At<uint32_t>(b, 0x65c864) |= mapped;
    At<uint32_t>(b, 0x65c868) |= mapped & ~At<uint32_t>(b, 0x65c86c);
}

inline void Tick(const re3exe::Base& b, const std::string& folder, Log log) {
    auto& r = State();
    if (!r.initialized) Init(b, folder, log);
    if (gSchemeLogged != gScheme) { gSchemeLogged = gScheme; log("MODERN_CONTROLS scheme=%d (live switch)%s", Scheme(), r.coreOk ? "" : " - disabled, stays tank"); }
    if (const LONG run = InterlockedExchange(&gStickRunRequest, -1); run >= 0 && (run != 0) != r.settings.stickRun) {
        r.settings.stickRun = run != 0; log("MODERN_CONTROLS stick_run=%d (live switch)", r.settings.stickRun ? 1 : 0);
    }
    if (r.debug) DebugInput(b, r, log);
    if (r.injectRFrames > 0 && --r.injectRFrames == 0) log("MODERN_QA inject rstick end");
    const bool injecting = r.injectFrames > 0;
    if (injecting && --r.injectFrames == 0) {
        const auto* p = reinterpret_cast<const uint8_t*>(b + 0x65cbf4); const auto* m = reinterpret_cast<const int16_t*>(b + 0x11c398);
        log("MODERN_QA inject end after %d frames pos=%d,%d yaw=%03x sub=%u room=R%X%02X cam=%d camera rows right=%d,%d,%d down=%d,%d,%d view=%d,%d,%d",
            r.injectTotal, *reinterpret_cast<const int32_t*>(p + 0x34), *reinterpret_cast<const int32_t*>(p + 0x3c),
            *reinterpret_cast<const uint16_t*>(p + 0x6e) & 0xfff, p[5], (At<uint16_t>(b, 0x661fa6) + 1) & 15, At<uint16_t>(b, 0x661fa8) & 0xff,
            At<int16_t>(b, 0x661faa), m[0], m[1], m[2], m[3], m[4], m[5], m[6], m[7], m[8]);
    }
    ++r.tick;
    if (Scheme() != 1 || !r.coreOk || gSuspended) { r.lock.Reset(); r.filter.engaged = false; return; }

    // input direction: analog stick, else the raw direction bits (keyboard arrows, d-pad)
    Stick stick; int lx = 0, ly = 0; const char* why = "";
    bool analog = false;
    if (injecting && r.injectAnalog) { lx = r.injectX; ly = r.injectY; analog = true; }
    else if (r.settings.analog) {
        analog = ReadAnalog(b, r, lx, ly, &why);
        if (analog && !r.analogLogged) { r.analogLogged = true; log("MODERN_CONTROLS analog source now available (joystick 0 lX/lY)"); }
    }
    const uint32_t raw = At<uint32_t>(b, 0x65c858);
    if (analog && SwallowedStick(lx, ly, raw)) {   // pad words cleared by someone (menu) although the stick is far out
        if (r.debug && !r.lastSwallowed) log("MODERN_QA stick %d,%d but no raw direction bits: input swallowed, native", lx, ly);
        r.lastSwallowed = true; r.filter.engaged = false; r.lock.Reset(); return;
    }
    r.lastSwallowed = false;
    if (!(analog && r.filter.Update(lx, ly, r.settings, stick))) { if (!analog) r.filter.engaged = false; stick = FromRawBits(raw); }
    const uint32_t room = (static_cast<uint32_t>(At<uint16_t>(b, 0x661fa6)) << 16) | At<uint16_t>(b, 0x661fa8);
    if (room != r.room) { r.room = room; r.lock.Reset(); }
    if (!stick.active) {
        if (r.lock.active && ++r.releaseTicks <= kReleaseGrace) return;   // a tick without pad bits at a cut is not a release
        if (r.debug && r.lock.active) log("MODERN_QA lock released (input released)");
        r.lock.Reset(); return;
    }
    r.releaseTicks = 0;
    const char* block = Blocked(b);
    if (r.debug && block != r.lastBlock) { r.lastBlock = block; log("MODERN_QA gate %s cam=%d lock=%d", block ? block : "open", At<int16_t>(b, 0x661faa), r.lock.active ? 1 : 0); }
    if (block) {
        if (++r.blockTicks > kBlockGrace && r.lock.active) { if (r.debug) log("MODERN_QA lock released (guard for %u ticks)", r.blockTicks); r.lock.Reset(); }
        if (r.blockTicks == kBlockGrace + 1 && block != r.loggedBlock && r.blockLines < 6) {   // a few lines a session, also without debug_warp
            r.loggedBlock = block; ++r.blockLines;
            log("MODERN_CONTROLS native for %u ticks with a direction held: %s (A5C880=%08x A61F5C=%08x A5CBD0=%08x slot1=%u room R%X%02X)", r.blockTicks, block,
                At<uint32_t>(b, 0x65c880), At<uint32_t>(b, 0x661f5c), At<uint32_t>(b, 0x65cbd0), unsigned(At<uint16_t>(b, 0x6591a4)),
                (At<uint16_t>(b, 0x661fa6) + 1) & 15, At<uint16_t>(b, 0x661fa8) & 0xff);
        }
        return;
    }
    r.blockTicks = 0;

    auto* player = reinterpret_cast<uint8_t*>(b + 0x65cbf4);
    uint32_t& held = At<uint32_t>(b, 0x65c864);
    const Klass klass = Classify(player[4], player[5], held);
    if (r.debug && klass != r.lastKlass) { r.lastKlass = klass; log("MODERN_QA class %s sub=%u", KlassName(klass), player[5]); }
    if (klass == Klass::Pass) return;   // aim/weapon/quick turn/...: native (the lock is kept while the input is held)

    const CameraBasis current = BasisFromMatrix(reinterpret_cast<const int16_t*>(b + 0x11c398));
    if (!current.valid) return;
    const auto event = r.lock.Update(current, stick, r.settings.lockAngle);
    if (r.debug && event != r.lastEvent && event != CameraLock::Followed)
        log("MODERN_QA lock %s cam=%d", event == CameraLock::Started ? "started" : event == CameraLock::Relocked ? "relocked to the new camera" :
            "kept on the previous camera after a cut", At<int16_t>(b, 0x661faa));
    r.lastEvent = event;
    const Vec2 direction = WorldDirection(r.lock.basis, stick.x, stick.y);
    if (Length(direction) < 0.5) return;
    const int target = YawFromDirection(direction);
    auto& yawField = *reinterpret_cast<int16_t*>(player + 0x6e);
    const Decision d = Decide(klass, player[5], yawField, target, r.settings);
    if (d.writeYaw) yawField = static_cast<int16_t>(d.yaw);
    uint32_t bits = d.bits;
    if (r.settings.stickRun && (bits & kForward) && stick.magnitude * 1000 >= r.settings.runThreshold) bits |= kRun;
    const uint32_t managed = kDirectionMask | (bits & kRun);
    const uint32_t previous = At<uint32_t>(b, 0x65c86c);   // what the game saw last tick (the poll's own edge base)
    held = (held & ~managed) | bits;
    uint32_t& pressed = At<uint32_t>(b, 0x65c868);
    pressed = (pressed & ~managed) | (bits & ~previous & managed);
    if (r.debug && injecting && (r.injectFrames % 10 == 0))
        log("MODERN_QA t=%u %s sub=%u yaw=%03x target=%03x pos=%d,%d bits=%02x held=%04x cam=%d lock=%s stick=%.2f,%.2f", r.tick, KlassName(klass), player[5],
            static_cast<unsigned>(yawField) & 0xfff, static_cast<unsigned>(target), *reinterpret_cast<const int32_t*>(player + 0x34),
            *reinterpret_cast<const int32_t*>(player + 0x3c), bits, held, At<int16_t>(b, 0x661faa), SameBasis(r.lock.basis, current) ? "current" : "old-camera",
            stick.x, stick.y);
}
}  // namespace detail

// one call per input poll (mod_hooks.h AfterInputPoll); folder = the game folder (proxy.cpp root) (pilot.ini)
inline void Tick(const re3exe::Base& base, const std::string& folder, Log log) { detail::Tick(base, folder, log); }
inline bool ReadRightStick(const re3exe::Base& base, int& rx, int& ry) { return detail::ReadRightStick(base, rx, ry); }   // round 6
}  // namespace re3modern
