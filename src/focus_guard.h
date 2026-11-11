#pragma once
// Round 6 (2026-11-09 night), focus agent: the game freezes with the screen kept, or closes, when its exclusive-fullscreen
// window loses the focus or a device is plugged in. Report: runtime/staging/ctl-2026-11-09/r6-focus/REPORT.ru.txt, evidence
// (logs, thread stacks, tools): qa/ctl-2026-11-09/round6/focus. Included by proxy.cpp before the device hooks (uses log,
// root, mutex, frame, gameDevice, originalWndProc, deviceObject, method<>).
//
// What goes wrong (GOG ResidentEvil3.exe + GOG ddraw.dll "dxcfg" wrapper + system d3d9), 1 and 2 reproduced in QA with the
// installed DLL, 3 and 4 read from the code:
//  1. Frozen for ever in front of everything. The game's pump (408450) blocks in GetMessage while its "minimized" flag
//     app+78 is set; only WM_SIZE sets and clears it (408810). When the focus bounces away and back quickly, Direct3D
//     minimizes the window (WM_SIZE minimized), the game's rebuild of the exclusive device puts the window back on the
//     screen, and no WM_SIZE(restored) reaches the game. Seen: window visible, not iconic, in front, app+78 = 1, pause
//     counter 0, main thread in GetMessage (408492), game task parked in its yield (4133FF).
//  2. Closed after ~10 s of black screen when any device is plugged in or drops out. The wrapper's window procedure
//     (ddraw+489F0, over the game window) answers EVERY WM_DEVICECHANGE with PostMessage(WM_ACTIVATEAPP, TRUE) and
//     Sleep(2000) (ddraw+48ABB). The game's WM_ACTIVATEAPP(TRUE) handler (404760 case 1C -> GOG cave 509551 -> 4047B5)
//     tears its DirectDraw display down and builds it again (app vt+20 / vt+1C) - meant for the return from Alt+Tab, here
//     run on a healthy device that owns the exclusive screen. Without the mod that works (2 s freeze per message). With
//     the mod it did not: the mod's textures kept the old device alive until INSIDE the next CreateDevice, i.e. after the
//     wrapper had created its new Direct3D object; the new device then came up "not reset", the wrapper reset it inside
//     its creation routine (ddraw+22645 from ddraw+20E03), and the CreateDevice after that returned D3DERR_DEVICELOST for
//     good (4 attempts x 20 retries, a display mode switch each) -> the game destroys its window (408580 -> 4085AA).
//  3. The wrapper answers a lost device inside Flip with a loop without a wait (ddraw+2B84F) and with
//     SendMessage(hwnd, 0x500, 3, ...) to the window thread (ddraw+22510): it was written for a game that renders on its
//     window thread. This game renders in task thread 1 while the window (main) thread sleeps in WaitForSingleObject
//     (4133F9) until the task yields, so a loss first seen by the game task would leave the two waiting for each other.
//     Not seen in QA: on this PC the loss was always reported on the window thread first.
//  4. An activation handled when the game is no longer in front makes the exclusive CreateDevice fail -> 408580 closes
//     the game.
//  5. The game's display teardown (40AFF0, first step of the rebuild at an activation) spins until the renderer's "frame
//     open" flag [renderer+10CC10] is clear; the frame function 40B9C0 clears it after its flip (40BC95). The window
//     thread normally runs only while the game task is parked outside a frame, but the 3DD menu (F5) keeps the game task
//     inside the flip of an open frame and yields to the window thread from there: Alt+Tab back with the menu open ->
//     the window thread spins in 40B003 for ever (seen in QA: main thread busy at 40B003 <- 4043E5 <- 4047C7, menu loop
//     parked in its yield).
// What this header does (pilot.ini focus_guard=1, default; 0 = all of it off, as before):
//  a. WM_DEVICECHANGE stops at the mod's window procedure (it sits above the wrapper's), and the wrapper's answer is
//     patched out as well (one byte, signature-checked) in case the order of the procedures changes;
//  b. D3DERR_DEVICELOST / DEVICENOTRESET of Present and TestCooperativeLevel are not shown to the wrapper, so its
//     lost-device code (3, and the Reset of 2) never runs; the device comes back the way that works - the game's own
//     display rebuild on the window thread at WM_ACTIVATEAPP(TRUE). When the device is lost while the game is in front
//     and no activation reaches the game (none is coming, or Direct3D's own hook kept it), a timer on the window thread
//     pauses the game with the game's own pause and runs that rebuild itself once Direct3D says the device can be restored;
//  c. an activation that arrives while the game is already active on a healthy device is not passed on (nothing to rebuild);
//  d. when an exclusive CreateDevice still fails, a windowed device of the same size is created instead of letting the
//     game close (4); the exclusive one is rebuilt as soon as the game is in front;
//  e. the mod's own Direct3D resources are released when the wrapper asks for its next Direct3D object, not inside the
//     next CreateDevice: the old device dies where it dies without the mod (the cause of 2);
//  f. the timer delivers the missing WM_SIZE(restored) when the game was told "minimized" but its window is on the
//     screen (1).
//  g. an activation that arrives while the game task is inside an open frame (see 5) is kept back and delivered by the
//     timer once the frame is closed.
// pilot.ini focus_trace=1 (QA): callers of every CreateDevice / Reset, the loss simulation message; focus_parts (QA): bit
// mask of a..g (1, 2, 4, 8, 16, 32, 64; default 119 = all but d, see Install).
namespace re3focus {
inline int guard = 1;
inline unsigned parts = 119;                 // QA, pilot.ini focus_parts: 1 device change, 2 loss hidden, 4 redundant activation, 8 stand-in device, 16 early release, 32 stale minimized flag, 64 activation kept out of an open frame
inline bool trace = false;
inline unsigned traceLines = 0, noteLines = 0;
inline HWND window = nullptr;                // the game window (device window of the last CreateDevice)
inline volatile LONG appActive = 1;          // last WM_ACTIVATEAPP seen by the game window
inline volatile LONG lost = 0;               // the real device reported a loss (hidden from the wrapper)
inline bool windowedFallback = false;        // the current device is the windowed stand-in of d.
inline bool wrapperPatched = false;
inline bool heldPause = false;               // the game is paused by this header (device lost while it is the active application)
inline int activationDepth = 0;              // WM_ACTIVATEAPP(TRUE) being handled further down (the display may be half rebuilt)
inline bool sizeMinimized = false;           // the last WM_SIZE the game got said "minimized" (its flag app+78 is set)
inline DWORD staleSince = 0; inline unsigned unstuck = 0;
inline DWORD lostTick = 0, frontSince = 0, lastNudge = 0, lastFallback = 0;
inline unsigned hiddenOffWindowThread = 0;   // losses reported to a thread other than the window's (the game task): where the wrapper's own answer would dead-lock
inline unsigned earlyReleases = 0, nudges = 0, hiddenPresents = 0, hiddenChecks = 0, droppedActivations = 0, droppedDeviceChanges = 0, fallbacks = 0, episodes = 0;
constexpr UINT_PTR kTimer = 0x52364647;      // "R6FG"
// QA (focus_trace=1 only): PostMessage(game window, WM_APP+0x3F6, kSimulateMark, ms) = "the device is lost for ms, then can be
// restored" although the real device is fine - exercises the pause / rebuild request path that needs a loss without deactivation
constexpr UINT kSimulateMessage = WM_APP + 0x3f6; constexpr WPARAM kSimulateMark = 0x52364c53;
inline DWORD simulateUntil = 0; inline bool simulating = false;
inline uintptr_t exeBase = 0; inline bool frameFlagKnown = false;   // g: [renderer+10CC10], renderer = [5349C4] (data: same address in RE3_MERCE.exe)
inline bool deferredActivation = false; inline unsigned deferredCount = 0; inline bool menuAsked = false;

inline std::string Ini() { return root + "\\pilot.ini"; }
inline void Note(const char* format, ...) {   // limited log lines
    if (++noteLines > (trace ? 3000u : 300u)) return;
    char text[512]; va_list args; va_start(args, format); vsnprintf_s(text, sizeof(text), _TRUNCATE, format, args); va_end(args);
    log("FOCUS_GUARD %s", text);
}
// "module+offset" list of the return addresses on the current stack that lie in the game exe or the GOG wrapper (QA)
inline std::string Callers(unsigned limit = 10) {
    std::string text;
    const auto* stack = reinterpret_cast<const uintptr_t*>(_AddressOfReturnAddress());
    MEMORY_BASIC_INFORMATION mbi{};
    if (!VirtualQuery(stack, &mbi, sizeof(mbi))) return text;
    const auto* end = reinterpret_cast<const uintptr_t*>(static_cast<const uint8_t*>(mbi.BaseAddress) + mbi.RegionSize);
    const HMODULE exe = GetModuleHandleA(nullptr), dd = GetModuleHandleA("ddraw.dll");
    unsigned listed = 0;
    for (const uintptr_t* p = stack; p < end && p < stack + 1024 && listed < limit; ++p) {
        HMODULE m = nullptr;
        if (*p < 0x10000 || !GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCSTR>(*p), &m) || !m) continue;
        if ((m != exe && m != dd) || *p - reinterpret_cast<uintptr_t>(m) < 0x1000) continue;   // not the image headers (a module handle on the stack)
        const auto* code = reinterpret_cast<const uint8_t*>(*p);
        if (!(code[-5] == 0xe8 || code[-2] == 0xff || code[-3] == 0xff || code[-6] == 0xff)) continue;   // a return address follows a call
        char item[48]; sprintf_s(item, " %s+%x", m == exe ? "exe" : "ddraw", unsigned(*p - reinterpret_cast<uintptr_t>(m)));
        text += item; ++listed;
    }
    return text;
}
// g: the game task is inside its frame function (between the start of 40B9C0 and the end of its flip)
inline bool FrameOpen() {
    if (!frameFlagKnown || !(parts & 64)) return false;
    const auto* renderer = *reinterpret_cast<uint8_t* const*>(exeBase + 0x1349c4);
    return renderer && *reinterpret_cast<const volatile LONG*>(renderer + 0x10cc10) != 0;
}
inline bool FindFrameFlag() {
    exeBase = reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr));
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(exeBase);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(exeBase + dos->e_lfanew);
    if (nt->OptionalHeader.SizeOfImage < 0x667000) return false;
    // 40AFF0 push esi / mov esi,ecx / mov eax,[esi+10CC10] / test / je / mov eax,[esi+10CC10] / test / jne (the spin);
    // 40BC95 mov dword [esi+10CC10],0 (after the flip); 4043D3 mov ecx,[5349C4] (the renderer the teardown uses)
    static const uint8_t spin[] = {0x56, 0x8b, 0xf1, 0x8b, 0x86, 0x10, 0xcc, 0x10, 0x00, 0x85, 0xc0, 0x74, 0x0a, 0x8b, 0x86, 0x10, 0xcc, 0x10, 0x00, 0x85, 0xc0, 0x75, 0xf6};
    static const uint8_t clear[] = {0xc7, 0x86, 0x10, 0xcc, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00};
    const auto* load = reinterpret_cast<const uint8_t*>(exeBase + 0x43d3);
    return memcmp(reinterpret_cast<const void*>(exeBase + 0xaff0), spin, sizeof(spin)) == 0 && memcmp(reinterpret_cast<const void*>(exeBase + 0xbc95), clear, sizeof(clear)) == 0 &&
        load[0] == 0x8b && load[1] == 0x0d && *reinterpret_cast<const uint32_t*>(load + 2) == uint32_t(exeBase + 0x1349c4);
}
inline bool GameInFront() { return window && GetForegroundWindow() == window && !IsIconic(window); }
inline void Trace(const char* what, IDirect3DDevice9* device) {
    if (!trace || ++traceLines > 400) return;
    log("FOCUS_TRACE %s device=%p thread=%lu game_in_front=%u active=%ld lost=%ld depth=%d callers:%s", what, static_cast<void*>(device), GetCurrentThreadId(),
        GameInFront() ? 1u : 0u, appActive, lost, activationDepth, Callers().c_str());
}

