#pragma once
// Exact GOG RE3 packet adapter. No inline trampoline or on-disk patching.
// Include in ONE x86 translation unit. Install on the game thread before play.
// This header does not load meshes, touch D3D state, or poll input.
#include <windows.h>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>
#include <algorithm>
#include <utility>
#include "exe_map.h"

namespace re3bridge {
static_assert(sizeof(void*) == 4, "The native RE3 adapter requires x86.");
constexpr unsigned kMaxParts = 256;
constexpr unsigned kPartStride = 0xbc;
// Native pOT is 0x400 DWORDs, proved by clear at 423832/423838 and
// double-buffer stride 0x1000 at 4237ED. The theoretical OTZ range is larger.
constexpr unsigned kMaxBucket = 1023;
constexpr unsigned kMaxJobsPerActor = 8192;
constexpr unsigned kMaxRetainedFrames = 8;
// A frame whose sources/copies are still not proven consumed this many builds later keeps its commands but drops
// its GPU/CPU geometry (normal frames retire 3 builds later).
constexpr unsigned kStalePayloadBuilds = 150;

struct Matrix32 {
    int16_t rotation[9];
    int16_t padding;
    int32_t translation[3];
};
static_assert(sizeof(Matrix32) == 32, "Native MATRIX layout.");
struct PartSnapshot {
    void* nativePart = nullptr;
    void* runtimeMesh = nullptr;
    uint32_t flags = 0, attributes = 0;
    uint16_t renderAttributes = 0;
    uint32_t tintRGB = 0;
    int16_t scale[3]{};
    Matrix32 world{};
    bool visible = false; // Matches the native actor and per-part visibility gates.
};
struct ActorContext {
    uint32_t serial = 0;
    void* actor = nullptr;
    void* parts = nullptr;
    const void* positionContext = nullptr;
    uint32_t flags = 0;
    uint32_t* orderingTable = nullptr;
    uint8_t modelId = 0, weaponId = 0, partCount = 0;
    bool isPlayer = false;
    uint32_t playerCostume = 0;
    Matrix32 camera{};
    int16_t worldLight[9]{};
    // Original GTE light/projection registers. Read H +0x78, offsets +0x70/74,
    // ZSF3 +0x80; the current rotation is NOT necessarily the camera rotation.
    std::array<uint8_t, 0x100> gte{};
    std::vector<PartSnapshot> bones;
};
struct BucketJob {
    uint16_t bucket = 0;
    uintptr_t token = 0;
    // Optional ownership for token's geometry/material payload. Retained until
    // copied commands have rendered; a raw token alone requires releaseFrame.
    std::shared_ptr<void> keepAlive;
};
struct ActorPlan {
    std::vector<BucketJob> jobs;
    // Suppress only selected native parts. All attachments/effects not selected
    // still run through the original actor builder and original ordering table.
    std::array<uint8_t, kMaxParts> suppressParts{};
};
struct Callbacks {
    void* user = nullptr;
    bool (*beginBuild)(void* user, uint32_t serial) = nullptr;
    bool (*prepareActor)(void* user, const ActorContext&, ActorPlan&) = nullptr;
    // Must draw already prepared geometry; flush native/GOG batches here.
    // A false result is counted, but cannot reconstruct suppressed geometry.
    bool (*drawJob)(void* user, void* renderer, uintptr_t token) = nullptr;
    void (*releaseFrame)(void* user, uint32_t serial) = nullptr;
    void (*message)(void* user, const char* text) = nullptr;
};
struct Stats {
    uint64_t builds = 0, actorsPrepared = 0, actorsFallback = 0;
    uint64_t partsSuppressed = 0, packetsInserted = 0, packetsCopied = 0;
    uint64_t jobsDrawn = 0, drawFailures = 0, invalidPackets = 0;
    uint64_t capacityFallbacks = 0;
    uint64_t modeMarkersDrawn = 0, mixedModeQueues = 0;
    uint64_t sourceSlotsRetired = 0, queueResets = 0, framesRetired = 0;
    uint64_t nativeNearCutJobs = 0, emptyAfterNativeNearCut = 0;
    uint64_t retirementDeferred = 0;
    uint64_t frameBuilds = 0, stalePayloads = 0, zoneSlotsOrphaned = 0;
};

namespace detail {
template<class T> inline T Read(const void* p, size_t offset = 0) {
    T value{}; std::memcpy(&value, static_cast<const uint8_t*>(p) + offset, sizeof(T)); return value;
}
template<class T> inline void Write(void* p, size_t offset, T value) {
    std::memcpy(static_cast<uint8_t*>(p) + offset, &value, sizeof(T));
}
inline bool Accessible(const void* p, size_t bytes, bool writable = false) {
    uintptr_t at = reinterpret_cast<uintptr_t>(p);
    if (!at || bytes > UINTPTR_MAX - at) return false;
    const uintptr_t end = at + bytes;
    while (at < end) {
        MEMORY_BASIC_INFORMATION info{};
        if (!VirtualQuery(reinterpret_cast<void*>(at), &info, sizeof(info)) ||
            info.State != MEM_COMMIT || (info.Protect & (PAGE_GUARD | PAGE_NOACCESS))) return false;
        const DWORD access = info.Protect & 0xff;
        if (writable && access != PAGE_READWRITE && access != PAGE_WRITECOPY &&
            access != PAGE_EXECUTE_READWRITE && access != PAGE_EXECUTE_WRITECOPY) return false;
        const uintptr_t next = reinterpret_cast<uintptr_t>(info.BaseAddress) + info.RegionSize;
        if (next <= at) return false;
        at = (std::min)(next, end);
    }
    return true;
}

// Native code 0x34 is GT3: the walker copies EXACTLY 40 bytes at 0x41FE70.
// TPAGE +0x1a is deliberately excluded from our metadata and preserved at copy.
struct alignas(4) Packet { uint8_t bytes[40]{}; };
static_assert(sizeof(Packet) == 40, "Native GT3 copy size.");
constexpr uint32_t kMagicCommand = 0x34d34d52;
constexpr uint32_t kMagic2 = 0xb91e72c5;
constexpr uint32_t kMagic3 = 0x68444d33;
inline uint32_t Cookie(uint32_t serial, uint32_t batch, uint32_t job) {
    return 0x7a9d21e3u ^ serial * 0x9e3779b9u ^ batch * 0x85ebca6bu ^ job * 0xc2b2ae35u;
}
inline void Encode(Packet& packet, uint32_t serial, uint32_t batch, uint32_t job) {
    packet = Packet{};
    Write<uint32_t>(&packet, 4, kMagicCommand);
    Write<uint32_t>(&packet, 8, serial);
    Write<uint32_t>(&packet, 12, batch);
    Write<uint32_t>(&packet, 16, job);
    Write<uint32_t>(&packet, 20, kMagic2);
    Write<uint16_t>(&packet, 24, 0x5333);
    Write<uint32_t>(&packet, 28, Cookie(serial, batch, job));
    Write<uint32_t>(&packet, 32, ~serial);
    Write<uint32_t>(&packet, 36, kMagic3);
}
inline bool Signature(const void* packet) {
    return Read<uint32_t>(packet, 4) == kMagicCommand &&
        Read<uint32_t>(packet, 20) == kMagic2 && Read<uint32_t>(packet, 36) == kMagic3;
}
struct Batch {
    bool modeMarker = false;
    ActorPlan plan;
    std::vector<Packet> packets; // Sized ONCE before any OT pointer is published.
    std::vector<void*> sources; // Own packet, or a registered 40-byte native packet.
    std::vector<uint32_t*> sourceSlots;
    std::vector<uint8_t> sourceRetired;
    struct CopyRef {uint8_t queue = 255; uint64_t generation = 0; bool conflict = false;};
    std::vector<CopyRef> copies;
    std::vector<uint8_t> copied, rendered;
};
struct Frame {
    uint32_t serial = 0;
    bool mode = false, payloadReleased = false;
    std::vector<std::unique_ptr<Batch>> batches;
    size_t jobs = 0, copied = 0, rendered = 0;
};
using ActorFn = void (__cdecl*)(void*, void*, uint32_t, const void*);
using PartFn = void (__cdecl*)(void*, void*, uint32_t, const Matrix32*);
using BeginFn = void (__cdecl*)();
using CopyFn = int (__thiscall*)(void*, void*);
using DrawFn = void (__thiscall*)(void*, void*);
using FrameFn = void (__thiscall*)(void*);
using ClearOtFn = int (__cdecl*)(uint32_t*, int);
using QueueResetFn = void (__thiscall*)(void*);
struct Patch {
    void* address = nullptr;
    std::array<uint8_t, 5> original{}, replacement{};
    size_t size = 0;
};
struct State {
    re3exe::Base base;
    bool installed = false, enabled = true;
    uint32_t serial = 0;
    uint32_t activeHooks = 0;
    uint32_t otFrames = 0, buildOtFrame = 0; // main OT clears (one per native frame) / value at the last build start
    std::array<uint32_t*, 6> zoneTables{}; uint32_t zoneCount = 0; // room OTs listed at the last build start
    bool pendingModeValid = false, pendingMode = false;
    bool renderModeValid = false, renderMode = false;
    uint32_t renderSerial = 0;
    Frame* building = nullptr;
    const ActorContext* activeActor = nullptr;
    const ActorPlan* activePlan = nullptr;
    Callbacks callbacks{};
    Stats stats{};
    ActorFn originalActor = nullptr;
    PartFn originalPart = nullptr;
    BeginFn originalBegin = nullptr;
    CopyFn originalCopy = nullptr;
    DrawFn originalDraw = nullptr;
    FrameFn previousFrame = nullptr;
    ClearOtFn originalClearOt = nullptr;
    QueueResetFn originalQueueReset = nullptr;
    bool replaying = false; // fps60: the in-between pass draws a queue that its real frame draws again
    struct Queue {void* object = nullptr; uint64_t generation = 1;};
    std::array<Queue, 8> queues{};
    std::vector<std::unique_ptr<Frame>> frames;
    std::array<Patch, 19> patches{};
};
inline State& Get() { static State state; return state; }
struct ActiveHook {
    ActiveHook(){++Get().activeHooks;}
    ~ActiveHook(){--Get().activeHooks;}
    ActiveHook(const ActiveHook&)=delete;
    ActiveHook& operator=(const ActiveHook&)=delete;
};
inline void Message(const char* text) {
    auto& s = Get(); if (s.callbacks.message) s.callbacks.message(s.callbacks.user, text);
}
inline void Release(Frame& frame) {
    auto& s = Get();
    if (s.callbacks.releaseFrame) s.callbacks.releaseFrame(s.callbacks.user, frame.serial);
}
// Room ("zone") OTs: [A5CBD8] (<= 3) tables per buffer at A5C7F4 + buffer*16 (45AE98), cleared at 423875 and walked at
// 423F3B only while listed there. A room transition zeroes the count (42C250) and the next room lists its own tables, so a
// source in a zone OT that left the list is never cleared or walked again (the last frame before every room change).
inline void RetireOrphanedZoneSlots() {
    auto& s = Get(); if (!s.base) return;
    std::array<uint32_t*, 6> zones{}; uint32_t count = 0;
    const uint32_t perBuffer = (std::min)(Read<uint32_t>(reinterpret_cast<void*>(s.base + 0x65cbd8)), 3u);
    for (uint32_t b = 0; b < 2; ++b) for (uint32_t i = 0; i < perBuffer; ++i)
        zones[count++] = Read<uint32_t*>(reinterpret_cast<void*>(s.base + 0x65c7f4 + b * 16 + i * 4));
    auto listed = [](const uint32_t* slot, const std::array<uint32_t*, 6>& tables, uint32_t n) {
        for (uint32_t k = 0; k < n; ++k) if (tables[k] && slot >= tables[k] && slot < tables[k] + kMaxBucket + 1) return true;
        return false;
    };
    if (count != s.zoneCount || zones != s.zoneTables)
        for (auto& frame : s.frames) for (auto& batch : frame->batches) for (size_t i = 0; i < batch->sourceSlots.size(); ++i) {
            const uint32_t* slot = batch->sourceSlots[i];
            if (!batch->sourceRetired[i] && slot && listed(slot, s.zoneTables, s.zoneCount) && !listed(slot, zones, count)) {
                batch->sourceRetired[i] = 1; ++s.stats.zoneSlotsOrphaned;
            }
        }
    s.zoneTables = zones; s.zoneCount = count;
}
inline void RetireRenderedFrames() {
    auto& s = Get();
    // Copy/Draw callbacks can flush native queues recursively. Their local
    // Frame/Batch pointers must outlive the final copied/rendered bookkeeping.
    if (s.activeHooks) {++s.stats.retirementDeferred; return;}
    RetireOrphanedZoneSlots();
    for (auto it = s.frames.begin(); it != s.frames.end();) {
        auto& f = **it;
        // Keep two completed build generations, so no OT source pointer can be
        // reclaimed during the render/copy operation that just consumed it.
        const bool old = s.serial - f.serial > 2;
        bool consumersGone = true;
        size_t liveSources = 0, liveCopies = 0; const void* liveSlot = nullptr;
        for (auto& batch : f.batches) for (size_t i = 0; i < batch->sources.size(); ++i) {
            // An in-place native packet may be overwritten/reused without an
            // observed OT slot. Its old payload then no longer exists there.
            if (!batch->sourceRetired[i] && !batch->sourceSlots[i] &&
                batch->sources[i] != &batch->packets[i]) {
                const auto* source = batch->sources[i];
                if (!Accessible(source, sizeof(Packet)) || !Signature(source) ||
                    Read<uint32_t>(source, 8) != f.serial ||
                    Read<uint32_t>(source, 28) != Read<uint32_t>(&batch->packets[i], 28))
                    batch->sourceRetired[i] = 1;
            }
            const auto& copy = batch->copies[i];
            const bool pendingCopy = copy.conflict || (copy.queue < s.queues.size() &&
                s.queues[copy.queue].generation == copy.generation);
            if (!batch->sourceRetired[i] || pendingCopy) consumersGone = false;
            if (!batch->sourceRetired[i] && !liveSources++) liveSlot = batch->sourceSlots[i] ? static_cast<const void*>(batch->sourceSlots[i]) : batch->sources[i];
            if (pendingCopy) ++liveCopies;
        }
        if (old && consumersGone) {
            Release(**it); it = s.frames.erase(it); ++s.stats.framesRetired;
            continue;
        }
        // Not consumed long after its render (e.g. an OT slot that is never cleared again): the commands stay valid,
        // a late draw finds token 0 and is only counted as a failure, but geometry, stream pages and textures go.
        if (!f.payloadReleased && s.serial - f.serial > kStalePayloadBuilds) {
            f.payloadReleased = true; ++s.stats.stalePayloads;
            for (auto& batch : f.batches) for (auto& job : batch->plan.jobs) { job.token = 0; job.keepAlive.reset(); }
            if (s.stats.stalePayloads <= 16) {
                char text[200];
                snprintf(text, sizeof(text), "Native3D: frame %u unconsumed after %u builds (sources=%zu first=%p copies=%zu); HD payload released, commands kept.",
                    f.serial, s.serial - f.serial, liveSources, liveSlot, liveCopies);
                Message(text);
            }
        }
        ++it;
    }
}
inline int __cdecl ClearOtHook(uint32_t* table, int count) {
    auto& s = Get(); const int result = s.originalClearOt(table, count);
    // 423838 clears the current main OT (*A5C7A4) once per native frame (loop 4237DA), whichever task draws it.
    if (s.base && count == int(kMaxBucket + 1) && table == Read<uint32_t*>(reinterpret_cast<void*>(s.base + 0x65c7a4))) ++s.otFrames;
    // This exact native routine has already overwritten the complete chain of
    // roots. Copied render commands are separately retained by queue generation.
    if (count > 0 && count <= 0x10000) {
        const uintptr_t first = reinterpret_cast<uintptr_t>(table);
        const uintptr_t end = first + size_t(count) * sizeof(uint32_t);
        for (auto& frame : s.frames) for (auto& batch : frame->batches)
            for (size_t i = 0; i < batch->sourceSlots.size(); ++i) {
                const uintptr_t slot = reinterpret_cast<uintptr_t>(batch->sourceSlots[i]);
                if (!batch->sourceRetired[i] && slot >= first && slot < end) {
                    batch->sourceRetired[i] = 1; ++s.stats.sourceSlotsRetired;
                }
            }
    }
    return result;
}
inline void __fastcall QueueResetHook(void* queue, void*) {
    auto& s = Get(); s.originalQueueReset(queue);
    if (s.replaying) return; // the caller puts the cursors back; its commands are still to be drawn
    // 420760 sets BOTH native write/read cursors to base. The old copied
    // command list has no remaining consumer, whether rendered or discarded.
    for (auto& entry : s.queues) if (entry.object == queue) {++entry.generation; ++s.stats.queueResets; break;}
}
inline uint8_t TrackQueue(void* renderer) {
    auto& s = Get(); auto* queue = static_cast<uint8_t*>(renderer) + 0x40;
    for (uint8_t i = 0; i < s.queues.size(); ++i) if (s.queues[i].object == queue) return i;
    for (uint8_t i = 0; i < s.queues.size(); ++i) if (!s.queues[i].object) {s.queues[i].object = queue; return i;}
    return 255;
}
inline Batch* FindBatch(const void* packet, Frame** frameOut = nullptr, uint32_t* jobOut = nullptr) {
    auto& s = Get();
    const uint32_t serial = Read<uint32_t>(packet, 8), batch = Read<uint32_t>(packet, 12), job = Read<uint32_t>(packet, 16);
    if (Read<uint32_t>(packet, 28) != Cookie(serial, batch, job) ||
        Read<uint32_t>(packet, 32) != ~serial || Read<uint16_t>(packet, 24) != 0x5333) return nullptr;
    for (auto& f : s.frames) if (f->serial == serial) {
        if (batch >= f->batches.size() || job >= f->batches[batch]->plan.jobs.size()) return nullptr;
        if (frameOut) *frameOut = f.get();
        if (jobOut) *jobOut = job;
        return f->batches[batch].get();
    }
    return nullptr;
}
inline void BeginBuild() {
    auto& s = Get();
    ++s.serial; ++s.stats.builds; s.buildOtFrame = s.otFrames;
    RetireRenderedFrames();
    ActiveHook active;
    s.building = nullptr;
    s.pendingMode = false; s.pendingModeValid = true;
    try {
        if (s.callbacks.beginBuild) s.pendingMode = s.callbacks.beginBuild(s.callbacks.user, s.serial);
        if (!s.callbacks.prepareActor || !s.callbacks.drawJob) return;
        if (s.frames.size() >= kMaxRetainedFrames) {
            ++s.stats.capacityFallbacks;
            if (s.stats.capacityFallbacks == 1) Message("Native3D: retained command limit reached; original models retained.");
            return;
        }
        auto frame = std::make_unique<Frame>(); frame->serial = s.serial; frame->mode = s.pendingMode;
        s.building = frame.get(); s.frames.push_back(std::move(frame));
    } catch (...) { s.building = nullptr; Message("Native3D: build preparation failed; original models retained."); }
}
inline bool PublishModeMarker(uint32_t* slot) {
    auto& s = Get();
    if (!s.building || !Accessible(slot, sizeof(uint32_t), true)) return false;
    Batch* accepted = nullptr;
    try {
        auto batch = std::make_unique<Batch>(); batch->modeMarker = true;
        batch->plan.jobs.resize(1); batch->packets.resize(1); batch->sources.resize(1);
        batch->sourceSlots.resize(1, slot); batch->sourceRetired.resize(1); batch->copies.resize(1);
        batch->copied.resize(1); batch->rendered.resize(1);
        Encode(batch->packets[0], s.serial, static_cast<uint32_t>(s.building->batches.size()), 0);
        batch->sources[0] = &batch->packets[0]; accepted = batch.get();
        s.building->batches.push_back(std::move(batch));
        ++s.building->jobs;
    } catch (...) { return false; }
    Write<uint32_t>(&accepted->packets[0], 0, *slot | 3u);
    *slot = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&accepted->packets[0]));
    return true;
}
inline void __cdecl BeginHook() {
    auto& s = Get(); s.originalBegin(); BeginBuild();
    auto* mainOT = Read<uint32_t*>(reinterpret_cast<void*>(s.base + 0x65c7a4));
    if (mainOT) PublishModeMarker(mainOT + kMaxBucket);
}
// figure: Mercenaries PLAYER SELECT 447E50 = 4357D0 without the camera tests; its figures are player models (PL0xCH +
// PL0xW0y) in task actors outside the player slot.
inline bool Snapshot(void* actor, void* parts, uint32_t flags, const void* position, ActorContext& out, bool figure = false) {
    auto& s = Get();
    if (!Accessible(actor, 0x180) || !parts || !Accessible(position, 12)) return false;
    const uint8_t count = Read<uint8_t>(actor, 8);
    if (!count || !Accessible(parts, size_t(count) * kPartStride)) return false;
    out.serial = s.serial; out.actor = actor; out.parts = parts; out.flags = flags; out.positionContext = position;
    out.modelId = Read<uint8_t>(actor, 0x4a); out.weaponId = Read<uint8_t>(actor, 0x46); out.partCount = count;
    out.orderingTable = Read<uint32_t*>(actor, 0x7c);
    out.isPlayer = figure || actor == reinterpret_cast<void*>(s.base + 0x65cbf4);
    out.playerCostume = Read<uint32_t>(reinterpret_cast<void*>(s.base + 0x66262c));
    out.camera = Read<Matrix32>(reinterpret_cast<void*>(s.base + 0x11c398));
    std::memcpy(out.worldLight, reinterpret_cast<void*>(s.base + 0x11c358), sizeof(out.worldLight));
    const auto* gte = Read<const void*>(reinterpret_cast<void*>(s.base + 0x1349c8));
    if (!Accessible(gte, out.gte.size()) || !out.orderingTable) return false;
    std::memcpy(out.gte.data(), gte, out.gte.size());
    // These are the same side-effect-free visibility predicates called by
    // 4357D0 / 4358F0. Do not turn a hidden part into visible HD geometry.
    auto cameraMode = reinterpret_cast<int (__cdecl*)()>(s.base + 0x25540);
    auto visibleAt = reinterpret_cast<int (__cdecl*)(const void*, const void*)>(s.base + 0x26790);
    const void* cameraRegion = Read<const void*>(reinterpret_cast<void*>(s.base + 0x65c8a8));
    if (figure ? (flags & 0x10) != 0 : !Accessible(cameraRegion, 0x14) || (flags & 0x10) || (cameraMode() && (flags & 2))) return false;
    const bool actorVisible = figure || (flags & 0x40) || visibleAt(position, cameraRegion) != 0;
    out.bones.resize(count);
    for (unsigned i = 0; i < count; ++i) {
        auto* part = static_cast<uint8_t*>(parts) + size_t(i) * kPartStride;
        auto& b = out.bones[i];
        b.nativePart = part; b.runtimeMesh = Read<void*>(part, 8);
        b.flags = Read<uint32_t>(part); b.attributes = Read<uint32_t>(part, 4);
        b.renderAttributes = Read<uint16_t>(part, 0xa2); b.world = Read<Matrix32>(part, 0x40);
        b.tintRGB = Read<uint32_t>(part, 0x68) & 0xffffffu;
        std::memcpy(b.scale, part + 0x9c, sizeof(b.scale));
        b.visible = (b.flags & 1) && (b.renderAttributes & 1) && (actorVisible || (b.flags & 0x20));
        if (b.visible && (b.flags & 0x20)) b.visible = (!figure || Accessible(cameraRegion, 0x14)) && visibleAt(part + 0x54, cameraRegion) != 0;
    }
    return true;
}
inline bool ValidatePlan(const ActorContext& context, const ActorPlan& plan) {
    if (plan.jobs.empty() || plan.jobs.size() > kMaxJobsPerActor || !context.orderingTable) return false;
    bool suppressesVisible = false;
    for (unsigned i = 0; i < kMaxParts; ++i) if (plan.suppressParts[i]) {
        if (i >= context.bones.size()) return false;
        suppressesVisible |= context.bones[i].visible;
    }
    if (!suppressesVisible) return false;
    // One memory query for the whole table range: a query per job was ~700 system calls per frame with the GPU path's
    // job per bucket.
    uint16_t low = 0xffff, high = 0;
    for (const auto& job : plan.jobs) {
        if (job.bucket > kMaxBucket || !job.token) return false;
        low = (std::min)(low, job.bucket); high = (std::max)(high, job.bucket);
    }
    return Accessible(context.orderingTable + low, (size_t(high) - low + 1) * sizeof(uint32_t), true);
}
inline void InsertPackets(uint32_t* orderingTable, Batch& batch) {
    // Native prepending: link stores previous head | 3 (ONE command), OT head
    // stores the aligned packet address. Never use PSX 24-bit address masking.
    for (size_t i = 0; i < batch.plan.jobs.size(); ++i) {
        const auto bucket = batch.plan.jobs[i].bucket;
        Write<uint32_t>(&batch.packets[i], 0, orderingTable[bucket] | 3u);
        orderingTable[bucket] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&batch.packets[i]));
    }
}
inline void ApplyNativeActorCutoff(ActorPlan& plan, bool preserveNearBuckets) {
    if (preserveNearBuckets) return;
    auto& jobs = plan.jobs; const size_t before = jobs.size();
    jobs.erase(std::remove_if(jobs.begin(), jobs.end(), [](const BucketJob& job){return job.bucket < 8;}), jobs.end());
    Get().stats.nativeNearCutJobs += before - jobs.size();
    if (before && jobs.empty()) ++Get().stats.emptyAfterNativeNearCut;
}
inline void __cdecl PartHook(void* actor, void* part, uint32_t attributes, const Matrix32* world) {
    auto& s = Get();
    if (s.activeActor && s.activePlan && actor == s.activeActor->actor) {
        const uintptr_t first = reinterpret_cast<uintptr_t>(s.activeActor->parts);
        const uintptr_t current = reinterpret_cast<uintptr_t>(part);
        if (current >= first && (current - first) % kPartStride == 0) {
            const size_t index = (current - first) / kPartStride;
            if (index < s.activeActor->partCount && s.activePlan->suppressParts[index]) {
                ++s.stats.partsSuppressed; return;
            }
        }
    }
    s.originalPart(actor, part, attributes, world);
}
inline void DrawActor(ActorFn original, bool figure, void* actor, void* parts, uint32_t flags, const void* position) {
    ActiveHook active;
    auto& s = Get();
    ActorContext context;
    Batch* accepted = nullptr;
    if (s.enabled && s.building && s.pendingMode && !s.activeActor) {
        try {
            if (Snapshot(actor, parts, flags, position, context, figure)) {
                auto batch = std::make_unique<Batch>();
                bool prepared = s.callbacks.prepareActor(s.callbacks.user, context, batch->plan);
                // 423F6B/423FA7 clear OT[7] before walking main/room OTs when
                // bit31 is off. Such low buckets never reach the native copier.
                if (prepared) ApplyNativeActorCutoff(batch->plan,
                    (Read<uint32_t>(reinterpret_cast<void*>(s.base + 0x65cbd4)) & 0x80000000u) != 0);
                if (prepared && ValidatePlan(context, batch->plan)) {
                    const size_t count = batch->plan.jobs.size();
                    batch->packets.resize(count); batch->sources.resize(count);
                    batch->sourceSlots.resize(count); batch->sourceRetired.resize(count); batch->copies.resize(count);
                    batch->copied.resize(count); batch->rendered.resize(count);
                    const uint32_t index = static_cast<uint32_t>(s.building->batches.size());
                    for (uint32_t i = 0; i < count; ++i) {
                        Encode(batch->packets[i], s.serial, index, i);
                        batch->sources[i] = &batch->packets[i];
                        batch->sourceSlots[i] = context.orderingTable + batch->plan.jobs[i].bucket;
                    }
                    accepted = batch.get();
                    s.building->batches.push_back(std::move(batch)); // Last potentially throwing operation.
                    s.building->jobs += count;
                }
            }
        } catch (...) { accepted = nullptr; }
    }
    if (!accepted) { ++s.stats.actorsFallback; original(actor, parts, flags, position); return; }
    const auto* priorActor = s.activeActor; const auto* priorPlan = s.activePlan;
    s.activeActor = &context; s.activePlan = &accepted->plan;
    original(actor, parts, flags, position);
    s.activeActor = priorActor; s.activePlan = priorPlan;
    InsertPackets(context.orderingTable, *accepted);
    ++s.stats.actorsPrepared; s.stats.packetsInserted += accepted->packets.size();
}
inline void __cdecl ActorHook(void* actor, void* parts, uint32_t flags, const void* position) {
    DrawActor(Get().originalActor, false, actor, parts, flags, position);
}
inline int __fastcall CopyHook(void* renderer, void*, void* packet) {
    ActiveHook active;
    auto& s = Get();
    if (Signature(packet)) {
        Frame* frame = nullptr; uint32_t index = 0;
        auto* batch = FindBatch(packet, &frame, &index);
        // A recognized but expired/corrupt tag is consumed, never interpreted as
        // ordinary geometry. This path is diagnostic, not a model fallback.
        if (!batch || packet != batch->sources[index]) { ++s.stats.invalidPackets; return 40; }
        const uint16_t precedingTpage = static_cast<uint16_t>(Read<uint32_t>(renderer, 0x64));
        Write<uint16_t>(packet, 0x1a, precedingTpage);
        const uint8_t queue = TrackQueue(renderer);
        const auto* nativeQueue = static_cast<uint8_t*>(renderer) + 0x40;
        const uintptr_t beforeBytes = Read<uintptr_t>(nativeQueue, 8) - Read<uintptr_t>(nativeQueue);
        const int result = s.originalCopy(renderer, packet);
        const uintptr_t afterBytes = Read<uintptr_t>(nativeQueue, 8) - Read<uintptr_t>(nativeQueue);
        if (afterBytes >= beforeBytes + sizeof(Packet) + 4) {
            auto& copy = batch->copies[index];
            if (queue == 255 || (copy.queue < s.queues.size() && copy.queue != queue &&
                s.queues[copy.queue].generation == copy.generation)) copy.conflict = true;
            else {copy.queue = queue; copy.generation = s.queues[queue].generation;}
        }
        if (!batch->copied[index]) { batch->copied[index] = 1; ++frame->copied; }
        ++s.stats.packetsCopied; return result;
    }
    return s.originalCopy(renderer, packet);
}
inline void __fastcall DrawHook(void* renderer, void*, void* packet) {
    ActiveHook active;
    auto& s = Get();
    if (!Signature(packet)) { s.originalDraw(renderer, packet); return; }
    Frame* frame = nullptr; uint32_t index = 0;
    auto* batch = FindBatch(packet, &frame, &index);
    if (!batch || !batch->copied[index]) { ++s.stats.invalidPackets; return; }
    if (batch->modeMarker) ++s.stats.modeMarkersDrawn;
    else {
        bool okay = false;
        try { okay = s.callbacks.drawJob(s.callbacks.user, renderer, batch->plan.jobs[index].token); }
        catch (...) { okay = false; }
        if (okay) ++s.stats.jobsDrawn; else ++s.stats.drawFailures;
    }
    if (!batch->rendered[index]) { batch->rendered[index] = 1; ++frame->rendered; }
}
inline bool QueuedMode(void* renderer, bool& mode, uint32_t& serial) {
    // Native renderer+0x40 owns its COPIED queue: base+0, write cursor+8.
    // Entries are [DWORD alignedSize][payload]. Scan without advancing its
    // consumer cursor. This is the frame about to render, not the latest build.
    if (!Accessible(renderer, 0x50)) return false;
    const uintptr_t first = Read<uintptr_t>(renderer, 0x40);
    const uintptr_t end = Read<uintptr_t>(renderer, 0x48);
    if (!first || end < first || end - first > 0x100000 ||
        !Accessible(reinterpret_cast<void*>(first), end - first)) return false;
    bool found = false;
    for (uintptr_t at = first; at < end;) {
        if (end - at < 4) break;
        const uint32_t bytes = Read<uint32_t>(reinterpret_cast<void*>(at));
        if (!bytes || (bytes & 3u) || bytes > end - at - 4) break;
        const void* packet = reinterpret_cast<void*>(at + 4);
        if (bytes == sizeof(Packet) && Signature(packet)) {
            Frame* frame = nullptr;
            if (FindBatch(packet, &frame)) {
                if (!found) { found = true; mode = frame->mode; serial = frame->serial; }
                else if (mode != frame->mode) ++Get().stats.mixedModeQueues;
            }
        }
        at += bytes + 4;
    }
    return found;
}
inline void __fastcall FrameHook(void* renderer, void*) {
    ActiveHook active;
    auto& s = Get();
    const bool oldValid = s.renderModeValid, oldMode = s.renderMode;
    const uint32_t oldSerial = s.renderSerial;
    s.renderModeValid = QueuedMode(renderer, s.renderMode, s.renderSerial);
    // Chain the parent's already-installed 2D/UI reset/capture hook verbatim.
    s.previousFrame(renderer);
    s.pendingModeValid = false;
    s.renderModeValid = oldValid; s.renderMode = oldMode;
    s.renderSerial = oldSerial;
}
inline Batch* RegisterStandalone(uintptr_t token, const std::shared_ptr<void>& keepAlive, void* externalSource, uint32_t* sourceSlot) {
    auto& s = Get();
    if (!s.enabled || !s.building || !s.pendingMode || !token) return nullptr;
    try {
        auto batch = std::make_unique<Batch>();
        batch->plan.jobs.push_back(BucketJob{0, token, keepAlive});
        batch->packets.resize(1); batch->sources.resize(1);
        batch->sourceSlots.resize(1, sourceSlot); batch->sourceRetired.resize(1); batch->copies.resize(1);
        batch->copied.resize(1); batch->rendered.resize(1);
        Encode(batch->packets[0], s.serial, static_cast<uint32_t>(s.building->batches.size()), 0);
        batch->sources[0] = externalSource ? externalSource : &batch->packets[0];
        auto* accepted = batch.get();
        s.building->batches.push_back(std::move(batch));
        ++s.building->jobs;
        return accepted;
    } catch (...) { return nullptr; }
}
inline bool BuildCallPatch(Patch& patch, uintptr_t address, uintptr_t expectedTarget, void* replacement) {
    auto* bytes = reinterpret_cast<uint8_t*>(address);
    if (!Accessible(bytes, 5) || bytes[0] != 0xe8 ||
        address + 5 + Read<int32_t>(bytes, 1) != expectedTarget) return false;
    patch.address = bytes; patch.size = 5; std::memcpy(patch.original.data(), bytes, 5);
    patch.replacement[0] = 0xe8;
    Write<int32_t>(patch.replacement.data(), 1, static_cast<int32_t>(reinterpret_cast<uintptr_t>(replacement) - address - 5));
    return true;
}
inline bool BuildSlotPatch(Patch& patch, uintptr_t address, void* expected, void* replacement) {
    if (!Accessible(reinterpret_cast<void*>(address), 4) || Read<void*>(reinterpret_cast<void*>(address)) != expected) return false;
    patch.address = reinterpret_cast<void*>(address); patch.size = 4;
    std::memcpy(patch.original.data(), &expected, 4); std::memcpy(patch.replacement.data(), &replacement, 4); return true;
}
inline bool WritePatch(const Patch& patch, bool install) {
    const auto& bytes = install ? patch.replacement : patch.original;
    DWORD protection = 0, ignored = 0;
    if (!VirtualProtect(patch.address, patch.size, PAGE_EXECUTE_READWRITE, &protection)) return false;
    std::memcpy(patch.address, bytes.data(), patch.size);
    FlushInstructionCache(GetCurrentProcess(), patch.address, patch.size);
    VirtualProtect(patch.address, patch.size, protection, &ignored); return true;
}
} // detail

