#pragma once
#include "model_grip_lock.h"
#include "model_hand_ik.h"
#include "model_wounds.h"
#include "model_fps60.h"
#include <map>

// Native pose/order adapter glue. Included after the established D3D9 and
// background helpers in proxy.cpp; all disk game resources remain original.
struct ModelAsset {
    re3model::Mesh mesh;
    std::vector<re3model::MaterialTextures> textures;
    std::string id;
    bool missing=false;
    bool dualQuaternion=false;
    bool affineDualQuaternion=false;
    re3shoulder::ShoulderCorrective shoulderCorrective;
    re3poseweights::PoseWeights poseWeights; // pose-weights.txt (round 3): sleeve weights blended in for forward-raised arms
    re3face::FaceRig face;
    bool characterBody=false; // a whole-body actor replacement (near-cut and part-visibility guards apply)
    size_t bytes=0,textureBytes=0;uint32_t lastUsed=0;
    uint32_t retryBuild=0; // missing because a texture failed to load (fix 2026-10-31): getModel loads it again from this build
    // regions.txt (zombies): body region of every material, for per-actor clothing variants and the headless state
    enum Region:uint8_t{Upper=0,Lower=1,Head=2,Stump=3,Other=255};
    std::vector<uint8_t> materialRegion;
    // parts.txt (fix 2026-10-25, EM36 acid R509): per material the native parts that must be present / absent (bit masks)
    std::vector<std::pair<uint32_t,uint32_t>> materialParts;
    // fix 2026-10-25: bone with the largest summed weight of every triangle / vertex (enemy parts gone or split off), on first use
    std::vector<uint8_t> triangleBone,vertexBone;std::map<uint32_t,std::shared_ptr<ModelAsset>> caps;
    // var-<group>.png textures of the swap-set looks, loaded on first use (nullptr = missing file)
    std::unordered_map<std::string,IDirect3DTexture9*> variantTextures;
    // wrap.r3wrap (v10 overlays, Jill's harness): every overlay vertex rides a triangle of the actor's skinned HD body:
    // barycentric point, offset and normal in that triangle's frame (smooth normal, first edge, their cross product)
    struct WrapEntry{uint32_t v[3];float b1,b2,offset[3],normal[3];};
    std::vector<WrapEntry> wrap;uint32_t wrapBodyVertices=0;
    // follow.txt (Nemesis back worms EM36/3A parts 16-21): bone k of this extra mesh is station k of the native part's
    // own vertices (centroid of an index group), read every frame from the part's mesh object, where the engine writes
    // its morph (writhing) in place (4381F0: base copy + weighted deltas into mesh+0, 8-byte vertices)
    std::vector<std::vector<uint16_t>> followStations;uint32_t followVertices=0;bool followMoved=false;
    ~ModelAsset(){for(auto& t:textures){if(t.albedo)t.albedo->Release();if(t.stp)t.stp->Release();}for(auto& v:variantTextures)if(v.second)v.second->Release();}
};
struct PendingFrame;
struct ModelJob {
    std::shared_ptr<re3model::GpuFrame> geometry;
    size_t slice=0;
    std::shared_ptr<PendingFrame> pending;   // deferred prepare (fix 2026-10-25d): resolved to geometry/slice at the first draw
    uint16_t bucket=0;
    re3model::DrawOptions options{};
    std::string name;
    std::string captureTag;
};
static re3model::ModelRenderer modelRenderer;
// fps60 (model_fps60.h): F7 / pilot.ini fps60; recording happens in the actor prepare (fps60Actor), the in-between pass
// sets fps60Pass and draws every job from its frame's in-between copy (fps60Mids, dropped after the pass)
static bool fps60Enabled=true,fps60Pass=false;
static const void* fps60Actor=nullptr;
static std::vector<std::pair<const void*,unsigned>> fps60Occurrences;
static std::unordered_map<const re3model::GpuFrame*,std::shared_ptr<re3model::GpuFrame>> fps60Mids;
static std::uint64_t fps60PassEpoch=0;
static float fps60PassWeight=0.5f;   // how far from the build before toward the shown one (0.5 + lateness of the pass)
static unsigned fps60PassMoved=0,fps60PassKept=0,fps60PassFailed=0;
static double fps60PassReprojectMs=0,fps60PassUploadMs=0,fps60PassDrawMs=0,fps60PassResolveMs=0;
// fps60 camera cuts (2026-10-28): stage/room/camera of the game logic when each build began, [serial & 63] = {serial, key}
static uint32_t fps60BuildCamera[64][2]{};
// One recording per draw of the current actor: its InterpData, or null (fps60 off, outside an actor prepare).
static std::shared_ptr<re3fps60::InterpData> fps60Record(const void* asset,const std::vector<re3model::SkinnedVertex>& skinned,const re3model::Projection& projection){
    if(!fps60Enabled||!fps60Actor||skinned.empty())return {};
    unsigned occurrence=0;bool found=false;
    for(auto& entry:fps60Occurrences)if(entry.first==asset){occurrence=++entry.second;found=true;break;}
    if(!found)fps60Occurrences.push_back({asset,0});
    return re3fps60::Record(re3fps60::Key(fps60Actor,asset,occurrence),re3bridge::BuildSerial(),skinned,projection);
}
static std::unordered_map<std::string,std::shared_ptr<ModelAsset>> modelAssets;
static std::unordered_set<std::string> modelObserved;
static bool modelParity=false,modelActive=false,modelFatal=false;
static bool modelJillAllRooms=false;
static unsigned modelDraws=0;
static unsigned modelPreparedActorMask=0,modelRenderedActorMask=0,modelPreparedActors=0;
static uint64_t modelAffineFallbacks=0;
static bool modelAutoCapture=true;
struct ModelPerf {unsigned count=0;double skin=0,prepare=0,upload=0,maximum=0;};
static std::unordered_map<std::string,ModelPerf> modelPerf;
static double modelClockMs(){static LARGE_INTEGER frequency=[](){LARGE_INTEGER n;QueryPerformanceFrequency(&n);return n;}();LARGE_INTEGER now;QueryPerformanceCounter(&now);return double(now.QuadPart)*1000.0/double(frequency.QuadPart);}
// CPU time of our work per native frame: actor preparation (skin, project, upload) and slice drawing.
static double modelFramePrepareMs=0,modelFrameDrawMs=0;static unsigned modelFrameJobs=0;
struct ModelCostTimer{double& sink;double start;explicit ModelCostTimer(double& target):sink(target),start(modelClockMs()){}~ModelCostTimer(){sink+=modelClockMs()-start;}};
#include "model_pose_sequence.h"
#include <map>
static const char* modelLastSkinMode="unknown";
static re3shoulder::Factors modelLastShoulderFactors{};

static void modelMessage(void*,const char* message){log("MODEL %s",message);}
// ---------------------------------------------------------------- facial animation
static bool modelFaceEnabled=true;
static bool modelMotionSmoothing=true;
static re3face::LipSync faceLipSync;
struct VoiceChannelState{char name[16]{};double startTime=0;bool started=false;const re3face::LipClip* clip=nullptr;};
static VoiceChannelState voiceChannels[0x15];
static int(__thiscall* originalVoiceLoad)(void*,int,const char*)=nullptr;
static int(__thiscall* originalVoicePlay)(void*,int)=nullptr;
static double faceClock(){return modelClockMs()/1000.0;}
// Native sound object vtable 0x50A8A4: +0x18 load(channel,path), +0x1C play(channel), +0x30 isPlaying(channel).
static int __fastcall voiceLoadHook(void* self,void*,int channel,const char* path){
    const int result=originalVoiceLoad(self,channel,path);
    if(channel>=0&&channel<0x15){
        auto& state=voiceChannels[channel];state=VoiceChannelState{};
        if(path){
            std::string text(path,strnlen(path,1024));std::string lower=text;for(auto& c:lower)c=char(tolower((unsigned char)c));
            if(lower.find("voice")!=std::string::npos){
                const auto slash=text.find_last_of("/\\");std::string base=text.substr(slash==std::string::npos?0:slash+1);
                const auto dot=base.find('.');if(dot!=std::string::npos)base.resize(dot);
                strncpy_s(state.name,base.c_str(),_TRUNCATE);
                state.clip=faceLipSync.Valid()?faceLipSync.Find(state.name):nullptr;
            }
        }
    }
    return result;
}
static int __fastcall voicePlayHook(void* self,void*,int channel){
    const int result=originalVoicePlay(self,channel);
    if(channel>=0&&channel<0x15&&voiceChannels[channel].name[0]){
        auto& state=voiceChannels[channel];state.startTime=faceClock();state.started=true;
        log("VOICE_START channel=%d name=%s track=%s speaker=%d",channel,state.name,state.clip?"yes":"no",state.clip?int(state.clip->speaker):-1);
    }
    return result;
}
static bool voiceStillPlaying(int channel){
    auto object=*reinterpret_cast<void**>(gameBase+0x1349e0);
    if(!object||*reinterpret_cast<void**>(object)!=reinterpret_cast<void*>(gameBase+0x10a8a4))return false;
    auto isPlaying=reinterpret_cast<int(__thiscall*)(void*,int)>(gameBase+0x9300);
    return isPlaying(object,channel)!=0;
}
static void installVoiceHooks(){
    auto table=reinterpret_cast<void**>(gameBase+0x10a8a4);
    if(table[0x18/4]!=reinterpret_cast<void*>(gameBase+0x91c0)||table[0x1c/4]!=reinterpret_cast<void*>(gameBase+0x9240)||table[0x30/4]!=reinterpret_cast<void*>(gameBase+0x9300)){log("FACE_VOICE_HOOKS_SKIPPED sound vtable signature mismatch");return;}
    const uint8_t loadSig[]={0x81,0xec,0x00,0x01,0x00,0x00},playSig[]={0x8b,0x54,0x24,0x04,0x8b,0x01,0x52,0xff,0x50,0x4c},busySig[]={0x8b,0x44,0x24,0x04,0x85,0xc0,0x7c,0x40,0x83,0xf8,0x15};
    if(memcmp(reinterpret_cast<void*>(gameBase+0x91c0),loadSig,sizeof(loadSig))||memcmp(reinterpret_cast<void*>(gameBase+0x9240),playSig,sizeof(playSig))||memcmp(reinterpret_cast<void*>(gameBase+0x9300),busySig,sizeof(busySig))){log("FACE_VOICE_HOOKS_SKIPPED sound method signature mismatch");return;}
    originalVoiceLoad=reinterpret_cast<int(__thiscall*)(void*,int,const char*)>(table[0x18/4]);
    originalVoicePlay=reinterpret_cast<int(__thiscall*)(void*,int)>(table[0x1c/4]);
    DWORD protection,unused;VirtualProtect(table+0x18/4,8,PAGE_READWRITE,&protection);
    table[0x18/4]=reinterpret_cast<void*>(voiceLoadHook);table[0x1c/4]=reinterpret_cast<void*>(voicePlayHook);
    VirtualProtect(table+0x18/4,8,protection,&unused);
    log("FACE_VOICE_HOOKS_INSTALLED lipsync_clips=%zu",faceLipSync.ClipCount());
}
struct FaceActorRecord{re3face::ActorFace face;re3model::Vec3 eyeWorld{};double eyeTime=-1;bool lipActive=false;};
// ---------------------------------------------------------------- per-actor state
// Keyed by the native actor object. A slot that changes model (enemy slots are reused) or that was not
// prepared for a while starts over; the smoothers also restart by themselves after a >3 frame gap.
struct ActorRuntime{
    uint8_t modelId=0xff;std::string asset;uint32_t lastSerial=0;
    re3motion::Smoother motion;re3motion::JointSmoother joints;FaceActorRecord face;
    // v12 enemies: wounds of the player's shots, the parts in the native damage-red state and their per-vertex red
    std::vector<re3wound::Wound> wounds;uint32_t damagedParts=0;std::vector<uint32_t> damageTint;
    re3handik::State handIk;   // fix 2026-10-25b: player left-hand pose rows (hand-ik.txt)
    uint32_t dropSerial=0;float drop=0;   // fix 2026-10-25c: Western Custom aim, arms lowered (0..1)
};
static std::unordered_map<void*,ActorRuntime> actorRuntime;
static ActorRuntime& actorState(void* actor,uint8_t modelId,const std::string& asset,uint32_t serial){
    if(actorRuntime.size()>64)for(auto it=actorRuntime.begin();it!=actorRuntime.end();)if(serial-it->second.lastSerial>600)it=actorRuntime.erase(it);else ++it;
    auto& state=actorRuntime[actor];
    if(state.modelId!=modelId||state.asset!=asset){state=ActorRuntime{};state.modelId=modelId;state.asset=asset;}
    state.lastSerial=serial;return state;
}
// ---------------------------------------------------------------- character table
// model-pack/actors.txt adds whole-body replacements beyond the built-in Jill (player, costume 0) and Dario (EM54):
//   enemy  <EM id hex> -         <asset> <speaker|-> <geometry parts>
//   player <PL id hex> <costume|-> <asset> <speaker|-> <geometry parts>      (player id = actor+0x4A, 8 = Carlos)
// speaker: lip-sync speaker id in lipsync.r3lip (0 Jill, 1 Dario, 2 Brad, 3 Carlos, 4 Nikolai, 5 Mikhail; '-' none).
// geometry parts: native parts that carry surface; if one of them is hidden (blown-off head and similar) the
// actor stays native for that frame.
// Optional key=value fields after the parts:
//   bones=11            native skeleton size (15 default; the 11-bone zombies have no hand/foot bones; 1 = a rigid
//                       one-part creature such as the EM26 baby spider)
//   sig=P:V/T/Q,...     required part meshes (vertex/triangle/quad counts of the native 24-byte mesh object at
//                       part+8); zombie EMDs swap clothes/heads from section 13, only the listed look matches
//   weapon15=<asset>    part 15 is a hand+weapon replacement of hand part 4 (NPC handgun/rifle): when it is
//                       shown, the HD hand stays and <asset> (weapon only, hand-local) is drawn on it
//   weaponmesh=V/T/Q    counts of that hand+weapon mesh (file part 15): the engine draws the weapon by swapping
//                       the mesh objects of parts 4 and 15, so part 4 carrying these counts also means weapon15
//   weapons=<prefix>    player weapon meshes <prefix><weapon id hex> on hand bone 4 (model-pack/weapons.txt wins)
//   torso15=<group>     enemy part 15 is a spare torso (EM17): drawn by the HD body with the upper texture var-<group>
//   head=<part|->       head part for the blown-off-head stump (1 default; '-' none, a hidden head stays native)
//   extra<N>=<asset>    rigid HD mesh for the part N past the skeleton (Nemesis rocket launcher 17), drawn with
//                       that part's own native matrix while it is visible
//   keep=<N,...>        parts past the skeleton that stay native next to the HD body (Nemesis tentacles)
//   overlay=<asset>     extra HD mesh worn over the body (v10: Jill's harness PL01/EM5F), bound to the skinned body by the
//                       asset's wrap.r3wrap; a missing or mismatched overlay is skipped, the body still draws
//   headless=<asset>    player decapitated by a Hunter (head part 1 hidden and red): the body is drawn with <asset>, the
//                       same skeleton and vertex count without the head and with a neck stump (built-in Jill:
//                       jill-standard-headless)
//   blown<N>=<group>    part N in the native blown-off state (render 0x20, kept while hidden: the EM35 rocket launcher
//                       after the R30D cutscene) -> the body wears var-<group>.png (torn coat)
//   cut=<N,...>         skeleton parts the body pack can lose (its parts.txt hides/shows materials; EM36 acid in R509,
//                       EM38 death in R50D): hidden, falling off (flags 2|8) or shattering (render 0x20, drawn native) they
//                       keep the actor HD; extra<N>= of such a part = its HD piece while it falls
//   partscale=1         hidden non-geometry parts lose their HD triangles too (EM3E rescue helicopter, R50E: it grows from
//                       scale 0 to 4096 with skids and tail rotor hidden while flying in; scales go to the bones as for
//                       every enemy part)
// Visible parts past the skeleton that are not handled (gore pieces, unknown attachments) keep an enemy native.
struct PartSignature{uint8_t part=0;uint32_t vertices=0;uint16_t triangles=0,quads=0;};
struct ActorRule{bool player=false,anyCostume=false;uint8_t modelId=0;uint32_t costume=0;std::string asset;int speaker=-1;uint32_t geometry=0;unsigned bones=15;
    std::vector<PartSignature> signature;std::string weapon15,weapons,torso15,overlay,headless,blownGroup;bool hasWeaponMesh=false;PartSignature weaponMesh;
    int headPart=1,blownPart=-1;std::map<unsigned,std::string> extraParts;uint64_t keepNative=0;uint32_t cutParts=0;bool partScale=false;};