// ---- a. the wrapper's WM_DEVICECHANGE -> fake activation ----
// ddraw+48ABB: cmp edi,219h / jne +26h / mov eax,[ebp+8] / push 0 / push 1 / push 1Ch / push eax / call [PostMessageA] /
// push 7D0h / call [Sleep] / xor eax,eax ... ret 10h. The jne becomes a jmp: WM_DEVICECHANGE goes on to the next window
// procedure like any other message.
inline bool PatchWrapperDeviceChange() {
    auto* dd = reinterpret_cast<uint8_t*>(GetModuleHandleA("ddraw.dll"));
    if (!dd) return false;
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(dd);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(dd + dos->e_lfanew);
    if (nt->OptionalHeader.SizeOfImage < 0x48b00) return false;
    static const uint8_t head[] = {0x81, 0xff, 0x19, 0x02, 0x00, 0x00, 0x75, 0x26, 0x8b, 0x45, 0x08, 0x6a, 0x00, 0x6a, 0x01, 0x6a, 0x1c, 0x50, 0xff, 0x15};
    static const uint8_t tail[] = {0x68, 0xd0, 0x07, 0x00, 0x00, 0xff, 0x15};
    uint8_t* at = dd + 0x48abb;
    if (memcmp(at, head, sizeof(head)) != 0 || memcmp(at + 24, tail, sizeof(tail)) != 0) return false;
    DWORD protection = 0, unused = 0;
    if (!VirtualProtect(at + 6, 1, PAGE_EXECUTE_READWRITE, &protection)) return false;
    at[6] = 0xeb;
    VirtualProtect(at + 6, 1, protection, &unused); FlushInstructionCache(GetCurrentProcess(), at, 8);
    return true;
}

