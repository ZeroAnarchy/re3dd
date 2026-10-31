#pragma once
// Optional mesh-bound sparse shoulder correction. No history or time smoothing.
#include "model_runtime.h"
#include <array>
#include <windows.h>
#include <bcrypt.h>
#pragma comment(lib,"bcrypt.lib")

namespace re3shoulder {
using namespace re3model;
struct Header {
    char magic[8];
    std::uint32_t version,headerBytes,vertexCount,entryCount;
    float low,high;
    std::uint8_t baseMeshSha256[32];
    std::uint32_t entriesOffset,entryStride,fileBytes,reserved;
};
struct Entry {std::uint32_t vertexIndex,side;Vec3 deltaPosition,deltaNormal;};
static_assert(sizeof(Header)==80&&sizeof(Entry)==32,"R3SHCR1 binary layout mismatch");
struct RaisedHeader {float low,high;std::uint32_t layerCount;float headPitchLiftDegrees;};
static_assert(sizeof(RaisedHeader)==16,"R3SHCR2 extended header layout mismatch");
struct Factors {float side[2]{},raised[2]{};};
class ShoulderCorrective;
inline bool LoadShoulderCorrectiveBytes(const std::vector<std::uint8_t>& bytes,const std::vector<std::uint8_t>& meshBytes,const Mesh& mesh,ShoulderCorrective& output,std::string* error);

class ShoulderCorrective {
    bool valid_=false;
    float low_=0,high_=0,raisedLow_=0,raisedHigh_=0,headPitchLiftDegrees_=0;
    bool hasRaised_=false;
    const Vertex* boundVertices_=nullptr;
    const std::uint32_t* boundIndices_=nullptr;
    const Matrix3x4* boundInverse_=nullptr;
    std::size_t indexCount_=0,boneCount_=0;
    std::vector<Entry> entries_;
    std::vector<std::uint32_t> byVertex_,raisedByVertex_;
    std::array<std::uint8_t,32> meshSha_{};
    friend bool LoadShoulderCorrectiveBytes(const std::vector<std::uint8_t>&,const std::vector<std::uint8_t>&,const Mesh&,ShoulderCorrective&,std::string*);
public:
    // The bound Mesh is immutable after loading, as are normal runtime assets.
    bool Matches(const Mesh& mesh)const{return valid_&&boundVertices_==mesh.vertices.data()&&byVertex_.size()==mesh.vertices.size()&&boundIndices_==mesh.indices.data()&&indexCount_==mesh.indices.size()&&boundInverse_==mesh.inverseBind.data()&&boneCount_==mesh.inverseBind.size();}
    std::size_t EntryCount()const{return entries_.size();}
    std::size_t MemoryBytes()const{return entries_.capacity()*sizeof(Entry)+(byVertex_.capacity()+raisedByVertex_.capacity())*sizeof(std::uint32_t);}
    bool HasRaised()const{return hasRaised_;}
    unsigned FormatVersion()const{return valid_?(hasRaised_?2u:1u):0u;}
    float HeadPitchLiftDegrees()const{return headPitchLiftDegrees_;}
    const Entry* ForRaisedVertex(std::size_t index)const{if(!valid_||index>=raisedByVertex_.size())return nullptr;const auto entry=raisedByVertex_[index];return entry<entries_.size()?&entries_[entry]:nullptr;}
    const Entry* ForVertex(std::size_t index)const{if(!valid_||index>=byVertex_.size())return nullptr;const auto entry=byVertex_[index];return entry<entries_.size()?&entries_[entry]:nullptr;}
    bool Evaluate(const Mesh& mesh,const std::vector<Matrix3x4>& world,Factors& output,std::string* error=nullptr)const{
        auto fail=[&](const char* text){if(error)*error=text;return false;};
        if(!Matches(mesh)||world.size()<7)return fail("Shoulder corrective missing, unbound, or incomplete pose");
        const auto& torso=world[0];if(!Finite(torso))return fail("Shoulder corrective nonfinite torso");
        double a[9];for(unsigned row=0;row<3;++row)for(unsigned col=0;col<3;++col)a[row*3+col]=torso.m[row*4+col];
        const double det=a[0]*(a[4]*a[8]-a[5]*a[7])-a[1]*(a[3]*a[8]-a[5]*a[6])+a[2]*(a[3]*a[7]-a[4]*a[6]);
        if(!(det>1.e-12)||!std::isfinite(det))return fail("Shoulder corrective singular/reflected torso");
        const double inverse[9]={(a[4]*a[8]-a[5]*a[7])/det,(a[2]*a[7]-a[1]*a[8])/det,(a[1]*a[5]-a[2]*a[4])/det,
            (a[5]*a[6]-a[3]*a[8])/det,(a[0]*a[8]-a[2]*a[6])/det,(a[2]*a[3]-a[0]*a[5])/det,
            (a[3]*a[7]-a[4]*a[6])/det,(a[1]*a[6]-a[0]*a[7])/det,(a[0]*a[4]-a[1]*a[3])/det};
        double normA=0,normInverse=0;for(unsigned i=0;i<9;++i){normA+=a[i]*a[i];normInverse+=inverse[i]*inverse[i];}
        if(!std::isfinite(normA*normInverse)||normA*normInverse>1.e16)return fail("Shoulder corrective ill-conditioned torso");
        Factors factors;
        for(unsigned side=0;side<2;++side){const unsigned shoulder=side?5:2,elbow=shoulder+1;
            if(!Finite(world[shoulder])||!Finite(world[elbow]))return fail("Shoulder corrective nonfinite arm pose");
            double direction[3],local[3]{};for(unsigned row=0;row<3;++row)direction[row]=double(world[elbow].m[row*4+3])-world[shoulder].m[row*4+3];
            for(unsigned row=0;row<3;++row)for(unsigned col=0;col<3;++col)local[row]+=inverse[row*3+col]*direction[col];
            const double length=std::sqrt(local[0]*local[0]+local[1]*local[1]+local[2]*local[2]);
            if(!(length>1.e-10)||!std::isfinite(length))return fail("Shoulder corrective degenerate arm direction");
            const double downDot=(std::max)(-1.,(std::min)(1.,local[1]/length));
            const double t=(std::max)(0.,(std::min)(1.,(downDot-double(low_))/(double(high_)-low_)));
            factors.side[side]=float(t*t*(3-2*t));
            if(hasRaised_){const double raisedT=(std::max)(0.,(std::min)(1.,(downDot-double(raisedLow_))/(double(raisedHigh_)-raisedLow_)));factors.raised[side]=float(1-raisedT*raisedT*(3-2*raisedT));}
        }
        output=factors;if(error)error->clear();return true;
    }
};

inline bool ApplyEntry(const Entry& entry,float factor,const Vertex& source,Vertex& output){
    if(!std::isfinite(factor)||factor<0||factor>1)return false;
    if(factor==0){output=source;return true;}
    Vertex result=source;result.position=result.position+entry.deltaPosition*factor;
    const Vec3 normal=result.normal+entry.deltaNormal*factor;
    if(!Finite(result.position)||!Finite(normal)||Dot(normal,normal)<1.e-20f)return false;
    result.normal=Normalize(normal);output=result;return true;
}


inline bool ApplyCombined(const Entry* low,float lowFactor,const Entry& raised,float raisedFactor,const Vertex& source,Vertex& output){
    if(!std::isfinite(lowFactor)||!std::isfinite(raisedFactor)||lowFactor<0||lowFactor>1||raisedFactor<0||raisedFactor>1)return false;
    // Preserve every old operation and rounding when raised deformation is zero.
    if(raisedFactor==0){if(low&&lowFactor>0)return ApplyEntry(*low,lowFactor,source,output);output=source;return true;}
    Vertex result=source;Vec3 normal=source.normal;
    if(low&&lowFactor>0){result.position=result.position+low->deltaPosition*lowFactor;normal=normal+low->deltaNormal*lowFactor;}
    result.position=result.position+raised.deltaPosition*raisedFactor;normal=normal+raised.deltaNormal*raisedFactor;
    if(!Finite(result.position)||!Finite(normal)||Dot(normal,normal)<1.e-20f)return false;
    result.normal=Normalize(normal);output=result;return true;
}

namespace detail {
inline bool Sha256(const std::vector<std::uint8_t>& data,std::uint8_t output[32]){
    std::vector<std::uint8_t> object;
    struct Handles {BCRYPT_ALG_HANDLE algorithm=nullptr;BCRYPT_HASH_HANDLE hash=nullptr;~Handles(){if(hash)BCryptDestroyHash(hash);if(algorithm)BCryptCloseAlgorithmProvider(algorithm,0);}} h;
    if(data.size()>256u*1024*1024||BCryptOpenAlgorithmProvider(&h.algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)return false;
    ULONG bytes=0,written=0;
    if(BCryptGetProperty(h.algorithm,BCRYPT_OBJECT_LENGTH,reinterpret_cast<PUCHAR>(&bytes),sizeof(bytes),&written,0)<0||written!=sizeof(bytes)||!bytes||bytes>1024*1024)return false;
    object.resize(bytes);
    if(BCryptCreateHash(h.algorithm,&h.hash,object.data(),bytes,nullptr,0,0)<0)return false;
    return BCryptHashData(h.hash,const_cast<PUCHAR>(data.data()),ULONG(data.size()),0)>=0&&BCryptFinishHash(h.hash,output,32,0)>=0;
}
inline bool SameMesh(const Mesh& a,const Mesh& b){
    if(a.vertices.size()!=b.vertices.size()||a.indices!=b.indices||a.inverseBind.size()!=b.inverseBind.size()||a.materials.size()!=b.materials.size())return false;
    if(std::memcmp(a.vertices.data(),b.vertices.data(),a.vertices.size()*sizeof(Vertex))||std::memcmp(a.inverseBind.data(),b.inverseBind.data(),a.inverseBind.size()*sizeof(Matrix3x4)))return false;
    for(std::size_t i=0;i<a.materials.size();++i){const auto& x=a.materials[i];const auto& y=b.materials[i];if(x.firstIndex!=y.firstIndex||x.indexCount!=y.indexCount||x.flags!=y.flags||x.baseColorRGBA!=y.baseColorRGBA||x.albedoPath!=y.albedoPath||x.stpPath!=y.stpPath)return false;}
    return true;
}
inline bool SameDelta(const Entry& a,const Entry& b){return a.side==b.side&&!std::memcmp(&a.deltaPosition,&b.deltaPosition,24);}

inline bool JointNormalSafe(Vec3 normal,Vec3 low,Vec3 raised){
    const double n[3]={normal.x,normal.y,normal.z},a[3]={low.x,low.y,low.z},b[3]={raised.x,raised.y,raised.z};
    double aa=0,ab=0,bb=0,na=0,nb=0;for(unsigned k=0;k<3;++k){aa+=a[k]*a[k];ab+=a[k]*b[k];bb+=b[k]*b[k];na+=n[k]*a[k];nb+=n[k]*b[k];}
    double minimum=(std::numeric_limits<double>::max)();
    auto evaluate=[&](double x,double y){double length=0;for(unsigned k=0;k<3;++k){const double v=n[k]+x*a[k]+y*b[k];length+=v*v;}minimum=(std::min)(minimum,length);};
    auto clamp=[](double value){return (std::max)(0.,(std::min)(1.,value));};
    // Exact quadratic minimum on all four edges of the closed unit square.
    for(double fixed:{0.,1.}){evaluate(aa>0?clamp(-(na+fixed*ab)/aa):0,fixed);evaluate(fixed,bb>0?clamp(-(nb+fixed*ab)/bb):0);}
    const double cross[3]={a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]};
    const double determinant=cross[0]*cross[0]+cross[1]*cross[1]+cross[2]*cross[2];
    if(determinant>0&&std::isfinite(determinant)){
        const double x=(ab*nb-bb*na)/determinant,y=(ab*na-aa*nb)/determinant;
        if(std::isfinite(x)&&std::isfinite(y)&&x>=0&&x<=1&&y>=0&&y<=1)evaluate(x,y);
    }
    return std::isfinite(minimum)&&minimum>1.e-12;
}

inline bool ReadFile(const std::string& path,std::size_t minimum,std::size_t maximum,std::vector<std::uint8_t>& out){
    std::ifstream f(path,std::ios::binary|std::ios::ate);if(!f)return false;const auto size=f.tellg();if(size<static_cast<std::streamoff>(minimum)||size>static_cast<std::streamoff>(maximum))return false;
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));f.seekg(0);if(!f.read(reinterpret_cast<char*>(bytes.data()),static_cast<std::streamsize>(bytes.size())))return false;out=std::move(bytes);return true;
}
}

