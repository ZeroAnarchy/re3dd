// Title costume menu (fix round 2026-10-25). Static facts, exact GOG EXE:
//   new game (title subphase 1, 0x451050) -> subphase 0x0C, handler 0x4513B0 = slot 0x528FC8+0x30. Title object 0xA3F680:
//   +1 subphase, +4 cursor, +5 previous cursor (a difference plays the move sound). Cursor 0..7 = two rows of four words
//   (SPECIAL1..4 / SPECIAL5..7, NORMAL; raw DOWN/UP +-1 inside a row, RIGHT/LEFT +-4); confirm -> 0x5114C4[cursor] = {1..7,0} ->
//   0x4254C0 -> costume A6262C; the PLD loader (0x45F8D5) uses it for player ids 00 and 01. Draw case 0x4507E6 links the 8
//   word sprites (title +0x18 + buffer*0x298 + 0x104 + i*0x14) through the call at 0x45083D -> 0x423330.
// Remastered mode offers only costumes with an HD body: costume 0 (built-in Jill) and every costume c that actors.txt
// covers for both Jill ids (`player 00 c <asset>` and `player 01 c|- <asset>`). The menu hides the other words and the
// cursor moves over the offered ones only; F8 back to original shows and offers all eight again.
static uint8_t titleCostumeHdCursors=0xff; // cursor bits offered in remastered mode
static re3bridge::detail::Patch titleCostumePatches[3];
static void(__cdecl* titleCostumeOriginal)(uint8_t*)=nullptr;
static void(__cdecl* titleNewGameOriginal)(uint8_t*)=nullptr;
static void(__cdecl* titleCostumeLink)(void*,void*)=nullptr;
static bool titleCostumeAllowed(uint32_t costume){
    if(costume==0||!modelActive)return true; // NORMAL = built-in HD Jill; without HD models nothing is restricted
    bool pl00=false,pl01=false;
    for(const auto& rule:modelActorRules)if(rule.player&&(rule.anyCostume||rule.costume==costume)){pl00|=rule.modelId==0;pl01|=rule.modelId==1;}
    return pl00&&pl01;
}
static bool titleCostumeRemastered(){return desiredEnabled!=((InterlockedCompareExchange(&toggleRequested,0,0)&1)!=0);} // F8 still pending counts
static void __cdecl titleCostumeLinkHook(void* ot,void* sprite){
    const uintptr_t at=reinterpret_cast<uintptr_t>(sprite)-(gameBase+0x63f680+0x18),inBuffer=at%0x298;
    if(at<2*0x298&&inBuffer>=0x104&&(inBuffer-0x104)%0x14==0&&(inBuffer-0x104)/0x14<8&&titleCostumeRemastered()&&!(titleCostumeHdCursors>>((inBuffer-0x104)/0x14)&1))return;
    titleCostumeLink(ot,sprite);
}
static void __cdecl titleCostumeMenuHook(uint8_t* title){
    static bool open=false;
    const bool remastered=titleCostumeRemastered();const uint8_t offered=remastered?titleCostumeHdCursors:0xff;
    const auto table=reinterpret_cast<const uint32_t*>(gameBase+0x1114c4);
    if(!open){open=true;log("TITLE_COSTUME menu mode=%s offered=%02x",remastered?"remastered":"original",offered);}
    if(remastered){
        auto fresh=reinterpret_cast<uint32_t*>(gameBase+0x65c85c);const uint32_t pressed=*fresh;
        int at=int8_t(title[4]);
        if(at<0||at>7||!(offered>>at&1)){
            int home=-1;for(int i=7;i>=0;--i)if(offered>>i&1&&(home<0||table[i]==0))home=i;
            log("TITLE_COSTUME cursor %d -> %d (costume %u has no HD pack)",at,home,at>=0&&at<8?table[at]:0xffu);
            title[4]=title[5]=uint8_t(at=home);
        }
        // own move over the offered words in reading order; the native handler sees no direction this frame
        const int step=pressed&0x6000?1:pressed&0x9000?-1:0;
        if(step)for(int i=1;i<8;++i){const int next=(at+step*i+8)%8;if(offered>>next&1){title[4]=uint8_t(next);break;}}
        *fresh=pressed&~0xf000u;
    }
    titleCostumeOriginal(title); // confirm runs the whole new-game start (0x4510A0) and returns with main phase 4
    if(title[0]!=3||title[1]!=0x0c){open=false;
        if(title[0]!=3){const uint32_t costume=*reinterpret_cast<uint32_t*>(gameBase+0x66262c);
            log("TITLE_COSTUME selected costume=%u mode=%s hd_pack=%u",costume,remastered?"remastered":"original",titleCostumeAllowed(costume));}}
}
// Subphase 1 (0x451050) starts a game without the menu when A5C880&0x20 (keeps the costume of the last game, 0x451291) or
// &0x80: such a costume without an HD pack falls back to NORMAL in remastered mode.
static void __cdecl titleNewGameHook(uint8_t* title){
    titleNewGameOriginal(title);
    auto costume=reinterpret_cast<uint32_t*>(gameBase+0x66262c);
    if(title[0]!=3&&titleCostumeRemastered()&&!titleCostumeAllowed(*costume)){log("TITLE_COSTUME kept costume %u has no HD pack -> 0 (remastered mode)",*costume);*costume=0;}
}
static void installTitleCostume(){
    static const uint32_t costumes[8]={1,2,3,4,5,6,7,0};
    if(memcmp(reinterpret_cast<void*>(gameBase+0x1114c4),costumes,sizeof(costumes))!=0||*reinterpret_cast<uint8_t*>(gameBase+0x5083c)!=0x52||
       !re3bridge::detail::BuildSlotPatch(titleCostumePatches[0],gameBase+0x128ff8,reinterpret_cast<void*>(gameBase+0x513b0),reinterpret_cast<void*>(titleCostumeMenuHook))||
       !re3bridge::detail::BuildCallPatch(titleCostumePatches[1],gameBase+0x5083d,gameBase+0x23330,reinterpret_cast<void*>(titleCostumeLinkHook))||
       !re3bridge::detail::BuildSlotPatch(titleCostumePatches[2],gameBase+0x128fcc,reinterpret_cast<void*>(gameBase+0x51050),reinterpret_cast<void*>(titleNewGameHook))){log("TITLE_COSTUME_HOOKS skipped: signature mismatch, native costume menu");return;}
    titleCostumeOriginal=reinterpret_cast<void(__cdecl*)(uint8_t*)>(gameBase+0x513b0);titleNewGameOriginal=reinterpret_cast<void(__cdecl*)(uint8_t*)>(gameBase+0x51050);titleCostumeLink=reinterpret_cast<void(__cdecl*)(void*,void*)>(gameBase+0x23330);
    titleCostumeHdCursors=0;for(int i=0;i<8;++i)if(titleCostumeAllowed(costumes[i]))titleCostumeHdCursors|=uint8_t(1u<<i);
    size_t done=0;for(;done<3;++done)if(!re3bridge::detail::WritePatch(titleCostumePatches[done],true))break;
    if(done!=3){while(done)re3bridge::detail::WritePatch(titleCostumePatches[--done],false);log("TITLE_COSTUME_HOOKS failed: native costume menu");return;}
    std::string list;for(int i=0;i<8;++i)if(titleCostumeHdCursors>>i&1)list+=(list.empty()?"":",")+std::to_string(costumes[i]);
    log("TITLE_COSTUME_HOOKS installed remastered_costumes=%s cursors=%02x (actors.txt player 00+01 rows; 0 = built-in Jill)",list.c_str(),titleCostumeHdCursors);
}
