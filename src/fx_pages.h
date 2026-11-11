#pragma once
// ---------------------------------------------------------------- HD pages of more native effects (round 7, 10.11.2026)
// Included in extra_model_integration.h after fx_fire.h, inside namespace re3extra.
//
// User request 10.11: HD versions of the water splashes, the hit blood of the Grave Digger and of the final Nemesis, the
// acid he spits and the grenade rounds - "like the fire, where we only made a higher-resolution version of the assets".
// So exactly the fire round-5 scheme (fx_fire.h pages): the game's own effects keep drawing their own quads (geometry,
// frames, animation, timing, blending, colour modulation); only the texture a quad samples becomes an HD picture of the
// same native frames. proxy.cpp drawUiPacket asks pageBinding() and chiBinding() for every packet (one line).
//
// What is what (static analysis of ResidentEvil3.exe and the game files, models/fx-2026-11-10: tools/survey.py,
// spawn_sites.py, native_fx.py; no game run was allowed in this round):
//  - water: the footstep routine 429DC0 spawns key 000E (room bank 0E, a ripple ring lying on the water, flags B20B) on
//    one floor type and 061C (room bank 1C record 06, a splash) on the others, at the water level [A5CC62]; both banks
//    are in the 19 rooms with shallow water (R109 R10A R123 R124 R201 R205 R212 R30B R40C R40D R40E R410 R415 R503 R504
//    R507 R509 R50B R510), one and the same picture in all of them. Bank 1C has 34 records (0x161C, 0x71C, 0xE1C from
//    the weapon code 453980 / 463A40 and the worm modules too). Room banks 2D (a drop splash: dome, then a crown ring)
//    and 2E (water blobs; its records chain to 2D01) are in R40B R40D R503 R507 (2D also R50D).
//  - final Nemesis (R50D, module table 52BBC8, code 470190..47B9xx): his effects are room bank 40 (only R50D has it;
//    keys 0440 0B40 0C40 0D40 0E40 0F40 1240 pushed by 4724F0 / 472A30 / 474010 / 47ABF0 / 47AF60 / 4780A0), the
//    beige-olive spurt = his acid; his hit blood is the ordinary RED blot set of core bank 00 (keys 0300 / 1900 / 1600 /
//    1800 / 0700 / 0200, palette 0; record 19 = an opaque blot, flags B007), which blood_fx.h does not touch (it only
//    replaces 0C00 class 2B). Room bank 3E (a dark green blot on the floor, class 2A) is only in R50D too; nothing in the
//    EXE pushes its key with an immediate, it is taken as part of that fight (not verified in the game).
//  - Grave Digger / worms (module tables 52BFC4 and 52C5B0, code 47BD30..489xxx): hit blood = the same blot set in
//    palette 7 (olive; keys 07000700 07001900 07000000 07001500 07000100); 0x39 (room bank 39 of R40B R40E R415, pale
//    yellow lumps and their splat) is pushed only by that module (484C40 / 4864F0).
//  - grenade rounds (projectile code 4D3000..4D8000, spawn wrapper 43D280): explosive = core 03 palette 3 fireballs +
//    palette 1 smoke and flame = core 05 fire - both HD since the fire round; acid = core 03 palette 2 billows (02000903
//    02000A03 02000E03) + core 00 palette 3 drops (03000E00 03000F00); the white cloud 2203 / 0A03 / 2003 / 0B03 is core
//    03 palette 0 (HD since the fire round).
// Pages (model-pack/fx-pages, models/fx-2026-11-10/tools):
//  - c00r0/1/3/5/6/7.png: the blot block of core bank 00 (tpage 1E, u 0..255, v 0..87: the block is 240 wide, but its
//    quads 21 = (216,40,32) and 31 = (232,72,16) reach u 247) at 8x in the palette rows that
//    had no page (rows 2 and 4 = fx_nemesis_cloud.h native-arm-p5/p9): the same computed redraw as those two pages
//    (make_chi_pages.py rebuilds row 2 bit-exactly); until now these rows were drawn from the 4x TEXU upscale of ui-pack.
//  - c03r2 / c03r4.png: core bank 03 rows 2 and 4 from the fire round's Codex redraw of the billows with the native
//    colours of the row (as c03r1).
//  - b1c b0e b2d b2e b40 b3e b39.png: Codex image_gen redraws of the native frames of the room banks, native colours
//    at low frequency, native outline (make_pages.py).
// pilot.ini effects_fx=0 (F5 menu "HD water, acid and creature blood", live): everything here off = the look before
// this round. F8 (original): native, as every other binding of drawUiPacket.
namespace fxpages {
static bool enabled=true;              // pilot.ini effects_fx
struct Page{std::uint8_t core,bank,row;std::uint16_t w,h,scale;std::uint64_t layout;const char* png;};
static const Page kPages[]={
#include "fx_pages.inc"
};
constexpr unsigned kMaxQuads=64,kRows=8;
// a bank of the table kPages loaded in the bank table A465A0 (rebuilt once per frame: room loads replace entries 16..31)
struct Live{const std::uint8_t* header=nullptr;std::uint8_t id=0;bool core=false;std::uint16_t clut=0,tpage=0,u0=0,v0=0;unsigned count=0;
    std::uint8_t qu[kMaxQuads],qv[kMaxQuads],qs[kMaxQuads];std::uint64_t raw=0;unsigned rawBytes=0;const Page* page[kRows]{};};
inline std::uint64_t fnvBytes(const std::uint8_t* b,unsigned n){std::uint64_t h=0xcbf29ce484222325ull;for(unsigned i=0;i<n;++i){h^=b[i];h*=0x100000001b3ull;}return h;}
static Live live[32];static unsigned liveCount=0,liveFrame=~0u;
static std::uint64_t pageQuads=0,pageQuadsRoom=0,chiQuads[kRows]{};static unsigned bankLines=0,loadLines=0;static std::uint32_t pagesRoom=~0u;
// entries 0..15 of the bank table are the CORE00.ESP banks, 16..31 the room's (a room may have its own bank 03 / 05)
inline bool wanted(unsigned entry,std::uint8_t id){for(const auto& p:kPages)if(p.bank==id&&(p.core!=0)==(entry<16))return true;return false;}
// the page texture with mips (the sprites are drawn from well below to several times their texel size)
inline void ensureTexture(const char* png){
    if(!gameDevice)return;auto& t=uiTextures[png];if(t.missing)return;
    if(t.texture&&t.texture->GetLevelCount()>1)return;
    if(t.texture){t.texture->Release();t.texture=nullptr;t.bytes=0;}
    LARGE_INTEGER a,b,f;QueryPerformanceCounter(&a);
    t.texture=loadReplacement(gameDevice,0,png,16);t.lastUsed=uiFrameSerial;
    if(!t.texture){t.missing=true;log("FX_PAGE_MISSING %s (the previous look of this effect is kept)",png);return;}
    QueryPerformanceCounter(&b);QueryPerformanceFrequency(&f);
    D3DSURFACE_DESC desc{};t.texture->GetLevelDesc(0,&desc);t.bytes=std::size_t(desc.Width)*desc.Height*4u*4u/3u;
    if(loadLines<64){++loadLines;log("FX_PAGE_LOAD %s %ux%u levels=%u ms=%.1f",png,desc.Width,desc.Height,t.texture->GetLevelCount(),double(b.QuadPart-a.QuadPart)*1000.0/double(f.QuadPart));}
}
inline void scan(){
    if(liveFrame==uiFrameSerial)return;liveFrame=uiFrameSerial;
    Live fresh[32];unsigned count=0;
    for(unsigned e=0;e<32;++e){
        const auto at=reinterpret_cast<const void*>(gameBase+0x6465a0+e*12);
        const auto id=read<std::uint8_t>(at,8);if(!wanted(e,id))continue;
        const auto* h=read<const std::uint8_t*>(at);
        // the same header and table bytes as last frame: keep its parse (a new room may put another table of the same bank
        // id at the same address)
        bool known=false;
        for(unsigned i=0;i<liveCount;++i)if(live[i].header==h&&live[i].id==id&&live[i].core==(e<16)&&live[i].raw==fnvBytes(h,live[i].rawBytes)){fresh[count++]=live[i];known=true;break;}
        if(known)continue;
        if(!accessible(h,8))continue;
        const unsigned groups=read<std::uint16_t>(h,0),quads=read<std::uint16_t>(h,2);
        if(!groups||groups>256||!quads||quads>kMaxQuads||!accessible(h,8+groups*8+quads*4))continue;
        Live l;l.header=h;l.id=id;l.core=e<16;l.clut=read<std::uint16_t>(h,4);l.tpage=read<std::uint16_t>(h,6);
        l.rawBytes=8+groups*8+quads*4;l.raw=fnvBytes(h,l.rawBytes);
        std::uint8_t size[kMaxQuads]{};
        for(unsigned g=0;g<groups;++g){const unsigned start=h[8+g*8],n=h[9+g*8],s=h[11+g*8];if(!n||!s)continue;
            for(unsigned q=start;q<start+n&&q<quads;++q)size[q]=(std::max)(size[q],std::uint8_t(s));}
        const std::uint8_t* quad=h+8+groups*8;unsigned u0=255,v0=255;
        for(unsigned q=0;q<quads;++q)if(size[q]){u0=(std::min)(u0,unsigned(quad[q*4]));v0=(std::min)(v0,unsigned(quad[q*4+1]));}
        std::uint64_t hash=0xcbf29ce484222325ull;auto mix=[&](unsigned byte){hash^=byte&255u;hash*=0x100000001b3ull;};
        for(unsigned q=0;q<quads;++q)if(size[q]){l.qu[l.count]=quad[q*4];l.qv[l.count]=quad[q*4+1];l.qs[l.count]=size[q];++l.count;
            mix(quad[q*4]-u0);mix(quad[q*4+1]-v0);mix(size[q]);}
        if(!l.count)continue;
        l.u0=std::uint16_t(u0);l.v0=std::uint16_t(v0);
        for(const auto& p:kPages)if(p.bank==id&&(p.core!=0)==l.core&&p.row<kRows){
            if(p.layout==hash){l.page[p.row]=&p;ensureTexture(p.png);
                if(bankLines<256){++bankLines;log("FX_PAGE %s bank=%02x row=%u clut=%04x tpage=%04x region=%u,%u layout=%016llx -> %s",l.core?"core":"room",id,p.row,l.clut+0x40u*p.row,l.tpage,u0,v0,hash,p.png);}}
            else if(bankLines<256){++bankLines;log("FX_PAGE_LAYOUT_MISMATCH %s bank=%02x row=%u layout=%016llx expected=%016llx (the previous look is kept)",l.core?"core":"room",id,p.row,hash,p.layout);}}
        fresh[count++]=l;   // kept even without a page (layout mismatch): parsed and logged once, its quads stay as they were
    }
    std::memcpy(live,fresh,sizeof(Live)*count);liveCount=count;
}
// proxy.cpp drawUiPacket (one line, r7 fx pages): a quad of one of the banks above -> its HD page
static bool pageBinding(const std::uint8_t* p,unsigned tpage,unsigned clut,float minU,float minV,float maxU,float maxV,re3ui::Binding& binding){
    (void)p;
    if(!enabled||((tpage>>7)&3u)!=0)return false;   // only 4-bit pages
    scan();
    const unsigned page=tpage&0x1fu;
    for(unsigned i=0;i<liveCount;++i){const auto& l=live[i];
        if((l.tpage&0x1fu)!=page||clut<l.clut||((clut-l.clut)&0x3fu)||clut-l.clut>=0x40u*kRows)continue;
        const auto* pg=l.page[(clut-l.clut)>>6];if(!pg)continue;
        bool inside=false;   // the packet shows one quad of the bank (not something else on the same page)
        for(unsigned k=0;k<l.count&&!inside;++k){const float u=l.qu[k],v=l.qv[k],s=l.qs[k];inside=minU>=u-.5f&&maxU<=u+s+.5f&&minV>=v-.5f&&maxV<=v+s+.5f;}
        if(!inside)continue;
        ensureTexture(pg->png);
        binding=re3ui::Binding{};binding.assetId="fx-page";binding.pngPath=pg->png;binding.paletteIndex=pg->row;
        binding.originalW=pg->w;binding.originalH=pg->h;binding.offsetU=-float(l.u0);binding.offsetV=-float(l.v0);binding.scale=pg->scale;
        ++pageQuads;++pageQuadsRoom;
        return true;
    }
    return false;
}
// The blot block of core bank 00 (tpage 1E, any blend bits, u 0..255, v 0..87) in the CLUT rows 0, 1, 3, 5, 6, 7
// (7811 + 0x40 x row): the 8x page of that row instead of the 4x TEXU upscale. Rows 2 and 4 are fx_nemesis_cloud.h
// armBinding (asked before this one in drawUiPacket; with nemesis_cloud_fx=0 they stay on the TEXU upscale).
static const char* const kChiPng[kRows]={"model-pack/fx-pages/c00r0.png","model-pack/fx-pages/c00r1.png",nullptr,"model-pack/fx-pages/c00r3.png",nullptr,
    "model-pack/fx-pages/c00r5.png","model-pack/fx-pages/c00r6.png","model-pack/fx-pages/c00r7.png"};
static int chiPresent[kRows]={-1,-1,-1,-1,-1,-1,-1,-1};
static bool chiBinding(const std::uint8_t* p,unsigned tpage,unsigned clut,float minU,float minV,float maxU,float maxV,re3ui::Binding& binding){
    (void)p;
    if(!enabled||(tpage&0x19fu)!=0x1eu||clut<0x7811u||clut>0x79d1u||((clut-0x7811u)&0x3fu))return false;
    const unsigned row=(clut-0x7811u)>>6;if(row>=kRows||!kChiPng[row])return false;
    if(minU<0.f||minV<0.f||maxU>256.f||maxV>88.f)return false;
    if(chiPresent[row]<0){chiPresent[row]=GetFileAttributesA((root+"\\"+kChiPng[row]).c_str())!=INVALID_FILE_ATTRIBUTES?1:0;
        log(chiPresent[row]?"FX_PAGE_BLOTS row=%u clut=%04x -> %s (8x page of the core blot block, tpage 1E v<88)":"FX_PAGE_BLOTS row=%u clut=%04x: %s missing, the TEXU upscale is kept",row,clut,kChiPng[row]);}
    if(!chiPresent[row])return false;
    ensureTexture(kChiPng[row]);
    binding=re3ui::Binding{};binding.assetId="fx-page-blots";binding.pngPath=kChiPng[row];binding.paletteIndex=row;
    binding.originalW=256;binding.originalH=88;binding.offsetU=0;binding.offsetV=0;binding.scale=8;
    ++chiQuads[row];++pageQuads;++pageQuadsRoom;
    return true;
}
// QA (pilot.command fx-test, debug_warp=1 only): the native effects of these pages in front of the player through the
// game's own spawn 43D370(key, flags, matrix, offset, 0), as fire-test does. One group per command, in turn; only plain
// sprite records (classes 00 / 03 / 05, and the billow record 09 that fire-test has always used) - no emitter classes,
// which may expect an owner. w = water (061C splash, 000E ring, 002D drop), a = acid of R50D (0A40, 0740), b = blots
// (1900 in palettes 0 red / 7 olive / 3 yellow), g = billows (0903 in palettes 2 and 4), s = bank 39 (0139).
static unsigned testStep=0;static int testHold=0;   // testHold: ticks until fx-test pauses the game itself (update())
inline void spawnTest(){
    const auto* player=reinterpret_cast<const std::uint8_t*>(gameBase+0x65cbf4);if(!accessible(player,0x80))return;
    if(read<std::uint8_t>(reinterpret_cast<void*>(gameBase+0x3d370))!=0x51){log("FX_TEST spawn signature mismatch");return;}
    using Spawn=void*(__cdecl*)(std::uint32_t,std::uint32_t,const void*,const void*,const void*);
    const auto spawn=reinterpret_cast<Spawn>(gameBase+0x3d370);
    const float yaw=float(read<std::int16_t>(player,0x6e))*6.2831853f/4096.f;
    static re3bridge::Matrix32 matrix{};static const std::int16_t offset[3]{};
    for(unsigned k=0;k<9;++k)matrix.rotation[k]=std::int16_t(k%4==0?4096:0);
    const std::int32_t px=read<std::int32_t>(player,0x34),py=read<std::int32_t>(player,0x38),pz=read<std::int32_t>(player,0x3c);
    // a point `ahead` in front of the player, `side` to her right, `up` above her feet
    auto at=[&](float ahead,float side,std::int32_t up){matrix.translation[0]=px+std::int32_t(std::sin(yaw)*ahead+std::cos(yaw)*side);
        matrix.translation[1]=py-up;matrix.translation[2]=pz+std::int32_t(std::cos(yaw)*ahead-std::sin(yaw)*side);};
    auto one=[&](std::uint32_t key,const char* what){void* e=spawn(key,0x10000000u,&matrix,offset,nullptr);
        log("FX_TEST %s key=%08x -> %p at %d,%d,%d",what,key,e,matrix.translation[0],matrix.translation[1],matrix.translation[2]);};
    // r7b (in-game QA 10.11.2026 evening): every effect twice on a ring round the player (opposite points), so that part
    // of each group is in view whatever the camera ("ahead" went into the walls of R109 / R415); update() pauses the game
    // 3 or 8 ticks after the spawn (alternating per round of groups), so the harness can capture the same frame in HD and
    // in original mode. c = the blot rows 1 / 5 / 6. Red blots (row 0) show in original mode only: blood_fx.h hides them.
    auto ring=[&](unsigned twelfth,float r,std::int32_t up){const float a=float(twelfth)*0.5235988f;
        matrix.translation[0]=px+std::int32_t(std::cos(a)*r);matrix.translation[1]=py-up;matrix.translation[2]=pz+std::int32_t(std::sin(a)*r);};
    auto two=[&](std::uint32_t key,const char* what,unsigned twelfth,float r,std::int32_t up){ring(twelfth,r,up);one(key,what);ring(twelfth+6,r,up);one(key,what);};
    static const char steps[]="wabcgs";fire::Bank bank;
    for(unsigned tries=0;tries<6;++tries){
        const unsigned cycle=testStep/6;const char step=steps[testStep++%6];
        const int hold=(cycle&1)?8:3;bool done=false;
        if(step=='w'){
            if(fire::findBank(0x1c,bank)){two(0x0000061cu,"water splash 061C",1,800.f,300);two(0x0000021cu,"water splash 021C",4,800.f,300);done=true;}
            if(fire::findBank(0x0e,bank)){two(0x0000000eu,"water ring 000E",3,900.f,0);two(0x0000020eu,"water ring 020E",0,900.f,0);done=true;}
            if(fire::findBank(0x2d,bank)){two(0x0000002du,"drop splash 002D",5,700.f,0);two(0x0000012du,"drop splash 012D",2,700.f,0);done=true;}}
        if(step=='a'&&fire::findBank(0x40,bank)){two(0x00000a40u,"acid 0A40",1,900.f,900);two(0x00000740u,"acid 0740",3,900.f,900);two(0x00000540u,"acid 0540",5,900.f,600);done=true;}
        if(step=='b'&&fire::findBank(0x00,bank)){two(0x00001900u,"red blot 1900",1,800.f,900);two(0x07001900u,"olive blot 07001900",3,800.f,900);
            two(0x03001900u,"yellow blot 03001900",5,800.f,900);done=true;}
        if(step=='c'&&fire::findBank(0x00,bank)){two(0x01001900u,"dark green blot 01001900",1,800.f,900);two(0x05001900u,"blot row 5 05001900",3,800.f,900);
            two(0x06001900u,"blot row 6 06001900",5,800.f,900);done=true;}
        if(step=='g'&&fire::findBank(0x03,bank)){two(0x02000903u,"acid-round billow 02000903",1,1000.f,900);two(0x04000903u,"dark billow 04000903",4,1000.f,900);done=true;}
        if(step=='s'&&fire::findBank(0x39,bank)){two(0x00000139u,"bank 39 record 01",1,800.f,600);two(0x00000139u,"bank 39 record 01",4,800.f,300);done=true;}
        if(done){testHold=hold;log("FX_TEST group %c spawned, pause in %d ticks",step,hold);return;}
    }
    log("FX_TEST nothing to spawn in this room");
}
// per effect-loop tick (effectLoopHook): one line per room that drew something from these pages
inline void update(){
    if(InterlockedExchange(&fxTestRequest,0)!=0)try{spawnTest();}catch(...){log("FX_TEST exception");}   // proxy.cpp: pilot.command fx-test (debug_warp=1)
    if(testHold>0&&--testHold==0){setDiagnosticPause(true);log("FX_TEST_HOLD paused");}   // r7b: fx-test pauses the game itself (resume = pilot.command resume)
    const std::uint32_t room=(std::uint32_t(read<std::uint16_t>(reinterpret_cast<void*>(gameBase+0x661fa6)))<<16)|read<std::uint16_t>(reinterpret_cast<void*>(gameBase+0x661fa8));
    if(room==pagesRoom)return;
    if(pagesRoom!=~0u&&pageQuadsRoom)log("FX_PAGES room=%x quads=%llu (blots by row: %llu %llu - %llu - %llu %llu %llu)",pagesRoom,(unsigned long long)pageQuadsRoom,
        (unsigned long long)chiQuads[0],(unsigned long long)chiQuads[1],(unsigned long long)chiQuads[3],(unsigned long long)chiQuads[5],(unsigned long long)chiQuads[6],(unsigned long long)chiQuads[7]);
    pageQuadsRoom=0;for(auto& c:chiQuads)c=0;pagesRoom=room;
}
inline void install(){
    enabled=GetPrivateProfileIntA("pilot","effects_fx",1,(root+"\\pilot.ini").c_str())!=0;
    if(!enabled){log("FX_PAGES disabled by pilot.ini effects_fx=0 (water, acid, creature blood, acid round as before round 7)");return;}
    log("FX_PAGES_INSTALL_OK %u bank pages (core 03 rows 2/4, room banks 1C 0E 2D 2E 40 3E 39) + the core blot block rows 0 1 3 5 6 7: native effects, HD texture pages of their native frames",
        unsigned(sizeof(kPages)/sizeof(kPages[0])));
}
} // namespace fxpages
