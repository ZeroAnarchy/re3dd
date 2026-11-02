// RE3 GOG render experiment. Original disk files remain unchanged.
// Version-guarded native renderer hooks plus the GOG D3D9 device.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>
#include <wincodec.h>
#include <cstdio>
#include <cstdarg>
#include <cstdint>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <fstream>
#include <sstream>
#include <mutex>
#include <share.h>
#include <dbghelp.h>
#include <psapi.h>
#include <intrin.h>
#include "exe_map.h"
#include "state_shader.h"
#include "ui_runtime.h"
#include "ui_raw16.h"
#include "native_model_bridge.h"
#include "model_runtime.h"
#include "model_shader.h"
#include "model_skinning_dq.h"
#include "model_skinning_affine_dq.h"
#include "model_face_rig.h"
#include "model_pose_weights.h"
#include "model_motion_smoothing.h"
#include "fps_probe.h"
#include "texture_strips.h"

static HMODULE selfModule, realModule;
static std::string root;
static FILE* logFile;
static std::recursive_mutex mutex;
static unsigned frame;
static bool enabled = true, desiredEnabled = true, trace = true;
static bool quadLightState=true;   // pilot.ini quad_light_state (QuadState below)
static bool textureStrips=true;    // pilot.ini texture_strips (loadReplacement, texture_strips.h)
static UINT debugVaSqueezeMb=0;    // pilot.ini debug_va_squeeze_mb (QA: fragment the address space like a long session)
static bool modelPoseRequested=false,modelReloadRequested=false;
static std::string modelLoadRequest; // pilot.command model-load <asset> (debug_warp)
static std::string modelSequenceRequest;
static unsigned modelSequenceFrames=240,modelSequenceCaptureStride=0;
static std::string nativeDiagnosticRequest;
static std::string glockRequest;            // glock18c.h diagnostics (debug_warp), run on the game thread
static uint8_t glockEquippedItem();         // glock18c.h: equipped Glock item id (53/54) or 0
static void glockDiagnostic();
static const void* glockCombatFor(const void* combat,int weapon); // glock18c.h: AUTO hit table for the enemy shot test
static void glockShotDone(int weapon,const uint8_t* actor,int lifeBefore);
static int modelStateRestoreRequest=-1; // diagnostic A/B: 1 light per-slice state restore, 0 full state block
static int modelGpuRequestCommand=-1,modelGpuPixelRequestCommand=-1;   // pilot.command gpu-on/off, gpu-pixel/vertex (taken at the next build)
static bool f8WasDown = false;
static volatile LONG toggleRequested=0;
static volatile LONG fps60ToggleRequested=0;   // F7: 60 fps HD models on/off (fps60_integration.h)
static unsigned fps60CaptureSequence=0;        // diagnostic capture-fps60: real frame, in-between frame, real frame
static WNDPROC originalWndProc=nullptr;
static void logWindowEvent(HWND,UINT,WPARAM,LPARAM);
static LRESULT CALLBACK pilotWndProc(HWND w,UINT msg,WPARAM wp,LPARAM lp){logWindowEvent(w,msg,wp,lp);if(msg==WM_KEYDOWN&&wp==VK_F8){if(!(lp&(1l<<30)))InterlockedIncrement(&toggleRequested);return 0;}
  if(msg==WM_KEYDOWN&&wp==VK_F7){if(!(lp&(1l<<30)))InterlockedIncrement(&fps60ToggleRequested);return 0;}return CallWindowProcA(originalWndProc,w,msg,wp,lp);}
static void pollToggle(){if(InterlockedExchange(&toggleRequested,0)&1){desiredEnabled=!desiredEnabled;logFile && (fprintf(logFile,"TOGGLE %s\n",desiredEnabled?"remastered":"original"),fflush(logFile));}enabled=desiredEnabled;}
static size_t dumps = 0;
static std::unordered_set<uint64_t> dumped;
static std::unordered_map<uint64_t,std::string> replacements;
struct Object { void** original; void** hooked; DWORD protection; };
struct Texture { Object obj; D3DSURFACE_DESC desc{}; D3DLOCKED_RECT lock{};
  RECT rect{}; bool locked=false; uint64_t hash=0; IDirect3DTexture9* replacement=nullptr; };
struct Surface { Object obj; D3DSURFACE_DESC desc{}; D3DLOCKED_RECT lock{};
  RECT rect{}; bool locked=false; IDirect3DTexture9* texture=nullptr; uint64_t hash=0; };
static std::unordered_map<void*,Object> devices, d3ds;
static std::unordered_map<IDirect3DTexture9*,Texture> textures;
static std::unordered_map<IDirect3DSurface9*,Surface> surfaces;
static uint64_t boundHash[16]{};
static bool boundReplaced[16]{};
static std::unordered_set<uint64_t> drawnHashes;
static std::unordered_map<void**,void**> originalTables;
static IDirect3DDevice9* gameDevice=nullptr;
static uint8_t* gogWrapper=nullptr;
static re3ui::Registry uiRegistry;
static re3ui::Raw16Registry uiRawRegistry;
static bool uiRawReady=false;
static bool uiReady=false;
static unsigned uiFrameSerial=0,uiDraws=0;
static void installGameHooks();
static void gpuFlushPending();   // model_integration.h: models gathered by the GPU path go down before any other draw
static bool gpuFlushingNow();
static void gpuTraceNote(const char* text);
static void keepDeviceHooks();
static void gpuFlushRect(float left,float top,float right,float bottom);
static size_t gpuPendingCount();
static void beforeModelDeviceReset();
static void afterModelDeviceReset(HRESULT);
static size_t releaseModelDeviceResources(ULONG*);

// "module+offset" for crash lines; addresses outside any module stay raw.
static void describeAddress(const void* address,char* out,size_t size){
  HMODULE module=nullptr;char path[MAX_PATH];
  if(GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,(LPCSTR)address,&module)&&module&&GetModuleFileNameA(module,path,MAX_PATH)){
    const char* base=strrchr(path,'\\');sprintf_s(out,size,"%s+%x",base?base+1:path,(unsigned)((const uint8_t*)address-(const uint8_t*)module));
  }else sprintf_s(out,size,"%p",address);
}
static bool loggedExceptionCode(DWORD code){
  switch(code){
    case EXCEPTION_ACCESS_VIOLATION:case EXCEPTION_STACK_OVERFLOW:case EXCEPTION_ILLEGAL_INSTRUCTION:case EXCEPTION_PRIV_INSTRUCTION:
    case EXCEPTION_INT_DIVIDE_BY_ZERO:case EXCEPTION_IN_PAGE_ERROR:case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:case EXCEPTION_DATATYPE_MISALIGNMENT:
    case 0xE06D7363u: // C++ throw
      return true;
  }
  return false;
}
static LONG WINAPI exceptionLog(EXCEPTION_POINTERS* ex) {
  static volatile LONG logged=0;const auto* r=ex->ExceptionRecord;
  if(logFile&&loggedExceptionCode(r->ExceptionCode)&&InterlockedIncrement(&logged)<=64){
    char where[MAX_PATH+16];describeAddress(r->ExceptionAddress,where,sizeof(where));
    fprintf(logFile,"[%u] EXCEPTION %08lx address=%p (%s) access=%lu target=%p thread=%lu\n",frame,r->ExceptionCode,r->ExceptionAddress,where,
      (unsigned long)(r->NumberParameters>0?r->ExceptionInformation[0]:0),(void*)(r->NumberParameters>1?r->ExceptionInformation[1]:0),GetCurrentThreadId());fflush(logFile);
  }return EXCEPTION_CONTINUE_SEARCH;
}
// Last chance: log, list return addresses on the stack that point into modules, write a small minidump.
using MiniDumpWriteDumpFn=BOOL(WINAPI*)(HANDLE,DWORD,HANDLE,MINIDUMP_TYPE,PMINIDUMP_EXCEPTION_INFORMATION,PMINIDUMP_USER_STREAM_INFORMATION,PMINIDUMP_CALLBACK_INFORMATION);
static MiniDumpWriteDumpFn miniDumpWriteDump=nullptr;
static LPTOP_LEVEL_EXCEPTION_FILTER previousCrashFilter=nullptr;
static LONG WINAPI crashFilter(EXCEPTION_POINTERS* ex){
  static volatile LONG once=0;
  if(!InterlockedExchange(&once,1)&&logFile){
    const auto* r=ex->ExceptionRecord;const auto* c=ex->ContextRecord;char where[MAX_PATH+16];describeAddress(r->ExceptionAddress,where,sizeof(where));
    fprintf(logFile,"[%u] UNHANDLED_EXCEPTION %08lx address=%p (%s) esp=%08lx ebp=%08lx thread=%lu\n",frame,r->ExceptionCode,r->ExceptionAddress,where,c->Esp,c->Ebp,GetCurrentThreadId());
    const auto* stack=reinterpret_cast<const uintptr_t*>(c->Esp);MEMORY_BASIC_INFORMATION mbi{};unsigned listed=0;
    if(VirtualQuery(stack,&mbi,sizeof(mbi))&&mbi.State==MEM_COMMIT&&!(mbi.Protect&(PAGE_GUARD|PAGE_NOACCESS))){
      const auto end=reinterpret_cast<const uintptr_t*>(static_cast<const uint8_t*>(mbi.BaseAddress)+mbi.RegionSize);
      for(const uintptr_t* p=stack;p<end&&p<stack+512&&listed<24;++p){
        HMODULE m=nullptr;if(*p<0x10000||!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,(LPCSTR)*p,&m)||!m)continue;
        describeAddress((const void*)*p,where,sizeof(where));fprintf(logFile,"  stack+%03x %s\n",unsigned((p-stack)*sizeof(uintptr_t)),where);++listed;
      }
    }
    fflush(logFile);
    if(miniDumpWriteDump){
      CreateDirectoryA((root+"\\crash-dumps").c_str(),nullptr);SYSTEMTIME t;GetLocalTime(&t);char path[MAX_PATH];
      sprintf_s(path,"%s\\crash-dumps\\re3-%04u%02u%02u-%02u%02u%02u.dmp",root.c_str(),t.wYear,t.wMonth,t.wDay,t.wHour,t.wMinute,t.wSecond);
      HANDLE file=CreateFileA(path,GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
      if(file!=INVALID_HANDLE_VALUE){MINIDUMP_EXCEPTION_INFORMATION info{GetCurrentThreadId(),ex,FALSE};
        const BOOL ok=miniDumpWriteDump(GetCurrentProcess(),GetCurrentProcessId(),file,MINIDUMP_TYPE(MiniDumpWithThreadInfo|MiniDumpWithIndirectlyReferencedMemory),&info,nullptr,nullptr);
        CloseHandle(file);fprintf(logFile,"[%u] CRASH_DUMP %s written=%d\n",frame,path,ok);fflush(logFile);}
    }
  }
  return previousCrashFilter?previousCrashFilter(ex):EXCEPTION_CONTINUE_SEARCH;
}

static void log(const char* fmt,...) {
  if (!logFile) return; va_list args; va_start(args,fmt);
  fprintf(logFile,"[%u] ",frame); vfprintf(logFile,fmt,args); fputc('\n',logFile); fflush(logFile); va_end(args);
}
// Focus/size/system events of the game window: a lost exclusive-fullscreen device starts with one.
static void logWindowEvent(HWND w,UINT msg,WPARAM wp,LPARAM lp){
  char detail[160]="";const char* name=nullptr;
  switch(msg){
    case WM_ACTIVATEAPP:name="WM_ACTIVATEAPP";sprintf_s(detail,"active=%u other_thread=%lu",unsigned(wp!=0),(unsigned long)lp);break;
    case WM_SIZE:if(wp==SIZE_MINIMIZED||wp==SIZE_RESTORED||wp==SIZE_MAXIMIZED){name="WM_SIZE";sprintf_s(detail,"type=%u %ux%u",unsigned(wp),LOWORD(lp),HIWORD(lp));}break;
    case WM_DISPLAYCHANGE:name="WM_DISPLAYCHANGE";sprintf_s(detail,"%ux%u bpp=%u",LOWORD(lp),HIWORD(lp),unsigned(wp));break;
    case WM_SYSCOMMAND:{const unsigned cmd=unsigned(wp&0xfff0);if(cmd==SC_MINIMIZE||cmd==SC_RESTORE||cmd==SC_CLOSE||cmd==SC_SCREENSAVE||cmd==SC_MONITORPOWER){name="WM_SYSCOMMAND";sprintf_s(detail,"command=%04x",cmd);}break;}
    case WM_CLOSE:name="WM_CLOSE";break;
    case WM_DESTROY:name="WM_DESTROY";break;
    case WM_QUERYENDSESSION:name="WM_QUERYENDSESSION";break;
  }
  if(!name)return;
  if(msg==WM_ACTIVATEAPP&&!wp){
    HWND fg=GetForegroundWindow();DWORD pid=0;char title[80]="",exe[MAX_PATH]="?";
    if(fg){GetWindowThreadProcessId(fg,&pid);GetWindowTextA(fg,title,sizeof(title));}
    if(HANDLE p=pid?OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid):nullptr){DWORD n=MAX_PATH;QueryFullProcessImageNameA(p,0,exe,&n);CloseHandle(p);}
    log("WINDOW %s %s foreground_pid=%lu exe=%s title=\"%s\"",name,detail,pid,exe,title);
  }else log("WINDOW %s %s hwnd=%p",name,detail,w);
}
static uint64_t hashPixels(const uint8_t* p,int pitch,UINT w,UINT h,UINT bpp,D3DFORMAT fmt) {
  uint64_t x=14695981039346656037ull;
  auto mix=[&](uint8_t c){x=(x^c)*1099511628211ull;};
  for (auto n:{w,h,(UINT)fmt}) for(int k=0;k<4;k++)mix((n>>(8*k))&255);
  for(UINT y=0;y<h;y++)for(UINT b=0;b<w*bpp;b++)mix(p[y*pitch+b]);
  return x;
}
static unsigned bytesPerPixel(D3DFORMAT f) {
  switch(f){case D3DFMT_A8R8G8B8: case D3DFMT_X8R8G8B8: return 4;
    case D3DFMT_A1R5G5B5: case D3DFMT_X1R5G5B5: case D3DFMT_R5G6B5: case D3DFMT_A4R4G4B4:return 2;
    default:return 0;}
}
static uint64_t capture(const D3DLOCKED_RECT& lr,const RECT& r,const D3DSURFACE_DESC& d,const char* kind) {
  unsigned bpp=bytesPerPixel(d.Format);
  if(!lr.pBits||!bpp||r.left||r.top||r.right!=(LONG)d.Width||r.bottom!=(LONG)d.Height)return 0;
  if(d.Width>8192||d.Height>8192||lr.Pitch<(int)(d.Width*bpp)) return 0;
  auto* data=(const uint8_t*)lr.pBits;
  uint64_t hash=hashPixels(data,lr.Pitch,d.Width,d.Height,bpp,d.Format);
  if(trace && d.Width>=64 && d.Height>=64 && dumps<1500 && dumped.insert(hash).second) {
    char name[160]; sprintf_s(name,"\\captures\\%016llx_%ux%u_f%u.tga",hash,d.Width,d.Height,(UINT)d.Format);
    FILE* f=nullptr; fopen_s(&f,(root+name).c_str(),"wb");
    if(f){uint8_t hdr[18]{};hdr[2]=2;hdr[12]=d.Width&255;hdr[13]=d.Width>>8;hdr[14]=d.Height&255;hdr[15]=d.Height>>8;hdr[16]=32;hdr[17]=0x28;fwrite(hdr,1,18,f);
      std::vector<uint8_t> row(d.Width*4);
      for(UINT y=0;y<d.Height;y++) {auto* p=data+y*lr.Pitch;
        for(UINT x=0;x<d.Width;x++) {auto* dst=&row[x*4];
          if(bpp==4){memcpy(dst,p+x*4,4);if(d.Format==D3DFMT_X8R8G8B8)dst[3]=255;}
          else {uint16_t v=((const uint16_t*)p)[x];
            if(d.Format==D3DFMT_R5G6B5){dst[0]=(v&31)*255/31;dst[1]=((v>>5)&63)*255/63;dst[2]=((v>>11)&31)*255/31;dst[3]=255;}
            else if(d.Format==D3DFMT_A4R4G4B4){dst[0]=(v&15)*17;dst[1]=((v>>4)&15)*17;dst[2]=((v>>8)&15)*17;dst[3]=((v>>12)&15)*17;}
            else {dst[0]=(v&31)*255/31;dst[1]=((v>>5)&31)*255/31;dst[2]=((v>>10)&31)*255/31;dst[3]=d.Format==D3DFMT_X1R5G5B5?255:((v&32768)?255:0);}
          }
        }fwrite(row.data(),1,row.size(),f);
      }fclose(f);dumps++;log("CAPTURE %s %016llx %ux%u format=%u pool=%u usage=%08x",kind,hash,d.Width,d.Height,d.Format,d.Pool,d.Usage);
    }
  }
  return hash;
}
static Object cloneTable(void* p,size_t n) {
  auto table=*(void***)p;auto it=originalTables.find(table);void** copy;
  if(it==originalTables.end()){copy=new void*[n];memcpy(copy,table,n*sizeof(void*));originalTables.emplace(table,copy);}else copy=it->second;
  DWORD old;VirtualProtect(table,n*sizeof(void*),PAGE_READWRITE,&old);return {copy,table,old};
}
template<class F> static F method(Object& o,size_t index){return reinterpret_cast<F>(o.original[index]);}
static void loadMap() {
  std::ifstream in(root+"\\replacements.txt");std::string line;
  while(std::getline(in,line)){if(line.empty()||line[0]=='#')continue;std::istringstream s(line);uint64_t hash;std::string path;
    if(s>>std::hex>>hash){std::getline(s>>std::ws,path);if(!path.empty()) replacements[hash]=path;}}
  log("Loaded %zu replacement mappings",replacements.size());
}
static void init() {
  char p[MAX_PATH];GetModuleFileNameA(selfModule,p,MAX_PATH);root=p;root.resize(root.find_last_of("\\/"));
  CreateDirectoryA((root+"\\captures").c_str(),nullptr);logFile=_fsopen((root+"\\runtime.log").c_str(),"w",_SH_DENYNO);
  AddVectoredExceptionHandler(1,exceptionLog);
  if(HMODULE help=LoadLibraryA("dbghelp.dll"))miniDumpWriteDump=(MiniDumpWriteDumpFn)GetProcAddress(help,"MiniDumpWriteDump");
  previousCrashFilter=SetUnhandledExceptionFilter(crashFilter);
  char sys[MAX_PATH];GetSystemDirectoryA(sys,MAX_PATH);strcat_s(sys,"\\d3d9.dll");realModule=LoadLibraryA(sys);
  trace=GetPrivateProfileIntA("pilot","trace",1,(root+"\\pilot.ini").c_str())!=0;
  enabled=GetPrivateProfileIntA("pilot","enabled",1,(root+"\\pilot.ini").c_str())!=0;desiredEnabled=enabled;
  quadLightState=GetPrivateProfileIntA("pilot","quad_light_state",1,(root+"\\pilot.ini").c_str())!=0;
  textureStrips=GetPrivateProfileIntA("pilot","texture_strips",1,(root+"\\pilot.ini").c_str())!=0;
  debugVaSqueezeMb=(UINT)GetPrivateProfileIntA("pilot","debug_va_squeeze_mb",0,(root+"\\pilot.ini").c_str());
  CoInitializeEx(nullptr,COINIT_MULTITHREADED);loadMap();std::string uiError;uiReady=uiRegistry.load(root+"\\ui-pack\\manifest.txt",&uiError);log("UI_REGISTRY ready=%u %s",uiReady,uiError.c_str());installGameHooks();
  if(GetPrivateProfileIntA("pilot","fps_probe",0,(root+"\\pilot.ini").c_str()))fpsprobe::Install([](const char* line){log("%s",line);});
  log("RE3 pilot proxy initialized. Original D3D9=%s",sys);
}
static void hookSurface(IDirect3DSurface9*,IDirect3DTexture9*);
// While a movie plays (MOVIE_OPEN..MOVIE_CLOSE) the 1920x960 movie texture/surface is rewritten every frame; hashing
// it costs 10 ms per lock on the game thread (texture hooks exist only with trace=1). Nothing that large is replaced.
static bool moviePlaying=false;
static bool movieFrameBuffer(const D3DSURFACE_DESC& d){return moviePlaying&&d.Width*d.Height>=640u*320u;}

