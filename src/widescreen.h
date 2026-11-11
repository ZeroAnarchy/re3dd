#pragma once
// Controls round 2026-11-09, item 6: experimental widescreen (pilot.ini widescreen / widescreen_aspect).
// Report: runtime/staging/ctl-2026-11-09/widescreen/REPORT.ru.txt, offline tests qa/ctl-2026-11-09/widescreen.
//
// How a frame reaches the screen (GOG ddraw.dll, static analysis): the game draws into the wrapper's render-target surface
// (render_width x render_height, 2560x1920 Full). At the native Flip (410FE0 -> wrapper Flip 2B6B0, one copy per DirectDraw
// surface version: 2B6B0/321F0/35100/38130/3E3E0) the wrapper
//   1. StretchRect(game RT, NULL, level 0 of its copy texture [back+8C4], NULL, NONE)          (2B9AC)
//   2. SetRenderTarget(0, swap chain back buffer), viewport = whole back buffer, Clear black     (2BBB6, 2BBEB, 2BFF2)
//   3. writes 6 vertices (FVF 1C4: XYZRHW|DIFFUSE|SPECULAR|TEX1, stride 32, u/v 0..1) of the dxcfg "fit" rectangle
//      (offset/scale doubles 18181FF8/18181FE8 and 181827E8/18182008, "integer" 18181918/28/88/FA0) into its queue,
//      runs its D3DX effect ("Bicubic", or "NoFilter" when the back buffer is exactly the game size) and flushes the
//      queue: DrawPrimitiveUP(TRIANGLELIST, 2, queue, 32) in 45B30 (47071; indexed variant 470CB)
//   4. restores the target and calls IDirect3DSwapChain9::Present (2C5FE).
// The widescreen crop rewrites only that one draw (step 3) in our device hooks: the same 6 vertices, positions = the
// widescreen rectangle fitted into the viewport, v = the crop window of the frame. The wrapper's own bicubic filter samples
// the copy of the render target directly (no extra pass, no extra blur, no new D3D resources). Every other draw, and every
// frame shown as 4:3, is left exactly as the wrapper made it; widescreen=0 does not even look at the draws.
//
// When the crop is shown (game state read at the present quad, render thread; data addresses are the same in both exes):
//   - slot 0 task = gameplay main 42B0E0 ([A59128], set by the title's chain 450ABE), slot 0 not suspended
//     ([A59124] & 0x40: 42B080(0) from the status screen 45DCD0 = inventory/map/files/item box, option.bin, mem_card.bin
//     = typewriter save/load, the puzzle programs, pc_sys.bin, result.bin, select/opening/ending/epilogue/staff roll),
//   - slot 1 ([A591A4] status, [A591A8] entry) not running one of the overlay programs 0..16 of table 517970 or the status
//     screen 45E2F0 (diedemo.bin = game over and result.bin = Mercenaries results do not suspend slot 0),
//   - or a movie is playing (2:1 movies fit inside the 16:9 crop; the window is centred while a movie plays),
//   - or (round 3) the prologue slides (opening.bin) / the staff roll (staff_r.bin) run: centred,
//   - and no full-frame mod UI was noted in the last 200 ms (NoteFullFrameUi; not the F5 mod menu since round 6).
// Otherwise the full 4:3 frame is shown pillarboxed exactly as without the mod; round 2/3 add door animations, frames
// without a room background and frames that are black outside the text rows (BlackProbe) to the 4:3 cases.
// Message text (A57468 & 0x80, top line y = A57482): since round 2 the window does not move for text; the text (and since
// round 3 its yes/no row and the live-selection rows) is lifted into the window (PlaceMessage, SetChoiceDelta, SetLiveRow).
// Mercenaries (A5C880 & 0x80): the timer row y 32 stays inside (window top <= 30).
// Mode 2 follows the player (round 2: the rows of her HD outline as rendered, bones as the fallback; round 3: a steady
// low-passed box on a critically damped spring; round 4: the box hangs from her floor point with a peak-held extent, so the
// walk cycle does not move it, and the spring time is pilot.ini widescreen_smooth 0..3 / the F5 menu, see Window),
// centred in story scenes, door transitions and when she is not on screen, and snapped at camera cuts (stage/room/camera
// A61FA6/A8/AA) and whenever the crop starts. Round 4: the text-on-black task 42F4B0 (after a load, at a new game) is 4:3.
// Round 5: a speed limit (with a matching acceleration limit) and a start delay of the follow window, pilot.ini
// widescreen_speed / widescreen_delay and the F5 menu (see Window).
// Round 6: while the F5 menu is open the picture keeps its format - the crop window of the last game frame is held (the
// menu lays itself out inside it, MenuView); the right stick of the pad looks up / down (Peek, pilot.ini widescreen_peek).
#include <windows.h>
#include <d3d9.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include "exe_map.h"

