#pragma once
// Optional mesh-bound facial deformers (R3FACE1) and voice-driven lip sync (R3LIP1).
// Deformers act on bind-space vertices before skinning; neutral values restore the exact
// base bytes. Missing/invalid data leaves the static face; nothing here touches native state.
#include "model_runtime.h"
#include "model_shoulder_corrective.h"
#include <array>
#include <cmath>

namespace re3face {
using namespace re3model;
enum Channel : std::uint32_t { Jaw=0, Wide=1, Round=2, Blink=3, GazeYaw=4, GazePitch=5, ChannelCount=6 };
struct Header {
    char magic[8];
    std::uint32_t version,headerBytes,vertexCount,deformerCount,entryCount,deformersOffset,entriesOffset,fileBytes;
    std::uint8_t meshSha256[32];
    float eyeCentre[3];
    std::uint32_t reserved;
};
struct DiskDeformer {std::uint32_t type,channel,firstEntry,entryCount;float centre[3],axis[3],maxAngle;float reserved[5];};
struct Entry {std::uint32_t vertex;float a[3],b[3];};
static_assert(sizeof(Header)==88&&sizeof(DiskDeformer)==64&&sizeof(Entry)==28,"R3FACE1 layout mismatch");

class FaceRig {
    bool valid_=false;
    std::vector<DiskDeformer> deformers_;
    std::vector<Entry> entries_;
    std::vector<std::uint32_t> affected_;          // sorted unique vertices touched by any deformer
    std::vector<std::uint32_t> slot_;              // per vertex: index into affected_ or ~0
    std::vector<Vec3> basePosition_,baseNormal_;   // exact base values of affected vertices
    std::vector<Vec3> workPosition_,workNormal_;
    std::size_t vertexCount_=0;
    Vec3 eyeCentre_{};
    bool dirty_=false;
    friend bool LoadFaceRigBytes(const std::vector<std::uint8_t>&,const std::vector<std::uint8_t>&,const Mesh&,FaceRig&,std::string*);
public:
    bool Valid()const{return valid_;}
    std::size_t DeformerCount()const{return deformers_.size();}
    std::size_t AffectedCount()const{return affected_.size();}
    const std::vector<std::uint32_t>& Affected()const{return affected_;}
    Vec3 EyeCentre()const{return eyeCentre_;}
    std::size_t MemoryBytes()const{return entries_.capacity()*sizeof(Entry)+slot_.capacity()*4+affected_.capacity()*4+basePosition_.capacity()*4*sizeof(Vec3);}
    // Writes deformed positions/normals of the affected vertices into mesh (same object it was loaded for).
    // All-zero values restore the base bytes exactly. Returns false (and restores) on invalid input.
    bool Apply(Mesh& mesh,const float values[ChannelCount]){
        if(!valid_||mesh.vertices.size()!=vertexCount_)return false;
        bool any=false;for(unsigned c=0;c<ChannelCount;++c){if(!std::isfinite(values[c]))return Restore(mesh),false;any=any||values[c]!=0.f;}
        if(!any){Restore(mesh);return true;}
        workPosition_=basePosition_;workNormal_=baseNormal_;
        for(const auto& d:deformers_){
            if(d.type!=1)continue;const float value=values[d.channel];if(value==0.f)continue;
            for(std::uint32_t n=0;n<d.entryCount;++n){const auto& e=entries_[d.firstEntry+n];const auto s=slot_[e.vertex];
                workPosition_[s]=workPosition_[s]+Vec3{e.a[0],e.a[1],e.a[2]}*value;
                workNormal_[s]=workNormal_[s]+Vec3{e.b[0],e.b[1],e.b[2]}*value;}
        }
        for(const auto& d:deformers_){
            if(d.type!=2)continue;const float value=values[d.channel];if(value==0.f)continue;
            const Vec3 c{d.centre[0],d.centre[1],d.centre[2]},k{d.axis[0],d.axis[1],d.axis[2]};
            for(std::uint32_t n=0;n<d.entryCount;++n){const auto& e=entries_[d.firstEntry+n];const auto s=slot_[e.vertex];
                const float angle=value*d.maxAngle*e.a[0];if(angle==0.f)continue;
                const float cs=std::cos(angle),sn=std::sin(angle);
                auto rotate=[&](Vec3 v){const float kd=Dot(k,v);return v*cs+Cross(k,v)*sn+k*(kd*(1.f-cs));};
                workPosition_[s]=rotate(workPosition_[s]-c)+c;workNormal_[s]=rotate(workNormal_[s]);}
        }
        for(std::size_t s=0;s<affected_.size();++s){
            const Vec3 n=workNormal_[s];
            if(!Finite(workPosition_[s])||!Finite(n)||Dot(n,n)<1.e-20f){Restore(mesh);return false;}
        }
        for(std::size_t s=0;s<affected_.size();++s){auto& v=mesh.vertices[affected_[s]];v.position=workPosition_[s];v.normal=Normalize(workNormal_[s]);}
        dirty_=true;return true;
    }
    void Restore(Mesh& mesh){
        if(!valid_||!dirty_||mesh.vertices.size()!=vertexCount_)return;
        for(std::size_t s=0;s<affected_.size();++s){auto& v=mesh.vertices[affected_[s]];v.position=basePosition_[s];v.normal=baseNormal_[s];}
        dirty_=false;
    }
};

inline bool LoadFaceRigBytes(const std::vector<std::uint8_t>& bytes,const std::vector<std::uint8_t>& meshBytes,const Mesh& mesh,FaceRig& output,std::string* error=nullptr){
    auto fail=[&](const char* text){if(error)*error=text;return false;};
    try {
        if(bytes.size()<sizeof(Header)||bytes.size()>64u*1024*1024)return fail("Face rig size outside bounds");
        Header h;std::memcpy(&h,bytes.data(),sizeof(h));
        if(std::memcmp(h.magic,"R3FACE1\0",8)||h.version!=1||h.headerBytes!=sizeof(Header)||h.reserved||h.fileBytes!=bytes.size())return fail("Invalid R3FACE1 header");
        if(h.vertexCount!=mesh.vertices.size()||!h.deformerCount||h.deformerCount>64||!h.entryCount||h.entryCount>2000000)return fail("Face rig counts do not match mesh");
        if(h.deformersOffset!=sizeof(Header)||std::uint64_t(h.entriesOffset)!=std::uint64_t(h.deformersOffset)+64ull*h.deformerCount||std::uint64_t(h.entriesOffset)+28ull*h.entryCount!=h.fileBytes)return fail("Invalid face rig sections");
        std::uint8_t sha[32];if(!re3shoulder::detail::Sha256(meshBytes,sha))return fail("Cannot hash face rig base mesh");
        if(std::memcmp(sha,h.meshSha256,32))return fail("Face rig base mesh SHA256 mismatch");
        Mesh base;std::string meshError;if(!LoadMeshBytes(meshBytes,base,&meshError)||!re3shoulder::detail::SameMesh(base,mesh))return fail("Face rig loaded mesh differs from bound file");
        FaceRig rig;rig.deformers_.resize(h.deformerCount);rig.entries_.resize(h.entryCount);
        std::memcpy(rig.deformers_.data(),bytes.data()+h.deformersOffset,64ull*h.deformerCount);
        std::memcpy(rig.entries_.data(),bytes.data()+h.entriesOffset,28ull*h.entryCount);
        std::uint64_t next=0;std::vector<std::uint8_t> touched(h.vertexCount,0);
        for(const auto& d:rig.deformers_){
            if((d.type!=1&&d.type!=2)||d.channel>=ChannelCount||d.firstEntry!=next||!d.entryCount||next+d.entryCount>h.entryCount)return fail("Invalid face deformer range");
            for(float r:d.reserved)if(r!=0.f)return fail("Face deformer reserved field");
            if(d.type==2){
                const Vec3 k{d.axis[0],d.axis[1],d.axis[2]},c{d.centre[0],d.centre[1],d.centre[2]};
                if(!Finite(k)||!Finite(c)||std::abs(Dot(k,k)-1.f)>1.e-3f||!std::isfinite(d.maxAngle)||d.maxAngle<=0||d.maxAngle>1.5708f)return fail("Invalid face rotation deformer");
            }
            std::uint32_t prior=0;
            for(std::uint32_t n=0;n<d.entryCount;++n){const auto& e=rig.entries_[d.firstEntry+n];
                if(e.vertex>=h.vertexCount||(n&&e.vertex<=prior))return fail("Face deformer entries unsorted/out of range");
                prior=e.vertex;
                for(unsigned k=0;k<3;++k)if(!std::isfinite(e.a[k])||!std::isfinite(e.b[k]))return fail("Face deformer nonfinite entry");
                if(d.type==1&&(std::abs(e.a[0])>100||std::abs(e.a[1])>100||std::abs(e.a[2])>100||std::abs(e.b[0])>4||std::abs(e.b[1])>4||std::abs(e.b[2])>4))return fail("Face morph delta outside bounds");
                if(d.type==2&&(std::abs(e.a[0])>2.f||e.a[1]!=0||e.a[2]!=0||e.b[0]!=0||e.b[1]!=0||e.b[2]!=0))return fail("Face rotation weight invalid");
                touched[e.vertex]=1;
            }
            next+=d.entryCount;
        }
        if(next!=h.entryCount)return fail("Face deformers do not cover entries");
        rig.slot_.assign(h.vertexCount,(std::numeric_limits<std::uint32_t>::max)());
        for(std::uint32_t i=0;i<h.vertexCount;++i)if(touched[i]){rig.slot_[i]=std::uint32_t(rig.affected_.size());rig.affected_.push_back(i);rig.basePosition_.push_back(mesh.vertices[i].position);rig.baseNormal_.push_back(mesh.vertices[i].normal);}
        rig.workPosition_=rig.basePosition_;rig.workNormal_=rig.baseNormal_;
        rig.vertexCount_=h.vertexCount;rig.eyeCentre_={h.eyeCentre[0],h.eyeCentre[1],h.eyeCentre[2]};
        if(!Finite(rig.eyeCentre_))return fail("Face rig eye centre invalid");
        rig.valid_=true;output=std::move(rig);if(error)error->clear();return true;
    }catch(const std::exception&){return fail("Face rig allocation/validation failed");}
}
inline bool LoadFaceRig(const std::string& path,const std::string& meshPath,const Mesh& mesh,FaceRig& output,std::string* error=nullptr){
    std::vector<std::uint8_t> bytes,meshBytes;
    if(!re3shoulder::detail::ReadFile(path,sizeof(Header),64u*1024*1024,bytes)){if(error)*error="Missing face rig";return false;}
    if(!re3shoulder::detail::ReadFile(meshPath,64,256u*1024*1024,meshBytes)){if(error)*error="Cannot read face rig base mesh";return false;}
    return LoadFaceRigBytes(bytes,meshBytes,mesh,output,error);
}

// ------------------------------------------------------------------ lip-sync tracks
struct LipClip {char name[16];std::uint32_t speaker,frameCount,firstFrame;float duration;};
static_assert(sizeof(LipClip)==32,"R3LIP1 clip layout");
class LipSync {
    std::vector<LipClip> clips_;
    std::vector<std::uint8_t> frames_;
    std::uint32_t fps_=100;
public:
    bool Valid()const{return !clips_.empty();}
    std::size_t ClipCount()const{return clips_.size();}
    bool Load(const std::string& path,std::string* error=nullptr){
        auto fail=[&](const char* text){if(error)*error=text;return false;};
        std::vector<std::uint8_t> b;if(!re3shoulder::detail::ReadFile(path,32,64u*1024*1024,b))return fail("Missing lip-sync file");
        if(std::memcmp(b.data(),"R3LIP1\0\0",8))return fail("Invalid R3LIP1 magic");
        std::uint32_t v[6];std::memcpy(v,b.data()+8,24);
        const std::uint32_t version=v[0],header=v[1],count=v[2],fps=v[3],framesOffset=v[4],fileBytes=v[5];
        if(version!=1||header!=32||!count||count>100000||fps<10||fps>1000||fileBytes!=b.size()||framesOffset!=32+32ull*count)return fail("Invalid R3LIP1 header");
        std::vector<LipClip> clips(count);std::memcpy(clips.data(),b.data()+32,32ull*count);
        const std::uint64_t totalFrames=(b.size()-framesOffset)/4;if((b.size()-framesOffset)%4)return fail("R3LIP1 frame section misaligned");
        for(auto& c:clips){c.name[15]=0;if(std::uint64_t(c.firstFrame)+c.frameCount>totalFrames||c.speaker>15||!std::isfinite(c.duration))return fail("R3LIP1 clip outside frames");}
        clips_=std::move(clips);frames_.assign(b.begin()+framesOffset,b.end());fps_=fps;if(error)error->clear();return true;
    }
    const LipClip* Find(const char* name)const{
        for(const auto& c:clips_)if(!_stricmp(c.name,name))return &c;
        return nullptr;
    }
    // Linear interpolation of jaw/wide/round at time t (seconds); false past the end.
    bool Sample(const LipClip& c,double t,float out[3])const{
        if(!(t>=0))return false;const double f=t*fps_;const auto i=std::uint64_t(f);
        if(i+1>=c.frameCount){out[0]=out[1]=out[2]=0;return false;}
        const float a=float(f-double(i));const auto* p=&frames_[std::size_t(c.firstFrame+i)*4];const auto* q=p+4;
        for(unsigned k=0;k<3;++k)out[k]=((1.f-a)*p[k]+a*q[k])*(1.f/255.f);
        return true;
    }
};

// ------------------------------------------------------------------ per-actor procedural state
struct ActorFace {
    float values[ChannelCount]{};
    double lastTime=0,nextBlink=0,blinkStart=-1,nextSaccade=0;
    float saccadeYaw=0,saccadePitch=0;
    std::uint32_t random=0x9e3779b9u;
    float Random(){random^=random<<13;random^=random>>17;random^=random<<5;return float(random&0xffffff)/16777216.f;}
};
inline float Approach(float value,float target,float dt,float seconds){
    if(seconds<=0)return target;const float a=1.f-std::exp(-dt/seconds);return value+(target-value)*a;
}
// Blink curve: 70 ms closing, 40 ms closed, 110 ms opening.
inline float BlinkCurve(double t){
    if(t<0)return 0;if(t<0.07)return float(t/0.07);if(t<0.11)return 1;if(t<0.22)return float(1-(t-0.11)/0.11);return 0;
}
} // namespace re3face
