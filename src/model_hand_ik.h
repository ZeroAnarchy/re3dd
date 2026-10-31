#pragma once
// Fix 2026-10-25b (user QA; the user asked for these two hand poses explicitly, an exception to "move the weapon, never
// the hands"): Jill's left hand lower on the mine-thrower handle while aiming, her left palm up on the M4A1 handguard
// outside the aim poses. model-pack/hand-ik.txt:
//   R3HANDIK 1
//   pose <models> <weapons> <rest|aim> <rx ry rz> <full> <zero> <dx dy dz> <ax ay az> <px py pz> <fade frames> [lock]
// models/weapons: comma-separated hex ids. rel = inverse(hand 4) * hand 7 (native left wrist in the weapon frame: X up,
// Y muzzle, Z side). A row matches the player's model, weapon and state (aim = native aim mode, player routine bytes
// +4/+5 = 1/5: raise, held aim, fire, reload, lowering; rest = anything else) with weight 1 while rel's origin lies within
// <full> of r, 0 beyond <zero>. Target rel' = T(d) * [rotation vector a (degrees, weapon frame) about the palm point
// q = rel * p, p left-hand local] * rel; the arm reaches it with the two-bone IK of re3grip (shoulder 5, elbow 6, wrist 7
// rotate rigidly, native elbow plane, arm lengths kept), the wrist turned to rel'. The weight rises by at most 1/<fade>
// per frame and fades the same way when the row stops matching; a rest row is 0 at once in aim mode except during the
// raise (routine +6 = 0), so held aim, fire, reload and lowering stay native exactly. Hand 4 (the weapon) never moves.
// Fix 2026-10-25c (grenade launcher, left hand under the barrel): of several rows for one model/weapon/state the one with
// the highest weight is used (ties: the current row, else the first); a row ending in "lock" sets rel' itself (d = wrist
// position, a = wrist rotation vector in the weapon frame; p unused): the aim-up fire clip turns the native hand up to
// 36 deg against the gun. A jump between two lock rows of one state (aim straight -> up) hands the weight over instead
// of fading through the native pose.
#include "model_grip_lock.h"
#include <cstdint>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace re3handik {
using namespace re3model;
struct Row{std::vector<uint8_t> models,weapons;bool aim=false,lock=false;Vec3 ref{},shift{},axis{},palm{};float full=0,zero=1,fade=1;};
struct State{uint32_t serial=0;int row=-1;float shown=0;};

