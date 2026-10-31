#pragma once
// In-between poses for animation keys the game holds for several frames. Many cutscene clips repeat
// each keyframe (15 keys/s at 30 frames/s), which reads as stop motion on the HD models. When a new key
// arrives after a hold of 2..4 frames, the shown pose moves from the current one to the new key over that
// many frames (one key of latency); when the pose changes every frame the input passes through bit-exact.
// JointSmoother (below) then removes the whole-unit rounding of the native joint positions.
// Only HD rendering uses the result; native animation, collision and saves are never touched.
#include "model_runtime.h"
#include <array>
#include <cmath>
#include <cstdint>

namespace re3motion {
using re3model::Matrix3x4;
constexpr unsigned kBones=15;
using Pose=std::array<Matrix3x4,kBones>;

struct Quat {float w=1,x=0,y=0,z=0;};
// Rotation (Gram-Schmidt of the columns, i.e. bone axes in the parent space) times a residual stretch S,
// so native affine stretch survives interpolation: M = R * S.
struct Split {Quat q;float s[9];float t[3];};

inline Quat FromRotation(const float r[9]){ // r row-major
    Quat q;const float trace=r[0]+r[4]+r[8];
    if(trace>0){const float s=std::sqrt(trace+1.f)*2.f;q.w=0.25f*s;q.x=(r[7]-r[5])/s;q.y=(r[2]-r[6])/s;q.z=(r[3]-r[1])/s;}
    else if(r[0]>r[4]&&r[0]>r[8]){const float s=std::sqrt(1.f+r[0]-r[4]-r[8])*2.f;q.w=(r[7]-r[5])/s;q.x=0.25f*s;q.y=(r[1]+r[3])/s;q.z=(r[2]+r[6])/s;}
    else if(r[4]>r[8]){const float s=std::sqrt(1.f+r[4]-r[0]-r[8])*2.f;q.w=(r[2]-r[6])/s;q.x=(r[1]+r[3])/s;q.y=0.25f*s;q.z=(r[5]+r[7])/s;}
    else{const float s=std::sqrt(1.f+r[8]-r[0]-r[4])*2.f;q.w=(r[3]-r[1])/s;q.x=(r[2]+r[6])/s;q.y=(r[5]+r[7])/s;q.z=0.25f*s;}
    const float n=std::sqrt(q.w*q.w+q.x*q.x+q.y*q.y+q.z*q.z);q.w/=n;q.x/=n;q.y/=n;q.z/=n;return q;
}
inline void ToRotation(const Quat& q,float r[9]){
    const float xx=q.x*q.x,yy=q.y*q.y,zz=q.z*q.z,xy=q.x*q.y,xz=q.x*q.z,yz=q.y*q.z,wx=q.w*q.x,wy=q.w*q.y,wz=q.w*q.z;
    r[0]=1-2*(yy+zz);r[1]=2*(xy-wz);r[2]=2*(xz+wy);
    r[3]=2*(xy+wz);r[4]=1-2*(xx+zz);r[5]=2*(yz-wx);
    r[6]=2*(xz-wy);r[7]=2*(yz+wx);r[8]=1-2*(xx+yy);
}
inline Quat Slerp(Quat a,Quat b,float t){
    float d=a.w*b.w+a.x*b.x+a.y*b.y+a.z*b.z;if(d<0){d=-d;b={-b.w,-b.x,-b.y,-b.z};}
    float wa=1-t,wb=t;
    if(d<0.9995f){const float angle=std::acos(d),s=std::sin(angle);wa=std::sin((1-t)*angle)/s;wb=std::sin(t*angle)/s;}
    Quat q{wa*a.w+wb*b.w,wa*a.x+wb*b.x,wa*a.y+wb*b.y,wa*a.z+wb*b.z};
    const float n=std::sqrt(q.w*q.w+q.x*q.x+q.y*q.y+q.z*q.z);q.w/=n;q.x/=n;q.y/=n;q.z/=n;return q;
}
inline bool SplitMatrix(const Matrix3x4& m,Split& out){
    using re3model::Vec3;
    const Vec3 c0{m.m[0],m.m[4],m.m[8]},c1{m.m[1],m.m[5],m.m[9]},c2{m.m[2],m.m[6],m.m[10]};
    const float n0=std::sqrt(re3model::Dot(c0,c0));if(!(n0>1.e-6f))return false;
    const Vec3 e0=c0*(1.f/n0);Vec3 u1=c1-e0*re3model::Dot(c1,e0);const float n1=std::sqrt(re3model::Dot(u1,u1));if(!(n1>1.e-6f))return false;
    const Vec3 e1=u1*(1.f/n1),e2=re3model::Cross(e0,e1);
    if(re3model::Dot(e2,c2)<=0)return false; // reflection or degenerate: no interpolation
    const float r[9]={e0.x,e1.x,e2.x,e0.y,e1.y,e2.y,e0.z,e1.z,e2.z};
    out.q=FromRotation(r);
    const Vec3 e[3]={e0,e1,e2},c[3]={c0,c1,c2};
    for(unsigned i=0;i<3;++i)for(unsigned j=0;j<3;++j)out.s[i*3+j]=re3model::Dot(e[i],c[j]);
    out.t[0]=m.m[3];out.t[1]=m.m[7];out.t[2]=m.m[11];
    return std::isfinite(out.q.w)&&std::isfinite(out.q.x)&&std::isfinite(out.q.y)&&std::isfinite(out.q.z);
}
inline Matrix3x4 Compose(const Split& a){
    float r[9];ToRotation(a.q,r);Matrix3x4 m;
    for(unsigned i=0;i<3;++i){for(unsigned j=0;j<3;++j)m.m[i*4+j]=r[i*3]*a.s[j]+r[i*3+1]*a.s[3+j]+r[i*3+2]*a.s[6+j];m.m[i*4+3]=a.t[i];}
    return m;
}
inline bool Interpolate(const Matrix3x4& a,const Matrix3x4& b,float t,Matrix3x4& out){
    Split sa,sb;if(!SplitMatrix(a,sa)||!SplitMatrix(b,sb))return false;
    Split r;r.q=Slerp(sa.q,sb.q,t);
    for(unsigned k=0;k<9;++k)r.s[k]=sa.s[k]+(sb.s[k]-sa.s[k])*t;
    for(unsigned k=0;k<3;++k)r.t[k]=sa.t[k]+(sb.t[k]-sa.t[k])*t;
    out=Compose(r);return re3model::Finite(out);
}
inline bool Inverse(const Matrix3x4& a,Matrix3x4& out){
    Matrix3x4 n;if(!re3model::NormalMatrix(a,n))return false; // inverse-transpose of the 3x3
    for(unsigned i=0;i<3;++i)for(unsigned j=0;j<3;++j)out.m[i*4+j]=n.m[j*4+i];
    for(unsigned i=0;i<3;++i)out.m[i*4+3]=-(out.m[i*4]*a.m[3]+out.m[i*4+1]*a.m[7]+out.m[i*4+2]*a.m[11]);
    return re3model::Finite(out);
}
// Root (bone 0) in world space, the other bones relative to the root, so a turning body keeps its limbs.
inline bool BlendPose(const Pose& a,const Pose& b,float t,Pose& out){
    Matrix3x4 ia,ib;if(!Inverse(a[0],ia)||!Inverse(b[0],ib))return false;
    Pose result;if(!Interpolate(a[0],b[0],t,result[0]))return false;
    for(unsigned i=1;i<kBones;++i){
        Matrix3x4 local;
        if(!Interpolate(re3model::Multiply(ia,a[i]),re3model::Multiply(ib,b[i]),t,local))return false;
        result[i]=re3model::Multiply(result[0],local);
    }
    out=result;return true;
}
inline bool SamePose(const Pose& a,const Pose& b){
    for(unsigned i=0;i<kBones;++i)for(unsigned k=0;k<12;++k)if(a[i].m[k]!=b[i].m[k])return false;
    return true;
}
// Too far to be one animation step (teleport, room change, camera-relative snap): start over.
inline bool Discontinuous(const Pose& a,const Pose& b){
    for(unsigned i=0;i<kBones;++i){
        for(unsigned k=0;k<12;++k){const float d=std::abs(a[i].m[k]-b[i].m[k]);if((k%4==3)?d>600.f:d>1.f)return true;}
    }
    return false;
}

// Native world translations are whole units (~0.56 mm): a child joint = parent + parent * offset, rounded.
// The rounding makes every limb jump by up to a unit from frame to frame (std ~0.3 u on the bone offsets)
// and the breathing root steps by whole units. In the original 320x240 frame that was sub-pixel; at
// 2560x1920 in a cutscene close-up one unit is ~3 pixels, which reads as trembling while she breathes.
// JointSmoother keeps every native rotation as it is and rebuilds child translations in floating point
// from a running mean of each bone's offset in its parent's frame; whole-unit root steps that follow a
// still phase are spread over that phase (2..8 frames). Walking, turning and every larger motion pass.
constexpr int kParent[kBones]={-1,0,0,2,3,0,5,6,0,8,9,10,8,12,13}; // native skeleton (Jill and Dario)
class JointSmoother {
    bool have_=false;
    std::uint32_t lastSerial_=0,rootKey_=0;
    unsigned rootDuration_=1;
    re3model::Vec3 offset_[kBones]{},rawRoot_{},fromRoot_{},toRoot_{},shownRoot_{};
    Pose shown_{};
public:
    unsigned frames=0,rootSmoothed=0,offsetResets=0;
    void Reset(){have_=false;}
    const Pose& Process(const Pose& in,std::uint32_t serial){
        using re3model::Vec3;
        if(have_&&serial==lastSerial_)return shown_;
        const bool fresh=!have_||serial-lastSerial_>3;
        ++frames;
        Vec3 measured[kBones]{};
        for(unsigned b=1;b<kBones;++b){
            Matrix3x4 inv;if(!Inverse(in[kParent[b]],inv)){have_=false;shown_=in;return shown_;}
            measured[b]=re3model::TransformPoint(inv,{in[b].m[3],in[b].m[7],in[b].m[11]});
        }
        for(unsigned b=1;b<kBones;++b){
            const Vec3 d=measured[b]-offset_[b];
            if(fresh||re3model::Dot(d,d)>9.f){offset_[b]=measured[b];if(!fresh)++offsetResets;}
            else offset_[b]=offset_[b]+d*0.1f;
        }
        const Vec3 root{in[0].m[3],in[0].m[7],in[0].m[11]};
        if(fresh){rawRoot_=fromRoot_=toRoot_=shownRoot_=root;rootKey_=serial;rootDuration_=1;}
        else if(root.x!=rawRoot_.x||root.y!=rawRoot_.y||root.z!=rawRoot_.z){
            const unsigned interval=serial-rootKey_;const Vec3 jump=root-shownRoot_;
            rootDuration_=(interval>=2&&interval<=8&&re3model::Dot(jump,jump)<=6.25f)?interval:1;
            fromRoot_=shownRoot_;toRoot_=root;rootKey_=serial;
        }
        rawRoot_=root;
        const unsigned elapsed=serial-rootKey_+1;
        if(elapsed>=rootDuration_)shownRoot_=toRoot_;
        else{shownRoot_=fromRoot_+(toRoot_-fromRoot_)*(float(elapsed)/float(rootDuration_));++rootSmoothed;}
        Pose out=in;
        out[0].m[3]=shownRoot_.x;out[0].m[7]=shownRoot_.y;out[0].m[11]=shownRoot_.z;
        for(unsigned b=1;b<kBones;++b){
            const Matrix3x4& p=out[kParent[b]];
            const Vec3 t=re3model::TransformPoint(p,offset_[b]);
            out[b].m[3]=t.x;out[b].m[7]=t.y;out[b].m[11]=t.z;
        }
        for(const auto& m:out)if(!re3model::Finite(m)){have_=false;shown_=in;return shown_;}
        have_=true;lastSerial_=serial;shown_=out;return shown_;
    }
};

struct Stats {unsigned frames=0,keys=0,heldFrames=0,smoothed=0,resets=0,maxHold=0;};

class Smoother {
    bool have_=false;
    std::uint32_t lastSerial_=0,keySerial_=0;
    unsigned duration_=1;
    Pose raw_{},from_{},to_{},shown_{};
public:
    Stats stats;
    void Reset(){have_=false;}
    // serial: native build counter (one per rendered game frame).
    const Pose& Process(const Pose& raw,std::uint32_t serial){
        if(have_&&serial==lastSerial_)return shown_;
        ++stats.frames;
        if(!have_||serial-lastSerial_>3||Discontinuous(raw_,raw)){
            have_=true;raw_=from_=to_=shown_=raw;keySerial_=lastSerial_=serial;duration_=1;++stats.resets;return shown_;
        }
        if(!SamePose(raw_,raw)){
            const unsigned interval=serial-keySerial_;
            ++stats.keys;if(interval>stats.maxHold)stats.maxHold=interval;
            duration_=(interval>=2&&interval<=4)?interval:1;
            from_=shown_;to_=raw;keySerial_=serial;
        }else ++stats.heldFrames;
        raw_=raw;lastSerial_=serial;
        const unsigned elapsed=serial-keySerial_+1;
        if(elapsed>=duration_)shown_=to_;
        else if(BlendPose(from_,to_,float(elapsed)/float(duration_),shown_))++stats.smoothed;
        else shown_=to_;
        return shown_;
    }
};
} // namespace re3motion
