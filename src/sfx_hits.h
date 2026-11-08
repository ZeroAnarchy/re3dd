#pragma once
// ---------------------------------------------------------------- HD flesh-hit sounds (fx round 2026-11-08, agent hit-sound)
// Included by proxy.cpp before model_integration.h (log, root, gameBase, frame, mutex, desiredEnabled, re3bridge, re3model).
//
// User request 08.11: good sounds of bullets hitting flesh, to go with the HD blood of 07.11.
// What the game has (static + trace, see runtime\staging\fx-2026-11-08\hit-sound\REPORT.ru.txt):
//  - sound = the PSX SPU emulated over DirectSound: dsound.dll DirectSoundCreate (import 50A024), cooperative level 2 on the
//    game window, 100 static secondary buffers created with flags 0xE2 (STATIC|CTRLFREQUENCY|CTRLPAN|CTRLVOLUME, no
//    GLOBALFOCUS: muted while the window is not in front). The sound manager [5349CC] (vtable 50A968) keeps 24 voices
//    and the effects volume at +0x250 (bio3.ini [Sound] SEvol, 0..65535); a voice's DirectSound volume is
//    1000*log2(SEvol*voice/65535*0.010336/128) hundredths of a dB (40A340/40A930).
//  - effects are requested with 427B20(id, position, a, b): a 0x20-byte entry queued at [A59D94] (A59A94..A59D94), played
//    by the sound update 427760 (call 4239E6) once a frame. id = type<<16 | bank slot<<8 | index (0x10100 = the pistol shot).
//  - the enemy shot test 452DB0 plays nothing; whatever the enemy does after a hit (damage routine) is traced with
//    pilot.ini hit_sound_trace=1 (SE_REQ / SE_BANK lines).
// What this adds: its own DirectSound object on the same window with the same buffer flags (same focus rules as the game's
// effects), a worker thread that loads model-pack\hit-sounds (hits.txt + mono PCM WAVs) once and starts the voices, and a
// request queue the game thread fills from the shot-test wrapper (model_integration.h shotTestHook / corpseShotRay):
// no allocation, no file or DirectSound call on the game thread.
//  - kind: shotgun (04 10 13) / magnum (05, Eagle 0D) / heavy target (Nemesis 34-3B, Grave Digger 30 33, worm 32) / flesh;
//    a corpse hit (MODEL_CORPSE_HIT) = corpse. Knife 01, launchers 06-0A 0C 14, helicopters 3E 3F: no sound.
//  - one sound per enemy per shot test (a shotgun blast = one sound per body, max 2), automatic weapons at most one per
//    enemy per 55 ms (-1 dB), variants without repeats, pitch +-hit_sound_pitch %, pan from the hit point in camera space,
//    attenuation from Jill's distance (0 dB to 4 m, -3 dB per doubling, at most -9 dB), the game's SEvol, hit_sound_volume, max hit_sound_voices voices (oldest stolen).
//  - pilot.ini hit_sounds=0 off (default 1); F8 original mode off; hit_sound_delay_ms (default 45: measured in the loopback
//    recording of the QA run, the game's own shot sound starts 80-110 ms after the hit test and ours 83 ms after Play;
//    +45 ms puts the hit ~1 game tick after the shot onset, as the HD spray follows the muzzle flash by one tick).
//  - debug (debug_warp=1): pilot.command hit-sound-test plays every sound of every kind in turn (HIT_SOUND_TEST lines).
#include <dsound.h>
namespace sfxhits {
// round 3 (08.11, user: "shotgun and rifle hits not so good, pistol good"): kind auto = automatic fire (M4A1 0E/0F, gatling 0B,
// Glock 18C AUTO), short tight wet hits that stay separate in a 15/s burst; fx3 2026-11-08 hit-sound
enum Kind : std::uint8_t { Flesh = 0, Heavy, Shotgun, Magnum, Corpse, Auto, kKinds };
static const char* const kKindName[kKinds] = {"flesh", "heavy", "shotgun", "magnum", "corpse", "auto"};
constexpr unsigned kMaxPerKind = 12, kDuplicates = 3, kQueue = 48;

struct Config { bool enabled = true, trace = false; int volumePct = 100, delayMs = 45, voices = 8, pitchPct = 6; };
static Config config;
struct Request { std::uint8_t kind = 0, variant = 0; float gainDb = 0, pan = 0, pitch = 1; double due = 0; bool test = false; };
struct Sample {
    std::string file; float gainDb = 0; std::vector<std::int16_t> pcm; WAVEFORMATEX format{};
    IDirectSoundBuffer* voice[kDuplicates]{}; double busyUntil[kDuplicates]{}; double seconds = 0;
};
static Sample samples[kKinds][kMaxPerKind];
static unsigned sampleCount[kKinds]{};
static IDirectSound* device = nullptr;
static HANDLE wake = nullptr;
static SRWLOCK queueLock = SRWLOCK_INIT;
static Request queue[kQueue];
static unsigned queued = 0;
static volatile LONG ready = 0, testRequest = 0, installedFlag = 0;
static volatile LONG played = 0, dropped = 0, stolen = 0, failed = 0, lateMax = 0;
static double qpcScale = 0;
inline double nowMs() { LARGE_INTEGER t; QueryPerformanceCounter(&t); return double(t.QuadPart) * qpcScale; }

// ---------------------------------------------------------------- game state (game thread)
// effects volume of the running game: sound manager [5349CC] +0x250 (default 0xFFFF, bio3.ini SEvol); the game's own curve is
// 10 dB per halving of the voice level, capped at full: our sounds follow the slider below the configured default 49150
inline float seVolumeDb(int* raw = nullptr) {
    const auto manager = re3bridge::detail::Read<const std::uint8_t*>(reinterpret_cast<const void*>(gameBase + 0x1349cc));
    if (!re3bridge::detail::Accessible(manager, 0x254)) { if (raw) *raw = -1; return 0.f; }
    const int v = re3bridge::detail::Read<std::int32_t>(manager, 0x250); if (raw) *raw = v;
    if (v <= 0) return -100.f;
    return (std::min)(0.f, 10.f * std::log2(float(v) / 49150.f));
}
inline re3model::Vec3 playerPosition() {
    const auto* p = reinterpret_cast<const std::uint8_t*>(gameBase + 0x65cbf4);
    return {float(re3bridge::detail::Read<std::int32_t>(p, 0x34)), float(re3bridge::detail::Read<std::int32_t>(p, 0x38)), float(re3bridge::detail::Read<std::int32_t>(p, 0x3c))};
}
// pan -1..1 from the camera (fixed RE cameras: screen left/right), attenuation from Jill's distance to the hit
inline void placeSound(re3model::Vec3 at, float& pan, float& distanceDb, float& distance) {
    const auto camera = re3model::FromNativeMatrix(re3bridge::detail::Read<re3bridge::Matrix32>(reinterpret_cast<const void*>(gameBase + 0x11c398)));
    const auto c = re3model::TransformPoint(camera, at);
    pan = c.z > 200.f ? (std::max)(-1.f, (std::min)(1.f, c.x / (c.z * 0.9f))) : 0.f;
    const auto p = playerPosition(); const float dx = at.x - p.x, dz = at.z - p.z;
    distance = std::sqrt(dx * dx + dz * dz);
    distanceDb = distance <= 4000.f ? 0.f : (std::max)(-9.f, -3.f * std::log2(distance / 4000.f));
}
inline bool heavyTarget(unsigned model) { return (model >= 0x34 && model <= 0x3b) || model == 0x30 || model == 0x33 || model == 0x32; }
inline bool noFleshTarget(unsigned model) { return model == 0x3e || model == 0x3f; }   // helicopters (metal)
inline bool automatic(int weapon) { return weapon == 0x0b || weapon == 0x0e || weapon == 0x0f || (weapon == 0x03 && glockEquippedItem() == 0x54); }   // Glock AUTO = item 54 on M92F 03
// -1 = no flesh sound for this weapon (knife, grenade/rocket/mine launchers)
inline int weaponKind(int weapon) {
    switch (weapon) {
    case 0x01: case 0x06: case 0x07: case 0x08: case 0x09: case 0x0a: case 0x0c: case 0x14: return -1;
    case 0x04: case 0x10: case 0x13: return Shotgun;
    case 0x05: case 0x0d: return Magnum;
    default: return Flesh;
    }
}
static std::uint32_t seed = 0x5f3759dfu;
inline float rnd() { seed = seed * 1664525u + 1013904223u; return float((seed >> 8) & 0xffff) / 65535.f; }
static std::uint8_t lastVariant[kKinds][2] = {{0xff, 0xff}, {0xff, 0xff}, {0xff, 0xff}, {0xff, 0xff}, {0xff, 0xff}, {0xff, 0xff}};
inline std::uint8_t pickVariant(unsigned kind) {
    const unsigned n = sampleCount[kind]; if (n <= 1) return 0;
    std::uint8_t v = 0;
    for (int attempt = 0; attempt < 16; ++attempt) {
        v = std::uint8_t(unsigned(rnd() * n) % n);
        if (v != lastVariant[kind][0] && (n < 4 || v != lastVariant[kind][1])) break;
    }
    lastVariant[kind][1] = lastVariant[kind][0]; lastVariant[kind][0] = v; return v;
}
inline bool push(const Request& r) {
    AcquireSRWLockExclusive(&queueLock);
    const bool ok = queued < kQueue; if (ok) queue[queued++] = r;
    ReleaseSRWLockExclusive(&queueLock);
    if (ok) SetEvent(wake); else InterlockedIncrement(&dropped);
    return ok;
}

// ---------------------------------------------------------------- hits from the shot-test wrapper (game thread)
struct Hit { const std::uint8_t* actor; int lifeBefore, lifeAfter; };
static Hit pending[8]; static unsigned pendingCount = 0;
struct Recent { const void* actor; double at; };
static Recent recent[16]; static unsigned recentNext = 0;
static unsigned logged = 0; static std::uint64_t requested = 0, skippedRate = 0, skippedOff = 0;
inline bool soundOn() { return installedFlag && ready && config.enabled && desiredEnabled; }
// one enemy hit by the shot test that just ran (called for every hit, before the knife filter)
inline void onHit(int, const std::uint8_t* actor, int lifeBefore) {
    if (pendingCount < 8 && re3bridge::detail::Accessible(actor, 0xd0)) pending[pendingCount++] = {actor, lifeBefore, re3bridge::detail::Read<std::int16_t>(actor, 0xcc)};
}
inline void request(unsigned kind, int weapon, const std::uint8_t* actor, re3model::Vec3 at, float extraDb, const char* what, int lifeBefore, int lifeAfter) {
    if (!sampleCount[kind]) return;
    const double now = nowMs();
    // round 3: every hit of a burst gets its sound (at most one per enemy per 25 ms = per game tick; round 1-2: 55 ms, which
    // dropped every other gatling hit); a hit within 150 ms of the previous one on the same enemy is 1.5 dB lower - a steady
    // burst level instead of the first hit's (no build-up: the auto sounds are 0.12-0.25 s, at most ~3 overlap at 15/s)
    double since = 1e9;
    if (automatic(weapon) && actor) {
        for (const auto& r : recent) if (r.actor == actor) since = (std::min)(since, now - r.at);
        if (since < 25.0) { ++skippedRate; return; }
        recent[recentNext++ & 15] = {actor, now};
    }
    const float burstDb = (kind == Auto && since < 150.0) ? -1.5f : 0.f;
    // round 3: the shotguns run the shot test on 3 consecutive ticks per blast (QA run hs3-new: a kill in tick 1, then two
    // MODEL_CORPSE_HITs on the same fresh body in ticks 2-3 = three sounds in 66 ms). One blast = one sound per body: the same
    // body again within 220 ms is skipped; other bodies of the same blast (150 ms) at -4 dB, at most 2 sounds per blast.
    static double blastAt[4] = {-1e9, -1e9, -1e9, -1e9}; static const void* blastActor[4] = {}; static unsigned blastNext = 0;
    if (weaponKind(weapon) == Shotgun) {
        unsigned inBlast = 0;
        for (unsigned k = 0; k < 4; ++k) if (now - blastAt[k] < 220.0) {
            if (blastActor[k] == actor) { ++skippedRate; static unsigned skipLogs = 0; if (skipLogs < 24) { ++skipLogs; log("HIT_SOUND_SKIP %s weapon=%02x actor=%p: same body in the same shotgun blast (%.0f ms)", what, unsigned(weapon), actor, now - blastAt[k]); } return; }
            if (now - blastAt[k] < 150.0) ++inBlast;
        }
        if (inBlast >= 2) { ++skippedRate; return; }
        if (inBlast) extraDb -= 4.f;
        blastAt[blastNext & 3] = now; blastActor[blastNext & 3] = actor; ++blastNext;
    }
    float pan = 0, distanceDb = 0, distance = 0; placeSound(at, pan, distanceDb, distance);
    int seRaw = 0; const float seDb = seVolumeDb(&seRaw);
    const float userDb = config.volumePct <= 0 ? -100.f : 20.f * std::log10(float(config.volumePct) / 100.f);
    const float weaponDb = (automatic(weapon) && kind != Auto ? -1.f : 0.f) + burstDb;   // the auto kind has its own level in hits.txt
    Request r; r.kind = std::uint8_t(kind); r.variant = pickVariant(kind);
    r.gainDb = samples[kind][r.variant].gainDb + seDb + userDb + distanceDb + weaponDb + extraDb;
    r.pan = pan * 0.8f;
    const float spread = float(config.pitchPct) / 100.f;
    r.pitch = 1.f + (rnd() * 2.f - 1.f) * spread;
    if (actor && kind != Corpse && heavyTarget(re3bridge::detail::Read<std::uint8_t>(actor, 0x4a)) && (kind == Shotgun || kind == Magnum || kind == Auto)) r.pitch *= kind == Shotgun ? 0.9f : kind == Auto ? 0.88f : 0.93f;
    r.due = now + config.delayMs;
    if (!push(r)) return;
    ++requested;
    if (logged < 300 || requested % 50 == 0) {
        ++logged;
        log("HIT_SOUND kind=%s v=%02u file=%s weapon=%02x target=%02x actor=%p %s life=%d->%d dist=%.0f vol=%.1fdB (file %.1f se %.1f/%d dist %.1f weapon %.1f extra %.1f user %.1f) pan=%.2f pitch=%.3f delay=%dms t=%.1f build=%u n=%llu",
            kKindName[kind], unsigned(r.variant) + 1, samples[kind][r.variant].file.c_str(), unsigned(weapon), actor ? unsigned(re3bridge::detail::Read<std::uint8_t>(actor, 0x4a)) : 0u, actor, what,
            lifeBefore, lifeAfter, distance, r.gainDb, samples[kind][r.variant].gainDb, seDb, seRaw, distanceDb, weaponDb, extraDb, userDb, r.pan, r.pitch, config.delayMs, r.due, re3bridge::BuildSerial(), (unsigned long long)requested);
    }
    if (requested % 64 == 0) log("HIT_SOUND_STATS requested=%llu played=%ld dropped=%ld stolen=%ld failed=%ld skipped_rate=%llu skipped_off=%llu late_max_ms=%ld",
        (unsigned long long)requested, played, dropped, stolen, failed, (unsigned long long)skippedRate, (unsigned long long)skippedOff, lateMax);
}
// the shot test returned: every enemy it hit gets its sound (a shotgun blast one per body, the nearest at full level)
inline void onShotDone(int weapon, const re3bridge::Matrix32* hand) {
    const unsigned count = pendingCount; pendingCount = 0;
    if (!count) return;
    if (!soundOn()) {
        if (++skippedOff <= 12) log("HIT_SOUND_OFF weapon=%02x hits=%u (%s)", unsigned(weapon), count, !config.enabled ? "hit_sounds=0" : !installedFlag ? "not installed" : !ready ? "sounds not loaded yet" : "F8 original mode");
        return;
    }
    const int kindOfWeapon = weaponKind(weapon); if (kindOfWeapon < 0) return;
    re3model::Vec3 origin{}, dir{0, 0, 1}; bool ray = false;
    if (hand && re3bridge::detail::Accessible(hand, sizeof(*hand))) {
        const auto h = re3model::FromNativeMatrix(*hand); origin = {h.m[3], h.m[7], h.m[11]}; dir = re3model::Normalize({h.m[1], h.m[5], h.m[9]}); ray = true;
    }
    const auto player = playerPosition();
    // nearest first
    Hit hits[8]; float order[8]; re3model::Vec3 at[8];
    for (unsigned i = 0; i < count; ++i) {
        const auto* a = pending[i].actor;
        const re3model::Vec3 p{float(re3bridge::detail::Read<std::int32_t>(a, 0x34)), float(re3bridge::detail::Read<std::int32_t>(a, 0x38)), float(re3bridge::detail::Read<std::int32_t>(a, 0x3c))};
        re3model::Vec3 q = p;
        if (ray) { const float t = (std::max)(0.f, re3model::Dot(p - origin, dir)); q = origin + dir * t; }   // on the barrel line, at the enemy
        hits[i] = pending[i]; at[i] = q; const float dx = p.x - player.x, dz = p.z - player.z; order[i] = dx * dx + dz * dz;
    }
    for (unsigned i = 1; i < count; ++i) for (unsigned j = i; j > 0 && order[j] < order[j - 1]; --j) { std::swap(order[j], order[j - 1]); std::swap(hits[j], hits[j - 1]); std::swap(at[j], at[j - 1]); }
    const unsigned limit = kindOfWeapon == Shotgun ? 2u : 3u;
    for (unsigned i = 0; i < count && i < limit; ++i) {
        const unsigned model = re3bridge::detail::Read<std::uint8_t>(hits[i].actor, 0x4a);
        if (noFleshTarget(model)) continue;
        unsigned kind = unsigned(kindOfWeapon);
        if (kind == Flesh && automatic(weapon) && sampleCount[Auto]) kind = Auto;   // a heavy target too: short hits, lower (request)
        if (kind == Flesh && heavyTarget(model)) kind = Heavy;
        request(kind, weapon, hits[i].actor, at[i], i ? -4.f : 0.f, "hit", hits[i].lifeBefore, hits[i].lifeAfter);
    }
}
// a shot that hit no live enemy but a body lying in the room (model_integration.h corpseShotRay): a duller sound
inline void onCorpseHit(int weapon, const void* actor, re3model::Vec3 origin, re3model::Vec3 dir, float distance) {
    if (!soundOn()) { ++skippedOff; return; }
    if (weaponKind(weapon) < 0) return;
    const auto* a = static_cast<const std::uint8_t*>(actor);
    request(Corpse, weapon, a, origin + dir * distance, weaponKind(weapon) == Shotgun ? 2.f : weaponKind(weapon) == Magnum ? 1.f : 0.f, "corpse",
        int(re3bridge::detail::Read<std::int16_t>(a, 0xcc)), int(re3bridge::detail::Read<std::int16_t>(a, 0xcc)));
}
inline void requestTest() { InterlockedExchange(&testRequest, 1); if (wake) SetEvent(wake); }

// ---------------------------------------------------------------- worker thread: load once, start voices
inline bool readWav(const std::string& path, Sample& s) {
    std::ifstream in(path, std::ios::binary); if (!in) return false;
    std::vector<char> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (bytes.size() < 44 || std::memcmp(bytes.data(), "RIFF", 4) || std::memcmp(bytes.data() + 8, "WAVE", 4)) return false;
    unsigned channels = 0, rate = 0, bits = 0, format = 0; const char* data = nullptr; std::size_t dataBytes = 0;
    for (std::size_t at = 12; at + 8 <= bytes.size();) {
        std::uint32_t size; std::memcpy(&size, bytes.data() + at + 4, 4);
        if (at + 8 + size > bytes.size()) size = std::uint32_t(bytes.size() - at - 8);
        if (!std::memcmp(bytes.data() + at, "fmt ", 4) && size >= 16) {
            std::uint16_t f, ch, b; std::uint32_t r; std::memcpy(&f, bytes.data() + at + 8, 2); std::memcpy(&ch, bytes.data() + at + 10, 2);
            std::memcpy(&r, bytes.data() + at + 12, 4); std::memcpy(&b, bytes.data() + at + 22, 2); format = f; channels = ch; rate = r; bits = b;
        } else if (!std::memcmp(bytes.data() + at, "data", 4)) { data = bytes.data() + at + 8; dataBytes = size; }
        at += 8 + size + (size & 1);
    }
    if ((format != 1 && format != 0xfffe) || bits != 16 || !channels || channels > 2 || !rate || !data) return false;
    const std::size_t frames = dataBytes / (2 * channels); if (frames < 64) return false;
    s.pcm.resize(frames);
    for (std::size_t i = 0; i < frames; ++i) {
        int sum = 0; for (unsigned c = 0; c < channels; ++c) { std::int16_t v; std::memcpy(&v, data + (i * channels + c) * 2, 2); sum += v; }
        s.pcm[i] = std::int16_t(sum / int(channels));
    }
    s.format = WAVEFORMATEX{}; s.format.wFormatTag = WAVE_FORMAT_PCM; s.format.nChannels = 1; s.format.nSamplesPerSec = rate; s.format.wBitsPerSample = 16;
    s.format.nBlockAlign = 2; s.format.nAvgBytesPerSec = rate * 2; s.seconds = double(frames) / rate;
    return true;
}
inline bool fill(IDirectSoundBuffer* b, const Sample& s) {
    void* p1 = nullptr; void* p2 = nullptr; DWORD n1 = 0, n2 = 0; const DWORD bytes = DWORD(s.pcm.size() * 2);
    if (FAILED(b->Lock(0, bytes, &p1, &n1, &p2, &n2, 0))) return false;
    std::memcpy(p1, s.pcm.data(), (std::min)(n1, bytes)); if (p2 && n2) std::memcpy(p2, reinterpret_cast<const char*>(s.pcm.data()) + n1, (std::min)(n2, bytes - n1));
    b->Unlock(p1, n1, p2, n2); return true;
}
struct WindowSearch { DWORD pid; HWND found; };
inline BOOL CALLBACK findWindow(HWND w, LPARAM l) {
    auto* s = reinterpret_cast<WindowSearch*>(l); DWORD pid = 0; GetWindowThreadProcessId(w, &pid);
    RECT r{}; if (pid == s->pid && IsWindowVisible(w) && !GetWindow(w, GW_OWNER) && GetWindowRect(w, &r) && r.right - r.left >= 160) { s->found = w; return FALSE; }
    return TRUE;
}
inline void lockedLog(const char* text) { std::lock_guard<std::recursive_mutex> guard(mutex); log("%s", text); }
inline bool load() {
    // the game window (same focus rules as the game's own effect buffers): after the D3D device exists
    HWND window = nullptr;
    for (unsigned waited = 0; !window; ++waited) {
        if (gameDevice) { WindowSearch s{GetCurrentProcessId(), nullptr}; EnumWindows(findWindow, reinterpret_cast<LPARAM>(&s)); window = s.found; }
        if (!window) Sleep(250);
    }
    char line[512];
    HMODULE dsound = LoadLibraryA("dsound.dll");
    auto create = dsound ? reinterpret_cast<HRESULT(WINAPI*)(LPCGUID, LPDIRECTSOUND*, LPUNKNOWN)>(GetProcAddress(dsound, "DirectSoundCreate")) : nullptr;
    HRESULT hr = create ? create(nullptr, &device, nullptr) : E_FAIL;
    if (FAILED(hr) || !device) { std::snprintf(line, sizeof line, "HIT_SOUND_DISABLED DirectSoundCreate failed hr=%08lx: no hit sounds", hr); lockedLog(line); device = nullptr; return false; }
    hr = device->SetCooperativeLevel(window, DSSCL_NORMAL);
    if (FAILED(hr)) { std::snprintf(line, sizeof line, "HIT_SOUND_DISABLED SetCooperativeLevel failed hr=%08lx", hr); lockedLog(line); device->Release(); device = nullptr; return false; }
    // model-pack\hit-sounds\hits.txt: "<kind> <file.wav> <gain dB>" per sound
    const std::string dir = root + "\\model-pack\\hit-sounds\\";
    std::ifstream list(dir + "hits.txt"); std::string text; unsigned bad = 0, total = 0; double bytes = 0;
    while (std::getline(list, text)) {
        std::istringstream in(text); std::string kindName, file; float gain = 0;
        if (!(in >> kindName) || kindName[0] == '#') continue;
        if (!(in >> file >> gain)) { ++bad; continue; }
        unsigned kind = kKinds; for (unsigned k = 0; k < kKinds; ++k) if (kindName == kKindName[k]) kind = k;
        if (kind == kKinds || sampleCount[kind] >= kMaxPerKind || file.find("..") != std::string::npos) { ++bad; continue; }
        Sample& s = samples[kind][sampleCount[kind]];
        if (!readWav(dir + file, s)) { ++bad; std::snprintf(line, sizeof line, "HIT_SOUND_FILE_BAD %s (mono/stereo PCM16 WAV expected)", file.c_str()); lockedLog(line); continue; }
        s.file = file; s.gainDb = gain;
        DSBUFFERDESC desc{}; desc.dwSize = sizeof desc;
        desc.dwFlags = DSBCAPS_STATIC | DSBCAPS_CTRLFREQUENCY | DSBCAPS_CTRLPAN | DSBCAPS_CTRLVOLUME | DSBCAPS_GETCURRENTPOSITION2;   // the game's 0xE2 + position
        desc.dwBufferBytes = DWORD(s.pcm.size() * 2); desc.lpwfxFormat = &s.format;
        if (FAILED(device->CreateSoundBuffer(&desc, &s.voice[0], nullptr)) || !fill(s.voice[0], s)) { ++bad; if (s.voice[0]) s.voice[0]->Release(); s = Sample{}; continue; }
        for (unsigned d = 1; d < kDuplicates; ++d) if (FAILED(device->DuplicateSoundBuffer(s.voice[0], &s.voice[d]))) s.voice[d] = nullptr;
        ++sampleCount[kind]; ++total; bytes += double(s.pcm.size() * 2);
    }
    std::snprintf(line, sizeof line, "HIT_SOUND_READY window=%p sounds=%u (flesh %u heavy %u shotgun %u magnum %u corpse %u auto %u) bad=%u pcm_kb=%.0f voices=%d delay=%dms pitch=+-%d%% volume=%d%%",
        static_cast<void*>(window), total, sampleCount[Flesh], sampleCount[Heavy], sampleCount[Shotgun], sampleCount[Magnum], sampleCount[Corpse], sampleCount[Auto], bad, bytes / 1024., config.voices, config.delayMs, config.pitchPct, config.volumePct);
    lockedLog(line);
    return total > 0;
}
inline void play(const Request& r, double now) {
    Sample& s = samples[r.kind][r.variant]; if (!s.voice[0]) return;
    // a free duplicate of this sound, else the one that ends first; over the voice limit: the oldest voice stops
    unsigned active = 0; Sample* oldestSample = nullptr; unsigned oldestVoice = 0; double oldestEnd = 1e300;
    for (unsigned k = 0; k < kKinds; ++k) for (unsigned i = 0; i < sampleCount[k]; ++i) for (unsigned d = 0; d < kDuplicates; ++d)
        if (samples[k][i].voice[d] && samples[k][i].busyUntil[d] > now) { ++active; if (samples[k][i].busyUntil[d] < oldestEnd) { oldestEnd = samples[k][i].busyUntil[d]; oldestSample = &samples[k][i]; oldestVoice = d; } }
    unsigned use = kDuplicates;
    for (unsigned d = 0; d < kDuplicates; ++d) if (s.voice[d] && s.busyUntil[d] <= now) { use = d; break; }
    if (use == kDuplicates) { use = 0; for (unsigned d = 1; d < kDuplicates; ++d) if (s.voice[d] && s.busyUntil[d] < s.busyUntil[use]) use = d; InterlockedIncrement(&stolen); }
    else if (int(active) >= config.voices && oldestSample) { oldestSample->voice[oldestVoice]->Stop(); oldestSample->busyUntil[oldestVoice] = 0; InterlockedIncrement(&stolen); }
    IDirectSoundBuffer* b = s.voice[use];
    b->Stop(); b->SetCurrentPosition(0);
    const DWORD rate = DWORD((std::max)(100.f, float(s.format.nSamplesPerSec) * r.pitch));
    const LONG volume = LONG((std::max)(-10000.f, (std::min)(0.f, r.gainDb * 100.f)));
    const float p = (std::max)(-1.f, (std::min)(1.f, r.pan));
    b->SetFrequency(rate); b->SetVolume(volume); b->SetPan(LONG(p * 900.f));   // up to 9 dB off the far side
    HRESULT hr = b->Play(0, 0, 0);
    if (hr == DSERR_BUFFERLOST && SUCCEEDED(b->Restore()) && fill(b, s)) hr = b->Play(0, 0, 0);
    if (FAILED(hr)) { InterlockedIncrement(&failed); return; }
    s.busyUntil[use] = now + s.seconds / r.pitch * 1000.0 + 5.0;
    InterlockedIncrement(&played);
    const LONG late = LONG(now - r.due); if (late > lateMax) InterlockedExchange(&lateMax, late);
}
inline DWORD WINAPI worker(void*) {
    if (!load()) { InterlockedExchange(&installedFlag, 0); return 0; }
    InterlockedExchange(&ready, 1);
    Request due[kQueue];
    for (;;) {
        double now = nowMs(), next = 1e300; unsigned n = 0;
        AcquireSRWLockExclusive(&queueLock);
        for (unsigned i = 0; i < queued;) {
            if (queue[i].due <= now + 1.0) { due[n++] = queue[i]; queue[i] = queue[--queued]; }
            else { next = (std::min)(next, queue[i].due); ++i; }
        }
        ReleaseSRWLockExclusive(&queueLock);
        for (unsigned i = 0; i < n; ++i) {
            const double at = nowMs();
            play(due[i], at);
            if (config.trace && !due[i].test) { char line[160]; std::snprintf(line, sizeof line, "HIT_SOUND_PLAY kind=%s v=%02u t=%.1f late=%.1fms", kKindName[due[i].kind], unsigned(due[i].variant) + 1, at, at - due[i].due); lockedLog(line); }
            if (due[i].test) { char line[200]; std::snprintf(line, sizeof line, "HIT_SOUND_TEST kind=%s v=%02u file=%s vol=%.1fdB pan=%.2f pitch=%.3f t=%.1f", kKindName[due[i].kind], unsigned(due[i].variant) + 1,
                samples[due[i].kind][due[i].variant].file.c_str(), due[i].gainDb, due[i].pan, due[i].pitch, at); lockedLog(line); }
        }
        if (InterlockedExchange(&testRequest, 0)) {
            // every sound of every kind, 0.7 s apart, centre, nominal pitch; then flesh 1 hard left and hard right
            const float se = 0.f; double t = nowMs() + 300.0; unsigned queuedTest = 0;
            for (unsigned k = 0; k < kKinds; ++k) for (unsigned v = 0; v < sampleCount[k]; ++v) {
                Request r; r.kind = std::uint8_t(k); r.variant = std::uint8_t(v); r.gainDb = samples[k][v].gainDb + se; r.due = t; r.test = true; t += 700.0;
                if (push(r)) ++queuedTest; }
            for (float pan : {-1.f, 1.f}) { Request r; r.kind = Flesh; r.gainDb = samples[Flesh][0].gainDb; r.pan = pan; r.due = t; r.test = true; t += 700.0; if (sampleCount[Flesh] && push(r)) ++queuedTest; }
            char line[160]; std::snprintf(line, sizeof line, "HIT_SOUND_TEST queued=%u (all kinds, 0.7 s apart; volumes = file gains only, no SEvol)", queuedTest); lockedLog(line);
            continue;
        }
        const double wait = next >= 1e299 ? -1.0 : next - nowMs();
        WaitForSingleObject(wake, wait < 0 ? INFINITE : DWORD((std::max)(0.0, (std::min)(1000.0, wait))));
    }
}

// ---------------------------------------------------------------- diagnostics: what the game itself plays (hit_sound_trace=1)
using SoundUpdate = void(__cdecl*)();
static SoundUpdate originalSoundUpdate = nullptr;
using SoundRead = int(__cdecl*)(const char*, void*);
static SoundRead originalBankRead = nullptr;
static unsigned traceLines = 0;
// the frame's effect requests (queue A59A94..[A59D94], 0x20 each) just before 427760 plays them
inline void __cdecl soundUpdateHook() {
    {
        std::lock_guard<std::recursive_mutex> guard(mutex);
        const auto* begin = reinterpret_cast<const std::uint8_t*>(gameBase + 0x659a94);
        const auto* end = re3bridge::detail::Read<const std::uint8_t*>(reinterpret_cast<const void*>(gameBase + 0x659d94));
        if (end > begin && end <= begin + 0x300 && traceLines < 4000)
            for (const auto* e = begin; e < end && traceLines < 4000; e += 0x20) {
                ++traceLines;
                const unsigned slot = e[0x1f], index = re3bridge::detail::Read<std::uint16_t>(e, 0x1c);
                const auto* table = re3bridge::detail::Read<const std::uint8_t*>(reinterpret_cast<const void*>(gameBase + 0x6592c0 + slot * 4));
                const std::uint32_t entry = re3bridge::detail::Accessible(table + index * 4, 4) ? re3bridge::detail::Read<std::uint32_t>(table, index * 4) : 0xffffffffu;
                log("SE_REQ build=%u id=%x%02x%02x pos=%d,%d,%d a=%04x b=%04x entry=%08x",
                    re3bridge::BuildSerial(), unsigned(e[0x1e]), slot, index, re3bridge::detail::Read<std::int32_t>(e, 8), re3bridge::detail::Read<std::int32_t>(e, 0xc), re3bridge::detail::Read<std::int32_t>(e, 0x10),
                    unsigned(re3bridge::detail::Read<std::uint16_t>(e, 0x1a)), unsigned(re3bridge::detail::Read<std::uint16_t>(e, 0x18)), entry);
            }
    }
    originalSoundUpdate();
}
inline int __cdecl bankReadHook(const char* name, void* buffer) {
    const int size = originalBankRead(name, buffer);
    std::lock_guard<std::recursive_mutex> guard(mutex);
    if (traceLines < 4000) { ++traceLines; log("SE_BANK %s size=%d", re3bridge::detail::Accessible(name, 8) ? name : "?", size); }
    return size;
}
inline void installTrace() {
    re3bridge::detail::Patch update, bank;
    const bool a = re3bridge::detail::BuildCallPatch(update, gameBase + 0x239e6, gameBase + 0x27760, reinterpret_cast<void*>(soundUpdateHook));
    const bool b = re3bridge::detail::BuildCallPatch(bank, gameBase + 0x2a6b4, gameBase + 0x264e0, reinterpret_cast<void*>(bankReadHook));
    if (a) { originalSoundUpdate = reinterpret_cast<SoundUpdate>(gameBase + 0x27760); if (!re3bridge::detail::WritePatch(update, true)) originalSoundUpdate = nullptr; }
    if (b) { originalBankRead = reinterpret_cast<SoundRead>(gameBase + 0x264e0); if (!re3bridge::detail::WritePatch(bank, true)) originalBankRead = nullptr; }
    log("HIT_SOUND_TRACE effect requests 4239E6->427760 %s, bank reads 42A6B4->4264E0 %s", originalSoundUpdate ? "traced" : "NOT traced (signature)", originalBankRead ? "traced" : "NOT traced (signature)");
}

inline void install() {
    const std::string ini = root + "\\pilot.ini";
    config.enabled = GetPrivateProfileIntA("pilot", "hit_sounds", 1, ini.c_str()) != 0;
    config.trace = GetPrivateProfileIntA("pilot", "hit_sound_trace", 0, ini.c_str()) != 0;
    config.volumePct = (std::max)(0, (std::min)(200, int(GetPrivateProfileIntA("pilot", "hit_sound_volume", 100, ini.c_str()))));
    config.delayMs = (std::max)(0, (std::min)(200, int(GetPrivateProfileIntA("pilot", "hit_sound_delay_ms", 45, ini.c_str()))));
    config.voices = (std::max)(1, (std::min)(24, int(GetPrivateProfileIntA("pilot", "hit_sound_voices", 8, ini.c_str()))));
    config.pitchPct = (std::max)(0, (std::min)(20, int(GetPrivateProfileIntA("pilot", "hit_sound_pitch", 6, ini.c_str()))));
    if (config.trace) installTrace();
    if (!config.enabled) { log("HIT_SOUND disabled by pilot.ini hit_sounds=0"); return; }
    LARGE_INTEGER f; QueryPerformanceFrequency(&f); qpcScale = 1000.0 / double(f.QuadPart);
    seed ^= GetTickCount();
    wake = CreateEventA(nullptr, FALSE, FALSE, nullptr);
    InterlockedExchange(&installedFlag, 1);
    HANDLE thread = wake ? CreateThread(nullptr, 0, worker, nullptr, 0, nullptr) : nullptr;
    if (!thread) { InterlockedExchange(&installedFlag, 0); log("HIT_SOUND_DISABLED worker thread not started"); return; }
    SetThreadPriority(thread, THREAD_PRIORITY_ABOVE_NORMAL); CloseHandle(thread);
    log("HIT_SOUND installed: worker loads model-pack\\hit-sounds once the game window exists (volume %d%%, delay %d ms, voices %d, pitch +-%d%%)", config.volumePct, config.delayMs, config.voices, config.pitchPct);
}
} // namespace sfxhits
