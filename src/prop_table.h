#pragma once
// v14 prop table model-pack/props.txt, written by runtime/install-props-v14.py (format: PROPS-V14.ru.txt):
//   obj  <Rxxx|*> <object|*> <part> <asset|-> <V/T/Q:fnv64> [xf=12 floats] [depth=triangle|chest] [ambient=gain]
//   door <DO2 hex> <mesh record> <asset|-> <V/T/Q:fnv64> [xf=12 floats]
// Identity = counts of the native 24-byte mesh record (u16 vertices +8, u16 triangles +14, u16 quads +16) and FNV-1a
// 64 over the int16 x,y,z of its 8-byte vertices; the DLL repeats it on the live mesh before every replacement, so a
// row never applies to other geometry (other room state, scenario, reused object slot, failed DO2 load).
// xf maps asset space (fitted to the binding's native_ref) to this target's native part space. asset '-' hides the
// native part. 'obj * *' rows match by identity in any room/object. No game memory, D3D or logging here
// (static3d/props-v14/prop_table_test.cpp).
#include "model_runtime.h"
#include <cstdlib>
#include <istream>
#include <sstream>

namespace re3prop {
struct Identity {
    std::uint32_t vertices=0;std::uint16_t triangles=0,quads=0;std::uint64_t hash=0;
    bool operator==(const Identity& o)const{return vertices==o.vertices&&triangles==o.triangles&&quads==o.quads&&hash==o.hash;}
};
enum class Depth:std::uint8_t{Triangle,Chest};
struct Rule {
    std::string asset;Identity identity;re3model::Matrix3x4 transform;bool transformed=false;Depth depth=Depth::Triangle;unsigned line=0;
    float ambient=1.f;  // props-bright 2026-11-07: opt-in 'ambient=<0.25..8>' multiplies the native ambient (GTE BK) for this row only
    bool hide()const{return asset=="-";}
};
// room = stage word A61FA6 << 8 | room word A61FA8 (R10C = 0x000C, R50B = 0x040B)
inline std::uint32_t ObjectKey(std::uint32_t room,std::uint32_t object,std::uint32_t part){return (room<<16)|((object&0xff)<<8)|(part&0xff);}
inline std::uint32_t DoorKey(std::uint32_t do2,std::uint32_t part){return ((do2&0xff)<<8)|(part&0xff);}
struct Table {
    std::unordered_map<std::uint32_t,std::vector<Rule>> objects;  // ObjectKey
    std::unordered_map<std::uint32_t,std::vector<Rule>> anyRoom;  // part -> identity-only rows
    std::unordered_map<std::uint32_t,std::vector<Rule>> doors;    // DoorKey
    std::vector<std::string> assets;                              // distinct HD assets named by the rows
    unsigned rows=0,invalid=0,firstInvalid=0;
    bool empty()const{return objects.empty()&&anyRoom.empty()&&doors.empty();}
};
inline std::uint64_t VertexHash(const std::uint8_t* vertices,std::uint32_t count){
    std::uint64_t hash=14695981039346656037ull;
    for(std::uint32_t n=0;n<count;++n)for(unsigned b=0;b<6;++b)hash=(hash^vertices[n*8+b])*1099511628211ull;
    return hash;
}
inline const Rule* Match(const std::vector<Rule>* rules,const Identity& identity){
    if(rules)for(const auto& rule:*rules)if(rule.identity==identity)return &rule;
    return nullptr;
}
inline bool HexValue(const std::string& text,std::uint32_t limit,std::uint32_t& value){
    if(text.empty()||text.size()>8)return false;char* end=nullptr;const unsigned long v=std::strtoul(text.c_str(),&end,16);
    if(*end||v>limit)return false;value=std::uint32_t(v);return true;
}
inline bool DecimalValue(const std::string& text,std::uint32_t limit,std::uint32_t& value){
    if(text.empty()||text.size()>5||text.find_first_not_of("0123456789")!=std::string::npos)return false;
    const unsigned long v=std::strtoul(text.c_str(),nullptr,10);if(v>limit)return false;value=std::uint32_t(v);return true;
}
inline bool ParseRoom(const std::string& text,std::uint32_t& room){
    std::uint32_t stage=0,number=0;
    if(text.size()!=4||text[0]!='R'||!HexValue(text.substr(1,1),15,stage)||!stage||!HexValue(text.substr(2),255,number))return false;
    room=((stage-1)<<8)|number;return true;
}
inline bool ParseIdentity(const std::string& text,Identity& out){
    const auto colon=text.find(':');if(colon==std::string::npos||text.size()-colon-1!=16)return false;
    std::istringstream counts(text.substr(0,colon));std::string v,t,q;
    if(!std::getline(counts,v,'/')||!std::getline(counts,t,'/')||!std::getline(counts,q))return false;
    std::uint32_t vertices=0,triangles=0,quads=0;
    if(!DecimalValue(v,4096,vertices)||!vertices||!DecimalValue(t,65535,triangles)||!DecimalValue(q,65535,quads))return false;
    const std::string hex=text.substr(colon+1);if(hex.find_first_not_of("0123456789abcdefABCDEF")!=std::string::npos)return false;
    out.vertices=vertices;out.triangles=std::uint16_t(triangles);out.quads=std::uint16_t(quads);out.hash=std::strtoull(hex.c_str(),nullptr,16);
    return true;
}
inline bool SafeAsset(const std::string& asset){
    if(asset=="-")return true;
    if(asset.empty()||asset.size()>64)return false;
    for(char c:asset)if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-'||c=='_'))return false;
    return true;
}
inline bool ParseTransform(const std::string& text,re3model::Matrix3x4& out){
    std::istringstream list(text);std::string item;unsigned n=0;re3model::Matrix3x4 m;
    while(std::getline(list,item,',')){
        if(n>=12||item.empty())return false;char* end=nullptr;const float v=std::strtof(item.c_str(),&end);
        if(*end||!std::isfinite(v)||std::abs(v)>1.e7f)return false;m.m[n++]=v;
    }
    re3model::Matrix3x4 normal;if(n!=12||!re3model::NormalMatrix(m,normal))return false;
    out=m;return true;
}
// One rule per line; invalid lines are counted and skipped (the rest of the table still applies).
inline void ParseTable(std::istream& in,Table& out){
    out=Table{};std::string line;unsigned number=0;
    while(std::getline(in,line)){
        ++number;const auto hash=line.find('#');if(hash!=std::string::npos)line.resize(hash);
        std::istringstream fields(line);std::string kind;if(!(fields>>kind))continue;
        ++out.rows;Rule rule;rule.line=number;bool ok=true;std::string a,b,c,asset,identity,option;
        std::uint32_t room=0,object=0,part=0,do2=0;bool anyRoom=false;
        if(kind=="obj"){
            ok=bool(fields>>a>>b>>c>>asset>>identity);
            if(ok){anyRoom=a=="*";ok=anyRoom?b=="*":(ParseRoom(a,room)&&DecimalValue(b,255,object));}
            ok=ok&&DecimalValue(c,255,part);
        }else if(kind=="door"){
            ok=bool(fields>>a>>c>>asset>>identity)&&HexValue(a,255,do2)&&a.size()==2&&DecimalValue(c,255,part);
        }else ok=false;
        ok=ok&&SafeAsset(asset)&&ParseIdentity(identity,rule.identity);rule.asset=asset;
        while(ok&&(fields>>option)){
            const auto equal=option.find('=');if(equal==std::string::npos){ok=false;break;}
            const std::string key=option.substr(0,equal),value=option.substr(equal+1);
            if(key=="xf"){ok=ParseTransform(value,rule.transform);rule.transformed=true;}
            else if(key=="depth"&&kind=="obj"){if(value=="chest")rule.depth=Depth::Chest;else ok=value=="triangle";}
            else if(key=="ambient"&&kind=="obj"){char* end=nullptr;const float v=std::strtof(value.c_str(),&end);ok=end&&!*end&&std::isfinite(v)&&v>=.25f&&v<=8.f;if(ok)rule.ambient=v;}
            else ok=false;
        }
        // the chest proxy orders by the exact native lid vertices: it cannot follow a native-to-native transform
        ok=ok&&!(rule.depth==Depth::Chest&&(rule.transformed||rule.hide()));
        if(!ok){if(!out.invalid)out.firstInvalid=number;++out.invalid;continue;}
        if(!rule.hide()&&std::find(out.assets.begin(),out.assets.end(),rule.asset)==out.assets.end())out.assets.push_back(rule.asset);
        if(kind=="door")out.doors[DoorKey(do2,part)].push_back(rule);
        else if(anyRoom)out.anyRoom[part].push_back(rule);
        else out.objects[ObjectKey(room,object,part)].push_back(rule);
    }
}

// Door round 2026-11-11: model-pack/door-fit.txt (written by qa/doors-2026-11-11/tools/fit.py, DOORS-2026-11-11.ru.txt)
//   door <DO2 hex> <mesh record> <side> move=<dx,dy,dz> [at=<x|*>,<y|*>,<z|*>]
// moves an HD door part in the frame of its native PARENT object: T' = (at or T) + move, T = the part's parent-relative
// translation (door object +0x34/+0x38/+0x3C, int32). side: '*' any placement; x+ x- y+ y- z+ z- = sign of T on that
// axis; f+ f- = sign of the part's own rotation m[0][0] (object +0x20). The first matching side row wins, then the first
// '*' row. The DLL applies it after the native matrix as M . T(R^T (T' - T)), R = object +0x20, so a part the door
// script turns keeps turning about its own moved axis.
struct DoorFit {
    std::uint8_t axis=4;   // 0..2 sign of T on x/y/z, 3 facing (m[0][0]), 4 any
    std::int8_t sign=0;
    float move[3]{},at[3]{};bool hasAt[3]{};unsigned line=0;
};
struct DoorFits {
    std::unordered_map<std::uint32_t,std::vector<DoorFit>> rows;   // DoorKey
    unsigned count=0,invalid=0,firstInvalid=0;
};
inline bool ParseFitVector(const std::string& text,float out[3],bool given[3],bool allowStar,float limit){
    std::istringstream list(text);std::string item;unsigned n=0;
    while(std::getline(list,item,',')){
        if(n>=3||item.empty())return false;
        if(item=="*"){if(!allowStar)return false;given[n]=false;out[n++]=0;continue;}
        char* end=nullptr;const float v=std::strtof(item.c_str(),&end);
        if(*end||!std::isfinite(v)||std::abs(v)>limit)return false;
        given[n]=true;out[n++]=v;
    }
    return n==3;
}
inline void ParseDoorFits(std::istream& in,DoorFits& out){
    out=DoorFits{};std::string line;unsigned number=0;
    while(std::getline(in,line)){
        ++number;const auto hash=line.find('#');if(hash!=std::string::npos)line.resize(hash);
        std::istringstream fields(line);std::string kind,a,c,side,option;if(!(fields>>kind))continue;
        DoorFit fit;fit.line=number;std::uint32_t do2=0,part=0;bool moveSeen=false;
        bool ok=kind=="door"&&bool(fields>>a>>c>>side)&&a.size()==2&&HexValue(a,255,do2)&&DecimalValue(c,255,part);
        if(ok){
            if(side=="*")fit.axis=4;
            else if(side.size()==2&&(side[1]=='+'||side[1]=='-')&&(side[0]=='x'||side[0]=='y'||side[0]=='z'||side[0]=='f')){
                fit.axis=side[0]=='f'?3:std::uint8_t(side[0]-'x');fit.sign=side[1]=='+'?1:-1;}
            else ok=false;
        }
        while(ok&&(fields>>option)){
            const auto equal=option.find('=');if(equal==std::string::npos){ok=false;break;}
            const std::string key=option.substr(0,equal),value=option.substr(equal+1);bool given[3]{};
            if(key=="move"){ok=ParseFitVector(value,fit.move,given,false,4000.f);moveSeen=true;}
            else if(key=="at")ok=ParseFitVector(value,fit.at,fit.hasAt,true,60000.f);
            else ok=false;
        }
        ok=ok&&(moveSeen||fit.hasAt[0]||fit.hasAt[1]||fit.hasAt[2]);
        if(!ok){if(!out.invalid)out.firstInvalid=number;++out.invalid;continue;}
        out.rows[DoorKey(do2,part)].push_back(fit);++out.count;
    }
}
// t: the part's parent-relative translation (object +0x34), m00: its rotation m[0][0] (object +0x20, 4096 = 1)
inline const DoorFit* MatchDoorFit(const DoorFits& fits,std::uint32_t do2,std::uint32_t part,const std::int32_t t[3],std::int16_t m00){
    const auto found=fits.rows.find(DoorKey(do2,part));if(found==fits.rows.end())return nullptr;
    for(const auto& fit:found->second){
        if(fit.axis>3)continue;
        const std::int32_t v=fit.axis==3?std::int32_t(m00):t[fit.axis];
        if((fit.sign>0&&v>0)||(fit.sign<0&&v<0))return &fit;
    }
    for(const auto& fit:found->second)if(fit.axis==4)return &fit;
    return nullptr;
}
// the parent-frame displacement of the part: (at or t) + move - t
inline void DoorFitOffset(const DoorFit& fit,const std::int32_t t[3],float out[3]){
    for(unsigned k=0;k<3;++k)out[k]=(fit.hasAt[k]?fit.at[k]-float(t[k]):0.f)+fit.move[k];
}
} // namespace re3prop
