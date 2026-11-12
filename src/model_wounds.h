#pragma once

// v12 (user QA 2026-10-22): wounds on the HD enemies where the player's shots hit them (an experiment), and the red
// of a heavily hit part. Included by model_integration.h before the per-actor state; no native calls here.
//
// Hits: the player's shot test 4553A0 marks every enemy it hits in the hit-record list (A61D14, count A61D3C):
// record +0 = 0xFF, +2 the weapon, +4 the actor. model_integration.h wraps its player callers and queues the hit with
// the hand matrix of that shot; the next HD preparation of that actor puts a wound where the barrel line (hand +Y)
// meets the skinned body.
// Red: enemy damage code sets render attribute 4 on the hit parts with the colour 0x202030 at part+0x78 (zombies;
// R in the low byte), and the part draw 435A90 averages the part's vertex colours with it (4339E0) - a darker,
// slightly red part. The HD body gets a per-vertex multiplier from its skin weights on those parts, redder than the
// native average so the blood reads on HD textures.
namespace re3wound {
using re3model::Vec3;
// fx 2026-11-11e: the user's blood brightness (pilot.ini blood_pool_brightness 40..160 % / F5 "Brightness of blood"): the wound
// decals are drawn this much brighter / darker (blood_fx.h does the same with the pools, stains, sprays and drops)
static float bloodBrightness=1.f;
// A wound is a patch of the HD body's own triangles around the hit with decal texture coordinates fixed at the hit:
// the body's skinning carries it and nothing is projected again later.
struct Wound{
    std::vector<std::uint32_t> corners;   // body vertex of each patch corner
    std::vector<float> uv;                // decal coordinates per corner
    std::vector<std::uint32_t> triangles; // three corner indices per triangle
    std::vector<std::uint16_t> material;  // body material of each triangle (a hidden head hides its wounds)
    std::uint8_t texture=0;
    // 03.11b (wounds on kit parts): the pack the corners belong to; empty = the actor's body (all wounds before 03.11b)
    std::string mesh;
};
struct PendingHit{void* actor=nullptr;std::uint8_t weapon=0;re3model::Matrix3x4 hand;std::uint32_t serial=0;};
static std::vector<PendingHit> pendingHits;
// blood round 2026-11-07 (user: "wounds sometimes barely show"): 24 per actor (was 12), larger decals, an exit wound on
// the far side of the body for rifle/magnum/most handgun hits, and no more MODEL_WOUND_MISS (31 % of the hits of the
// user's 06.11 run): a hit the native test registered always gets a wound.
constexpr std::size_t kMaxWounds=24;
// wound-decals v2 (gpt-image-2.6): 0 entry, 1 torn through cloth, 2 entry with run-off streams (+V = down the body),
// 3 shotgun pellets, 4 exit wound
constexpr unsigned kTextures=5;
enum WoundTexture : std::uint8_t {WoundEntry=0,WoundTorn=1,WoundRun=2,WoundShotgun=3,WoundExit=4};
// Release 1.50 (user 12.11: "spiders, Drain Deimos, Brain Suckers and worms have green pools and sprays in the original; give
// them the same HD blood as the zombies, coloured green, with the same brightness and transparency settings"). The hue of
// the HD blood (wound decals, sprays, drops, floor splats, pools): the red art drawn as is, or turned into the native colour
// of the creature's blood by the shaders (model_shader.h / model_gpu.h BloodHue) - nothing new is loaded.
//  - green: the native palette row 1 of the blot set (~22,43,11) and the stains EF409F (Drain Deimos / Brain Sucker 23 28,
//    baby spider 26), BF10BF and BF407F (giant spider 25); the stain colour is subtracted from the floor (tpage D9), green
//    stays;
//  - olive: the Grave Digger and the worms (30 33 32): hit blood = palette row 7 (~61,61,11), stain BF3030 (blue taken away).
enum BloodHue : std::uint8_t {HueRed=0,HueGreen=1,HueOlive=2,kBloodHues=3};
inline std::uint8_t bloodHueOf(std::uint8_t model){
    switch(model){case 0x23:case 0x25:case 0x26:case 0x28:return HueGreen;case 0x30:case 0x32:case 0x33:return HueOlive;default:return HueRed;}
}
// A native shadow's colour (+0x1C, low byte = red): -1 the plain shadow of a living actor (808080 and other greys), else the
// stain a death routine made of it: 0 red (808010, the zombies and most enemies), 1 green, 2 olive, 3 the downed Nemesis'
// purple puddle (10AF10, fx_nemesis_cloud.h).
inline int stainKind(std::uint32_t colour){
    switch(colour&0xffffffu){case 0x808010u:return HueRed;case 0xef409fu:case 0xbf10bfu:case 0xbf407fu:return HueGreen;case 0xbf3030u:return HueOlive;case 0x10af10u:return 3;default:return -1;}
}
// One shot's result for the blood effects (blood_fx.h): where it went in and, when it left the body, where it came out.
struct ShotBlood{void* actor=nullptr;Vec3 entry{},direction{},normal{},exit{};bool hasExit=false;std::uint8_t weapon=0;std::uint32_t serial=0;std::uint32_t colour=0xffffffffu;float floorY=0;std::uint8_t hue=HueRed;};
static std::vector<ShotBlood> shotBlood;
inline bool isShotgun(std::uint8_t weapon){return weapon==4||weapon==0x10||weapon==0x13;}

inline float random(std::uint32_t& state){state=state*1664525u+1013904223u;return float((state>>8)&0xffff)/65535.f;}
inline std::uint32_t triangleMaterial(const re3model::Mesh& mesh,std::uint32_t triangle){
    for(std::uint32_t m=0;m<mesh.materials.size();++m){const auto& material=mesh.materials[m];
        if(triangle*3>=material.firstIndex&&triangle*3<material.firstIndex+material.indexCount)return m;}
    return 0;
}
inline bool visible(const std::vector<std::uint8_t>* hidden,std::size_t material){return !hidden||material>=hidden->size()||!(*hidden)[material];}
// First body triangle on the shot line (Moller-Trumbore, both sides).
inline bool castRay(const re3model::Mesh& mesh,const std::vector<re3model::SkinnedVertex>& body,const std::vector<std::uint8_t>* hidden,Vec3 origin,Vec3 direction,float& hitT,std::uint32_t& hitTriangle){
    hitT=1.e30f;bool found=false;
    for(std::uint32_t m=0;m<mesh.materials.size();++m){if(!visible(hidden,m))continue;const auto& material=mesh.materials[m];
        for(std::uint32_t index=material.firstIndex;index<material.firstIndex+material.indexCount;index+=3){
            const Vec3 a=body[mesh.indices[index]].position,b=body[mesh.indices[index+1]].position,c=body[mesh.indices[index+2]].position;
            const Vec3 e1=b-a,e2=c-a,p=re3model::Cross(direction,e2);const float det=re3model::Dot(e1,p);
            if(std::abs(det)<1.e-6f)continue;const float inverse=1.f/det;
            const Vec3 s=origin-a;const float u=re3model::Dot(s,p)*inverse;if(u<0||u>1)continue;
            const Vec3 q=re3model::Cross(s,e1);const float v=re3model::Dot(direction,q)*inverse;if(v<0||u+v>1)continue;
            const float t=re3model::Dot(e2,q)*inverse;
            if(t>1&&t<hitT){hitT=t;hitTriangle=index/3;found=true;}
        }
    }
    return found;
}
// Off the line (the native aim is a cone): the body vertex facing the shooter closest to the line, within 400 units.
inline bool nearestToRay(const re3model::Mesh& mesh,const std::vector<re3model::SkinnedVertex>& body,const std::vector<std::uint8_t>* hidden,Vec3 origin,Vec3 direction,Vec3& hit,Vec3& normal){
    std::vector<std::uint8_t> shown(body.size(),0);
    for(std::uint32_t m=0;m<mesh.materials.size();++m)if(visible(hidden,m)){const auto& material=mesh.materials[m];
        for(std::uint32_t i=material.firstIndex;i<material.firstIndex+material.indexCount;++i)shown[mesh.indices[i]]=1;}
    float best=400.f*400.f;bool found=false;
    for(std::size_t i=0;i<body.size();++i){if(!shown[i])continue;
        const Vec3 d=body[i].position-origin;const float t=re3model::Dot(d,direction);if(t<=0)continue;
        const Vec3 n=re3model::Normalize(body[i].normal);if(re3model::Dot(n,direction)>-0.1f)continue;
        const Vec3 off=d-direction*t;const float distance=re3model::Dot(off,off);
        if(distance<best){best=distance;hit=body[i].position;normal=n;found=true;}
    }
    return found;
}
inline bool makeWound(const re3model::Mesh& mesh,const std::vector<re3model::SkinnedVertex>& body,const std::vector<std::uint8_t>* hidden,Vec3 hit,Vec3 normal,float radius,float angle,std::uint8_t texture,Wound& out){
    out=Wound{};out.texture=texture;
    Vec3 tangent=re3model::Normalize(re3model::Cross(normal,std::abs(normal.y)<0.9f?Vec3{0,1,0}:Vec3{1,0,0}));
    Vec3 bitangent=re3model::Cross(normal,tangent);
    const float c=std::cos(angle),s=std::sin(angle);
    const Vec3 t=tangent*c+bitangent*s,b=bitangent*c-tangent*s;
    std::unordered_map<std::uint32_t,std::uint32_t> corner;
    const float reach=radius*1.6f;
    for(std::uint32_t m=0;m<mesh.materials.size();++m){if(!visible(hidden,m))continue;const auto& material=mesh.materials[m];
        for(std::uint32_t index=material.firstIndex;index<material.firstIndex+material.indexCount;index+=3){
            const std::uint32_t v[3]={mesh.indices[index],mesh.indices[index+1],mesh.indices[index+2]};
            const Vec3 p[3]={body[v[0]].position,body[v[1]].position,body[v[2]].position};
            bool close=false,inSlab=false;
            for(const auto& point:p){const Vec3 d=point-hit;close=close||re3model::Dot(d,d)<reach*reach;inSlab=inSlab||std::abs(re3model::Dot(d,normal))<radius;}
            if(!close||!inSlab)continue;
            // surfaces across the shot only (either winding: some cloth layers face inward); the slab keeps the far
            // side of a limb out
            Vec3 face=re3model::Cross(p[1]-p[0],p[2]-p[0]);if(re3model::Dot(face,face)<1.e-12f)continue;face=re3model::Normalize(face);
            if(std::abs(re3model::Dot(face,normal))<0.25f)continue;
            float u[3],w[3];
            for(unsigned k=0;k<3;++k){const Vec3 d=p[k]-hit;u[k]=re3model::Dot(d,t)/(2*radius)+0.5f;w[k]=re3model::Dot(d,b)/(2*radius)+0.5f;}
            if((u[0]<0&&u[1]<0&&u[2]<0)||(u[0]>1&&u[1]>1&&u[2]>1)||(w[0]<0&&w[1]<0&&w[2]<0)||(w[0]>1&&w[1]>1&&w[2]>1))continue;
            for(unsigned k=0;k<3;++k){
                const auto found=corner.find(v[k]);std::uint32_t id;
                if(found!=corner.end())id=found->second;
                else{id=std::uint32_t(out.corners.size());corner.emplace(v[k],id);out.corners.push_back(v[k]);out.uv.push_back(u[k]);out.uv.push_back(w[k]);}
                out.triangles.push_back(id);
            }
            out.material.push_back(std::uint16_t(m));
            if(out.material.size()>=1500)return true; // dense cloth (torn jeans) still gets a whole wound
        }
    }
    return !out.material.empty();
}
// 03.11b: a zombie of the kit is its body (the regions it still shows) plus up to three part packs with their own
// vertices, triangles and UVs; the shot goes into all of them as they are posed now. A plain body is one target.
struct ShotTarget{const re3model::Mesh* mesh=nullptr;const std::vector<re3model::SkinnedVertex>* skinned=nullptr;const std::vector<std::uint8_t>* hidden=nullptr;};
// The vertex closest to the line with the best squared distance carried across meshes; facing = only vertices whose
// normal faces the shooter (the normal returned always faces the shooter).
inline bool nearestToRayBest(const re3model::Mesh& mesh,const std::vector<re3model::SkinnedVertex>& body,const std::vector<std::uint8_t>* hidden,Vec3 origin,Vec3 direction,float& best,Vec3& hit,Vec3& normal,bool facing=true){
    std::vector<std::uint8_t> shown(body.size(),0);
    for(std::uint32_t m=0;m<mesh.materials.size();++m)if(visible(hidden,m)){const auto& material=mesh.materials[m];
        for(std::uint32_t i=material.firstIndex;i<material.firstIndex+material.indexCount;++i)shown[mesh.indices[i]]=1;}
    bool found=false;
    for(std::size_t i=0;i<body.size();++i){if(!shown[i])continue;
        const Vec3 d=body[i].position-origin;const float t=re3model::Dot(d,direction);if(t<=0)continue;
        Vec3 n=re3model::Normalize(body[i].normal);if(facing&&re3model::Dot(n,direction)>-0.1f)continue;
        if(re3model::Dot(n,direction)>0)n=n*-1.f;
        const Vec3 off=d-direction*t;const float distance=re3model::Dot(off,off);
        if(distance<best){best=distance;hit=body[i].position;normal=n;found=true;}
    }
    return found;
}
// Last surface of the targets on the line within `reach` past the entry: where the bullet leaves the body. The normal
// returned points along the shot (out of the body on the far side).
inline int farHit(const ShotTarget* targets,std::size_t count,Vec3 origin,Vec3 direction,float entryT,float reach,Vec3& where,Vec3& normal){
    int chosen=-1;float bestT=entryT+60.f;
    for(std::size_t k=0;k<count;++k){const auto& target=targets[k];
        if(!target.mesh||!target.skinned||target.skinned->size()!=target.mesh->vertices.size())continue;
        const auto& mesh=*target.mesh;const auto& body=*target.skinned;
        for(std::uint32_t m=0;m<mesh.materials.size();++m){if(!visible(target.hidden,m))continue;const auto& material=mesh.materials[m];
            for(std::uint32_t index=material.firstIndex;index<material.firstIndex+material.indexCount;index+=3){
                const Vec3 a=body[mesh.indices[index]].position,b=body[mesh.indices[index+1]].position,c=body[mesh.indices[index+2]].position;
                const Vec3 e1=b-a,e2=c-a,p=re3model::Cross(direction,e2);const float det=re3model::Dot(e1,p);
                if(std::abs(det)<1.e-6f)continue;const float inverse=1.f/det;
                const Vec3 s=origin-a;const float u=re3model::Dot(s,p)*inverse;if(u<0||u>1)continue;
                const Vec3 q=re3model::Cross(s,e1);const float v=re3model::Dot(direction,q)*inverse;if(v<0||u+v>1)continue;
                const float t=re3model::Dot(e2,q)*inverse;
                if(t>bestT&&t<entryT+reach){bestT=t;chosen=int(k);normal=re3model::Normalize(re3model::Cross(e1,e2));}
            }
        }
    }
    if(chosen>=0){where=origin+direction*bestT;if(re3model::Dot(normal,direction)<0)normal=normal*-1.f;}
    return chosen;
}
inline float woundAngle(std::uint8_t texture,Vec3 normal,std::uint32_t& seed){
    float angle=random(seed)*6.2831853f;
    if(texture==WoundRun){
        // the run-off of the run texture (+V) points down the body (native world +Y)
        const Vec3 tangent=re3model::Normalize(re3model::Cross(normal,std::abs(normal.y)<0.9f?Vec3{0,1,0}:Vec3{1,0,0})),bitangent=re3model::Cross(normal,tangent);
        const Vec3 down{0,1,0};const float c=re3model::Dot(down,bitangent),s=-re3model::Dot(down,tangent);
        if(c*c+s*s>1.e-4f)angle=std::atan2(s,c)+(random(seed)-0.5f)*0.5f;
    }
    return angle;
}
struct ShotWound{int target=-1;Wound wound;Vec3 where{},normal{};bool onLine=false,exit=false;};
// Entry wound (always, when any target has a surface: the native test registered the hit) and, for bullets that pass
// through, the exit wound. Native units (~mm): radius = half the side of the decal square; the v2 decals fill ~96 % of
// it (v12: 75/92/110 with the wound in the middle 60-72 %).
inline std::size_t shotWounds(const ShotTarget* targets,std::size_t count,const re3model::Matrix3x4& hand,std::uint8_t weapon,std::uint32_t seed,ShotWound out[2],Vec3* directionOut=nullptr){
    const Vec3 origin{hand.m[3],hand.m[7],hand.m[11]},direction=re3model::Normalize({hand.m[1],hand.m[5],hand.m[9]});
    if(directionOut)*directionOut=direction;
    int chosen=-1;float bestT=1.e30f;std::uint32_t bestTriangle=0;bool onLine=false;
    for(std::size_t k=0;k<count;++k){const auto& target=targets[k];
        if(!target.mesh||!target.skinned||target.skinned->size()!=target.mesh->vertices.size())continue;
        float t=0;std::uint32_t triangle=0;
        if(castRay(*target.mesh,*target.skinned,target.hidden,origin,direction,t,triangle)&&t<bestT){bestT=t;bestTriangle=triangle;chosen=int(k);onLine=true;}
    }
    Vec3 where,normal;
    if(onLine){const auto& target=targets[chosen];const auto& mesh=*target.mesh;const auto& body=*target.skinned;
        const Vec3 a=body[mesh.indices[bestTriangle*3]].position,b=body[mesh.indices[bestTriangle*3+1]].position,c=body[mesh.indices[bestTriangle*3+2]].position;
        where=origin+direction*bestT;normal=re3model::Normalize(re3model::Cross(b-a,c-a));
        if(re3model::Dot(normal,direction)>0)normal=normal*-1.f;
    }else{
        // the native aim is a cone: the vertex facing the shooter closest to the line (400, then 900 units), else any vertex
        for(const float cone:{400.f,900.f,1.e5f}){float best=cone*cone;
            for(std::size_t k=0;k<count;++k){const auto& target=targets[k];
                if(!target.mesh||!target.skinned||target.skinned->size()!=target.mesh->vertices.size())continue;
                if(nearestToRayBest(*target.mesh,*target.skinned,target.hidden,origin,direction,best,where,normal,cone<1.e4f))chosen=int(k);}
            if(chosen>=0)break;}
        if(chosen<0)return 0;
    }
    const bool shotgun=isShotgun(weapon);
    const float scale=0.88f+0.24f*random(seed);
    const float radius=(shotgun?135.f:weapon==5?118.f:98.f)*scale;
    const std::uint8_t texture=shotgun?std::uint8_t(WoundShotgun):std::uint8_t(random(seed)<0.3f?WoundRun:(random(seed)<0.5f?WoundEntry:WoundTorn));
    std::size_t made=0;
    {const auto& target=targets[chosen];ShotWound& w=out[made];
        if(makeWound(*target.mesh,*target.skinned,target.hidden,where,normal,radius,woundAngle(texture,normal,seed),texture,w.wound)){
            w.target=chosen;w.where=where;w.normal=normal;w.onLine=onLine;++made;}}
    // exit wound: magnum always, rifle/handgun often, never for pellets; only for a real hit on the line
    const float exitChance=shotgun?0.f:weapon==5?1.f:0.65f;
    if(onLine&&random(seed)<exitChance){Vec3 exitAt,exitNormal;
        const int farTarget=farHit(targets,count,origin,direction,bestT,900.f,exitAt,exitNormal);
        if(farTarget>=0){const auto& target=targets[farTarget];ShotWound& w=out[made];
            if(makeWound(*target.mesh,*target.skinned,target.hidden,exitAt,exitNormal,radius*1.15f,random(seed)*6.2831853f,WoundExit,w.wound)){
                w.target=farTarget;w.where=exitAt;w.normal=exitNormal;w.onLine=true;w.exit=true;++made;}}
    }
    return made;
}
// GPU path: the multiplier of each bone; the vertex shader blends it by the skin weights (= damageTint up to rounding).
inline void damageBoneTint(const std::uint32_t partColour[32],std::uint32_t damaged,float out[32][3]){
    for(unsigned i=0;i<32;++i){const std::uint32_t c=partColour[i];const bool red=(damaged>>i)&1u;
        const float base[3]={(128.f+float(c&255))/256.f,(128.f+float((c>>8)&255))/256.f,(128.f+float((c>>16)&255))/256.f};
        out[i][0]=red?(std::min)(1.f,base[0]*1.28f):1.f;out[i][1]=red?base[1]*0.72f:1.f;out[i][2]=red?base[2]*0.68f:1.f;}
}
// Per-vertex multiplier of the red parts: native colour C (part+0x78) gives the native average (128+C)/256, pushed
// toward red for the HD textures; blended by the vertex's skin weights on those parts.
inline void damageTint(const re3model::Mesh& mesh,const std::uint32_t partColour[32],std::uint32_t damaged,std::vector<std::uint32_t>& out){
    out.clear();if(!damaged)return;
    float factor[32][3];
    for(unsigned i=0;i<32;++i){const std::uint32_t c=partColour[i];
        const float base[3]={(128.f+float(c&255))/256.f,(128.f+float((c>>8)&255))/256.f,(128.f+float((c>>16)&255))/256.f};
        factor[i][0]=(std::min)(1.f,base[0]*1.28f);factor[i][1]=base[1]*0.72f;factor[i][2]=base[2]*0.68f;}
    out.resize(mesh.vertices.size());
    for(std::size_t v=0;v<mesh.vertices.size();++v){const auto& vertex=mesh.vertices[v];float rgb[3]={0,0,0},total=0;
        for(unsigned k=0;k<4;++k){if(!vertex.weight[k])continue;const float w=float(vertex.weight[k])/65535.f;total+=w;const unsigned bone=vertex.bone[k];
            const bool red=bone<32&&((damaged>>bone)&1u);for(unsigned c=0;c<3;++c)rgb[c]+=w*(red?factor[bone][c]:1.f);}
        if(total<=0){out[v]=0xffffffffu;continue;}
        std::uint32_t argb=0xff000000u;for(unsigned c=0;c<3;++c)argb|=std::uint32_t((std::min)(255.f,rgb[c]/total*255.f+.5f))<<(16-8*c);
        out[v]=argb;
    }
}
} // namespace re3wound
