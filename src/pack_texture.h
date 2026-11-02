#pragma once
// HD backgrounds and mask atlases from .r3tx block files (2026-11-02, r3tx_format.h). Included by proxy.cpp before getScene:
// uses its device originals (method/devices/originalTables), root and log.
// GPU path: the blocks go from the file into small SYSTEMMEM DXT5 strips (<= 1 MB, read straight into the locked strip) and
// UpdateSurface copies them into a DEFAULT DXT5 texture; the pixel shaders of state_shader.h turn YCoCg into RGB. An atlas
// gets a second DXT5 texture whose alpha blocks are the mask alpha (colour blocks zero). No whole-image buffer anywhere.
// CPU path (no DXT5 texture format or no YCoCg shaders, or pilot.ini pack_dxt=2): the same blocks decoded into A8R8G8B8 strips;
// those textures are drawn exactly like the PNG ones.
// pilot.ini pack_dxt (packDxtMode in proxy.cpp): 1 (default) .r3tx on the GPU, 2 .r3tx decoded on the CPU (QA), 0 the PNGs only.
#include "r3tx_format.h"

// packNpot: block textures of any size. D3D9 devices with D3DPTEXTURECAPS_POW2 allow non-power-of-two textures only
// conditionally, and DXT formats are excluded there (2560x1920 / 2048x1920 would fail): those take the CPU path.
static IDirect3DDevice9* packCapsDevice=nullptr;static bool packDxt5=false,packNpot=false;

struct PackTexture{IDirect3DTexture9* colour=nullptr;IDirect3DTexture9* alpha=nullptr;bool ycocg=false;size_t bytes=0;};

static bool packReadAt(HANDLE f,uint64_t offset,void* to,DWORD bytes){
  LARGE_INTEGER at;at.QuadPart=(LONGLONG)offset;DWORD got=0;
  return SetFilePointerEx(f,at,nullptr,FILE_BEGIN)&&ReadFile(f,to,bytes,&got,nullptr)&&got==bytes;
}
// PNG next to a .r3tx: none (release patch), the one it was made from, or a newer one (stale .r3tx: the PNG wins)
static bool packStale(const std::string& png,const r3tx::Header& h){
  HANDLE f=CreateFileA(png.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);if(f==INVALID_HANDLE_VALUE)return false;
  LARGE_INTEGER size{};uint8_t tail[16];uint32_t crc=0;bool same=GetFileSizeEx(f,&size)&&size.QuadPart==h.sourceSize&&size.QuadPart>=16&&
    packReadAt(f,uint64_t(size.QuadPart)-16,tail,16)&&r3tx::PngTail(tail,crc)&&crc==h.sourceCrc;
  CloseHandle(f);return !same;
}
static void packCaps(IDirect3DDevice9* d){
  if(packCapsDevice==d)return;packCapsDevice=d;packDxt5=false;packNpot=false;
  IDirect3D9* d3d=nullptr;D3DDEVICE_CREATION_PARAMETERS cp{};D3DDISPLAYMODE mode{};D3DCAPS9 caps{};
  if(SUCCEEDED(d->GetDirect3D(&d3d))&&SUCCEEDED(d->GetCreationParameters(&cp))&&SUCCEEDED(d->GetDisplayMode(0,&mode)))
    packDxt5=SUCCEEDED(d3d->CheckDeviceFormat(cp.AdapterOrdinal,cp.DeviceType,mode.Format,0,D3DRTYPE_TEXTURE,D3DFMT_DXT5));
  if(d3d)d3d->Release();
  if(SUCCEEDED(d->GetDeviceCaps(&caps)))packNpot=!(caps.TextureCaps&D3DPTEXTURECAPS_POW2);
  log("PACK_TEXTURES mode=%d dxt5=%u npot=%u (pilot.ini pack_dxt: 1 GPU blocks, 2 CPU decode, 0 PNG)",packDxtMode,packDxt5,packNpot);
}

