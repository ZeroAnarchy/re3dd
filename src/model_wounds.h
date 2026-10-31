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
// A wound is a patch of the HD body's own triangles around the hit with decal texture coordinates fixed at the hit:
// the body's skinning carries it and nothing is projected again later.
struct Wound{
    std::vector<std::uint32_t> corners;   // body vertex of each patch corner
    std::vector<float> uv;                // decal coordinates per corner
    std::vector<std::uint32_t> triangles; // three corner indices per triangle
    std::vector<std::uint16_t> material;  // body material of each triangle (a hidden head hides its wounds)
    std::uint8_t texture=0;
};
struct PendingHit{void* actor=nullptr;std::uint8_t weapon=0;re3model::Matrix3x4 hand;std::uint32_t serial=0;};
static std::vector<PendingHit> pendingHits;
constexpr std::size_t kMaxWounds=12;
constexpr unsigned kTextures=4; // wound-decals: three bullet wounds, one shotgun wound

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
// Shot from the hand matrix of the shot (Y = barrel) into the skinned body: the wound and where it went.
inline bool woundFromShot(const re3model::Mesh& mesh,const std::vector<re3model::SkinnedVertex>& body,const std::vector<std::uint8_t>* hidden,const re3model::Matrix3x4& hand,std::uint8_t weapon,std::uint32_t seed,Wound& wound,Vec3& where,bool& onLine){
    if(body.size()!=mesh.vertices.size())return false;
    const Vec3 origin{hand.m[3],hand.m[7],hand.m[11]},direction=re3model::Normalize({hand.m[1],hand.m[5],hand.m[9]});
    float t=0;std::uint32_t triangle=0;Vec3 normal;
    onLine=castRay(mesh,body,hidden,origin,direction,t,triangle);
    if(onLine){
        const Vec3 a=body[mesh.indices[triangle*3]].position,b=body[mesh.indices[triangle*3+1]].position,c=body[mesh.indices[triangle*3+2]].position;
        where=origin+direction*t;normal=re3model::Normalize(re3model::Cross(b-a,c-a));
        if(re3model::Dot(normal,direction)>0)normal=normal*-1.f;
    }else if(!nearestToRay(mesh,body,hidden,origin,direction,where,normal))return false;
    const bool shotgun=weapon==4||weapon==0x10||weapon==0x13;
    // native units (~mm): half the side of the decal square. v12b textures keep the wound in the middle 60-72% of the
    // square with a blood stain around it, so the wound itself stays about its v12 size and the stain is added
    const float radius=shotgun?110.f:weapon==5?92.f:75.f;
    const std::uint8_t texture=shotgun?3:std::uint8_t((std::min)(2.f,random(seed)*3.f));
    float angle=random(seed)*6.2831853f;
    if(texture==2){
        // the run-off of wound-2 (texture +V) points down the body (native world +Y)
        const Vec3 tangent=re3model::Normalize(re3model::Cross(normal,std::abs(normal.y)<0.9f?Vec3{0,1,0}:Vec3{1,0,0})),bitangent=re3model::Cross(normal,tangent);
        const Vec3 down{0,1,0};const float c=re3model::Dot(down,bitangent),s=-re3model::Dot(down,tangent);
        if(c*c+s*s>1.e-4f)angle=std::atan2(s,c);
    }
    return makeWound(mesh,body,hidden,where,normal,radius,angle,texture,wound);
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
