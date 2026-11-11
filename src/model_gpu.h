#pragma once
// GPU path for HD models (2026-11-02). The CPU path (model_runtime.h + ModelRenderer::drawSlice) skins, lights, projects,
// clips and sorts every triangle of every actor every frame and then draws it in hundreds of OT bucket slices. Here the
// static mesh lives in video memory and the vertex shader does all of that per vertex from a bone palette:
//   skinning      linear (464 of 466 assets) or the affine dual quaternion of Jill; the bone matrices stay on the CPU
//   deformers     the few thousand vertices a face rig or shoulder corrective touches stay a CPU job in BIND space: the
//                 triangles that reach them are drawn from a small dynamic vertex range, the rest from the static buffer.
//                 Same deformer code and data, the shader only skins the result
//   lighting      the native NCCS / sculpted formula, per vertex or per pixel
//   near clip     the hardware's: clip z = w - near reproduces the CPU depth 1 - near / z and its clip plane exactly
//   OT order      a model has jobs in every bucket of its depth range, a job only marks its bucket; models are drawn
//                 when native work has to go over them (gpuFlushPending; a mask or UI quad only draws the models whose
//                 screen rectangle it touches, gpuFlushRect), one draw per material per model, and the pixel shader
//                 keeps the pixels whose camera depth lies in the flushed buckets. Same order against masks and effects
//                 as per-triangle buckets, per pixel instead of per triangle
//   60 fps        the in-between frame is the same draw with a blended palette
// Compatibility: vs_3_0/ps_3_0 with hardware vertex processing and no vertex texture fetch (GeForce 6, Radeon X1000,
// Intel HD and later); without them, or with pilot.ini gpu_models=0, every model takes the CPU path as before.
#include <windows.h>
#include <d3d9.h>
#include <d3dcompiler.h>
#include <array>
#include <atomic>
#include <string>
#include <thread>
#include <map>
#include <memory>
#include <new>
#include <vector>
#include "model_runtime.h"
#include "model_skinning_affine_dq.h"
#include "model_face_rig.h"
#include "model_pose_weights.h"

namespace re3model {struct MaterialTextures {IDirect3DTexture9* albedo=nullptr;IDirect3DTexture9* stp=nullptr;};}

