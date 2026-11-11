#pragma once
// fps60 (2026-10-27) runtime side, see model_fps60.h. Included after model_integration.h in proxy.cpp.
//
// The game's render thread waits for its frame in the virtual vblank poll 420960 (60 Hz from timeGetTime, counter at
// +5AC; reached through the vtables at 50ACA0 and 510E10). At the vblank between two 30 Hz frames, while that thread
// is idle in the wait and the native queue already holds the next build, the in-between frame draws that queue again
// (40B9C0: native commands, HD jobs from their moved copies, flip) and puts the queue cursors back for the real frame.
// It is skipped when the vblank is seen late (the thread was busy past it), when the last passes cost too much, during
// movies, in original mode (F8), without HD models, and when the game already runs at 60 (menus).
// Camera cuts (2026-10-28): the first build of a new camera reaches the queue in parts. At the vblank before its frame
// the queue already held the HD models (projected with the new camera) but not yet the background and masks, which
// the game adds once the new background is loaded: that pass showed the old picture with new-camera models (cut probe
// R10D0B->0C, R10D01->02). No pass while the queue, the shown frame and the game logic are not on one camera
// (stage/room/camera at the start of each build), or when the queue lacks the background command (40B9C0 code 0x10)
// the shown frame had.
// F7 toggles (pilot.ini fps60, written back); "60 FPS" / "30 FPS" is drawn for 1.5 s before the flip (call 40BC84).
#pragma comment(lib,"gdi32.lib")
using VblankFn=void(__thiscall*)(void*);
static VblankFn originalVblank=nullptr;
static DWORD fps60Thread=0;            // the thread of the native frame (40B9C0)
static void* fps60Timer=nullptr;       // the vblank object that thread polls
static uint32_t fps60FrameVblank=0,fps60FrameInterval=0,fps60DoneFor=~0u,fps60RenderSerial=0;
static double fps60PassAverage=0,fps60PassLongest=0,fps60PassSum=0;
static unsigned fps60Passes=0,fps60Candidates=0,fps60Late=0,fps60Budget=0,fps60NoQueue=0,fps60CameraSkips=0,fps60TornSkips=0,fps60TornPasses=0;
static bool fps60ShownBackground=false;   // the queue of the real frame just drawn had a background command
static unsigned fps60PassFrame=0;          // backgroundHook count (frame) when the pass began
static unsigned fps60Moved=0,fps60Kept=0,fps60Failed=0;static double fps60ReprojectSum=0,fps60UploadSum=0,fps60DrawSum=0,fps60ResolveSum=0,fps60FlipSum=0;
static double fps60RealSum=0,fps60RealStart=0;static unsigned fps60RealCount=0;
static double fps60PassQuadSum=0,fps60RealQuadSum=0,fps60RealQuadStart=0;static unsigned fps60PassQuadCalls=0,fps60RealQuadCalls=0,fps60RealQuadCallsStart=0;   // the real native frame (40B9C0) for comparison
static uint64_t fps60PassCounter=0;
static double fps60NoticeUntil=0;
static IDirect3DTexture9* fps60NoticeTextures[2]{};   // 0 "30 FPS", 1 "60 FPS"
static IDirect3DDevice9* fps60NoticeDevice=nullptr;

