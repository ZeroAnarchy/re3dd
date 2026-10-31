#pragma once
// v14 prop table model-pack/props.txt, written by runtime/install-props-v14.py (format: PROPS-V14.ru.txt):
//   obj  <Rxxx|*> <object|*> <part> <asset|-> <V/T/Q:fnv64> [xf=12 floats] [depth=triangle|chest]
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
} // namespace re3prop