// ---- b. the device loss is not shown to the wrapper ----
inline void NoteLost(const char* where, HRESULT hr) {
    if (InterlockedExchange(&lost, 1) == 0) {
        lostTick = GetTickCount(); frontSince = 0; ++episodes;
        Note("device lost (%s %08lx, thread %lu, frame %u, active %ld): hidden from the GOG wrapper, the game's own rebuild brings it back", where, hr, GetCurrentThreadId(), frame, appActive);
    }
}
using TestLevelFn = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*);
inline HRESULT CallTestLevel(TestLevelFn real, IDirect3DDevice9* device) {   // the pointer may be a device the wrapper has just destroyed
    __try { return real(device); } __except (EXCEPTION_EXECUTE_HANDLER) { return E_FAIL; }
}
inline HRESULT RealTestLevel(IDirect3DDevice9* device) {
    TestLevelFn real = nullptr;
    { std::lock_guard<std::recursive_mutex> hold(mutex); real = method<TestLevelFn>(deviceObject(device), 3); }
    const HRESULT hr = CallTestLevel(real, device);
    if (simulating && SUCCEEDED(hr)) return int(GetTickCount() - simulateUntil) < 0 ? D3DERR_DEVICELOST : D3DERR_DEVICENOTRESET;   // QA
    return hr;
}
inline HRESULT STDMETHODCALLTYPE TestLevelHook(IDirect3DDevice9* device) {
    TestLevelFn real = nullptr;
    { std::lock_guard<std::recursive_mutex> hold(mutex); real = method<TestLevelFn>(deviceObject(device), 3); }
    const HRESULT hr = real(device);
    if (!guard || !(parts & 2) || (hr != D3DERR_DEVICELOST && hr != D3DERR_DEVICENOTRESET)) return hr;
    NoteLost("TestCooperativeLevel", hr); ++hiddenChecks;
    if (window && GetWindowThreadProcessId(window, nullptr) != GetCurrentThreadId()) ++hiddenOffWindowThread;
    return D3D_OK;
}
using SwapPresentFn2 = HRESULT(STDMETHODCALLTYPE*)(IDirect3DSwapChain9*, const RECT*, const RECT*, HWND, const RGNDATA*, DWORD);
inline SwapPresentFn2 nextSwapPresent = nullptr;
inline HRESULT STDMETHODCALLTYPE SwapPresentHook(IDirect3DSwapChain9* chain, const RECT* source, const RECT* target, HWND override, const RGNDATA* dirty, DWORD flags) {
    const HRESULT hr = nextSwapPresent(chain, source, target, override, dirty, flags);
    if (!guard || !(parts & 2) || hr != D3DERR_DEVICELOST) return hr;
    NoteLost("Present", hr); ++hiddenPresents;
    if (window && GetWindowThreadProcessId(window, nullptr) != GetCurrentThreadId()) ++hiddenOffWindowThread;
    return D3D_OK;
}
// hookDevice (proxy.cpp), before the hook list is taken: device vtable entries of this header
inline void HookDeviceTable(void** table) {
    if (guard) table[3] = reinterpret_cast<void*>(TestLevelHook);
}
// hookDevice, after the device is registered: the swap chain's Present (the wrapper presents through the swap chain)
inline void HookSwapChain(IDirect3DDevice9* device) {
    if (!guard) return;
    IDirect3DSwapChain9* chain = nullptr;
    if (FAILED(device->GetSwapChain(0, &chain)) || !chain) { Note("swap chain not available: Present is not guarded"); return; }
    void** table = *reinterpret_cast<void***>(chain);
    if (table[3] != reinterpret_cast<void*>(SwapPresentHook)) {
        DWORD protection = 0, unused = 0;
        if (VirtualProtect(&table[3], sizeof(void*), PAGE_READWRITE, &protection)) {
            nextSwapPresent = reinterpret_cast<SwapPresentFn2>(table[3]); table[3] = reinterpret_cast<void*>(SwapPresentHook);
            VirtualProtect(&table[3], sizeof(void*), protection, &unused);
            Note("swap chain Present guarded (table %p)", static_cast<void*>(table));
        }
    }
    chain->Release();
}