static ULONG STDMETHODCALLTYPE textureRelease(IDirect3DTexture9* t){
  std::lock_guard<std::recursive_mutex> guard(mutex);auto it=textures.find(t);if(it==textures.end())return ((ULONG(STDMETHODCALLTYPE*)(IDirect3DTexture9*))originalTables.at(*(void***)t)[2])(t);
  auto orig=method<ULONG(STDMETHODCALLTYPE*)(IDirect3DTexture9*)>(it->second.obj,2);auto n=orig(t);
  if(!n){auto repl=it->second.replacement;textures.erase(it);if(repl)repl->Release();}
  return n;
}
static HRESULT STDMETHODCALLTYPE textureLock(IDirect3DTexture9* t,UINT level,D3DLOCKED_RECT* l,const RECT* r,DWORD flags){
  std::lock_guard<std::recursive_mutex> guard(mutex);auto& a=textures.at(t);
  auto hr=method<HRESULT(STDMETHODCALLTYPE*)(IDirect3DTexture9*,UINT,D3DLOCKED_RECT*,const RECT*,DWORD)>(a.obj,19)(t,level,l,r,flags);
  if(SUCCEEDED(hr)&&level==0){a.lock=*l;a.rect=r?*r:RECT{0,0,(LONG)a.desc.Width,(LONG)a.desc.Height};a.locked=true;}
  return hr;
}
static HRESULT STDMETHODCALLTYPE textureUnlock(IDirect3DTexture9* t,UINT level){
  std::lock_guard<std::recursive_mutex> guard(mutex);auto& a=textures.at(t);
  if(level==0&&a.locked){uint64_t h=movieFrameBuffer(a.desc)?0:capture(a.lock,a.rect,a.desc,"texture");if(h&&h!=a.hash){a.hash=h;if(a.replacement){a.replacement->Release();a.replacement=nullptr;}}a.locked=false;}
  return method<HRESULT(STDMETHODCALLTYPE*)(IDirect3DTexture9*,UINT)>(a.obj,20)(t,level);
}
static HRESULT STDMETHODCALLTYPE textureGetSurface(IDirect3DTexture9* t,UINT level,IDirect3DSurface9** s){
  std::lock_guard<std::recursive_mutex> guard(mutex);auto& a=textures.at(t);
  auto hr=method<HRESULT(STDMETHODCALLTYPE*)(IDirect3DTexture9*,UINT,IDirect3DSurface9**)>(a.obj,18)(t,level,s);
  if(SUCCEEDED(hr)&&level==0)hookSurface(*s,t);return hr;
}
static void hookTexture(IDirect3DTexture9* t){
  if(textures.count(t))return;Texture a{};t->GetLevelDesc(0,&a.desc);a.obj=cloneTable(t,64);
  a.obj.hooked[2]=(void*)textureRelease;a.obj.hooked[18]=(void*)textureGetSurface;a.obj.hooked[19]=(void*)textureLock;a.obj.hooked[20]=(void*)textureUnlock;
  textures.emplace(t,a);log("TEXTURE %p %ux%u format=%u pool=%u usage=%08x",t,a.desc.Width,a.desc.Height,a.desc.Format,a.desc.Pool,a.desc.Usage);
}
static ULONG STDMETHODCALLTYPE surfaceRelease(IDirect3DSurface9* t){
  std::lock_guard<std::recursive_mutex> guard(mutex);auto it=surfaces.find(t);if(it==surfaces.end())return ((ULONG(STDMETHODCALLTYPE*)(IDirect3DSurface9*))originalTables.at(*(void***)t)[2])(t);
  auto n=method<ULONG(STDMETHODCALLTYPE*)(IDirect3DSurface9*)>(it->second.obj,2)(t);
  if(!n){surfaces.erase(it);}return n;
}
static HRESULT STDMETHODCALLTYPE surfaceLock(IDirect3DSurface9* t,D3DLOCKED_RECT* l,const RECT* r,DWORD flags){
  std::lock_guard<std::recursive_mutex> guard(mutex);auto& a=surfaces.at(t);
  auto hr=method<HRESULT(STDMETHODCALLTYPE*)(IDirect3DSurface9*,D3DLOCKED_RECT*,const RECT*,DWORD)>(a.obj,13)(t,l,r,flags);
  if(SUCCEEDED(hr)){a.lock=*l;a.rect=r?*r:RECT{0,0,(LONG)a.desc.Width,(LONG)a.desc.Height};a.locked=true;}return hr;
}
static HRESULT STDMETHODCALLTYPE surfaceUnlock(IDirect3DSurface9* t){
  std::lock_guard<std::recursive_mutex> guard(mutex);auto& a=surfaces.at(t);
  if(a.locked&&movieFrameBuffer(a.desc))a.locked=false;
  if(a.locked){a.hash=capture(a.lock,a.rect,a.desc,"surface");a.locked=false;
    if(a.texture&&textures.count(a.texture)){auto& tx=textures.at(a.texture);if(a.hash!=tx.hash){tx.hash=a.hash;if(tx.replacement){tx.replacement->Release();tx.replacement=nullptr;}}}}
  return method<HRESULT(STDMETHODCALLTYPE*)(IDirect3DSurface9*)>(a.obj,14)(t);
}
static void hookSurface(IDirect3DSurface9* s,IDirect3DTexture9* t){
  if(surfaces.count(s)){if(t)surfaces.at(s).texture=t;return;}Surface a{};a.texture=t;s->GetDesc(&a.desc);a.obj=cloneTable(s,64);
  a.obj.hooked[2]=(void*)surfaceRelease;a.obj.hooked[13]=(void*)surfaceLock;a.obj.hooked[14]=(void*)surfaceUnlock;surfaces.emplace(s,a);
}

