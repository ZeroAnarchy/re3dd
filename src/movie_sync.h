// movie_sync.h - RE3 pilot runtime (d3d9 proxy), v14 movies, 2026-10-23: the movie picture follows the movie sound.
//
// GOG dshow.dll (FFmpeg, 26.11.2025) plays the sound on its own waveOut thread and gives the game one decoded frame per
// IDirectDrawStreamSample::Update (the game calls it once per drawn frame, 0x411B84). Update waits 1/fps after the
// previous Update returned (0x100C47A0: relative, no catch-up) and the decoder thread never drops a frame (two buffers,
// one frame per Update, 0x100C35EA..0x100C3A76). Every millisecond the game spends beyond 1/30 s between two Updates -
// a slow frame, a texture load at the movie start - therefore stays as a lag of the picture behind the sound until the
// movie ends (QA 2026-10-23: seconds behind in the opening and the in-game movies, fine before the title screen).
//
// Fix: the sample's Update slot (vtable RVA 0xA92D30, the only sample class) is wrapped. Clock = playback position of
// the movie's own waveOut device (player+0x21C, 44.1 kHz); if the sound does not move for 0.4 s while the player is not
// paused (player+0x198 == 1: game window not in front), the clock runs on from its last value by wall time. The frame
// about to be shown, number `shown`, is due at shown/30 s. Ahead: the call waits for it (as dshow did) up to 40 ms,
// else the previous frame stays on screen. Behind by 2+ frames: the late frames are consumed with extra native Updates
// (each after the decoder wrote it: buffer index player+0x50284 flipped) for up to 25 ms per call. A frame the decoder
// has not written within 30 ms is not taken (native Update would show a stale buffer and reorder frames). dshow's own
// wait is skipped by zeroing its last-Update time (player+0x502A0) before each native call. All offsets are verified
// by code signature at install; any other dshow.dll keeps the native behaviour (log MOVIE_SYNC skipped).
// Per movie, SyncReport() gives the MOVIE_SYNC log line (frames, skipped, held, lateness, gaps between game frames).
//
// Stalls (QA 2026-10-23, DLL cab91b83: one 4-5 s gap between two game frames right after the start of opn and ins01,
// nothing logged in it; black picture with the sound running, then the picture raced to catch up): a watch thread sees
// the game thread stay away from Update for 0.25 s, pauses the movie sound (waveOutPause; the game thread restarts it
// on its next Update, so picture and sound go on together from where they stopped) and samples the game thread's stack
// at 0.25/0.8/1.5/2.5/4/6 s (EIP + return addresses found on the stack). The game thread logs them as MOVIE_STALL.
#pragma once
#include <windows.h>
#include <mmsystem.h>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

