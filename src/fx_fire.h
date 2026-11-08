#pragma once
// ---------------------------------------------------------------- HD fire (FX round 2026-11-08, agent "fire")
// Included at the end of extra_model_integration.h after blood_fx.h, inside namespace re3extra (read/accessible/active/
// once/mutex, getModel, modelRenderer, effectGeneration, emittedSinglePacket, ChooseTable).
//
// What the original draws (models/fx-2026-11-08/fire/tools: esp_dump.py, esp_survey.py, core_quads.py; staging REPORT.ru.txt):
// every native effect is a slot of the pool A46720 (96 x 0xD4), spawned by 43D370(key, ...). key low byte = ESP bank id,
// looked up in the bank table A465A0 (32 x 12 bytes: +0 texture header {u16 groups, u16 quads, u16 clut, u16 tpage},
// +4 sequence table, +8 id; 0..15 = CORE00.ESP banks 00 05 01 02 03 04, 16..31 = the room's RDT banks, 43D1B0); key >> 8
// = record (template) of the bank, key >> 24 = palette row (+0xB3, clut + 0x40 per row). The effect loop 458130 draws
// each one as a screen-aligned FT4 billboard (4583D0, the call 4583A2 wrapped by re3extra::billboardHook), one packet
// per effect and frame, its UVs = one quad {u, v, dx, dy} of the bank (the animation frame), anchor = effect +48/+4C/+50.
//  - flames: room banks 20..28 (texture pages labelled FIRE_xx; R10D: 24 = FIRE_05, 26 = FIRE_07), persistent class-00
//    effects standing on the floor, 8-14 frame loops of 24..56 px flames anchored at the bottom centre, tpage abr 1
//    (additive). In 41 of the 131 rooms (static/survey/rooms.json).
//  - billows: core bank 03 (CORE00.ESP), one 16-frame grey puff animation coloured by the palette row: 3 = fire colours
//    (fireballs: the intro explosion of R10D, keys 0903/0A03/1F03, abr 1/3 additive), 1 = dim grey with abr 2
//    (subtractive: the smoke columns over the R10D fires, key 1303, and the explosion smoke 0803), 0 = grey abr 2
//    (dark explosion smoke 4D03).
//  - explosions: room bank 0C (16 frames, "BOM" page) in 25 rooms (the room's explosions: in R10D its record 0 is a
//    damaging gas explosion, fire-test spawned it).
//  - burning bodies: core bank 05 (CORE00.ESP) quads 0-9 = a 10-frame flame (40 px, effects 0005 class 05 on the burning
//    zombies of the R10D intro), quads 10-18 = sparks and glowing dots.
// Not fire, left alone: core 00 blood (blood_fx.h), core 01 muzzle flashes (agent muzzle), core 02 cases, core 04, room
// banks 08/09/18 (blood mist / gore / dust), 1D (small candle-like flames of R300-R317, not seen burning, unverified).
// What this does: such a packet is REPLACED in place (re3bridge::ReplacePacket, its exact OT position, like the cases)
// by an HD job drawn where the native sprite is, at its size (the native screen quad back-projected at the effect's
// depth): flames = Codex image_gen flame frames (two sheets, tall/wide), cross-faded in a per-flame random order (no
// visible loop, neighbours never in step), flickering, with an ember bed at the base and sparks rising from it;
// billows/explosions = HD fireball / smoke flipbooks at the native frame (quad index -> progress). Fire is additive
// (native semi-transparent path, abr 1), smoke subtracted like the native (abr 2). F8 (original) and pilot.ini fire_fx=0: native.
//
// Round 5 (fx5 08.11, user: "like drawn pictures changing"; keep the original effect, only sharper): by default
// (pilot.ini fire_fx=1) the native fire effects draw as in the original - their own quads, frames, animation, sizes and
// blending - and only the texture they sample is an HD page of the same native frames (models/fx-2026-11-08/fire/round5:
// Codex image_gen redraws of every native frame, native colours at low frequency, native outline, faded inside the
// window the game shows of a quad). proxy.cpp drawUiPacket asks pageBinding() for every packet (like the muzzle flash
// page, fx_muzzle.h flashBinding): a 4-bit quad of a loaded fire bank (bank table A465A0: core 03 rows 3/1/0, core 05
// row 0 quads 0-9, room 20-28 and 0C, matched by header clut/tpage + palette row and checked against the bank's quad
// rects and their layout hash) gets model-pack/fx-fire/pages/<page>.png (mip-mapped: the sprites are drawn from 0.3x to
// 5x their texel size). Item twinkle 0905 (core 05 row 1) and the core 05 stars (quads 10+) stay native.
// fire_fx=2 = the round-2 HD clips below (billboard()), fire_fx=0 = native.
namespace fire {
using re3model::Vec3;
static bool enabled=true;              // round-2 HD clips active (pilot.ini fire_fx=2; fx5: no longer the default)
static unsigned fireMode=1;            // fx5 2026-11-08 fire: pilot.ini fire_fx 0 native, 1 HD pages of the native frames (default), 2 round-2 clips
static unsigned debugSlow=1;           // pilot.ini fire_debug_slow (QA): HD flame motion N times slower
static unsigned burstCount=0,burstEvery=6;   // pilot.ini fire_debug_burst=N (QA): every fire-test also takes N frame captures, one per fire_debug_burst_every ticks
static std::string testSteps="fxl";    // pilot.ini fire_test_steps: what each fire-test does in turn (b = captures only, f = fireball+smoke, x = explosion 0C, l = flame 0024)
static unsigned burstLeft=0,burstNext=0,testStep=0;
// round 2 (fx2 08.11): flame materials = two computed continuous loops per style (tall a/b, wide a/b), plus a glow sprite
constexpr unsigned kTall=0,kWide=1;   // flame styles (Rule::style)
constexpr unsigned kTallA=0,kTallB=1,kWideA=2,kWideB=3,kExplosion=4,kSmoke=5,kSparks=6,kEmbers=7,kGlow=8,kMaterials=9;
enum Kind:std::uint8_t{None=0,Flame=1,Burst=2,Smoke=3,Ember=4};
struct Sheet{unsigned cols=1,rows=1,count=1;float cw=1,ch=1;};
struct Rule{Kind kind=None;std::uint8_t style=kTall;float scale=1,alpha=1;std::uint32_t tint=0xffffffffu;};
struct Config{
    bool loaded=false;Sheet sheet[kMaterials];float flameBase=0.98f,sparkAngle[16]{},sparkElong[16]{};
    Rule bank[256];          // by ESP bank id (key low byte), room banks and core banks except 03
    Rule core03[16][4];      // core bank 03 billows by palette row (+0xB3 & 15) and blend (tpage bits 5-6)
    float flameWidth[2]{1.05f,1.45f},flameHeight[2]{1.55f,1.35f};  // HD flame cell vs the native square, tall / wide (v2: x1.25, the
                             // native blocky flames fill their square, the HD ones are thinner and lighter)
    float flameRate=11.f;    // (round 1) unused since round 2
    float loopFps[2]{16.f,13.f};   // round 2: frames per second of the two loop layers of a hearth (fire.txt loop_fps)
    float glow=0.10f;        // round 2: soft halo (bloom) intensity (fire.txt glow)
    int sparkFrom[256];      // per bank: quads from this index on are sparks/embers (HD spark particles), -1 = none (all now)
    unsigned bank05Quads=10;std::uint32_t bank05Palettes=1u;   // bank 05: quads below this and palettes (bit mask) that are fire
    float sparkRate=1.6f;    // sparks per second per flame
    bool sparks=true,embers=true;
};
static Config config;
inline void defaults(){
    // flames: tall for the narrow tongues (21-24, 27), wide for the bushy ones (20, 25, 26, 28)
    for(unsigned b=0x20;b<=0x28;++b){config.bank[b].kind=Flame;config.bank[b].style=(b==0x20||b==0x25||b==0x26||b==0x28)?kWide:kTall;}
    config.bank[0x0c]={Burst,kExplosion,1.3f,1.f,0xffffffffu};
    // core bank 05 (CORE00.ESP): quads 0-9 a 10-frame flame (burning enemies: 0005 on the R10D zombies, 0405/0505 of a
    // flame round, 0205/0305 after an explosion, all palette 0 = clut 7A11), 10-18 small stars/dots. Only palette 0
    // and quads 0-9 are fire (bank05Fire). Integration fix 2026-11-08: 0905 (record 9, class 00, flags B803,
    // palette 1 = clut 7A51, tpage 003E) is the twinkle of a pickup item (key in the R100 key box, items in R123/
    // R124/R317 ...); v1-v6 turned it into an HD ember/fireball.
    config.bank[0x05]={Flame,kTall,0.85f,1.f,0xffffffffu};   // v6: x0.85 (the R30C fire after a flame round stood ~2x the native height)
    // v5: 1.15 x the native square (1.3 in v4 covered more than the native blobs in the R10D intro), the quarter-added
    // (abr 3) billows at 0.45
    for(unsigned abr=0;abr<4;++abr)config.core03[3][abr]={Burst,kExplosion,1.15f,abr==3?0.45f:1.f,0xffffffffu};
    // smoke is SUBTRACTED like the native (abr 2): tint x alpha is taken off the scene, so white-hot fire under it stays
    // warm (v2 alpha-blended a dark grey: a dirty grey-green patch over the intro fireballs)
    config.core03[1][2]={Smoke,kSmoke,1.35f,0.8f,0xff5a5856u};    // dim grey palette (max 7/31): fire / explosion smoke
    config.core03[0][2]={Smoke,kSmoke,1.35f,0.85f,0xff9a9690u};   // grey palette (max 30/31): dark explosion smoke
}
inline void loadConfig(){
    if(config.loaded)return;config.loaded=true;for(auto& v:config.sparkFrom)v=-1;defaults();
    std::ifstream file(root+"/model-pack/fx-fire/fire.txt");std::string line;
    auto kindOf=[](const std::string& k){return k=="flame"?Flame:k=="burst"?Burst:k=="smoke"?Smoke:None;};
    auto styleOf=[](const std::string& s){return std::uint8_t(s=="wide"?kWide:s=="smoke"?kSmoke:s=="explosion"?kExplosion:kTall);};
    while(std::getline(file,line)){std::istringstream in(line);std::string key;if(!(in>>key)||key[0]=='#')continue;
        if(key=="sheet"){unsigned m=0,c=1,r=1,n=1;float w=1,h=1,tw=1,th=1;if(in>>m>>c>>r>>n>>w>>h>>tw>>th&&m<kMaterials&&tw>0&&th>0){config.sheet[m]={c,r,n,w/tw,h/th};}}
        else if(key=="flame_base")in>>config.flameBase;
        else if(key=="spark"){unsigned k=0;float a=0,e=1;if(in>>k>>a>>e&&k<16){config.sparkAngle[k]=a;config.sparkElong[k]=e;}}
        else if(key=="flame_size"){unsigned s=0;float w=1,h=1;if(in>>s>>w>>h&&s<2){config.flameWidth[s]=w;config.flameHeight[s]=h;}}
        else if(key=="flame_rate")in>>config.flameRate;
        else if(key=="loop_fps")in>>config.loopFps[0]>>config.loopFps[1];
        else if(key=="glow")in>>config.glow;
        else if(key=="bank05_quads")in>>config.bank05Quads;
        else if(key=="bank05_palettes"){std::string m;in>>m;config.bank05Palettes=std::uint32_t(std::strtoul(m.c_str(),nullptr,16));}
        else if(key=="spark_rate")in>>config.sparkRate;
        else if(key=="sparks"){int v=1;in>>v;config.sparks=v!=0;}
        else if(key=="embers"){int v=1;in>>v;config.embers=v!=0;}
        // bank <hex id> <none|flame|burst|smoke> <tall|wide|explosion|smoke> <scale> <alpha> [tint hex]
        else if(key=="bank"||key=="core03"){std::string id,kind,style;float scale=1,alpha=1;std::string tint="ffffffff";in>>id>>kind>>style>>scale>>alpha>>tint;
            Rule rule{kindOf(kind),styleOf(style),scale,alpha,std::uint32_t(std::strtoul(tint.c_str(),nullptr,16))};
            if(key=="bank"){const unsigned b=unsigned(std::strtoul(id.c_str(),nullptr,16));if(b<256)config.bank[b]=rule;}
            else{unsigned p=0,abr=0;if(sscanf_s(id.c_str(),"%u/%u",&p,&abr)==2&&p<16&&abr<4)config.core03[p][abr]=rule;}}
    }
}
inline std::shared_ptr<ModelAsset> pack(){
    auto asset=getModel("fx-fire");
    if(!asset||asset->mesh.materials.size()!=kMaterials||asset->textures.size()!=kMaterials){once("fx-fire-pack","HD fire pack unavailable (model-pack/fx-fire round 2, 9 materials): native fire kept");return nullptr;}
    for(const auto& t:asset->textures)if(!t.stp){once("fx-fire-stp","HD fire pack needs the white stp maps: native fire kept");return nullptr;}
    loadConfig();return asset;
}

// ---------------------------------------------------------------- native banks
struct Bank{std::uint8_t id=0;unsigned groups=0,quads=0;const std::uint8_t* quad=nullptr;};
inline bool findBank(std::uint8_t id,Bank& out){
    for(std::uintptr_t at=gameBase+0x6465a0;at<gameBase+0x646720;at+=12){
        if(read<std::uint8_t>(reinterpret_cast<void*>(at),8)!=id)continue;
        const auto* header=read<const std::uint8_t*>(reinterpret_cast<void*>(at));if(!accessible(header,8))return false;
        out.id=id;out.groups=read<std::uint16_t>(header,0);out.quads=read<std::uint16_t>(header,2);out.quad=header+8+out.groups*8;
        return out.groups<512&&out.quads&&out.quads<256&&accessible(out.quad,out.quads*4);
    }
    return false;
}
// the frame shown: the bank quad whose texel origin is the packet's smallest u/v
inline int quadOf(const Bank& bank,const std::uint8_t* packet){
    unsigned u=255,v=255;for(unsigned k=0;k<4;++k){u=(std::min)(u,unsigned(packet[12+8*k]));v=(std::min)(v,unsigned(packet[13+8*k]));}
    for(unsigned q=0;q<bank.quads;++q)if(bank.quad[q*4]==u&&bank.quad[q*4+1]==v)return int(q);
    return -1;
}

// per-build caches (perf v5): the bank table changes only on room loads; one VirtualQuery per OT/packet page and build
inline bool inPool(const std::uint8_t* e){const auto a=reinterpret_cast<std::uintptr_t>(e),start=gameBase+0x646720;return a>=start&&a<start+96u*0xd4&&(a-start)%0xd4==0;}
struct BankCache{std::uint32_t serial=~0u;std::uint8_t state[256]{};Bank bank[256];};static BankCache bankCache;
inline bool cachedBank(std::uint8_t id,Bank& out){
    const auto serial=re3bridge::BuildSerial();if(bankCache.serial!=serial){bankCache.serial=serial;std::memset(bankCache.state,0,sizeof(bankCache.state));}
    if(!bankCache.state[id])bankCache.state[id]=findBank(id,bankCache.bank[id])?1:2;
    if(bankCache.state[id]!=1)return false;out=bankCache.bank[id];return true;
}
struct PageCheck{std::uint32_t serial=~0u;std::uintptr_t pages[16]{};unsigned count=0;};static PageCheck pageCheck;
inline bool writablePage(const void* p,std::size_t bytes){
    const auto serial=re3bridge::BuildSerial();if(pageCheck.serial!=serial){pageCheck.serial=serial;pageCheck.count=0;}
    const auto first=reinterpret_cast<std::uintptr_t>(p)&~std::uintptr_t(0xfff),last=(reinterpret_cast<std::uintptr_t>(p)+bytes-1)&~std::uintptr_t(0xfff);
    for(auto page=first;page<=last;page+=0x1000){bool known=false;for(unsigned k=0;k<pageCheck.count;++k)if(pageCheck.pages[k]==page){known=true;break;}
        if(known)continue;if(!accessible(reinterpret_cast<void*>(page),0x1000,true))return false;if(pageCheck.count<16)pageCheck.pages[pageCheck.count++]=page;}
    return true;
}
// the OT slot whose head is this just-emitted single FT4 (re3extra::emittedSinglePacket without its two VirtualQuery calls)
inline bool headBucket(std::uint8_t* packet,std::uint32_t* orderingTable,unsigned& bucket){
    if(!orderingTable||!writablePage(packet,40)||!writablePage(orderingTable,1024*4)||re3bridge::detail::Signature(packet))return false;
    if((read<std::uint32_t>(packet)&3u)!=3u||(packet[7]&0xfcu)!=0x2c)return false;
    const auto address=std::uint32_t(reinterpret_cast<std::uintptr_t>(packet))&~3u;
    for(unsigned n=0;n<1024;++n)if((orderingTable[n]&~3u)==address){bucket=n;return true;}
    return false;
}

// ---------------------------------------------------------------- state
struct Spark{Vec3 pos{},vel{};float size=0;std::uint32_t born=0,life=30;std::uint8_t cell=0;};
// round 2: two continuous loop layers per hearth (own clock, rate, mirror), no random cell order any more
struct FlameState{std::uint64_t generation=0;std::uint32_t seed=0,lastTick=0,nextSpark=0;float t[2]{},rate[2]{},wobble=0,offsetB=0;std::uint8_t style=kTall;bool flip[2]{};std::vector<Spark> sparks;};
static std::unordered_map<std::uintptr_t,FlameState> flames;
static std::uint32_t tick=0,fireRoom=~0u;
static std::uint64_t replaced=0,refused=0,flameSprites=0,burstSprites=0,smokeSprites=0;
static double spentMs=0;static unsigned spentFrames=0;
static unsigned calibrationLines=0,classLines=0;
inline std::uint32_t hashU(std::uint32_t x){x^=x>>16;x*=0x7feb352du;x^=x>>15;x*=0x846ca68bu;x^=x>>16;return x;}
inline float rnd(std::uint32_t& s){s=s*1664525u+1013904223u;return float((s>>8)&0xffff)/65535.f;}

// ---------------------------------------------------------------- camera (once per build)
struct Camera{bool ok=false;std::uint32_t serial=~0u;re3model::Projection base;float inv[9]{};Vec3 right{},up{};};
static Camera camera;
inline bool snapCamera(){
    const auto serial=re3bridge::BuildSerial();if(camera.serial==serial)return camera.ok;
    camera.serial=serial;camera.ok=false;
    const auto gte=read<const void*>(reinterpret_cast<void*>(gameBase+0x1349c8));if(!accessible(gte,0x100))return false;
    re3bridge::ActorContext context;context.camera=read<re3bridge::Matrix32>(reinterpret_cast<void*>(gameBase+0x11c398));
    std::memcpy(context.gte.data(),gte,context.gte.size());
    camera.base=actorProjection(context);
    if(!re3model::SetNativeProjectionRegisters(camera.base,context.gte.data(),context.gte.size()))return false;
    camera.base.shade=nullptr;camera.base.shadeContext=nullptr;
    const float* m=camera.base.camera.m;
    const float a=m[0],b=m[1],c=m[2],d=m[4],e=m[5],f=m[6],g=m[8],h=m[9],i=m[10];
    const float det=a*(e*i-f*h)-b*(d*i-f*g)+c*(d*h-e*g);if(std::fabs(det)<1.e-9f)return false;
    const float k=1.f/det;
    const float inv[9]={(e*i-f*h)*k,(c*h-b*i)*k,(b*f-c*e)*k,(f*g-d*i)*k,(a*i-c*g)*k,(c*d-a*f)*k,(d*h-e*g)*k,(b*g-a*h)*k,(a*e-b*d)*k};
    std::memcpy(camera.inv,inv,sizeof(inv));
    camera.right={inv[0],inv[3],inv[6]};camera.up={-inv[1],-inv[4],-inv[7]};   // camera +x, camera -y (screen up) in world
    return camera.ok=true;
}
// a camera-space point back to the world
inline Vec3 toWorld(const Vec3& c){const float* m=camera.base.camera.m;const Vec3 d{c.x-m[3],c.y-m[7],c.z-m[11]};const float* v=camera.inv;
    return {v[0]*d.x+v[1]*d.y+v[2]*d.z,v[3]*d.x+v[4]*d.y+v[5]*d.z,v[6]*d.x+v[7]*d.y+v[8]*d.z};}

// ---------------------------------------------------------------- HD geometry of one replaced packet
struct Quad{Vec3 c[4];float uv[8];std::uint32_t argb;unsigned material;};
inline std::uint32_t argbOf(float r,float g,float b,float a){auto q=[](float v){return std::uint32_t((std::min)(1.f,(std::max)(0.f,v))*255.f+.5f);};return (q(a)<<24)|(q(r)<<16)|(q(g)<<8)|q(b);}
inline std::uint32_t scaledArgb(std::uint32_t tint,float k,float alpha){return argbOf(float((tint>>16)&255)/255.f*k,float((tint>>8)&255)/255.f*k,float(tint&255)/255.f*k,alpha);}
inline void cellUV(unsigned material,unsigned index,bool flip,float uv[8]){
    const auto& s=config.sheet[material];index%=(std::max)(1u,s.count);const unsigned c=index%s.cols,r=index/s.cols;
    const float e=0.75f/2048.f,u0=c*s.cw+e,v0=r*s.ch+e,u1=(c+1)*s.cw-e,v1=(r+1)*s.ch-e;
    const float x0=flip?u1:u0,x1=flip?u0:u1;const float out[8]={x0,v0,x1,v0,x1,v1,x0,v1};std::memcpy(uv,out,sizeof(out));
}
// a screen-aligned sprite: `at` in the world, (ax, ay) = anchor inside the cell (0..1, v down), roll around the anchor
inline void sprite(std::vector<Quad>& quads,unsigned material,unsigned cell,bool flip,const Vec3& at,float w,float h,float ax,float ay,float roll,std::uint32_t argb){
    Quad q;q.material=material;q.argb=argb;cellUV(material,cell,flip,q.uv);
    const float cs=std::cos(roll),sn=std::sin(roll);const Vec3 r=camera.right*cs+camera.up*sn,u=camera.up*cs-camera.right*sn;
    const float tx[4]={0,1,1,0},ty[4]={0,0,1,1};
    for(unsigned k=0;k<4;++k)q.c[k]=at+r*(w*(tx[k]-ax))+u*(h*(ay-ty[k]));
    quads.push_back(q);
}
inline std::uint16_t fixedBucket(void* context,std::uint16_t,std::uint16_t,std::uint16_t){return *static_cast<const std::uint16_t*>(context);}
// the pack's materials with the runtime flags (two-sided; every job draws through the native semi-transparent pass:
// fire abr 1 = added, smoke abr 2 = subtracted), built once per loaded asset
static std::weak_ptr<ModelAsset> materialsOf;static re3model::Mesh materialsMesh;
inline const re3model::Mesh& packMaterials(const std::shared_ptr<ModelAsset>& asset){
    if(materialsOf.lock()!=asset){materialsMesh=re3model::Mesh{};materialsMesh.materials=asset->mesh.materials;
        for(unsigned k=0;k<kMaterials;++k)materialsMesh.materials[k].flags=re3model::TwoSided;materialsOf=asset;}
    return materialsMesh;
}
// The sprites of a build are prepared at their packets (billboard hook) and uploaded together after the effect loop
// (flushBatch, from update()): one stream upload per frame instead of one per sprite (v2 measured 0.41 ms/frame with an
// upload per sprite in the R10D intro). Each sprite is one slice of the shared frame and one job, inserted into the OT
// slot its native packet was linked to (EnqueueAbsoluteBatch: retired with the OT clear, like the blood batches); the
// native packet is then collapsed to a point. NOT ReplacePacket without a slot: such in-place sources are only retired
// when the packet memory is overwritten, effect packets often are not, and the bridge then ran out of frames and kept
// every HD model native (QA run fire-v3-hd, MODEL_STATS capacity fallbacks from the first camera change).
struct PendingSprite{std::uint8_t* packet=nullptr;std::uint32_t* slot=nullptr;std::uint32_t check[9]{};unsigned bucket=0,firstRun=0,runCount=0,abr=1;};
static std::vector<PendingSprite> pendingSprites;static re3model::FrameGeometry pendingGeometry;static std::uint32_t pendingSerial=~0u;
static std::shared_ptr<ModelAsset> pendingAsset;
inline bool queueSprite(std::uint8_t* packet,std::uint32_t* orderingTable,unsigned bucket,const std::vector<Quad>& quads,unsigned abr,const std::shared_ptr<ModelAsset>& asset){
    if(quads.empty())return false;
    const auto serial=re3bridge::BuildSerial();
    if(pendingSerial!=serial){pendingSprites.clear();pendingGeometry=re3model::FrameGeometry{};pendingSerial=serial;}   // another build's leftovers: native kept
    // round 2 (perf): the camera-facing quads are projected here directly (the projection of re3model::PrepareFrame /
    // ProjectCameraPoint in SmoothSubpixel mode: screen = cam.xy * H / z + offset, viewport scale, half-pixel offset; z =
    // 1 - near / z, rhw = 1 / z, diffuse = the quad tint) instead of one general PrepareFrame per sprite: the two loop layers
    // + glow doubled the quads and PrepareFrame cost 0.48 ms / frame in the R10D intro. A quad crossing the near plane is
    // left out (a sprite at the camera). Runs are grouped by material (additive / subtractive sprites: order-free).
    (void)asset;
    const auto& P=camera.base;
    PendingSprite sprite;sprite.firstRun=std::uint32_t(pendingGeometry.runs.size());
    for(unsigned m=0;m<kMaterials;++m){const auto first=std::uint32_t(pendingGeometry.vertices.size());
        for(const auto& q:quads){if(q.material!=m)continue;
            re3model::DrawVertex v[4];bool ok=true;
            for(unsigned k=0;k<4&&ok;++k){const auto c=re3model::TransformPoint(P.camera,q.c[k]);
                if(!(c.z>=P.nearPlane)||!std::isfinite(c.x)||!std::isfinite(c.y)){ok=false;break;}
                const float z=(std::min)(65535.f,c.z);
                const float sx=(std::max)(-1024.f,(std::min)(1023.f,(std::max)(-32768.f,(std::min)(32767.f,c.x))*P.h/z+P.offsetX));
                const float sy=(std::max)(-1024.f,(std::min)(1023.f,(std::max)(-32768.f,(std::min)(32767.f,c.y))*P.h/z+P.offsetY));
                v[k]={sx*P.scaleX+P.pixelOffsetX,sy*P.scaleY+P.pixelOffsetY,1.f-P.nearPlane/z,P.perspectiveCorrect?1.f/z:1.f,q.argb,q.uv[k*2],q.uv[k*2+1]};}
            if(!ok)continue;
            for(const unsigned i:{0u,1u,2u,0u,2u,3u})pendingGeometry.vertices.push_back(v[i]);}
        const auto count=std::uint32_t(pendingGeometry.vertices.size())-first;
        if(count)pendingGeometry.runs.push_back({first,count,m});}
    sprite.runCount=std::uint32_t(pendingGeometry.runs.size())-sprite.firstRun;
    if(!sprite.runCount)return false;
    sprite.packet=packet;sprite.slot=orderingTable+bucket;sprite.bucket=bucket;sprite.abr=abr;
    std::memcpy(sprite.check,packet+4,sizeof(sprite.check));   // the whole packet after its link: untouched at the flush (another hook that collapsed it, e.g. the muzzle smoke of fx_muzzle.h, wins)
    pendingSprites.push_back(sprite);pendingAsset=asset;
    return true;
}
inline void flushBatch(){
    if(pendingSprites.empty()){pendingAsset.reset();return;}
    const LARGE_INTEGER start=[]{LARGE_INTEGER t;QueryPerformanceCounter(&t);return t;}();
    struct Timer{LARGE_INTEGER s;~Timer(){LARGE_INTEGER e,f;QueryPerformanceCounter(&e);QueryPerformanceFrequency(&f);spentMs+=double(e.QuadPart-s.QuadPart)*1000.0/double(f.QuadPart);}}timer{start};
    struct Clear{~Clear(){pendingSprites.clear();pendingGeometry=re3model::FrameGeometry{};pendingAsset.reset();}}clear;
    if(pendingSerial!=re3bridge::BuildSerial()||!pendingAsset||!active()||!modelRenderer.resetDevice(gameDevice)){refused+=pendingSprites.size();return;}
    pendingGeometry.slices.clear();
    for(const auto& sp:pendingSprites)pendingGeometry.slices.push_back({std::uint16_t(sp.bucket),sp.firstRun,sp.runCount});
    HRESULT hr;auto gpu=modelRenderer.upload(packMaterials(pendingAsset),std::move(pendingGeometry),pendingAsset->textures,false,&hr);
    if(!gpu||gpu->slices.size()!=pendingSprites.size()){once("fire-upload","HD fire upload failed; native sprites kept");refused+=pendingSprites.size();return;}
    std::vector<re3bridge::AbsoluteJob> jobs;std::vector<std::uint8_t*> hidden;jobs.reserve(pendingSprites.size());hidden.reserve(pendingSprites.size());
    for(std::size_t n=0;n<pendingSprites.size();++n){const auto& sp=pendingSprites[n];
        // the native packet must still be the one the effect loop wrote (no other code touched it since)
        if(std::memcmp(sp.packet+4,sp.check,sizeof(sp.check))||re3bridge::detail::Signature(sp.packet)){++refused;continue;}
        auto job=std::make_shared<ModelJob>();job->geometry=gpu;job->slice=n;job->name="fx-fire";
        job->options.nativeSemiTransparent=true;job->options.abr=sp.abr;
        jobs.push_back({sp.slot,{std::uint16_t(sp.bucket),reinterpret_cast<std::uintptr_t>(job.get()),job}});hidden.push_back(sp.packet);
    }
    if(jobs.empty())return;
    if(!re3bridge::EnqueueAbsoluteBatch(jobs)){once("fire-enqueue","HD fire skipped for a frame (bridge refused the batch); native sprites kept");refused+=jobs.size();return;}
    // published: the native sprites draw nothing now (all four corners on the first, as blood_fx collapses 0C00)
    for(auto* q:hidden){const auto xy0=read<std::uint32_t>(q,8);std::memcpy(q+16,&xy0,4);std::memcpy(q+24,&xy0,4);std::memcpy(q+32,&xy0,4);}
    replaced+=jobs.size();
}

// ---------------------------------------------------------------- per kind
// native screen quad -> world: the base point (bottom centre) / centre and the world size of the native square.
// Round 2: the packet corners are whole native pixels (320x240): a moving or tiny effect jumped by up to 1/2 px (= 4 px at
// 2560) and its size by one native pixel a frame. The box is now taken RELATIVE to the effect's own (float) projected
// anchor and smoothed per effect, so the HD sprite follows the anchor smoothly.
struct Smooth{std::uint64_t generation=0;std::uint32_t seen=0;bool init=false;float oc=0,ob=0,oy=0,pw=0,ph=0;
    int lastQ=-1;std::uint32_t changeTick=0;float est=2.f;};
static std::unordered_map<std::uintptr_t,Smooth> smooths;
struct Footprint{Vec3 base{},centre{};float w=0,h=0,z=0;};
inline bool footprint(const std::uint8_t* packet,const std::uint8_t* effect,Footprint& out,Smooth* sm=nullptr){
    int x0=32767,y0=32767,x1=-32768,y1=-32768;
    for(unsigned k=0;k<4;++k){const int x=read<std::int16_t>(packet,8+8*k),y=read<std::int16_t>(packet,10+8*k);x0=(std::min)(x0,x);y0=(std::min)(y0,y);x1=(std::max)(x1,x);y1=(std::max)(y1,y);}
    if(x1<=x0||y1<=y0||x1-x0>2000||y1-y0>2000)return false;
    const Vec3 p{float(read<std::int32_t>(effect,0x48)),float(read<std::int32_t>(effect,0x4c)),float(read<std::int32_t>(effect,0x50))};
    const auto c=re3model::TransformPoint(camera.base.camera,p);if(c.z<64.f)return false;
    const float h=camera.base.h,ox=camera.base.offsetX,oy=camera.base.offsetY,k=c.z/h;
    const float sx=c.x/k+ox,sy=c.y/k+oy;   // the anchor on the native screen, unrounded
    float oc=float(x0+x1)*0.5f-sx,ob=float(y1)-sy,om=float(y0+y1)*0.5f-sy,pw=float(x1-x0),ph=float(y1-y0);
    if(sm){
        if(!sm->init){sm->init=true;sm->oc=oc;sm->ob=ob;sm->oy=om;sm->pw=pw;sm->ph=ph;}
        else{const float a=0.22f;sm->oc+=(oc-sm->oc)*a;sm->ob+=(ob-sm->ob)*a;sm->oy+=(om-sm->oy)*a;sm->pw+=(pw-sm->pw)*a;sm->ph+=(ph-sm->ph)*a;}
        oc=sm->oc;ob=sm->ob;om=sm->oy;pw=sm->pw;ph=sm->ph;sm->seen=tick;
    }
    out.z=c.z;out.w=pw*k;out.h=ph*k;
    out.base=toWorld({(sx+oc-ox)*k,(sy+ob-oy)*k,c.z});
    out.centre=toWorld({(sx+oc-ox)*k,(sy+om-oy)*k,c.z});
    return true;
}
inline Smooth& smoothOf(std::uintptr_t address,std::uint64_t generation){
    auto& sm=smooths[address];if(sm.generation!=generation){sm=Smooth{};sm.generation=generation;}return sm;
}
inline FlameState& flameOf(std::uintptr_t address,std::uint64_t generation,std::uint8_t style){
    auto& f=flames[address];
    if(f.generation!=generation||!f.seed){
        f=FlameState{};f.generation=generation;f.seed=hashU(std::uint32_t(address)*2654435761u^std::uint32_t(generation)*40503u^fireRoom)|1u;
        f.style=style;f.wobble=rnd(f.seed)*6.2831853f;
        // two layers: each its own loop (a/b sheet), random start, own rate (+-8 %), own mirror; together they never repeat
        for(unsigned l=0;l<2;++l){const unsigned m=(style==kWide?kWideA:kTallA)+l;const float n=float((std::max)(1u,config.sheet[m].count));
            f.t[l]=rnd(f.seed)*n;f.rate[l]=config.loopFps[l]*(0.92f+0.16f*rnd(f.seed))/30.f;f.flip[l]=rnd(f.seed)<0.5f;}
        f.offsetB=(rnd(f.seed)<0.5f?-1.f:1.f)*(0.12f+0.1f*rnd(f.seed));   // the second layer: a smaller tongue cluster beside the first
        f.lastTick=tick;f.nextSpark=tick+unsigned(rnd(f.seed)*40.f);
    }
    return f;
}
inline void stepFlame(FlameState& f,const Footprint& fp){
    if(tick-f.lastTick>90)f.lastTick=tick-90;
    while(f.lastTick!=tick){++f.lastTick;
        for(unsigned l=0;l<2;++l){const unsigned m=(f.style==kWide?kWideA:kTallA)+l;const float n=float((std::max)(1u,config.sheet[m].count));
            f.t[l]+=f.rate[l]/float(debugSlow);while(f.t[l]>=n)f.t[l]-=n;}
        for(auto& s:f.sparks){s.vel.y-=0.55f/float(debugSlow);s.vel=s.vel*0.985f;s.pos=s.pos+s.vel*(1.f/float(debugSlow));}
        f.sparks.erase(std::remove_if(f.sparks.begin(),f.sparks.end(),[&](const Spark& s){return (tick-s.born)/debugSlow>s.life;}),f.sparks.end());
        if(config.sparks&&tick>=f.nextSpark&&f.sparks.size()<10){
            Spark s;s.born=tick;s.life=unsigned(22+rnd(f.seed)*30);s.cell=std::uint8_t(rnd(f.seed)*15.99f);s.size=fp.w*(0.035f+0.04f*rnd(f.seed));
            s.pos=fp.base+camera.right*((rnd(f.seed)-0.5f)*fp.w*0.5f)+camera.up*(fp.h*(0.15f+0.35f*rnd(f.seed)));
            const float speed=fp.w*(0.025f+0.03f*rnd(f.seed));
            s.vel=Vec3{0,-speed,0}+camera.right*((rnd(f.seed)-0.5f)*speed*0.8f);
            f.sparks.push_back(s);
            f.nextSpark=tick+unsigned((15.f+rnd(f.seed)*30.f)*1.6f/(std::max)(0.1f,config.sparkRate)*float(debugSlow));
        }
    }
}
inline std::uint32_t warmArgb(float k,float alpha){return argbOf(1.f*k,0.58f*k,0.27f*k,alpha);}
inline void buildFlame(std::vector<Quad>& quads,FlameState& f,const Footprint& fp,const Rule& rule,float shade){
    const unsigned style=f.style;const float w=fp.w*config.flameWidth[style==kWide]*rule.scale,h=fp.w*config.flameHeight[style==kWide]*rule.scale;
    const float tt=float(tick)/float(debugSlow);
    // the loops carry the motion now: only a slow, small breathing of the brightness
    const float flick=0.94f+0.04f*std::sin(tt*0.37f+f.wobble)+0.02f*std::sin(tt*0.91f+f.wobble*1.7f);
    const float k=shade*flick*rule.alpha;
    // soft halo (round 2, user: "a little blur/bloom"): a warm glow around the fire, drawn first (under the flame)
    if(config.glow>0)sprite(quads,kGlow,0,false,fp.base+camera.up*(h*0.30f),w*(style==kWide?1.25f:1.9f),w*(style==kWide?1.25f:1.9f)*(style==kWide?0.8f:1.f),0.5f,0.5f,0,
        warmArgb(config.glow*shade*(0.9f+0.1f*std::sin(tt*0.53f+f.wobble)),1.f));
    // the ember bed under the flame
    if(config.embers&&config.sheet[kEmbers].count)sprite(quads,kEmbers,(f.seed>>9)%config.sheet[kEmbers].count,f.flip[0],fp.base+camera.up*(fp.w*0.02f),w*0.95f*(style==kWide?0.8f:1.2f),w*0.95f*0.26f,0.5f,0.58f,0,
        scaledArgb(rule.tint,0.6f*shade*(0.9f+0.1f*std::sin(tt*0.7f+f.wobble)),0.5f));
    // two continuous loops, each cross-faded between its neighbouring frames (fractional time)
    const float weight[2]={0.66f,0.32f};
    for(unsigned l=0;l<2;++l){const unsigned m=(style==kWide?kWideA:kTallA)+l;const unsigned n=(std::max)(1u,config.sheet[m].count);
        const unsigned i0=unsigned(f.t[l])%n,i1=(i0+1)%n;const float blend=f.t[l]-std::floor(f.t[l]);
        const Vec3 at=fp.base+camera.right*(l?w*f.offsetB:0.f);const float hl=l?h*0.85f:h,wl=l?w*0.7f:w;
        sprite(quads,m,i0,f.flip[l],at,wl,hl,0.5f,config.flameBase,0,scaledArgb(rule.tint,k*weight[l],1.f-blend));
        sprite(quads,m,i1,f.flip[l],at,wl,hl,0.5f,config.flameBase,0,scaledArgb(rule.tint,k*weight[l],blend));}
    for(const auto& s:f.sparks){const float age=float(tick-s.born)/float(debugSlow)/float(s.life);const float fade=age<0.7f?1.f:(1.f-age)/0.3f;
        // streak cells: tail -> head along the motion
        float roll=0;const float e=config.sparkElong[s.cell];
        if(e>1.6f){const float sx=re3model::Dot(camera.right,s.vel),sy=re3model::Dot(camera.up,s.vel);roll=std::atan2(sy,sx)+config.sparkAngle[s.cell];}
        sprite(quads,kSparks,s.cell,false,s.pos,s.size*3.f,s.size*3.f,0.5f,0.5f,roll,scaledArgb(0xffffffffu,shade,fade));}
}

// ---------------------------------------------------------------- the billboard hook (one line in re3extra::billboardHook)
inline void billboard(void* packets,void* effectPointer,int count,std::uint32_t* orderingTable,int command){
    if(!enabled||count!=1||(command&0xfc)!=0x2c||!active())return;
    // perf (v5): no VirtualQuery per sprite (Accessible) for pointers the native emitter itself just used: the effect
    // must be a slot of the pool, the packet/OT are checked once per build (otChecked)
    const auto* effect=static_cast<const std::uint8_t*>(effectPointer);if(!inPool(effect))return;
    const auto key=read<std::uint16_t>(effect,0x28);const std::uint8_t bankId=std::uint8_t(key&0xff);
    if(!config.loaded)loadConfig();
    Rule rule=config.bank[bankId];
    const unsigned palette=read<std::uint8_t>(effect,0xb3)&15u,abr=(read<std::uint16_t>(effect,0x46)>>5)&3u;
    if(bankId==3)rule=config.core03[palette][abr];
    // a subtractive sprite of a fireball bank darkens the scene: its smoke (explosion 0C spawns 090C with palette 1, abr 2)
    if(rule.kind==Burst&&abr==2)rule={Smoke,kSmoke,1.35f,0.85f,0xff7a7672u};
    if(rule.kind==None)return;
    LARGE_INTEGER start;   // round 2: timed from after pack(): the one-time PNG load of the pack is logged apart (FIRE_PACK_LOAD)
    auto* packet=static_cast<std::uint8_t*>(packets);
    unsigned bucket=0;if(!headBucket(packet,orderingTable,bucket))return;
    if(read<std::uint16_t>(packet,14)!=read<std::uint16_t>(effect,0x44))return;   // the effect's own texture
    {LARGE_INTEGER a,b,f;QueryPerformanceCounter(&a);auto loaded=pack();QueryPerformanceCounter(&b);QueryPerformanceFrequency(&f);
        static bool loadLogged=false;const double ms=double(b.QuadPart-a.QuadPart)*1000.0/double(f.QuadPart);if(ms>5&&!loadLogged){loadLogged=true;log("FIRE_PACK_LOAD ms=%.1f (one-time load of model-pack/fx-fire, not in FIRE_PERF)",ms);}
        if(!loaded)return;}
    QueryPerformanceCounter(&start);
    auto asset=pack();if(!asset||!snapCamera())return;
    Bank bank;if(!cachedBank(bankId,bank))return;
    const int q=quadOf(bank,packet);
    // bank 05: only its flame frames in the fire palette; the item twinkle 0905 (palette 1) and the stars (quads 10+)
    // stay native (integration fix 2026-11-08)
    if(bankId==5&&(q<0||unsigned(q)>=config.bank05Quads||!(config.bank05Palettes&(1u<<palette)))){
        static unsigned keptLines=0;if(keptLines<8){static std::unordered_map<std::uint32_t,unsigned> seen;if(!seen[key|(palette<<16)]++){++keptLines;
            log("FIRE_KEEP_NATIVE key=%04x bank=05 palette=%u quad=%d class=%02x (not fire: item twinkle / star, native kept)",key,palette,q,read<std::uint8_t>(effect));}}
        return;}
    // the native packet colour (808080 = 1) keeps the native fades
    const float shade=(std::min)(1.f,float((std::max)((std::max)(packet[4],packet[5]),packet[6]))/128.f);
    const auto address=reinterpret_cast<std::uintptr_t>(effect);
    const auto generationAt=effectGeneration.find(address);const std::uint64_t generation=generationAt==effectGeneration.end()?0:generationAt->second;
    auto& sm=smoothOf(address,generation);
    Footprint fp;if(!footprint(packet,effect,fp,&sm))return;
    static std::vector<Quad> quads;quads.clear();unsigned mode=1;   // native semi-transparency: 1 added, 2 subtracted
    if(rule.kind==Flame&&config.sparkFrom[bankId]>=0&&q>=config.sparkFrom[bankId])rule.kind=Ember;
    if(rule.kind==Ember){
        // a native spark / glowing dot: one HD ember particle at its place, the cell fixed per effect
        std::uint32_t seed=hashU(std::uint32_t(address)^std::uint32_t(generation)*2654435761u)|1u;const unsigned cell=unsigned(rnd(seed)*15.99f);
        const float side=(std::max)(fp.w,fp.h)*2.4f*rule.scale;
        sprite(quads,kSparks,cell,false,fp.centre,side,side,0.5f,0.5f,rnd(seed)*6.2831853f,scaledArgb(rule.tint,shade,1.f));++burstSprites;
    }else if(rule.kind==Flame){
        auto& f=flameOf(address,generation,rule.style);stepFlame(f,fp);buildFlame(quads,f,fp,rule,shade);++flameSprites;
    }else{
        // one-shot billows / explosions: the native frame (quad index) gives the progress through the HD flipbook.
        // Round 2: continuous between native frames (the time since the quad changed / the measured ticks per quad), so
        // the cross-fade moves every game frame instead of stepping with the native animation.
        float cont=q<0?0.5f*float(bank.quads-1):float(q);
        if(q>=0){if(sm.lastQ<0){sm.lastQ=q;sm.changeTick=tick;}
            else if(q!=sm.lastQ){const unsigned dt=tick-sm.changeTick;if(dt>=1&&dt<=8)sm.est=sm.est*0.5f+float(dt)*0.5f;sm.lastQ=q;sm.changeTick=tick;}
            if(q+1<int(bank.quads))cont=float(q)+(std::min)(0.95f,float(tick-sm.changeTick)/(std::max)(1.f,sm.est));}
        const float progress=cont/float((std::max)(1u,bank.quads-1));
        const unsigned material=rule.kind==Smoke?kSmoke:kExplosion;const unsigned n=(std::max)(1u,config.sheet[material].count);
        const float at=progress*float(n-1);const unsigned f0=(std::min)(n-1,unsigned(at)),f1=(std::min)(n-1,f0+1);const float blend=at-float(f0);
        std::uint32_t seed=hashU(std::uint32_t(address)^std::uint32_t(generation)*2654435761u)|1u;const float roll=rnd(seed)*6.2831853f;const bool flip=rnd(seed)<0.5f;
        const float side=(std::max)(fp.w,fp.h)*rule.scale;
        if(rule.kind==Smoke){mode=2;
            sprite(quads,kSmoke,f0,flip,fp.centre,side,side,0.5f,0.5f,roll,scaledArgb(rule.tint,1.f,rule.alpha*shade*(1.f-blend)));
            sprite(quads,kSmoke,f1,flip,fp.centre,side,side,0.5f,0.5f,roll,scaledArgb(rule.tint,1.f,rule.alpha*shade*blend));++smokeSprites;}
        else{sprite(quads,kExplosion,f0,flip,fp.centre,side,side,0.5f,0.5f,roll,scaledArgb(rule.tint,shade*rule.alpha,1.f-blend));
            sprite(quads,kExplosion,f1,flip,fp.centre,side,side,0.5f,0.5f,roll,scaledArgb(rule.tint,shade*rule.alpha,blend));++burstSprites;}
    }
    if(calibrationLines<24&&(rule.kind==Flame?calibrationLines<12:true)){++calibrationLines;
        const Vec3 p{float(read<std::int32_t>(effect,0x48)),float(read<std::int32_t>(effect,0x4c)),float(read<std::int32_t>(effect,0x50))};
        const auto c=re3model::TransformPoint(camera.base.camera,p);
        log("FIRE_CAL key=%04x kind=%u quad=%d/%u pkt=%d,%d..%d,%d anchor_screen=%.1f,%.1f z=%.0f world=%.0fx%.0f H=%.1f off=%.1f,%.1f bucket=%u shade=%.2f palette=%u abr=%u",
            key,unsigned(rule.kind),q,bank.quads,read<std::int16_t>(packet,8),read<std::int16_t>(packet,10),read<std::int16_t>(packet,32),read<std::int16_t>(packet,34),
            c.x*camera.base.h/c.z+camera.base.offsetX,c.y*camera.base.h/c.z+camera.base.offsetY,c.z,fp.w,fp.h,camera.base.h,camera.base.offsetX,camera.base.offsetY,bucket,shade,palette,abr);}
    if(!queueSprite(packet,orderingTable,bucket,quads,mode,asset))++refused;
    LARGE_INTEGER end,freq;QueryPerformanceCounter(&end);QueryPerformanceFrequency(&freq);spentMs+=double(end.QuadPart-start.QuadPart)*1000.0/double(freq.QuadPart);
    if(classLines<40){static std::unordered_map<std::uint32_t,unsigned> seen;const std::uint32_t id=key|(palette<<16)|(abr<<20);
        if(!seen[id]++){++classLines;log("FIRE_NATIVE key=%04x bank=%02x palette=%u abr=%u class=%02x kind=%u quads=%u (native %s replaced by HD)",key,unsigned(bankId),palette,abr,read<std::uint8_t>(effect),unsigned(rule.kind),bank.quads,
            rule.kind==Flame?"flame":rule.kind==Smoke?"smoke":rule.kind==Ember?"spark":"fireball");}}
}

// ---------------------------------------------------------------- round 5: HD texture pages of the native frames
namespace pages {
struct Page{std::uint8_t bank,row;std::uint16_t w,h,scale;std::uint64_t layout;std::uint8_t quadLimit;const char* png;};
static const Page kPages[]={
#include "fx_fire_pages.inc"
};
constexpr unsigned kMaxQuads=64;
// a fire bank loaded in the bank table (rebuilt once per frame: room loads replace entries 16..31)
struct Live{const std::uint8_t* header=nullptr;std::uint8_t id=0;std::uint16_t clut=0,tpage=0,u0=0,v0=0;unsigned count=0;
    std::uint8_t qu[kMaxQuads],qv[kMaxQuads],qs[kMaxQuads];std::uint64_t layout=0,raw=0;unsigned rawBytes=0;const Page* page[4]{};};
inline std::uint64_t fnvBytes(const std::uint8_t* b,unsigned n){std::uint64_t h=0xcbf29ce484222325ull;for(unsigned i=0;i<n;++i){h^=b[i];h*=0x100000001b3ull;}return h;}
static Live live[32];static unsigned liveCount=0,liveFrame=~0u;
static std::uint64_t pageQuads=0,pageQuadsRoom=0;static unsigned bankLines=0,loadLines=0;   // log budgets (per session)
inline bool fireId(unsigned entry,std::uint8_t id){return entry<16?(id==0x03||id==0x05):((id>=0x20&&id<=0x28)||id==0x0c);}
inline std::uint8_t quadLimit(std::uint8_t id){return id==0x05?std::uint8_t(config.bank05Quads):std::uint8_t(0);}
// the page texture with mips (uiTexture() would load it without): first use of a page, or a reload after the ui cache dropped it
inline void ensureTexture(const char* png){
    if(!gameDevice)return;auto& t=uiTextures[png];if(t.missing)return;
    if(t.texture&&t.texture->GetLevelCount()>1)return;
    if(t.texture){t.texture->Release();t.texture=nullptr;t.bytes=0;}
    LARGE_INTEGER a,b,f;QueryPerformanceCounter(&a);
    t.texture=loadReplacement(gameDevice,0,png,16);t.lastUsed=uiFrameSerial;
    if(!t.texture){t.missing=true;log("FIRE_PAGE_MISSING %s (native page kept)",png);return;}
    QueryPerformanceCounter(&b);QueryPerformanceFrequency(&f);
    D3DSURFACE_DESC desc{};t.texture->GetLevelDesc(0,&desc);t.bytes=std::size_t(desc.Width)*desc.Height*4u*4u/3u;
    if(loadLines<64){++loadLines;log("FIRE_PAGE_LOAD %s %ux%u levels=%u ms=%.1f",png,desc.Width,desc.Height,t.texture->GetLevelCount(),double(b.QuadPart-a.QuadPart)*1000.0/double(f.QuadPart));}
}
inline void scan(){
    if(liveFrame==uiFrameSerial)return;liveFrame=uiFrameSerial;
    Live fresh[32];unsigned count=0;
    for(unsigned e=0;e<32;++e){
        const auto at=reinterpret_cast<const void*>(gameBase+0x6465a0+e*12);
        const auto id=read<std::uint8_t>(at,8);if(!fireId(e,id))continue;
        const auto* h=read<const std::uint8_t*>(at);
        // the same header and table bytes as last frame: keep its parse (no VirtualQuery per bank and frame; a new room
        // may put another table of the same bank at the same address)
        bool known=false;
        for(unsigned i=0;i<liveCount;++i)if(live[i].header==h&&live[i].id==id&&live[i].raw==fnvBytes(h,live[i].rawBytes)){fresh[count++]=live[i];known=true;break;}
        if(known)continue;
        if(!accessible(h,8))continue;
        const unsigned groups=read<std::uint16_t>(h,0),quads=read<std::uint16_t>(h,2);
        if(!groups||groups>256||!quads||quads>kMaxQuads||!accessible(h,8+groups*8+quads*4))continue;
        Live l;l.header=h;l.id=id;l.clut=read<std::uint16_t>(h,4);l.tpage=read<std::uint16_t>(h,6);
        l.rawBytes=8+groups*8+quads*4;l.raw=fnvBytes(h,l.rawBytes);
        std::uint8_t size[kMaxQuads]{};
        for(unsigned g=0;g<groups;++g){const unsigned start=h[8+g*8],n=h[9+g*8],s=h[11+g*8];if(!n||!s)continue;
            for(unsigned q=start;q<start+n&&q<quads;++q)size[q]=(std::max)(size[q],std::uint8_t(s));}
        const unsigned limit=quadLimit(id)?(std::min)(unsigned(quadLimit(id)),quads):quads;
        const std::uint8_t* quad=h+8+groups*8;unsigned u0=255,v0=255;
        for(unsigned q=0;q<limit;++q)if(size[q]){u0=(std::min)(u0,unsigned(quad[q*4]));v0=(std::min)(v0,unsigned(quad[q*4+1]));}
        std::uint64_t hash=0xcbf29ce484222325ull;auto mix=[&](unsigned byte){hash^=byte&255u;hash*=0x100000001b3ull;};
        for(unsigned q=0;q<limit;++q)if(size[q]){l.qu[l.count]=quad[q*4];l.qv[l.count]=quad[q*4+1];l.qs[l.count]=size[q];++l.count;
            mix(quad[q*4]-u0);mix(quad[q*4+1]-v0);mix(size[q]);}
        if(!l.count)continue;
        l.u0=std::uint16_t(u0);l.v0=std::uint16_t(v0);l.layout=hash;
        for(const auto& p:kPages)if(p.bank==id&&p.row<4){
            if(p.layout==hash){l.page[p.row]=&p;ensureTexture(p.png);
                if(bankLines<256){++bankLines;log("FIRE_PAGE bank=%02x row=%u clut=%04x tpage=%04x region=%u,%u layout=%016llx -> %s",id,p.row,l.clut+0x40u*p.row,l.tpage,u0,v0,hash,p.png);}}
            else if(bankLines<256){++bankLines;log("FIRE_PAGE_LAYOUT_MISMATCH bank=%02x row=%u layout=%016llx expected=%016llx (native page kept)",id,p.row,hash,p.layout);}}
        fresh[count++]=l;   // kept even without a page (layout mismatch): parsed and logged once, its quads stay native
    }
    std::memcpy(live,fresh,sizeof(Live)*count);liveCount=count;
}
} // namespace pages
// proxy.cpp drawUiPacket (one line, fx5 2026-11-08 fire): a quad of a native fire bank page -> the HD page of that bank
static bool pageBinding(const std::uint8_t* p,unsigned tpage,unsigned clut,float minU,float minV,float maxU,float maxV,re3ui::Binding& binding){
    if(fireMode!=1||((tpage>>7)&3u)!=0)return false;   // only 4-bit pages
    pages::scan();
    const unsigned page=tpage&0x1fu;
    for(unsigned i=0;i<pages::liveCount;++i){const auto& l=pages::live[i];
        if((l.tpage&0x1fu)!=page||clut<l.clut||((clut-l.clut)&0x3fu)||clut-l.clut>=0x100u)continue;
        const auto* pg=l.page[(clut-l.clut)>>6];if(!pg)continue;
        bool inside=false;   // the packet shows one quad of the bank (not something else on the same page)
        for(unsigned k=0;k<l.count&&!inside;++k){const float u=l.qu[k],v=l.qv[k],s=l.qs[k];inside=minU>=u-.5f&&maxU<=u+s+.5f&&minV>=v-.5f&&maxV<=v+s+.5f;}
        if(!inside)continue;
        pages::ensureTexture(pg->png);
        binding=re3ui::Binding{};binding.assetId="fx-fire-page";binding.pngPath=pg->png;binding.paletteIndex=pg->row;
        binding.originalW=pg->w;binding.originalH=pg->h;binding.offsetU=-float(l.u0);binding.offsetV=-float(l.v0);binding.scale=pg->scale;
        ++pages::pageQuads;++pages::pageQuadsRoom;
        if(trace){static std::uint32_t seen[48];static unsigned seenCount=0;const std::uint32_t key=(std::uint32_t(l.id)<<24)|(clut&0xffffu)|((unsigned(minU)&0xffu)<<16);
            bool known=false;for(unsigned s=0;s<seenCount;++s)if(seen[s]==key){known=true;break;}
            if(!known&&seenCount<48){seen[seenCount++]=key;
                log("FIRE_PAGE_QUAD bank=%02x row=%u tpage=%x clut=%x uv=%.0f,%.0f-%.0f,%.0f command=%02x rgb=%02x%02x%02x",l.id,pg->row,tpage,clut,minU,minV,maxU,maxV,p[7],p[4],p[5],p[6]);}}
        return true;
    }
    return false;
}

// ---------------------------------------------------------------- per effect-loop tick (effectLoopHook)
inline void spawnTest(){
    // native effects in front of the player through the game's own spawn 43D370(key, flags, matrix, offset, 0): a core
    // fireball (0903 palette 3), its smoke (1303 palette 1) and, when the room has bank 0C, an explosion
    const auto* player=reinterpret_cast<const std::uint8_t*>(gameBase+0x65cbf4);if(!accessible(player,0x80))return;
    const float yaw=float(read<std::int16_t>(player,0x6e))*6.2831853f/4096.f;
    static re3bridge::Matrix32 matrix{};static const std::int16_t offset[3]{};
    for(unsigned k=0;k<9;++k)matrix.rotation[k]=std::int16_t(k%4==0?4096:0);
    // the explosion (0C, damaging: at 1800 it killed Jill in the v1 run) farther away
    const char next=testSteps.empty()?'f':testSteps[testStep%testSteps.size()];const float distance=next=='x'?4500.f:1800.f;
    matrix.translation[0]=read<std::int32_t>(player,0x34)+std::int32_t(std::sin(yaw)*distance);
    matrix.translation[1]=read<std::int32_t>(player,0x38)-900;
    matrix.translation[2]=read<std::int32_t>(player,0x3c)+std::int32_t(std::cos(yaw)*distance);
    using Spawn=void*(__cdecl*)(std::uint32_t,std::uint32_t,const void*,const void*,const void*);
    const auto spawn=reinterpret_cast<Spawn>(gameBase+0x3d370);
    if(read<std::uint8_t>(reinterpret_cast<void*>(gameBase+0x3d370))!=0x51){log("FIRE_TEST spawn signature mismatch");return;}
    const char step=testSteps.empty()?'f':testSteps[testStep++%testSteps.size()];Bank bank;
    if(burstCount){burstLeft=burstCount;burstNext=tick+2;}
    if(step=='b'){log("FIRE_TEST captures only");return;}
    if(step=='f'&&findBank(3,bank)){void* e=spawn(0x03000903u,0x10000000u,&matrix,offset,nullptr);void* s=spawn(0x01001303u,0x10000000u,&matrix,offset,nullptr);
        log("FIRE_TEST core fireball 0903 -> %p, smoke 1303 -> %p at %d,%d,%d",e,s,matrix.translation[0],matrix.translation[1],matrix.translation[2]);}
    else if(step=='x'&&findBank(0x0c,bank)){void* e=spawn(0x0000000cu,0x10000000u,&matrix,offset,nullptr);log("FIRE_TEST explosion 000C -> %p at %d,%d,%d",e,matrix.translation[0],matrix.translation[1],matrix.translation[2]);}
    else if(step=='l'&&findBank(0x24,bank)){matrix.translation[1]=read<std::int32_t>(player,0x38);void* e=spawn(0x00000024u,0x10000000u,&matrix,offset,nullptr);log("FIRE_TEST flame 0024 -> %p",e);}
    else log("FIRE_TEST step %c: bank not loaded in this room",step);
}
inline void update(){
    flushBatch();   // this build's sprites (the effect loop has just drawn them)
    const std::uint32_t room=(std::uint32_t(read<std::uint16_t>(reinterpret_cast<void*>(gameBase+0x661fa6)))<<16)|read<std::uint16_t>(reinterpret_cast<void*>(gameBase+0x661fa8));
    if(room!=fireRoom){
        if(fireRoom!=~0u&&(replaced||refused))log("FIRE_ROOM room=%x replaced=%llu refused=%llu flames=%llu fireballs=%llu smoke=%llu tracked=%zu",fireRoom,(unsigned long long)replaced,(unsigned long long)refused,
            (unsigned long long)flameSprites,(unsigned long long)burstSprites,(unsigned long long)smokeSprites,flames.size());
        if(fireRoom!=~0u&&pages::pageQuadsRoom)log("FIRE_PAGES room=%x quads=%llu (native fire drawn from the HD pages)",fireRoom,(unsigned long long)pages::pageQuadsRoom);   // fx5
        pages::pageQuadsRoom=0;
        fireRoom=room;flames.clear();smooths.clear();
    }
    ++tick;
    if(++spentFrames>=600){if(replaced)log("FIRE_PERF frames=600 cpu_ms_per_frame=%.3f replaced=%llu refused=%llu",spentMs/600.0,(unsigned long long)replaced,(unsigned long long)refused);spentMs=0;spentFrames=0;}
    if(burstLeft&&tick>=burstNext){--burstLeft;burstNext=tick+burstEvery;captureRequested=true;}   // QA burst (proxy.cpp captureFrame)
    const bool test=InterlockedExchange(&fireTestRequest,0)!=0;   // proxy.cpp: pilot.command fire-test (debug_warp=1)
    // the test spawns the native effects with fire_fx=0 too (the native "before" captures of the same plan)
    if(test)try{spawnTest();}catch(...){log("FIRE_TEST exception");}
    if(!enabled)return;
    // flames not drawn for a second (effect gone, slot reused) are forgotten
    if((tick&31)==0)for(auto it=flames.begin();it!=flames.end();){if(tick-it->second.lastTick>60)it=flames.erase(it);else ++it;}
    if((tick&31)==0)for(auto it=smooths.begin();it!=smooths.end();){if(tick-it->second.seen>60)it=smooths.erase(it);else ++it;}
}
inline void install(){
    const std::string ini=root+"\\pilot.ini";
    // fx5 2026-11-08 fire: 0 native, 1 HD pages of the native frames (default), 2 round-2 HD clips
    fireMode=(std::min)(2u,unsigned(GetPrivateProfileIntA("pilot","fire_fx",1,ini.c_str())));enabled=fireMode==2;
    debugSlow=(std::max)(1u,(std::min)(16u,unsigned(GetPrivateProfileIntA("pilot","fire_debug_slow",1,ini.c_str()))));
    burstCount=(std::min)(200u,unsigned(GetPrivateProfileIntA("pilot","fire_debug_burst",0,ini.c_str())));
    burstEvery=(std::max)(1u,(std::min)(60u,unsigned(GetPrivateProfileIntA("pilot","fire_debug_burst_every",6,ini.c_str()))));
    {char steps[32]{};GetPrivateProfileStringA("pilot","fire_test_steps","fxl",steps,sizeof(steps),ini.c_str());testSteps=steps;}
    if(fireMode==0){log("FIRE_FX disabled by pilot.ini fire_fx=0 (native fire)");return;}
    if(fireMode==1){log("FIRE_FX_INSTALL_OK round 5 (fire_fx=1): native fire effects, HD texture pages of their native frames (%u pages: room banks 20-28/0C, core 03 rows 3/1/0, core 05 row 0 quads 0-9)",unsigned(sizeof(pages::kPages)/sizeof(pages::kPages[0])));return;}
    log("FIRE_FX_INSTALL_OK round 2 (fire_fx=2): continuous flame loops + glow; flames (room banks 20-28, core bank 05), billows (core bank 03: fireballs palette 3, smoke palettes 0/1), explosions (bank 0C) replaced at their packets");
}
} // namespace fire
