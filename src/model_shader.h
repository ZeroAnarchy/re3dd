#pragma once

// D3D9 stream batching for CPU-skinned R3MESH1 geometry. The native adapter
// drains legacy queues and ensures BeginScene before drawSlice. Every slice
// restores D3D state AND the original depth surface explicitly.
#include <windows.h>
#include <d3d9.h>
#include <d3dcompiler.h>
#include <memory>
#include <new>
#include <cstdio>
#include "model_runtime.h"
#include "model_gpu.h"

namespace re3model {

inline IDirect3DPixelShader9* CreateModelPixelShader(IDirect3DDevice9* device,HRESULT* result=nullptr){
    if(result)*result=E_INVALIDARG;if(!device)return nullptr;
    D3DCAPS9 caps{};HRESULT hr=device->GetDeviceCaps(&caps);
    if(FAILED(hr)||caps.PixelShaderVersion<D3DPS_VERSION(2,0)){if(result)*result=FAILED(hr)?hr:D3DERR_NOTAVAILABLE;return nullptr;}
    wchar_t path[MAX_PATH]{};const UINT size=GetSystemDirectoryW(path,MAX_PATH);const wchar_t suffix[]=L"\\d3dcompiler_47.dll";
    if(!size||size+_countof(suffix)>MAX_PATH){if(result)*result=HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);return nullptr;}
    std::memcpy(path+size,suffix,sizeof(suffix));HMODULE library=LoadLibraryExW(path,nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
    if(!library){if(result)*result=HRESULT_FROM_WIN32(GetLastError());return nullptr;}
    using Compile=HRESULT(WINAPI*)(LPCVOID,SIZE_T,LPCSTR,const D3D_SHADER_MACRO*,ID3DInclude*,LPCSTR,LPCSTR,UINT,UINT,ID3DBlob**,ID3DBlob**);
    const auto compile=reinterpret_cast<Compile>(GetProcAddress(library,"D3DCompile"));
    if(!compile){FreeLibrary(library);if(result)*result=HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND);return nullptr;}
    static const char source[]=R"HLSL(
sampler2D albedo : register(s0);
sampler2D stpMap : register(s1);
// x=coverage cutoff, y=-1 all/0 opaque-STP/1 semitransparent-STP/2 growth (s1 = reach map, tint.a = progress,
// w = edge sharpness), z=opacity.
float4 options : register(c0);
float4 main(float4 tint : COLOR0, float2 uv : TEXCOORD0) : COLOR0 {
    float4 texel=tex2D(albedo,uv);
    float coverage=texel.a*tint.a;
    if(options.y>1.5) coverage=texel.a*saturate((tint.a-tex2D(stpMap,uv).r)*options.w);
    clip(coverage-options.x);
    if(options.y>=0&&options.y<1.5) {
        float stp=tex2D(stpMap,uv).r;
        if(options.y>0.5) clip(stp-0.5);
        else clip(0.499-stp);
    }
    return float4(texel.rgb*tint.rgb,coverage*options.z);
}
)HLSL";
    ID3DBlob* code=nullptr;ID3DBlob* errors=nullptr;IDirect3DPixelShader9* shader=nullptr;
    hr=compile(source,sizeof(source)-1,"RE3 model texture",nullptr,nullptr,"main",
        caps.PixelShaderVersion>=D3DPS_VERSION(3,0)?"ps_3_0":"ps_2_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code,&errors);
    if(errors){OutputDebugStringA("RE3 model shader: ");OutputDebugStringA(static_cast<const char*>(errors->GetBufferPointer()));errors->Release();}
    if(SUCCEEDED(hr))hr=device->CreatePixelShader(static_cast<const DWORD*>(code->GetBufferPointer()),&shader);
    if(code)code->Release();FreeLibrary(library);if(result)*result=hr;return SUCCEEDED(hr)?shader:nullptr;
}

struct GpuMaterial {MaterialTextures textures;std::uint32_t flags=0;};
struct ModelStreamPage {
    IDirect3DVertexBuffer9* buffer=nullptr;
    std::uint32_t capacity=0,used=0;
    std::uint64_t epoch=~std::uint64_t(0);
    ~ModelStreamPage(){if(buffer)buffer->Release();}
};
struct GpuFrame {
    // Native OT commands are copied/rendered one or more build epochs later.
    // Hold the page itself so a later build cannot DISCARD its live contents.
    std::shared_ptr<ModelStreamPage> page;
    IDirect3DVertexBuffer9* buffer=nullptr;
    std::uint32_t firstVertex=0,vertexCount=0;
    std::uint64_t epoch=0,deviceGeneration=0;
    bool allStp=false;
    // Retained until the native packet's last owner retires. Reset must release
    // DEFAULT resources while leaving that packet and its CPU payload intact.
    std::vector<DrawVertex> cpuVertices;
    D3DVIEWPORT9 viewport{};
    std::vector<DrawRun> runs;
    std::vector<DepthSlice> slices;
    std::vector<GpuMaterial> materials;
    // fps60 (2026-10-27): what the in-between frame needs to move this geometry (model_fps60.h InterpData), or null
    std::shared_ptr<void> interp;
    GpuFrame()=default;
    GpuFrame(const GpuFrame&)=delete;GpuFrame& operator=(const GpuFrame&)=delete;
    ~GpuFrame(){for(auto& m:materials){if(m.textures.albedo)m.textures.albedo->Release();if(m.textures.stp)m.textures.stp->Release();}
        VertexRecycler::Get().Give(std::move(cpuVertices));}
};
struct DrawOptions {
    bool useDepth=true;
    bool nativeSemiTransparent=false;
    unsigned abr=0;
};
using RenderLog=void(*)(void* context,const char* message);
inline bool IsTransientDrawStatus(HRESULT status){
    return status==D3DERR_DEVICELOST||status==D3DERR_DEVICENOTRESET;
}

inline DrawVertex RemapModelViewport(DrawVertex vertex,const D3DVIEWPORT9& source,const D3DVIEWPORT9& target){
    // Projection stores D3D9 pixel centres with the half-pixel correction.
    vertex.x=(vertex.x+.5f-float(source.X))*float(target.Width)/float(source.Width)+float(target.X)-.5f;
    vertex.y=(vertex.y+.5f-float(source.Y))*float(target.Height)/float(source.Height)+float(target.Y)-.5f;
    return vertex;
}