// ---- d. an exclusive CreateDevice that fails ----
// createDevice (proxy.cpp): `create` repeats the call with other present parameters. Called twice: right after the first
// attempt (early: only within 5 s of the last stand-in, so a rebuild that cannot get the screen does not go through the
// proxy's 20 retries = 20 display mode switches per attempt again) and after the proxy's own retries.
template <class Create> inline HRESULT CreateFallback(HRESULT hr, const D3DPRESENT_PARAMETERS* wanted, bool early, Create create) {
    if (!guard || !(parts & 8) || SUCCEEDED(hr) || !wanted || wanted->Windowed) return hr;
    if (early && (hr != D3DERR_DEVICELOST || !lastFallback || GetTickCount() - lastFallback > 5000)) return hr;
    D3DPRESENT_PARAMETERS windowed = *wanted;
    windowed.Windowed = TRUE; windowed.FullScreen_RefreshRateInHz = 0;
    const HRESULT second = create(&windowed);
    ++fallbacks;
    Note("exclusive CreateDevice failed %08lx (game in front %u): windowed device %ux%u instead -> %08lx%s", hr, GameInFront() ? 1u : 0u,
        windowed.BackBufferWidth, windowed.BackBufferHeight, second, SUCCEEDED(second) ? ", the exclusive one is rebuilt when the game is in front" : "");
    if (FAILED(second)) return hr;
    windowedFallback = true; frontSince = 0; lastFallback = GetTickCount(); if (!lastFallback) lastFallback = 1;
    return second;
}

