#pragma once
#include <deque>

// Included AFTER model_integration.h helpers in the same x86 translation unit.
// Exact-build door and spent-case render adapters; native scripts, flight,
// collision, sounds and lifetime remain owned by the original game.
namespace re3extra {

using DoorBuild=void(__cdecl*)();
using DoorObjectBuild=void(__cdecl*)(void*,void*,void*);
using BillboardBuild=void(__cdecl*)(void*,void*,int,int,int,std::uint32_t*,int,int);
using AllocateEffect=void*(__cdecl*)();
using EffectLoop=void(__cdecl*)();
static DoorBuild originalDoorBuild=nullptr;
static DoorObjectBuild originalDoorObject=nullptr;
static BillboardBuild originalBillboard=nullptr;
static AllocateEffect originalAllocate=nullptr;
static EffectLoop originalEffectLoop=nullptr;
static bool installed=false,doorFrameAllowed=false;
static bool effectTrace=false;      // blood round 2026-11-07: pilot.ini effect_trace=1 (traceEffects)
static unsigned effectTraceLines=0;
static int doorLatched=-1; // DO2 id of the running door loop, latched at its first build; -1 = native door
static unsigned doorCaptureTick=0;

template<class T> inline T read(const void* p,std::size_t offset=0){return re3bridge::detail::Read<T>(p,offset);}
inline bool accessible(const void* p,std::size_t bytes,bool write=false){return re3bridge::detail::Accessible(p,bytes,write);}
inline void once(const std::string& key,const char* reason){if(modelObserved.insert("extra:"+key).second)log("MODEL_EXTRA %s %s",key.c_str(),reason);}
inline bool active(){return modelActive&&!modelParity&&!modelFatal&&gameDevice&&gogWrapper&&re3bridge::BuildModeIsRemastered();}

inline bool snapshotProjection(re3model::Projection& projection,bool cameraSpace=false){
    const auto gte=read<const void*>(reinterpret_cast<void*>(gameBase+0x1349c8));
    if(!accessible(gte,0x100))return false;
    re3bridge::ActorContext context{};
    context.camera=read<re3bridge::Matrix32>(reinterpret_cast<void*>(gameBase+0x11c398));
    std::memcpy(context.gte.data(),gte,context.gte.size());
    projection=actorProjection(context);
    if(!re3model::SetNativeProjectionRegisters(projection,context.gte.data(),context.gte.size()))return false;
    if(cameraSpace)projection.camera=re3model::Matrix3x4{};
    // Exact native lighting has a separate opt-in callback. These adapters use
    // the same initial neutral material/texture lighting as the actor pilot.
    projection.shade=nullptr;projection.shadeContext=nullptr;
    return true;
}

// v14: any DOORxx.DO2 (doors, ladders, gates, elevators: every transition of the door loop 4696D0), chosen by the file
// the loader actually read. 469E80 writes the hex digits (table 51BD24, upper case) of request byte +0C into
// "bio19/data/door/door00.do2" at 52AC34/35 and, when that file fails, loads DOOR00 and writes "00"; door init 469430
// copies the same request byte to A61F94. Both live in exe data. The request pointer *A5C8B8 does not: it points into
// the source room's script/AOT bytes, which the room loader (42C3CE -> 4684D0 -> 4264E0 into the shared RDT buffer)
// overwrites while this cooperative door loop is still drawing (static3d/door/identity-lifetime.txt), so it is never
// read. The v1 pilot gate (R100<->R101 destination bytes, DOOR01) is gone; the identity is latched once per door
// loop and every frame must still show the active bit 00400000 and the same file digits. A failed load (digits !=
// requested id) stays native: no HD door over the DOOR00 fallback merely from the request ID. Each door object then
// needs a props.txt door row for (DO2, mesh record +C2) whose identity matches its live mesh (+14).
inline int loadedDoorFile(){
    auto digit=[](char c){return c>='0'&&c<='9'?c-'0':c>='A'&&c<='F'?c-'A'+10:-1;};
    const int high=digit(read<char>(reinterpret_cast<void*>(gameBase+0x12ac34))),low=digit(read<char>(reinterpret_cast<void*>(gameBase+0x12ac35)));
    return high<0||low<0?-1:high*16+low;
}
inline int latchDoor(){
    if(!(read<std::uint32_t>(reinterpret_cast<void*>(gameBase+0x65c880))&0x400000u))return -1;
    const int loaded=loadedDoorFile();
    return loaded>=0&&loaded==int(read<std::uint16_t>(reinterpret_cast<void*>(gameBase+0x661f94)))?loaded:-1;
}

struct DoorDepth {std::uint16_t zsf3=341;unsigned shift=7;};
inline std::uint16_t doorDepth(void* data,std::uint16_t a,std::uint16_t b,std::uint16_t c){
    const auto& value=*static_cast<const DoorDepth*>(data);
    const std::uint64_t average=(std::uint64_t(unsigned(a)+unsigned(b)+unsigned(c))*value.zsf3)>>12;
    return std::uint16_t((std::min)(std::uint64_t(0x1fff),average)>>value.shift);
}
inline bool predictedDoorFlags(const std::uint8_t* object,std::uint16_t& flags){
    flags=read<std::uint16_t>(object,0xc0);
    if(flags&0x800u){
        const auto context=read<const void*>(reinterpret_cast<void*>(gameBase+0x635b48));
        if(!accessible(context,0x228))return false;
        if(int(read<std::uint16_t>(context,0x226))-100==read<std::uint8_t>(object,0xc3)){
            if((flags&0x30u)==0x30u)flags=std::uint16_t(flags&~0x20u);else flags=std::uint16_t(flags|0x10u);
        }
    }
    return true;
}
inline bool knownDoorObject(const void* object){
    for(unsigned n=0;n<10;++n)if(read<const void*>(reinterpret_cast<void*>(gameBase+0x635b20+n*4))==object)return true;
    return false;
}

inline bool prepareDoor(void* objectPointer,void* packets,void* temporary,bool& published){
    if(!doorFrameAllowed||!active()||!accessible(objectPointer,0xc8)||!accessible(temporary,8))return false;
    const auto object=static_cast<const std::uint8_t*>(objectPointer);
    if(!knownDoorObject(object)||!read<std::uint32_t>(object))return false;
    const auto index=read<std::uint8_t>(object,0xc2); // DO2 mesh record of this door object (469E30 binds +14 to it)
    const auto nativeMesh=read<const void*>(temporary);
    if(nativeMesh!=read<const void*>(object,0x14)||!accessible(nativeMesh,24))return false;
    const auto nativeTriangles=read<const void*>(nativeMesh,0xc);
    if(!read<std::uint16_t>(nativeMesh,0x14)||!accessible(nativeTriangles,12))return false;
    char part[40];sprintf_s(part,"DO2=%02X part=%u",unsigned(doorLatched),unsigned(index));
    re3prop::Identity identity;const char* reason="";
    const auto* rule=re3prop::doorRule(std::uint32_t(doorLatched),index,nativeMesh,identity,reason);
    if(!rule){
        if(modelObserved.size()<6000&&modelObserved.insert(std::string("door-native:")+part+reason).second)
            log("MODEL_DOOR_NATIVE %s reason=%s identity=%s object=%p",part,reason,re3prop::identityText(identity).c_str(),objectPointer);
        return false;
    }
    std::uint16_t flags=0;if(!predictedDoorFlags(object,flags))return false;
    const unsigned style=flags&0x30u;
    if(!style||(flags&0x200u)){
        if(modelObserved.size()<6000&&modelObserved.insert(std::string("door-flags:")+part+std::to_string(flags)).second)
            log("MODEL_DOOR_NATIVE %s reason=unsupported-flags flags=%04x (native blend/order path kept)",part,flags);
        return false;
    }
    // Prepare the native zero-geometry call before publication. 469A00's prefix
    // updates a timed render-order flag and GTE RGB; preserving it is essential.
    std::uint32_t emptyMesh[6];std::memcpy(emptyMesh,nativeMesh,sizeof(emptyMesh));emptyMesh[5]&=0xffff0000u;
    struct Temporary {void* mesh;std::uint32_t rgb;};
    Temporary empty{emptyMesh,read<std::uint32_t>(temporary,4)};
    if(rule->hide()){
        published=true;originalDoorObject(objectPointer,packets,&empty);
        if(modelObserved.insert(std::string("door-hide:")+part).second)log("MODEL_DOOR_HIDE %s identity=%s (native part suppressed by the table)",part,re3prop::identityText(identity).c_str());
        return true;
    }
    auto asset=getModel(rule->asset);
    if(!asset||asset->mesh.inverseBind.size()!=1){once(rule->asset+"-door-asset","asset unavailable or not rigid; native door part retained");return false;}
    if(!modelRenderer.resetDevice(gameDevice))return false;
    re3model::Projection projection;if(!snapshotProjection(projection,true))return false;
    // v20: the native door light instead of the bare texture. 469800 gives each door object ambient 0x40 (0x88 with
    // flag 0x40) and colour matrix 52AC00 in the GTE, and lights the DO2 normals with the rows of 52ABE0 fixed in CAMERA
    // space (light matrix = 52ABE0 * object+4C); RGB +74 = 808080 (469E30). Here the normals are camera-space too.
    re3model::NativeLighting doorLight;re3model::SculptedLighting doorShade;const auto& lit=re3model::AssetLightingStore()[rule->asset];
    if(re3model::CaptureNativeLighting(doorLight,reinterpret_cast<void*>(gameBase+0x12abe0),read<const void*>(reinterpret_cast<void*>(gameBase+0x1349c8)),0x100,read<std::uint32_t>(temporary,4)&0xffffffu)){
        const auto camera=read<re3bridge::Matrix32>(reinterpret_cast<void*>(gameBase+0x11c398)); // world Y-down: up = -(camera column 1)
        doorShade=re3model::MakeSculptedLighting(doorLight,lit,
            {-camera.rotation[1]/4096.f,-camera.rotation[4]/4096.f,-camera.rotation[7]/4096.f});
        projection.shadeVertex=&re3model::ShadeSculpted;projection.shadeContext=&doorShade;
    }
    DoorDepth depth{std::uint16_t(projection.zsf3),style==0x20?12u:7u};
    projection.depth=doorDepth;projection.depthContext=&depth;projection.bucketLimit=style==0x20?2u:64u;
    // +4C already includes the camera (and the parent chain +78 of multi-part doors); xf maps a native_ref-fitted
    // asset onto this DO2's own mesh record.
    auto cameraMatrix=re3model::FromNativeMatrix(read<re3bridge::Matrix32>(object,0x4c));
    if(rule->transformed)cameraMatrix=re3model::Multiply(cameraMatrix,rule->transform);
    re3model::FrameGeometry geometry;std::string error;
    // GPU path: the static mesh with the door's camera matrix as its one bone, in camera space like the CPU path
    const auto table0=read<std::uint32_t*>(reinterpret_cast<void*>(gameBase+(style==0x20?0x65c7e8:0x65c7a4)));
    const std::size_t byteOffset0=style==0x10?0x81c:style==0x30?0x20:0;const unsigned slots0=style==0x20?2u:64u;
    if(projection.shadeVertex&&table0&&accessible(reinterpret_cast<const std::uint8_t*>(table0)+byteOffset0,slots0*4,true)){
        auto gpuProjection=projection;gpuProjection.camera=re3model::Matrix3x4{};re3bridge::ActorPlan gpuPlan;
        gpuBucketShift=depth.shift;gpuNearCut=false;gpuDoorLight=&doorShade;gpuPlainLight=false;
        const int gpu=appendGpuJobs(asset,{cameraMatrix},gpuProjection,gpuPlan,nullptr,nullptr,nullptr);
        gpuBucketShift=3;gpuNearCut=true;gpuDoorLight=nullptr;
        if(gpu>0&&!gpuPlan.jobs.empty()){
            std::vector<re3bridge::AbsoluteJob> absolute;absolute.reserve(gpuPlan.jobs.size());bool fits=true;
            for(const auto& job:gpuPlan.jobs){fits=fits&&job.bucket<slots0;absolute.push_back({reinterpret_cast<std::uint32_t*>(reinterpret_cast<std::uint8_t*>(table0)+byteOffset0)+job.bucket,job});}
            if(fits&&re3bridge::EnqueueAbsoluteBatch(absolute)){
                published=true;originalDoorObject(objectPointer,packets,&empty);re3prop::noteUse(rule->asset);
                if(modelObserved.insert(std::string("door-prepared:")+part+rule->asset).second)log("MODEL_DOOR_PREPARED %s asset=%s identity=%s xf=%u object=%p flags=%04x style=%02x jobs=%zu matrix=camera-space gpu",part,rule->asset.c_str(),
                    re3prop::identityText(identity).c_str(),rule->transformed?1u:0u,objectPointer,flags,style,gpuPlan.jobs.size());
                return true;}
        }
        if(gpu==0)return false;
    }
    if(!re3model::PrepareRigidByCameraMatrix(asset->mesh,cameraMatrix,projection,geometry,&error)||geometry.vertices.empty()){
        once(rule->asset+"-prepare",error.c_str());return false;
    }
    const auto table=read<std::uint32_t*>(reinterpret_cast<void*>(gameBase+(style==0x20?0x65c7e8:0x65c7a4)));
    const std::size_t byteOffset=style==0x10?0x81c:style==0x30?0x20:0;
    const unsigned slots=style==0x20?2u:64u;
    if(!table||!accessible(reinterpret_cast<const std::uint8_t*>(table)+byteOffset,slots*4,true))return false;
    HRESULT hr;auto gpu=modelRenderer.upload(asset->mesh,geometry,asset->textures,false,&hr);
    if(!gpu){once(rule->asset+"-upload","GPU upload failed; native door retained");return false;}
    std::vector<re3bridge::AbsoluteJob> jobs;jobs.reserve(gpu->slices.size());
    for(std::size_t n=0;n<gpu->slices.size();++n){
        if(gpu->slices[n].bucket>=slots)return false;
        auto job=std::make_shared<ModelJob>();job->geometry=gpu;job->slice=n;job->name=rule->asset;
        if(index==0&&n==0&&(doorCaptureTick==4||doorCaptureTick==50||doorCaptureTick==100||doorCaptureTick==150||doorCaptureTick==200||doorCaptureTick==250||doorCaptureTick==300)){
            char tag[16];sprintf_s(tag,"DOOR%02X-",unsigned(doorLatched));job->captureTag=tag+std::to_string(read<uint16_t>(reinterpret_cast<void*>(gameBase+0x661f8e)))+"-"+std::to_string(doorCaptureTick);}
        auto slot=reinterpret_cast<std::uint32_t*>(reinterpret_cast<std::uint8_t*>(table)+byteOffset)+gpu->slices[n].bucket;
        jobs.push_back({slot,{gpu->slices[n].bucket,reinterpret_cast<std::uintptr_t>(job.get()),job}});
    }
    if(!re3bridge::EnqueueAbsoluteBatch(jobs))return false;
    published=true;
    originalDoorObject(objectPointer,packets,&empty);
    re3prop::noteUse(rule->asset);
    if(modelObserved.insert(std::string("door-prepared:")+part+rule->asset).second)
        log("MODEL_DOOR_PREPARED %s asset=%s identity=%s xf=%u object=%p flags=%04x style=%02x slices=%zu matrix=camera-space light=%s ambient=%d rgb=%02x%02x%02x ao=%zu",part,rule->asset.c_str(),
            re3prop::identityText(identity).c_str(),rule->transformed?1u:0u,objectPointer,flags,style,gpu->slices.size(),projection.shadeVertex?(lit.sculpted?"sculpted":"native"):"none",
            int(doorLight.ambient[0]),doorLight.baseRGB[0],doorLight.baseRGB[1],doorLight.baseRGB[2],lit.occlusion.size());
    return true;
}

inline void __cdecl doorObjectHook(void* object,void* packets,void* temporary){
    std::lock_guard<std::recursive_mutex> guard(mutex);
    bool published=false;
    try{if(prepareDoor(object,packets,temporary,published))return;}
    catch(...){log("MODEL_EXTRA door-prepare-exception committed=%u",published);if(published)return;}
    originalDoorObject(object,packets,temporary);
}
inline void __cdecl doorBuildHook(){
    std::lock_guard<std::recursive_mutex> guard(mutex);
    static const void* lastDoorContext=nullptr;static uint16_t lastDoorCounter=0xffff;
    const auto context=read<const void*>(reinterpret_cast<void*>(gameBase+0x635b48));
    const bool contextValid=accessible(context,0x228);
    const auto counter=contextValid?read<uint16_t>(context,0x226):uint16_t(0);
    if(!contextValid||context!=lastDoorContext||counter==0||counter<lastDoorCounter){
        doorLatched=contextValid?latchDoor():-1;
        log("MODEL_DOOR_IDENTITY latched=%u DO2=%02X requested=%02X file=%c%c source=%s room=%s",doorLatched>=0?1u:0u,doorLatched>=0?unsigned(doorLatched):0xffu,
            unsigned(read<std::uint16_t>(reinterpret_cast<void*>(gameBase+0x661f94))),read<char>(reinterpret_cast<void*>(gameBase+0x12ac34)),
            read<char>(reinterpret_cast<void*>(gameBase+0x12ac35)),currentScene.c_str(),re3prop::roomText(re3prop::currentRoom()).c_str());
    }
    try{re3prop::housekeeping();}catch(...){}
    lastDoorContext=context;lastDoorCounter=counter;doorCaptureTick=unsigned(counter)+1;
    auto mainOT=read<std::uint32_t*>(reinterpret_cast<void*>(gameBase+0x65c7a4));
    // The same1024-slot main OT is cleared/walked by the door's42AFB0 loop.
    re3bridge::BeginExternalBuild(mainOT&&accessible(mainOT,1024*4,true)?mainOT+1023:nullptr);
    // The loader overwrites the shared source RDT while this cooperative door
    // loop is still drawing. A5C8B8 then points at unrelated destination bytes.
    // Retain the proven entry identity, but validate native loop/file/object
    // identity throughout the animation. Never dereference the stale AOT.
    doorFrameAllowed=active()&&doorLatched>=0&&
        (read<std::uint32_t>(reinterpret_cast<void*>(gameBase+0x65c880))&0x400000u)&&loadedDoorFile()==doorLatched;
    originalDoorBuild();doorFrameAllowed=false;
}

// ---------------------------------------------------------------- spent cases (v12)
// Every case of CORE00 effect 02 (callback class 0x10, behaviour 4D51A0) is drawn as a 3D case: variant 0 pistol
// (Sigpro, M92F, Eagle, NPC handguns; palette 4 = the nickel cases of the enhanced rounds), 1 assault rifle, 2 shotgun
// shell (Benelli, M37; palette 5 = the blue enhanced shells), 10 the magnum cases dropped by the reload spawner
// (variant 3), 12 gatling. Any shooter: v11 bound the case to Jill's standard model and her weapon at spawn, so her
// cases stayed native in the harness (PL01) and Carlos's never changed.
// The native flight, bounces, sounds and lifetime stay. The case tumbles in 3D at the native position and lies down
// after its first bounce; when the native effect retires it (second ground or third wall contact) it stays on the
// floor as an HD case until the room changes or kMaxResting newer cases push the oldest out.
struct CaseType{const char* asset;float length,spin;};
static const CaseType caseTypes[]={{"casing-pistol",66,0.78f},{"casing-pistol-nickel",66,0.78f},{"casing-rifle",88,0.7f},
    {"casing-shell",120,0.45f},{"casing-shell-blue",120,0.45f},{"casing-magnum",76,0.12f},{"casing-gatling",96,0.4f}};
constexpr int kCaseTypes=int(sizeof(caseTypes)/sizeof(caseTypes[0]));
// CORE00.ESP template of each case variant (effect +0xCC): flags, acceleration, velocity
struct CaseTemplate{unsigned variant;std::uint16_t flags;std::int8_t acceleration[3];std::int16_t velocity[3];};
static const CaseTemplate caseTemplates[]={{0,0xa093,{0,9,0},{140,-50,0}},{1,0xa09b,{0,9,0},{80,-20,0}},{2,0xa093,{0,9,0},{80,-20,0}},
    {10,0xa093,{0,9,0},{0,9,0}},{12,0xa09b,{0,9,0},{10,-20,0}}};
constexpr std::size_t kMaxResting=64;
static const re3model::Vec3 kUp{0,-1,0}; // native world is Y-down
struct LiveCase{
    std::uint64_t generation=0;int type=-1;
    re3model::Vec3 axis{0,1,0},roll{1,0,0},spin{0,1,0};float rate=0;
    std::int32_t position[3]{},floorY=0;std::uint8_t tint[3]{128,128,128};
    bool landed=false;std::uint32_t seed=0,lastSerial=0;
};
struct RestingCase{int type=0;re3model::Vec3 axis,roll,centre;float restY=0,fall=0;std::uint8_t tint[3]{128,128,128};};
static std::unordered_map<std::uintptr_t,LiveCase> liveCases;
static std::unordered_map<std::uintptr_t,std::uint64_t> effectGeneration;
static std::uint64_t effectAllocations=0,casesRetired=0;
static std::deque<RestingCase> restingCases;
static std::uint32_t caseRoom=~0u,restingVersion=0;
static bool caseLogged[kCaseTypes]{};

inline re3model::Vec3 rotateAround(re3model::Vec3 v,re3model::Vec3 k,float angle){
    const float c=std::cos(angle),s=std::sin(angle);
    return v*c+re3model::Cross(k,v)*s+k*(re3model::Dot(k,v)*(1-c));
}
inline float caseRandom(std::uint32_t& state){state=state*1664525u+1013904223u;return float((state>>8)&0xffff)/65535.f;}
// the long axis keeps its direction, the roll axis is made perpendicular to it
inline void orthonormalize(re3model::Vec3& axis,re3model::Vec3& roll){
    axis=re3model::Normalize(axis);roll=roll-axis*re3model::Dot(roll,axis);
    if(re3model::Dot(roll,roll)<1.e-6f){roll=re3model::Cross(axis,std::abs(axis.y)<0.9f?re3model::Vec3{0,1,0}:re3model::Vec3{1,0,0});}
    roll=re3model::Normalize(roll);
}
inline int caseIdentity(const std::uint8_t* effect){
    const auto address=reinterpret_cast<std::uintptr_t>(effect);
    const auto start=gameBase+0x646720;
    if(address<start||address>=start+96u*0xd4||(address-start)%0xd4||!accessible(effect,0xd4))return -1;
    const auto flags=read<std::uint16_t>(effect,0x24),key=read<std::uint16_t>(effect,0x28);
    if((flags&0xa000u)!=0xa000u||(flags&0x200u)||read<std::uint8_t>(effect)!=0x10||(key&0xffu)!=2)return -1;
    const unsigned variant=key>>8;const CaseTemplate* expected=nullptr;
    for(const auto& t:caseTemplates)if(t.variant==variant)expected=&t;
    const auto source=read<const std::uint8_t*>(effect,0xcc);
    if(!expected||!accessible(source,0x24)||source[0]!=0x10||read<std::uint16_t>(source,4)!=expected->flags||
       std::memcmp(source+0xc,expected->acceleration,3)||std::memcmp(source+0x10,expected->velocity,6))return -1;
    // the spawn key's top byte selects the CLUT row (+0xB3): CORE00 brass/shell CLUT 7C91 plus one row per palette
    const unsigned palette=read<std::uint8_t>(effect,0xb3)&15u;
    if(read<std::uint16_t>(effect,0x44)!=0x7c91u+palette*0x40u||(read<std::uint16_t>(effect,0x46)&0x1ffu)!=0x1e)return -1;
    switch(variant){case 0:return palette==4?1:0;case 1:return 2;case 2:return palette==5?4:3;case 10:return 5;case 12:return 6;}
    return -1;
}
inline bool emittedSinglePacket(void* packet,std::uint32_t* orderingTable,unsigned& bucket){
    if(!accessible(packet,40,true)||!accessible(orderingTable,1024*4,true)||re3bridge::detail::Signature(packet))return false;
    if((read<std::uint32_t>(packet)&3u)!=3u||(read<std::uint8_t>(packet,7)&0xfcu)!=0x2c)return false;
    const auto address=std::uint32_t(reinterpret_cast<std::uintptr_t>(packet))&~3u;
    for(unsigned n=0;n<1024;++n)if((orderingTable[n]&~3u)==address){bucket=n;return true;}
    return false;
}
inline bool nativeCaseTile(const std::uint8_t* packet,const std::uint8_t* effect){
    const unsigned u=packet[12],v=packet[13];
    // CORE02 16px tiles: brass row 216 (pistol, rifle, magnum, gatling) and shell row 232, eight 45-degree turns
    if(u>112||u%16||(v!=216u&&v!=232u))return false;
    if(read<std::uint16_t>(packet,14)!=read<std::uint16_t>(effect,0x44)||(read<std::uint16_t>(packet,22)&0x1ffu)!=0x1e)return false;
    return packet[20]==u+15&&packet[21]==v&&packet[28]==u&&packet[29]==v+15&&packet[36]==u+15&&packet[37]==v+15;
}
// Case mesh: long axis +Y; world length per type, radius for lying on the floor.
struct CaseBounds{re3model::Vec3 centre;float height=0,radius=0;};
inline bool caseBounds(const ModelAsset& asset,CaseBounds& out){
    if(asset.mesh.vertices.empty())return false;
    re3model::Vec3 low=asset.mesh.vertices[0].position,high=low;
    for(const auto& v:asset.mesh.vertices){low.x=(std::min)(low.x,v.position.x);low.y=(std::min)(low.y,v.position.y);low.z=(std::min)(low.z,v.position.z);
        high.x=(std::max)(high.x,v.position.x);high.y=(std::max)(high.y,v.position.y);high.z=(std::max)(high.z,v.position.z);}
    out.centre=(low+high)*0.5f;out.height=high.y-low.y;out.radius=0;
    for(const auto& v:asset.mesh.vertices)out.radius=(std::max)(out.radius,std::hypot(v.position.x-out.centre.x,v.position.z-out.centre.z));
    return out.height>1.e-6f;
}
inline re3model::Matrix3x4 caseMatrix(const re3model::Vec3& axis,const re3model::Vec3& roll,const re3model::Vec3& centre,const CaseBounds& bounds,float length){
    const float scale=length/bounds.height;const auto side=re3model::Cross(roll,axis);
    const re3model::Vec3 columns[3]={roll*scale,axis*scale,side*scale};
    const auto offset=columns[0]*bounds.centre.x+columns[1]*bounds.centre.y+columns[2]*bounds.centre.z;
    re3model::Matrix3x4 m;
    for(unsigned r=0;r<3;++r){for(unsigned c=0;c<3;++c)m.m[r*4+c]=(&columns[c].x)[r];m.m[r*4+3]=(&centre.x)[r]-(&offset.x)[r];}
    return m;
}
// a soft key light from above plus ambient, times the native case brightness (+0xB0, 0x80 = 1; set at spawn from
// the room light, so cases stay dark in dark rooms like the original sprites)
struct CaseShade{float scale[3];};
static const re3model::Vec3 kCaseLight=re3model::Normalize({0.35f,-0.8f,-0.45f});
inline std::uint32_t shadeCase(void* context,re3model::Vec3,re3model::Vec3 normal,const re3model::Material& material){
    const auto& shade=*static_cast<const CaseShade*>(context);const auto base=re3model::RGBAtoARGB(material.baseColorRGBA);
    const float light=0.6f+0.55f*(std::max)(0.f,re3model::Dot(re3model::Normalize(normal),kCaseLight));
    std::uint32_t out=base&0xff000000u;
    for(unsigned c=0;c<3;++c){const unsigned shift=16-8*c;
        out|=std::uint32_t((std::min)(255.f,float((base>>shift)&255u)*(std::min)(1.f,light*shade.scale[c])+.5f))<<shift;}
    return out;
}
inline void caseShade(const std::uint8_t tint[3],CaseShade& shade){for(unsigned c=0;c<3;++c)shade.scale[c]=float(tint[c])/128.f;}
inline void initLiveCase(LiveCase& live,const std::uint8_t* effect,int type,std::uint64_t generation){
    live=LiveCase{};live.generation=generation;live.type=type;
    live.seed=std::uint32_t(generation*2654435761u)^std::uint32_t(reinterpret_cast<std::uintptr_t>(effect));
    // the emitter (+0x70) is the shooter's hand at spawn: X up, Y barrel, Z side. The case leaves the chamber along
    // the barrel and turns end over end around the side axis and the vertical, a little different every shot.
    const auto emitter=re3model::FromNativeMatrix(read<re3bridge::Matrix32>(effect,0x70));
    const re3model::Vec3 up=re3model::Normalize({emitter.m[0],emitter.m[4],emitter.m[8]}),barrel=re3model::Normalize({emitter.m[1],emitter.m[5],emitter.m[9]}),
        side=re3model::Normalize({emitter.m[2],emitter.m[6],emitter.m[10]});
    live.axis=barrel;live.roll=up;orthonormalize(live.axis,live.roll);
    live.spin=re3model::Normalize(side*(0.6f+0.4f*caseRandom(live.seed))+kUp*(0.4f+0.6f*caseRandom(live.seed))+barrel*((caseRandom(live.seed)-0.5f)*0.4f));
    live.rate=caseTypes[type].spin*(0.8f+0.4f*caseRandom(live.seed))*(caseRandom(live.seed)<0.5f?-1.f:1.f);
}
// one native motion step: tumble in the air; after the first bounce turn the long axis down to the floor and let the
// spin die out around the vertical
inline void stepLiveCase(LiveCase& live){
    if(live.landed){
        auto flat=live.axis-kUp*re3model::Dot(live.axis,kUp);
        if(re3model::Dot(flat,flat)<1.e-4f)flat=live.roll-kUp*re3model::Dot(live.roll,kUp);
        flat=re3model::Normalize(flat);
        const float angle=std::acos((std::max)(-1.f,(std::min)(1.f,re3model::Dot(live.axis,flat))));
        if(angle>1.e-3f){const auto k=re3model::Normalize(re3model::Cross(live.axis,flat));live.axis=rotateAround(live.axis,k,angle*0.45f);live.roll=rotateAround(live.roll,k,angle*0.45f);}
        live.rate*=0.6f;live.axis=rotateAround(live.axis,kUp,live.rate);live.roll=rotateAround(live.roll,kUp,live.rate);
    }else{live.axis=rotateAround(live.axis,live.spin,live.rate);live.roll=rotateAround(live.roll,live.spin,live.rate);}
    orthonormalize(live.axis,live.roll);
}
// The native effect is gone: the case stays where it fell. A case that died against a wall in the air falls to the
// floor of its shooter (+0x54), like the native bounce height.
inline void retireCase(const LiveCase& live,const std::uint8_t* effect){
    if(live.type<0||live.type>=kCaseTypes)return;
    std::int32_t position[3];std::memcpy(position,live.position,sizeof(position));std::int32_t floorY=live.floorY;
    if(effect)for(unsigned r=0;r<3;++r)position[r]=read<std::int32_t>(effect,0x48+r*4);
    if(effect)floorY=read<std::int32_t>(effect,0x54);
    auto asset=getModel(caseTypes[live.type].asset);CaseBounds bounds;if(!asset||!caseBounds(*asset,bounds))return;
    RestingCase rest;rest.type=live.type;std::memcpy(rest.tint,live.tint,3);
    auto flat=live.axis-kUp*re3model::Dot(live.axis,kUp);
    if(re3model::Dot(flat,flat)<1.e-4f)flat=live.roll-kUp*re3model::Dot(live.roll,kUp);
    rest.axis=re3model::Normalize(flat);
    // lying on its side: any roll around the long axis, from the case's own random sequence
    std::uint32_t seed=live.seed^0x9e3779b9u;const float roll=caseRandom(seed)*6.2831853f;
    const auto across=re3model::Normalize(re3model::Cross(rest.axis,kUp));
    rest.roll=across*std::cos(roll)+kUp*std::sin(roll);orthonormalize(rest.axis,rest.roll);
    const float radius=bounds.radius*caseTypes[live.type].length/bounds.height;
    rest.centre={float(position[0]),float(position[1]),float(position[2])};rest.restY=float(floorY)-radius;
    if(rest.centre.y>rest.restY)rest.centre.y=rest.restY;
    restingCases.push_back(rest);while(restingCases.size()>kMaxResting)restingCases.pop_front();++restingVersion;
    if(++casesRetired==1||casesRetired%200==0)log("MODEL_CASE_REST %s retired=%llu resting=%zu floor=%d lift=%.1f",caseTypes[live.type].asset,(unsigned long long)casesRetired,restingCases.size(),floorY,radius);
}
inline bool prepareCasing(void* packetPointer,void* effectPointer,int groupSize,int count,int projectionH,std::uint32_t* orderingTable,int command,int){
    if(!active()||(!pilotRoom()&&!modelJillAllRooms&&!modelCharactersAllRooms)||groupSize!=16||count!=1||(command&0xfc)!=0x2c||projectionH<=0)return false;
    const auto effect=static_cast<const std::uint8_t*>(effectPointer);
    const int type=caseIdentity(effect);if(type<0)return false;
    unsigned nativeBucket=0;if(!emittedSinglePacket(packetPointer,orderingTable,nativeBucket))return false;
    const auto packet=static_cast<const std::uint8_t*>(packetPointer);
    if(!nativeCaseTile(packet,effect)){if(modelObserved.insert("case-tile:"+std::to_string(type)).second)log("MODEL_CASE_TILE_REJECT %s UV=%u,%u/%u,%u/%u,%u/%u,%u clut=%04x tpage=%04x",caseTypes[type].asset,packet[12],packet[13],packet[20],packet[21],packet[28],packet[29],packet[36],packet[37],read<uint16_t>(packet,14),read<uint16_t>(packet,22));return false;}
    auto asset=getModel(caseTypes[type].asset);CaseBounds bounds;
    if(!asset||asset->mesh.inverseBind.size()!=1||!caseBounds(*asset,bounds)||!modelRenderer.resetDevice(gameDevice))return false;
    re3model::Projection projection;if(!snapshotProjection(projection,false))return false;
    const float hRatio=float(projectionH)/projection.h;
    if(hRatio<0.25f||hRatio>4.f){once("casing-projection-ratio","keeping native packet; unmatched projection scale");return false;}
    std::int32_t position[3];
    for(unsigned r=0;r<3;++r){position[r]=read<std::int32_t>(effect,0x48+r*4);
        // The native billboard sends these through int16 GTE input slots.
        if(position[r]<-32768||position[r]>32767){once("casing-world-wrap","keeping native wrapped-position effect");return false;}}
    const auto address=reinterpret_cast<std::uintptr_t>(effect);
    const auto generationAt=effectGeneration.find(address);const std::uint64_t generation=generationAt==effectGeneration.end()?0:generationAt->second;
    auto& live=liveCases[address];
    if(live.type!=type||live.generation!=generation)initLiveCase(live,effect,type,generation);
    else if(std::memcmp(position,live.position,sizeof(position)))stepLiveCase(live);
    std::memcpy(live.position,position,sizeof(position));live.floorY=read<std::int32_t>(effect,0x54);
    for(unsigned c=0;c<3;++c)live.tint[c]=read<std::uint8_t>(effect,0xb0+c);
    live.landed=live.landed||read<std::int16_t>(effect,0xb4)>=1;live.lastSerial=re3bridge::BuildSerial();
    re3model::Vec3 centre{float(position[0]),float(position[1]),float(position[2])};
    // the native sprite bounces with its centre on the floor; the 3D case rests on it (+0x54: the collision floor)
    if(live.landed)centre.y=(std::min)(centre.y,float(live.floorY)-bounds.radius*caseTypes[type].length/bounds.height);
    CaseShade shade;caseShade(live.tint,shade);projection.shade=shadeCase;projection.shadeContext=&shade;projection.cachePureShade=true;
    std::vector<re3model::SkinnedVertex> skinned;re3model::FrameGeometry geometry;std::string error;
    if(!re3model::Skin(asset->mesh,{caseMatrix(live.axis,live.roll,centre,bounds,caseTypes[type].length)},skinned,&error)||
       !re3model::PrepareFrame(asset->mesh,skinned,projection,geometry,&error)||geometry.vertices.empty())return false;
    // One original effect packet has one native OT position. Keep that exact
    // link and draw all internal model slices within this one replacement job.
    geometry.slices.clear();geometry.slices.push_back({std::uint16_t(nativeBucket),0,std::uint32_t(geometry.runs.size())});
    HRESULT hr;auto gpu=modelRenderer.upload(asset->mesh,geometry,asset->textures,false,&hr);if(!gpu)return false;
    auto job=std::make_shared<ModelJob>();job->geometry=gpu;job->slice=0;job->name=caseTypes[type].asset;
    if(!re3bridge::ReplacePacket(packetPointer,reinterpret_cast<std::uintptr_t>(job.get()),job,orderingTable+nativeBucket))return false;
    if(!caseLogged[type]){caseLogged[type]=true;
        log("MODEL_CASING_PREPARED %s key=%04x palette=%u effect=%p generation=%llu bucket=%u Hratio=%.4f length=%.0f triangles=%zu",caseTypes[type].asset,unsigned(read<std::uint16_t>(effect,0x28)),
            unsigned(read<std::uint8_t>(effect,0xb3)&15u),effect,(unsigned long long)generation,nativeBucket,hRatio,caseTypes[type].length,asset->mesh.indices.size()/3);
    }
    return true;
}
namespace blood{inline void collapseNativeSpray(void* packets,const void* effect,int count);}
namespace fire{inline void billboard(void* packets,void* effect,int count,std::uint32_t* orderingTable,int command);}   // fx 2026-11-08 fire
namespace muzzle{static void collapseNative(void* packets,const void* effect,int count);} // fx 2026-11-08 muzzle
inline void __cdecl billboardHook(void* packets,void* effect,int groupSize,int count,int projectionH,std::uint32_t* orderingTable,int command,int groupStart){
    std::lock_guard<std::recursive_mutex> guard(mutex);
    originalBillboard(packets,effect,groupSize,count,projectionH,orderingTable,command,groupStart);
    if(effectTrace&&effectTraceLines<6000&&accessible(effect,0xd4)&&accessible(packets,40)){const auto key=read<std::uint16_t>(effect,0x28);static std::unordered_map<std::uint32_t,unsigned> seen;
        const auto* p=static_cast<const std::uint8_t*>(packets);const std::uint32_t id=key|(std::uint32_t(command&0xff)<<16)|(std::uint32_t(count&0xff)<<24);
        if(++seen[id]<=2){++effectTraceLines;log("EFFECT_BILLBOARD key=%04x group=%d count=%d command=%02x H=%d start=%d pkt=%02x%02x%02x%02x %02x%02x%02x%02x uv=%u,%u clut=%04x tpage=%04x",key,groupSize,count,command,projectionH,groupStart,
            p[0],p[1],p[2],p[3],p[4],p[5],p[6],p[7],p[12],p[13],read<std::uint16_t>(p,14),read<std::uint16_t>(p,22));}}
    try{blood::collapseNativeSpray(packets,effect,count);}catch(...){}
    try{ensureFrameBuild();fire::billboard(packets,effect,count,orderingTable,command);}catch(...){}   // fx 2026-11-08 fire
    try{muzzle::collapseNative(packets,effect,count);}catch(...){} // fx 2026-11-08 muzzle
    try{ensureFrameBuild();prepareCasing(packets,effect,groupSize,count,projectionH,orderingTable,command,groupStart);}
    catch(...){log("MODEL_EXTRA casing-prepare-exception; original emission already preserved");}
}
// Every effect allocation (both native callers of 43DD00) starts a new generation for that pool slot; a case still
// recorded there was retired by the native code before its slot could be reused.
inline void* __cdecl allocateHook(){
    std::lock_guard<std::recursive_mutex> guard(mutex);
    auto effect=originalAllocate();
    try{
        if(effect&&effect!=reinterpret_cast<void*>(~std::uintptr_t(0))){
            const auto address=reinterpret_cast<std::uintptr_t>(effect);
            const auto live=liveCases.find(address);
            if(live!=liveCases.end()){if(active())retireCase(live->second,nullptr);liveCases.erase(live);}
            effectGeneration[address]=++effectAllocations;
        }
    }catch(...){log("MODEL_EXTRA effect-allocation bookkeeping failed; native effect retained");}
    return effect;
}
// Resting cases are drawn after the native effect loop, each at the OT bucket of its centre depth (the native effect
// mapping, SZ/32) in the ordering table the native code picks for that position (45ADC0: camera zones or the main OT).
// The merged CPU geometry is reused while the camera and the set of cases stay the same.
// The cache holds no device objects (a device rebuild must not find our references): textures come from the
// current assets every frame. Nor does it hold ordering-table pointers: the native OTs are double-buffered (main OT
// A5A7A4 + 0x1000 * buffer, zone OTs per buffer, the buffer flips every frame at 42F816), so each geometry keeps
// one case position and 45ADC0 picks its table again every frame.
using ChooseTable=std::uint32_t*(__cdecl*)(const std::int32_t*);
struct RestingDraw{std::uint32_t* table;std::int32_t position[3];std::uint16_t bucket;re3model::FrameGeometry geometry;std::uint32_t materialBase;};
struct RestingGroup{std::int32_t position[3];re3model::FrameGeometry geometry;};
struct RestingCache{std::uint32_t version=~0u;std::uint8_t camera[32]{},projection[0x14]{};D3DVIEWPORT9 viewport{};
    std::vector<RestingGroup> tables;re3model::Mesh materials;std::vector<int> types;};
static RestingCache restingCache;
inline std::uint16_t constantBucket(void* context,std::uint16_t,std::uint16_t,std::uint16_t){return *static_cast<const std::uint16_t*>(context);}
inline bool buildRestingCases(const re3model::Projection& base,RestingCache& cache){
    cache.tables.clear();cache.materials=re3model::Mesh{};cache.types.clear();
    std::vector<RestingDraw> draws;std::shared_ptr<ModelAsset> assets[kCaseTypes];std::uint32_t materialBase[kCaseTypes]{};
    const auto chooseTable=reinterpret_cast<ChooseTable>(gameBase+0x5adc0);
    for(const auto& rest:restingCases){
        if(!assets[rest.type]){assets[rest.type]=getModel(caseTypes[rest.type].asset);if(!assets[rest.type])continue;
            materialBase[rest.type]=std::uint32_t(cache.materials.materials.size());
            for(const auto& m:assets[rest.type]->mesh.materials)cache.materials.materials.push_back(m);cache.types.push_back(rest.type);}
        const auto& asset=*assets[rest.type];CaseBounds bounds;if(!caseBounds(asset,bounds))continue;
        const auto camera=re3model::TransformPoint(base.camera,rest.centre);
        if(camera.z<512.f||camera.z>32767.f)continue;
        const float sx=camera.x*base.h/camera.z+base.offsetX,sy=camera.y*base.h/camera.z+base.offsetY;
        if(sx<-96.f||sx>416.f||sy<-96.f||sy>336.f)continue;
        const std::int32_t position[3]={std::int32_t(rest.centre.x),std::int32_t(rest.centre.y),std::int32_t(rest.centre.z)};
        auto* table=chooseTable(position);if(!table||!accessible(table,1024*4,true))continue;
        RestingDraw draw;draw.table=table;std::memcpy(draw.position,position,sizeof(position));draw.bucket=std::uint16_t(std::uint32_t(camera.z)>>5);draw.materialBase=materialBase[rest.type];
        auto projection=base;CaseShade shade;caseShade(rest.tint,shade);
        projection.shade=shadeCase;projection.shadeContext=&shade;projection.cachePureShade=true;projection.depth=constantBucket;projection.depthContext=&draw.bucket;
        std::vector<re3model::SkinnedVertex> skinned;std::string error;
        if(!re3model::Skin(asset.mesh,{caseMatrix(rest.axis,rest.roll,rest.centre,bounds,caseTypes[rest.type].length)},skinned,&error)||
           !re3model::PrepareFrame(asset.mesh,skinned,projection,draw.geometry,&error)||draw.geometry.vertices.empty())continue;
        draws.push_back(std::move(draw));
    }
    // one geometry per ordering table, one slice per bucket
    std::stable_sort(draws.begin(),draws.end(),[](const RestingDraw& a,const RestingDraw& b){return a.table!=b.table?a.table<b.table:a.bucket>b.bucket;});
    for(std::size_t i=0;i<draws.size();){
        RestingGroup group;auto& merged=group.geometry;std::uint32_t* table=draws[i].table;std::memcpy(group.position,draws[i].position,sizeof(group.position));
        for(;i<draws.size()&&draws[i].table==table;++i){const auto& draw=draws[i];
            if(merged.slices.empty()||merged.slices.back().bucket!=draw.bucket)merged.slices.push_back({draw.bucket,std::uint32_t(merged.runs.size()),0});
            const auto first=std::uint32_t(merged.vertices.size());
            merged.vertices.insert(merged.vertices.end(),draw.geometry.vertices.begin(),draw.geometry.vertices.end());
            for(const auto& run:draw.geometry.runs){merged.runs.push_back({first+run.firstVertex,run.vertexCount,run.materialIndex+draw.materialBase});++merged.slices.back().runCount;}
        }
        cache.tables.push_back(std::move(group));
    }
    return true;
}
inline void drawRestingCases(){
    if(restingCases.empty()||!modelRenderer.resetDevice(gameDevice))return;
    const auto gte=read<const void*>(reinterpret_cast<void*>(gameBase+0x1349c8));if(!accessible(gte,0x100))return;
    re3bridge::ActorContext context;context.camera=read<re3bridge::Matrix32>(reinterpret_cast<void*>(gameBase+0x11c398));
    std::memcpy(context.gte.data(),gte,context.gte.size());
    auto projection=actorProjection(context);
    if(!re3model::SetNativeProjectionRegisters(projection,context.gte.data(),context.gte.size()))return;
    D3DVIEWPORT9 viewport{};gameDevice->GetViewport(&viewport);
    auto& cache=restingCache;
    const bool same=cache.version==restingVersion&&!std::memcmp(cache.camera,&context.camera,sizeof(cache.camera))&&
        !std::memcmp(cache.projection,context.gte.data()+0x70,sizeof(cache.projection))&&!std::memcmp(&cache.viewport,&viewport,sizeof(viewport));
    if(!same){
        if(!buildRestingCases(projection,cache))return;
        cache.version=restingVersion;std::memcpy(cache.camera,&context.camera,sizeof(cache.camera));std::memcpy(cache.projection,context.gte.data()+0x70,sizeof(cache.projection));cache.viewport=viewport;
    }
    std::vector<re3model::MaterialTextures> caseTextures;
    for(const int type:cache.types){auto asset=getModel(caseTypes[type].asset);if(!asset){cache.version=~0u;return;}caseTextures.insert(caseTextures.end(),asset->textures.begin(),asset->textures.end());}
    if(caseTextures.size()!=cache.materials.materials.size()){cache.version=~0u;return;}
    const auto chooseTable=reinterpret_cast<ChooseTable>(gameBase+0x5adc0);
    for(const auto& group:cache.tables){
        const auto& geometry=group.geometry;if(geometry.vertices.empty())continue;
        auto* table=chooseTable(group.position);if(!table||!accessible(table,1024*4,true))continue;
        HRESULT hr;auto gpu=modelRenderer.upload(cache.materials,geometry,caseTextures,false,&hr);if(!gpu)return;
        std::vector<re3bridge::BucketJob> jobs;jobs.reserve(gpu->slices.size());
        for(std::size_t n=0;n<gpu->slices.size();++n){auto job=std::make_shared<ModelJob>();job->geometry=gpu;job->slice=n;job->name="casing-resting";
            jobs.push_back({gpu->slices[n].bucket,reinterpret_cast<std::uintptr_t>(job.get()),job});}
        if(!re3bridge::EnqueueBatch(table,1024,jobs)){once("casing-resting-enqueue","resting cases skipped for this table");continue;}
    }
}
inline void updateCases(){
    const std::uint32_t room=(std::uint32_t(read<std::uint16_t>(reinterpret_cast<void*>(gameBase+0x661fa6)))<<16)|read<std::uint16_t>(reinterpret_cast<void*>(gameBase+0x661fa8));
    if(room!=caseRoom){
        if(caseRoom!=~0u&&(!restingCases.empty()||!liveCases.empty()))log("MODEL_CASE_CLEAR room=%x resting=%zu live=%zu",room,restingCases.size(),liveCases.size());
        caseRoom=room;restingCases.clear();liveCases.clear();++restingVersion;
    }
    if(!active())return;
    const auto serial=re3bridge::BuildSerial();
    for(auto it=liveCases.begin();it!=liveCases.end();){
        const auto effect=reinterpret_cast<const std::uint8_t*>(it->first);
        const auto generation=effectGeneration.find(it->first);
        if(generation==effectGeneration.end()||generation->second!=it->second.generation||!accessible(effect,0xd4)){it=liveCases.erase(it);continue;}
        if(read<std::uint16_t>(effect,0x24)==0){retireCase(it->second,effect);it=liveCases.erase(it);continue;}
        if(serial-it->second.lastSerial>900){it=liveCases.erase(it);continue;}   // alive but not drawn for half a minute
        ++it;
    }
    for(auto& rest:restingCases)if(rest.centre.y<rest.restY){rest.fall+=9.f;rest.centre.y=(std::min)(rest.restY,rest.centre.y+rest.fall);++restingVersion;}
    drawRestingCases();
}
// ---------------------------------------------------------------- effect trace (blood round 2026-11-07, pilot.ini effect_trace=1)
// Every native effect of the pool (96 x 0xD4 at A46720) as it appears and ends, with its key, flags, template and the
// first bytes; every enemy of the list whose life or routine bytes change. Diagnostic only: reads, never writes.
struct TracedEffect{bool live=false;std::uint32_t born=0;std::uint16_t key=0;};
static TracedEffect tracedEffects[96];
static std::unordered_map<std::uint16_t,unsigned> tracedKeys;
struct TracedEnemy{std::int16_t life=0;std::uint32_t routine=0;};
static std::unordered_map<std::uintptr_t,TracedEnemy> tracedEnemies;
inline void traceEffects(){
    if(!effectTrace||effectTraceLines>6000)return;
    const auto serial=re3bridge::BuildSerial();
    const auto start=gameBase+0x646720;
    for(unsigned slot=0;slot<96;++slot){
        const auto* e=reinterpret_cast<const std::uint8_t*>(start+slot*0xd4);if(!accessible(e,0xd4))continue;
        const auto flags=read<std::uint16_t>(e,0x24);const bool live=(flags&0xa000u)==0xa000u;auto& t=tracedEffects[slot];
        const auto key=read<std::uint16_t>(e,0x28);
        if(live&&(!t.live||t.key!=key)){
            t.live=true;t.born=serial;t.key=key;const unsigned seen=++tracedKeys[key];++effectTraceLines;
            log("EFFECT_NEW slot=%u serial=%u key=%04x class=%02x flags=%04x b23=%02x tpl=%08x pos=%d,%d,%d floor=%d clut=%04x tpage=%04x b0=%02x%02x%02x%02x seen=%u",
                slot,serial,key,read<std::uint8_t>(e,0),flags,read<std::uint8_t>(e,0x23),read<std::uint32_t>(e,0xcc),read<std::int32_t>(e,0x48),read<std::int32_t>(e,0x4c),read<std::int32_t>(e,0x50),
                read<std::int32_t>(e,0x54),read<std::uint16_t>(e,0x44),read<std::uint16_t>(e,0x46),read<std::uint8_t>(e,0xb0),read<std::uint8_t>(e,0xb1),read<std::uint8_t>(e,0xb2),read<std::uint8_t>(e,0xb3),seen);
            if(seen<=3){char hex[0xd4*2+1];for(unsigned i=0;i<0xd4;++i)std::snprintf(hex+i*2,3,"%02x",e[i]);log("EFFECT_BYTES key=%04x %s",key,hex);
                const auto tpl=read<const std::uint8_t*>(e,0xcc);if(accessible(tpl,0x40)){char th[0x40*2+1];for(unsigned i=0;i<0x40;++i)std::snprintf(th+i*2,3,"%02x",tpl[i]);log("EFFECT_TEMPLATE key=%04x %s",key,th);}}
        }else if(!live&&t.live){t.live=false;++effectTraceLines;log("EFFECT_END slot=%u serial=%u key=%04x frames=%u",slot,serial,t.key,serial-t.born);}
    }
    const auto end=read<std::uintptr_t>(reinterpret_cast<void*>(gameBase+0x65ce6c));
    for(std::uintptr_t at=gameBase+0x65cdcc;at<end&&at<gameBase+0x65cdcc+40*4;at+=4){
        auto* actor=read<std::uint8_t*>(reinterpret_cast<void*>(at));if(!accessible(actor,0x134))continue;
        const auto life=read<std::int16_t>(actor,0xcc);const auto routine=read<std::uint32_t>(actor,4);
        auto& t=tracedEnemies[reinterpret_cast<std::uintptr_t>(actor)];
        if(t.life!=life||t.routine!=routine){++effectTraceLines;
            log("ENEMY_STATE serial=%u actor=%p model=%02x b8a=%02x life=%d->%d routine=%08x->%08x pos=%d,%d,%d",serial,actor,read<std::uint8_t>(actor,0x4a),read<std::uint8_t>(actor,0x8a),
                t.life,life,t.routine,routine,read<std::int32_t>(actor,0x34),read<std::int32_t>(actor,0x38),read<std::int32_t>(actor,0x3c));
            t.life=life;t.routine=routine;}
    }
}
namespace nemesis_cloud{inline bool owns(const void* effect);inline void onShadow(void* shadow);}   // fx 2026-11-08 nemesis-cloud
#include "blood_fx.h"
#include "fx_nemesis_cloud.h"   // fx 2026-11-08 nemesis-cloud
#include "fx_fire.h"   // fx 2026-11-08 fire
#include "fx_muzzle.h" // fx 2026-11-08 muzzle
inline void __cdecl effectLoopHook(){
    originalEffectLoop();
    std::lock_guard<std::recursive_mutex> guard(mutex);
    try{traceEffects();}catch(...){log("EFFECT_TRACE exception");}
    try{updateCases();}catch(...){log("MODEL_EXTRA resting-case-exception; native effects unaffected");}
    try{blood::update();}catch(...){once("blood-update-exception","HD blood skipped this frame");}
    try{nemesis_cloud::update();}catch(...){once("nemesis-cloud-update-exception","HD Nemesis cloud skipped this frame");}   // fx 2026-11-08 nemesis-cloud
    try{fire::update();}catch(...){once("fire-update-exception","HD fire skipped this frame");}   // fx 2026-11-08 fire
    try{muzzle::update();}catch(...){once("muzzle-update-exception","HD muzzle FX skipped this frame");} // fx 2026-11-08 muzzle
}

} // namespace re3extra

