#pragma once
// Fix 2026-10-25c: Mercenaries PLAYER SELECT (select.bin = program 446AC0 of table 517974). 446B00 draws the three figures
// every frame: actors *A5CDC4/C8/CC (task heap, stride 0x1FC) set up at 446F81 from records 526D40 (model, weapon):
// 0A Nikolai + 02 SIGPRO, 08 Carlos + 0E M4A1, 09 Mikhail + 04 Benelli; files PL0xCH.PLD (mesh and skeleton of PL0x.PLD)
// and the stage's PL0xW0y.PLW. Per figure 4471B0 sets its point light (light matrix -> 51C358, colour -> GTE and 51C338;
// the highlighted figure fades in), 435580 builds the skeleton, then 447E50 = 4357D0 without the camera tests draws parts
// with flags&1 and render&1 through 4358F0 (call 447E83). The gameplay build start 42BA96 never runs there, so the first
// figure of a native frame starts the build. HD = the actors.txt player rows and weapons.txt rows of the stage.
static re3bridge::detail::Patch merceSelectPatches[2];
static re3bridge::detail::ActorFn merceSelectOriginal=nullptr;
static void __cdecl merceSelectFigureHook(void* actor,void* parts,uint32_t flags,const void* position){
    std::lock_guard<std::recursive_mutex> guard(mutex);
    try{ensureFrameBuild();}catch(...){}
    const auto before=re3bridge::GetStats().actorsPrepared;
    re3bridge::DrawFigure(merceSelectOriginal,actor,parts,flags,position);
    try{
        static std::unordered_map<void*,uint32_t> shown;static unsigned lines=0;
        if(lines>=48||!re3bridge::detail::Accessible(actor,0x180))return;
        const bool hd=re3bridge::GetStats().actorsPrepared!=before,remastered=re3bridge::BuildModeIsRemastered();
        const uint32_t key=(uint32_t(re3bridge::detail::Read<uint8_t>(actor,0x4a))<<16)|(uint32_t(re3bridge::detail::Read<uint8_t>(actor,0x46))<<8)|(hd?2u:0u)|(remastered?1u:0u);
        auto hit=shown.find(actor);if(hit!=shown.end()&&hit->second==key)return;shown[actor]=key;++lines;
        const auto gte=re3bridge::detail::Read<const uint8_t*>(reinterpret_cast<void*>(gameBase+0x1349c8));
        const auto light=reinterpret_cast<const int16_t*>(gameBase+0x11c358);int16_t colour[9]{};int32_t ambient[3]{};uint16_t h=0;
        if(re3bridge::detail::Accessible(gte,0x100)){std::memcpy(colour,gte+0x50,sizeof(colour));std::memcpy(ambient,gte+0x44,sizeof(ambient));std::memcpy(&h,gte+0x78,2);}
        log("MERCE_SELECT_FIGURE actor=%p model=%02x weapon=%02x parts=%u flags=%x mode=%s hd=%u H=%u light0=%d,%d,%d colour=%d,%d,%d ambient=%d,%d,%d",actor,key>>16,(key>>8)&255u,
            re3bridge::detail::Read<uint8_t>(actor,8),flags,remastered?"remastered":"original",unsigned(hd),h,light[0],light[1],light[2],colour[0],colour[4],colour[8],ambient[0],ambient[1],ambient[2]);
    }catch(...){}
}
static void installMerceSelect(){
    if(!modelActive||modelParity||!re3bridge::Installed())return;
    auto& p=merceSelectPatches;
    if(!re3bridge::detail::BuildCallPatch(p[0],gameBase+0x46caf,gameBase+0x47e50,reinterpret_cast<void*>(merceSelectFigureHook))||
       !re3bridge::detail::BuildCallPatch(p[1],gameBase+0x47e83,gameBase+0x358f0,reinterpret_cast<void*>(re3bridge::detail::PartHook))){
        log("MERCE_SELECT_HOOKS skipped: native call signature mismatch, select figures stay native");return;}
    merceSelectOriginal=reinterpret_cast<re3bridge::detail::ActorFn>(gameBase+0x47e50);
    size_t done=0;for(;done<2;++done)if(!re3bridge::detail::WritePatch(p[done],true))break;
    if(done!=2){while(done)re3bridge::detail::WritePatch(p[--done],false);log("MERCE_SELECT_HOOKS failed: native calls restored");return;}
    log("MERCE_SELECT_HOOKS installed 446CAF/447E83 (PLAYER SELECT figures: actors.txt player rows + weapons.txt)");
}