// Rows of 4x4 blocks [by0, by0 + count) of one plane into a locked strip.
//   dxt: colour plane as is (16 bytes per block); alpha plane widened to DXT5 blocks (8 alpha bytes, 8 zero colour bytes)
//   cpu: colour (+ alpha) decoded to BGRA
struct PackStripSource{HANDLE file;const r3tx::Header* h;bool alphaPlane;bool cpu;std::vector<uint8_t>* row;};
static bool packFillStrip(const PackStripSource& s,UINT by0,UINT count,uint8_t* bits,INT pitch){
  const UINT bw=r3tx::BlocksWide(s.h->width);const uint64_t colourAt=sizeof(r3tx::Header),alphaAt=colourAt+s.h->colourBytes;
  auto& row=*s.row;
  for(UINT r=0;r<count;++r){const UINT by=by0+r;uint8_t* dst=bits+size_t(r)*(s.cpu?4u:1u)*pitch;
    if(!s.cpu&&!s.alphaPlane){if(!packReadAt(s.file,colourAt+uint64_t(by)*bw*16,dst,bw*16))return false;continue;}
    if(!s.cpu){row.resize(size_t(bw)*8);if(!packReadAt(s.file,alphaAt+uint64_t(by)*bw*8,row.data(),bw*8))return false;
      for(UINT x=0;x<bw;++x){memcpy(dst+size_t(x)*16,row.data()+size_t(x)*8,8);memset(dst+size_t(x)*16+8,0,8);}continue;}
    const bool alpha=s.h->alphaBytes!=0;row.resize(size_t(bw)*(alpha?24:16));
    if(!packReadAt(s.file,colourAt+uint64_t(by)*bw*16,row.data(),bw*16)||(alpha&&!packReadAt(s.file,alphaAt+uint64_t(by)*bw*8,row.data()+size_t(bw)*16,bw*8)))return false;
    for(UINT x=0;x<bw;++x)r3tx::DecodeBlock(row.data()+size_t(x)*16,alpha?row.data()+size_t(bw)*16+size_t(x)*8:nullptr,dst+size_t(x)*16,size_t(pitch));
  }
  return true;
}
// One DEFAULT texture (DXT5, or A8R8G8B8 for the CPU path) filled strip after strip through UpdateSurface.
static HRESULT packUpload(IDirect3DDevice9* d,const PackStripSource& s,IDirect3DTexture9** out){
  using CreateOffscreenFn=HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*,UINT,UINT,D3DFORMAT,D3DPOOL,IDirect3DSurface9**,HANDLE*);
  using UpdateSurfaceFn=HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*,IDirect3DSurface9*,const RECT*,IDirect3DSurface9*,const POINT*);
  using GetSurfaceFn=HRESULT(STDMETHODCALLTYPE*)(IDirect3DTexture9*,UINT,IDirect3DSurface9**);
  using LockFn=HRESULT(STDMETHODCALLTYPE*)(IDirect3DSurface9*,D3DLOCKED_RECT*,const RECT*,DWORD);
  using UnlockFn=HRESULT(STDMETHODCALLTYPE*)(IDirect3DSurface9*);using ReleaseFn=ULONG(STDMETHODCALLTYPE*)(IUnknown*);
  auto table=[](void* object){auto vt=*(void***)object;auto it=originalTables.find(vt);return it!=originalTables.end()?it->second:vt;};
  const UINT w=s.h->width,h=s.h->height,bw=r3tx::BlocksWide(w),bh=r3tx::BlocksHigh(h);const D3DFORMAT format=s.cpu?D3DFMT_A8R8G8B8:D3DFMT_DXT5;
  const size_t blockRowBytes=s.cpu?size_t(w)*16:size_t(bw)*16;   // bytes of 4 pixel rows
  const UINT stripBlocks=(UINT)(std::max)(size_t(1),(std::min)(size_t(bh),(size_t(1)<<20)/blockRowBytes));
  auto& device=devices.at(d);
  HRESULT hr=method<CreateTextureFn>(device,23)(d,w,h,1,0,format,D3DPOOL_DEFAULT,out,nullptr);
  IDirect3DTexture9* stripTexture=nullptr;IDirect3DSurface9* strip=nullptr;IDirect3DSurface9* level=nullptr;
  // a DXT5 strip is a SYSTEMMEM texture (offscreen plain surfaces of block formats are not reliable in D3D9), A8R8G8B8 a plain surface
  if(SUCCEEDED(hr))hr=s.cpu?((CreateOffscreenFn)device.original[36])(d,w,stripBlocks*4,format,D3DPOOL_SYSTEMMEM,&strip,nullptr)
    :method<CreateTextureFn>(device,23)(d,w,stripBlocks*4,1,0,format,D3DPOOL_SYSTEMMEM,&stripTexture,nullptr);
  if(SUCCEEDED(hr)&&stripTexture)hr=((GetSurfaceFn)table(stripTexture)[18])(stripTexture,0,&strip);
  if(SUCCEEDED(hr))hr=((GetSurfaceFn)table(*out)[18])(*out,0,&level);
  for(UINT by0=0;SUCCEEDED(hr)&&by0<bh;by0+=stripBlocks){
    const UINT count=(std::min)(stripBlocks,bh-by0);D3DLOCKED_RECT lr{};
    hr=((LockFn)table(strip)[13])(strip,&lr,nullptr,0);if(FAILED(hr))break;
    const bool filled=packFillStrip(s,by0,count,(uint8_t*)lr.pBits,lr.Pitch);((UnlockFn)table(strip)[14])(strip);
    if(!filled){hr=HRESULT_FROM_WIN32(ERROR_READ_FAULT);break;}
    const RECT from{0,0,LONG(w),LONG(count*4)};const POINT to{0,LONG(by0*4)};
    hr=((UpdateSurfaceFn)device.original[30])(d,strip,&from,level,&to);
  }
  if(level)((ReleaseFn)table(level)[2])(level);
  if(strip)((ReleaseFn)table(strip)[2])(strip);
  if(stripTexture)((ReleaseFn)table(stripTexture)[2])(stripTexture);
  if(FAILED(hr)&&*out){(*out)->Release();*out=nullptr;}
  return hr;
}
// <relative stem>.r3tx -> textures. False when there is no usable one (missing, stale against its PNG, invalid, failed):
// the caller loads the PNG. gpuReady: the YCoCg shaders exist (else the CPU path).
static bool loadPackTexture(IDirect3DDevice9* d,const std::string& stem,bool gpuReady,PackTexture& out){
  if(!d||!packDxtMode)return false;
  const std::string path=root+"\\"+stem+".r3tx";
  HANDLE f=CreateFileA(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_SEQUENTIAL_SCAN,nullptr);
  if(f==INVALID_HANDLE_VALUE)return false;
  r3tx::Header h{};LARGE_INTEGER size{};DWORD got=0;
  const bool valid=GetFileSizeEx(f,&size)&&ReadFile(f,&h,sizeof(h),&got,nullptr)&&got==sizeof(h)&&r3tx::Valid(h,uint64_t(size.QuadPart));
  if(!valid){CloseHandle(f);log("PACK_TEXTURE_INVALID %s",path.c_str());return false;}
  if(packStale(root+"\\"+stem+".png",h)){CloseHandle(f);log("PACK_TEXTURE_STALE %s (its PNG changed: run tools\\r3tx)",path.c_str());return false;}
  packCaps(d);
  const bool pow2=!(h.width&(h.width-1))&&!(h.height&(h.height-1));
  bool cpu=packDxtMode==2||!packDxt5||!gpuReady||(!packNpot&&!pow2);std::vector<uint8_t> row;HRESULT hr=E_FAIL;
  for(;;){
    hr=packUpload(d,{f,&h,false,cpu,&row},&out.colour);
    if(SUCCEEDED(hr)&&!cpu&&h.alphaBytes)hr=packUpload(d,{f,&h,true,false,&row},&out.alpha);
    if(SUCCEEDED(hr))break;
    if(out.colour)out.colour->Release();if(out.alpha)out.alpha->Release();out={};size_t freeBytes,largest;addressSpace(freeBytes,largest);
    log("PACK_TEXTURE_FAILED hr=%08lx %s path=%s va_free_mb=%zu va_largest_mb=%zu",hr,path.c_str(),cpu?"cpu":"gpu",freeBytes>>20,largest>>20);
    if(cpu)break;cpu=true;   // the driver refused the block texture: the same blocks decoded, drawn like a PNG
  }
  CloseHandle(f);if(FAILED(hr))return false;
  out.ycocg=!cpu;out.bytes=cpu?size_t(h.width)*h.height*4:size_t(h.width)*h.height*(out.alpha?2:1);
  return true;
}
