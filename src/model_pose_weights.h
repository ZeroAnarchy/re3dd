#pragma once
// Optional pose-driven second weight set (round 3, Carlos: the rolled olive sleeves stayed out sideways as flaps when
// the arm swung forward to aim). Listed vertices are skinned again with their own weights (linear) and blended in by
// how far each upper arm points forward of the torso and not down, so a sleeve follows a raised arm while the accepted
// weights stay untouched for lowered arms (factor 0 -> output identical to the plain skin). Bound to the exact
// model.r3mesh by SHA-256. No history or time smoothing.
//
// pose-weights.txt:
//   R3POSEWEIGHTS 1
//   mesh <sha256 hex of model.r3mesh> <vertex count>
//   driver <side 0 right|1 left> <shoulder bone> <elbow bone> <forward low> <forward high> <down low> <down high>
//   v <vertex> <side> <bone0..3> <weight0..3 as uint16 summing to 65535> [dx dy dz]
//     dx dy dz: optional bind-space offset of the vertex for the second skin (the wide sleeve cuff drawn in to the arm)
// The arm direction (elbow - shoulder) is taken in the torso frame (bone 0; bind: +x forward, +y down):
// factor = smoothstep(forward low, high, dir.x) * (1 - smoothstep(down low, high, dir.y)).
#include "model_runtime.h"
#include "model_shoulder_corrective.h"
#include <sstream>