static IDirect3DSurface9* fps60Shots[3]{};   // capture-fps60: GPU copies of real, in-between, real; written to disk after
// Cut probe (pilot.ini fps60_cut_probe=N, 2026-10-28, artifacts at camera cuts): every flip (R real, M in-between) keeps
// a half-size GPU copy and a line in a ring of 10; when the drawn background changes (camera or room) 4 more flips are
// added and the ring is written for the first N changes: captures\cut-<n>-<i>-<R|M>-<scene>.tga and FPS60_CUT lines
// (builds, the logic's camera, mask sprites seen / found in the room table / drawn HD, HD frames moved / kept).
struct Fps60CutShot{IDirect3DSurface9* surface=nullptr;char line[300]{};char kind='R';char scene[16]{};};
static Fps60CutShot fps60CutRing[10];
static unsigned fps60CutProbe=0,fps60CutHead=0,fps60CutLeft=0,fps60CutSequence=0,fps60CutPassQueue=0,fps60CutMisses=0;
static unsigned fps60CutSeen=0,fps60CutTable=0,fps60CutDrawn=0;
static std::string fps60CutScene;
static void fps60CutRelease(){for(auto& s:fps60CutRing)if(s.surface){s.surface->Release();s.surface=nullptr;}}
static void fps60ReleaseDevice(){for(auto& t:fps60NoticeTextures)if(t){t->Release();t=nullptr;}fps60NoticeDevice=nullptr;for(auto& s:fps60Shots)if(s){s->Release();s=nullptr;}fps60CutRelease();}
static void fps60WriteTga(IDirect3DSurface9* shot,const char* name){
    D3DSURFACE_DESC desc{};shot->GetDesc(&desc);
    IDirect3DSurface9* copy=nullptr;HRESULT hr=gameDevice->CreateOffscreenPlainSurface(desc.Width,desc.Height,desc.Format,D3DPOOL_SYSTEMMEM,&copy,nullptr);
    if(SUCCEEDED(hr))hr=gameDevice->GetRenderTargetData(shot,copy);D3DLOCKED_RECT lr;
    if(SUCCEEDED(hr)&&(desc.Format==D3DFMT_X8R8G8B8||desc.Format==D3DFMT_A8R8G8B8)&&SUCCEEDED(copy->LockRect(&lr,nullptr,D3DLOCK_READONLY))){
        FILE* out=nullptr;fopen_s(&out,(root+name).c_str(),"wb");
        if(out){uint8_t hdr[18]{};hdr[2]=2;hdr[12]=(uint8_t)desc.Width;hdr[13]=(uint8_t)(desc.Width>>8);hdr[14]=(uint8_t)desc.Height;hdr[15]=(uint8_t)(desc.Height>>8);hdr[16]=32;hdr[17]=0x28;fwrite(hdr,1,18,out);
            for(UINT y=0;y<desc.Height;y++)fwrite((uint8_t*)lr.pBits+y*lr.Pitch,4,desc.Width,out);fclose(out);log("FRAME_CAPTURE %s %ux%u",name,desc.Width,desc.Height);}
        copy->UnlockRect();}
    else log("FPS60_SNAPSHOT_WRITE_FAILED %s hr=%08lx format=%u",name,hr,desc.Format);
    if(copy)copy->Release();
}
static void fps60CutWrite(){
    ++fps60CutSequence;
    for(unsigned k=0;k<10;++k){auto& shot=fps60CutRing[(fps60CutHead+k)%10];if(!shot.line[0])continue;
        log("FPS60_CUT %u.%u %s",fps60CutSequence,k,shot.line);
        if(shot.surface){char name[96];sprintf_s(name,"\\captures\\cut-%u-%u-%c-%s.tga",fps60CutSequence,k,shot.kind,shot.scene);fps60WriteTga(shot.surface,name);}
        shot.line[0]=0;}
}
static void fps60CutFlip(){
    if(!fps60CutProbe||!gameDevice)return;
    auto& shot=fps60CutRing[fps60CutHead];fps60CutHead=(fps60CutHead+1)%10;
    IDirect3DSurface9* target=nullptr;
    if(SUCCEEDED(gameDevice->GetRenderTarget(0,&target))){D3DSURFACE_DESC desc{};target->GetDesc(&desc);
        if(!shot.surface&&FAILED(gameDevice->CreateRenderTarget(desc.Width/2,desc.Height/2,desc.Format,D3DMULTISAMPLE_NONE,0,FALSE,&shot.surface,nullptr)))shot.surface=nullptr;
        if(shot.surface)gameDevice->StretchRect(target,nullptr,shot.surface,nullptr,D3DTEXF_LINEAR);
        target->Release();}
    const bool cut=!fps60CutScene.empty()&&fps60CutScene!=currentScene;fps60CutScene=currentScene;
    const unsigned seen=maskSeenTotal-fps60CutSeen,table=maskTableTotal-fps60CutTable,drawn=maskDrawnTotal-fps60CutDrawn;
    fps60CutSeen=maskSeenTotal;fps60CutTable=maskTableTotal;fps60CutDrawn=maskDrawnTotal;
    shot.kind=fps60Pass?'M':'R';strncpy_s(shot.scene,currentScene.c_str(),_TRUNCATE);
    const auto& store=re3fps60::GetStore();
    sprintf_s(shot.line,"%c t=%.1f scene=%s queue=%u render=%u build=%u logic_camera=%u:%02x:%02x masks seen=%u table=%u hd=%u moved=%u kept=%u failed=%u weight=%.2f records cut=%llu matched=%llu",
        shot.kind,modelClockMs(),currentScene.c_str(),fps60Pass?fps60CutPassQueue:0u,re3bridge::RenderSerial(),re3bridge::BuildSerial(),
        unsigned(*(uint16_t*)(gameBase+0x661fa6)),unsigned(*(uint16_t*)(gameBase+0x661fa8)),unsigned(*(uint16_t*)(gameBase+0x661faa)),
        seen,table,drawn,fps60Pass?fps60PassMoved:0u,fps60Pass?fps60PassKept:0u,fps60Pass?fps60PassFailed:0u,fps60Pass?fps60PassWeight:1.f,(unsigned long long)store.cut,(unsigned long long)store.matched);
    if(seen!=table&&fps60CutMisses<300){++fps60CutMisses;log("FPS60_MASK_MISS %s",shot.line);}
    if(cut&&!fps60CutLeft)fps60CutLeft=5;
    if(fps60CutLeft&&--fps60CutLeft==0){fps60CutWrite();if(--fps60CutProbe==0)fps60CutRelease();}
}
// The frame just flipped into GPU copy `slot` (StretchRect: no readback on the game thread while the sequence runs).
static bool fps60Snapshot(unsigned slot){
    auto d=gameDevice;IDirect3DSurface9* target=nullptr;if(!d||FAILED(d->GetRenderTarget(0,&target)))return false;
    D3DSURFACE_DESC desc{};HRESULT hr=target->GetDesc(&desc);
    if(SUCCEEDED(hr)&&!fps60Shots[slot])hr=d->CreateRenderTarget(desc.Width,desc.Height,desc.Format,D3DMULTISAMPLE_NONE,0,FALSE,&fps60Shots[slot],nullptr);
    if(SUCCEEDED(hr))hr=d->StretchRect(target,nullptr,fps60Shots[slot],nullptr,D3DTEXF_NONE);
    target->Release();if(FAILED(hr))log("FPS60_SNAPSHOT_FAILED slot=%u hr=%08lx",slot,hr);return SUCCEEDED(hr);
}
static void fps60WriteShots(){
    static const char* names[3]={"a-real","b-mid","c-real"};static unsigned sequence=0;++sequence;
    for(unsigned slot=0;slot<3;++slot){auto shot=fps60Shots[slot];if(!shot)continue;D3DSURFACE_DESC desc{};shot->GetDesc(&desc);
        IDirect3DSurface9* copy=nullptr;HRESULT hr=gameDevice->CreateOffscreenPlainSurface(desc.Width,desc.Height,desc.Format,D3DPOOL_SYSTEMMEM,&copy,nullptr);
        if(SUCCEEDED(hr))hr=gameDevice->GetRenderTargetData(shot,copy);D3DLOCKED_RECT lr;
        if(SUCCEEDED(hr)&&(desc.Format==D3DFMT_X8R8G8B8||desc.Format==D3DFMT_A8R8G8B8)&&SUCCEEDED(copy->LockRect(&lr,nullptr,D3DLOCK_READONLY))){
            char name[160];sprintf_s(name,"\\captures\\frame-fps60-%u-%s-%s.tga",sequence,names[slot],currentScene.c_str());FILE* out=nullptr;fopen_s(&out,(root+name).c_str(),"wb");
            if(out){uint8_t hdr[18]{};hdr[2]=2;hdr[12]=(uint8_t)desc.Width;hdr[13]=(uint8_t)(desc.Width>>8);hdr[14]=(uint8_t)desc.Height;hdr[15]=(uint8_t)(desc.Height>>8);hdr[16]=32;hdr[17]=0x28;fwrite(hdr,1,18,out);
                for(UINT y=0;y<desc.Height;y++)fwrite((uint8_t*)lr.pBits+y*lr.Pitch,4,desc.Width,out);fclose(out);log("FRAME_CAPTURE %s %ux%u",name,desc.Width,desc.Height);}
            copy->UnlockRect();}
        else log("FPS60_SNAPSHOT_WRITE_FAILED slot=%u hr=%08lx format=%u",slot,hr,desc.Format);
        if(copy)copy->Release();}
}
// White text with luminance alpha on a 512x128 texture (GDI, then a DEFAULT pool copy like loadReplacement).
static IDirect3DTexture9* fps60TextTexture(const char* text){
    constexpr int W=512,H=128;BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=W;info.bmiHeader.biHeight=-H;
    info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
    void* bits=nullptr;HDC dc=CreateCompatibleDC(nullptr);if(!dc)return nullptr;
    HBITMAP bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&bits,nullptr,0);if(!bitmap||!bits){DeleteDC(dc);return nullptr;}
    HGDIOBJ oldBitmap=SelectObject(dc,bitmap);std::memset(bits,0,size_t(W)*H*4);
    HFONT font=CreateFontA(100,0,0,0,FW_BOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH|FF_SWISS,"Arial");
    HGDIOBJ oldFont=SelectObject(dc,font);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,RGB(255,255,255));
    RECT r{0,0,W,H};DrawTextA(dc,text,-1,&r,DT_CENTER|DT_VCENTER|DT_SINGLELINE);GdiFlush();
    std::vector<uint32_t> pixels(size_t(W)*H);
    for(size_t i=0;i<pixels.size();++i){const uint32_t c=static_cast<const uint32_t*>(bits)[i];const uint32_t v=(std::max)((std::max)(c&255,(c>>8)&255),(c>>16)&255);pixels[i]=(v<<24)|0xffffffu;}
    SelectObject(dc,oldFont);DeleteObject(font);SelectObject(dc,oldBitmap);DeleteObject(bitmap);DeleteDC(dc);
    auto d=gameDevice;IDirect3DTexture9* staging=nullptr,*out=nullptr;
    if(FAILED(method<CreateTextureFn>(devices.at(d),23)(d,W,H,1,0,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,&staging,nullptr)))return nullptr;
    auto vt=*(void***)staging;auto table=originalTables.find(vt);if(table!=originalTables.end())vt=table->second;
    using LockFn=HRESULT(STDMETHODCALLTYPE*)(IDirect3DTexture9*,UINT,D3DLOCKED_RECT*,const RECT*,DWORD);using UnlockFn=HRESULT(STDMETHODCALLTYPE*)(IDirect3DTexture9*,UINT);
    D3DLOCKED_RECT lr;HRESULT hr=((LockFn)vt[19])(staging,0,&lr,nullptr,0);
    if(SUCCEEDED(hr)){for(int y=0;y<H;++y)std::memcpy((uint8_t*)lr.pBits+size_t(y)*lr.Pitch,&pixels[size_t(y)*W],size_t(W)*4);((UnlockFn)vt[20])(staging,0);
        hr=method<CreateTextureFn>(devices.at(d),23)(d,W,H,1,0,D3DFMT_A8R8G8B8,D3DPOOL_DEFAULT,&out,nullptr);
        if(SUCCEEDED(hr))hr=method<HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*,IDirect3DBaseTexture9*,IDirect3DBaseTexture9*)>(devices.at(d),31)(d,staging,out);
        if(FAILED(hr)&&out){out->Release();out=nullptr;}}
    staging->Release();return out;
}
static void fps60DrawNotice(){
    if(!fps60NoticeUntil||modelClockMs()>fps60NoticeUntil||!gameDevice||!gogWrapper)return;
    if(fps60NoticeDevice!=gameDevice){fps60ReleaseDevice();fps60NoticeDevice=gameDevice;}
    auto& texture=fps60NoticeTextures[fps60Enabled?1:0];
    if(!texture)texture=fps60TextTexture(fps60Enabled?"60 FPS":"30 FPS");
    if(!texture)return;
    D3DVIEWPORT9 vp{};gameDevice->GetViewport(&vp);
    const float h=vp.Height*0.07f,w=h*4,x1=vp.X+vp.Width-h*0.4f,x0=x1-w,y0=vp.Y+h*0.3f+[&]{float t,b;return re3widescreen::CropWindow(t,b)?t*vp.Height:0.f;}()/* controls round 2026-11-09 widescreen: inside the crop */,y1=y0+h,shadow=(std::max)(2.f,h*0.03f);
    auto quad=[&](float dx,float dy,DWORD color){ScreenVertex v[4]={{x0+dx-.5f,y0+dy-.5f,0,1,color,0,0},{x1+dx-.5f,y0+dy-.5f,0,1,color,1,0},{x0+dx-.5f,y1+dy-.5f,0,1,color,0,1},{x1+dx-.5f,y1+dy-.5f,0,1,color,1,1}};
        drawQuad(texture,0,0,0,0,0,0,0,0,true,nullptr,nullptr,nullptr,nullptr,v);};
    quad(shadow,shadow,0xff000000u);quad(0,0,0xffffffffu);
}
using FlipFn=int(__thiscall*)(void*,int);
static double fps60FlipMs=0;
static int __fastcall fps60FlipHook(void* display,void*,int argument){
    {std::lock_guard<std::recursive_mutex> guard(mutex);keepDeviceHooks();}
    gpuFlushPending();   // the queue is drawn: the models still gathered go down before the flip
    {std::lock_guard<std::recursive_mutex> guard(mutex);gpuTraceFrame();gpuTraceNote(fps60Pass?"flip in-between":"flip");}
    {std::lock_guard<std::recursive_mutex> guard(mutex);fps60DrawNotice();fps60CutFlip();
        if(fps60Pass&&fps60ShownBackground&&frame==fps60PassFrame&&++fps60TornPasses<=20)log("FPS60_TORN_PASS queue=%u scene=%s (in-between frame drawn without the background)",fps60CutPassQueue,currentScene.c_str());}
    const double started=modelClockMs();const int result=reinterpret_cast<FlipFn>(gameBase+0x10fe0)(display,argument);fps60FlipMs+=modelClockMs()-started;
    return result;
}