inline bool Ids(const std::string& text,std::vector<uint8_t>& out){
    std::istringstream in(text);std::string id;
    while(std::getline(in,id,',')){char* end=nullptr;const unsigned long v=strtoul(id.c_str(),&end,16);if(id.empty()||*end||v>255)return false;out.push_back(uint8_t(v));}
    return !out.empty();
}
inline bool Load(const std::string& path,std::vector<Row>& rows,unsigned& invalid){
    rows.clear();invalid=0;std::ifstream in(path);if(!in)return false;std::string line;bool header=false;
    while(std::getline(in,line)){
        const auto hash=line.find('#');if(hash!=std::string::npos)line.resize(hash);
        std::istringstream f(line);std::string key;if(!(f>>key))continue;
        if(key=="R3HANDIK"){unsigned version=0;f>>version;header=version==1;continue;}
        Row r;std::string models,weapons,state,extra;float v[15]{};bool ok=header&&key=="pose"&&bool(f>>models>>weapons>>state)&&Ids(models,r.models)&&Ids(weapons,r.weapons)&&(state=="aim"||state=="rest");
        for(unsigned i=0;i<15&&ok;++i)ok=bool(f>>v[i])&&std::isfinite(v[i])&&std::fabs(v[i])<4000.f;
        if(ok&&f>>extra){r.lock=extra=="lock";ok=r.lock&&!(f>>extra);}
        ok=ok&&v[3]>=0&&v[4]>v[3]&&v[14]>=1&&v[14]<=120;
        if(!ok){++invalid;continue;}
        r.aim=state=="aim";r.ref={v[0],v[1],v[2]};r.full=v[3];r.zero=v[4];r.shift={v[5],v[6],v[7]};r.axis={v[8],v[9],v[10]};r.palm={v[11],v[12],v[13]};r.fade=v[14];
        rows.push_back(r);
    }
    return header;
}
inline bool Has(const std::vector<uint8_t>& ids,uint8_t id){for(auto v:ids)if(v==id)return true;return false;}
inline bool Covers(const std::vector<Row>& rows,uint8_t model,uint8_t weapon){for(const auto& r:rows)if(Has(r.models,model)&&Has(r.weapons,weapon))return true;return false;}
// rotation (row-major 3x3) by the rotation vector a in degrees (Rodrigues)
inline void AxisAngle(Vec3 a,float r[9]){
    const float angle=re3grip::Length(a)*0.0174532925f;for(int i=0;i<9;++i)r[i]=(i%4==0)?1.f:0.f;if(angle<1.e-7f)return;
    const Vec3 k=Normalize(a);const float s=std::sin(angle),c=1-std::cos(angle);
    const float K[9]={0,-k.z,k.y, k.z,0,-k.x, -k.y,k.x,0};
    for(int i=0;i<3;++i)for(int j=0;j<3;++j){float t=0;for(int q=0;q<3;++q)t+=K[i*3+q]*K[q*3+j];r[i*3+j]+=s*K[i*3+j]+c*t;}
}
// wrist 7 toward the world pose target by f: position lerp, rotation lerp re-orthonormalised at the native scale, arm by
// the two-bone IK of re3grip::LockLeftGrip
inline bool Reach(std::vector<Matrix3x4>& b,const Matrix3x4& target,float f){
    using namespace re3grip;
    const Vec3 pw=Origin(b[7]),goal=pw*(1-f)+Origin(target)*f;
    Vec3 c0{b[7].m[0]*(1-f)+target.m[0]*f,b[7].m[4]*(1-f)+target.m[4]*f,b[7].m[8]*(1-f)+target.m[8]*f};
    Vec3 c1{b[7].m[1]*(1-f)+target.m[1]*f,b[7].m[5]*(1-f)+target.m[5]*f,b[7].m[9]*(1-f)+target.m[9]*f};
    const float scale=Length(Vec3{b[7].m[0],b[7].m[4],b[7].m[8]});
    if(Length(c0)<1e-6f)return false;c0=Normalize(c0);c1=c1-c0*Dot(c1,c0);if(Length(c1)<1e-6f)return false;c1=Normalize(c1);const Vec3 c2=Cross(c0,c1);
    const Vec3 ps=Origin(b[5]),pe=Origin(b[6]);
    const float la=Length(pe-ps),lb=Length(pw-pe);if(la<1e-3f||lb<1e-3f)return false;
    const Vec3 dv=goal-ps;const float dlen=(std::max)(std::fabs(la-lb)+1e-3f,(std::min)(la+lb-1e-3f,Length(dv)));
    const Vec3 dn=Normalize(dv);Vec3 pole=(pe-ps)-dn*Dot(pe-ps,dn);if(Length(pole)<1e-4f)return false;pole=Normalize(pole);
    const float ca=(la*la+dlen*dlen-lb*lb)/(2*la*dlen),sa=std::sqrt((std::max)(0.f,1-ca*ca));
    const Vec3 pe2=ps+dn*(la*ca)+pole*(la*sa);
    float r1[9];RotationBetween(pe-ps,pe2-ps,r1);
    RotateLinear(r1,b[5]);RotateLinear(r1,b[6]);b[6].m[3]=pe2.x;b[6].m[7]=pe2.y;b[6].m[11]=pe2.z;
    const Vec3 pw1=pe2+re3grip::Apply(r1,pw-pe),wrist=ps+dn*dlen;
    float r2[9];RotationBetween(pw1-pe2,wrist-pe2,r2);RotateLinear(r2,b[6]);
    for(int i=0;i<3;++i){b[7].m[i*4+0]=(&c0.x)[i]*scale;b[7].m[i*4+1]=(&c1.x)[i]*scale;b[7].m[i*4+2]=(&c2.x)[i]*scale;}
    b[7].m[3]=wrist.x;b[7].m[7]=wrist.y;b[7].m[11]=wrist.z;
    return true;
}
// Fix 2026-10-25c follow-up: both arms (2-4 right, 5-7 left) turned down by <degrees> rigidly about the shoulder line, so
// the hands keep their relation to each other and to the gun; rot/pivot receive the world rotation for the effects.
inline bool LowerArms(std::vector<Matrix3x4>& b,float degrees,float rot[9],Vec3& pivot){
    using namespace re3grip;
    const Vec3 ps=Origin(b[2]),pl=Origin(b[5]);if(Length(pl-ps)<1e-3f)return false;const Vec3 axis=Normalize(pl-ps);
    const Vec3 down=Origin(b[0])-(ps+pl)*0.5f,muzzle=Vec3{b[4].m[1],b[4].m[5],b[4].m[9]};   // toward the hips; hand 4 Y = muzzle
    AxisAngle(axis*degrees,rot);if(Dot(re3grip::Apply(rot,muzzle),down)<Dot(muzzle,down))AxisAngle(axis*-degrees,rot);
    pivot=ps;
    for(unsigned i=2;i<8;++i){const Vec3 o=ps+re3grip::Apply(rot,Origin(b[i])-ps);RotateLinear(rot,b[i]);b[i].m[3]=o.x;b[i].m[7]=o.y;b[i].m[11]=o.z;}
    return true;
}
// Returns the weight used this frame (0 = bones untouched); row receives the row index (-1 none).
inline float Apply(const std::vector<Row>& rows,State& s,std::vector<Matrix3x4>& b,uint8_t model,uint8_t weapon,const uint8_t routine[3],uint32_t serial,int& row){
    row=-1;if(b.size()<8||rows.empty())return 0;
    if(s.row>=int(rows.size()))s=State{};
    Matrix3x4 inv4;if(!InverseAffine(b[4],inv4))return 0;
    const Matrix3x4 rel=Multiply(inv4,b[7]);const bool aim=routine[0]==1&&routine[1]==5;
    int match=-1;float weight=0;
    for(size_t i=0;i<rows.size();++i){const Row& r=rows[i];
        if(r.aim!=aim||!Has(r.models,model)||!Has(r.weapons,weapon))continue;
        const float w=re3grip::Clamp01((r.zero-re3grip::Length(re3grip::Origin(rel)-r.ref))/(r.zero-r.full));
        if(match<0||w>weight||(w==weight&&int(i)==s.row)){match=int(i);weight=w;}}   // ties: the current row
    const bool fresh=s.serial==0||serial-s.serial>4;const bool step=serial!=s.serial;s.serial=serial;
    if(fresh){s.row=match;s.shown=weight;}
    else if(step){
        if(s.row!=match){
            if(s.row<0)s.shown=0;
            else if(!rows[s.row].aim&&aim&&routine[2]!=0)s.shown=0;      // rest row: held aim, fire, reload, lowering stay native
            else if(match>=0&&rows[s.row].lock&&rows[match].lock&&rows[s.row].aim==rows[match].aim)s.row=match;   // lock rows hand over
            else s.shown=(std::max)(0.f,s.shown-1.f/rows[s.row].fade);   // fading out after the row stopped matching
            if(s.shown<=0){s.row=match;s.shown=0;}
        }
        if(s.row==match&&match>=0)s.shown=(std::min)(weight,s.shown+1.f/rows[match].fade);
    }
    if(s.row<0||s.shown<=0)return 0;
    const Row& r=rows[s.row];
    // target relative wrist: rotation a about the palm point (weapon frame), then shift d
    const Vec3 q=TransformPoint(rel,r.palm);float rot[9];AxisAngle(r.axis,rot);
    Matrix3x4 t=rel;re3grip::RotateLinear(rot,t);const Vec3 o=r.lock?r.shift:re3grip::Apply(rot,re3grip::Origin(rel)-q)+q+r.shift;
    if(r.lock)for(int i=0;i<3;++i)for(int j=0;j<3;++j)t.m[i*4+j]=rot[i*3+j];   // Reach keeps the native wrist scale
    t.m[3]=o.x;t.m[7]=o.y;t.m[11]=o.z;
    if(!Reach(b,Multiply(b[4],t),s.shown))return 0;
    row=s.row;return s.shown;
}
} // namespace re3handik
