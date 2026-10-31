#pragma once
// fps60 (2026-10-27): HD models at 60 frames per second while the game itself runs at 30.
//
// The game ticks at 30 Hz: its render thread waits two virtual 60 Hz vblanks (420960, timeGetTime) per frame, then draws
// the copied native command queue (40B9C0) and flips. When that queue is drawn, the next build already exists, so at the
// vblank between two frames the queue holds the build that is shown next. The in-between frame draws that queue once
// more (native background, effects and UI as they will be shown) with every HD model of that build moved halfway back
// toward its pose of the build before: each emitted vertex of the prepared frame is projected again from the midpoint
// of its skinned position in the two builds. Lighting, triangle order and OT buckets stay those of the shown build.
//
// Only the HD draw is touched: native simulation, timing and the 30 Hz frames themselves are unchanged.
#include "model_runtime.h"
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <memory>
#include <unordered_map>
#include <vector>

namespace re3fps60 {
using re3model::Vec3;
// What an HD frame needs to be drawn again in between: the skinned positions of its build and of the same draw in the
// build before (null: nothing to move toward, drawn as it is), the projection and the source of every emitted vertex.
struct InterpData {
    std::shared_ptr<const std::vector<Vec3>> current,previous;
    std::vector<re3model::VertexSource> sources;
    re3model::Projection projection;   // numeric fields only (pointers are not used)
    std::uint32_t serial=0,previousSerial=0;   // builds of current / previous (diagnostics)
};
// The last recorded positions of every draw (key: actor, asset, occurrence of that asset in the actor's prepare).
struct Recorded {std::shared_ptr<const std::vector<Vec3>> positions;re3model::Matrix3x4 camera;float h=0,offsetX=0,offsetY=0;std::uint32_t serial=0;};
struct Store {
    std::unordered_map<std::uint64_t,Recorded> last;
    std::uint64_t matched=0,unmatched=0,cut=0,jumped=0;
    void Expire(std::uint32_t serial){
        if(last.size()<256)return;
        for(auto it=last.begin();it!=last.end();)if(serial-it->second.serial>30)it=last.erase(it);else ++it;
    }
};
inline Store& GetStore(){static Store store;return store;}
inline std::uint64_t Key(const void* actor,const void* asset,unsigned occurrence){
    std::uint64_t h=0xcbf29ce484222325ull;
    auto mix=[&](std::uint64_t v){for(unsigned i=0;i<8;++i){h=(h^((v>>(i*8))&0xff))*0x100000001b3ull;}};
    mix(reinterpret_cast<std::uintptr_t>(actor));mix(reinterpret_cast<std::uintptr_t>(asset));mix(occurrence);return h;
}
inline bool SameView(const Recorded& r,const re3model::Projection& p){
    return !std::memcmp(r.camera.m,p.camera.m,sizeof(p.camera.m))&&r.h==p.h&&r.offsetX==p.offsetX&&r.offsetY==p.offsetY;
}
// Records the skinned positions of one draw of build `serial` and returns its InterpData (previous set when the same
// draw was recorded in the build before, under the same camera, and did not jump: room/camera cuts, warps, respawns).
inline std::shared_ptr<InterpData> Record(std::uint64_t key,std::uint32_t serial,const std::vector<re3model::SkinnedVertex>& skinned,const re3model::Projection& projection){
    auto& store=GetStore();
    auto positions=std::make_shared<std::vector<Vec3>>(skinned.size());
    for(std::size_t i=0;i<skinned.size();++i)(*positions)[i]=skinned[i].position;
    auto data=std::make_shared<InterpData>();data->current=positions;data->projection=projection;data->serial=serial;
    auto& slot=store.last[key];
    if(slot.positions&&slot.serial+1==serial&&slot.positions->size()==positions->size()){
        if(!SameView(slot,projection))++store.cut;
        else{
            // a move of more than 600 units (two meters) in one tick is a jump, not motion
            float worst=0;const auto& before=*slot.positions;
            for(std::size_t i=0;i<before.size();i+=97){const Vec3 d{before[i].x-(*positions)[i].x,before[i].y-(*positions)[i].y,before[i].z-(*positions)[i].z};worst=(std::max)(worst,d.x*d.x+d.y*d.y+d.z*d.z);}
            if(worst>600.f*600.f)++store.jumped;else{data->previous=slot.positions;data->previousSerial=slot.serial;++store.matched;}
        }
    }else ++store.unmatched;
    slot.positions=positions;slot.camera=projection.camera;slot.h=projection.h;slot.offsetX=projection.offsetX;slot.offsetY=projection.offsetY;slot.serial=serial;
    store.Expire(serial);
    return data;
}
// The vertices of `base` (prepared from data.current) moved to `weight` of the way from data.previous to data.current.
inline bool Reproject(const InterpData& data,const std::vector<re3model::DrawVertex>& base,float weight,std::vector<re3model::DrawVertex>& out){
    if(!data.previous||!data.current||data.sources.size()!=base.size())return false;
    const auto& now=*data.current;const auto& before=*data.previous;const std::size_t limit=now.size();
    if(before.size()!=limit)return false;
    out.resize(base.size());const auto& p=data.projection;std::atomic<bool> bad{false};
    auto body=[&](std::size_t,std::size_t begin,std::size_t end){
        for(std::size_t i=begin;i<end;++i){const auto& s=data.sources[i];
            if(s.a>=limit||s.b>=limit){bad=true;return;}
            auto at=[&](const std::vector<Vec3>& v){const Vec3& a=v[s.a];const Vec3& b=v[s.b];return Vec3{a.x+(b.x-a.x)*s.t,a.y+(b.y-a.y)*s.t,a.z+(b.z-a.z)*s.t};};
            const Vec3 x0=at(before),x1=at(now);
            const Vec3 world{x0.x+(x1.x-x0.x)*weight,x0.y+(x1.y-x0.y)*weight,x0.z+(x1.z-x0.z)*weight};
            re3model::ProjectedPoint projected;
            if(!re3model::ProjectCameraPoint(re3model::TransformPoint(p.camera,world),p,projected)){bad=true;return;}
            const float renderZ=(std::max)(projected.z,p.nearPlane);
            auto& v=out[i];v=base[i];v.x=projected.x;v.y=projected.y;v.z=1.f-p.nearPlane/renderZ;v.rhw=p.perspectiveCorrect?1.f/renderZ:1.f;
        }
    };
    if(base.size()>=8192){if(!re3model::parallel::For(base.size(),4096,body))return false;}
    else body(0,0,base.size());
    return !bad;
}
} // namespace re3fps60
