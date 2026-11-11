#pragma once
// Disposable QA harness only. Mirrors verified native script position setters
// and inventory-close weapon loading. The explicit dormant-pickup command only
// registers a shipped AOT through its native handler; no save/animation writes.
static void diagnosticActorControl(){
    if(nativeDiagnosticRequest.empty()||!GetPrivateProfileIntA("pilot","debug_warp",0,(root+"\\pilot.ini").c_str()))return;
    using namespace re3bridge::detail;
    const auto player=reinterpret_cast<uint8_t*>(gameBase+0x65cbf4);
    const auto room=Read<uint16_t>(reinterpret_cast<void*>(gameBase+0x661fa8));
    if(Read<uint16_t>(reinterpret_cast<void*>(gameBase+0x661fa6))||room>1||!Accessible(player,0x180)||Read<uint8_t>(player,8)<15)return;
    if((Read<uint32_t>(reinterpret_cast<void*>(gameBase+0x661f5c))&0x100u)||
       (Read<uint32_t>(reinterpret_cast<void*>(gameBase+0x65c880))&(0x400000u|0x10000u|0x80u))||
       (Read<uint32_t>(reinterpret_cast<void*>(gameBase+0x65c864))&0x540u))return;
    const auto command=nativeDiagnosticRequest;nativeDiagnosticRequest.clear();
    if(command=="prepare-warehouse-replay"){
        auto flags=reinterpret_cast<uint32_t*>(gameBase+0x661fd0);
        if(room||(*flags&0x00400000u)||(Read<uint32_t>(reinterpret_cast<void*>(gameBase+0x65cbd4))&0x40000000u)){log("DIAGNOSTIC_REPLAY rejected non-R100/living-Dario/pause guard");return;}
        const auto previous=*flags;*flags&=~0x00100000u;
        log("DIAGNOSTIC_REPLAY cleared only original bank3 bit0B previous=%08x; native R100-to-R101 transition still required; disposable process only",previous);return;
    }
    if(command=="activate-dormant-pickup"){
        static const uint8_t expected[]={0x67,0x07,0x02,0x31,0,0,0x80,0xa9,0xb1,0xb2,0x84,0x03,0xa4,0x06,0x85,0,1,0,0x47,0,6,1};
        const auto rdt=Read<uint8_t*>(reinterpret_cast<void*>(gameBase+0x65c898));
        if(room||Read<uint8_t>(player,5)||!Accessible(rdt,0xda0+sizeof(expected))||memcmp(rdt+0xda0,expected,sizeof(expected))||
           Read<uint32_t>(reinterpret_cast<void*>(gameBase+0x65c8d8))||
           (Read<uint32_t>(reinterpret_cast<void*>(gameBase+0x662040))&0x01000000u)||
           (Read<uint32_t>(reinterpret_cast<void*>(gameBase+0x65cbd4))&0x40000000u)){
            log("DIAGNOSTIC_DORMANT_PICKUP rejected source/state/owned guard");return;
        }
        uint32_t work[8]{};work[7]=reinterpret_cast<uint32_t>(rdt+0xda0);
        const auto result=reinterpret_cast<int(__cdecl*)(void*)>(gameBase+0x6b7a0)(work);
        log("DIAGNOSTIC_DORMANT_PICKUP native67 result=%d registered=%u; original dormant A2 only, real action required; disposable process only",result,Read<uint32_t>(reinterpret_cast<void*>(gameBase+0x65c8d8))==reinterpret_cast<uint32_t>(rdt+0xda2));return;
    }
    if(command=="equip-m92"||command=="equip-m4"){
        const auto inventory=Read<uint8_t*>(reinterpret_cast<void*>(gameBase+0x6623e4));
        if(!Accessible(inventory,0x12b,true)){log("DIAGNOSTIC_EQUIP rejected invalid inventory");return;}
        const unsigned first=command=="equip-m92"?3:0x0e,second=command=="equip-m92"?2:0x0f;
        const auto find=reinterpret_cast<uint8_t(__cdecl*)(unsigned,unsigned)>(gameBase+0xde9d0);
        unsigned weapon=first;uint8_t slot=find(first,0);if(slot==0xff){weapon=second;slot=find(second,0);}
        if(slot==0xff||slot>=inventory[0x12a]||inventory[slot*4]!=weapon||!inventory[slot*4+1]){log("DIAGNOSTIC_EQUIP rejected no existing loaded %s slot",command.c_str());return;}
        inventory[0x128]=slot;inventory[0x129]=uint8_t(weapon);player[0x46]=uint8_t(weapon);
        reinterpret_cast<void(__cdecl*)(void*)>(gameBase+0x52620)(player);
        reinterpret_cast<void(__cdecl*)(void*)>(gameBase+0x52310)(player);
        log("DIAGNOSTIC_EQUIP native loader weapon=%02x slot=%u ammo=%u",weapon,slot,inventory[slot*4+1]);return;
    }
    int x=0,y=0,z=0;int16_t yaw=0;uint8_t floor=0;
    if(command=="at-chest"){
        if(room){log("DIAGNOSTIC_POSITION rejected chest requires R100");return;}
        x=-27312;z=-21766;yaw=0x800;
    }else if(command=="at-pickup"){
        if(room){log("DIAGNOSTIC_POSITION rejected animated document pickup requires R100");return;}
        x=-22314;z=-19500;yaw=0;
    }else if(command=="at-typewriter"){
        if(room){log("DIAGNOSTIC_POSITION rejected typewriter requires R100");return;}
        x=-24452;z=-19412;yaw=0x800;
    }else if(command=="at-door"){
        if(room==0){x=-20002;z=-20665;yaw=0;}
        else{x=-20410;y=-7200;z=-11275;yaw=0x800;floor=4;}
    }else if(command.rfind("at ",0)==0){   // fix 2026-11-11d (shadow QA): "at x y z yaw floor", R100/R101 as the rows above
        int yawIn=0,floorIn=0;
        if(sscanf_s(command.c_str()+3,"%d %d %d %d %d",&x,&y,&z,&yawIn,&floorIn)!=5||floorIn<0||floorIn>15||x<-32768||x>32767||y<-32768||y>32767||z<-32768||z>32767){log("DIAGNOSTIC_POSITION rejected '%s'",command.c_str());return;}
        yaw=int16_t(yawIn&0xfff);floor=uint8_t(floorIn);
    }else return;
    Write<int16_t>(player,0x6c,0);Write<int16_t>(player,0x6e,yaw);Write<int16_t>(player,0x70,0);
    const int xyz[3]={x,y,z};for(unsigned i=0;i<3;i++){Write<int32_t>(player,0x34+i*4,xyz[i]);Write<int16_t>(player,0x40+i*2,int16_t(xyz[i]));}
    player[9]=floor;reinterpret_cast<void(__cdecl*)(void*)>(gameBase+0x5cb90)(player+0x80);
    log("DIAGNOSTIC_POSITION %s R10%u XYZ=%d,%d,%d yaw=%04x floor=%u; native interaction still required",command.c_str(),room,x,y,z,unsigned(yaw),floor);
}
