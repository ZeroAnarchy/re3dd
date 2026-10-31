#pragma once
// fps60 probe (diagnostic only, pilot.ini fps_probe=1): how the game paces its frames. IAT hooks on the timing imports of
// the exe and of the GOG ddraw wrapper record call counts, waits and the caller chains; the frame hook and Present record
// their intervals. Every 300 frames FPS_PROBE lines summarize the window. Nothing is changed in the game's behaviour.
#include <windows.h>
#include <mmsystem.h>
#include <cstdint>
#include <cstdio>
#include <string>
#include <unordered_map>
#include <mutex>
#include <intrin.h>

namespace fpsprobe {
inline bool on = false;
inline double Now(){static const double f=[]{LARGE_INTEGER q;QueryPerformanceFrequency(&q);return double(q.QuadPart);}();LARGE_INTEGER c;QueryPerformanceCounter(&c);return double(c.QuadPart)*1000.0/f;}
struct Site { unsigned count=0; double ms=0, argSum=0; DWORD thread=0; std::string chain; };
inline std::mutex lock;
inline std::unordered_map<std::string,Site> sites; // key: api + caller chain
inline void (*sink)(const char*) = nullptr;
inline std::string Describe(const void* address){
    HMODULE module=nullptr;char path[MAX_PATH],out[MAX_PATH+16];
    if(GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,(LPCSTR)address,&module)&&module&&GetModuleFileNameA(module,path,MAX_PATH)){
        const char* base=strrchr(path,'\\');sprintf_s(out,"%s+%x",base?base+1:path,(unsigned)((const uint8_t*)address-(const uint8_t*)module));
    }else sprintf_s(out,"%p",address);
    return out;
}
inline std::string Chain(unsigned skip,unsigned depth){
    void* frames[12]{};const USHORT n=CaptureStackBackTrace(skip+1,(std::min)(depth,12u),frames,nullptr);std::string s;
    for(USHORT i=0;i<n;++i){if(i)s+=" < ";s+=Describe(frames[i]);}
    return s;
}
inline void Record(const char* api,double ms,double arg,const void* caller){
    if(!on)return;char key[128];sprintf_s(key,"%s %s t%lu",api,Describe(caller).c_str(),(unsigned long)GetCurrentThreadId());std::lock_guard<std::mutex> g(lock);
    auto& s=sites[key];s.count++;s.ms+=ms;s.argSum+=arg;s.thread=GetCurrentThreadId();
}
// Timeline of one stretch of gameplay frames: build start, frame enter/exit, present, long waits (game thread).
struct Event{double t;char kind;unsigned a,b;DWORD thread;const void* where;};
inline Event events[400];inline volatile LONG eventCount=-1; // -1 = not armed
inline void Mark(char kind,unsigned a=0,unsigned b=0,const void* where=nullptr){
    if(eventCount<0)return;const LONG i=InterlockedIncrement(&eventCount)-1;if(i>=LONG(sizeof(events)/sizeof(events[0])))return;
    events[i]={Now(),kind,a,b,GetCurrentThreadId(),where};
}
inline void Arm(){if(eventCount==-1)InterlockedExchange(&eventCount,0);}
inline bool Full(){return eventCount>=LONG(sizeof(events)/sizeof(events[0]));}
inline void DumpTimeline(){
    if(!sink||eventCount<=0)return;const LONG n=(std::min)(LONG(eventCount),LONG(sizeof(events)/sizeof(events[0])));char line[256];
    for(LONG i=0;i<n;++i){const auto& e=events[i];sprintf_s(line,"FPS_TL %.3f %c a=%u b=%u t%lu %s",e.t-events[0].t,e.kind,e.a,e.b,(unsigned long)e.thread,e.where?Describe(e.where).c_str():"");sink(line);}
    eventCount=-2;
}
inline void Flush(const char* why){
    std::unordered_map<std::string,Site> copy;{std::lock_guard<std::mutex> g(lock);copy.swap(sites);}
    if(!sink)return;char line[2048];
    for(auto& kv:copy){sprintf_s(line,"FPS_PROBE_SITE %s calls=%u ms=%.2f argsum=%.0f thread=%lu %s",why,kv.second.count,kv.second.ms,kv.second.argSum,(unsigned long)kv.second.thread,kv.first.c_str());sink(line);}
}