namespace re3widescreen {

constexpr float kFrameW = 320.f, kFrameH = 240.f;   // native frame (PSX units); the render target is k x this

// ------------------------------------------------------------------ pure parts (offline tests compile these alone)
// "16:9", "16/10", "21:9", "2.35", "auto"/"0" (= aspect of the output). Returns 0 for auto, -1 for invalid.
inline float ParseAspect(const char* text) {
    if (!text) return -1;
    while (*text == ' ' || *text == '\t') ++text;
    if (!_strnicmp(text, "auto", 4)) return 0;
    char* end = nullptr; const double a = strtod(text, &end);
    if (end == text) return -1;
    double value = a;
    if (*end == ':' || *end == '/' || *end == 'x' || *end == 'X') {
        const char* second = end + 1; char* end2 = nullptr; const double b = strtod(second, &end2);
        if (end2 == second || b <= 0) return -1;
        value = a / b;
    }
    if (value == 0) return 0;
    if (!(value >= 4.0 / 3.0 - 1e-6 && value <= 3.6)) return -1;
    return float(value);
}

struct Rect { float l = 0, t = 0, r = 0, b = 0; };

// The crop window (top row, 240-line frame) for one presented frame.
struct WindowInput {
    int mode = 1;              // 1 centred, 2 follow
    float cropH = 180;         // rows of the 240-line frame shown
    bool snap = false;         // camera cut / crop just started / mode changed
    bool centre = false;       // movie or door transition: centred, no follow
    bool playerValid = false;
    float playerTop = 0, playerBottom = 0;   // Jill's head and feet rows
    bool rootValid = false; float rootRow = 0;   // round 3: row of her position on the floor (no walk-cycle bob)
    bool message = false; float messageTop = 185;
    bool merc = false;         // Mercenaries timer row 32
    double dt = 1.0 / 60;      // seconds since the previous presented frame
    float scale = 0;           // round 4 QA trace only: rows of 1750 units at her floor point (not her size: see Window)
    float smooth = 0.6f;       // round 4: spring time of the follow window, s (pilot.ini widescreen_smooth)
    float maxSpeed = 0;        // round 5: speed limit of the window, rows/s of the 240-row frame (0 = none, as round 4)
    float delay = 0;           // round 5: s a move must be wanted before the window starts it (0 = at once, as round 4)
};
// Round 4 (pilot.ini widescreen_smooth 0..3, F5 menu "Camera smoothing"): spring time of the follow window per level.
constexpr int kSmoothLevels = 4, kSmoothDefault = 2;
constexpr float kSmoothTimes[kSmoothLevels] = {0.20f, 0.35f, 0.60f, 1.00f};
inline float SmoothTime(int level) { return kSmoothTimes[(std::min)((std::max)(level, 0), kSmoothLevels - 1)]; }
// Round 5 (09.11 night, user: "even at maximum smoothing the camera still moves sharply when she runs fast; add a limit of
// its speed and a delay so it does not move at all when it is not really needed - the camera changes soon anyway; both
// adjustable; Jill may leave the picture for a while"): pilot.ini widescreen_speed 0..4 / widescreen_delay 0..5, F5 menu.
// Speeds in rows/s of the 240-row frame (the 16:9 window shows 180 rows: 50 rows/s = 28 % of the screen height a second;
// Jill runs 50..60 rows/s on screen in game runs r4-load / r4-death).
constexpr int kSpeedLevels = 5, kSpeedDefault = 2;
constexpr float kSpeeds[kSpeedLevels] = {0.f, 80.f, 50.f, 32.f, 20.f};
constexpr int kDelayLevels = 6, kDelayDefault = 2;
constexpr float kDelays[kDelayLevels] = {0.f, 0.25f, 0.5f, 1.f, 1.5f, 2.f};
inline float SpeedLimit(int level) { return kSpeeds[(std::min)((std::max)(level, 0), kSpeedLevels - 1)]; }
inline float StartDelay(int level) { return kDelays[(std::min)((std::max)(level, 0), kDelayLevels - 1)]; }
// Critically damped spring towards `target` (Game Programming Gems 4, "Critically Damped Ease-In/Ease-Out Smoothing"):
// the speed `vel` (rows/s) is continuous, so a target that moves in steps (stairs, walk-cycle bob) gives a smooth camera.
inline float SmoothDamp(float current, float target, float& vel, float smoothTime, float maxSpeed, float dt) {
    if (!(dt > 0.f)) return current;
    smoothTime = (std::max)(0.0001f, smoothTime);
    const float omega = 2.f / smoothTime, x = omega * dt;
    const float decay = 1.f / (1.f + x + 0.48f * x * x + 0.235f * x * x * x);
    const float wanted = target, maxChange = maxSpeed * smoothTime;
    const float change = (std::min)((std::max)(current - target, -maxChange), maxChange);
    target = current - change;
    const float temp = (vel + omega * change) * dt;
    vel = (vel - omega * temp) * decay;
    float out = target + (change + temp) * decay;
    if ((wanted - current > 0.f) == (out > wanted)) { out = wanted; vel = 0.f; }   // no overshoot
    return out;
}

struct Window {
    float top = 0;
    bool valid = false;
    // Round 2 (09.11, user: "the follow camera seems to aim at her feet, her head leaves the picture at the top; it should
    // show the whole character as long as the crop can still move"): a soft margin (10 % of the window) the lazy target
    // keeps, and a hard one (3 %) that holds whatever the smoothing does. Taller than the window: one end is kept.
    static constexpr float kSoft = 0.10f, kHard = 0.03f;
    // Round 3 (09.11 evening, user: "when Jill nearly fills the window it jumps between showing her head and her feet with
    // every step; on the police-station stairs it jumps up with every step"): a steady box, a critically damped spring
    // (SmoothDamp), the hard margin on the box, the end she walks to kept when she does not fit (+/- 2 rows hysteresis,
    // direction from her floor point, kept at least kAnchorHold s, the head again after kStillHead s standing).
    // Round 4 (09.11 night, user: "much smoother, but the camera still sways up and down with her head while walking or
    // running; that makes you seasick in a long session - maybe a separate setting for the smoothing?"): the round-3 box
    // was her low-passed outline centred on its own centre (0.12 s), and the walk cycle (about 2 steps a second) passes
    // such a filter almost whole, so the box - and in a tight fit, where the hard margin holds it, the window - still rose
    // and fell with every step. Now each END of the box is an envelope of her outline (low-passed kRowTau):
    //   - it takes her outline (as drawn) at once when it reaches past it (the box only grows at once), keeps its place
    //     while the outline comes back within kEnvTouch of it at least every kEnvHold s (the walk cycle does that every
    //     step), and only then eases back to her outline (kEnvTau). Head bob, arm swing, aiming or crouching stay inside
    //     and do not move the window. The end she walks away from (direction from her floor point) lets go sooner
    //     (kTrailHold, kTrailTau), so a walk down or up the screen does not stretch the box for long.
    //   - The floor point (the projected actor position) gives only the direction: game runs r4-load / r4-death showed
    //     it 5..15 rows off her HD feet by an amount that changes across a room, moving 0.3..2 times as much as her
    //     outline (the camera's perspective), and the projected height of 1750 units was 14 rows where her outline is 68
    //     (top-down camera of R10C00): a first build that hung the box from that point swelled the box on every walk.
    //   - The spring time is the player's choice (WindowInput::smooth = kSmoothTimes[widescreen_smooth]). While the box
    //     sits inside the window the window coasts to rest (no target AT the window: SmoothDamp's no-overshoot rule would
    //     stop it dead in one frame - the round-3 window did that); once she pushes a soft margin, the target is that
    //     margin plus the box's speed x the spring time (feed-forward: a slow spring keeps pace with a running Jill
    //     instead of trailing until the hard margin catches her), never past what keeps the box inside with the hard
    //     margin; a box past the hard margin is brought back within kClampTau, never out of the window.
    //   - When she does not fit, the kept end follows on a spring of at most kOverSmooth s.
    // Round 5 (see kSpeeds): the round-4 window at the slowest spring (1.0 s) still moved at up to 150..200 rows/s in game
    // runs: a running Jill outpaces the slow spring, reaches the hard margin, and the margin (kClampTau 0.08 s) took the
    // window along at once - the faster she ran, the sharper. Two settings now come after the target and the spring:
    //   - the start delay (WindowInput::delay): a window at rest starts only after the target has wanted it more than
    //     kNeedRows away for that long without a break; a camera cut before then (snap) means it never moved. Once
    //     moving it keeps following until it is at rest again (kRestSpeed, kRestRows), and the delay is armed again only
    //     after kRearm s at rest: in a slow walk the lazy window rests between pushes of the margin, and a delay before
    //     each push made it stop and go (test_round5 tight walk).
    //   - the speed limit (WindowInput::maxSpeed): the window's speed never exceeds it, and changes at most by
    //     maxSpeed / AccelTime(smooth) per second (the smoothing level sets how gently it speeds up and slows down); it
    //     brakes in time to stop at the target and at the edges of the picture (StopSpeed, frame by frame). The hard
    //     margin then only asks the window to hurry within those limits, and Jill may leave the picture for a while
    //     (the user's choice).
    //   Movies, door transitions and story scenes (WindowInput::centre) stay centred at once (their own black bars).
    //   Both at 0: exactly the round-4 window (test_round5 compares it with r4_window.h).
    static constexpr float kNeedRows = 2.f, kRestSpeed = 0.5f, kRestRows = 0.5f, kRearm = 1.0f, kBrake = 0.8f;
    static float AccelTime(float smooth) { return (std::max)(0.25f, 0.6f * smooth); }   // s from rest to the speed limit
    // the highest speed from which slowing down by `accel` (rows/s^2) every frame of dt stops within `rows`
    static float StopSpeed(float rows, float accel, float dt) {
        if (!(rows > 0.f)) return 0.f;
        const float step = accel * dt;
        return step * (std::sqrt(0.25f + 2.f * rows / (dt * step)) - 0.5f);
    }
    float wait = 0;                      // round 5: s the target has wanted a move while the window rests
    float rested = 1e9f;                 // round 5: s since the window came to rest (the delay is armed after kRearm)
    bool moving = false;                 // round 5: the window follows (not at rest)
    static constexpr float kMaxSpeed = 900.f;
    static constexpr float kVelTau = 0.25f, kDirSpeed = 15.f, kStillHead = 1.0f, kAnchorHold = 0.8f;
    static constexpr float kRowTau = 0.12f;       // s: low-pass of her outline rows and of the floor point
    static constexpr float kCentreTau = 0.35f;    // s: no floor point - her direction from the outline centre low-passed this long
    static constexpr float kEnvInit = 0.015f;     // a new box starts this much (of its height) wider at each end
    static constexpr float kEnvHold = 1.5f, kEnvTouch = 0.02f, kEnvTau = 1.0f;   // s, box heights, s
    static constexpr float kTrailHold = 0.6f, kTrailTau = 0.4f;   // the end she walks away from: s, s
    static constexpr float kTrailSpeed = 1.5f, kTrailSpeedCentre = 8.f;   // rows/s: from the floor point (no bob) / the outline centre
    static constexpr float kLead = 0.12f, kOverSmooth = 0.4f, kSteadyTau = 0.5f, kFeedMax = 0.5f, kClampTau = 0.08f;   // s
    float vel = 0;                       // window speed, rows/s
    bool body = false;                   // the follow state below is valid
    bool fromRoot = false;
    float rowTop = 0, rowBottom = 0;     // her outline, low-passed kRowTau
    float edgeTop = 0, edgeBottom = 0;   // the box: envelope of rowTop / rowBottom
    float holdTop = 0, holdBottom = 0;   // seconds since her outline last reached that end
    float span = 0;                      // box height, rows
    float dirRow = 0;                    // the floor point low-passed kRowTau (or the slow outline centre): her direction
    float bodyVel = 0;                   // her speed on screen from dirRow, rows/s (+ = down the screen): direction only
    float boxVel = 0;                    // the box centre's speed, rows/s
    float steadyVel = 0;                 // the same, low-passed kSteadyTau more (lead, feed-forward)
    float lead = 0;                      // rows the box reaches ahead (+ down)
    float anchorAge = 0;                 // seconds since the kept end changed
    int anchor = -1;                     // when she does not fit: -1 head, +1 feet
    float still = 0;                     // seconds without moving up or down the screen
    bool over = false;                   // does not fit with the hard margin
    int moveDir = 0;                     // she moves down (+1) / up (-1) the screen (kTrailSpeed): that end leads
    static bool Follows(const WindowInput& in) { return in.mode == 2 && !in.centre && in.playerValid; }
    float HeadRow() const { return edgeTop + (std::min)(0.f, lead); }
    float FeetRow() const { return edgeBottom + (std::max)(0.f, lead); }
    float Update(const WindowInput& in) {
        const float maxTop = (std::max)(0.f, kFrameH - in.cropH), centre = maxTop * 0.5f;
        const float hard = in.cropH * kHard, soft = in.cropH * kSoft;
        const float dt = float((std::min)((std::max)(in.dt, 0.0), 0.25));
        const float smooth = (std::max)(in.smooth, 0.05f);
        const bool restart = in.snap || !valid;
        const bool follows = Follows(in);
        float want = centre;
        if (follows) {
            const bool hasRoot = in.rootValid;
            const float outlineCentre = (in.playerTop + in.playerBottom) * 0.5f;
            if (restart || !body) {   // a new box (camera cut, crop start): her outline as it is now
                rowTop = in.playerTop; rowBottom = in.playerBottom;
                const float widen = kEnvInit * (rowBottom - rowTop);
                edgeTop = rowTop - widen; edgeBottom = rowBottom + widen; holdTop = holdBottom = 0;
                fromRoot = hasRoot; dirRow = hasRoot ? in.rootRow : outlineCentre;
                bodyVel = boxVel = steadyVel = lead = 0; moveDir = 0; anchor = -1; still = 0; anchorAge = 0; body = true;
                span = edgeBottom - edgeTop;
                over = span + 2.f * hard > in.cropH + 2.f;
            } else {
                const float lp = 1.f - std::exp(-dt / kRowTau);
                rowTop += (in.playerTop - rowTop) * lp; rowBottom += (in.playerBottom - rowBottom) * lp;
                // her direction: the floor point (round 3: the outline centre bobs 20+ rows/s with a tall Jill's steps)
                if (hasRoot != fromRoot) { fromRoot = hasRoot; dirRow = hasRoot ? in.rootRow : outlineCentre; }
                const float was = dirRow;
                dirRow += ((hasRoot ? in.rootRow : outlineCentre) - dirRow) * (hasRoot ? lp : 1.f - std::exp(-dt / kCentreTau));
                if (dt > 0.f) bodyVel += ((dirRow - was) / dt - bodyVel) * (1.f - std::exp(-dt / kVelTau));
                const int dir = bodyVel > kDirSpeed ? 1 : bodyVel < -kDirSpeed ? -1 : 0;
                // the two ends: out at once, back after the hold (sooner for the end she walks away from)
                const float boxWas = (edgeTop + edgeBottom) * 0.5f;
                const float height = (std::max)(edgeBottom - edgeTop, 1.f), touch = kEnvTouch * height;
                const float easeSlow = 1.f - std::exp(-dt / kEnvTau), easeTrail = 1.f - std::exp(-dt / kTrailTau);
                // out: her outline as drawn (a run must not lag behind the low-pass: game run r4-load, 60 rows/s up);
                // back: the low-passed outline
                const float trail = hasRoot ? kTrailSpeed : kTrailSpeedCentre;
                // (never the end the window is kept at when she does not fit: that end must stay calm)
                const bool trailTop = bodyVel > trail && !(over && anchor < 0), trailBottom = bodyVel < -trail && !(over && anchor > 0);
                moveDir = trailTop ? 1 : trailBottom ? -1 : 0;
                if (in.playerTop <= edgeTop) { edgeTop = in.playerTop; holdTop = 0; }
                else if (rowTop <= edgeTop + touch) holdTop = 0;
                else if ((holdTop += dt) > (trailTop ? kTrailHold : kEnvHold)) edgeTop += (rowTop - edgeTop) * (trailTop ? easeTrail : easeSlow);
                if (in.playerBottom >= edgeBottom) { edgeBottom = in.playerBottom; holdBottom = 0; }
                else if (rowBottom >= edgeBottom - touch) holdBottom = 0;
                else if ((holdBottom += dt) > (trailBottom ? kTrailHold : kEnvHold)) edgeBottom += (rowBottom - edgeBottom) * (trailBottom ? easeTrail : easeSlow);
                span = edgeBottom - edgeTop;
                if (dt > 0.f) boxVel += (((edgeTop + edgeBottom) * 0.5f - boxWas) / dt - boxVel) * (1.f - std::exp(-dt / kVelTau));
                steadyVel += (boxVel - steadyVel) * (1.f - std::exp(-dt / kSteadyTau));
                if (!over && span + 2.f * hard > in.cropH + 2.f) over = true;
                else if (over && span + 2.f * hard < in.cropH - 2.f) over = false;
                anchorAge += dt;
                const int before = anchor;
                // the kept end when she does not fit (round 3: kDirSpeed; game run r4-death: a slower threshold made the kept
                // end flip while she fought the alley zombies), the head after kStillHead s standing
                if (dir < 0) { still = 0; if (anchorAge > kAnchorHold) anchor = -1; }
                else if (dir > 0) { still = 0; if (anchorAge > kAnchorHold) anchor = +1; }
                else if ((still += dt) > kStillHead) anchor = -1;
                if (anchor != before) anchorAge = 0;
            }
            // a small reach ahead on the side the box moves to (the low-pass lag, as round 3), within the free room
            const float room = (std::max)(0.f, in.cropH - span - 2.f * hard);
            lead = over ? 0.f : (std::min)((std::max)(steadyVel * kLead, -room), room);
            const float head = HeadRow(), feet = FeetRow();
            if (over) {
                want = anchor > 0 ? feet + hard - in.cropH : head - hard;
                // the box with its hard margins does not fit, the box itself does: both ends stay in (only the kept end
                // has its margin as far as the room allows)
                if (edgeBottom - edgeTop <= in.cropH) want = (std::min)((std::max)(want, edgeBottom - in.cropH), edgeTop);
            } else if (restart) want = (head + feet) * 0.5f - in.cropH * 0.5f;
            else {   // lazy: move only as far as the box needs; the margin shrinks smoothly from soft to hard as she grows
                const float m = (std::min)(soft, (std::max)(hard, (in.cropH - span) * 0.5f));
                const float feed = (std::min)(smooth, kFeedMax);   // feed-forward time (a longer one amplifies the walk cycle)
                want = top + vel * smooth * 0.5f;   // nothing to do: coast to rest (the speed decays over smooth / 2)
                const float wantFeet = feet + m - in.cropH + (std::max)(0.f, steadyVel) * feed, wantHead = head - m + (std::min)(0.f, steadyVel) * feed;
                if (feet + m > want + in.cropH) want = wantFeet;
                if (head - m < want) want = wantHead;   // a conflict: the head wins, standing or walking up the screen...
                // ...but the feet while she walks down it: the end she walks to only ever moves outwards (no walk-cycle bob),
                // while the trailing end follows her outline sooner (kTrailTau) - in a tight fit that end swayed the window
                if (moveDir > 0 && feet - head + 2.f * m > in.cropH) want = wantFeet;
                // the feed-forward and the coasting never take the box past the hard margin on the other side
                const float lo = feet + hard - in.cropH, hi = head - hard;
                if (lo <= hi) want = (std::min)((std::max)(want, lo), hi);
            }
        } else body = false;
        if (in.merc) want = (std::min)(want, 30.f);
        want = (std::min)((std::max)(want, 0.f), maxTop);
        if (restart) { top = want; vel = 0; valid = true; wait = 0; rested = 1e9f; moving = false; return top; }
        const bool calm = !in.centre;   // round 5: the delay and the speed limit (not for centred movies / doors / scenes)
        if (calm && in.delay > 0.f && !moving) {
            const bool need = std::fabs(want - top) > kNeedRows;
            wait = need ? wait + dt : 0.f;
            rested += dt;
            // not yet: the camera may change before then (straight on when it rested only a moment: a slow walk)
            if (!need || (wait < in.delay && rested > kRearm)) { vel = 0.f; return top; }
        }
        moving = true; wait = 0.f;
        const float before = top, vel0 = vel;
        top = SmoothDamp(top, want, vel, follows ? (over ? (std::min)(smooth, kOverSmooth) : smooth) : 0.25f, kMaxSpeed, dt);
        const bool limited = calm && in.maxSpeed > 0.f && dt > 0.f;
        if (follows && !over) {
            // the box keeps the hard margin: a box past it is brought back within kClampTau (a slow spring that a running
            // Jill outpaces is taken over without a jolt), and it never leaves the window itself. (Not when she does not fit
            // with the margins: the target keeps both ends in if the box itself fits, the spring smooths it - a clamp there
            // passed the steps of the leading end on to the window: test_round4 tight walk)
            const float margin = hard;
            float lo = FeetRow() + margin - in.cropH; const float hi = HeadRow() - margin;
            if (lo > hi) lo = hi;
            const float held = (std::min)((std::max)(top, lo), hi);
            if (held != top) {
                top += (held - top) * (1.f - std::exp(-dt / kClampTau));
                if (!limited) top = (std::min)((std::max)(top, lo - margin), hi + margin);   // round 5: within the limits only
                if (dt > 0.f) vel = (top - before) / dt;
            }
        }
        if (limited) {   // round 5: at most maxSpeed, speed changes at most `accel` per second, braking in time for the target
            const float accel = in.maxSpeed / AccelTime(smooth);
            float v = (std::min)((std::max)((top - before) / dt, -in.maxSpeed), in.maxSpeed);
            // braking is planned with kBrake of the allowed deceleration: the frame time jitters (game runs: 0.015..0.018 s),
            // and planned with all of it the window drifted above the plan and still hit the edge at 2..4 rows/s
            const float d = want - before, brake = accel * kBrake;
            if (v * d > 0.f) { const float stop = StopSpeed(std::fabs(d), brake, dt); if (std::fabs(v) > stop) v = d > 0.f ? stop : -stop; }
            // never run into the edge of the picture (or the Mercenaries timer row): those stop the window dead
            const float bottomEdge = in.merc ? (std::min)(maxTop, 30.f) : maxTop;
            if (v > 0.f) v = (std::min)(v, StopSpeed(bottomEdge - before, brake, dt));
            else if (v < 0.f) v = (std::max)(v, -StopSpeed(before, brake, dt));
            v = vel0 + (std::min)((std::max)(v - vel0, -accel * dt), accel * dt);
            top = before + v * dt; vel = v;
        }
        if (in.merc && top > 30.f) { top = 30.f; vel = 0; }
        if (top < 0.f || top > maxTop) { top = (std::min)((std::max)(top, 0.f), maxTop); vel = 0; }
        if (std::fabs(vel) < kRestSpeed && std::fabs(want - top) < kRestRows && moving) { moving = false; rested = 0.f; }   // at rest
        return top;
    }
};

// The wrapper's present vertex (FVF 0x1C4), 32 bytes.
struct QuadVertex { float x, y, z, rhw; DWORD diffuse, specular; float u, v; };
static_assert(sizeof(QuadVertex) == 32, "wrapper present vertex");

// Widescreen rectangle of aspect `aspect` fitted into the viewport, in whole pixels.
inline Rect FitRect(const D3DVIEWPORT9& vp, float aspect) {
    Rect r; const float w = float(vp.Width), h = float(vp.Height);
    if (w / h >= aspect) { const float tw = std::floor(h * aspect + 0.5f); r.l = float(vp.X) + std::floor((w - tw) * 0.5f + 0.5f); r.t = float(vp.Y); r.r = r.l + tw; r.b = r.t + h; }
    else { const float th = std::floor(w / aspect + 0.5f); r.l = float(vp.X); r.t = float(vp.Y) + std::floor((h - th) * 0.5f + 0.5f); r.r = r.l + w; r.b = r.t + th; }
    return r;
}
// Is this the wrapper's full-texture quad (every vertex on a u/v corner, the corners at consistent positions, centred in
// the viewport)? Fills its rectangle.
inline bool RecogniseQuad(const QuadVertex* v, unsigned count, const D3DVIEWPORT9& vp, Rect& quad, const char** why) {
    auto corner = [](float c) { return std::fabs(c) < 1e-3f ? 0 : std::fabs(c - 1.f) < 1e-3f ? 1 : -1; };
    float xs[2] = {NAN, NAN}, ys[2] = {NAN, NAN}; unsigned seen = 0;
    for (unsigned i = 0; i < count; ++i) {
        const int cu = corner(v[i].u), cv = corner(v[i].v);
        if (cu < 0 || cv < 0 || !std::isfinite(v[i].x) || !std::isfinite(v[i].y)) { if (why) *why = "vertex not on a texture corner"; return false; }
        if (std::isnan(xs[cu])) xs[cu] = v[i].x; else if (std::fabs(xs[cu] - v[i].x) > 0.01f) { if (why) *why = "corner x differs"; return false; }
        if (std::isnan(ys[cv])) ys[cv] = v[i].y; else if (std::fabs(ys[cv] - v[i].y) > 0.01f) { if (why) *why = "corner y differs"; return false; }
        seen |= 1u << (cu * 2 + cv);
    }
    if (seen != 15u || !(xs[1] > xs[0]) || !(ys[1] > ys[0])) { if (why) *why = "not a full quad"; return false; }
    quad = {xs[0], ys[0], xs[1], ys[1]};
    const float cx = (xs[0] + xs[1]) * 0.5f + 0.5f, cy = (ys[0] + ys[1]) * 0.5f + 0.5f;
    const float vx = vp.X + vp.Width * 0.5f, vy = vp.Y + vp.Height * 0.5f;
    if (std::fabs(cx - vx) > (std::max)(2.f, vp.Width * 0.01f) || std::fabs(cy - vy) > (std::max)(2.f, vp.Height * 0.01f)) {
        if (why) *why = "wrapper picture not centred in the viewport"; return false;
    }
    return true;
}
// Rewrites the quad: positions = target, v = rows top..top+cropH of the frame (u stays 0..1: the full width).
inline void RewriteQuad(const QuadVertex* in, QuadVertex* out, unsigned count, const Rect& target, float top, float cropH) {
    const float v0 = top / kFrameH, v1 = (top + cropH) / kFrameH;
    for (unsigned i = 0; i < count; ++i) {
        out[i] = in[i];
        const bool right = in[i].u > 0.5f, bottom = in[i].v > 0.5f;
        out[i].x = (right ? target.r : target.l) - 0.5f;
        out[i].y = (bottom ? target.b : target.t) - 0.5f;
        out[i].v = bottom ? v1 : v0;
    }
}

// ------------------------------------------------------------------ game state
struct GameState {
    bool verified = false;       // exe checks passed
    bool gameplay = false;       // slot 0 = gameplay main
    bool suspended = false;      // slot 0 suspended by an overlay program / the status screen
    bool program = false;        // slot 1 runs an overlay program (game over, results, ...)
    bool movie = false;
    bool message = false; float messageTop = 185;
    bool merc = false, door = false;
    bool doorAnimation = false;  // round 2: A5C880 & 0x400000 = the door animation (the HD door models draw on it: 46973B)
    bool cutscene = false;       // round 2: gameplay with A61F5C & 0x08 (event script: story scenes 0x88/0xA8/0x1C8, never set
                                 // in free play, grabs 0x180 or messages; game runs 09.11)
    int messageLines = 0;        // round 2: lines of the message page on screen (0xFC = new line, 0xFD page, 0xFE end)
    int messageChoice = -1;      // round 3: argument of the page's choice code 0xFB (-1 none; & 0x80 = live selection)
    bool messageMore = false;    // round 3: the page ends with 0xFD (the "next page" arrow is drawn below it)
    uint16_t messageType = 0;    // round 3: message object +75E6 ([A57486]): & 0x3000 = 0x1000 item / 0x2000 room message
    bool slides = false;         // round 3: slot 1 runs opening.bin (prologue slides) or staff_r.bin (staff roll)
    uint32_t programEntry = 0;   // round 3 QA: slot 1 entry
    uint32_t camera = 0;         // stage/room/camera
    bool playerValid = false; float playerTop = 0, playerBottom = 0;
    bool pointValid = false; float pointTop = 0, pointBottom = 0;   // round 2 QA: the old actor-point estimate (y-1750 .. y)
    bool rootValid = false; float rootRow = 0;   // round 3: actor position (+34/38/3C) projected: where she stands
    float rootScale = 0;         // round 4: rows between that point and 1750 units above it (her standing height), 0 unknown
    bool blackText = false;      // round 4: a task slot runs the text-on-black task 42F4B0 (after a load, at a new game)
};

using Log = void (*)(const char*, ...);
inline Log gLog = nullptr;
inline re3exe::Base gBase;
inline const bool* gMoviePlaying = nullptr;
inline float (*gMovieAspect)() = nullptr;   // re3playermovies::ScreenAspect (player movie picture on screen), wired in proxy.cpp (merge 2026-11-09)
inline std::atomic<int> gMode{0};
inline float gAspect = 16.f / 9.f;      // 0 = auto (the output's aspect)
inline char gAspectText[32] = "16:9";
inline bool gInstalled = false, gVerified = false;
inline uintptr_t gGameplayEntry = 0, gStatusEntry = 0;
// Round 4 (09.11 night, user: "the text on the black screen after loading a save is still cut at the bottom"): that screen
// ("I will survive. They can't stop, my last escape...", and at a new game "Farewell to my life...") is the task 42F4B0
// (installed by 450FA2 / 451858 / 451FFD; lines at y 0xB9 and 0xC9 through 42EF50, a page every 180 ticks). Game runs
// r3-final / r4-load: it runs in task slot 1 exactly while the text shows. Round 3 found that screen with the black probe,
// which only runs while Jill is not on screen - and after a death the HD renderer's last rows of Jill stayed "current"
// through the whole text (no HD build there, see proxy.cpp gPlayerRows), so the probe never ran and the crop cut the text.
inline uintptr_t gTextEntry = 0;
inline std::atomic<int> gSmoothLevel{kSmoothDefault};   // round 4: pilot.ini widescreen_smooth / F5 menu (kSmoothTimes)
inline std::atomic<int> gSpeedLevel{kSpeedDefault};     // round 5: pilot.ini widescreen_speed / F5 menu (kSpeeds)
inline std::atomic<int> gDelayLevel{kDelayDefault};     // round 5: pilot.ini widescreen_delay / F5 menu (kDelays)
inline std::atomic<int> gPeekOn{1};                     // round 6: pilot.ini widescreen_peek / F5 menu (the right stick looks up / down)
// round 6: the right stick, y -1 (up) .. +1 (down); false = no pad, the stick is bound to a game action, input is blocked
inline bool (*gPeekStick)(float& y) = nullptr;
inline uintptr_t gPrograms[17]{};
inline volatile LONG gFullFrameUiTick = 0;  // GetTickCount of the last NoteFullFrameUi (0 = never)
inline const volatile bool* gFullFrameUiOpen = nullptr;   // round 2 (09.11): re3menu::modal = the mod menu is open (whole time)
inline bool gQa = false;                     // pilot.ini debug_warp=1: WIDESCREEN_QA lines (state changes, follow rows)
// Round 2 (09.11, user: "the black screen with text at the bottom after loading a save / before the first room is cropped
// too; keep it 4:3, it is black anyway"): proxy backgroundHook count (one per room background drawn) and the fps60
// in-between pass flag. Three presented frames without a new room background (text on black, door animation, loading)
// show the 4:3 frame; the first background brings the crop back.
inline const volatile unsigned* gBackgroundCount = nullptr;
// Round 2: rows of the player's HD model as the renderer built it this frame (proxy: model_integration.h notePlayerRows);
// false when the HD player was not built in this or the previous build (native model, off screen): the bones then.
inline bool (*gPlayerRows)(float& top, float& bottom) = nullptr;
inline const volatile bool* gExtraPass = nullptr;
inline bool (*gStateOverride)(GameState&) = nullptr;   // offline tests

template<class T> inline T Data(uint32_t rva) { return *reinterpret_cast<const volatile T*>(gBase.value + rva); }

// Round 2 (09.11): lines of the message page now on screen. The glyph loop 42E7E0 draws from the page start [A57470]
// (message object A4FEA0 +75D0) with y = [A57482] (+75E2): FC = next line (+16 rows, 42EAD0), FD xx = next page, FE = end;
// F3/F4/F9/FA and EA..F0 carry one argument byte. Scans at most 600 bytes; 1..6 lines.
// Round 3 (09.11 evening, user: "the 'discard the key?' / typewriter question is no longer cut, but its yes/no below is not
// visible"): 0xFB xx = choice (state machine 42DB70 code switch 42DD61 -> state 4 -> 42DF01: xx & 0x80 live selection
// 42E360, else 42E060). The page ends there.
struct MessagePageInfo { int lines = 3; int choice = -1; bool more = false; };
inline MessagePageInfo MessagePage() {
    MessagePageInfo info;
    const auto* text = reinterpret_cast<const uint8_t*>(uintptr_t(Data<uint32_t>(0x657470)));
    MEMORY_BASIC_INFORMATION mbi{};
    if (!text || !VirtualQuery(text, &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT || (mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS))) return info;
    const uintptr_t end = (std::min)(uintptr_t(reinterpret_cast<uintptr_t>(text) + 600), uintptr_t(reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize));
    int lines = 1;
    for (const uint8_t* c = text; reinterpret_cast<uintptr_t>(c) < end; ++c) {
        if (*c == 0xfe) break;
        if (*c == 0xfd) { info.more = true; break; }
        if (*c == 0xfb) { if (reinterpret_cast<uintptr_t>(c) + 1 < end) info.choice = c[1]; break; }
        if (*c == 0xfc) ++lines;
        else if ((*c >= 0xea && *c <= 0xf0) || *c == 0xf3 || *c == 0xf4 || *c == 0xf9 || *c == 0xfa) ++c;
    }
    info.lines = (std::min)(lines, 6);
    return info;
}
inline int MessageLines() { return MessagePage().lines; }

inline bool ReadGame(GameState& s) {
    if (gStateOverride) return gStateOverride(s);
    s = GameState{};
    if (!gVerified || !gBase.value) return false;
    s.verified = true;
    s.gameplay = Data<uint32_t>(0x659128) == gGameplayEntry;
    s.suspended = (Data<uint8_t>(0x659124) & 0x40) != 0;
    if (Data<uint16_t>(0x6591a4)) {
        const uintptr_t entry = Data<uint32_t>(0x6591a8);
        s.programEntry = uint32_t(entry);
        if (entry == gStatusEntry) s.program = true;
        for (uintptr_t p : gPrograms) if (p && entry == p) s.program = true;
    }
    // round 3 (user: "make the slides at the start and the end of the game (with the credits) widescreen without bars too"):
    // program 5 opening.bin (the prologue slideshow, pictures on rows 24..216, no text) and program 11 staff_r.bin (the
    // staff roll) in any of the three task slots (status A59124/A591A4/A59224, entry +4; a chain 42AF60 -> 42B040 replaces the
    // entry of the slot running it: from the title menu that is slot 2, game run r3-c2)
    for (unsigned slot = 0; slot < 3; ++slot) {
        const uintptr_t entry = (slot == 0 || Data<uint16_t>(0x659124 + slot * 0x80)) ? uintptr_t(Data<uint32_t>(0x659128 + slot * 0x80)) : 0;
        if (entry && ((gPrograms[5] && entry == gPrograms[5]) || (gPrograms[11] && entry == gPrograms[11]))) s.slides = true;
        if (entry && gTextEntry && entry == gTextEntry) s.blackText = true;   // round 4
    }
    s.movie = gMoviePlaying && *gMoviePlaying;
    s.message = (Data<uint8_t>(0x657468) & 0x80) != 0;
    if (s.message) { const int y = Data<int16_t>(0x657482); s.messageTop = (y >= 0 && y < 240) ? float(y) : 185.f; }
    const uint32_t flags = Data<uint32_t>(0x65c880);
    s.merc = (flags & 0x80) != 0;
    s.door = (flags & 0x400) != 0;
    s.doorAnimation = (flags & 0x400000) != 0;
    s.cutscene = s.gameplay && (Data<uint32_t>(0x661f5c) & 0x08) != 0;
    if (s.message) {
        const MessagePageInfo page = MessagePage();
        s.messageLines = page.lines; s.messageChoice = page.choice; s.messageMore = page.more;
        s.messageType = Data<uint16_t>(0x657486);
    }
    float drawnTop = 0, drawnBottom = 0;
    const bool drawn = s.gameplay && gPlayerRows && gPlayerRows(drawnTop, drawnBottom) && drawnBottom > drawnTop &&
                       drawnBottom > -40.f && drawnTop < kFrameH + 40.f && drawnBottom - drawnTop < 2.f * kFrameH;
    s.camera = (uint32_t(Data<uint16_t>(0x661fa6) & 0xff) << 16) | (uint32_t(Data<uint16_t>(0x661fa8) & 0xff) << 8) | (Data<uint16_t>(0x661faa) & 0xff);
    // Jill's head and feet rows: camera (51C398: int16 rotation 4.12, int32 translation) and GTE H / OFY ([5349C8] +78/+74)
    if (s.gameplay) {
        const auto* player = reinterpret_cast<const uint8_t*>(gBase.value + 0x65cbf4);
        const auto* camera = reinterpret_cast<const uint8_t*>(gBase.value + 0x11c398);
        const auto* gte = *reinterpret_cast<const uint8_t* const*>(gBase.value + 0x1349c8);
        MEMORY_BASIC_INFORMATION mbi{};
        const bool gteOk = gte && VirtualQuery(gte, &mbi, sizeof(mbi)) && mbi.State == MEM_COMMIT &&
                           !(mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS)) && reinterpret_cast<uintptr_t>(gte) + 0x80 <= reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
        if (gteOk) {
            const float h = float(*reinterpret_cast<const uint16_t*>(gte + 0x78));
            float ofy = float(*reinterpret_cast<const int32_t*>(gte + 0x74));
            if (std::fabs(ofy) > 4096.f) ofy /= 65536.f;   // 16.16 like the PSX GTE
            const int16_t* r = reinterpret_cast<const int16_t*>(camera);
            const int32_t* t = reinterpret_cast<const int32_t*>(camera + 20);
            const float px = float(*reinterpret_cast<const int32_t*>(player + 0x34)), py = float(*reinterpret_cast<const int32_t*>(player + 0x38)),
                        pz = float(*reinterpret_cast<const int32_t*>(player + 0x3c));
            auto row = [&](float wy, float& out) {
                const float cy = (r[3] * px + r[4] * wy + r[5] * pz) / 4096.f + float(t[1]);
                const float cz = (r[6] * px + r[7] * wy + r[8] * pz) / 4096.f + float(t[2]);
                if (!(cz > 100.f) || !std::isfinite(cy)) return false;
                out = ofy + h * cy / cz; return std::isfinite(out);
            };
            float head = 0, feet = 0;
            if (h > 0 && ofy > 0 && ofy < 240 && row(py - 1750.f, head) && row(py, feet) && feet > head &&
                feet > -40.f && head < kFrameH + 40.f && feet - head < 2.f * kFrameH) {
                s.pointValid = true; s.pointTop = head; s.pointBottom = feet; s.rootValid = true; s.rootRow = feet; s.rootScale = feet - head;
            }
            // Round 2 (09.11, game run: the estimate above put the "head" at her hips and the "feet" below her soles, so
            // the follow window kept her feet and cut her head): the bones the game posed this frame (parts [+108], count
            // +8, stride BC, world position +54 - the points the HD renderer and the native visibility test 426790 use).
            // Top = the highest joint (the head pivot) + 300 units of head, bottom = the lowest joint (ankle) + 120 units.
            const auto* parts = *reinterpret_cast<const uint8_t* const*>(player + 0x108);
            const unsigned count = player[8];
            MEMORY_BASIC_INFORMATION pm{};
            if (h > 0 && ofy > 0 && ofy < 240 && count >= 2 && count <= 24 && parts && VirtualQuery(parts, &pm, sizeof(pm)) && pm.State == MEM_COMMIT &&
                !(pm.Protect & (PAGE_GUARD | PAGE_NOACCESS)) && reinterpret_cast<uintptr_t>(parts) + count * 0xbcu <= reinterpret_cast<uintptr_t>(pm.BaseAddress) + pm.RegionSize) {
                const int32_t* top = nullptr; const int32_t* bottom = nullptr;
                for (unsigned i = 0; i < count; ++i) {
                    const int32_t* w = reinterpret_cast<const int32_t*>(parts + i * 0xbcu + 0x54);
                    if (std::abs(w[0] - int32_t(px)) > 4000 || std::abs(w[1] - int32_t(py)) > 4000 || std::abs(w[2] - int32_t(pz)) > 4000) continue;   // not posed
                    if (!top || w[1] < top[1]) top = w;
                    if (!bottom || w[1] > bottom[1]) bottom = w;
                }
                if (gQa && gLog) {   // round 2 QA: raw projection inputs every 3 s (checked against 4:3 captures offline)
                    static DWORD lastDump = 0; const DWORD now = GetTickCount();
                    if (now - lastDump >= 3000) {
                        lastDump = now; char line[1400];
                        int n = sprintf_s(line, "WIDESCREEN_QA_BONES cam=%d,%d,%d,%d,%d,%d,%d,%d,%d t=%d,%d,%d h=%.0f ofy=%.2f pos=%.0f,%.0f,%.0f parts=%u:",
                                          r[0], r[1], r[2], r[3], r[4], r[5], r[6], r[7], r[8], t[0], t[1], t[2], h, ofy, px, py, pz, count);
                        for (unsigned i = 0; i < count && n > 0 && n < int(sizeof(line)) - 40; ++i) {
                            const int32_t* w = reinterpret_cast<const int32_t*>(parts + i * 0xbcu + 0x54);
                            n += sprintf_s(line + n, sizeof(line) - n, " %d,%d,%d", w[0], w[1], w[2]);
                        }
                        gLog("%s", line);
                    }
                }
                auto rowAt = [&](const int32_t* w, float dy, float& out) {
                    const float x = float(w[0]), y = float(w[1]) + dy, z = float(w[2]);
                    const float cy = (r[3] * x + r[4] * y + r[5] * z) / 4096.f + float(t[1]);
                    const float cz = (r[6] * x + r[7] * y + r[8] * z) / 4096.f + float(t[2]);
                    if (!(cz > 100.f) || !std::isfinite(cy)) return false;
                    out = ofy + h * cy / cz; return std::isfinite(out);
                };
                float bt = 0, bb = 0;
                if (top && bottom && top != bottom && rowAt(top, -300.f, bt) && rowAt(bottom, 120.f, bb) && bb > bt &&
                    bb > -40.f && bt < kFrameH + 40.f && bb - bt < 2.f * kFrameH) {
                    s.playerValid = true; s.playerTop = bt; s.playerBottom = bb;
                }
            }
        }
    }
    // round 2: the HD model's own rows win (QA keeps the bone estimate in "point")
    if (drawn) {
        if (s.playerValid) { s.pointTop = s.playerTop; s.pointBottom = s.playerBottom; }
        s.playerValid = true; s.playerTop = drawnTop; s.playerBottom = drawnBottom;
    }
    return true;
}

