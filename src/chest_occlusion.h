#pragma once

// The original 26-vertex lid controls only native OT ordering. HD geometry,
// projection, shading and GPU self-depth remain unchanged. This first mapping
// assigns whole HD triangles by their fixed local-space closest source face.
#include "model_runtime.h"
#include "chest_occlusion_data.h"
#include <array>
#include <climits>

namespace re3chest {
struct DepthState {
    std::array<std::uint16_t,48> buckets{};
    bool valid = false;
};
inline std::uint64_t HashByte(std::uint64_t hash,std::uint8_t value) {
    return (hash ^ value) * 1099511628211ull;
}
inline std::uint64_t HashWord(std::uint64_t hash,std::uint32_t value) {
    for(unsigned n=0;n<4;++n)hash=HashByte(hash,std::uint8_t(value>>(n*8)));
    return hash;
}
inline bool ValidateMesh(const re3model::Mesh& mesh) {
    if(mesh.vertices.size()!=data::kVertexCount || mesh.indices.size()!=data::kTriangleCount*3u || mesh.inverseBind.size()!=1) return false;
    const re3model::Matrix3x4 identity;
    for(unsigned i=0;i<12;++i)if(mesh.inverseBind[0].m[i]!=identity.m[i])return false;
    std::uint64_t hash=14695981039346656037ull;
    for(const auto& vertex:mesh.vertices) {
        const float position[3]={vertex.position.x,vertex.position.y,vertex.position.z};
        for(float value:position){std::uint32_t bits;std::memcpy(&bits,&value,sizeof(bits));hash=HashWord(hash,bits);}
    }
    for(auto index:mesh.indices)hash=HashWord(hash,index);
    return hash==data::kGeometryFnv;
}
inline std::int64_t Shift12(std::int64_t value) {
    return value>=0?value/4096:-((-value+4095)/4096);
}
template<class NativeMatrix>
inline bool PrepareDepthState(const NativeMatrix& camera,const NativeMatrix& world,
                              std::int16_t zsf3,DepthState& output) {
    output=DepthState{};
    if(zsf3<0)return false;
    // Native CompMatrix425270 ->507440 ->415D60 quantizes the combined rotation
    // before RTPT. Translation is cameraRotation*worldTranslation>>12+cameraT.
    // Only row2 is needed for source depth. Reject overflow rather than invent
    // a different transform for malformed or unsupported state.
    std::int16_t row[3]{};
    for(unsigned col=0;col<3;++col) {
        std::int64_t sum=0;
        for(unsigned k=0;k<3;++k)sum+=std::int64_t(camera.rotation[6+k])*world.rotation[k*3+col];
        if(sum<INT32_MIN||sum>INT32_MAX)return false;
        const auto value=Shift12(sum);if(value<INT16_MIN||value>INT16_MAX)return false;
        row[col]=std::int16_t(value);
    }
    std::int64_t translation=0;
    for(unsigned k=0;k<3;++k)translation+=std::int64_t(camera.rotation[6+k])*world.translation[k];
    translation=Shift12(translation)+camera.translation[2];
    if(translation<INT32_MIN||translation>INT32_MAX)return false;
    std::uint16_t depth[26]{};
    for(unsigned i=0;i<26;++i) {
        std::int64_t sum=0;
        for(unsigned k=0;k<3;++k)sum+=std::int64_t(row[k])*data::kProxyVertices[i][k];
        const auto z=Shift12(sum)+translation;
        depth[i]=std::uint16_t((std::max)(std::int64_t(0),(std::min)(std::int64_t(32767),z)));
    }
    for(unsigned i=0;i<48;++i) {
        const auto* triangle=data::kProxyTriangles[i];
        const auto bucket=re3model::DepthBucket(depth[triangle[0]],depth[triangle[1]],depth[triangle[2]],zsf3);
        if(bucket>=1024)return false;
        output.buckets[i]=bucket;
    }
    output.valid=true;return true;
}
inline std::uint16_t TriangleDepth(void* opaque,std::uint32_t sourceTriangle,
                                  std::uint16_t,std::uint16_t,std::uint16_t) {
    const auto* state=static_cast<const DepthState*>(opaque);
    if(!state||!state->valid||sourceTriangle>=data::kTriangleCount)return 65535;
    return state->buckets[data::kSourceForTriangle[sourceTriangle]];
}
} // namespace re3chest