namespace re3poseweights {
using namespace re3model;
struct Entry {std::uint32_t vertex;std::uint8_t side;std::uint16_t bone[4],weight[4];Vec3 offset;};
struct Driver {unsigned shoulder=0,elbow=0;float forwardLow=0,forwardHigh=1,downLow=0,downHigh=1;bool set=false;};
struct PoseWeights {
    std::vector<Entry> entries;Driver driver[2];bool valid=false;
    std::size_t MemoryBytes()const{return entries.capacity()*sizeof(Entry);}
};

inline float Smoothstep(float low,float high,float x){const float t=(std::max)(0.f,(std::min)(1.f,(x-low)/(high-low)));return t*t*(3-2*t);}

inline bool Load(const std::string& path,const std::string& meshPath,const Mesh& mesh,PoseWeights& output,std::string* error){
    auto fail=[&](const char* text){if(error)*error=text;return false;};
    std::vector<std::uint8_t> bytes,meshBytes;
    if(!re3shoulder::detail::ReadFile(path,16,16u*1024*1024,bytes))return fail("Missing or invalid pose-weights.txt");
    if(!re3shoulder::detail::ReadFile(meshPath,64,256u*1024*1024,meshBytes))return fail("Cannot read pose-weights base mesh");
    std::uint8_t sha[32];if(!re3shoulder::detail::Sha256(meshBytes,sha))return fail("Cannot hash pose-weights base mesh");
    char shaText[65];for(unsigned i=0;i<32;++i)sprintf_s(shaText+2*i,3,"%02x",sha[i]);
    std::istringstream in(std::string(bytes.begin(),bytes.end()));std::string line,key;PoseWeights result;bool header=false,bound=false;
    while(std::getline(in,line)){
        std::istringstream f(line);if(!(f>>key)||key[0]=='#')continue;
        if(key=="R3POSEWEIGHTS"){unsigned version=0;f>>version;header=version==1;continue;}
        if(!header)return fail("pose-weights.txt: missing R3POSEWEIGHTS 1 header");
        if(key=="mesh"){std::string hex;std::size_t count=0;f>>hex>>count;if(hex!=shaText||count!=mesh.vertices.size())return fail("pose-weights.txt is bound to another model.r3mesh");bound=true;continue;}
        if(key=="driver"){unsigned side=2;Driver d;f>>side>>d.shoulder>>d.elbow>>d.forwardLow>>d.forwardHigh>>d.downLow>>d.downHigh;
            if(!f||side>1||d.shoulder>=mesh.inverseBind.size()||d.elbow>=mesh.inverseBind.size()||!(d.forwardHigh>d.forwardLow)||!(d.downHigh>d.downLow))return fail("pose-weights.txt: invalid driver");
            d.set=true;result.driver[side]=d;continue;}
        if(key=="v"){Entry e{};unsigned side=2,b[4]{},w[4]{};f>>e.vertex>>side>>b[0]>>b[1]>>b[2]>>b[3]>>w[0]>>w[1]>>w[2]>>w[3];
            unsigned sum=0;bool ok=bool(f)&&side<=1&&e.vertex<mesh.vertices.size();
            for(unsigned k=0;k<4&&ok;++k){ok=b[k]<mesh.inverseBind.size()&&w[k]<=65535;e.bone[k]=std::uint16_t(b[k]);e.weight[k]=std::uint16_t(w[k]);sum+=w[k];}
            if(!ok||sum!=65535)return fail("pose-weights.txt: invalid vertex entry");
            float o[3]{};if(f>>o[0]){f>>o[1]>>o[2];if(!f||!std::isfinite(o[0])||!std::isfinite(o[1])||!std::isfinite(o[2])||std::abs(o[0])+std::abs(o[1])+std::abs(o[2])>2000)return fail("pose-weights.txt: invalid vertex offset");}
            e.offset={o[0],o[1],o[2]};
            e.side=std::uint8_t(side);result.entries.push_back(e);continue;}
        return fail("pose-weights.txt: unknown line");
    }
    if(!bound||result.entries.empty())return fail("pose-weights.txt: no mesh binding or no entries");
    for(const auto& e:result.entries)if(!result.driver[e.side].set)return fail("pose-weights.txt: entry without a driver for its side");
    result.valid=true;output=std::move(result);if(error)error->clear();return true;
}

// factors[side]: how far this pose blends the second skin in.
inline bool Factors(const PoseWeights& weights,const std::vector<Matrix3x4>& world,float factors[2],std::string* error){
    auto fail=[&](const char* text){if(error)*error=text;return false;};
    factors[0]=factors[1]=0;
    const auto& torso=world[0];double a[9];for(unsigned r=0;r<3;++r)for(unsigned c=0;c<3;++c)a[r*3+c]=torso.m[r*4+c];
    const double det=a[0]*(a[4]*a[8]-a[5]*a[7])-a[1]*(a[3]*a[8]-a[5]*a[6])+a[2]*(a[3]*a[7]-a[4]*a[6]);
    if(!(std::abs(det)>1.e-12)||!std::isfinite(det))return fail("pose weights: singular torso");
    const double inv[9]={(a[4]*a[8]-a[5]*a[7])/det,(a[2]*a[7]-a[1]*a[8])/det,(a[1]*a[5]-a[2]*a[4])/det,
        (a[5]*a[6]-a[3]*a[8])/det,(a[0]*a[8]-a[2]*a[6])/det,(a[2]*a[3]-a[0]*a[5])/det,
        (a[3]*a[7]-a[4]*a[6])/det,(a[1]*a[6]-a[0]*a[7])/det,(a[0]*a[4]-a[1]*a[3])/det};
    for(unsigned side=0;side<2;++side){const auto& d=weights.driver[side];if(!d.set)continue;
        double dir[3],local[3]{};for(unsigned r=0;r<3;++r)dir[r]=double(world[d.elbow].m[r*4+3])-world[d.shoulder].m[r*4+3];
        for(unsigned r=0;r<3;++r)for(unsigned c=0;c<3;++c)local[r]+=inv[r*3+c]*dir[c];
        const double length=std::sqrt(local[0]*local[0]+local[1]*local[1]+local[2]*local[2]);
        if(!(length>1.e-9)||!std::isfinite(length))return fail("pose weights: degenerate arm direction");
        factors[side]=Smoothstep(d.forwardLow,d.forwardHigh,float(local[0]/length))*(1-Smoothstep(d.downLow,d.downHigh,float(local[1]/length)));
    }
    return true;
}
// factors[side] receives the blend factor used this frame; skinned must be the plain skin of mesh with world.
inline bool Apply(const PoseWeights& weights,const Mesh& mesh,const std::vector<Matrix3x4>& world,std::vector<SkinnedVertex>& skinned,float factors[2],std::string* error){
    auto fail=[&](const char* text){if(error)*error=text;return false;};
    if(!weights.valid||skinned.size()!=mesh.vertices.size()||world.size()<mesh.inverseBind.size())return fail("pose weights: unbound or incomplete pose");
    if(!Factors(weights,world,factors,error))return false;
    if(factors[0]<=0&&factors[1]<=0)return true;   // lowered arms: the plain skin, untouched
    std::vector<Matrix3x4> transforms(mesh.inverseBind.size()),normals(mesh.inverseBind.size());std::vector<std::uint8_t> ready(mesh.inverseBind.size(),0);
    for(const auto& e:weights.entries){const float s=factors[e.side];if(s<=0)continue;
        const auto& v=mesh.vertices[e.vertex];const Vec3 bind=v.position+e.offset;Vec3 position{},normal{};
        for(unsigned k=0;k<4;++k)if(e.weight[k]){const unsigned b=e.bone[k];
            if(!ready[b]){transforms[b]=Multiply(world[b],mesh.inverseBind[b]);if(!NormalMatrix(transforms[b],normals[b]))return fail("pose weights: singular skin transform");ready[b]=1;}
            const float w=float(e.weight[k])*(1.f/65535.f);position=position+TransformPoint(transforms[b],bind)*w;normal=normal+TransformVector(normals[b],v.normal)*w;}
        auto& out=skinned[e.vertex];const Vec3 n=out.normal*(1-s)+Normalize(normal)*s;
        out.position=out.position*(1-s)+position*s;
        if(!Finite(out.position)||!Finite(n)||Dot(n,n)<1.e-20f)return fail("pose weights produced an invalid vertex");
        out.normal=Normalize(n);
    }
    if(error)error->clear();return true;
}
} // namespace re3poseweights