// Round 6 (09.11 night, user: "while the right stick is deflected the camera moves that way if there is something to show
// there; released, it centres on the character again"): a layer over the follow / centred window. The stick (dead zone
// 0.25, then linear) is followed by a critically damped spring of 0.3 s - it starts and stops softly (the user minds
// sharp window moves), a full sweep of the picture takes about 0.7 s - and moves the shown window from where the window
// below stands toward the edge of the picture on that side: full deflection = the edge, nothing to show on that side =
// no movement. The window below keeps following her, so on release the picture eases back to it. With the stick at rest
// the result is the window below, bit for bit.
struct Peek {
    static constexpr float kDead = 0.25f, kTime = 0.30f;
    float value = 0, vel = 0;   // the eased stick, -1 (up) .. +1 (down), and its speed
    static float Shape(float stick) {
        const float a = std::fabs(stick);
        if (!(a > kDead)) return 0.f;
        const float v = (std::min)(1.f, (a - kDead) / (1.f - kDead));
        return stick < 0.f ? -v : v;
    }
    // below: the window top without the peek; bottomEdge: the lowest top allowed (the picture's, or the Mercenaries timer's)
    float Update(float below, float stick, float bottomEdge, float dt) {
        const float want = Shape(stick);
        value = SmoothDamp(value, want, vel, kTime, 100.f, dt);
        if (want == 0.f && std::fabs(value) < 0.003f && std::fabs(vel) < 0.05f) value = vel = 0.f;
        if (value == 0.f) return below;
        return value < 0.f ? below + value * (std::max)(0.f, below) : below + value * (std::max)(0.f, bottomEdge - below);
    }
};

