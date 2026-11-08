#pragma once
// ---------------------------------------------------------------- fx 2026-11-08 nemesis-cloud: HD purple-grey gas out of Nemesis' body
// Included at the end of extra_model_integration.h after blood_fx.h, inside namespace re3extra (read/accessible/active/once/
// mutex, getModel, modelRenderer, actorProjection, actorRuntime, effectGeneration, kWallSplit, the native OT helpers).
//
// User request 2026-11-08 (qa/fx-2026-11-08/USER-REPORT.ru.txt): "like the blood, HD effects ... of the purple-grey cloud out
// of Nemesis' body". What the original does (runs qa/fx-2026-11-08/harness/runs/*nc-*, effect_trace=1):
//  - Nemesis' sprites are the common blood effects (effect set 0 = key low byte 00, templates 7310B8/73129C/730F84/7310E4;
//    owner actor at effect +0xBC, the part matrix it was spawned on at +0xC0) in palette +0xB3 = 4: TEXU tpage 1E / clut
//    7911 = the purple palette (ui-pack TEXU palette-09: CHI blood blots in purple). His downing (routine xx0103, life <= 0, ~130 game frames = 4.3 s
//    until he lies, routine 0007/0107) throws ~33 of them a second out of his upper body: 0C00 class 2B (12-13 frames,
//    sometimes 26/39), 1700 class 00 (12), 0500 class 01 (18-26) and 0D00 class 00 on the floor (12). In HD (TEXU HD
//    palette 9, semi-transparent average blend) that reads as a soft purple-grey cloud.
//    Palette = effect +0xB3: 0 = clut 7811 the ordinary red hit blood (his hits bleed red like anybody's: kept for
//    blood_fx), 4 = clut 7911 purple (the downing), 2 = clut 7891 grey-beige (with 4 in his tentacle kill of Jill, routine
//    xx0D01, at her head while he is alive: not his body, left as it was).
//  - Since the blood round (07.11) blood_fx.h collapses every 0C00 class 2B billboard and hides its texture at the renderer
//    copy: Nemesis' purple sprites vanished and HD RED blood sprays replaced them (BLOOD_NATIVE_REPLACED actor=Nemesis).
//  - Lying (routine 0107, life -1) his SHADOW (actor+0x160, as the zombies' blood pool in blood_fx.h) turns into a purple
//    stain: colour 808080 -> 10AF10 (subtractive tpage D9: green taken away = magenta/purple), size 800x900 -> 1400x1500
//    in ~75 frames, then it stays. In HD it is a soft magenta glow spreading out of the body over the floor and over the
//    HD body itself (run nc-hd1, cloud-states.jsonl) - the purple cloud out of his body that stays.
// Here: those effects are Nemesis', not blood. Their FT4 packets are hidden at the renderer copy (chained filter), blood_fx
// leaves them alone (one call point in collapseNativeSpray), and each native effect emits an HD gas puff at its place;
// the purple stain is not drawn (flag 4 off, as blood_fx does with the red one: call point in blood::shadowHook + every
// frame here) and low gas seeps out of the body over the stain's area while it grows, then a thin haze stays over it:
// 4x4 sheet model-pack/fx-nemesis-cloud/cloud.png (row = puff variant, column = life stage dense -> opened -> wisps ->
// almost gone, cross-faded), rising, spreading, turning, thinning; floor effects make low spreading puffs. Lit by Nemesis'
// native light (as the HD sprays), drawn into the native ordering tables like the blood (one bucket per puff, the R20F
// window wall rule of kWallSplit). F8 / pilot.ini nemesis_cloud_fx=0: native sprites (and blood_fx's old handling).
// fx2 2026-11-08 (round 2, user: "the old effect is still there out of Nemesis' arm ... find where else, e.g. the shoulder"):
// the same sprites (set 0, keys 0C00/1700/0500/0D00) in palettes 2 (clut 7891, grey-beige = TEXU palette 5) and 4 (clut 7911,
// purple) are thrown by the LIVING Nemesis too (his tentacle attacks; see REPORT round 2 for where). blood_fx turned their 0C00
// into red HD sprays and hid the rest (DD7C526B). Now: every such sprite of a living Nemesis gets an HD gas puff (material 1
// arm.png: rows 0-1 purple, 2-3 grey-beige by its palette) that sits exactly on the native sprite for its whole life (same
// points, same flight) and fades a few frames after it; the native ones are hidden while one lives. The downing gas and the
// stain gas (round 1) are unchanged. nemesis_cloud_fx=0 / F8: all native (blood_fx leaves Nemesis' palette 2/4 sprites alone).
// fx5 2026-11-08 (round 5, user: "you fixed Nemesis' effect badly. Leave it as it was if you can't, just raise the resolution
// of its original effect a little"): the living Nemesis' sprites are the NATIVE ones again by default - the game's own
// effects, positions, frames, timing and 50/50 blend - drawn from an 8x texture of the same TEXU frames instead of the
// ui-pack TEXU palette-05/09 4x upscale: proxy.cpp drawUiPacket asks armBinding() for every quad (one line, as the muzzle's
// flashBinding); tpage 1E, CLUT 7891 (palette 2, grey-beige) or 7911 (palette 4, purple), u 0..239, v 0..87 (the CHI_00
// block) -> model-pack/fx-nemesis-cloud/native-arm-p5.png / -p9.png (models/fx-2026-11-08/nemesis-cloud/make_arm_texture.py:
// a computed smooth-label vectorisation of the native indices, no generation). pilot.ini nemesis_cloud_arm=0 (default) that,
// =1 the round-2..4 HD gas on them. The downing gas / stain (round 1) is unchanged. Blood never turns them red (owns()).
namespace nemesis_cloud {
using re3model::Vec3;
static bool enabled=true;            // pilot.ini nemesis_cloud_fx
static unsigned debugSlow=1;         // pilot.ini nemesis_cloud_debug_slow (QA captures): puffs N times slower
static bool filterInstalled=false;
static re3bridge::detail::PacketFilter previousFilter=nullptr;
constexpr std::size_t kMaxPuffs=180;
enum Kind:std::uint8_t{Spray=0,Splat=1,Drip=2,Floor=3,Seep=4,Haze=5,Arm=6,Unknown=255};
struct Puff{Vec3 pos{},vel{};float size=0,grow=0,roll=0,spin=0,alpha=1,rise=0;std::uint8_t row=0,kind=0,palette=4,material=0,armKey=0;std::uint16_t life=45;std::uint32_t born=0,colour=0xffa0a0a0u;
    std::uintptr_t effect=0;std::uint64_t generation=0;Vec3 last{};bool following=false;};
static std::vector<Puff> puffs;
struct Seen{std::uintptr_t effect=0;std::uint64_t generation=0;std::uint32_t tick=0;};
static std::vector<Seen> seen;
static std::uint32_t textures[8]{};static unsigned textureCount=0;   // tpage<<16|clut of Nemesis' sprites in this room
static std::uint32_t room=~0u,tick=0,seedState=0x2545f491u;
static bool nemesisHere=false,nemesisDown=false,packOk=false,armPack=false,armActive=false;
static std::uint64_t armSeen=0,armSpawned=0;static unsigned armLogged=0;
static std::uint64_t spawned=0,nativeSeen=0,hidden=0;
static unsigned unknownLogged=0,spawnLogged=0;
static double perfSum=0,perfMax=0;static unsigned perfFrames=0,perfQuads=0;   // NEMESIS_CLOUD_PERF (frames with puffs)
inline float rnd(){seedState=seedState*1664525u+1013904223u;return float((seedState>>8)&0xffff)/65535.f;}
inline float rnd(float a,float b){return a+(b-a)*rnd();}
inline bool nemesisModel(std::uint8_t m){return m>=0x34&&m<=0x3b;}

inline std::shared_ptr<ModelAsset> pack(){
    auto asset=getModel("fx-nemesis-cloud");
    if(!asset||asset->mesh.materials.empty()||asset->textures.empty()){once("nemesis-cloud-pack","HD Nemesis cloud pack unavailable (model-pack/fx-nemesis-cloud): native sprites kept");return nullptr;}
    return asset;
}
inline bool ready(){return enabled&&active()&&packOk;}
// a sprite of Nemesis' own set: set 0 (key low byte 00), palette +0xB3 != 0 (0 = the red hit blood), owner +0xBC a Nemesis actor
inline const std::uint8_t* nemesisSprite(const void* effect){
    if(!accessible(effect,0xd4))return nullptr;
    if(read<std::uint16_t>(effect,0x28)&0xffu)return nullptr;
    if(!(read<std::uint8_t>(effect,0xb3)&15u))return nullptr;
    const auto* owner=read<const std::uint8_t*>(effect,0xbc);
    if(!accessible(owner,0x100)||!nemesisModel(owner[0x4a]))return nullptr;
    return owner;
}
inline Kind kindOf(std::uint16_t key){switch(key){case 0x0c00:return Spray;case 0x1700:return Splat;case 0x0500:return Drip;case 0x0d00:return Floor;}return Unknown;}
// one of Nemesis' own non-blood sprites out of his body while he goes down: set 0 (blood effects), owner +0xBC a Nemesis
// actor with life <= 0 (downing xx0103 at -7, down 0007/0107/0207 at -1), palette +0xB3 not 0 (0 = the red hit blood)
inline const std::uint8_t* ownerOf(const void* effect){
    if(!accessible(effect,0xd4))return nullptr;
    const auto key=read<std::uint16_t>(effect,0x28);if(key&0xffu)return nullptr;
    if(!(read<std::uint8_t>(effect,0xb3)&15u))return nullptr;
    const auto* owner=read<const std::uint8_t*>(effect,0xbc);
    if(!accessible(owner,0x100)||!nemesisModel(owner[0x4a])||read<std::int16_t>(owner,0xcc)>0)return nullptr;
    return owner;
}
// blood_fx.h collapseNativeSpray: Nemesis' palette 2/4 sprites are not blood (no red replacement, texture not taken as blood's).
// fx2: of the living Nemesis too, and with nemesis_cloud_fx=0 too (then they stay native, as in the original).
inline bool owns(const void* effect){
    if(!active())return false;
    const auto* owner=nemesisSprite(effect);return owner&&kindOf(read<std::uint16_t>(effect,0x28))!=Unknown;
}
inline void noteTexture(const void* effect){
    const std::uint32_t id=(std::uint32_t(read<std::uint16_t>(effect,0x46))<<16)|read<std::uint16_t>(effect,0x44);
    for(unsigned k=0;k<textureCount;++k)if(textures[k]==id)return;
    if(textureCount<8){textures[textureCount++]=id;log("NEMESIS_CLOUD_TEXTURE tpage=%04x clut=%04x (Nemesis' purple sprites hidden at the renderer copy while the HD cloud is drawn)",unsigned(id>>16),unsigned(id&0xffffu));}
}
// renderer FT4 copy (41FAB1), chained before blood_fx's filter: semi-transparent FT4 on Nemesis' sprite texture while a
// Nemesis is down (his tentacle kill of Jill, alive, uses the same textures: left to blood_fx as before)
inline bool packetFilter(const void* packet){
    if(textureCount&&(nemesisDown||armActive)&&ready()){
        const auto* p=static_cast<const std::uint8_t*>(packet);
        if((p[7]&0xfe)==0x2e){const std::uint32_t id=(std::uint32_t(read<std::uint16_t>(p,22))<<16)|read<std::uint16_t>(p,14);
            for(unsigned k=0;k<textureCount;++k)if(textures[k]==id){++hidden;return true;}}
    }
    return previousFilter?previousFilter(packet):false;
}

// ---------------------------------------------------------------- emission
inline std::uint32_t lightOf(const void* actor){
    std::uint32_t light=0xffa0a0a0u;
    if(actor){const auto found=actorRuntime.find(const_cast<void*>(actor));if(found!=actorRuntime.end())light=found->second.bloodLightView;}
    return light;
}
inline void addPuff(Puff p){
    if(puffs.size()>=kMaxPuffs)puffs.erase(puffs.begin());
    puffs.push_back(p);++spawned;
}
inline void emit(Kind kind,const Vec3& at,const Vec3& from,std::uint32_t light,std::uintptr_t effect,std::uint64_t generation,std::uint8_t palette=4){
    // the native downing throws pairs of sprites at one point: one puff there (fx2: one per palette)
    for(const auto& q:puffs)if(q.born==tick&&q.kind==kind&&q.palette==palette){const Vec3 d=q.pos-at;if(re3model::Dot(d,d)<150.f*150.f)return;}
    Puff p;p.kind=kind;p.palette=palette;p.pos=at;p.last=at;p.born=tick;p.colour=light;p.row=std::uint8_t(rnd()*3.99f);p.roll=rnd()*6.2831853f;p.spin=rnd(-0.035f,0.035f);
    p.effect=effect;p.generation=generation;p.following=effect!=0;
    Vec3 out=at-from;out.y*=0.35f;out=re3model::Dot(out,out)>1.f?re3model::Normalize(out):Vec3{rnd(-1.f,1.f),0,rnd(-1.f,1.f)};
    switch(kind){
    case Spray:p.size=rnd(520.f,700.f);p.grow=rnd(1.5f,2.1f);p.life=std::uint16_t(rnd(52.f,72.f));p.alpha=rnd(0.62f,0.8f);p.vel=out*rnd(5.f,10.f);p.rise=rnd(1.2f,2.2f);break;
    case Splat:p.size=rnd(420.f,580.f);p.grow=rnd(1.4f,1.9f);p.life=std::uint16_t(rnd(44.f,60.f));p.alpha=rnd(0.5f,0.68f);p.vel=out*rnd(3.f,7.f);p.rise=rnd(1.f,2.f);break;
    case Drip:p.size=rnd(300.f,420.f);p.grow=rnd(1.6f,2.2f);p.life=std::uint16_t(rnd(38.f,52.f));p.alpha=rnd(0.42f,0.58f);p.vel=out*rnd(2.f,5.f);p.rise=rnd(0.6f,1.4f);break;
    case Floor:p.size=rnd(560.f,820.f);p.grow=rnd(1.7f,2.4f);p.life=std::uint16_t(rnd(60.f,84.f));p.alpha=rnd(0.45f,0.6f);p.vel={rnd(-6.f,6.f),0,rnd(-6.f,6.f)};p.rise=rnd(0.4f,0.9f);p.pos.y-=110.f;p.following=false;break;
    // out of the downed body over the stain (`from` = the stain centre): low, spreading outward, purple rows mostly
    case Seep:p.size=rnd(520.f,820.f);p.grow=rnd(1.5f,2.1f);p.life=std::uint16_t(rnd(80.f,110.f));p.alpha=rnd(0.32f,0.48f);out.y=0;p.vel=out*rnd(4.f,8.f);p.rise=rnd(0.25f,0.6f);p.following=false;
        p.row=std::uint8_t(rnd()<0.75f?(rnd()<0.5f?0:2):(rnd()<0.5f?1:3));break;
    case Haze:p.size=rnd(680.f,1050.f);p.grow=rnd(1.3f,1.7f);p.life=std::uint16_t(rnd(100.f,140.f));p.alpha=rnd(0.24f,0.34f);out.y=0;p.vel=out*rnd(1.f,3.f);p.rise=rnd(0.15f,0.4f);p.following=false;
        p.row=std::uint8_t(rnd()<0.6f?(rnd()<0.5f?0:2):(rnd()<0.5f?1:3));break;
    default:return;}
    addPuff(p);
}
// fx2: a sprite of the living Nemesis -> a gas puff ON the native sprite for its whole life (step() moves it with the sprite, no
// damping) plus a short fade; purple (palette 4) or grey-beige (palette 2) rows of arm.png; size ~1.5x the native sprite
// fx4 2026-11-08: user "более разреженный эффект фиолетового тумана вокруг руки, сейчас очень густой" -> the purple (palette 4)
// arm puffs: pilot.ini nemesis_cloud_arm_purple_density (percent, default 45) scales their opacity, they are 0.8x smaller, and
// puffs born in the same tick closer than 220 units merge (was 60), so the hand gets separate wisps instead of one thick fog
// fx5: 0 = the native arm sprites with the 8x texture (armBinding), 1 = the HD gas puffs on them (rounds 2-4)
inline int armMode(){
    static int value=-1;
    if(value<0){value=GetPrivateProfileIntA("pilot","nemesis_cloud_arm",0,(root+"\\pilot.ini").c_str())!=0?1:0;
        log("NEMESIS_CLOUD_ARM_MODE %d (%s)",value,value?"HD gas on the living Nemesis' sprites":"native sprites, 8x texture native-arm-p5/p9");}
    return value;
}
inline int purpleDensity(){
    static int value=-1;
    if(value<0){value=(std::max)(5,(std::min)(100,int(GetPrivateProfileIntA("pilot","nemesis_cloud_arm_purple_density",45,(root+"\\pilot.ini").c_str()))));
        log("NEMESIS_CLOUD_ARM_PURPLE density=%d%% size=0.8 merge=220",value);}
    return value;
}
inline void emitArm(Kind native,const Vec3& at,std::uint32_t light,std::uintptr_t effect,std::uint64_t generation,std::uint8_t palette){
    const float merge=palette==4?220.f:60.f;   // fx4: purple wisps further apart
    for(const auto& q:puffs)if(q.born==tick&&q.kind==Arm&&q.palette==palette&&q.armKey==native){const Vec3 d=q.pos-at;if(re3model::Dot(d,d)<merge*merge)return;}
    Puff p;p.kind=Arm;p.armKey=std::uint8_t(native);p.material=1;p.palette=palette;p.pos=at;p.last=at;p.born=tick;p.colour=light;
    p.row=std::uint8_t((palette==2?2:0)+(rnd()<0.5f?1:0));p.roll=rnd()*6.2831853f;p.spin=rnd(-0.05f,0.05f);
    p.effect=effect;p.generation=generation;p.following=true;p.rise=0;
    // nominal native lives (trace): 0C00 12-13, 1700 12, 0500 18-26, 0D00 12 frames; +10 frames of fading after it
    switch(native){
    // v2 (run nc2-attack-new: grey 0C00 hid the purple): purple 1700/0500 larger and denser, grey 0C00 a little smaller
    case Spray:p.size=rnd(320.f,420.f);p.grow=rnd(1.4f,1.7f);p.life=24;p.alpha=rnd(0.6f,0.75f);break;
    case Splat:p.size=rnd(460.f,600.f);p.grow=rnd(1.4f,1.8f);p.life=22;p.alpha=rnd(0.8f,0.92f);break;
    case Drip:p.size=rnd(300.f,400.f);p.grow=rnd(1.3f,1.6f);p.life=32;p.alpha=rnd(0.72f,0.86f);break;
    case Floor:p.size=rnd(340.f,460.f);p.grow=rnd(1.5f,1.9f);p.life=24;p.alpha=rnd(0.5f,0.65f);p.pos.y-=60.f;p.last=p.pos;break;
    default:return;}
    if(palette==4){p.alpha*=float(purpleDensity())/100.f;p.size*=0.8f;}   // fx4: sparser purple fog around the hand
    addPuff(p);++armSpawned;
}
// every live effect of the pool (96 x 0xD4 at A46720) that is one of Nemesis' sprites: a puff at its first frame, the puff
// follows the native sprite while it lives (the native sprite flies out of the body), then drifts on its own
inline void scanEffects(){
    const auto start=gameBase+0x646720;
    seen.erase(std::remove_if(seen.begin(),seen.end(),[](const Seen& s){return tick-s.tick>120;}),seen.end());
    for(unsigned slot=0;slot<96;++slot){
        const auto* e=reinterpret_cast<const std::uint8_t*>(start+slot*0xd4);if(!accessible(e,0xd4))continue;
        if((read<std::uint16_t>(e,0x24)&0xa000u)!=0xa000u)continue;
        const auto* owner=nemesisSprite(e);if(!owner)continue;
        const auto key=read<std::uint16_t>(e,0x28);const Kind kind=kindOf(key);
        if(read<std::int16_t>(owner,0xcc)>0){   // fx2: the living Nemesis (tentacle attacks)
            if(kind==Unknown||!armPack||armMode()==0)continue;   // fx5: native sprites (armBinding draws them at 8x)
            armActive=true;
            const auto address=reinterpret_cast<std::uintptr_t>(e);
            const auto at=effectGeneration.find(address);const std::uint64_t generation=at==effectGeneration.end()?0:at->second;
            bool known=false;for(auto& sn:seen)if(sn.effect==address&&sn.generation==generation){sn.tick=tick;known=true;break;}
            if(known)continue;
            seen.push_back({address,generation,tick});++armSeen;noteTexture(e);
            const Vec3 pos{float(read<std::int32_t>(e,0x48)),float(read<std::int32_t>(e,0x4c)),float(read<std::int32_t>(e,0x50))};
            const std::uint8_t palette=std::uint8_t(read<std::uint8_t>(e,0xb3)&15u);
            emitArm(kind,pos,lightOf(owner),address,generation,palette);
            if(armLogged<16){++armLogged;log("NEMESIS_CLOUD_ARM key=%04x pal=%u at=%.0f,%.0f,%.0f routine=%08x life=%d light=%06x (HD puff on the living Nemesis' sprite)",key,unsigned(palette),pos.x,pos.y,pos.z,
                read<std::uint32_t>(owner,4),int(read<std::int16_t>(owner,0xcc)),lightOf(owner)&0xffffffu);}
            continue;
        }
        if(kind==Unknown){if(unknownLogged<12){++unknownLogged;log("NEMESIS_CLOUD_OTHER key=%04x class=%02x flags=%04x clut=%04x tpage=%04x model=%02x (Nemesis effect left native)",key,e[0],read<std::uint16_t>(e,0x24),read<std::uint16_t>(e,0x44),read<std::uint16_t>(e,0x46),owner[0x4a]);}continue;}
        const auto address=reinterpret_cast<std::uintptr_t>(e);
        const auto at=effectGeneration.find(address);const std::uint64_t generation=at==effectGeneration.end()?0:at->second;
        bool known=false;for(auto& s:seen)if(s.effect==address&&s.generation==generation){s.tick=tick;known=true;break;}
        if(known)continue;
        seen.push_back({address,generation,tick});++nativeSeen;noteTexture(e);
        const Vec3 pos{float(read<std::int32_t>(e,0x48)),float(read<std::int32_t>(e,0x4c)),float(read<std::int32_t>(e,0x50))};
        // the body point it came out of: the part matrix it was spawned on (+0x70 copy, translation +0x84), else the root
        Vec3 from{float(read<std::int32_t>(e,0x84)),float(read<std::int32_t>(e,0x88)),float(read<std::int32_t>(e,0x8c))};
        if(kind==Floor||(from.x==0&&from.y==0&&from.z==0))from={float(read<std::int32_t>(owner,0x34)),float(read<std::int32_t>(owner,0x38))-1200.f,float(read<std::int32_t>(owner,0x3c))};
        // fewer puffs than native sprites: the native downing throws ~33 a second, an HD puff is much larger
        if((kind==Splat&&rnd()<0.45f)||(kind==Drip&&rnd()<0.5f)||(kind==Floor&&rnd()<0.4f))continue;
        emit(kind,pos,from,lightOf(owner),address,generation,std::uint8_t(read<std::uint8_t>(e,0xb3)&15u));
        if(spawnLogged<10){++spawnLogged;log("NEMESIS_CLOUD_SPAWN key=%04x kind=%u at=%.0f,%.0f,%.0f from=%.0f,%.0f,%.0f model=%02x routine=%08x life=%d light=%06x",key,unsigned(kind),pos.x,pos.y,pos.z,from.x,from.y,from.z,owner[0x4a],read<std::uint32_t>(owner,4),int(read<std::int16_t>(owner,0xcc)),lightOf(owner)&0xffffffu);}
    }
}
// ---------------------------------------------------------------- the downed Nemesis' purple stain (his shadow)
struct Stain{const std::uint8_t* actor=nullptr;std::uint8_t* shadow=nullptr;Vec3 centre{};float size=0;std::uint32_t born=0,grownAt=0,seen=0;bool logged=false;};
static std::vector<Stain> stains;
static std::uint64_t stainsHidden=0;
inline bool purpleStain(const std::uint8_t* shadow){
    const std::uint32_t c=read<std::uint32_t>(shadow,0x1c)&0xffffffu;const unsigned r=c&255u,g=(c>>8)&255u,b=(c>>16)&255u;
    return c!=0x808080u&&r==b&&g>r+8;   // 808080 -> 7A867A ... 10AF10: green kept above red/blue (subtracted = magenta)
}
inline const std::uint8_t* stainActor(const void* shadow){
    const auto end=read<std::uintptr_t>(reinterpret_cast<void*>(gameBase+0x65ce6c));
    for(std::uintptr_t a=gameBase+0x65cdcc;a<end&&a<gameBase+0x65cdcc+48*4;a+=4){const auto* actor=read<const std::uint8_t*>(reinterpret_cast<void*>(a));
        if(accessible(actor,0x164)&&read<const void*>(actor,0x160)==shadow)return actor;}
    return nullptr;
}
// the stain of a downed Nemesis (life <= 0): not drawn (flag 4 off), HD gas over it. blood_fx's shadowHook calls this right
// after the native shadow update 457130; update() once a frame for every downed Nemesis
inline void onShadow(void* shadowPointer){
    if(!ready())return;
    auto* shadow=static_cast<std::uint8_t*>(shadowPointer);if(!accessible(shadow,0x68,true)||!purpleStain(shadow))return;
    Stain* known=nullptr;for(auto& st:stains)if(st.shadow==shadow){known=&st;break;}
    const std::uint8_t* actor=known?known->actor:stainActor(shadow);
    if(!accessible(actor,0x100)||!nemesisModel(actor[0x4a])||read<std::int16_t>(actor,0xcc)>0)return;
    if(shadow[0xe]&4){shadow[0xe]=std::uint8_t(shadow[0xe]&~4u);++stainsHidden;}
    if(!known){if(stains.size()>=4)stains.erase(stains.begin());stains.push_back({});known=&stains.back();known->actor=actor;known->shadow=shadow;known->born=tick;known->grownAt=tick;}
    known->centre={float(read<std::int16_t>(shadow,8)),float(read<std::int16_t>(shadow,0xa)),float(read<std::int16_t>(shadow,0xc))};
    const float size=float((std::max)(read<std::int16_t>(shadow,4),read<std::int16_t>(shadow,6)));
    if(size>known->size+0.5f){known->size=size;known->grownAt=tick;}
    known->seen=tick;
    if(!known->logged){known->logged=true;log("NEMESIS_CLOUD_STAIN actor=%p model=%02x shadow=%p at=%.0f,%.0f,%.0f size=%.0f colour=%06x (purple stain hidden, HD gas over it)",actor,actor[0x4a],shadow,known->centre.x,known->centre.y,known->centre.z,size,read<std::uint32_t>(shadow,0x1c)&0xffffffu);}
}
inline void stainGas(){
    for(auto it=stains.begin();it!=stains.end();){
        if(tick-it->seen>45){it=stains.erase(it);continue;}
        const bool growing=tick-it->grownAt<8;const float radius=it->size*0.42f;const std::uint32_t age=tick-it->born;
        const std::uint32_t light=lightOf(it->actor);
        // while it spreads: ~10 puffs a second seeping out of the body; then a thin haze, rarer over half a minute
        const float rate=growing?0.34f:(std::max)(0.05f,0.14f-0.09f*float(tick-it->grownAt)/900.f);
        if(rnd()<rate){
            const float a=rnd()*6.2831853f,r=radius*std::sqrt(rnd())*(growing?(std::min)(1.f,0.35f+float(age)/60.f):1.f);
            const Vec3 at{it->centre.x+std::cos(a)*r,it->centre.y-rnd(60.f,240.f),it->centre.z+std::sin(a)*r};
            emit(growing?Seep:Haze,at,it->centre,light,0,0);
        }
        ++it;
    }
}
inline void step(){
    const bool move=tick%debugSlow==0;
    for(auto it=puffs.begin();it!=puffs.end();){
        Puff& p=*it;const std::uint32_t age=(tick-p.born)/debugSlow;
        if(age>=p.life){it=puffs.erase(it);continue;}
        if(p.following){
            bool alive=false;
            if(accessible(reinterpret_cast<const void*>(p.effect),0xd4)){const auto* e=reinterpret_cast<const std::uint8_t*>(p.effect);
                const auto at=effectGeneration.find(p.effect);const std::uint64_t generation=at==effectGeneration.end()?0:at->second;
                if(generation==p.generation&&(read<std::uint16_t>(e,0x24)&0xa000u)==0xa000u){
                    const Vec3 now{float(read<std::int32_t>(e,0x48)),float(read<std::int32_t>(e,0x4c)),float(read<std::int32_t>(e,0x50))};
                    if(p.kind==Arm){   // fx2: exactly on the native sprite; alive at least 10 frames past it
                        const Vec3 d=now-p.last;p.vel=d;p.last=now;p.pos=now;if(p.armKey==Floor)p.pos.y-=60.f;
                        const std::uint16_t need=std::uint16_t((tick-p.born)/debugSlow+10u);if(p.life<need)p.life=need;
                        ++it;continue;}
                    // the native sprite's own flight, damped (gas does not keep a blood drop's speed)
                    const Vec3 d=now-p.last;p.last=now;
                    if(re3model::Dot(d,d)<400.f*400.f){p.vel=p.vel*0.6f+d*0.25f;}alive=true;}}
            if(!alive)p.following=false;
        }
        if(move&&p.kind==Arm){p.vel=p.vel*0.6f;p.pos=p.pos+p.vel;p.roll+=p.spin;++it;continue;}   // fx2: the native sprite is gone: a short drift
        if(move){
            // buoyant gas: rises (world Y is down), slows, spreads; floor puffs stay low
            p.vel=p.vel*0.94f;p.vel.y-=p.rise*0.06f;
            if((p.kind==Floor||p.kind==Seep||p.kind==Haze)&&p.vel.y<-1.2f)p.vel.y=-1.2f;
            p.pos=p.pos+p.vel;p.roll+=p.spin;
        }
        ++it;
    }
}

// ---------------------------------------------------------------- drawing (as blood_fx.h drawBlood)
inline void draw(){
    if(puffs.empty())return;
    auto asset=pack();if(!asset||!modelRenderer.resetDevice(gameDevice))return;
    const auto gte=read<const void*>(reinterpret_cast<void*>(gameBase+0x1349c8));if(!accessible(gte,0x100))return;
    re3bridge::ActorContext context;context.camera=read<re3bridge::Matrix32>(reinterpret_cast<void*>(gameBase+0x11c398));
    std::memcpy(context.gte.data(),gte,context.gte.size());
    auto base=actorProjection(context);
    if(!re3model::SetNativeProjectionRegisters(base,context.gte.data(),context.gte.size()))return;
    base.shade=nullptr;base.shadeContext=nullptr;
    const auto& cam=base.camera;
    const Vec3 right=re3model::Normalize({cam.m[0],cam.m[1],cam.m[2]}),down=re3model::Normalize({cam.m[4],cam.m[5],cam.m[6]});
    re3model::Matrix3x4 view;Vec3 eye{0,0,0};const bool haveEye=re3model::InverseAffine(cam,view);if(haveEye)eye={view.m[3],view.m[7],view.m[11]};
    // R20F window walls (model_integration.h kWallSplit): the room side over the wall masks, beyond it behind them
    const WallSplitRule* wall=nullptr;
    if(wallSplitOn()){const std::uint16_t stage=read<std::uint16_t>(reinterpret_cast<void*>(gameBase+0x661fa6)),rm=read<std::uint16_t>(reinterpret_cast<void*>(gameBase+0x661fa8)),camera=read<std::uint16_t>(reinterpret_cast<void*>(gameBase+0x661faa));
        for(const auto& r:kWallSplit)if(r.stage==stage&&r.room==rm&&r.camera==camera)wall=&r;}
    struct Quad{Vec3 anchor;float depth;std::vector<re3model::SkinnedVertex> vertices;std::vector<std::uint32_t> indices,tint;int bucket;unsigned material=0;};
    std::vector<Quad> quads;quads.reserve(puffs.size()*2);
    // fx3 2026-11-08: user "хорошо смотрится … но нужно темнее их сделать, слишком яркие" -> brightness factors (percent),
    // pilot.ini nemesis_cloud_arm_brightness (the living Nemesis' arm/grab puffs) and nemesis_cloud_brightness (downing gas)
    static int brightArm=-1,brightBody=-1;
    if(brightArm<0){const std::string ini=root+"\\pilot.ini";
        brightArm=(std::max)(10,(std::min)(200,int(GetPrivateProfileIntA("pilot","nemesis_cloud_arm_brightness",55,ini.c_str()))));
        brightBody=(std::max)(10,(std::min)(200,int(GetPrivateProfileIntA("pilot","nemesis_cloud_brightness",55,ini.c_str()))));
        log("NEMESIS_CLOUD_BRIGHTNESS arm=%d%% body=%d%%",brightArm,brightBody);}
    for(const auto& p:puffs){
        const float t=float((tick-p.born)/debugSlow)/float(p.life);
        // dense -> opened -> wisps -> almost gone: stage s, cross-fade between floor(s) and floor(s)+1
        const float s=(std::min)(2.999f,std::pow(t,0.8f)*3.f);const unsigned s0=unsigned(s);const float f=s-float(s0);
        const float fadeIn=(std::min)(1.f,t/0.08f),fadeOut=1.f-std::pow(t,2.2f);
        const float alpha=p.alpha*fadeIn*fadeOut;if(alpha<0.01f)continue;
        float size=p.size*(1.f+(p.grow-1.f)*(1.f-std::pow(1.f-t,1.6f)));
        // pulled toward the eye (same screen size) so the quad stays in front of the body it comes out of
        Vec3 at=p.pos;
        if(haveEye){const Vec3 d=eye-p.pos;const float dist=std::sqrt(re3model::Dot(d,d));const float pull=(std::min)(size*0.45f,dist-400.f);
            if(pull>0){at=p.pos+d*(pull/dist);size*=(dist-pull)/dist;}}
        const auto camera=re3model::TransformPoint(cam,at);if(camera.z<256.f||camera.z>32767.f)continue;
        int bucket=int((std::max)(1.f,camera.z))>>5;
        if(wall){const float side=wall->normal[0]*p.pos.x+wall->normal[1]*p.pos.y+wall->normal[2]*p.pos.z+wall->offset+float(wallSplitShift());
            bucket=side>=0?(std::min)(bucket,int(wall->frontMax)):int(wall->beyond);}
        const std::uint32_t light=blood::scaled(blood::lifted(p.colour,p.kind==Arm?0xb4:0x78),float(p.kind==Arm?brightArm:brightBody)/100.f);   // fx2: arm puffs nearly unlit like the native sprites; fx3: darker
        for(unsigned k=0;k<2;++k){
            const float a=alpha*(k?f:1.f-f);if(a<0.01f||(k&&s0>=3))continue;
            Quad q;q.anchor=at;q.depth=camera.z;q.bucket=bucket;q.material=p.material<asset->mesh.materials.size()?p.material:0u;
            float cell[4];blood::gridCell(p.row*4u+s0+k,4,4,cell);
            blood::Item item;blood::sprite(item,right,down,at,size,size,p.roll+(k?0.15f:0.f),0.5f,0.5f,cell,blood::withAlpha(light,a));
            q.vertices=std::move(item.vertices);q.indices=std::move(item.indices);q.tint=std::move(item.tint);quads.push_back(std::move(q));
        }
    }
    if(quads.empty())return;
    // far to near (blended gas)
    std::stable_sort(quads.begin(),quads.end(),[](const Quad& a,const Quad& b){return a.depth>b.depth;});
    re3model::Mesh materials;materials.materials=asset->mesh.materials;   // fx2: 0 cloud (downing), 1 arm (living Nemesis)
    for(auto& m:materials.materials)m.flags=re3model::AlphaBlend|re3model::TwoSided;
    const auto chooseTable=reinterpret_cast<ChooseTable>(gameBase+0x5adc0);
    struct Piece{std::uint32_t* table;std::uint16_t bucket;std::vector<re3model::DrawVertex> vertices;unsigned material=0;};
    std::vector<Piece> pieces;
    for(auto& q:quads){
        const std::int32_t position[3]={std::int32_t(q.anchor.x),std::int32_t(q.anchor.y),std::int32_t(q.anchor.z)};
        auto* table=chooseTable(position);if(!table||!accessible(table,1024*4,true))continue;
        re3model::Mesh mesh;mesh.vertices.resize(q.vertices.size());mesh.indices=q.indices;
        re3model::Material m=materials.materials[q.material];m.firstIndex=0;m.indexCount=std::uint32_t(q.indices.size());mesh.materials.push_back(m);
        auto projection=base;projection.vertexTint=&q.tint;std::uint16_t fixed=std::uint16_t((std::min)(1023,(std::max)(1,q.bucket)));
        projection.depth=blood::fixedBucket;projection.depthContext=&fixed;
        re3model::FrameGeometry geometry;std::string error;
        if(!re3model::PrepareFrame(mesh,q.vertices,projection,geometry,&error)||geometry.vertices.empty())continue;
        for(const auto& slice:geometry.slices)for(unsigned r=0;r<slice.runCount;++r){const auto& run=geometry.runs[slice.firstRun+r];
            Piece piece;piece.table=table;piece.bucket=slice.bucket;piece.material=q.material;
            piece.vertices.assign(geometry.vertices.begin()+run.firstVertex,geometry.vertices.begin()+run.firstVertex+run.vertexCount);pieces.push_back(std::move(piece));}
    }
    if(pieces.empty())return;
    std::stable_sort(pieces.begin(),pieces.end(),[](const Piece& a,const Piece& b){return a.table!=b.table?a.table<b.table:a.bucket>b.bucket;});
    for(std::size_t i=0;i<pieces.size();){
        std::uint32_t* table=pieces[i].table;re3model::FrameGeometry merged;
        for(;i<pieces.size()&&pieces[i].table==table;++i){const auto& piece=pieces[i];
            if(merged.slices.empty()||merged.slices.back().bucket!=piece.bucket)merged.slices.push_back({piece.bucket,std::uint32_t(merged.runs.size()),0});
            merged.runs.push_back({std::uint32_t(merged.vertices.size()),std::uint32_t(piece.vertices.size()),piece.material});++merged.slices.back().runCount;
            merged.vertices.insert(merged.vertices.end(),piece.vertices.begin(),piece.vertices.end());}
        HRESULT hr;auto gpu=modelRenderer.upload(materials,std::move(merged),asset->textures,false,&hr);if(!gpu){once("nemesis-cloud-upload","HD Nemesis cloud upload failed this frame");return;}
        std::vector<re3bridge::BucketJob> jobs;jobs.reserve(gpu->slices.size());
        for(std::size_t n=0;n<gpu->slices.size();++n){auto job=std::make_shared<ModelJob>();job->geometry=gpu;job->slice=n;job->name="fx-nemesis-cloud";
            jobs.push_back({gpu->slices[n].bucket,reinterpret_cast<std::uintptr_t>(job.get()),job});}
        if(!re3bridge::EnqueueBatch(table,1024,jobs))once("nemesis-cloud-enqueue","HD Nemesis cloud skipped for one ordering table");
    }
}

// ---------------------------------------------------------------- QA: pilot.command nemesis-cloud-test (debug_warp)
// a downing-like emission in front of the player for 4.3 s (the native downing's length), any room
static std::uint32_t testUntil=0;static Vec3 testAt{};
inline void test(){
    if(InterlockedExchange(&::nemesisCloudTestRequest,0)){
        const auto* player=reinterpret_cast<const std::uint8_t*>(gameBase+0x65cbf4);
        const float yaw=float(read<std::int16_t>(player,0x6e))*6.2831853f/4096.f;
        testAt={float(read<std::int32_t>(player,0x34))+std::sin(yaw)*1400.f,float(read<std::int32_t>(player,0x38)),float(read<std::int32_t>(player,0x3c))+std::cos(yaw)*1400.f};
        testUntil=tick+130;log("NEMESIS_CLOUD_TEST at=%.0f,%.0f,%.0f (130 frames)",testAt.x,testAt.y,testAt.z);
    }
    if(tick<testUntil){
        const auto* player=reinterpret_cast<const std::uint8_t*>(gameBase+0x65cbf4);const std::uint32_t light=lightOf(player);
        const Vec3 chest{testAt.x,testAt.y-1500.f,testAt.z};
        if(rnd()<0.5f)emit(Spray,chest+Vec3{rnd(-250.f,250.f),rnd(-300.f,250.f),rnd(-250.f,250.f)},chest,light,0,0);
        if(rnd()<0.25f)emit(Splat,chest+Vec3{rnd(-300.f,300.f),rnd(0.f,500.f),rnd(-300.f,300.f)},chest,light,0,0);
        if(rnd()<0.12f)emit(Floor,Vec3{testAt.x+rnd(-500.f,500.f),testAt.y,testAt.z+rnd(-500.f,500.f)},testAt,light,0,0);
    }
}

// ---------------------------------------------------------------- per effect-loop frame (extra_model_integration.h effectLoopHook)
// ---------------------------------------------------------------- fx5: the native arm sprites at 8x (proxy.cpp drawUiPacket)
// A quad of tpage 1E (any blend bits) with CLUT 7891 / 7911 inside the CHI_00 block (u 0..239, v 0..87) is drawn from the
// 8x page of that palette: same vertices, UVs, colour and semi-transparency as the game's quad (the binding only swaps the
// texture). HD mode only (F8 = native), nemesis_cloud_fx=1, nemesis_cloud_arm=0, the PNG present.
static std::uint64_t armQuads=0;
static bool armBinding(const std::uint8_t* p,unsigned tpage,unsigned clut,float minU,float minV,float maxU,float maxV,re3ui::Binding& binding){
    (void)p;
    if(!enabled||(tpage&0x19fu)!=0x1eu||(clut!=0x7891u&&clut!=0x7911u))return false;
    if(minU<0.f||minV<0.f||maxU>240.f||maxV>88.f)return false;
    if(armMode()!=0||!active())return false;
    static int present=-1;
    if(present<0){present=GetFileAttributesA((root+"\\model-pack\\fx-nemesis-cloud\\native-arm-p5.png").c_str())!=INVALID_FILE_ATTRIBUTES&&
                          GetFileAttributesA((root+"\\model-pack\\fx-nemesis-cloud\\native-arm-p9.png").c_str())!=INVALID_FILE_ATTRIBUTES?1:0;
        log(present?"NEMESIS_CLOUD_ARM_TEXTURE 8x native-arm-p5/p9 (tpage 1E CLUT 7891/7911 u<240 v<88)":"NEMESIS_CLOUD_ARM_TEXTURE missing (model-pack/fx-nemesis-cloud/native-arm-p5/p9.png): TEXU upscale kept");}
    if(!present)return false;
    const bool purple=clut==0x7911u;
    binding=re3ui::Binding{};binding.assetId=purple?"fx-nemesis-arm-p9":"fx-nemesis-arm-p5";
    binding.pngPath=purple?"model-pack/fx-nemesis-cloud/native-arm-p9.png":"model-pack/fx-nemesis-cloud/native-arm-p5.png";
    binding.originalW=240;binding.originalH=88;binding.offsetU=0;binding.offsetV=0;binding.scale=8;binding.paletteIndex=purple?9:5;
    if(++armQuads==1||armQuads%5000==0)log("NEMESIS_CLOUD_ARM_QUADS %llu (last clut=%04x uv=%.0f,%.0f-%.0f,%.0f)",(unsigned long long)armQuads,clut,minU,minV,maxU,maxV);
    return true;
}

// ---------------------------------------------------------------- QA diagnostics (fx2 2026-11-08, any mode incl. F8 and nemesis_cloud_fx=0)
// pilot.ini nemesis_cloud_trace=1: every effect of the pool owned by a Nemesis actor (+0xBC), any key/palette, as it appears and
// ends (NEMESIS_FX_NEW / NEMESIS_FX_END: key, class, palette +0xB3, owner routine/life, the part it was spawned on (+0xC0 into
// the parts array actor+0x108, stride 0xBC, matrix +0x14), position relative to Nemesis and to the player, lifetime, flight).
// pilot.ini nemesis_cloud_debug_burst=N (+ _every=K ticks): the first Nemesis sprite in palette 2/4 of a living Nemesis (or of a
// downed one with _burst_down=1) starts N frame captures, one every K effect-loop ticks (game time, the game keeps running;
// files captures\frame-NCB<n>-...tga, moved by the QA driver).
static bool traceOn=false,debugLoaded=false,burstDown=false;
static unsigned burstCount=0,burstEvery=4,burstLeft=0,burstNext=0,burstSerial=0,burstQuietUntil=0,traceLines=0;
struct Traced{bool live=false;std::uint16_t key=0;std::uint8_t palette=0;std::uint32_t born=0;std::int32_t pos[3]{};};
static Traced traced[96];
inline void loadDebugKeys(){
    if(debugLoaded)return;debugLoaded=true;const std::string ini=root+"\\pilot.ini";
    if(!GetPrivateProfileIntA("pilot","debug_warp",0,ini.c_str()))return;
    traceOn=GetPrivateProfileIntA("pilot","nemesis_cloud_trace",0,ini.c_str())!=0;
    burstCount=(std::min)(400u,unsigned(GetPrivateProfileIntA("pilot","nemesis_cloud_debug_burst",0,ini.c_str())));
    burstEvery=(std::max)(1u,(std::min)(30u,unsigned(GetPrivateProfileIntA("pilot","nemesis_cloud_debug_burst_every",4,ini.c_str()))));
    burstDown=GetPrivateProfileIntA("pilot","nemesis_cloud_debug_burst_down",0,ini.c_str())!=0;
    if(traceOn||burstCount)log("NEMESIS_CLOUD_DEBUG trace=%d burst=%u every=%u down=%d",int(traceOn),burstCount,burstEvery,int(burstDown));
}
inline int partOf(const std::uint8_t* owner,const void* matrix){
    const auto parts=reinterpret_cast<std::uintptr_t>(read<const void*>(owner,0x108)),m=reinterpret_cast<std::uintptr_t>(matrix);
    if(!parts||m<parts+0x14)return -1;   // the part's matrix at +0x14 or its world copy at +0x40 (4AFD90: parts+5*0xBC+0x40)
    for(const std::uintptr_t o:{std::uintptr_t(0x14),std::uintptr_t(0x40)})if(m>=parts+o&&(m-parts-o)%0xbc==0&&(m-parts-o)/0xbc<=48)return int((m-parts-o)/0xbc);
    return -1;
}
inline void diagnose(){
    loadDebugKeys();
    if(burstLeft&&tick>=burstNext){--burstLeft;burstNext=tick+burstEvery;char tag[32];sprintf_s(tag,"NCB%02u-%03u",burstSerial,burstCount-burstLeft-1);captureTag=tag;captureRequested=true;
        if(!burstLeft){burstQuietUntil=tick+150;log("NEMESIS_CLOUD_BURST_END serial=%u",burstSerial);}}
    if(!traceOn&&!burstCount)return;
    const auto* player=reinterpret_cast<const std::uint8_t*>(gameBase+0x65cbf4);
    const auto start=gameBase+0x646720;
    for(unsigned slot=0;slot<96;++slot){
        const auto* e=reinterpret_cast<const std::uint8_t*>(start+slot*0xd4);if(!accessible(e,0xd4))continue;
        auto& t=traced[slot];const bool live=(read<std::uint16_t>(e,0x24)&0xa000u)==0xa000u;
        const auto* owner=live?read<const std::uint8_t*>(e,0xbc):nullptr;
        const bool nem=live&&accessible(owner,0x110)&&nemesisModel(owner[0x4a]);
        const auto key=read<std::uint16_t>(e,0x28);
        if(t.live&&(!live||!nem||key!=t.key)){
            if(traceOn&&traceLines<1600){++traceLines;log("NEMESIS_FX_END slot=%u key=%04x pal=%u frames=%u from=%d,%d,%d to=%d,%d,%d",slot,t.key,t.palette,tick-t.born,t.pos[0],t.pos[1],t.pos[2],
                read<std::int32_t>(e,0x48),read<std::int32_t>(e,0x4c),read<std::int32_t>(e,0x50));}
            t.live=false;
        }
        if(!nem||t.live)continue;
        t.live=true;t.key=key;t.palette=std::uint8_t(read<std::uint8_t>(e,0xb3)&15u);t.born=tick;for(unsigned r=0;r<3;++r)t.pos[r]=read<std::int32_t>(e,0x48+r*4);
        const int life=read<std::int16_t>(owner,0xcc);
        if(traceOn&&traceLines<1600){++traceLines;
            const float nx=float(t.pos[0]-read<std::int32_t>(owner,0x34)),nz=float(t.pos[2]-read<std::int32_t>(owner,0x3c));
            const float px=float(t.pos[0]-read<std::int32_t>(player,0x34)),pz=float(t.pos[2]-read<std::int32_t>(player,0x3c));
            log("NEMESIS_FX_NEW slot=%u key=%04x class=%02x flags=%04x pal=%u clut=%04x model=%02x routine=%08x life=%d part=%d pos=%d,%d,%d up=%d dN=%.0f dJ=%.0f jill_routine=%08x jill_life=%d",
                slot,key,e[0],read<std::uint16_t>(e,0x24),t.palette,read<std::uint16_t>(e,0x44),owner[0x4a],read<std::uint32_t>(owner,4),life,partOf(owner,read<const void*>(e,0xc0)),
                t.pos[0],t.pos[1],t.pos[2],read<std::int32_t>(owner,0x38)-t.pos[1],std::sqrt(nx*nx+nz*nz),std::sqrt(px*px+pz*pz),read<std::uint32_t>(player,4),int(read<std::int16_t>(player,0xcc)));}
        if(burstCount&&!burstLeft&&tick>=burstQuietUntil&&t.palette&&(life>0||burstDown)&&(key&0xffu)==0){
            ++burstSerial;burstLeft=burstCount;burstNext=tick;log("NEMESIS_CLOUD_BURST serial=%u key=%04x pal=%u routine=%08x life=%d (%u captures every %u ticks)",burstSerial,key,t.palette,read<std::uint32_t>(owner,4),life,burstCount,burstEvery);}
    }
}
inline void update(){
    const std::uint32_t now=(std::uint32_t(read<std::uint16_t>(reinterpret_cast<void*>(gameBase+0x661fa6)))<<16)|read<std::uint16_t>(reinterpret_cast<void*>(gameBase+0x661fa8));
    if(now!=room){
        if(room!=~0u&&(spawned||nativeSeen||armSeen))log("NEMESIS_CLOUD_CLEAR room=%x native=%llu puffs=%llu hidden_packets=%llu stain_frames_hidden=%llu arm_native=%llu arm_puffs=%llu",now,(unsigned long long)nativeSeen,(unsigned long long)spawned,(unsigned long long)hidden,(unsigned long long)stainsHidden,(unsigned long long)armSeen,(unsigned long long)armSpawned);
        room=now;puffs.clear();seen.clear();stains.clear();textureCount=0;testUntil=0;
    }
    ++tick;
    try{diagnose();}catch(...){once("nemesis-cloud-diagnose","QA diagnostics skipped");}
    if(!enabled)return;
    nemesisHere=false;nemesisDown=false;std::uint8_t* downed[4]{};unsigned downedCount=0;
    {const auto end=read<std::uintptr_t>(reinterpret_cast<void*>(gameBase+0x65ce6c));
        for(std::uintptr_t a=gameBase+0x65cdcc;a<end&&a<gameBase+0x65cdcc+48*4;a+=4){const auto* actor=read<const std::uint8_t*>(reinterpret_cast<void*>(a));
            if(!accessible(actor,0x164)||!nemesisModel(actor[0x4a]))continue;
            nemesisHere=true;
            if(read<std::int16_t>(actor,0xcc)<=0){nemesisDown=true;if(downedCount<4)downed[downedCount++]=read<std::uint8_t*>(actor,0x160);}}}
    // F8 (original): the purple stain is drawn again (a downed body's shadow may no longer be updated by 457130: flag 4
    // would stay off)
    if(!active()){puffs.clear();for(const auto& st:stains)if(accessible(st.shadow,0x68,true)&&purpleStain(st.shadow))st.shadow[0xe]=std::uint8_t(st.shadow[0xe]|4u);stains.clear();return;}
    {const auto asset=pack();packOk=bool(asset);armPack=packOk&&asset->mesh.materials.size()>=2&&asset->textures.size()>=2;}if(!packOk)return;
    armActive=false;   // fx2: set by scanEffects while a sprite of the living Nemesis lives (the copy filter hides its texture then)
    test();
    if(nemesisHere)scanEffects();
    for(unsigned k=0;k<downedCount;++k)if(downed[k])onShadow(downed[k]);
    stainGas();
    step();
    if(!puffs.empty()){LARGE_INTEGER f,a,b;QueryPerformanceFrequency(&f);QueryPerformanceCounter(&a);draw();QueryPerformanceCounter(&b);
        const double ms=double(b.QuadPart-a.QuadPart)*1000.0/double(f.QuadPart);perfSum+=ms;perfMax=(std::max)(perfMax,ms);++perfFrames;perfQuads+=unsigned(puffs.size());
        if(perfFrames==150){log("NEMESIS_CLOUD_PERF frames=150 draw_avg_ms=%.3f draw_max_ms=%.3f puffs_avg=%.1f",perfSum/150.0,perfMax,double(perfQuads)/150.0);perfSum=perfMax=0;perfFrames=perfQuads=0;}}
}
inline void install(){
    if(!enabled){log("NEMESIS_CLOUD disabled by pilot.ini nemesis_cloud_fx=0 (Nemesis' palette 2/4 sprites and stain native; blood_fx leaves them alone)");return;}
    previousFilter=re3bridge::detail::ft4Filter;
    if(previousFilter){re3bridge::detail::ft4Filter=&packetFilter;filterInstalled=true;}
    else filterInstalled=re3bridge::detail::InstallFt4Filter(gameBase,&packetFilter);
    log(filterInstalled?"NEMESIS_CLOUD_INSTALL_OK HD purple-grey gas from Nemesis' sprites (set 0: 0C00/1700/0500/0D00, palette != 0) while he goes down and over his purple stain, fx2: HD puffs on the sprites of the living Nemesis, native ones hidden (FT4 copy filter chained=%d)":
        "NEMESIS_CLOUD_INSTALL_FAILED FT4 copy filter unavailable (chained=%d): native sprites kept",int(previousFilter!=nullptr));
    if(!filterInstalled)enabled=false;
}
} // namespace nemesis_cloud
