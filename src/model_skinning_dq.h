#pragma once

// Optional CPU dual-quaternion skinning. This header does not change the default
// Skin() path, native animation matrices, weights, or binding. Callers may fall
// back to Skin() when a pose has genuine scale/shear and this function rejects it.
#include "model_runtime.h"

namespace re3model {

namespace skinning_dq_detail {

// Native 12-bit fixed-point rotation concatenation is slightly nonorthogonal.
// Captured Jill skin transforms reach max|R^T R-I|=0.0069431. An exact native
// fixed12 sweep of 1,068 distinct original poses at all 4,096 headings reached
// 0.0061998; an additional 4,096 composed XYZ actor-rotation stress sweep reached
// 0.0073673. Allow 64 fixed-point steps (0.015625), over twice that measured
// bound, so ordinary rounding cannot toggle DQ/LBS near the acceptance limit.
// This covers the tested pose/rotation domain, not arbitrary scaled animations.
// Larger scale/shear and all reflections fail (even 1% uniform scale fails).
// Accepted roundoff is removed by conversion to a unit quaternion. This is not
// a general affine DQ scheme: intentional scale needs the existing LBS path.
constexpr double kRigidityTolerance=64.0/4096.0;

struct Quaternion {double w=0,x=0,y=0,z=0;};
struct DualQuaternion {Quaternion real,dual;};

inline Quaternion Add(Quaternion a,Quaternion b){return {a.w+b.w,a.x+b.x,a.y+b.y,a.z+b.z};}
inline Quaternion Scale(Quaternion q,double s){return {q.w*s,q.x*s,q.y*s,q.z*s};}
inline double Dot(Quaternion a,Quaternion b){return a.w*b.w+a.x*b.x+a.y*b.y+a.z*b.z;}
inline Quaternion Conjugate(Quaternion q){return {q.w,-q.x,-q.y,-q.z};}
inline Quaternion Product(Quaternion a,Quaternion b){return {
    a.w*b.w-a.x*b.x-a.y*b.y-a.z*b.z,
    a.w*b.x+a.x*b.w+a.y*b.z-a.z*b.y,
    a.w*b.y-a.x*b.z+a.y*b.w+a.z*b.x,
    a.w*b.z+a.x*b.y-a.y*b.x+a.z*b.w};}
inline bool Finite(Quaternion q){return std::isfinite(q.w)&&std::isfinite(q.x)&&std::isfinite(q.y)&&std::isfinite(q.z);}

inline bool FromRigidMatrix(const Matrix3x4& matrix,DualQuaternion& output){
    if(!re3model::Finite(matrix))return false;
    double r[3][3]{};
    for(unsigned row=0;row<3;++row)for(unsigned col=0;col<3;++col)r[row][col]=matrix.m[row*4+col];
    for(unsigned i=0;i<3;++i)for(unsigned j=i;j<3;++j){
        const double gram=r[0][i]*r[0][j]+r[1][i]*r[1][j]+r[2][i]*r[2][j];
        if(std::abs(gram-(i==j?1.0:0.0))>kRigidityTolerance)return false;
    }
    const double determinant=r[0][0]*(r[1][1]*r[2][2]-r[1][2]*r[2][1])
        -r[0][1]*(r[1][0]*r[2][2]-r[1][2]*r[2][0])+r[0][2]*(r[1][0]*r[2][1]-r[1][1]*r[2][0]);
    if(!(determinant>0))return false;
    Quaternion q;const double trace=r[0][0]+r[1][1]+r[2][2];
    // Largest-diagonal branches remain well-conditioned at/near 180 degrees.
    if(trace>0){
        const double s=2*std::sqrt(trace+1);
        q={.25*s,(r[2][1]-r[1][2])/s,(r[0][2]-r[2][0])/s,(r[1][0]-r[0][1])/s};
    }else if(r[0][0]>=r[1][1]&&r[0][0]>=r[2][2]){
        const double s=2*std::sqrt(1+r[0][0]-r[1][1]-r[2][2]);
        q={(r[2][1]-r[1][2])/s,.25*s,(r[0][1]+r[1][0])/s,(r[0][2]+r[2][0])/s};
    }else if(r[1][1]>=r[2][2]){
        const double s=2*std::sqrt(1+r[1][1]-r[0][0]-r[2][2]);
        q={(r[0][2]-r[2][0])/s,(r[0][1]+r[1][0])/s,.25*s,(r[1][2]+r[2][1])/s};
    }else{
        const double s=2*std::sqrt(1+r[2][2]-r[0][0]-r[1][1]);
        q={(r[1][0]-r[0][1])/s,(r[0][2]+r[2][0])/s,(r[1][2]+r[2][1])/s,.25*s};
    }
    const double lengthSquared=Dot(q,q);
    if(!Finite(q)||!(lengthSquared>1.e-20))return false;
    q=Scale(q,1/std::sqrt(lengthSquared));
    const Quaternion translation{0,matrix.m[3],matrix.m[7],matrix.m[11]};
    output={q,Scale(Product(translation,q),.5)};
    return Finite(output.dual);
}

inline Vec3 Rotate(Quaternion q,Vec3 vector){
    // Unit-quaternion rotation v + 2*q.xyz cross(q.xyz cross v + q.w*v).
    const double tx=2*(q.y*vector.z-q.z*vector.y);
    const double ty=2*(q.z*vector.x-q.x*vector.z);
    const double tz=2*(q.x*vector.y-q.y*vector.x);
    return {float(vector.x+q.w*tx+q.y*tz-q.z*ty),
        float(vector.y+q.w*ty+q.z*tx-q.x*tz),float(vector.z+q.w*tz+q.x*ty-q.y*tx)};
}

} // namespace skinning_dq_detail

// Pose-only eligibility check for an explicit HD LBS fallback after DQ rejects
// native affine animation (for example the original entrywise leg blend).
// The mesh must still have passed LoadMesh validation. No tolerance is widened,
// no input matrix is changed, and an entirely rigid pose is not a fallback case.
inline bool CanUseAffineSkinFallback(const Mesh& mesh,const std::vector<Matrix3x4>& worldBones,
    std::string* error=nullptr){
    auto fail=[&](const char* message){if(error)*error=message;return false;};
    if(mesh.inverseBind.empty()||mesh.inverseBind.size()>256||worldBones.size()<mesh.inverseBind.size())
        return fail("Affine fallback missing or excessive pose matrices");
    bool needsAffine=false;
    for(std::size_t bone=0;bone<mesh.inverseBind.size();++bone){
        if(!Finite(worldBones[bone])||!Finite(mesh.inverseBind[bone]))return fail("Affine fallback nonfinite pose");
        const Matrix3x4 transform=Multiply(worldBones[bone],mesh.inverseBind[bone]);
        if(!Finite(transform))return fail("Affine fallback nonfinite skin transform");
        const Vec3 x{transform.m[0],transform.m[1],transform.m[2]},
            y{transform.m[4],transform.m[5],transform.m[6]},z{transform.m[8],transform.m[9],transform.m[10]};
        const float determinant=Dot(x,Cross(y,z));
        // Match NormalMatrix's singularity floor, and additionally exclude all
        // orientation-reversing transforms. Do not hide invalid pose data.
        if(!std::isfinite(determinant)||determinant<=1.e-12f)
            return fail("Affine fallback singular or orientation-reversing transform");
        Matrix3x4 normal;
        if(!NormalMatrix(transform,normal))return fail("Affine fallback invalid normal transform");
        skinning_dq_detail::DualQuaternion rigid;
        if(!skinning_dq_detail::FromRigidMatrix(transform,rigid))needsAffine=true;
    }
    if(!needsAffine)return fail("Affine fallback not needed for a rigid pose");
    if(error)error->clear();return true;
}

inline bool SkinDualQuaternion(const Mesh& mesh,const std::vector<Matrix3x4>& worldBones,
    std::vector<SkinnedVertex>& output,std::string* error=nullptr){
    using namespace skinning_dq_detail;
    auto fail=[&](const char* message){if(error)*error=message;return false;};
    try {
        if(mesh.inverseBind.empty()||worldBones.size()<mesh.inverseBind.size())return fail("DQ missing native pose matrices");
        std::vector<DualQuaternion> transforms(mesh.inverseBind.size());
        for(std::size_t bone=0;bone<transforms.size();++bone){
            // Match Skin(): column-vector model bind -> world, with no second
            // camera transform and no alteration of the supplied native pose.
            if(!FromRigidMatrix(Multiply(worldBones[bone],mesh.inverseBind[bone]),transforms[bone]))
                return fail("DQ requires finite rigid skin transforms within native fixed-point tolerance");
        }
        std::vector<SkinnedVertex> result;result.reserve(mesh.vertices.size());
        for(std::size_t vertexIndex=0;vertexIndex<mesh.vertices.size();++vertexIndex){const auto& vertex=mesh.vertices[vertexIndex];
            if(!re3model::Finite(vertex.position)||!re3model::Finite(vertex.normal)||
                re3model::Dot(vertex.normal,vertex.normal)<1.e-20f||!std::isfinite(vertex.uv.x)||!std::isfinite(vertex.uv.y))
                return fail("DQ nonfinite or degenerate vertex");
            const auto representative=CanonicalSkinVertex(mesh,vertexIndex);
            if(representative<vertexIndex){auto reused=result[representative];reused.uv=vertex.uv;result.push_back(reused);continue;}
            unsigned sum=0,reference=0;
            for(unsigned k=0;k<4;++k){
                if(vertex.bone[k]>=transforms.size())return fail("DQ bone index outside skeleton");
                sum+=vertex.weight[k];
                if(vertex.weight[k]>vertex.weight[reference])reference=k;
            }
            if(sum!=65535)return fail("DQ weights must sum to 65535");
            const Quaternion hemisphere=transforms[vertex.bone[reference]].real;
            Quaternion real{},dual{};
            for(unsigned k=0;k<4;++k)if(vertex.weight[k]){
                const auto& transform=transforms[vertex.bone[k]];
                double weight=double(vertex.weight[k])/65535.0;
                // Real and dual parts must flip together for antipodal inputs.
                if(Dot(hemisphere,transform.real)<0)weight=-weight;
                real=Add(real,Scale(transform.real,weight));dual=Add(dual,Scale(transform.dual,weight));
            }
            const double lengthSquared=Dot(real,real);
            if(!Finite(real)||!Finite(dual)||!(lengthSquared>1.e-20))return fail("DQ degenerate quaternion blend");
            const double inverseLength=1/std::sqrt(lengthSquared);
            real=Scale(real,inverseLength);dual=Scale(dual,inverseLength);
            // A rigid unit dual quaternion obeys dot(real,dual)==0.
            dual=Add(dual,Scale(real,-Dot(real,dual)));
            const Quaternion translation=Scale(Product(dual,Conjugate(real)),2);
            Vec3 position=Rotate(real,vertex.position);
            position={float(double(position.x)+translation.x),float(double(position.y)+translation.y),float(double(position.z)+translation.z)};
            const Vec3 normal=Rotate(real,vertex.normal);
            if(!re3model::Finite(position)||!re3model::Finite(normal))return fail("DQ produced nonfinite geometry");
            result.push_back({position,re3model::Normalize(normal),vertex.uv});
        }
        output=std::move(result);if(error)error->clear();return true;
    }catch(const std::exception&){return fail("DQ allocation or validation failed");}
}

} // namespace re3model