// ------------------------------------------------------------------ runtime
inline const char* ModeName(int m) { return m == 1 ? "centred" : m == 2 ? "follow" : "off"; }
inline void SetMode(int mode);
inline int Mode() { return gMode.load(); }
// The F5 mod menu (or any full-frame mod UI drawn into the game frame): call every frame it is drawn; the full 4:3 frame
// is shown while it was noted in the last 200 ms.
inline void NoteFullFrameUi() { InterlockedExchange(&gFullFrameUiTick, LONG(GetTickCount() | 1)); }

struct Runtime {
    IDirect3DTexture9* armed = nullptr;   // the wrapper's copy texture, pointer identity only (no reference kept)
    UINT armedW = 0, armedH = 0;
    Window window;
    bool cropping = false, everCropping = false, quadSeen = false, layoutLogged = false;
    std::string lastState;
    uint32_t lastCamera = ~0u; bool lastMovie = false, lastDoor = false; int lastMode = -1;
    LARGE_INTEGER lastTime{};
    float cropTop = 0, cropRows = kFrameH;   // for CropWindow (render thread)
    Peek peek;                               // round 6: the right stick over the window
    float shown = 16.f / 9.f;                // round 6: aspect of the crop on screen (for the frames held under the F5 menu)
    int prevMode = -1; bool held = false;    // round 6: mode at the previous present; the F5 menu holds the picture
    QuadVertex quad[64];
};
inline Runtime gRun;