using CreateTextureFn=HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*,UINT,UINT,UINT,DWORD,D3DFORMAT,D3DPOOL,IDirect3DTexture9**,HANDLE*);
static HRESULT STDMETHODCALLTYPE createTexture(IDirect3DDevice9* d,UINT w,UINT h,UINT l,DWORD u,D3DFORMAT f,D3DPOOL p,IDirect3DTexture9** t,HANDLE* sh){
  std::lock_guard<std::recursive_mutex> guard(mutex);auto hr=method<CreateTextureFn>(devices.at(d),23)(d,w,h,l,u,f,p,t,sh);if(SUCCEEDED(hr))hookTexture(*t);return hr;
}
static HRESULT STDMETHODCALLTYPE updateTexture(IDirect3DDevice9* d,IDirect3DBaseTexture9* s,IDirect3DBaseTexture9* t){
  std::lock_guard<std::recursive_mutex> guard(mutex);auto hr=method<HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*,IDirect3DBaseTexture9*,IDirect3DBaseTexture9*)>(devices.at(d),31)(d,s,t);
  auto from=textures.find((IDirect3DTexture9*)s),to=textures.find((IDirect3DTexture9*)t);
  if(SUCCEEDED(hr)&&from!=textures.end()&&to!=textures.end()&&from->second.hash!=to->second.hash){to->second.hash=from->second.hash;if(to->second.replacement){to->second.replacement->Release();to->second.replacement=nullptr;}}
  return hr;
}
static HRESULT STDMETHODCALLTYPE updateSurface(IDirect3DDevice9* d,IDirect3DSurface9* s,const RECT* r,IDirect3DSurface9* t,const POINT* p){
  std::lock_guard<std::recursive_mutex> guard(mutex);auto hr=method<HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*,IDirect3DSurface9*,const RECT*,IDirect3DSurface9*,const POINT*)>(devices.at(d),30)(d,s,r,t,p);
  auto from=surfaces.find(s),to=surfaces.find(t);
  if(SUCCEEDED(hr)&&from!=surfaces.end()&&to!=surfaces.end()&&!r&&(!p||(!p->x&&!p->y))){to->second.hash=from->second.hash;auto tx=textures.find(to->second.texture);if(tx!=textures.end())tx->second.hash=from->second.hash;}
  return hr;
}
static HRESULT STDMETHODCALLTYPE createOffscreen(IDirect3DDevice9* d,UINT w,UINT h,D3DFORMAT f,D3DPOOL p,IDirect3DSurface9** t,HANDLE* sh){
  std::lock_guard<std::recursive_mutex> guard(mutex);auto hr=method<HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*,UINT,UINT,D3DFORMAT,D3DPOOL,IDirect3DSurface9**,HANDLE*)>(devices.at(d),36)(d,w,h,f,p,t,sh);if(SUCCEEDED(hr))hookSurface(*t,nullptr);return hr;
}
// Free address space of this 32-bit process (2 GB, no large-address flag) and its largest block (movies, loads, QA).
static void addressSpace(size_t& freeBytes,size_t& largest){
  freeBytes=largest=0;MEMORY_BASIC_INFORMATION mbi{};
  for(uintptr_t at=0x10000;at<0x7fff0000&&VirtualQuery((void*)at,&mbi,sizeof(mbi));at=(uintptr_t)mbi.BaseAddress+mbi.RegionSize){
    if(mbi.State==MEM_FREE){freeBytes+=mbi.RegionSize;largest=(std::max)(largest,(size_t)mbi.RegionSize);}
    if(!mbi.RegionSize)break;
  }
}
static unsigned stripLoads=0,stagingLoads=0;
// QA only (debug_va_squeeze_mb=N): 1 MB reserved after every N MB of free address space, so no free block is larger
// than about N MB, like the fragmented address space of a long session (2026-10-31: 4096 staging failed, 2048 loaded).
static void squeezeAddressSpace(UINT mb){
  const size_t step=size_t(mb)<<20,slice=size_t(1)<<20;size_t reserved=0;MEMORY_BASIC_INFORMATION m;
  for(uintptr_t a=0x10000;a<0x7FFF0000&&VirtualQuery((void*)a,&m,sizeof m);a=(uintptr_t)m.BaseAddress+m.RegionSize){
    if(m.State!=MEM_FREE)continue;
    const uintptr_t from=((uintptr_t)m.BaseAddress+0xFFFF)&~uintptr_t(0xFFFF),end=(uintptr_t)m.BaseAddress+m.RegionSize;
    for(uintptr_t p=from+step;p+slice<=end;p+=step+slice)if(VirtualAlloc((void*)p,slice,MEM_RESERVE,PAGE_NOACCESS))reserved+=slice;
  }
  size_t freeBytes,largest;addressSpace(freeBytes,largest);
  log("DEBUG_VA_SQUEEZE block_mb=%u reserved_mb=%zu free_mb=%zu largest_mb=%zu",mb,reserved>>20,freeBytes>>20,largest>>20);
}
// mipLevels > 1 (HD model textures): box-filtered chain down to mipLevels levels. Without it a 1024/2048
// texture minified 20-30x on an actor at play distance aliases (sparkling eyes, shimmering hair).
static IDirect3DTexture9* loadReplacement(IDirect3DDevice9* d,uint64_t hash,const std::string& relative,UINT mipLevels=1){
  if(!d)return nullptr;
  static bool squeezed=false;if(debugVaSqueezeMb&&mipLevels>1&&!squeezed){squeezed=true;squeezeAddressSpace(debugVaSqueezeMb);}
  std::string path=root+"\\"+relative;std::wstring wide(path.begin(),path.end());
  IWICImagingFactory* factory=nullptr;IWICBitmapDecoder* decoder=nullptr;IWICBitmapFrameDecode* image=nullptr;IWICFormatConverter* conv=nullptr;
  IDirect3DTexture9* out=nullptr;UINT w=0,h=0;HRESULT hr=CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory));
  if(SUCCEEDED(hr))hr=factory->CreateDecoderFromFilename(wide.c_str(),nullptr,GENERIC_READ,WICDecodeMetadataCacheOnLoad,&decoder);
  if(SUCCEEDED(hr))hr=decoder->GetFrame(0,&image);
  if(SUCCEEDED(hr))hr=image->GetSize(&w,&h);
  if(SUCCEEDED(hr)&&(w>8192||h>8192||w==0||h==0))hr=E_INVALIDARG;
  if(SUCCEEDED(hr))hr=factory->CreateFormatConverter(&conv);
  if(SUCCEEDED(hr))hr=conv->Initialize(image,GUID_WICPixelFormat32bppBGRA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom);
  UINT levels=1;
  if(SUCCEEDED(hr)&&mipLevels>1)for(UINT side=(std::min)(w,h);levels<mipLevels&&side>=128;side/=2)++levels; // smallest level >= 64
  // Decoded into a SYSTEMMEM staging texture and copied to a DEFAULT (video memory) one. MANAGED textures
  // keep a second copy inside this 32-bit process (2 GB, no large-address flag): ~350 MB of HD backgrounds,
  // UI and models left too little room and later backgrounds failed with E_OUTOFMEMORY.
  // Images above 4 MB go up in strips instead (texture_strips.h): the whole-image staging needs one free block of
  // its size (85 MB for 4096x4096 with mips), which a long session no longer has.
  IDirect3DTexture9* staging=nullptr;const char* pool="default";UINT strips=0;
  auto originalTable=[](void* object){auto vt=*(void***)object;auto it=originalTables.find(vt);return it!=originalTables.end()?it->second:vt;};
  if(SUCCEEDED(hr)&&textureStrips&&re3strips::Eligible(w,h,levels)){
    HRESULT sh=method<CreateTextureFn>(devices.at(d),23)(d,w,h,levels,0,D3DFMT_A8R8G8B8,D3DPOOL_DEFAULT,&out,nullptr);
    if(SUCCEEDED(sh))sh=re3strips::Upload(d,devices.at(d).original,originalTable,conv,w,h,levels,out);
    if(SUCCEEDED(sh)){const UINT rows=re3strips::StripRows(w,h,levels);strips=(h+rows-1)/rows;}
    else{if(out){out->Release();out=nullptr;}log("REPLACEMENT_STRIPS_FAILED hr=%08lx %s; whole-image staging",sh,path.c_str());}
  }
  auto textureVtable=[&](IDirect3DTexture9* t){return originalTable(t);};
  using LockFn=HRESULT(STDMETHODCALLTYPE*)(IDirect3DTexture9*,UINT,D3DLOCKED_RECT*,const RECT*,DWORD);using UnlockFn=HRESULT(STDMETHODCALLTYPE*)(IDirect3DTexture9*,UINT);
  if(SUCCEEDED(hr)&&!strips)hr=method<CreateTextureFn>(devices.at(d),23)(d,w,h,levels,0,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,&staging,nullptr);
  if(SUCCEEDED(hr)&&!strips){auto vt=textureVtable(staging);auto lock=(LockFn)vt[19];auto unlock=(UnlockFn)vt[20];D3DLOCKED_RECT lr;
    hr=lock(staging,0,&lr,nullptr,0);
    if(SUCCEEDED(hr)){hr=conv->CopyPixels(nullptr,lr.Pitch,lr.Pitch*h,(BYTE*)lr.pBits);unlock(staging,0);}
    // box-filtered mip chain, each level from the one above it
    for(UINT level=1,lw=w,lh=h;SUCCEEDED(hr)&&level<levels;++level){
      D3DLOCKED_RECT above,here;hr=lock(staging,level-1,&above,nullptr,D3DLOCK_READONLY);if(FAILED(hr))break;
      hr=lock(staging,level,&here,nullptr,0);if(FAILED(hr)){unlock(staging,level-1);break;}
      const UINT nw=(std::max)(1u,lw/2),nh=(std::max)(1u,lh/2);
      for(UINT y=0;y<nh;++y)re3strips::BoxRow((const uint8_t*)above.pBits+size_t((std::min)(2*y,lh-1))*above.Pitch,(const uint8_t*)above.pBits+size_t((std::min)(2*y+1,lh-1))*above.Pitch,
        (uint8_t*)here.pBits+size_t(y)*here.Pitch,lw,nw);
      unlock(staging,level);unlock(staging,level-1);lw=nw;lh=nh;
    }}
  if(SUCCEEDED(hr)&&!strips){
    hr=method<CreateTextureFn>(devices.at(d),23)(d,w,h,levels,0,D3DFMT_A8R8G8B8,D3DPOOL_DEFAULT,&out,nullptr);
    if(SUCCEEDED(hr))hr=method<HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*,IDirect3DBaseTexture9*,IDirect3DBaseTexture9*)>(devices.at(d),31)(d,staging,out);
    if(FAILED(hr)){ // video memory refused: the previous MANAGED path, filled from the staging levels
      if(out){out->Release();out=nullptr;}pool="managed";
      hr=method<CreateTextureFn>(devices.at(d),23)(d,w,h,levels,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&out,nullptr);
      if(SUCCEEDED(hr)){auto sv=textureVtable(staging);auto ov=textureVtable(out);
        for(UINT level=0,lw=w,lh=h;SUCCEEDED(hr)&&level<levels;++level,lw=(std::max)(1u,lw/2),lh=(std::max)(1u,lh/2)){
          D3DLOCKED_RECT from,to;hr=((LockFn)sv[19])(staging,level,&from,nullptr,D3DLOCK_READONLY);if(FAILED(hr))break;
          hr=((LockFn)ov[19])(out,level,&to,nullptr,0);
          if(SUCCEEDED(hr)){for(UINT y=0;y<lh;++y)memcpy((uint8_t*)to.pBits+size_t(y)*to.Pitch,(const uint8_t*)from.pBits+size_t(y)*from.Pitch,size_t(lw)*4);((UnlockFn)ov[20])(out,level);}
          ((UnlockFn)sv[20])(staging,level);
        }}
    }
  }
  if(staging)staging->Release();
  if(conv)conv->Release();if(image)image->Release();if(decoder)decoder->Release();if(factory)factory->Release();
  if(FAILED(hr)){if(out)out->Release();out=nullptr;size_t freeBytes,largest;addressSpace(freeBytes,largest);
    log("REPLACEMENT_FAILED %016llx hr=%08lx %s va_free_mb=%zu va_largest_mb=%zu",hash,hr,path.c_str(),freeBytes>>20,largest>>20);}
  else if(strips){++stripLoads;log("REPLACEMENT_LOADED %016llx %ux%u mips=%u pool=%s strips=%u %s",hash,w,h,levels,pool,strips,path.c_str());}
  else{++stagingLoads;if(levels>1||strcmp(pool,"default"))log("REPLACEMENT_LOADED %016llx %ux%u mips=%u pool=%s %s",hash,w,h,levels,pool,path.c_str());else log("REPLACEMENT_LOADED %016llx %ux%u %s",hash,w,h,path.c_str());}
  return out;
}
static HRESULT STDMETHODCALLTYPE setTexture(IDirect3DDevice9* d,DWORD stage,IDirect3DBaseTexture9* t){
  std::lock_guard<std::recursive_mutex> guard(mutex);auto it=textures.find((IDirect3DTexture9*)t);uint64_t hash=0;bool replaced=false;
  if(it!=textures.end()){auto& tx=it->second;hash=tx.hash;if(enabled){auto repl=replacements.find(hash);if(repl!=replacements.end()){
      if(!tx.replacement)tx.replacement=loadReplacement(d,hash,repl->second);if(tx.replacement){t=tx.replacement;replaced=true;}}}}
  if(stage<16){boundHash[stage]=hash;boundReplaced[stage]=replaced;}
  return method<HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*,DWORD,IDirect3DBaseTexture9*)>(devices.at(d),65)(d,stage,t);
}
// The originals of a device call. A device the hooks were not installed for (another device on a shared table) gets
// the originals of its table, never the hooked entries themselves.
static Object& deviceObject(IDirect3DDevice9* d){
  auto it=devices.find(d);if(it!=devices.end())return it->second;
  static Object other;void** table=*(void***)d;auto found=originalTables.find(table);other={found!=originalTables.end()?found->second:table,table,0};return other;
}
// pilot.ini gpu_device_trace=1: what reaches the device besides draws, one line per frame whose sequence differs from the last
static bool gpuTraceOn=false;static std::string gpuTraceText,gpuTraceSign,gpuTraceLast;static unsigned gpuTraceLogged=0,gpuTraceDraws=0;
static void gpuTraceEvent(const std::string& text){
  if(gpuTraceText.size()<1500){if(gpuTraceDraws)gpuTraceText+="d"+std::to_string(gpuTraceDraws)+" ";gpuTraceText+=text;gpuTraceText+=' ';}
  gpuTraceDraws=0;gpuTraceSign+=text.substr(0,text.find(':'));gpuTraceSign+=gpuPendingCount()?'+':'-';
}
static void gpuTraceFrame(){
  if(!gpuTraceOn)return;
  if(gpuTraceSign!=gpuTraceLast&&gpuTraceLogged<600){++gpuTraceLogged;log("GPU_TRACE frame=%u %s d%u",frame,gpuTraceText.c_str(),gpuTraceDraws);gpuTraceLast=gpuTraceSign;}
  gpuTraceText.clear();gpuTraceSign.clear();gpuTraceDraws=0;
}
// pilot.ini gpu_trace_frames=a,b,...: every gather, flush, quad and device draw of those frames, in order
static std::vector<unsigned> gpuTraceFrames;static bool gpuTraceNow(){return !gpuTraceFrames.empty()&&std::find(gpuTraceFrames.begin(),gpuTraceFrames.end(),frame)!=gpuTraceFrames.end();}
static void gpuTraceDetail(IDirect3DDevice9* d,const char* kind,D3DPRIMITIVETYPE type,UINT count,const void* data,UINT stride){
  if(!gpuTraceNow())return;DWORD z=0,alpha=0,src=0,dst=0,fvf=0;d->GetRenderState(D3DRS_ZENABLE,&z);d->GetRenderState(D3DRS_ALPHABLENDENABLE,&alpha);d->GetRenderState(D3DRS_SRCBLEND,&src);d->GetRenderState(D3DRS_DESTBLEND,&dst);d->GetFVF(&fvf);
  IDirect3DBaseTexture9* texture=nullptr;d->GetTexture(0,&texture);if(texture)texture->Release();
  char extra[160]="";if(data&&stride>=20&&(fvf&D3DFVF_POSITION_MASK)==D3DFVF_XYZRHW){const float* v=static_cast<const float*>(data);float l=v[0],t=v[1],r=v[0],b=v[1];const unsigned n=type==D3DPT_TRIANGLESTRIP||type==D3DPT_TRIANGLEFAN?count+2:count*3;
    for(unsigned i=1;i<n&&i<4096;++i){const float* w=reinterpret_cast<const float*>(static_cast<const uint8_t*>(data)+i*stride);l=(std::min)(l,w[0]);r=(std::max)(r,w[0]);t=(std::min)(t,w[1]);b=(std::max)(b,w[1]);}
    sprintf_s(extra," rect=%.0f,%.0f,%.0f,%.0f colour=%08lx",l,t,r,b,*reinterpret_cast<const DWORD*>(static_cast<const uint8_t*>(data)+16));}
  log("GPU_TRACE_EVENT draw-%s type=%u n=%u pending=%zu z=%lu alpha=%lu %lu/%lu fvf=%lx tex=%p%s",kind,unsigned(type),count,gpuPendingCount(),z,alpha,src,dst,fvf,(void*)texture,extra);
}
static void gpuTraceNote(const char* text){if(gpuTraceNow())log("GPU_TRACE_EVENT %s pending=%zu",text,gpuPendingCount());}
static void gpuTraceDraw(){if(!gpuTraceOn)return;if(gpuPendingCount())gpuTraceEvent("drawflush:"+std::to_string(gpuPendingCount()));++gpuTraceDraws;}
static void drawInfo(IDirect3DDevice9* d,const char* kind,UINT count){
  if(!trace||!boundHash[0]||!drawnHashes.insert(boundHash[0]).second)return;
  DWORD z=0,zb=0,alpha=0,src=0,dst=0;d->GetRenderState(D3DRS_ZENABLE,&z);d->GetRenderState(D3DRS_ZWRITEENABLE,&zb);d->GetRenderState(D3DRS_ALPHABLENDENABLE,&alpha);d->GetRenderState(D3DRS_SRCBLEND,&src);d->GetRenderState(D3DRS_DESTBLEND,&dst);
  log("DRAW %s hash=%016llx count=%u z=%lu zw=%lu alpha=%lu src=%lu dst=%lu replaced=%u",kind,boundHash[0],count,z,zb,alpha,src,dst,boundReplaced[0]);
}
static HRESULT STDMETHODCALLTYPE drawPrimitive(IDirect3DDevice9* d,D3DPRIMITIVETYPE t,UINT s,UINT n){std::lock_guard<std::recursive_mutex> guard(mutex);gpuTraceDetail(d,"primitive",t,n,nullptr,0);gpuTraceDraw();gpuFlushPending();drawInfo(d,"primitive",n);return method<HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*,D3DPRIMITIVETYPE,UINT,UINT)>(deviceObject(d),81)(d,t,s,n);}
static HRESULT STDMETHODCALLTYPE drawIndexed(IDirect3DDevice9* d,D3DPRIMITIVETYPE t,INT b,UINT m,UINT v,UINT s,UINT n){std::lock_guard<std::recursive_mutex> guard(mutex);gpuTraceDetail(d,"indexed",t,n,nullptr,0);gpuTraceDraw();gpuFlushPending();drawInfo(d,"indexed",n);return method<HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*,D3DPRIMITIVETYPE,INT,UINT,UINT,UINT,UINT)>(deviceObject(d),82)(d,t,b,m,v,s,n);}
static HRESULT STDMETHODCALLTYPE drawUP(IDirect3DDevice9* d,D3DPRIMITIVETYPE t,UINT n,const void* v,UINT stride){std::lock_guard<std::recursive_mutex> guard(mutex);gpuTraceDetail(d,"up",t,n,v,stride);gpuTraceDraw();gpuFlushPending();drawInfo(d,"up",n);return method<HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*,D3DPRIMITIVETYPE,UINT,const void*,UINT)>(deviceObject(d),83)(d,t,n,v,stride);}
static HRESULT STDMETHODCALLTYPE drawIndexedUP(IDirect3DDevice9* d,D3DPRIMITIVETYPE t,UINT m,UINT v,UINT n,const void* i,D3DFORMAT f,const void* data,UINT stride){std::lock_guard<std::recursive_mutex> guard(mutex);gpuTraceDetail(d,"indexedup",t,n,nullptr,0);gpuTraceDraw();gpuFlushPending();drawInfo(d,"indexedup",n);return method<HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*,D3DPRIMITIVETYPE,UINT,UINT,UINT,const void*,D3DFORMAT,const void*,UINT)>(deviceObject(d),84)(d,t,m,v,n,i,f,data,stride);}
static HRESULT STDMETHODCALLTYPE endSceneHook(IDirect3DDevice9* d){std::lock_guard<std::recursive_mutex> guard(mutex);gpuTraceNote("endscene");gpuFlushPending();return method<HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*)>(deviceObject(d),42)(d);}
// Anything that fills pixels or changes where they land also has to wait for the models gathered before it
static HRESULT STDMETHODCALLTYPE clearHook(IDirect3DDevice9* d,DWORD count,const D3DRECT* rects,DWORD flags,D3DCOLOR colour,float z,DWORD stencil){std::lock_guard<std::recursive_mutex> guard(mutex);
  if(gpuTraceOn&&!gpuFlushingNow()){char text[96];const D3DRECT r=count&&rects?rects[0]:D3DRECT{};sprintf_s(text,"clear:%lu,%lx,%08lx,%ld,%ld,%ld,%ld",count,flags,colour,r.x1,r.y1,r.x2,r.y2);gpuTraceEvent(text);}
  gpuFlushPending();return method<HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*,DWORD,const D3DRECT*,DWORD,D3DCOLOR,float,DWORD)>(deviceObject(d),43)(d,count,rects,flags,colour,z,stencil);}
static HRESULT STDMETHODCALLTYPE colorFillHook(IDirect3DDevice9* d,IDirect3DSurface9* surface,const RECT* r,D3DCOLOR colour){std::lock_guard<std::recursive_mutex> guard(mutex);
  if(gpuTraceOn){char text[96];const RECT q=r?*r:RECT{};sprintf_s(text,"fill:%08lx,%ld,%ld,%ld,%ld",colour,q.left,q.top,q.right,q.bottom);gpuTraceEvent(text);}
  gpuFlushPending();return method<HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*,IDirect3DSurface9*,const RECT*,D3DCOLOR)>(deviceObject(d),35)(d,surface,r,colour);}
static HRESULT STDMETHODCALLTYPE stretchRectHook(IDirect3DDevice9* d,IDirect3DSurface9* source,const RECT* from,IDirect3DSurface9* target,const RECT* to,D3DTEXTUREFILTERTYPE filter){std::lock_guard<std::recursive_mutex> guard(mutex);
  if(gpuTraceOn){char text[96];const RECT q=to?*to:RECT{};sprintf_s(text,"stretch:%ld,%ld,%ld,%ld",q.left,q.top,q.right,q.bottom);gpuTraceEvent(text);}
  gpuFlushPending();return method<HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*,IDirect3DSurface9*,const RECT*,IDirect3DSurface9*,const RECT*,D3DTEXTUREFILTERTYPE)>(deviceObject(d),34)(d,source,from,target,to,filter);}
static HRESULT STDMETHODCALLTYPE setRenderTargetHook(IDirect3DDevice9* d,DWORD index,IDirect3DSurface9* surface){std::lock_guard<std::recursive_mutex> guard(mutex);
  if(gpuTraceOn)gpuTraceEvent("target");
  gpuFlushPending();return method<HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*,DWORD,IDirect3DSurface9*)>(deviceObject(d),37)(d,index,surface);}
static HRESULT STDMETHODCALLTYPE setViewportHook(IDirect3DDevice9* d,const D3DVIEWPORT9* v){std::lock_guard<std::recursive_mutex> guard(mutex);
  D3DVIEWPORT9 now{};if(v&&SUCCEEDED(d->GetViewport(&now))&&(now.X!=v->X||now.Y!=v->Y||now.Width!=v->Width||now.Height!=v->Height||now.MinZ!=v->MinZ||now.MaxZ!=v->MaxZ)){
    if(gpuTraceOn&&!gpuFlushingNow()){char text[96];sprintf_s(text,"viewport:%lu,%lu,%lu,%lu",v->X,v->Y,v->Width,v->Height);gpuTraceEvent(text);}
    gpuFlushPending();}
  return method<HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*,const D3DVIEWPORT9*)>(deviceObject(d),47)(d,v);}
static HRESULT STDMETHODCALLTYPE setRenderStateTrace(IDirect3DDevice9* d,D3DRENDERSTATETYPE state,DWORD value){std::lock_guard<std::recursive_mutex> guard(mutex);
  if(gpuTraceOn&&state==D3DRS_SCISSORTESTENABLE&&!gpuFlushingNow()){DWORD now=0;d->GetRenderState(state,&now);if(now!=value)gpuTraceEvent("scissor:"+std::to_string(value));}
  return method<HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*,D3DRENDERSTATETYPE,DWORD)>(deviceObject(d),57)(d,state,value);}
static HRESULT STDMETHODCALLTYPE setScissorTrace(IDirect3DDevice9* d,const RECT* r){std::lock_guard<std::recursive_mutex> guard(mutex);
  if(gpuTraceOn&&r&&!gpuFlushingNow()){char text[96];sprintf_s(text,"rect:%ld,%ld,%ld,%ld",r->left,r->top,r->right,r->bottom);gpuTraceEvent(text);}
  return method<HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*,const RECT*)>(deviceObject(d),75)(d,r);}