// The caller must already have verified the exact EXE SHA256 recorded in the
// native-model-contract.json. These local instruction guards additionally catch
// conflicting patches. expectedFrameHook is the parent's current frameHook;
// nullptr requires the unmodified native frame handler.
inline bool Install(re3exe::Base gameBase, const Callbacks& callbacks, void* expectedFrameHook = nullptr) {
    auto& s = detail::Get(); if (s.installed) return false;
    if (!gameBase || !callbacks.beginBuild || !callbacks.prepareActor || !callbacks.drawJob ||
        !detail::Accessible(reinterpret_cast<void*>(gameBase.value), 0x1000)) return false;
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(gameBase.value);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < 0 || dos->e_lfanew > 0x800) return false;
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(gameBase + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->FileHeader.Machine != IMAGE_FILE_MACHINE_I386 ||
        (nt->OptionalHeader.SizeOfImage != 0x667000 && !re3exe::gSteam)) return false;   // Steam: the protector's header
    if (!expectedFrameHook) expectedFrameHook = reinterpret_cast<void*>(gameBase + 0xb9c0);
    auto& p = s.patches;
    const bool guards =
        detail::BuildCallPatch(p[0], gameBase + 0x2ba96, gameBase + 0x26730, reinterpret_cast<void*>(detail::BeginHook)) &&
        detail::BuildCallPatch(p[1], gameBase + 0x2bba6, gameBase + 0x357d0, reinterpret_cast<void*>(detail::ActorHook)) &&
        detail::BuildCallPatch(p[2], gameBase + 0x3583c, gameBase + 0x358f0, reinterpret_cast<void*>(detail::PartHook)) &&
        detail::BuildCallPatch(p[3], gameBase + 0x358d4, gameBase + 0x358f0, reinterpret_cast<void*>(detail::PartHook)) &&
        detail::BuildCallPatch(p[4], gameBase + 0x1facb, gameBase + 0x1fe70, reinterpret_cast<void*>(detail::CopyHook)) &&
        detail::BuildSlotPatch(p[5], gameBase + 0x10aa78 + 0x140, reinterpret_cast<void*>(gameBase + 0xca10), reinterpret_cast<void*>(detail::DrawHook)) &&
        detail::BuildSlotPatch(p[6], gameBase + 0x10aa78 + 0x20, expectedFrameHook, reinterpret_cast<void*>(detail::FrameHook));
    if (!guards) return false;
    const uintptr_t clearCalls[] = {0x23828,0x23838,0x23846,0x23875,0x2f99e,0x2f9aa,0x2fa3a,0x5aefc};
    const uintptr_t queueCalls[] = {0xbc73,0xbcc3,0x1f957,0x206c4};
    for (size_t i = 0; i < 8; ++i) if (!detail::BuildCallPatch(p[7+i], gameBase+clearCalls[i], gameBase+0x232e0, reinterpret_cast<void*>(detail::ClearOtHook))) return false;
    for (size_t i = 0; i < 4; ++i) if (!detail::BuildCallPatch(p[15+i], gameBase+queueCalls[i], gameBase+0x20760, reinterpret_cast<void*>(detail::QueueResetHook))) return false;
    s.base = gameBase; s.callbacks = callbacks;
    s.originalBegin = reinterpret_cast<detail::BeginFn>(gameBase + 0x26730);
    s.originalActor = reinterpret_cast<detail::ActorFn>(gameBase + 0x357d0);
    s.originalPart = reinterpret_cast<detail::PartFn>(gameBase + 0x358f0);
    s.originalCopy = reinterpret_cast<detail::CopyFn>(gameBase + 0x1fe70);
    s.originalDraw = reinterpret_cast<detail::DrawFn>(gameBase + 0xca10);
    s.previousFrame = reinterpret_cast<detail::FrameFn>(expectedFrameHook);
    s.originalClearOt = reinterpret_cast<detail::ClearOtFn>(gameBase + 0x232e0);
    s.originalQueueReset = reinterpret_cast<detail::QueueResetFn>(gameBase + 0x20760);
    for (size_t i = 0; i < p.size(); ++i) if (!detail::WritePatch(p[i], true)) {
        while (i) detail::WritePatch(p[--i], false);
        detail::Message("Native3D: hook installation failed and earlier patches were rolled back."); return false;
    }
    s.installed = true;
    detail::Message("Native3D: guarded build/copy/draw, chained frame, OT-clear and command-queue retirement hooks installed.");
    return true;
}
inline bool Installed() { return detail::Get().installed; }
inline void SetReplaying(bool replaying) { detail::Get().replaying = replaying; }
inline Stats GetStats() { return detail::Get().stats; }
inline void SetEnabled(bool enabled) { detail::Get().enabled = enabled; }
// During the chained parent frame hook, skip its ordinary pollToggle when true.
// Use this exact value for both 2D and UI mode. A press after build waits for the
// next build; when false (e.g. UI-only frame), parent's usual polling is valid.
inline bool GetRenderMode(bool& mode) {
    const auto& s = detail::Get(); if (!s.renderModeValid) return false;
    mode = s.renderMode; return true;
}
inline uint32_t BuildSerial() { return detail::Get().serial; }
inline uint32_t RenderSerial() { return detail::Get().renderSerial; }
// For door/other independent native render loops only: call once BEFORE their
// first replacement is prepared, at a verified build-start boundary. Do not call
// once per part or per packet. The normal actor loop already calls BeginBuild.
inline void BeginExternalBuild(uint32_t* modeMarkerSlot = nullptr) {
    detail::BeginBuild();
    if (modeMarkerSlot) detail::PublishModeMarker(modeMarkerSlot);
}
// Event programs with their own frame loop (12 via 4D80A0, 15 = 43E5E0, 16 = 43DF50: puzzle/close-up screens) draw
// room objects (45A040) and effects but never reach the gameplay build start 42BA96. Call before preparing such a
// replacement: when no build started since this native frame's main OT clear, starts one exactly like BeginHook.
inline bool EnsureFrameBuild() {
    auto& s = detail::Get();
    if (!s.installed || s.otFrames == s.buildOtFrame) return false;
    auto* mainOT = detail::Read<uint32_t*>(reinterpret_cast<void*>(s.base + 0x65c7a4));
    BeginExternalBuild(mainOT && detail::Accessible(mainOT, (kMaxBucket + 1) * sizeof(uint32_t), true) ? mainOT + kMaxBucket : nullptr);
    ++s.stats.frameBuilds; return true;
}
// A figure drawn by its own routine (Mercenaries PLAYER SELECT 447E50, see Snapshot) whose part calls reach
// detail::PartHook: same plan, part suppression and OT insertion as the actor loop.
inline void DrawFigure(detail::ActorFn original, void* actor, void* parts, uint32_t flags, const void* position) {
    detail::DrawActor(original, true, actor, parts, flags, position);
}
inline bool BuildModeIsRemastered() {
    const auto& s = detail::Get(); return s.enabled && s.building && s.pendingMode;
}
// After a native constructor has emitted a SINGLE 40-byte command, replace its
// payload in place. Registration/allocation must finish before changing bytes;
// failure preserves the original packet. The caller proves the 40-byte extent,
// low link bits == 3, exact asset identity, and successful original emission.
inline bool ReplacePacket(void* packet40, uintptr_t token, const std::shared_ptr<void>& keepAlive = {}, uint32_t* sourceSlot = nullptr) {
    if (!detail::Accessible(packet40, sizeof(detail::Packet), true) ||
        (detail::Read<uint32_t>(packet40) & 3u) != 3u || detail::Signature(packet40)) return false;
    const uint8_t command = detail::Read<uint8_t>(packet40, 7) & 0xfcu;
    if (command != 0x2c && command != 0x34) return false; // Native FT4 / GT3 are exactly 40 bytes.
    if (sourceSlot && (!detail::Accessible(sourceSlot, 4, true) ||
        (*sourceSlot & ~3u) != reinterpret_cast<uintptr_t>(packet40))) return false;
    auto* batch = detail::RegisterStandalone(token, keepAlive, packet40, sourceSlot);
    if (!batch) return false;
    const uint32_t link = detail::Read<uint32_t>(packet40);
    std::memcpy(packet40, &batch->packets[0], sizeof(detail::Packet));
    detail::Write<uint32_t>(packet40, 0, link);
    ++detail::Get().stats.packetsInserted;
    return true;
}
// The caller supplies an already verified native OT SLOT, not an OT base or
// byte offset. Intended for loops whose depth mapping differs from actor OTZ.
inline bool EnqueueAbsolute(uint32_t* orderingTableSlot, uintptr_t token, const std::shared_ptr<void>& keepAlive = {}) {
    if (!detail::Accessible(orderingTableSlot, sizeof(uint32_t), true)) return false;
    auto* batch = detail::RegisterStandalone(token, keepAlive, nullptr, orderingTableSlot);
    if (!batch) return false;
    detail::Write<uint32_t>(&batch->packets[0], 0, *orderingTableSlot | 3u);
    *orderingTableSlot = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&batch->packets[0]));
    ++detail::Get().stats.packetsInserted;
    return true;
}
using AbsoluteJob = std::pair<uint32_t*, BucketJob>;
// For independent native ordering maps (e.g. doors), every absolute slot is
// caller-verified. Nothing is published until all allocation/registration ends.
inline bool EnqueueAbsoluteBatch(const std::vector<AbsoluteJob>& jobs) {
    auto& s = detail::Get();
    if (!s.enabled || !s.building || !s.pendingMode || jobs.empty() || jobs.size() > kMaxJobsPerActor) return false;
    uintptr_t checkedPage = 0;   // one memory query per page of slots, not per slot
    for (const auto& entry : jobs) {
        if (!entry.second.token) return false;
        const uintptr_t page = reinterpret_cast<uintptr_t>(entry.first) & ~uintptr_t(0xfff);
        if (page != checkedPage) { if (!detail::Accessible(entry.first, sizeof(uint32_t), true)) return false; checkedPage = page; }
    }
    detail::Batch* accepted = nullptr;
    try {
        auto batch = std::make_unique<detail::Batch>();
        const size_t count = jobs.size();
        batch->plan.jobs.reserve(count);
        for (const auto& entry : jobs) batch->plan.jobs.push_back(entry.second);
        batch->packets.resize(count); batch->sources.resize(count);
        batch->sourceSlots.resize(count); batch->sourceRetired.resize(count); batch->copies.resize(count);
        batch->copied.resize(count); batch->rendered.resize(count);
        const auto index = static_cast<uint32_t>(s.building->batches.size());
        for (uint32_t i = 0; i < count; ++i) {
            detail::Encode(batch->packets[i], s.serial, index, i);
            batch->sources[i] = &batch->packets[i];
            batch->sourceSlots[i] = jobs[i].first;
        }
        accepted = batch.get();
        s.building->batches.push_back(std::move(batch)); // Last throwing operation.
        s.building->jobs += count;
    } catch (...) { return false; }
    for (size_t i = 0; i < jobs.size(); ++i) {
        detail::Write<uint32_t>(&accepted->packets[i], 0, *jobs[i].first | 3u);
        *jobs[i].first = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&accepted->packets[i]));
    }
    s.stats.packetsInserted += jobs.size();
    return true;
}
// Native actor/prop OT accepts at most 1024 entries. Explicit slotCount also
// permits smaller verified maps; a batch with any out-of-range index is intact.
inline bool EnqueueBatch(uint32_t* orderingTable, uint32_t slotCount, const std::vector<BucketJob>& jobs) {
    if (!orderingTable || !slotCount || slotCount > kMaxBucket + 1 || jobs.empty() || jobs.size() > kMaxJobsPerActor) return false;
    try {
        std::vector<AbsoluteJob> absolute; absolute.reserve(jobs.size());
        for (const auto& job : jobs) {
            if (job.bucket >= slotCount) return false;
            absolute.emplace_back(orderingTable + job.bucket, job);
        }
        return EnqueueAbsoluteBatch(absolute);
    } catch (...) { return false; }
}
// Only call after all native queues/OTs are discarded or process is shutting
// down. Disabling via SetEnabled(false) keeps pending HD commands alive.
inline void ReleaseDiscardedQueues() {
    auto& s = detail::Get();
    if (s.activeHooks) {++s.stats.retirementDeferred; return;}
    for (auto& frame : s.frames) detail::Release(*frame);
    s.frames.clear(); s.building = nullptr;
    s.pendingModeValid = false; s.renderModeValid = false;
}
} // namespace re3bridge