namespace re3movie {
namespace sync_detail {
using UpdateFn = HRESULT(STDMETHODCALLTYPE*)(void*, DWORD, HANDLE, void*, DWORD_PTR);
using PositionFn = MMRESULT(WINAPI*)(HWAVEOUT, LPMMTIME, UINT);
constexpr uint32_t kImageSize = 0x20A6000, kUpdateRva = 0xC2950, kSlotRva = 0xA92D30;
constexpr uint32_t kIndex = 0x50284, kEnd = 0x5028C, kLastQpc = 0x502A0, kWave = 0x21C, kPause = 0x198, kEvents = 0x220;
constexpr double kFps = 30.0, kRate = 44100.0;
// code bytes that hold every offset used here ("??" = any byte)
struct Signature { uint32_t rva; const char* bytes; };
constexpr Signature kSignatures[] = {
    {0xC2950, "55 8b ec 83 ec 0c 53 56 68 ?? ?? ?? ?? ff 15 ?? ?? ?? ?? 8b 75 08 8b 56 0c 85 d2 0f 84 ?? ?? ?? ?? 8b 4e 08 85 c9 0f 84 ?? ?? ?? ?? 8b 49 08 85 c9 0f 84 ?? ?? ?? ?? 8b 41 08 8b 58 08"},
    {0xC2998, "83 bb 8c 02 05 00 00"},                                   // Update: end of stream flag
    {0xC29C8, "8b 83 84 02 05 00"},                                      // Update: buffer index
    {0xC2B07, "8b 83 84 02 05 00 ff b4 83 20 02 00 00"},                 // Update: SetEvent(buffer event)
    {0xC35EA, "8b 86 84 02 05 00 8d 9e 84 02 05 00 6a ff"},              // decoder: wait for the buffer event
    {0xC2B1A, "8b cb e8 7f 1c 00 00"},                                   // Update -> wait 0x100C47A0
    {0xC47A0, "55 8b ec 83 e4 f8 83 ec 0c 53 56 8b f1 57"},
    {0xC4806, "81 c6 a0 02 05 00"},                                      // wait: last Update time
    {0xC3A66, "8b 86 84 02 05 00 8d 8e 84 02 05 00 48 83 e0 01 87 01"},  // decoder: index flip
    {0xC3646, "c7 86 98 01 00 00 01 00 00 00"},                          // decoder: paused = 1
    {0xC46D1, "8d 87 1c 02 00 00"},                                      // waveOutOpen(&player->wave, ...)
    {0xC46EB, "c7 44 24 7c 44 ac 00 00"},                                // 44100 Hz
};
struct State {
  void* sample = nullptr; uint8_t* player = nullptr; double opened = 0, start = 0, lastCall = 0, lastSound = 0, soundMoved = 0;
  unsigned calls = 0, shown = 0, skipped = 0, held = 0, starved = 0, lateEvents = 0, slowFrames = 0, restarts = 0; LONG lastIndex = 0;
  double maxLate = 0, maxGap = 0, excess = 0, endOffset = 0; bool sound = false, soundStarted = false, soundLost = false, active = false;
  unsigned stalls = 0; double stalled = 0, firstFrameWait = 0;
  char name[64] = "-";
};
using WaveFn = MMRESULT(WINAPI*)(HWAVEOUT);
using SinkFn = void (*)(const char*);
struct StallSample { LONG ms; DWORD eip, esi, edi, ecx, count, ret[20]; };
constexpr LONG kSamples = 64;  // one every 100 ms from 0.25 s
// shared with the watch thread: plain words only (the game thread may free the player while the watch reads)
struct Watch {
  volatile LONG phase = 0;       // 0 game thread outside Update, 1 inside Update, 2 movie sound paused by the watch
  volatile LONG active = 0;      // a movie is playing (first Update seen, not ended)
  volatile LONG lastCallMs = 0;  // when the game thread last left Update
  volatile LONG samples = 0;     // stack samples of the current stall
  volatile LONG noPause = 0;     // this stall is inside the audio code: the sound stays on
  HWAVEOUT volatile wave = nullptr;
  DWORD thread = 0; HANDLE handle = nullptr;
  StallSample sample[kSamples];
  ULONGLONG user0 = 0, kernel0 = 0; DWORD faults0 = 0;  // game thread CPU and process page faults at the stall start
  LONG startMs = 0, firstVideoMs = -1, flips = 0, lastIndex = -1;  // decoder: first written frame (player+0x190 != 0), index flips in 6 s
  CRITICAL_SECTION lock; bool ready = false;  // lock initialised (InstallSync)
};
inline UpdateFn original = nullptr;
inline PositionFn position = nullptr;
inline WaveFn pauseWave = nullptr, restartWave = nullptr;
inline SinkFn sink = nullptr;
inline State state;
inline Watch watch;

inline double Seconds() {
  static LARGE_INTEGER f{}; if (!f.QuadPart) QueryPerformanceFrequency(&f);
  LARGE_INTEGER c; QueryPerformanceCounter(&c); return double(c.QuadPart) / double(f.QuadPart);
}
inline LONG NowMs() { return static_cast<LONG>(static_cast<long long>(Seconds() * 1000.0)); }
inline bool Matches(const uint8_t* p, const char* pattern) {
  for (const char* c = pattern; *c;) {
    if (*c == ' ') { ++c; continue; }
    if (c[0] != '?' && strtoul(std::string(c, 2).c_str(), nullptr, 16) != *p) return false;
    ++p; c += 2;
  }
  return true;
}
// same chain as Update 0x100C2971..0x100C298A: sample -> stream -> ... -> player
inline uint8_t* Player(void* sample) {
  auto next = [](void* p) -> void* { return p ? *reinterpret_cast<void**>(static_cast<uint8_t*>(p) + 8) : nullptr; };
  return static_cast<uint8_t*>(next(next(next(next(sample)))));
}
inline LONG Field(uint8_t* p, uint32_t offset) { return *reinterpret_cast<volatile LONG*>(p + offset); }
// waveOut playback position of the movie sound in seconds, -1 without a device
inline double Sound(uint8_t* p) {
  auto wave = *reinterpret_cast<HWAVEOUT volatile*>(p + kWave);
  if (!wave || !position) return -1;
  MMTIME t{}; t.wType = TIME_SAMPLES;
  if (position(wave, &t, sizeof t) != MMSYSERR_NOERROR) return -1;
  if (t.wType == TIME_SAMPLES) return t.u.sample / kRate;
  if (t.wType == TIME_BYTES) return t.u.cb / (kRate * 4);
  if (t.wType == TIME_MS) return t.u.ms / 1000.0;
  return -1;
}
// Movie time: the sound position while it moves (or while the player is paused); stuck for 0.4 s (1.5 s before it ever
// moved: a slow device start) the time runs on from its last value by wall time until the sound moves again.
inline double Clock(State& s, double now) {
  const double sound = Sound(s.player);
  if (sound >= 0) {
    s.sound = true;
    if (sound != s.lastSound) { s.lastSound = sound; s.soundMoved = now; s.soundStarted = true; return sound; }
    if (Field(s.player, kPause) == 1) s.soundMoved = now;
    if (now - s.soundMoved < (s.soundStarted ? 0.4 : 1.5)) return sound;
    s.soundLost = true;
  }
  return s.lastSound + (now - s.soundMoved);
}
// One native Update = one frame; false (nothing taken) if the decoder has not written the next frame by `deadline`.
// The decoder flips the index after writing a buffer. Index unchanged with the buffer's event still set = still
// decoding: wait. Event reset = it took the buffer and is writing it (a few ms), or, at the movie start, it filled both
// buffers before the first Update and now waits for this one: after 25 ms the frame is taken natively.
inline bool Take(State& s, double deadline, HRESULT& hr) {
  if (!s.shown) {
    // First frame of a movie (the game has just done Seek(0)+RUN in the same call, 0x411B47): wait until the decoder
    // has written it (stream time player+0x190 set, or the buffer index flipped), at most 300 ms. Until then GetTime
    // is 0 and the game's movie state spins game ticks (2000 in 22 ms for opn), each queueing one more movie draw;
    // the first frame then ran ~2000 HD Blts (5 s still picture, QA 2026-10-23). It also shows frame 0, not a
    // blank buffer.
    const double until = Seconds() + 0.3;
    while (!*reinterpret_cast<volatile LONGLONG*>(s.player + 0x190) && !Field(s.player, kIndex) && !Field(s.player, kEnd) && Seconds() < until)
      Sleep(1);
    s.firstFrameWait = Seconds() - (until - 0.3);
  }
  if (s.shown) {
    double taken = 0;
    while (Field(s.player, kIndex) == s.lastIndex && !Field(s.player, kEnd)) {
      const double now = Seconds();
      auto event = *reinterpret_cast<HANDLE volatile*>(s.player + kEvents + 4 * (s.lastIndex & 1));
      if (WaitForSingleObject(event, 0) == WAIT_TIMEOUT) { if (!taken) taken = now; else if (now - taken > 0.025) break; }
      else taken = 0;
      if (now >= deadline) return false;
      SwitchToThread();
    }
  }
  s.lastIndex = Field(s.player, kIndex);
  *reinterpret_cast<volatile LONGLONG*>(s.player + kLastQpc) = 0;
  hr = original(s.sample, 0, nullptr, nullptr, 0);
  ++s.shown;
  return true;
}
// --- stall watch -------------------------------------------------------------------------------------------------
inline bool ReturnAddress(DWORD a) {  // points into loaded code right after a call instruction
  MEMORY_BASIC_INFORMATION m{};
  if (a < 0x10000 || (a & 0xFFF) < 8 || !VirtualQuery(reinterpret_cast<void*>(a), &m, sizeof m)) return false;
  if (m.State != MEM_COMMIT || m.Type != MEM_IMAGE || !(m.Protect & (PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY))) return false;
  const uint8_t* p = reinterpret_cast<const uint8_t*>(a);
  return p[-5] == 0xE8 || (p[-6] == 0xFF && (p[-5] & 0x38) == 0x10) || (p[-2] == 0xFF && (p[-1] & 0x38) == 0x10) ||
         (p[-3] == 0xFF && (p[-2] & 0x38) == 0x10) || (p[-7] == 0xFF && (p[-6] & 0x38) == 0x10);
}
// The game thread is suspended only for GetThreadContext and a copy of its stack: no lock is taken meanwhile.
inline void SampleGame(StallSample& out, LONG ms) {
  static DWORD stack[2048];
  out.ms = ms; out.eip = 0; out.count = 0;
  if (!watch.handle || SuspendThread(watch.handle) == static_cast<DWORD>(-1)) return;
  CONTEXT c{}; c.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER; size_t n = 0;
  if (GetThreadContext(watch.handle, &c)) {
    MEMORY_BASIC_INFORMATION m{};
    if (VirtualQuery(reinterpret_cast<void*>(static_cast<uintptr_t>(c.Esp)), &m, sizeof m) && m.State == MEM_COMMIT) {
      n = (std::min)(static_cast<size_t>(reinterpret_cast<uintptr_t>(m.BaseAddress) + m.RegionSize - c.Esp) / 4, sizeof stack / 4);
      memcpy(stack, reinterpret_cast<void*>(static_cast<uintptr_t>(c.Esp)), n * 4);
    }
  }
  ResumeThread(watch.handle);
  out.eip = c.Eip; out.esi = c.Esi; out.edi = c.Edi; out.ecx = c.Ecx;
  for (size_t i = 0; i < n && out.count < 20; ++i) if (ReturnAddress(stack[i])) out.ret[out.count++] = stack[i];
}
// The game thread waiting inside dshow/winmm/wdmaud may be waiting for the sound itself: then the sound is not paused
// (or is restarted), so the watch can never hold the game.
struct Range { uintptr_t lo = 0, hi = 0; };
inline Range audioCode[3];
inline void NoteModule(Range& r, const char* name) {
  if (auto m = reinterpret_cast<uint8_t*>(GetModuleHandleA(name))) {
    auto nt = reinterpret_cast<IMAGE_NT_HEADERS*>(m + reinterpret_cast<IMAGE_DOS_HEADER*>(m)->e_lfanew);
    r.lo = reinterpret_cast<uintptr_t>(m); r.hi = r.lo + nt->OptionalHeader.SizeOfImage;
  }
}
inline bool InAudio(const StallSample& x) {
  auto in = [](DWORD a) { for (const auto& r : audioCode) if (a >= r.lo && a < r.hi) return true; return false; };
  if (in(x.eip)) return true;
  for (DWORD j = 0; j < x.count && j < 8; ++j) if (in(x.ret[j])) return true;
  return false;
}
// watch.lock: the player is read here only while the movie is active; SyncReport deactivates under it before the close
inline void ThreadCpu(ULONGLONG& user, ULONGLONG& kernel) {
  FILETIME c, e, k, u; user = kernel = 0;
  if (watch.handle && GetThreadTimes(watch.handle, &c, &e, &k, &u)) {
    user = (ULONGLONG(u.dwHighDateTime) << 32) | u.dwLowDateTime; kernel = (ULONGLONG(k.dwHighDateTime) << 32) | k.dwLowDateTime;
  }
}
inline DWORD PageFaults() {
  struct Counters { DWORD cb, faults; SIZE_T rest[8]; } c{}; c.cb = sizeof c;
  using Fn = BOOL(WINAPI*)(HANDLE, void*, DWORD);
  static auto fn = reinterpret_cast<Fn>(GetProcAddress(GetModuleHandleA("kernel32.dll"), "K32GetProcessMemoryInfo"));
  return fn && fn(GetCurrentProcess(), &c, c.cb) ? c.faults : 0;
}
inline DWORD WINAPI WatchThread(void*) {
  for (;;) {
    Sleep(5);
    if (watch.active && NowMs() - watch.startMs < 6000) {  // decoder progress at the movie start
      EnterCriticalSection(&watch.lock);
      if (watch.active && state.player) {
        if (watch.firstVideoMs < 0 && *reinterpret_cast<volatile LONGLONG*>(state.player + 0x190)) watch.firstVideoMs = NowMs() - watch.startMs;
        const LONG index = Field(state.player, kIndex);
        if (index != watch.lastIndex) { if (watch.lastIndex >= 0) ++watch.flips; watch.lastIndex = index; }
      }
      LeaveCriticalSection(&watch.lock);
    }
    if (!watch.active || watch.phase == 1) continue;
    const LONG away = NowMs() - watch.lastCallMs;
    if (away < 250) continue;
    EnterCriticalSection(&watch.lock);
    if (watch.active && state.player && !Field(state.player, kEnd)) {  // at the end of the file the game may just stop
      const LONG k = watch.samples;
      bool audio = false;
      if (k < kSamples && away >= 250 + 100 * k) {
        if (!k) { ThreadCpu(watch.user0, watch.kernel0); watch.faults0 = PageFaults(); }
        SampleGame(watch.sample[k], away); audio = InAudio(watch.sample[k]);
        InterlockedCompareExchange(&watch.samples, k + 1, k);
      }
      if (audio) { if (InterlockedCompareExchange(&watch.phase, 0, 2) == 2 && restartWave) restartWave(watch.wave); watch.noPause = 1; }
      else if (!watch.noPause && watch.phase == 0 && watch.wave && pauseWave) {  // after the first sample (see above)
        pauseWave(watch.wave);
        if (InterlockedCompareExchange(&watch.phase, 2, 0) != 0 && restartWave) restartWave(watch.wave);
      }
    }
    LeaveCriticalSection(&watch.lock);
  }
}
inline void Describe(DWORD a, char* out, size_t size) {
  HMODULE m = nullptr; char path[MAX_PATH] = "";
  if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCSTR>(static_cast<uintptr_t>(a)), &m) && m &&
      GetModuleFileNameA(m, path, MAX_PATH)) {
    const char* base = strrchr(path, '\\');
    const bool system = _strnicmp(path, "C:\\WINDOWS\\", 11) == 0;  // the system d3d9.dll vs the pilot's d3d9.dll
    sprintf_s(out, size, "%s%s+%lx", system ? "sys:" : "", base ? base + 1 : path, static_cast<unsigned long>(a - static_cast<DWORD>(reinterpret_cast<uintptr_t>(m))));
  } else sprintf_s(out, size, "%08lx", static_cast<unsigned long>(a));
}
inline void Region(DWORD a, char* out, size_t size) {  // memory behind a register: type, protection (0x200 nocache, 0x400 write-combine)
  MEMORY_BASIC_INFORMATION m{};
  if (!a || !VirtualQuery(reinterpret_cast<void*>(static_cast<uintptr_t>(a)), &m, sizeof m)) { sprintf_s(out, size, "%08lx(-)", static_cast<unsigned long>(a)); return; }
  const char* type = m.Type == MEM_IMAGE ? "img" : m.Type == MEM_MAPPED ? "map" : m.Type == MEM_PRIVATE ? "priv" : "free";
  sprintf_s(out, size, "%08lx(%s prot=%lx alloc=%p size=%lx)", static_cast<unsigned long>(a), type, static_cast<unsigned long>(m.Protect), m.AllocationBase,
            static_cast<unsigned long>(m.RegionSize));
}
inline void LogStall(const State& s, LONG count, LONG away, bool soundPaused) {
  if (!sink) return;
  char b[200];
  ULONGLONG user = 0, kernel = 0; ThreadCpu(user, kernel);
  sprintf_s(b, "MOVIE_STALL %s frame=%u away_ms=%ld sound_paused=%d samples=%ld game_thread_cpu_ms user=%.0f kernel=%.0f page_faults=%lu",
            s.name, s.shown, away, soundPaused ? 1 : 0, count, (user - watch.user0) / 1e4, (kernel - watch.kernel0) / 1e4,
            static_cast<unsigned long>(PageFaults() - watch.faults0));
  sink(b);
  std::string hot = "MOVIE_STALL_EIP";  // every sample: ms eip<-first return address
  for (LONG i = 0; i < count && i < kSamples; ++i) {
    const StallSample& x = watch.sample[i];
    sprintf_s(b, " %ld:", x.ms); hot += b; Describe(x.eip, b, sizeof b); hot += b;
    if (x.count) { hot += "<"; Describe(x.ret[0], b, sizeof b); hot += b; }
  }
  sink(hot.c_str());
  for (LONG i : {0L, 5L, 12L, 22L, 37L, 57L, count - 1}) {  // full stacks at ~0.25/0.75/1.45/2.45/3.95/5.95 s and the last
    if (i < 0 || i >= count || (i == count - 1 && (i == 0 || i == 5 || i == 12 || i == 22 || i == 37 || i == 57))) continue;
    const StallSample& x = watch.sample[i];
    sprintf_s(b, "MOVIE_STALL_STACK %s sample_ms=%ld eip=", s.name, x.ms);
    std::string line = b;
    Describe(x.eip, b, sizeof b); line += b;
    line += " esi="; Region(x.esi, b, sizeof b); line += b;
    line += " edi="; Region(x.edi, b, sizeof b); line += b;
    sprintf_s(b, " ecx=%lx stack:", static_cast<unsigned long>(x.ecx)); line += b;
    for (DWORD j = 0; j < x.count; ++j) { Describe(x.ret[j], b, sizeof b); line += ' '; line += b; }
    sink(line.c_str());
  }
}
// --- one game frame ----------------------------------------------------------------------------------------------
inline HRESULT Step(void* sample, DWORD flags, HANDLE event, void* apc, DWORD_PTR data, bool soundPaused, LONG away) {
  uint8_t* player = Player(sample);
  if (!player || flags || event || apc || Field(player, kEnd)) {
    if (soundPaused && restartWave) restartWave(watch.wave);
    watch.active = 0; InterlockedExchange(&watch.samples, 0);
    return original(sample, flags, event, apc, data);
  }
  State& s = state;
  double now = Seconds();
  if (!s.active || s.sample != sample || s.player != player) {
    State fresh; memcpy(fresh.name, s.name, sizeof fresh.name); fresh.opened = s.active ? 0 : s.opened;
    s = fresh; s.active = true; s.sample = sample; s.player = player; s.start = s.soundMoved = now;
    if (watch.thread != GetCurrentThreadId()) {
      if (watch.handle) CloseHandle(watch.handle);
      watch.thread = GetCurrentThreadId();
      watch.handle = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, watch.thread);
    }
    watch.wave = *reinterpret_cast<HWAVEOUT volatile*>(player + kWave);
    watch.startMs = NowMs(); watch.firstVideoMs = -1; watch.flips = 0; watch.lastIndex = -1;
    InterlockedExchange(&watch.samples, 0); watch.active = 1;
  } else if (s.soundStarted) {  // played again from the start (Seek(0) + RUN on the same sample, 0x411B47)
    const double sound = Sound(player);
    if (sound >= 0 && sound + 0.25 < s.lastSound) { s.shown = 0; s.lastSound = sound; s.soundMoved = now; ++s.restarts; }
  }
  // back from a stall: the sound goes on from where the watch paused it (unless dshow paused it: window not in front)
  if (soundPaused) { if (Field(player, kPause) != 1 && restartWave) restartWave(watch.wave); s.soundMoved = now; }
  if (const LONG k = InterlockedExchange(&watch.samples, 0)) { ++s.stalls; s.stalled += away / 1000.0; LogStall(s, k, away, soundPaused); }
  watch.noPause = 0;
  if (s.calls) {
    const double gap = now - s.lastCall;
    if (gap > s.maxGap) s.maxGap = gap;
    if (gap > 1.0 / kFps) { s.excess += gap - 1.0 / kFps; if (gap > 0.040) ++s.slowFrames; }
  }
  ++s.calls;
  HRESULT hr = S_OK;
  double clock = Clock(s, now), due = s.shown / kFps;
  if (s.shown && clock < due) {  // ahead of the sound: wait like dshow did, at most 40 ms
    const double until = now + 0.040;
    while ((clock = Clock(s, now = Seconds())) < due && now < until) { if (due - clock > 0.003) Sleep(1); else SwitchToThread(); }
    if (clock < due) { ++s.held; s.lastCall = Seconds(); s.endOffset = clock - due; return S_OK; }
  }
  const double late = clock - due;
  if (late > s.maxLate) s.maxLate = late;
  if (late >= 2 / kFps) {  // behind: consume the late frames, at most 25 ms per game frame
    ++s.lateEvents;
    const double budget = Seconds() + 0.025;
    while (Clock(s, Seconds()) - s.shown / kFps >= 2 / kFps && Seconds() < budget && Take(s, budget, hr)) ++s.skipped;
  }
  if (!Take(s, Seconds() + 0.030, hr)) ++s.starved;
  s.lastCall = Seconds();
  s.endOffset = Clock(s, s.lastCall) - (s.shown - 1) / kFps;
  return hr;
}
inline HRESULT STDMETHODCALLTYPE Update(void* sample, DWORD flags, HANDLE event, void* apc, DWORD_PTR data) {
  const bool soundPaused = InterlockedExchange(&watch.phase, 1) == 2;
  const HRESULT hr = Step(sample, flags, event, apc, data, soundPaused, NowMs() - watch.lastCallMs);
  watch.lastCallMs = NowMs();
  InterlockedExchange(&watch.phase, 0);
  return hr;
}
}  // namespace sync_detail