class ModelRenderer {
    IDirect3DDevice9* device_=nullptr;
    IDirect3DPixelShader9* shader_=nullptr;
    IDirect3DSurface9* depth_=nullptr;
    D3DSURFACE_DESC depthTarget_{};
    std::vector<std::shared_ptr<ModelStreamPage>> pages_;
    std::vector<std::weak_ptr<GpuFrame>> liveFrames_;
    std::uint64_t epoch_=~std::uint64_t(0),deviceGeneration_=0;
    std::uint64_t renderedEpoch_=~std::uint64_t(0);
    std::size_t streamBytes_=0,maxStreamBytes_=96u*1024*1024;
    std::size_t maxCpuBytes_=96u*1024*1024;
    bool resetPending_=false;
    bool depthCleared_=false,depthWarning_=false;
    const char* lastUploadFailure_=nullptr;
    std::uint64_t lastUploadFailureEpoch_=0;
    RenderLog logger_=nullptr;void* loggerContext_=nullptr;
    // GPU path (model_gpu.h): shaders on demand and one dynamic ring for the deformed vertices of the frame
    re3gpu::Shaders gpuShaders_;
    IDirect3DVertexBuffer9* gpuRing_=nullptr;std::uint32_t gpuRingUsed_=0,gpuRingGeneration_=1;
    static constexpr std::uint32_t kGpuRingVertices=1u<<18;   // 14.7 MB, ~20 face rigs of a frame
    int gpuSupported_=-1;
    void log(const char* text){if(logger_)logger_(loggerContext_,text);else OutputDebugStringA(text);}
    void releaseStream(){
        for(auto& weak:liveFrames_)if(auto frame=weak.lock()){
            frame->buffer=nullptr;frame->page.reset();
        }
        // Explicitly release each buffer, even if a caller kept a page alias.
        for(auto& page:pages_)if(page->buffer){page->buffer->Release();page->buffer=nullptr;}
        pages_.clear();streamBytes_=0;
        re3gpu::SafeRelease(gpuRing_);gpuRingUsed_=0;++gpuRingGeneration_;
    }
    std::size_t retainedCpuBytes(){
        std::size_t bytes=0;
        liveFrames_.erase(std::remove_if(liveFrames_.begin(),liveFrames_.end(),[&](const std::weak_ptr<GpuFrame>& weak){
            const auto frame=weak.lock();if(!frame)return true;
            bytes+=frame->cpuVertices.size()*sizeof(DrawVertex);return false;
        }),liveFrames_.end());
        return bytes;
    }
    void reportUploadFailure(const char* stage,HRESULT status,std::size_t requested,std::size_t retained){
        // Distinguish our bounded caches from an actual D3D/allocator failure.
        // Do not allocate memory here: this also runs from bad_alloc handling.
        if(lastUploadFailure_&&!std::strcmp(lastUploadFailure_,stage)&&epoch_-lastUploadFailureEpoch_<120)return;
        lastUploadFailure_=stage;lastUploadFailureEpoch_=epoch_;
        std::size_t live=0,pinned=0,pinnedUsed=0,pinnedCapacity=0;
        std::uint64_t oldest=epoch_;
        for(const auto& weak:liveFrames_)if(auto frame=weak.lock()){
            ++live;if(frame->epoch<oldest)oldest=frame->epoch;
        }
        for(const auto& page:pages_)if(page.use_count()>1){
            ++pinned;pinnedUsed+=std::size_t(page->used)*sizeof(DrawVertex);
            pinnedCapacity+=std::size_t(page->capacity)*sizeof(DrawVertex);
        }
        char message[512]{};
        std::snprintf(message,sizeof(message),"RE3 model upload failure stage=%s hr=%08lx requested=%zu cpu=%zu/%zu pool=%zu/%zu pages=%zu pinnedPages=%zu pinnedUsed=%zu pinnedCapacity=%zu liveFrames=%zu epoch=%llu oldestEpoch=%llu\n",
            stage,static_cast<unsigned long>(status),requested,retained,maxCpuBytes_,streamBytes_,maxStreamBytes_,pages_.size(),pinned,pinnedUsed,pinnedCapacity,live,
            static_cast<unsigned long long>(epoch_),static_cast<unsigned long long>(oldest));
        log(message);
    }
    void releaseDepth(){if(depth_)depth_->Release();depth_=nullptr;depthTarget_={};depthCleared_=false;}
    bool ensureDepth(){
        IDirect3DSurface9* target=nullptr;if(FAILED(device_->GetRenderTarget(0,&target)))return false;
        D3DSURFACE_DESC desc{};const HRESULT got=target->GetDesc(&desc);target->Release();if(FAILED(got))return false;
        if(depth_&&(depthTarget_.Width!=desc.Width||depthTarget_.Height!=desc.Height||depthTarget_.MultiSampleType!=desc.MultiSampleType||depthTarget_.MultiSampleQuality!=desc.MultiSampleQuality))releaseDepth();
        if(!depth_){
            for(D3DFORMAT format:{D3DFMT_D24S8,D3DFMT_D24X8,D3DFMT_D16}){
                // Preserve our Z contents while native foreground draws restore
                // their own depth attachment between model bucket callbacks.
                if(SUCCEEDED(device_->CreateDepthStencilSurface(desc.Width,desc.Height,format,desc.MultiSampleType,desc.MultiSampleQuality,FALSE,&depth_,nullptr)))break;
            }
            if(!depth_)return false;depthTarget_=desc;depthCleared_=false;
        }
        return true;
    }
    HRESULT setCommon(){
        HRESULT hr=device_->SetVertexShader(nullptr);if(FAILED(hr))return hr;
        hr=device_->SetPixelShader(shader_);if(FAILED(hr))return hr;
        hr=device_->SetFVF(D3DFVF_XYZRHW|D3DFVF_DIFFUSE|D3DFVF_TEX1);if(FAILED(hr))return hr;
        const std::pair<D3DRENDERSTATETYPE,DWORD> renderStates[]={
            {D3DRS_LIGHTING,FALSE},{D3DRS_CULLMODE,D3DCULL_NONE},{D3DRS_FOGENABLE,FALSE},
            {D3DRS_STENCILENABLE,FALSE},{D3DRS_SCISSORTESTENABLE,FALSE},{D3DRS_FILLMODE,D3DFILL_SOLID},
            {D3DRS_COLORWRITEENABLE,15},{D3DRS_SRGBWRITEENABLE,FALSE},{D3DRS_CLIPPLANEENABLE,0},
            {D3DRS_ALPHATESTENABLE,FALSE},{D3DRS_SEPARATEALPHABLENDENABLE,FALSE},{D3DRS_ZFUNC,D3DCMP_LESSEQUAL}};
        for(const auto& state:renderStates){
            hr=device_->SetRenderState(state.first,state.second);if(FAILED(hr))return hr;
        }
        const std::pair<D3DSAMPLERSTATETYPE,DWORD> samplerStates[]={
            {D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP},{D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP},
            {D3DSAMP_MINFILTER,D3DTEXF_LINEAR},{D3DSAMP_MAGFILTER,D3DTEXF_LINEAR},
            {D3DSAMP_MIPFILTER,D3DTEXF_LINEAR},{D3DSAMP_SRGBTEXTURE,FALSE}};
        for(DWORD sampler=0;sampler<2;++sampler)for(const auto& state:samplerStates){
            hr=device_->SetSamplerState(sampler,state.first,state.second);if(FAILED(hr))return hr;
        }
        return S_OK;
    }
    HRESULT pass(const GpuFrame& frame,const DrawRun& run,const GpuMaterial& material,bool useDepth,float selector,bool blended,unsigned abr,const DrawVertex* cpuVertices){
        float constants[4]={(material.flags&AlphaCutout)?0.5f:(0.5f/255.f),selector,1,0};
        DWORD source=D3DBLEND_SRCALPHA,destination=D3DBLEND_INVSRCALPHA,operation=D3DBLENDOP_ADD;
        if(selector>0.5f){
            if(abr==0)constants[2]=0.5f;
            else {destination=D3DBLEND_ONE;if(abr==2)operation=D3DBLENDOP_REVSUBTRACT;else if(abr==3)constants[2]=0.25f;}
        }
        // blood round 2026-11-07: a reach map in s1 (drawSlice binds the stp texture there)
        if(selector<0&&(material.flags&GrowthMask)&&material.textures.stp){constants[1]=2;constants[3]=56;}
        HRESULT hr=device_->SetPixelShaderConstantF(0,constants,1);if(FAILED(hr))return hr;
        const std::pair<D3DRENDERSTATETYPE,DWORD> states[]={
            {D3DRS_ZENABLE,useDepth?D3DZB_TRUE:D3DZB_FALSE},{D3DRS_ZWRITEENABLE,useDepth&&!blended},
            {D3DRS_ALPHABLENDENABLE,blended},{D3DRS_SRCBLEND,source},{D3DRS_DESTBLEND,destination},{D3DRS_BLENDOP,operation}};
        for(const auto& state:states){hr=device_->SetRenderState(state.first,state.second);if(FAILED(hr))return hr;}
        if(cpuVertices)return device_->DrawPrimitiveUP(D3DPT_TRIANGLELIST,run.vertexCount/3,cpuVertices,sizeof(DrawVertex));
        return device_->DrawPrimitive(D3DPT_TRIANGLELIST,frame.firstVertex+run.firstVertex,run.vertexCount/3);
    }
    // Device state that setCommon/pass/drawSlice change, saved and put back around each slice. A
    // D3DSBT_ALL state block per slice cost far more than the draw itself (hundreds of slices per frame
    // with several HD actors); the device is not pure, so Get* reads the runtime's state cache.
    struct SavedState {
        static constexpr D3DRENDERSTATETYPE kRender[]={D3DRS_LIGHTING,D3DRS_CULLMODE,D3DRS_FOGENABLE,D3DRS_STENCILENABLE,D3DRS_SCISSORTESTENABLE,D3DRS_FILLMODE,
            D3DRS_COLORWRITEENABLE,D3DRS_SRGBWRITEENABLE,D3DRS_CLIPPLANEENABLE,D3DRS_ALPHATESTENABLE,D3DRS_SEPARATEALPHABLENDENABLE,D3DRS_ZFUNC,
            D3DRS_ZENABLE,D3DRS_ZWRITEENABLE,D3DRS_ALPHABLENDENABLE,D3DRS_SRCBLEND,D3DRS_DESTBLEND,D3DRS_BLENDOP};
        static constexpr D3DSAMPLERSTATETYPE kSampler[]={D3DSAMP_ADDRESSU,D3DSAMP_ADDRESSV,D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER,D3DSAMP_MIPFILTER,D3DSAMP_SRGBTEXTURE};
        DWORD render[sizeof(kRender)/sizeof(kRender[0])]{},sampler[2][sizeof(kSampler)/sizeof(kSampler[0])]{},fvf=0;
        IDirect3DBaseTexture9* texture[2]{};IDirect3DVertexShader9* vertexShader=nullptr;IDirect3DPixelShader9* pixelShader=nullptr;
        IDirect3DVertexDeclaration9* declaration=nullptr;IDirect3DVertexBuffer9* stream=nullptr;UINT streamOffset=0,streamStride=0;float constant[4]{};
        void release(){for(auto& t:texture)if(t){t->Release();t=nullptr;}
            if(vertexShader)vertexShader->Release();if(pixelShader)pixelShader->Release();if(declaration)declaration->Release();if(stream)stream->Release();
            vertexShader=nullptr;pixelShader=nullptr;declaration=nullptr;stream=nullptr;}
        HRESULT capture(IDirect3DDevice9* d){
            HRESULT hr=S_OK;
            for(std::size_t i=0;i<sizeof(kRender)/sizeof(kRender[0])&&SUCCEEDED(hr);++i)hr=d->GetRenderState(kRender[i],&render[i]);
            for(DWORD s=0;s<2&&SUCCEEDED(hr);++s)for(std::size_t i=0;i<sizeof(kSampler)/sizeof(kSampler[0])&&SUCCEEDED(hr);++i)hr=d->GetSamplerState(s,kSampler[i],&sampler[s][i]);
            for(DWORD s=0;s<2&&SUCCEEDED(hr);++s)hr=d->GetTexture(s,&texture[s]);
            if(SUCCEEDED(hr))hr=d->GetVertexShader(&vertexShader);if(SUCCEEDED(hr))hr=d->GetPixelShader(&pixelShader);
            if(SUCCEEDED(hr))hr=d->GetFVF(&fvf);if(SUCCEEDED(hr))hr=d->GetVertexDeclaration(&declaration);
            if(SUCCEEDED(hr))hr=d->GetStreamSource(0,&stream,&streamOffset,&streamStride);
            if(SUCCEEDED(hr))hr=d->GetPixelShaderConstantF(0,constant,1);
            if(FAILED(hr))release();
            return hr;
        }
        HRESULT restore(IDirect3DDevice9* d){
            HRESULT first=S_OK;auto keep=[&](HRESULT hr){if(SUCCEEDED(first)&&FAILED(hr))first=hr;};
            for(std::size_t i=0;i<sizeof(kRender)/sizeof(kRender[0]);++i)keep(d->SetRenderState(kRender[i],render[i]));
            for(DWORD s=0;s<2;++s)for(std::size_t i=0;i<sizeof(kSampler)/sizeof(kSampler[0]);++i)keep(d->SetSamplerState(s,kSampler[i],sampler[s][i]));
            for(DWORD s=0;s<2;++s)keep(d->SetTexture(s,texture[s]));
            keep(d->SetVertexShader(vertexShader));keep(d->SetPixelShader(pixelShader));
            keep(fvf?d->SetFVF(fvf):d->SetVertexDeclaration(declaration));
            keep(d->SetStreamSource(0,stream,streamOffset,streamStride));
            keep(d->SetPixelShaderConstantF(0,constant,1));
            release();return first;
        }
    };
public:
    // Stream pages no GpuFrame/native job owns any more (use_count 1), e.g. while a movie plays and nothing is
    // drawn. The next upload creates pages again. Returns the freed bytes.
    std::size_t trimStream(){
        std::size_t freed=0;
        pages_.erase(std::remove_if(pages_.begin(),pages_.end(),[&](const std::shared_ptr<ModelStreamPage>& page){
            if(page.use_count()!=1)return false;const std::size_t bytes=std::size_t(page->capacity)*sizeof(DrawVertex);
            if(page->buffer){page->buffer->Release();page->buffer=nullptr;}streamBytes_-=bytes;freed+=bytes;return true;
        }),pages_.end());
        return freed;
    }
    std::size_t streamBytes()const{return streamBytes_;}
    // Fix 2026-10-25e: depth surface resets (epoch switches while drawing), one per drawn OT when all its frames share
    // their build epoch; logged as MODEL_DEPTH_RESETS
    std::uint64_t depthResets=0;
    // fps60 (2026-10-27): while nonzero, every slice drawn shares this depth epoch (one depth clear for the whole
    // in-between pass, whatever build its frames were uploaded for)
    std::uint64_t passKey=0;
    // false: the previous full D3DSBT_ALL state block per slice (A/B measurement)
    bool lightStateRestore=true;
    ModelRenderer()=default;
    ModelRenderer(const ModelRenderer&)=delete;ModelRenderer& operator=(const ModelRenderer&)=delete;
    ~ModelRenderer(){resetDevice(nullptr);}
    void setLogger(RenderLog logger,void* context=nullptr){logger_=logger;loggerContext_=context;}
    // Serialize with upload/drawSlice and call immediately before every native
    // device Reset. Do NOT release native packet arrays or their GpuFrame owners.
    // Managed textures and shaders survive; all our DEFAULT resources are freed.
    void beforeReset(){
        resetPending_=true;
        if(device_){device_->SetStreamSource(0,nullptr,0,0);device_->SetDepthStencilSurface(nullptr);}
        releaseStream();releaseDepth();renderedEpoch_=~std::uint64_t(0);
    }
    void afterReset(HRESULT status){
        if(FAILED(status))return;
        resetPending_=false;depthWarning_=false;renderedEpoch_=~std::uint64_t(0);
    }
    bool resetPending()const{return resetPending_;}
    void onLostDevice(){beforeReset();}
    // The game is destroying its device (GOG wrapper rebuild after a lost device). Any reference
    // kept here holds the old device in exclusive fullscreen and the rebuild's CreateDevice fails,
    // so retained frames lose their textures too; their native packets then draw S_FALSE.
    void releaseDeviceResources(){
        for(auto& weak:liveFrames_)if(auto frame=weak.lock())for(auto& m:frame->materials){
            if(m.textures.albedo)m.textures.albedo->Release();
            if(m.textures.stp)m.textures.stp->Release();
            m.textures={};
        }
        resetDevice(nullptr);
    }
    ULONG deviceReferences()const{if(!device_)return 0;device_->AddRef();return device_->Release();}
    bool resetDevice(IDirect3DDevice9* device){
        if(device_==device)return shader_!=nullptr&&!resetPending_;
        releaseStream();releaseDepth();if(shader_)shader_->Release();shader_=nullptr;gpuShaders_.release();gpuSupported_=-1;
        // Different device identity: old managed textures cannot be drawn on it.
        // Old native jobs remain alive and drawSlice returns S_FALSE for them.
        liveFrames_.clear();
        if(device_)device_->Release();device_=device;++deviceGeneration_;epoch_=renderedEpoch_=~std::uint64_t(0);depthWarning_=false;resetPending_=false;lastUploadFailure_=nullptr;
        if(!device_)return false;device_->AddRef();HRESULT hr;shader_=CreateModelPixelShader(device_,&hr);
        if(!shader_)log("RE3 model pixel shader unavailable; keep native mesh\n");
        return shader_!=nullptr;
    }
    void beginFrame(std::uint64_t serial){
        // This advances allocation only. It must not invalidate deferred draws
        // or clear their depth surface before their native render phase runs.
        epoch_=serial;
    }
    std::uint64_t epoch()const{return epoch_;}
    std::shared_ptr<GpuFrame> upload(const Mesh& mesh,const FrameGeometry& geometry,const std::vector<MaterialTextures>& textures,bool requireStp=false,HRESULT* result=nullptr){
        return uploadFrame(mesh,geometry,nullptr,textures,requireStp,result,epoch_);
    }
    // Fix 2026-10-25d: a frame prepared for this upload only gives its vertex array to the GpuFrame (no 1-6 MB copy).
    std::shared_ptr<GpuFrame> upload(const Mesh& mesh,FrameGeometry&& geometry,const std::vector<MaterialTextures>& textures,bool requireStp=false,HRESULT* result=nullptr){
        return uploadFrame(mesh,geometry,&geometry.vertices,textures,requireStp,result,epoch_);
    }
    // Fix 2026-10-25e: a frame uploaded while its OT draws (deferred prepare) carries the epoch of the build that queued
    // it. The native OT of build N draws one or two builds later; with the current epoch the deferred frames (N+1, N+2)
    // and the build-time frames of the same OT (N) alternated, every switch cleared the depth surface (drawSlice), and
    // Jill's body painted over the harness, zombie bodies over their wound decals.
    std::shared_ptr<GpuFrame> uploadAt(std::uint64_t epoch,const Mesh& mesh,FrameGeometry&& geometry,const std::vector<MaterialTextures>& textures,bool requireStp=false,HRESULT* result=nullptr){
        return uploadFrame(mesh,geometry,&geometry.vertices,textures,requireStp,result,epoch);
    }
    // fps60 (2026-10-27): the same runs, slices and materials as `base` with other vertices (the in-between pose).
    std::shared_ptr<GpuFrame> uploadLike(const GpuFrame& base,std::vector<DrawVertex>&& vertices,std::uint64_t epoch,HRESULT* result=nullptr){
        if(result)*result=E_INVALIDARG;
        if(resetPending_){if(result)*result=D3DERR_DEVICELOST;return {};}
        if(!device_||!shader_||vertices.size()!=base.vertexCount||base.deviceGeneration!=deviceGeneration_)return {};
        try {
        const std::size_t retained=retainedCpuBytes(),required=vertices.size()*sizeof(DrawVertex);
        if(retained>maxCpuBytes_||required>maxCpuBytes_-retained){reportUploadFailure("cpu-budget",E_OUTOFMEMORY,required,retained);if(result)*result=E_OUTOFMEMORY;return {};}
        auto frame=std::make_shared<GpuFrame>();frame->viewport=base.viewport;frame->cpuVertices=std::move(vertices);
        HRESULT hr=fillPage(*frame,epoch,required,retained);if(FAILED(hr)){if(result)*result=hr;return {};}
        frame->runs=base.runs;frame->slices=base.slices;frame->allStp=base.allStp;frame->materials=base.materials;
        for(auto& m:frame->materials){if(m.textures.albedo)m.textures.albedo->AddRef();if(m.textures.stp)m.textures.stp->AddRef();}
        liveFrames_.push_back(frame);
        if(result)*result=S_OK;return frame;
        }catch(const std::bad_alloc&){reportUploadFailure("cpu-allocation",E_OUTOFMEMORY,vertices.size()*sizeof(DrawVertex),retainedCpuBytes());if(result)*result=E_OUTOFMEMORY;return {};}
    }
    // A stream page with room for frame.cpuVertices (one of this epoch, a free one or a new one), filled; sets the
    // frame's page, buffer, range, epoch and device generation.
    HRESULT fillPage(GpuFrame& frame,std::uint64_t epoch,std::size_t required,std::size_t retained){
        const auto count=std::uint32_t(frame.cpuVertices.size());std::shared_ptr<ModelStreamPage> selected;HRESULT hr=S_OK;
        for(auto& page:pages_)if(page->epoch==epoch&&page->capacity-page->used>=count){selected=page;break;}
        if(!selected)for(auto& page:pages_)if(page.use_count()==1&&page->capacity>=count){
            page->epoch=epoch;page->used=0;selected=page;break;
        }
        if(!selected){
            std::uint32_t capacity=65536;while(capacity<count)capacity*=2;
            const std::size_t bytes=std::size_t(capacity)*sizeof(DrawVertex);
            if(bytes>maxStreamBytes_-streamBytes_)pages_.erase(std::remove_if(pages_.begin(),pages_.end(),[&](const std::shared_ptr<ModelStreamPage>& page){
                if(page.use_count()!=1)return false;streamBytes_-=std::size_t(page->capacity)*sizeof(DrawVertex);return true;
            }),pages_.end());
            if(bytes>maxStreamBytes_-streamBytes_){reportUploadFailure("pool-budget",E_OUTOFMEMORY,bytes,retained);return E_OUTOFMEMORY;}
            selected=std::make_shared<ModelStreamPage>();
            hr=device_->CreateVertexBuffer(UINT(bytes),D3DUSAGE_DYNAMIC|D3DUSAGE_WRITEONLY,
                D3DFVF_XYZRHW|D3DFVF_DIFFUSE|D3DFVF_TEX1,D3DPOOL_DEFAULT,&selected->buffer,nullptr);
            if(FAILED(hr)){reportUploadFailure("create-vb",hr,bytes,retained);return hr;}
            selected->capacity=capacity;selected->epoch=epoch;
            pages_.push_back(selected);streamBytes_+=bytes;
        }
        void* memory=nullptr;const std::uint32_t first=selected->used;
        hr=selected->buffer->Lock(first*sizeof(DrawVertex),count*sizeof(DrawVertex),&memory,first?D3DLOCK_NOOVERWRITE:D3DLOCK_DISCARD);
        if(FAILED(hr)){reportUploadFailure("lock-vb",hr,required,retained);return hr;}
        std::memcpy(memory,frame.cpuVertices.data(),std::size_t(count)*sizeof(DrawVertex));hr=selected->buffer->Unlock();
        if(FAILED(hr)){reportUploadFailure("unlock-vb",hr,required,retained);return hr;}
        selected->used+=count;
        frame.page=selected;frame.buffer=selected->buffer;frame.firstVertex=first;frame.vertexCount=count;
        frame.epoch=epoch;frame.deviceGeneration=deviceGeneration_;
        return S_OK;
    }
    std::shared_ptr<GpuFrame> uploadFrame(const Mesh& mesh,const FrameGeometry& geometry,std::vector<DrawVertex>* give,const std::vector<MaterialTextures>& textures,bool requireStp,HRESULT* result,std::uint64_t epoch){
        if(result)*result=E_INVALIDARG;
        if(resetPending_){if(result)*result=D3DERR_DEVICELOST;return {};}
        if(!device_||!shader_||geometry.vertices.empty()||geometry.vertices.size()>3000000||textures.size()!=mesh.materials.size())return {};
        for(const auto& t:textures)if(!t.albedo||(requireStp&&!t.stp))return {};
        try {
        const std::size_t retained=retainedCpuBytes(),required=geometry.vertices.size()*sizeof(DrawVertex);
        if(retained>maxCpuBytes_||required>maxCpuBytes_-retained){reportUploadFailure("cpu-budget",E_OUTOFMEMORY,required,retained);if(result)*result=E_OUTOFMEMORY;return {};}
        auto frame=std::make_shared<GpuFrame>();
        HRESULT hr=device_->GetViewport(&frame->viewport);
        if(FAILED(hr)||!frame->viewport.Width||!frame->viewport.Height){if(result)*result=FAILED(hr)?hr:E_INVALIDARG;return {};}
        const auto count=std::uint32_t(geometry.vertices.size());std::shared_ptr<ModelStreamPage> selected;
        if(give)frame->cpuVertices=std::move(*give);else frame->cpuVertices=geometry.vertices;
        for(auto& page:pages_)if(page->epoch==epoch&&page->capacity-page->used>=count){selected=page;break;}
        if(!selected)for(auto& page:pages_)if(page.use_count()==1&&page->capacity>=count){
            page->epoch=epoch;page->used=0;selected=page;break;
        }
        if(!selected){
            std::uint32_t capacity=65536;while(capacity<count)capacity*=2;
            const std::size_t bytes=std::size_t(capacity)*sizeof(DrawVertex);
            // Reclaim only pages with no surviving GpuFrame/native job owner.
            if(bytes>maxStreamBytes_-streamBytes_)pages_.erase(std::remove_if(pages_.begin(),pages_.end(),[&](const std::shared_ptr<ModelStreamPage>& page){
                if(page.use_count()!=1)return false;streamBytes_-=std::size_t(page->capacity)*sizeof(DrawVertex);return true;
            }),pages_.end());
            if(bytes>maxStreamBytes_-streamBytes_){reportUploadFailure("pool-budget",E_OUTOFMEMORY,bytes,retained);if(result)*result=E_OUTOFMEMORY;return {};}
            selected=std::make_shared<ModelStreamPage>();
            hr=device_->CreateVertexBuffer(UINT(bytes),D3DUSAGE_DYNAMIC|D3DUSAGE_WRITEONLY,
                D3DFVF_XYZRHW|D3DFVF_DIFFUSE|D3DFVF_TEX1,D3DPOOL_DEFAULT,&selected->buffer,nullptr);
            if(FAILED(hr)){reportUploadFailure("create-vb",hr,bytes,retained);if(result)*result=hr;return {};}
            selected->capacity=capacity;selected->epoch=epoch;
            pages_.push_back(selected);streamBytes_+=bytes;
        }
        void* memory=nullptr;const std::uint32_t first=selected->used;
        hr=selected->buffer->Lock(first*sizeof(DrawVertex),count*sizeof(DrawVertex),&memory,first?D3DLOCK_NOOVERWRITE:D3DLOCK_DISCARD);
        if(FAILED(hr)){reportUploadFailure("lock-vb",hr,required,retained);if(result)*result=hr;return {};}
        std::memcpy(memory,frame->cpuVertices.data(),std::size_t(count)*sizeof(DrawVertex));hr=selected->buffer->Unlock();
        if(FAILED(hr)){reportUploadFailure("unlock-vb",hr,required,retained);if(result)*result=hr;return {};}
        selected->used+=count;
        frame->page=selected;frame->buffer=selected->buffer;frame->firstVertex=first;frame->vertexCount=count;
        frame->epoch=epoch;frame->deviceGeneration=deviceGeneration_;frame->runs=geometry.runs;frame->slices=geometry.slices;frame->allStp=true;
        frame->materials.reserve(textures.size());
        for(std::size_t n=0;n<textures.size();++n){
            GpuMaterial material{textures[n],mesh.materials[n].flags};material.textures.albedo->AddRef();
            if(material.textures.stp)material.textures.stp->AddRef();else frame->allStp=false;
            frame->materials.push_back(material);
        }
        liveFrames_.push_back(frame);
        if(result)*result=S_OK;return frame;
        }catch(const std::bad_alloc&){reportUploadFailure("cpu-allocation",E_OUTOFMEMORY,geometry.vertices.size()*sizeof(DrawVertex),retainedCpuBytes());if(result)*result=E_OUTOFMEMORY;return {};}
    }
    // ------------------------------------------------------------ GPU path
    // Whether this device can take the GPU path (checked once per device; `why` is filled the first time it cannot).
    bool gpuUsable(std::string* why=nullptr){
        if(!device_)return false;
        if(gpuSupported_<0){std::string reason;gpuSupported_=re3gpu::DeviceSupports(device_,reason)&&SUCCEEDED(gpuShaders_.declarations(device_));if(!gpuSupported_&&why)*why=reason.empty()?"vertex declarations":reason;}
        return gpuSupported_>0;
    }
    const std::string& gpuShaderError()const{return gpuShaders_.lastError();}
    IDirect3DDevice9* device()const{return device_;}
    bool gpuPixelLight=false;
    HRESULT gpuUpload(const void* data,std::uint32_t vertices,std::uint32_t& first,std::uint32_t& generation,std::uint64_t& bytes){
        constexpr std::uint32_t stride=sizeof(re3gpu::GpuVertex);
        if(!vertices||vertices>kGpuRingVertices)return E_INVALIDARG;
        if(!gpuRing_){const HRESULT made=device_->CreateVertexBuffer(kGpuRingVertices*stride,D3DUSAGE_DYNAMIC|D3DUSAGE_WRITEONLY,0,D3DPOOL_DEFAULT,&gpuRing_,nullptr);if(FAILED(made))return made;gpuRingUsed_=0;++gpuRingGeneration_;}
        DWORD flags=D3DLOCK_NOOVERWRITE;if(gpuRingUsed_+vertices>kGpuRingVertices){gpuRingUsed_=0;++gpuRingGeneration_;flags=D3DLOCK_DISCARD;}
        void* memory=nullptr;HRESULT hr=gpuRing_->Lock(gpuRingUsed_*stride,vertices*stride,&memory,flags);if(FAILED(hr))return hr;
        std::memcpy(memory,data,std::size_t(vertices)*stride);hr=gpuRing_->Unlock();if(FAILED(hr))return hr;
        first=gpuRingUsed_;generation=gpuRingGeneration_;gpuRingUsed_+=vertices;bytes+=std::size_t(vertices)*stride;return S_OK;
    }
    // The in-between palette: positions and normals of a linear skin blend like the skinned vertices themselves; dual
    // quaternions blend in their hemisphere and the shader normalizes the mix.
    // Crystals round 2026-11-07: the GEM shaders for material m of d when it is a Gem material with a linear skin (drawGpu
    // reaches it in the blended phase only, Gem implies AlphaBlend); sets the eye (VS c17), the draw's light (PS c2..c12) and
    // the gem.txt values (PS c14, c15, c17, c18; c16 per pass). false = draw it as before.
    bool gemWarned_=false;
    bool beginGem(const re3gpu::Draw& d,std::size_t m,bool lit){
        if(!(d.mesh->materialFlags[m]&Gem)||d.skin!=re3gpu::Skin::Linear||d.mesh->materialGem.size()<(m+1)*kGemParams)return false;
        IDirect3DVertexShader9* vertex=nullptr;IDirect3DPixelShader9* pixel=nullptr;
        if(!gpuShaders_.gem(device_,vertex,pixel)){
            if(!gemWarned_){gemWarned_=true;char text[700];std::snprintf(text,sizeof(text),"MODEL_GEM_SHADER_FAILED %.600s; Gem materials draw as plain AlphaBlend\n",gpuShaders_.lastError().c_str());log(text);}
            return false;}
        if(FAILED(device_->SetVertexShader(vertex))||FAILED(device_->SetPixelShader(pixel)))return false;
        const float eye[4]={d.eye[0],d.eye[1],d.eye[2],0};device_->SetVertexShaderConstantF(17,eye,1);
        device_->SetPixelShaderConstantF(2,&d.light.rows[0][0],10);
        float profile[4];std::memcpy(profile,d.light.profile1,16);profile[2]=lit?1.f:0.f;device_->SetPixelShaderConstantF(12,profile,1);
        const float* g=&d.mesh->materialGem[m*kGemParams];
        device_->SetPixelShaderConstantF(14,g,2);device_->SetPixelShaderConstantF(17,g+12,2);
        return true;
    }
    static void blendPalette(const re3gpu::Draw& d,float weight,float* out){
        const auto& to=*d.palette;const auto& from=*d.previous;const std::size_t count=to.size();
        if(d.skin==re3gpu::Skin::Affine){
            for(std::size_t b=0;b+16<=count;b+=16){const float* q=&to[b];const float* p=&from[b];const float dot=q[0]*p[0]+q[1]*p[1]+q[2]*p[2]+q[3]*p[3];const float s=dot<0?-1.f:1.f;
                for(unsigned k=0;k<8;++k)out[b+k]=p[k]*s+(q[k]-p[k]*s)*weight;for(unsigned k=8;k<16;++k)out[b+k]=p[k]+(q[k]-p[k])*weight;}
        }else for(std::size_t i=0;i<count;++i)out[i]=from[i]+(to[i]-from[i])*weight;
    }
    // Draws the gathered models, each limited to the camera depth of its buckets. weight: 1 = the build itself, below 1
    // the in-between frame. The caller holds the scene open. Device state comes back as it was.
    HRESULT drawGpu(const re3gpu::Entry* entries,std::size_t count,float weight,re3gpu::Stats& stats){
        if(resetPending_)return D3DERR_DEVICELOST;
        if(!device_||!shader_)return E_INVALIDARG;
        auto* pixelShader=gpuShaders_.pixel(device_,gpuPixelLight);if(!pixelShader)return E_NOINTERFACE;
        SavedState saved;HRESULT hr=saved.capture(device_);if(FAILED(hr))return hr;
        IDirect3DIndexBuffer9* savedIndices=nullptr;device_->GetIndices(&savedIndices);
        const BOOL software=device_->GetSoftwareVertexProcessing();if(software)device_->SetSoftwareVertexProcessing(FALSE);
        IDirect3DSurface9* previousDepth=nullptr;device_->GetDepthStencilSurface(&previousDepth);
        D3DVIEWPORT9 viewport{};device_->GetViewport(&viewport);
        const bool useDepth=ensureDepth()&&SUCCEEDED(device_->SetDepthStencilSurface(depth_));
        if(!useDepth&&!depthWarning_){depthWarning_=true;log("RE3 model depth surface unavailable; painter ordering fallback\n");}
        hr=setCommon();
        if(SUCCEEDED(hr))hr=device_->SetPixelShader(pixelShader);
        // the near plane is the hardware's clipper here; pre-transformed draws never needed the state
        DWORD savedClipping=TRUE;device_->GetRenderState(D3DRS_CLIPPING,&savedClipping);device_->SetRenderState(D3DRS_CLIPPING,TRUE);
        static thread_local float blended[re3gpu::kPaletteFloats];
        const float wide=1.e9f;bool anyBlend=false;
        for(unsigned phase=0;phase<2&&SUCCEEDED(hr)&&(!phase||anyBlend);++phase){
            const std::pair<D3DRENDERSTATETYPE,DWORD> states[]={{D3DRS_ZENABLE,useDepth?D3DZB_TRUE:D3DZB_FALSE},{D3DRS_ZWRITEENABLE,useDepth&&!phase},{D3DRS_ALPHABLENDENABLE,phase},
                {D3DRS_SRCBLEND,D3DBLEND_SRCALPHA},{D3DRS_DESTBLEND,D3DBLEND_INVSRCALPHA},{D3DRS_BLENDOP,D3DBLENDOP_ADD}};
            for(const auto& state:states)device_->SetRenderState(state.first,state.second);
            re3gpu::Skin currentSkin=re3gpu::Skin(255);IDirect3DVertexBuffer9* stream0=nullptr,*stream1=nullptr;IDirect3DIndexBuffer9* currentIndices=nullptr;
            for(std::size_t e=0;e<count&&SUCCEEDED(hr);++e){const auto& entry=entries[e];const auto& d=*entry.draw;
                if(!d.ready()||(phase&&!d.mesh->hasBlend))continue;anyBlend=anyBlend||d.mesh->hasBlend;++stats.entries;
                const std::uint64_t depthKey=(passKey?passKey:d.epoch)^(d.ownDepth?0x8000000000000000ull:0ull);   // fix 2026-11-08: Draw::ownDepth
                if(renderedEpoch_!=depthKey){renderedEpoch_=depthKey;depthCleared_=false;++depthResets;}
                if(useDepth&&!depthCleared_)depthCleared_=SUCCEEDED(device_->Clear(0,nullptr,D3DCLEAR_ZBUFFER,0,1,0));
                if(d.skin!=currentSkin){currentSkin=d.skin;auto* shader=gpuShaders_.vertex(device_,d.skin,gpuPixelLight);if(!shader){hr=E_NOINTERFACE;break;}
                    hr=device_->SetVertexDeclaration(gpuShaders_.declaration(d.skin));if(SUCCEEDED(hr))hr=device_->SetVertexShader(shader);if(FAILED(hr))break;
                    if(d.skin!=re3gpu::Skin::LinearPose&&stream1){device_->SetStreamSource(1,nullptr,0,0);stream1=nullptr;}}
                const float width=float(viewport.Width),height=float(viewport.Height);
                const bool between=weight<1.f&&d.previous&&d.palette&&d.previous->size()==d.palette->size();
                float constants[4][4]={{d.h/160.f,d.offsetX/160.f+2.f*d.pixelOffsetX/width-1.f,-d.h/120.f,1.f-d.offsetY/120.f-2.f*d.pixelOffsetY/height},
                    {d.nearPlane,d.skin==re3gpu::Skin::World?weight:1.f,0,0},{},{}};
                for(unsigned k=0;k<2;++k)constants[1][2+k]=between?d.previousPoseFactor[k]+(d.poseFactor[k]-d.previousPoseFactor[k])*weight:d.poseFactor[k];
                device_->SetVertexShaderConstantF(0,constants[0],2);device_->SetVertexShaderConstantF(2,d.cameraRows,3);
                device_->SetVertexShaderConstantF(5,&d.light.rows[0][0],11);if(gpuPixelLight)device_->SetPixelShaderConstantF(2,&d.light.rows[0][0],11);
                if(d.skin==re3gpu::Skin::Decal){const float eye[4]={d.eye[0],d.eye[1],d.eye[2],0};device_->SetVertexShaderConstantF(17,eye,1);}
                if(d.palette&&d.paletteRegs){const float* palette=d.palette->data();if(between){blendPalette(d,weight,blended);palette=blended;}
                    device_->SetVertexShaderConstantF(re3gpu::kPaletteRegister,palette,d.paletteRegs);}
                // pixels of the flushed buckets; the ends of the model's own range stay open (its depth range is a bound)
                const float range[4]={entry.low>d.bucketLow||d.nearClosed?float(entry.low)*d.bucketDepth:-wide,entry.high<d.bucketHigh?float(entry.high+1)*d.bucketDepth:wide,0,0};
                device_->SetPixelShaderConstantF(1,range,1);
                // fix 2026-11-08 (wall split): the side of a masked wall this draw keeps (Draw::wallPlane / wallSide)
                {const float side[4]={d.wallSide,d.wallSide!=0?0.f:1.f,0,0};device_->SetVertexShaderConstantF(18,d.wallPlane,1);device_->SetPixelShaderConstantF(13,side,1);}
                // this build's moving vertices: into the ring once, whatever the number of flushes and passes
                const bool hasDynamic=!d.dynamic.empty(),isWorld=d.skin==re3gpu::Skin::World,isDecal=d.skin==re3gpu::Skin::Decal;
                auto& mutableDraw=const_cast<re3gpu::Draw&>(d);
                if((hasDynamic||isWorld||isDecal)&&d.ringGeneration!=gpuRingGeneration_){std::uint32_t first=0,generation=0;
                    const void* data=isWorld?static_cast<const void*>(d.world.data()):isDecal?static_cast<const void*>(d.decal.data()):static_cast<const void*>(d.dynamic.data());
                    const std::size_t vertices=isWorld?d.world.size():isDecal?d.decal.size():d.dynamic.size();
                    hr=gpuUpload(data,std::uint32_t(vertices),first,generation,stats.bytes);if(FAILED(hr))break;mutableDraw.ringVertex=first;mutableDraw.ringGeneration=generation;}
                bool lit=false;device_->SetVertexShaderConstantF(15,d.light.profile1,1);if(gpuPixelLight)device_->SetPixelShaderConstantF(12,d.light.profile1,1);lit=d.lit;
                auto material=[&](std::size_t m)->bool{
                    const std::uint32_t flags=d.mesh->materialFlags[m];if(((flags&AlphaBlend)!=0)!=(phase!=0))return false;
                    float colour[4];for(unsigned k=0;k<4;++k)colour[k]=d.mesh->materialColour[m*4+k]*d.tint[k];
                    device_->SetVertexShaderConstantF(16,colour,1);
                    const float options[4]={(flags&AlphaCutout)?0.5f:(0.5f/255.f),-1,1,0};device_->SetPixelShaderConstantF(0,options,1);
                    const bool wanted=d.lit&&!(flags&Unlit);
                    if(wanted!=lit){lit=wanted;float profile[4];std::memcpy(profile,d.light.profile1,16);profile[2]=lit?1.f:0.f;device_->SetVertexShaderConstantF(15,profile,1);if(gpuPixelLight)device_->SetPixelShaderConstantF(12,profile,1);}
                    hr=device_->SetTexture(0,d.textures[m].albedo);return SUCCEEDED(hr);};
                if(isDecal){
                    if(stream0!=gpuRing_){stream0=gpuRing_;hr=device_->SetStreamSource(0,gpuRing_,0,sizeof(re3gpu::GpuVertex));if(FAILED(hr))break;}
                    for(const auto& run:d.decalRuns)if(material(run.material)){hr=device_->DrawPrimitive(D3DPT_TRIANGLELIST,d.ringVertex+run.firstVertex,run.vertexCount/3);++stats.draws;stats.triangles+=run.vertexCount/3;if(FAILED(hr))break;}
                    continue;
                }
                if(!d.indices||!d.indices->buffer)continue;
                if(currentIndices!=d.indices->buffer){currentIndices=d.indices->buffer;hr=device_->SetIndices(currentIndices);if(FAILED(hr))break;}
                const auto& ranges=d.indices->ranges;
                // split by depth: only the chunks that reach the flushed depths (64 units of room: the in-between pose moved)
                const bool split=(range[0]>-wide||range[1]<wide)&&!isWorld&&d.skin!=re3gpu::Skin::LinearPose&&!d.indices->chunks.empty();
                if(split&&mutableDraw.chunkDepth.empty())re3gpu::ChunkDepths(mutableDraw);
                auto drawChunks=[&](std::uint32_t firstChunk,std::uint32_t chunks,std::uint32_t firstIndex,std::uint32_t indices,INT base,UINT vertices){
                    if(!split){hr=device_->DrawIndexedPrimitive(D3DPT_TRIANGLELIST,base,0,vertices,firstIndex,indices/3);++stats.draws;stats.triangles+=indices/3;return;}
                    const auto& list=d.indices->chunks;std::uint32_t runFirst=0,runIndices=0;
                    auto flushRun=[&]{if(runIndices&&SUCCEEDED(hr)){hr=device_->DrawIndexedPrimitive(D3DPT_TRIANGLELIST,base,0,vertices,runFirst,runIndices/3);++stats.draws;stats.triangles+=runIndices/3;}runIndices=0;};
                    for(std::uint32_t c=firstChunk;c<firstChunk+chunks;++c){
                        if(d.chunkDepth[c*2]-64<=range[1]&&d.chunkDepth[c*2+1]+64>=range[0]){if(!runIndices)runFirst=list[c].first;runIndices+=list[c].indices;}
                        else flushRun();}
                    flushRun();};
                auto bindStatic=[&]{auto* vb=isWorld?gpuRing_:d.mesh->vertices;if(stream0!=vb){stream0=vb;hr=device_->SetStreamSource(0,vb,0,sizeof(re3gpu::GpuVertex));}
                    if(d.skin==re3gpu::Skin::LinearPose&&stream1!=d.mesh->pose){stream1=d.mesh->pose;device_->SetStreamSource(1,stream1,0,sizeof(re3gpu::PoseVertex));}};
                // BaseVertexIndex moves every stream: with a second stream the ring offset goes into stream 0 instead
                const bool poseDynamic=d.skin==re3gpu::Skin::LinearPose;
                auto bindDynamic=[&]{
                    if(poseDynamic){stream0=nullptr;hr=device_->SetStreamSource(0,gpuRing_,d.ringVertex*sizeof(re3gpu::GpuVertex),sizeof(re3gpu::GpuVertex));
                        if(stream1!=d.mesh->dynamicPose){stream1=d.mesh->dynamicPose;device_->SetStreamSource(1,stream1,0,sizeof(re3gpu::PoseVertex));}}
                    else if(stream0!=gpuRing_){stream0=gpuRing_;hr=device_->SetStreamSource(0,gpuRing_,0,sizeof(re3gpu::GpuVertex));}};
                for(std::size_t m=0;m<ranges.size()&&SUCCEEDED(hr);++m){const auto& range2=ranges[m];
                    if((m<64&&((d.hiddenMaterials>>m)&1u))||(!range2.staticIndices&&!range2.dynamicIndices)||!material(m))continue;
                    // crystals round 2026-11-07: a Gem material (blended phase, linear skin) takes the GEM shaders, far facets first
                    const bool gem=beginGem(d,m,lit);
                    for(unsigned side=0;side<(gem?2u:1u)&&SUCCEEDED(hr);++side){
                        if(gem){const float* g=&d.mesh->materialGem[m*kGemParams];const float pass[4]={side?1.f:-1.f,g[9],g[10],g[11]};device_->SetPixelShaderConstantF(16,pass,1);}
                        if(range2.staticIndices){bindStatic();
                            if(SUCCEEDED(hr))drawChunks(range2.firstChunk,range2.staticChunks,range2.firstStatic,range2.staticIndices,isWorld?INT(d.ringVertex):0,d.mesh->vertexCount);}
                        if(range2.dynamicIndices&&SUCCEEDED(hr)){bindDynamic();
                            if(SUCCEEDED(hr))drawChunks(range2.firstChunk+range2.staticChunks,range2.dynamicChunks,range2.firstDynamic,range2.dynamicIndices,poseDynamic?0:INT(d.ringVertex),UINT(d.dynamic.size()));}
                    }
                    if(gem){auto* shader=gpuShaders_.vertex(device_,currentSkin,gpuPixelLight);if(shader)device_->SetVertexShader(shader);device_->SetPixelShader(pixelShader);}
                }
            }
            device_->SetStreamSource(1,nullptr,0,0);
        }
        device_->SetRenderState(D3DRS_CLIPPING,savedClipping);
        const HRESULT depthRestore=device_->SetDepthStencilSurface(previousDepth);if(previousDepth)previousDepth->Release();
        device_->SetIndices(savedIndices);if(savedIndices)savedIndices->Release();
        if(software)device_->SetSoftwareVertexProcessing(TRUE);
        const HRESULT restored=saved.restore(device_);++stats.flushes;
        if(SUCCEEDED(hr)&&FAILED(depthRestore))hr=depthRestore;if(SUCCEEDED(hr)&&FAILED(restored))hr=restored;
        return hr;
    }
    // Round models-2026-11-03: whether a slice has AlphaBlend runs that the caller draws apart (blend=1 below, then blend=2
    // after the opaque slices of the same native stretch, as the GPU path's second phase). Not for nativeSemiTransparent
    // slices (props: their own two passes per run).
    static bool sliceHasBlend(const GpuFrame& frame,std::size_t index,const DrawOptions& options){
        if(options.nativeSemiTransparent||index>=frame.slices.size())return false;const auto& slice=frame.slices[index];
        if(std::uint64_t(slice.firstRun)+slice.runCount>frame.runs.size())return false;
        for(unsigned n=0;n<slice.runCount;++n){const auto m=frame.runs[slice.firstRun+n].materialIndex;if(m<frame.materials.size()&&(frame.materials[m].flags&AlphaBlend))return true;}
        return false;
    }
    // blend: 0 every run in order (as before), 1 the runs without AlphaBlend only, 2 the AlphaBlend runs only.
    HRESULT drawSlice(const GpuFrame& frame,std::size_t index,const DrawOptions& options={},unsigned blend=0){
        if(resetPending_)return D3DERR_DEVICELOST;
        if(frame.deviceGeneration!=deviceGeneration_)return S_FALSE;
        const bool cpu=!frame.buffer;
        if(!device_||!shader_||(cpu?frame.cpuVertices.size()!=frame.vertexCount:!frame.page||!frame.page->buffer||frame.page->epoch!=frame.epoch)||index>=frame.slices.size()||
            options.abr>3||(options.nativeSemiTransparent&&!frame.allStp))return E_INVALIDARG;
        const auto& slice=frame.slices[index];
        if(std::uint64_t(slice.firstRun)+slice.runCount>frame.runs.size())return E_INVALIDARG;
        // Validate the whole slice before touching the target.
        for(unsigned n=0;n<slice.runCount;++n){const auto& run=frame.runs[slice.firstRun+n];
            if(run.materialIndex>=frame.materials.size()||!run.vertexCount||run.vertexCount%3||std::uint64_t(run.firstVertex)+run.vertexCount>frame.vertexCount)return E_INVALIDARG;
        }
        D3DVIEWPORT9 currentViewport{};std::vector<DrawVertex> remapped;bool remap=false;
        if(cpu){
            const HRESULT got=device_->GetViewport(&currentViewport);if(FAILED(got))return got;
            if(!frame.viewport.Width||!frame.viewport.Height||!currentViewport.Width||!currentViewport.Height)return E_INVALIDARG;
            remap=frame.viewport.X!=currentViewport.X||frame.viewport.Y!=currentViewport.Y||frame.viewport.Width!=currentViewport.Width||frame.viewport.Height!=currentViewport.Height;
            if(remap){
                std::size_t largest=0;for(unsigned n=0;n<slice.runCount;++n)largest=(std::max)(largest,std::size_t(frame.runs[slice.firstRun+n].vertexCount));
                try{remapped.resize(largest);}catch(const std::bad_alloc&){return E_OUTOFMEMORY;}
            }
        }
        IDirect3DStateBlock9* state=nullptr;SavedState saved;HRESULT hr=lightStateRestore?saved.capture(device_):E_FAIL;
        const bool light=SUCCEEDED(hr);
        if(!light){hr=device_->CreateStateBlock(D3DSBT_ALL,&state);if(FAILED(hr))return hr;}
        IDirect3DSurface9* previousDepth=nullptr;device_->GetDepthStencilSurface(&previousDepth);
        const std::uint64_t depthKey=passKey?passKey:frame.epoch;
        if(renderedEpoch_!=depthKey){renderedEpoch_=depthKey;depthCleared_=false;++depthResets;}
        bool useDepth=false;
        if(options.useDepth&&ensureDepth()&&SUCCEEDED(device_->SetDepthStencilSurface(depth_))){
            if(!depthCleared_)depthCleared_=SUCCEEDED(device_->Clear(0,nullptr,D3DCLEAR_ZBUFFER,0,1,0));
            useDepth=depthCleared_;
        }
        if(options.useDepth&&!useDepth&&!depthWarning_){depthWarning_=true;log("RE3 model depth surface unavailable; painter ordering fallback\n");}
        hr=setCommon();
        if(SUCCEEDED(hr)&&!cpu)hr=device_->SetStreamSource(0,frame.buffer,0,sizeof(DrawVertex));
        for(unsigned n=0;n<slice.runCount&&SUCCEEDED(hr);++n){const auto& run=frame.runs[slice.firstRun+n];const auto& material=frame.materials[run.materialIndex];
            if(blend&&!options.nativeSemiTransparent&&(((material.flags&AlphaBlend)!=0)!=(blend==2)))continue;
            const DrawVertex* vertices=cpu?frame.cpuVertices.data()+run.firstVertex:nullptr;
            if(remap){for(unsigned v=0;v<run.vertexCount;++v)remapped[v]=RemapModelViewport(vertices[v],frame.viewport,currentViewport);vertices=remapped.data();}
            hr=device_->SetTexture(0,material.textures.albedo);
            if(SUCCEEDED(hr))hr=device_->SetTexture(1,material.textures.stp?material.textures.stp:material.textures.albedo);
            if(FAILED(hr))break;
            if(options.nativeSemiTransparent){
                hr=pass(frame,run,material,useDepth,0,false,0,vertices);
                if(SUCCEEDED(hr))hr=pass(frame,run,material,useDepth,1,true,options.abr,vertices);
            }else hr=pass(frame,run,material,useDepth,-1,(material.flags&AlphaBlend)!=0,0,vertices);
        }
        // State blocks do not restore render/depth targets.
        const HRESULT depthRestore=device_->SetDepthStencilSurface(previousDepth);if(previousDepth)previousDepth->Release();
        HRESULT restored;if(light)restored=saved.restore(device_);else{restored=state->Apply();state->Release();}
        if(SUCCEEDED(hr)&&FAILED(depthRestore))hr=depthRestore;if(SUCCEEDED(hr)&&FAILED(restored))hr=restored;
        return hr;
    }
};

} // namespace re3model
