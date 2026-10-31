#pragma once

// Bounded R3MESH1 loader and CPU geometry helpers. No native game arrays/calls.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <fstream>
#include <exception>
#include <limits>
#include <string>
#include <utility>
#include <vector>
#include <unordered_map>
#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <intrin.h>
#include <windows.h>

namespace re3model {

// Fixed worker pool for the per-vertex / per-triangle CPU work of large HD actors (one caller at a time:
// the game's render thread). Each Run owns its counters, so a worker that wakes late can never take a
// chunk of a later Run. The pool is never destroyed: at process exit the idle threads are just
// terminated and nothing joins under the loader lock.
namespace parallel {
// Fix 2026-10-25d (perf): one frame runs ~40 Runs back to back (3-4 per actor). Waking sleeping workers through the
// condition variable each time cost more than the chunks themselves, so workers (and the caller waiting for the last
// chunks) first spin up to kSpinMicroseconds for the next Run of the same burst and only then sleep.
inline long long SpinTicks(){static const long long ticks=[]{LARGE_INTEGER f;QueryPerformanceFrequency(&f);return f.QuadPart*300/1000000;}();return ticks;}
inline long long Ticks(){LARGE_INTEGER t;QueryPerformanceCounter(&t);return t.QuadPart;}
class Pool {
    struct Job {const std::function<void(std::size_t)>* task=nullptr;std::size_t chunks=0;std::atomic<std::size_t> next{0},finished{0};std::atomic<bool> failed{false};};
    std::mutex mutex_;std::condition_variable wake_,done_;
    std::shared_ptr<Job> current_;std::uint64_t generation_=0;unsigned workers_=0;
    std::atomic<std::uint64_t> published_{0};   // generation_, readable without the mutex (spinning)
    void Drain(Job& job){
        for(;;){
            const std::size_t chunk=job.next.fetch_add(1);if(chunk>=job.chunks)return;
            try{(*job.task)(chunk);}catch(...){job.failed=true;}
            if(job.finished.fetch_add(1)+1==job.chunks){std::lock_guard<std::mutex> lock(mutex_);done_.notify_all();}
        }
    }
    void Work(){
        std::uint64_t seen=0;
        for(;;){
            if(published_.load(std::memory_order_acquire)==seen){const long long until=Ticks()+SpinTicks();
                while(published_.load(std::memory_order_acquire)==seen&&Ticks()<until)_mm_pause();}
            std::shared_ptr<Job> job;
            {std::unique_lock<std::mutex> lock(mutex_);wake_.wait(lock,[&]{return generation_!=seen;});seen=generation_;job=current_;}
            if(job)Drain(*job);
        }
    }
    Pool(){
        const unsigned hardware=std::thread::hardware_concurrency();
        const unsigned count=hardware>1?(std::min)(hardware-1,7u):0u;
        for(unsigned i=0;i<count;++i){try{std::thread(&Pool::Work,this).detach();++workers_;}catch(...){break;}}
    }
public:
    static Pool& Get(){static Pool* pool=new Pool;return *pool;}
    unsigned Workers()const{return workers_;}
    // Runs task(0..chunks-1) on the workers and the calling thread; false if any chunk threw.
    bool Run(std::size_t chunks,const std::function<void(std::size_t)>& task){
        if(!chunks)return true;
        auto job=std::make_shared<Job>();job->task=&task;job->chunks=chunks;
        if(workers_&&chunks>1){{std::lock_guard<std::mutex> lock(mutex_);current_=job;++generation_;published_.store(generation_,std::memory_order_release);}wake_.notify_all();}
        Drain(*job);
        if(job->finished.load()!=chunks){const long long until=Ticks()+SpinTicks();while(job->finished.load()!=chunks&&Ticks()<until)_mm_pause();}
        {std::unique_lock<std::mutex> lock(mutex_);done_.wait(lock,[&]{return job->finished.load()==chunks;});if(current_==job)current_.reset();}
        return !job->failed;
    }
};
// Set on threads that must not use the pool (async actor tasks): For then runs its chunks in order on that thread.
inline bool& InlineOnly(){thread_local bool value=false;return value;}
// body(begin,end) over [0,count) in chunks of `grain` items.
template<class Body> inline bool For(std::size_t count,std::size_t grain,Body body){
    const std::size_t chunks=(count+grain-1)/grain;
    if(InlineOnly()){try{for(std::size_t chunk=0;chunk<chunks;++chunk){const std::size_t begin=chunk*grain;body(chunk,begin,(std::min)(count,begin+grain));}}catch(...){return false;}return true;}
    const std::function<void(std::size_t)> task=[&](std::size_t chunk){const std::size_t begin=chunk*grain;body(chunk,begin,(std::min)(count,begin+grain));};
    return Pool::Get().Run(chunks,task);
}
} // namespace parallel

struct Vec2 { float x=0, y=0; };
struct Vec3 { float x=0, y=0, z=0; };
inline Vec3 operator+(Vec3 a,Vec3 b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
inline Vec3 operator-(Vec3 a,Vec3 b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
inline Vec3 operator*(Vec3 a,float b){return {a.x*b,a.y*b,a.z*b};}
inline float Dot(Vec3 a,Vec3 b){return a.x*b.x+a.y*b.y+a.z*b.z;}
inline Vec3 Cross(Vec3 a,Vec3 b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
inline bool Finite(Vec3 a){return std::isfinite(a.x)&&std::isfinite(a.y)&&std::isfinite(a.z);}
inline Vec3 Normalize(Vec3 a){float n=Dot(a,a);return n>1.e-20f&&std::isfinite(n)?a*(1.f/std::sqrt(n)):Vec3{0,0,1};}

struct Matrix3x4 {
    float m[12]={1,0,0,0,0,1,0,0,0,0,1,0};
};
template<class NativeMatrix> inline Matrix3x4 FromNativeMatrix(const NativeMatrix& native){
    Matrix3x4 result;
    for(unsigned row=0;row<3;++row){
        for(unsigned col=0;col<3;++col)result.m[row*4+col]=float(native.rotation[row*3+col])*(1.f/4096.f);
        result.m[row*4+3]=float(native.translation[row]);
    }
    return result;
}
inline bool Finite(const Matrix3x4& a){for(float f:a.m)if(!std::isfinite(f))return false;return true;}
inline Vec3 TransformPoint(const Matrix3x4& a,Vec3 p){return {
    a.m[0]*p.x+a.m[1]*p.y+a.m[2]*p.z+a.m[3],
    a.m[4]*p.x+a.m[5]*p.y+a.m[6]*p.z+a.m[7],
    a.m[8]*p.x+a.m[9]*p.y+a.m[10]*p.z+a.m[11]};}
inline Vec3 TransformVector(const Matrix3x4& a,Vec3 p){return {
    a.m[0]*p.x+a.m[1]*p.y+a.m[2]*p.z,
    a.m[4]*p.x+a.m[5]*p.y+a.m[6]*p.z,
    a.m[8]*p.x+a.m[9]*p.y+a.m[10]*p.z};}
inline Matrix3x4 Multiply(const Matrix3x4& a,const Matrix3x4& b){
    Matrix3x4 r;
    for(unsigned row=0;row<3;++row){
        for(unsigned col=0;col<3;++col)r.m[row*4+col]=a.m[row*4]*b.m[col]+a.m[row*4+1]*b.m[4+col]+a.m[row*4+2]*b.m[8+col];
        r.m[row*4+3]=a.m[row*4]*b.m[3]+a.m[row*4+1]*b.m[7]+a.m[row*4+2]*b.m[11]+a.m[row*4+3];
    }
    return r;
}
inline bool NormalMatrix(const Matrix3x4& a,Matrix3x4& out);
// Inverse of an invertible affine 3x4 (rotation/scale part from the cofactor matrix).
inline bool InverseAffine(const Matrix3x4& a,Matrix3x4& out){
    Matrix3x4 n;if(!NormalMatrix(a,n))return false; // n = transpose of the inverse linear part
    for(unsigned r=0;r<3;++r)for(unsigned c=0;c<3;++c)out.m[r*4+c]=n.m[c*4+r];
    for(unsigned r=0;r<3;++r)out.m[r*4+3]=-(out.m[r*4]*a.m[3]+out.m[r*4+1]*a.m[7]+out.m[r*4+2]*a.m[11]);
    return Finite(out);
}
inline bool NormalMatrix(const Matrix3x4& a,Matrix3x4& out){
    const Vec3 x{a.m[0],a.m[1],a.m[2]},y{a.m[4],a.m[5],a.m[6]},z{a.m[8],a.m[9],a.m[10]};
    const Vec3 c0=Cross(y,z),c1=Cross(z,x),c2=Cross(x,y);
    const float det=Dot(x,c0);if(!std::isfinite(det)||std::abs(det)<1.e-12f)return false;
    const float r=1.f/det;
    out={{c0.x*r,c0.y*r,c0.z*r,0,c1.x*r,c1.y*r,c1.z*r,0,c2.x*r,c2.y*r,c2.z*r,0}};
    return Finite(out);
}

struct Header {
    char magic[8];
    std::uint32_t version,headerBytes,vertexCount,indexCount,boneCount,materialCount,vertexStride;
    std::uint32_t verticesOffset,indicesOffset,inverseBindOffset,materialsOffset,stringsOffset,stringsBytes,fileBytes;
};
struct Vertex {
    Vec3 position,normal;
    Vec2 uv;
    std::uint16_t bone[4],weight[4];
};
struct DiskMaterial {
    std::uint32_t firstIndex,indexCount,albedoOffset,albedoBytes,stpOffset,stpBytes;
    std::uint32_t flags,baseColorRGBA,reserved0,reserved1;
};
static_assert(sizeof(Header)==64&&sizeof(Vertex)==48&&sizeof(Matrix3x4)==48&&sizeof(DiskMaterial)==40,"R3MESH1 layout mismatch");
enum MaterialFlags : std::uint32_t { TwoSided=1, AlphaCutout=2, AlphaBlend=4, Unlit=8 };
struct Material {
    std::uint32_t firstIndex=0,indexCount=0,flags=0,baseColorRGBA=0xffffffffu;
    std::string albedoPath,stpPath;
};
struct Mesh {
    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices;
    std::vector<Matrix3x4> inverseBind;
    std::vector<Material> materials;
    // First exact PN + ordered influence representative, excluding only UV.
    // Optional acceleration: absent/stale maps always retain the full path.
    std::vector<std::uint32_t> canonicalVertex;
};
struct CanonicalVertexKey {
    std::uint32_t words[10];
    bool operator==(const CanonicalVertexKey& other)const{return !std::memcmp(words,other.words,sizeof(words));}
};
struct CanonicalVertexHash {
    std::size_t operator()(const CanonicalVertexKey& key)const{
        std::uint32_t hash=2166136261u;
        const auto* bytes=reinterpret_cast<const unsigned char*>(key.words);
        for(unsigned i=0;i<sizeof(key.words);++i)hash=(hash^bytes[i])*16777619u;
        return hash;
    }
};
inline bool SameCanonicalVertex(const Vertex& a,const Vertex& b){
    return !std::memcmp(&a.position,&b.position,24)&&!std::memcmp(a.bone,b.bone,16);
}
inline bool BuildCanonicalVertexMap(Mesh& mesh){
    try {
        if(mesh.vertices.size()>1000000){mesh.canonicalVertex.clear();return false;}
        std::unordered_map<CanonicalVertexKey,std::uint32_t,CanonicalVertexHash> first;
        first.reserve(mesh.vertices.size());
        std::vector<std::uint32_t> result;result.reserve(mesh.vertices.size());
        for(std::uint32_t i=0;i<mesh.vertices.size();++i){
            const auto& v=mesh.vertices[i];CanonicalVertexKey key;
            std::memcpy(key.words,&v.position,24);std::memcpy(key.words+6,v.bone,16);
            result.push_back(first.emplace(key,i).first->second);
        }
        mesh.canonicalVertex=std::move(result);return true;
    }catch(const std::exception&){mesh.canonicalVertex.clear();return false;}
}
inline std::size_t CanonicalSkinVertex(const Mesh& mesh,std::size_t index){
    if(mesh.canonicalVertex.size()!=mesh.vertices.size())return index;
    const auto representative=mesh.canonicalVertex[index];
    // Guard stale maps and direct caller edits without changing the public API.
    return representative<index&&SameCanonicalVertex(mesh.vertices[index],mesh.vertices[representative])?representative:index;
}

inline bool ValidRelativePath(const std::string& path){
    if(path.empty()||path.size()>4096||path[0]=='/'||path[0]=='\\'||path.find(':')!=std::string::npos)return false;
    std::string component;
    for(unsigned char c:path){
        if(c<32)return false;
        if(c=='/'||c=='\\'){if(component=="..")return false;component.clear();}
        else component.push_back(char(c));
    }
    return component!="..";
}
inline bool LoadMeshBytes(const std::vector<std::uint8_t>& bytes,Mesh& output,std::string* error=nullptr){
    try {
    auto fail=[&](const char* text){if(error)*error=text;return false;};
    if(bytes.size()<sizeof(Header)||bytes.size()>256u*1024*1024)return fail("Mesh size outside bounds");
    Header h;std::memcpy(&h,bytes.data(),sizeof(h));
    if(std::memcmp(h.magic,"R3MESH1\0",8)||h.version!=1||h.headerBytes!=64||h.vertexStride!=48||h.fileBytes!=bytes.size())return fail("Invalid R3MESH1 header");
    if(!h.vertexCount||h.vertexCount>1000000||h.indexCount<3||h.indexCount>6000000||h.indexCount%3||
       !h.boneCount||h.boneCount>256||!h.materialCount||h.materialCount>4096)return fail("Mesh count outside bounds");
    std::uint64_t expected=64;
    if(h.verticesOffset!=expected)return fail("Invalid vertex offset");expected+=std::uint64_t(h.vertexCount)*48;
    if(h.indicesOffset!=expected)return fail("Invalid index offset");expected+=std::uint64_t(h.indexCount)*4;
    if(h.inverseBindOffset!=expected)return fail("Invalid inverse-bind offset");expected+=std::uint64_t(h.boneCount)*48;
    if(h.materialsOffset!=expected)return fail("Invalid material offset");expected+=std::uint64_t(h.materialCount)*40;
    if(h.stringsOffset!=expected)return fail("Invalid string offset");expected+=h.stringsBytes;
    if(expected!=bytes.size())return fail("Sections do not cover declared file");
    Mesh mesh;mesh.vertices.resize(h.vertexCount);mesh.indices.resize(h.indexCount);mesh.inverseBind.resize(h.boneCount);mesh.materials.reserve(h.materialCount);
    std::memcpy(mesh.vertices.data(),bytes.data()+h.verticesOffset,std::size_t(h.vertexCount)*48);
    std::memcpy(mesh.indices.data(),bytes.data()+h.indicesOffset,std::size_t(h.indexCount)*4);
    std::memcpy(mesh.inverseBind.data(),bytes.data()+h.inverseBindOffset,std::size_t(h.boneCount)*48);
    for(const auto& v:mesh.vertices){
        if(!Finite(v.position)||!Finite(v.normal)||!std::isfinite(v.uv.x)||!std::isfinite(v.uv.y)||Dot(v.normal,v.normal)<1.e-20f)return fail("Nonfinite/degenerate vertex");
        unsigned sum=0;for(unsigned n=0;n<4;++n){if(v.bone[n]>=h.boneCount)return fail("Bone index outside skeleton");sum+=v.weight[n];}
        if(sum!=65535)return fail("Weights must sum to 65535");
    }
    for(auto i:mesh.indices)if(i>=h.vertexCount)return fail("Triangle index outside vertices");
    for(const auto& m:mesh.inverseBind){Matrix3x4 normal;if(!Finite(m)||!NormalMatrix(m,normal))return fail("Invalid inverse bind matrix");}
    auto readPath=[&](std::uint32_t offset,std::uint32_t length,std::string& path,bool optional){
        if(!length){path.clear();return optional&&offset==0;}
        if(length>4096||std::uint64_t(offset)+length>h.stringsBytes)return false;
        path.assign(reinterpret_cast<const char*>(bytes.data()+h.stringsOffset+offset),length);
        return ValidRelativePath(path);
    };
    std::uint32_t next=0;
    for(unsigned n=0;n<h.materialCount;++n){
        DiskMaterial disk;std::memcpy(&disk,bytes.data()+h.materialsOffset+std::size_t(n)*40,40);
        if(disk.firstIndex!=next||!disk.indexCount||disk.indexCount%3||std::uint64_t(next)+disk.indexCount>h.indexCount||disk.flags>15||disk.reserved0||disk.reserved1)return fail("Invalid material range/flags");
        Material m;m.firstIndex=disk.firstIndex;m.indexCount=disk.indexCount;m.flags=disk.flags;m.baseColorRGBA=disk.baseColorRGBA;
        if(!readPath(disk.albedoOffset,disk.albedoBytes,m.albedoPath,false)||!readPath(disk.stpOffset,disk.stpBytes,m.stpPath,true))return fail("Invalid material path");
        mesh.materials.push_back(std::move(m));next+=disk.indexCount;
    }
    if(next!=h.indexCount)return fail("Materials do not cover all triangles");
    BuildCanonicalVertexMap(mesh);
    output=std::move(mesh);if(error)error->clear();return true;
    }catch(const std::exception&){if(error)*error="Mesh validation/allocation failed";return false;}
}
inline bool LoadMesh(const std::string& path,Mesh& output,std::string* error=nullptr){
    try {
    std::ifstream file(path,std::ios::binary|std::ios::ate);
    if(!file){if(error)*error="Cannot open mesh: "+path;return false;}
    const auto size=file.tellg();if(size<64||size>256ll*1024*1024){if(error)*error="Mesh file size outside bounds";return false;}
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));file.seekg(0);
    if(!file.read(reinterpret_cast<char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()))){if(error)*error="Truncated mesh read";return false;}
    return LoadMeshBytes(bytes,output,error);
    }catch(const std::exception&){if(error)*error="Mesh file allocation/read failed";return false;}
}

struct SkinnedVertex { Vec3 position,normal;Vec2 uv; };
inline bool Skin(const Mesh& mesh,const std::vector<Matrix3x4>& worldBones,std::vector<SkinnedVertex>& output,std::string* error=nullptr){
    if(worldBones.size()<mesh.inverseBind.size()){if(error)*error="Missing native pose matrices";return false;}
    std::vector<Matrix3x4> transforms(mesh.inverseBind.size()),normals(mesh.inverseBind.size());
    for(std::size_t n=0;n<transforms.size();++n){
        if(!Finite(worldBones[n])){if(error)*error="Nonfinite native pose";return false;}
        transforms[n]=Multiply(worldBones[n],mesh.inverseBind[n]);
        if(!NormalMatrix(transforms[n],normals[n])){
            // A native part scale collapsed to 0 (EM3B tentacle growing out, scale 4096,0,4096 baked into the world
            // matrix) is drawn collapsed like the native part: normals from the cofactor matrix (det x inverse-transpose,
            // renormalized per vertex) instead of refusing the frame.
            const float* a=transforms[n].m;const Vec3 x{a[0],a[1],a[2]},y{a[4],a[5],a[6]},z{a[8],a[9],a[10]};
            const Vec3 c0=Cross(y,z),c1=Cross(z,x),c2=Cross(x,y);
            normals[n]={{c0.x,c0.y,c0.z,0,c1.x,c1.y,c1.z,0,c2.x,c2.y,c2.z,0}};
            if(!Finite(normals[n])){if(error)*error="Singular skin transform";return false;}
        }
    }
    // Chunks on the worker pool; UV-split duplicates copy their representative afterwards in index order.
    const std::size_t count=mesh.vertices.size();
    // Fix 2026-10-25d: the caller's array is reused (every element is written below; on failure the caller drops it).
    std::vector<SkinnedVertex> result;result.swap(output);result.resize(count);std::vector<std::uint8_t> bad((count+2047)/2048,0);
    if(!parallel::For(count,2048,[&](std::size_t chunk,std::size_t begin,std::size_t end){
        for(std::size_t i=begin;i<end;++i){const auto& v=mesh.vertices[i];
            if(CanonicalSkinVertex(mesh,i)<i)continue;
            Vec3 position{},normal{};
            for(unsigned k=0;k<4;++k)if(v.weight[k]){
                const float weight=float(v.weight[k])*(1.f/65535.f);
                position=position+TransformPoint(transforms[v.bone[k]],v.position)*weight;
                normal=normal+TransformVector(normals[v.bone[k]],v.normal)*weight;
            }
            if(!Finite(position)||!Finite(normal)){bad[chunk]=1;return;}
            result[i]={position,Normalize(normal),v.uv};
        }
    })||std::find(bad.begin(),bad.end(),std::uint8_t(1))!=bad.end()){if(error)*error="Skinning produced nonfinite geometry";return false;}
    for(std::size_t i=0;i<count;++i){const auto representative=CanonicalSkinVertex(mesh,i);if(representative<i){auto reused=result[representative];reused.uv=mesh.vertices[i].uv;result[i]=reused;}}
    output=std::move(result);if(error)error->clear();return true;
}

enum class ProjectionMode { NativeInteger, SmoothSubpixel };
enum class FaceCull { None, Clockwise, CounterClockwise };
struct DrawVertex {
    float x,y,z,rhw;
    std::uint32_t diffuse;
    float u,v;
};
static_assert(sizeof(DrawVertex)==28,"XYZRHW/diffuse/UV vertex layout mismatch");
// fps60 (2026-10-27): where an emitted vertex comes from: skinned vertex a, or the point t of the way from a to b (a
// corner made by the near clip). Filled only for Projection::recordSources; the in-between frame reprojects these.
struct VertexSource {std::uint32_t a=0,b=0;float t=0;};
struct DrawRun {std::uint32_t firstVertex=0,vertexCount=0,materialIndex=0;};
struct DepthSlice {std::uint16_t bucket=0;std::uint32_t firstRun=0,runCount=0;};
struct FrameGeometry {
    std::vector<DrawVertex> vertices;
    std::vector<DrawRun> runs;
    std::vector<DepthSlice> slices;
    std::vector<VertexSource> sources;   // one per vertex with Projection::recordSources, else empty
    std::uint32_t sourceTriangles=0,clippedTriangles=0,culledTriangles=0;
};
using ShadeFunction=std::uint32_t(*)(void* context,Vec3 worldPosition,Vec3 worldNormal,const Material& material);
// Per-vertex variant (baked occlusion): vertex = mesh vertex, near-clipped corners pass their triangle's first vertex.
// Must be pure: results are cached per canonical vertex like ShadeNativeNccs.
using ShadeVertexFunction=std::uint32_t(*)(void* context,Vec3 worldPosition,Vec3 worldNormal,const Material& material,std::uint32_t vertex);
using DepthFunction=std::uint16_t(*)(void* context,std::uint16_t z0,std::uint16_t z1,std::uint16_t z2);
// Optional source-aware policy. sourceTriangle is the original index-buffer
// triangle ordinal (first index / 3), unchanged for every near-clipped fan piece.
// Takes precedence over DepthFunction; the legacy callback remains unchanged.
using TriangleDepthFunction=std::uint16_t(*)(void* context,std::uint32_t sourceTriangle,std::uint16_t z0,std::uint16_t z1,std::uint16_t z2);
struct Projection {
    Matrix3x4 camera;
    float h=256,offsetX=160,offsetY=120;
    std::int16_t zsf3=341;
    float nearPlane=1;
    float scaleX=1,scaleY=1,pixelOffsetX=0,pixelOffsetY=0;
    ProjectionMode mode=ProjectionMode::NativeInteger;
    FaceCull cull=FaceCull::None;
    bool perspectiveCorrect=true;
    // Native OT insertion prepends each primitive. False reproduces reverse
    // source emission within a bucket; true sorts HD triangles far-to-near.
    bool sortWithinBucketByDepth=false;
    std::uint32_t tintARGB=0xffffffffu;
    std::uint32_t maxOutputTriangles=500000;
    // Adapter-supplied safe OT extent. 1024 covers the observed actor default
    // (stored SZ<=32767, ZSF3=341); it is not proof of allocation length.
    std::uint32_t bucketLimit=1024;
    ShadeFunction shade=nullptr;
    void* shadeContext=nullptr;
    // Custom callbacks keep their original per-corner evaluation order unless
    // explicitly declared pure. Built-in native lighting is already pure.
    bool cachePureShade=false;
    DepthFunction depth=nullptr;
    void* depthContext=nullptr;
    TriangleDepthFunction triangleDepth=nullptr;
    void* triangleDepthContext=nullptr;
    // Optional, one entry per mesh material: nonzero = the material emits no triangles (a hidden body region).
    const std::vector<std::uint8_t>* hiddenMaterials=nullptr;
    // Optional, one entry per source triangle (index/3): nonzero = not emitted (enemy parts gone or split off, fix 2026-10-25).
    const std::vector<std::uint8_t>* hiddenTriangles=nullptr;
    // Opaque meshes drawn with the depth test: order triangles inside a bucket by material instead of depth, so
    // every slice has one draw run per material (several materials with per-actor textures stay cheap).
    bool sortWithinBucketByMaterial=false;
    // Optional, one ARGB multiplier per mesh vertex (v12: the red of damaged enemy parts). Near-clipped corners take
    // the multiplier of their triangle's first vertex.
    const std::vector<std::uint32_t>* vertexTint=nullptr;
    // Takes precedence over shade (same shadeContext).
    ShadeVertexFunction shadeVertex=nullptr;
    // fps60: also emit FrameGeometry::sources (same vertices and order otherwise).
    bool recordSources=false;
};
inline std::uint32_t VertexTint(const Projection& p,std::uint32_t vertex){return p.vertexTint&&vertex<p.vertexTint->size()?(*p.vertexTint)[vertex]:0xffffffffu;}
inline bool MaterialHidden(const Projection& p,std::size_t material){return p.hiddenMaterials&&material<p.hiddenMaterials->size()&&(*p.hiddenMaterials)[material];}
inline bool TriangleHidden(const Projection& p,std::size_t triangle){return p.hiddenTriangles&&triangle<p.hiddenTriangles->size()&&(*p.hiddenTriangles)[triangle];}
inline bool SetNativeProjectionRegisters(Projection& projection,const void* data,std::size_t size){
    if(!data||size<0x84)return false;
    const auto bytes=static_cast<const std::uint8_t*>(data);
    std::int32_t x=0,y=0;std::uint16_t h=0;std::int16_t scale=0;
    std::memcpy(&x,bytes+0x70,4);std::memcpy(&y,bytes+0x74,4);
    std::memcpy(&h,bytes+0x78,2);std::memcpy(&scale,bytes+0x80,2);
    if(!h||scale<0)return false;
    projection.offsetX=float(x);projection.offsetY=float(y);projection.h=float(h);projection.zsf3=scale;
    return true;
}
inline std::uint32_t RGBAtoARGB(std::uint32_t c){return (c&0xff00ff00u)|((c&255u)<<16)|((c>>16)&255u);}
inline std::uint32_t MultiplyColor(std::uint32_t a,std::uint32_t b){
    std::uint32_t out=0;for(unsigned k=0;k<4;++k){unsigned shift=k*8;out|=(((a>>shift)&255u)*((b>>shift)&255u)+127u)/255u<<shift;}return out;
}
// Optional provisional world-space lighting, not a recreation of native NCCS.
// Leave Projection.shade null for neutral texture/rig parity. A verified native
// lighting adapter can replace this callback without changing mesh data.
struct DirectionalLighting {
    Vec3 ambient{0.55f,0.55f,0.55f};
    Vec3 directionToLight{-0.35f,-0.75f,-0.55f};
    Vec3 color{0.45f,0.45f,0.45f};
};
inline std::uint32_t ShadeDirectional(void* context,Vec3,Vec3 normal,const Material& material){
    const auto base=RGBAtoARGB(material.baseColorRGBA);
    if(material.flags&Unlit)return base;
    const DirectionalLighting defaults;
    const auto& light=context?*static_cast<const DirectionalLighting*>(context):defaults;
    const float intensity=(std::max)(0.f,Dot(Normalize(normal),Normalize(light.directionToLight)));
    const Vec3 factor=light.ambient+light.color*intensity;
    auto channel=[](unsigned value,float factor){return unsigned((std::max)(0.f,(std::min)(255.f,float(value)*factor))+0.5f);};
    return (base&0xff000000u)|(channel((base>>16)&255,factor.x)<<16)|(channel((base>>8)&255,factor.y)<<8)|channel(base&255,factor.z);
}
struct NativeLighting {
    std::int16_t worldLight[9]{};
    std::int16_t color[9]{};
    std::int32_t ambient[3]{};
    std::uint8_t baseRGB[3]={128,128,128};
};
inline bool CaptureNativeLighting(NativeLighting& out,const void* worldLight,const void* gte,std::size_t gteBytes,std::uint32_t baseRGB=0x00808080u){
    if(!worldLight||!gte||gteBytes<0x62)return false;
    std::memcpy(out.worldLight,worldLight,sizeof(out.worldLight));
    const auto registers=static_cast<const std::uint8_t*>(gte);
    std::memcpy(out.ambient,registers+0x44,sizeof(out.ambient));
    std::memcpy(out.color,registers+0x50,sizeof(out.color));
    for(unsigned i=0;i<3;++i)out.baseRGB[i]=std::uint8_t(baseRGB>>(i*8));
    return true;
}
inline std::uint32_t ShadeNativeNccs(void* context,Vec3,Vec3 normal,const Material& material){
    const auto base=RGBAtoARGB(material.baseColorRGBA);
    if(!context||(material.flags&Unlit))return base;
    const auto& state=*static_cast<const NativeLighting*>(context);
    normal=Normalize(normal);
    auto normalInteger=[](float component){
        const float scaled=component*4096.f;
        const auto whole=std::int32_t(scaled);
        const float fraction=scaled-float(whole);
        return whole+std::int32_t(fraction>=.5f)-std::int32_t(fraction<=-.5f);
    };
    const std::int32_t n[3]={normalInteger(normal.x),normalInteger(normal.y),normalInteger(normal.z)};
    static_assert((std::int64_t(-1)>>12)==-1,"Native fixed-point arithmetic requires signed arithmetic shift");
    auto clamp=[](std::int64_t value)->std::int32_t{return value<=0?0:value>=32767?32767:std::int32_t(value);};
    std::int32_t light[3]{};
    for(unsigned row=0;row<3;++row){
        // Each quantized normal component is within +/-4096, hence the entire
        // signed16 dot is bounded by3*32768*4096=402653184 (fits signed32).
        std::int32_t value=0;for(unsigned col=0;col<3;++col)value+=std::int32_t(state.worldLight[row*3+col])*n[col];light[row]=clamp(value>>12);
    }
    std::uint32_t rgb[3]{};
    for(unsigned row=0;row<3;++row){
        const auto* coefficients=state.color+row*3;
        const unsigned upperBits=unsigned(std::uint16_t(coefficients[0])|std::uint16_t(coefficients[1])|std::uint16_t(coefficients[2]));
        std::int64_t value;
        if(!(upperBits&0xc000u)){
            // All coefficients are0..16383, row L1<=49149; multiplying each
            // clamped light<=32767 keeps the sum below signed32 max.
            value=std::int32_t(coefficients[0])*light[0]+std::int32_t(coefficients[1])*light[1]+std::int32_t(coefficients[2])*light[2];
        }else{
            value=0;for(unsigned col=0;col<3;++col)value+=std::int64_t(coefficients[col])*light[col];
        }
        const auto channel=std::uint32_t(clamp((value>>12)+state.ambient[row]));
        const auto nativeColor=(std::min)(255u,(std::uint32_t(state.baseRGB[row])*channel)>>12);
        rgb[row]=(std::min)(255u,nativeColor*2u);
    }
    return MultiplyColor(base,0xff000000u|(rgb[0]<<16)|(rgb[1]<<8)|rgb[2]);
}
// v20: the native light terms in float (per-light Lambert with the row magnitude, colour matrix, ambient, part RGB and
// the renderer's x2) reshaped by a per-asset profile: baked per-vertex ambient occlusion, hemisphere ambient (more from
// above), key/fill weights of the strongest/other lights, ambient/direct scales and a soft shoulder instead of the hard
// 1.0 clip. The default profile without occlusion reproduces ShadeNativeNccs up to integer rounding.
struct LightProfile {float ambient=1,direct=1,hemi=0,occlusion=0,occlusionDirect=0,key=1,fill=1,shoulder=0;};
struct SculptedLighting {
    float rows[9]{},colour[9]{},ambient[3]{},base[3]{};Vec3 up{0,-1,0};
    LightProfile profile;const std::vector<std::uint8_t>* occlusion=nullptr;
};
// Per pack (model-pack/<id>/): occlusion.r3ao = 'R3AO1\0\0\0', vertex count, FNV-1a of the 48-byte vertex records it was
// baked for, one byte per vertex (255 = open); lighting.txt = 'key value' lines. sculpted = lighting.txt present.
struct AssetLighting {std::vector<std::uint8_t> occlusion;LightProfile profile;bool sculpted=false;};
inline AssetLighting LoadAssetLighting(const std::string& directory,const Mesh& mesh,std::string& note){
    AssetLighting out;note.clear();
    std::ifstream ao(directory+"occlusion.r3ao",std::ios::binary);
    if(ao){
        char magic[8]{};std::uint32_t head[2]{};ao.read(magic,8);ao.read(reinterpret_cast<char*>(head),sizeof(head));
        std::uint32_t hash=2166136261u;const auto* bytes=reinterpret_cast<const std::uint8_t*>(mesh.vertices.data());
        for(std::size_t i=0,n=mesh.vertices.size()*sizeof(Vertex);i<n;++i)hash=(hash^bytes[i])*16777619u;
        if(ao&&!std::memcmp(magic,"R3AO1\0\0\0",8)&&head[0]==mesh.vertices.size()&&head[1]==hash){
            out.occlusion.resize(head[0]);ao.read(reinterpret_cast<char*>(out.occlusion.data()),std::streamsize(head[0]));if(!ao)out.occlusion.clear();
        }
        if(out.occlusion.empty())note="MODEL_OCCLUSION_IGNORED (occlusion.r3ao does not match model.r3mesh)";
    }
    std::ifstream text(directory+"lighting.txt");std::string key;float value;auto& p=out.profile;
    while(text>>key>>value){out.sculpted=true;
        if(key=="ambient")p.ambient=value;else if(key=="direct")p.direct=value;else if(key=="hemi")p.hemi=value;else if(key=="occlusion")p.occlusion=value;
        else if(key=="occlusion_direct")p.occlusionDirect=value;else if(key=="key")p.key=value;else if(key=="fill")p.fill=value;else if(key=="shoulder")p.shoulder=(std::min)(0.9f,(std::max)(0.f,value));}
    if(out.sculpted&&note.empty()){char line[200];std::snprintf(line,sizeof(line),"MODEL_LIGHTING ambient=%.2f direct=%.2f hemi=%.2f occlusion=%.2f/%.2f key=%.2f fill=%.2f shoulder=%.2f ao_vertices=%zu",
        p.ambient,p.direct,p.hemi,p.occlusion,p.occlusionDirect,p.key,p.fill,p.shoulder,out.occlusion.size());note=line;}
    return out;
}
// Beside the asset cache, by pack id (the same pack reloads overwrite it; at most one byte per vertex per pack).
inline std::unordered_map<std::string,AssetLighting>& AssetLightingStore(){static std::unordered_map<std::string,AssetLighting> store;return store;}
// up: the world's up direction in the space of the normals the callback receives (native world is Y-down).
inline SculptedLighting MakeSculptedLighting(const NativeLighting& native,const AssetLighting& asset,Vec3 up){
    const auto& profile=asset.profile;SculptedLighting s;s.profile=profile;s.occlusion=asset.occlusion.empty()?nullptr:&asset.occlusion;s.up=Normalize(up);
    float strength[3]{};unsigned key=0;
    for(unsigned i=0;i<3;++i){
        const Vec3 row{native.worldLight[i*3]/4096.f,native.worldLight[i*3+1]/4096.f,native.worldLight[i*3+2]/4096.f};
        float peak=0;for(unsigned k=0;k<3;++k)peak=(std::max)(peak,native.color[k*3+i]/4096.f);
        strength[i]=std::sqrt(Dot(row,row))*peak;if(strength[i]>strength[key])key=i;
    }
    for(unsigned i=0;i<3;++i){const float weight=i==key?profile.key:profile.fill;for(unsigned c=0;c<3;++c)s.rows[i*3+c]=native.worldLight[i*3+c]/4096.f*weight;}
    for(unsigned k=0;k<9;++k)s.colour[k]=native.color[k]/4096.f;
    for(unsigned k=0;k<3;++k){s.ambient[k]=float(native.ambient[k])/4096.f;s.base[k]=2.f*native.baseRGB[k]/255.f;}
    return s;
}
inline std::uint32_t ShadeSculpted(void* context,Vec3,Vec3 normal,const Material& material,std::uint32_t vertex){
    const auto base=RGBAtoARGB(material.baseColorRGBA);
    if(!context||(material.flags&Unlit))return base;
    const auto& s=*static_cast<const SculptedLighting*>(context);const auto& p=s.profile;
    normal=Normalize(normal);
    const float ao=s.occlusion&&vertex<s.occlusion->size()?float((*s.occlusion)[vertex])*(1.f/255.f):1.f;
    float lambert[3];
    for(unsigned i=0;i<3;++i)lambert[i]=(std::max)(0.f,s.rows[i*3]*normal.x+s.rows[i*3+1]*normal.y+s.rows[i*3+2]*normal.z);
    const float ambient=p.ambient*(1-p.occlusion*(1-ao))*(std::max)(0.f,1+p.hemi*Dot(normal,s.up)),direct=p.direct*(1-p.occlusionDirect*(1-ao));
    std::uint32_t rgb=0xff000000u;
    for(unsigned k=0;k<3;++k){
        float x=(s.ambient[k]*ambient+(s.colour[k*3]*lambert[0]+s.colour[k*3+1]*lambert[1]+s.colour[k*3+2]*lambert[2])*direct)*s.base[k];
        const float knee=1-p.shoulder;if(p.shoulder>0&&x>knee)x=knee+p.shoulder*(1-std::exp((knee-x)/p.shoulder));
        rgb|=std::uint32_t((std::min)(255.f,(std::max)(0.f,x*255.f+.5f)))<<(16-8*k);
    }
    return MultiplyColor(base,rgb);
}
inline std::uint32_t ShadeAt(const Projection& p,Vec3 world,Vec3 normal,const Material& material,std::uint32_t vertex){
    return p.shadeVertex?p.shadeVertex(p.shadeContext,world,normal,material,vertex):p.shade?p.shade(p.shadeContext,world,normal,material):RGBAtoARGB(material.baseColorRGBA);
}
inline std::uint16_t StoredDepth(float z){
    if(!std::isfinite(z)||z<=0)return 0;
    return z>=32767.f?std::uint16_t(32767):std::uint16_t(z);
}
inline std::uint16_t DepthBucket(std::uint16_t a,std::uint16_t b,std::uint16_t c,std::int16_t zsf3){
    const std::int64_t product=std::int64_t(unsigned(a)+unsigned(b)+unsigned(c))*zsf3;
    const std::int64_t otz=product<=0?0:(std::min)(std::int64_t(65535),product/4096);
    return std::uint16_t(otz>>3);
}
struct ProjectedPoint {float x=0,y=0,z=0;std::uint16_t depth=0;};
inline bool ProjectCameraPoint(Vec3 point,const Projection& p,ProjectedPoint& out){
    if(!Finite(point)||!std::isfinite(p.h)||p.h<=0||!std::isfinite(p.offsetX)||!std::isfinite(p.offsetY))return false;
    float x=point.x,y=point.y,z=point.z;
    if(p.mode==ProjectionMode::NativeInteger){x=std::floor(x);y=std::floor(y);z=std::floor(z);}
    x=(std::max)(-32768.f,(std::min)(32767.f,x));
    y=(std::max)(-32768.f,(std::min)(32767.f,y));
    z=(std::max)(0.f,(std::min)(65535.f,z));
    const float divisor=(std::max)(z,1.f);
    float sx=x*p.h/divisor,sy=y*p.h/divisor;
    if(p.mode==ProjectionMode::NativeInteger){sx=std::trunc(sx);sy=std::trunc(sy);}
    sx=(std::max)(-1024.f,(std::min)(1023.f,sx+p.offsetX));
    sy=(std::max)(-1024.f,(std::min)(1023.f,sy+p.offsetY));
    out={sx*p.scaleX+p.pixelOffsetX,sy*p.scaleY+p.pixelOffsetY,z,StoredDepth(z)};
    return std::isfinite(out.x)&&std::isfinite(out.y);
}

namespace detail {
struct ClipVertex {Vec3 camera,world,normal;Vec2 uv;std::uint32_t source=0;VertexSource from{};bool clip=false;};
inline ClipVertex Interpolate(const ClipVertex& a,const ClipVertex& b,float t){return {
    a.camera+(b.camera-a.camera)*t,a.world+(b.world-a.world)*t,
    Normalize(a.normal+(b.normal-a.normal)*t),
    {a.uv.x+(b.uv.x-a.uv.x)*t,a.uv.y+(b.uv.y-a.uv.y)*t},0,{a.source,b.source,t},true};}
inline unsigned ClipNear(const ClipVertex* input,unsigned count,float nearPlane,ClipVertex* out){
    unsigned n=0;ClipVertex previous=input[count-1];bool prevInside=previous.camera.z>=nearPlane;
    for(unsigned i=0;i<count;++i){const auto current=input[i];const bool inside=current.camera.z>=nearPlane;
        if(inside!=prevInside){const float dz=current.camera.z-previous.camera.z;if(std::abs(dz)>1.e-12f)out[n++]=Interpolate(previous,current,(nearPlane-previous.camera.z)/dz);}
        if(inside)out[n++]=current;previous=current;prevInside=inside;
    }
    return n;
}
struct Triangle {DrawVertex v[3];std::uint16_t bucket;std::uint32_t material;float depth;std::uint32_t sequence;VertexSource src[3];};
// The source of an emitted corner: an input vertex is its own source (a=b), a clip vertex keeps its edge.
inline VertexSource SourceOf(const ClipVertex& v){return v.clip?v.from:VertexSource{v.source,v.source,0};}
struct TriangleOrder {std::uint16_t bucket;float depth;std::uint32_t sequence,index;};

// Stable LSD radix passes over the same comparator keys. Positive finite float
// bit patterns have numeric order; zero is normalized for +/-0 equivalence.
// Unsupported depths retain the original stable comparator path.
struct SequenceKey {std::uint32_t operator()(const TriangleOrder& value)const{return ~value.sequence;}};
struct DepthKey {std::uint32_t operator()(const TriangleOrder& value)const{std::uint32_t bits=0;if(value.depth!=0)std::memcpy(&bits,&value.depth,4);return ~bits;}};
struct BucketKey {std::uint32_t operator()(const TriangleOrder& value)const{return std::uint16_t(~value.bucket);}};
template<class Key> inline void StableRadixKey(std::vector<TriangleOrder>& order,std::vector<TriangleOrder>& scratch,Key key,unsigned keyBits){
    constexpr unsigned radixBits=11,radixSize=1u<<radixBits,mask=radixSize-1;
    for(unsigned shift=0;shift<keyBits;shift+=radixBits){
        std::uint32_t counts[radixSize]{};unsigned populated=0;
        for(const auto& value:order){auto& count=counts[(key(value)>>shift)&mask];populated+=count==0;++count;}
        if(populated<2)continue;
        std::uint32_t next=0;for(auto& count:counts){const auto size=count;count=next;next+=size;}
        for(const auto& value:order)scratch[counts[(key(value)>>shift)&mask]++]=value;
        order.swap(scratch);
    }
}
inline void StableSortTriangleOrder(std::vector<TriangleOrder>& order,bool sortDepth,std::vector<TriangleOrder>& scratch){
    auto comparator=[&](const TriangleOrder& a,const TriangleOrder& b){
        if(a.bucket!=b.bucket)return a.bucket>b.bucket;
        if(sortDepth&&a.depth!=b.depth)return a.depth>b.depth;
        return a.sequence>b.sequence;
    };
    const bool validDepth=!sortDepth||std::all_of(order.begin(),order.end(),[](const TriangleOrder& value){return std::isfinite(value.depth)&&value.depth>=0;});
    if(order.size()<64||!validDepth){std::stable_sort(order.begin(),order.end(),comparator);return;}
    try {scratch.resize(order.size());}
    catch(const std::bad_alloc&){std::stable_sort(order.begin(),order.end(),comparator);return;}
    if(std::is_sorted(order.begin(),order.end(),[](const TriangleOrder& a,const TriangleOrder& b){return a.sequence<b.sequence;})){
        // Input in nondecreasing source sequence: the stable descending-sequence pass equals the groups of
        // equal sequence in reverse order with their inner order kept (one linear pass, same result).
        std::size_t out=0;
        for(std::size_t end=order.size();end>0;){std::size_t begin=end-1;while(begin>0&&order[begin-1].sequence==order[end-1].sequence)--begin;
            for(std::size_t i=begin;i<end;++i)scratch[out++]=order[i];end=begin;}
        order.swap(scratch);
    }else StableRadixKey(order,scratch,SequenceKey{},32);
    if(sortDepth)StableRadixKey(order,scratch,DepthKey{},32);
    StableRadixKey(order,scratch,BucketKey{},16);
}

struct PreparedVertexCache {
    ProjectedPoint projected{};
    DrawVertex draw{};
    std::uint32_t material=(std::numeric_limits<std::uint32_t>::max)();
    bool projectedReady=false;
};
// Fix 2026-10-25d (perf): the per-call buffers of PrepareFrame/BuildFrame live on in the calling thread and are refilled
// every call instead of being allocated and zero-filled again for every actor every frame (the 32-bit heap hands blocks
// above ~0.5 MB straight to VirtualAlloc: ~14k page faults per frame with Jill + 8 zombies). Output bytes are unchanged.
struct PrepareChunk {std::vector<Triangle> triangles;std::uint32_t culled=0,clipped=0;const char* problem=nullptr;};
struct PrepareScratch {
    std::vector<Vec3> cameraVertices;std::vector<std::uint32_t> canonical,firstMaterial;std::vector<std::uint8_t> copied;
    std::vector<PreparedVertexCache> cache;std::vector<PrepareChunk> chunks;std::vector<Triangle> triangles;
    std::vector<const Triangle*> pointers;std::vector<TriangleOrder> order,sortScratch;
};
inline PrepareScratch& Scratch(){thread_local PrepareScratch scratch;return scratch;}
}
// Vertex arrays of finished frames (GpuFrame::cpuVertices) come back here and are handed to the next BuildFrame of a
// similar size, so the expanded triangle vertices are not a fresh VirtualAlloc for every actor every frame either.
class VertexRecycler {
    std::mutex mutex_;std::vector<std::vector<DrawVertex>> free_;std::size_t bytes_=0;
    static constexpr std::size_t kMaxBytes=32u<<20,kMaxArray=16u<<20;
public:
    static VertexRecycler& Get(){static VertexRecycler* recycler=new VertexRecycler;return *recycler;}   // never destroyed
    std::vector<DrawVertex> Take(std::size_t count){
        std::lock_guard<std::mutex> lock(mutex_);std::size_t best=free_.size();
        for(std::size_t i=0;i<free_.size();++i)if(free_[i].capacity()>=count&&(best==free_.size()||free_[i].capacity()<free_[best].capacity()))best=i;
        if(best==free_.size())return {};
        std::vector<DrawVertex> out=std::move(free_[best]);free_.erase(free_.begin()+std::ptrdiff_t(best));bytes_-=out.capacity()*sizeof(DrawVertex);return out;
    }
    void Give(std::vector<DrawVertex>&& array){
        const std::size_t bytes=array.capacity()*sizeof(DrawVertex);if(!bytes||bytes>kMaxArray)return;
        array.clear();std::lock_guard<std::mutex> lock(mutex_);
        while(!free_.empty()&&bytes_+bytes>kMaxBytes){bytes_-=free_.front().capacity()*sizeof(DrawVertex);free_.erase(free_.begin());}
        try{free_.push_back(std::move(array));bytes_+=bytes;}catch(...){}
    }
};
#define RE3_VERTEX_RECYCLER 1

#ifdef RE3_PROFILE_PREPARE
struct PrepareProfile{double camera=0,triangles=0,sort=0,build=0;};
inline PrepareProfile& PrepareProfileData(){static PrepareProfile data;return data;}
#define RE3_PROFILE_MARK(name) const auto name=std::chrono::high_resolution_clock::now()
#define RE3_PROFILE_ADD(field,a,b) PrepareProfileData().field+=std::chrono::duration<double,std::milli>(b-a).count()
#else
#define RE3_PROFILE_MARK(name)
#define RE3_PROFILE_ADD(field,a,b)
#endif
namespace detail {
// Sorting the 100-byte payload moves all three expanded vertices repeatedly.
// Keep geometry stationary and stably sort its compact 16-byte depth keys.
template<class Get> inline void BuildFrame(std::size_t count,Get triangleAt,const Projection& p,FrameGeometry& frame){
    RE3_PROFILE_MARK(profileSortStart);
    auto& scratch=Scratch();auto& order=scratch.order;order.clear();order.reserve(count);
    for(std::uint32_t i=0;i<count;++i){const Triangle& triangle=triangleAt(i);order.push_back({triangle.bucket,triangle.depth,triangle.sequence,i});}
    StableSortTriangleOrder(order,p.sortWithinBucketByDepth||p.sortWithinBucketByMaterial,scratch.sortScratch);
    RE3_PROFILE_MARK(profileSort);RE3_PROFILE_ADD(sort,profileSortStart,profileSort);
    // Slices and runs in one cheap pass; the 28-byte vertices are then copied in chunks (same layout).
    for(std::size_t n=0;n<order.size();++n){const Triangle& triangle=triangleAt(order[n].index);
        if(frame.slices.empty()||frame.slices.back().bucket!=triangle.bucket)
            frame.slices.push_back({triangle.bucket,std::uint32_t(frame.runs.size()),0});
        auto& slice=frame.slices.back();
        if(!slice.runCount||frame.runs.back().materialIndex!=triangle.material){
            frame.runs.push_back({std::uint32_t(n*3),0,triangle.material});++slice.runCount;
        }
        frame.runs.back().vertexCount+=3;
    }
    frame.vertices=VertexRecycler::Get().Take(order.size()*3);frame.vertices.resize(order.size()*3);
    if(p.recordSources)frame.sources.resize(order.size()*3);
    auto copy=[&](std::size_t,std::size_t begin,std::size_t end){for(std::size_t n=begin;n<end;++n){const Triangle& t=triangleAt(order[n].index);std::memcpy(&frame.vertices[n*3],t.v,3*sizeof(DrawVertex));
        if(p.recordSources)std::memcpy(&frame.sources[n*3],t.src,3*sizeof(VertexSource));}};
    if(order.size()>=4096)parallel::For(order.size(),2048,copy);else copy(0,0,order.size());   // was >=16384: zombies (16k) copied on one thread
    RE3_PROFILE_MARK(profileBuild);RE3_PROFILE_ADD(build,profileSort,profileBuild);
}
// Same result as the sequential PrepareFrame body for pure shading and the built-in depth bucket (checked
// byte for byte on recorded poses by static3d/perf). Vertex work runs once per canonical vertex on the
// worker pool; triangles run in chunks with a read-only vertex cache and are concatenated in source order.
inline bool PrepareFrameParallel(const Mesh& mesh,const std::vector<SkinnedVertex>& skinned,const Projection& p,FrameGeometry& output,std::string* error){
    auto fail=[&](const char* text){if(error)*error=text;return false;};
    RE3_PROFILE_MARK(profileStart);
    const std::size_t count=skinned.size();
    FrameGeometry frame;frame.sourceTriangles=std::uint32_t(mesh.indices.size()/3);
    auto& scratch=Scratch();
    auto& cameraVertices=scratch.cameraVertices;cameraVertices.resize(count);auto& canonical=scratch.canonical;canonical.resize(count);
    auto& copied=scratch.copied;copied.assign(count,0);
    const bool hasCanonical=mesh.canonicalVertex.size()==count;
    std::atomic<bool> nonfinite{false};
    if(!parallel::For(count,1024,[&](std::size_t,std::size_t begin,std::size_t end){
        for(std::size_t i=begin;i<end;++i){const auto& v=skinned[i];
            const auto representative=hasCanonical?mesh.canonicalVertex[i]:std::uint32_t(i);
            if(representative<i&&!std::memcmp(&v.position,&skinned[representative].position,24)){copied[i]=1;continue;}
            const auto point=TransformPoint(p.camera,v.position);if(!Finite(point)){nonfinite=true;return;}
            cameraVertices[i]=point;canonical[i]=std::uint32_t(i);
        }
    })||nonfinite)return fail("Nonfinite camera vertex");
    for(std::size_t i=0;i<count;++i)if(copied[i]){const auto representative=mesh.canonicalVertex[i];cameraVertices[i]=cameraVertices[representative];canonical[i]=canonical[representative];}
    RE3_PROFILE_MARK(profileCamera);RE3_PROFILE_ADD(camera,profileStart,profileCamera);
    // The first material that uses each canonical vertex in an unclipped triangle gets its draw vertex cached.
    const std::uint32_t none=(std::numeric_limits<std::uint32_t>::max)();
    auto& firstMaterial=scratch.firstMaterial;firstMaterial.assign(count,none);
    struct Range {std::uint32_t material,firstIndex,triangles,firstTriangle;};std::vector<Range> ranges;std::uint32_t totalTriangles=0;
    for(std::size_t mi=0;mi<mesh.materials.size();++mi){const auto& material=mesh.materials[mi];
        if(MaterialHidden(p,mi)||!material.indexCount)continue;
        ranges.push_back({std::uint32_t(mi),material.firstIndex,material.indexCount/3,totalTriangles});totalTriangles+=material.indexCount/3;
        for(std::uint32_t index=material.firstIndex;index<material.firstIndex+material.indexCount;index+=3){
            if(TriangleHidden(p,index/3))continue;
            bool unclipped=true;for(unsigned k=0;k<3;++k)unclipped=unclipped&&cameraVertices[mesh.indices[index+k]].z>=p.nearPlane;
            if(unclipped)for(unsigned k=0;k<3;++k){auto& first=firstMaterial[canonical[mesh.indices[index+k]]];if(first==none)first=std::uint32_t(mi);}
        }
    }
    // Every entry read below is written first (vertices of visible unclipped triangles), so stale entries are harmless.
    auto& cache=scratch.cache;cache.resize(count);std::atomic<bool> badProjection{false};
    auto drawVertex=[&](const Vec3& world,const Vec3& normal,const Vec2& uv,const ProjectedPoint& projected,const Material& material,std::uint32_t vertex){
        const auto color=ShadeAt(p,world,normal,material,vertex);const auto tint=VertexTint(p,vertex);
        const float renderZ=(std::max)(projected.z,p.nearPlane);
        auto diffuse=MultiplyColor(color,p.tintARGB);if(tint!=0xffffffffu)diffuse=MultiplyColor(diffuse,tint);
        return DrawVertex{projected.x,projected.y,1.f-p.nearPlane/renderZ,p.perspectiveCorrect?1.f/renderZ:1.f,diffuse,uv.x,uv.y};
    };
    if(!parallel::For(count,1024,[&](std::size_t,std::size_t begin,std::size_t end){
        for(std::size_t c=begin;c<end;++c){if(firstMaterial[c]==none)continue;auto& cached=cache[c];
            if(!ProjectCameraPoint(cameraVertices[c],p,cached.projected)){badProjection=true;return;}
            cached.projectedReady=true;
            cached.draw=drawVertex(skinned[c].position,skinned[c].normal,skinned[c].uv,cached.projected,mesh.materials[firstMaterial[c]],std::uint32_t(c));cached.material=firstMaterial[c];
        }
    })||badProjection)return fail("Projection produced nonfinite screen coordinates");
    const std::size_t grain=1024;   // 1024 (was 4096): a 16k-triangle zombie used 4 of 8 threads
   auto& chunks=scratch.chunks;chunks.resize((totalTriangles+grain-1)/grain);
    for(auto& chunk:chunks){chunk.triangles.clear();chunk.culled=chunk.clipped=0;chunk.problem=nullptr;}
    if(!parallel::For(totalTriangles,grain,[&](std::size_t chunkIndex,std::size_t begin,std::size_t end){
        auto& out=chunks[chunkIndex];out.triangles.reserve(end-begin);std::size_t r=0;
        for(std::size_t sequence=begin;sequence<end;++sequence){
            while(sequence>=std::size_t(ranges[r].firstTriangle)+ranges[r].triangles)++r;
            const std::uint32_t mi=ranges[r].material;const auto& material=mesh.materials[mi];
            const std::uint32_t index=ranges[r].firstIndex+3*std::uint32_t(sequence-ranges[r].firstTriangle);
            if(TriangleHidden(p,index/3))continue;
            ClipVertex original[3];bool unclipped=true;
            for(unsigned k=0;k<3;++k){const auto vi=mesh.indices[index+k];const auto& v=skinned[vi];original[k]={cameraVertices[vi],v.position,v.normal,v.uv,vi};unclipped=unclipped&&cameraVertices[vi].z>=p.nearPlane;}
            ClipVertex clipped[6];const unsigned clipCount=unclipped?3:ClipNear(original,3,p.nearPlane,clipped);
            if(clipCount<3){++out.culled;continue;}if(clipCount!=3)++out.clipped;
            for(unsigned fan=1;fan+1<clipCount;++fan){
                const ClipVertex* source[3]={unclipped?&original[0]:&clipped[0],unclipped?&original[1]:&clipped[fan],unclipped?&original[2]:&clipped[fan+1]};
                ProjectedPoint projected[3];bool valid=true;
                for(unsigned k=0;k<3;++k){
                    if(unclipped)projected[k]=cache[canonical[mesh.indices[index+k]]].projected;
                    else if(!ProjectCameraPoint(source[k]->camera,p,projected[k]))valid=false;
                }
                if(!valid){out.problem="Projection produced nonfinite screen coordinates";return;}
                const float area=(projected[1].x-projected[0].x)*(projected[2].y-projected[0].y)-(projected[1].y-projected[0].y)*(projected[2].x-projected[0].x);
                const bool sided=(material.flags&TwoSided)!=0;
                if(std::abs(area)<1.e-8f||(!sided&&((p.cull==FaceCull::Clockwise&&area>0)||(p.cull==FaceCull::CounterClockwise&&area<0)))){++out.culled;continue;}
                Triangle triangle{};triangle.material=mi;triangle.sequence=std::uint32_t(sequence);
                triangle.bucket=DepthBucket(projected[0].depth,projected[1].depth,projected[2].depth,p.zsf3);
                if(triangle.bucket>=p.bucketLimit){out.problem="Depth bucket exceeds the adapter's safe OT range";return;}
                triangle.depth=p.sortWithinBucketByMaterial?float(mi):projected[0].z+projected[1].z+projected[2].z;
                for(unsigned k=0;k<3;++k){
                    triangle.src[k]=SourceOf(*source[k]);
                    if(unclipped){const auto& cached=cache[canonical[mesh.indices[index+k]]];
                        if(cached.material==mi){triangle.v[k]=cached.draw;triangle.v[k].u=source[k]->uv.x;triangle.v[k].v=source[k]->uv.y;continue;}}
                    triangle.v[k]=drawVertex(source[k]->world,source[k]->normal,source[k]->uv,projected[k],material,mesh.indices[unclipped?index+k:index]);
                }
                out.triangles.push_back(triangle);
            }
        }
    }))return fail("Prepare allocation failed");
    std::size_t produced=0;
    for(const auto& chunk:chunks){if(chunk.problem)return fail(chunk.problem);produced+=chunk.triangles.size();frame.culledTriangles+=chunk.culled;frame.clippedTriangles+=chunk.clipped;}
    if(produced>p.maxOutputTriangles)return fail("Prepared triangle budget exceeded");
    // Chunks stay where they are; a pointer per triangle gives the concatenated source order.
    auto& triangles=scratch.pointers;triangles.clear();triangles.reserve(produced);
    for(const auto& chunk:chunks)for(const auto& triangle:chunk.triangles)triangles.push_back(&triangle);
    RE3_PROFILE_MARK(profileTriangles);RE3_PROFILE_ADD(triangles,profileCamera,profileTriangles);
    BuildFrame(triangles.size(),[&](std::size_t i)->const Triangle&{return *triangles[i];},p,frame);
    output=std::move(frame);if(error)error->clear();return true;
}
} // namespace detail
inline bool PrepareFrame(const Mesh& mesh,const std::vector<SkinnedVertex>& skinned,const Projection& p,FrameGeometry& output,std::string* error=nullptr){
    auto fail=[&](const char* text){if(error)*error=text;return false;};
    if(skinned.size()!=mesh.vertices.size()||!Finite(p.camera)||!std::isfinite(p.nearPlane)||p.nearPlane<1||
       !std::isfinite(p.scaleX)||!std::isfinite(p.scaleY)||p.scaleX<=0||p.scaleY<=0||!p.maxOutputTriangles||p.maxOutputTriangles>1000000||!p.bucketLimit||p.bucketLimit>8192)return fail("Invalid projection/geometry inputs");
    // Large actors with pure shading and the built-in depth bucket take the pooled path (same bytes).
    if(skinned.size()>=8192&&(!p.shade||p.shade==ShadeNativeNccs||p.cachePureShade)&&!p.depth&&!p.triangleDepth&&parallel::Pool::Get().Workers())
        return detail::PrepareFrameParallel(mesh,skinned,p,output,error);
    RE3_PROFILE_MARK(profileStart);
    FrameGeometry frame;frame.sourceTriangles=std::uint32_t(mesh.indices.size()/3);
    auto& scratch=detail::Scratch();
    auto& triangles=scratch.triangles;triangles.clear();triangles.reserve((std::min)(std::size_t(p.maxOutputTriangles),mesh.indices.size()/3));
    auto& cameraVertices=scratch.cameraVertices;cameraVertices.clear();cameraVertices.reserve(skinned.size());
    auto& canonical=scratch.canonical;canonical.resize(skinned.size());
    const bool hasCanonical=mesh.canonicalVertex.size()==skinned.size();
    for(std::size_t i=0;i<skinned.size();++i){const auto& v=skinned[i];
        const auto representative=hasCanonical?mesh.canonicalVertex[i]:std::uint32_t(i);
        // PrepareFrame also accepts externally supplied skinned geometry. Only
        // reuse when its actual PN bytes match, independent of the bind mesh.
        if(representative<i&&!std::memcmp(&v.position,&skinned[representative].position,24)){
            cameraVertices.push_back(cameraVertices[representative]);canonical[i]=canonical[representative];
        }else{
            const auto point=TransformPoint(p.camera,v.position);if(!Finite(point))return fail("Nonfinite camera vertex");
            cameraVertices.push_back(point);canonical[i]=std::uint32_t(i);
        }
    }
    RE3_PROFILE_MARK(profileCamera);RE3_PROFILE_ADD(camera,profileStart,profileCamera);
    // Most triangles never cross the near plane. Their indexed vertices can
    // reuse projection and material-dependent lighting within this one frame.
    // Interpolated clip vertices retain the original uncached evaluation path.
    auto& vertexCache=scratch.cache;vertexCache.assign(skinned.size(),detail::PreparedVertexCache{});
    const bool cacheShade=!p.shade||p.shade==ShadeNativeNccs||p.cachePureShade;
    std::uint32_t sequence=0;
    for(std::size_t mi=0;mi<mesh.materials.size();++mi){const auto& material=mesh.materials[mi];
        if(MaterialHidden(p,mi)){sequence+=material.indexCount/3;continue;}
        for(std::uint32_t index=material.firstIndex;index<material.firstIndex+material.indexCount;index+=3,++sequence){
            if(TriangleHidden(p,index/3))continue;
            detail::ClipVertex original[3];
            bool unclipped=true;
            for(unsigned k=0;k<3;++k){const auto vi=mesh.indices[index+k];const auto& v=skinned[vi];original[k]={cameraVertices[vi],v.position,v.normal,v.uv,vi};unclipped=unclipped&&cameraVertices[vi].z>=p.nearPlane;}
            detail::ClipVertex clipped[6];const unsigned count=unclipped?3:detail::ClipNear(original,3,p.nearPlane,clipped);
            if(count<3){++frame.culledTriangles;continue;}if(count!=3)++frame.clippedTriangles;
            for(unsigned fan=1;fan+1<count;++fan){
                const detail::ClipVertex* source[3]={unclipped?&original[0]:&clipped[0],unclipped?&original[1]:&clipped[fan],unclipped?&original[2]:&clipped[fan+1]};
                ProjectedPoint projected[3];bool valid=true;
                for(unsigned k=0;k<3;++k){
                    if(unclipped){auto& cached=vertexCache[canonical[mesh.indices[index+k]]];
                        if(!cached.projectedReady){if(!ProjectCameraPoint(source[k]->camera,p,cached.projected))valid=false;else cached.projectedReady=true;}
                        projected[k]=cached.projected;
                    }else if(!ProjectCameraPoint(source[k]->camera,p,projected[k]))valid=false;
                }
                if(!valid)return fail("Projection produced nonfinite screen coordinates");
                const float area=(projected[1].x-projected[0].x)*(projected[2].y-projected[0].y)-(projected[1].y-projected[0].y)*(projected[2].x-projected[0].x);
                const bool sided=(material.flags&TwoSided)!=0;
                if(std::abs(area)<1.e-8f||(!sided&&((p.cull==FaceCull::Clockwise&&area>0)||(p.cull==FaceCull::CounterClockwise&&area<0)))){++frame.culledTriangles;continue;}
                detail::Triangle triangle{};triangle.material=std::uint32_t(mi);triangle.sequence=sequence;
                triangle.bucket=p.triangleDepth?p.triangleDepth(p.triangleDepthContext,index/3,projected[0].depth,projected[1].depth,projected[2].depth):
                    p.depth?p.depth(p.depthContext,projected[0].depth,projected[1].depth,projected[2].depth):DepthBucket(projected[0].depth,projected[1].depth,projected[2].depth,p.zsf3);
                if(triangle.bucket>=p.bucketLimit)return fail("Depth bucket exceeds the adapter's safe OT range");
                triangle.depth=p.sortWithinBucketByMaterial?float(mi):projected[0].z+projected[1].z+projected[2].z;
                for(unsigned k=0;k<3;++k){
                    triangle.src[k]=detail::SourceOf(*source[k]);
                    auto* cached=unclipped&&cacheShade?&vertexCache[canonical[mesh.indices[index+k]]]:nullptr;
                    if(cached&&cached->material==mi){triangle.v[k]=cached->draw;triangle.v[k].u=source[k]->uv.x;triangle.v[k].v=source[k]->uv.y;continue;}
                    const auto color=ShadeAt(p,source[k]->world,source[k]->normal,material,mesh.indices[unclipped?index+k:index]);
                    const float renderZ=(std::max)(projected[k].z,p.nearPlane);
                    const auto tint=VertexTint(p,mesh.indices[unclipped?index+k:index]);
                    auto diffuse=MultiplyColor(color,p.tintARGB);if(tint!=0xffffffffu)diffuse=MultiplyColor(diffuse,tint);
                    triangle.v[k]={projected[k].x,projected[k].y,1.f-p.nearPlane/renderZ,p.perspectiveCorrect?1.f/renderZ:1.f,
                        diffuse,source[k]->uv.x,source[k]->uv.y};
                    if(cached){cached->draw=triangle.v[k];cached->material=std::uint32_t(mi);}
                }
                if(triangles.size()>=p.maxOutputTriangles)return fail("Prepared triangle budget exceeded");
                triangles.push_back(triangle);
            }
        }
    }
    RE3_PROFILE_MARK(profileTriangles);RE3_PROFILE_ADD(triangles,profileCamera,profileTriangles);
    detail::BuildFrame(triangles.size(),[&](std::size_t i)->const detail::Triangle&{return triangles[i];},p,frame);
    output=std::move(frame);if(error)error->clear();return true;
}

// For native door/effect matrices that already include the camera. This helper
// deliberately clears Projection.camera to avoid applying the view twice.
// The callback's position/normal space is camera space for this entry point.
inline bool PrepareRigidByCameraMatrix(const Mesh& mesh,const Matrix3x4& cameraMatrix,const Projection& options,FrameGeometry& output,std::string* error=nullptr){
    if(mesh.inverseBind.size()!=1){if(error)*error="Rigid camera-space mesh requires one bone";return false;}
    std::vector<SkinnedVertex> skinned;
    if(!Skin(mesh,std::vector<Matrix3x4>{cameraMatrix},skinned,error))return false;
    Projection projection=options;projection.camera=Matrix3x4{};
    return PrepareFrame(mesh,skinned,projection,output,error);
}

} // namespace re3model
