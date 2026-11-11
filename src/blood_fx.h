#pragma once
// ---------------------------------------------------------------- HD blood (round 2026-11-07)
// Included at the end of extra_model_integration.h, inside namespace re3extra (read/accessible/active/once/mutex,
// getModel, modelRenderer, the native OT helpers of the resting cases).
//
// User request 2026-11-07: better blood pools under killed enemies, blood sprays on hits and more visible wounds, the 2D
// art from the OpenAI Images API (gpt-image-2.6-sunburst, models/blood-fx/, pack model-pack/blood-fx). Everything is drawn
// into the native ordering tables after the effect loop, like the resting cases:
//  - pools: the native pool is the actor's SHADOW (actor+0x160): +0/+2 offset, +4/+6 size, +8/+A/+C world x/y/z (int16),
//    +E flags (4 = draw this frame), +10 angle, +14 -> actor position, two FT4 packets at +18/+40 (colour words +1C/+44,
//    tpage D9 = subtractive). The enemy death routines (e.g. 49992F) turn the colour into 0x808010 (subtracting mostly
//    G and B leaves a dark red stain) and grow +4/+6 by 8 a frame for 90 frames; 425540 ('g' = the German build) skips
//    both. The shadow update 457130 (one call, 42BB18) is wrapped: a blood-red shadow keeps its native update but loses
//    flag 4, and an HD pool spreads at its place instead (reach map, model_shader.h GrowthMask).
//  - sprays: every hit that made a wound (re3wound::shotBlood) gets a radial puff at the entry, a cone along the shot at
//    the exit wound (or a short one out of the entry toward the shooter) and droplets that fly with gravity and leave
//    splats on the floor. The native hit blood (effect key 0C00, class 2B, semi-transparent FT4 billboards, a soft red
//    cloud in HD) is always collapsed to nothing; one with no HD hit near it gets an HD replacement spray (v2, 07.11).
//  - splats: floor decals where droplets land, kept for the room visit.
// pilot.ini blood_fx=0 turns all of it off (native shadows/pools and native hit blood come back).
namespace blood {
using re3model::Vec3;
constexpr unsigned kPools=4,kSplatMaterial=4,kConeMaterial=5,kPuffMaterial=6,kDropMaterial=7,kMaterials=8;
constexpr std::size_t kMaxSplats=160,kMaxDrops=220,kMaxBursts=40,kMaxPools=24;
static bool enabled=true;            // pilot.ini blood_fx
static unsigned debugSlow=1;         // pilot.ini blood_debug_slow (QA captures): sprays and droplets N times slower
static float poolOpacity=0.8f;        // release 1.50: 1 - pilot.ini blood_pool_transparency (0..80 %, default 20) / F5 menu: the HD pools (model_shader.h growthOpacity) and the floor stains
static float poolBrightness=1.f;      // fix 2026-11-11: pilot.ini blood_pool_brightness 40..160 % / F5 menu (red pools and floor stains; fx 2026-11-11e: the sprays and drops of hits too, the wound decals through re3wound::bloodBrightness)
static int debugPoolBucket=-1;       // pilot.ini blood_debug_pool_bucket (QA): pools in this one OT bucket
static const std::uint8_t debugShadowKey[4]{};
static bool shadowHooked=false;
struct Config{float coneAnchor[2]{0.02f,0.53f};unsigned coneFrames=14,puffFrames=11;float dropAngle[16]{},dropElong[16]{},dropSize[16]{};bool loaded=false;};
static Config config;
struct Pool{const void* shadow=nullptr;const void* actor=nullptr;Vec3 centre{};float size=0,angle=0,w0=0;std::uint8_t variant=0;std::uint32_t born=0,seen=0,colour=0xff808080u;bool logged=false;};
struct Splat{Vec3 pos{};float size=0,angle=0;std::uint8_t cell=0;std::uint32_t colour=0xff808080u;};
struct Burst{Vec3 pos{},dir{};float size=0,roll=0;std::uint8_t material=kPuffMaterial;std::uint32_t born=0,colour=0xff808080u;bool directed=false;};
struct Drop{Vec3 pos{},vel{};float size=0,floorY=0;std::uint8_t cell=0;std::uint32_t born=0,colour=0xff808080u;};
static std::vector<Pool> pools;
static std::deque<Splat> splats;
static std::vector<Burst> bursts;
static std::vector<Drop> drops;
static std::uint32_t bloodRoom=~0u,tick=0,seedState=0x9e3779b9u;
static std::uint64_t poolsStarted=0,burstsSpawned=0,nativeSprayHidden=0,splatsMade=0;
inline float rnd(){seedState=seedState*1664525u+1013904223u;return float((seedState>>8)&0xffff)/65535.f;}
inline float rnd(float a,float b){return a+(b-a)*rnd();}

inline void loadConfig(){
    if(config.loaded)return;config.loaded=true;
    std::ifstream file(root+"/model-pack/blood-fx/blood.txt");std::string line;
    for(unsigned k=0;k<16;++k){config.dropElong[k]=1;config.dropSize[k]=0.5f;}
    while(std::getline(file,line)){std::istringstream in(line);std::string key;if(!(in>>key)||key[0]=='#')continue;
        if(key=="cone_anchor")in>>config.coneAnchor[0]>>config.coneAnchor[1];
        else if(key=="cone_frames")in>>config.coneFrames;
        else if(key=="puff_frames")in>>config.puffFrames;
        else if(key=="drop"){unsigned k=0;float a=0,e=1,s=0.5f;if(in>>k>>a>>e>>s&&k<16){config.dropAngle[k]=a;config.dropElong[k]=e;config.dropSize[k]=s;}}}
    config.coneFrames=(std::max)(1u,(std::min)(16u,config.coneFrames));config.puffFrames=(std::max)(1u,(std::min)(16u,config.puffFrames));
}
inline std::shared_ptr<ModelAsset> pack(){
    auto asset=getModel("blood-fx");
    if(!asset||asset->mesh.materials.size()!=kMaterials||asset->textures.size()!=kMaterials){once("blood-fx-pack","HD blood pack unavailable (model-pack/blood-fx, 8 materials): native blood kept");return nullptr;}
    for(unsigned k=0;k<kPools;++k)if(!asset->textures[k].stp){once("blood-fx-reach","HD blood pools need their reach maps: native pools kept");return nullptr;}
    loadConfig();return asset;
}
inline bool ready(){return enabled&&active()&&shadowHooked;}

// ---------------------------------------------------------------- native shadow -> HD pool
inline const void* actorOfShadow(const void* shadow){
    const auto end=read<std::uintptr_t>(reinterpret_cast<void*>(gameBase+0x65ce6c));
    for(std::uintptr_t at=gameBase+0x65cdcc;at<end&&at<gameBase+0x65cdcc+48*4;at+=4){auto* actor=read<const std::uint8_t*>(reinterpret_cast<void*>(at));
        if(accessible(actor,0x164)&&read<const void*>(actor,0x160)==shadow)return actor;}
    const auto* player=reinterpret_cast<const std::uint8_t*>(gameBase+0x65cbf4);
    if(accessible(player,0x164)&&read<const void*>(player,0x160)==shadow)return player;
    return nullptr;
}
inline bool bloodShadow(const std::uint8_t* shadow){return (read<std::uint32_t>(shadow,0x1c)&0xffffffu)==0x808010u;}
inline void poolFromShadow(std::uint8_t* shadow){
    if(!accessible(shadow,0x68,true)||!bloodShadow(shadow))return;
    // a corpse's shadow is no longer updated by 457130 (flag 4 stays as it was last set): a known pool keeps going
    // while the stain stays blood red; a new one needs the stain shown (flag 4) once
    Pool* known=nullptr;for(auto& p:pools)if(p.shadow==shadow){known=&p;break;}
    if(!known&&!(shadow[0xe]&4))return;
    auto asset=pack();if(!asset)return;
    shadow[0xe]=std::uint8_t(shadow[0xe]&~4u);   // the native stain is not drawn; the HD pool is
    const float w=float(read<std::int16_t>(shadow,4)),h=float(read<std::int16_t>(shadow,6));
    const Vec3 centre{float(read<std::int16_t>(shadow,8)),float(read<std::int16_t>(shadow,0xa)),float(read<std::int16_t>(shadow,0xc))};
    Pool* pool=known;
    if(!pool){
        if(pools.size()>=kMaxPools)pools.erase(pools.begin());
        pools.push_back({});pool=&pools.back();pool->shadow=shadow;pool->actor=actorOfShadow(shadow);pool->born=tick;pool->w0=(std::max)(w,h);
        std::uint32_t seed=std::uint32_t(reinterpret_cast<std::uintptr_t>(shadow)*2654435761u)^(bloodRoom*40503u)^tick;
        seed=seed*1664525u+1013904223u;pool->variant=std::uint8_t((seed>>12)%kPools);
        // the body's long axis when the parts can tell (head part 1 from the root part 0), else the shadow's angle
        pool->angle=float(read<std::int16_t>(shadow,0x10))*6.2831853f/4096.f;
        if(pool->actor){const auto parts=read<const std::uint8_t*>(pool->actor,0x108);
            if(accessible(parts,0xbc*2)){const auto m0=read<re3bridge::Matrix32>(parts,0x14),m1=read<re3bridge::Matrix32>(parts+0xbc,0x14);
                const float dx=float(m1.translation[0]-m0.translation[0]),dz=float(m1.translation[2]-m0.translation[2]);if(dx*dx+dz*dz>200.f*200.f)pool->angle=std::atan2(dz,dx);}}
        ++poolsStarted;
    }
    pool->centre=centre;pool->seen=tick;
    // the native stain grows 8 a frame for 90 frames: the HD pool takes the size it will end at (and more if it keeps growing)
    // 1.5x: the native stain is a soft blob that reads ~1.9 m wide around a corpse (diag capture 07.11); the HD pool's
    // outline fills ~85 % of its square, so the same reach needs the larger square
    pool->size=(std::max)((std::max)(w,h),pool->w0+720.f)*1.5f;
    if(pool->actor){const auto found=actorRuntime.find(const_cast<void*>(pool->actor));if(found!=actorRuntime.end())pool->colour=found->second.bloodLightUp;}
    if(!pool->logged){pool->logged=true;log("BLOOD_POOL start actor=%p shadow=%p at=%.0f,%.0f,%.0f size=%.0fx%.0f final=%.0f angle=%.2f variant=%u light=%06x",pool->actor,shadow,centre.x,centre.y,centre.z,w,h,pool->size,pool->angle,unsigned(pool->variant),pool->colour&0xffffffu);}
}
using ShadowUpdate=void(__cdecl*)(void*,int,int,int);
static ShadowUpdate originalShadow=nullptr;
inline void __cdecl shadowHook(void* shadow,int y,int angle,int flags){
    originalShadow(shadow,y,angle,flags);
    std::lock_guard<std::recursive_mutex> guard(mutex);
    // lighting round 2026-11-10: the black blob of an actor with an HD shadow is not drawn (model_integration.h)
    try{if(active()&&accessible(shadow,0x20,true)){auto* s=static_cast<std::uint8_t*>(shadow);if(lightPlainShadow(read<std::uint32_t>(s,0x1c)))lightOnNativeShadow(s,actorOfShadow(s));}}catch(...){}
    if(!enabled||!active())return;
    try{poolFromShadow(static_cast<std::uint8_t*>(shadow));}catch(...){once("blood-pool-exception","pool bookkeeping failed; native shadow kept");}
    try{nemesis_cloud::onShadow(shadow);}catch(...){}   // fx 2026-11-08 nemesis-cloud: the downed Nemesis' purple stain
}

// ---------------------------------------------------------------- hits -> sprays, droplets, splats
inline void spawnFromShots(){
    auto& shots=re3wound::shotBlood;if(shots.empty())return;
    for(const auto& s:shots){
        const bool shotgun=re3wound::isShotgun(s.weapon);const float power=shotgun?1.5f:s.weapon==5?1.35f:1.f;
        const Vec3 dir=re3model::Normalize(s.direction),out=re3model::Normalize(s.normal);
        if(bursts.size()<kMaxBursts){Burst b;b.pos=s.entry+out*12.f;b.size=rnd(540.f,700.f)*power;b.roll=rnd()*6.2831853f;b.material=kPuffMaterial;b.born=tick;b.colour=s.colour;bursts.push_back(b);}
        if(bursts.size()<kMaxBursts){Burst b;b.material=kConeMaterial;b.born=tick;b.colour=s.colour;b.directed=true;
            if(s.hasExit){b.pos=s.exit;b.dir=dir;b.size=rnd(1150.f,1450.f)*power;}
            else{b.pos=s.entry+out*8.f;b.dir=re3model::Normalize(out*0.8f-dir*0.2f+Vec3{0,-0.25f,0});b.size=rnd(550.f,720.f)*power;}
            bursts.push_back(b);}
        // droplets: out of the exit along the shot, out of the entry back toward the shooter; gravity pulls them to the floor
        // automatic fire (M4A1 0E/0F, gatling) hits several times a second: fewer droplets per hit
        const bool automatic=s.weapon==0x0e||s.weapon==0x0f||s.weapon==0x0b;   // fx 2026-11-08: 0x0B = gatling (0x11 was Sigpro E)
        const unsigned count=unsigned((shotgun?9:automatic?2:5)+rnd()*(automatic?3:5));
        for(unsigned k=0;k<count&&drops.size()<kMaxDrops;++k){Drop d;
            const bool fromExit=s.hasExit&&k%3!=0;
            const Vec3 base=fromExit?dir:re3model::Normalize(out*0.85f-dir*0.15f);
            const Vec3 jitter{rnd(-0.55f,0.55f),rnd(-0.75f,0.25f),rnd(-0.55f,0.55f)};
            d.vel=re3model::Normalize(base+jitter)*rnd(34.f,82.f)*(fromExit?1.25f:1.f);
            d.pos=(fromExit?s.exit:s.entry)+d.vel*0.5f;d.size=rnd(22.f,48.f)*(shotgun?1.1f:1.f);d.floorY=s.floorY;
            static const std::uint8_t cells[]={0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15};d.cell=cells[unsigned(rnd()*15.99f)];d.born=tick;d.colour=s.colour;drops.push_back(d);}
        ++burstsSpawned;
    }
    shots.clear();
}
inline void stepDrops(){
    const bool move=tick%debugSlow==0;
    for(auto it=drops.begin();it!=drops.end();){
        // real gravity at the 30 Hz effect loop: 9.8 m/s^2 = 10.9 units/tick^2 (1 unit ~ 1 mm); 07.11 v1 used 3.6, a
        // third of it ("drops fall very slowly", user QA 07.11)
        if(move){it->vel.y+=10.5f;it->vel=it->vel*0.993f;it->pos=it->pos+it->vel;}
        if(it->pos.y>=it->floorY-2.f){
            if(splats.size()>=kMaxSplats)splats.pop_front();
            Splat sp;sp.pos={it->pos.x,it->floorY-2.f,it->pos.z};sp.size=it->size*rnd(2.2f,3.6f);sp.angle=rnd()*6.2831853f;sp.cell=std::uint8_t(rnd()*7.99f);sp.colour=it->colour;splats.push_back(sp);++splatsMade;
            it=drops.erase(it);continue;}
        if((tick-it->born)/debugSlow>75){it=drops.erase(it);continue;}
        ++it;
    }
    bursts.erase(std::remove_if(bursts.begin(),bursts.end(),[](const Burst& b){return (tick-b.born)/debugSlow>=(b.material==kConeMaterial?config.coneFrames:config.puffFrames);}),bursts.end());
    // a pool whose shadow is no longer updated as blood (actor slot reused, revived) is dropped after a few seconds
    for(auto& p:pools)if(p.shadow==debugShadowKey+0||p.shadow==debugShadowKey+1)p.seen=tick;
    pools.erase(std::remove_if(pools.begin(),pools.end(),[](const Pool& p){return tick-p.seen>90;}),pools.end());
}
// HD sprays of the last ticks (where they started): native hit blood near one needs no replacement
struct RecentHit{Vec3 at{};std::uint32_t tick=0;};
static std::vector<RecentHit> recentHits;
inline void noteHit(const Vec3& at){if(recentHits.size()>32)recentHits.erase(recentHits.begin());recentHits.push_back({at,tick});}
// a hit that has the HD spray close to `at`: recent sprays and wounds not yet turned into sprays (shotBlood)
inline bool nearHdHit(const Vec3& at,float radius,std::uint32_t window){
    auto close=[&](float hx,float hz){const float dx=hx-at.x,dz=hz-at.z;return dx*dx+dz*dz<radius*radius;};
    for(const auto& h:recentHits)if(tick-h.tick<window&&close(h.at.x,h.at.z))return true;
    for(const auto& s:re3wound::shotBlood)if(close(s.entry.x,s.entry.z))return true;
    return false;
}
// a queued shot (pendingHits) on an actor with an HD model near `at`: its wound and spray may still come (off screen,
// deferred prepare). A shot on an actor without one stays queued until it expires and never brings a spray.
inline bool pendingHdHit(const Vec3& at,float radius){
    for(const auto& h:re3wound::pendingHits){if(!accessible(h.actor,0x40)||actorRuntime.find(h.actor)==actorRuntime.end())continue;
        const float dx=float(read<std::int32_t>(h.actor,0x34))-at.x,dz=float(read<std::int32_t>(h.actor,0x3c))-at.z;if(dx*dx+dz*dz<radius*radius)return true;}
    return false;
}
// the actor (enemies, then the player) nearest to a point on the floor plane; fix 2026-11-11: not Nemesis' tentacles
// (actors of their own in the air - the stains of a tentacle blow took their y for the floor, see actorFloorY)
inline const std::uint8_t* nearestActor(const Vec3& at){
    const std::uint8_t* best=nullptr;float bestDistance=3000.f*3000.f;
    auto visit=[&](const std::uint8_t* actor){if(!accessible(actor,0x4c)||sfxhits::appendageTarget(read<std::uint8_t>(actor,0x4a)))return;const float dx=float(read<std::int32_t>(actor,0x34))-at.x,dz=float(read<std::int32_t>(actor,0x3c))-at.z;
        if(dx*dx+dz*dz<bestDistance){bestDistance=dx*dx+dz*dz;best=actor;}};
    const auto end=read<std::uintptr_t>(reinterpret_cast<void*>(gameBase+0x65ce6c));
    for(std::uintptr_t a=gameBase+0x65cdcc;a<end&&a<gameBase+0x65cdcc+48*4;a+=4)visit(read<const std::uint8_t*>(reinterpret_cast<void*>(a)));
    visit(reinterpret_cast<const std::uint8_t*>(gameBase+0x65cbf4));
    return best;
}
// Native blood with no HD spray near it (a hit that made no HD wound: an enemy without an HD model, a failed prepare,
// Jill bitten, ...): an HD spray at the native effect's place, out of the hit toward the shooter. One per 700 units
// and 8 ticks: the damage routine starts 2-3 0C00 effects per hit.
struct Replacement{Vec3 at{};std::uint32_t tick=0;};
static std::vector<Replacement> replacements;
static std::uint64_t nativeReplaced=0;
inline void replaceNativeSpray(const Vec3& at){
    for(const auto& r:replacements)if(tick-r.tick<8){const float dx=r.at.x-at.x,dz=r.at.z-at.z;if(dx*dx+dz*dz<700.f*700.f)return;}
    if(replacements.size()>16)replacements.erase(replacements.begin());replacements.push_back({at,tick});
    const auto* player=reinterpret_cast<const std::uint8_t*>(gameBase+0x65cbf4);
    const Vec3 shooter{float(read<std::int32_t>(player,0x34)),float(read<std::int32_t>(player,0x38)),float(read<std::int32_t>(player,0x3c))};
    Vec3 dir{at.x-shooter.x,0,at.z-shooter.z};if(re3model::Dot(dir,dir)<1.f)dir={0,0,1};dir=re3model::Normalize(dir);
    const auto* actor=nearestActor(at);
    re3wound::ShotBlood s;s.actor=nullptr;s.entry=at;s.direction=dir;s.normal=dir*-1.f;s.hasExit=false;s.weapon=0;s.colour=0xffb4aca4u;
    s.floorY=actorFloorY(actor?actor:player);   // fix 2026-11-11: the floor under it, not its own y
    if(actor){const auto found=actorRuntime.find(const_cast<std::uint8_t*>(actor));if(found!=actorRuntime.end())s.colour=found->second.bloodLightView;}
    re3wound::shotBlood.push_back(s);++nativeReplaced;   // turned into the spray by spawnFromShots of this same update
    if(nativeReplaced<=8)log("BLOOD_NATIVE_REPLACED at=%.0f,%.0f,%.0f actor=%p floor=%.0f (native hit blood with no HD wound near it)",at.x,at.y,at.z,actor,s.floorY);
}
// Every native hit-blood billboard (key 0C00, class 2B) is collapsed to a point (all four FT4 corners on the first):
// nothing is drawn, the native effect itself runs on untouched. (07.11 v1 collapsed it only near an HD spray: hits that
// made no HD wound kept the native soft red cloud — user QA 07.11.) Its first frame queues a decision, taken 3+ ticks
// later in update() (the HD wound of the hit arrives a frame or two after the native blood): an HD spray near it ->
// nothing more, else an HD replacement spray.
struct NativeSpray{std::uintptr_t effect=0;std::uint64_t generation=0;std::uint32_t tick=0;};
static std::vector<NativeSpray> nativeSprays;
struct NativeDecision{Vec3 at{};std::uint32_t born=0;};
static std::vector<NativeDecision> nativeDecisions;
inline void decideNativeSprays(){
    for(auto it=nativeDecisions.begin();it!=nativeDecisions.end();){
        const std::uint32_t age=tick-it->born;
        if(nearHdHit(it->at,1500.f,45+age)){it=nativeDecisions.erase(it);continue;}
        if(age<3||(age<12&&pendingHdHit(it->at,1500.f))){++it;continue;}
        replaceNativeSpray(it->at);it=nativeDecisions.erase(it);
    }
}
// The renderer copy filter (native_model_bridge.h FilteredFt4, call 41FAB1): every semi-transparent FT4 on the native
// hit blood's texture (tpage/clut of the 0C00 effects, effect +0x46/+0x44; 001E/7811 in the R10x rooms) is copied
// degenerate while the HD blood is drawn. Fix round 2026-11-07: v2 collapsed the packets right after 4583D0 wrote them,
// but most 0C00 sprites still reached the renderer whole (blood_packet_census: 464 of them in one R10D run) — the cloud
// the user saw. The filter at the copy cannot be missed.
static std::uint32_t bloodTextures[8]={0x001e7811u};static unsigned bloodTextureCount=1;
static bool packReady=false;   // the blood-fx pack loaded (update()): no HD sprays without it, so nothing is hidden
inline void noteBloodTexture(const void* effect){
    const std::uint32_t id=(std::uint32_t(read<std::uint16_t>(effect,0x46))<<16)|read<std::uint16_t>(effect,0x44);
    for(unsigned k=0;k<bloodTextureCount;++k)if(bloodTextures[k]==id)return;
    if(bloodTextureCount<8){bloodTextures[bloodTextureCount++]=id;log("BLOOD_NATIVE_TEXTURE tpage=%04x clut=%04x (native hit blood hidden at the renderer copy)",unsigned(id>>16),unsigned(id&0xffffu));}
}
inline bool nativeBloodPacket(const void* packet){
    if(!enabled||!shadowHooked||!packReady||!active())return false;
    const auto* p=static_cast<const std::uint8_t*>(packet);if((p[7]&0xfe)!=0x2e)return false;
    const std::uint32_t id=(std::uint32_t(read<std::uint16_t>(p,22))<<16)|read<std::uint16_t>(p,14);
    for(unsigned k=0;k<bloodTextureCount;++k)if(bloodTextures[k]==id)return true;
    return false;
}
static bool copyFilter=false;   // installed (else the packets are collapsed where 4583D0 / 458750 wrote them)
static unsigned collapseDebugLines=0;
inline void collapseNativeSpray(void* packets,const void* effect,int count){
    if(!accessible(effect,0xd4)||read<std::uint16_t>(effect,0x28)!=0x0c00u||read<std::uint8_t>(effect)!=0x2b)return;
    if(nemesis_cloud::owns(effect))return;   // fx 2026-11-08 nemesis-cloud: Nemesis' purple sprites, not blood
    auto* p=static_cast<std::uint8_t*>(packets);
    if(effectTrace&&collapseDebugLines<400){++collapseDebugLines;log("BLOOD_COLLAPSE_DBG tick=%u effect=%p packets=%p count=%d ready=%d active=%d writable=%d pack=%d code=%02x xy0=%08x xy1=%08x",tick,effect,packets,count,int(ready()),int(active()),int(accessible(p,40*(count>0?count:1),true)),int(bool(pack())),p[7],read<std::uint32_t>(p,8),read<std::uint32_t>(p,16));}
    if(!ready()||count<1||count>16)return;
    if(!accessible(p,40*count,true)||!pack())return;
    noteBloodTexture(effect);
    // without the copy filter: the packets just written (the emitter writes nothing for an effect off screen, so only
    // packets carrying this effect's own texture are touched)
    if(!copyFilter)for(int k=0;k<count;++k){auto* q=p+40*k;if((q[7]&0xfc)!=0x2c)continue;
        if(read<std::uint16_t>(q,14)!=read<std::uint16_t>(effect,0x44)||read<std::uint16_t>(q,22)!=read<std::uint16_t>(effect,0x46))continue;
        const auto xy0=read<std::uint32_t>(q,8);std::memcpy(q+16,&xy0,4);std::memcpy(q+24,&xy0,4);std::memcpy(q+32,&xy0,4);++nativeSprayHidden;}
    const auto address=reinterpret_cast<std::uintptr_t>(effect);
    const auto generationAt=effectGeneration.find(address);const std::uint64_t generation=generationAt==effectGeneration.end()?0:generationAt->second;
    for(auto& n:nativeSprays)if(n.effect==address&&n.generation==generation&&tick-n.tick<90){n.tick=tick;return;}
    nativeSprays.erase(std::remove_if(nativeSprays.begin(),nativeSprays.end(),[&](const NativeSpray& n){return n.effect==address||tick-n.tick>=90;}),nativeSprays.end());
    nativeSprays.push_back({address,generation,tick});
    const Vec3 at{float(read<std::int32_t>(effect,0x48)),float(read<std::int32_t>(effect,0x4c)),float(read<std::int32_t>(effect,0x50))};
    if(nativeDecisions.size()<32)nativeDecisions.push_back({at,tick});
}

// ---------------------------------------------------------------- drawing
struct Item{Vec3 anchor{};std::vector<re3model::SkinnedVertex> vertices;std::vector<std::uint32_t> indices;std::vector<std::uint32_t> tint;unsigned material=0;int bucket=-1;};
inline void quad(Item& item,const Vec3 corner[4],const float uv[8],std::uint32_t argb){
    const auto base=std::uint32_t(item.vertices.size());
    for(unsigned k=0;k<4;++k){item.vertices.push_back({corner[k],{0,-1,0},{uv[k*2],uv[k*2+1]}});item.tint.push_back(argb);}
    for(const unsigned i:{0u,1u,2u,0u,2u,3u})item.indices.push_back(base+i);
}
// a camera-facing sprite: texture +U along `angle` on screen (0 = right), anchor (ax, ay) of the cell at `at`
inline void sprite(Item& item,const Vec3& right,const Vec3& down,const Vec3& at,float width,float height,float angle,float ax,float ay,const float cell[4],std::uint32_t argb){
    const float c=std::cos(angle),s=std::sin(angle);
    const Vec3 u=right*c+down*s,v=down*c-right*s;
    Vec3 corner[4];float uv[8];const float tx[4]={0,1,1,0},ty[4]={0,0,1,1};
    for(unsigned k=0;k<4;++k){corner[k]=at+u*(width*(tx[k]-ax))+v*(height*(ty[k]-ay));uv[k*2]=cell[0]+(cell[2]-cell[0])*tx[k];uv[k*2+1]=cell[1]+(cell[3]-cell[1])*ty[k];}
    quad(item,corner,uv,argb);
}
// at least `low` (0..255) in every channel, keeping the hue of the light
inline std::uint32_t lifted(std::uint32_t argb,unsigned low){std::uint32_t out=argb&0xff000000u;for(unsigned s=0;s<24;s+=8){const unsigned c=(argb>>s)&255u;out|=std::uint32_t((std::max)(c,low))<<s;}return out;}
inline std::uint32_t scaled(std::uint32_t argb,float k){std::uint32_t out=argb&0xff000000u;for(unsigned s=0;s<24;s+=8)out|=std::uint32_t((std::min)(255.f,float((argb>>s)&255u)*k+.5f))<<s;return out;}
// pools and floor splats: 0.7 of the native light on the floor, at least 0x5c (07.11 v1: at least 0x90 on a lifted red
// texture read as bright cherry red — "too bright", user QA 07.11)
inline std::uint32_t poolTint(std::uint32_t argb){return lifted(scaled(argb,0.7f*poolBrightness),unsigned(92.f*poolBrightness+.5f));}   // fix 2026-11-11: x the user's brightness
// fx 2026-11-11e: sprays and flying drops - the light lifted as before, then x the user's brightness
inline std::uint32_t sprayTint(std::uint32_t argb,unsigned low){return scaled(lifted(argb,low),poolBrightness);}
inline std::uint32_t withAlpha(std::uint32_t argb,float alpha){return (argb&0xffffffu)|(std::uint32_t((std::min)(1.f,(std::max)(0.f,alpha))*255.f+.5f)<<24);}
inline void gridCell(unsigned index,unsigned cols,unsigned rows,float out[4]){const unsigned c=index%cols,r=index/cols;
    // half a texel of the 2048/1024 sheets in so neighbours never bleed under bilinear filtering
    const float eu=0.5f/2048.f,ev=0.5f/2048.f;out[0]=float(c)/cols+eu;out[1]=float(r)/rows+ev;out[2]=float(c+1)/cols-eu;out[3]=float(r+1)/rows-ev;}
inline float screenAngle(const Vec3& right,const Vec3& down,const Vec3& dir,bool& valid){const float x=re3model::Dot(right,dir),y=re3model::Dot(down,dir);valid=x*x+y*y>0.04f;return std::atan2(y,x);}
inline std::uint16_t fixedBucket(void* context,std::uint16_t,std::uint16_t,std::uint16_t){return *static_cast<const std::uint16_t*>(context);}

inline void drawBlood(){
    if(!ready())return;
    if(pools.empty()&&splats.empty()&&bursts.empty()&&drops.empty())return;
    auto asset=pack();if(!asset||!modelRenderer.resetDevice(gameDevice))return;
    const auto gte=read<const void*>(reinterpret_cast<void*>(gameBase+0x1349c8));if(!accessible(gte,0x100))return;
    re3bridge::ActorContext context;context.camera=read<re3bridge::Matrix32>(reinterpret_cast<void*>(gameBase+0x11c398));
    std::memcpy(context.gte.data(),gte,context.gte.size());
    auto base=actorProjection(context);
    if(!re3model::SetNativeProjectionRegisters(base,context.gte.data(),context.gte.size()))return;
    base.shade=nullptr;base.shadeContext=nullptr;
    const auto& cam=base.camera;
    const Vec3 right=re3model::Normalize({cam.m[0],cam.m[1],cam.m[2]}),down=re3model::Normalize({cam.m[4],cam.m[5],cam.m[6]}),forward=re3model::Normalize({cam.m[8],cam.m[9],cam.m[10]});
    std::vector<Item> items;
    // pools: a 4x4 grid on the floor (the floor of a lying body sits ~3 units under the stain's height), reach-map growth
    for(const auto& p:pools){
        const float t=(std::min)(1.f,float(tick-p.born)/165.f),progress=1.f-std::pow(1.f-t,1.7f);
        if(tick-p.born==60||tick-p.born==170){const auto c=re3model::TransformPoint(cam,p.centre);log("BLOOD_POOL_DRAW shadow=%p age=%u progress=%.2f size=%.0f camera_z=%.0f screen=%.0f,%.0f",p.shadow,tick-p.born,progress,p.size,c.z,c.x*base.h/c.z+base.offsetX,c.y*base.h/c.z+base.offsetY);}
        Item item;item.material=p.variant;item.anchor=p.centre;
        const float half=p.size*0.5f,c=std::cos(p.angle),s=std::sin(p.angle);
        const Vec3 ax{c,0,s},az{-s,0,c},origin{p.centre.x,p.centre.y-3.f,p.centre.z};
        const unsigned n=4;const std::uint32_t argb=withAlpha(poolTint(p.colour),progress);
        for(unsigned j=0;j<n;++j)for(unsigned i=0;i<n;++i){Vec3 corner[4];float uv[8];const unsigned ci[4]={i,i+1,i+1,i},cj[4]={j,j,j+1,j+1};
            for(unsigned k=0;k<4;++k){const float u=float(ci[k])/n,v=float(cj[k])/n;corner[k]=origin+ax*((u-0.5f)*2*half)+az*((v-0.5f)*2*half);uv[k*2]=u;uv[k*2+1]=v;}
            quad(item,corner,uv,argb);}
        // one bucket for the whole pool (per-triangle buckets split it around native semi-transparent overlays: a
        // checkerboard of darker cells in the office QA), that of its centre a little farther than the body on it
        {const auto cz=re3model::TransformPoint(cam,p.centre);item.bucket=int((std::max)(1.f,cz.z+120.f))>>5;}
        if(debugPoolBucket>=0)item.bucket=debugPoolBucket;
        item.anchor.y-=1.f;
        items.push_back(std::move(item));
    }
    for(const auto& sp:splats){
        Item item;item.material=kSplatMaterial;item.anchor=sp.pos;float cell[4];gridCell(sp.cell,4,2,cell);
        const float c=std::cos(sp.angle),s=std::sin(sp.angle),half=sp.size*0.5f;const Vec3 ax{c,0,s},az{-s,0,c};
        Vec3 corner[4];float uv[8];const float tx[4]={0,1,1,0},ty[4]={0,0,1,1};
        for(unsigned k=0;k<4;++k){corner[k]=sp.pos+ax*((tx[k]-0.5f)*2*half)+az*((ty[k]-0.5f)*2*half);uv[k*2]=cell[0]+(cell[2]-cell[0])*tx[k];uv[k*2+1]=cell[1]+(cell[3]-cell[1])*ty[k];}
        quad(item,corner,uv,withAlpha(poolTint(sp.colour),0.97f*poolOpacity));
        // fix 2026-11-11: sorted by its far edge (was the centre + 60): whatever stands on the far half is drawn after it
        {const auto cz=re3model::TransformPoint(cam,sp.pos);const float reach=half*(std::fabs(cam.m[8]*ax.x+cam.m[10]*ax.z)+std::fabs(cam.m[8]*az.x+cam.m[10]*az.z));
            item.bucket=int((std::max)(1.f,cz.z+(std::max)(60.f,reach+40.f)))>>5;}
        items.push_back(std::move(item));
    }
    for(const auto& b:bursts){
        Item item;item.material=b.material;item.anchor=b.pos;const unsigned cellIndex=(tick-b.born)/debugSlow;float cell[4];gridCell(cellIndex,4,4,cell);
        if(b.material==kConeMaterial){bool valid=false;const float angle=screenAngle(right,down,b.dir,valid);
            // the spray's length on screen shrinks when it points along the view
            const float along=valid?(std::max)(0.35f,std::sqrt(1.f-std::pow(re3model::Dot(forward,b.dir),2.f))):0.35f;
            sprite(item,right,down,b.pos,b.size*along,b.size,valid?angle:b.roll,config.coneAnchor[0],config.coneAnchor[1],cell,withAlpha(sprayTint(b.colour,0xb0),1.f));}
        else sprite(item,right,down,b.pos,b.size,b.size,b.roll,0.5f,0.5f,cell,withAlpha(sprayTint(b.colour,0xb0),1.f));
        // in front of the wound it came out of: drawn after the body's buckets there
        const auto camera=re3model::TransformPoint(cam,b.pos);item.bucket=int((std::max)(1.f,camera.z-260.f))>>5;
        items.push_back(std::move(item));
    }
    for(const auto& d:drops){
        Item item;item.material=kDropMaterial;item.anchor=d.pos;float cell[4];gridCell(d.cell,4,4,cell);
        bool valid=false;const float moving=screenAngle(right,down,re3model::Normalize(d.vel),valid);
        const float elong=config.dropElong[d.cell];const float angle=elong>1.4f&&valid?moving-config.dropAngle[d.cell]:0.f;
        const float size=d.size*1.5f;
        sprite(item,right,down,d.pos,size,size,angle,0.5f,0.5f,cell,withAlpha(sprayTint(d.colour,0xc0),1.f));
        const auto camera=re3model::TransformPoint(cam,d.pos);item.bucket=int((std::max)(1.f,camera.z-60.f))>>5;
        items.push_back(std::move(item));
    }
    // per ordering table (45ADC0 by the item's anchor): one geometry, its runs grouped by bucket, far buckets first
    re3model::Mesh materials;materials.materials=asset->mesh.materials;
    for(unsigned k=0;k<kMaterials;++k){materials.materials[k].flags=re3model::AlphaBlend|re3model::TwoSided|(k<kPools?re3model::GrowthMask:0u);}
    const auto chooseTable=reinterpret_cast<ChooseTable>(gameBase+0x5adc0);
    struct Piece{std::uint32_t* table;std::int32_t position[3];std::uint16_t bucket;std::vector<re3model::DrawVertex> vertices;unsigned material;std::size_t order;};
    std::vector<Piece> pieces;
    for(std::size_t index=0;index<items.size();++index){auto& item=items[index];if(item.vertices.empty())continue;
        const auto camera=re3model::TransformPoint(cam,item.anchor);if(camera.z<256.f||camera.z>32767.f)continue;
        const std::int32_t position[3]={std::int32_t(item.anchor.x),std::int32_t(item.anchor.y),std::int32_t(item.anchor.z)};
        auto* table=chooseTable(position);if(!table||!accessible(table,1024*4,true))continue;
        re3model::Mesh mesh;mesh.vertices.resize(item.vertices.size());mesh.indices=item.indices;
        re3model::Material m=materials.materials[item.material];m.firstIndex=0;m.indexCount=std::uint32_t(item.indices.size());mesh.materials.push_back(m);
        auto projection=base;projection.vertexTint=&item.tint;std::uint16_t fixed=0;
        if(item.bucket>=0){fixed=std::uint16_t((std::min)(1023,item.bucket));projection.depth=fixedBucket;projection.depthContext=&fixed;}
        re3model::FrameGeometry geometry;std::string error;
        if(!re3model::PrepareFrame(mesh,item.vertices,projection,geometry,&error)||geometry.vertices.empty())continue;
        if(item.material<kPools&&(tick&127)==0&&!geometry.slices.empty()){unsigned lo=1023,hi=0;for(const auto& sl:geometry.slices){lo=(std::min)(lo,unsigned(sl.bucket));hi=(std::max)(hi,unsigned(sl.bucket));}
            log("BLOOD_POOL_BUCKETS variant=%u buckets=%u-%u slices=%zu vertices=%zu table=%p",item.material,lo,hi,geometry.slices.size(),geometry.vertices.size(),table);}
        for(const auto& slice:geometry.slices)for(unsigned r=0;r<slice.runCount;++r){const auto& run=geometry.runs[slice.firstRun+r];
            Piece piece;piece.table=table;std::memcpy(piece.position,position,sizeof(position));piece.bucket=slice.bucket;piece.material=item.material;piece.order=pieces.size();
            piece.vertices.assign(geometry.vertices.begin()+run.firstVertex,geometry.vertices.begin()+run.firstVertex+run.vertexCount);pieces.push_back(std::move(piece));}
    }
    if(pieces.empty())return;
    modelRenderer.growthOpacity=poolOpacity;   // release 1.50: only the HD pools are GrowthMask materials
    std::stable_sort(pieces.begin(),pieces.end(),[](const Piece& a,const Piece& b){return a.table!=b.table?a.table<b.table:a.bucket>b.bucket;});
    for(std::size_t i=0;i<pieces.size();){
        std::uint32_t* table=pieces[i].table;re3model::FrameGeometry merged;
        for(;i<pieces.size()&&pieces[i].table==table;++i){const auto& piece=pieces[i];
            if(merged.slices.empty()||merged.slices.back().bucket!=piece.bucket)merged.slices.push_back({piece.bucket,std::uint32_t(merged.runs.size()),0});
            merged.runs.push_back({std::uint32_t(merged.vertices.size()),std::uint32_t(piece.vertices.size()),piece.material});++merged.slices.back().runCount;
            merged.vertices.insert(merged.vertices.end(),piece.vertices.begin(),piece.vertices.end());}
        HRESULT hr;auto gpu=modelRenderer.upload(materials,std::move(merged),asset->textures,false,&hr);if(!gpu){once("blood-upload","HD blood upload failed this frame");return;}
        std::vector<re3bridge::BucketJob> jobs;jobs.reserve(gpu->slices.size());
        for(std::size_t n=0;n<gpu->slices.size();++n){auto job=std::make_shared<ModelJob>();job->geometry=gpu;job->slice=n;job->name="blood-fx";
            jobs.push_back({gpu->slices[n].bucket,reinterpret_cast<std::uintptr_t>(job.get()),job});}
        if(!re3bridge::EnqueueBatch(table,1024,jobs))once("blood-enqueue","HD blood skipped for one ordering table");
    }
}
// every actor's shadow, once a frame: the HD pools of corpses whose shadow update the native code no longer calls
inline void scanShadows(){
    auto visit=[](const std::uint8_t* actor){if(!accessible(actor,0x164))return;auto* shadow=read<std::uint8_t*>(actor,0x160);if(shadow&&accessible(shadow,0x68,true))poolFromShadow(shadow);};
    const auto end=read<std::uintptr_t>(reinterpret_cast<void*>(gameBase+0x65ce6c));
    for(std::uintptr_t at=gameBase+0x65cdcc;at<end&&at<gameBase+0x65cdcc+48*4;at+=4)visit(read<const std::uint8_t*>(reinterpret_cast<void*>(at)));
    visit(reinterpret_cast<const std::uint8_t*>(gameBase+0x65cbf4));
}
inline void update(){
    const std::uint32_t room=(std::uint32_t(read<std::uint16_t>(reinterpret_cast<void*>(gameBase+0x661fa6)))<<16)|read<std::uint16_t>(reinterpret_cast<void*>(gameBase+0x661fa8));
    if(room!=bloodRoom){
        if(bloodRoom!=~0u&&(!pools.empty()||!splats.empty()))log("BLOOD_CLEAR room=%x pools=%zu splats=%zu started=%llu sprays=%llu splats_made=%llu native_spray_hidden=%llu copy_filtered=%llu native_replaced=%llu corpse_hits=%llu",room,pools.size(),splats.size(),
            (unsigned long long)poolsStarted,(unsigned long long)burstsSpawned,(unsigned long long)splatsMade,(unsigned long long)nativeSprayHidden,(unsigned long long)re3bridge::detail::ft4Filtered,(unsigned long long)nativeReplaced,(unsigned long long)corpseHits);
        bloodRoom=room;pools.clear();splats.clear();bursts.clear();drops.clear();recentHits.clear();re3wound::shotBlood.clear();nativeSprays.clear();replacements.clear();nativeDecisions.clear();
    }
    ++tick;
    if(!enabled||!active()){re3wound::shotBlood.clear();return;}
    packReady=bool(pack());
    for(const auto& s:re3wound::shotBlood)noteHit(s.entry);
    if(InterlockedExchange(&corpseTestRequest,0))corpseShotTest();
    if(const LONG wanted=InterlockedExchange(&enemyLifeRequest,0))enemyLifeOne(int(wanted));
    if(InterlockedExchange(&bloodTestRequest,0)){
        const auto* player=reinterpret_cast<const std::uint8_t*>(gameBase+0x65cbf4);
        const Vec3 at{float(read<std::int32_t>(player,0x34)),float(read<std::int32_t>(player,0x38)),float(read<std::int32_t>(player,0x3c))};
        const float yaw=float(read<std::int16_t>(player,0x6e))*6.2831853f/4096.f;const Vec3 ahead{std::sin(yaw),0,std::cos(yaw)};
        static unsigned n=0;Pool p;p.shadow=debugShadowKey+(n&1);p.actor=player;p.centre=at-ahead*float(n&1)*1100.f;p.size=1320;p.w0=600;p.variant=std::uint8_t(n%kPools);p.born=tick;p.seen=tick;p.colour=0xffc8c0b8u;p.logged=true;
        pools.erase(std::remove_if(pools.begin(),pools.end(),[&](const Pool& q){return q.shadow==p.shadow;}),pools.end());pools.push_back(p);++n;
        re3wound::ShotBlood s;s.actor=nullptr;s.entry=at+ahead*500.f+Vec3{0,-1200.f,0};s.direction=ahead;s.normal=ahead*-1.f;s.hasExit=true;s.exit=s.entry+ahead*250.f;s.weapon=3;s.colour=0xffc8c0b8u;s.floorY=at.y;re3wound::shotBlood.push_back(s);
        log("BLOOD_TEST pool variant=%u at=%.0f,%.0f,%.0f yaw=%.2f",unsigned(p.variant),p.centre.x,p.centre.y,p.centre.z,yaw);}
    decideNativeSprays();scanShadows();spawnFromShots();stepDrops();drawBlood();
}
// The effect loop's second emitter 458750 (call 45832F: effects with flag 0x200 = rotated sprites, e.g. the muzzle flash
// 0801) takes the same arguments as the billboard 4583D0: traced with effect_trace, native blood collapsed there too.
static BillboardBuild originalRotated=nullptr;
inline void __cdecl rotatedHook(void* packets,void* effect,int groupSize,int count,int projectionH,std::uint32_t* orderingTable,int command,int groupStart){
    std::lock_guard<std::recursive_mutex> guard(mutex);
    originalRotated(packets,effect,groupSize,count,projectionH,orderingTable,command,groupStart);
    if(effectTrace&&effectTraceLines<6000&&accessible(effect,0xd4)&&accessible(packets,40)){static std::unordered_map<std::uint32_t,unsigned> seen;
        const auto key=read<std::uint16_t>(effect,0x28);const auto* p=static_cast<const std::uint8_t*>(packets);
        if(++seen[key]<=3){++effectTraceLines;log("EFFECT_ROTATED key=%04x class=%02x flags=%04x group=%d count=%d command=%02x pkt=%02x clut=%04x tpage=%04x",key,read<std::uint8_t>(effect),read<std::uint16_t>(effect,0x24),groupSize,count,command,p[7],read<std::uint16_t>(p,14),read<std::uint16_t>(p,22));}}
    try{collapseNativeSpray(packets,effect,count);}catch(...){}
    try{muzzle::collapseNative(packets,effect,count);}catch(...){} // fx 2026-11-08 muzzle
}
inline void install(){
    if(!enabled){log("BLOOD_FX disabled by pilot.ini blood_fx=0");return;}
    copyFilter=re3bridge::detail::InstallFt4Filter(gameBase,&nativeBloodPacket);
    log(copyFilter?"BLOOD_FX_COPY_FILTER installed (FT4 copy 41FAB1: native hit blood texture hidden while HD blood is drawn)":"BLOOD_FX_COPY_FILTER_SKIPPED call signature mismatch: native blood collapsed at its emitter");
    {re3bridge::detail::Patch rotated;
        if(re3bridge::detail::BuildCallPatch(rotated,gameBase+0x5832f,gameBase+0x58750,reinterpret_cast<void*>(rotatedHook))){originalRotated=reinterpret_cast<BillboardBuild>(gameBase+0x58750);
            if(!re3bridge::detail::WritePatch(rotated,true))log("BLOOD_FX_ROTATED_HOOK_FAILED");}
        else log("BLOOD_FX_ROTATED_HOOK_SKIPPED call signature mismatch");}
    re3bridge::detail::Patch patch;
    if(!re3bridge::detail::BuildCallPatch(patch,gameBase+0x2bb18,gameBase+0x57130,reinterpret_cast<void*>(shadowHook))){log("BLOOD_FX_SHADOW_HOOK_SKIPPED call signature mismatch: native pools kept");return;}
    originalShadow=reinterpret_cast<ShadowUpdate>(gameBase+0x57130);
    if(!re3bridge::detail::WritePatch(patch,true)){log("BLOOD_FX_SHADOW_HOOK_FAILED");return;}
    shadowHooked=true;log("BLOOD_FX_INSTALL_OK shadow update 457130 wrapped (HD pools), HD sprays/droplets/splats (v2: real gravity, darker pools), native hit blood 0C00 always collapsed (HD replacement where no HD wound)");
}
} // namespace blood