static fpsprobe::Rate probePresent{"present"};
static HRESULT STDMETHODCALLTYPE present(IDirect3DDevice9* d,const RECT* s,const RECT* t,HWND h,const RGNDATA* dirty){
  const double probeStart=fpsprobe::on?fpsprobe::Now():0;
  if(fpsprobe::on){static unsigned chains=0;if(chains<4){++chains;log("FPS_PROBE present thread=%lu caller=%s",GetCurrentThreadId(),fpsprobe::Describe(_ReturnAddress()).c_str());}fpsprobe::Mark('P',0,0,_ReturnAddress());}
  std::lock_guard<std::recursive_mutex> guard(mutex);auto hr=method<HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*,const RECT*,const RECT*,HWND,const RGNDATA*)>(deviceObject(d),17)(d,s,t,h,dirty);
  if(fpsprobe::on)probePresent.Tick(probeStart,fpsprobe::Now()-probeStart);
  // F8 is consumed only at the next native background command, once per scene frame.
  return hr;
}
using SwapPresentFn=HRESULT(STDMETHODCALLTYPE*)(IDirect3DSwapChain9*,const RECT*,const RECT*,HWND,const RGNDATA*,DWORD);
static SwapPresentFn originalSwapPresent=nullptr;
static HRESULT STDMETHODCALLTYPE swapPresent(IDirect3DSwapChain9* c,const RECT* s,const RECT* t,HWND h,const RGNDATA* dirty,DWORD flags){
  static unsigned logged=0;if(logged<4){++logged;log("FPS_PROBE swapchain present thread=%lu caller=%s flags=%lx",GetCurrentThreadId(),fpsprobe::Describe(_ReturnAddress()).c_str(),flags);}
  fpsprobe::Mark('Q',flags,0,_ReturnAddress());const double t0=fpsprobe::Now();auto hr=originalSwapPresent(c,s,t,h,dirty,flags);fpsprobe::Mark('q',unsigned((fpsprobe::Now()-t0)*1000));probePresent.Tick(t0,fpsprobe::Now()-t0);return hr;
}
static void releasePilotDeviceResources(const char* reason);
static HRESULT STDMETHODCALLTYPE resetDeviceHook(IDirect3DDevice9* d,D3DPRESENT_PARAMETERS* parameters){
  // Our textures live in D3DPOOL_DEFAULT: Reset fails while any exists. Caches reload on first use.
  std::lock_guard<std::recursive_mutex> guard(mutex);beforeModelDeviceReset();releasePilotDeviceResources("reset");
  auto hr=method<HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*,D3DPRESENT_PARAMETERS*)>(deviceObject(d),16)(d,parameters);
  afterModelDeviceReset(hr);log("DEVICE_RESET result=%08lx",hr);return hr;
}
// Fix 2026-11-02 (GPU round): this Windows' d3d9 keeps the device table on the heap and rewrites its entries after
// CreateDevice (seen: every hook gone by the first frame). The draw/EndScene/Clear hooks are what puts gathered GPU
// models down before native work (letterbox bands and black screens drawn after the last model job), so they are put
// back at every frame and every gathered job; the entry the runtime wrote becomes the original that is called.
static std::vector<std::pair<unsigned,void*>> deviceHooks;static unsigned deviceHookRestores=0;
static void keepDeviceHooks(){
  if(!gameDevice||deviceHooks.empty())return;auto it=devices.find(gameDevice);if(it==devices.end())return;auto& o=it->second;
  void** table=*(void***)gameDevice;
  if(table!=o.hooked){
    // a table of its own now: hook it too, with its own originals
    auto other=cloneTable(gameDevice,119);for(const auto& h:deviceHooks)other.hooked[h.first]=h.second;DWORD unused;VirtualProtect(other.hooked,119*sizeof(void*),other.protection,&unused);
    o=other;++deviceHookRestores;log("DEVICE_HOOKS_TABLE new device table %p",(void*)table);return;}
  bool open=false;DWORD protection=0;unsigned restored=0;
  for(const auto& h:deviceHooks){if(table[h.first]==h.second)continue;
    if(!open){open=VirtualProtect(table,119*sizeof(void*),PAGE_READWRITE,&protection)!=FALSE;if(!open)return;}
    o.original[h.first]=table[h.first];table[h.first]=h.second;++restored;}
  if(open){DWORD unused;VirtualProtect(table,119*sizeof(void*),protection,&unused);}
  if(restored&&++deviceHookRestores<=6)log("DEVICE_HOOKS_RESTORED entries=%u time=%u frame=%u",restored,deviceHookRestores,frame);
}
static void hookDevice(IDirect3DDevice9* d){
  gameDevice=d;
  auto o=cloneTable(d,119);o.hooked[16]=(void*)resetDeviceHook;if(fpsprobe::on)o.hooked[17]=(void*)present;if(trace){o.hooked[17]=(void*)present;o.hooked[23]=(void*)createTexture;o.hooked[30]=(void*)updateSurface;o.hooked[31]=(void*)updateTexture;o.hooked[36]=(void*)createOffscreen;o.hooked[65]=(void*)setTexture;
  }
  // always: the GPU path holds models back until something else draws (gpuFlushPending)
  o.hooked[42]=(void*)endSceneHook;o.hooked[81]=(void*)drawPrimitive;o.hooked[82]=(void*)drawIndexed;o.hooked[83]=(void*)drawUP;o.hooked[84]=(void*)drawIndexedUP;
  o.hooked[34]=(void*)stretchRectHook;o.hooked[35]=(void*)colorFillHook;o.hooked[37]=(void*)setRenderTargetHook;o.hooked[43]=(void*)clearHook;o.hooked[47]=(void*)setViewportHook;
  gpuTraceOn=GetPrivateProfileIntA("pilot","gpu_device_trace",0,(root+"\\pilot.ini").c_str())!=0;
  {char list[256]{};GetPrivateProfileStringA("pilot","gpu_trace_frames","",list,sizeof(list),(root+"\\pilot.ini").c_str());gpuTraceFrames.clear();for(char* p=list;*p;){char* end=nullptr;const unsigned long n=strtoul(p,&end,10);if(end==p)break;gpuTraceFrames.push_back(unsigned(n));p=*end?end+1:end;}}if(gpuTraceOn){o.hooked[57]=(void*)setRenderStateTrace;o.hooked[75]=(void*)setScissorTrace;}
  deviceHooks.clear();for(unsigned i=0;i<119;++i)if(o.hooked[i]!=o.original[i])deviceHooks.push_back({i,o.hooked[i]});
  devices.erase(d);devices.emplace(d,o);
  DWORD unused;VirtualProtect(o.hooked,119*sizeof(void*),o.protection,&unused);
  if(fpsprobe::on&&!originalSwapPresent){IDirect3DSwapChain9* chain=nullptr;
    if(SUCCEEDED(d->GetSwapChain(0,&chain))&&chain){void** vt=*(void***)chain;originalSwapPresent=(SwapPresentFn)vt[3];DWORD prot;VirtualProtect(&vt[3],4,PAGE_READWRITE,&prot);vt[3]=(void*)swapPresent;VirtualProtect(&vt[3],4,prot,&unused);chain->Release();log("FPS_PROBE swapchain present hooked");}}
  D3DPRESENT_PARAMETERS pp{};IDirect3DSwapChain9* sc=nullptr;if(SUCCEEDED(d->GetSwapChain(0,&sc))&&sc){sc->GetPresentParameters(&pp);sc->Release();log("DEVICE_PRESENT_PARAMETERS interval=%x swap=%u buffers=%u windowed=%u refresh=%u",pp.PresentationInterval,pp.SwapEffect,pp.BackBufferCount,pp.Windowed,pp.FullScreen_RefreshRateInHz);}
}