// Round 6: the crop window on screen now (top row and rows of the 240-row frame), for the F5 menu to lay itself out in;
// false = the whole 4:3 frame is shown. Game thread (the menu loop and the present quad run on it).
inline bool MenuView(float& top, float& rows) {
    if (!gMode.load() || !gRun.cropping) return false;
    top = gRun.cropTop; rows = gRun.cropRows;
    return true;
}
// Fix 2026-11-11: rows of the 240-row frame the crop cuts off at the top right now (0 while the 4:3 frame is shown). What the
// game draws at the top of the frame for the whole scene (the missile radar, proxy.cpp radarDrawHook) is moved down by it.
inline int HudTop() { return gRun.cropping ? int(std::lround(gRun.cropTop)) : 0; }
inline void SetPeek(int on) {
    const int value = on ? 1 : 0, before = gPeekOn.exchange(value);
    if (before != value && gLog) gLog("WIDESCREEN_PEEK %d -> %d (the right stick looks up / down)", before, value);
}
inline void SetMode(int mode) {
    if (mode < 0 || mode > 2) mode = 0;
    const int before = gMode.exchange(mode);
    if (before != mode && gLog) gLog("WIDESCREEN_MODE %d (%s) -> %d (%s)", before, ModeName(before), mode, ModeName(mode));
}
// Round 4: camera smoothing of the follow mode (pilot.ini widescreen_smooth, F5 menu), 0..3 -> kSmoothTimes.
inline void SetSmooth(int level) {
    level = (std::min)((std::max)(level, 0), kSmoothLevels - 1);
    const int before = gSmoothLevel.exchange(level);
    if (before != level && gLog) gLog("WIDESCREEN_SMOOTH %d -> %d (spring %.2f s)", before, level, SmoothTime(level));
}
// Round 5: speed limit (pilot.ini widescreen_speed 0..4 -> kSpeeds) and start delay (widescreen_delay 0..5 -> kDelays).
inline void SetSpeed(int level) {
    level = (std::min)((std::max)(level, 0), kSpeedLevels - 1);
    const int before = gSpeedLevel.exchange(level);
    if (before != level && gLog) gLog("WIDESCREEN_SPEED %d -> %d (limit %.0f rows/s%s)", before, level, SpeedLimit(level), level ? "" : " = none");
}
inline void SetDelay(int level) {
    level = (std::min)((std::max)(level, 0), kDelayLevels - 1);
    const int before = gDelayLevel.exchange(level);
    if (before != level && gLog) gLog("WIDESCREEN_DELAY %d -> %d (%.2f s)", before, level, StartDelay(level));
}
// Rows of the 4:3 frame on screen as fractions (0..1) for overlays drawn into the frame; false = the full frame.
inline bool CropWindow(float& top, float& bottom) {
    if (!gRun.cropping || gMode.load() == 0) { top = 0; bottom = 1; return false; }
    top = gRun.cropTop / kFrameH; bottom = (gRun.cropTop + gRun.cropRows) / kFrameH; return true;
}

// Round 2 (09.11, user: "in the cutscenes the camera slides down and a wide black band shows at the bottom; same with the
// 'discard the key?' text"): the window no longer moves for message text. Story scenes draw their own black bars
// (rows 0..24 and 216..240) and voice their lines through the message system without visible text, so the old
// "show y-8..y+56" rule slid the window onto the lower bar. Now the TEXT moves: while the crop is shown, the message's
// top line [A57482] (read by the glyph loop every frame, written by 42DA10 only when a message starts) is lifted so the
// page (lines x 16 rows) ends 4 rows above the window bottom, and put back when the crop ends or the message changes.
struct MessagePlacement { bool owned = false; int16_t original = 185, written = 185; };
inline MessagePlacement gMessage;

