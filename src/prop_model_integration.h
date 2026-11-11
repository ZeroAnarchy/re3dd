#pragma once
#include "chest_occlusion.h"
#include "prop_table.h"
#include "prop_native_blend.h"

// Included after model_integration.h. v14: every RDT object part the object renderer 45A040 draws through 45A1B0 ->
// CALL 45A1F1 -> 437DC0 can be replaced from model-pack/props.txt (prop_table.h): om_set objects, item pickups
// (item_aot_set md1 = RDT object index) and the save-room chest lid. Object index = (object - A5EA90) / 0x194 (45A066
// direct array; om_set 46A71E fills slot md1 from RDT model [RDT+30][md1*8+4], 46A861); part index = (part -
// object+C4) / 0xBC (4589E0/458AB0: part n draws mesh record object+14 + n*0x18, kept at part+8). om_set type 1
// (+4A == 1, tiled 4E3860) never reaches 437DC0. The native scripts, matrices, visibility (45A1B0 culling, part
// flag 1), animation and pickup removal stay native: a part the game does not draw is never drawn here.
// Static evidence: runtime/static3d/prop-adapter-contract.txt, PROPS-V14.ru.txt.
namespace re3prop {
using PartFn = void (__cdecl*)(void*, void*, uint32_t, const re3bridge::Matrix32*);
static PartFn originalPart = nullptr;
static re3bridge::detail::Patch partPatch{};
static bool installed = false;
static constexpr uint32_t kOrderingTableSlots = 1024;
static constexpr uint32_t kObjectStride = 0x194, kMaxObjects = 64;
static Table table;
static DoorFits doorFits;   // door round 2026-11-11: model-pack/door-fit.txt
static std::unordered_set<std::string> tableAssets;
static bool traceParts = false; // debug_warp: MODEL_PROP_SEEN once for every distinct drawn part (table work)

inline uint32_t currentRoom() {
    return (uint32_t(re3bridge::detail::Read<uint16_t>(reinterpret_cast<void*>(gameBase + 0x661fa6))) << 8) |
           (re3bridge::detail::Read<uint16_t>(reinterpret_cast<void*>(gameBase + 0x661fa8)) & 0xffu);
}
inline std::string roomText(uint32_t room) { char text[8]; sprintf_s(text, "R%X%02X", ((room >> 8) + 1) & 0xf, room & 0xff); return text; }
inline std::string identityText(const Identity& id) {
    char text[48]; sprintf_s(text, "%u/%u/%u:%016llx", id.vertices, id.triangles, id.quads, static_cast<unsigned long long>(id.hash)); return text;
}
inline bool noteOnce(const std::string& key) { return modelObserved.size() < 6000 && modelObserved.insert("prop:" + key).second; }
// Counts + vertex hash of a native 24-byte mesh record (RDT object part+8, DO2 door object+14).
inline bool nativeIdentity(const void* mesh, Identity& out) {
    using namespace re3bridge;
    if (!detail::Accessible(mesh, 24)) return false;
    out.vertices = detail::Read<uint16_t>(mesh, 8); out.triangles = detail::Read<uint16_t>(mesh, 20); out.quads = detail::Read<uint16_t>(mesh, 22);
    const auto* vertices = detail::Read<const uint8_t*>(mesh, 0);
    if (!out.vertices || out.vertices > 4096 || !detail::Accessible(vertices, size_t(out.vertices) * 8)) return false;
    out.hash = VertexHash(vertices, out.vertices); return true;
}

// ---------------------------------------------------------------- memory (32-bit process)
// Prop assets are cheap meshes with 1-2k textures (VRAM, DEFAULT pool) but there are dozens per game. On every room
// change a prop asset not drawn in this or the previous room (and by nothing else for 300 builds) is released (a
// quick return through a door finds its props still loaded); all prop assets together keep under
// kPropTextureBudget (least recently drawn first). An asset in use (shared_ptr held) or drawn this build is never
// released; queued GPU frames AddRef their textures. getModel's global 128 MB / 768 MB budget still applies.
static constexpr size_t kPropTextureBudget = 384u * 1024 * 1024; // R30C alone needs ~176 MiB (19 assets)
static uint32_t lastRoom = ~0u, roomGeneration = 0;
static size_t lastAssetCount = 0;
static std::unordered_map<std::string, uint32_t> assetRoom;
inline void noteUse(const std::string& id) { assetRoom[id] = roomGeneration; }
inline void evictProps(bool roomChanged) {
    if (roomChanged) for (auto it = modelAssets.begin(); it != modelAssets.end();) {
        const auto used = assetRoom.find(it->first);
        // also drawn elsewhere lately (a v9 weapon LOD bound as a floor pickup is also the weapon in Jill's hand)
        if (!tableAssets.count(it->first) || it->second->missing || it->second.use_count() != 1 ||
            (used != assetRoom.end() && roomGeneration - used->second < 2) || re3bridge::BuildSerial() - it->second->lastUsed <= 300) { ++it; continue; }
        log("MODEL_PROP_UNLOAD %s room=%s texture_bytes=%zu bytes=%zu (entering this room; not drawn here or in the previous room)", it->first.c_str(),
            roomText(lastRoom).c_str(), it->second->textureBytes, it->second->bytes);
        assetRoom.erase(it->first); it = modelAssets.erase(it);
    }
    for (;;) {
        size_t propBytes = 0; auto victim = modelAssets.end();
        for (auto it = modelAssets.begin(); it != modelAssets.end(); ++it) {
            if (!tableAssets.count(it->first) || it->second->missing) continue;
            propBytes += it->second->textureBytes + it->second->bytes;
            if (it->second.use_count() == 1 && it->second->lastUsed != re3bridge::BuildSerial() &&
                (victim == modelAssets.end() || it->second->lastUsed < victim->second->lastUsed)) victim = it;
        }
        if (propBytes <= kPropTextureBudget || victim == modelAssets.end()) break;
        log("MODEL_PROP_EVICT %s texture_bytes=%zu props_total=%zu budget=%zu", victim->first.c_str(), victim->second->textureBytes, propBytes, kPropTextureBudget);
        assetRoom.erase(victim->first); modelAssets.erase(victim);
    }
}
// Static props reuse their projected CPU geometry: RE3 cameras are fixed and most objects do not move, so a part
// whose asset, final matrix, projection, viewport, native lighting and proxy depth are byte-identical to the last
// frame skips skinning and projection and is only re-uploaded (the stream pages are per frame). Cleared per room.
struct PropGeometry { std::weak_ptr<ModelAsset> asset; std::vector<uint8_t> key; re3model::FrameGeometry geometry; uint32_t serial = 0; };
static std::unordered_map<uint32_t, PropGeometry> propGeometry;
static uint64_t propCacheHits = 0, propCacheMisses = 0;
template <class T> inline void keyBytes(std::vector<uint8_t>& key, const T& value) {
    const auto* b = reinterpret_cast<const uint8_t*>(&value); key.insert(key.end(), b, b + sizeof(T));
}
inline void housekeeping() {
    if (table.empty()) return;
    const uint32_t room = currentRoom(); const bool changed = room != lastRoom;
    if (changed) {
        if (propCacheHits + propCacheMisses)
            log("MODEL_PROP_CACHE room=%s parts=%zu hits=%llu misses=%llu", roomText(lastRoom).c_str(), propGeometry.size(),
                static_cast<unsigned long long>(propCacheHits), static_cast<unsigned long long>(propCacheMisses));
        propGeometry.clear(); propCacheHits = propCacheMisses = 0;
        lastRoom = room; ++roomGeneration;
    }
    if (!changed && modelAssets.size() == lastAssetCount) return;
    evictProps(changed); lastAssetCount = modelAssets.size();
    if (changed) {
        size_t loaded = 0, bytes = 0;
        for (const auto& entry : modelAssets) if (tableAssets.count(entry.first) && !entry.second->missing) { ++loaded; bytes += entry.second->textureBytes + entry.second->bytes; }
        if (loaded) log("MODEL_PROP_ROOM room=%s props_loaded=%zu bytes=%zu", roomText(room).c_str(), loaded, bytes);
    }
}

// ---------------------------------------------------------------- RDT object parts
static bool preparePart(void* object, void* part, uint32_t attributes, const re3bridge::Matrix32* world) {
    using namespace re3bridge;
    if (!modelActive || modelFatal || modelParity || !gameDevice || !gogWrapper || !BuildModeIsRemastered()) return false;
    // Cheap identity first (this runs for every drawn part): A5EA90 is the array itself, not a pointer.
    const uintptr_t base = gameBase + 0x65ea90, at = reinterpret_cast<uintptr_t>(object);
    if (at < base || (at - base) % kObjectStride || (at - base) / kObjectStride >= kMaxObjects) return false;
    const uint32_t index = uint32_t((at - base) / kObjectStride);
    auto* parts = detail::Read<uint8_t*>(object, 0xc4);
    const uintptr_t partAt = reinterpret_cast<uintptr_t>(part), partsAt = reinterpret_cast<uintptr_t>(parts);
    if (!parts || partAt < partsAt || (partAt - partsAt) % kPartStride) return false;
    const uint32_t partIndex = uint32_t((partAt - partsAt) / kPartStride);
    if (partIndex >= detail::Read<uint8_t>(object, 8) ||
        world != reinterpret_cast<const Matrix32*>(static_cast<uint8_t*>(part) + 0x40)) return false;
    const uint32_t room = currentRoom();
    const auto exact = table.objects.find(ObjectKey(room, index, partIndex));
    const auto any = table.anyRoom.find(partIndex);
    const bool tracing = traceParts && modelObserved.size() < 6000;
    if (exact == table.objects.end() && any == table.anyRoom.end() && !tracing) return false;
    if (!detail::Accessible(part, kPartStride)) return false;
    Identity identity;
    if (!nativeIdentity(detail::Read<const void*>(part, 8), identity)) return false;
    if (tracing) {
        char key[96]; sprintf_s(key, "seen:%x:%u:%u:%s", room, index, partIndex, identityText(identity).c_str());
        if (modelObserved.insert(key).second)
            log("MODEL_PROP_SEEN room=%s object=%u part=%u identity=%s attributes=%x flags=%x", roomText(room).c_str(), index, partIndex,
                identityText(identity).c_str(), attributes, detail::Read<uint32_t>(part));
    }
    const Rule* rule = Match(exact == table.objects.end() ? nullptr : &exact->second, identity);
    if (!rule) rule = Match(any == table.anyRoom.end() ? nullptr : &any->second, identity);
    char where[64]; sprintf_s(where, "room=%s object=%u part=%u", roomText(room).c_str(), index, partIndex);
    if (!rule) {
        if (exact != table.objects.end() && noteOnce(std::string("mismatch:") + where + identityText(identity)))
            log("MODEL_PROP_MISMATCH %s asset=%s expected=%s native=%s; native part kept", where, exact->second.front().asset.c_str(),
                identityText(exact->second.front().identity).c_str(), identityText(identity).c_str());
        return false;
    }
    const uint32_t flags = detail::Read<uint32_t>(part);
    auto fallback = [&](const char* reason) {
        if (noteOnce(std::string("fallback:") + where + reason))
            log("MODEL_PROP_FALLBACK %s asset=%s reason=%s attributes=%x flags=%x", where, rule->asset.c_str(), reason, attributes, flags);
        return false;
    };
    // The native caller already passed object/part visibility gates. Keep the special flat/shadow route (20),
    // parts without a light refresh (200) and dynamically scaled parts (part flag 4, 5072E0) on the native path.
    // Semi-transparent parts (100: 435DF0 emits GT3 36) require an explicit STP map on every HD material below.
    if (!(flags & 1) || !(attributes & 2)) return false;
    if (flags & 4) return fallback("scaled-part");
    if (attributes & 0x20) return fallback("special-route-20");
    if (attributes & 0x200) return fallback("no-light-200");
    // 435DF0 orders into the CURRENT object's pOT (A5C8A4 +7C); 45A071 sets it to this object.
    if (detail::Read<void*>(reinterpret_cast<void*>(gameBase + 0x65c8a4)) != object) return fallback("not-current-object");
    if (rule->hide()) {
        try { if (noteOnce(std::string("hide:") + where)) log("MODEL_PROP_HIDE %s identity=%s (native part suppressed by the table)", where, identityText(identity).c_str()); } catch (...) {}
        return true;
    }
    auto* orderingTable = detail::Read<uint32_t*>(object, 0x7c);
    if (!orderingTable || !detail::Accessible(orderingTable, kOrderingTableSlots * sizeof(uint32_t), true)) return fallback("ordering-table");
    const auto* gte = detail::Read<const void*>(reinterpret_cast<void*>(gameBase + 0x1349c8));
    if (!detail::Accessible(gte, 0x100) || !modelRenderer.resetDevice(gameDevice)) return false;
    auto asset = getModel(rule->asset);
    if (!asset) return fallback("asset-unavailable");
    if (asset->mesh.inverseBind.size() != 1) return fallback("asset-not-rigid");
    const bool semiTransparent = (attributes & 0x100) != 0;
    unsigned nativeAbr = 0;
    if (semiTransparent) {
        // Never infer STP from HD albedo alpha: opaque texels and semi-transparent glass coexist in R40B.
        if (asset->textures.empty()) return fallback("semi-transparent-no-stp");
        for (const auto& texture : asset->textures) if (!texture.stp) return fallback("semi-transparent-no-stp");
        const void* mesh = detail::Read<const void*>(part, 8);
        const auto* triangles = detail::Read<const uint8_t*>(mesh, 12);
        const auto* quads = detail::Read<const uint8_t*>(mesh, 16);
        if ((identity.triangles && !detail::Accessible(triangles, size_t(identity.triangles) * 12)) ||
            (identity.quads && !detail::Accessible(quads, size_t(identity.quads) * 16))) return fallback("semi-transparent-mesh");
        if (!UniformNativeAbr(triangles, identity.triangles, quads, identity.quads, nativeAbr))
            return fallback("semi-transparent-mixed-abr");
    }
    re3chest::DepthState proxyDepth;
    ActorContext context;
    context.camera = detail::Read<Matrix32>(reinterpret_cast<void*>(gameBase + 0x11c398));
    std::memcpy(context.gte.data(), gte, context.gte.size());
    auto projection = actorProjection(context);
    if (!re3model::SetNativeProjectionRegisters(projection, context.gte.data(), context.gte.size())) return false;
    projection.bucketLimit = kOrderingTableSlots;
    if (rule->depth == Depth::Chest) {
        // Immutable loaded mesh identity is checked once per asset instance. Any changed geometry/index ordering
        // requires a regenerated proxy map. The native lid must be the exact 26-vertex proxy (chest_occlusion_data.h).
        static std::weak_ptr<ModelAsset> checkedAsset;
        static bool proxyIdentity = false;
        if (checkedAsset.lock().get() != asset.get()) {
            checkedAsset = asset;
            proxyIdentity = re3chest::ValidateMesh(asset->mesh);
            log("MODEL_CHEST_PROXY_IDENTITY valid=%u triangles=%zu geometryFNV=%016llx", proxyIdentity ? 1u : 0u,
                asset->mesh.indices.size()/3, static_cast<unsigned long long>(re3chest::data::kGeometryFnv));
        }
        if (!proxyIdentity) return fallback("chest-proxy-asset");
        const auto* native = detail::Read<const uint8_t*>(detail::Read<const void*>(part, 8), 0);
        if (identity.vertices != 26) return fallback("chest-proxy-native");
        for (unsigned v = 0; v < 26; ++v) for (unsigned k = 0; k < 3; ++k)
            if (detail::Read<int16_t>(native, v * 8 + k * 2) != re3chest::data::kProxyVertices[v][k]) return fallback("chest-proxy-native");
        if (!re3chest::PrepareDepthState(context.camera, *world, projection.zsf3, proxyDepth)) return false;
        projection.triangleDepth = re3chest::TriangleDepth;
        projection.triangleDepthContext = &proxyDepth;
    }
    re3model::NativeLighting lighting;
    if (!re3model::CaptureNativeLighting(lighting, reinterpret_cast<void*>(gameBase + 0x11c358),
            context.gte.data(), context.gte.size(), detail::Read<uint32_t>(part, 0x68))) return false;
    // props-bright 2026-11-07: opt-in per row ('ambient=g'): the native ambient (GTE BK, +44) x g for this part only. In game
    // (R10D intro leaves, R218 hose card) the HD part lit by the captured native light is 0.3-0.4 of the native brightness
    // where only the ambient reaches it (BK 1024 = 0.25 in this formula) while the native part renders about 1.0 x its
    // texel; parts that face a light match (R100 key). Every row without the option keeps the native light exactly.
    if (rule->ambient != 1.f)
        for (auto& a : lighting.ambient) a = std::int32_t((std::max)(-2147483000.0, (std::min)(2147483000.0, double(a) * rule->ambient)));
    projection.shade = re3model::ShadeNativeNccs;
    projection.shadeContext = &lighting; // PrepareFrame bakes colors synchronously.
    // Mesh vertices are fitted to native part-local coordinates (of native_ref; xf carries them onto this variant);
    // the original part matrix supplies all motion (lid hinge, statue arm, gears), with no second animation clock.
    const auto native = re3model::FromNativeMatrix(*world);
    const auto bone = rule->transformed ? re3model::Multiply(native, rule->transform) : native;
    // crystals round 2026-11-07 (debug_warp only): the part matrix once, for authoring xf rows (scale about a resting point)
    if (tracing && noteOnce(std::string("matrix:") + where)) {
        const float* m = native.m;
        log("MODEL_PROP_MATRIX %s asset=%s native=%.5f,%.5f,%.5f,%.2f,%.5f,%.5f,%.5f,%.2f,%.5f,%.5f,%.5f,%.2f", where, rule->asset.c_str(),
            m[0], m[1], m[2], m[3], m[4], m[5], m[6], m[7], m[8], m[9], m[10], m[11]);
    }
    // Opaque props take the GPU path: the static mesh and one bone. The chest (its proxy maps triangles to buckets one by
    // one) and semi-transparent parts (STP passes) stay on the cached CPU geometry below.
    if (!semiTransparent && rule->depth != Depth::Chest) {
        ActorPlan gpuPlan; gpuPlainLight = true;
        const int gpu = appendGpuJobs(asset, {bone}, projection, gpuPlan, nullptr, nullptr, nullptr); gpuPlainLight = false;
        if (gpu == 0) return fallback("prepare");
        if (gpu > 0) {
            if (!gpuPlan.jobs.empty() && !EnqueueBatch(orderingTable, kOrderingTableSlots, gpuPlan.jobs)) return fallback("enqueue");
            try {
                noteUse(rule->asset);
                if (noteOnce(std::string("ok:") + where + rule->asset))
                    log("MODEL_PROP %s asset=%s identity=%s depth=triangle xf=%u ambient=%.2f attributes=%x semi=0 abr=0 jobs=%zu triangles=%zu OT=%p gpu", where, rule->asset.c_str(), identityText(identity).c_str(),
                        rule->transformed ? 1u : 0u, double(rule->ambient), attributes, gpuPlan.jobs.size(), asset->mesh.indices.size() / 3, orderingTable);
            } catch (...) {}
            return true;
        }
    }
    std::vector<uint8_t> key; key.reserve(320);
    keyBytes(key, asset.get()); keyBytes(key, rule); keyBytes(key, bone); keyBytes(key, projection.camera);
    keyBytes(key, projection.h); keyBytes(key, projection.offsetX); keyBytes(key, projection.offsetY); keyBytes(key, projection.zsf3);
    keyBytes(key, projection.scaleX); keyBytes(key, projection.scaleY); keyBytes(key, projection.pixelOffsetX); keyBytes(key, projection.pixelOffsetY);
    keyBytes(key, projection.nearPlane); keyBytes(key, projection.mode); keyBytes(key, projection.perspectiveCorrect);
    keyBytes(key, lighting.worldLight); keyBytes(key, lighting.color); keyBytes(key, lighting.ambient); keyBytes(key, lighting.baseRGB);
    if (rule->depth == Depth::Chest) keyBytes(key, proxyDepth.buckets);
    auto& cached = propGeometry[ObjectKey(room, index, partIndex)];
    const bool hit = cached.asset.lock() == asset && cached.key == key && !cached.geometry.vertices.empty();
    if (hit) ++propCacheHits;
    else {
        ++propCacheMisses; cached = PropGeometry{};
        std::vector<re3model::SkinnedVertex> skinned; std::string error;
        if (!re3model::Skin(asset->mesh, {bone}, skinned, &error) || !re3model::PrepareFrame(asset->mesh, skinned, projection, cached.geometry, &error) ||
            cached.geometry.vertices.empty()) {
            cached = PropGeometry{};
            if (noteOnce(std::string("prepare-failed:") + rule->asset)) log("MODEL_PREPARE_FAILED %s %s", rule->asset.c_str(), error.c_str());
            return fallback("prepare");
        }
        cached.asset = asset; cached.key = std::move(key);
    }
    cached.serial = BuildSerial();
    HRESULT hr; auto gpu = modelRenderer.upload(asset->mesh, cached.geometry, asset->textures, semiTransparent, &hr);
    if (!gpu) { log("MODEL_UPLOAD_FAILED %s hr=%08lx", rule->asset.c_str(), hr); return fallback("upload"); }
    ActorPlan plan;
    for (size_t n = 0; n < gpu->slices.size(); ++n) {
        auto job = std::make_shared<ModelJob>(); job->geometry = gpu; job->slice = n; job->name = rule->asset;
        job->options.nativeSemiTransparent = semiTransparent; job->options.abr = nativeAbr;
        plan.jobs.push_back({gpu->slices[n].bucket, reinterpret_cast<uintptr_t>(job.get()), job});
    }
    if (plan.jobs.empty()) return fallback("prepare");
    if (rule->depth == Depth::Chest) {
        static int lastAngle=0;static unsigned capturedPhases=0;
        const int angle=std::abs(int(detail::Read<int16_t>(object,0x70)));
        if(!angle)capturedPhases=0;
        unsigned phase=0;if(angle>=256&&angle>lastAngle)phase=1;if(angle>=768)phase=2;if(angle>=128&&angle<lastAngle)phase=4;
        if(phase&&!(capturedPhases&phase)){capturedPhases|=phase;reinterpret_cast<ModelJob*>(plan.jobs[0].token)->captureTag=phase==1?"CHEST-opening":phase==2?"CHEST-open":"CHEST-closing";}
        lastAngle=angle;
    }
    // This single operation validates/registers ALL jobs before publishing any OT pointers. On failure the
    // original function remains safe to call.
    if (!EnqueueBatch(orderingTable, kOrderingTableSlots, plan.jobs)) return fallback("enqueue");
    // After commit no diagnostic allocation may make the caller fall back and draw the original on top of
    // already-enqueued replacement geometry.
    try {
        noteUse(rule->asset);
        if (noteOnce(std::string("ok:") + where + rule->asset))
            log("MODEL_PROP %s asset=%s identity=%s depth=%s xf=%u attributes=%x semi=%u abr=%u jobs=%zu triangles=%zu OT=%p", where, rule->asset.c_str(), identityText(identity).c_str(),
                rule->depth == Depth::Chest ? "chest" : "triangle", rule->transformed ? 1u : 0u, attributes, semiTransparent ? 1u : 0u, nativeAbr, plan.jobs.size(), cached.geometry.vertices.size() / 3, orderingTable);
    } catch (...) {}
    return true;
}

static void __cdecl partHook(void* object, void* part, uint32_t attributes,
                             const re3bridge::Matrix32* world) {
    bool accepted = false;
    {
        std::lock_guard<std::recursive_mutex> guard(mutex);
        try { ensureFrameBuild(); housekeeping(); accepted = preparePart(object, part, attributes, world); }
        catch (...) { accepted = false; }
    }
    if (!accepted) originalPart(object, part, attributes, world);
}

// Movies (proxy.cpp movieHook, renderer vtable +30 = 40F080 -> 411DB0 open): the 2 GB non-LAA process needs ~373 MB
// of free address space while an HD movie plays (video-v14 harness: original 320x160 +308 MB, HD H.264 +373 MB).
// Nothing is drawn during a movie, so before the open the caller may release what only speeds up the next frames:
// level 1 = prop geometry cache, unreferenced prop assets, idle model stream pages; level 2 = also every other
// unreferenced model asset (characters reload on their next frame). Quality is unchanged. Returns a summary.
inline std::string trimForMovie(int level) {
    size_t props = 0, assets = 0, bytes = 0, geometry = propGeometry.size();
    propGeometry.clear();
    for (auto it = modelAssets.begin(); it != modelAssets.end();) {
        const bool prop = tableAssets.count(it->first) != 0;
        if (it->second.use_count() != 1 || it->second->missing || (!prop && level < 2)) { ++it; continue; }
        bytes += it->second->bytes + it->second->textureBytes; (prop ? props : assets)++;
        assetRoom.erase(it->first); it = modelAssets.erase(it);
    }
    lastAssetCount = modelAssets.size();
    const size_t stream = modelRenderer.trimStream();
    char text[160]; sprintf_s(text, "level=%d prop_parts=%zu prop_assets=%zu other_assets=%zu asset_bytes=%zu stream_bytes=%zu", level, geometry, props,
        assets, bytes, stream);
    return text;
}

// Door rows (extra_model_integration.h): the rule for DO2 <do2>, mesh record <part> whose identity matches the live
// mesh; nullptr = keep the native door part. reason receives why.
inline const Rule* doorRule(uint32_t do2, uint32_t part, const void* mesh, Identity& identity, const char*& reason) {
    const auto found = table.doors.find(DoorKey(do2, part));
    if (found == table.doors.end()) { reason = "no-hd-asset"; return nullptr; }
    if (!nativeIdentity(mesh, identity)) { reason = "mesh-unreadable"; return nullptr; }
    const Rule* rule = Match(&found->second, identity);
    reason = rule ? "ok" : "identity-mismatch";
    return rule;
}
inline void loadTable() {
    std::ifstream in(root + "/model-pack/props.txt");
    if (!in) { log("MODEL_PROP_TABLE missing model-pack/props.txt: all props and doors stay native"); return; }
    ParseTable(in, table);
    tableAssets.clear(); for (const auto& id : table.assets) tableAssets.insert(id);
    size_t objectRows = 0, anyRows = 0, doorRows = 0;
    for (const auto& e : table.objects) objectRows += e.second.size();
    for (const auto& e : table.anyRoom) anyRows += e.second.size();
    for (const auto& e : table.doors) doorRows += e.second.size();
    log("MODEL_PROP_TABLE rows=%u object=%zu any_room=%zu door=%zu assets=%zu invalid=%u first_invalid_line=%u", table.rows, objectRows, anyRows,
        doorRows, table.assets.size(), table.invalid, table.firstInvalid);
    // door round 2026-11-11: optional handle placement fits (prop_table.h DoorFit, extra_model_integration.h prepareDoor)
    std::ifstream fits(root + "/model-pack/door-fit.txt");
    if (fits) { ParseDoorFits(fits, doorFits);
        log("MODEL_DOOR_FIT_TABLE rows=%u keys=%zu invalid=%u first_invalid_line=%u", doorFits.count, doorFits.rows.size(), doorFits.invalid, doorFits.firstInvalid); }
}
} // namespace re3prop

static void installPropModels() {
    using namespace re3prop;
    if (installed || !modelActive || !re3bridge::Installed()) return;
    loadTable();
    traceParts = GetPrivateProfileIntA("pilot", "debug_warp", 0, (root + "\\pilot.ini").c_str()) != 0;
    originalPart = reinterpret_cast<PartFn>(gameBase + 0x37dc0);
    // Exact E8 call45A1F1 ->437DC0; never overwrite an unknown/conflicting hook.
    if (!re3bridge::detail::BuildCallPatch(partPatch, gameBase + 0x5a1f1,
            gameBase + 0x37dc0, reinterpret_cast<void*>(partHook)) ||
        !re3bridge::detail::WritePatch(partPatch, true)) {
        log("MODEL_PROP_INSTALL_FAILED keeping original room objects");
        return;
    }
    installed = true;
    log("MODEL_PROP_INSTALL_OK part hook 45A1F1 table rows=%u trace=%u OTslots=%u texture_budget=%zu", table.rows, traceParts ? 1u : 0u,
        kOrderingTableSlots, kPropTextureBudget);
}
