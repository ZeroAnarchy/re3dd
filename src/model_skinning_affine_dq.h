#pragma once
// Opt-in continuous affine dual-quaternion skinning. Native world*customInverse
// matrices remain authoritative; the existing rigid-DQ/LBS entry points are unchanged.
#include "model_skinning_dq.h"
#include "model_shoulder_corrective.h"

namespace re3affine {
using namespace re3model;
using skinning_dq_detail::Quaternion;
using skinning_dq_detail::DualQuaternion;
using skinning_dq_detail::Add;
using skinning_dq_detail::Scale;
using skinning_dq_detail::Product;
using skinning_dq_detail::Conjugate;
using skinning_dq_detail::Dot;

struct Mat3 {double m[9]{};};
struct Stretch {double xx=0,xy=0,xz=0,yy=0,yz=0,zz=0;};
struct Joint {DualQuaternion rigid;Stretch stretch;Matrix3x4 original,normal;};
struct Diagnostics {unsigned polarIterations=0;double maximumCondition=0,maximumReconstructionError=0;};
inline bool Finite(const Mat3& a){for(double x:a.m)if(!std::isfinite(x))return false;return true;}
inline double Determinant(const Mat3& a){const auto* m=a.m;return m[0]*(m[4]*m[8]-m[5]*m[7])-m[1]*(m[3]*m[8]-m[5]*m[6])+m[2]*(m[3]*m[7]-m[4]*m[6]);}
inline double NormSquared(const Mat3& a){double n=0;for(double x:a.m)n+=x*x;return n;}
inline bool InverseTranspose(const Mat3& a,Mat3& out){
    const auto* m=a.m;const double det=Determinant(a);if(!(det>0)||!std::isfinite(det))return false;
    const double inv=1/det;
    out={{(m[4]*m[8]-m[5]*m[7])*inv,(m[5]*m[6]-m[3]*m[8])*inv,(m[3]*m[7]-m[4]*m[6])*inv,
        (m[2]*m[7]-m[1]*m[8])*inv,(m[0]*m[8]-m[2]*m[6])*inv,(m[1]*m[6]-m[0]*m[7])*inv,
        (m[1]*m[5]-m[2]*m[4])*inv,(m[2]*m[3]-m[0]*m[5])*inv,(m[0]*m[4]-m[1]*m[3])*inv}};
    return Finite(out);
}
inline Quaternion RotationQuaternion(const Mat3& matrix){
    const auto* r=matrix.m;Quaternion q;const double trace=r[0]+r[4]+r[8];
    if(trace>0){const double s=2*std::sqrt(trace+1);q={.25*s,(r[7]-r[5])/s,(r[2]-r[6])/s,(r[3]-r[1])/s};}
    else if(r[0]>=r[4]&&r[0]>=r[8]){const double s=2*std::sqrt(1+r[0]-r[4]-r[8]);q={(r[7]-r[5])/s,.25*s,(r[1]+r[3])/s,(r[2]+r[6])/s};}
    else if(r[4]>=r[8]){const double s=2*std::sqrt(1+r[4]-r[0]-r[8]);q={(r[2]-r[6])/s,(r[1]+r[3])/s,.25*s,(r[5]+r[7])/s};}
    else{const double s=2*std::sqrt(1+r[8]-r[0]-r[4]);q={(r[3]-r[1])/s,(r[2]+r[6])/s,(r[5]+r[7])/s,.25*s};}
    return Scale(q,1/std::sqrt(Dot(q,q)));
}
inline bool Decompose(const Matrix3x4& matrix,Joint& out,Diagnostics* diagnostics=nullptr){
    if(!re3model::Finite(matrix))return false;
    Mat3 a;for(unsigned row=0;row<3;++row)for(unsigned col=0;col<3;++col)a.m[row*3+col]=matrix.m[row*4+col];
    // This is a safety bound on singular/ill-conditioned input, never a switch
    // between two valid deformation models. Reflection cannot have SPD stretch.
    const double det=Determinant(a);if(!(det>1.e-12)||!std::isfinite(det))return false;
    Mat3 inverse;if(!InverseTranspose(a,inverse))return false;
    const double condition=std::sqrt(NormSquared(a)*NormSquared(inverse));
    if(!std::isfinite(condition)||condition>1.e8)return false;
    Mat3 rotation=a;bool converged=false;unsigned iterations=0;
    for(;iterations<32;++iterations){
        if(!InverseTranspose(rotation,inverse))return false;
        const double gamma=std::sqrt(std::sqrt(NormSquared(inverse)/NormSquared(rotation)));
        if(!(gamma>0)||!std::isfinite(gamma))return false;
        Mat3 next;double delta=0;
        for(unsigned k=0;k<9;++k){next.m[k]=.5*(gamma*rotation.m[k]+inverse.m[k]/gamma);delta=(std::max)(delta,std::abs(next.m[k]-rotation.m[k]));}
        rotation=next;if(delta<1.e-13){converged=true;break;}
    }
    if(!converged||!Finite(rotation)||!(Determinant(rotation)>0))return false;
    Mat3 s;double orthogonalError=0;
    for(unsigned row=0;row<3;++row)for(unsigned col=0;col<3;++col){
        for(unsigned k=0;k<3;++k)s.m[row*3+col]+=rotation.m[k*3+row]*a.m[k*3+col];
        double gram=0;for(unsigned k=0;k<3;++k)gram+=rotation.m[k*3+row]*rotation.m[k*3+col];
        orthogonalError=(std::max)(orthogonalError,std::abs(gram-(row==col?1.:0.)));
    }
    if(orthogonalError>1.e-11)return false;
    for(unsigned row=0;row<3;++row)for(unsigned col=row+1;col<3;++col)s.m[row*3+col]=s.m[col*3+row]=.5*(s.m[row*3+col]+s.m[col*3+row]);
    if(!(s.m[0]>0)||!(s.m[0]*s.m[4]-s.m[1]*s.m[1]>0)||!(Determinant(s)>0))return false;
    double error=0;
    for(unsigned row=0;row<3;++row)for(unsigned col=0;col<3;++col){double reconstructed=0;for(unsigned k=0;k<3;++k)reconstructed+=rotation.m[row*3+k]*s.m[k*3+col];error=(std::max)(error,std::abs(reconstructed-a.m[row*3+col]));}
    if(error>1.e-10*(std::max)(1.,std::sqrt(NormSquared(a))))return false;
    const auto q=RotationQuaternion(rotation);
    out.rigid={q,Scale(Product(Quaternion{0,matrix.m[3],matrix.m[7],matrix.m[11]},q),.5)};
    out.stretch={s.m[0],s.m[1],s.m[2],s.m[4],s.m[5],s.m[8]};out.original=matrix;
    if(!NormalMatrix(matrix,out.normal)||!skinning_dq_detail::Finite(out.rigid.real)||!skinning_dq_detail::Finite(out.rigid.dual))return false;
    if(diagnostics){diagnostics->polarIterations+=iterations+1;diagnostics->maximumCondition=(std::max)(diagnostics->maximumCondition,condition);diagnostics->maximumReconstructionError=(std::max)(diagnostics->maximumReconstructionError,error);}
    return true;
}
inline void RotateDouble(Quaternion q,const double in[3],double out[3]){
    const double tx=2*(q.y*in[2]-q.z*in[1]),ty=2*(q.z*in[0]-q.x*in[2]),tz=2*(q.x*in[1]-q.y*in[0]);
    out[0]=in[0]+q.w*tx+q.y*tz-q.z*ty;out[1]=in[1]+q.w*ty+q.z*tx-q.x*tz;out[2]=in[2]+q.w*tz+q.x*ty-q.y*tx;
}
// only: skin just these vertices (GPU path: the body vertices an overlay rides on); the others keep what the array holds.
template<bool Corrected> inline bool SkinAffineDualQuaternionImpl(const Mesh& mesh,const std::vector<Matrix3x4>& worldBones,
    std::vector<SkinnedVertex>& output,std::string* error,Diagnostics* diagnostics,
    const re3shoulder::ShoulderCorrective* corrective,re3shoulder::Factors* usedFactors,const std::vector<std::uint32_t>* only=nullptr){
    auto fail=[&](const char* text){if(error)*error=text;return false;};
    try {
        re3shoulder::Factors factors;(void)factors;(void)corrective;(void)usedFactors;
        if constexpr(Corrected){if(!corrective||!corrective->Evaluate(mesh,worldBones,factors,error))return false;}
        if(mesh.inverseBind.empty()||mesh.inverseBind.size()>256||worldBones.size()<mesh.inverseBind.size())return fail("Affine DQ missing/excessive pose matrices");
        Matrix3x4 adjustedHead;bool changeHead=false;
        if constexpr(Corrected){const float headFactor=(std::min)(factors.raised[0],factors.raised[1]);
            if(headFactor>0&&corrective->HeadPitchLiftDegrees()!=0){
                const double angle=-double(corrective->HeadPitchLiftDegrees())*headFactor*(3.14159265358979323846/180.0);
                const float cosine=float(std::cos(angle)),sine=float(std::sin(angle));
                const Matrix3x4 rotation{{cosine,-sine,0,0,sine,cosine,0,0,0,0,1,0}};
                adjustedHead=Multiply(worldBones[1],rotation);changeHead=true;
            }
        }
        // The original pose array stays immutable. Exact zero takes the prior
        // head matrix; no copied pose vector or temporal state is introduced.
        std::vector<Joint> joints(mesh.inverseBind.size());
        for(std::size_t i=0;i<joints.size();++i){const auto& world=changeHead&&i==1?adjustedHead:worldBones[i];
            if(!Decompose(Multiply(world,mesh.inverseBind[i]),joints[i],diagnostics))return fail("Affine DQ requires finite positive well-conditioned skin transforms");
        }
        // Every vertex is independent except UV-split duplicates, which copy their representative. The
        // vertices run in chunks on the worker pool, duplicates are copied afterwards in index order and the
        // lowest failing index reports, so the output and errors equal the sequential loop's.
        const std::size_t count=mesh.vertices.size();
        std::vector<SkinnedVertex> result;result.swap(output);result.resize(count);std::vector<std::uint8_t> duplicate(only?0:count,0);   // caller's array reused
        auto vertex=[&](std::size_t i)->const char*{const auto& baseVertex=mesh.vertices[i];const Vertex* selected=&baseVertex;Vertex adjusted;
            if constexpr(Corrected){const auto* entry=corrective->ForVertex(i);
                // Exact zero leaves base PN bytes untouched. Loader validation
                // proves every canonical UV split receives identical deltas.
                const auto* raised=corrective->ForRaisedVertex(i);
                const float raisedFactor=raised?factors.raised[raised->side-2]:0;
                if(raisedFactor>0){const float lowFactor=entry?factors.side[entry->side]:0;if(!re3shoulder::ApplyCombined(entry,lowFactor,*raised,raisedFactor,baseVertex,adjusted))return "Raised shoulder corrective produced invalid vertex";selected=&adjusted;}
                else if(entry&&factors.side[entry->side]>0){if(!re3shoulder::ApplyEntry(*entry,factors.side[entry->side],baseVertex,adjusted))return "Shoulder corrective produced invalid vertex";selected=&adjusted;}
            }
            const auto& v=*selected;
            if(!re3model::Finite(v.position)||!re3model::Finite(v.normal)||re3model::Dot(v.normal,v.normal)<1.e-20f||!std::isfinite(v.uv.x)||!std::isfinite(v.uv.y))return "Affine DQ invalid vertex";
            if(!only){const auto representative=CanonicalSkinVertex(mesh,i);if(representative<i){duplicate[i]=1;return nullptr;}}
            unsigned sum=0,reference=0;for(unsigned k=0;k<4;++k){if(v.bone[k]>=joints.size())return "Affine DQ bone outside skeleton";sum+=v.weight[k];if(v.weight[k]>v.weight[reference])reference=k;}
            if(sum!=65535)return "Affine DQ invalid weights";
            if(v.weight[reference]==65535){
                // Exactly the same affine transform/normal arithmetic as LBS
                // for a single influence. This is an algebraic identity, not
                // a rigidity threshold or a different deformation algorithm.
                const auto& joint=joints[v.bone[reference]];
                const Vec3 point=Vec3{}+TransformPoint(joint.original,v.position)*1.f,normal=TransformVector(joint.normal,v.normal);
                if(!re3model::Finite(point)||!re3model::Finite(normal))return "Affine DQ nonfinite single-influence output";
                result[i]={point,Normalize(Vec3{}+normal*1.f),v.uv};return nullptr;
            }
            const auto hemisphere=joints[v.bone[reference]].rigid.real;Quaternion real{},dual{};Stretch stretch;
            for(unsigned k=0;k<4;++k)if(v.weight[k]){const auto& j=joints[v.bone[k]];const double w=double(v.weight[k])/65535.;const double signedWeight=Dot(hemisphere,j.rigid.real)<0?-w:w;
                real=Add(real,Scale(j.rigid.real,signedWeight));dual=Add(dual,Scale(j.rigid.dual,signedWeight));
                stretch.xx+=w*j.stretch.xx;stretch.xy+=w*j.stretch.xy;stretch.xz+=w*j.stretch.xz;stretch.yy+=w*j.stretch.yy;stretch.yz+=w*j.stretch.yz;stretch.zz+=w*j.stretch.zz;
            }
            const double lengthSquared=Dot(real,real);if(!(lengthSquared>1.e-20)||!skinning_dq_detail::Finite(real)||!skinning_dq_detail::Finite(dual))return "Affine DQ degenerate quaternion blend";
            const double inverseLength=1/std::sqrt(lengthSquared);real=Scale(real,inverseLength);dual=Scale(dual,inverseLength);dual=Add(dual,Scale(real,-Dot(real,dual)));
            const auto translation=Scale(Product(dual,Conjugate(real)),2);
            const double stretched[3]={stretch.xx*v.position.x+stretch.xy*v.position.y+stretch.xz*v.position.z,stretch.xy*v.position.x+stretch.yy*v.position.y+stretch.yz*v.position.z,stretch.xz*v.position.x+stretch.yz*v.position.y+stretch.zz*v.position.z};
            double point[3];RotateDouble(real,stretched,point);point[0]+=translation.x;point[1]+=translation.y;point[2]+=translation.z;
            // Adjugate(Sbar) is det(Sbar)*inverseTranspose(Sbar); the positive
            // determinant cancels during normalization, avoiding a division.
            const double c00=stretch.yy*stretch.zz-stretch.yz*stretch.yz,c01=stretch.xz*stretch.yz-stretch.xy*stretch.zz,c02=stretch.xy*stretch.yz-stretch.xz*stretch.yy;
            const double c11=stretch.xx*stretch.zz-stretch.xz*stretch.xz,c12=stretch.xy*stretch.xz-stretch.xx*stretch.yz,c22=stretch.xx*stretch.yy-stretch.xy*stretch.xy;
            const double det=stretch.xx*c00+stretch.xy*c01+stretch.xz*c02;
            if(!(det>0)||!std::isfinite(det))return "Affine DQ invalid blended stretch";
            const double unrotatedNormal[3]={c00*v.normal.x+c01*v.normal.y+c02*v.normal.z,c01*v.normal.x+c11*v.normal.y+c12*v.normal.z,c02*v.normal.x+c12*v.normal.y+c22*v.normal.z};
            double normal[3];RotateDouble(real,unrotatedNormal,normal);const double norm=std::sqrt(normal[0]*normal[0]+normal[1]*normal[1]+normal[2]*normal[2]);
            if(!(norm>1.e-20)||!std::isfinite(norm))return "Affine DQ invalid normal";
            const Vec3 p{float(point[0]),float(point[1]),float(point[2])},n{float(normal[0]/norm),float(normal[1]/norm),float(normal[2]/norm)};
            if(!re3model::Finite(p)||!re3model::Finite(n))return "Affine DQ nonfinite output";
            result[i]={p,n,v.uv};return nullptr;
        };
        if(only){for(const auto i:*only)if(const char* problem=vertex(i))return fail(problem);
            output=std::move(result);if constexpr(Corrected){if(usedFactors)*usedFactors=factors;}if(error)error->clear();return true;}
        const std::size_t grain=2048;std::vector<std::pair<std::size_t,const char*>> failures((count+grain-1)/grain,{count,nullptr});
        if(!re3model::parallel::For(count,grain,[&](std::size_t chunk,std::size_t begin,std::size_t end){
            for(std::size_t i=begin;i<end;++i)if(const char* problem=vertex(i)){failures[chunk]={i,problem};return;}
        }))return fail("Affine DQ allocation/validation failed");
        for(const auto& failure:failures)if(failure.second)return fail(failure.second);
        for(std::size_t i=0;i<count;++i)if(duplicate[i]){auto copy=result[CanonicalSkinVertex(mesh,i)];copy.uv=mesh.vertices[i].uv;result[i]=copy;}
        output=std::move(result);if constexpr(Corrected){if(usedFactors)*usedFactors=factors;}if(error)error->clear();return true;
    }catch(const std::exception&){return fail("Affine DQ allocation/validation failed");}
}
inline bool SkinAffineDualQuaternion(const Mesh& mesh,const std::vector<Matrix3x4>& worldBones,
    std::vector<SkinnedVertex>& output,std::string* error=nullptr,Diagnostics* diagnostics=nullptr){
    return SkinAffineDualQuaternionImpl<false>(mesh,worldBones,output,error,diagnostics,nullptr,nullptr);
}
inline bool SkinAffineDualQuaternionCorrected(const Mesh& mesh,const std::vector<Matrix3x4>& worldBones,
    const re3shoulder::ShoulderCorrective& corrective,std::vector<SkinnedVertex>& output,std::string* error=nullptr,
    Diagnostics* diagnostics=nullptr,re3shoulder::Factors* usedFactors=nullptr,const std::vector<std::uint32_t>* only=nullptr){
    return SkinAffineDualQuaternionImpl<true>(mesh,worldBones,output,error,diagnostics,&corrective,usedFactors,only);
}
} // namespace re3affine