static void fps60Report();
// Whether the native queue holds a background command (entries [DWORD size][packet], code = packet byte 7 & FC; 0x10
// dispatches to renderer vtable +118, backgroundHook). Same walk as re3bridge::detail::QueuedMode.
static bool fps60QueueBackground(uint8_t* renderer){
    using re3bridge::detail::Read;using re3bridge::detail::Accessible;
    if(!renderer||!Accessible(renderer,0x50))return false;
    const uintptr_t first=Read<uintptr_t>(renderer,0x40),end=Read<uintptr_t>(renderer,0x48);
    if(!first||end<first||end-first>0x100000||!Accessible(reinterpret_cast<void*>(first),end-first))return false;
    for(uintptr_t at=first;at<end;){
        if(end-at<4)break;
        const uint32_t bytes=Read<uint32_t>(reinterpret_cast<void*>(at));
        if(!bytes||(bytes&3u)||bytes>end-at-4)break;
        if(bytes>=8&&(reinterpret_cast<const uint8_t*>(at+4)[7]&0xfc)==0x10)return true;
        at+=bytes+4;
    }
    return false;
}
static bool fps60BuildCameraOf(uint32_t serial,uint32_t& key){const auto& e=fps60BuildCamera[serial&63];if(e[0]!=serial)return false;key=e[1];return true;}
// Real native frame (frame hook, before 40B9C0): its thread, vblank and rendered build; F7 is taken here.
static void fps60OnRealFrame(){
    if(InterlockedExchange(&fps60ToggleRequested,0)&1){
        fps60Enabled=!fps60Enabled;fps60NoticeUntil=modelClockMs()+1500;
        WritePrivateProfileStringA("pilot","fps60",fps60Enabled?"1":"0",(root+"\\pilot.ini").c_str());
        log("FPS60 %s (F7)",fps60Enabled?"on":"off");
    }
    static unsigned realFrames=0;if(++realFrames%300==0)fps60Report();
    fps60Thread=GetCurrentThreadId();fps60RenderSerial=re3bridge::RenderSerial();fps60ShownBackground=fps60QueueBackground(*reinterpret_cast<uint8_t**>(gameBase+0x1349c4));fps60RealStart=modelClockMs();fps60RealQuadStart=quadMs;fps60RealQuadCallsStart=quadCalls;
    if(fps60Timer){const uint32_t count=*reinterpret_cast<volatile uint32_t*>(static_cast<uint8_t*>(fps60Timer)+0x5ac);fps60FrameInterval=count-fps60FrameVblank;fps60FrameVblank=count;}
}
// After the real native frame (frame hook, after 40B9C0): the capture-fps60 sequence.
static void fps60AfterRealFrame(){
    if(fps60RealStart){fps60RealSum+=modelClockMs()-fps60RealStart;++fps60RealCount;fps60RealStart=0;fps60RealQuadSum+=quadMs-fps60RealQuadStart;fps60RealQuadCalls+=quadCalls-fps60RealQuadCallsStart;}
    if(fps60CaptureSequence==3||fps60CaptureSequence==2){if(fps60Snapshot(0))fps60CaptureSequence=2;}
    else if(fps60CaptureSequence==1){if(fps60Snapshot(2))fps60WriteShots();fps60CaptureSequence=0;}
}
static void fps60Report(){
    if(!fps60Candidates&&!fps60Passes)return;
    const auto& store=re3fps60::GetStore();
    const double n=fps60Passes?double(fps60Passes):1.0;
    log("FPS60_STATS passes=%u candidates=%u no_time=%u late_run=%u no_queue=%u camera_cut=%u no_background=%u pass_avg_ms=%.2f pass_max_ms=%.2f reproject_ms=%.2f upload_ms=%.2f hd_draw_ms=%.2f deferred_prepare_ms=%.2f flip_ms=%.2f real_frame_ms=%.2f quads_pass=%.1f/%.2fms quads_real=%.1f/%.2fms moved=%u kept=%u failed=%u recorded matched=%llu unmatched=%llu cut=%llu jumped=%llu",
        fps60Passes,fps60Candidates,fps60Late,fps60Budget,fps60NoQueue,fps60CameraSkips,fps60TornSkips,fps60PassSum/n,fps60PassLongest,fps60ReprojectSum/n,fps60UploadSum/n,fps60DrawSum/n,fps60ResolveSum/n,fps60FlipSum/n,fps60RealCount?fps60RealSum/fps60RealCount:0.0,fps60PassQuadCalls/n,fps60PassQuadSum/n,fps60RealCount?double(fps60RealQuadCalls)/fps60RealCount:0.0,fps60RealCount?fps60RealQuadSum/fps60RealCount:0.0,fps60Moved,fps60Kept,fps60Failed,
        (unsigned long long)store.matched,(unsigned long long)store.unmatched,(unsigned long long)store.cut,(unsigned long long)store.jumped);
    fps60Passes=fps60Candidates=fps60Late=fps60Budget=fps60NoQueue=fps60CameraSkips=fps60TornSkips=fps60Moved=fps60Kept=fps60Failed=fps60RealCount=0;fps60PassSum=fps60PassLongest=fps60ReprojectSum=fps60UploadSum=fps60DrawSum=fps60ResolveSum=fps60FlipSum=fps60RealSum=0;fps60PassQuadSum=fps60RealQuadSum=0;fps60PassQuadCalls=fps60RealQuadCalls=0;
}
static void fps60RunPass(){
    auto renderer=*reinterpret_cast<uint8_t**>(gameBase+0x1349c4);
    if(!renderer||!re3bridge::detail::Accessible(renderer,0x50,true)){++fps60NoQueue;return;}
    auto* queue=renderer+0x40;const uintptr_t begin=*reinterpret_cast<uintptr_t*>(queue),end=*reinterpret_cast<uintptr_t*>(queue+8),cursor=*reinterpret_cast<uintptr_t*>(queue+0xc);
    bool mode=false;uint32_t serial=0;
    // the queue must hold the next HD build (not yet drawn); empty or the one just shown: nothing to draw in between
    if(end<=begin||!re3bridge::detail::QueuedMode(renderer,mode,serial)||!mode||serial==fps60RenderSerial){++fps60NoQueue;return;}
    // camera cuts: queue, shown frame and game logic on one camera, and the background in the queue (see the top)
    uint32_t queuedCamera=0,shownCamera=0;
    const uint32_t logicCamera=(*(uint16_t*)(gameBase+0x661fa6)<<16)|(*(uint16_t*)(gameBase+0x661fa8)<<8)|*(uint16_t*)(gameBase+0x661faa);
    if(!fps60BuildCameraOf(serial,queuedCamera)||!fps60BuildCameraOf(fps60RenderSerial,shownCamera)||queuedCamera!=shownCamera||queuedCamera!=logicCamera){++fps60CameraSkips;return;}
    if(fps60ShownBackground&&!fps60QueueBackground(renderer)){++fps60TornSkips;return;}
    std::lock_guard<std::recursive_mutex> guard(mutex);
    fps60CutPassQueue=serial;
    {static unsigned logged=0;if(logged<6){++logged;log("FPS60_PASS queue_build=%u shown_build=%u latest_build=%u queue_bytes=%u weight=%.2f",serial,fps60RenderSerial,re3bridge::BuildSerial(),unsigned(end-begin),fps60PassWeight);}}
    const double started=modelClockMs(),quadStart=quadMs;const unsigned quadCallsStart=quadCalls;
    fps60Pass=true;fps60PassEpoch=(1ull<<63)|++fps60PassCounter;modelRenderer.passKey=fps60PassEpoch;re3bridge::SetReplaying(true);
    fps60PassFrame=frame;fps60PassMoved=fps60PassKept=fps60PassFailed=0;fps60PassReprojectMs=fps60PassUploadMs=fps60PassDrawMs=fps60PassResolveMs=0;fps60FlipMs=0;
    originalFrame(renderer);   // 40B9C0: draws the queue, flips, resets the cursors
    re3bridge::SetReplaying(false);modelRenderer.passKey=0;fps60Pass=false;fps60Mids.clear();
    *reinterpret_cast<uintptr_t*>(queue+8)=end;*reinterpret_cast<uintptr_t*>(queue+0xc)=cursor;
    const double spent=modelClockMs()-started;
    // The estimate is what the pass adds: deferred actors it prepares (resolve) would be prepared by the real frame
    // anyway. One slow pass (first use of buffers in a room, a texture load) must not hold it up for long.
    const double sample=(std::min)((std::max)(0.0,spent-fps60PassResolveMs),20.0);fps60PassAverage=fps60PassAverage?fps60PassAverage*0.9+sample*0.1:sample;
    ++fps60Passes;fps60PassSum+=spent;fps60PassLongest=(std::max)(fps60PassLongest,spent);fps60ReprojectSum+=fps60PassReprojectMs;
    fps60PassQuadSum+=quadMs-quadStart;fps60PassQuadCalls+=quadCalls-quadCallsStart;
    fps60UploadSum+=fps60PassUploadMs;fps60DrawSum+=fps60PassDrawMs;fps60ResolveSum+=fps60PassResolveMs;fps60FlipSum+=fps60FlipMs;
    fps60Moved+=fps60PassMoved;fps60Kept+=fps60PassKept;fps60Failed+=fps60PassFailed;
    if(fps60CaptureSequence==2&&fps60Snapshot(1))fps60CaptureSequence=1;
}
// lateness: how long after the in-between vblank the render thread got to it (busy with the real frame and the game
// logic before). The pass runs when it still ends 2.5 ms before the real frame's vblank, its pose moved further toward
// the next build by the lateness (shown later, so the motion stays even in time).
static void fps60OnVblank(uint32_t count,double lateness){
    if(!fps60Enabled||!enabled||!modelActive||modelFatal||moviePlaying||!gameDevice||!gogWrapper||fps60Pass)return;
    if(count!=fps60FrameVblank+1||fps60FrameInterval<2||fps60DoneFor==fps60FrameVblank)return;
    fps60DoneFor=fps60FrameVblank;++fps60Candidates;
    static unsigned detailed=0;if(detailed<24){++detailed;log("FPS60_CANDIDATE vblank=%u lateness_ms=%.1f pass_avg_ms=%.2f",count,lateness,fps60PassAverage);}
    const double estimate=(std::max)(fps60PassAverage,2.0);
    if(16.667-lateness-estimate<2.5){++fps60Late;fps60PassAverage*=0.97;return;}   // decays: an estimate that blocks is re-tried
    if(lateness>2)++fps60Budget;   // reported as late_run
    fps60PassWeight=float((std::min)(0.85,0.5+(std::max)(0.0,lateness)/33.333));
    fps60RunPass();
}
static void __fastcall fps60VblankHook(void* timer,void*){
    auto* counter=reinterpret_cast<volatile uint32_t*>(static_cast<uint8_t*>(timer)+0x5ac);
    const uint32_t before=*counter;originalVblank(timer);const uint32_t after=*counter;
    if(GetCurrentThreadId()!=fps60Thread)return;
    fps60Timer=timer;
    if(after==before)return;
    // vblank c was due at the game's clock base+c*16.667 = (deadline of the next one, +5B4) - 16.667
    static const auto clock=reinterpret_cast<DWORD(WINAPI*)()>(GetProcAddress(GetModuleHandleA("winmm.dll"),"timeGetTime"));
    const double lateness=clock?double(int32_t(clock()-*reinterpret_cast<volatile uint32_t*>(static_cast<uint8_t*>(timer)+0x5b4)))+16.667:0.0;
    fps60OnVblank(after,lateness);
}
static void installFps60(){
    if(!modelActive){log("FPS60 not installed: HD models are off");return;}
    const uintptr_t vblank=gameBase+0x20960;unsigned slots=0;
    for(const uint32_t rva:{0x10aca0u,0x110e10u}){auto slot=reinterpret_cast<void**>(gameBase+rva);
        if(*slot!=reinterpret_cast<void*>(vblank))continue;
        originalVblank=reinterpret_cast<VblankFn>(vblank);DWORD protection;VirtualProtect(slot,4,PAGE_READWRITE,&protection);*slot=reinterpret_cast<void*>(fps60VblankHook);VirtualProtect(slot,4,protection,&protection);++slots;}
    bool flip=false;{auto call=reinterpret_cast<uint8_t*>(gameBase+0xbc84);
        if(call[0]==0xe8&&reinterpret_cast<uintptr_t>(call+5)+*reinterpret_cast<int32_t*>(call+1)==gameBase+0x10fe0){
            DWORD protection;VirtualProtect(call,5,PAGE_EXECUTE_READWRITE,&protection);*reinterpret_cast<int32_t*>(call+1)=int32_t(reinterpret_cast<uint8_t*>(fps60FlipHook)-(call+5));
            VirtualProtect(call,5,protection,&protection);FlushInstructionCache(GetCurrentProcess(),call,5);flip=true;}}
    fps60CutProbe=GetPrivateProfileIntA("pilot","fps60_cut_probe",0,(root+"\\pilot.ini").c_str());
    log("FPS60_INSTALL vblank_slots=%u flip_hook=%u enabled=%u (F7 toggles) cut_probe=%u",slots,unsigned(flip),unsigned(fps60Enabled),fps60CutProbe);
}
