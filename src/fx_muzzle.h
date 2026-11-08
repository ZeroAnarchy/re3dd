#pragma once
// ---------------------------------------------------------------- HD muzzle flashes and gun smoke (fx round 2026-11-08, muzzle agent)
// Included at the end of extra_model_integration.h after blood_fx.h, inside namespace re3extra (read/accessible/active/once,
// getModel, modelRenderer, the blood:: sprite helpers, the native OT helpers of the resting cases).
//
// Native (identified 08.11, runtime/staging/fx-2026-11-08/muzzle/REPORT.ru.txt): the gun routines (table 52A3B0; NPC guns
// 4E3430/4E3530/4EB830/4EBA20; mine thrower 467000/468340) spawn their effects with 43D370(kind, key, hand matrix = part 4
// +0x40, short offset[3] in the hand frame, extra); kind low byte = effect bank, next byte = template, kept at +0x28. Flash =
// 0001 (core bank 1: a billboard blob, 3 ticks: quads 0 -> 1 -> 2), its parts 0801 (long flame tongues, rotated sprites,
// quads 18-20 -> 21-23), 0901 (spark clusters), 0A01/0B01 (small streaks, quad 7); all on tpage 3E / CLUT 7C11, additive.
// Smoke = bank 3 (0003/0603/0703/2303/0503). model_integration.h wraps 113 of these calls (effectSpawnHook) and reports
// every spawn here (one line there).
// Round 2 (08.11, after the user played round 1: "too small and thin, and they move with the barrel's recoil; make them
// denser, closer to the original, slightly higher resolution; smoke good but bigger and denser"):
//  - the flash IS the native one again: its effects run and draw where the game puts them (fixed in the world at the
//    shot, native size, orientation, frames and timing); only their texture page is redrawn at 8x (Codex image_gen from
//    the native sprites, model-pack/fx-muzzle/native-flash.png). proxy.cpp drawUiPacket asks flashBinding() for every
//    FT4 it draws: a quad of tpage 3E / CLUT 7C11 inside the bank-1 strip (u 0..247, v 168..215) gets that texture with
//    the same vertices and UVs (the TEXU palette-33 4x upscale of ui-pack is bypassed for it). Round 1's own HD flame
//    (side sprite + star at the live muzzle) is gone.
//  - fps60: a flash part in its first build is not drawn in the in-between frame before that build (its packets are
//    recorded by content when the emitters write them, collapseNative) - as round 1.
//  - smoke: HD puffs in world space as round 1, bigger and denser, more of them in a burst; the native smoke sprites of
//    the replaced smoke effects are collapsed where 4583D0 / 458750 write them.
// pilot.ini muzzle_fx=0 (or F8) = all native.
namespace muzzle {
using re3model::Vec3;
enum Gun:std::uint8_t{Pistol,Rifle,Shotgun,Magnum,Gatling,Launcher,Rocket,kGuns};
static const char* const kGunNames[kGuns]={"pistol","rifle","shotgun","magnum","gatling","launcher","rocket"};
enum Role:std::uint8_t{Flash,Spike,Smoke,PortSmoke,Keep,Unknown};
static const char* const kRoleNames[]={"flash","spike","smoke","port-smoke","keep","unknown"};
struct SiteRow{std::uint32_t rva;std::uint8_t gun,role;};
// every 43D370 call of the gun routines (qa/fix-2026-10-24/weapons/work/spawn_table.py, models/fx-2026-11-08/muzzle/
// tools/spawn_scan.py); roles from the static offsets and the effect_trace runs of this round
static const SiteRow kSites[]={
    // 02/03 handgun 465E30 (Sigpro, M92F, Glock 18C = alias of 03): casing, flash 0001, smoke 0003
    {0x65eed,Pistol,Keep},{0x65f1e,Pistol,Flash},{0x65f45,Pistol,Smoke},
    // 04 Benelli 465F60: flash 0001, star 0801, smoke 0003/0703, flame puffs 0901 x2 (flash texture), side spikes 0A01 x4 (rotated
    // flash sprites), shell, receiver smoke
    {0x66007,Shotgun,Flash},{0x66048,Shotgun,Spike},{0x6606e,Shotgun,Smoke},{0x66097,Shotgun,Smoke},{0x660c8,Shotgun,Spike},{0x660fb,Shotgun,Spike},
    {0x66182,Shotgun,Spike},{0x661f1,Shotgun,Spike},{0x66257,Shotgun,Spike},{0x662bf,Shotgun,Spike},{0x6635b,Shotgun,Keep},{0x66381,Shotgun,PortSmoke},
    // 05 magnum 4663B0: flash, star, cylinder-gap smoke (port), smoke 0703, side spikes 0B01 x4 (rotated flash sprites)
    {0x66457,Magnum,Flash},{0x66498,Magnum,Spike},{0x664be,Magnum,PortSmoke},{0x664e7,Magnum,Smoke},{0x66572,Magnum,Spike},{0x665e0,Magnum,Spike},{0x66648,Magnum,Spike},{0x666b0,Magnum,Spike},
    // 06-09 grenade launcher 4666C0/466820/466980/466AE0: flash, smoke (the shell and the break-open smoke stay native)
    {0x66760,Launcher,Flash},{0x66789,Launcher,Smoke},{0x668c0,Launcher,Flash},{0x668e9,Launcher,Smoke},{0x66a20,Launcher,Flash},{0x66a49,Launcher,Smoke},{0x66b80,Launcher,Flash},{0x66ba9,Launcher,Smoke},
    // 0A rocket launcher 466C40: the rocket (0502), launch flash, smoke, back blast x2
    {0x66d0e,Rocket,Keep},{0x66d37,Rocket,Flash},{0x66d5b,Rocket,Smoke},{0x66d82,Rocket,PortSmoke},{0x66dba,Rocket,PortSmoke},
    // 0B gatling 466E00: flash, star, casing, side smoke 0503 (port list), smoke 0603
    {0x66eac,Gatling,Flash},{0x66eea,Gatling,Spike},{0x66f27,Gatling,Keep},{0x66f62,Gatling,PortSmoke},{0x66fc7,Gatling,Smoke},
    // 0C / 14 mine thrower 467000 / 468340 (own call patches below)
    {0x67075,Launcher,Flash},{0x67099,Launcher,Smoke},{0x670c3,Launcher,Smoke},{0x683b5,Launcher,Flash},{0x683d9,Launcher,Smoke},{0x68400,Launcher,Smoke},
    // 0D Eagle 4671A0
    {0x6725a,Pistol,Keep},{0x67290,Pistol,Flash},{0x672bf,Pistol,Smoke},
    // 0E / 0F M4A1 4672E0 / 467590: flash, 4 star spikes 0801, casing, muzzle smoke 0603, port smoke 0603
    {0x67337,Rifle,Flash},{0x67377,Rifle,Spike},{0x673b9,Rifle,Spike},{0x673fd,Rifle,Spike},{0x67444,Rifle,Spike},{0x67481,Rifle,Keep},{0x674bd,Rifle,Smoke},{0x674fd,Rifle,PortSmoke},
    {0x67643,Rifle,Flash},{0x67686,Rifle,Spike},{0x676c8,Rifle,Spike},{0x6770c,Rifle,Spike},{0x67750,Rifle,Spike},{0x67790,Rifle,Keep},{0x677fa,Rifle,Smoke},{0x6783a,Rifle,PortSmoke},
    // 10 Western Custom M37 467860
    {0x67929,Shotgun,Flash},{0x6796c,Shotgun,Spike},{0x67993,Shotgun,PortSmoke},{0x679bd,Shotgun,Smoke},{0x679ef,Shotgun,Spike},{0x67a23,Shotgun,Spike},
    {0x67aac,Shotgun,Spike},{0x67b1d,Shotgun,Spike},{0x67b85,Shotgun,Spike},{0x67bef,Shotgun,Spike},{0x67c96,Shotgun,Keep},{0x67cbb,Shotgun,PortSmoke},
    // 11 / 12 enhanced handguns 467D10
    {0x67dcc,Pistol,Keep},{0x67dfa,Pistol,Flash},{0x67e36,Pistol,Spike},{0x67e5a,Pistol,Smoke},{0x67e80,Pistol,Smoke},
    // 13 enhanced Benelli 467EA0 (2303 = smoke, bank 3)
    {0x67f47,Shotgun,Flash},{0x67f88,Shotgun,Spike},{0x67fb1,Shotgun,Smoke},{0x67fd7,Shotgun,Smoke},{0x68000,Shotgun,Smoke},{0x6802c,Shotgun,Smoke},{0x6805c,Shotgun,Spike},{0x6808d,Shotgun,Spike},
    {0x68113,Shotgun,Spike},{0x68181,Shotgun,Spike},{0x681e9,Shotgun,Spike},{0x68251,Shotgun,Spike},{0x682eb,Shotgun,Keep},{0x68311,Shotgun,PortSmoke},
    // NPC guns (Carlos / Mikhail / Nikolai / soldiers in cut scenes): handgun A/B 4E3430/4EB830, rifle A/B 4E3530/4EBA20
    {0xe34b5,Pistol,Keep},{0xe34e6,Pistol,Flash},{0xe3516,Pistol,Smoke},
    {0xe3587,Rifle,Flash},{0xe35c5,Rifle,Spike},{0xe360a,Rifle,Spike},{0xe364c,Rifle,Spike},{0xe3691,Rifle,Spike},{0xe36d5,Rifle,Keep},{0xe3713,Rifle,Smoke},{0xe374f,Rifle,PortSmoke},
    {0xeb917,Pistol,Keep},{0xeb945,Pistol,Flash},{0xeb974,Pistol,Smoke},
    {0xebab1,Rifle,Flash},{0xebaf1,Rifle,Spike},{0xebb33,Rifle,Spike},{0xebb7a,Rifle,Spike},{0xebbbe,Rifle,Spike},{0xebbf8,Rifle,Keep},{0xebc4e,Rifle,Smoke},{0xebc8e,Rifle,PortSmoke}};
constexpr std::size_t kSiteCount=sizeof(kSites)/sizeof(kSites[0]);
static const std::uint32_t kMineSites[]={0x67075,0x67099,0x670c3,0x683b5,0x683d9,0x68400};
// The native flash 0001 is a billboard CENTRED ahead of the barrel: its offset lies beyond the native muzzle tip (top
// vertices of the PLW weapon mesh, hand frame: M92F 411 vs flash 450, Eagle 543/600, Benelli 1275/1500, magnum 518/540,
// M4A1 1215/1300, rocket launcher 276,976 / 380,1200) and the weapons.txt muzzle row moves both alike. The HD flame starts
// at the tip: the flash point goes back by these hand-frame units (Y = along the barrel, X = up). Gatling, M37 and the
// grenade launcher keep their barrels in part 22: measured on the HD captures (runs 20261108-043909 / -044352).
struct FlashBack{std::uint32_t rva;float back,down;};
static const FlashBack kFlashBack[]={{0x65f1e,40,0},{0x67dfa,40,0},{0xe34e6,40,0},{0xeb945,40,0},{0x67290,57,0},
    {0x66007,225,0},{0x67f47,225,0},{0x67929,55,0},{0x66457,22,0},{0x67337,120,0},{0x67643,120,0},{0xe3587,120,0},{0xebab1,120,0},
    {0x66d37,224,104},{0x66eac,140,0}};

// per gun: world units (~1 mm). length = how far along the barrel the smoke starts (the native flame's reach)
struct Style{float length;unsigned puffs;float puffSize,puffLife,puffAlpha,puffSpeed;unsigned burstCap;};
// round 2: puffs x1.5-1.6 bigger, denser, automatic guns 2 a shot and twice the burst cap; the launchers keep their size
// (one shot already fills a corridor) - no grey sheets over the whole room
static Style styles[kGuns]={
    /*pistol  */{560,2,880,32,0.60f,1.f,18},
    /*rifle   */{700,2,800,28,0.52f,1.f,22},
    /*shotgun */{1150,4,1250,42,0.68f,1.3f,20},
    /*magnum  */{1050,3,1120,38,0.66f,1.2f,18},
    /*gatling */{1000,2,820,26,0.50f,1.f,26},
    /*launcher*/{900,4,1150,46,0.66f,1.1f,16},
    /*rocket  */{1500,5,1250,50,0.64f,1.4f,16}};
constexpr unsigned kSmokeMaterial=0,kMaterials=2;
constexpr std::size_t kMaxPuffs=128,kMaxFlashFx=96,kMaxNewPackets=512;

static bool enabled=true;            // pilot.ini muzzle_fx
static bool trace=false;             // pilot.ini muzzle_trace: every spawn of the gun routines logged (MUZZLE_SPAWN)
static unsigned debugSlow=1;         // pilot.ini muzzle_debug_slow (QA captures): smoke N times slower
static bool packReady=false,installed=false,debugWarp=false,flashTexture=false,configLoaded=false;
struct Puff{Vec3 pos{},vel{};float size=0,roll=0,spin=0,alpha=0;std::uint32_t born=0,life=30,light=0xff808080u;std::uint8_t sheet=0;bool mirror=false;const void* hand=nullptr;};
struct Hidden{std::uintptr_t effect=0;std::uint64_t generation=0;std::uint32_t tick=0;std::uint16_t kind=0;};
struct FlashFx{std::uintptr_t effect=0;std::uint64_t generation=0;std::uint32_t serial=0;bool emitted=false;bool pin=false;Vec3 at{};};
struct NewPacket{std::uint64_t hash=0;std::uint32_t serial=0;};
static std::vector<Puff> puffs;
static std::vector<Hidden> hidden;          // native smoke effects whose sprites are collapsed
static std::vector<FlashFx> flashFx;        // native flash parts spawned by the gun routines (their first build matters)
static std::deque<NewPacket> newPackets;    // content of flash packets in their first build (fps60 in-between frame)
static std::unordered_map<std::uintptr_t,std::size_t> siteAt;   // return address (call + 5) -> kSites row
static std::uint32_t tick=0,room=~0u,seedState=0x2545f491u;
static std::uint64_t flashes=0,puffsMade=0,hiddenEffects=0,collapsedPackets=0,spawnLines=0,flashQuads=0,flashSkipped=0,pinnedPackets=0;
static double drawMs=0;static unsigned drawFrames=0;
inline float rnd(){seedState=seedState*1664525u+1013904223u;return float((seedState>>8)&0xffff)/65535.f;}
inline float rnd(float a,float b){return a+(b-a)*rnd();}

// ---------------------------------------------------------------- pack
inline void loadConfig(){
    if(configLoaded)return;configLoaded=true;
    std::ifstream file(root+"/model-pack/fx-muzzle/muzzle.txt");std::string line;
    while(std::getline(file,line)){std::istringstream in(line);std::string key;if(!(in>>key)||key[0]=='#')continue;
        // gun <name> <length> <puffs> <puffSize> <puffLife> <puffAlpha> <burstCap>: tuning without a rebuild
        if(key=="gun"){std::string name;Style s{};if(in>>name>>s.length>>s.puffs>>s.puffSize>>s.puffLife>>s.puffAlpha>>s.burstCap)
            for(unsigned g=0;g<kGuns;++g)if(name==kGunNames[g]){s.puffSpeed=styles[g].puffSpeed;s.puffs=(std::min)(8u,s.puffs);s.burstCap=(std::max)(1u,(std::min)(40u,s.burstCap));styles[g]=s;
                log("MUZZLE_STYLE %s puffs=%u size=%.0f life=%.0f alpha=%.2f cap=%u",name.c_str(),s.puffs,s.puffSize,s.puffLife,s.puffAlpha,s.burstCap);}}}
    std::ifstream flash(root+"/model-pack/fx-muzzle/native-flash.png",std::ios::binary);flashTexture=bool(flash);
    log("MUZZLE_FX_PACK smoke materials=%u native-flash.png=%s",kMaterials,flashTexture?"yes (flash page tpage 3E/CLUT 7C11 at 8x)":"MISSING (native flash page as before)");
}
inline std::shared_ptr<ModelAsset> pack(){
    auto asset=getModel("fx-muzzle");
    if(!asset||asset->mesh.materials.size()!=kMaterials||asset->textures.size()!=kMaterials){once("muzzle-pack","HD muzzle pack unavailable (model-pack/fx-muzzle, 2 smoke materials): native smoke kept");return nullptr;}
    loadConfig();return asset;
}
inline bool ready(){return enabled&&installed&&packReady&&active();}

// ---------------------------------------------------------------- spawns (effectSpawnHook / mineSpawnHook)
// The gun routines run in the actor update, before this build's effect loop (update(), which counts `tick`): what they
// spawn belongs to tick+1, the tick drawn by that effect loop.
inline bool inPool(const void* effect){
    const auto address=reinterpret_cast<std::uintptr_t>(effect),start=std::uintptr_t(gameBase+0x646720);
    return address>=start&&address<start+96u*0xd4&&(address-start)%0xd4==0&&accessible(effect,0xd4);
}
inline std::uint64_t generationOf(std::uintptr_t address){const auto g=effectGeneration.find(address);return g==effectGeneration.end()?0:g->second;}
inline const std::uint8_t* actorOfHand(const void* hand){
    auto owns=[&](const std::uint8_t* actor){if(!accessible(actor,0x10c))return false;const auto parts=read<const std::uint8_t*>(actor,0x108);return parts&&parts+4*0xbc+0x40==hand;};
    const auto* player=reinterpret_cast<const std::uint8_t*>(gameBase+0x65cbf4);if(owns(player))return player;
    for(const auto& entry:actorRuntime){const auto* actor=static_cast<const std::uint8_t*>(entry.first);if(owns(actor))return actor;}
    return nullptr;
}
inline std::uint32_t lightOf(const void* actor){
    if(!actor)return 0xff808080u;const auto found=actorRuntime.find(const_cast<void*>(actor));
    return found!=actorRuntime.end()&&re3bridge::BuildSerial()-found->second.lastSerial<30?found->second.bloodLightView:0xff808080u;
}
// the muzzle at the shot (fixed from then on, like the native flash): the hand matrix of the shot times the offset
// effectSpawnHook handed to the game, moved back from the native flash centre to the barrel tip; the barrel = hand +Y
inline bool muzzleAtShot(const SiteRow& site,const void* hand,const std::int16_t* used,Vec3& pos,Vec3& dir){
    if(!hand||!accessible(hand,32)||!used||!accessible(used,6))return false;
    float local[3]={float(used[0]),float(used[1]),float(used[2])};
    for(const auto& b:kFlashBack)if(b.rva==site.rva){local[1]-=b.back;local[0]-=b.down;break;}
    const auto m=re3model::FromNativeMatrix(read<re3bridge::Matrix32>(hand));
    pos=re3model::TransformPoint(m,{local[0],local[1],local[2]});dir=re3model::Normalize({m.m[1],m.m[5],m.m[9]});
    const auto hit=muzzleHands.find(hand);
    if(hit!=muzzleHands.end()&&hit->second.dropped&&re3bridge::BuildSerial()-hit->second.serial<=3)dir=re3model::Normalize(re3grip::Apply(hit->second.drop,dir));
    return true;
}
inline void hide(const std::uint8_t* effect,int kind){
    if(!inPool(effect))return;
    const auto address=reinterpret_cast<std::uintptr_t>(effect);const std::uint64_t generation=generationOf(address);
    for(auto& h:hidden)if(h.effect==address){h.generation=generation;h.tick=tick+1;h.kind=std::uint16_t(kind);return;}
    if(hidden.size()>=96)hidden.erase(hidden.begin());
    hidden.push_back({address,generation,tick+1,std::uint16_t(kind)});++hiddenEffects;
}
// pin: the core flash 0001 re-reads the live hand matrix every tick (MUZZLE_FLASH_EMIT, run 20261108-133237: its
// +0x48/+0x70 follow the recoil on ticks 2-3), so its place at the spawn is kept and later builds are moved back there
inline void noteFlashPart(const std::uint8_t* effect,bool pin){
    if(!inPool(effect))return;
    const auto address=reinterpret_cast<std::uintptr_t>(effect);
    flashFx.erase(std::remove_if(flashFx.begin(),flashFx.end(),[&](const FlashFx& f){return f.effect==address||re3bridge::BuildSerial()-f.serial>8;}),flashFx.end());
    if(flashFx.size()>=kMaxFlashFx)flashFx.erase(flashFx.begin());
    FlashFx f{address,generationOf(address),re3bridge::BuildSerial()};
    f.pin=pin;f.at={float(read<std::int32_t>(effect,0x48)),float(read<std::int32_t>(effect,0x4c)),float(read<std::int32_t>(effect,0x50))};
    flashFx.push_back(f);
}
inline void spawnPuff(const Vec3& at,const Vec3& dir,const Style& st,std::uint8_t gun,float sizeScale,float alphaScale,float speedScale,std::uint32_t light,const void* hand){
    if(puffs.size()>=kMaxPuffs)puffs.erase(puffs.begin());
    Puff p;p.pos=at+Vec3{rnd(-30.f,30.f),rnd(-30.f,30.f),rnd(-30.f,30.f)};
    p.vel=dir*(rnd(8.f,22.f)*st.puffSpeed*speedScale)+Vec3{rnd(-1.5f,1.5f),rnd(-2.5f,0.f),rnd(-1.5f,1.5f)};
    p.size=st.puffSize*sizeScale*rnd(0.82f,1.18f);p.life=std::uint32_t((std::max)(8.f,st.puffLife*rnd(0.85f,1.25f)));p.alpha=(std::min)(0.9f,st.puffAlpha*alphaScale*rnd(0.88f,1.f));
    const bool heavy=gun==Shotgun||gun==Launcher||gun==Rocket;p.sheet=std::uint8_t(rnd()<(heavy?0.75f:0.4f)?1:0);
    p.roll=rnd()*6.2831853f;p.spin=rnd(-0.035f,0.035f);p.mirror=rnd()<0.5f;p.light=light;p.hand=hand;p.born=tick+1;puffs.push_back(p);++puffsMade;
}
// smoke of a shot in world space: along the first part of the flame, pushed out along the barrel, then rising and
// spreading. A burst keeps adding smoke, up to burstCap live puffs of this hand (beyond 2/3 of it: every other shot).
inline void shotSmoke(const SiteRow& site,const void* hand,const std::int16_t* used,const std::uint8_t* effect){
    Vec3 pos{},dir{0,0,1};
    if(!muzzleAtShot(site,hand,used,pos,dir)){if(!inPool(effect))return;pos={float(read<std::int32_t>(effect,0x48)),float(read<std::int32_t>(effect,0x4c)),float(read<std::int32_t>(effect,0x50))};}
    const Style& st=styles[site.gun];unsigned n=st.puffs;float alpha=1.f;++flashes;
    unsigned same=0;for(const auto& p:puffs)if(p.hand==hand)++same;
    if(same>=st.burstCap)n=0;else if(same>=st.burstCap*2/3){n=(flashes&1)?0u:(std::max)(1u,n/2);alpha=0.85f;}
    const std::uint32_t light=lightOf(actorOfHand(hand));
    for(unsigned k=0;k<n;++k)spawnPuff(pos+dir*(rnd(0.1f,0.75f)*st.length),dir,st,site.gun,1.f,alpha,1.f,light,hand);
}
static std::uint32_t lightTraceShots=0;static std::uint32_t lightTraceBorn=0;static const void* lightTraceActor=nullptr;
static LONG pendingTest=0;static unsigned testIndex=0;static int burstArmed=0;static std::uint32_t burstStart=0;static std::string burstLabel;static unsigned burstSerial=0;
static void onSpawn(bool port,int kind,int key,const void* hand,const std::int16_t* offset,const std::int16_t* used,int result,const void* returnAddress){
    std::lock_guard<std::recursive_mutex> guard(mutex);
    try{
        const auto at=siteAt.find(reinterpret_cast<std::uintptr_t>(returnAddress));
        const SiteRow* site=at==siteAt.end()?nullptr:&kSites[at->second];
        const auto* effect=reinterpret_cast<const std::uint8_t*>(static_cast<std::uintptr_t>(static_cast<std::uint32_t>(result)));
        const bool pooled=inPool(effect);
        if(trace&&spawnLines<1500){++spawnLines;
            Vec3 computed{};if(hand&&accessible(hand,32)&&used&&accessible(used,6))computed=re3model::TransformPoint(re3model::FromNativeMatrix(read<re3bridge::Matrix32>(hand)),{float(used[0]),float(used[1]),float(used[2])});
            log("MUZZLE_SPAWN tick=%u site=%x gun=%s role=%s port=%d kind=%x key=%x offset=%d,%d,%d used=%d,%d,%d effect=%p slot=%d e.key=%04x class=%02x flags=%04x tpage=%04x clut=%04x pos=%d,%d,%d hand*used=%.0f,%.0f,%.0f actor=%p",
                tick,site?unsigned(site->rva):0u,site?kGunNames[site->gun]:"?",site?kRoleNames[site->role]:"?",int(port),unsigned(kind),unsigned(key),
                offset?offset[0]:0,offset?offset[1]:0,offset?offset[2]:0,used?used[0]:0,used?used[1]:0,used?used[2]:0,effect,pooled?int((reinterpret_cast<std::uintptr_t>(effect)-std::uintptr_t(gameBase+0x646720))/0xd4):-1,
                pooled?unsigned(read<std::uint16_t>(effect,0x28)):0u,pooled?unsigned(read<std::uint8_t>(effect)):0u,pooled?unsigned(read<std::uint16_t>(effect,0x24)):0u,
                pooled?unsigned(read<std::uint16_t>(effect,0x46)):0u,pooled?unsigned(read<std::uint16_t>(effect,0x44)):0u,
                pooled?read<std::int32_t>(effect,0x48):0,pooled?read<std::int32_t>(effect,0x4c):0,pooled?read<std::int32_t>(effect,0x50):0,computed.x,computed.y,computed.z,actorOfHand(hand));}
        if(!site)return;
        if(site->role==Flash&&burstArmed>0&&!burstStart){burstStart=tick+1;++burstSerial;char label[64];sprintf_s(label,"MZ%u-%s",burstSerial,kGunNames[site->gun]);burstLabel=label;
            // where the native flash is in the 2560x1920 capture (crops of the QA sheets)
            float sx=-1,sy=-1,depth=0;re3model::Projection projection;
            if(pooled&&snapshotProjection(projection)){const auto c=re3model::TransformPoint(projection.camera,{float(read<std::int32_t>(effect,0x48)),float(read<std::int32_t>(effect,0x4c)),float(read<std::int32_t>(effect,0x50))});
                if(c.z>1.f){sx=(c.x*projection.h/c.z+projection.offsetX)*8.f;sy=(c.y*projection.h/c.z+projection.offsetY)*8.f;depth=c.z;}}
            log("MUZZLE_TEST burst %s armed=%d tick=%u screen=%.0f,%.0f depth=%.0f",burstLabel.c_str(),burstArmed,tick,sx,sy,depth);}
        if(site->role==Flash&&lightTraceShots<12){++lightTraceShots;lightTraceBorn=tick+1;lightTraceActor=actorOfHand(hand);}
        if(!enabled||!installed)return;
        // the flash parts stay native (HD texture page, flashBinding); their first build is remembered for fps60
        if(site->role==Flash||site->role==Spike)noteFlashPart(effect,site->role==Flash);
        if(!ready())return;
        switch(site->role){
        case Flash:shotSmoke(*site,hand,used,effect);break;
        case Smoke:hide(effect,kind);break;
        case PortSmoke:{hide(effect,kind);if(!pooled)break;
            const Vec3 at3{float(read<std::int32_t>(effect,0x48)),float(read<std::int32_t>(effect,0x4c)),float(read<std::int32_t>(effect,0x50))};
            const std::uint32_t light=lightOf(actorOfHand(hand));const Vec3 up{0,-1,0};
            if(site->gun==Rocket){for(unsigned k=0;k<2;++k)spawnPuff(at3,up,styles[Rocket],Rocket,0.9f,0.9f,0.6f,light,hand);break;}   // back blast
            spawnPuff(at3,up,styles[site->gun],site->gun,0.6f,0.7f,0.3f,light,hand);break;}
        default:break;
        }
    }catch(...){once("muzzle-spawn-exception","HD muzzle bookkeeping failed; native effect kept");}
}

// ---------------------------------------------------------------- native sprites
// Called right after 4583D0 (billboards) / 458750 (rotated sprites) wrote an effect's packets (extra_model_integration.h
// billboardHook, blood_fx.h rotatedHook). A replaced smoke effect's FT4s are collapsed to their first corner (nothing
// drawn; the effect runs on). A flash part in the build it was spawned in: its packets' content is remembered, so the
// fps60 in-between frame before that build can leave them out (flashBinding).
inline std::uint64_t packetHash(const std::uint8_t* q){std::uint64_t h=1469598103934665603ull;for(unsigned i=8;i<40;++i){h^=q[i];h*=1099511628211ull;}return h;}
static unsigned collapseLines=0;
static void collapseNative(void* packets,const void* effect,int count){
    if(!enabled||!installed||!active()||count<1||count>16)return;
    const auto address=reinterpret_cast<std::uintptr_t>(effect);
    auto* p=static_cast<std::uint8_t*>(packets);
    // the first emission of a flash part (the build serial may have moved on once between the spawn in the actor update
    // and the effect loop): its packets are the ones of its first build
    if(!flashFx.empty()&&flashTexture){const auto serial=re3bridge::BuildSerial();
        for(auto& f:flashFx)if(f.effect==address&&!f.emitted&&serial-f.serial<=2&&generationOf(address)==f.generation){f.emitted=true;
            if(accessible(p,40*count))for(int k=0;k<count;++k){if(newPackets.size()>=kMaxNewPackets)newPackets.pop_front();newPackets.push_back({packetHash(p+40*k),serial});}
            static unsigned newLines=0;if(newLines<4){++newLines;log("MUZZLE_FLASH_FIRST effect=%p count=%d serial=%u spawn_serial=%u code=%02x tpage=%04x clut=%04x",effect,count,serial,f.serial,p[7],read<std::uint16_t>(p,22),read<std::uint16_t>(p,14));}
            break;}}
    if(trace&&!flashFx.empty()&&accessible(p,40)&&inPool(static_cast<const std::uint8_t*>(effect))){static unsigned emitLines=0;   // where a flash part is drawn, tick by tick
        for(const auto& f:flashFx)if(f.effect==address&&emitLines<160){++emitLines;const auto* e=static_cast<const std::uint8_t*>(effect);
            log("MUZZLE_FLASH_EMIT effect=%p key=%04x serial=%u age=%u pos=%d,%d,%d m70=%d,%d,%d,%d xy0=%d,%d xy3=%d,%d count=%d",effect,unsigned(read<std::uint16_t>(e,0x28)),re3bridge::BuildSerial(),re3bridge::BuildSerial()-f.serial,
                read<std::int32_t>(e,0x48),read<std::int32_t>(e,0x4c),read<std::int32_t>(e,0x50),read<std::int32_t>(e,0x70),read<std::int32_t>(e,0x74),read<std::int32_t>(e,0x78),read<std::int32_t>(e,0x7c),
                int(read<std::int16_t>(p,8)),int(read<std::int16_t>(p,10)),int(read<std::int16_t>(p,32)),int(read<std::int16_t>(p,34)),count);break;}}
    // a pinned flash part drawn from a moved place (the hand recoiled): its quads go back to the spawn place, scaled by
    // the depth ratio (a billboard's size is 1/depth) - the flash stays where the shot was, as in the brief of round 2
    if(!flashFx.empty()){
        for(const auto& f:flashFx)if(f.effect==address&&f.pin&&generationOf(address)==f.generation){
            const auto* e=static_cast<const std::uint8_t*>(effect);
            const Vec3 now{float(read<std::int32_t>(e,0x48)),float(read<std::int32_t>(e,0x4c)),float(read<std::int32_t>(e,0x50))};
            re3model::Projection pr;
            if(std::fabs(now.x-f.at.x)+std::fabs(now.y-f.at.y)+std::fabs(now.z-f.at.z)>2.f&&accessible(p,40*count,true)&&snapshotProjection(pr)){
                const auto a=re3model::TransformPoint(pr.camera,now),b=re3model::TransformPoint(pr.camera,f.at);
                if(a.z>1.f&&b.z>1.f){
                    const float ax=a.x*pr.h/a.z+pr.offsetX,ay=a.y*pr.h/a.z+pr.offsetY,bx=b.x*pr.h/b.z+pr.offsetX,by=b.y*pr.h/b.z+pr.offsetY,s=a.z/b.z;
                    for(int k=0;k<count;++k){auto* q=p+40*k;if((q[7]&0xfc)!=0x2c)continue;
                        for(int i=0;i<4;++i){auto* xy=reinterpret_cast<std::int16_t*>(q+8+8*i);
                            xy[0]=std::int16_t(std::lround(bx+(xy[0]-ax)*s));xy[1]=std::int16_t(std::lround(by+(xy[1]-ay)*s));}}
                    ++pinnedPackets;
                    static unsigned pinLines=0;if(trace&&pinLines<12){++pinLines;log("MUZZLE_FLASH_PIN effect=%p key=%04x age=%u moved=%.0f,%.0f,%.0f screen %.1f,%.1f -> %.1f,%.1f scale=%.3f",effect,unsigned(read<std::uint16_t>(e,0x28)),re3bridge::BuildSerial()-f.serial,now.x-f.at.x,now.y-f.at.y,now.z-f.at.z,ax,ay,bx,by,s);}
                }}
            break;}
    }
    if(hidden.empty()||!packReady)return;
    const Hidden* h=nullptr;for(const auto& x:hidden)if(x.effect==address){h=&x;break;}
    if(!h||generationOf(address)!=h->generation)return;
    if(!accessible(p,40*count,true))return;
    for(int k=0;k<count;++k){auto* q=p+40*k;
        if((q[7]&0xfc)!=0x2c){if(collapseLines<40){++collapseLines;log("MUZZLE_COLLAPSE_SKIP kind=%04x code=%02x (not an FT4)",unsigned(h->kind),q[7]);}continue;}
        const auto xy0=read<std::uint32_t>(q,8);std::memcpy(q+16,&xy0,4);std::memcpy(q+24,&xy0,4);std::memcpy(q+32,&xy0,4);++collapsedPackets;}
    if(collapseLines<40){static std::unordered_set<std::uint32_t> seen;const auto key=std::uint32_t(h->kind)|(std::uint32_t(count)<<16);
        if(seen.insert(key).second){++collapseLines;log("MUZZLE_COLLAPSE kind=%04x count=%d code=%02x tpage=%04x clut=%04x (native smoke sprite hidden, HD smoke drawn)",unsigned(h->kind),count,p[7],read<std::uint16_t>(p,22),read<std::uint16_t>(p,14));}}
}
// proxy.cpp drawUiPacket, every SPRT/FT4/FT3/GT4 the renderer draws: a quad of the native gun-flash page (tpage 3E 4-bit,
// CLUT 7C11, core bank 1: u 0..247, v 168..215) is drawn from native-flash.png (8x, same layout) instead of the TEXU
// palette-33 upscale - same vertices, UVs, colour and additive blend as the game's quad. In the fps60 in-between frame a
// flash quad of its first build is drawn from an empty texture (nothing drawn).
static bool flashBinding(const std::uint8_t* p,unsigned tpage,unsigned clut,float minU,float minV,float maxU,float maxV,re3ui::Binding& binding){
    if(!enabled||!installed||!flashTexture||clut!=0x7c11u||(tpage&0x19fu)!=0x1eu||minV<168.f||maxV>216.f||minU<0.f||maxU>248.f)return false;
    bool skip=false;
    if(fps60Pass&&!newPackets.empty()&&accessible(p,40)){const auto h=packetHash(p);for(const auto& n:newPackets)if(n.hash==h){skip=true;break;}}
    binding=re3ui::Binding{};binding.assetId=skip?"fx-muzzle-flash-new":"fx-muzzle-native-flash";
    binding.pngPath=skip?"model-pack/fx-muzzle/native-flash-off.png":"model-pack/fx-muzzle/native-flash.png";
    binding.originalW=256;binding.originalH=48;binding.offsetU=0;binding.offsetV=-168;binding.scale=8;
    if(skip)++flashSkipped;else ++flashQuads;
    if(trace&&!fps60Pass&&(p[7]&0xfcu)==0x2cu&&accessible(p,40)){   // the UV rects the game really draws from the page
        static std::uint32_t seen[64];static unsigned seenCount=0;
        const std::uint32_t key=(unsigned(minU)&0xffu)|((unsigned(minV)&0xffu)<<8)|((unsigned(maxU)&0xffu)<<16)|((unsigned(maxV)&0xffu)<<24);
        bool known=false;for(unsigned i=0;i<seenCount;++i)if(seen[i]==key){known=true;break;}
        if(!known&&seenCount<64){seen[seenCount++]=key;
            log("MUZZLE_FLASH_RECT uv=%.0f,%.0f-%.0f,%.0f uv0=%u,%u uv1=%u,%u uv2=%u,%u uv3=%u,%u xy0=%d,%d xy3=%d,%d rgb=%02x%02x%02x",minU,minV,maxU,maxV,
                p[12],p[13],p[20],p[21],p[28],p[29],p[36],p[37],int(read<std::int16_t>(p,8)),int(read<std::int16_t>(p,10)),int(read<std::int16_t>(p,32)),int(read<std::int16_t>(p,34)),p[4],p[5],p[6]);}}
    return true;
}

// ---------------------------------------------------------------- drawing (smoke)
inline void cellRect(unsigned col,unsigned row,unsigned cols,unsigned rows,float out[4]){
    const float e=0.5f/1024.f;out[0]=float(col)/cols+e;out[1]=float(row)/rows+e;out[2]=float(col+1)/cols-e;out[3]=float(row+1)/rows-e;}
struct Item{Vec3 anchor{};std::vector<re3model::SkinnedVertex> vertices;std::vector<std::uint32_t> indices,tint;unsigned material=0;int bucket=-1;};
inline void sprite(Item& item,const Vec3& right,const Vec3& down,const Vec3& at,float width,float height,float angle,float ax,float ay,const float cell[4],std::uint32_t argb){
    blood::Item b;blood::sprite(b,right,down,at,width,height,angle,ax,ay,cell,argb);
    const auto base=std::uint32_t(item.vertices.size());item.vertices.insert(item.vertices.end(),b.vertices.begin(),b.vertices.end());
    item.tint.insert(item.tint.end(),b.tint.begin(),b.tint.end());for(const auto i:b.indices)item.indices.push_back(base+i);
}
// tag: a QA capture of the frame that draws this build (set on its first job, as the SEQ- captures of drawModelJob)
static void draw(const std::string& tag){
    if(puffs.empty()&&tag.empty())return;
    auto asset=pack();if(!asset||!modelRenderer.resetDevice(gameDevice))return;
    const double start=modelClockMs();
    const auto gte=read<const void*>(reinterpret_cast<void*>(gameBase+0x1349c8));if(!accessible(gte,0x100))return;
    re3bridge::ActorContext context;context.camera=read<re3bridge::Matrix32>(reinterpret_cast<void*>(gameBase+0x11c398));
    std::memcpy(context.gte.data(),gte,context.gte.size());
    auto base=actorProjection(context);
    if(!re3model::SetNativeProjectionRegisters(base,context.gte.data(),context.gte.size()))return;
    base.shade=nullptr;base.shadeContext=nullptr;
    const auto& cam=base.camera;
    const Vec3 right=re3model::Normalize({cam.m[0],cam.m[1],cam.m[2]}),down=re3model::Normalize({cam.m[4],cam.m[5],cam.m[6]});
    std::vector<Item> items;
    for(const auto& p:puffs){
        const float t=(std::min)(0.999f,float(tick-p.born)/float(p.life*debugSlow));
        const float sheetPos=t*15.999f;const unsigned f0=unsigned(sheetPos),f1=(std::min)(15u,f0+1);const float w=sheetPos-float(f0);
        const float age=float(tick-p.born)/float(debugSlow);
        // appears with the flash's second tick, denser for longer (round 2), gone at the end of its life
        const float fadeIn=(std::min)(1.f,(std::max)(0.f,age-0.5f)/1.5f),alpha=p.alpha*fadeIn*std::pow(1.f-t,0.45f);
        const float size=p.size*(0.85f+0.4f*t);
        // the room's light on the shooter (a surface toward the camera), lit by the flash for the first ticks
        std::uint32_t light=blood::lifted(blood::scaled(p.light,0.75f),0x30);
        if(age<2.5f){const float k=1.f-age/2.5f;std::uint32_t warm=light&0xff000000u;const unsigned add[3]={0x18,0x38,0x58};
            for(unsigned c=0;c<3;++c){const unsigned v=(light>>(c*8))&255u;warm|=std::uint32_t((std::min)(255u,v+unsigned(float(add[c])*k)))<<(c*8);}light=warm;}
        for(unsigned layer=0;layer<2;++layer){const float a=alpha*(layer?w:1.f-w);if(a<0.01f)continue;
            Item item;item.material=kSmokeMaterial+p.sheet;item.anchor=p.pos;
            float cell[4];const unsigned f=layer?f1:f0;cellRect(f%4,f/4,4,4,cell);if(p.mirror)std::swap(cell[0],cell[2]);
            sprite(item,right,down,p.pos,size,size,p.roll+p.spin*age,0.5f,0.5f,cell,blood::withAlpha(light,a));
            const auto c=re3model::TransformPoint(cam,p.pos);item.bucket=int((std::max)(1.f,c.z-40.f))>>5;items.push_back(std::move(item));}
    }
    if(!tag.empty()){   // QA: an invisible quad at the player carries the capture tag when nothing else is drawn
        const auto* player=reinterpret_cast<const std::uint8_t*>(gameBase+0x65cbf4);Item item;item.material=kSmokeMaterial;
        item.anchor={float(read<std::int32_t>(player,0x34)),float(read<std::int32_t>(player,0x38))-900.f,float(read<std::int32_t>(player,0x3c))};
        float cell[4];cellRect(15,3,4,4,cell);sprite(item,right,down,item.anchor,8.f,8.f,0.f,0.5f,0.5f,cell,0x00808080u);
        const auto c=re3model::TransformPoint(cam,item.anchor);item.bucket=int((std::max)(1.f,c.z))>>5;items.push_back(std::move(item));}
    if(items.empty())return;
    // per ordering table (45ADC0 by the item's anchor): one geometry, its runs grouped by bucket, far first (blended)
    re3model::Mesh materials;std::vector<re3model::MaterialTextures> smokeTextures;
    for(unsigned k=0;k<kMaterials;++k){auto m=asset->mesh.materials[k];m.flags=re3model::TwoSided|re3model::AlphaBlend;materials.materials.push_back(m);smokeTextures.push_back(asset->textures[k]);}
    const auto chooseTable=reinterpret_cast<ChooseTable>(gameBase+0x5adc0);
    struct Piece{std::uint32_t* table;std::uint16_t bucket;std::vector<re3model::DrawVertex> vertices;unsigned material;};
    std::vector<Piece> pieces;
    for(auto& item:items){if(item.vertices.empty())continue;
        const auto camera=re3model::TransformPoint(cam,item.anchor);if(camera.z<256.f||camera.z>32767.f)continue;
        const std::int32_t position[3]={std::int32_t(item.anchor.x),std::int32_t(item.anchor.y),std::int32_t(item.anchor.z)};
        auto* table=chooseTable(position);if(!table||!accessible(table,1024*4,true))continue;
        re3model::Mesh mesh;mesh.vertices.resize(item.vertices.size());mesh.indices=item.indices;
        re3model::Material m=materials.materials[item.material];m.firstIndex=0;m.indexCount=std::uint32_t(item.indices.size());mesh.materials.push_back(m);
        auto projection=base;projection.vertexTint=&item.tint;std::uint16_t fixed=std::uint16_t((std::min)(1023,(std::max)(0,item.bucket)));
        projection.depth=blood::fixedBucket;projection.depthContext=&fixed;
        re3model::FrameGeometry geometry;std::string error;
        if(!re3model::PrepareFrame(mesh,item.vertices,projection,geometry,&error)||geometry.vertices.empty())continue;
        for(const auto& slice:geometry.slices)for(unsigned r=0;r<slice.runCount;++r){const auto& run=geometry.runs[slice.firstRun+r];
            Piece piece;piece.table=table;piece.bucket=slice.bucket;piece.material=item.material;
            piece.vertices.assign(geometry.vertices.begin()+run.firstVertex,geometry.vertices.begin()+run.firstVertex+run.vertexCount);pieces.push_back(std::move(piece));}
    }
    if(pieces.empty())return;
    std::stable_sort(pieces.begin(),pieces.end(),[](const Piece& a,const Piece& b){return a.table!=b.table?a.table<b.table:a.bucket>b.bucket;});
    bool tagged=false;
    for(std::size_t i=0;i<pieces.size();){
        std::uint32_t* table=pieces[i].table;re3model::FrameGeometry merged;
        for(;i<pieces.size()&&pieces[i].table==table;++i){const auto& piece=pieces[i];
            if(merged.slices.empty()||merged.slices.back().bucket!=piece.bucket)merged.slices.push_back({piece.bucket,std::uint32_t(merged.runs.size()),0});
            merged.runs.push_back({std::uint32_t(merged.vertices.size()),std::uint32_t(piece.vertices.size()),piece.material});++merged.slices.back().runCount;
            merged.vertices.insert(merged.vertices.end(),piece.vertices.begin(),piece.vertices.end());}
        HRESULT hr;auto gpu=modelRenderer.upload(materials,std::move(merged),smokeTextures,false,&hr);if(!gpu){once("muzzle-upload","HD muzzle smoke upload failed this frame");return;}
        std::vector<re3bridge::BucketJob> jobs;jobs.reserve(gpu->slices.size());
        for(std::size_t n=0;n<gpu->slices.size();++n){auto job=std::make_shared<ModelJob>();job->geometry=gpu;job->slice=n;job->name="muzzle-smoke";
            if(!tagged&&!tag.empty()){job->captureTag=tag;tagged=true;}
            jobs.push_back({gpu->slices[n].bucket,reinterpret_cast<std::uintptr_t>(job.get()),job});}
        if(!re3bridge::EnqueueBatch(table,1024,jobs))once("muzzle-enqueue","HD muzzle smoke skipped for one ordering table");
    }
    drawMs+=modelClockMs()-start;++drawFrames;
}

// ---------------------------------------------------------------- QA (debug_warp): pilot.command muzzle-test / muzzle-capture / muzzle-mid
// muzzle-test: the next gun of the list into the equipped inventory slot (99 rounds), loaded as the inventory EQUIP does
// (452620 + 452310, like diagnostic equip-m4), and a capture burst at its next flash; muzzle-capture: only the burst;
// muzzle-mid: the burst plus the fps60 trio (real / in-between / real) around the flash. In F8 (original) the burst
// captures are requested directly (nothing of ours is drawn there to carry the tag).
static const std::uint8_t kTestGuns[]={0x03,0x04,0x05,0x06,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f,0x10,0x12,0x13};
static const unsigned kBurstTicks[]={0,1,2,3,4,6,9,13,18,24,31,39,48};
inline bool equipNext(){
    auto* player=reinterpret_cast<std::uint8_t*>(gameBase+0x65cbf4);
    if(read<std::uint16_t>(reinterpret_cast<void*>(gameBase+0x661fa6))>7||!accessible(player,0x180,true)||read<std::uint8_t>(player,8)<15)return false;
    if((read<std::uint32_t>(reinterpret_cast<void*>(gameBase+0x661f5c))&0x100u)||(read<std::uint32_t>(reinterpret_cast<void*>(gameBase+0x65c880))&(0x400000u|0x10000u|0x80u))||
       (read<std::uint32_t>(reinterpret_cast<void*>(gameBase+0x65c864))&0x540u))return false;   // a menu, a door, aiming or firing: next tick
    auto* inv=read<std::uint8_t*>(reinterpret_cast<void*>(gameBase+0x6623e4));
    if(!accessible(inv,0x12b,true)||!inv[0x12a]){log("MUZZLE_TEST rejected: no inventory");return true;}
    const std::uint8_t weapon=kTestGuns[testIndex++%(sizeof(kTestGuns))];
    unsigned slot=inv[0x128];if(slot>=inv[0x12a])slot=0;
    const auto attributes=reinterpret_cast<const std::uint8_t*>(gameBase+0x12ac50)+weapon*4;
    inv[slot*4]=weapon;inv[slot*4+1]=99;inv[slot*4+2]=read<std::uint8_t>(attributes,3);inv[slot*4+3]=0;
    inv[0x128]=std::uint8_t(slot);inv[0x129]=weapon;player[0x46]=weapon;
    reinterpret_cast<void(__cdecl*)(void*)>(gameBase+0x52620)(player);
    reinterpret_cast<void(__cdecl*)(void*)>(gameBase+0x52310)(player);
    log("MUZZLE_TEST equip weapon=%02x slot=%u (native loaders 452620/452310)",weapon,slot);return true;
}
inline std::string runTests(){std::string tag;
    const LONG request=InterlockedExchange(&muzzleTestRequest,0);
    if(request){pendingTest=request;burstArmed=0;burstStart=0;}
    if(pendingTest==1&&!equipNext())return tag;
    if(pendingTest){burstArmed=int(pendingTest);burstStart=0;pendingTest=0;}
    if(burstArmed&&burstStart){const unsigned age=tick-burstStart;
        // muzzle-mid: the fps60 trio (next real frame, in-between, real) armed at the flash tick (odd bursts) or one tick
        // later (even bursts)
        if(burstArmed==3&&age==(burstSerial&1u?0u:1u)){fps60CaptureSequence=3;log("MUZZLE_TEST fps60 capture (real / in-between / real) %s age=%u",burstLabel.c_str(),age);}
        for(unsigned k=0;k<sizeof(kBurstTicks)/sizeof(kBurstTicks[0]);++k)if(kBurstTicks[k]*debugSlow==age){
            if(burstArmed==3&&age<3)break;   // the fps60 trio is taken around the flash instead
            tag="SEQ-"+burstLabel+"-t"+std::to_string(kBurstTicks[k]);break;}   // SEQ-: drawModelJob captures it with pilot.ini auto_capture=0 too
        if(age>kBurstTicks[sizeof(kBurstTicks)/sizeof(kBurstTicks[0])-1]*debugSlow){log("MUZZLE_TEST burst %s done",burstLabel.c_str());burstArmed=0;burstStart=0;}}
    if(!tag.empty()&&!active()){captureTag=tag;captureRequested=true;tag.clear();}   // F8: no job of ours to carry it
    return tag;
}

// ---------------------------------------------------------------- per tick (the effect loop hook)
static void update(){
    const std::uint32_t now=(std::uint32_t(read<std::uint16_t>(reinterpret_cast<void*>(gameBase+0x661fa6)))<<16)|read<std::uint16_t>(reinterpret_cast<void*>(gameBase+0x661fa8));
    if(now!=room){if(room!=~0u&&(flashes||puffsMade))log("MUZZLE_CLEAR room=%x shots=%llu puffs=%llu hidden=%llu collapsed=%llu flash_quads=%llu flash_new_skipped=%llu flash_pinned=%llu draw_ms=%.3f/frame",now,(unsigned long long)flashes,(unsigned long long)puffsMade,
            (unsigned long long)hiddenEffects,(unsigned long long)collapsedPackets,(unsigned long long)flashQuads,(unsigned long long)flashSkipped,(unsigned long long)pinnedPackets,drawFrames?drawMs/drawFrames:0.0);
        room=now;puffs.clear();hidden.clear();flashFx.clear();newPackets.clear();}
    ++tick;
    const std::string tag=debugWarp?runTests():std::string();
    if(lightTraceActor&&tick-lightTraceBorn<=8){const unsigned age=tick-lightTraceBorn;if(age==0||age==1||age==2||age==4||age==8)log("MUZZLE_LIGHT shot=%u age=%u actor=%p light=%06x",lightTraceShots,age,lightTraceActor,lightOf(lightTraceActor)&0xffffffu);}
    if(!enabled){puffs.clear();if(!tag.empty()&&active()&&pack())draw(tag);return;}
    packReady=active()&&bool(pack());
    // smoke: pushed out along the barrel, slowed by the air, rising, slowly turning
    if(tick%debugSlow==0)for(auto& p:puffs){p.vel=p.vel*0.86f;p.vel.y=(std::max)(-7.f,p.vel.y-0.45f);p.pos=p.pos+p.vel;}
    puffs.erase(std::remove_if(puffs.begin(),puffs.end(),[](const Puff& p){return tick-p.born>=p.life*debugSlow;}),puffs.end());
    hidden.erase(std::remove_if(hidden.begin(),hidden.end(),[](const Hidden& h){return tick-h.tick>150;}),hidden.end());
    {const auto serial=re3bridge::BuildSerial();while(!newPackets.empty()&&serial-newPackets.front().serial>6)newPackets.pop_front();}
    if(tick%900==0&&(flashes||puffsMade)){log("MUZZLE_STATS shots=%llu puffs=%llu live_puffs=%zu hidden=%llu collapsed=%llu flash_quads=%llu flash_new_skipped=%llu flash_pinned=%llu draw_ms=%.3f/frame",(unsigned long long)flashes,(unsigned long long)puffsMade,puffs.size(),
        (unsigned long long)hiddenEffects,(unsigned long long)collapsedPackets,(unsigned long long)flashQuads,(unsigned long long)flashSkipped,(unsigned long long)pinnedPackets,drawFrames?drawMs/drawFrames:0.0);}
    if(active())draw(tag);
}

// ---------------------------------------------------------------- install
using Spawn=int(__cdecl*)(int,int,const void*,const std::int16_t*,const void*);
static Spawn originalMineSpawn=nullptr;
static int __cdecl mineSpawnHook(int kind,int key,const void* hand,const std::int16_t* offset,const void* extra){
    const int result=originalMineSpawn(kind,key,hand,offset,extra);
    onSpawn(false,kind,key,hand,offset,offset,result,_ReturnAddress());
    return result;
}
static void install(){
    const std::string ini=root+"\\pilot.ini";
    enabled=GetPrivateProfileIntA("pilot","muzzle_fx",1,ini.c_str())!=0;
    trace=GetPrivateProfileIntA("pilot","muzzle_trace",0,ini.c_str())!=0;debugWarp=GetPrivateProfileIntA("pilot","debug_warp",0,ini.c_str())!=0;
    debugSlow=(std::max)(1u,(std::min)(16u,unsigned(GetPrivateProfileIntA("pilot","muzzle_debug_slow",1,ini.c_str()))));
    for(std::size_t i=0;i<kSiteCount;++i)siteAt[std::uintptr_t(gameBase+kSites[i].rva)+5]=i;
    if(!enabled){log("MUZZLE_FX disabled by pilot.ini muzzle_fx=0 (native muzzle flashes and smoke)%s",trace?", spawn trace on":"");return;}
    originalMineSpawn=reinterpret_cast<Spawn>(gameBase+0x3d370);
    unsigned mine=0;
    for(const auto site:kMineSites){re3bridge::detail::Patch patch;
        if(re3bridge::detail::BuildCallPatch(patch,gameBase+site,gameBase+0x3d370,reinterpret_cast<void*>(mineSpawnHook))&&re3bridge::detail::WritePatch(patch,true))++mine;}
    installed=true;loadConfig();
    log("MUZZLE_FX_INSTALL_OK round 2: native flash on the HD page (fixed in the world, native size), HD smoke; sites=%zu, mine thrower calls wrapped %u/%zu, debug_slow=%u trace=%d",kSiteCount,mine,sizeof(kMineSites)/sizeof(kMineSites[0]),debugSlow,int(trace));
}
} // namespace muzzle