// Original GOG renderer command boundaries recovered from the exact shipped EXE.
// Background and occlusion are changed together; absent assets always call the original.
using BackgroundFn=void(__thiscall*)(void*,void*);
using SpriteFn=void(__thiscall*)(void*,void*,unsigned);
static BackgroundFn originalBackground=nullptr;
static BackgroundFn originalRawBackground=nullptr;
static SpriteFn originalSprite=nullptr;
static BackgroundFn originalTexturedQuad=nullptr;
static BackgroundFn originalTexturedTriangle=nullptr,originalGouraudQuad=nullptr;
using UploadFn=int(__thiscall*)(void*,const re3ui::Rect16*,const void*);
using ClearVramFn=int(__thiscall*)(void*,const re3ui::Rect16*,int,int,int);
using MoveVramFn=int(__thiscall*)(void*,const re3ui::Rect16*,int,int);
static UploadFn originalUpload=nullptr;
static ClearVramFn originalClearVram=nullptr;
static MoveVramFn originalMoveVram=nullptr;
static int __fastcall uploadHook(void* renderer,void*,const re3ui::Rect16* r,const void* p){auto value=originalUpload(renderer,r,p);if(uiReady&&r&&p)uiRegistry.upload(*r,p);return value;}
static int __fastcall clearVramHook(void* renderer,void*,const re3ui::Rect16* r,int red,int green,int blue){auto value=originalClearVram(renderer,r,red,green,blue);if(uiReady&&r)uiRegistry.clear(*r);return value;}
static int __fastcall moveVramHook(void* renderer,void*,const re3ui::Rect16* r,int x,int y){auto value=originalMoveVram(renderer,r,x,y);if(uiReady&&r)uiRegistry.move(*r,x,y);return value;}
static void(__thiscall* originalFrame)(void*)=nullptr;
static re3exe::Base gameBase; // gameBase+<ResidentEvil3.exe RVA> = address in the running exe (exe_map.h)
static void __cdecl inputPollHook(){
  ((void(__cdecl*)())(gameBase+0x2cad0))();
  if(!GetPrivateProfileIntA("pilot","debug_warp",0,(root+"\\pilot.ini").c_str()))return;
  glockDiagnostic();
  // Diagnostic runs skip the native movie loader through its verified flag;
  // its caller still follows the game's own failure/cleanup path. debug_movies=1 keeps the movies (movie QA).
  static const bool keepMovies=GetPrivateProfileIntA("pilot","debug_movies",0,(root+"\\pilot.ini").c_str())!=0;
  if(auto renderer=*(uint8_t**)(gameBase+0x1349c4);renderer&&!keepMovies)*(uint32_t*)(renderer+0xcc08)=1;
    static unsigned held=0,directMapped=0,remaining=0,tapOn=0,tapOff=0,tapTick=0;static bool first=false,requestedNormalCostume=false;static int requestedMenu=-1,chainProgram=-1;
  bool normalMenu=*(uint8_t*)(gameBase+0x63f680)==3&&*(uint8_t*)(gameBase+0x63f681)==0&&!(*(uint32_t*)(gameBase+0x65c880)&0x80);
  if(normalMenu)*(uint16_t*)(gameBase+0x63f696)=900;
  std::ifstream in(root+"\\pilot.input");if(in){std::string action;unsigned duration=3;in>>action>>duration;in.close();DeleteFileA((root+"\\pilot.input").c_str());
    if(action=="demoexit"||action=="newgame"||action=="loadgame"){if(*(uint8_t*)(gameBase+0x6587ee)==1)*(uint8_t*)(gameBase+0x6587ee)=2;if(action!="demoexit"){requestedMenu=action=="newgame"?0:2;requestedNormalCostume=action=="newgame";}log("INPUT %s",action.c_str());}
    else if(action=="staffroll"){chainProgram=11;log("INPUT %s",action.c_str());}
    // tapfire / tapfire-slow: aim held, fire pressed 1 frame of 2 / 2 frames of 6 (Glock MANUAL rate QA)
    else {tapOn=action=="tapfire"?1:action=="tapfire-slow"?2:0;tapOff=action=="tapfire"?1:4;tapTick=0;
      directMapped=action=="aim"||tapOn?0x100:action=="fire"?0x140:action=="action"?0x80:action=="forward"?1:action=="backward"?4:action=="turn_left"?8:action=="turn_right"?2:0;
      held=action=="confirm"?0x800:action=="left"?0x1000:action=="right"?0x4000:action=="up"?0x8000:action=="down"?0x2000:action=="cancel"?0x40:action=="skip"?0x10:0;remaining=duration>600?600:duration;first=true;log("INPUT %s raw=%x mapped=%x frames=%u",action.c_str(),held,directMapped,remaining);}}
  if(requestedMenu>=0&&normalMenu){*(uint8_t*)(gameBase+0x63f684)=(uint8_t)requestedMenu;*(uint8_t*)(gameBase+0x63f685)=(uint8_t)requestedMenu;held=0x800;remaining=3;first=true;log("INPUT menu choice=%d at verified title phase",requestedMenu);requestedMenu=-1;}
    // Diagnostic new-game route only: the unlocked costume menu maps cursor 7
    // to native costume 0 via 0x5114c4. Production input never enters this hook.
    if(requestedNormalCostume&&*(uint8_t*)(gameBase+0x63f680)==3&&*(uint8_t*)(gameBase+0x63f681)==0x0c){
      *(uint8_t*)(gameBase+0x63f684)=7;*(uint8_t*)(gameBase+0x63f685)=7;
      held=0x800;directMapped=0;remaining=3;first=true;requestedNormalCostume=false;log("INPUT native Normal costume cursor=7");}
  if(remaining&&(held||directMapped)){unsigned mapped=directMapped;
    if(tapOn){const unsigned phase=tapTick++%(tapOn+tapOff);if(phase<tapOn)mapped|=0x40;if(phase==0)*(uint32_t*)(gameBase+0x65c868)|=0x40;}
    auto table=(uint16_t*)(gameBase+0x117dac+*(uint8_t*)(gameBase+0x6628bd)*32);for(int i=0;i<16;i++)if(held&table[i])mapped|=1u<<i;if(held==0x800)mapped|=0x1000;
    *(uint32_t*)(gameBase+0x65c858)|=held;*(uint32_t*)(gameBase+0x65c864)|=mapped;
    if(first){*(uint32_t*)(gameBase+0x65c85c)|=held;*(uint32_t*)(gameBase+0x65c860)|=held;*(uint32_t*)(gameBase+0x65c868)|=mapped;first=false;}remaining--;}
  // Diagnostic staff roll: chain the current game task to overlay program 11 (staff_r, 0x440C30: credits over the
  // STR_BG.DAT slides) through the game's own task chain 0x42AF60. Last statement, no lock held: the task restarts
  // at the program entry and this frame is never returned to.
  if(chainProgram>=0){const int program=chainProgram;chainProgram=-1;
    static const uint8_t chainCode[]={0x8b,0x44,0x24,0x04,0x56,0x8d,0x34,0x40,0xc1,0xe6,0x02,0xff,0x96};
    if(memcmp((void*)(gameBase+0x2af60),chainCode,sizeof(chainCode))==0&&*(uint32_t*)(gameBase+0x117974+program*12)==gameBase+0x40c30){
      log("DIAGNOSTIC_CHAIN program=%d",program);((void(__cdecl*)(int))(gameBase+0x2af60))(program);}
    else log("DIAGNOSTIC_CHAIN program=%d refused: chain code or program table differs",program);}
}
struct Scene {
  IDirect3DTexture9* background=nullptr;
  IDirect3DTexture9* mask=nullptr;
  // Composite scenes own their canonical resources; they never borrow a cache
  // entry that could be evicted while loading the variant.
  IDirect3DTexture9* baseBackground=nullptr;
  IDirect3DTexture9* stateAlpha=nullptr;
  UINT atlasWidth=0,atlasHeight=0;
  bool missing=false,composite=false;
  unsigned lastUsed=0;
  size_t bytes=0;
  float uv[9]={1,0,0,0,1,0,0,0,1};
  float baseUv[9]={1,0,0,0,1,0,0,0,1};
};
static std::unordered_map<std::string,Scene> scenes;
static IDirect3DPixelShader9* backgroundShader=nullptr;
static IDirect3DDevice9* backgroundShaderDevice=nullptr;
static bool backgroundShaderAttempted=false;
static void releaseScene(Scene& scene){
  for(auto slot:{&scene.background,&scene.mask,&scene.baseBackground,&scene.stateAlpha}){
    if(*slot)(*slot)->Release();*slot=nullptr;
  }
  scene.bytes=0;
}
static bool ensureBackgroundShader(){
  if(backgroundShaderDevice!=gameDevice){
    for(auto& entry:scenes)releaseScene(entry.second);
    scenes.clear();ReleaseBackgroundShader(backgroundShader);
    backgroundShaderDevice=gameDevice;backgroundShaderAttempted=false;
  }
  if(!gameDevice)return false;
  if(!backgroundShaderAttempted){
    backgroundShaderAttempted=true;HRESULT hr=E_FAIL;
    backgroundShader=CreateBackgroundShader(gameDevice,&hr);
    log("BACKGROUND_SHADER result=%08lx ready=%u",hr,backgroundShader!=nullptr);
  }
  return backgroundShader!=nullptr;
}
static bool readSceneMatrix(const std::string& path,float* matrix){
  std::ifstream input(path);
  for(unsigned i=0;i<9;i++)if(!(input>>matrix[i])||!std::isfinite(matrix[i]))return false;
  std::string extra;if(input>>extra)return false;
  // A valid homography must not cross its projective horizon in the viewport.
  const float denominators[]={matrix[8],matrix[6]+matrix[8],matrix[7]+matrix[8],matrix[6]+matrix[7]+matrix[8]};
  for(float q:denominators)if(!std::isfinite(q)||std::abs(q)<1.e-6f||std::signbit(q)!=std::signbit(denominators[0]))return false;
  return true;
}
static std::string currentScene;
static bool nativeFrameRemastered=false;
static unsigned maskDraws=0;
static unsigned maskSeenTotal=0,maskTableTotal=0,maskDrawnTotal=0;   // fps60 cut probe: mask sprites seen / found in the room table / drawn HD
static bool captureRequested=false;
static std::string captureTag;
static void diagnosticControl();
static bool diagnosticPaused=false,diagnosticPausePrior=false,diagnosticFreezeDario=false;
static void setDiagnosticPause(bool paused){
  if(!gameBase||!GetPrivateProfileIntA("pilot","debug_warp",0,(root+"\\pilot.ini").c_str()))return;
  auto flags=reinterpret_cast<uint32_t*>(gameBase+0x65cbd4);
  if(paused&&!diagnosticPaused){diagnosticPausePrior=(*flags&0x40000000u)!=0;*flags|=0x40000000u;diagnosticPaused=true;log("DIAGNOSTIC_PAUSE native simulation gate enabled; rendering retained");}
  else if(!paused&&diagnosticPaused){*flags=(*flags&~0x40000000u)|(diagnosticPausePrior?0x40000000u:0);diagnosticPaused=false;log("DIAGNOSTIC_PAUSE restored original gate bit");}
}
static void captureFrame(){
  IDirect3DSurface9* target=nullptr,*copy=nullptr;D3DSURFACE_DESC desc{};auto d=gameDevice;
  if(!d||FAILED(d->GetRenderTarget(0,&target)))return;HRESULT hr=target->GetDesc(&desc);
  if(SUCCEEDED(hr))hr=d->CreateOffscreenPlainSurface(desc.Width,desc.Height,desc.Format,D3DPOOL_SYSTEMMEM,&copy,nullptr);
  if(SUCCEEDED(hr))hr=d->GetRenderTargetData(target,copy);
  if(SUCCEEDED(hr)&&(desc.Format==D3DFMT_X8R8G8B8||desc.Format==D3DFMT_A8R8G8B8)){D3DLOCKED_RECT lr;if(SUCCEEDED(copy->LockRect(&lr,nullptr,D3DLOCK_READONLY))){
    char name[160];sprintf_s(name,"\\captures\\frame-%s-%s-%u-%u.tga",captureTag.empty()?(uiDraws?"UI":currentScene.c_str()):captureTag.c_str(),(nativeFrameRemastered||uiDraws||(!captureTag.empty()&&enabled))?"remastered":"original",frame,uiFrameSerial);FILE* out=nullptr;fopen_s(&out,(root+name).c_str(),"wb");
    if(out){uint8_t hdr[18]{};hdr[2]=2;hdr[12]=(uint8_t)desc.Width;hdr[13]=(uint8_t)(desc.Width>>8);hdr[14]=(uint8_t)desc.Height;hdr[15]=(uint8_t)(desc.Height>>8);hdr[16]=32;hdr[17]=0x28;fwrite(hdr,1,18,out);for(UINT y=0;y<desc.Height;y++)fwrite((uint8_t*)lr.pBits+y*lr.Pitch,4,desc.Width,out);fclose(out);log("FRAME_CAPTURE %s %ux%u masks=%u",name,desc.Width,desc.Height,maskDraws);}copy->UnlockRect();}}
  else log("FRAME_CAPTURE_FAILED hr=%08lx format=%u",hr,desc.Format);
  if(copy)copy->Release();target->Release();captureTag.clear();
}
static void fps60OnRealFrame();
static void fps60AfterRealFrame();
static void __fastcall frameHook(void* renderer,void*){
  bool latched;if(re3bridge::GetRenderMode(latched))enabled=latched;else pollToggle();
  fps60OnRealFrame();
  uiFrameSerial++;diagnosticControl();uiDraws=0;nativeFrameRemastered=false;maskDraws=0;
  static bool previousMode=true;static unsigned transitionFrames=0;
  if(previousMode!=enabled){previousMode=enabled;transitionFrames=12;}
  if(re3bridge::Installed()&&(transitionFrames||uiFrameSerial%300==0)){
    const auto stats=re3bridge::GetStats();
    log("MODEL_FRAME build=%u render=%u desired=%u mode=%u copied=%llu drawn=%llu modeMarkers=%llu capacity=%llu mixed=%llu",re3bridge::BuildSerial(),re3bridge::RenderSerial(),desiredEnabled,enabled,stats.packetsCopied,stats.jobsDrawn,stats.modeMarkersDrawn,stats.capacityFallbacks,stats.mixedModeQueues);
    if(transitionFrames)transitionFrames--;
  }
  static fpsprobe::Rate probeFrame{"frame"};const double probeStart=fpsprobe::on?fpsprobe::Now():0;
  if(fpsprobe::on){fpsprobe::Mark('F',re3bridge::RenderSerial(),re3bridge::BuildSerial(),_ReturnAddress());
    // arm the timeline once gameplay runs at its 30 Hz with HD actors (modelPreparedActors) for 3 s
    static unsigned steady=0;steady=(probeFrame.last&&probeStart-probeFrame.last>25&&probeStart-probeFrame.last<45&&re3bridge::GetStats().actorsPrepared>8)?steady+1:0;if(steady==90)fpsprobe::Arm();}
  originalFrame(renderer);
  if(fpsprobe::on){fpsprobe::Mark('f',re3bridge::RenderSerial(),re3bridge::BuildSerial());probeFrame.Tick(probeStart,fpsprobe::Now()-probeStart);if(fpsprobe::Full())fpsprobe::DumpTimeline();if(uiFrameSerial%300==0){probeFrame.Report();probePresent.Report();fpsprobe::Flush("window300");}}
  if(nativeFrameRemastered&&frame%120==0)log("FRAME_MASKS %s replaced=%u",currentScene.c_str(),maskDraws);
  if(uiDraws&&uiFrameSerial%120==0)log("UI_FRAME replaced=%u serial=%u",uiDraws,uiFrameSerial);
  if(captureRequested){captureRequested=false;captureFrame();}nativeFrameRemastered=false;
  fps60AfterRealFrame();
}
#include "diagnostic_room_control.h"
static void diagnosticControl(){
  static unsigned next=0;if(uiFrameSerial<next)return;next=uiFrameSerial+15;
  static std::string pendingWarp;static unsigned nextWarp=0;
  std::string path=root+"\\pilot.command";std::ifstream in(path);std::string command;if(in){in>>command;
    if(command=="pose-sequence"&&GetPrivateProfileIntA("pilot","debug_warp",0,(root+"\\pilot.ini").c_str())){
      std::string label;unsigned count=240,stride=0;in>>label>>count>>stride;
      if(!label.empty()&&label.size()<=64&&label.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_")==std::string::npos){modelSequenceRequest=label;modelSequenceFrames=(std::max)(30u,(std::min)(count,600u));modelSequenceCaptureStride=stride?(std::max)(2u,stride):0;}
    }
    if(command=="room")queueDiagnosticRoom(in,!pendingWarp.empty());
    // QA (fix 2026-10-31): load one model-pack asset at the next build without its actor (texture loads under debug_va_squeeze_mb)
    if(command=="model-load"&&GetPrivateProfileIntA("pilot","debug_warp",0,(root+"\\pilot.ini").c_str())){std::string asset;in>>asset;
      if(!asset.empty()&&asset.size()<=64&&asset.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789-")==std::string::npos)modelLoadRequest=asset;}
    in.close();DeleteFileA(path.c_str());}
  if(command=="original"){desiredEnabled=false;log("COMMAND original queued");}
  if(command=="remastered"){desiredEnabled=true;log("COMMAND remastered queued");}
  if(command=="pose")modelPoseRequested=true;
  if(command=="state-light"||command=="state-full"){modelStateRestoreRequest=command=="state-light";log("COMMAND %s queued",command.c_str());}
  if(command=="gpu-on"||command=="gpu-off"){modelGpuRequestCommand=command=="gpu-on";log("COMMAND %s queued",command.c_str());}
  if(command=="gpu-pixel"||command=="gpu-vertex"){modelGpuPixelRequestCommand=command=="gpu-pixel";log("COMMAND %s queued",command.c_str());}
  if(command=="reload-models")modelReloadRequested=true;
  if(command=="pause")setDiagnosticPause(true);
  if(command=="resume")setDiagnosticPause(false);
  if(command=="arm-dario-pause"&&GetPrivateProfileIntA("pilot","debug_warp",0,(root+"\\pilot.ini").c_str()))diagnosticFreezeDario=true;
  if(command=="at-chest"||command=="at-door"||command=="at-pickup"||command=="at-typewriter"||command=="activate-dormant-pickup"||command=="prepare-warehouse-replay"||command=="equip-m92"||command=="equip-m4")nativeDiagnosticRequest=command;
  if((command=="glock-give"||command=="glock-flag"||command=="glock-money"||command=="glock-equip"||command=="glock-mode"||command=="glock-recoil-native"||command=="glock-recoil-soft"||command=="glock-m92")&&GetPrivateProfileIntA("pilot","debug_warp",0,(root+"\\pilot.ini").c_str())){glockRequest=command;log("COMMAND %s queued",command.c_str());}
  if(command=="capture")captureRequested=true;
  if(command=="capture-fps60"){fps60CaptureSequence=3;log("COMMAND capture-fps60 queued");}
  if(command=="fps60-toggle"){InterlockedIncrement(&fps60ToggleRequested);log("COMMAND fps60-toggle queued");}
  if(command=="quad-light"||command=="quad-full"){quadLightState=command=="quad-light";log("COMMAND %s (drawQuad state save)",command.c_str());}
  if(command=="warehouse"||command=="office"){
    if(diagnosticRoomPending())log("DIAGNOSTIC_WARP rejected %s: generic room warp already pending",command.c_str());
    else pendingWarp=command;
  }
  tickDiagnosticRoom();
  if(pendingWarp.empty()||frame==0)return;
  if(*(uint16_t*)(gameBase+0x661fa6)==0&&*(uint16_t*)(gameBase+0x661fa8)==(pendingWarp=="warehouse"?1:0)){log("DIAGNOSTIC_WARP complete %s",pendingWarp.c_str());pendingWarp.clear();return;}
  if(frame<nextWarp)return;nextWarp=frame+120;command=pendingWarp;
  if(command=="warehouse"&&GetPrivateProfileIntA("pilot","debug_warp",0,(root+"\\pilot.ini").c_str())){
    // R102.RDT door at0x95C, payload+14. Preserves engine transition and destination setup.
    static uint8_t door[18]={0xae,0xee,0xf0,0xf1,0xe3,0x92,0x40,0x0c,0,1,7,2,0,0,0,0,0,0};
    auto flags=(uint32_t*)(gameBase+0x661f5c);auto lock=*flags&0x100;*flags&=~0x100;((void(__cdecl*)(const uint8_t*))(gameBase+0x5c090))(door);*flags|=lock;log("DIAGNOSTIC_WARP R101 camera7 pending=%u flags=%08x",*(uint32_t*)(gameBase+0x6587a0),*flags);
  }
  if(command=="office"&&GetPrivateProfileIntA("pilot","debug_warp",0,(root+"\\pilot.ini").c_str())){static uint8_t door[18]={0x50,0xb0,0,0,0xca,0xae,0,8,0,0,0,0,1,0,0,0,0,0};((void(__cdecl*)(const uint8_t*))(gameBase+0x5c090))(door);log("DIAGNOSTIC_WARP R100 pending=%u",*(uint32_t*)(gameBase+0x6587a0));}
}
static void flushGame(void* renderer){
  auto batch=(uint8_t*)renderer+0x84;
  if(*(uint32_t*)(batch+0x8088)){gpuFlushPending();((void(__thiscall*)(void*))(gameBase+0x13090))(batch);*(uint32_t*)(batch+0x8084)=0;*(uint32_t*)(batch+0x8088)=0;}
}
static std::string sceneName(const char* filename){
  if(!filename)return {};std::string s(filename);auto slash=s.find_last_of("/\\");if(slash!=std::string::npos)s.erase(0,slash+1);auto dot=s.find_last_of('.');if(dot!=std::string::npos)s.resize(dot);for(auto& c:s)c=(char)toupper(c);return s;
}
static Scene* getScene(const std::string& id){
  if(!ensureBackgroundShader())return nullptr;
  auto& s=scenes[id];if(s.missing)return nullptr;s.lastUsed=frame;
  if(!s.background){
    // A required marker distinguishes a complete canonical composition from a
    // plain candidate. Missing metadata/resources preserve the whole old frame.
    const std::string relative="pack\\"+id+"\\",absolute=root+"\\"+relative;
    Scene candidate;candidate.lastUsed=frame;
    auto fail=[&](const char* why)->Scene*{releaseScene(candidate);s.missing=true;log("SCENE_UNAVAILABLE %s %s",id.c_str(),why);return nullptr;};
    std::ifstream state(absolute+"state.txt");std::string mode,baseId,extra;
    if(!(state>>mode)||(mode!="single"&&mode!="composite"))return fail("state marker missing/invalid");
    candidate.composite=mode=="composite";
    if(candidate.composite){
      if(!(state>>baseId)||baseId.size()!=6||baseId[0]!='R'||baseId.find_first_not_of("0123456789ABCDEF",1)!=std::string::npos)return fail("base id invalid");
    }
    if(state>>extra)return fail("unexpected state metadata");
    if(!readSceneMatrix(absolute+"uv.txt",candidate.uv))return fail("variant UV missing/invalid");
    if(candidate.composite&&!readSceneMatrix(absolute+"base_uv.txt",candidate.baseUv))return fail("base UV missing/invalid");
    for(const char* name:{"background.png","atlas.png"})if(GetFileAttributesA((absolute+name).c_str())==INVALID_FILE_ATTRIBUTES)return fail("background/atlas missing");
    if(candidate.composite)for(const char* name:{"base.png","alpha.png"})if(GetFileAttributesA((absolute+name).c_str())==INVALID_FILE_ATTRIBUTES)return fail("canonical composition resource missing");
    candidate.background=loadReplacement(gameDevice,0,relative+"background.png");
    candidate.mask=loadReplacement(gameDevice,0,relative+"atlas.png");
    if(candidate.composite){
      candidate.baseBackground=loadReplacement(gameDevice,0,relative+"base.png");
      candidate.stateAlpha=loadReplacement(gameDevice,0,relative+"alpha.png");
    }
    if(!candidate.background||!candidate.mask||(candidate.composite&&(!candidate.baseBackground||!candidate.stateAlpha)))return fail("texture load failed");
    D3DSURFACE_DESC desc{};
    if(FAILED(candidate.mask->GetLevelDesc(0,&desc)))return fail("atlas description failed");
    candidate.atlasWidth=desc.Width;candidate.atlasHeight=desc.Height;
    for(auto texture:{candidate.background,candidate.mask,candidate.baseBackground,candidate.stateAlpha})if(texture){
      if(FAILED(texture->GetLevelDesc(0,&desc)))return fail("texture description failed");
      candidate.bytes+=(size_t)desc.Width*desc.Height*4;
    }
    s=candidate;
    log("SCENE_READY %s atlas=%ux%u composition=%u base=%s bytes=%zu",id.c_str(),s.atlasWidth,s.atlasHeight,s.composite,baseId.c_str(),s.bytes);
    for(;;){size_t total=0;Scene* oldest=nullptr;for(auto& kv:scenes){total+=kv.second.bytes;if(kv.first!=id&&kv.second.background&&(!oldest||kv.second.lastUsed<oldest->lastUsed))oldest=&kv.second;}if(total<=192u*1024*1024||!oldest)break;releaseScene(*oldest);log("SCENE_CACHE_EVICT residentBytes=%zu",total);}
  }return &s;
}
struct ScreenVertex{float x,y,z,rhw;DWORD c;float u,v;};
static double quadMs=0;static unsigned quadCalls=0;   // fps60 profile: time in drawQuad / drawUiVertices
// fps60 (2026-10-27): drawQuad / drawUiVertices kept the device state in a D3DSBT_ALL state block per call, ~57 us each:
// ~100 masks per frame = 5.7 ms of every frame. QuadState saves and puts back only what they (and SetBackgroundShader)
// set. pilot.ini quad_light_state=0 returns to the full state block.
struct QuadState {
  static constexpr D3DRENDERSTATETYPE kRender[]={D3DRS_ZENABLE,D3DRS_ZWRITEENABLE,D3DRS_LIGHTING,D3DRS_CULLMODE,D3DRS_FOGENABLE,D3DRS_COLORWRITEENABLE,
    D3DRS_STENCILENABLE,D3DRS_SCISSORTESTENABLE,D3DRS_FILLMODE,D3DRS_SRGBWRITEENABLE,D3DRS_CLIPPLANEENABLE,D3DRS_SEPARATEALPHABLENDENABLE,
    D3DRS_ALPHABLENDENABLE,D3DRS_SRCBLEND,D3DRS_DESTBLEND,D3DRS_ALPHATESTENABLE,D3DRS_ALPHAFUNC,D3DRS_ALPHAREF,D3DRS_BLENDOP,D3DRS_SPECULARENABLE};
  static constexpr D3DTEXTURESTAGESTATETYPE kStage0[]={D3DTSS_TEXCOORDINDEX,D3DTSS_TEXTURETRANSFORMFLAGS,D3DTSS_COLOROP,D3DTSS_COLORARG1,D3DTSS_COLORARG2,
    D3DTSS_ALPHAOP,D3DTSS_ALPHAARG1,D3DTSS_ALPHAARG2};
  static constexpr D3DSAMPLERSTATETYPE kSampler[]={D3DSAMP_ADDRESSU,D3DSAMP_ADDRESSV,D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER,D3DSAMP_MIPFILTER,D3DSAMP_SRGBTEXTURE};
  IDirect3DStateBlock9* block=nullptr;   // quad_light_state=0
  DWORD render[sizeof(kRender)/sizeof(kRender[0])]{},stage0[sizeof(kStage0)/sizeof(kStage0[0])]{},stage1[2]{},sampler[3][sizeof(kSampler)/sizeof(kSampler[0])]{},fvf=0;
  IDirect3DBaseTexture9* texture[3]{};IDirect3DVertexShader9* vertexShader=nullptr;IDirect3DPixelShader9* pixelShader=nullptr;
  IDirect3DVertexDeclaration9* declaration=nullptr;IDirect3DVertexBuffer9* stream=nullptr;UINT streamOffset=0,streamStride=0;float constant[6][4]{};
  void release(){if(block){block->Release();block=nullptr;}for(auto& t:texture)if(t){t->Release();t=nullptr;}
    if(vertexShader)vertexShader->Release();if(pixelShader)pixelShader->Release();if(declaration)declaration->Release();if(stream)stream->Release();
    vertexShader=nullptr;pixelShader=nullptr;declaration=nullptr;stream=nullptr;}
  HRESULT capture(IDirect3DDevice9* d){
    if(!quadLightState)return d->CreateStateBlock(D3DSBT_ALL,&block);
    HRESULT hr=S_OK;
    for(size_t i=0;i<sizeof(kRender)/sizeof(kRender[0])&&SUCCEEDED(hr);++i)hr=d->GetRenderState(kRender[i],&render[i]);
    for(size_t i=0;i<sizeof(kStage0)/sizeof(kStage0[0])&&SUCCEEDED(hr);++i)hr=d->GetTextureStageState(0,kStage0[i],&stage0[i]);
    if(SUCCEEDED(hr))hr=d->GetTextureStageState(1,D3DTSS_COLOROP,&stage1[0]);if(SUCCEEDED(hr))hr=d->GetTextureStageState(1,D3DTSS_ALPHAOP,&stage1[1]);
    for(DWORD s=0;s<3&&SUCCEEDED(hr);++s){for(size_t i=0;i<sizeof(kSampler)/sizeof(kSampler[0])&&SUCCEEDED(hr);++i)hr=d->GetSamplerState(s,kSampler[i],&sampler[s][i]);if(SUCCEEDED(hr))hr=d->GetTexture(s,&texture[s]);}
    if(SUCCEEDED(hr))hr=d->GetVertexShader(&vertexShader);if(SUCCEEDED(hr))hr=d->GetPixelShader(&pixelShader);
    if(SUCCEEDED(hr))hr=d->GetFVF(&fvf);if(SUCCEEDED(hr))hr=d->GetVertexDeclaration(&declaration);
    if(SUCCEEDED(hr))hr=d->GetStreamSource(0,&stream,&streamOffset,&streamStride);
    if(SUCCEEDED(hr))hr=d->GetPixelShaderConstantF(0,&constant[0][0],6);
    if(FAILED(hr))release();
    return hr;
  }
  void restore(IDirect3DDevice9* d){
    if(block){block->Apply();release();return;}
    for(size_t i=0;i<sizeof(kRender)/sizeof(kRender[0]);++i)d->SetRenderState(kRender[i],render[i]);
    for(size_t i=0;i<sizeof(kStage0)/sizeof(kStage0[0]);++i)d->SetTextureStageState(0,kStage0[i],stage0[i]);
    d->SetTextureStageState(1,D3DTSS_COLOROP,stage1[0]);d->SetTextureStageState(1,D3DTSS_ALPHAOP,stage1[1]);
    for(DWORD s=0;s<3;++s){for(size_t i=0;i<sizeof(kSampler)/sizeof(kSampler[0]);++i)d->SetSamplerState(s,kSampler[i],sampler[s][i]);
      method<HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*,DWORD,IDirect3DBaseTexture9*)>(devices.at(d),65)(d,s,texture[s]);}
    d->SetVertexShader(vertexShader);d->SetPixelShader(pixelShader);
    if(fvf)d->SetFVF(fvf);else d->SetVertexDeclaration(declaration);
    d->SetStreamSource(0,stream,streamOffset,streamStride);d->SetPixelShaderConstantF(0,&constant[0][0],6);
    release();
  }
};
struct QuadTimer{LARGE_INTEGER t0;QuadTimer(){QueryPerformanceCounter(&t0);++quadCalls;}~QuadTimer(){static const double f=[]{LARGE_INTEGER q;QueryPerformanceFrequency(&q);return double(q.QuadPart);}();LARGE_INTEGER t1;QueryPerformanceCounter(&t1);quadMs+=double(t1.QuadPart-t0.QuadPart)*1000.0/f;}};
static bool drawQuad(IDirect3DTexture9* tex,float left,float top,float right,float bottom,float u0,float v0,float u1,float v1,bool alpha,const float* H=nullptr,IDirect3DTexture9* base=nullptr,const float* Hbase=nullptr,IDirect3DTexture9* stateAlpha=nullptr,const ScreenVertex* supplied=nullptr){
  if(H&&(!backgroundShader||backgroundShaderDevice!=gameDevice))return false;
  if((base||Hbase||stateAlpha)&&(!H||!base||!Hbase||!stateAlpha))return false;
  QuadTimer quadTimer;
  if(supplied){float l=supplied[0].x,t=supplied[0].y,r=l,b=t;for(unsigned i=1;i<4;++i){l=(std::min)(l,supplied[i].x);r=(std::max)(r,supplied[i].x);t=(std::min)(t,supplied[i].y);b=(std::max)(b,supplied[i].y);}gpuFlushRect(l,t,r,b);}else gpuFlushRect(left,top,right,bottom);
  // The GOG legacy wrapper owns a second deferred vertex queue. Drain it before
  // direct D3D9 work, otherwise those old vertices overwrite the replacement.
  auto dd=gogWrapper;
  if(dd){auto cs=(CRITICAL_SECTION*)(dd+0x181fb0);EnterCriticalSection(cs);((void(__cdecl*)())(dd+0x45b30))();LeaveCriticalSection(cs);}
  auto d=gameDevice;QuadState state;if(FAILED(state.capture(d)))return false;
  bool ownScene=dd&&!*(uint32_t*)(dd+0x181fe4);if(ownScene&&FAILED(d->BeginScene())){state.release();return false;}
  ScreenVertex v[]={{left-.5f,top-.5f,0,1,0xffffffff,u0,v0},{right-.5f,top-.5f,0,1,0xffffffff,u1,v0},{left-.5f,bottom-.5f,0,1,0xffffffff,u0,v1},{right-.5f,bottom-.5f,0,1,0xffffffff,u1,v1}};if(supplied)memcpy(v,supplied,sizeof(v));
  d->SetVertexShader(nullptr);d->SetPixelShader(nullptr);d->SetFVF(D3DFVF_XYZRHW|D3DFVF_DIFFUSE|D3DFVF_TEX1);
  d->SetRenderState(D3DRS_ZENABLE,FALSE);d->SetRenderState(D3DRS_ZWRITEENABLE,FALSE);d->SetRenderState(D3DRS_LIGHTING,FALSE);d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE);d->SetRenderState(D3DRS_FOGENABLE,FALSE);
  d->SetRenderState(D3DRS_COLORWRITEENABLE,15);d->SetRenderState(D3DRS_STENCILENABLE,FALSE);d->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE);d->SetRenderState(D3DRS_FILLMODE,D3DFILL_SOLID);d->SetRenderState(D3DRS_SRGBWRITEENABLE,FALSE);d->SetRenderState(D3DRS_CLIPPLANEENABLE,0);d->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE,FALSE);
  d->SetTextureStageState(0,D3DTSS_TEXCOORDINDEX,0);d->SetTextureStageState(0,D3DTSS_TEXTURETRANSFORMFLAGS,D3DTTFF_DISABLE);d->SetSamplerState(0,D3DSAMP_SRGBTEXTURE,FALSE);
  d->SetRenderState(D3DRS_ALPHABLENDENABLE,alpha);d->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_SRCALPHA);d->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_INVSRCALPHA);d->SetRenderState(D3DRS_ALPHATESTENABLE,alpha);d->SetRenderState(D3DRS_ALPHAFUNC,D3DCMP_GREATER);d->SetRenderState(D3DRS_ALPHAREF,0);
  d->SetTextureStageState(0,D3DTSS_COLOROP,supplied?D3DTOP_MODULATE:D3DTOP_SELECTARG1);d->SetTextureStageState(0,D3DTSS_COLORARG1,D3DTA_TEXTURE);d->SetTextureStageState(0,D3DTSS_COLORARG2,D3DTA_DIFFUSE);d->SetTextureStageState(0,D3DTSS_ALPHAOP,D3DTOP_SELECTARG1);d->SetTextureStageState(0,D3DTSS_ALPHAARG1,D3DTA_TEXTURE);d->SetTextureStageState(1,D3DTSS_COLOROP,D3DTOP_DISABLE);d->SetTextureStageState(1,D3DTSS_ALPHAOP,D3DTOP_DISABLE);
  d->SetSamplerState(0,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP);d->SetSamplerState(0,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP);d->SetSamplerState(0,D3DSAMP_MINFILTER,D3DTEXF_LINEAR);d->SetSamplerState(0,D3DSAMP_MAGFILTER,D3DTEXF_LINEAR);d->SetSamplerState(0,D3DSAMP_MIPFILTER,D3DTEXF_NONE);
  // Canonical ordinary frames and state variants use this exact per-pixel
  // projection path, avoiding a sampling change outside the state-alpha area.
  HRESULT hr=H?SetBackgroundShader(d,backgroundShader,Hbase?Hbase:H,H,base?base:tex,tex,stateAlpha?stateAlpha:tex)
    :method<HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*,DWORD,IDirect3DBaseTexture9*)>(devices.at(d),65)(d,0,tex);
  if(SUCCEEDED(hr))hr=method<HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*,D3DPRIMITIVETYPE,UINT,const void*,UINT)>(devices.at(d),83)(d,D3DPT_TRIANGLESTRIP,2,v,sizeof(ScreenVertex));if(ownScene)d->EndScene();state.restore(d);return SUCCEEDED(hr);
}
struct UiTexture {IDirect3DTexture9* texture=nullptr;unsigned lastUsed=0;size_t bytes=0;bool missing=false;};
static std::unordered_map<std::string,UiTexture> uiTextures;
static std::unordered_set<std::string> uiSeen;
static IDirect3DTexture9* uiTexture(const re3ui::Binding& b){
  static IDirect3DDevice9* textureDevice=nullptr;if(textureDevice!=gameDevice){for(auto& item:uiTextures)if(item.second.texture)item.second.texture->Release();uiTextures.clear();textureDevice=gameDevice;}
  auto& current=uiTextures[b.pngPath];if(current.missing)return nullptr;current.lastUsed=uiFrameSerial;
  if(!current.texture){current.texture=loadReplacement(gameDevice,0,b.pngPath);if(!current.texture){current.missing=true;return nullptr;}D3DSURFACE_DESC desc{};current.texture->GetLevelDesc(0,&desc);current.bytes=(size_t)desc.Width*desc.Height*4;
    for(;;){size_t total=0;UiTexture* oldest=nullptr;for(auto& entry:uiTextures){total+=entry.second.bytes;if(entry.first!=b.pngPath&&entry.second.texture&&(!oldest||entry.second.lastUsed<oldest->lastUsed))oldest=&entry.second;}if(total<=128u*1024*1024||!oldest)break;oldest->texture->Release();oldest->texture=nullptr;oldest->bytes=0;}}
  return current.texture;
}
static void __fastcall rawBackgroundHook(void* renderer,void*,void* packet){
  std::lock_guard<std::recursive_mutex> guard(mutex);
  flushGame(renderer);originalRawBackground(renderer,packet);flushGame(renderer);
  try {
  if(!enabled||!uiReady||!uiRawReady||!gameDevice||!gogWrapper)return;
  uint8_t bytes[28];SIZE_T copied=0;
  if(!ReadProcessMemory(GetCurrentProcess(),packet,bytes,sizeof(bytes),&copied)||copied!=sizeof(bytes))return;
  re3ui::Raw16Packet decoded;if(!re3ui::Raw16Registry::decodePacket(bytes,sizeof(bytes),decoded)||decoded.width!=320||decoded.height!=240)return;
  std::vector<uint8_t> source(size_t(decoded.width)*decoded.height*2);
  if(!ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(uintptr_t(decoded.sourceAddress)),source.data(),source.size(),&copied)||copied!=source.size())return;
  re3ui::Binding binding;if(!uiRawRegistry.resolve(decoded,source.data(),source.size(),binding)){
    // Evidence for screens that are not registered yet: one line per exact source.
    static std::unordered_set<uint64_t> unknown;const auto hash=re3ui::Registry::hashBytes(source.data(),source.size());
    if(unknown.size()<64&&unknown.insert(hash).second)log("UI_RAW16_UNKNOWN dimensions=%dx%d sourceFNV=%016llx",decoded.width,decoded.height,hash);
    return;
  }
  auto texture=uiTexture(binding);if(!texture)return;
  const float sx=*(float*)((uint8_t*)renderer+0x84d4),sy=*(float*)((uint8_t*)renderer+0x84d8);
  if(!std::isfinite(sx)||!std::isfinite(sy)||sx<=0||sy<=0)return;
  if(drawQuad(texture,decoded.x*sx,decoded.y*sy,(decoded.x+decoded.width)*sx,(decoded.y+decoded.height)*sy,0,0,1,1,false)){
    ++uiDraws;if(uiSeen.insert("raw16:"+binding.assetId).second)log("UI_RAW16_REPLACED %s dimensions=%dx%d sourceFNV=%016llx",binding.assetId.c_str(),decoded.width,decoded.height,re3ui::Registry::hashBytes(source.data(),source.size()));
  }
  } catch(...){static bool warned=false;if(!warned){warned=true;log("UI_RAW16 replacement preparation failed; original background retained");}}
}
// Native UI primitive families (dispatch 0x40B9C0): SPRT 0x64 -> slot148,
// FT4 0x2C -> 13C, FT3 0x24 -> 138, GT4 0x3C -> 144. GT3 (slot140) belongs
// to the Native3D model bridge and stays native.
enum UiPrimitive{UiSprite,UiFlatQuad,UiFlatTriangle,UiGouraudQuad};
static const char* uiPrimitiveName(UiPrimitive kind){return kind==UiSprite?"sprite":kind==UiFlatQuad?"FT4":kind==UiFlatTriangle?"FT3":"GT4";}
// Native batch blend classes (0x40D480/0x40CE20 -> 0x412E00): opaque and ABR0
// use SRCALPHA/INVSRCALPHA, ABR1/ABR3 SRCALPHA/ONE, ABR2 the three-pass
// destination-minus-source sequence (D3D9 reverse subtract here).
enum UiBlend{UiBlendAlpha,UiBlendAdd,UiBlendSubtract};
struct UiVertex{float x,y,z,rhw;DWORD diffuse,specular;float u,v;};
static bool drawUiVertices(IDirect3DTexture9* tex,const UiVertex* v,unsigned count,UiBlend blend,bool specular){
  QuadTimer quadTimer;
  {float l=v[0].x,t=v[0].y,r=l,b=t;for(unsigned i=1;i<count;++i){l=(std::min)(l,v[i].x);r=(std::max)(r,v[i].x);t=(std::min)(t,v[i].y);b=(std::max)(b,v[i].y);}gpuFlushRect(l,t,r,b);}
  auto dd=gogWrapper;
  if(dd){auto cs=(CRITICAL_SECTION*)(dd+0x181fb0);EnterCriticalSection(cs);((void(__cdecl*)())(dd+0x45b30))();LeaveCriticalSection(cs);}
  auto d=gameDevice;QuadState state;if(FAILED(state.capture(d)))return false;
  bool ownScene=dd&&!*(uint32_t*)(dd+0x181fe4);if(ownScene&&FAILED(d->BeginScene())){state.release();return false;}
  d->SetVertexShader(nullptr);d->SetPixelShader(nullptr);d->SetFVF(D3DFVF_XYZRHW|D3DFVF_DIFFUSE|D3DFVF_SPECULAR|D3DFVF_TEX1);
  d->SetRenderState(D3DRS_ZENABLE,FALSE);d->SetRenderState(D3DRS_ZWRITEENABLE,FALSE);d->SetRenderState(D3DRS_LIGHTING,FALSE);d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE);d->SetRenderState(D3DRS_FOGENABLE,FALSE);
  d->SetRenderState(D3DRS_COLORWRITEENABLE,15);d->SetRenderState(D3DRS_STENCILENABLE,FALSE);d->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE);d->SetRenderState(D3DRS_FILLMODE,D3DFILL_SOLID);d->SetRenderState(D3DRS_SRGBWRITEENABLE,FALSE);d->SetRenderState(D3DRS_CLIPPLANEENABLE,0);d->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE,FALSE);
  d->SetTextureStageState(0,D3DTSS_TEXCOORDINDEX,0);d->SetTextureStageState(0,D3DTSS_TEXTURETRANSFORMFLAGS,D3DTTFF_DISABLE);d->SetSamplerState(0,D3DSAMP_SRGBTEXTURE,FALSE);
  d->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE);d->SetRenderState(D3DRS_BLENDOP,blend==UiBlendSubtract?D3DBLENDOP_REVSUBTRACT:D3DBLENDOP_ADD);
  d->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_SRCALPHA);d->SetRenderState(D3DRS_DESTBLEND,blend==UiBlendAlpha?D3DBLEND_INVSRCALPHA:D3DBLEND_ONE);
  d->SetRenderState(D3DRS_ALPHATESTENABLE,TRUE);d->SetRenderState(D3DRS_ALPHAFUNC,D3DCMP_GREATER);d->SetRenderState(D3DRS_ALPHAREF,0);
  // Native overbright: texture*min(2c,255) plus specular max(2c-256,0).
  d->SetRenderState(D3DRS_SPECULARENABLE,specular?TRUE:FALSE);
  d->SetTextureStageState(0,D3DTSS_COLOROP,D3DTOP_MODULATE);d->SetTextureStageState(0,D3DTSS_COLORARG1,D3DTA_TEXTURE);d->SetTextureStageState(0,D3DTSS_COLORARG2,D3DTA_DIFFUSE);
  d->SetTextureStageState(0,D3DTSS_ALPHAOP,D3DTOP_MODULATE);d->SetTextureStageState(0,D3DTSS_ALPHAARG1,D3DTA_TEXTURE);d->SetTextureStageState(0,D3DTSS_ALPHAARG2,D3DTA_DIFFUSE);
  d->SetTextureStageState(1,D3DTSS_COLOROP,D3DTOP_DISABLE);d->SetTextureStageState(1,D3DTSS_ALPHAOP,D3DTOP_DISABLE);
  d->SetSamplerState(0,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP);d->SetSamplerState(0,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP);d->SetSamplerState(0,D3DSAMP_MINFILTER,D3DTEXF_LINEAR);d->SetSamplerState(0,D3DSAMP_MAGFILTER,D3DTEXF_LINEAR);d->SetSamplerState(0,D3DSAMP_MIPFILTER,D3DTEXF_NONE);
  HRESULT hr=method<HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*,DWORD,IDirect3DBaseTexture9*)>(devices.at(d),65)(d,0,tex);
  if(SUCCEEDED(hr))hr=method<HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*,D3DPRIMITIVETYPE,UINT,const void*,UINT)>(devices.at(d),83)(d,count==4?D3DPT_TRIANGLESTRIP:D3DPT_TRIANGLELIST,count==4?2:1,v,sizeof(UiVertex));
  if(ownScene)d->EndScene();state.restore(d);return SUCCEEDED(hr);
}
// One line per unmatched registered image/palette state; never selects art.
static void uiDiagnoseMiss(unsigned tpage,unsigned clut,float minU,float minV,float maxU,float maxV,UiPrimitive kind,uint8_t command){
  static std::unordered_set<std::string> seen;if(seen.size()>=256)return;
  re3ui::IdentityProbe probe;if(!uiRegistry.probe(tpage,clut,probe,minU,minV,maxU,maxV))return;
  char key[160];sprintf_s(key,"%s:%04x:%016llx:%u",probe.assetId.c_str(),clut,probe.palette.complete?probe.palette.hash:0ull,unsigned(probe.palette.complete));
  if(!seen.insert(key).second)return;
  unsigned known=0;for(auto value:probe.palette.known)known+=value!=0;
  log("UI_UNMATCHED %s primitive=%s tpage=%x clut=%x command=%02x paletteFNV=%016llx paletteKnown=%u/%u",probe.assetId.c_str(),uiPrimitiveName(kind),tpage,clut,command,probe.palette.complete?probe.palette.hash:0ull,known,probe.palette.entries);
}
#include "ui_probe_integration.h"
static bool drawUiPacket(void* renderer,const uint8_t* p,unsigned tpage,UiPrimitive kind){
  if(!enabled||!uiReady||!gameDevice||!gogWrapper)return false;
  const uint8_t command=p[7];const unsigned count=kind==UiFlatTriangle?3:4;
  float us[4],vs[4],xs[4],ys[4];const uint8_t* rgb[4];float sx=*(float*)((uint8_t*)renderer+0x84d4),sy=*(float*)((uint8_t*)renderer+0x84d8);
  if(kind==UiSprite){float x=*(int16_t*)(p+8),y=*(int16_t*)(p+10),w=*(int16_t*)(p+16),h=*(int16_t*)(p+18);if(w<=0||h<=0)return false;
    xs[0]=xs[2]=x;xs[1]=xs[3]=x+w;ys[0]=ys[1]=y;ys[2]=ys[3]=y+h;
    us[0]=us[2]=p[12];us[1]=us[3]=p[12]+w;vs[0]=vs[1]=p[13];vs[2]=vs[3]=p[13]+h;
    for(int i=0;i<4;i++)rgb[i]=p+4;
  }else{const unsigned stride=kind==UiGouraudQuad?12:8;
    for(unsigned i=0;i<count;i++){xs[i]=*(int16_t*)(p+8+i*stride);ys[i]=*(int16_t*)(p+10+i*stride);us[i]=p[12+i*stride];vs[i]=p[13+i*stride];rgb[i]=kind==UiGouraudQuad?p+4+i*12:p+4;}}
  float minU=us[0],maxU=us[0],minV=vs[0],maxV=vs[0];for(unsigned i=1;i<count;i++){minU=(std::min)(minU,us[i]);maxU=(std::max)(maxU,us[i]);minV=(std::min)(minV,vs[i]);maxV=(std::max)(maxV,vs[i]);}
  const unsigned clut=*(uint16_t*)(p+14);
  // Read once: this path now sees every SPRT/FT4/FT3/GT4 packet of the frame.
  static const bool probeItems=GetPrivateProfileIntA("pilot","debug_warp",0,(root+"\\pilot.ini").c_str())!=0;
  if(probeItems)diagnosticUiProbe(tpage,clut,minU,minV,maxU,maxV);
  re3ui::Binding binding;bool resolved=uiRegistry.resolve(tpage,clut,binding,minU,minV,maxU,maxV);
  // A flipped polygon (radar bracket FT4 at 45EF47: v 191..222 over a TIM at v 192) names one edge texel the GPU never
  // samples. Retry only failed polygons with the sampled texel range so such mirrors resolve; resolved draws are unchanged.
  if(!resolved&&kind!=UiSprite){float cu[4],cv[4],meanU=0,meanV=0;for(unsigned i=0;i<count;i++){meanU+=us[i]/count;meanV+=vs[i]/count;}
    for(unsigned i=0;i<count;i++){cu[i]=us[i]+(us[i]<meanU?.5f:us[i]>meanU?-.5f:0);cv[i]=vs[i]+(vs[i]<meanV?.5f:vs[i]>meanV?-.5f:0);}
    float lu=*std::min_element(cu,cu+count),hu=*std::max_element(cu,cu+count),lv=*std::min_element(cv,cv+count),hv=*std::max_element(cv,cv+count);
    lu=std::ceil(lu);hu=std::floor(hu);lv=std::ceil(lv);hv=std::floor(hv);if(hu>lu&&hv>lv)resolved=uiRegistry.resolve(tpage,clut,binding,lu,lv,hu,hv);}
  // A sprite's u+w / v+h edge is exclusive. RADAR.TIM (VRAM 660,448) lands right after Jill's portrait (STMAIN0U u 0..39 at
  // x 640) and takes the next word, so retry failed sprites with the sampled texels only.
  if(!resolved&&kind==UiSprite&&(resolved=uiRegistry.resolve(tpage,clut,binding,minU,minV,maxU-.5f,maxV-.5f))&&uiSeen.size()<1024&&uiSeen.insert("edge:"+binding.assetId).second)
    log("UI_SPRITE_EDGE_RETRY %s palette=%u tpage=%x clut=%x u=%g..%g v=%g..%g",binding.assetId.c_str(),binding.paletteIndex,tpage,clut,minU,maxU,minV,maxV);
  if(!resolved){uiDiagnoseMiss(tpage,clut,minU,minV,maxU,maxV,kind,command);return false;}
  auto texture=uiTexture(binding);if(!texture){if(uiSeen.insert("missing:"+binding.pngPath).second)log("UI_TEXTURE_MISSING %s %s",binding.assetId.c_str(),binding.pngPath.c_str());return false;}
  DWORD alpha=0xff000000;UiBlend blend=UiBlendAlpha;
  if(command&2)switch((tpage>>5)&3){case 0:alpha=0x80000000;break;case 1:blend=UiBlendAdd;break;case 2:blend=UiBlendSubtract;break;default:blend=UiBlendAdd;alpha=0x40000000;break;}
  float adjust=*(float*)((uint8_t*)renderer+0xcb00),meanU=0,meanV=0;for(unsigned i=0;i<count;i++){meanU+=us[i]/count;meanV+=vs[i]/count;}
  auto contract=[](float value,float mean){float candidate=value+(value<mean?.5f:value>mean?-.5f:0);return (value<mean&&candidate>mean)||(value>mean&&candidate<mean)?value:candidate;};
  bool specular=false;UiVertex vertices[4];
  for(unsigned i=0;i<count;i++){
    DWORD diffuse=alpha|0xffffff,overflow=0;
    if(!(command&1)){diffuse=alpha;for(int c=0;c<3;c++){const unsigned twice=rgb[i][c]*2u;diffuse|=DWORD((std::min)(twice,255u))<<(16-8*c);overflow|=DWORD(twice>=256u?twice-256u:0u)<<(16-8*c);}}
    specular|=overflow!=0;
    float u=kind==UiSprite?us[i]-.01f:contract(us[i],meanU),v=kind==UiSprite?vs[i]-.01f:contract(vs[i],meanV);
    vertices[i]={xs[i]*sx-.5f,ys[i]*sy-.5f,0,1,diffuse,overflow,(binding.offsetU+u+adjust)/binding.originalW,(binding.offsetV+v+adjust)/binding.originalH};
  }
  flushGame(renderer);bool drawn=drawUiVertices(texture,vertices,count,blend,specular);
  if(drawn){uiDraws++;char key[128];sprintf_s(key,"%s:%u:%s:%u",binding.assetId.c_str(),binding.paletteIndex,uiPrimitiveName(kind),unsigned(command&3));
    if(uiSeen.size()<1024&&uiSeen.insert(key).second)log("UI_REPLACED %s palette=%u tpage=%x clut=%x primitive=%s command=%02x",binding.assetId.c_str(),binding.paletteIndex,tpage,clut,uiPrimitiveName(kind),command);}
  return drawn;
}
static bool drawUiItemGrid(void* renderer,const uint8_t* packet,unsigned tpage){
  if(!enabled||!uiReady||((tpage>>7)&3)!=1||packet[12]!=0||packet[13]!=72||*(int16_t*)(packet+16)!=80||*(int16_t*)(packet+18)!=120)return false;
  bool identified=false;const auto clut=*(uint16_t*)(packet+14);
  for(int row=0;row<4&&!identified;row++)for(int col=0;col<2&&!identified;col++){
    re3ui::IdentityProbe probe;if(uiRegistry.probe(tpage,clut,probe,float(col*40),float(72+row*30),float(col*40+40),float(102+row*30))&&probe.assetId.compare(0,5,"ITEMI")==0)identified=true;
  }
  if(!identified)return false;
  // Native SPRT combines eight independently uploaded icons. Preserve its
  // complete original draw, then replace only exact known palette/image tiles.
  originalSprite(renderer,const_cast<uint8_t*>(packet),tpage);
  const auto x=*(int16_t*)(packet+8),y=*(int16_t*)(packet+10);
  for(int row=0;row<4;row++)for(int col=0;col<2;col++){
    uint8_t tile[24]{};memcpy(tile,packet,20);*(int16_t*)(tile+8)=int16_t(x+col*40);*(int16_t*)(tile+10)=int16_t(y+row*30);
    tile[12]=uint8_t(col*40);tile[13]=uint8_t(72+row*30);*(int16_t*)(tile+16)=40;*(int16_t*)(tile+18)=30;
    drawUiPacket(renderer,tile,tpage,UiSprite);
  }
  return true;
}
static void __fastcall texturedQuadHook(void* renderer,void*,void* packet){std::lock_guard<std::recursive_mutex> guard(mutex);auto p=(uint8_t*)packet;if(drawUiPacket(renderer,p,*(uint16_t*)(p+22),UiFlatQuad))return;originalTexturedQuad(renderer,packet);}
static void __fastcall texturedTriangleHook(void* renderer,void*,void* packet){std::lock_guard<std::recursive_mutex> guard(mutex);auto p=(uint8_t*)packet;if(drawUiPacket(renderer,p,*(uint16_t*)(p+22),UiFlatTriangle))return;originalTexturedTriangle(renderer,packet);}
static void __fastcall gouraudQuadHook(void* renderer,void*,void* packet){std::lock_guard<std::recursive_mutex> guard(mutex);auto p=(uint8_t*)packet;if(drawUiPacket(renderer,p,*(uint16_t*)(p+26),UiGouraudQuad))return;originalGouraudQuad(renderer,packet);}
static void __fastcall backgroundHook(void* renderer,void*,void* packet){
  std::lock_guard<std::recursive_mutex> guard(mutex);auto p=(uint8_t*)packet;
  frame++;keepDeviceHooks();
  auto id=sceneName(*(const char**)(p+8));if(currentScene!=id){currentScene=id;D3DVIEWPORT9 vp{};if(gameDevice)gameDevice->GetViewport(&vp);log("BACKGROUND %s xywh=%d,%d,%d,%d viewport=%ux%u",id.c_str(),*(int*)(p+12),*(int*)(p+16),*(int*)(p+20),*(int*)(p+24),vp.Width,vp.Height);}
  nativeFrameRemastered=false;
  auto scene=enabled&&gameDevice?getScene(id):nullptr;
  if(scene&&gogWrapper){
    // GOG initializes/clears its D3D9 render pass lazily on the first legacy draw.
    // Submit and flush the old background first, then overwrite it before any actor.
    flushGame(renderer);originalBackground(renderer,packet);flushGame(renderer);
    // Packet rect is 0,0,320,240 except in the vertical scroll mode (flag A61F5C&0x4000, y = var A61FC0; R3030C tower
    // pan, 423C26): there y moves and a raw16 half from the RDT (+44) is drawn above/below by rawBackgroundHook.
    D3DVIEWPORT9 vp;gameDevice->GetViewport(&vp);const float sx=vp.Width/320.f,sy=vp.Height/240.f;
    int bx=*(int*)(p+12),by=*(int*)(p+16),bw=*(int*)(p+20),bh=*(int*)(p+24);if(bw<=0||bh<=0){bx=by=0;bw=320;bh=240;}
    nativeFrameRemastered=drawQuad(scene->background,bx*sx,by*sy,(bx+bw)*sx,(by+bh)*sy,0,0,1,1,false,scene->uv,scene->baseBackground,scene->composite?scene->baseUv:nullptr,scene->stateAlpha);
    if(frame%120==0)log("REMASTER_FRAME %s viewport=%ux%u succeeded=%u",id.c_str(),vp.Width,vp.Height,nativeFrameRemastered);return;
  }
  originalBackground(renderer,packet);
}
static void __fastcall spriteHook(void* renderer,void*,void* packet,unsigned tpage){
  std::lock_guard<std::recursive_mutex> guard(mutex);auto p=(uint8_t*)packet;
  auto maskBase=*(uintptr_t*)(gameBase+0x64baa8);auto rdt=*(uint8_t**)(gameBase+0x65c898);
  // Dispatch receives a copied packet. Compare its immutable payload with the
  // actual current room's two original24-byte packets per tile, ignoring links.
  bool nativeMask=false;if(nativeFrameRemastered&&rdt&&maskBase&&tpage==0x95&&*(uint16_t*)(p+14)==0x7800){for(unsigned i=0;i<(unsigned)rdt[0]*2;i++)if(memcmp(p+4,(void*)(maskBase+i*24+8),16)==0){nativeMask=true;break;}}
  if(nativeFrameRemastered&&tpage==0x95&&*(uint16_t*)(p+14)==0x7800){++maskSeenTotal;if(nativeMask)++maskTableTotal;}
  if(nativeFrameRemastered&&nativeMask&&tpage==0x95&&*(uint16_t*)(p+14)==0x7800){
    auto scene=getScene(currentScene);if(scene){flushGame(renderer);D3DVIEWPORT9 vp;gameDevice->GetViewport(&vp);
      float sx=vp.Width/320.f,sy=vp.Height/240.f;float x=*(int16_t*)(p+8),y=*(int16_t*)(p+10),w=*(int16_t*)(p+16),h=*(int16_t*)(p+18);
      float u=p[12]*8.f,v=p[13]*8.f;
      if(drawQuad(scene->mask,x*sx,y*sy,(x+w)*sx,(y+h)*sy,u/scene->atlasWidth,v/scene->atlasHeight,(u+w*8)/scene->atlasWidth,(v+h*8)/scene->atlasHeight,true)){maskDraws++;maskDrawnTotal++;return;}
    }
  }if(drawUiItemGrid(renderer,p,tpage)||drawUiPacket(renderer,p,tpage,UiSprite))return;originalSprite(renderer,packet,tpage);
}
#include "diagnostic_actor_control.h"
#include "model_integration.h"
#include "prop_model_integration.h"
#include "extra_model_integration.h"
#include "merce_select.h"
#include "movie_rgb32_patch.h"
#include "movie_sync.h"
#include "title_costume.h"
#include "fps60_integration.h"
#include "glock18c.h"
// Movie call trace (2026-10-23, the 5 s still first frame of opn/ins01): the renderer's movie methods +10 (frame:
// Seek/RUN on start, Update when movie+20 == 0), +124 (Blt into the texture, resets movie+20, draw), +34 (start
// request), +38 (GetTime) are wrapped; the first 2000 calls after MOVIE_OPEN are logged run-length compressed at
// MOVIE_CLOSE as MOVIE_TRACE (slot, flag movie+20 before>after, return).
// Calls are counted per 100 ms bucket and identity (slot, caller, flag before>after, return; +38 returns >0 as 1).
struct MovieEvent{uint16_t bucket,slot;uint8_t before,after;int32_t ret;uint32_t caller,count;};
static MovieEvent movieEvents[2000];static unsigned movieEventCount=0;static double movieTraceStart=0;static bool movieTraceOn=false;
static double traceNow(){LARGE_INTEGER f,c;QueryPerformanceFrequency(&f);QueryPerformanceCounter(&c);return double(c.QuadPart)/double(f.QuadPart);}
static void traceMovie(uint16_t slot,void* renderer,uint8_t before,int32_t ret,void* caller){
  if(!movieTraceOn)return;auto movie=(uint8_t*)renderer+0xcb08;const double ms=(traceNow()-movieTraceStart)*1000;if(ms>=12000)return;
  MovieEvent e{uint16_t(ms/100),slot,before,(uint8_t)*(uint32_t*)(movie+0x20),slot==0x38&&ret>0?1:ret,uint32_t(uintptr_t(caller)),1};
  for(unsigned k=movieEventCount;k--&&movieEvents[k].bucket==e.bucket;){auto& o=movieEvents[k];
    if(o.slot==e.slot&&o.before==e.before&&o.after==e.after&&o.ret==e.ret&&o.caller==e.caller){++o.count;return;}}
  if(movieEventCount<2000)movieEvents[movieEventCount++]=e;
}
using MovieFrameFn=int(__thiscall*)(void*);using MovieDrawFn=int(__thiscall*)(void*,void*);using MovieTimeFn=int(__thiscall*)(void*);  // +38: _ftol(time), -1 = none
static MovieFrameFn originalMovieFrame=nullptr,originalMovieStart=nullptr;static MovieDrawFn originalMovieDraw=nullptr;static MovieTimeFn originalMovieTime=nullptr;
static int __fastcall movieFrameHook(void* r,void*){uint8_t b=(uint8_t)*(uint32_t*)((uint8_t*)r+0xcb08+0x20);int v=originalMovieFrame(r);traceMovie(0x10,r,b,v,_ReturnAddress());return v;}
static int __fastcall movieStartHook(void* r,void*){uint8_t b=(uint8_t)*(uint32_t*)((uint8_t*)r+0xcb08+0x20);int v=originalMovieStart(r);traceMovie(0x34,r,b,v,_ReturnAddress());return v;}
// The movie draw (+124: Blt of the whole movie surface into the texture, then the quad) runs once per frame: the game
// queues one per game tick, and ticks spinning while the movie time is still 0 queued ~2000 into one frame (5 s of
// 1920x960 Blts). A second one in the same frame draws the same picture again, so it is skipped.
static unsigned movieDrawSerial=~0u,movieDrawsSkipped=0;
static int __fastcall movieDrawHook(void* r,void*,void* p){uint8_t b=(uint8_t)*(uint32_t*)((uint8_t*)r+0xcb08+0x20);
  if(moviePlaying&&movieDrawSerial==uiFrameSerial){++movieDrawsSkipped;traceMovie(0x125,r,b,0,_ReturnAddress());return 0;}
  movieDrawSerial=uiFrameSerial;int v=originalMovieDraw(r,p);traceMovie(0x124,r,b,v,_ReturnAddress());return v;}
