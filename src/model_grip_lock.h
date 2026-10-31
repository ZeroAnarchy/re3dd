#pragma once
// v10 (user QA 2026-10-22): Jill's fingers of both hands crossed when she aimed the M92F up or down; straight aim
// and the magnum were fine. The M92F/SIGPRO upper-body bank (PL00W02/W03 sections 5/6, 9 bones) aims straight at
// frame 10, up at frame 30 and down at frame 50; up and down bring the left hand ~65 units closer to the right
// (hand bone distance 136 -> 44..104), so the HD fingers interpenetrate. Here the left hand keeps the straight-aim
// grip relative to the right hand (kStraightGrip = inverse(hand 4) * hand 7 of that frame) and the left arm reaches
// it with a two-bone IK: upper arm 5 and forearm 6 rotate rigidly about the shoulder and the elbow, the elbow stays
// in its plane, so the wrist does not stretch. Gates (models/characters/v10-tools/hand_fix.py emulates them on every
// keyframe): hands closer than 140 (the straight grips are 136 and 159 apart: unchanged), relative pose within 55 deg
// and 125 units of the straight grip and the left hand not below/behind the right one (the reload frames, where the
// left hand fetches and inserts the magazine, stay native).
#include "model_runtime.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace re3grip {
using namespace re3model;

// row-major 3x4, native units (hand_fix.py --emit)
inline const Matrix3x4& StraightGrip(){
    static const Matrix3x4 t{{0.792035f,0.255464f,-0.554454f,-49.7849f, -0.039354f,0.927709f,0.371224f,57.574041f, 0.609206f,-0.272202f,0.744832f,112.579432f}};
    return t;
}
inline Vec3 Origin(const Matrix3x4& m){return {m.m[3],m.m[7],m.m[11]};}
inline float Length(Vec3 v){return std::sqrt(Dot(v,v));}
inline float Clamp01(float v){return v<0?0:v>1?1:v;}
// rotation (row-major 3x3) taking direction u onto direction v (Rodrigues)
inline void RotationBetween(Vec3 u,Vec3 v,float r[9]){
    u=Normalize(u);v=Normalize(v);const Vec3 c=Cross(u,v);const float s=Length(c),d=Dot(u,v);
    for(int i=0;i<9;++i)r[i]=(i%4==0)?1.f:0.f;
    if(s<1.e-7f)return;
    const Vec3 k=c*(1.f/s);const float a=std::atan2(s,d),sa=std::sin(a),ca=1-std::cos(a);
    const float K[9]={0,-k.z,k.y, k.z,0,-k.x, -k.y,k.x,0};
    float K2[9];for(int i=0;i<3;++i)for(int j=0;j<3;++j){float t=0;for(int q=0;q<3;++q)t+=K[i*3+q]*K[q*3+j];K2[i*3+j]=t;}
    for(int i=0;i<9;++i)r[i]+=sa*K[i]+ca*K2[i];
}
inline void RotateLinear(const float r[9],Matrix3x4& m){   // m.linear = r * m.linear (translation kept)
    float l[9];for(int i=0;i<3;++i)for(int j=0;j<3;++j)l[i*3+j]=m.m[i*4+j];
    for(int i=0;i<3;++i)for(int j=0;j<3;++j){float t=0;for(int q=0;q<3;++q)t+=r[i*3+q]*l[q*3+j];m.m[i*4+j]=t;}
}
inline Vec3 Apply(const float r[9],Vec3 v){return {r[0]*v.x+r[1]*v.y+r[2]*v.z,r[3]*v.x+r[4]*v.y+r[5]*v.z,r[6]*v.x+r[7]*v.y+r[8]*v.z};}