// Round 3 (09.11 evening, user: "with the 'discard the key?' or the typewriter question the text is fine now, but the yes/no
// choice below it is not visible"; "in the negative-picture scenes with a choice of two decisions the lower one is cut"):
// the choices are not drawn at the message's y.
//   Yes/No (42E060, choice code FB with xx & 0x80 clear): side by side at a FIXED row - immediates of
//   `mov [esp+1C], 0CDh` 42E1AB (item messages, type & 0x3000 = 0x1000, text at 173) and `mov [esp+1C], 0C9h` 42E1C1
//   (room messages, text at 185 -> the choice on its second line 201); the cursor uses the same row (42E30B).
//   They are moved by the same amount as the text: the immediates are rewritten while a lifted message shows.
//   Live selection (42C780: message 0x2001 + FB 8x, drawn by 42E360): two options below each other, 22 rows apart, at the
//   row in [A58783] = 0xB3 written by `mov byte [A58783], 0B3h` 42C7FA (the negative flash before uses 0x62, inside).
//   While the crop shows, that immediate and the byte (when it holds 0xB3) are lifted so both options end 4 rows above
//   the window bottom.
struct CodePatches {
    bool choiceOk = false, liveOk = false;
    volatile int32_t* choiceImm[2]{};      // 42E1AF (0xCD), 42E1C5 (0xC9)
    int choiceDelta = 0;
    volatile uint8_t* liveImm = nullptr;   // 42C800 (0xB3)
    int liveWritten = -1;                  // the row we put into [A58783] (-1 none)
};
inline CodePatches gPatches;
constexpr int kChoiceItemRow = 0xcd, kChoiceRoomRow = 0xc9, kLiveRow = 0xb3;
inline void InstallCodePatches() {
    auto& p = gPatches;
    static const uint8_t item[] = {0xc7, 0x44, 0x24, 0x1c, 0xcd, 0x00, 0x00, 0x00};
    static const uint8_t room[] = {0xc7, 0x44, 0x24, 0x1c, 0xc9, 0x00, 0x00, 0x00};
    static const uint8_t live[] = {0xc6, 0x05, 0x83, 0x87, 0xa5, 0x00, 0xb3};
    const uintptr_t a = gBase + 0x2e1ab, b = gBase + 0x2e1c1, c = gBase + 0x2c7fa;
    DWORD old = 0;
    if (!memcmp(reinterpret_cast<const void*>(a), item, sizeof(item)) && !memcmp(reinterpret_cast<const void*>(b), room, sizeof(room)) &&
        VirtualProtect(reinterpret_cast<void*>(a), (b + sizeof(room)) - a, PAGE_EXECUTE_READWRITE, &old)) {
        p.choiceImm[0] = reinterpret_cast<volatile int32_t*>(a + 4); p.choiceImm[1] = reinterpret_cast<volatile int32_t*>(b + 4); p.choiceOk = true;
    }
    if (!memcmp(reinterpret_cast<const void*>(c), live, sizeof(live)) && VirtualProtect(reinterpret_cast<void*>(c), sizeof(live), PAGE_EXECUTE_READWRITE, &old)) {
        p.liveImm = reinterpret_cast<volatile uint8_t*>(c + 6); p.liveOk = true;
    }
    if (gLog) gLog("WIDESCREEN choice rows: yes/no %s, live selection %s", p.choiceOk ? "42E1AB/42E1C1" : "native (code differs)", p.liveOk ? "42C7FA" : "native (code differs)");
}
inline void SetChoiceDelta(int delta) {
    auto& p = gPatches;
    if (!p.choiceOk || delta == p.choiceDelta) return;
    *p.choiceImm[0] = kChoiceItemRow + delta; *p.choiceImm[1] = kChoiceRoomRow + delta; p.choiceDelta = delta;
    FlushInstructionCache(GetCurrentProcess(), nullptr, 0);
}
// row: the live-selection row for this frame (kLiveRow = native)
inline void SetLiveRow(int row) {
    auto& p = gPatches;
    if (!p.liveOk) return;
    row = (std::min)((std::max)(row, 0), kLiveRow);
    if (*p.liveImm != uint8_t(row)) { *p.liveImm = uint8_t(row); FlushInstructionCache(GetCurrentProcess(), nullptr, 0); }
    auto& data = *reinterpret_cast<volatile uint8_t*>(gBase.value + 0x658783);
    if (data == kLiveRow || (p.liveWritten >= 0 && data == uint8_t(p.liveWritten))) data = uint8_t(row);
    p.liveWritten = row == kLiveRow ? -1 : row;
}

inline void ReleaseMessage() {
    SetChoiceDelta(0);
    if (!gMessage.owned || !gBase.value) return;
    auto& y = *reinterpret_cast<volatile int16_t*>(gBase.value + 0x657482);
    if (y == gMessage.written) y = gMessage.original;
    gMessage.owned = false;
}
// Rows the page needs below its y: its lines, the yes/no row (fixed: moves with the same delta), the next-page arrow
// (42DE81: y+0x2A item / y+0x1C room messages, about 8 rows tall). Pure, for tests.
inline float MessageExtent(int original, int lines, int choice, bool more, uint16_t type) {
    const bool item = (type & 0x3000) == 0x1000;
    float extent = float((std::max)(1, lines)) * 16.f;
    if (choice >= 0 && !(choice & 0x80)) extent = (std::max)(extent, float((item ? kChoiceItemRow : kChoiceRoomRow) + 16 - original));
    if (more) extent = (std::max)(extent, float((item ? 0x2a : 0x1c) + 10));
    return extent;
}
inline void PlaceMessage(float top, float cropH, int lines, int choice = -1, bool more = false, uint16_t type = 0x2000) {
    auto& y = *reinterpret_cast<volatile int16_t*>(gBase.value + 0x657482);
    if (!gMessage.owned || y != gMessage.written) { gMessage.original = y; gMessage.owned = true; }   // a new message: its own y
    const float height = MessageExtent(gMessage.original, lines, choice, more, type);
    const float lowest = top + cropH - 4.f - height, highest = top + 2.f;
    float want = float(gMessage.original);
    if (want > lowest) want = lowest;          // lift the page into the window
    if (want < highest && lowest >= highest) want = highest;   // a message above the window comes down (if it fits)
    gMessage.written = int16_t(std::lround(want));
    if (y != gMessage.written) y = gMessage.written;
    SetChoiceDelta(choice >= 0 && !(choice & 0x80) ? gMessage.written - gMessage.original : 0);
}

inline void Install(const re3exe::Base& base, const std::string& ini, Log log, const bool* moviePlaying) {
    gBase = base; gLog = log; gMoviePlaying = moviePlaying; gInstalled = true;
    gQa = GetPrivateProfileIntA("pilot", "debug_warp", 0, ini.c_str()) != 0;
    int mode = int(GetPrivateProfileIntA("pilot", "widescreen", 0, ini.c_str()));
    if (mode < 0 || mode > 2) mode = 0;
    gSmoothLevel.store((std::min)((std::max)(int(GetPrivateProfileIntA("pilot", "widescreen_smooth", kSmoothDefault, ini.c_str())), 0), kSmoothLevels - 1));
    gSpeedLevel.store((std::min)((std::max)(int(GetPrivateProfileIntA("pilot", "widescreen_speed", kSpeedDefault, ini.c_str())), 0), kSpeedLevels - 1));
    gDelayLevel.store((std::min)((std::max)(int(GetPrivateProfileIntA("pilot", "widescreen_delay", kDelayDefault, ini.c_str())), 0), kDelayLevels - 1));
    gPeekOn.store(GetPrivateProfileIntA("pilot", "widescreen_peek", 1, ini.c_str()) != 0 ? 1 : 0);   // round 6
    char text[32] = "16:9"; GetPrivateProfileStringA("pilot", "widescreen_aspect", "16:9", text, sizeof(text), ini.c_str());
    const float aspect = ParseAspect(text);
    if (aspect < 0) { if (log) log("WIDESCREEN widescreen_aspect=\"%s\" refused (W:H 4:3..32:9, a number, or auto): 16:9", text); strcpy_s(gAspectText, "16:9"); gAspect = 16.f / 9.f; }
    else { strncpy_s(gAspectText, text, _TRUNCATE); gAspect = aspect; }
    // code identity: gameplay main 42B0E0 and the status screen task 45E2F0 (both exes through exe_map.h)
    static const uint8_t gameplay[] = {0x51, 0x53, 0x55, 0x56, 0x57, 0xbe, 0x60, 0xa7, 0xa5, 0x00, 0xe8};
    static const uint8_t status[] = {0x56, 0x68, 0xe0, 0x5e, 0xa3, 0x00, 0x33, 0xf6, 0xe8};
    gGameplayEntry = base + 0x2b0e0; gStatusEntry = base + 0x5e2f0;
    const bool g = memcmp(reinterpret_cast<const void*>(gGameplayEntry), gameplay, sizeof(gameplay)) == 0;
    const bool st = memcmp(reinterpret_cast<const void*>(gStatusEntry), status, sizeof(status)) == 0;
    // overlay programs 0..16 of the program table 517970 (+4 = task entry; 17..23 are the stage loaders)
    unsigned programs = 0; const uintptr_t imageEnd = base.value + 0x667000;
    for (unsigned i = 0; i < 17; ++i) {
        const uintptr_t entry = Data<uint32_t>(0x117970 + i * 12 + 4);
        if (entry > base.value + 0x1000 && entry < imageEnd) { gPrograms[i] = entry; ++programs; }
    }
    // the game-over entry must be diedemo.bin 44B8E0
    static const uint8_t diedemo[] = {0x57, 0x33, 0xff, 0x89, 0x3d, 0xa0, 0x11, 0xa4, 0x00};
    const bool dd = gPrograms[3] && memcmp(reinterpret_cast<const void*>(gPrograms[3]), diedemo, sizeof(diedemo)) == 0;
    gVerified = g && st && programs == 17 && dd;
    gMode.store(gVerified ? mode : 0);
    if (gVerified) InstallCodePatches();
    // round 4: the text-on-black task 42F4B0 (push ecx; xor eax,eax; mov byte [A628BB],1; mov al,[A5C875]); RE3_MERCE 42F500
    static const uint8_t textTask[] = {0x51, 0x33, 0xc0, 0xc6, 0x05, 0xbb, 0x28, 0xa6, 0x00, 0x01, 0xa0, 0x75, 0xc8, 0xa5, 0x00};
    const uintptr_t textEntry = base + 0x2f4b0;
    gTextEntry = memcmp(reinterpret_cast<const void*>(textEntry), textTask, sizeof(textTask)) == 0 ? textEntry : 0;
    if (log) log("WIDESCREEN text on black: %s, camera smoothing %d (spring %.2f s), speed limit %d (%.0f rows/s), start delay %d (%.2f s)",
                 gTextEntry ? "task 42F4B0 -> 4:3" : "task not found (black probe only)", gSmoothLevel.load(), SmoothTime(gSmoothLevel.load()),
                 gSpeedLevel.load(), SpeedLimit(gSpeedLevel.load()), gDelayLevel.load(), StartDelay(gDelayLevel.load()));
    if (log) {
        if (!gVerified) log("WIDESCREEN exe check failed (gameplay=%u status=%u programs=%u diedemo=%u): widescreen stays off", g, st, programs, dd);
        log("WIDESCREEN mode=%d (%s) aspect=%s (%.4f%s) present=wrapper quad rewrite (game RT copy, wrapper bicubic)%s", gMode.load(),
            ModeName(gMode.load()), gAspectText, gAspect, gAspect == 0 ? " = output" : "", mode && !gVerified ? " DISABLED" : "");
    }
}