// Wraps the sample's Update slot of the loaded GOG dshow.dll. Returns "installed" or why it was skipped.
// `log` receives the MOVIE_STALL lines (on the game thread).
inline const char* InstallSync(void (*log)(const char*) = nullptr) {
  using namespace sync_detail;
  auto base = reinterpret_cast<uint8_t*>(GetModuleHandleA("dshow.dll"));
  if (!base) return "skipped: dshow.dll not loaded";
  auto nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + reinterpret_cast<IMAGE_DOS_HEADER*>(base)->e_lfanew);
  if (nt->OptionalHeader.SizeOfImage != kImageSize) return "skipped: unknown dshow.dll (image size)";
  for (const auto& sig : kSignatures) if (!Matches(base + sig.rva, sig.bytes)) return "skipped: dshow.dll code signature mismatch";
  auto slot = reinterpret_cast<void**>(base + kSlotRva);
  if (*slot == reinterpret_cast<void*>(&Update)) return "installed (already)";
  if (*slot != base + kUpdateRva) return "skipped: sample vtable mismatch";
  HMODULE winmm = GetModuleHandleA("winmm.dll");
  if (!winmm) winmm = LoadLibraryA("winmm.dll");
  position = winmm ? reinterpret_cast<PositionFn>(GetProcAddress(winmm, "waveOutGetPosition")) : nullptr;
  pauseWave = winmm ? reinterpret_cast<WaveFn>(GetProcAddress(winmm, "waveOutPause")) : nullptr;
  restartWave = winmm ? reinterpret_cast<WaveFn>(GetProcAddress(winmm, "waveOutRestart")) : nullptr;
  if (!pauseWave || !restartWave) pauseWave = restartWave = nullptr;
  sink = log;
  original = reinterpret_cast<UpdateFn>(*slot);
  DWORD old = 0, unused = 0;
  if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &old)) return "skipped: VirtualProtect failed";
  NoteModule(audioCode[0], "dshow.dll"); NoteModule(audioCode[1], "winmm.dll"); NoteModule(audioCode[2], "wdmaud.drv");
  InitializeCriticalSection(&watch.lock); watch.ready = true;
  *slot = reinterpret_cast<void*>(&Update);
  VirtualProtect(slot, sizeof(void*), old, &unused);
  if (HANDLE t = CreateThread(nullptr, 64 * 1024, WatchThread, nullptr, 0, nullptr)) CloseHandle(t);
  return position ? "installed (clock: movie sound, stall watch)" : "installed (clock: wall time, no waveOutGetPosition)";
}
inline bool SyncInstalled() { return sync_detail::original != nullptr; }
// Movie open (before the native open): the next Update starts a new movie.
inline void NoteMovieOpen(const char* name) {
  auto& w = sync_detail::watch;
  if (w.ready) { EnterCriticalSection(&w.lock); w.active = 0; LeaveCriticalSection(&w.lock); }
  auto& s = sync_detail::state; s.active = false; s.opened = sync_detail::Seconds();
  strncpy_s(s.name, name ? name : "-", _TRUNCATE);
}
// Movie close: one log line for the movie that just ended (empty if no Update was seen).
inline std::string SyncReport() {
  auto& s = sync_detail::state;
  auto& w = sync_detail::watch;
  if (w.ready) { EnterCriticalSection(&w.lock); w.active = 0; LeaveCriticalSection(&w.lock); }
  if (!s.active) return {};
  s.active = false;
  char line[512];
  sprintf_s(line, "MOVIE_SYNC %s clock=%s frames=%u calls=%u skipped=%u held=%u starved=%u late_events=%u max_late_ms=%.0f end_offset_ms=%.0f "
            "first_update_ms=%.0f max_gap_ms=%.0f slow_frames=%u native_lag_ms=%.0f restarts=%u stalls=%u stalled_ms=%.0f "
            "decoder_first_frame_ms=%ld decoder_flips_6s=%ld first_frame_wait_ms=%.0f",
            s.name, s.soundLost ? "sound-lost" : s.sound ? "sound" : "wall", s.shown, s.calls, s.skipped, s.held, s.starved, s.lateEvents,
            s.maxLate * 1000, s.endOffset * 1000, s.opened ? (s.start - s.opened) * 1000 : -1.0, s.maxGap * 1000, s.slowFrames, s.excess * 1000,
            s.restarts, s.stalls, s.stalled * 1000, w.firstVideoMs, w.flips, s.firstFrameWait * 1000);
  return line;
}
}  // namespace re3movie