static void installExtraModels(){
    using namespace re3extra;
    if(installed||!modelActive||modelParity||!re3bridge::Installed())return;
    re3bridge::detail::Patch patches[6];
    if(!re3bridge::detail::BuildCallPatch(patches[0],gameBase+0x6973b,gameBase+0x69800,reinterpret_cast<void*>(doorBuildHook))||
       !re3bridge::detail::BuildCallPatch(patches[1],gameBase+0x699c6,gameBase+0x69a00,reinterpret_cast<void*>(doorObjectHook))||
       !re3bridge::detail::BuildCallPatch(patches[2],gameBase+0x583a2,gameBase+0x583d0,reinterpret_cast<void*>(billboardHook))||
       !re3bridge::detail::BuildCallPatch(patches[3],gameBase+0x3d3aa,gameBase+0x3dd00,reinterpret_cast<void*>(allocateHook))||
       !re3bridge::detail::BuildCallPatch(patches[4],gameBase+0x3d632,gameBase+0x3dd00,reinterpret_cast<void*>(allocateHook))||
       // gameplay frame: actors (42BBA6) then the effect loop 458130
       !re3bridge::detail::BuildCallPatch(patches[5],gameBase+0x2bbd7,gameBase+0x58130,reinterpret_cast<void*>(effectLoopHook))){
        log("MODEL_EXTRA_INSTALL_SKIPPED native call signature mismatch");return;
    }
    originalDoorBuild=reinterpret_cast<DoorBuild>(gameBase+0x69800);
    originalDoorObject=reinterpret_cast<DoorObjectBuild>(gameBase+0x69a00);
    originalBillboard=reinterpret_cast<BillboardBuild>(gameBase+0x583d0);
    originalAllocate=reinterpret_cast<AllocateEffect>(gameBase+0x3dd00);
    originalEffectLoop=reinterpret_cast<EffectLoop>(gameBase+0x58130);
    unsigned done=0;for(;done<6;++done)if(!re3bridge::detail::WritePatch(patches[done],true))break;
    if(done!=6){while(done)re3bridge::detail::WritePatch(patches[--done],false);log("MODEL_EXTRA_INSTALL_FAILED native calls restored");return;}
    muzzle::install(); // fx 2026-11-08 muzzle (before the blood install line: its hooks only need the effect hooks above)
    installed=true;blood::install();nemesis_cloud::install();/* fx 2026-11-08 nemesis-cloud */log("MODEL_EXTRA_INSTALL_OK doors: any DO2 from props.txt (%zu door keys) + CORE02 cases (pistol/nickel/rifle/shell/blue shell/magnum/gatling, resting max %zu); original simulation retained",
        re3prop::table.doors.size(),kMaxResting);
    fire::install();   // fx 2026-11-08 fire
}