inline bool LoadShoulderCorrectiveBytes(const std::vector<std::uint8_t>& bytes,const std::vector<std::uint8_t>& meshBytes,const Mesh& mesh,ShoulderCorrective& output,std::string* error=nullptr){
    auto fail=[&](const char* text){if(error)*error=text;return false;};
    try {
        if(bytes.size()<sizeof(Header)||bytes.size()>96u+2000000u*32u)return fail("Shoulder corrective file size outside bounds");
        Header header;std::memcpy(&header,bytes.data(),sizeof(header));
        const bool v1=!std::memcmp(header.magic,"R3SHCR1\0",8)&&header.version==1;
        const bool v2=!std::memcmp(header.magic,"R3SHCR2\0",8)&&header.version==2;
        const unsigned headerSize=v2?96:80;
        if((!v1&&!v2)||header.headerBytes!=headerSize||header.entriesOffset!=headerSize||header.entryStride!=32||header.reserved||header.fileBytes!=bytes.size())return fail("Invalid shoulder corrective header");
        if(bytes.size()<headerSize||!header.vertexCount||header.vertexCount>1000000||!header.entryCount||header.entryCount>header.vertexCount*(v2?2u:1u)||header.vertexCount!=mesh.vertices.size()||std::uint64_t(headerSize)+std::uint64_t(header.entryCount)*32!=bytes.size())return fail("Invalid shoulder corrective counts/sections");
        auto thresholds=[](float low,float high){return std::isfinite(low)&&std::isfinite(high)&&low>=-1&&high<=1&&low<high;};
        if(!thresholds(header.low,header.high))return fail("Invalid shoulder corrective driver thresholds");
        RaisedHeader raised{};if(v2){std::memcpy(&raised,bytes.data()+80,16);if(raised.layerCount!=2||!thresholds(raised.low,raised.high)||!std::isfinite(raised.headPitchLiftDegrees)||std::abs(raised.headPitchLiftDegrees)>10)return fail("Invalid raised shoulder driver/head header");}
        std::uint8_t sha[32];if(!detail::Sha256(meshBytes,sha))return fail("Cannot hash shoulder corrective base mesh");
        if(std::memcmp(sha,header.baseMeshSha256,32))return fail("Shoulder corrective base mesh SHA256 mismatch");
        Mesh base;std::string meshError;if(!LoadMeshBytes(meshBytes,base,&meshError)||!detail::SameMesh(base,mesh))return fail("Shoulder corrective loaded mesh differs from bound file");
        if(base.inverseBind.size()<7)return fail("Shoulder corrective requires torso/arm bones0..6");
        if(base.canonicalVertex.size()!=base.vertices.size()&&!BuildCanonicalVertexMap(base))return fail("Cannot validate shoulder corrective canonical groups");
        ShoulderCorrective result;result.entries_.resize(header.entryCount);std::memcpy(result.entries_.data(),bytes.data()+headerSize,header.entryCount*32);
        const auto empty=(std::numeric_limits<std::uint32_t>::max)();result.byVertex_.assign(header.vertexCount,empty);if(v2)result.raisedByVertex_.assign(header.vertexCount,empty);
        unsigned lowCount=0,raisedCount=0;
        for(std::uint32_t n=0;n<header.entryCount;++n){const auto& entry=result.entries_[n];
            if(entry.vertexIndex>=header.vertexCount||entry.side>=(v2?4u:2u))return fail("Shoulder corrective vertex/channel outside range");
            if(n){const auto& prior=result.entries_[n-1];if(entry.vertexIndex<prior.vertexIndex||(entry.vertexIndex==prior.vertexIndex&&(!v2||entry.side<=prior.side)))return fail("Shoulder corrective entries must be sorted and channel-unique");}
            auto& lookup=entry.side<2?result.byVertex_:result.raisedByVertex_;
            if(lookup[entry.vertexIndex]!=empty)return fail("Shoulder corrective has multiple sides in one vertex layer");
            if(!Finite(entry.deltaPosition)||!Finite(entry.deltaNormal))return fail("Shoulder corrective nonfinite delta");
            if(Dot(entry.deltaPosition,entry.deltaPosition)==0&&Dot(entry.deltaNormal,entry.deltaNormal)==0)return fail("Shoulder corrective redundant zero entry");
            const auto& vertex=base.vertices[entry.vertexIndex];
            if(!Finite(vertex.position+entry.deltaPosition)||!Finite(vertex.normal+entry.deltaNormal))return fail("Shoulder corrective nonfinite endpoint");
            if(!detail::JointNormalSafe(vertex.normal,entry.deltaNormal,{}))return fail("Shoulder corrective normal degenerates during interpolation");
            lookup[entry.vertexIndex]=n;if(entry.side<2)++lowCount;else ++raisedCount;
        }
        if(!lowCount||(v2&&!raisedCount))return fail("Shoulder corrective required layer is empty");
        for(std::size_t i=0;i<base.vertices.size();++i){const auto canonical=base.canonicalVertex[i];
            for(unsigned layer=0;layer<(v2?2u:1u);++layer){const auto& lookup=layer?result.raisedByVertex_:result.byVertex_;const auto a=lookup[i],b=lookup[canonical];
                if(a>=result.entries_.size()&&b>=result.entries_.size())continue;
                if(a>=result.entries_.size()||b>=result.entries_.size()||!detail::SameDelta(result.entries_[a],result.entries_[b]))return fail("Shoulder corrective canonical UV-split deltas differ");
            }
            if(v2){const auto a=result.byVertex_[i],b=result.raisedByVertex_[i];if(a<result.entries_.size()&&b<result.entries_.size()){
                const auto& low=result.entries_[a];const auto& high=result.entries_[b];const auto& vertex=base.vertices[i];
                if(!Finite(vertex.position+low.deltaPosition+high.deltaPosition)||!Finite(vertex.normal+low.deltaNormal+high.deltaNormal))return fail("Shoulder corrective nonfinite combined endpoint");
                if(!detail::JointNormalSafe(vertex.normal,low.deltaNormal,high.deltaNormal))return fail("Shoulder corrective combined normal degenerates inside layer square");
            }}
        }
        result.valid_=true;result.low_=header.low;result.high_=header.high;result.hasRaised_=v2;result.raisedLow_=raised.low;result.raisedHigh_=raised.high;result.headPitchLiftDegrees_=raised.headPitchLiftDegrees;result.boundVertices_=mesh.vertices.data();result.boundIndices_=mesh.indices.data();result.boundInverse_=mesh.inverseBind.data();result.indexCount_=mesh.indices.size();result.boneCount_=mesh.inverseBind.size();std::memcpy(result.meshSha_.data(),sha,32);
        output=std::move(result);if(error)error->clear();return true;
    }catch(const std::exception&){return fail("Shoulder corrective allocation/validation failed");}
}
inline bool LoadShoulderCorrective(const std::string& path,const std::string& meshPath,const Mesh& mesh,ShoulderCorrective& output,std::string* error=nullptr){
    try {std::vector<std::uint8_t> bytes,meshBytes;
        if(!detail::ReadFile(path,80,96u+2000000u*32u,bytes)){if(error)*error="Missing or invalid required shoulder corrective file";return false;}
        if(!detail::ReadFile(meshPath,64,256u*1024*1024,meshBytes)){if(error)*error="Cannot read shoulder corrective base mesh";return false;}
        return LoadShoulderCorrectiveBytes(bytes,meshBytes,mesh,output,error);
    }catch(const std::exception&){if(error)*error="Shoulder corrective read/allocation failed";return false;}
}
} // namespace re3shoulder