using SleepFn=void(WINAPI*)(DWORD);using TimeFn=DWORD(WINAPI*)();using WaitFn=DWORD(WINAPI*)(HANDLE,DWORD);
using SetEventTimerFn=MMRESULT(WINAPI*)(UINT,UINT,LPTIMECALLBACK,DWORD_PTR,UINT);
inline SleepFn realSleep=nullptr;inline TimeFn realTime=nullptr;inline WaitFn realWait=nullptr;inline SetEventTimerFn realSetEvent=nullptr;
inline void WINAPI HookSleep(DWORD ms){const void* c=_ReturnAddress();const double t=Now();realSleep(ms);const double d=Now()-t;Record("Sleep",d,ms,c);if(d>1)Mark('S',unsigned(d*1000),ms,c);}
inline DWORD WINAPI HookTime(){Record("timeGetTime",0,0,_ReturnAddress());return realTime();}
inline DWORD WINAPI HookWait(HANDLE h,DWORD ms){const void* c=_ReturnAddress();const double t=Now();const DWORD r=realWait(h,ms);const double d=Now()-t;Record("WaitForSingleObject",d,ms==INFINITE?-1.0:ms,c);if(d>1)Mark('W',unsigned(d*1000),unsigned(uintptr_t(h)),c);return r;}
inline MMRESULT WINAPI HookSetEvent(UINT delay,UINT resolution,LPTIMECALLBACK cb,DWORD_PTR user,UINT flags){
    if(sink){char line[256];sprintf_s(line,"FPS_PROBE timeSetEvent delay=%u resolution=%u flags=%x callback=%s caller=%s",delay,resolution,flags,Describe((void*)cb).c_str(),Chain(1,4).c_str());sink(line);}
    return realSetEvent(delay,resolution,cb,user,flags);
}
// Patch one import of `module` (by dll and function name) to `hook`; returns the old target.
inline void* PatchImport(HMODULE module,const char* dll,const char* function,void* hook){
    if(!module)return nullptr;auto base=(uint8_t*)module;auto dos=(IMAGE_DOS_HEADER*)base;auto nt=(IMAGE_NT_HEADERS*)(base+dos->e_lfanew);
    const auto& dir=nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];if(!dir.VirtualAddress)return nullptr;
    for(auto desc=(IMAGE_IMPORT_DESCRIPTOR*)(base+dir.VirtualAddress);desc->Name;++desc){
        if(_stricmp((const char*)(base+desc->Name),dll))continue;
        auto names=(IMAGE_THUNK_DATA*)(base+(desc->OriginalFirstThunk?desc->OriginalFirstThunk:desc->FirstThunk));auto slots=(IMAGE_THUNK_DATA*)(base+desc->FirstThunk);
        for(;names->u1.AddressOfData;++names,++slots){
            if(IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal))continue;auto byName=(IMAGE_IMPORT_BY_NAME*)(base+names->u1.AddressOfData);
            if(strcmp((const char*)byName->Name,function))continue;
            void* old=(void*)slots->u1.Function;DWORD prot;VirtualProtect(&slots->u1.Function,4,PAGE_READWRITE,&prot);slots->u1.Function=(uintptr_t)hook;VirtualProtect(&slots->u1.Function,4,prot,&prot);return old;
        }
    }
    return nullptr;
}
inline void Install(void (*log)(const char*)){
    sink=log;on=true;HMODULE exe=GetModuleHandleA(nullptr),dd=GetModuleHandleA("ddraw.dll");
    realSleep=(SleepFn)GetProcAddress(GetModuleHandleA("kernel32.dll"),"Sleep");realTime=(TimeFn)GetProcAddress(GetModuleHandleA("winmm.dll"),"timeGetTime");
    realWait=(WaitFn)GetProcAddress(GetModuleHandleA("kernel32.dll"),"WaitForSingleObject");realSetEvent=(SetEventTimerFn)GetProcAddress(GetModuleHandleA("winmm.dll"),"timeSetEvent");
    unsigned n=0;
    for(HMODULE m:{exe,dd}){
        n+=PatchImport(m,"KERNEL32.dll","Sleep",(void*)HookSleep)!=nullptr;n+=PatchImport(m,"WINMM.dll","timeGetTime",(void*)HookTime)!=nullptr;
        n+=PatchImport(m,"KERNEL32.dll","WaitForSingleObject",(void*)HookWait)!=nullptr;n+=PatchImport(m,"WINMM.dll","timeSetEvent",(void*)HookSetEvent)!=nullptr;
    }
    char line[96];sprintf_s(line,"FPS_PROBE installed imports=%u ddraw=%p",n,(void*)dd);log(line);
}
// Interval statistics for one event stream (frame hook, Present, ...).
struct Rate {
    const char* name;double last=0,sum=0,mx=0,mn=1e9,busy=0;unsigned n=0;
    void Tick(double now,double busyMs){if(last){const double d=now-last;sum+=d;mx=(std::max)(mx,d);mn=(std::min)(mn,d);++n;}busy+=busyMs;last=now;}
    void Report(){if(!n||!sink)return;char line[256];sprintf_s(line,"FPS_PROBE_RATE %s n=%u mean_ms=%.2f min_ms=%.2f max_ms=%.2f busy_ms_per=%.2f",name,n,sum/n,mn,mx,busy/n);sink(line);sum=mx=busy=0;mn=1e9;n=0;}
};
} // namespace fpsprobe