// device hook StretchRect (before the original): the wrapper's flip copies the game render target into level 0 of its
// copy texture with both rectangles NULL -> that texture is the one the next present quad samples
// Round 2 (09.11, game run: the monologues of story scenes - "September 28th. Daylight..", "I'm still alive..." - draw the room
// and then black over it, with text: a background IS drawn, so the no-background rule does not see them): while a story
// scene shows message text, the game frame is point-sampled into a 64x48 render target at the flip copy and read back one
// frame later (two targets in turn: the one read is a frame old, no stall). Black = every sample outside the rows of the
// text at most 6 in each channel -> the 4:3 frame (text at its own place). Pre-rendered rooms are never pure black over
// the whole frame; the probe runs only during story scenes (black between their shots shows 4:3 too: invisible on black).
struct BlackProbe {
    IDirect3DSurface9* target[2]{}; IDirect3DSurface9* readback = nullptr; IDirect3DDevice9* device = nullptr;
    bool written[2]{}; unsigned next = 0; bool failed = false; D3DFORMAT format = D3DFMT_UNKNOWN;
    int verdict = -1, blackRun = 0, pictureRun = 0;   // last verdict (-1 none, 0 picture, 1 black), consecutive black / picture
    float textTop = 0, textBottom = 0;   // rows of the text in the frame copied next (excluded)
    float copiedRows[2][2]{};            // the text rows of the frame in each target (a copy is read two flips later)
    void Release() {
        for (auto*& t : target) if (t) { t->Release(); t = nullptr; }
        if (readback) { readback->Release(); readback = nullptr; }
        written[0] = written[1] = false; verdict = -1; blackRun = 0; device = nullptr;
    }
};
inline BlackProbe gProbe;
inline bool gProbeWanted = false;   // OnDrawUP: a story scene shows text (probe the next copies)
constexpr UINT kProbeW = 64, kProbeH = 48;
inline void ProbeRead(unsigned index) {
    auto& p = gProbe;
    if (!p.written[index] || !p.target[index] || !p.readback) return;
    p.written[index] = false;
    if (FAILED(p.device->GetRenderTargetData(p.target[index], p.readback))) { p.failed = true; return; }
    D3DLOCKED_RECT lr{};
    if (FAILED(p.readback->LockRect(&lr, nullptr, D3DLOCK_READONLY))) { p.failed = true; return; }
    bool black = true; UINT hitX = 0, hitY = 0; unsigned hitValue = 0;
    for (UINT y = 0; y < kProbeH && black; ++y) {
        const float row = (float(y) + 0.5f) * kFrameH / float(kProbeH);
        if (row >= p.copiedRows[index][0] && row <= p.copiedRows[index][1]) continue;
        const auto* line = reinterpret_cast<const uint8_t*>(lr.pBits) + size_t(y) * lr.Pitch;
        for (UINT x = 0; x < kProbeW; ++x) if (line[x * 4] > 6 || line[x * 4 + 1] > 6 || line[x * 4 + 2] > 6) {
            black = false; hitX = x; hitY = y; hitValue = (std::max)((std::max)(line[x * 4], line[x * 4 + 1]), line[x * 4 + 2]); break; }
    }
    p.readback->UnlockRect();
    if (gQa && gLog && (black ? 1 : 0) != p.verdict) {
        static unsigned lines = 0;
        if (lines++ < 300) gLog("WIDESCREEN_PROBE %s band=%.0f..%.0f%s", black ? "black" : "picture", p.copiedRows[index][0], p.copiedRows[index][1],
                                black ? "" : (std::string(" first lit sample x=") + std::to_string(hitX) + " row=" + std::to_string(int((hitY + 0.5f) * 5.f)) + " value=" + std::to_string(hitValue)).c_str());
    }
    p.verdict = black ? 1 : 0; p.blackRun = black ? p.blackRun + 1 : 0; p.pictureRun = black ? 0 : p.pictureRun + 1;
}
inline void ProbeFrame(IDirect3DDevice9* d, IDirect3DSurface9* source, const D3DSURFACE_DESC& s) {
    auto& p = gProbe;
    if (p.failed || !d) return;
    if (p.device && p.device != d) p.Release();
    if (!p.target[0]) {
        if (s.Format != D3DFMT_X8R8G8B8 && s.Format != D3DFMT_A8R8G8B8) { p.failed = true; if (gLog) gLog("WIDESCREEN probe off: game frame format %u", unsigned(s.Format)); return; }
        p.device = d; p.format = s.Format;
        for (auto*& t : p.target)
            if (FAILED(d->CreateRenderTarget(kProbeW, kProbeH, s.Format, D3DMULTISAMPLE_NONE, 0, FALSE, &t, nullptr))) { p.Release(); p.failed = true; return; }
        if (FAILED(d->CreateOffscreenPlainSurface(kProbeW, kProbeH, s.Format, D3DPOOL_SYSTEMMEM, &p.readback, nullptr))) { p.Release(); p.failed = true; return; }
    }
    ProbeRead(p.next);   // the copy made two flips ago into this target (finished long since)
    if (SUCCEEDED(d->StretchRect(source, nullptr, p.target[p.next], nullptr, D3DTEXF_POINT))) {
        p.written[p.next] = true; p.copiedRows[p.next][0] = p.textTop; p.copiedRows[p.next][1] = p.textBottom;
    }
    p.next ^= 1u;
}

inline void OnStretchRect(IDirect3DDevice9* device, IDirect3DSurface9* source, const RECT* from, IDirect3DSurface9* target, const RECT* to) {
    if (!gMode.load()) {
        // round 6: widescreen is off - the crop state of the last cropped frame is forgotten (the present quad is not looked
        // at in mode 0, so it used to stay as it was for the rest of the run); not while the F5 menu is open: switching
        // the row off and on again there returns to the window the menu was opened with
        if (!(gFullFrameUiOpen && *gFullFrameUiOpen)) { gRun.cropping = false; gRun.prevMode = 0; }
        return;
    }
    if (!gVerified || !source || !target || from || to) return;
    IDirect3DTexture9* texture = nullptr;
    if (FAILED(target->GetContainer(__uuidof(IDirect3DTexture9), reinterpret_cast<void**>(&texture))) || !texture) return;
    D3DSURFACE_DESC d{}, s{};
    if (SUCCEEDED(target->GetDesc(&d)) && SUCCEEDED(source->GetDesc(&s)) && d.Width == s.Width && d.Height == s.Height &&
        d.Width >= 320 && (d.Width * 3 > d.Height * 4 ? d.Width * 3 - d.Height * 4 : d.Height * 4 - d.Width * 3) <= 12) {
        gRun.armed = texture; gRun.armedW = d.Width; gRun.armedH = d.Height;
        if (gFullFrameUiOpen && *gFullFrameUiOpen) {}   // round 6: the dimmed frame under the open F5 menu is not probed
        else if (gProbeWanted) ProbeFrame(device, source, s);
        else if (gProbe.verdict >= 0) { gProbe.verdict = -1; gProbe.blackRun = 0; gProbe.written[0] = gProbe.written[1] = false; }
    }
    texture->Release();
}

inline void OnDeviceRelease() { gRun.armed = nullptr; gRun.window.valid = false; gProbe.Release(); gProbe.failed = false; }