// Returns the blend factor used (0 = untouched).
inline float LockLeftGrip(std::vector<Matrix3x4>& b,uint8_t weapon){
    if(b.size()<8||!(weapon==0x02||weapon==0x03||weapon==0x11||weapon==0x12))return 0;
    const float d=Length(Origin(b[7])-Origin(b[4]));
    if(!(d<140.f))return 0;
    Matrix3x4 inv4,invT;if(!InverseAffine(b[4],inv4)||!InverseAffine(StraightGrip(),invT))return 0;
    const Matrix3x4 rel=Multiply(inv4,b[7]);const Matrix3x4 D=Multiply(invT,rel);
    // relative rotation angle (columns normalised: native matrices may carry a uniform scale) and translation
    float tr=0;for(int i=0;i<3;++i){const float n=std::sqrt(D.m[0+i]*D.m[0+i]+D.m[4+i]*D.m[4+i]+D.m[8+i]*D.m[8+i]);tr+=n>1e-6f?D.m[i*4+i]/n:0;}
    const float angle=std::acos((std::max)(-1.f,(std::min)(1.f,(tr-1)*0.5f)))*57.2957795f;
    const float shift=Length(Origin(D));
    const float f=Clamp01((140.f-d)/20.f)*Clamp01((55.f-angle)/10.f)*Clamp01((125.f-shift)/8.f)*Clamp01((rel.m[7]+50.f)/12.f)*Clamp01((rel.m[3]+66.f)/10.f);
    if(f<=0)return 0;
    const Matrix3x4 target=Multiply(b[4],StraightGrip());
    // blended wrist pose: position lerp, rotation lerp re-orthonormalised (Gram-Schmidt) at the current scale
    const Vec3 pw=Origin(b[7]);const Vec3 goal=pw*(1-f)+Origin(target)*f;
    Vec3 c0{b[7].m[0]*(1-f)+target.m[0]*f,b[7].m[4]*(1-f)+target.m[4]*f,b[7].m[8]*(1-f)+target.m[8]*f};
    Vec3 c1{b[7].m[1]*(1-f)+target.m[1]*f,b[7].m[5]*(1-f)+target.m[5]*f,b[7].m[9]*(1-f)+target.m[9]*f};
    const float scale=Length(Vec3{b[7].m[0],b[7].m[4],b[7].m[8]});
    c0=Normalize(c0);c1=Normalize(c1-c0*Dot(c1,c0));const Vec3 c2=Cross(c0,c1);
    // two-bone IK: shoulder 5, elbow 6, wrist 7
    const Vec3 ps=Origin(b[5]),pe=Origin(b[6]);
    const float la=Length(pe-ps),lb=Length(pw-pe);
    if(la<1e-3f||lb<1e-3f)return 0;
    const Vec3 dv=goal-ps;const float dlen=(std::max)(std::fabs(la-lb)+1e-3f,(std::min)(la+lb-1e-3f,Length(dv)));
    const Vec3 dn=Normalize(dv);
    Vec3 pole=(pe-ps)-dn*Dot(pe-ps,dn);if(Length(pole)<1e-4f)return 0;pole=Normalize(pole);
    const float ca=(la*la+dlen*dlen-lb*lb)/(2*la*dlen),sa=std::sqrt((std::max)(0.f,1-ca*ca));
    const Vec3 pe2=ps+dn*(la*ca)+pole*(la*sa);
    float r1[9];RotationBetween(pe-ps,pe2-ps,r1);
    RotateLinear(r1,b[5]);RotateLinear(r1,b[6]);b[6].m[3]=pe2.x;b[6].m[7]=pe2.y;b[6].m[11]=pe2.z;
    const Vec3 pw1=pe2+Apply(r1,pw-pe);const Vec3 wrist=ps+dn*dlen;
    float r2[9];RotationBetween(pw1-pe2,wrist-pe2,r2);RotateLinear(r2,b[6]);
    for(int i=0;i<3;++i){b[7].m[i*4+0]=(&c0.x)[i]*scale;b[7].m[i*4+1]=(&c1.x)[i]*scale;b[7].m[i*4+2]=(&c2.x)[i]*scale;}
    b[7].m[3]=wrist.x;b[7].m[7]=wrist.y;b[7].m[11]=wrist.z;
    return f;
}

// Fix 2026-10-24 (user asked for it explicitly: Carlos's left hand does not reach the M4A1 foregrip). The HD Carlos
// hand is ~90 units shorter than his native one, so in the native support frames (PL08W0E aim/fire/idle, EM5C) the
// foregrip sits at his fingertips. The left wrist is moved by palm->foregrip (palm: left-hand local, foregrip: weapon
// / hand-4 local, weapons.txt support row) with its rotation kept, the arm reaching it by the same two-bone IK.
// Gate on that distance: support frames 92..130, reload and off-gun frames >= 260 (weapons qa, gate study).
inline float ReachForegrip(std::vector<Matrix3x4>& b,Vec3 foregrip,Vec3 palm){
    if(b.size()<8)return 0;
    const Vec3 e=TransformPoint(b[4],foregrip)-TransformPoint(b[7],palm);
    const float f=Clamp01((175.f-Length(e))/40.f);if(f<=0)return 0;
    const Vec3 ps=Origin(b[5]),pe=Origin(b[6]),pw=Origin(b[7]),goal=pw+e*f;
    const float la=Length(pe-ps),lb=Length(pw-pe);if(la<1e-3f||lb<1e-3f)return 0;
    const Vec3 dv=goal-ps;const float dlen=(std::max)(std::fabs(la-lb)+1e-3f,(std::min)(la+lb-1e-3f,Length(dv)));
    const Vec3 dn=Normalize(dv);Vec3 pole=(pe-ps)-dn*Dot(pe-ps,dn);if(Length(pole)<1e-4f)return 0;pole=Normalize(pole);
    const float ca=(la*la+dlen*dlen-lb*lb)/(2*la*dlen),sa=std::sqrt((std::max)(0.f,1-ca*ca));
    const Vec3 pe2=ps+dn*(la*ca)+pole*(la*sa);
    float r1[9];RotationBetween(pe-ps,pe2-ps,r1);
    RotateLinear(r1,b[5]);RotateLinear(r1,b[6]);b[6].m[3]=pe2.x;b[6].m[7]=pe2.y;b[6].m[11]=pe2.z;
    const Vec3 pw1=pe2+Apply(r1,pw-pe),wrist=ps+dn*dlen;
    float r2[9];RotationBetween(pw1-pe2,wrist-pe2,r2);RotateLinear(r2,b[6]);
    b[7].m[3]=wrist.x;b[7].m[7]=wrist.y;b[7].m[11]=wrist.z;
    return f;
}
}
