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
        for(unsigned k=0;k<4;++k)gpu->materialColour.push_back(float((m.baseColorRGBA>>(k*8))&255u)/255.f);}
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
// One model of one build, from its OT jobs to the frame that retires them.
struct Draw {
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
float3 Shade(float3 n,float ao){
    n=normalize(n);
    float3 lambert=max(0,float3(dot(lightData[0].xyz,n),dot(lightData[1].xyz,n),dot(lightData[2].xyz,n)));
    float ambient=lightData[9].x*(1-lightData[9].w*(1-ao))*max(0,1+lightData[9].z*dot(n,lightData[8].xyz));
    float direct=lightData[9].y*(1-lightData[10].x*(1-ao));
    float3 x=(lightData[6].rgb*ambient+float3(dot(lightData[3].xyz,lambert),dot(lightData[4].xyz,lambert),dot(lightData[5].xyz,lambert))*direct)*lightData[7].rgb;
    float shoulder=lightData[10].y;
    if(shoulder>0){float knee=1-shoulder;x=lerp(x,knee+shoulder*(1-exp((knee-x)/shoulder)),step(knee,x));}
    return saturate(x);
}
#ifdef VERTEX_SHADER
float4 projection:register(c0);   // clip.x = x*X + y*Z, clip.y = z*Y + w*Z
float4 misc:register(c1);         // x near plane, y World/in-between weight, zw pose factors
float4 cameraRows[3]:register(c2);
float4 colourTint:register(c16);  // material colour x draw tint
float4 eyeWorld:register(c17);
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
struct Output {float4 position:POSITION;float4 colour:COLOR0;float2 uv:TEXCOORD0;float2 depthOcclusion:TEXCOORD1;
#ifdef PIXEL_LIGHT
    float3 normal:TEXCOORD2;
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
    float4 h=float4(world,1);
    float3 camera=float3(dot(cameraRows[0],h),dot(cameraRows[1],h),dot(cameraRows[2],h));
    output.position=float4(projection.x*camera.x+projection.y*camera.z,projection.z*camera.y+projection.w*camera.z,camera.z-misc.x,camera.z);
    output.uv=input.uv;output.depthOcclusion=float2(camera.z,input.colour.r);
    float4 colour=colourTint;
#ifndef SKIN_DECAL
    colour.rgb*=tint;   // damage red belongs to the body, not to its wound decals
#endif
#ifdef PIXEL_LIGHT
    output.normal=normal;
#else
    if(lightData[10].z>0)colour.rgb*=Shade(normal,input.colour.r);
#endif
    output.colour=colour;
    return output;
}
#else
sampler2D albedo:register(s0);
float4 options:register(c0);   // x coverage cutoff, z opacity
float4 range:register(c1);     // x,y camera depth of the flushed buckets
float4 main(float4 tint:COLOR0,float2 uv:TEXCOORD0,float2 depthOcclusion:TEXCOORD1
#ifdef PIXEL_LIGHT
    ,float3 normal:TEXCOORD2
#endif
    ):COLOR0{
    clip(depthOcclusion.x-range.x);clip(range.y-depthOcclusion.x);
    float4 texel=tex2D(albedo,uv);
    float coverage=texel.a*tint.a;
    clip(coverage-options.x);
    float3 colour=tint.rgb;
#ifdef PIXEL_LIGHT
    if(lightData[10].z>0)colour*=Shade(normal,depthOcclusion.y);
#endif
    return float4(texel.rgb*colour,coverage*options.z);
}
#endif
)HLSL";}

class Shaders {
    IDirect3DVertexShader9* vertex_[5][2]{};IDirect3DPixelShader9* pixel_[2]{};
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
    void release(){for(auto& row:vertex_)for(auto& v:row)SafeRelease(v);for(auto& p:pixel_)SafeRelease(p);SafeRelease(skin_);SafeRelease(pose_);SafeRelease(world_);}
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
    IDirect3DVertexShader9* vertex(IDirect3DDevice9* d,Skin skin,bool pixelLight){
        auto& slot=vertex_[unsigned(skin)][pixelLight];if(slot)return slot;
        std::vector<D3D_SHADER_MACRO> macros{{"VERTEX_SHADER","1"},{"LIGHT_REGISTER","c5"}};if(pixelLight)macros.push_back({"PIXEL_LIGHT","1"});
        if(skin==Skin::Affine)macros.push_back({"SKIN_AFFINE","1"});else if(skin==Skin::World)macros.push_back({"SKIN_WORLD","1"});else macros.push_back({"SKIN_LINEAR","1"});
        if(skin==Skin::LinearPose)macros.push_back({"SKIN_POSE","1"});if(skin==Skin::Decal)macros.push_back({"SKIN_DECAL","1"});
        macros.push_back({nullptr,nullptr});ID3DBlob* code=nullptr;
        if(SUCCEEDED(build(macros,"vs_3_0",&code)))d->CreateVertexShader(static_cast<const DWORD*>(code->GetBufferPointer()),&slot);
        if(code)code->Release();return slot;
    }
    IDirect3DPixelShader9* pixel(IDirect3DDevice9* d,bool pixelLight){
        auto& slot=pixel_[pixelLight];if(slot)return slot;
        std::vector<D3D_SHADER_MACRO> macros{{"LIGHT_REGISTER","c2"}};if(pixelLight)macros.push_back({"PIXEL_LIGHT","1"});macros.push_back({nullptr,nullptr});ID3DBlob* code=nullptr;
        if(SUCCEEDED(build(macros,"ps_3_0",&code)))d->CreatePixelShader(static_cast<const DWORD*>(code->GetBufferPointer()),&slot);
        if(code)code->Release();return slot;
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