// device hooks DrawPrimitiveUP / DrawIndexedPrimitiveUP (before the original): returns the vertex data to draw.
inline const void* OnDrawUP(IDirect3DDevice9* d, D3DPRIMITIVETYPE type, UINT primitives, const void* data, UINT stride, UINT firstVertex, UINT vertices) {
    if (!gRun.armed || !d || !data) return data;
    if (type != D3DPT_TRIANGLELIST || primitives != 2 || stride != sizeof(QuadVertex) || firstVertex + vertices > 64 || vertices < 4) return data;
    IDirect3DBaseTexture9* bound = nullptr;
    if (FAILED(d->GetTexture(0, &bound))) return data;
    if (bound) bound->Release();
    if (bound != gRun.armed) return data;
    IDirect3DSurface9 *rt = nullptr, *bb = nullptr; bool backBuffer = false;
    if (SUCCEEDED(d->GetRenderTarget(0, &rt)) && SUCCEEDED(d->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &bb))) backBuffer = rt == bb;
    if (rt) rt->Release(); if (bb) bb->Release();
    if (!backBuffer) return data;
    gRun.armed = nullptr;   // one present quad per copy
    const int mode = gMode.load();
    if (!mode) { gRun.cropping = false; gRun.prevMode = 0; ReleaseMessage(); SetLiveRow(kLiveRow); return data; }
    D3DVIEWPORT9 vp{}; if (FAILED(d->GetViewport(&vp)) || vp.Width < 64 || vp.Height < 48) return data;
    const auto* in = static_cast<const QuadVertex*>(data);
    Rect wrapperQuad; const char* why = nullptr;
    const bool recognised = RecogniseQuad(in + firstVertex, vertices, vp, wrapperQuad, &why);
    if (!gRun.quadSeen && recognised && gLog) {
        gRun.quadSeen = true;
        gLog("WIDESCREEN present quad found: copy texture %ux%u viewport %lu,%lu %lux%lu wrapper picture %.1f,%.1f..%.1f,%.1f", gRun.armedW, gRun.armedH,
             vp.X, vp.Y, vp.Width, vp.Height, wrapperQuad.l, wrapperQuad.t, wrapperQuad.r, wrapperQuad.b);
    }
    // Round 6 (user: "with widescreen on, opening the F5 menu must not change the format to 4:3 while it is open"): the menu
    // is open and the game stands still. Nothing of the game state is judged - the rules that count presents or age (no
    // new room background for 3 presents, rows of the HD player older than 200 ms, the black probe) would all trip - and
    // the picture stays what the last game frame was: the same crop window (the menu lays itself out inside it,
    // MenuView), or the 4:3 frame. Changing the widescreen row in the open menu shows at once: off = the 4:3 frame
    // (above), on from off = a centred crop when the game stands in plain gameplay; the first game frame after the menu
    // then snaps to its own window.
    if (gFullFrameUiOpen && *gFullFrameUiOpen) {
        LARGE_INTEGER held; QueryPerformanceCounter(&held); gRun.lastTime = held;   // no time jump for the window afterwards
        const float outputAspect = float(vp.Width) / float(vp.Height);
        const float aspect = gAspect > 0 ? gAspect : (std::min)(outputAspect, 3.6f);
        const bool wide = recognised && outputAspect >= 4.f / 3.f * 1.02f && aspect >= 4.f / 3.f * 1.02f;
        if (!gRun.held && gLog)
            gLog("WIDESCREEN_HOLD F5 menu open: %s", gRun.cropping && wide ? "the crop window stays" : "the 4:3 frame stays");
        gRun.held = true;
        if (mode != gRun.prevMode && !gRun.cropping && wide) {
            GameState s; ReadGame(s);
            if (s.verified && s.gameplay && !s.suspended && !s.program && !s.movie && !s.slides && !s.blackText && !s.doorAnimation) {
                gRun.cropRows = kFrameH * (4.f / 3.f) / aspect; gRun.cropTop = (kFrameH - gRun.cropRows) * 0.5f; gRun.shown = aspect;
                gRun.cropping = true; gRun.window.valid = false; gRun.lastMode = -1;
                if (gLog) gLog("WIDESCREEN_HOLD widescreen switched on in the open menu: centred crop until the menu closes");
            }
        }
        gRun.prevMode = mode;
        if (!gRun.cropping || !wide) return data;
        RewriteQuad(in + firstVertex, gRun.quad + firstVertex, vertices, FitRect(vp, gRun.shown), gRun.cropTop, gRun.cropRows);
        if (firstVertex) memcpy(gRun.quad, in, firstVertex * sizeof(QuadVertex));
        return gRun.quad;
    }
    gRun.held = false; gRun.prevMode = mode;
    // state of this frame
    GameState s; ReadGame(s);
    static unsigned lastBackground = 0, presentsWithoutBackground = 0;
    if (gBackgroundCount && !(gExtraPass && *gExtraPass)) {
        const unsigned n = *gBackgroundCount;
        presentsWithoutBackground = n == lastBackground ? presentsWithoutBackground + 1 : 0;
        lastBackground = n;
    }
    const bool noBackground = gBackgroundCount && presentsWithoutBackground >= 3;
    // the whole story scene, not only while text shows: the monologue drops its message flag between lines and pages
    // Round 3 (09.11 evening, user: "right at the start, after the prologue slides and the movie, the text on black is still
    // cut at the bottom"): that text ("Farewell to my life. / Farewell to my home.", "This is my, last escape...", rows
    // 186..215) is drawn in room R10D0B over a black cover - a background is drawn, no message, no story-scene flag
    // (game run r3-open: A61F5C 0, A57468 0, Jill not posed). So the probe also runs in gameplay while Jill is not on
    // screen (her rows unknown: not drawn, not posed); with her on screen the picture is never black.
    gProbeWanted = !s.movie && (s.cutscene || (s.gameplay && !s.suspended && !s.program && !s.playerValid));
    // rows of the text in the next frame: the y the game draws it at after this present placed (or put back) the text
    // (game run: the glyphs of a presented frame were laid out two presents before it, so the band is the union of the
    // text's last three positions and its own one)
    auto probeRows = [&] {
        static float history[3] = {-1, -1, -1}; static int lines = 1;
        if (!gProbeWanted) { history[0] = history[1] = history[2] = -1; return; }
        history[2] = history[1]; history[1] = history[0];
        history[0] = s.message ? float(Data<int16_t>(0x657482)) : -1.f;   // text fades out of the band over three presents
        if (s.message) lines = (std::max)(1, s.messageLines);
        float low = 1e9f, high = -1e9f;
        if (gMessage.owned) low = high = float(gMessage.original);
        for (float h : history) if (h >= 0) { low = (std::min)(low, h); high = (std::max)(high, h); }
        if (low > high) { gProbe.textTop = 179.f; gProbe.textBottom = kFrameH; return; }   // between pages: the native text rows
        gProbe.textTop = low - 6.f; gProbe.textBottom = high + float(lines) * 16.f + 6.f;
    };
    if (gQa && gLog && s.verified) {   // round 2 (09.11) QA: what the game state looks like in cutscenes, doors, messages
        static uint32_t lastKey = ~0u, lastA5C880 = ~0u, lastA61F5C = ~0u, lastCam = ~0u; static unsigned lines = 0; static DWORD lastBeat = 0;
        const uint32_t a5c880 = Data<uint32_t>(0x65c880), a61f5c = Data<uint32_t>(0x661f5c);
        const uint32_t key = uint32_t(s.gameplay) | uint32_t(s.suspended) << 1 | uint32_t(s.program) << 2 | uint32_t(s.movie) << 3 |
                             uint32_t(s.message) << 4 | uint32_t(s.door) << 5 | uint32_t(s.merc) << 6 | uint32_t(s.playerValid) << 7;
        const DWORD tick = GetTickCount();
        const bool change = key != lastKey || a5c880 != lastA5C880 || a61f5c != lastA61F5C || s.camera != lastCam;
        if (lines < 6000 && (change || tick - lastBeat >= 500)) {
            ++lines; lastBeat = tick; lastKey = key; lastA5C880 = a5c880; lastA61F5C = a61f5c; lastCam = s.camera;
            gLog("WIDESCREEN_QA %s gp=%d susp=%d prog=%d movie=%d msg=%d x=%d y=%d door=%d merc=%d cam=%06x A5C880=%08x A61F5C=%08x "
                 "A57468=%02x A5CD21=%02x fade=%02x player=%d %.1f..%.1f point=%.1f..%.1f crop=%d window=%.1f scene=%d lines=%d nobg=%u probe=%d/%d "
                 "entry=%x/%x/%x slides=%d choice=%d more=%d type=%x live=%u text=%d scale=%.1f", change ? "change" : "beat", s.gameplay, s.suspended,
                 s.program, s.movie, s.message, Data<int16_t>(0x657480), Data<int16_t>(0x657482), s.door, s.merc, s.camera, a5c880, a61f5c,
                 Data<uint8_t>(0x657468), Data<uint8_t>(0x65cd21), Data<uint8_t>(0x6628c4), s.playerValid, s.playerTop, s.playerBottom,
                 s.pointTop, s.pointBottom, gRun.cropping, gRun.cropTop, s.cutscene, s.messageLines, presentsWithoutBackground, gProbe.verdict, gProbe.blackRun,
                 Data<uint32_t>(0x659128), s.programEntry, Data<uint32_t>(0x659228), s.slides, s.messageChoice, s.messageMore, s.messageType, Data<uint8_t>(0x658783),
                 int(s.blackText), s.rootScale);
        }
    }
    const float outputAspect = float(vp.Width) / float(vp.Height);
    const float aspect = gAspect > 0 ? gAspect : (std::min)(outputAspect, 3.6f);
    // round 2 (09.11, user: the menu and the frame jumped between 4:3 and the crop on every arrow key): the note stores
    // tick|1, one ms AFTER the present's tick on every even tick, and the unsigned age was then 0xFFFFFFFF -> "old" ->
    // crop. Signed age, and the open flag of the menu for its whole lifetime.
    const LONG uiTick = gFullFrameUiTick;
    const bool uiNoted = uiTick && int(GetTickCount() - DWORD(uiTick)) < 200;   // round 6: the open F5 menu is handled above
    const char* reason = nullptr;
    if (!recognised) reason = "layout";
    else if (outputAspect < 4.f / 3.f * 1.02f || aspect < 4.f / 3.f * 1.02f) reason = "output-4:3";
    else if (uiNoted) reason = "mod-menu";
    else if (!s.verified) reason = "exe";
    else if (s.movie) reason = nullptr;
    else if (s.slides) reason = nullptr;   // round 3: prologue slides / staff roll, centred
    else if (s.blackText) reason = "black";   // round 4: the text-on-black task (after a load, at a new game): 4:3
    // round 2 (09.11, user: "no crop in the door animations: the screen is black, the side bars are not visible anyway")
    else if (s.doorAnimation) reason = "door";
    else if (noBackground) reason = "black";
    // a story scene's text on black (BlackProbe): two black copies enter, two lit ones leave (one stray frame between
    // monologue pages does not flash the crop)
    else if (gProbeWanted && (gProbe.blackRun >= 2 || (gRun.lastState == "full black" && gProbe.verdict >= 0 && gProbe.pictureRun < 2))) reason = "black";
    else if (!s.gameplay) reason = "menu";
    else if (s.suspended) reason = "overlay";
    else if (s.program) reason = "program";
    const bool crop = reason == nullptr;
    // timing
    LARGE_INTEGER now, freq; QueryPerformanceCounter(&now); QueryPerformanceFrequency(&freq);
    const double dt = gRun.lastTime.QuadPart ? double(now.QuadPart - gRun.lastTime.QuadPart) / double(freq.QuadPart) : 1.0 / 60;
    gRun.lastTime = now;
    // movies are 2:1 inside the 4:3 frame (rows 40..200): a crop wider than 2:1 (21:9) would cut them, so at most 2:1 then
    const float movieAspect = gMovieAspect ? gMovieAspect() : 2.f;   // a 16:9 / 4:3 player movie is taller than the 2:1 band
    const float shown = s.movie ? (std::min)(aspect, movieAspect) : aspect;
    const Rect target = FitRect(vp, shown);
    const float cropH = kFrameH * (4.f / 3.f) / shown;
    // one log line per state change
    char state[160];
    if (crop) sprintf_s(state, "crop %s", s.movie ? "movie" : s.slides ? "slides" : "gameplay");
    else sprintf_s(state, "full %s", reason);
    if (!recognised && !gRun.layoutLogged && gLog) { gRun.layoutLogged = true; gLog("WIDESCREEN present quad not used: %s (viewport %lux%lu)", why ? why : "?", vp.Width, vp.Height); }
    if (!crop) {
        if (gRun.lastState != state && gLog)
            gLog("WIDESCREEN_STATE %s mode=%s output=%lux%lu (4:3 frame as the wrapper draws it)", state, ModeName(mode), vp.Width, vp.Height);
        gRun.lastState = state; gRun.cropping = false; gRun.window.valid = false;
        ReleaseMessage(); SetLiveRow(kLiveRow); probeRows();
        return data;
    }
    WindowInput w;
    w.mode = mode; w.cropH = cropH; w.dt = dt;
    w.snap = !gRun.cropping || mode != gRun.lastMode || s.camera != gRun.lastCamera || s.movie != gRun.lastMovie || (s.door != gRun.lastDoor && !s.door);
    w.centre = s.movie || s.door || s.cutscene || s.slides;   // round 2: story scenes centred (their bars stay outside at 16:9 and wider)
    w.playerValid = s.playerValid && !s.movie; w.playerTop = s.playerTop; w.playerBottom = s.playerBottom;
    w.rootValid = s.rootValid && !s.movie; w.rootRow = s.rootRow;
    w.scale = s.rootValid && !s.movie ? s.rootScale : 0.f; w.smooth = SmoothTime(gSmoothLevel.load());   // round 4
    w.maxSpeed = SpeedLimit(gSpeedLevel.load()); w.delay = StartDelay(gDelayLevel.load());                // round 5
    w.message = false; w.messageTop = s.messageTop;   // round 2: the text moves, not the window (PlaceMessage below)
    w.merc = s.merc && !s.movie;
    const float below = gRun.window.Update(w);
    // round 6: the right stick looks up / down (not in centred scenes, movies, door transitions: their bars)
    float stick = 0.f;
    const bool peeking = gPeekOn.load() && !w.centre && gPeekStick && gPeekStick(stick);
    const float maxTop = (std::max)(0.f, kFrameH - cropH);
    const float top = (std::min)(maxTop, (std::max)(0.f, gRun.peek.Update(below, peeking ? stick : 0.f, w.merc ? (std::min)(maxTop, 30.f) : maxTop, float(dt))));
    gRun.lastMode = mode; gRun.lastCamera = s.camera; gRun.lastMovie = s.movie; gRun.lastDoor = s.door;
    gRun.cropping = true; gRun.everCropping = true; gRun.cropTop = top; gRun.cropRows = cropH; gRun.shown = shown;
    if (s.message && !s.movie) PlaceMessage(top, cropH, s.messageLines, s.messageChoice, s.messageMore, s.messageType); else ReleaseMessage();
    SetLiveRow(int(std::floor(top + cropH - 4.f - 22.f - 16.f)));   // round 3: both live-selection options inside
    probeRows();
    if (gQa && gLog && Window::Follows(w)) {   // round 3 QA: the follow window frame by frame (offline replay of real rows)
        static unsigned traced = 0;
        if (traced++ < 40000)
            gLog("WIDESCREEN_TRACE dt=%.4f raw=%.2f..%.2f body=%.2f..%.2f span=%.2f v=%.1f anchor=%d over=%d want-top=%.2f vel=%.1f snap=%d root=%d,%.2f scale=%.2f smooth=%.2f limit=%.0f delay=%.2f wait=%.2f peek=%.3f shown=%.2f",
                 dt, s.playerTop, s.playerBottom, gRun.window.HeadRow(), gRun.window.FeetRow(), gRun.window.span, gRun.window.bodyVel,
                 gRun.window.anchor, int(gRun.window.over), below, gRun.window.vel, int(w.snap), int(w.rootValid), w.rootRow, w.scale, w.smooth,
                 w.maxSpeed, w.delay, gRun.window.moving ? -1.f : gRun.window.wait, gRun.peek.value, top);
    }
    if (gRun.lastState != state && gLog)
        gLog("WIDESCREEN_STATE %s mode=%s aspect=%.4f output=%lux%lu target=%.0f,%.0f %.0fx%.0f rows=%.1f..%.1f of 240", state, ModeName(mode), shown,
             vp.Width, vp.Height, target.l, target.t, target.r - target.l, target.b - target.t, top, top + cropH);
    gRun.lastState = state;
    RewriteQuad(in + firstVertex, gRun.quad + firstVertex, vertices, target, top, cropH);
    if (firstVertex) memcpy(gRun.quad, in, firstVertex * sizeof(QuadVertex));
    return gRun.quad;
}

}  // namespace re3widescreen