static std::vector<ActorRule> modelActorRules;
static bool modelCharactersAllRooms=true;
static void loadActorRules(){
    modelActorRules.clear();std::ifstream in(root+"/model-pack/actors.txt");std::string line;unsigned number=0;
    while(std::getline(in,line)){
        ++number;const auto hash=line.find('#');if(hash!=std::string::npos)line.resize(hash);
        std::istringstream fields(line);std::string kind,model,costume,asset,speaker,parts;
        if(!(fields>>kind))continue;
        ActorRule rule;bool ok=(kind=="enemy"||kind=="player")&&(fields>>model>>costume>>asset>>speaker>>parts);
        if(ok){
            rule.player=kind=="player";char* end=nullptr;const unsigned long id=strtoul(model.c_str(),&end,16);ok=*end==0&&id<256;rule.modelId=uint8_t(id);
            if(rule.player){if(costume=="-")rule.anyCostume=true;else{rule.costume=uint32_t(strtoul(costume.c_str(),&end,10));ok=ok&&*end==0;}}
            rule.asset=asset;ok=ok&&!asset.empty()&&asset.find_first_of("/\\.")==std::string::npos;
            if(speaker!="-"){rule.speaker=int(strtol(speaker.c_str(),&end,10));ok=ok&&*end==0&&rule.speaker>=0&&rule.speaker<16;}
            std::string option;
            while(ok&&(fields>>option)){
                const auto equal=option.find('=');if(equal==std::string::npos){ok=false;break;}
                const std::string key=option.substr(0,equal),value=option.substr(equal+1);
                if(key=="bones"){rule.bones=unsigned(strtoul(value.c_str(),&end,10));ok=*end==0&&rule.bones>=1&&rule.bones<=32;}
                else if(key=="partscale"){ok=value=="0"||value=="1";rule.partScale=value=="1";}
                else if(key=="head"){if(value=="-")rule.headPart=-1;else{rule.headPart=int(strtol(value.c_str(),&end,10));ok=*end==0&&rule.headPart>=0&&rule.headPart<32;}}
                else if(key.compare(0,5,"extra")==0&&key.size()>5){const unsigned part=unsigned(strtoul(key.c_str()+5,&end,10));
                    ok=*end==0&&part<64&&!value.empty()&&value.find_first_of("/\\.")==std::string::npos;if(ok)rule.extraParts[part]=value;}
                else if(key=="keep"){std::istringstream list(value);std::string item;
                    while(ok&&std::getline(list,item,',')){const unsigned long part=strtoul(item.c_str(),&end,10);ok=*end==0&&part<64;if(ok)rule.keepNative|=1ull<<part;}}
                else if(key=="cut"){std::istringstream list(value);std::string item;ok=!value.empty()&&value.back()!=',';
                    while(ok&&std::getline(list,item,',')){const unsigned long part=strtoul(item.c_str(),&end,10);ok=!item.empty()&&*end==0&&part<32;if(ok)rule.cutParts|=1u<<part;}}
                else if(key=="weapon15"){rule.weapon15=value;ok=!value.empty()&&value.find_first_of("/\\.")==std::string::npos;}
                else if(key=="weapons"){rule.weapons=value;ok=!value.empty()&&value.find_first_of("/\\.")==std::string::npos;}
                // enemy extra part 15 that is a whole spare torso (EM17: shirt open over a torn chest): HD upper texture var-<group>.png
                else if(key=="torso15"){rule.torso15=value;ok=!value.empty()&&value.find_first_of("/\\.")==std::string::npos;}
                else if(key=="overlay"){rule.overlay=value;ok=!value.empty()&&value.find_first_of("/\\.")==std::string::npos;}
                else if(key=="headless"){rule.headless=value;ok=!value.empty()&&value.find_first_of("/\\.")==std::string::npos;}
                else if(key.compare(0,5,"blown")==0&&key.size()>5){rule.blownPart=int(strtoul(key.c_str()+5,&end,10));rule.blownGroup=value;
                    ok=*end==0&&rule.blownPart>=0&&rule.blownPart<64&&!value.empty()&&value.find_first_of("/\\.")==std::string::npos;}
                else if(key=="weaponmesh"){unsigned v=0,t=0,q=0;
                    ok=sscanf_s(value.c_str(),"%u/%u/%u",&v,&t,&q)==3&&v<65536&&t<65536&&q<65536;
                    rule.hasWeaponMesh=ok;rule.weaponMesh.part=4;rule.weaponMesh.vertices=v;rule.weaponMesh.triangles=uint16_t(t);rule.weaponMesh.quads=uint16_t(q);}
                else if(key=="sig"){
                    std::istringstream list(value);std::string item;
                    while(ok&&std::getline(list,item,',')){
                        PartSignature s;unsigned p=0,v=0,t=0,q=0;
                        ok=sscanf_s(item.c_str(),"%u:%u/%u/%u",&p,&v,&t,&q)==4&&p<32&&v<65536&&t<65536&&q<65536;
                        s.part=uint8_t(p);s.vertices=v;s.triangles=uint16_t(t);s.quads=uint16_t(q);if(ok)rule.signature.push_back(s);
                    }
                }else ok=false;
            }
            std::istringstream list(parts);std::string item;
            while(ok&&std::getline(list,item,',')){const long part=strtol(item.c_str(),&end,10);ok=*end==0&&part>=0&&part<long(rule.bones);if(ok)rule.geometry|=1u<<part;}
            ok=ok&&rule.geometry;
            if(ok&&!rule.player&&rule.modelId==0x54)ok=false; // Dario is built in
            if(ok&&rule.player&&rule.modelId==0&&(rule.anyCostume||rule.costume==0))ok=false; // standard Jill is built in
        }
        if(!ok){log("MODEL_ACTOR_RULE_INVALID line=%u",number);continue;}
        modelActorRules.push_back(rule);
        log("MODEL_ACTOR_RULE %s %02x costume=%u asset=%s speaker=%d geometry=%04x bones=%u signature=%zu weapon15=%s weaponmesh=%u/%u/%u weapons=%s torso15=%s head=%d extra=%zu keep=%llx overlay=%s headless=%s blown=%d:%s cut=%x",rule.player?"player":"enemy",rule.modelId,rule.costume,rule.asset.c_str(),rule.speaker,rule.geometry,rule.bones,rule.signature.size(),rule.weapon15.c_str(),rule.weaponMesh.vertices,rule.weaponMesh.triangles,rule.weaponMesh.quads,rule.weapons.c_str(),rule.torso15.c_str(),rule.headPart,rule.extraParts.size(),(unsigned long long)rule.keepNative,rule.overlay.c_str(),rule.headless.c_str(),rule.blownPart,rule.blownGroup.c_str(),rule.cutParts);
    }
}
// Vertex/triangle/quad counts of a part's native mesh object (24 bytes: vertex, normal, count, triangle and quad
// references, then u16 triangle and quad counts, the MD2 object layout of the file). The file's u32 vertex count
// is zero above 16 bits; the engine keeps its own value in the upper half at runtime (seen in the October 2026
// log: 38 vertices read as 0x000D0026), so only the low half is the count.
static bool partMeshCounts(const re3bridge::PartSnapshot& part,uint32_t& vertices,uint16_t& triangles,uint16_t& quads){
    if(!part.runtimeMesh||!re3bridge::detail::Accessible(part.runtimeMesh,24))return false;
    vertices=re3bridge::detail::Read<uint16_t>(part.runtimeMesh,8);triangles=re3bridge::detail::Read<uint16_t>(part.runtimeMesh,20);quads=re3bridge::detail::Read<uint16_t>(part.runtimeMesh,22);
    return true;
}
static std::string partSignatureText(const re3bridge::ActorContext& context){
    std::string text;char item[48];
    for(size_t i=0;i<context.bones.size()&&i<32;++i){uint32_t v=0;uint16_t t=0,q=0;
        if(partMeshCounts(context.bones[i],v,t,q))sprintf_s(item,"%s%zu:%u/%u/%u",text.empty()?"":",",i,v,t,q);else sprintf_s(item,"%s%zu:?",text.empty()?"":",",i);
        text+=item;}
    return text;
}
static bool signatureMatches(const ActorRule& rule,const re3bridge::ActorContext& context){
    for(const auto& s:rule.signature){uint32_t v=0;uint16_t t=0,q=0;
        if(s.part>=context.bones.size()||!partMeshCounts(context.bones[s.part],v,t,q)||v!=s.vertices||t!=s.triangles||q!=s.quads)return false;}
    return true;
}
// Evidence for zombie looks and gore states: every change of a part's flags, render attributes or mesh object
// on an enemy of actors.txt (head blow-off and similar), capped; diagnostic runs also dump the actor and part
// records once per actor and room.
static void logEnemyPartStates(const re3bridge::ActorContext& context){
    if(context.isPlayer)return;
    if(std::none_of(modelActorRules.begin(),modelActorRules.end(),[&](const ActorRule& r){return !r.player&&r.modelId==context.modelId;}))return;
    struct PartState{uint32_t flags=0;uint16_t render=0;void* mesh=nullptr;int16_t scale[3]{};};
    static std::unordered_map<void*,std::pair<uint8_t,std::vector<PartState>>> previous;static unsigned lines=0;
    if(previous.size()>256)previous.clear();
    auto& entry=previous[context.actor];const bool fresh=entry.first!=context.modelId||entry.second.size()!=context.bones.size();
    if(fresh){entry.first=context.modelId;entry.second.assign(context.bones.size(),PartState{});}
    for(size_t i=0;i<context.bones.size();++i){const auto& b=context.bones[i];auto& old=entry.second[i];
        const bool changed=old.flags!=b.flags||old.render!=b.renderAttributes||old.mesh!=b.runtimeMesh||std::memcmp(old.scale,b.scale,sizeof(old.scale));
        if(changed&&!fresh&&lines<3000){++lines;uint32_t v=0;uint16_t t=0,q=0;partMeshCounts(b,v,t,q);
            log("MODEL_PART_STATE actor=%p model=%02x part=%zu flags=%x->%x render=%x->%x mesh=%p->%p counts=%u/%u/%u scale=%d,%d,%d->%d,%d,%d visible=%u room=%u%02x",
                context.actor,context.modelId,i,old.flags,b.flags,old.render,b.renderAttributes,old.mesh,b.runtimeMesh,v,t,q,old.scale[0],old.scale[1],old.scale[2],b.scale[0],b.scale[1],b.scale[2],b.visible,
                *(uint16_t*)(gameBase+0x661fa6),*(uint16_t*)(gameBase+0x661fa8));}
        old.flags=b.flags;old.render=b.renderAttributes;old.mesh=b.runtimeMesh;std::memcpy(old.scale,b.scale,sizeof(old.scale));
    }
    static bool diagnostics=GetPrivateProfileIntA("pilot","debug_warp",0,(root+"\\pilot.ini").c_str())!=0;static unsigned actorDumps=0;
    char key[64];sprintf_s(key,"dump:%p:%u:%u",context.actor,*(uint16_t*)(gameBase+0x661fa6),*(uint16_t*)(gameBase+0x661fa8));
    if(diagnostics&&actorDumps<96&&modelObserved.insert(key).second&&re3bridge::detail::Accessible(context.actor,0x200)){++actorDumps;
        auto hex=[](const void* data,size_t size){static const char* digits="0123456789abcdef";std::string text;text.reserve(size*2);for(size_t k=0;k<size;++k){const auto c=static_cast<const uint8_t*>(data)[k];text+=digits[c>>4];text+=digits[c&15];}return text;};
        log("MODEL_ACTOR_DUMP actor=%p model=%02x parts=%u room=%u%02x bytes=%s",context.actor,context.modelId,context.partCount,*(uint16_t*)(gameBase+0x661fa6),*(uint16_t*)(gameBase+0x661fa8),hex(context.actor,0x200).c_str());
        for(size_t i=0;i<context.bones.size();++i)log("MODEL_PART_DUMP actor=%p part=%zu bytes=%s mesh=%s",context.actor,i,hex(context.bones[i].nativePart,re3bridge::kPartStride).c_str(),
            context.bones[i].runtimeMesh&&re3bridge::detail::Accessible(context.bones[i].runtimeMesh,24)?hex(context.bones[i].runtimeMesh,24).c_str():"-");
    }
}
// ---------------------------------------------------------------- zombie looks (model-pack/looks.txt)
//   look <model hex> <slot> <triangle key> <quad key> <record> <group|-|!> <region> [body=<asset>]
// Every main (section 14) and swap (section 13) mesh record of the zombie EMDs, written by
// models/characters/variants/make_looks.py. The engine dresses each zombie from these records part by part (the
// swap records of EM1E/EM16 have the main vertex counts, only other UVs, so counts cannot tell them apart). The
// key of a part is (mesh object pointer - triangle pointer, mesh object pointer - quad pointer) = the record's
// index*24 minus its file offsets, the same wherever the EMD was loaded. group '-' = main (base texture), '!' = a
// record without an HD counterpart yet (the actor stays native), else var-<group>.png of the asset paints
// <region>. Two records of one file may share a key (EM13/EM18 first swap part): the one whose group other parts
// of the actor also use wins, else the main one. body=<asset>: the record belongs to another HD body with the same
// skeleton (the women of EM19/EM1D/EM1F are the EM1C body); an actor switches body only when every part names
// the same one, mixed bodies stay native.
struct LookRecord{uint8_t slot=0;std::string record,group,region,body;};
static std::unordered_map<uint8_t,std::unordered_map<int64_t,std::vector<LookRecord>>> modelLooks;
// Player weapons (model-pack/weapons.txt, v9): weapon <player model hex> <weapon id hex> <asset|-> [aux22=<asset>] [hide22]
// <asset> is the weapon-only mesh on hand bone 4; aux22 an HD mesh for the PLW's second mesh, actor part 22 (gatling
// barrels, mine-thrower front, the whole M37 of the lever spin), drawn with part 22's own motion relative to the hand;
// hide22 hides that native part (grenade-launcher barrel, which the HD weapon already carries). Ids not in the table
// keep the earlier rules (Jill: knife/M92F/M4A1/native weapons, Carlos: actors.txt weapons= prefix).
struct WeaponEntry{std::string main,aux22;bool hide22=false;};
static std::map<uint16_t,WeaponEntry> modelWeapons;
// Round glock18c: item <player model hex> <inventory item hex> <asset|-> [aux22=..] [hide22] -- the equipped inventory item
// (A623E4+129) picks the HD weapon when the native weapon id is only an alias (Glock 18C items 53/54 load as M92F 03).
static std::map<uint16_t,WeaponEntry> modelItemWeapons;
// Fix 2026-10-24, per HD weapon asset (player main or NPC weapon15), hand-4 frame:
//   muzzle <asset> dx dy dz [px py pz]   native muzzle effects move by d (HD muzzle - native muzzle), casing/port
//                                        effects by p (the shift this asset got relative to the accepted casings)
//   support <asset> fx fy fz px py pz    the left hand's palm point p (left-hand local) reaches foregrip f (ReachForegrip)
//   move22 <aux asset> dx dy dz          the aux22 piece (gatling barrels) moves by d in the hand frame with its weapon
struct WeaponFit{bool muzzle=false,support=false,move22=false;float shift[3]{},port[3]{},foregrip[3]{},palm[3]{},hand22[3]{};};
static std::map<std::string,WeaponFit> modelWeaponFits;
static std::vector<re3handik::Row> modelHandIk;   // fix 2026-10-25b: model-pack/hand-ik.txt (model_hand_ik.h)
// hand matrix (part 4 +0x40, the pointer the gun routines pass to 43D370) -> shifts of the HD weapon drawn on it
struct MuzzleHand{float shift[3],port[3];uint32_t serial;bool dropped;float drop[9],pivot[3];re3model::Matrix3x4 hand;};
static std::unordered_map<const void*,MuzzleHand> muzzleHands;
static void loadWeapons(){
    modelWeapons.clear();modelItemWeapons.clear();modelWeaponFits.clear();std::ifstream in(root+"/model-pack/weapons.txt");std::string line;unsigned number=0;
    while(std::getline(in,line)){
        ++number;const auto hash=line.find('#');if(hash!=std::string::npos)line.resize(hash);
        std::istringstream fields(line);std::string kind,model,weapon,main,option;if(!(fields>>kind))continue;
        if(kind=="muzzle"||kind=="support"||kind=="move22"){
            std::string asset;float v[6]{};unsigned n=0;bool ok=bool(fields>>asset)&&!asset.empty()&&asset.find_first_of("/\\.")==std::string::npos;
            while(ok&&n<6&&(fields>>v[n]))++n;
            std::string extra;ok=ok&&!(fields>>extra);
            ok=ok&&std::all_of(v,v+6,[](float x){return std::isfinite(x)&&std::fabs(x)<4000.f;})&&(kind=="muzzle"?(n==3||n==6):kind=="move22"?n==3:n==6);
            if(!ok){log("MODEL_WEAPON_RULE_INVALID line=%u",number);continue;}
            auto& fit=modelWeaponFits[asset];
            if(kind=="muzzle"){fit.muzzle=true;std::copy(v,v+3,fit.shift);std::copy(v+3,v+6,fit.port);}
            else if(kind=="move22"){fit.move22=true;std::copy(v,v+3,fit.hand22);}
            else{fit.support=true;std::copy(v,v+3,fit.foregrip);std::copy(v+3,v+6,fit.palm);}
            continue;
        }
        char* end=nullptr;bool ok=(kind=="weapon"||kind=="item")&&(fields>>model>>weapon>>main);WeaponEntry entry;unsigned long m=0,w=0;
        if(ok){m=strtoul(model.c_str(),&end,16);ok=*end==0&&m<256;w=strtoul(weapon.c_str(),&end,16);ok=ok&&*end==0&&w<256;}
        auto safe=[](const std::string& v){return !v.empty()&&v.find_first_of("/\\.")==std::string::npos;};
        if(ok){if(main!="-"){ok=safe(main);entry.main=main;}}
        while(ok&&(fields>>option)){if(option=="hide22")entry.hide22=true;else if(option.compare(0,6,"aux22=")==0){entry.aux22=option.substr(6);ok=safe(entry.aux22);}else ok=false;}
        if(!ok){log("MODEL_WEAPON_RULE_INVALID line=%u",number);continue;}
        (kind=="item"?modelItemWeapons:modelWeapons)[uint16_t((m<<8)|w)]=entry;
    }
    log("MODEL_WEAPON_TABLE entries=%zu items=%zu fits=%zu",modelWeapons.size(),modelItemWeapons.size(),modelWeaponFits.size());
}
static void loadLooks(){
    modelLooks.clear();std::ifstream in(root+"/model-pack/looks.txt");std::string line;unsigned count=0,bad=0;
    while(std::getline(in,line)){
        const auto hash=line.find('#');if(hash!=std::string::npos)line.resize(hash);
        std::istringstream fields(line);std::string kind,model;unsigned slot=0;long long tri=0,quad=0;LookRecord r;
        if(!(fields>>kind))continue;
        if(kind!="look"||!(fields>>model>>slot>>tri>>quad>>r.record>>r.group>>r.region)||slot>=32){++bad;continue;}
        std::string option;if(fields>>option){if(option.compare(0,5,"body=")||option.size()<6||option.find_first_of("/\\.")!=std::string::npos){++bad;continue;}r.body=option.substr(5);}
        char* end=nullptr;const unsigned long id=strtoul(model.c_str(),&end,16);if(*end||id>255){++bad;continue;}
        r.slot=uint8_t(slot);modelLooks[uint8_t(id)][(int64_t(int32_t(tri))<<32)|uint32_t(int32_t(quad))].push_back(r);++count;
    }
    if(count||bad)log("MODEL_LOOKS records=%u models=%zu invalid_lines=%u",count,modelLooks.size(),bad);
}
static bool partLookKey(const re3bridge::PartSnapshot& part,int64_t& key){
    if(!part.runtimeMesh||!re3bridge::detail::Accessible(part.runtimeMesh,24))return false;
    const uint32_t object=uint32_t(reinterpret_cast<uintptr_t>(part.runtimeMesh));
    const uint32_t triangles=re3bridge::detail::Read<uint32_t>(part.runtimeMesh,12),quads=re3bridge::detail::Read<uint32_t>(part.runtimeMesh,16);
    key=(int64_t(int32_t(object-triangles))<<32)|uint32_t(object-quads);return true;
}
struct ResolvedLook{bool valid=false;std::string text,regionGroup[3],body;};
static ResolvedLook modelLastLook;
static bool resolveLook(const ActorRule& rule,const re3bridge::ActorContext& context,const std::unordered_map<int64_t,std::vector<LookRecord>>& table,ResolvedLook& out){
    out=ResolvedLook{};const LookRecord* chosen[32]{};std::vector<const std::vector<LookRecord>*> candidates(rule.bones,nullptr);
    for(unsigned slot=0;slot<rule.bones&&slot<context.bones.size();++slot){
        if(!((rule.geometry>>slot)&1u))continue;
        int64_t key=0;if(!partLookKey(context.bones[slot],key))return false;
        auto hit=table.find(key);const std::vector<LookRecord>* list=hit==table.end()?nullptr:&hit->second;
        bool slotMatch=false;if(list)for(const auto& r:*list)slotMatch|=r.slot==slot;
        if(!slotMatch){char note[96];sprintf_s(note,"look-unknown:%02x:%u:%lld",context.modelId,slot,key);
            if(modelObserved.size()<4000&&modelObserved.insert(note).second)log("MODEL_LOOK_UNKNOWN model=%02x slot=%u key=%d/%d",context.modelId,slot,int32_t(key>>32),int32_t(uint32_t(key)));
            return false;}
        candidates[slot]=list;
    }
    // unambiguous records first, then ambiguous keys by the groups the other parts use
    std::unordered_set<std::string> groups;
    for(unsigned slot=0;slot<candidates.size();++slot)if(candidates[slot]){unsigned n=0;const LookRecord* only=nullptr;
        for(const auto& r:*candidates[slot])if(r.slot==slot){++n;only=&r;}
        if(n==1){chosen[slot]=only;if(only->group!="-")groups.insert(only->group);}}
    for(unsigned slot=0;slot<candidates.size();++slot)if(candidates[slot]&&!chosen[slot]){const LookRecord* main=nullptr;
        for(const auto& r:*candidates[slot])if(r.slot==slot){if(r.group=="-")main=&r;else if(groups.count(r.group))chosen[slot]=&r;}
        if(!chosen[slot])for(const auto& r:*candidates[slot])if(r.slot==slot){chosen[slot]=main?main:&r;break;}}
    bool supported=true;unsigned withBody=0,parts=0;
    for(unsigned slot=0;slot<candidates.size();++slot)if(chosen[slot]){++parts;const auto& r=*chosen[slot];
        if(!r.body.empty()){if(out.body.empty())out.body=r.body;if(out.body==r.body)++withBody;}
        if(!out.text.empty())out.text+=",";out.text+=r.record;
        if(r.group=="!"){supported=false;continue;}
        if(r.group=="-")continue;
        const int region=r.region=="upper"?0:r.region=="lower"?1:r.region=="head"?2:-1;
        if(region<0)continue;
        // several sets on one region (EM13 boots with the patterned skirt): sorted "a+b" names the composite
        // var-a+b.png (variants/make_combos.py); prepareModelActor falls back to the first set without it
        auto& names=out.regionGroup[region];std::vector<std::string> list;
        for(size_t start=0;!names.empty()&&start<=names.size();){const size_t plus=names.find('+',start);list.push_back(names.substr(start,plus==std::string::npos?std::string::npos:plus-start));if(plus==std::string::npos)break;start=plus+1;}
        if(std::find(list.begin(),list.end(),r.group)==list.end()){list.push_back(r.group);std::sort(list.begin(),list.end());
            names.clear();for(const auto& n:list){if(!names.empty())names+="+";names+=n;}}
    }
    if(!supported){if(modelObserved.size()<4000&&modelObserved.insert("look-native:"+out.text).second)log("MODEL_LOOK_NATIVE model=%02x look=%s (no HD variant yet)",context.modelId,out.text.c_str());return false;}
    if(withBody&&withBody!=parts){if(modelObserved.size()<4000&&modelObserved.insert("look-mixed-body:"+out.text).second)log("MODEL_LOOK_NATIVE model=%02x look=%s (parts of two bodies)",context.modelId,out.text.c_str());return false;}
    out.valid=true;return true;
}
static const ActorRule* findActorRule(const re3bridge::ActorContext& context){
    logEnemyPartStates(context);
    // every distinct enemy look is logged once: this is how in-game swap sets show up in runtime.log
    if(!context.isPlayer){const auto text=partSignatureText(context);char key[16];sprintf_s(key,"sig:%02x:",context.modelId);
        if(modelObserved.size()<4000&&modelObserved.insert(key+text).second)log("MODEL_PART_SIGNATURE model=%02x parts=%u %s",context.modelId,context.partCount,text.c_str());}
    modelLastLook=ResolvedLook{};
    const auto looks=context.isPlayer?modelLooks.end():modelLooks.find(context.modelId);
    for(const auto& rule:modelActorRules)
        if(rule.player==context.isPlayer&&rule.modelId==context.modelId&&(!rule.player||rule.anyCostume||rule.costume==context.playerCostume)){
            if(looks!=modelLooks.end()){if(resolveLook(rule,context,looks->second,modelLastLook))return &rule;}
            else if(signatureMatches(rule,context))return &rule;
        }
    return nullptr;
}
struct ModelAsset;
static void driveFace(const std::shared_ptr<ModelAsset>& asset,ActorRuntime& actor,int speaker,const std::vector<re3model::Matrix3x4>& bones);
static bool pilotRoom(){return *(uint16_t*)(gameBase+0x661fa6)==0&&*(uint16_t*)(gameBase+0x661fa8)<=1;}
static std::shared_ptr<ModelAsset> getModel(const std::string& id){
    static IDirect3DDevice9* assetDevice=nullptr;if(assetDevice!=gameDevice){modelAssets.clear();modelObserved.clear();modelFatal=false;assetDevice=gameDevice;}
    auto found=modelAssets.find(id);
    if(found!=modelAssets.end()&&found->second->retryBuild&&int32_t(re3bridge::BuildSerial()-found->second->retryBuild)>=0){log("MODEL_TEXTURE_RETRY %s",id.c_str());modelAssets.erase(found);found=modelAssets.end();}
    if(found!=modelAssets.end()){found->second->lastUsed=re3bridge::BuildSerial();return found->second->missing?nullptr:found->second;}
    auto asset=std::make_shared<ModelAsset>();asset->id=id;asset->lastUsed=re3bridge::BuildSerial();modelAssets[id]=asset;
    std::string relative="model-pack/"+id+"/",error;
    {std::ifstream mode(root+"/"+relative+"skinning.txt");std::string selected;if(mode>>selected){if(selected=="dual-quaternion")asset->dualQuaternion=true;else if(selected=="affine-dual-quaternion")asset->affineDualQuaternion=true;else if(selected!="linear"){asset->missing=true;log("MODEL_UNAVAILABLE %s unknown skinning mode",id.c_str());return nullptr;}}}
    if(!re3model::LoadMesh(root+"/"+relative+"model.r3mesh",asset->mesh,&error)){asset->missing=true;log("MODEL_UNAVAILABLE %s %s",id.c_str(),error.c_str());return nullptr;}
    asset->bytes=asset->mesh.vertices.size()*sizeof(re3model::Vertex)+asset->mesh.indices.size()*sizeof(uint32_t)+asset->mesh.canonicalVertex.capacity()*sizeof(uint32_t);
    {std::string note;auto& light=re3model::AssetLightingStore()[id];light=re3model::LoadAssetLighting(root+"/"+relative,asset->mesh,note);asset->bytes+=light.occlusion.size();
        if(!note.empty())log("%s %s",note.c_str(),id.c_str());}
    {std::ifstream wrap(root+"/"+relative+"wrap.r3wrap",std::ios::binary);
        if(wrap){
            static_assert(sizeof(ModelAsset::WrapEntry)==44,"R3WRAP1 entry layout");
            char magic[8]{};uint32_t head[4]{};wrap.read(magic,8);wrap.read(reinterpret_cast<char*>(head),sizeof(head));
            bool ok=wrap&&!std::memcmp(magic,"R3WRAP1",8)&&head[0]==1&&head[1]==asset->mesh.vertices.size()&&head[2]>0;
            if(ok){asset->wrap.resize(head[1]);wrap.read(reinterpret_cast<char*>(asset->wrap.data()),std::streamsize(head[1]*sizeof(ModelAsset::WrapEntry)));ok=bool(wrap);}
            for(size_t i=0;ok&&i<asset->wrap.size();++i)for(unsigned k=0;k<3;++k)ok=ok&&asset->wrap[i].v[k]<head[2];
            if(!ok){asset->missing=true;log("MODEL_UNAVAILABLE %s invalid wrap.r3wrap",id.c_str());return nullptr;}
            asset->wrapBodyVertices=head[2];asset->bytes+=asset->wrap.size()*sizeof(ModelAsset::WrapEntry);
        }}
    {std::ifstream follow(root+"/"+relative+"follow.txt");std::string line;
        while(follow&&std::getline(follow,line)){std::istringstream f(line);std::string key;if(!(f>>key)||key[0]=='#')continue;
            if(key=="vertices")f>>asset->followVertices;
            else if(key=="station"){std::vector<uint16_t> s;unsigned i=0;while(f>>i)s.push_back(uint16_t(i));asset->followStations.push_back(s);}}
        bool ok=asset->followStations.empty()||(asset->followVertices&&asset->followVertices<=1024&&asset->followStations.size()==asset->mesh.inverseBind.size());
        for(const auto& s:asset->followStations){ok=ok&&!s.empty();for(auto i:s)ok=ok&&i<asset->followVertices;}
        if(!ok){asset->missing=true;log("MODEL_UNAVAILABLE %s invalid follow.txt",id.c_str());return nullptr;}}
    if(asset->affineDualQuaternion){
        if(!re3shoulder::LoadShoulderCorrective(root+"/"+relative+"shoulder.r3corrective",root+"/"+relative+"model.r3mesh",asset->mesh,asset->shoulderCorrective,&error)){
            asset->missing=true;log("MODEL_UNAVAILABLE %s required shoulder corrective: %s",id.c_str(),error.c_str());return nullptr;
        }
        asset->bytes+=asset->shoulderCorrective.MemoryBytes();
        log("MODEL_SHOULDER_CORRECTIVE %s format=%u entries=%zu raised=%u headLift=%.3f mesh-identity=verified",id.c_str(),asset->shoulderCorrective.FormatVersion(),asset->shoulderCorrective.EntryCount(),asset->shoulderCorrective.HasRaised()?1u:0u,asset->shoulderCorrective.HeadPitchLiftDegrees());
    }
    {const std::string posePath=root+"/"+relative+"pose-weights.txt";
        if(GetFileAttributesA(posePath.c_str())!=INVALID_FILE_ATTRIBUTES){
            if(re3poseweights::Load(posePath,root+"/"+relative+"model.r3mesh",asset->mesh,asset->poseWeights,&error)){
                asset->bytes+=asset->poseWeights.MemoryBytes();
                log("MODEL_POSE_WEIGHTS %s entries=%zu mesh-identity=verified",id.c_str(),asset->poseWeights.entries.size());
            }else log("MODEL_POSE_WEIGHTS_UNAVAILABLE %s %s; plain skin kept",id.c_str(),error.c_str());
        }
    }
    if(modelFaceEnabled){
        const std::string facePath=root+"/"+relative+"face.r3face";
        if(GetFileAttributesA(facePath.c_str())!=INVALID_FILE_ATTRIBUTES){
            if(re3face::LoadFaceRig(facePath,root+"/"+relative+"model.r3mesh",asset->mesh,asset->face,&error)){
                asset->bytes+=asset->face.MemoryBytes();
                log("MODEL_FACE_RIG %s deformers=%zu affected=%zu mesh-identity=verified",id.c_str(),asset->face.DeformerCount(),asset->face.AffectedCount());
            }else log("MODEL_FACE_RIG_UNAVAILABLE %s %s; static face kept",id.c_str(),error.c_str());
        }
    }
    const UINT mipLevels=id.compare(0,7,"parity-")?8:1; // diagnostic native-palette parity assets stay single-level
    std::unordered_map<std::string,IDirect3DTexture9*> loaded; // materials sharing one image share one texture
    auto load=[&](const std::string& path)->IDirect3DTexture9*{
        auto hit=loaded.find(path);if(hit!=loaded.end()){if(hit->second)hit->second->AddRef();return hit->second;}
        auto tex=loadReplacement(gameDevice,0,relative+path,mipLevels);loaded[path]=tex;
        if(tex){D3DSURFACE_DESC desc{};if(SUCCEEDED(tex->GetLevelDesc(0,&desc)))(desc.Pool==D3DPOOL_DEFAULT?asset->textureBytes:asset->bytes)+=size_t(desc.Width)*desc.Height*4;}
        return tex;};
    for(const auto& material:asset->mesh.materials){
        re3model::MaterialTextures texture;
        texture.albedo=load(material.albedoPath);
        if(!material.stpPath.empty())texture.stp=load(material.stpPath);
        asset->textures.push_back(texture);
        if(!texture.albedo||(!material.stpPath.empty()&&!texture.stp)){asset->missing=true;
            // a failed load (address space, video memory) may pass: native model meanwhile, another try after 10 s, doubling up to ~5 min
            static std::unordered_map<std::string,unsigned> retries;const unsigned n=(std::min)(retries[id]++,5u);
            asset->retryBuild=(std::max)(1u,re3bridge::BuildSerial()+(300u<<n));log("MODEL_TEXTURE_UNAVAILABLE %s retry_in_builds=%u",id.c_str(),300u<<n);return nullptr;}
    }
    {std::ifstream regions(root+"/"+relative+"regions.txt");std::string name;
        while(regions>>name)asset->materialRegion.push_back(name=="upper"?ModelAsset::Upper:name=="lower"?ModelAsset::Lower:name=="head"?ModelAsset::Head:name=="stump"?ModelAsset::Stump:ModelAsset::Other);
        if(!asset->materialRegion.empty()&&asset->materialRegion.size()!=asset->mesh.materials.size()){log("MODEL_REGIONS_INVALID %s materials=%zu regions=%zu",id.c_str(),asset->mesh.materials.size(),asset->materialRegion.size());asset->materialRegion.clear();}}
    // parts.txt: one line per material, '*' or a +P/-P list (P<32): drawn while every +P part is present and every -P part absent
    {std::ifstream parts(root+"/"+relative+"parts.txt");std::string line;bool ok=true;
        while(parts&&std::getline(parts,line)){if(!line.empty()&&line.back()=='\r')line.pop_back();if(line.empty()||line[0]=='#')continue;std::pair<uint32_t,uint32_t> c{0,0};
            if(line!="*"){std::istringstream list(line);std::string item;while(ok&&std::getline(list,item,',')){char* end=nullptr;const unsigned long n=item.size()>1?strtoul(item.c_str()+1,&end,10):99;
                ok=(item[0]=='+'||item[0]=='-')&&end&&*end==0&&n<32;if(ok)(item[0]=='+'?c.first:c.second)|=1u<<n;}}
            asset->materialParts.push_back(c);}
        if(!asset->materialParts.empty()&&(!ok||asset->materialParts.size()!=asset->mesh.materials.size())){log("MODEL_PARTS_INVALID %s materials=%zu lines=%zu",id.c_str(),asset->mesh.materials.size(),asset->materialParts.size());asset->materialParts.clear();}}
    // Budgets: in-process memory (meshes, rigs, MANAGED fallback textures) of the 2 GB 32-bit process, and
    // video memory for DEFAULT-pool textures. Assets of the current frame are never evicted.
    for(;;){size_t total=0,textureTotal=0;auto victim=modelAssets.end();for(auto it=modelAssets.begin();it!=modelAssets.end();++it){total+=it->second->bytes;textureTotal+=it->second->textureBytes;if(it->first!=id&&it->second.use_count()==1&&it->second->lastUsed!=re3bridge::BuildSerial()&&(victim==modelAssets.end()||it->second->lastUsed<victim->second->lastUsed))victim=it;}
        if((total<=128u*1024*1024&&textureTotal<=768u*1024*1024)||victim==modelAssets.end())break;log("MODEL_CACHE_EVICT %s bytes=%zu texture_bytes=%zu",victim->first.c_str(),victim->second->bytes,victim->second->textureBytes);modelAssets.erase(victim);
    }
    log("MODEL_READY %s vertices=%zu triangles=%zu bones=%zu materials=%zu",id.c_str(),asset->mesh.vertices.size(),asset->mesh.indices.size()/3,asset->mesh.inverseBind.size(),asset->mesh.materials.size());return asset;
}
static void driveFace(const std::shared_ptr<ModelAsset>& asset,ActorRuntime& actor,int speaker,const std::vector<re3model::Matrix3x4>& bones){
    if(!modelFaceEnabled||!asset->face.Valid()||bones.size()<2||asset->mesh.inverseBind.size()<2)return;
    auto& record=actor.face;auto& st=record.face;
    const double now=faceClock();
    if(!st.lastTime){st.random^=uint32_t(reinterpret_cast<uintptr_t>(asset.get()))*2654435761u+uint32_t((speaker&15)*977);st.nextBlink=now+1.0+st.Random()*3.0;st.nextSaccade=now+0.5;}
    const float dt=float(st.lastTime?(std::min)(0.1,(std::max)(0.0,now-st.lastTime)):1.0/30);st.lastTime=now;
    // head transform of this actor and its eye midpoint in world space
    const auto head=re3model::Multiply(bones[1],asset->mesh.inverseBind[1]);
    record.eyeWorld=re3model::TransformPoint(head,asset->face.EyeCentre());record.eyeTime=now;
    // lip sync from the voice clips of this actor's speaker id (-1: none)
    float lip[3]={0,0,0};const char* clipName=nullptr;
    for(int c=0;c<0x15&&speaker>=0;++c){auto& v=voiceChannels[c];
        if(!v.started||!v.clip||int(v.clip->speaker)!=speaker)continue;
        if(now-v.startTime>0.25&&!voiceStillPlaying(c)){v.started=false;continue;}
        if(faceLipSync.Sample(*v.clip,now-v.startTime,lip)){clipName=v.name;break;}
    }
    if(clipName&&!record.lipActive)log("FACE_LIPSYNC %s clip=%s",asset->id.c_str(),clipName);
    record.lipActive=clipName!=nullptr;
    st.values[re3face::Jaw]=re3face::Approach(st.values[re3face::Jaw],lip[0],dt,0.025f);
    st.values[re3face::Wide]=re3face::Approach(st.values[re3face::Wide],lip[1],dt,0.04f);
    st.values[re3face::Round]=re3face::Approach(st.values[re3face::Round],lip[2],dt,0.04f);
    if(st.values[re3face::Jaw]<1.e-3f&&lip[0]==0)st.values[re3face::Jaw]=0;
    if(st.values[re3face::Wide]<1.e-3f&&lip[1]==0)st.values[re3face::Wide]=0;
    if(st.values[re3face::Round]<1.e-3f&&lip[2]==0)st.values[re3face::Round]=0;
    // blinks (occasional double blink)
    if(now>=st.nextBlink){st.blinkStart=now;st.nextBlink=now+(st.Random()<0.12f?0.35:2.0+st.Random()*4.5);}
    st.values[re3face::Blink]=re3face::BlinkCurve(now-st.blinkStart);
    // gaze: toward another HD face in front of this one (the one speaking first, else the nearest), else
    // small idle saccades
    // idle saccades: up to ~10 deg sideways / ~3.5 deg up-down, and now and then a short ~20 deg glance
    if(now>=st.nextSaccade){
        const bool glance=st.Random()<0.2f;
        st.saccadeYaw=(st.Random()-0.5f)*(glance?1.4f:0.72f);st.saccadePitch=(st.Random()-0.5f)*(glance?0.5f:0.4f);
        st.nextSaccade=now+(glance?0.35+st.Random()*0.5:0.5+st.Random()*2.3);
    }
    float targetYaw=st.saccadeYaw,targetPitch=st.saccadePitch;
    const float* m=head.m;
    const float a00=m[0],a01=m[1],a02=m[2],a10=m[4],a11=m[5],a12=m[6],a20=m[8],a21=m[9],a22=m[10];
    const float c00=a11*a22-a12*a21,c01=a02*a21-a01*a22,c02=a01*a12-a02*a11,c10=a12*a20-a10*a22,c11=a00*a22-a02*a20,c12=a02*a10-a00*a12,c20=a10*a21-a11*a20,c21=a01*a20-a00*a21,c22=a00*a11-a01*a10;
    const float det=a00*c00+a01*c10+a02*c20;
    bool lookSpeaking=false;float lookDistance=0;
    for(const auto& entry:actorRuntime){
        const auto& other=entry.second.face;
        if(&other==&record||!(other.eyeTime>0&&now-other.eyeTime<0.5)||std::abs(det)<=1.e-6f)continue;
        const re3model::Vec3 d=other.eyeWorld-record.eyeWorld;
        const re3model::Vec3 b{(c00*d.x+c01*d.y+c02*d.z)/det,(c10*d.x+c11*d.y+c12*d.z)/det,(c20*d.x+c21*d.y+c22*d.z)/det};
        const float horizontal=std::sqrt(b.x*b.x+b.z*b.z);
        const float yaw=std::atan2(-b.z,b.x),pitch=std::atan2(b.y,horizontal);
        const float maxYaw=0.4887f,maxPitch=0.3142f; // authored limits: 28 and 18 degrees
        if(!(b.x>0&&std::abs(yaw)<maxYaw*1.6f))continue;
        const float distance=re3model::Dot(d,d);
        if(lookDistance>0&&((lookSpeaking&&!other.lipActive)||(lookSpeaking==other.lipActive&&distance>=lookDistance)))continue;
        lookSpeaking=other.lipActive;lookDistance=(std::max)(distance,1.e-6f);
        targetYaw=(std::max)(-1.f,(std::min)(1.f,yaw/maxYaw))+st.saccadeYaw*0.2f;targetPitch=(std::max)(-1.f,(std::min)(1.f,pitch/maxPitch))+st.saccadePitch*0.2f;
    }
    st.values[re3face::GazeYaw]=re3face::Approach(st.values[re3face::GazeYaw],targetYaw,dt,0.045f);
    st.values[re3face::GazePitch]=re3face::Approach(st.values[re3face::GazePitch],targetPitch,dt,0.045f);
    if(!asset->face.Apply(asset->mesh,st.values)&&modelObserved.insert("face-apply-failed:"+asset->id).second)log("MODEL_FACE_APPLY_FAILED %s; static face restored",asset->id.c_str());
}
static re3model::Projection actorProjection(const re3bridge::ActorContext& context){
    re3model::Projection projection;
    projection.camera=re3model::FromNativeMatrix(context.camera);
    re3model::SetNativeProjectionRegisters(projection,context.gte.data(),context.gte.size());
    D3DVIEWPORT9 viewport{};gameDevice->GetViewport(&viewport);
    projection.scaleX=viewport.Width/320.f;projection.scaleY=viewport.Height/240.f;
    projection.pixelOffsetX=projection.pixelOffsetY=-.5f;
    projection.mode=modelParity?re3model::ProjectionMode::NativeInteger:re3model::ProjectionMode::SmoothSubpixel;
    projection.nearPlane=16;projection.perspectiveCorrect=!modelParity;
    projection.cull=re3model::FaceCull::None;projection.sortWithinBucketByDepth=true;
    return projection;
}
// Fix 2026-10-25d (perf): with many HD enemies the per-actor prepare (projection, lighting, triangle order) ran one actor
// after another on the game thread: 20+ ms per frame in the first alley (Jill + 8 zombies), the game slowed below 30 fps.
// A small actor (< 30000 vertices, pure lighting, no depth callback, wholly beyond the near plane, all buckets >= 8) now
// gets its OT jobs at build time for every bucket of its vertex depth range (each emitted triangle's three depths lie
// inside it, DepthBucket is monotonic, so every slice finds its job; empty buckets draw nothing), and its PrepareFrame
// runs at the first draw of any deferred job, together with all other queued actors: one actor per worker on the pool.
// The geometry is byte-identical to the prepare at build time; everything else (near actors, Jill, Carlos, doors,
// props) is prepared at build time as before. pilot.ini defer_prepare=0 turns it off.
static std::vector<std::vector<re3model::SkinnedVertex>> skinSpares;   // game thread only
static std::vector<re3model::SkinnedVertex> takeSkinSpare(){if(skinSpares.empty())return {};auto array=std::move(skinSpares.back());skinSpares.pop_back();return array;}
static void giveSkinSpare(std::vector<re3model::SkinnedVertex>&& array){if(array.capacity()&&skinSpares.size()<24){array.clear();skinSpares.push_back(std::move(array));}}
struct PendingFrame {
    std::shared_ptr<ModelAsset> asset;std::vector<re3model::SkinnedVertex> skinned;re3model::Projection projection;
    std::vector<uint8_t> hiddenMaterials,hiddenTriangles;std::vector<uint32_t> vertexTint;
    re3model::NativeLighting native;re3model::SculptedLighting sculpted;std::vector<std::uint8_t> occlusion;
    std::vector<re3model::MaterialTextures> textures;re3model::FrameGeometry geometry;std::string error;
    int state=0;bool queued=false;   // 0 waiting, 1 prepared, 2 prepare failed, 3 uploaded, 4 upload failed
    std::shared_ptr<re3model::GpuFrame> gpu;uint16_t bucketLow=0,bucketHigh=0;std::vector<int16_t> sliceOf;
    double skinMs=0,prepareMs=0;
    uint64_t epoch=0;   // fix 2026-10-25e: the renderer epoch of the build that queued it (uploadAt)
    std::shared_ptr<re3fps60::InterpData> interp;   // fps60: attached to the uploaded frame
};
static std::vector<std::shared_ptr<PendingFrame>> pendingFrames;
static bool modelDeferPrepare=true;static unsigned modelFrameDeferred=0;
static bool deferModelJobs(const std::shared_ptr<ModelAsset>& asset,std::vector<re3model::SkinnedVertex>& skinned,const re3model::Projection& p,re3bridge::ActorPlan& plan,
    const std::vector<re3model::MaterialTextures>* actorTextures,std::vector<re3model::SkinnedVertex>* skinnedOut,double skinMs,const std::shared_ptr<re3fps60::InterpData>& interp=nullptr){
    if(!modelDeferPrepare||modelParity||asset->mesh.vertices.size()>=30000||skinned.size()!=asset->mesh.vertices.size()||p.depth||p.triangleDepth)return false;
    const bool sculpt=p.shadeVertex==&re3model::ShadeSculpted&&p.shadeContext;
    if((p.shadeVertex&&!sculpt)||(!p.shadeVertex&&p.shade&&p.shade!=&re3model::ShadeNativeNccs))return false;
    float zmin=(std::numeric_limits<float>::max)(),zmax=-(std::numeric_limits<float>::max)();
    for(const auto& v:skinned){const auto c=re3model::TransformPoint(p.camera,v.position);if(!re3model::Finite(c))return false;zmin=(std::min)(zmin,c.z);zmax=(std::max)(zmax,c.z);}
    if(!(zmin>=p.nearPlane))return false;   // reaches the near plane: clipped triangles and the native near cut stay on the build path
    auto depthOf=[](float z){return re3model::StoredDepth((std::max)(0.f,(std::min)(65535.f,z)));};
    const auto dl=depthOf(zmin),dh=depthOf(zmax);
    const uint16_t low=re3model::DepthBucket(dl,dl,dl,p.zsf3),high=re3model::DepthBucket(dh,dh,dh,p.zsf3);
    if(low<8||high>=p.bucketLimit||high>re3bridge::kMaxBucket||high-low>=512)return false;
    auto pf=std::make_shared<PendingFrame>();pf->asset=asset;pf->projection=p;
    if(skinnedOut)pf->skinned=skinned;else{pf->skinned=takeSkinSpare();pf->skinned.swap(skinned);}
    if(p.hiddenMaterials){pf->hiddenMaterials=*p.hiddenMaterials;pf->projection.hiddenMaterials=&pf->hiddenMaterials;}
    if(p.hiddenTriangles){pf->hiddenTriangles=*p.hiddenTriangles;pf->projection.hiddenTriangles=&pf->hiddenTriangles;}
    if(p.vertexTint){pf->vertexTint=*p.vertexTint;pf->projection.vertexTint=&pf->vertexTint;}
    if(sculpt){pf->sculpted=*static_cast<const re3model::SculptedLighting*>(p.shadeContext);
        if(pf->sculpted.occlusion){pf->occlusion=*pf->sculpted.occlusion;pf->sculpted.occlusion=&pf->occlusion;}pf->projection.shadeContext=&pf->sculpted;}
    else if(p.shade==&re3model::ShadeNativeNccs&&p.shadeContext){pf->native=*static_cast<const re3model::NativeLighting*>(p.shadeContext);pf->projection.shadeContext=&pf->native;}
    pf->textures=actorTextures?*actorTextures:asset->textures;pf->bucketLow=low;pf->bucketHigh=high;pf->skinMs=skinMs;pf->epoch=modelRenderer.epoch();pf->interp=interp;
    for(int b=high;b>=int(low);--b){auto job=std::make_shared<ModelJob>();job->pending=pf;job->bucket=uint16_t(b);job->name=asset->id;plan.jobs.push_back({uint16_t(b),reinterpret_cast<uintptr_t>(job.get()),job});}
    pf->queued=true;pendingFrames.push_back(pf);++modelFrameDeferred;
    if(modelObserved.insert("deferred:"+asset->id).second)log("MODEL_DEFERRED %s buckets=%u-%u (prepared at the first draw with the other queued actors)",asset->id.c_str(),unsigned(low),unsigned(high));
    if(skinnedOut)*skinnedOut=std::move(skinned);
    return true;
}
// Prepares every queued frame (and `needed`, if it was dropped from the queue): one actor per pool worker, each actor on
// one thread; one or two actors keep the internally parallel prepare.
static void runPendingFrames(const std::shared_ptr<PendingFrame>& needed){
    std::vector<std::shared_ptr<PendingFrame>> work;work.swap(pendingFrames);
    if(needed&&needed->state==0&&!needed->queued)work.push_back(needed);
    work.erase(std::remove_if(work.begin(),work.end(),[](const std::shared_ptr<PendingFrame>& pf){pf->queued=false;return pf->state!=0;}),work.end());
    if(work.empty())return;
    auto prepare=[](PendingFrame& pf){std::string error;
        try{if(re3model::PrepareFrame(pf.asset->mesh,pf.skinned,pf.projection,pf.geometry,&error)&&!pf.geometry.vertices.empty())pf.state=1;else{pf.error=error.empty()?"no triangles":error;pf.state=2;}}
        catch(...){pf.error="exception";pf.state=2;}};
    const double started=modelClockMs();
    if(work.size()<3)for(auto& pf:work)prepare(*pf);
    else{
        const std::function<void(std::size_t)> task=[&](std::size_t i){re3model::parallel::InlineOnly()=true;prepare(*work[i]);re3model::parallel::InlineOnly()=false;};
        if(!re3model::parallel::Pool::Get().Run(work.size(),task))for(auto& pf:work)if(pf->state==0){pf->state=2;pf->error="worker failure";}
    }
    const double each=(modelClockMs()-started)/double(work.size());
    for(auto& pf:work){pf->prepareMs=each;giveSkinSpare(std::move(pf->skinned));
        if(pf->state==2&&modelObserved.insert("failed:"+pf->asset->id).second)log("MODEL_PREPARE_FAILED %s %s (deferred)",pf->asset->id.c_str(),pf->error.c_str());}
}
// A deferred job: its frame prepared (with every queued one) and uploaded at the first draw; false = nothing to draw
// in this bucket.
static bool resolvePendingJob(ModelJob& job){
    auto& pf=*job.pending;
    if(pf.state==0)runPendingFrames(job.pending);
    if(pf.state==1){
        const double started=modelClockMs();HRESULT hr;
        // the epoch of its build, not the current one: one depth surface for the whole OT (fix 2026-10-25e)
        auto sources=std::move(pf.geometry.sources);
        pf.gpu=modelRenderer.uploadAt(pf.epoch,pf.asset->mesh,std::move(pf.geometry),pf.textures,false,&hr);pf.geometry=re3model::FrameGeometry{};
        if(pf.gpu&&pf.interp&&sources.size()==pf.gpu->vertexCount){pf.interp->sources=std::move(sources);pf.gpu->interp=pf.interp;}
        if(!pf.gpu){pf.state=4;if(!re3model::IsTransientDrawStatus(hr))log("MODEL_UPLOAD_FAILED %s hr=%08lx (deferred)",pf.asset->id.c_str(),hr);return false;}
        pf.state=3;pf.sliceOf.assign(size_t(pf.bucketHigh-pf.bucketLow)+1,-1);
        for(size_t i=0;i<pf.gpu->slices.size();++i){const auto b=pf.gpu->slices[i].bucket;
            if(b>=pf.bucketLow&&b<=pf.bucketHigh)pf.sliceOf[b-pf.bucketLow]=int16_t(i);
            else if(modelObserved.insert("defer-miss:"+pf.asset->id).second)log("MODEL_DEFER_BUCKET_MISS %s bucket=%u range=%u-%u",pf.asset->id.c_str(),unsigned(b),unsigned(pf.bucketLow),unsigned(pf.bucketHigh));}
        auto& perf=modelPerf[pf.asset->id];perf.count++;perf.skin+=pf.skinMs;perf.prepare+=pf.prepareMs;perf.upload+=modelClockMs()-started;perf.maximum=(std::max)(perf.maximum,pf.skinMs+pf.prepareMs+modelClockMs()-started);
        if(perf.count==300){log("MODEL_PERF %s samples=%u skin_avg_ms=%.3f prepare_avg_ms=%.3f upload_avg_ms=%.3f total_max_ms=%.3f deferred",pf.asset->id.c_str(),perf.count,perf.skin/perf.count,perf.prepare/perf.count,perf.upload/perf.count,perf.maximum);perf=ModelPerf{};}
        if(modelObserved.insert("prepared:"+pf.asset->id).second)log("MODEL_PREPARED %s triangles=%zu slices=%zu deferred buckets=%u-%u",pf.asset->id.c_str(),size_t(pf.gpu->vertexCount/3),pf.gpu->slices.size(),unsigned(pf.bucketLow),unsigned(pf.bucketHigh));
    }
    if(pf.state!=3)return false;
    const int slice=pf.sliceOf[job.bucket-pf.bucketLow];if(slice<0)return false;
    job.geometry=pf.gpu;job.slice=size_t(slice);return true;
}
// skinnedOut: the skinned vertices of this asset for an overlay worn over it; preskinned: vertices already placed
// (overlays), skinning is skipped.
static bool appendModelJobs(const std::shared_ptr<ModelAsset>& asset,const std::vector<re3model::Matrix3x4>& bones,const re3model::Projection& projection,re3bridge::ActorPlan& plan,const std::vector<re3model::MaterialTextures>* actorTextures=nullptr,
    std::vector<re3model::SkinnedVertex>* skinnedOut=nullptr,const std::vector<re3model::SkinnedVertex>* preskinned=nullptr){
    std::string error;std::vector<re3model::SkinnedVertex> skinned;re3model::FrameGeometry geometry;
    // Fix 2026-10-25d: skinned arrays of earlier calls are reused (no fresh 0.5-2 MB per actor per frame).
    skinned=takeSkinSpare();struct GiveBack {std::vector<re3model::SkinnedVertex>& array;~GiveBack(){giveSkinSpare(std::move(array));}} giveBack{skinned};
    const double started=modelClockMs();bool skinnedOkay=false;
    if(preskinned){skinned=*preskinned;skinnedOkay=skinned.size()==asset->mesh.vertices.size();if(!skinnedOkay)error="overlay vertex count";}
    else{
    modelLastSkinMode=asset->affineDualQuaternion?"affine-dual-quaternion":asset->dualQuaternion?"dual-quaternion":"linear";
    // The explicit v2 mode retains native affine stretch continuously. It never
    // switches the entire body to LBS because one leg matrix is non-rigid.
    // Invalid v2 transforms return to the native actor through the normal guard.
    modelLastShoulderFactors={};
    skinnedOkay=asset->affineDualQuaternion?re3affine::SkinAffineDualQuaternionCorrected(asset->mesh,bones,asset->shoulderCorrective,skinned,&error,nullptr,&modelLastShoulderFactors):asset->dualQuaternion?re3model::SkinDualQuaternion(asset->mesh,bones,skinned,&error):re3model::Skin(asset->mesh,bones,skinned,&error);
    }
    if(!skinnedOkay&&asset->dualQuaternion&&GetPrivateProfileIntA("pilot","debug_warp",0,(root+"\\pilot.ini").c_str())&&modelObserved.insert("failed-pose:"+asset->id).second){
        std::ofstream out(root+"/dq-failed-pose.json");out<<std::setprecision(9)<<"{\"asset\":\""<<asset->id<<"\",\"serial\":"<<re3bridge::BuildSerial()<<",\"scene\":\""<<currentScene<<"\",\"world_bones\":[";
        for(size_t i=0;i<bones.size();++i){if(i)out<<",";out<<"[";for(unsigned k=0;k<12;++k){if(k)out<<",";out<<bones[i].m[k];}out<<"]";}out<<"],\"inverse_bind\":[";
        for(size_t i=0;i<asset->mesh.inverseBind.size();++i){if(i)out<<",";out<<"[";for(unsigned k=0;k<12;++k){if(k)out<<",";out<<asset->mesh.inverseBind[i].m[k];}out<<"]";}out<<"]}";log("MODEL_DQ_FAILED_POSE saved original affine transforms for audit");
    }
    if(!skinnedOkay&&asset->dualQuaternion){
        std::string affineError;
        if(re3model::CanUseAffineSkinFallback(asset->mesh,bones,&affineError)){
            const auto dqReason=error;error.clear();skinnedOkay=re3model::Skin(asset->mesh,bones,skinned,&error);
            if(skinnedOkay){modelLastSkinMode="affine-linear";++modelAffineFallbacks;if(modelObserved.insert("affine-lbs:"+asset->id+":"+currentScene).second)log("MODEL_AFFINE_LBS %s scene=%s serial=%u reason=%s; original finite nonsingular affine transforms retained with HD geometry",asset->id.c_str(),currentScene.c_str(),re3bridge::BuildSerial(),dqReason.c_str());}
        }
    }
    if(skinnedOkay&&!preskinned&&asset->poseWeights.valid){
        float factors[2]{};std::string poseError;
        if(!re3poseweights::Apply(asset->poseWeights,asset->mesh,bones,skinned,factors,&poseError)){skinnedOkay=false;error=poseError;}
        else if((factors[0]>0.5f||factors[1]>0.5f)&&modelObserved.insert("pose-weights-on:"+asset->id+":"+currentScene).second)
            log("MODEL_POSE_WEIGHTS_ACTIVE %s scene=%s right=%.2f left=%.2f",asset->id.c_str(),currentScene.c_str(),factors[0],factors[1]);
    }
    const double skinnedAt=modelClockMs();
    // v20: this pack's lighting.txt/occlusion.r3ao over the captured native light (world-space normals, Y-down)
    re3model::SculptedLighting sculpted;re3model::Projection sculptedProjection;const re3model::Projection* used=&projection;
    const auto lit=re3model::AssetLightingStore().find(asset->id);
    if(lit!=re3model::AssetLightingStore().end()&&lit->second.sculpted&&projection.shade==&re3model::ShadeNativeNccs&&projection.shadeContext){
        sculpted=re3model::MakeSculptedLighting(*static_cast<const re3model::NativeLighting*>(projection.shadeContext),lit->second,{0,-1,0});
        sculptedProjection=projection;sculptedProjection.shade=nullptr;sculptedProjection.shadeVertex=&re3model::ShadeSculpted;sculptedProjection.shadeContext=&sculpted;used=&sculptedProjection;
    }
    std::shared_ptr<re3fps60::InterpData> interp;re3model::Projection recorded;
    if(skinnedOkay&&(interp=fps60Record(asset.get(),skinned,*used))){recorded=*used;recorded.recordSources=true;used=&recorded;}
    if(skinnedOkay&&deferModelJobs(asset,skinned,*used,plan,actorTextures,skinnedOut,skinnedAt-started,interp))return true;
    if(!skinnedOkay||!re3model::PrepareFrame(asset->mesh,skinned,*used,geometry,&error)||geometry.vertices.empty()){
        if(modelObserved.insert("failed:"+asset->id).second)log("MODEL_PREPARE_FAILED %s %s vertices=%zu",asset->id.c_str(),error.c_str(),geometry.vertices.size());return false;
    }
    const double preparedAt=modelClockMs();
    if(*(uint16_t*)(gameBase+0x661fa8)==1&&*(uint16_t*)(gameBase+0x661faa)==0x1a&&GetPrivateProfileIntA("pilot","debug_warp",0,(root+"\\pilot.ini").c_str())&&modelObserved.insert("close-depth:"+asset->id).second){
        unsigned low[8]{},visible=0;for(const auto& slice:geometry.slices){unsigned triangles=0;for(unsigned n=0;n<slice.runCount;n++)triangles+=geometry.runs[slice.firstRun+n].vertexCount/3;if(slice.bucket<8)low[slice.bucket]+=triangles;else visible+=triangles;}
        log("MODEL_CLOSE_DEPTH %s cut=%u bins0to7=%u,%u,%u,%u,%u,%u,%u,%u visibleTriangles=%u",asset->id.c_str(),!(*(uint32_t*)(gameBase+0x65cbd4)&0x80000000u),low[0],low[1],low[2],low[3],low[4],low[5],low[6],low[7],visible);
    }
    if((asset->characterBody||asset->id=="jill-standard"||asset->id=="dario"||asset->id=="parity-jill"||asset->id=="parity-dario")&&!(*(uint32_t*)(gameBase+0x65cbd4)&0x80000000u)){
        const bool bodyInNativeRange=std::any_of(geometry.slices.begin(),geometry.slices.end(),[](const re3model::DepthSlice& slice){return slice.bucket>=8;});
        if(!bodyInNativeRange){if(modelObserved.insert("near-native-fallback:"+asset->id).second)log("MODEL_BODY_NATIVE_NEAR_FALLBACK %s all body slices lie below native OT cut8",asset->id.c_str());return false;}
    }
    auto sources=std::move(geometry.sources);
    HRESULT hr;auto gpu=modelRenderer.upload(asset->mesh,std::move(geometry),actorTextures?*actorTextures:asset->textures,false,&hr);
    if(!gpu){log("MODEL_UPLOAD_FAILED %s hr=%08lx",asset->id.c_str(),hr);return false;}
    if(interp&&sources.size()==gpu->vertexCount){interp->sources=std::move(sources);gpu->interp=interp;}
    for(size_t index=0;index<gpu->slices.size();++index){auto job=std::make_shared<ModelJob>();job->geometry=gpu;job->slice=index;job->name=asset->id;plan.jobs.push_back({gpu->slices[index].bucket,reinterpret_cast<uintptr_t>(job.get()),job});}
    const double uploadedAt=modelClockMs();auto& perf=modelPerf[asset->id];perf.count++;perf.skin+=skinnedAt-started;perf.prepare+=preparedAt-skinnedAt;perf.upload+=uploadedAt-preparedAt;perf.maximum=(std::max)(perf.maximum,uploadedAt-started);
    if(perf.count==300){log("MODEL_PERF %s samples=%u skin_avg_ms=%.3f prepare_avg_ms=%.3f upload_avg_ms=%.3f total_max_ms=%.3f",asset->id.c_str(),perf.count,perf.skin/perf.count,perf.prepare/perf.count,perf.upload/perf.count,perf.maximum);perf=ModelPerf{};}
    if(modelObserved.insert("prepared:"+asset->id).second)log("MODEL_PREPARED %s triangles=%zu slices=%zu H=%.1f offset=%.1f,%.1f scale=%.3f,%.3f zsf3=%d",asset->id.c_str(),size_t(gpu->vertexCount/3),gpu->slices.size(),projection.h,projection.offsetX,projection.offsetY,projection.scaleX,projection.scaleY,projection.zsf3);
    if(skinnedOut)*skinnedOut=std::move(skinned);
    return true;
}
// An overlay (actors.txt overlay=, Jill's harness) placed on the skinned body: each vertex follows its body triangle
// with the body's own skinning, shoulder corrective included, so it never slides off or lags behind.
static void appendOverlay(const std::string& bodyName,const std::string& id,const std::vector<re3model::SkinnedVertex>& body,const re3model::Projection& bodyProjection,re3bridge::ActorPlan& plan){
    auto overlay=getModel(id);
    if(!overlay||overlay->wrap.empty()||overlay->wrapBodyVertices!=body.size()){
        if(modelObserved.insert("overlay-unavailable:"+id).second)log("MODEL_OVERLAY_UNAVAILABLE %s on %s wrap=%zu wrapBody=%u body=%zu",id.c_str(),bodyName.c_str(),overlay?overlay->wrap.size():0,overlay?overlay->wrapBodyVertices:0,body.size());
        return;
    }
    std::vector<re3model::SkinnedVertex> placed(overlay->wrap.size());
    for(size_t i=0;i<placed.size();++i){
        const auto& w=overlay->wrap[i];const auto& a=body[w.v[0]];const auto& b=body[w.v[1]];const auto& c=body[w.v[2]];
        const float b0=1-w.b1-w.b2;
        const re3model::Vec3 base=a.position*b0+b.position*w.b1+c.position*w.b2;
        const re3model::Vec3 n=re3model::Normalize(a.normal*b0+b.normal*w.b1+c.normal*w.b2);
        const re3model::Vec3 e1=b.position-a.position;
        const re3model::Vec3 t=re3model::Normalize(e1-n*re3model::Dot(e1,n));
        const re3model::Vec3 bt=re3model::Cross(n,t);
        placed[i].position=base+n*w.offset[0]+t*w.offset[1]+bt*w.offset[2];
        placed[i].normal=re3model::Normalize(n*w.normal[0]+t*w.normal[1]+bt*w.normal[2]);
        placed[i].uv=overlay->mesh.vertices[i].uv;
    }
    auto projection=bodyProjection;projection.hiddenMaterials=nullptr;projection.sortWithinBucketByMaterial=false;
    if(appendModelJobs(overlay,{},projection,plan,nullptr,nullptr,&placed)&&modelObserved.insert("overlay:"+bodyName+":"+id).second)
        log("MODEL_OVERLAY %s on %s vertices=%zu",id.c_str(),bodyName.c_str(),placed.size());
}
// v12 wounds (model_wounds.h): every shot the player hit this actor with since its last preparation becomes a wound
// on the body as it is skinned now.
static void takeShots(const re3bridge::ActorContext& context,ActorRuntime& actor,const ModelAsset& body,const std::vector<re3model::SkinnedVertex>& skinned,
    const std::vector<uint8_t>* hidden,const std::string& name){
    static unsigned logged=0;
    auto& hits=re3wound::pendingHits;
    for(auto it=hits.begin();it!=hits.end();){
        if(it->actor!=context.actor){++it;continue;}
        re3wound::Wound wound;re3model::Vec3 where;bool onLine=false;
        const uint32_t seed=uint32_t(context.serial*2654435761u)^uint32_t(reinterpret_cast<uintptr_t>(context.actor))^uint32_t(actor.wounds.size()*40503u);
        if(re3wound::woundFromShot(body.mesh,skinned,hidden,it->hand,it->weapon,seed,wound,where,onLine)){
            if(actor.wounds.size()>=re3wound::kMaxWounds)actor.wounds.erase(actor.wounds.begin());
            if(logged<60){++logged;log("MODEL_WOUND %s model=%02x actor=%p weapon=%02x at=%.0f,%.0f,%.0f line=%u triangles=%zu texture=%u wounds=%zu delay=%u",name.c_str(),context.modelId,context.actor,it->weapon,where.x,where.y,where.z,
                unsigned(onLine),wound.material.size(),unsigned(wound.texture),actor.wounds.size()+1,context.serial-it->serial);}
            actor.wounds.push_back(std::move(wound));
        }else if(logged<60){++logged;log("MODEL_WOUND_MISS %s model=%02x weapon=%02x (no body surface near the barrel line)",name.c_str(),context.modelId,it->weapon);}
        it=hits.erase(it);
    }
}
// The wounds as extra triangles over the skinned body (wound-decals: one alpha-cutout material per texture). Each
// vertex sits 1.5 units out along the normal and is then pulled 10 units toward the camera eye along its own view
// ray: the screen position stays exactly the same, the depth is always in front of the body (v12 used the normal
// offset alone, 2 units: at grazing angles that is less than the depth step and the body won the depth test).
static void appendWounds(const std::vector<re3wound::Wound>& wounds,const std::vector<re3model::SkinnedVertex>& body,const std::vector<uint8_t>* hidden,
    const re3model::Projection& bodyProjection,re3bridge::ActorPlan& plan,const std::string& bodyId,const std::vector<uint8_t>* vertexBone=nullptr,uint32_t goneBones=0){
    if(wounds.empty())return;
    re3model::Matrix3x4 view;if(!re3model::InverseAffine(bodyProjection.camera,view))return;
    const re3model::Vec3 eye{view.m[3],view.m[7],view.m[11]};
    auto decals=getModel("wound-decals");
    if(!decals||decals->mesh.materials.size()!=re3wound::kTextures){if(modelObserved.insert("wound-decals-unavailable").second)log("MODEL_WOUND_DECALS_UNAVAILABLE materials=%zu",decals?decals->mesh.materials.size():0);return;}
    re3model::Mesh mesh;std::vector<re3model::SkinnedVertex> placed;
    // v20: a body with lighting.txt is drawn by ShadeSculpted (appendModelJobs); its wounds are lit the same way, with the
    // occlusion of the body vertex each corner sits on, not by the plain native formula (lit patches on shaded skin)
    const auto found=re3model::AssetLightingStore().find(bodyId);std::vector<uint8_t> placedOcclusion;
    const re3model::AssetLighting* bodyLight=found!=re3model::AssetLightingStore().end()&&found->second.sculpted&&bodyProjection.shade==&re3model::ShadeNativeNccs&&bodyProjection.shadeContext?&found->second:nullptr;
    for(unsigned texture=0;texture<re3wound::kTextures;++texture){
        auto material=decals->mesh.materials[texture];material.firstIndex=uint32_t(mesh.indices.size());
        for(const auto& wound:wounds){
            if(wound.texture!=texture||std::any_of(wound.corners.begin(),wound.corners.end(),[&](uint32_t v){return v>=body.size();}))continue;
            for(size_t t=0;t<wound.material.size();++t){
                if(hidden&&wound.material[t]<hidden->size()&&(*hidden)[wound.material[t]])continue;
                if(vertexBone&&goneBones){const uint32_t v=wound.corners[wound.triangles[t*3]];if(v<vertexBone->size()&&((goneBones>>(*vertexBone)[v])&1u))continue;}
                for(unsigned k=0;k<3;++k){const uint32_t corner=wound.triangles[t*3+k];const auto& source=body[wound.corners[corner]];const auto normal=re3model::Normalize(source.normal);
                    const auto out=source.position+normal*1.5f,toEye=eye-out;const float distance=std::sqrt(re3model::Dot(toEye,toEye));
                    const auto position=distance>40.f?out+toEye*(10.f/distance):out;
                    placed.push_back({position,normal,{wound.uv[corner*2],wound.uv[corner*2+1]}});mesh.indices.push_back(uint32_t(placed.size()-1));
                    if(bodyLight)placedOcclusion.push_back(wound.corners[corner]<bodyLight->occlusion.size()?bodyLight->occlusion[wound.corners[corner]]:uint8_t(255));}
            }
        }
        material.indexCount=uint32_t(mesh.indices.size())-material.firstIndex;mesh.materials.push_back(material);
    }
    if(placed.empty())return;
    mesh.vertices.resize(placed.size());
    auto projection=bodyProjection;projection.hiddenMaterials=nullptr;projection.sortWithinBucketByMaterial=false;projection.vertexTint=nullptr;
    re3model::SculptedLighting sculpted;
    if(bodyLight){sculpted=re3model::MakeSculptedLighting(*static_cast<const re3model::NativeLighting*>(bodyProjection.shadeContext),*bodyLight,{0,-1,0});
        sculpted.occlusion=bodyLight->occlusion.empty()?nullptr:&placedOcclusion;projection.shade=nullptr;projection.shadeVertex=&re3model::ShadeSculpted;projection.shadeContext=&sculpted;}
    re3model::FrameGeometry geometry;std::string error;
    auto interp=fps60Record(decals.get(),placed,projection);projection.recordSources=interp!=nullptr;
    if(!re3model::PrepareFrame(mesh,placed,projection,geometry,&error)||geometry.vertices.empty())return;
    auto sources=std::move(geometry.sources);
    HRESULT hr;auto gpu=modelRenderer.upload(mesh,geometry,decals->textures,false,&hr);if(!gpu)return;
    if(interp&&sources.size()==gpu->vertexCount){interp->sources=std::move(sources);gpu->interp=interp;}
    for(size_t index=0;index<gpu->slices.size();++index){auto job=std::make_shared<ModelJob>();job->geometry=gpu;job->slice=index;job->name="wound-decals";plan.jobs.push_back({gpu->slices[index].bucket,reinterpret_cast<uintptr_t>(job.get()),job});}
}
// Bones of a follow.txt extra mesh: the part's world matrix with its own scale (4358F0 scales the part matrix when
// flags&4 and render attributes lack 0x10), times station frames from the part's current vertices: origin = centroid,
// rotation = shortest arc from the rest chain direction. Rest stations are the translations of the inverse binds.
// Unreadable vertices keep the rest stations (rigid worm on the scaled part matrix).
static std::vector<re3model::Matrix3x4> followNativePart(ModelAsset& asset,const re3bridge::PartSnapshot& part,unsigned index){
    re3model::Matrix3x4 world=re3model::FromNativeMatrix(part.world);
    if((part.flags&4)&&!(part.renderAttributes&0x10)&&part.renderAttributes)
        for(unsigned r=0;r<3;++r)for(unsigned c=0;c<3;++c)world.m[r*4+c]*=part.scale[c]*(1.f/4096.f);
    const size_t n=asset.followStations.size();std::vector<re3model::Vec3> rest(n),now(n);
    for(size_t k=0;k<n;++k){const float* ib=asset.mesh.inverseBind[k].m;rest[k]={-ib[3],-ib[7],-ib[11]};now[k]=rest[k];}
    const void* verts=part.runtimeMesh&&re3bridge::detail::Accessible(part.runtimeMesh,24)?re3bridge::detail::Read<const void*>(part.runtimeMesh,0):nullptr;
    const bool native=verts&&re3bridge::detail::Read<uint16_t>(part.runtimeMesh,8)==asset.followVertices&&re3bridge::detail::Accessible(verts,asset.followVertices*8);
    float moved=0;
    if(native)for(size_t k=0;k<n;++k){
        re3model::Vec3 c{};for(auto i:asset.followStations[k]){const auto* p=static_cast<const int16_t*>(verts)+size_t(i)*4;c=c+re3model::Vec3{float(p[0]),float(p[1]),float(p[2])};}
        now[k]=c*(1.f/float(asset.followStations[k].size()));const auto d=now[k]-rest[k];moved=(std::max)(moved,std::sqrt(re3model::Dot(d,d)));
    }
    char note[96];sprintf_s(note,"follow:%s:%u",asset.id.c_str(),index);
    if(modelObserved.insert(note).second)log("MODEL_FOLLOW %s part=%u native=%u moved=%.0f scale=%d,%d,%d",asset.id.c_str(),index,unsigned(native),moved,part.scale[0],part.scale[1],part.scale[2]);
    if(moved>1&&!asset.followMoved){asset.followMoved=true;log("MODEL_FOLLOW_MOVED %s part=%u moved=%.0f (native morph followed)",asset.id.c_str(),index,moved);}
    std::vector<re3model::Matrix3x4> bones(n);
    for(size_t k=0;k<n;++k){
        const size_t a=k?k-1:0,b=k+1<n?k+1:n-1;
        const re3model::Vec3 d0=re3model::Normalize(rest[b]-rest[a]),d1=re3model::Normalize(now[b]-now[a]),v=re3model::Cross(d0,d1);
        const float c=re3model::Dot(d0,d1);re3model::Matrix3x4 station;
        if(c>-0.999f){const float h=1.f/(1.f+c);
            station.m[0]=c+h*v.x*v.x;station.m[1]=h*v.x*v.y-v.z;station.m[2]=h*v.x*v.z+v.y;
            station.m[4]=h*v.x*v.y+v.z;station.m[5]=c+h*v.y*v.y;station.m[6]=h*v.y*v.z-v.x;
            station.m[8]=h*v.x*v.z-v.y;station.m[9]=h*v.y*v.z+v.x;station.m[10]=c+h*v.z*v.z;}
        station.m[3]=now[k].x;station.m[7]=now[k].y;station.m[11]=now[k].z;
        bones[k]=re3model::Multiply(world,station);
    }
    return bones;
}
// fix 2026-10-25: caps over the holes that hidden limb triangles leave: one fan per border loop, each vertex skinned with its
// border vertex's weights (the centre with the loop's summed weights), so one cap closes both the body and the falling piece.
// Packs with a Stump material (zombies) get stump.png projected on the loop plane, others (creatures) one texel of the
// surface around the hole. Built once per mask (nullptr: none).
static std::shared_ptr<ModelAsset> limbCaps(ModelAsset& body,uint32_t mask){
    const auto found=body.caps.find(mask);if(found!=body.caps.end())return found->second;
    auto& slot=body.caps[mask];const auto& mesh=body.mesh;size_t stump=SIZE_MAX;
    for(size_t m=0;m<body.materialRegion.size();++m)if(body.materialRegion[m]==ModelAsset::Stump){stump=m;break;}
    if(body.triangleBone.size()*3!=mesh.indices.size()||body.textures.size()!=mesh.materials.size())return slot;
    const bool stumpTexture=stump<body.textures.size()&&body.textures[stump].albedo;
    std::map<std::array<uint32_t,3>,uint32_t> keys;std::vector<uint32_t> key(mesh.vertices.size());
    for(size_t v=0;v<key.size();++v){std::array<uint32_t,3> k;std::memcpy(k.data(),&mesh.vertices[v].position,12);key[v]=keys.emplace(k,uint32_t(keys.size())).first->second;}
    struct Edge{unsigned visible=0,hidden=0;uint32_t a=0,b=0,material=0;re3model::Vec3 away{};};std::map<std::pair<uint32_t,uint32_t>,Edge> edges;
    for(size_t m=0;m<mesh.materials.size();++m){if(m==stump)continue;
        for(uint32_t t=mesh.materials[m].firstIndex/3;t<(mesh.materials[m].firstIndex+mesh.materials[m].indexCount)/3;++t){const bool hidden=((mask>>body.triangleBone[t])&1u)!=0;
            for(unsigned k=0;k<3;++k){const uint32_t a=mesh.indices[t*3+k],b=mesh.indices[t*3+(k+1)%3],c=mesh.indices[t*3+(k+2)%3];
                auto& e=edges[{(std::min)(key[a],key[b]),(std::max)(key[a],key[b])}];
                if(hidden){++e.hidden;e.away=e.away+mesh.vertices[c].position;}else{++e.visible;e.a=a;e.b=b;e.material=uint32_t(m);}}}}
    std::map<uint32_t,const Edge*> next;for(const auto& [k,e]:edges)if(e.visible==1&&e.hidden)next[key[e.a]]=&e;
    auto cap=std::make_shared<ModelAsset>();cap->id=body.id+"-caps";cap->mesh.inverseBind=mesh.inverseBind;unsigned loops=0;
    std::unordered_set<uint32_t> used;std::map<uint32_t,std::vector<uint32_t>> byMaterial;
    for(const auto& [start,first]:next){
        if(used.count(start))continue;std::vector<const Edge*> loop;uint32_t at=start;
        while(loop.size()<1024){const auto hit=next.find(at);if(hit==next.end()||used.count(at))break;used.insert(at);loop.push_back(hit->second);at=key[hit->second->b];if(at==start)break;}
        if(at!=start||loop.size()<3)continue;
        re3model::Vec3 c{},n{},away{};float awayCount=0;std::map<uint16_t,float> weights;
        for(size_t i=0;i<loop.size();++i){const auto& p=mesh.vertices[loop[i]->a].position,&q=mesh.vertices[loop[(i+1)%loop.size()]->a].position;c=c+p;n=n+re3model::Cross(p,q);
            away=away+loop[i]->away;awayCount+=float(loop[i]->hidden);for(unsigned k=0;k<4;++k)weights[mesh.vertices[loop[i]->a].bone[k]]+=mesh.vertices[loop[i]->a].weight[k];}
        c=c*(1.f/float(loop.size()));if(re3model::Dot(n,away*(1.f/awayCount)-c)<0)n=n*-1.f;n=re3model::Normalize(n);
        const auto e1=re3model::Normalize(re3model::Cross(n,std::abs(n.x)<0.9f?re3model::Vec3{1,0,0}:re3model::Vec3{0,1,0})),e2=re3model::Cross(n,e1);
        float radius=1;for(const auto* e:loop){const auto d=mesh.vertices[e->a].position-c;radius=(std::max)(radius,(std::max)(std::abs(re3model::Dot(d,e1)),std::abs(re3model::Dot(d,e2))));}
        const uint32_t material=stumpTexture?uint32_t(stump):loop[0]->material;const auto texel=mesh.vertices[loop[0]->a].uv;
        re3model::Vertex centre{};centre.position=c;centre.normal=n;centre.uv=stumpTexture?re3model::Vec2{0.5f,0.5f}:texel;
        std::vector<std::pair<float,uint16_t>> order;for(const auto& [b,w]:weights)order.push_back({w,b});std::sort(order.rbegin(),order.rend());
        float total=0;for(size_t k=0;k<4&&k<order.size();++k)total+=order[k].first;int sum=0;
        for(unsigned k=0;k<4;++k){centre.bone[k]=k<order.size()?order[k].second:0;centre.weight[k]=uint16_t(k<order.size()?std::lround(order[k].first/total*65535.f):0);sum+=centre.weight[k];}
        // fix 2026-10-25b: the rounding remainder (|r|<=2) goes to the largest weight. The old remainder slot wrapped to 65535
        // when the rounded weights summed to 65536 (EM18 left shoulder loop: two exact halves rounded up), so the linearly
        // skinned centre sat at twice its world position and its fan drew a thin stripe to the screen edge
        centre.weight[0]=uint16_t(int(centre.weight[0])+65535-sum);
        const uint32_t base=uint32_t(cap->mesh.vertices.size());cap->mesh.vertices.push_back(centre);
        for(const auto* e:loop){auto v=mesh.vertices[e->a];const auto d=v.position-c;v.normal=n;v.uv=stumpTexture?re3model::Vec2{0.5f+0.42f*re3model::Dot(d,e1)/radius,0.5f+0.42f*re3model::Dot(d,e2)/radius}:texel;cap->mesh.vertices.push_back(v);}
        auto& list=byMaterial[material];for(uint32_t i=0;i<loop.size();++i){list.push_back(base);list.push_back(base+1+i);list.push_back(base+1+(i+1)%uint32_t(loop.size()));}
        ++loops;
    }
    if(!loops)return slot;
    for(const auto& [m,list]:byMaterial){auto material=mesh.materials[m];material.firstIndex=uint32_t(cap->mesh.indices.size());material.indexCount=uint32_t(list.size());
        cap->mesh.indices.insert(cap->mesh.indices.end(),list.begin(),list.end());cap->mesh.materials.push_back(material);
        auto capTextures=body.textures[m];if(capTextures.albedo)capTextures.albedo->AddRef();if(capTextures.stp)capTextures.stp->AddRef();cap->textures.push_back(capTextures);}
    log("MODEL_PART_CAPS %s mask=%x loops=%u triangles=%zu",body.id.c_str(),mask,loops,cap->mesh.indices.size()/3);
    return slot=cap;
}
static bool prepareModelActor(void*,const re3bridge::ActorContext& context,re3bridge::ActorPlan& plan){
    ModelCostTimer cost(modelFramePrepareMs);
    struct Fps60Scope{Fps60Scope(const void* actor){fps60Actor=actor;fps60Occurrences.clear();}~Fps60Scope(){fps60Actor=nullptr;}} fps60Scope(context.actor);
    static bool capturedCloseCamera=false;
    const bool autoCloseCamera=!capturedCloseCamera&&context.isPlayer&&*(uint16_t*)(gameBase+0x661fa6)==0&&*(uint16_t*)(gameBase+0x661fa8)==1&&*(uint16_t*)(gameBase+0x661faa)==0x1a&&GetPrivateProfileIntA("pilot","debug_warp",0,(root+"\\pilot.ini").c_str());
    if((modelPoseRequested||autoCloseCamera)&&context.isPlayer&&context.bones.size()>=15){
        modelPoseRequested=false;if(autoCloseCamera)capturedCloseCamera=true;std::ofstream out(root+(autoCloseCamera?"/native-pose-R1011A.json":"/native-pose.json"));
        auto matrix=[&](const re3bridge::Matrix32& m){out<<"[";for(int i=0;i<12;i++){if(i)out<<",";if(i%4==3)out<<m.translation[i/4];else out<<m.rotation[(i/4)*3+i%4]/4096.f;}out<<"]";};
        out<<"{\"schema\":\"re3-native-world-pose-v1\",\"serial\":"<<context.serial<<",\"model_id\":"<<unsigned(context.modelId)<<",\"weapon_id\":"<<unsigned(context.weaponId)<<",\"coordinate_space\":\"native world Y-down, matrices row-major 3x4 column-vector\",\"camera\":";matrix(context.camera);
        out<<",\"world_bones\":[";for(unsigned i=0;i<15;i++){if(i)out<<",";matrix(context.bones[i].world);}out<<"],\"weapon_hand_world\":";matrix(context.bones[4].world);out<<",\"projection_h\":"<<re3bridge::detail::Read<uint16_t>(context.gte.data(),0x78)<<",\"zsf3\":"<<re3bridge::detail::Read<int16_t>(context.gte.data(),0x80)<<"}";
        log("MODEL_POSE_SAVED serial=%u weapon=%02x",context.serial,context.weaponId);
    }
    // characters need their full skeleton (checked below against the rule); Nemesis tentacles have 6 bones, the EM26
    // baby spider a single part
    if(!modelActive||modelFatal||!gameDevice||!gogWrapper||context.bones.empty())return false;
    const bool jill=context.isPlayer&&context.modelId==0&&context.playerCostume==0;
    if((jill||(!context.isPlayer&&context.modelId==0x54))&&context.bones.size()<15)return false;
    const bool dario=!context.isPlayer&&context.modelId==0x54;
    const ActorRule* rule=(jill||dario||modelParity)?nullptr:findActorRule(context);
    if(!jill&&!dario&&!rule)return false;
    const unsigned boneCount=rule?rule->bones:15u;
    if(context.bones.size()<boneCount)return false;
    // The standard player and her attached weapon, and the characters of actors.txt, opt into other rooms.
    // Dario, props and diagnostic parity keep their original pilot scope.
    if(!pilotRoom()&&!(jill&&!modelParity&&modelJillAllRooms)&&!(rule&&modelCharactersAllRooms))return false;
    // NPC hand+weapon: part 15 shown over the hidden hand part 4, or (Brad/Carlos cutscenes, October 2026 log) the
    // engine swapped the mesh objects of parts 4 and 15 and part 4 itself draws hand+weapon.
    // Enemy spare torso in part 15 (EM17, R118 corridor, October 2026): drawn by the HD body with the upper texture
    // var-<torso15>.png. Before this it was taken for an NPC weapon and kept the whole zombie native without a log line.
    const bool torso15Shown=rule&&!rule->player&&!rule->torso15.empty()&&context.bones.size()>15&&context.bones[15].visible;
    // fix 2026-10-25b: not for player rules (Jill PL01, Carlos): their parts 15-20 are the weapon's effect parts (mine thrower:
    // one mine per round in parts 15..20, part 15 visible while the first mine flies) and stay native as for Jill PL00
    const bool weapon15Shown=rule&&!rule->player&&!torso15Shown&&boneCount==15&&context.bones.size()>15&&context.bones[15].visible;
    uint32_t handVertices=0;uint16_t handTriangles=0,handQuads=0;
    const bool weaponInHand4=rule&&rule->hasWeaponMesh&&boneCount==15&&partMeshCounts(context.bones[4],handVertices,handTriangles,handQuads)&&
        handVertices==rule->weaponMesh.vertices&&handTriangles==rule->weaponMesh.triangles&&handQuads==rule->weaponMesh.quads;
    if((weapon15Shown||weaponInHand4)&&rule->weapon15.empty())return false;
    // Enemy head part hidden or in the native special state (render 0x20 with a collapsed scale: blown off by a
    // magnum/shotgun hit): the HD body drops its head region and shows the neck stump instead of going native.
    bool headGone=false;
    // fix 2026-10-25: enemy part states no longer send the whole actor native. Native per-part update 435750: flags 2 = own
    // matrix (not rebuilt from the parent), flags 8 = fall physics 436D40 (parts_down / explosion drops set 0xA, the part is
    // hidden after the last bounce and its children get flags&~1|8); render 0x20 = shatter 436F70 (polygons fly, flags
    // cleared after 28 frames); flags 4 = scale applied at draw (4358F0). HD: a gone or shattering part loses the triangles
    // of its bone (the shatter itself stays native), a falling part with its children is a rigid HD piece on its own matrix,
    // a scale goes into the bone matrix. The head of a pack with regions keeps the Head/Stump switch.
    // (zombies, creatures, Nemesis; NPCs with a held weapon keep the whole-actor fallback)
    const bool enemyParts=rule&&!rule->player&&rule->weapon15.empty();uint32_t partGone=0,partShatter=0,partFalling=0,partScaled=0;
    // v20: a player decapitated by a Hunter (EM22 grab routine 4C9780 reddens parts 0,1,2,5, then the head is hidden;
    // user log 2026-10-24, R40F: head flags=1 render=4) is drawn with the headless pack instead of going native
    const std::string headlessAsset=!context.isPlayer||modelParity?std::string():jill?std::string("jill-standard-headless"):rule?rule->headless:std::string();
    for(unsigned i=0;i<boneCount;i++){const auto& part=context.bones[i];
        if(rule&&i<32&&((rule->cutParts>>i)&1u))continue; // fix 2026-10-25: a cut= part is the body pack's business (parts.txt)
        // Native MDF keeps transform-only bones with <4 mesh vertices and
        // renderAttributes=0; their surfaces are drawn by other weighted parts.
        const bool expectedGeometry=rule?((rule->geometry>>i)&1u)!=0&&!(weapon15Shown&&i==4)&&!(torso15Shown&&i==0):jill?(i==0||i==1||i==2||i==4||i==5||i==7||i==8||i==10||i==13):(i==0||i==1||i==2||i==4||i==5||i==7||i==8||i==11||i==14);
        const bool semanticHidden=expectedGeometry&&(!(part.flags&1)||!(part.renderAttributes&1));
        const bool scaleAlreadyInWorld=(part.renderAttributes&0x10)||part.renderAttributes==0;
        const bool scaled=(part.flags&4)&&!scaleAlreadyInWorld&&(part.scale[0]!=4096||part.scale[1]!=4096||part.scale[2]!=4096);
        if(enemyParts&&i<32){const uint32_t bit=1u<<i;const bool shatter=(part.renderAttributes&0x20)!=0;
            const bool hidden=expectedGeometry?semanticHidden:rule->partScale?!(part.flags&1)||!(part.renderAttributes&1):(part.flags&9)==8;
            // a zero scale axis (EM3E/EM27 growing in from 0) is gone rather than a singular bone the skin would refuse
            const bool flat=scaled&&(!part.scale[0]||!part.scale[1]||!part.scale[2]);
            if(hidden||shatter||flat)partGone|=bit;else if((part.flags&0xb)==0xb)partFalling|=bit;else if(scaled)partScaled|=bit;
            if(shatter&&part.visible)partShatter|=bit;
            continue;}
        if(i==1&&!headlessAsset.empty()&&!(part.renderAttributes&1)&&(part.renderAttributes&4)){headGone=true;continue;}
        if(semanticHidden||scaled||(part.renderAttributes&0x20)){if(modelObserved.insert("native-special-part:"+std::to_string(i)).second)log("MODEL_NATIVE_PART_FALLBACK part=%u flags=%x render=%x scale=%d,%d,%d",i,part.flags,part.renderAttributes,part.scale[0],part.scale[1],part.scale[2]);return false;}
    }
    // Enemies: a visible part past the skeleton that is not the handled weapon (torn-open torso of EM17 and
    // similar) keeps the whole actor native. Player parts 15-20 are effect quads and stay native as for Jill.
    if(rule&&!rule->player)for(size_t i=boneCount;i<context.bones.size();++i)if(context.bones[i].visible&&!(i==15&&(weapon15Shown||torso15Shown))&&
        !(i<64&&((rule->keepNative>>i)&1))&&!rule->extraParts.count(unsigned(i))){
        if(modelObserved.insert("extra-part:"+rule->asset+":"+std::to_string(i)).second)log("MODEL_EXTRA_PART_FALLBACK %s part=%zu",rule->asset.c_str(),i);return false;}
    if(!modelRenderer.resetDevice(gameDevice))return false;
    std::string name=rule?(modelLastLook.valid&&!modelLastLook.body.empty()?modelLastLook.body:rule->asset):modelParity?(jill?"parity-jill":"parity-dario"):(jill?"jill-standard":"dario");
    if(headGone&&context.isPlayer)name=headlessAsset;
    auto body=getModel(name);if(!body)return false;
    if(rule||headGone){body->characterBody=true;if(body->mesh.inverseBind.size()!=boneCount){if(modelObserved.insert("bone-mismatch:"+name).second)log("MODEL_UNAVAILABLE %s has %zu bones, native skeleton %u",name.c_str(),body->mesh.inverseBind.size(),boneCount);return false;}}
    // fix 2026-10-25: native 4A32D0/4A3460/4A4BB0 (EM36 acid, R509) drop the left arm (6,8 fall), head (2 hidden, spare 22
    // falls) and right forearm (4 hidden, spare 23 and hand 5 fall); the EM38 death 476840 shatters limbs (render 0x20).
    // A cut= part hidden, falling (flags 2|8) or shattering is absent; a shattering one keeps its native draw (the effect).
    uint32_t cutAbsent=0,cutShatter=0;int cutParent[32];
    if(rule&&rule->cutParts){const uintptr_t first=reinterpret_cast<uintptr_t>(context.parts);const unsigned cutBones=boneCount<32u?boneCount:32u;
        for(unsigned i=0;i<cutBones;++i){const uintptr_t p=re3bridge::detail::Read<uintptr_t>(context.bones[i].nativePart,0xa4);
            cutParent[i]=p>=first&&(p-first)%re3bridge::kPartStride==0&&(p-first)/re3bridge::kPartStride<cutBones?int((p-first)/re3bridge::kPartStride):-1;}
        for(unsigned i=0;i<cutBones;++i){const auto& b=context.bones[i];if(!((rule->cutParts>>i)&1u))continue;
            if(!(b.flags&1)||!(b.renderAttributes&1)||(b.flags&0xa)==0xa||(b.renderAttributes&0x20))cutAbsent|=1u<<i;
            if((b.renderAttributes&0x20)&&b.visible)cutShatter|=1u<<i;}
        // the native children of an absent cut part (EM36 part 7 gets flags 8 under the falling arm 6) are the cut's business
        // too (their triangles sit in its parts.txt materials): no enemyParts split, reattach or limb caps for them
        uint32_t cutTree=0;for(unsigned i=0;i<cutBones;++i)for(int a=cutParent[i],n=0;a>=0&&n<32;a=cutParent[a],++n)if((cutAbsent>>a)&1u){cutTree|=1u<<i;break;}
        partGone&=~cutTree;partFalling&=~cutTree;partScaled&=~cutTree;}
    if(cutAbsent&&body->materialParts.empty()){if(modelObserved.insert("cut-native:"+name).second)log("MODEL_NATIVE_PART_FALLBACK %s absent=%x (no parts.txt)",name.c_str(),cutAbsent);return false;}
    // Body regions (zombies): per-actor clothing textures of the resolved look, head/stump switch.
    const bool zones=rule&&!body->materialRegion.empty();
    const bool hasHeadRegion=zones&&std::find(body->materialRegion.begin(),body->materialRegion.end(),uint8_t(ModelAsset::Head))!=body->materialRegion.end();
    if(headGone&&context.isPlayer&&modelObserved.insert("headless:"+name).second)log("MODEL_HEADLESS %s model=%02x head flags=%x render=%x torso render=%x",name.c_str(),context.modelId,context.bones[1].flags,context.bones[1].renderAttributes,context.bones[0].renderAttributes);
    const int head=enemyParts?rule->headPart:-1;
    if(head>=0&&head<32&&hasHeadRegion&&(((partGone|partFalling|partScaled)>>head)&1u)){headGone=true;partGone&=~(1u<<head);partScaled&=~(1u<<head);}
    // falling pieces: the part and its native children (parent part pointer at +0xA4); nested falling parts ride the outer one.
    // reattach: bones whose native matrix left the skeleton (falling, shattering, own matrix, or stale: 435750 only updates
    // parts with flags&1) follow their parent in the bind relation on the body, so no border vertex is pulled after them
    unsigned pieces=0,pieceRoot[32]{},depth[32]{};int pieceParent[32]{},parent[32]{};uint32_t pieceBones[32]{},split=0,reattach=0;
    if(partGone|partFalling){
        const uintptr_t first=reinterpret_cast<uintptr_t>(context.parts);
        for(unsigned i=0;i<boneCount;++i){const uintptr_t p=re3bridge::detail::Read<uintptr_t>(context.bones[i].nativePart,0xa4);
            parent[i]=p>=first&&(p-first)%re3bridge::kPartStride==0&&(p-first)/re3bridge::kPartStride<boneCount?int((p-first)/re3bridge::kPartStride):-1;}
        for(unsigned i=0;i<boneCount;++i)for(int a=parent[i];a>=0&&depth[i]<32;a=parent[a])++depth[i];
        for(unsigned r=0;r<boneCount;++r)if((partFalling>>r)&1u){
            bool inner=false;for(int a=parent[r],n=0;a>=0&&n<32;a=parent[a],++n)inner|=((partFalling>>a)&1u)!=0;
            if(inner)continue;
            if(parent[r]<0){if(modelObserved.insert("native-falling-root:"+name).second)log("MODEL_NATIVE_PART_FALLBACK %s part=%u flags=%x falling without a parent",name.c_str(),r,context.bones[r].flags);return false;}
            uint32_t members=0;for(unsigned b=0;b<boneCount;++b)for(int a=int(b),n=0;a>=0&&n<32;a=parent[a],++n)if(a==int(r)){members|=1u<<b;break;}
            pieceRoot[pieces]=r;pieceParent[pieces]=parent[r];pieceBones[pieces++]=members;if(!(headGone&&int(r)==head))split|=members;
        }
        reattach=split;
        for(unsigned i=0;i<boneCount;++i){const auto& b=context.bones[i];if(((partGone>>i)&1u)&&(!(b.flags&1)||(b.flags&2)||(b.renderAttributes&0x20))&&parent[i]>=0)reattach|=1u<<i;}
    }
    // nothing of the body left (zombies appearing part by part, everything shot off): the native draws the rest
    if(enemyParts&&((partGone|split)&rule->geometry)==rule->geometry)return false;
    if((partGone|split)&&body->triangleBone.empty()){
        const auto& mesh=body->mesh;body->vertexBone.resize(mesh.vertices.size());body->triangleBone.resize(mesh.indices.size()/3);
        for(size_t v=0;v<mesh.vertices.size();++v){const auto& x=mesh.vertices[v];unsigned best=0;for(unsigned k=1;k<4;++k)if(x.weight[k]>x.weight[best])best=k;body->vertexBone[v]=uint8_t(x.bone[best]);}
        for(size_t t=0;t<body->triangleBone.size();++t){uint16_t bone[12];uint32_t sum[12]{};unsigned n=0,best=0;
            for(unsigned c=0;c<3;++c){const auto& x=mesh.vertices[mesh.indices[t*3+c]];
                for(unsigned k=0;k<4;++k){if(!x.weight[k])continue;unsigned j=0;while(j<n&&bone[j]!=x.bone[k])++j;if(j==n){bone[n++]=x.bone[k];}sum[j]+=x.weight[k];}}
            for(unsigned j=1;j<n;++j)if(sum[j]>sum[best])best=j;
            body->triangleBone[t]=uint8_t(n?bone[best]:0);}
    }
    std::vector<re3model::MaterialTextures> actorTextures;std::vector<uint8_t> hiddenMaterials;
    const bool partMaterials=rule&&!body->materialParts.empty();
    if(partMaterials){hiddenMaterials.assign(body->materialParts.size(),0);
        for(size_t m=0;m<hiddenMaterials.size();++m)hiddenMaterials[m]=uint8_t((body->materialParts[m].first&cutAbsent)||(body->materialParts[m].second&~cutAbsent));
        char note[128];sprintf_s(note,"cut:%s:%x",name.c_str(),cutAbsent);
        if(cutAbsent&&modelObserved.size()<4000&&modelObserved.insert(note).second)log("MODEL_PART_CUT %s model=%02x absent=%x",name.c_str(),context.modelId,cutAbsent);}
    if(zones){
        actorTextures=body->textures;if(!partMaterials)hiddenMaterials.assign(body->materialRegion.size(),0);
        auto look=modelLastLook;
        // the spare torso replaces the base torso only; an upper swap set already wears its own (EM17 set0 = same torso)
        if(torso15Shown&&look.regionGroup[ModelAsset::Upper].empty()){look.valid=true;look.regionGroup[ModelAsset::Upper]=rule->torso15;}
        for(size_t m=0;m<body->materialRegion.size();++m){const auto region=body->materialRegion[m];
            if(region==ModelAsset::Stump)hiddenMaterials[m]|=uint8_t(!headGone);
            if(region==ModelAsset::Head)hiddenMaterials[m]|=uint8_t(headGone);
            if(region<=ModelAsset::Head&&look.valid&&!look.regionGroup[region].empty()){
                auto variant=[&](const std::string& group)->IDirect3DTexture9*{auto hit=body->variantTextures.find(group);
                    if(hit==body->variantTextures.end()){
                        auto tex=loadReplacement(gameDevice,0,"model-pack/"+name+"/var-"+group+".png",8);
                        if(tex){D3DSURFACE_DESC desc{};if(SUCCEEDED(tex->GetLevelDesc(0,&desc)))(desc.Pool==D3DPOOL_DEFAULT?body->textureBytes:body->bytes)+=size_t(desc.Width)*desc.Height*4;}
                        else log("MODEL_VARIANT_MISSING %s var-%s.png",name.c_str(),group.c_str());
                        hit=body->variantTextures.emplace(group,tex).first;
                    }
                    return hit->second;};
                const auto& group=look.regionGroup[region];auto* tex=variant(group);const size_t plus=group.find('+');
                if(!tex&&plus!=std::string::npos){
                    if(modelObserved.size()<4000&&modelObserved.insert("look-mix:"+name+":"+group).second)log("MODEL_LOOK_MIXED %s model=%02x sets=%s kept=%s",name.c_str(),context.modelId,group.c_str(),group.substr(0,plus).c_str());
                    tex=variant(group.substr(0,plus));
                }
                if(!tex)return false;
                actorTextures[m].albedo=tex;
            }
        }
        char note[512];sprintf_s(note,"look:%s:%s:%u",name.c_str(),look.text.c_str(),unsigned(headGone));
        if(modelObserved.size()<4000&&modelObserved.insert(note).second)log("MODEL_LOOK %s model=%02x look=%s upper=%s lower=%s head=%s headless=%u",name.c_str(),context.modelId,look.text.c_str(),
            look.regionGroup[0].empty()?"base":look.regionGroup[0].c_str(),look.regionGroup[1].empty()?"base":look.regionGroup[1].c_str(),look.regionGroup[2].empty()?"base":look.regionGroup[2].c_str(),unsigned(headGone));
    }
    // v20 EM35 (R30D): the rocket launcher Carlos blows up in the cutscene stays in render 0x20 (also while hidden) until a
    // retry shows it intact; meanwhile the native coat is torn (damage TIM pages) -> var-<group>.png replaces albedo.png
    if(!zones&&rule&&rule->blownPart>=0&&size_t(rule->blownPart)<context.bones.size()&&(context.bones[rule->blownPart].renderAttributes&0x20)){
        auto hit=body->variantTextures.find(rule->blownGroup);
        if(hit==body->variantTextures.end()){
            auto tex=loadReplacement(gameDevice,0,"model-pack/"+name+"/var-"+rule->blownGroup+".png",8);
            if(tex){D3DSURFACE_DESC desc{};if(SUCCEEDED(tex->GetLevelDesc(0,&desc)))(desc.Pool==D3DPOOL_DEFAULT?body->textureBytes:body->bytes)+=size_t(desc.Width)*desc.Height*4;}
            else log("MODEL_VARIANT_MISSING %s var-%s.png",name.c_str(),rule->blownGroup.c_str());
            hit=body->variantTextures.emplace(rule->blownGroup,tex).first;
        }
        if(hit->second){actorTextures=body->textures;
            for(size_t m=0;m<actorTextures.size()&&m<body->mesh.materials.size();++m)if(body->mesh.materials[m].albedoPath=="albedo.png")actorTextures[m].albedo=hit->second;
            if(modelObserved.insert("blown:"+name).second)log("MODEL_BLOWN %s model=%02x part=%d var-%s",name.c_str(),context.modelId,rule->blownPart,rule->blownGroup.c_str());}
    }
    std::vector<re3model::Matrix3x4> bones;for(unsigned i=0;i<boneCount;i++)bones.push_back(re3model::FromNativeMatrix(context.bones[i].world));
    auto& actor=actorState(context.actor,context.modelId,name,context.serial);
    if(modelMotionSmoothing&&!modelParity&&boneCount==15){
        auto& smoother=actor.motion;re3motion::Pose raw;std::copy(bones.begin(),bones.end(),raw.begin());
        auto& joints=actor.joints;
        const auto& shown=joints.Process(smoother.Process(raw,context.serial),context.serial);std::copy(shown.begin(),shown.end(),bones.begin());
        if(smoother.stats.frames>=300){const auto& s=smoother.stats;log("MODEL_MOTION %s frames=%u keys=%u held=%u smoothed=%u resets=%u max_hold=%u root_smoothed=%u offset_resets=%u",name.c_str(),s.frames,s.keys,s.heldFrames,s.smoothed,s.resets,s.maxHold,joints.rootSmoothed,joints.offsetResets);smoother.stats={};joints.rootSmoothed=joints.offsetResets=0;}
    }
    if(headGone){
        // the stump and the neck follow the torso: the native head matrix is collapsed after a blow-off
        re3model::Matrix3x4 headBind;
        const unsigned h=rule?unsigned(rule->headPart):1u;
        if(re3model::InverseAffine(body->mesh.inverseBind[h],headBind))bones[h]=re3model::Multiply(bones[0],re3model::Multiply(body->mesh.inverseBind[0],headBind));
    }
    // fix 2026-10-25: native part scale (EM27 growing in); reattached bones (see above), parents first
    for(unsigned i=0;i<boneCount&&i<32;++i)if((partScaled>>i)&1u)for(unsigned r=0;r<3;++r)for(unsigned c=0;c<3;++c)bones[i].m[r*4+c]*=context.bones[i].scale[c]*(1.f/4096.f);
    for(unsigned d=1;d<32&&reattach;++d)for(unsigned b=0;b<boneCount;++b){re3model::Matrix3x4 bind;
        if(((reattach>>b)&1u)&&depth[b]==d&&re3model::InverseAffine(body->mesh.inverseBind[b],bind))bones[b]=re3model::Multiply(re3model::Multiply(bones[parent[b]],body->mesh.inverseBind[parent[b]]),bind);}
    // the bones of an absent cut part and of its native children keep their bind relation to the nearest present ancestor
    // (parent part pointer +0xA4): the stump does not stretch after a falling part; the pieces use the part matrices
    if(cutAbsent){ // cutParent: see cutAbsent above
        for(unsigned b=0;b<boneCount&&b<32;++b){int gone=-1;for(int a=int(b),n=0;a>=0&&n<32;a=cutParent[a],++n)if((cutAbsent>>a)&1u)gone=a;
            if(gone<0)continue;int keep=cutParent[gone];while(keep>=0&&((cutAbsent>>keep)&1u))keep=cutParent[keep];
            re3model::Matrix3x4 bind;if(keep>=0&&re3model::InverseAffine(body->mesh.inverseBind[b],bind))bones[b]=re3model::Multiply(bones[keep],re3model::Multiply(body->mesh.inverseBind[keep],bind));}}
    // fix 2026-10-25c: Jill aiming the Western Custom straight ahead: the HD gun sits 6.4 deg nose-up in the hand (its grip
    // fix), so both arms are lowered from the shoulders by that much; gun and left hand follow (6-frame blend). Aiming up or
    // down stays native: the direction is the nearest native left wrist of the held aims (PL00W10 bank 2, weapon frame),
    // kept while the hand is off the gun (lever spin, reload, raise)
    float dropRot[9];re3model::Vec3 dropPivot{};bool dropped=false;
    if(context.isPlayer&&(context.modelId==0||context.modelId==1)&&!modelParity&&boneCount==15){
        const uint8_t sub=re3bridge::detail::Read<uint8_t>(context.actor,6);
        const bool aim=context.weaponId==0x10&&re3bridge::detail::Read<uint8_t>(context.actor,4)==1&&re3bridge::detail::Read<uint8_t>(context.actor,5)==5&&sub!=3;
        float want=aim?actor.drop:0.f;re3model::Matrix3x4 inv4;
        if(aim&&(sub==0||sub==1)&&re3model::InverseAffine(bones[4],inv4)){
            const re3model::Vec3 rel=re3grip::Origin(re3model::Multiply(inv4,bones[7]));
            const float ds=re3grip::Length(rel-re3model::Vec3{-46.6f,278.9f,-44.4f}),du=re3grip::Length(rel-re3model::Vec3{-55.7f,340.4f,61.5f}),dd=re3grip::Length(rel-re3model::Vec3{-85.9f,257.f,14.f});
            if((std::min)(ds,(std::min)(du,dd))<60)want=ds<du&&ds<dd?1.f:0.f;}
        if(context.serial!=actor.dropSerial){const bool fresh=actor.dropSerial==0||context.serial-actor.dropSerial>4;actor.dropSerial=context.serial;
            actor.drop=fresh?want:want>actor.drop?(std::min)(want,actor.drop+1.f/6):(std::max)(want,actor.drop-1.f/6);}
        if(actor.drop>0)dropped=re3handik::LowerArms(bones,6.4f*actor.drop,dropRot,dropPivot);
        if(dropped&&actor.drop>0.99f&&modelObserved.insert("arm-drop").second)log("MODEL_ARM_DROP weapon=%02x degrees=6.4",context.weaponId);
    }
    // v10: Jill (also with the harness, PL01) aiming a handgun up/down keeps the straight-aim left-hand grip
    if(context.isPlayer&&(context.modelId==0||context.modelId==1)&&!modelParity&&boneCount==15){
        const float grip=re3grip::LockLeftGrip(bones,context.weaponId);
        if(grip>0.99f&&modelObserved.insert("grip-lock").second)log("MODEL_GRIP_LOCK weapon=%02x",context.weaponId);
    }
    // fix 2026-10-25b: hand-ik.txt rows (Jill: left hand lower on the mine-thrower handle in aim, palm on the M4A1 handguard
    // outside aim); the factor per native state is logged once
    if(context.isPlayer&&!modelParity&&boneCount==15&&!modelHandIk.empty()){
        const uint8_t routine[3]={re3bridge::detail::Read<uint8_t>(context.actor,4),re3bridge::detail::Read<uint8_t>(context.actor,5),re3bridge::detail::Read<uint8_t>(context.actor,6)};int row=-1;
        const float f=re3handik::Apply(modelHandIk,actor.handIk,bones,context.modelId,context.weaponId,routine,context.serial,row);
        char note[96];sprintf_s(note,"hand-ik:%d:%02x:%02x:%u,%u,%u:%u",row,context.modelId,context.weaponId,routine[0],routine[1],routine[2],f>0.99f?2u:f>0?1u:0u);
        if(re3handik::Covers(modelHandIk,context.modelId,context.weaponId)&&modelObserved.size()<4000&&modelObserved.insert(note).second)
            log("MODEL_HAND_IK row=%d model=%02x weapon=%02x routine=%u,%u,%u factor=%.2f",row,context.modelId,context.weaponId,routine[0],routine[1],routine[2],f);
    }
    if(!modelParity)driveFace(body,actor,jill?0:dario?1:rule->speaker,bones);
    auto projection=actorProjection(context);
    if(zones||partMaterials){projection.hiddenMaterials=&hiddenMaterials;projection.sortWithinBucketByMaterial=true;}
    std::vector<uint8_t> bodyHidden;
    if(partGone|split){bodyHidden.resize(body->triangleBone.size());for(size_t t=0;t<bodyHidden.size();++t)bodyHidden[t]=uint8_t(((partGone|split)>>body->triangleBone[t])&1u);projection.hiddenTriangles=&bodyHidden;}
    if(partGone|partShatter|partFalling|partScaled){char note[160];sprintf_s(note,"parts:%s:%x:%x:%x:%x:%u",name.c_str(),partGone,partShatter,partFalling,partScaled,unsigned(headGone));
        if(modelObserved.size()<4000&&modelObserved.insert(note).second)log("MODEL_PART_SPLIT %s model=%02x gone=%x shatter=%x falling=%x scaled=%x headless=%u pieces=%u",name.c_str(),context.modelId,partGone,partShatter,partFalling,partScaled,unsigned(headGone),pieces);}
    re3model::NativeLighting lighting;
    const auto tint=re3bridge::detail::Read<uint32_t>(context.bones[0].nativePart,0x68)&0xffffffu;
    if(re3model::CaptureNativeLighting(lighting,context.worldLight,context.gte.data(),context.gte.size(),tint)){
        projection.shade=&re3model::ShadeNativeNccs;projection.shadeContext=&lighting;
    }
    std::shared_ptr<ModelAsset> weapon,weaponAux;
    const WeaponEntry* weaponRule=nullptr;
    uint8_t equippedItem=0;
    if((jill||(rule&&rule->player))&&!modelParity&&context.weaponId){
        // the main game's player with an aliased inventory item (Glock 18C 53/54 = M92F 03)
        if(context.actor==reinterpret_cast<void*>(gameBase+0x65cbf4)&&(equippedItem=glockEquippedItem())!=0){
            const auto item=modelItemWeapons.find(uint16_t((unsigned(context.modelId)<<8)|equippedItem));if(item!=modelItemWeapons.end())weaponRule=&item->second;else equippedItem=0;}
        if(!weaponRule){const auto hit=modelWeapons.find(uint16_t((unsigned(context.modelId)<<8)|context.weaponId));if(hit!=modelWeapons.end())weaponRule=&hit->second;}
    }
    if(weaponRule){
        if(!weaponRule->main.empty()){weapon=getModel(weaponRule->main);if(!weapon)return false;}
        if(!weaponRule->aux22.empty()&&context.bones.size()>22&&context.bones[22].visible){weaponAux=getModel(weaponRule->aux22);if(!weaponAux)return false;}
        char note[160];sprintf_s(note,"weapon:%02x:%02x:%02x",context.modelId,context.weaponId,equippedItem);
        if(modelObserved.insert(note).second)log("MODEL_WEAPON model=%02x weapon=%02x item=%02x main=%s aux22=%s hide22=%u parts=%u",context.modelId,context.weaponId,equippedItem,weaponRule->main.empty()?"-":weaponRule->main.c_str(),weaponRule->aux22.empty()?"-":weaponRule->aux22.c_str(),unsigned(weaponRule->hide22),context.partCount);
    }
    else if(jill&&!modelParity&&context.weaponId&&context.weaponId!=0x10){
        std::string weaponName=context.weaponId==1?"knife-native":(context.weaponId==2||context.weaponId==3)?"m92f":(context.weaponId==0x0e||context.weaponId==0x0f)?"m4a1":"";
        if(weaponName.empty()){const unsigned safeIds[]={4,5,6,7,8,9,0x0a,0x0b,0x0d,0x11,0x12,0x13};if(std::find(std::begin(safeIds),std::end(safeIds),context.weaponId)==std::end(safeIds))return false;char nativeName[32];sprintf_s(nativeName,"native-weapon-%02X",context.weaponId);weaponName=nativeName;}
        weapon=getModel(weaponName);if(!weapon)return false;
    }
    // Player characters of actors.txt (Carlos): weapon-only meshes of their own PLW files, hand-local.
    else if(rule&&rule->player&&!rule->weapons.empty()&&context.weaponId){
        char weaponName[64];sprintf_s(weaponName,"%s%02X",rule->weapons.c_str(),context.weaponId);
        weapon=getModel(weaponName);if(!weapon)return false;
    }
    std::shared_ptr<ModelAsset> heldWeapon;
    if(weapon15Shown||weaponInHand4){heldWeapon=getModel(rule->weapon15);if(!heldWeapon)return false;}
    // fix 2026-10-24: weapons.txt muzzle/support rows of the HD weapon in hand 4 (left hand to the foregrip before skinning)
    const WeaponFit* fit=nullptr;
    // fix 2026-10-25b: a part-22-only gun (M37, main '-') carries its muzzle row on the aux22 asset
    if(const ModelAsset* held=heldWeapon?heldWeapon.get():weapon&&context.bones[4].visible?weapon.get():!weapon&&weaponAux?weaponAux.get():nullptr){const auto hit=modelWeaponFits.find(held->id);if(hit!=modelWeaponFits.end())fit=&hit->second;}
    if(fit&&fit->support){
        const float f=re3grip::ReachForegrip(bones,{fit->foregrip[0],fit->foregrip[1],fit->foregrip[2]},{fit->palm[0],fit->palm[1],fit->palm[2]});
        if(f>0.99f&&modelObserved.insert("support:"+name).second)log("MODEL_SUPPORT_HAND %s model=%02x weapon=%02x",name.c_str(),context.modelId,context.weaponId);
    }
    // M37 ID10 has only the hand in native part4; its actual gun remains the
    // untouched native part22. The HD body supplies the replacement hand.
    std::vector<re3model::SkinnedVertex> bodySkinned;const bool wearsOverlay=rule&&!rule->overlay.empty();
    // v12 enemies: the native damage red of hit parts (render attribute 4, colour at part+0x78) and the wounds of the
    // player's shots
    bool woundsWanted=false;
    if(rule&&!rule->player&&!modelParity){
        uint32_t damaged=0,colour[32]{};
        for(unsigned i=0;i<boneCount&&i<32;++i)if(context.bones[i].renderAttributes&4){damaged|=1u<<i;colour[i]=re3bridge::detail::Read<uint32_t>(context.bones[i].nativePart,0x78)&0xffffffu;}
        // The wounds stay for the whole room (cleared in beginModelBuild) and with the actor's model (actorState): a
        // part losing its native red only recolours it, and dead or downed enemies keep their wounds.
        if(damaged!=actor.damagedParts||(damaged&&actor.damageTint.size()!=body->mesh.vertices.size())){
            actor.damagedParts=damaged;re3wound::damageTint(body->mesh,colour,damaged,actor.damageTint);
            char note[96];sprintf_s(note,"damage-red:%s:%x",name.c_str(),damaged);
            if(damaged&&modelObserved.size()<4000&&modelObserved.insert(note).second){unsigned first=0;while(!((damaged>>first)&1u))++first;
                log("MODEL_DAMAGE_RED %s model=%02x parts=%x colour=%06x",name.c_str(),context.modelId,damaged,colour[first]);}
        }
        if(!actor.damageTint.empty())projection.vertexTint=&actor.damageTint;
        woundsWanted=!actor.wounds.empty()||std::any_of(re3wound::pendingHits.begin(),re3wound::pendingHits.end(),[&](const re3wound::PendingHit& h){return h.actor==context.actor;});
    }
    const bool bodyReady=appendModelJobs(body,bones,projection,plan,actorTextures.empty()?nullptr:&actorTextures,(wearsOverlay||woundsWanted)?&bodySkinned:nullptr);
    // falling pieces (R402 head from parts_down, limbs blown off): the piece's own native matrices, the rest of the mesh rigid
    // with its root, only the piece's triangles (a head of a pack with regions: the Head material); a failing piece skips itself
    auto capProjection=projection;capProjection.hiddenMaterials=nullptr;capProjection.hiddenTriangles=nullptr;capProjection.vertexTint=nullptr;capProjection.sortWithinBucketByMaterial=false;
    if(bodyReady&&rule&&reattach)if(auto caps=limbCaps(*body,reattach))appendModelJobs(caps,bones,capProjection,plan);
    for(unsigned k=0;k<pieces&&bodyReady;++k){
        const unsigned r=pieceRoot[k];const bool region=headGone&&int(r)==head;
        const auto raw=re3model::FromNativeMatrix(context.bones[r].world),rigid=re3model::Multiply(raw,body->mesh.inverseBind[r]);
        std::vector<re3model::Matrix3x4> pose(boneCount,raw);
        for(unsigned b=0;b<boneCount;++b){re3model::Matrix3x4 bind;
            if((pieceBones[k]>>b)&1u)pose[b]=re3model::FromNativeMatrix(context.bones[b].world);else if(re3model::InverseAffine(body->mesh.inverseBind[b],bind))pose[b]=re3model::Multiply(rigid,bind);}
        auto pieceProjection=projection;std::vector<uint8_t> pieceMaterials,pieceTriangles;
        if(region){pieceMaterials.resize(body->materialRegion.size());for(size_t m=0;m<pieceMaterials.size();++m)pieceMaterials[m]=body->materialRegion[m]!=ModelAsset::Head;pieceProjection.hiddenMaterials=&pieceMaterials;pieceProjection.hiddenTriangles=nullptr;}
        else{pieceTriangles.resize(body->triangleBone.size());for(size_t t=0;t<pieceTriangles.size();++t){const unsigned b=body->triangleBone[t];pieceTriangles[t]=uint8_t(!((pieceBones[k]>>b)&1u)||((partGone>>b)&1u));}pieceProjection.hiddenTriangles=&pieceTriangles;}
        const bool drawn=appendModelJobs(body,pose,pieceProjection,plan,actorTextures.empty()?nullptr:&actorTextures);
        if(drawn&&!region)if(auto caps=limbCaps(*body,pieceBones[k]))appendModelJobs(caps,pose,capProjection,plan);
        const float dx=raw.m[3]-bones[r].m[3],dy=raw.m[7]-bones[r].m[7],dz=raw.m[11]-bones[r].m[11],drift=std::sqrt(dx*dx+dy*dy+dz*dz);
        char note[96];sprintf_s(note,"piece:%p:%u:%u",context.actor,r,unsigned(drift>300));
        if(modelObserved.size()<4000&&modelObserved.insert(note).second)log("MODEL_PART_PIECE %s model=%02x actor=%p part=%u parent=%d bones=%x flags=%x drift=%.0f region=%u drawn=%u",name.c_str(),context.modelId,context.actor,r,pieceParent[k],pieceBones[k],context.bones[r].flags,drift,unsigned(region),unsigned(drawn));
    }
    projection.vertexTint=nullptr; // the body's own vertex order only
    if(partMaterials){projection.hiddenMaterials=nullptr;projection.sortWithinBucketByMaterial=false;} // the body's own materials only
    projection.hiddenTriangles=nullptr;
    if(jill){const auto tag=recordModelPoseSequence(context,bodyReady?modelLastSkinMode:"native-fallback",bodyReady&&body->affineDualQuaternion?&modelLastShoulderFactors:nullptr);if(!tag.empty()&&!plan.jobs.empty())reinterpret_cast<ModelJob*>(plan.jobs.front().token)->captureTag=tag;}
    if(!bodyReady)return false;
    if(wearsOverlay)appendOverlay(name,rule->overlay,bodySkinned,projection,plan);
    if(woundsWanted){takeShots(context,actor,*body,bodySkinned,zones||partMaterials?&hiddenMaterials:nullptr,name);appendWounds(actor.wounds,bodySkinned,zones||partMaterials?&hiddenMaterials:nullptr,projection,plan,body->id,&body->vertexBone,partGone|split);}
    if(weapon&&context.bones[4].visible){
        std::vector<re3model::Matrix3x4> hand{bones[4]}; // same (smoothed) hand as the body
        if(!appendModelJobs(weapon,hand,projection,plan))return false;
    }
    if(weaponAux){
        // part 22 keeps its own native motion (barrel spin, M37 lever spin) relative to the native hand; carried over
        // to the smoothed HD hand so the piece stays on the weapon
        const auto raw4=re3model::FromNativeMatrix(context.bones[4].world),raw22=re3model::FromNativeMatrix(context.bones[22].world);
        re3model::Matrix3x4 handInverse;if(!re3model::InverseAffine(raw4,handInverse))return false;
        auto local22=re3model::Multiply(handInverse,raw22);
        const auto move=modelWeaponFits.find(weaponAux->id);   // fix 2026-10-24: follows its moved weapon (hand frame)
        if(move!=modelWeaponFits.end()&&move->second.move22){local22.m[3]+=move->second.hand22[0];local22.m[7]+=move->second.hand22[1];local22.m[11]+=move->second.hand22[2];}
        std::vector<re3model::Matrix3x4> part22{re3model::Multiply(bones[4],local22)};
        if(!appendModelJobs(weaponAux,part22,projection,plan))return false;
    }
    if(weaponRule&&(weaponAux||weaponRule->hide22||!weaponRule->aux22.empty())&&context.bones.size()>22)plan.suppressParts[22]=1;
    // rigid HD meshes for enemy parts past the skeleton (Nemesis rocket launcher): the part's own native matrix
    // Render 0x20 is the native shatter state (4358F0 -> 4326C0/432AC0/436F70): the engine stops updating the part's
    // matrix and draws its polygons flying apart until the counter at +0x7C passes 28 and clears the part (EM35 launcher
    // shot by Carlos in R30D). The HD mesh would hang intact on the frozen matrix (round 3), so the part stays native.
    if(rule&&!rule->player)for(const auto& [part,asset]:rule->extraParts){
        if(part>=context.bones.size()||!context.bones[part].visible)continue;
        if(part<boneCount&&(context.bones[part].flags&0xa)!=0xa)continue; // a skeleton part's piece only while it falls
        if(context.bones[part].renderAttributes&0x20){
            if(modelObserved.insert("extra-shatter:"+name+":"+std::to_string(part)).second)log("MODEL_EXTRA_PART_SHATTER %s part=%u native shatter kept",name.c_str(),part);
            continue;
        }
        auto extra=getModel(asset);if(!extra)return false;
        if(!extra->followStations.empty()){
            // a worm wholly clipped this frame keeps its native part instead of turning the whole actor native
            if(!appendModelJobs(extra,followNativePart(*extra,context.bones[part],part),projection,plan))continue;
        }else{
        std::vector<re3model::Matrix3x4> local{re3model::FromNativeMatrix(context.bones[part].world)};
        if(!appendModelJobs(extra,local,projection,plan))return false;
        }
        plan.suppressParts[part]=1;
        if(modelObserved.insert("extra:"+name+":"+std::to_string(part)).second)log("MODEL_EXTRA_PART %s part=%u asset=%s",name.c_str(),part,asset.c_str());
    }
    if(heldWeapon){
        // Part 15 hangs on the wrist of hand 4; follow the (smoothed) HD hand unless the engine moved it apart.
        // Swapped into part 4, the weapon simply rides hand 4.
        bool same=true;
        if(!weaponInHand4){
            const auto raw15=re3model::FromNativeMatrix(context.bones[15].world),raw4=re3model::FromNativeMatrix(context.bones[4].world);
            for(unsigned k=0;k<12;++k)same=same&&std::abs(raw15.m[k]-raw4.m[k])<=((k%4==3)?2.f:0.002f);
            if(!same){std::vector<re3model::Matrix3x4> hand{raw15};if(!appendModelJobs(heldWeapon,hand,projection,plan))return false;}
        }
        if(same){std::vector<re3model::Matrix3x4> hand{bones[4]};if(!appendModelJobs(heldWeapon,hand,projection,plan))return false;}
        plan.suppressParts[15]=1;
        if(modelObserved.insert("held-weapon:"+rule->weapon15+(weaponInHand4?":part4":":part15")).second)log("MODEL_HELD_WEAPON %s %s via=%s",rule->asset.c_str(),rule->weapon15.c_str(),weaponInHand4?"part4-swap":"part15");
    }
    for(unsigned i=0;i<boneCount;i++)plan.suppressParts[i]=1;
    for(unsigned i=0;i<boneCount&&i<32;i++)if(((partShatter|cutShatter)>>i)&1u)plan.suppressParts[i]=0; // its flying polygons stay native
    if(torso15Shown){plan.suppressParts[15]=1;if(modelObserved.insert("torso15:"+name).second)log("MODEL_TORSO15 %s model=%02x upper=var-%s part0visible=%u",name.c_str(),context.modelId,rule->torso15.c_str(),unsigned(context.bones[0].visible));}
    // Diagnostic original-mesh parity retains the native weapon+hand command.
    // Production HD body includes its hands and uses a separate gun-only mesh.
    if(jill&&modelParity&&context.weaponId)plan.suppressParts[4]=0;
    if(modelObserved.insert("actor:"+name).second)log("MODEL_ACTOR %s model=%02x weapon=%02x costume=%u parts=%u actor=%p",name.c_str(),context.modelId,context.weaponId,context.playerCostume,context.partCount,context.actor);
    if(fit&&fit->muzzle){if(muzzleHands.size()>64)muzzleHands.clear();auto& m=muzzleHands[static_cast<const uint8_t*>(context.bones[4].nativePart)+0x40];std::copy(fit->shift,fit->shift+3,m.shift);std::copy(fit->port,fit->port+3,m.port);m.serial=context.serial;
        m.dropped=dropped;if(dropped){std::copy(dropRot,dropRot+9,m.drop);m.pivot[0]=dropPivot.x;m.pivot[1]=dropPivot.y;m.pivot[2]=dropPivot.z;m.hand=re3model::FromNativeMatrix(context.bones[4].world);}}
    modelPreparedActorMask|=jill?1u:dario?2u:4u;++modelPreparedActors;
    return true;
}
static bool beginModelBuild(void*,uint32_t serial){
    fpsprobe::Mark('B',serial,re3bridge::RenderSerial(),_ReturnAddress());
    static double lastBuildTime=0;static bool previousMode=false;
    static unsigned dualSamples=0,soloSamples=0;static double dualTime=0,soloTime=0,dualMax=0,soloMax=0;
    const double entryTime=modelClockMs(),delta=entryTime-lastBuildTime;
    static uint32_t previousCamera=~0u;static unsigned steadyWarm=0,steadySamples=0;static double steadyTime=0,steadyMax=0;
    const uint32_t cameraKey=(*(uint16_t*)(gameBase+0x661fa6)<<16)|(*(uint16_t*)(gameBase+0x661fa8)<<8)|*(uint16_t*)(gameBase+0x661faa);
    if(lastBuildTime&&previousMode&&modelPreparedActorMask==3&&modelRenderedActorMask==3&&cameraKey==previousCamera&&delta>0&&delta<1000){
        if(++steadyWarm>10){++steadySamples;steadyTime+=delta;steadyMax=(std::max)(steadyMax,delta);if(steadySamples==120){log("MODEL_STEADY_DUAL_RATE camera=%06x actors=2 mode=remastered samples=120 warmup_frames=10 elapsed_ms=%.3f fps=%.3f max_frame_ms=%.3f",cameraKey,steadyTime,120000.0/steadyTime,steadyMax);steadySamples=0;steadyTime=steadyMax=0;}}
    }else{steadyWarm=steadySamples=0;steadyTime=steadyMax=0;}
    previousCamera=cameraKey;modelRenderedActorMask=0;
    fps60BuildCamera[serial&63][0]=serial;fps60BuildCamera[serial&63][1]=cameraKey;
    // v12: wounds and damage red belong to the enemies of one room visit; queued hits wait 30 frames for their actor
    static uint32_t woundRoom=~0u;
    if((cameraKey>>8)!=woundRoom){
        size_t cleared=0;for(auto& entry:actorRuntime){cleared+=entry.second.wounds.size();entry.second.wounds.clear();entry.second.damageTint.clear();entry.second.damagedParts=0;}
        if(cleared)log("MODEL_WOUND_CLEAR room=%04x wounds=%zu",unsigned(cameraKey>>8),cleared);
        woundRoom=cameraKey>>8;re3wound::pendingHits.clear();
    }
    re3wound::pendingHits.erase(std::remove_if(re3wound::pendingHits.begin(),re3wound::pendingHits.end(),[&](const re3wound::PendingHit& h){return serial-h.serial>30;}),re3wound::pendingHits.end());
    if(lastBuildTime&&previousMode&&delta>0&&delta<1000){
        if(modelPreparedActorMask==3){++dualSamples;dualTime+=delta;dualMax=(std::max)(dualMax,delta);if(dualSamples==300){log("MODEL_ACTOR_RATE actors=2 mode=remastered samples=%u elapsed_ms=%.3f fps=%.3f max_frame_ms=%.3f",dualSamples,dualTime,300000.0/dualTime,dualMax);dualSamples=0;dualTime=dualMax=0;}}
        else if(modelPreparedActorMask==1){++soloSamples;soloTime+=delta;soloMax=(std::max)(soloMax,delta);if(soloSamples==300){log("MODEL_ACTOR_RATE actors=1 mode=remastered samples=%u elapsed_ms=%.3f fps=%.3f max_frame_ms=%.3f",soloSamples,soloTime,300000.0/soloTime,soloMax);soloSamples=0;soloTime=soloMax=0;}}
        // frames with characters from actors.txt, grouped by the number of HD actors (6 = six or more)
        if(modelPreparedActorMask&4){
            static unsigned samples[7]{};static double time[7]{},longest[7]{},prepare[7]{},draw[7]{};static unsigned jobs[7]{};const unsigned n=(std::min)(modelPreparedActors,6u);
            ++samples[n];time[n]+=delta;longest[n]=(std::max)(longest[n],delta);prepare[n]+=modelFramePrepareMs;draw[n]+=modelFrameDrawMs;jobs[n]+=modelFrameJobs;
            if(samples[n]==300){log("MODEL_ACTOR_RATE characters actors=%u%s mode=remastered samples=300 elapsed_ms=%.3f fps=%.3f max_frame_ms=%.3f prepare_ms=%.3f draw_ms=%.3f slices=%.1f state=%s",n,n==6?"+":"",time[n],300000.0/time[n],longest[n],
                prepare[n]/300,draw[n]/300,jobs[n]/300.0,modelRenderer.lightStateRestore?"light":"full");samples[n]=0;time[n]=longest[n]=prepare[n]=draw[n]=0;jobs[n]=0;}
        }
    }
    lastBuildTime=entryTime;modelPreparedActorMask=0;modelPreparedActors=0;modelFramePrepareMs=modelFrameDrawMs=0;modelFrameJobs=0;modelFrameDeferred=0;
    // deferred frames whose jobs were never drawn: dropped from the queue (prepared alone if one is drawn after all)
    if(pendingFrames.size()>96){for(auto& pf:pendingFrames)pf->queued=false;pendingFrames.clear();}
    if(modelStateRestoreRequest>=0){modelRenderer.lightStateRestore=modelStateRestoreRequest!=0;modelStateRestoreRequest=-1;log("MODEL_STATE_RESTORE %s",modelRenderer.lightStateRestore?"light":"full");}
    pollToggle();modelDraws=0;
    previousMode=desiredEnabled;modelAutoCapture=GetPrivateProfileIntA("pilot","auto_capture",1,(root+"\\pilot.ini").c_str())!=0;
    diagnosticActorControl();
    if(modelReloadRequested){modelReloadRequested=false;modelAssets.clear();modelObserved.clear();modelFatal=false;log("MODEL_RELOAD next-build assets refreshed; queued GPU payloads retained");}
    if(!modelLoadRequest.empty()&&gameDevice){const bool ready=getModel(modelLoadRequest)!=nullptr;log("MODEL_LOAD_TEST %s ready=%u",modelLoadRequest.c_str(),ready);modelLoadRequest.clear();}
    if(gameDevice&&modelRenderer.resetDevice(gameDevice))modelRenderer.beginFrame(serial);
    if(serial%300==0&&modelRenderer.depthResets){log("MODEL_DEPTH_RESETS builds=300 resets=%llu (one per frame when every model of an OT shares its build epoch)",modelRenderer.depthResets);modelRenderer.depthResets=0;}
    if(serial%300==0){const auto stats=re3bridge::GetStats();log("MODEL_STATS build=%llu prepared=%llu copied=%llu drawn=%llu failed=%llu invalid=%llu capacity=%llu sourceRetired=%llu queueResets=%llu framesRetired=%llu nearCut=%llu emptyNearCut=%llu affineLbs=%llu frameBuilds=%llu zoneOrphans=%llu stalePayloads=%llu",stats.builds,stats.actorsPrepared,stats.packetsCopied,stats.jobsDrawn,stats.drawFailures,stats.invalidPackets,stats.capacityFallbacks,stats.sourceSlotsRetired,stats.queueResets,stats.framesRetired,stats.nativeNearCutJobs,stats.emptyAfterNativeNearCut,modelAffineFallbacks,stats.frameBuilds,stats.zoneSlotsOrphaned,stats.stalePayloads);
        // address space of the 2 GB process: large replacement images need free blocks (texture_strips.h)
        size_t freeBytes,largest,assetBytes=0,textureBytes=0;addressSpace(freeBytes,largest);for(const auto& a:modelAssets){assetBytes+=a.second->bytes;textureBytes+=a.second->textureBytes;}
        PROCESS_MEMORY_COUNTERS_EX pm{};pm.cb=sizeof pm;K32GetProcessMemoryInfo(GetCurrentProcess(),(PROCESS_MEMORY_COUNTERS*)&pm,sizeof pm);
        log("PROCESS_VA free_mb=%zu largest_mb=%zu private_mb=%zu model_assets_mb=%zu model_textures_mb=%zu strip_loads=%u staging_loads=%u",freeBytes>>20,largest>>20,(size_t)pm.PrivateUsage>>20,assetBytes>>20,textureBytes>>20,stripLoads,stagingLoads);}
    static double intervalStart=0;static unsigned intervalCount=0;const double now=modelClockMs();if(!intervalStart)intervalStart=now;else if(++intervalCount==300){log("MODEL_BUILD_RATE samples=300 elapsed_ms=%.3f fps=%.3f",now-intervalStart,300000.0/(now-intervalStart));intervalStart=now;intervalCount=0;}
    return desiredEnabled;
}
// Puzzle/close-up event programs draw room objects and effects in their own frame loop, without the gameplay build
// start: one build per native frame for them (uploads retire, F8 is polled), logged once per episode.
static void ensureFrameBuild(){
    static uint32_t lastFrameBuild=0;
    if(!re3bridge::EnsureFrameBuild())return;
    const auto serial=re3bridge::BuildSerial();
    if(serial-lastFrameBuild>1)log("MODEL_FRAME_BUILD build=%u scene=%s (room objects drawn by an event program loop; one build per native frame)",serial,currentScene.c_str());
    lastFrameBuild=serial;
}
// fps60: the in-between copy of a frame (its InterpData moved halfway), made once per pass; the frame itself when it has
// nothing to move toward or the copy fails.
static const re3model::GpuFrame* fps60Mid(const std::shared_ptr<re3model::GpuFrame>& real){
    if(!real->interp)return real.get();
    auto hit=fps60Mids.find(real.get());if(hit!=fps60Mids.end())return hit->second?hit->second.get():real.get();
    std::shared_ptr<re3model::GpuFrame> mid;const auto& data=*std::static_pointer_cast<re3fps60::InterpData>(real->interp);
    // not moved since the build before (standing, held still): the real frame is the in-between frame
    if(data.previous&&data.current&&data.previous->size()==data.current->size()&&!std::memcmp(data.previous->data(),data.current->data(),data.current->size()*sizeof(re3model::Vec3))){
        ++fps60PassKept;fps60Mids.emplace(real.get(),nullptr);return real.get();}
    {static unsigned logged=0;if(logged<12){++logged;log("FPS60_MID item_build=%u previous_build=%u vertices=%u",data.serial,data.previousSerial,real->vertexCount);}}
    if(data.previous){
        const double started=modelClockMs();std::vector<re3model::DrawVertex> moved=re3model::VertexRecycler::Get().Take(real->cpuVertices.size());
        const bool moved2=re3fps60::Reproject(data,real->cpuVertices,fps60PassWeight,moved);const double reprojected=modelClockMs();fps60PassReprojectMs+=reprojected-started;
        if(moved2){HRESULT hr;mid=modelRenderer.uploadLike(*real,std::move(moved),fps60PassEpoch,&hr);}
        fps60PassUploadMs+=modelClockMs()-reprojected;
        if(mid)++fps60PassMoved;else ++fps60PassFailed;
    }else ++fps60PassKept;
    fps60Mids.emplace(real.get(),mid);
    return mid?mid.get():real.get();
}
static bool drawModelJob(void*,void* renderer,uintptr_t token){
    if(fps60Pass){
        auto job=reinterpret_cast<ModelJob*>(token);if(!job||!gameDevice)return false;
        if(job->pending){const double resolving=modelClockMs();const bool any=resolvePendingJob(*job);fps60PassResolveMs+=modelClockMs()-resolving;if(!any)return true;}
        if(!job->geometry)return false;
        const auto* shown=fps60Mid(job->geometry);
        ModelCostTimer drawCost(fps60PassDrawMs);
        flushGame(renderer);auto dd=gogWrapper;if(!dd)return false;
        auto critical=reinterpret_cast<CRITICAL_SECTION*>(dd+0x181fb0);EnterCriticalSection(critical);((void(__cdecl*)())(dd+0x45b30))();LeaveCriticalSection(critical);
        bool ownScene=!*(uint32_t*)(dd+0x181fe4);if(ownScene&&FAILED(gameDevice->BeginScene()))return false;
        auto hr=modelRenderer.drawSlice(*shown,job->slice,job->options);
        if(ownScene)gameDevice->EndScene();
        return SUCCEEDED(hr)||re3model::IsTransientDrawStatus(hr);
    }
    ModelCostTimer cost(modelFrameDrawMs);++modelFrameJobs;
    auto job=reinterpret_cast<ModelJob*>(token);if(!job||!gameDevice)return false;
    if(job->pending&&!resolvePendingJob(*job))return true;   // deferred: empty bucket (or its frame failed, logged once)
    if(!job->geometry)return false;
    flushGame(renderer);auto dd=gogWrapper;if(!dd)return false;
    auto critical=reinterpret_cast<CRITICAL_SECTION*>(dd+0x181fb0);EnterCriticalSection(critical);((void(__cdecl*)())(dd+0x45b30))();LeaveCriticalSection(critical);
    bool ownScene=!*(uint32_t*)(dd+0x181fe4);if(ownScene&&FAILED(gameDevice->BeginScene()))return false;
    auto hr=modelRenderer.drawSlice(*job->geometry,job->slice,job->options);
    if(ownScene)gameDevice->EndScene();
    if(re3model::IsTransientDrawStatus(hr)){if(modelObserved.insert("transient-device").second)log("MODEL_DEVICE_WAIT hr=%08lx",hr);return true;}
    if(FAILED(hr)){log("MODEL_DRAW_FAILED %s hr=%08lx",job->name.c_str(),hr);modelFatal=true;return false;}
    modelDraws++;
    if(job->name=="jill-standard")modelRenderedActorMask|=1;else if(job->name=="dario")modelRenderedActorMask|=2;
    if((modelAutoCapture||job->captureTag.compare(0,4,"SEQ-")==0)&&!job->captureTag.empty()&&GetPrivateProfileIntA("pilot","debug_warp",0,(root+"\\pilot.ini").c_str())){captureTag=job->captureTag;captureRequested=true;}
    if(modelObserved.insert("drawn:"+job->name).second)log("MODEL_DRAWN %s epoch=%llu slices=%zu",job->name.c_str(),job->geometry->epoch,job->geometry->slices.size());
    if(modelAutoCapture&&job->name.compare(0,7,"casing-")==0&&GetPrivateProfileIntA("pilot","debug_warp",0,(root+"\\pilot.ini").c_str())){static std::unordered_map<std::string,unsigned> samples;auto n=++samples[job->name];if(n==1||n==6||n==12){captureRequested=true;captureTag=job->name+"-"+std::to_string(n);}}
    if(job->name=="dario"&&job->slice==0&&GetPrivateProfileIntA("pilot","debug_warp",0,(root+"\\pilot.ini").c_str())){static unsigned samples=0;auto n=++samples;if(modelAutoCapture&&(n==10||n==60||n==120)){captureRequested=true;captureTag="DARIO-"+std::to_string(n);}if(n==10&&diagnosticFreezeDario){diagnosticFreezeDario=false;setDiagnosticPause(true);}}
    return true;
}
static size_t releaseModelDeviceResources(ULONG* deviceReferences){
    if(deviceReferences)*deviceReferences=modelRenderer.deviceReferences();
    const size_t assets=modelAssets.size();
    modelRenderer.releaseDeviceResources();modelAssets.clear();modelObserved.clear();modelFatal=false;
    return assets;
}
static void beforeModelDeviceReset(){modelRenderer.beforeReset();}
static void afterModelDeviceReset(HRESULT result){modelRenderer.afterReset(result);if(SUCCEEDED(result)){modelFatal=false;modelObserved.erase("transient-device");}}
// v12 wounds: the enemy shot test 452DB0 (hand matrix, per-model weapon table, weapon) walks the enemy list (A5CDCC up
// to the end pointer at A5CE6C: the actor loop's own actor pointers past the player and partner) and writes a hit into
// the enemy it picks: life +0xCC lowered by the weapon damage, routine +4 = damage (low byte 2), weapon +0x12C. Its
// gun callers (player weapon routines, Carlos/NPC shots) are wrapped and the enemies compared before and after.
// (4553A0, wrapped in the first v12, is the test for shootable objects such as barrels: it never sees a zombie.)
using ShotTest=int(__cdecl*)(const re3bridge::Matrix32*,const void*,int);
static ShotTest originalShotTest=nullptr;
static unsigned shotTestsLogged=0;
static int __cdecl shotTestHook(const re3bridge::Matrix32* hand,const void* table,int weapon){
    std::lock_guard<std::recursive_mutex> guard(mutex);
    struct Enemy{uint8_t* actor;int16_t life;uint32_t routine;};
    Enemy before[40];unsigned count=0;
    try{
        const auto end=re3bridge::detail::Read<uintptr_t>(reinterpret_cast<void*>(gameBase+0x65ce6c));
        for(uintptr_t at=gameBase+0x65cdcc;at<end&&count<40;at+=4){
            auto* actor=re3bridge::detail::Read<uint8_t*>(reinterpret_cast<void*>(at));
            if(re3bridge::detail::Accessible(actor,0x134))before[count++]={actor,re3bridge::detail::Read<int16_t>(actor,0xcc),re3bridge::detail::Read<uint32_t>(actor,4)};
        }
    }catch(...){count=0;}
    // Glock 18C AUTO: the M92F damage table with the M4A1 AUTO hit cooldown (glock18c.h)
    const int result=originalShotTest(hand,glockCombatFor(table,weapon),weapon);
    try{
        unsigned hits=0;
        for(unsigned i=0;i<count;++i){
            const auto life=re3bridge::detail::Read<int16_t>(before[i].actor,0xcc);const auto routine=re3bridge::detail::Read<uint32_t>(before[i].actor,4);
            if(life>=before[i].life&&(routine==before[i].routine||(routine&0xff)!=2))continue;
            ++hits;glockShotDone(weapon,before[i].actor,before[i].life);
            // the knife (weapon 1) runs the same test with its own short range: no bullet wound
            if(weapon==1||!hand||!re3bridge::detail::Accessible(hand,sizeof(*hand)))continue;
            re3wound::PendingHit hit;hit.actor=before[i].actor;hit.weapon=uint8_t(weapon);hit.serial=re3bridge::BuildSerial();hit.hand=re3model::FromNativeMatrix(*hand);
            if(re3wound::pendingHits.size()<32)re3wound::pendingHits.push_back(hit);
            if(modelObserved.insert("shot-hit:"+std::to_string(weapon)).second)log("MODEL_SHOT_HIT weapon=%02x actor=%p life=%d->%d routine=%08x",unsigned(weapon),hit.actor,before[i].life,life,routine);
        }
        if(!hits)glockShotDone(weapon,nullptr,0);
        if(shotTestsLogged<16){++shotTestsLogged;log("MODEL_SHOT_TEST weapon=%02x enemies=%u result=%d hits=%u",unsigned(weapon),count,result,hits);}
    }catch(...){log("MODEL_SHOT_HIT bookkeeping failed; native shot retained");}
    return result;
}
static void installShotHooks(){
    // player weapon routines (table 52A3B0) and the two NPC gun routines that aim with their hand matrix
    static const uintptr_t sites[]={0x64075,0x65e6e,0x65fa1,0x662ef,0x663f1,0x66e52,0x66fef,0x671de,0x6752b,0x675df,0x678a4,0x678c0,0x67c0b,0x67c27,0x67d4f,0x67ee1,0x68282,
        0xe345e,0xeb99d};
    constexpr size_t count=sizeof(sites)/sizeof(sites[0]);
    re3bridge::detail::Patch patches[count];
    for(size_t i=0;i<count;++i)if(!re3bridge::detail::BuildCallPatch(patches[i],gameBase+sites[i],gameBase+0x52db0,reinterpret_cast<void*>(shotTestHook))){log("MODEL_SHOT_HOOKS_SKIPPED call %zu signature mismatch; no wounds",i);return;}
    originalShotTest=reinterpret_cast<ShotTest>(gameBase+0x52db0);
    size_t done=0;for(;done<count;++done)if(!re3bridge::detail::WritePatch(patches[done],true))break;
    if(done!=count){while(done)re3bridge::detail::WritePatch(patches[--done],false);log("MODEL_SHOT_HOOKS_FAILED native calls restored");return;}
    log("MODEL_SHOT_HOOKS_OK calls=%zu (wounds on HD enemies)",count);
}
// Fix 2026-10-24: the gun routines spawn muzzle flash/smoke/sparks and the casing with 43D370(kind, key, hand matrix
// = part 4 +0x40, short offset[3] in the hand frame, extra) at fixed offsets (pistols flash 100,450,0, M4A1
// 190,1300,20, ...), i.e. at the NATIVE muzzle. For a hand drawn with an HD weapon that has a muzzle row in the last
// frames the offset moves by its muzzle shift (muzzle sites) or port shift (casing/ejection sites).
using EffectSpawn=int(__cdecl*)(int,int,const void*,const int16_t*,const void*);
static EffectSpawn originalEffectSpawn=nullptr;
template<bool Port> static int __cdecl effectSpawnHook(int kind,int key,const void* hand,const int16_t* offset,const void* extra){
    int16_t moved[3];const int16_t* use=offset;
    try{
        std::lock_guard<std::recursive_mutex> guard(mutex);
        const auto hit=offset?muzzleHands.find(hand):muzzleHands.end();
        if(hit!=muzzleHands.end()&&re3bridge::BuildSerial()-hit->second.serial<=3&&re3bridge::detail::Accessible(offset,6)){
            const float* s=Port?hit->second.port:hit->second.shift;
            float local[3];for(int k=0;k<3;++k)local[k]=offset[k]+s[k];
            re3model::Matrix3x4 inv;const MuzzleHand& h=hit->second;   // fix 2026-10-25c: lowered arms carry the effect point
            if(h.dropped&&re3model::InverseAffine(h.hand,inv)){const re3model::Vec3 p{h.pivot[0],h.pivot[1],h.pivot[2]};
                const re3model::Vec3 w=re3model::TransformPoint(h.hand,{local[0],local[1],local[2]}),back=re3model::TransformPoint(inv,p+re3grip::Apply(h.drop,w-p));
                local[0]=back.x;local[1]=back.y;local[2]=back.z;}
            for(int k=0;k<3;++k)moved[k]=int16_t((std::max)(-32768L,(std::min)(32767L,std::lround(local[k]))));
            use=moved;
            if(modelObserved.insert(std::string(Port?"port-shift:":"muzzle-shift:")+std::to_string(kind)).second)log("MODEL_MUZZLE_SHIFT %s kind=%x offset=%d,%d,%d -> %d,%d,%d",Port?"port":"muzzle",unsigned(kind),offset[0],offset[1],offset[2],moved[0],moved[1],moved[2]);
        }
    }catch(...){use=offset;}
    return originalEffectSpawn(kind,key,hand,use,extra);
}
static void installMuzzleHooks(){
    // static table: qa/fix-2026-10-24/weapons/work/spawn_table.py (player routines 52A3B0 for 02-05/0A/0B/0D-0F/11-13, NPC
    // 4E3430/4E3530/4EB830/4EBA20); port = casings, ejection/side smoke, magnum cylinder flash, rocket back-blast
    // fix 2026-10-25b: grenade launcher 06-09 (4666C0..466C40: flash 150,1300,0 and smoke 150,1100,0; the shell itself
    // and the break-open smoke stay native) and M37 10 (467860: flash, sparks, pellets; casing and receiver smoke = port)
    static const uintptr_t muzzle[]={0x65f1e,0x65f45, 0x66007,0x66048,0x6606e,0x66097,0x660c8,0x660fb,0x66182,0x661f1,0x66257,0x662bf,
        0x66457,0x66498,0x664e7,0x66572,0x665e0,0x66648,0x666b0, 0x67290,0x672bf, 0x67337,0x67377,0x673b9,0x673fd,0x67444,0x674bd,
        0x67643,0x67686,0x676c8,0x6770c,0x67750,0x677fa, 0x67dfa,0x67e36,0x67e5a,0x67e80,
        0x67f47,0x67f88,0x67fb1,0x67fd7,0x68000,0x6802c,0x6805c,0x6808d,0x68113,0x68181,0x681e9,0x68251,
        0xe34e6,0xe3516,0xe3587,0xe35c5,0xe360a,0xe364c,0xe3691,0xe3713, 0xeb945,0xeb974,0xebab1,0xebaf1,0xebb33,0xebb7a,0xebbbe,0xebc4e,
        0x66d0e,0x66d37,0x66d5b, 0x66eac,0x66eea,0x66fc7,
        0x66760,0x66789,0x668c0,0x668e9,0x66a20,0x66a49,0x66b80,0x66ba9, 0x67929,0x6796c,0x679bd,0x679ef,0x67a23,0x67aac,0x67b1d,0x67b85,0x67bef};
    static const uintptr_t port[]={0x65eed,0x67dcc,0x6725a,0x67481,0x674fd,0x67790,0x6783a,0xe34b5,0xe36d5,0xe374f,0xeb917,0xebbf8,0xebc8e,
        0x6635b,0x66381,0x682eb,0x68311,0x664be, 0x66d82,0x66dba, 0x66f27,0x66f62, 0x67993,0x67c96,0x67cbb};
    constexpr size_t nm=sizeof(muzzle)/sizeof(muzzle[0]),count=nm+sizeof(port)/sizeof(port[0]);
    re3bridge::detail::Patch patches[count];
    for(size_t i=0;i<count;++i)if(!re3bridge::detail::BuildCallPatch(patches[i],gameBase+(i<nm?muzzle[i]:port[i-nm]),gameBase+0x3d370,reinterpret_cast<void*>(i<nm?&effectSpawnHook<false>:&effectSpawnHook<true>))){log("MODEL_MUZZLE_HOOKS_SKIPPED call %zu signature mismatch; native muzzle effects",i);return;}
    originalEffectSpawn=reinterpret_cast<EffectSpawn>(gameBase+0x3d370);
    size_t done=0;for(;done<count;++done)if(!re3bridge::detail::WritePatch(patches[done],true))break;
    if(done!=count){while(done)re3bridge::detail::WritePatch(patches[--done],false);log("MODEL_MUZZLE_HOOKS_FAILED native calls restored");return;}
    log("MODEL_MUZZLE_HOOKS_OK calls=%zu (muzzle %zu, port %zu)",count,nm,count-nm);
}
static void installModels(){
    modelActive=GetPrivateProfileIntA("pilot","models",0,(root+"\\pilot.ini").c_str())!=0;
    modelParity=GetPrivateProfileIntA("pilot","model_parity",0,(root+"\\pilot.ini").c_str())!=0;
    modelJillAllRooms=GetPrivateProfileIntA("pilot","jill_all_rooms",0,(root+"\\pilot.ini").c_str())!=0;
    modelDeferPrepare=GetPrivateProfileIntA("pilot","defer_prepare",1,(root+"\\pilot.ini").c_str())!=0;
    modelFaceEnabled=GetPrivateProfileIntA("pilot","face_animation",1,(root+"\\pilot.ini").c_str())!=0;
    fps60Enabled=GetPrivateProfileIntA("pilot","fps60",1,(root+"\\pilot.ini").c_str())!=0;
    modelMotionSmoothing=GetPrivateProfileIntA("pilot","motion_smoothing",1,(root+"\\pilot.ini").c_str())!=0;
    modelCharactersAllRooms=GetPrivateProfileIntA("pilot","characters_all_rooms",1,(root+"\\pilot.ini").c_str())!=0;
    if(!modelActive)return;modelRenderer.setLogger(modelMessage);
    if(GetPrivateProfileIntA("pilot","characters",1,(root+"\\pilot.ini").c_str())){loadActorRules();loadLooks();}
    loadWeapons();
    {unsigned bad=0;if(re3handik::Load(root+"/model-pack/hand-ik.txt",modelHandIk,bad)||bad)log("MODEL_HAND_IK_TABLE rows=%zu invalid=%u",modelHandIk.size(),bad);}
    if(modelFaceEnabled){
        std::string lipError;
        if(!faceLipSync.Load(root+"/model-pack/lipsync.r3lip",&lipError))log("FACE_LIPSYNC_UNAVAILABLE %s",lipError.c_str());
        installVoiceHooks();
    }
    re3bridge::Callbacks callbacks;callbacks.beginBuild=beginModelBuild;callbacks.prepareActor=prepareModelActor;callbacks.drawJob=drawModelJob;callbacks.message=modelMessage;
    if(!re3bridge::Install(gameBase,callbacks,reinterpret_cast<void*>(frameHook))){modelActive=false;log("MODEL_INSTALL_FAILED keeping original geometry");}
    else{if(!modelParity&&GetPrivateProfileIntA("pilot","wounds",1,(root+"\\pilot.ini").c_str()))installShotHooks();if(!modelParity&&!modelWeaponFits.empty())installMuzzleHooks();log("MODEL_INSTALL_OK parity=%u jillAllRooms=%u faceAnimation=%u motionSmoothing=%u characters=%zu charactersAllRooms=%u",modelParity,modelJillAllRooms,modelFaceEnabled,modelMotionSmoothing,modelActorRules.size(),modelCharactersAllRooms);}
}
