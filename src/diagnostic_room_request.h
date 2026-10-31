#pragma once
#include <istream>
#include <string>
#include "diagnostic_room_entries.h"

namespace re3diagnostic {
inline int RoomHex(char c) { return c>='0'&&c<='9'?c-'0':c>='A'&&c<='F'?c-'A'+10:-1; }
inline bool ValidRoomName(const std::string& room) {
    return room.size()==4&&room[0]=='R'&&room[1]>='1'&&room[1]<='7'&&RoomHex(room[2])>=0&&RoomHex(room[3])>=0;
}
inline const RoomEntry* FindRoomEntry(const std::string& room,unsigned entry) {
    if(!ValidRoomName(room))return nullptr;
    for(const auto& row:kRoomEntries)if(row.entry==entry&&room==row.room)return &row;
    return nullptr;
}
// Input starts AFTER the literal command "room". Only a whitelisted Rxxx and an optional decimal entry index.
// No paths, arbitrary coordinates, signs, hexadecimal index strings, suffixes or extra arguments are accepted.
inline const RoomEntry* ParseRoomRequest(std::istream& in,const char*& reason) {
    std::string room,index,extra;unsigned entry=0;
    if(!(in>>room)||!ValidRoomName(room)){reason="invalid-room-name";return nullptr;}
    if(in>>index){
        if(index.empty()||index.size()>3){reason="invalid-entry";return nullptr;}
        for(char c:index){if(c<'0'||c>'9'){reason="invalid-entry";return nullptr;}entry=entry*10+unsigned(c-'0');}
        if(in>>extra){reason="extra-arguments";return nullptr;}
    }
    const auto* result=FindRoomEntry(room,entry);
    reason=result?"ok":"no-verified-entry";return result;
}
} // namespace re3diagnostic