// ---- e. the old device goes when the wrapper lets it go ----
// ProxyDirect3DCreate9 (proxy.cpp). The wrapper rebuilds like this: release its device and its Direct3D object, Direct3DCreate9,
// CreateDevice. The mod's textures / buffers keep the old device (and through it the old Direct3D object) alive, and the
// proxy used to drop them only inside the next CreateDevice - after the new Direct3D object existed. With an old device that
// is lost this does no harm; with a healthy one that owns the exclusive screen the new device comes up lost, the wrapper
// resets it, and the CreateDevice after that never succeeds again (QA: base DLL on the title screen, no model loaded).
// Now everything of the mod is released when the wrapper asks for its next Direct3D object, so the old device dies before it.
inline bool ReleaseAtDirect3DCreate(IDirect3DDevice9* device) {
    if (!guard || !(parts & 16) || !device) return false;
    ++earlyReleases; Trace("Direct3DCreate9 while a device of the mod is registered: its resources are released first", device);
    return true;
}

// The game's own window procedure (the class procedure, 410670): messages made here go to it directly. Direct3D's hook
// below ours answers some activations itself and does not pass them on (QA: an activation after a quick focus bounce
// never reached the game, which then ran on with a lost device), so a message meant for the game must not depend on it.
inline WNDPROC gameProc = nullptr;