static int __fastcall movieTimeHook(void* r,void*){uint8_t b=(uint8_t)*(uint32_t*)((uint8_t*)r+0xcb08+0x20);int v=originalMovieTime(r);traceMovie(0x38,r,b,v,_ReturnAddress());return v;}
static void logMovieTrace(){  // MOVIE_TRACE <bucket start ms>: slot@caller f<before>><after> r<ret> x<count> ...
  if(!movieEventCount)return;std::string line="MOVIE_TRACE";char b[96];int bucket=-1;
  for(unsigned i=0;i<movieEventCount;i++){const auto& e=movieEvents[i];
    if(e.bucket!=bucket){bucket=e.bucket;sprintf_s(b," | %dms:",bucket*100);line+=b;}
    sprintf_s(b," %x@%x f%u>%u r%d x%u",e.slot,e.caller,e.before,e.after,e.ret,e.count);line+=b;
    if(line.size()>3000){log("%s",line.c_str());line="MOVIE_TRACE";}}
  log("%s",line.c_str());movieEventCount=0;
}
// Movies (v14): free address space around the native movie open/close (renderer vtable +30 = 40F080, thiscall
// (path), null path = close). The 2 GB non-LAA process needs ~373 MB free while an HD movie plays; if less than
// kMovieNeed + 256 MB is free, idle prop/stream caches are released before the open (level 1), and if that is not
// enough every unreferenced model asset too (level 2). Each open/close logs MOVIE_OPEN/MOVIE_CLOSE with the free
// address space (total and largest block) for QA.
using MovieFn=int(__thiscall*)(void*,const char*);
static MovieFn originalMovie=nullptr;
static constexpr size_t kMovieNeed=448u*1024*1024;
static int __fastcall movieHook(void* renderer,void*,const char* path){
  size_t free0=0,big0=0,free1=0,big1=0;std::string trim="level=0";char name[64]="-";
  try{
    addressSpace(free0,big0);free1=free0;big1=big0;
    if(path&&re3bridge::detail::Accessible(path,1)){const char* base=path;for(const char* c=path;*c&&c-path<512;++c)if(*c=='/'||*c=='\\')base=c+1;strncpy_s(name,base,_TRUNCATE);}
    if(path&&modelActive&&free0<kMovieNeed+256u*1024*1024){
      std::lock_guard<std::recursive_mutex> guard(mutex);
      trim=re3prop::trimForMovie(1);addressSpace(free1,big1);
      if(free1<kMovieNeed){trim=re3prop::trimForMovie(2);addressSpace(free1,big1);}
    }
  }catch(...){trim="failed";}
  if(path){re3movie::NoteMovieOpen(name);movieEventCount=0;movieTraceStart=traceNow();movieTraceOn=true;}
  else{const std::string sync=re3movie::SyncReport();if(!sync.empty())log("%s duplicate_draws_skipped=%u",sync.c_str(),movieDrawsSkipped);movieDrawsSkipped=0;movieTraceOn=false;
    if(GetPrivateProfileIntA("pilot","debug_movies",0,(root+"\\pilot.ini").c_str()))logMovieTrace();else movieEventCount=0;}
  const int result=originalMovie(renderer,path);
  moviePlaying=path&&result;
  try{size_t free2=0,big2=0;addressSpace(free2,big2);const double mb=1048576.0;
    if(path)log("MOVIE_OPEN %s result=%d va_free_mb=%.0f trimmed_mb=%.0f after_open_mb=%.0f largest_mb=%.0f->%.0f %s%s",name,result,free0/mb,free1/mb,free2/mb,big0/mb,big2/mb,trim.c_str(),
      free1<kMovieNeed?" LOW_ADDRESS_SPACE":"");
    else log("MOVIE_CLOSE va_free_mb=%.0f->%.0f largest_mb=%.0f",free0/mb,free2/mb,big2/mb);
  }catch(...){}
  return result;
}
static void installGameHooks(){
  gameBase=(uintptr_t)GetModuleHandleA(nullptr);auto* nt=(IMAGE_NT_HEADERS*)(gameBase+((IMAGE_DOS_HEADER*)gameBase.value)->e_lfanew);
  if(nt->OptionalHeader.SizeOfImage!=0x667000){log("Native hooks disabled: unexpected image size %x",nt->OptionalHeader.SizeOfImage);return;}
  {uint64_t fileHash=0;const auto exe=re3exe::Detect(gameBase.value,&fileHash);
    re3exe::gMissLog=[](uint32_t rva){log("EXE_MAP_MISS rva=%x not in the verified RE3_MERCE.exe map: its guard fails, feature stays native",rva);};
    log("GAME_EXE %s file_fnv64=%016llx",exe==re3exe::Exe::Main?"ResidentEvil3.exe direct":exe==re3exe::Exe::Merce?"RE3_MERCE.exe via verified address map":"unknown",(unsigned long long)fileHash);
    if(exe==re3exe::Exe::Unknown){log("Native hooks disabled: unknown game exe");return;}
    re3exe::gRunning=exe;}
  // v14 movies: X8R8G8B8 movie texture/surface instead of R5G6B5 (no 565 banding in the HD movies); before the opening
  // movie, independent of the remaster toggle. video-v14/runtime-proposal/MOVIES-RUNTIME.ru.txt 4.2.
  if(GetPrivateProfileIntA("pilot","movie_rgb32",1,(root+"\\pilot.ini").c_str()))
    log("MOVIE_RGB32 %s",re3movie::ApplyRgb32Patch(gameBase)?"applied 411BD3/411BE8 push 2 -> 8 (X8R8G8B8 movie texture)":"skipped: signature mismatch, 16-bit movie texture kept");
  else log("MOVIE_RGB32 disabled by pilot.ini movie_rgb32=0");
  // v14 movies: picture locked to the movie sound (GOG dshow.dll never catches up a late frame), movie_sync.h.
  if(GetPrivateProfileIntA("pilot","movie_sync",1,(root+"\\pilot.ini").c_str()))log("MOVIE_SYNC %s",re3movie::InstallSync([](const char* line){log("%s",line);}));
  else log("MOVIE_SYNC disabled by pilot.ini movie_sync=0");
  auto bg=(void**)(gameBase+0x10ab90),sprite=(void**)(gameBase+0x10abc0),frameSlot=(void**)(gameBase+0x10aa98);
  if(*bg!=(void*)(gameBase+0xe760)||*sprite!=(void*)(gameBase+0xd480)||*frameSlot!=(void*)(gameBase+0xb9c0)){log("Native hooks disabled: dispatch signature mismatch");return;}
  originalFrame=(void(__thiscall*)(void*))*frameSlot;DWORD fp;VirtualProtect(frameSlot,4,PAGE_READWRITE,&fp);*frameSlot=(void*)frameHook;DWORD fu;VirtualProtect(frameSlot,4,fp,&fu);
  originalBackground=(BackgroundFn)*bg;originalSprite=(SpriteFn)*sprite;DWORD old;VirtualProtect(bg,0x40,PAGE_READWRITE,&old);*bg=(void*)backgroundHook;*sprite=(void*)spriteHook;DWORD unused;VirtualProtect(bg,0x40,old,&unused);log("Native GOG background and sprite dispatch hooks installed");
  if(uiReady){
    struct UiSlot{unsigned offset,expected;void* hook;void** original;};
    UiSlot slots[]={{0x4c,0xb1b0,(void*)uploadHook,(void**)&originalUpload},{0x48,0xb110,(void*)clearVramHook,(void**)&originalClearVram},{0x54,0xb320,(void*)moveVramHook,(void**)&originalMoveVram},{0x13c,0xc6c0,(void*)texturedQuadHook,(void**)&originalTexturedQuad},{0x138,0xc3d0,(void*)texturedTriangleHook,(void**)&originalTexturedTriangle},{0x144,0xce20,(void*)gouraudQuadHook,(void**)&originalGouraudQuad}};
    for(auto& item:slots)if(*(void**)(gameBase+0x10aa78+item.offset)!=(void*)(gameBase+item.expected))uiReady=false;
    if(uiReady)for(auto& item:slots){auto slot=(void**)(gameBase+0x10aa78+item.offset);*item.original=*slot;DWORD prot;VirtualProtect(slot,4,PAGE_READWRITE,&prot);*slot=item.hook;VirtualProtect(slot,4,prot,&unused);}
    log("Native UI VRAM/SPRT/FT4/FT3/GT4 hooks installed=%u",uiReady);
  }
  if(uiReady){
    std::string error;uiRawReady=uiRawRegistry.load(root+"\\ui-raw16.txt",&error);
    auto slot=reinterpret_cast<void**>(gameBase+0x10aa78+0x11c);
    if(*slot!=reinterpret_cast<void*>(gameBase+0xe7d0))uiRawReady=false;
    if(uiRawReady){originalRawBackground=reinterpret_cast<BackgroundFn>(*slot);DWORD prot;VirtualProtect(slot,4,PAGE_READWRITE,&prot);*slot=reinterpret_cast<void*>(rawBackgroundHook);VirtualProtect(slot,4,prot,&unused);}
    log("Native UI raw16 hook installed=%u %s",uiRawReady,error.c_str());
  }
  if(GetPrivateProfileIntA("pilot","debug_warp",0,(root+"\\pilot.ini").c_str()))for(unsigned rva:{0x238c6u,0x2fb60u}){auto call=(uint8_t*)(gameBase+rva);if(call[0]==0xe8&&(uintptr_t)(call+5+*(int32_t*)(call+1))==gameBase+0x2cad0){DWORD prot;VirtualProtect(call,5,PAGE_EXECUTE_READWRITE,&prot);*(int32_t*)(call+1)=(int32_t)((uint8_t*)inputPollHook-(call+5));VirtualProtect(call,5,prot,&unused);FlushInstructionCache(GetCurrentProcess(),call,5);log("Diagnostic input hook installed at %x",rva);}}
  const unsigned gameModes[]={0x10a550,0x10a554,0x10a594,0x10a598};bool gm=true;
  for(int i=0;i<4;i++)if(*(uint32_t*)(gameBase+gameModes[i])!=(i%2?768u:1024u))gm=false;
  if(gm){for(int i=0;i<4;i++){DWORD prot;auto ptr=(void*)(gameBase+gameModes[i]);VirtualProtect(ptr,4,PAGE_READWRITE,&prot);*(uint32_t*)ptr=i%2?1920:2560;VirtualProtect(ptr,4,prot,&unused);}log("Native resolution filter/rank table 1024x768 -> 2560x1920 patched in memory");}
  auto dd=(uint8_t*)GetModuleHandleA("ddraw.dll");
  if(dd){const uint8_t sig[]={0x81,0xec,0xa8,0,0,0,0x57,0x33,0xff,0x3b};if(dd[0x45b30]==0x8b&&dd[0x45b31]==0x15&&memcmp(dd+0x45b36,sig,sizeof(sig))==0){gogWrapper=dd;log("GOG wrapper deferred-queue flush signature verified");}else log("GOG wrapper flush signature mismatch: remaster disabled");
    const unsigned offsets[]={0x1d1a7,0x1d1ae,0x21cf7,0x21cfe};bool match=true;
    for(int i=0;i<4;i++){if(*(uint32_t*)(dd+offsets[i])!=(i%2?768u:1024u))match=false;}
    if(match){for(int i=0;i<4;i++){DWORD prot;VirtualProtect(dd+offsets[i],4,PAGE_EXECUTE_READWRITE,&prot);*(uint32_t*)(dd+offsets[i])=i%2?1920:2560;VirtualProtect(dd+offsets[i],4,prot,&unused);}FlushInstructionCache(GetCurrentProcess(),dd,0x23000);log("GOG synthetic display mode 1024x768 -> 2560x1920 patched in memory");}
    else log("GOG synthetic mode patch skipped: signature mismatch");
  }
  installModels();installPropModels();installExtraModels();installMerceSelect();installTitleCostume();installFps60();glock::install();
  {auto slot=reinterpret_cast<void**>(gameBase+0x10aa78+0x30);
    if(*slot==reinterpret_cast<void*>(gameBase+0xf080)){originalMovie=reinterpret_cast<MovieFn>(*slot);DWORD prot;VirtualProtect(slot,4,PAGE_READWRITE,&prot);*slot=reinterpret_cast<void*>(movieHook);VirtualProtect(slot,4,prot,&unused);log("MOVIE_HOOK installed (renderer +30 = 40F080), trim below %zu MB free",(kMovieNeed>>20)+256);}
    else log("MOVIE_HOOK skipped: renderer vtable +30 signature mismatch");}
  {auto table=reinterpret_cast<void**>(gameBase+0x10aa78);
    if(table[0x10/4]==reinterpret_cast<void*>(gameBase+0xaf20)&&table[0x34/4]==reinterpret_cast<void*>(gameBase+0xf0f0)&&table[0x38/4]==reinterpret_cast<void*>(gameBase+0xf100)&&table[0x124/4]==reinterpret_cast<void*>(gameBase+0xe930)){
      originalMovieFrame=(MovieFrameFn)table[0x10/4];originalMovieStart=(MovieFrameFn)table[0x34/4];originalMovieTime=(MovieTimeFn)table[0x38/4];originalMovieDraw=(MovieDrawFn)table[0x124/4];
      DWORD prot;VirtualProtect(table,0x128,PAGE_READWRITE,&prot);
      table[0x10/4]=(void*)movieFrameHook;table[0x34/4]=(void*)movieStartHook;table[0x38/4]=(void*)movieTimeHook;table[0x124/4]=(void*)movieDrawHook;
      VirtualProtect(table,0x128,prot,&unused);log("MOVIE_TRACE hooks installed (renderer +10/+34/+38/+124)");}
    else log("MOVIE_TRACE hooks skipped: renderer vtable signature mismatch");}
}
// GOG wrapper rebuild after a lost device: it releases its device, then creates a new one. Anything
// still holding the old device keeps it alive in exclusive fullscreen and the new CreateDevice fails
// with D3DERR_DEVICELOST (the wrapper then gives up and the game closes), so drop it all first.
static void releasePilotDeviceResources(const char* reason){
  ULONG references=0;const size_t assets=releaseModelDeviceResources(&references);
  const size_t sceneCount=scenes.size();for(auto& entry:scenes)releaseScene(entry.second);scenes.clear();
  ReleaseBackgroundShader(backgroundShader);backgroundShaderDevice=nullptr;backgroundShaderAttempted=false;
  size_t ui=0;for(auto& item:uiTextures)if(item.second.texture){item.second.texture->Release();++ui;}uiTextures.clear();
  size_t replaced=0;for(auto& entry:textures)if(entry.second.replacement){entry.second.replacement->Release();entry.second.replacement=nullptr;++replaced;}
  fps60ReleaseDevice();
  log("DEVICE_RELEASE %s device=%p released model-assets=%zu scenes=%zu ui-textures=%zu replacements=%zu device-refs-before=%lu",reason,gameDevice,assets,sceneCount,ui,replaced,references);
}
static void releaseDeviceBoundResources(){releasePilotDeviceResources("rebuild");gameDevice=nullptr;}
static HRESULT STDMETHODCALLTYPE createDevice(IDirect3D9* d,UINT a,D3DDEVTYPE t,HWND h,DWORD flags,D3DPRESENT_PARAMETERS* p,IDirect3DDevice9** out){
  std::lock_guard<std::recursive_mutex> guard(mutex);log("CREATE_DEVICE bb=%ux%u windowed=%u flags=%08lx",p->BackBufferWidth,p->BackBufferHeight,p->Windowed,flags);
  if(gameDevice)releaseDeviceBoundResources();
  auto create=method<HRESULT(STDMETHODCALLTYPE*)(IDirect3D9*,UINT,D3DDEVTYPE,HWND,DWORD,D3DPRESENT_PARAMETERS*,IDirect3DDevice9**)>(d3ds.at(d),16);
  auto hr=create(d,a,t,h,flags,p,out);
  // The released device may leave exclusive mode a moment later; retry briefly while the game window is in front.
  for(unsigned retry=0;hr==D3DERR_DEVICELOST&&!p->Windowed&&retry<20;++retry){
    HWND fg=GetForegroundWindow();DWORD pid=0;if(fg)GetWindowThreadProcessId(fg,&pid);
    if(retry==0)log("CREATE_DEVICE_LOST foreground_is_game=%u focus_iconic=%u",pid==GetCurrentProcessId(),h&&IsIconic(h)?1u:0u);
    if(pid!=GetCurrentProcessId())break;
    Sleep(50);hr=create(d,a,t,h,flags,p,out);
  }
  if(SUCCEEDED(hr)){hookDevice(*out);HWND hwnd=p->hDeviceWindow?p->hDeviceWindow:h;if(!originalWndProc&&hwnd)originalWndProc=(WNDPROC)SetWindowLongPtrA(hwnd,GWLP_WNDPROC,(LONG_PTR)pilotWndProc);}log("CREATE_DEVICE result=%08lx",hr);return hr;
}
extern "C" IDirect3D9* WINAPI ProxyDirect3DCreate9(UINT sdk){
  std::lock_guard<std::recursive_mutex> guard(mutex);if(!realModule)init();
  auto f=(IDirect3D9*(WINAPI*)(UINT))GetProcAddress(realModule,"Direct3DCreate9");log("Direct3DCreate9 real=%p sdk=%u",f,sdk);if(!f)return nullptr;auto d=f(sdk);log("Direct3D9 object=%p",d);if(d){auto o=cloneTable(d,17);o.hooked[16]=(void*)createDevice;d3ds.emplace(d,o);DWORD unused;VirtualProtect(o.hooked,17*sizeof(void*),o.protection,&unused);}return d;
}
BOOL WINAPI DllMain(HINSTANCE h,DWORD reason,LPVOID reserved){
  if(reason==DLL_PROCESS_ATTACH){selfModule=h;DisableThreadLibraryCalls(h);}
  else if(reason==DLL_PROCESS_DETACH&&logFile){fprintf(logFile,"[%u] PROCESS_DETACH %s\n",frame,reserved?"process-exit":"library-unload");fflush(logFile);}
  return TRUE;
}