namespace re3gpu {
using namespace re3model;
constexpr unsigned kMaxBones=32,kLinearRegs=6,kAffineRegs=4,kPaletteFloats=kMaxBones*kLinearRegs*4;
constexpr unsigned kPaletteRegister=24;
#pragma pack(push,1)
// Weights are floats: NVIDIA reports no USHORT4N in D3D9, and they have to sum to 1 far better than SHORT4N rounds.
struct GpuVertex {float position[3],normal[3],uv[2];std::uint8_t bone[4];float weight[4];std::uint32_t colour;};
struct WorldVertex {float position[3],normal[3],uv[2],previous[3],unused[2];std::uint32_t colour;};
struct PoseVertex {std::uint8_t bone[4];float weight[4];float offset[3];float side[2];};
#pragma pack(pop)
static_assert(sizeof(GpuVertex)==56&&sizeof(WorldVertex)==56&&sizeof(PoseVertex)==40,"GPU vertex layout");
inline std::uint32_t OcclusionColour(std::uint8_t ao){return 0xff000000u|std::uint32_t(ao)*0x010101u;}

template<class T> inline void SafeRelease(T*& p){if(p){p->Release();p=nullptr;}}
struct MaterialRange {std::uint32_t firstStatic=0,staticIndices=0,firstDynamic=0,dynamicIndices=0,firstChunk=0,staticChunks=0,dynamicChunks=0;};
// The triangles of one material dominated by one bone, in a row of the index buffer. A model split by a mask only needs
// the chunks that reach the flushed depths. low/high: bind-space bounds, bones: the bones that move them.
struct Chunk {std::uint32_t first=0,indices=0,bones=0;Vec3 low{},high{};};
// One index buffer: per material the triangles of the static vertices (indices into the mesh) and those that reach a
// deformed vertex (indices into the dynamic range).
struct IndexSet {
    IDirect3DIndexBuffer9* buffer=nullptr;std::vector<MaterialRange> ranges;std::vector<Chunk> chunks;
    IndexSet()=default;IndexSet(const IndexSet&)=delete;IndexSet& operator=(const IndexSet&)=delete;
    ~IndexSet(){SafeRelease(buffer);}
};
struct BoneBounds {Vec3 low{},high{};bool used=false;};
struct Mesh {
    IDirect3DVertexBuffer9* vertices=nullptr;   // GpuVertex, every vertex
    IDirect3DVertexBuffer9* pose=nullptr,*dynamicPose=nullptr;   // PoseVertex of the vertices / the dynamic range
    std::uint32_t vertexCount=0;
    std::shared_ptr<IndexSet> base;
    std::map<std::uint32_t,std::shared_ptr<IndexSet>> variants;   // keepBones mask -> triangles of those bones
    std::vector<std::uint32_t> dynamicSource;   // dynamic vertex -> mesh vertex
    std::vector<std::uint32_t> morphed;         // dynamic vertices a deformer can move
    std::vector<GpuVertex> dynamicTemplate;
    std::vector<std::uint8_t> vertexDynamic;    // per mesh vertex: reached by a deformer
    std::vector<std::uint32_t> dynamicOf;       // mesh vertex -> dynamic vertex or ~0
    std::vector<BoneBounds> bounds;
    std::vector<std::uint8_t> triangleBone;     // dominant bone of every source triangle
    std::vector<std::uint32_t> materialFlags;std::vector<float> materialColour;   // 4 per material, RGBA 0..1
    std::vector<float> materialGem;   // kGemParams per material (Gem materials, crystals round 2026-11-07)
    std::vector<float> materialMetal; // kMetalParams per material (Metal materials, door round 2026-11-11)
    std::vector<float> materialLeather; // kLeatherParams per material (Leather materials, fx 2026-11-11e)
    bool hasBlend=false;std::size_t bytes=0;
    Mesh()=default;Mesh(const Mesh&)=delete;Mesh& operator=(const Mesh&)=delete;
    ~Mesh(){SafeRelease(vertices);SafeRelease(pose);SafeRelease(dynamicPose);}
};
inline HRESULT FillBuffer(IDirect3DDevice9* device,const void* data,std::size_t bytes,IDirect3DVertexBuffer9** out){
    HRESULT hr=device->CreateVertexBuffer(UINT(bytes),D3DUSAGE_WRITEONLY,0,D3DPOOL_DEFAULT,out,nullptr);if(FAILED(hr))return hr;
    void* memory=nullptr;hr=(*out)->Lock(0,0,&memory,0);if(FAILED(hr)){SafeRelease(*out);return hr;}
    std::memcpy(memory,data,bytes);hr=(*out)->Unlock();if(FAILED(hr))SafeRelease(*out);return hr;
}
// Triangle order for the vertex cache (Tipsify, Sander et al. 2007). The packs come out of decimation in nearly random
// order: a 16-entry cache misses 2.6 vertices per triangle, the shader would skin each vertex about three times. Linear
// time, run when an index set is built; opaque meshes do not care about the order.
inline void OptimizeTriangleOrder(std::uint32_t* indices,std::size_t indexCount,std::uint32_t vertexCount,int cacheSize=16){
    const std::size_t triangles=indexCount/3;if(triangles<2||!vertexCount)return;
    std::vector<std::uint32_t> first(std::size_t(vertexCount)+1,0),adjacent(indexCount),live(vertexCount,0);std::vector<int> stamp(vertexCount,0);
    for(std::size_t i=0;i<indexCount;++i)++live[indices[i]];
    for(std::uint32_t v=0;v<vertexCount;++v)first[v+1]=first[v]+live[v];
    {std::vector<std::uint32_t> fill(first.begin(),first.end()-1);for(std::size_t i=0;i<indexCount;++i)adjacent[fill[indices[i]]++]=std::uint32_t(i/3);}
    std::vector<std::uint8_t> done(triangles,0);std::vector<std::uint32_t> output,deadEnd,next;output.reserve(indexCount);deadEnd.reserve(indexCount);next.reserve(64);
    int fan=int(indices[0]),time=cacheSize+1;std::uint32_t cursor=1;
    while(fan>=0){
        next.clear();
        for(std::uint32_t a=first[fan];a<first[std::size_t(fan)+1];++a){const std::uint32_t t=adjacent[a];if(done[t])continue;done[t]=1;
            for(unsigned k=0;k<3;++k){const std::uint32_t v=indices[t*3+k];output.push_back(v);deadEnd.push_back(v);next.push_back(v);--live[v];
                if(time-stamp[v]>cacheSize)stamp[v]=time++;}}
        int best=-1,priority=-1;
        for(const std::uint32_t v:next)if(live[v]>0){int p=0;if(time-stamp[v]+2*int(live[v])<=cacheSize)p=time-stamp[v];if(p>priority){priority=p;best=int(v);}}
        if(best<0){while(!deadEnd.empty()&&best<0){const std::uint32_t v=deadEnd.back();deadEnd.pop_back();if(live[v]>0)best=int(v);}}
        if(best<0){while(cursor<vertexCount&&best<0){if(live[cursor]>0)best=int(cursor);++cursor;}}
        fan=best;
    }
    if(output.size()==indexCount)std::memcpy(indices,output.data(),indexCount*4);
}
inline std::shared_ptr<IndexSet> BuildIndices(IDirect3DDevice9* device,const re3model::Mesh& source,const Mesh& gpu,const std::vector<std::uint8_t>* triangleBone,std::uint32_t keep,HRESULT* result=nullptr){
    auto set=std::make_shared<IndexSet>();std::vector<std::uint32_t> list;list.reserve(source.indices.size());
    set->ranges.resize(source.materials.size());
    auto kept=[&](std::uint32_t t){return !triangleBone||t>=triangleBone->size()||((keep>>(*triangleBone)[t])&1u)!=0;};
    // every triangle that reaches a deformed vertex has its three vertices in the dynamic range; a neighbour with one or
    // two of them does not move and stays static
    auto dynamic=[&](const std::uint32_t* v){return !gpu.vertexDynamic.empty()&&gpu.vertexDynamic[v[0]]&&gpu.vertexDynamic[v[1]]&&gpu.vertexDynamic[v[2]];};
    std::vector<std::vector<std::uint32_t>> byBone(kMaxBones);
    // vertices numbered per chunk for the optimizer: its tables are as large as the highest index
    std::vector<std::uint32_t> local(gpu.vertexCount,~0u),used;
    // one pass per kind: the static triangles of the material by dominant bone, then the dynamic ones
    auto gather=[&](const Material& material,bool wantDynamic,std::uint32_t& chunks){
        for(auto& b:byBone)b.clear();
        for(std::uint32_t i=material.firstIndex;i<material.firstIndex+material.indexCount;i+=3){const std::uint32_t* v=&source.indices[i];
            if(kept(i/3)&&dynamic(v)==wantDynamic)byBone[gpu.triangleBone[i/3]&(kMaxBones-1)].push_back(i);}
        for(const auto& triangles:byBone){if(triangles.empty())continue;Chunk chunk;chunk.first=std::uint32_t(list.size());bool any=false;
            for(const std::uint32_t i:triangles)for(unsigned k=0;k<3;++k){const std::uint32_t index=source.indices[i+k];const auto& x=source.vertices[index];
                list.push_back(wantDynamic?gpu.dynamicOf[index]:index);for(unsigned w=0;w<4;++w)if(x.weight[w])chunk.bones|=1u<<(x.bone[w]&31);
                if(!any){any=true;chunk.low=chunk.high=x.position;}else{chunk.low={(std::min)(chunk.low.x,x.position.x),(std::min)(chunk.low.y,x.position.y),(std::min)(chunk.low.z,x.position.z)};chunk.high={(std::max)(chunk.high.x,x.position.x),(std::max)(chunk.high.y,x.position.y),(std::max)(chunk.high.z,x.position.z)};}}
            chunk.indices=std::uint32_t(list.size())-chunk.first;chunk.low=chunk.low-Vec3{24,24,24};chunk.high=chunk.high+Vec3{24,24,24};
            used.clear();std::uint32_t* in=list.data()+chunk.first;
            for(std::uint32_t i=0;i<chunk.indices;++i){auto& id=local[in[i]];if(id==~0u){id=std::uint32_t(used.size());used.push_back(in[i]);}in[i]=id;}
            for(const std::uint32_t v:used)local[v]=~0u;
            OptimizeTriangleOrder(in,chunk.indices,std::uint32_t(used.size()));
            for(std::uint32_t i=0;i<chunk.indices;++i)in[i]=used[in[i]];
            set->chunks.push_back(chunk);++chunks;}
    };
    for(std::size_t m=0;m<source.materials.size();++m){const auto& material=source.materials[m];auto& range=set->ranges[m];
        range.firstChunk=std::uint32_t(set->chunks.size());range.firstStatic=std::uint32_t(list.size());
        gather(material,false,range.staticChunks);
        range.staticIndices=std::uint32_t(list.size())-range.firstStatic;range.firstDynamic=std::uint32_t(list.size());
        if(!gpu.vertexDynamic.empty())gather(material,true,range.dynamicChunks);
        range.dynamicIndices=std::uint32_t(list.size())-range.firstDynamic;
    }
    HRESULT hr=S_OK;if(result)*result=S_OK;
    if(list.empty())return set;
    hr=device->CreateIndexBuffer(UINT(list.size()*4),D3DUSAGE_WRITEONLY,D3DFMT_INDEX32,D3DPOOL_DEFAULT,&set->buffer,nullptr);
    void* memory=nullptr;if(SUCCEEDED(hr))hr=set->buffer->Lock(0,0,&memory,0);
    if(SUCCEEDED(hr)){std::memcpy(memory,list.data(),list.size()*4);hr=set->buffer->Unlock();}
    if(result)*result=hr;if(FAILED(hr))return {};
    return set;
}
// deformed: vertices a deformer may move (face rig, shoulder corrective). occlusion: one byte per vertex, may be null.
inline std::shared_ptr<Mesh> BuildMesh(IDirect3DDevice9* device,const re3model::Mesh& source,const std::vector<std::uint8_t>& deformed,const std::vector<std::uint8_t>* occlusion,const re3poseweights::PoseWeights* pose,bool referenceBoneFirst,HRESULT* result=nullptr){
    auto gpu=std::make_shared<Mesh>();HRESULT local;HRESULT& hr=result?*result:local;hr=E_INVALIDARG;
    const std::size_t count=source.vertices.size();if(!device||!count||source.inverseBind.size()>kMaxBones||source.materials.size()>64)return {};
    gpu->vertexCount=std::uint32_t(count);
    std::vector<GpuVertex> vertices(count);gpu->bounds.resize(source.inverseBind.size());
    for(std::size_t i=0;i<count;++i){const auto& v=source.vertices[i];auto& out=vertices[i];
        std::memcpy(out.position,&v.position,12);std::memcpy(out.normal,&v.normal,12);out.uv[0]=v.uv.x;out.uv[1]=v.uv.y;
        // the affine dual quaternion blend takes its hemisphere from the largest weight: that bone comes first
        unsigned order[4]={0,1,2,3};
        if(referenceBoneFirst){unsigned reference=0;for(unsigned k=0;k<4;++k)if(v.weight[k]>v.weight[reference])reference=k;std::swap(order[0],order[reference]);}
        for(unsigned k=0;k<4;++k){out.bone[k]=std::uint8_t(v.bone[order[k]]);out.weight[k]=float(v.weight[order[k]])/65535.f;}
        out.colour=OcclusionColour(occlusion&&i<occlusion->size()?(*occlusion)[i]:std::uint8_t(255));
        for(unsigned k=0;k<4;++k)if(v.weight[k]&&v.bone[k]<gpu->bounds.size()){auto& b=gpu->bounds[v.bone[k]];
            if(!b.used){b.used=true;b.low=b.high=v.position;}
            else{b.low={(std::min)(b.low.x,v.position.x),(std::min)(b.low.y,v.position.y),(std::min)(b.low.z,v.position.z)};b.high={(std::max)(b.high.x,v.position.x),(std::max)(b.high.y,v.position.y),(std::max)(b.high.z,v.position.z)};}}
    }
    // deformers move their vertices a little in bind space: room for that in the bounds of the depth range
    for(auto& b:gpu->bounds){b.low=b.low-Vec3{24,24,24};b.high=b.high+Vec3{24,24,24};}
    bool any=false;for(auto d:deformed)if(d){any=true;break;}
    if(any){
        gpu->vertexDynamic.assign(count,0);gpu->dynamicOf.assign(count,~0u);
        for(std::size_t i=0;i+2<source.indices.size();i+=3){const std::uint32_t* v=&source.indices[i];
            if(deformed[v[0]]||deformed[v[1]]||deformed[v[2]])for(unsigned k=0;k<3;++k)gpu->vertexDynamic[v[k]]=1;}
        for(std::uint32_t i=0;i<count;++i)if(gpu->vertexDynamic[i]){gpu->dynamicOf[i]=std::uint32_t(gpu->dynamicSource.size());gpu->dynamicSource.push_back(i);gpu->dynamicTemplate.push_back(vertices[i]);if(deformed[i])gpu->morphed.push_back(gpu->dynamicOf[i]);}
    }
    hr=FillBuffer(device,vertices.data(),count*sizeof(GpuVertex),&gpu->vertices);if(FAILED(hr))return {};
    gpu->bytes=count*sizeof(GpuVertex);
    if(pose&&pose->valid){
        std::vector<PoseVertex> second(count);
        for(const auto& e:pose->entries){auto& out=second[e.vertex];for(unsigned k=0;k<4;++k){out.bone[k]=std::uint8_t(e.bone[k]);out.weight[k]=float(e.weight[k])/65535.f;}
            out.offset[0]=e.offset.x;out.offset[1]=e.offset.y;out.offset[2]=e.offset.z;out.side[e.side]=1;}
        hr=FillBuffer(device,second.data(),count*sizeof(PoseVertex),&gpu->pose);if(FAILED(hr))return {};gpu->bytes+=count*sizeof(PoseVertex);
        if(!gpu->dynamicSource.empty()){std::vector<PoseVertex> dynamic(gpu->dynamicSource.size());for(std::size_t k=0;k<dynamic.size();++k)dynamic[k]=second[gpu->dynamicSource[k]];
            hr=FillBuffer(device,dynamic.data(),dynamic.size()*sizeof(PoseVertex),&gpu->dynamicPose);if(FAILED(hr))return {};}
    }
    gpu->triangleBone.resize(source.indices.size()/3);
    for(std::size_t t=0;t<gpu->triangleBone.size();++t){std::uint16_t bone[12];std::uint32_t sum[12]{};unsigned n=0,best=0;
        for(unsigned c=0;c<3;++c){const auto& x=source.vertices[source.indices[t*3+c]];
            for(unsigned k=0;k<4;++k){if(!x.weight[k])continue;unsigned j=0;while(j<n&&bone[j]!=x.bone[k])++j;if(j==n)bone[n++]=x.bone[k];sum[j]+=x.weight[k];}}
        for(unsigned j=1;j<n;++j)if(sum[j]>sum[best])best=j;gpu->triangleBone[t]=std::uint8_t(n?bone[best]:0);}
    gpu->base=BuildIndices(device,source,*gpu,nullptr,~0u,&hr);if(!gpu->base)return {};gpu->bytes+=source.indices.size()*4;
    gpu->materialFlags.reserve(source.materials.size());gpu->materialColour.reserve(source.materials.size()*4);
    for(const auto& m:source.materials){gpu->materialFlags.push_back(m.flags);gpu->hasBlend=gpu->hasBlend||(m.flags&re3model::AlphaBlend)!=0;
        for(unsigned k=0;k<4;++k)gpu->materialColour.push_back(float((m.baseColorRGBA>>(k*8))&255u)/255.f);
        gpu->materialGem.insert(gpu->materialGem.end(),m.gem,m.gem+kGemParams);
        gpu->materialMetal.insert(gpu->materialMetal.end(),m.metal,m.metal+kMetalParams);
        gpu->materialLeather.insert(gpu->materialLeather.end(),m.leather,m.leather+kLeatherParams);}
    hr=S_OK;return gpu;
}

enum class Skin:std::uint8_t {Linear,LinearPose,Affine,World,Decal};
// Light constants of one draw (the order of the shader's lightData array).
struct LightConstants {float rows[3][4]{},colour[3][4]{},ambient[4]{},base[4]{},up[4]{},profile0[4]{},profile1[4]{};};
inline void MakeLight(const SculptedLighting& s,LightConstants& out){
    for(unsigned r=0;r<3;++r)for(unsigned c=0;c<3;++c){out.rows[r][c]=s.rows[r*3+c];out.colour[r][c]=s.colour[r*3+c];}
    for(unsigned k=0;k<3;++k){out.ambient[k]=s.ambient[k];out.base[k]=s.base[k];}
    out.up[0]=s.up.x;out.up[1]=s.up.y;out.up[2]=s.up.z;
    const auto& p=s.profile;out.profile0[0]=p.ambient;out.profile0[1]=p.direct;out.profile0[2]=p.hemi;out.profile0[3]=p.occlusion;
    out.profile1[0]=p.occlusionDirect;out.profile1[1]=p.shoulder;out.profile1[2]=1;
}
struct DrawRange {std::uint32_t firstVertex=0,vertexCount=0,material=0;};
// ---------------------------------------------------------------- lighting round 2026-11-10 (dynamic shadows, flash light)
// One HD actor of one build as a shadow caster and receiver. Its draws (body, kit parts, weapon, overlay) are projected
// along floorDir onto the plane y = floorY (the floor shadow, drawn right after the room background) and rendered along
// toLight into a tile of the shadow map (what the actor's own pixels and its neighbours' are tested against).
struct Draw;
// fix 2026-11-11d (shadow at floor edges): where an actor's floor shadow may lie, from the room's collision data baked per
// room (model-pack\floors\<room>.r3floor, tools\rdt_floor\bake_floors.py; model_integration.h lightReceivers). World units,
// Y down. None or mode 0: the endless plane as before.
struct ShadowBlock {float q[8]{};float top=0,bottom=0;};          // a convex footprint (x,z corners in order) between two heights
struct ShadowFace {float a[2]{},b[2]{},n[2]{};float top=0,bottom=0;};   // a vertical face a-b (x,z), unit normal n toward the actor
struct FloorArea {int x0=0,z0=0,cell=64,nx=0,nz=0;std::vector<std::uint8_t> inside;};   // the floor area under the actor: 255 in its cells (row-major)
struct ShadowReceivers {
    int mode=0;                         // 0 endless plane, 1 cut at the floor edge, 2 + the cut part on wall faces
    std::shared_ptr<const FloorArea> area;
    std::vector<ShadowBlock> blocks;    // what can hide the floor from the camera (32 at most)
    std::vector<ShadowFace> faces;      // mode 2: faces near the actor, lit from its side and seen by the camera (4 at most)
};
struct ShadowGroup {
    const void* actor=nullptr;
    float toLight[3]={0,-1,0};    // unit, world (Y down): from the actor toward its key light (the strongest native light row)
    float floorDir[3]={0,-1,0};   // the same with its elevation above the floor kept over the configured minimum
    float strength=0;             // darkness of the floor shadow, 0..1
    float floorY=0,feet[3]{};     // world: the floor under the actor and its root on it
    float centre[3]{},radius=0;   // world: a sphere round the actor
    unsigned keyLight=0;          // the row of the key light in the draws' light constants
    std::vector<std::weak_ptr<Draw>> casters;
    // the shadow map of the pass that drew it last (ModelRenderer::renderLighting)
    int tile=-1;float rows[3][4]{};float tileRect[4]{};float depthRange=1;std::uint64_t renderedPass=0;
    std::shared_ptr<const ShadowReceivers> receivers;   // fix 2026-11-11d
};
// A point light of a muzzle flash at one build (world), already scaled by its age.
struct FlashLight {float position[3]{},radius=0,colour[3]{},floorY=0;bool fresh=false;};
struct ShadowFrame {
    std::uint32_t serial=0;std::vector<std::shared_ptr<ShadowGroup>> groups;std::vector<FlashLight> flashes;
    // the camera of the build (of its first skinned draw): the floor light of a flash is placed with it
    bool camera=false;float cameraRows[12]{},origin[3]{},h=256,offsetX=160,offsetY=120,pixelOffsetX=0,pixelOffsetY=0;
    float bucketDepth=0;   // fix 2026-11-11c: camera depth of one ordering-table slot (of that draw), for the mask depths
};
// An additive screen quad of the radial light texture (muzzle flash halo, core, afterglow), in target pixels.
struct GlowQuad {float x=0,y=0,half=0;float colour[3]{};};
struct LightingConfig {
    bool floor=true,self=true,pool=true;
    float strength=1.f,selfStrength=0.75f;   // floor shadow darkness scale; how much of the key light a shadowed model pixel loses
    float tint[3]={0.10f,0.11f,0.15f};       // the picture under a full floor shadow is multiplied by this
    // world scale: a character is about 3000 units tall (the muzzle of a pistol aimed straight is 2459 above the floor)
    float fadeStart=2800.f,fadeEnd=5200.f;   // floor shadow fades with the distance from the actor's feet (world units)
    float referenceHeight=3000.f;            // caster height at which the wide penumbra takes over
    float bias=7.f,slopeBias=26.f;           // shadow map bias, world units
    float poolGain=0.6f,poolReach=1.6f;      // floor light of a muzzle flash; its reach in radii of the flash light
    float contact=0.55f;                     // a second, straight-down projection at this share of the darkness: the soft
                                             // contact shadow under the feet whatever the light's direction (0 = none)
    unsigned tileSize=1024;
};
// One model of one build, from its OT jobs to the frame that retires them.
struct Draw {
    std::shared_ptr<ShadowGroup> shadow;   // lighting round 2026-11-10: the actor this draw belongs to (receives its shadow map)
    float flash[8]{};                      // the flash light of this build: xyz position (world), 1/radius; rgb colour x intensity
    std::shared_ptr<const void> owner;   // the ModelAsset: buffers and textures stay valid; dropped before a device reset
    Mesh* mesh=nullptr;std::shared_ptr<IndexSet> indices;
    Skin skin=Skin::Linear;
    std::vector<MaterialTextures> textures;std::uint64_t hiddenMaterials=0;
    std::shared_ptr<std::vector<float>> palette,previous;unsigned paletteRegs=0;
    std::vector<float> affineRows,chunkDepth;   // skin transforms of an affine draw; low/high camera depth per chunk, on the first split
    // Palette and camera are relative to `origin`, the rounded camera eye: the float work of the shader then stays near
    // zero instead of at world coordinates of 20000+ (0.3 pixel of noise at the near plane). Same origin for all builds of
    // one camera, so palettes of two builds still blend.
    float origin[3]{};
    float cameraRows[12]{},h=256,offsetX=160,offsetY=120,pixelOffsetX=0,pixelOffsetY=0,nearPlane=16,poseFactor[2]{},previousPoseFactor[2]{},tint[4]={1,1,1,1},eye[3]{};
    float rect[4]={-1.e9f,-1.e9f,1.e9f,1.e9f};   // screen pixels this draw can touch
    LightConstants light;bool lit=false;
    std::uint64_t epoch=0;std::uint16_t bucketHigh=0,bucketLow=0;bool nearClosed=false;float bucketDepth=32.f;
    // fix 2026-11-08 (wall split): a wall plane in camera space (dot(xyz,camera)+w, positive = the room side) and the side
    // this draw keeps: +1 the room side, -1 beyond the wall, 0 everything (model_integration.h kWallSplit)
    float wallPlane[4]={0,0,0,1},wallSide=0;
    // fix 2026-11-08: a draw hidden behind wall masks (bone split) gets its own model depth: its depth never rejects the
    // room-side draws (Nemesis' legs swing in front of his arms while both hang through the R20F04 window)
    bool ownDepth=false;
    std::string name;
    std::vector<GpuVertex> dynamic;   // Linear/Affine: the deformed vertices of this build
    std::vector<WorldVertex> world;   // World: placed vertices of this build (overlay)
    std::vector<GpuVertex> decal;std::vector<DrawRange> decalRuns;   // Decal: triangle list
    // where the dynamic vertices of this draw sit in the stream (ring generation and vertex offset)
    std::uint32_t ringGeneration=0,ringVertex=0;
    std::uint32_t listSerial=0,listIndex=0;   // its entry in the list being gathered
    bool ready()const{return owner&&mesh;}
};
struct Entry {std::shared_ptr<Draw> draw;std::uint16_t high=0,low=0;};
struct Stats {std::uint64_t flushes=0,entries=0,draws=0,triangles=0,bytes=0,flushesFull=0;double milliseconds=0;};

// The composed palette of a linear skin: world * inverse bind, cofactor normal matrix (renormalized by the shader), damage
// tint of the bone in the normal rows' w. Same matrices as re3model::Skin; false for a nonfinite pose.
inline bool LinearPalette(const re3model::Mesh& mesh,const std::vector<Matrix3x4>& world,const float (*boneTint)[3],float* out,std::string* error){
    if(world.size()<mesh.inverseBind.size()){if(error)*error="Missing native pose matrices";return false;}
    for(std::size_t n=0;n<mesh.inverseBind.size();++n){
        if(!Finite(world[n])){if(error)*error="Nonfinite native pose";return false;}
        const Matrix3x4 t=Multiply(world[n],mesh.inverseBind[n]);Matrix3x4 normal;
        if(!NormalMatrix(t,normal)){const float* a=t.m;const Vec3 x{a[0],a[1],a[2]},y{a[4],a[5],a[6]},z{a[8],a[9],a[10]};const Vec3 c0=Cross(y,z),c1=Cross(z,x),c2=Cross(x,y);
            normal={{c0.x,c0.y,c0.z,0,c1.x,c1.y,c1.z,0,c2.x,c2.y,c2.z,0}};if(!Finite(normal)){if(error)*error="Singular skin transform";return false;}}
        float* row=out+n*kLinearRegs*4;std::memcpy(row,t.m,48);std::memcpy(row+12,normal.m,48);
        const float* tint=boneTint&&n<kMaxBones?boneTint[n]:nullptr;for(unsigned c=0;c<3;++c)row[12+c*4+3]=tint?tint[c]:1.f;
    }
    return true;
}
// Rigid dual quaternion and stretch of every bone (re3affine::Decompose). Layout: real xyzw, dual xyzw, stretch xx xy xz yy, yz zz.
inline bool AffinePalette(const re3model::Mesh& mesh,const std::vector<Matrix3x4>& world,const Matrix3x4* head,float* out,std::string* error){
    if(mesh.inverseBind.empty()||world.size()<mesh.inverseBind.size()){if(error)*error="Affine DQ missing pose matrices";return false;}
    for(std::size_t i=0;i<mesh.inverseBind.size();++i){re3affine::Joint joint;
        if(!re3affine::Decompose(Multiply(head&&i==1?*head:world[i],mesh.inverseBind[i]),joint)){if(error)*error="Affine DQ requires finite positive well-conditioned skin transforms";return false;}
        float* r=out+i*kAffineRegs*4;const auto& q=joint.rigid.real;const auto& d=joint.rigid.dual;const auto& s=joint.stretch;
        r[0]=float(q.x);r[1]=float(q.y);r[2]=float(q.z);r[3]=float(q.w);r[4]=float(d.x);r[5]=float(d.y);r[6]=float(d.z);r[7]=float(d.w);
        r[8]=float(s.xx);r[9]=float(s.xy);r[10]=float(s.xz);r[11]=float(s.yy);r[12]=float(s.yz);r[13]=float(s.zz);r[14]=r[15]=0;
    }
    return true;
}
// The two raised-arm factors and the head lift of the corrective, as in re3affine::SkinAffineDualQuaternionImpl.
inline bool CorrectiveHead(const re3shoulder::ShoulderCorrective& corrective,const re3shoulder::Factors& factors,const std::vector<Matrix3x4>& world,Matrix3x4& head){
    const float headFactor=(std::min)(factors.raised[0],factors.raised[1]);
    if(!(headFactor>0)||corrective.HeadPitchLiftDegrees()==0)return false;
    const double angle=-double(corrective.HeadPitchLiftDegrees())*headFactor*(3.14159265358979323846/180.0);
    const float cosine=float(std::cos(angle)),sine=float(std::sin(angle));
    head=Multiply(world[1],Matrix3x4{{cosine,-sine,0,0,sine,cosine,0,0,0,0,1,0}});return true;
}
// Camera depth of every chunk of a draw (as DepthRange, per chunk), kept from the first flush that splits the model.
inline void ChunkDepths(Draw& d){
    const auto& chunks=d.indices->chunks;d.chunkDepth.resize(chunks.size()*2);const bool affine=d.skin==Skin::Affine;
    const float* transforms=affine?d.affineRows.data():d.palette->data();const unsigned stride=affine?12:kLinearRegs*4;const float* cam=d.cameraRows+8;
    for(std::size_t c=0;c<chunks.size();++c){const auto& chunk=chunks[c];float low=1.e30f,high=-1.e30f;
        for(std::uint32_t mask=chunk.bones;mask;mask&=mask-1){unsigned b=0;_BitScanForward(reinterpret_cast<unsigned long*>(&b),mask);const float* t=transforms+b*stride;
            float row[4];for(unsigned k=0;k<4;++k)row[k]=cam[0]*t[k]+cam[1]*t[4+k]+cam[2]*t[8+k];row[3]+=cam[3];
            float zl=row[3],zh=row[3];const float* a=&chunk.low.x;const float* e=&chunk.high.x;
            for(unsigned k=0;k<3;++k){const float p=row[k]*a[k],q=row[k]*e[k];zl+=(std::min)(p,q);zh+=(std::max)(p,q);}
            low=(std::min)(low,zl);high=(std::max)(high,zh);}
        d.chunkDepth[c*2]=low;d.chunkDepth[c*2+1]=high;}
}
// The dynamic vertices of this build: the template with the current bind position/normal of every vertex a deformer can
// move (face rigs write the mesh in place; the shoulder corrective is applied here as the CPU skin applies it per vertex).
inline void FillDynamic(const re3model::Mesh& mesh,const Mesh& gpu,const re3shoulder::ShoulderCorrective* corrective,const re3shoulder::Factors* factors,std::vector<GpuVertex>& out){
    out=gpu.dynamicTemplate;
    for(const std::uint32_t k:gpu.morphed){const std::uint32_t i=gpu.dynamicSource[k];const Vertex* v=&mesh.vertices[i];Vertex adjusted;
        if(corrective&&factors){const auto* entry=corrective->ForVertex(i);const auto* raised=corrective->ForRaisedVertex(i);
            const float raisedFactor=raised?factors->raised[raised->side-2]:0;
            if(raisedFactor>0){const float lowFactor=entry?factors->side[entry->side]:0;if(re3shoulder::ApplyCombined(entry,lowFactor,*raised,raisedFactor,*v,adjusted))v=&adjusted;}
            else if(entry&&factors->side[entry->side]>0){if(re3shoulder::ApplyEntry(*entry,factors->side[entry->side],*v,adjusted))v=&adjusted;}}
        auto& to=out[k];std::memcpy(to.position,&v->position,12);std::memcpy(to.normal,&v->normal,12);
    }
}
// Camera depth range of the mesh for these skin transforms: every vertex is a weighted mean of its bones' transforms of
// it, so it lies between the extremes of the bones' bounds under their transforms. rows: the palette's 3x4 transforms.
inline void DepthRange(const Mesh& gpu,const float* transforms,unsigned stride,const float* cameraZ,float& low,float& high){
    low=(std::numeric_limits<float>::max)();high=-low;
    for(std::size_t b=0;b<gpu.bounds.size();++b){const auto& box=gpu.bounds[b];if(!box.used)continue;const float* t=transforms+b*stride;
        float row[4];for(unsigned c=0;c<4;++c)row[c]=cameraZ[0]*t[c]+cameraZ[1]*t[4+c]+cameraZ[2]*t[8+c];row[3]+=cameraZ[3];
        float zl=row[3],zh=row[3];const float* a=&box.low.x;const float* e=&box.high.x;
        for(unsigned c=0;c<3;++c){const float p=row[c]*a[c],q=row[c]*e[c];zl+=(std::min)(p,q);zh+=(std::max)(p,q);}
        low=(std::min)(low,zl);high=(std::max)(high,zh);}
}
// Screen rectangle of the same bounds: a quad outside it cannot be in front of or behind anything of this draw.
inline void ScreenRect(const Mesh& gpu,const float* transforms,unsigned stride,const Projection& p,float* rect){
    float l=1.e9f,t=1.e9f,r=-1.e9f,b=-1.e9f;const float* cam=p.camera.m;
    for(std::size_t n=0;n<gpu.bounds.size();++n){const auto& box=gpu.bounds[n];if(!box.used)continue;const float* s=transforms+n*stride;float m[12];
        for(unsigned row=0;row<3;++row)for(unsigned c=0;c<4;++c)m[row*4+c]=cam[row*4]*s[c]+cam[row*4+1]*s[4+c]+cam[row*4+2]*s[8+c]+(c==3?cam[row*4+3]:0.f);
        for(unsigned corner=0;corner<8;++corner){const float x=corner&1?box.high.x:box.low.x,y=corner&2?box.high.y:box.low.y,z=corner&4?box.high.z:box.low.z;
            const float cz=m[8]*x+m[9]*y+m[10]*z+m[11];if(cz<p.nearPlane)return;   // reaches the near plane: the whole screen
            const float sx=(m[0]*x+m[1]*y+m[2]*z+m[3])*p.h/cz+p.offsetX,sy=(m[4]*x+m[5]*y+m[6]*z+m[7])*p.h/cz+p.offsetY;
            l=(std::min)(l,sx);r=(std::max)(r,sx);t=(std::min)(t,sy);b=(std::max)(b,sy);}
    }
    if(l>r)return;rect[0]=l*p.scaleX-4;rect[1]=t*p.scaleY-4;rect[2]=r*p.scaleX+4;rect[3]=b*p.scaleY+4;
}
inline void ScreenRectWorld(const WorldVertex* v,std::size_t count,const Projection& p,float* rect){
    float l=1.e9f,t=1.e9f,r=-1.e9f,b=-1.e9f;
    for(std::size_t i=0;i<count;++i){const Vec3 c=TransformPoint(p.camera,{v[i].position[0],v[i].position[1],v[i].position[2]});if(c.z<p.nearPlane)return;
        const float sx=c.x*p.h/c.z+p.offsetX,sy=c.y*p.h/c.z+p.offsetY;l=(std::min)(l,sx);r=(std::max)(r,sx);t=(std::min)(t,sy);b=(std::max)(b,sy);}
    if(l>r)return;rect[0]=l*p.scaleX-4;rect[1]=t*p.scaleY-4;rect[2]=r*p.scaleX+4;rect[3]=b*p.scaleY+4;
}
inline void DepthRangeWorld(const WorldVertex* v,std::size_t count,const float* cameraZ,float& low,float& high){
    low=(std::numeric_limits<float>::max)();high=-low;
    for(std::size_t i=0;i<count;++i){const float z=cameraZ[0]*v[i].position[0]+cameraZ[1]*v[i].position[1]+cameraZ[2]*v[i].position[2]+cameraZ[3];low=(std::min)(low,z);high=(std::max)(high,z);}
}

// ---------------------------------------------------------------- shaders
inline const char* ShaderSource(){return R"HLSL(
float4 lightData[11]:register(LIGHT_REGISTER);
// lighting round 2026-11-10: keep = how much of each native light reaches the surface (the shadow map takes the key light
// away), extra = light added on top (the muzzle flash). keep 1 and extra 0 give the formula of 2026-11-02.
float3 ShadeLit(float3 n,float ao,float3 keep,float3 extra){
    n=normalize(n);
    float3 lambert=max(0,float3(dot(lightData[0].xyz,n),dot(lightData[1].xyz,n),dot(lightData[2].xyz,n)))*keep;
    float ambient=lightData[9].x*(1-lightData[9].w*(1-ao))*max(0,1+lightData[9].z*dot(n,lightData[8].xyz));
    float direct=lightData[9].y*(1-lightData[10].x*(1-ao));
    float3 x=(lightData[6].rgb*ambient+float3(dot(lightData[3].xyz,lambert),dot(lightData[4].xyz,lambert),dot(lightData[5].xyz,lambert))*direct)*lightData[7].rgb+extra;
    float shoulder=lightData[10].y;
    if(shoulder>0){float knee=1-shoulder;x=lerp(x,knee+shoulder*(1-exp((knee-x)/shoulder)),step(knee,x));}
    return saturate(x);
}
float3 Shade(float3 n,float ao){return ShadeLit(n,ao,float3(1,1,1),float3(0,0,0));}
#ifdef VERTEX_SHADER
float4 projection:register(c0);   // clip.x = x*X + y*Z, clip.y = z*Y + w*Z
float4 misc:register(c1);         // x near plane, y World/in-between weight, zw pose factors
float4 cameraRows[3]:register(c2);
float4 colourTint:register(c16);  // material colour x draw tint
float4 eyeWorld:register(c17);
float4 wallPlane:register(c18);   // fix 2026-11-08: camera-space wall plane (Draw::wallPlane)
// lighting round 2026-11-10 (c19..c23)
#ifdef SHADOW_FLOOR
float4 shadowPlane:register(c19);   // xyz toward the light (unit), w the floor's y in the draw's space
float4 shadowFeet:register(c20);    // xyz the actor's feet in the draw's space, w 1 / reference height
// fix 2026-11-11d (shadow at floor edges): the plane the caster is laid on, in the draw's space - unit normal toward the
// actor's side, w = dot(normal, a point of it); the floor is (0,-1,0,-floorY), a wall face its own plane - and the world
// position of the draw's origin (the pixel shader tests the point against the room in world units)
float4 receiverPlane:register(c21);
float4 drawOrigin:register(c22);
#endif
#if defined(SHADOW_DEPTH)||defined(FX_LIGHT)
float4 shadowRows[3]:register(c21); // the draw's space -> shadow map clip xy (-1..1) and depth (0..1) of its actor's tile
#endif
float4 palette[216]:register(c24);
struct Input {
    float3 position:POSITION;float3 normal:NORMAL;float2 uv:TEXCOORD0;float4 colour:COLOR0;
#ifdef SKIN_WORLD
    float3 previous:TEXCOORD1;
#else
    float4 bones:BLENDINDICES0;float4 weights:BLENDWEIGHT0;
#endif
#ifdef SKIN_POSE
    float4 bones2:BLENDINDICES1;float4 weights2:BLENDWEIGHT1;float3 offset:TEXCOORD1;float2 side:TEXCOORD2;
#endif
};
struct Output {float4 position:POSITION;float4 colour:COLOR0;float2 uv:TEXCOORD0;float3 depthOcclusion:TEXCOORD1;
#if defined(PIXEL_LIGHT)||defined(GEM)||defined(METAL)
    float3 normal:TEXCOORD2;
#endif
#if defined(GEM)||defined(METAL)
    float3 toEye:TEXCOORD3;
#endif
#ifdef FX_LIGHT
    float3 world:TEXCOORD3;float3 shadowCoord:TEXCOORD4;
#endif
#ifdef SHADOW_FLOOR
    float3 receiver:TEXCOORD2;   // fix 2026-11-11d: the shadow point in world units
#endif
#ifdef LEATHER
#ifndef FX_LIGHT
    float3 world:TEXCOORD3;
#endif
    float3 bind:TEXCOORD5;float3 bindNormal:TEXCOORD6;   // fx 2026-11-11e: the bind pose, for the grain
#endif
};
#if defined(SKIN_LINEAR)
void One(float index,float weight,float4 p,float3 n,inout float3 position,inout float3 normal,inout float3 tint){
    int i=int(index+0.5)*6;float4 a=palette[i+3],b=palette[i+4],c=palette[i+5];
    position+=weight*float3(dot(palette[i],p),dot(palette[i+1],p),dot(palette[i+2],p));
    normal+=weight*float3(dot(a.xyz,n),dot(b.xyz,n),dot(c.xyz,n));tint+=weight*float3(a.w,b.w,c.w);
}
#endif
#ifdef SKIN_AFFINE
float3 RotateBy(float4 q,float3 v){float3 t=2*cross(q.xyz,v);return v+q.w*t+cross(q.xyz,t);}
#endif
Output main(Input input){
    Output output;float3 world=0,normal=0,tint=1;float3 bindNormal=input.normal;
#if defined(SKIN_LINEAR)
    float4 p=float4(input.position,1);tint=0;
    One(input.bones.x,input.weights.x,p,bindNormal,world,normal,tint);
    One(input.bones.y,input.weights.y,p,bindNormal,world,normal,tint);
    One(input.bones.z,input.weights.z,p,bindNormal,world,normal,tint);
    One(input.bones.w,input.weights.w,p,bindNormal,world,normal,tint);
#ifdef SKIN_POSE
    float s=dot(input.side,misc.zw);
    if(s>0){float3 w2=0,n2=0,t2=0;float4 p2=float4(input.position+input.offset,1);
        One(input.bones2.x,input.weights2.x,p2,bindNormal,w2,n2,t2);
        One(input.bones2.y,input.weights2.y,p2,bindNormal,w2,n2,t2);
        One(input.bones2.z,input.weights2.z,p2,bindNormal,w2,n2,t2);
        One(input.bones2.w,input.weights2.w,p2,bindNormal,w2,n2,t2);
        world=lerp(world,w2,s);normal=lerp(normalize(normal),normalize(n2),s);}
#endif
#elif defined(SKIN_AFFINE)
    int i0=int(input.bones.x+0.5)*4,i1=int(input.bones.y+0.5)*4,i2=int(input.bones.z+0.5)*4,i3=int(input.bones.w+0.5)*4;
    float4 q0=palette[i0],q1=palette[i1],q2=palette[i2],q3=palette[i3];
    float w0=input.weights.x,w1=dot(q0,q1)<0?-input.weights.y:input.weights.y,w2=dot(q0,q2)<0?-input.weights.z:input.weights.z,w3=dot(q0,q3)<0?-input.weights.w:input.weights.w;
    float4 real=w0*q0+w1*q1+w2*q2+w3*q3,dual=w0*palette[i0+1]+w1*palette[i1+1]+w2*palette[i2+1]+w3*palette[i3+1];
    float4 s0=input.weights.x*palette[i0+2]+input.weights.y*palette[i1+2]+input.weights.z*palette[i2+2]+input.weights.w*palette[i3+2];
    float4 s1=input.weights.x*palette[i0+3]+input.weights.y*palette[i1+3]+input.weights.z*palette[i2+3]+input.weights.w*palette[i3+3];
    float inverse=1/length(real);real*=inverse;dual*=inverse;dual-=real*dot(real,dual);
    float3 translation=2*(real.w*dual.xyz-dual.w*real.xyz+cross(real.xyz,dual.xyz));
    float3 v=input.position;
    float xx=s0.x,xy=s0.y,xz=s0.z,yy=s0.w,yz=s1.x,zz=s1.y;
    world=RotateBy(real,float3(xx*v.x+xy*v.y+xz*v.z,xy*v.x+yy*v.y+yz*v.z,xz*v.x+yz*v.y+zz*v.z))+translation;
    float c00=yy*zz-yz*yz,c01=xz*yz-xy*zz,c02=xy*yz-xz*yy,c11=xx*zz-xz*xz,c12=xy*xz-xx*yz,c22=xx*yy-xy*xy;
    normal=RotateBy(real,float3(c00*bindNormal.x+c01*bindNormal.y+c02*bindNormal.z,c01*bindNormal.x+c11*bindNormal.y+c12*bindNormal.z,c02*bindNormal.x+c12*bindNormal.y+c22*bindNormal.z));
#else
    world=lerp(input.previous,input.position,misc.y);normal=bindNormal;
#endif
    normal=dot(normal,normal)>1e-20?normalize(normal):float3(0,0,1);   // a bone flattened to nothing: as re3model::Normalize
#ifdef SKIN_DECAL
    float3 out0=world+normal*1.5,toEye=eyeWorld.xyz-out0;float distance=length(toEye);
    world=distance>40?out0+toEye*(10/distance):out0;
#endif
#ifdef SHADOW_FLOOR
    // the vertex slides along the light onto the receiving plane (Y down: above the floor = smaller y); behind it, it stays
    // and is only put on it. For the floor (receiverPlane (0,-1,0,-floorY)) this is the 2026-11-10 formula.
    float height=max(0,shadowPlane.w-world.y);
    float side=dot(receiverPlane.xyz,world)-receiverPlane.w;
    world-=shadowPlane.xyz*(max(0,side)/max(0.05,dot(receiverPlane.xyz,shadowPlane.xyz)));
    world-=receiverPlane.xyz*(dot(receiverPlane.xyz,world)-receiverPlane.w);
#endif
    float4 h=float4(world,1);
    float3 camera=float3(dot(cameraRows[0],h),dot(cameraRows[1],h),dot(cameraRows[2],h));
    output.position=float4(projection.x*camera.x+projection.y*camera.z,projection.z*camera.y+projection.w*camera.z,camera.z-misc.x,camera.z);
    output.uv=input.uv;output.depthOcclusion=float3(camera.z,input.colour.r,dot(wallPlane.xyz,camera)+wallPlane.w);
#ifdef SHADOW_FLOOR
    output.depthOcclusion=float3(height*shadowFeet.w,length(world.xz-shadowFeet.xz),camera.z);   // fix 2026-11-11d: z = camera depth of the floor point
    output.receiver=world+drawOrigin.xyz;
#endif
#ifdef SHADOW_DEPTH
    // orthographic along the light; what lies nearer to the light than the tile's range is flattened onto its near plane
    float lightDepth=dot(shadowRows[2],h);
    output.position=float4(dot(shadowRows[0],h),dot(shadowRows[1],h),clamp(lightDepth,0,1),1);
    output.depthOcclusion=float3(lightDepth,0,0);
#endif
#ifdef FX_LIGHT
    output.world=world;output.shadowCoord=float3(dot(shadowRows[0],h),dot(shadowRows[1],h),dot(shadowRows[2],h));
#endif
#ifdef LEATHER
#ifndef FX_LIGHT
    output.world=world;
#endif
    output.bind=input.position;output.bindNormal=bindNormal;
#endif
    float4 colour=colourTint;
#ifndef SKIN_DECAL
    colour.rgb*=tint;   // damage red belongs to the body, not to its wound decals
#endif
#if defined(PIXEL_LIGHT)||defined(GEM)||defined(METAL)
    output.normal=normal;
#elif !defined(SHADOW_FLOOR)&&!defined(SHADOW_DEPTH)
    if(lightData[10].z>0)colour.rgb*=Shade(normal,input.colour.r);
#endif
#if defined(GEM)||defined(METAL)
    output.toEye=eyeWorld.xyz-world;   // world and eye are both relative to the draw's origin (the rounded eye)
#endif
    output.colour=colour;
    return output;
}
#else
sampler2D albedo:register(s0);
float4 options:register(c0);   // x coverage cutoff, z opacity
float4 range:register(c1);     // x,y camera depth of the flushed buckets
#ifdef GEM
// Crystals round 2026-11-07: a translucent faceted gem (Gem materials, drawn in the AlphaBlend phase in two passes: the far
// facets, seen through the gem, then the near ones). gem.txt values (model_integration.h loadGemMaterials):
float4 gemA:register(c14);   // opacity, glow, specular, shininess
float4 gemB:register(c15);   // rim colour rgb, rim
float4 gemC:register(c16);   // pass (-1 far facets, +1 near facets), inner, gain, saturation
float4 gemD:register(c17);   // inner opacity, fresnel power, inner specular, facets
float4 gemE:register(c18);   // sheen (reflection of a facet facing the eye), environment floor, glint, glint height
float4 main(float4 tint:COLOR0,float2 uv:TEXCOORD0,float2 depthOcclusion:TEXCOORD1,float3 normal:TEXCOORD2,float3 toEye:TEXCOORD3):COLOR0{
    clip(depthOcclusion.x-range.x);clip(range.y-depthOcclusion.x);
    float3 v=normalize(toEye),n=normalize(normal);
    float facing=dot(n,v);
    clip(facing*gemC.x);                      // this pass's side only (far facets face away from the eye)
    float far=facing<0?1:0;
    float3 s=facing<0?-n:n;                   // a far facet is seen from inside the gem
    float4 texel=tex2D(albedo,uv);
    float coverage=texel.a*tint.a;
    clip(coverage-options.x);
    float3 base=texel.rgb*tint.rgb;
    float grey=dot(base,float3(0.299,0.587,0.114));
    base=max(0,lerp(grey,base,1+gemC.w))*gemC.z;
    float3 light=lightData[10].z>0?Shade(s,1):float3(1,1,1);
    float fresnel=pow(1-saturate(abs(facing)),gemD.y);
    float3 spec=0;
    [unroll] for(int i=0;i<3;++i){
        float3 l=lightData[i].xyz;float strength=length(l);
        if(strength>1e-4){float3 h=normalize(l/strength+v);
            spec+=pow(saturate(dot(s,h)),gemA.w)*strength*float3(lightData[3][i],lightData[4][i],lightData[5][i]);}
    }
    spec*=gemA.z*lerp(1,gemD.z,far);
    // glint: a light just above the eye, so the table of a stone turned to the camera flashes as a cut gem does
    float3 g=normalize(v+lightData[8].xyz*gemE.w);
    spec+=gemB.rgb*gemE.z*(1-far)*pow(saturate(dot(s,normalize(g+v))),gemA.w*0.5);
    // cut-stone look: a bright sky / dark floor environment in the reflection, and a tone that changes from facet to facet
    float3 r=reflect(-v,s);
    float env=gemE.y+(1-gemE.y)*pow(saturate((dot(r,lightData[8].xyz)-0.05)/0.6),1.5);
    float tone=1+gemD.w*sin(7*dot(s,float3(0.8,0.3,0.5)))*sin(5*dot(s,float3(-0.2,0.9,0.4)));
    float3 colour=base*(light+gemA.y)*tone*lerp(1,gemC.y,far)+gemB.rgb*gemB.w*(gemE.x+(1-gemE.x)*fresnel)*env*(1-far)+spec;
    float alpha=lerp(lerp(gemA.x,gemD.x,far),1,fresnel*(1-far));
    alpha=saturate(max(alpha,dot(spec,float3(0.333,0.333,0.334))));
    return float4(colour,alpha*coverage*options.z);
}
#elif defined(METAL)
// Door round 2026-11-11: an opaque metal (Metal materials, metal.txt values, model_integration.h loadMetalMaterials). The
// native light still decides how bright the part is: a small diffuse share of the albedo, Blinn-Phong highlights of the
// three native light rows (their colour-matrix column, as the GEM shader), and a sky-above / floor-below environment
// reflection scaled by the light the scene gives a surface facing up and one facing the eye, so a knob in a dark corridor
// stays dark. Highlights and reflection take the albedo colour by 'tint' (brass) with a Schlick fresnel toward white.
float4 wallSide:register(c13);
float4 metalA:register(c14);   // diffuse, specular, shininess, reflect
float4 metalB:register(c15);   // sky rgb, fresnel
float4 metalC:register(c16);   // floor rgb, tint
float4 metalD:register(c17);   // gain, follow, horizon, softness
float4 main(float4 tint:COLOR0,float2 uv:TEXCOORD0,float3 depthOcclusion:TEXCOORD1,float3 normal:TEXCOORD2,float3 toEye:TEXCOORD3):COLOR0{
    clip(depthOcclusion.x-range.x);clip(range.y-depthOcclusion.x);
    clip(depthOcclusion.z*wallSide.x+wallSide.y);
    float4 texel=tex2D(albedo,uv);
    float coverage=texel.a*tint.a;
    clip(coverage-options.x);
    float3 base=texel.rgb*tint.rgb*metalD.x;
    if(lightData[10].z<=0)return float4(base,coverage*options.z);   // unlit material / draw: plain, as before
    float3 n=normalize(normal),v=normalize(toEye);
    float ao=depthOcclusion.y,ndv=saturate(dot(n,v));
    float3 spec=0;
    [unroll] for(int i=0;i<3;++i){
        float3 l=lightData[i].xyz;float strength=length(l);
        if(strength>1e-4){float3 h=normalize(l/strength+v);
            spec+=pow(saturate(dot(n,h)),metalA.z)*saturate(dot(n,l/strength)*4)*strength*float3(lightData[3][i],lightData[4][i],lightData[5][i]);}
    }
    spec*=metalA.y*lightData[7].rgb*(0.35+0.65*ao);
    float3 up=lightData[8].xyz;
    float3 level=0.5*(Shade(up,1)+Shade(v,1));
    float3 r=reflect(-v,n);
    float sky=smoothstep(metalD.z-metalD.w,metalD.z+metalD.w,dot(r,up));
    float3 env=lerp(metalC.rgb,metalB.rgb,sky)*lerp(float3(1,1,1),level,metalD.y)*lerp(1,ao,0.8);
    float3 f0=lerp(float3(1,1,1),saturate(base*1.6),metalC.w);
    float3 fr=f0+(1-f0)*(metalB.w*pow(1-ndv,5));
    float3 colour=base*Shade(n,ao)*metalA.x+(spec+env*metalA.w)*fr;
    return float4(colour,coverage*options.z);
}
#elif defined(SHADOW_FLOOR)
// lighting round 2026-11-10: a caster's silhouette on the floor. r = darkness, g = darkness x (1 - caster height 0..1).
// Blended with MAX, so where several parts cover a floor point the LOWEST counts (run 3: with the highest, the head over
// the feet under a light from above made the whole shadow "high": wide and faint). The composite takes the height back
// from the blurred pair and widens the penumbra with it.
float4 casterOptions:register(c14);  // x darkness of this actor's shadow, y fade start, z 1 / fade length (distance from the feet)
// fix 2026-11-11d (user 11.11, R10105: Dario's body shadow on the beam of the walkway above him): the nearest mask of the
// background at this pixel (ModelRenderer::renderMaskDepth, (slot + slack) / 1020, 1 = no mask); a floor point farther than
// it lies behind that part of the picture and casts nothing there
sampler2D maskDepth:register(s1);
float4 maskOptions:register(c15);    // xy 1 / target size, z camera depth -> mask units, w 1 on
// fix 2026-11-11d (user 11.11: "clip at the floor boundary and draw that part of the shadow on the walls"): the room from its
// collision data (model_integration.h lightReceivers). A floor point casts only on the floor area under the actor's feet
// (a mask of the room in cells, linear between them) where the camera sees it; the part cut off goes to the wall faces in
// passes of its own (the caster laid on the face's plane), each point on its face, above the floor, seen by the camera.
// Block footprints are convex quads (x,z corners in order; a triangle repeats a corner) between a top and a bottom y.
sampler2D floorAreaMask:register(s2);
float4 clipCounts:register(c16);     // y blocks, z pass (0 none, 1 floor, 2 wall face)
float4 clipEye:register(c17);        // xyz the eye (world), w the floor's y
float4 clipFaceA:register(c18);      // wall pass: the face from (x,z) to (z,w)
float4 clipFaceB:register(c19);      // wall pass: x its top y, y its bottom y
float4 floorMap:register(c20);       // the area mask: world x, z of its corner, 1 / its width, 1 / its depth
float4 solidA[32]:register(c64);float4 solidB[32]:register(c96);float4 solidH[32]:register(c128);
float Cross2(float2 u,float2 v){return u.x*v.y-u.y*v.x;}
// whether the segment e -> p runs through the block (its footprint between its heights)
bool BlockHit(float4 a,float4 b,float4 h,float3 e,float3 p){
    float3 d=p-e;float t0=0,t1=1;
    if(abs(d.y)<1e-3){if(e.y<h.x||e.y>h.y)return false;}
    else{float ta=(h.x-e.y)/d.y,tb=(h.y-e.y)/d.y;t0=max(t0,min(ta,tb));t1=min(t1,max(ta,tb));}
    float2 c0=a.xy,c1=a.zw,c2=b.xy,c3=b.zw;
    float o=Cross2(c1-c0,c2-c0)+Cross2(c2-c0,c3-c0)>=0?1:-1;
    float2 cs[4]={c0,c1,c2,c3};
    [unroll] for(int k=0;k<4;++k){float2 c=cs[k],u=cs[k==3?0:k+1]-c;if(dot(u,u)<1e-2)continue;
        float f0=o*Cross2(u,e.xz-c),f1=o*Cross2(u,p.xz-c);
        if(f0<0&&f1<0)return false;
        if(f0<0)t0=max(t0,f0/(f0-f1));else if(f1<0)t1=min(t1,f0/(f0-f1));}
    return t0<t1;
}
float4 main(float4 tint:COLOR0,float2 uv:TEXCOORD0,float3 info:TEXCOORD1,float3 receiver:TEXCOORD2,float2 vpos:VPOS):COLOR0{
    clip(tex2D(albedo,uv).a*tint.a-options.x);
    if(maskOptions.w>0){float m=tex2Dlod(maskDepth,float4((vpos+0.5)*maskOptions.xy,0,0)).r;if(m<0.999)clip(m-info.z*maskOptions.z);}
    if(clipCounts.z>0.5){
        float3 p=receiver;
        if(clipCounts.z<1.5){
            float2 fuv=(p.xz-floorMap.xy)*floorMap.zw;
            clip(min(min(fuv.x,fuv.y),min(1-fuv.x,1-fuv.y)));
            clip(tex2Dlod(floorAreaMask,float4(fuv,0,0)).r-0.5);
        }else{
            float2 ab=clipFaceA.zw-clipFaceA.xy;float t=dot(p.xz-clipFaceA.xy,ab)/max(dot(ab,ab),1);
            clip(t);clip(1-t);clip(p.y-clipFaceB.x);clip(min(clipFaceB.y,clipEye.w)-p.y);
        }
        // seen from the camera: no block between the eye and the point (pulled 60 units toward the eye off its own surface)
        float3 e=clipEye.xyz,q=p+normalize(e-p)*60;
        [loop] for(int k=0;k<32;++k){if(k>=int(clipCounts.y))break;if(BlockHit(solidA[k],solidB[k],solidH[k],e,q))clip(-1);}
    }
    float s=casterOptions.x*(1-saturate((info.y-casterOptions.y)*casterOptions.z));
    return float4(s,s*saturate(1-info.x),0,1);
}
#elif defined(SHADOW_DEPTH)
// the shadow map: depth along the light, 0 at the near plane of the actor's tile
float4 main(float4 tint:COLOR0,float2 uv:TEXCOORD0,float3 info:TEXCOORD1):COLOR0{
    clip(tex2D(albedo,uv).a*tint.a-options.x);
    return float4(saturate(info.x),0,0,1);
}
#elif defined(LEATHER)
// fx 2026-11-11e (user 11.11: "a realistic shader of Nemesis' black leather suit, the second phase with the bare torso and the
// leather trousers too"): Leather materials (leather.txt values, model_integration.h loadLeatherMaterials) - the usual lit
// material (shadow map, flash light: the FX_LIGHT code below, unchanged) plus, where the albedo is dark and colourless
// (the mask: leather; bright and colourless: buckles), a bumped normal (the folds painted into the albedo as a height, a
// fine grain from the grain map in bind space), two Blinn-Phong lobes of the native light rows (a wide sheen and a tight
// coat highlight) with a Schlick fresnel, and a sky-above / floor-below reflection toward grazing angles that follows the
// scene's light level. Skin and cloth keep the plain shading.
float4 wallSide:register(c13);
sampler2D leatherGrain:register(s2);   // rg = slope of the grain height (0.5 = flat), b = the height
float4 leatherEye:register(c20);       // xyz the eye in the draw's space
float4 leatherA:register(c21);         // diffuse, specular, gloss (exponent of the wide lobe), coat (share of the tight lobe)
float4 leatherB:register(c22);         // coat gloss (exponent), fresnel f0, rim (environment reflection), follow
float4 leatherC:register(c23);         // folds (height of the albedo's shading, world units), grain (height), 1 / grain tile, grain gloss
float4 leatherD:register(c24);         // mask: chroma low, chroma high, luma low, luma high
float4 leatherE:register(c25);         // sky rgb, buckle (specular of the metal bits, x)
float4 leatherF:register(c26);         // floor rgb, fill (sheen of a surface facing the eye)
#ifdef FX_LIGHT
sampler2D shadowMap:register(s1);
float4 shadowTile:register(c14);float4 shadowBox:register(c15);float4 shadowOptions:register(c16);float4 shadowKey:register(c17);float4 flashData[2]:register(c18);
float ShadowTap(float2 uv,float depth){return tex2Dlod(shadowMap,float4(uv,0,0)).r<depth?0.0:1.0;}
float ShadowLit(float3 coord,float facing){
    float2 uv=coord.xy*shadowTile.xy+shadowTile.zw;
    if(uv.x<shadowBox.x||uv.y<shadowBox.y||uv.x>shadowBox.z||uv.y>shadowBox.w||coord.z>=1)return 1;
    float depth=coord.z-shadowOptions.y-shadowOptions.z*(1-facing);
    float2 t=uv*shadowOptions.w-0.5,f=frac(t),base=(floor(t)+0.5)*shadowKey.w;
    float wx[4]={1-f.x,1,1,f.x},wy[4]={1-f.y,1,1,f.y};float sum=0;
    [unroll] for(int j=0;j<4;++j){[unroll] for(int i=0;i<4;++i)sum+=wx[i]*wy[j]*ShadowTap(base+float2(i-1,j-1)*shadowKey.w,depth);}
    return sum/9;
}
#endif
float Luma(float3 c){return dot(c,float3(0.299,0.587,0.114));}
float4 main(float4 tint:COLOR0,float2 uv:TEXCOORD0,float3 depthOcclusion:TEXCOORD1,float3 normal:TEXCOORD2,float3 world:TEXCOORD3
#ifdef FX_LIGHT
    ,float3 shadowCoord:TEXCOORD4
#endif
    ,float3 bind:TEXCOORD5,float3 bindNormal:TEXCOORD6):COLOR0{
    clip(depthOcclusion.x-range.x);clip(range.y-depthOcclusion.x);
    clip(depthOcclusion.z*wallSide.x+wallSide.y);
    float4 texel=tex2D(albedo,uv);
    float coverage=texel.a*tint.a;
    clip(coverage-options.x);
    float3 base=texel.rgb*tint.rgb;
    // the mask from the albedo itself: colourless and dark = leather, colourless and bright = a buckle
    float luma=Luma(texel.rgb);
    float chroma=max(texel.r,max(texel.g,texel.b))-min(texel.r,min(texel.g,texel.b));
    float grey=1-smoothstep(leatherD.x,leatherD.y,chroma);
    float leather=grey*(1-smoothstep(leatherD.z,leatherD.w,luma));
    float buckle=grey*smoothstep(leatherD.w,leatherD.w+0.2,luma);
    float mask=saturate(leather+buckle);
    // the bump (Mikkelsen, surface gradient from screen derivatives; all the gradient work before any branch): the folds
    // painted into the albedo and the grain map laid on the three planes of the bind space
    float2 dux=ddx(uv),duy=ddy(uv);
    float hx=Luma(tex2D(albedo,uv+dux).rgb),hy=Luma(tex2D(albedo,uv+duy).rgb);
    float3 bn=abs(normalize(bindNormal));bn*=bn;bn*=bn;bn/=dot(bn,float3(1,1,1));
    float3 gp=bind*leatherC.z,gpx=ddx(gp),gpy=ddy(gp);
    float4 g0=tex2D(leatherGrain,gp.yz),g1=tex2D(leatherGrain,gp.zx),g2=tex2D(leatherGrain,gp.xy);
    float2 s0=(g0.rg-0.5)*bn.x,s1=(g1.rg-0.5)*bn.y,s2=(g2.rg-0.5)*bn.z;
    float grainHeight=g0.b*bn.x+g1.b*bn.y+g2.b*bn.z;
    float dBs=leatherC.x*(hx-luma)+leatherC.y*192*(dot(s0,gpx.yz)+dot(s1,gpx.zx)+dot(s2,gpx.xy));
    float dBt=leatherC.x*(hy-luma)+leatherC.y*192*(dot(s0,gpy.yz)+dot(s1,gpy.zx)+dot(s2,gpy.xy));
    float3 n0=normalize(normal);
    float3 dpx=ddx(world),dpy=ddy(world);
    float3 r1=cross(dpy,n0),r2=cross(n0,dpx);float det=dot(dpx,r1);
    float3 bumped=abs(det)*n0-sign(det)*mask*(dBs*r1+dBt*r2);
    float3 nb=dot(bumped,bumped)>1e-20?normalize(bumped):n0;
    nb=normalize(lerp(n0,nb,saturate(dot(nb,n0)*4+1)));   // never turned away from the surface
    float3 result=base;
    if(lightData[10].z>0){
        float ao=depthOcclusion.y;
        float3 keep=1,extra=0,v=normalize(leatherEye.xyz-world),flashGloss=0;
        float e1=max(1,leatherA.z*(1+leatherC.w*(grainHeight*2-1))),e2=max(1,leatherB.x*(1+leatherC.w*(grainHeight*2-1)));
#ifdef FX_LIGHT
        if(shadowOptions.x>0){
            float3 key=lightData[0].xyz*shadowKey.x+lightData[1].xyz*shadowKey.y+lightData[2].xyz*shadowKey.z;
            float facing=dot(n0,key)/max(length(key),1e-4);
            if(facing>0)keep-=shadowKey.xyz*(shadowOptions.x*(1-ShadowLit(shadowCoord,saturate(facing))));
        }
        if(flashData[1].w>0){
            float3 to=flashData[0].xyz-world;float d=length(to),a=saturate(1-d*flashData[0].w);
            extra=flashData[1].rgb*(a*a*saturate((dot(nb,to)/max(d,1)+0.3)/1.3));
            float3 hf=normalize(to/max(d,1)+v);float nf=saturate(dot(nb,hf));
            flashGloss=flashData[1].rgb*(a*a*saturate(dot(nb,to)/max(d,1)*4)*((1-leatherA.w)*pow(nf,e1)*(e1+8)*0.125+leatherA.w*pow(nf,e2)*(e2+8)*0.125));
        }
#endif
        float3 plain=ShadeLit(n0,ao,keep,extra),lit=ShadeLit(nb,ao,keep,extra);
        float ndv=saturate(dot(nb,v));
        float3 spec=0;
        [unroll] for(int i=0;i<3;++i){
            float3 l=lightData[i].xyz;float strength=length(l);
            if(strength>1e-4){float3 ld=l/strength,h=normalize(ld+v);float ndh=saturate(dot(nb,h));
                float lobe=(1-leatherA.w)*pow(ndh,e1)*(e1+8)*0.125+leatherA.w*pow(ndh,e2)*(e2+8)*0.125;
                float fh=leatherB.y+(1-leatherB.y)*pow(1-saturate(dot(v,h)),5);
                spec+=lobe*fh*saturate(dot(nb,ld)*4)*saturate(dot(n0,ld)*4+0.5)*strength*keep[i]*float3(lightData[3][i],lightData[4][i],lightData[5][i]);}
        }
        spec=(spec*lightData[9].y+flashGloss*leatherB.y)*lightData[7].rgb*(0.3+0.7*ao);
        float3 up=lightData[8].xyz,level=0.5*(Shade(up,1)+Shade(v,1));
        float3 r=reflect(-v,nb);
        float sky=smoothstep(-0.25,0.55,dot(r,up));
        float3 env=lerp(leatherF.rgb,leatherE.rgb,sky)*lerp(float3(1,1,1),level,leatherB.w)*lerp(1,ao,0.8);
        float fr=leatherB.y+(1-leatherB.y)*pow(1-ndv,5);
        float3 sheen=level*(leatherF.w*pow(ndv,max(1,e1*0.35))*lerp(1,ao,0.8));
        float3 gloss=(spec*leatherA.y+env*(leatherB.z*fr)+sheen)*(leather+buckle*leatherE.w);
        result=base*lerp(plain,lit*lerp(1,leatherA.x,leather),mask)+gloss*tint.rgb;
    }
    return float4(result,coverage*options.z);
}
#else
float4 wallSide:register(c13);   // fix 2026-11-08: x side kept (+1 room, -1 beyond the wall, 0 all), y 1 when off
#ifdef FX_LIGHT
// lighting round 2026-11-10: the actor's shadow map takes the key light away where something stands between the pixel and
// the light (its own body, its weapon, a neighbour); the muzzle flash of this build lights the pixel as a point light.
sampler2D shadowMap:register(s1);
float4 shadowTile:register(c14);     // atlas uv = coord.xy * tile.xy + tile.zw
float4 shadowBox:register(c15);      // the tile in the atlas less a border: min uv, max uv
float4 shadowOptions:register(c16);  // x strength (0 = no map), y bias, z slope bias (both in map depth), w atlas size
float4 shadowKey:register(c17);      // xyz 1 for the light row the map belongs to, w 1 / atlas size
float4 flashData[2]:register(c18);   // [0] xyz position in the draw's space, w 1 / radius; [1] rgb colour x intensity, w on
float ShadowTap(float2 uv,float depth){return tex2Dlod(shadowMap,float4(uv,0,0)).r<depth?0.0:1.0;}
float ShadowLit(float3 coord,float facing){
    float2 uv=coord.xy*shadowTile.xy+shadowTile.zw;
    if(uv.x<shadowBox.x||uv.y<shadowBox.y||uv.x>shadowBox.z||uv.y>shadowBox.w||coord.z>=1)return 1;
    float depth=coord.z-shadowOptions.y-shadowOptions.z*(1-facing);
    float2 t=uv*shadowOptions.w-0.5,f=frac(t),base=(floor(t)+0.5)*shadowKey.w;
    float wx[4]={1-f.x,1,1,f.x},wy[4]={1-f.y,1,1,f.y};float sum=0;
    [unroll] for(int j=0;j<4;++j){[unroll] for(int i=0;i<4;++i)sum+=wx[i]*wy[j]*ShadowTap(base+float2(i-1,j-1)*shadowKey.w,depth);}
    return sum/9;
}
#endif
float4 main(float4 tint:COLOR0,float2 uv:TEXCOORD0,float3 depthOcclusion:TEXCOORD1
#ifdef PIXEL_LIGHT
    ,float3 normal:TEXCOORD2
#endif
#ifdef FX_LIGHT
    ,float3 world:TEXCOORD3,float3 shadowCoord:TEXCOORD4
#endif
    ):COLOR0{
    clip(depthOcclusion.x-range.x);clip(range.y-depthOcclusion.x);
    clip(depthOcclusion.z*wallSide.x+wallSide.y);
    float4 texel=tex2D(albedo,uv);
    float coverage=texel.a*tint.a;
    clip(coverage-options.x);
    float3 colour=tint.rgb;
#ifdef FX_LIGHT
    if(lightData[10].z>0){
        float3 n=normalize(normal),keep=1,extra=0;
        if(shadowOptions.x>0){
            float3 key=lightData[0].xyz*shadowKey.x+lightData[1].xyz*shadowKey.y+lightData[2].xyz*shadowKey.z;
            float facing=dot(n,key)/max(length(key),1e-4);
            if(facing>0)keep-=shadowKey.xyz*(shadowOptions.x*(1-ShadowLit(shadowCoord,saturate(facing))));
        }
        if(flashData[1].w>0){
            // wrapped: the flash is a small bright volume right at the hands, plain Lambert leaves black terminators there
            float3 to=flashData[0].xyz-world;float d=length(to),a=saturate(1-d*flashData[0].w);
            extra=flashData[1].rgb*(a*a*saturate((dot(n,to)/max(d,1)+0.3)/1.3));
        }
        colour*=ShadeLit(n,depthOcclusion.y,keep,extra);
    }
#elif defined(PIXEL_LIGHT)
    if(lightData[10].z>0)colour*=Shade(normal,depthOcclusion.y);
#endif
    return float4(texel.rgb*colour,coverage*options.z);
}
#endif
#endif
)HLSL";}

// ---------------------------------------------------------------- fx 2026-11-11e: volumetric slime (Nemesis' hand)
// User 11.11: "redo the bubbling purple slime out of his right fist, now it looks too much like a flat picture - make it look
// cool and volumetric". The slime is a real surface: a smooth union of round cones (a sphere, a drop with a tail, a strand)
// found per pixel by sphere tracing inside a screen rectangle, with its own depth (it stands in front of and behind the HD
// models pixel by pixel), a normal, crevice shading between merged blobs, light through its thin parts, wet highlights of the
// native lights and a fresnel reflection. Everything is in camera space; fx_slime_sim.h makes the primitives.
struct SlimePrim {float a[4]{},b[4]{},c[4]{};};   // a: xyz + radius, b: xyz + radius (a = b: a sphere), c: x palette (0 purple .. 1 grey-beige), y hollow (a bubble), z froth, w calm (a flat film: no lumps)
constexpr unsigned kSlimeMaxPrims=24;
struct SlimeBatch {
    std::vector<SlimePrim> now,before;    // this build and the one before, same order (the in-between frame blends them)
    float rect[4]{};                      // viewport pixels the batch can touch (both builds)
    float bound[4]{},boundBefore[4]{};    // bounding sphere
    float ray[4]{};                       // x/z = px * [0] + [1], y/z = py * [2] + [3] (px, py: viewport pixels)
    float nearPlane=16,time=0,timeBefore=0,sharpness=0.055f,noise=22.f,noiseScale=1.f/260.f,opacity=1;
    // fx 2026-11-11f: gloss; milk = how soon the body turns cloudy with depth (1 / units); shoulder of the tone curve; absorption (1 / units)
    // fx 2026-11-11f2 (user: "can this slime be made darker?"): absorption 1 / 180 -> 1 / 130
    float look[4]={0.7f,1.f/400.f,0.4f,1.f/130.f};
    // froth amount, film of a bubble at its rim, height of the froth's small domes (units), brightness of the slime's own colour
    // (not of its glints; fx_slime_sim.h DefaultTones sets it: pilot.ini nemesis_slime_brightness)
    float look2[4]={1.f,0.8f,7.f,1.f};
    float refraction=22.f;                // how far (target pixels) the picture behind is shifted by the surface's slope
    float floorPlane[4]={0,0,0,1.e6f};    // dot(n, p) + w >= 0 above the floor; n = 0: no floor
    float lightDir[3][4]{},lightColour[3][4]{},ambient[4]{},up[4]={0,-1,0,0};
    float tones[6][4]{};                  // purple: deep, body, thin; grey-beige: deep, body, thin
    std::uint64_t epoch=0;bool fresh=false;
};
// fx 2026-11-11f (user: "it looks a little like plastic ... can it be runnier, more transparent and with some foam as in the
// original?"): the body is a liquid now. The renderer hands the shader a copy of the picture behind the batch (sceneMap); what
// the view ray crosses inside the body is measured (five samples: solid slime in full, the inside of a bubble hardly at all) and
// the picture shows through it, shifted by the surface's slope, tinted by absorption and clouded with depth; a bubble is a film
// (clear in the middle, bright at its rim); froth - patches of small cells with dark seams and a glint each - lies where the
// primitives ask for it and in the creases. Without the copy (scene.w = 0) the same is blended by alpha.
inline const char* SlimeSource(){return R"HLSL(
#define N SLIME_PRIMS
float4 rayA:register(c0);
float4 bound:register(c1);
float4 misc:register(c2);         // x time (s), y near plane, z sharpness of the union, w noise amplitude
float4 floorPlane:register(c3);
float4 lightDir[3]:register(c4);  // toward each light x its strength
float4 lightCol[3]:register(c7);
float4 ambient:register(c10);     // rgb, w opacity
float4 upDir:register(c11);       // xyz up, w noise tiles per unit
float4 tones[6]:register(c12);
float4 look:register(c18);        // x gloss (highlights x), y milk (1 / units), z shoulder of the tone curve, w absorption (1 / units)
float4 scene:register(c19);       // xy 1 / target size, z refraction (pixels), w 1 = sceneMap holds the picture behind
float4 look2:register(c20);       // x froth amount, y film of a bubble, z froth dome height (units), w brightness of its own colour
float4 prims[N*3]:register(c21);
sampler3D noiseMap:register(s0);  // r wide lumps, g small lumps, b distance to the nearest froth cell centre, a seam between cells
sampler2D sceneMap:register(s1);
// attr: x palette, y hollow, z froth, w calm
float Body(float3 p,out float4 attr,out float radius){
    float sum=0,wr=0;float4 wa=0;
    for(int i=0;i<N;++i){
        float4 a=prims[i*3],b=prims[i*3+1];
        float3 pa=p-a.xyz,ba=b.xyz-a.xyz;
        float h=saturate(dot(pa,ba)/max(dot(ba,ba),1e-3));
        float r=lerp(a.w,b.w,h);
        float e=exp(min(40,-misc.z*(length(pa-ba*h)-r)));
        sum+=e;wa+=e*prims[i*3+2];wr+=e*r;
    }
    float inv=1/max(sum,1e-30);
    attr=wa*inv;radius=wr*inv;
    return -log(max(sum,1e-30))/misc.z;
}
// the noise rides the first (largest) primitive, so the pattern stays on the mass when the hand moves, and streams slowly;
// a calm film (a puddle) keeps it still in the room
// (two patterns mixed by value: mixing their places would sweep one pattern through the other where a splash meets its puddle)
float4 Noise(float3 p,float calm){
    float4 moving=tex3Dlod(noiseMap,float4((p-prims[0].xyz)*upDir.w+misc.x*float3(0.02,-0.07,0.03),0));
    float4 still=tex3Dlod(noiseMap,float4(p*upDir.w,0));
    return lerp(moving,still,saturate(calm*1.7));
}
float Map(float3 p){
    float4 at;float r;float d=Body(p,at,r);
    float4 n=Noise(p,at.w);
    float amp=min(misc.w,r*0.24)*(1-0.9*at.w);
    d+=(n.r-0.5)*amp*1.2+(n.g-0.5)*amp*0.3;
    // froth: a small dome over every cell
    float dome=1-n.b;d-=at.z*(1-at.w)*look2.z*dome*dome*saturate(r*0.06);
    return max(d,-(dot(floorPlane.xyz,p)+floorPlane.w));
}
// (real loops, not unrolled copies: the largest variant has to stay under 4096 instruction slots - NVIDIA's ps_3_0 limit)
float3 Normal(float3 p){
    const float e=2.4;float3 g=0;
    [loop] for(int j=0;j<4;++j){
        float3 k=float3((j==0||j==3)?1:-1,j>=2?1:-1,(j==1||j==3)?1:-1);   // the four corners of a tetrahedron
        g+=k*Map(p+k*e);
    }
    return dot(g,g)>1e-12?normalize(g):float3(0,0,-1);
}
struct Out {float4 colour:COLOR0;float depth:DEPTH;};
Out main(float2 vpos:VPOS){
    Out o;
    float3 rd=normalize(float3(vpos.x*rayA.x+rayA.y,vpos.y*rayA.z+rayA.w,1));
    float b=dot(bound.xyz,rd),h=b*b-dot(bound.xyz,bound.xyz)+bound.w*bound.w;
    float s=sqrt(max(h,0)),t=max(b-s,misc.y/rd.z),tEnd=b+s,hit=0;
    if(h>0){
        // to the surface, then onto it (a flat film seen at a low angle shows the stop distance as rings otherwise)
        [loop] for(int i=0;i<96;++i){
            float d=Map(rd*t);
            if(d<0.06){hit=1;break;}
            t+=d*(d<0.6?0.85:0.62);
            if(t>tEnd)break;
        }
    }
    clip(hit-0.5);
    float3 colour=0;float alpha=0,z=misc.y+1;
    if(hit>0.5){
        float3 p=rd*t;z=p.z;
        float3 n=Normal(p),v=-rd;
        float ndv=saturate(dot(n,v));
        float4 at,a1;float radius,r1;Body(p,at,radius);
        float occl=saturate(Body(p+n*16,a1,r1)/16);      // 1 open .. 0 deep in a crease between merged blobs
        // what the ray crosses behind the surface (units): solid slime in full, the inside of a bubble hardly, nothing under the floor
        // (samples at 6, 13, 29, 64, 140 units behind the surface, each standing for 1.25 x its own depth)
        // (the floor ends the body exactly: where the ray meets it is known, so a thin film has no steps in its depth)
        float tau=0,thick=0,deepAt=6,floorAt=(dot(floorPlane.xyz,p)+floorPlane.w)/max(1e-3,-dot(floorPlane.xyz,rd));
        [loop] for(int j=0;j<5;++j){
            float3 q=p+rd*deepAt;float d=Body(q,a1,r1),len=deepAt*1.25;
            float inside=saturate(0.5-d*0.12)*saturate((floorAt-deepAt)/len+0.5);
            thick+=inside*len;tau+=inside*len*(1-0.94*a1.y)*(1+38*a1.w);   // a film on the floor is thin but dense
            deepAt*=2.2;
        }
        float3 deep=lerp(tones[0].rgb,tones[3].rgb,at.x),body=lerp(tones[1].rgb,tones[4].rgb,at.x),thin=lerp(tones[2].rgb,tones[5].rgb,at.x);
        float4 nz=Noise(p,at.w);
        // the froth's own fine bubbles
        float4 nf=tex3Dlod(noiseMap,float4((p-prims[0].xyz)*upDir.w*2.3+misc.x*float3(0.02,-0.05,0.03)+0.19,0));
        float2 wide=tex3Dlod(noiseMap,float4((p-prims[0].xyz)*upDir.w*0.41+misc.x*float3(-0.03,0.02,0.04)+0.37,0)).rg;
        float3 light=ambient.rgb*(0.55+0.45*saturate(dot(n,upDir.xyz)*0.5+0.5)),spec=0,through=0;
        [unroll] for(int k=0;k<3;++k){
            float3 l=lightDir[k].xyz;float ls=length(l);
            if(ls>1e-4){l/=ls;float nl=dot(n,l),w=saturate((nl+0.5)/1.5);
                light+=lightCol[k].rgb*(ls*w*w);
                through+=lightCol[k].rgb*(ls*saturate(0.3-nl*0.7));
                float3 hv=normalize(l+v);float nh=saturate(dot(n,hv));
                spec+=lightCol[k].rgb*(ls*saturate(nl*4+0.8)*(pow(nh,360)*6+pow(nh,70)*0.12));}
        }
        // a light above and behind the eye: it reads wet in flat light too
        {float3 hv=normalize(normalize(v+upDir.xyz*0.8)+v);float nh=saturate(dot(n,hv));
            spec+=(ambient.rgb+light*0.25)*(pow(nh,260)*1.8+pow(nh,40)*0.06);}
        float cavity=0.35+0.65*occl;
        float milk=1-exp(-tau*look.y*(1+2.5*at.x));     // 0 clear .. 1 cloudy (the grey-beige one is mucus: cloudy sooner)
        float3 albedo=lerp(thin,body,saturate(0.2+milk*1.3))*(0.85+0.3*nz.g);
        albedo=lerp(albedo,deep,0.45*saturate(milk*1.6-0.75));
        // the cloudy body: lit from outside, a little light held inside, light passing through its thin parts
        float3 bodyLit=albedo*(light*cavity+through*(1-milk)*0.8)+body*(ambient.rgb+light*0.2)*(0.24*milk);
        // the picture behind, shifted by the slope of the surface and tinted by what the body absorbs
        float3 sigma=(1.1-body/max(max(body.r,body.g),max(body.b,1e-3)))*look.w;
        float3 clear=exp(-tau*sigma);
        // (the shift grows with what the ray crosses, 0.4 of it as seen on screen, scene.z pixels at most: a thin film hardly
        // moves the floor under it, a thick drop is a lens)
        float2 uv=(vpos+0.5-n.xy*min(scene.z,thick*0.4/(rayA.x*p.z)))*scene.xy;
        float3 behind=tex2Dlod(sceneMap,float4(uv,0,0)).rgb*scene.w;
        // a bubble: a film, bright where it is seen edge-on
        float film=at.y*look2.y*pow(1-ndv,1.7);
        float3 filmCol=lerp(thin,1,0.14)*(light*0.9+ambient.rgb*0.4);   // (fx 2026-11-11f2: darker - the film and the froth are less white)
        // froth: patches where the primitives ask for it and in the creases; small cells with dark seams and a glint each
        float want=saturate(at.z*look2.x);
        float patch=smoothstep(0.62-0.45*want,0.72-0.45*want,wide.r*0.75+wide.g*0.25+(1-occl)*0.25)*saturate(want*5)*(1-at.w*0.6);
        float seam=smoothstep(0.02,0.22,nf.a),bead=1-nf.b;
        float3 froth=lerp(thin,1,0.22)*(light*0.9+ambient.rgb*0.45)*(0.7+0.14*seam+0.16*bead*bead)*(0.55+0.45*cavity)+(light+ambient.rgb)*(0.22*pow(saturate(1-nf.b*1.45),10));
        float frothA=patch*0.92;
        float seen=(1-milk)*(1-film)*(1-frothA);       // how much of the picture behind is left
        float fres=0.03+0.97*pow(1-ndv,5);
        float3 r=reflect(rd,n);float sky=smoothstep(-0.25,0.6,dot(r,upDir.xyz));
        float3 env=lerp(float3(0.04,0.035,0.045),float3(0.62,0.6,0.72),sky)*(ambient.rgb*1.5+light*0.3);
        float3 own=((bodyLit*milk*(1-film)+filmCol*film)*(1-frothA)+froth*frothA)*look2.w+(spec*look.x+env*(fres*look2.w))*cavity*(1-0.6*frothA);
        // the soft shoulder of the models' light: bright rooms do not burn it out
        float knee=1-look.z;own=lerp(own,knee+look.z*(1-exp((knee-own)/look.z)),step(knee,own));
        if(scene.w>0.5){colour=own+behind*clear*seen;alpha=ambient.w;}
        else{float left=seen*dot(clear,float3(0.3,0.5,0.2));alpha=ambient.w*(1-left);colour=own/max(1-left,0.04);}
    }
    o.colour=float4(colour,alpha);o.depth=saturate(1-misc.y/z);
    return o;
}
)HLSL";}
class Shaders {
    // variants: 0 vertex light, 1 pixel light, 2 pixel light + shadow map + flash light (lighting round 2026-11-10),
    // 3 floor shadow caster, 4 shadow map caster
    IDirect3DVertexShader9* vertex_[5][5]{};IDirect3DPixelShader9* pixel_[5]{};bool failed_[5]{};
    IDirect3DVertexShader9* gemVertex_=nullptr;IDirect3DPixelShader9* gemPixel_=nullptr;bool gemFailed_=false;   // crystals round 2026-11-07
    IDirect3DVertexShader9* metalVertex_=nullptr;IDirect3DPixelShader9* metalPixel_=nullptr;bool metalFailed_=false;   // door round 2026-11-11
    IDirect3DVertexShader9* leatherVertex_[2][2]{};IDirect3DPixelShader9* leatherPixel_[2]{};bool leatherFailed_=false;   // fx 2026-11-11e: [linear, linear + pose][pixel light, + shadow map / flash]
    IDirect3DVertexDeclaration9* skin_=nullptr,*pose_=nullptr,*world_=nullptr;
    using Compile=HRESULT(WINAPI*)(LPCVOID,SIZE_T,LPCSTR,const D3D_SHADER_MACRO*,ID3DInclude*,LPCSTR,LPCSTR,UINT,UINT,ID3DBlob**,ID3DBlob**);
    HMODULE library_=nullptr;Compile compile_=nullptr;
    std::string lastError_;
    bool load(){
        if(compile_)return true;
        wchar_t path[MAX_PATH]{};const UINT size=GetSystemDirectoryW(path,MAX_PATH);const wchar_t suffix[]=L"\\d3dcompiler_47.dll";
        if(!size||size+_countof(suffix)>MAX_PATH)return false;std::memcpy(path+size,suffix,sizeof(suffix));
        library_=LoadLibraryExW(path,nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);if(!library_)return false;
        compile_=reinterpret_cast<Compile>(GetProcAddress(library_,"D3DCompile"));return compile_!=nullptr;
    }
    HRESULT build(const std::vector<D3D_SHADER_MACRO>& macros,const char* profile,ID3DBlob** code){
        if(!load())return E_NOINTERFACE;ID3DBlob* errors=nullptr;const char* source=ShaderSource();
        const HRESULT hr=compile_(source,std::strlen(source),"RE3 GPU model",macros.data(),nullptr,"main",profile,D3DCOMPILE_OPTIMIZATION_LEVEL3,0,code,&errors);
        if(errors){lastError_.assign(static_cast<const char*>(errors->GetBufferPointer()),errors->GetBufferSize());errors->Release();}
        return hr;
    }
public:
    const std::string& lastError()const{return lastError_;}
    ~Shaders(){release();if(library_)FreeLibrary(library_);}
    void release(){for(auto& row:vertex_)for(auto& v:row)SafeRelease(v);for(auto& p:pixel_)SafeRelease(p);SafeRelease(skin_);SafeRelease(pose_);SafeRelease(world_);
        SafeRelease(gemVertex_);SafeRelease(gemPixel_);gemFailed_=false;SafeRelease(metalVertex_);SafeRelease(metalPixel_);metalFailed_=false;for(auto& f:failed_)f=false;
        for(auto& row:leatherVertex_)for(auto& v:row)SafeRelease(v);for(auto& p:leatherPixel_)SafeRelease(p);leatherFailed_=false;}
    static void variantMacros(std::vector<D3D_SHADER_MACRO>& macros,unsigned variant){
        if(variant==1||variant==2)macros.push_back({"PIXEL_LIGHT","1"});
        if(variant==2)macros.push_back({"FX_LIGHT","1"});
        if(variant==3)macros.push_back({"SHADOW_FLOOR","1"});
        if(variant==4)macros.push_back({"SHADOW_DEPTH","1"});
    }
    // a variant whose shaders did not build is not tried again on this device (lighting variants: the caller falls back)
    bool variantFailed(unsigned variant)const{return variant<5&&failed_[variant];}
    HRESULT declarations(IDirect3DDevice9* d){
        if(skin_)return S_OK;
        const D3DVERTEXELEMENT9 skin[]={{0,0,D3DDECLTYPE_FLOAT3,0,D3DDECLUSAGE_POSITION,0},{0,12,D3DDECLTYPE_FLOAT3,0,D3DDECLUSAGE_NORMAL,0},{0,24,D3DDECLTYPE_FLOAT2,0,D3DDECLUSAGE_TEXCOORD,0},
            {0,32,D3DDECLTYPE_UBYTE4,0,D3DDECLUSAGE_BLENDINDICES,0},{0,36,D3DDECLTYPE_FLOAT4,0,D3DDECLUSAGE_BLENDWEIGHT,0},{0,52,D3DDECLTYPE_D3DCOLOR,0,D3DDECLUSAGE_COLOR,0},D3DDECL_END()};
        const D3DVERTEXELEMENT9 pose[]={{0,0,D3DDECLTYPE_FLOAT3,0,D3DDECLUSAGE_POSITION,0},{0,12,D3DDECLTYPE_FLOAT3,0,D3DDECLUSAGE_NORMAL,0},{0,24,D3DDECLTYPE_FLOAT2,0,D3DDECLUSAGE_TEXCOORD,0},
            {0,32,D3DDECLTYPE_UBYTE4,0,D3DDECLUSAGE_BLENDINDICES,0},{0,36,D3DDECLTYPE_FLOAT4,0,D3DDECLUSAGE_BLENDWEIGHT,0},{0,52,D3DDECLTYPE_D3DCOLOR,0,D3DDECLUSAGE_COLOR,0},
            {1,0,D3DDECLTYPE_UBYTE4,0,D3DDECLUSAGE_BLENDINDICES,1},{1,4,D3DDECLTYPE_FLOAT4,0,D3DDECLUSAGE_BLENDWEIGHT,1},{1,20,D3DDECLTYPE_FLOAT3,0,D3DDECLUSAGE_TEXCOORD,1},{1,32,D3DDECLTYPE_FLOAT2,0,D3DDECLUSAGE_TEXCOORD,2},D3DDECL_END()};
        const D3DVERTEXELEMENT9 world[]={{0,0,D3DDECLTYPE_FLOAT3,0,D3DDECLUSAGE_POSITION,0},{0,12,D3DDECLTYPE_FLOAT3,0,D3DDECLUSAGE_NORMAL,0},{0,24,D3DDECLTYPE_FLOAT2,0,D3DDECLUSAGE_TEXCOORD,0},
            {0,32,D3DDECLTYPE_FLOAT3,0,D3DDECLUSAGE_TEXCOORD,1},{0,52,D3DDECLTYPE_D3DCOLOR,0,D3DDECLUSAGE_COLOR,0},D3DDECL_END()};
        HRESULT hr=d->CreateVertexDeclaration(skin,&skin_);if(SUCCEEDED(hr))hr=d->CreateVertexDeclaration(pose,&pose_);if(SUCCEEDED(hr))hr=d->CreateVertexDeclaration(world,&world_);return hr;
    }
    IDirect3DVertexDeclaration9* declaration(Skin skin)const{return skin==Skin::LinearPose?pose_:skin==Skin::World?world_:skin_;}
    IDirect3DVertexShader9* vertex(IDirect3DDevice9* d,Skin skin,unsigned variant){
        if(variant>4||failed_[variant])return nullptr;
        auto& slot=vertex_[unsigned(skin)][variant];if(slot)return slot;
        std::vector<D3D_SHADER_MACRO> macros{{"VERTEX_SHADER","1"},{"LIGHT_REGISTER","c5"}};variantMacros(macros,variant);
        if(skin==Skin::Affine)macros.push_back({"SKIN_AFFINE","1"});else if(skin==Skin::World)macros.push_back({"SKIN_WORLD","1"});else macros.push_back({"SKIN_LINEAR","1"});
        if(skin==Skin::LinearPose)macros.push_back({"SKIN_POSE","1"});if(skin==Skin::Decal)macros.push_back({"SKIN_DECAL","1"});
        macros.push_back({nullptr,nullptr});ID3DBlob* code=nullptr;
        if(SUCCEEDED(build(macros,"vs_3_0",&code)))d->CreateVertexShader(static_cast<const DWORD*>(code->GetBufferPointer()),&slot);
        if(code)code->Release();if(!slot&&variant>=2)failed_[variant]=true;return slot;
    }
    IDirect3DPixelShader9* pixel(IDirect3DDevice9* d,unsigned variant){
        if(variant>4||failed_[variant])return nullptr;
        auto& slot=pixel_[variant];if(slot)return slot;
        std::vector<D3D_SHADER_MACRO> macros{{"LIGHT_REGISTER","c2"}};variantMacros(macros,variant);macros.push_back({nullptr,nullptr});ID3DBlob* code=nullptr;
        if(SUCCEEDED(build(macros,"ps_3_0",&code)))d->CreatePixelShader(static_cast<const DWORD*>(code->GetBufferPointer()),&slot);
        if(code)code->Release();if(!slot&&variant>=2)failed_[variant]=true;return slot;
    }
    // lighting round 2026-11-10: small pixel shaders for pre-transformed quads (blur, composite, light sprites)
    IDirect3DPixelShader9* quadShader(IDirect3DDevice9* d,const char* source,IDirect3DPixelShader9*& slot,bool& failed,const D3D_SHADER_MACRO* macros=nullptr){
        if(slot||failed)return slot;
        if(!load()){failed=true;return nullptr;}
        ID3DBlob* code=nullptr,*errors=nullptr;
        const HRESULT hr=compile_(source,std::strlen(source),"RE3 lighting quad",macros,nullptr,"main","ps_3_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code,&errors);
        if(errors){lastError_.assign(static_cast<const char*>(errors->GetBufferPointer()),errors->GetBufferSize());errors->Release();}
        if(SUCCEEDED(hr)&&code)d->CreatePixelShader(static_cast<const DWORD*>(code->GetBufferPointer()),&slot);
        if(code)code->Release();if(!slot)failed=true;return slot;
    }
    // fx 2026-11-11e: one source compiled once per value of one define, on a worker thread (D3DCompile is free-threaded; the
    // large slime shaders take a second each). state: 0 running, 1 done, -1 failed. The device thread makes the shader objects.
    struct Background {std::atomic<int> state{0};std::vector<std::vector<std::uint8_t>> code;std::string error;};
    std::shared_ptr<Background> compileInBackground(const char* source,const char* define,std::vector<std::string> values,const char* profile){
        auto result=std::make_shared<Background>();
        if(!load()){result->state=-1;result->error="d3dcompiler_47.dll unavailable";return result;}
        const Compile compile=compile_;const std::string name=define,target=profile;
        std::thread([result,compile,source,name,target,values]{
            for(const auto& value:values){
                const D3D_SHADER_MACRO macros[2]={{name.c_str(),value.c_str()},{nullptr,nullptr}};ID3DBlob* code=nullptr,*errors=nullptr;
                // optimization level 1: the same code as level 3 here, compiled five times faster
                const HRESULT hr=compile(source,std::strlen(source),"RE3 background shader",macros,nullptr,"main",target.c_str(),0,0,&code,&errors);
                if(FAILED(hr)||!code){if(errors)result->error.assign(static_cast<const char*>(errors->GetBufferPointer()),errors->GetBufferSize());else result->error="compile failed";
                    if(errors)errors->Release();if(code)code->Release();result->state=-1;return;}
                const auto* bytes=static_cast<const std::uint8_t*>(code->GetBufferPointer());result->code.emplace_back(bytes,bytes+code->GetBufferSize());
                if(errors)errors->Release();code->Release();
            }
            result->state=1;
        }).detach();
        return result;
    }
    void setLastError(const std::string& text){lastError_=text;}
    // Crystals round 2026-11-07: the GEM pair (linear skin only). False after a failed build: Gem materials then draw as plain
    // AlphaBlend materials (failed() reports it once); the rest of the GPU path is not affected.
    bool gem(IDirect3DDevice9* d,IDirect3DVertexShader9*& vertex,IDirect3DPixelShader9*& pixel){
        if(!gemVertex_&&!gemFailed_){
            std::vector<D3D_SHADER_MACRO> macros{{"VERTEX_SHADER","1"},{"LIGHT_REGISTER","c5"},{"SKIN_LINEAR","1"},{"GEM","1"},{nullptr,nullptr}};ID3DBlob* code=nullptr;
            if(SUCCEEDED(build(macros,"vs_3_0",&code)))d->CreateVertexShader(static_cast<const DWORD*>(code->GetBufferPointer()),&gemVertex_);
            if(code){code->Release();code=nullptr;}
            std::vector<D3D_SHADER_MACRO> pixelMacros{{"LIGHT_REGISTER","c2"},{"GEM","1"},{nullptr,nullptr}};
            if(gemVertex_&&SUCCEEDED(build(pixelMacros,"ps_3_0",&code)))d->CreatePixelShader(static_cast<const DWORD*>(code->GetBufferPointer()),&gemPixel_);
            if(code)code->Release();
            if(!gemVertex_||!gemPixel_){SafeRelease(gemVertex_);SafeRelease(gemPixel_);gemFailed_=true;}
        }
        vertex=gemVertex_;pixel=gemPixel_;return gemVertex_&&gemPixel_;
    }
    bool gemFailed()const{return gemFailed_;}
    // Door round 2026-11-11: the METAL pair (linear skin only). False after a failed build: Metal materials then draw as
    // plain materials (the caller reports it once); nothing else is affected.
    bool metal(IDirect3DDevice9* d,IDirect3DVertexShader9*& vertex,IDirect3DPixelShader9*& pixel){
        if(!metalVertex_&&!metalFailed_){
            std::vector<D3D_SHADER_MACRO> macros{{"VERTEX_SHADER","1"},{"LIGHT_REGISTER","c5"},{"SKIN_LINEAR","1"},{"METAL","1"},{nullptr,nullptr}};ID3DBlob* code=nullptr;
            if(SUCCEEDED(build(macros,"vs_3_0",&code)))d->CreateVertexShader(static_cast<const DWORD*>(code->GetBufferPointer()),&metalVertex_);
            if(code){code->Release();code=nullptr;}
            std::vector<D3D_SHADER_MACRO> pixelMacros{{"LIGHT_REGISTER","c2"},{"METAL","1"},{nullptr,nullptr}};
            if(metalVertex_&&SUCCEEDED(build(pixelMacros,"ps_3_0",&code)))d->CreatePixelShader(static_cast<const DWORD*>(code->GetBufferPointer()),&metalPixel_);
            if(code)code->Release();
            if(!metalVertex_||!metalPixel_){SafeRelease(metalVertex_);SafeRelease(metalPixel_);metalFailed_=true;}
        }
        vertex=metalVertex_;pixel=metalPixel_;return metalVertex_&&metalPixel_;
    }
    // fx 2026-11-11e: the LEATHER pair of a linear skin (with or without the second pose stream); fx = with the shadow map and
    // the flash light (variant 2), else the per-pixel light alone. False after a failed build: Leather materials then draw as
    // plain materials (the caller reports it once).
    bool leather(IDirect3DDevice9* d,bool pose,bool fx,IDirect3DVertexShader9*& vertex,IDirect3DPixelShader9*& pixel){
        if(leatherFailed_)return false;
        auto& v=leatherVertex_[pose?1:0][fx?1:0];auto& p=leatherPixel_[fx?1:0];
        if(!v){std::vector<D3D_SHADER_MACRO> macros{{"VERTEX_SHADER","1"},{"LIGHT_REGISTER","c5"},{"SKIN_LINEAR","1"},{"PIXEL_LIGHT","1"},{"LEATHER","1"}};
            if(pose)macros.push_back({"SKIN_POSE","1"});if(fx)macros.push_back({"FX_LIGHT","1"});macros.push_back({nullptr,nullptr});ID3DBlob* code=nullptr;
            if(SUCCEEDED(build(macros,"vs_3_0",&code)))d->CreateVertexShader(static_cast<const DWORD*>(code->GetBufferPointer()),&v);
            if(code)code->Release();}
        if(v&&!p){std::vector<D3D_SHADER_MACRO> macros{{"LIGHT_REGISTER","c2"},{"PIXEL_LIGHT","1"},{"LEATHER","1"}};if(fx)macros.push_back({"FX_LIGHT","1"});macros.push_back({nullptr,nullptr});ID3DBlob* code=nullptr;
            if(SUCCEEDED(build(macros,"ps_3_0",&code)))d->CreatePixelShader(static_cast<const DWORD*>(code->GetBufferPointer()),&p);
            if(code)code->Release();}
        if(!v||!p){leatherFailed_=true;return false;}
        vertex=v;pixel=p;return true;
    }
};
// True when this device runs the path in hardware. The GOG wrapper creates a mixed device (flags 0x82), so nothing here
// needs another device: the draws clear SetSoftwareVertexProcessing.
inline bool DeviceSupports(IDirect3DDevice9* device,std::string& why){
    D3DCAPS9 caps{};if(FAILED(device->GetDeviceCaps(&caps))){why="GetDeviceCaps failed";return false;}
    if(caps.VertexShaderVersion<D3DVS_VERSION(3,0)||caps.PixelShaderVersion<D3DPS_VERSION(3,0)){why="no shader model 3";return false;}
    if(!(caps.DevCaps&D3DDEVCAPS_HWTRANSFORMANDLIGHT)){why="no hardware vertex processing";return false;}
    if(caps.MaxVertexShaderConst<kPaletteRegister+kMaxBones*kLinearRegs){why="too few vertex constants";return false;}
    if(!(caps.DeclTypes&D3DDTCAPS_UBYTE4)){why="vertex formats missing";return false;}
    if(caps.MaxVertexIndex<0x100000||caps.MaxStreams<2||caps.MaxPrimitiveCount<0x100000||!(caps.DevCaps2&D3DDEVCAPS2_STREAMOFFSET)){why="index or stream limits";return false;}
    return true;
}
} // namespace re3gpu