// ---- call points in proxy.cpp ----
inline void BeforeReset(IDirect3DDevice9* device) { Trace("reset", device); }
inline void AfterReset(IDirect3DDevice9* device, HRESULT hr) {
    (void)device;
    if (SUCCEEDED(hr) && InterlockedExchange(&lost, 0)) Note("device reset: no longer lost");
}
inline void BeforeCreate(IDirect3DDevice9* old) { Trace("create, old", old); windowedFallback = false; }
inline void AfterCreate(IDirect3DDevice9* created, HRESULT hr, HWND deviceWindow) {
    if (trace && traceLines <= 400) log("FOCUS_TRACE create result=%08lx device=%p windowed_stand_in=%u", hr, static_cast<void*>(created), windowedFallback ? 1u : 0u);
    if (FAILED(hr)) return;
    if (deviceWindow && deviceWindow != window) {
        window = deviceWindow; if (guard) SetTimer(window, kTimer, 250, nullptr);
        // the class procedure = the game's own (RegisterClassA at 4105D9 with 410670); taken only when it lies in the exe
        const auto classProc = reinterpret_cast<WNDPROC>(GetClassLongPtrA(window, GCLP_WNDPROC)); HMODULE owner = nullptr;
        if (classProc && GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCSTR>(classProc), &owner) && owner == GetModuleHandleA(nullptr)) gameProc = classProc;
        Note("game window %p, its own procedure %p%s", static_cast<void*>(window), static_cast<void*>(gameProc), gameProc ? "" : " (class procedure not in the exe: messages for the game go down the whole chain)");
    }
    simulating = false;
    if (InterlockedExchange(&lost, 0)) Note("device rebuilt %lu ms after the loss (hidden so far: %u presents, %u level checks, %u of them outside the window thread)", GetTickCount() - lostTick, hiddenPresents, hiddenChecks, hiddenOffWindowThread);
    if (!windowedFallback) nudges = 0;
}
inline LRESULT ToGame(UINT msg, WPARAM wp, LPARAM lp) { return CallWindowProcA(gameProc ? gameProc : originalWndProc, window, msg, wp, lp); }
// The game's own pause (app vt+10 / vt+14: pause counter +7C, all sound channels) through the two messages whose handlers
// are exactly those calls (404760: WM_ENTERMENULOOP -> 404B05, WM_EXITMENULOOP -> 404AFA); window thread only.
inline void HoldPause(bool hold, const char* why) {
    if (hold == heldPause || !window || !originalWndProc) return;
    heldPause = hold;
    ToGame(hold ? WM_ENTERMENULOOP : WM_EXITMENULOOP, 0, 0);
    Note("game %s (%s)", hold ? "paused" : "resumed", why);
}
// The game's display rebuild (its WM_ACTIVATEAPP(TRUE) handler: 4047B5 pause, teardown, SetCooperativeLevel /
// SetDisplayMode / surfaces, resume), run by this header; window thread, game task parked outside a frame.
inline void RebuildDisplay(const char* why) {
    ++nudges; lastNudge = GetTickCount(); if (!lastNudge) lastNudge = 1;
    Note("%s: the game's display rebuild is run (#%u)", why, nudges);
    InterlockedExchange(&appActive, 1);
    ++activationDepth;
    ToGame(WM_ACTIVATEAPP, TRUE, 0);
    --activationDepth;
    if (heldPause && !lost) HoldPause(false, "display rebuilt");
}
// window thread, every 250 ms while it pumps (= while the game task is parked)
inline void Tick() {
    if (!guard || !window || activationDepth) return;
    const DWORD now = GetTickCount();
    // f. The game blocks in GetMessage while its "minimized" flag (app+78, set and cleared only by WM_SIZE in 408810) is set.
    // After a quick focus bounce Direct3D has minimized the window (WM_SIZE minimized) and the rebuild of the exclusive device
    // has put it back on the screen without any WM_SIZE reaching the game: the flag stays set for ever and the game never
    // runs again, in front of everything. The missing message is delivered here.
    if ((parts & 32) && sizeMinimized && originalWndProc && IsWindowVisible(window) && !IsIconic(window)) {
        if (!staleSince) staleSince = now ? now : 1;
        else if (now - staleSince >= 300) {
            RECT client{}; GetClientRect(window, &client);
            sizeMinimized = false; staleSince = 0; ++unstuck;
            Note("the game was told its window is minimized, but the window is on the screen again and no WM_SIZE followed: WM_SIZE(restored, %ldx%ld) delivered, #%u", client.right, client.bottom, unstuck);
            ToGame(WM_SIZE, SIZE_RESTORED, MAKELPARAM(client.right, client.bottom));
        }
    } else staleSince = 0;
    if (!lost && heldPause) HoldPause(false, "device is back");
    // b / d / g. A lost device, a stand-in device or a kept-back activation while the game is in front: nothing else will
    // make the game rebuild its display (no activation is coming, or Direct3D's hook kept it for itself).
    if (!lost && !windowedFallback && !deferredActivation) { frontSince = 0; return; }
    if (!gameDevice || !originalWndProc) return;
    if (!GameInFront()) { frontSince = 0; return; }   // away: the activation on the way back rebuilds
    if (!frontSince) frontSince = now ? now : 1;
    if (now - frontSince < 300) return;
    if (FrameOpen()) {   // the 3DD menu holds the frame open: nothing can be rebuilt (5); it closes itself on focus loss, else ask it once
        if (!menuAsked && now - frontSince >= 1000) { menuAsked = true; Note("the 3DD menu holds the frame open while the display has to be rebuilt: the menu is asked to close"); std::lock_guard<std::recursive_mutex> hold(mutex); re3menu::ReleaseDevice(); }
        return;
    }
    menuAsked = false;
    const HRESULT level = RealTestLevel(gameDevice);
    if (deferredActivation) {   // g: the frame is closed now
        deferredActivation = false;
        if (FAILED(level) || windowedFallback) { RebuildDisplay("the frame is closed, the kept activation is carried out"); return; }
        Note("the frame is closed and the device is fine: the kept activation needs no rebuild");
    }
    if (lost) {
        if (SUCCEEDED(level)) { InterlockedExchange(&lost, 0); Note("device is healthy again without a rebuild"); if (heldPause) HoldPause(false, "device is back"); return; }
        if (!heldPause) HoldPause(true, "device lost while the game is in front");
        // DEVICELOST = Direct3D still counts the game as inactive (QA: after a quick focus bounce it stayed so with the game
        // in front): wait a little, then run the rebuild anyway - a new exclusive device can be created once the game is in front
        if (level != D3DERR_DEVICENOTRESET && now - frontSince < 2000) return;
    } else if (!windowedFallback) return;
    if (lastNudge && now - lastNudge < 3000) return;
    if (nudges >= 8) { if (nudges == 8) { ++nudges; Note("the display did not come back after 8 rebuilds; waiting for the next activation"); if (heldPause) HoldPause(false, "giving up"); } return; }
    RebuildDisplay(lost ? "device lost and the game is in front, no activation came" : "windowed stand-in device and the game is in front");
}
// pilotWndProc: true = handled here (result set)
bool OnWindowMessage(HWND w, UINT msg, WPARAM wp, LPARAM lp, LRESULT& result) {
    result = 0;
    switch (msg) {
    case WM_TIMER:
        if (wp != kTimer) return false;
        Tick(); return true;
    case WM_SIZE:
        if (w == window) { sizeMinimized = wp == SIZE_MINIMIZED || wp == SIZE_MAXHIDE; staleSince = 0; }   // 408810: these two set app+78, any other clears it
        return false;
    case kSimulateMessage:
        if (!trace || wp != kSimulateMark || !guard) return false;
        simulating = true; simulateUntil = GetTickCount() + DWORD(lp);
        NoteLost("QA simulation", D3DERR_DEVICELOST);
        return true;
    case WM_DEVICECHANGE:
        // nothing below needs it (game: DefWindowProc; GOG dinput.dll has its own notification window); the GOG wrapper's
        // procedure would turn it into an activation
        if (++traceLines <= 400) log("WINDOW WM_DEVICECHANGE event=%04x%s", unsigned(wp), guard && (parts & 1) ? " (kept from the GOG wrapper)" : "");
        if (!guard || !(parts & 1)) return false;
        ++droppedDeviceChanges; result = DefWindowProcA(w, msg, wp, lp);
        return true;
    case WM_ACTIVATEAPP: {
        if (!guard || !originalWndProc) return false;
        if (!wp) { InterlockedExchange(&appActive, 0); frontSince = 0; return false; }
        const bool wasActive = InterlockedExchange(&appActive, 1) == 1;
        if (FrameOpen()) {   // g: the game's rebuild would spin in 40AFF0 until the frame is closed, and the frame is closed by the parked game task
            if (!deferredActivation) { ++deferredCount; Note("WM_ACTIVATEAPP(TRUE) while the game task is inside an open frame (3DD menu): kept back until the frame is closed, #%u", deferredCount); }
            deferredActivation = true; frontSince = 0;
            return true;
        }
        if ((parts & 4) && wasActive && !activationDepth && w == window && gameDevice && !lost && !windowedFallback && !IsIconic(w)) {
            // already the active application and nothing is being rebuilt: is there anything to rebuild?
            if (SUCCEEDED(RealTestLevel(gameDevice))) {
                ++droppedActivations;
                Note("WM_ACTIVATEAPP(TRUE, %lu) while already active on a healthy device: not passed on (no display rebuild), #%u", static_cast<unsigned long>(lp), droppedActivations);
                return true;
            }
        }
        // the rest of the chain (Direct3D's hook, the wrapper, the game: display rebuild) with the depth known
        ++activationDepth;
        result = CallWindowProcA(originalWndProc, w, msg, wp, lp);
        --activationDepth;
        if (!activationDepth && heldPause && !lost) HoldPause(false, "display rebuilt");
        return true;
    }
    default: return false;
    }
}
inline void Install() {
    guard = int(GetPrivateProfileIntA("pilot", "focus_guard", 1, Ini().c_str()));
    trace = GetPrivateProfileIntA("pilot", "focus_trace", 0, Ini().c_str()) != 0;
    parts = GetPrivateProfileIntA("pilot", "focus_parts", 119, Ini().c_str());   // merge (orchestrator): d, the windowed stand-in, is off by default - it never triggered in the QA of the full build, so it is untested there; focus_parts=127 switches it on
    if (guard && (parts & 1)) wrapperPatched = PatchWrapperDeviceChange();
    frameFlagKnown = guard && FindFrameFlag();
    log("FOCUS_GUARD %s%s%s", guard ? "on" : "off (pilot.ini focus_guard=0)",
        guard ? (wrapperPatched ? ": GOG wrapper WM_DEVICECHANGE -> activation + Sleep(2000) patched out (ddraw+48AC1)" : ": GOG wrapper WM_DEVICECHANGE code left alone") : "",
        trace ? ", trace on" : "");
    if (guard && !frameFlagKnown) log("FOCUS_GUARD frame flag code (40AFF0 / 40BC95 / 4043D3) not recognised: activations are not kept out of an open frame");
    if (guard && parts != 119) log("FOCUS_GUARD parts=%u (QA: 1 device change, 2 loss hidden, 4 redundant activation, 8 stand-in device, 16 early release, 32 stale minimized flag, 64 activation kept out of an open frame)", parts);
}
}  // namespace re3focus
