#pragma once
#include "diagnostic_room_request.h"

// Included by proxy.cpp after the frame/capture helpers. All requests run on the existing native frame thread.
// Payloads are immutable static data: 45C090 stores their pointer until the original transition has completed.
static const re3diagnostic::RoomEntry* diagnosticRoomRequest=nullptr;
static bool diagnosticRoomSubmitted=false;
static ULONGLONG diagnosticRoomQueuedAt=0,diagnosticRoomSubmittedAt=0;
static unsigned diagnosticRoomSubmittedSerial=0;
static const char* diagnosticRoomWaitReason=nullptr;
static bool diagnosticRoomPending(){return diagnosticRoomRequest!=nullptr;}
static void queueDiagnosticRoom(std::istream& in,bool legacyWarpPending){
    if(!GetPrivateProfileIntA("pilot","debug_warp",0,(root+"\\pilot.ini").c_str())){log("DIAGNOSTIC_ROOM rejected reason=debug-disabled");return;}
    if(legacyWarpPending||diagnosticRoomPending()){log("DIAGNOSTIC_ROOM rejected reason=warp-already-pending");return;}
    const char* reason=nullptr;const auto* request=re3diagnostic::ParseRoomRequest(in,reason);
    if(!request){log("DIAGNOSTIC_ROOM rejected reason=%s",reason);return;}
    diagnosticRoomRequest=request;diagnosticRoomSubmitted=false;diagnosticRoomQueuedAt=GetTickCount64();diagnosticRoomWaitReason=nullptr;
    log("DIAGNOSTIC_ROOM queued room=%s entry=%u source=%s offset=%x camera=%u floor=%u DO2=%02x",request->room,request->entry,
        request->sourceRoom,request->sourceOffset,request->payload[10],request->payload[11],request->payload[12]);
}
static const char* diagnosticRoomReady(){
    using namespace re3bridge::detail;
    if(!gameBase||!frame)return "no-game-frame";
    const auto player=reinterpret_cast<const uint8_t*>(gameBase+0x65cbf4);
    if(!Accessible(player,0x194)||Read<uint8_t>(player,8)<15)return "player-not-loaded";
    if(Read<uint8_t>(player,4)!=1||Read<uint8_t>(player,5)!=0)return "player-not-idle";
    if(Read<uint8_t>(reinterpret_cast<void*>(gameBase+0x6587a0))||Read<uint8_t>(reinterpret_cast<void*>(gameBase+0x6587a1)))return "native-request-pending";
    if(Read<uint32_t>(reinterpret_cast<void*>(gameBase+0x65c880))&0x11590480u)return "movie-ui-door-or-scenario";
    if(Read<uint32_t>(reinterpret_cast<void*>(gameBase+0x661f5c))&0x10400308u)return "native-state-lock";
    if(Read<uint32_t>(reinterpret_cast<void*>(gameBase+0x661d44))||Read<uint32_t>(reinterpret_cast<void*>(gameBase+0x661d48)))return "interaction-controller";
    if(Read<uint8_t>(reinterpret_cast<void*>(gameBase+0x6628c4))||Read<uint8_t>(reinterpret_cast<void*>(gameBase+0x6628b6)))return "fade-or-camera";
    if(Read<uint8_t>(reinterpret_cast<void*>(gameBase+0x657468))&0xc0u)return "native-text";
    if(Read<uint8_t>(reinterpret_cast<void*>(gameBase+0x65cd21)))return "native-door-guard";
    if(Read<uint32_t>(reinterpret_cast<void*>(gameBase+0x65cbd4))&0x40000000u)return "diagnostic-pause";
    if(Read<uint32_t>(reinterpret_cast<void*>(gameBase+0x65cbd0)))return "simulation-domains-blocked";
    if(Read<uint32_t>(reinterpret_cast<void*>(gameBase+0x65c864)))return "held-input";
    const auto rdt=Read<const uint8_t*>(reinterpret_cast<void*>(gameBase+0x65c898));
    if(!Accessible(rdt,100)||!rdt[1])return "rdt-not-loaded";
    return nullptr;
}
static void tickDiagnosticRoom(){
    if(!diagnosticRoomRequest)return;
    if(!GetPrivateProfileIntA("pilot","debug_warp",0,(root+"\\pilot.ini").c_str())){
        log("DIAGNOSTIC_ROOM cancelled reason=debug-disabled");diagnosticRoomRequest=nullptr;return;
    }
    using namespace re3bridge::detail;
    const auto* request=diagnosticRoomRequest;const auto now=GetTickCount64();
    if(diagnosticRoomSubmitted){
        const bool arrived=Read<uint16_t>(reinterpret_cast<void*>(gameBase+0x661fa6))==request->payload[8]&&
            Read<uint16_t>(reinterpret_cast<void*>(gameBase+0x661fa8))==request->payload[9];
        // Room scripts may immediately start an interaction; completion means the native transition finished,
        // not that those scripts were bypassed. Any subsequent warp still waits for the full idle guards.
        const bool transitionFinished=!Read<uint8_t>(reinterpret_cast<void*>(gameBase+0x6587a0))&&
            !Read<uint8_t>(reinterpret_cast<void*>(gameBase+0x6587a1))&&
            !(Read<uint32_t>(reinterpret_cast<void*>(gameBase+0x65c880))&0x400000u)&&
            !Read<uint8_t>(reinterpret_cast<void*>(gameBase+0x6628c4));
        if(arrived&&uiFrameSerial>diagnosticRoomSubmittedSerial&&transitionFinished){
            log("DIAGNOSTIC_ROOM complete room=%s entry=%u elapsed_ms=%llu",request->room,request->entry,
                static_cast<unsigned long long>(now-diagnosticRoomSubmittedAt));diagnosticRoomRequest=nullptr;return;
        }
        if(now-diagnosticRoomSubmittedAt>180000){log("DIAGNOSTIC_ROOM timeout room=%s entry=%u phase=transition (native state left untouched)",request->room,request->entry);diagnosticRoomRequest=nullptr;}
        return;
    }
    const char* waiting=diagnosticRoomReady();
    if(waiting){
        if(waiting!=diagnosticRoomWaitReason){log("DIAGNOSTIC_ROOM waiting room=%s entry=%u reason=%s",request->room,request->entry,waiting);diagnosticRoomWaitReason=waiting;}
        if(now-diagnosticRoomQueuedAt>30000){log("DIAGNOSTIC_ROOM timeout room=%s entry=%u phase=idle-guard",request->room,request->entry);diagnosticRoomRequest=nullptr;}
        return;
    }
    static const uint8_t signature[]={0xa1,0x5c,0x1f,0xa6,0x00,0xa9,0x00,0x00,0x40,0x00,0x75,0x39};
    if(memcmp(reinterpret_cast<void*>(gameBase+0x5c090),signature,sizeof(signature))){
        log("DIAGNOSTIC_ROOM rejected room=%s entry=%u reason=native-signature",request->room,request->entry);diagnosticRoomRequest=nullptr;return;
    }
    reinterpret_cast<void(__cdecl*)(const uint8_t*)>(gameBase+0x5c090)(request->payload);
    if(Read<uint8_t>(reinterpret_cast<void*>(gameBase+0x6587a0))!=1||
       Read<const uint8_t*>(reinterpret_cast<void*>(gameBase+0x65c8b8))!=request->payload){
        log("DIAGNOSTIC_ROOM rejected room=%s entry=%u reason=native-request-not-accepted",request->room,request->entry);diagnosticRoomRequest=nullptr;return;
    }
    diagnosticRoomSubmitted=true;diagnosticRoomSubmittedAt=now;diagnosticRoomSubmittedSerial=uiFrameSerial;
    log("DIAGNOSTIC_ROOM requested room=%s entry=%u camera=%u floor=%u DO2=%02x",request->room,request->entry,
        request->payload[10],request->payload[11],request->payload[12]);
}
