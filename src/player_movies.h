#pragma once
// player_movies.h - controls round 2026-11-09, item 5: the player's own movie files and their picture geometry.
// Report: runtime/staging/ctl-2026-11-09/movies/REPORT.ru.txt; player instructions README-movies.en.txt / .ru.txt there.
//
// 1. Folder. The game opens <bio3.ini Movie=>\<name>.dat through the renderer's movie open (vtable +30 = 40F080, wrapped
//    by proxy.cpp movieHook). If <game folder>\3DD-Movies\<name>.<ext> exists (pilot.ini player_movies=1, the default;
//    player_movies_dir= another folder, relative to the game folder or absolute), that file is opened instead; bio3.ini
//    and the game's own movies are not touched. Extensions in this order: dat mp4 mkv avi webm mov m4v mpg mpeg ts m2ts
//    wmv. GOG dshow.dll is FFmpeg (libavformat/libavcodec): offline on the real dshow.dll (qa/ctl-2026-11-09/movies,
//    pm_test open / crash_probe) H.264 (8 and 10 bit), HEVC 8/10 bit, VP8, VP9, MPEG-1/2, MPEG-4 ASP (XviD), WMV2, MJPEG,
//    ProRes in MP4/M4V/MKV/WebM/AVI/MOV/MPEG-PS/MPEG-TS/M2TS/WMV with AAC, AC3, E-AC3, MP2, MP3, FLAC, PCM, Opus, Vorbis,
//    WMA2 (44.1/48 kHz, stereo or 5.1) and files without sound decode; AV1 does not (one frame, then end of stream).
//    Two things break the native player and are refused here (the original movie is played instead, one log line says
//    why): a frame the movie texture cannot hold (larger than 2048x2048, the GOG ddraw limit: the game falls back to a
//    256x256 texture) and MONO sound (dshow's resampler is set up for stereo; once the movie runs its audio thread
//    crashes the game: dshow.dll+BC87F, player+0x164 = channels; opening alone is safe - crash_probe -3000). The checks
//    run right after the native open, before the game starts the movie. "roopne" (the title loop, the only loop movie
//    the exe opens) also takes roop_nee.*. pilot.ini player_movies is read again at every movie open, SetEnabled() is
//    the live setter for the F5 menu (both take effect at the next movie).
//
// 2. Geometry (pilot.ini movie_fit=1, the default; read again at every movie open). The game draws the movie as one
//    textured quad on the 2:1 band of its 4:3 screen (40E930: band from the movie packet, rows 40..200 of 240; UVs 411FA0
//    = the whole frame) - any frame is squeezed into it. For a frame that is not 2:1 the active picture is measured once
//    per file: Media Foundation (a worker thread, software decoding) decodes the key frames at 16 points of the file
//    (plus every 8th frame from a third of the file on when they are mostly black) and the rows/columns that are not
//    black in any of them give the picture (bars symmetrised; a picture within 5 % of 2:1 is snapped to 2:1,
//    the RE3 picture; the pixel aspect of the file is applied). The scan runs BEFORE the native open (no second open,
//    no extra address space while a movie is loaded); files that need one are scanned in the background at start
//    (the player folder, and game-folder movies whose size is not the GOG original's), paused while a movie plays, so
//    the open normally finds the result ready (else it waits at most 3 s). If MF cannot read the file (H.264 in MPEG-PS,
//    no MF codec): a 4:3 frame is taken as a 2:1 picture letterboxed in it (the community packs made for the SourceNext
//    / Classic REbirth versions), any other frame as a full picture. The quad is rebuilt in the UV call of the movie
//    draw (call 40EA4A -> 411FA0, patched to UvHook): UVs = the picture only, the quad = the picture at its own aspect
//    fitted into the game's 4:3 screen and centred on the band (2:1 = the band exactly, 16:9 = full width, 4:3 = full
//    screen, narrower = pillarbox). Never a non-uniform stretch. 2:1 frames (the originals, the private HD set) keep the
//    native quad bit for bit. The movie draw is native in HD and original (F8) mode alike, so both get the same quad.
//
// Log: PLAYER_MOVIES at install (folder, files found, hooks, background scans); one PLAYER_MOVIE line per movie open
// (file used and why, file size, frame size, fps, sound channels, picture rectangle and how it was found, the quad).
// Widescreen (widescreen.h crops the final frame to 16:9 around the 2:1 band while a movie plays): ScreenAspect() gives
// the aspect of the movie picture on screen (2.0 for the native quad) so the crop can stay inside it.
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <algorithm>
#include <cstdarg>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include "exe_map.h"
#pragma comment(lib, "mfuuid.lib")  // GUID constants only; mfplat.dll / mfreadwrite.dll are loaded at run time

namespace re3playermovies {
using SinkFn = void (*)(const char*);
using OpenFn = int(__thiscall*)(void*, const char*);
using UvFn = void(__thiscall*)(void*, float*);

// ---------------------------------------------------------------------------------------------------------------------
// picture detection (independent of the game: also used by the offline test qa/ctl-2026-11-09/movies/harness/pm_test)
struct Scan {
  bool ok = false;           // a picture was measured (at least 2 frames with content)
  bool twoToOne = false;     // not scanned: the frame is 2:1 (native quad)
  uint32_t fourcc = 0;       // the video codec as MF names it (subtype Data1: 'H264', 'HEVC', 'AV01'...), 0 = unknown
  int frameW = 0, frameH = 0;  // analysed area (= the frame dshow shows)
  int x0 = 0, y0 = 0, x1 = 0, y1 = 0;  // non-black rows/columns over all samples (pixels of the analysed area)
  double par = 1.0;          // pixel aspect ratio stated by the file (1 = square)
  int frames = 0, content = 0;  // frames analysed / with content
  double ms = 0, openMs = 0, firstMs = 0;  // time spent: total, file opened, first frame decoded
  char error[96] = "";
};
inline bool IsTwoToOne(int w, int h) { return w > 0 && h > 0 && std::abs(w - 2 * h) <= (std::max)(2, w / 100); }

namespace detail {
template <class T> struct ComPtr {
  T* p = nullptr;
  ~ComPtr() { if (p) p->Release(); }
  T** operator&() { if (p) { p->Release(); p = nullptr; } return &p; }
  T* operator->() const { return p; }
  explicit operator bool() const { return p != nullptr; }
};
inline double Now() {
  static LARGE_INTEGER f{}; if (!f.QuadPart) QueryPerformanceFrequency(&f);
  LARGE_INTEGER c; QueryPerformanceCounter(&c); return double(c.QuadPart) / double(f.QuadPart);
}
using MFStartupFn = HRESULT(WINAPI*)(ULONG, DWORD);
using MFShutdownFn = HRESULT(WINAPI*)();
using MFCreateAttributesFn = HRESULT(WINAPI*)(IMFAttributes**, UINT32);
using MFCreateMediaTypeFn = HRESULT(WINAPI*)(IMFMediaType**);
using MFCreateFileFn = HRESULT(WINAPI*)(MF_FILE_ACCESSMODE, MF_FILE_OPENMODE, MF_FILE_FLAGS, LPCWSTR, IMFByteStream**);
using MFCreateSourceResolverFn = HRESULT(WINAPI*)(IMFSourceResolver**);
using MFCreateSourceReaderFromMediaSourceFn = HRESULT(WINAPI*)(IMFMediaSource*, IMFAttributes*, IMFSourceReader**);
struct Mf {
  MFStartupFn startup = nullptr; MFShutdownFn shutdown = nullptr; MFCreateAttributesFn attributes = nullptr;
  MFCreateMediaTypeFn mediaType = nullptr; MFCreateFileFn file = nullptr; MFCreateSourceResolverFn resolver = nullptr;
  MFCreateSourceReaderFromMediaSourceFn reader = nullptr; bool tried = false;
};
inline Mf& MfApi() {
  static Mf mf; static std::mutex lock; std::lock_guard<std::mutex> guard(lock);
  if (!mf.tried) {
    mf.tried = true;
    HMODULE plat = LoadLibraryExA("mfplat.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    HMODULE rw = plat ? LoadLibraryExA("mfreadwrite.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32) : nullptr;
    if (plat && rw) {
      mf.startup = reinterpret_cast<MFStartupFn>(GetProcAddress(plat, "MFStartup"));
      mf.shutdown = reinterpret_cast<MFShutdownFn>(GetProcAddress(plat, "MFShutdown"));
      mf.attributes = reinterpret_cast<MFCreateAttributesFn>(GetProcAddress(plat, "MFCreateAttributes"));
      mf.mediaType = reinterpret_cast<MFCreateMediaTypeFn>(GetProcAddress(plat, "MFCreateMediaType"));
      mf.file = reinterpret_cast<MFCreateFileFn>(GetProcAddress(plat, "MFCreateFile"));
      mf.resolver = reinterpret_cast<MFCreateSourceResolverFn>(GetProcAddress(plat, "MFCreateSourceResolver"));
      mf.reader = reinterpret_cast<MFCreateSourceReaderFromMediaSourceFn>(GetProcAddress(rw, "MFCreateSourceReaderFromMediaSource"));
    }
    if (!mf.startup || !mf.shutdown || !mf.attributes || !mf.mediaType || !mf.file || !mf.resolver || !mf.reader) mf.startup = nullptr;
  }
  return mf;
}
// The container from the first bytes (players rename .mp4 to .dat): the resolver gets a matching file name as its hint.
inline const wchar_t* ContainerHint(const wchar_t* path) {
  uint8_t b[400] = {}; DWORD got = 0;
  HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
  if (f == INVALID_HANDLE_VALUE) return nullptr;
  ReadFile(f, b, sizeof b, &got, nullptr); CloseHandle(f);
  if (got < 16) return nullptr;
  if (!memcmp(b + 4, "ftyp", 4) || !memcmp(b + 4, "moov", 4) || !memcmp(b + 4, "mdat", 4) || !memcmp(b + 4, "wide", 4) || !memcmp(b + 4, "free", 4)) return L"movie.mp4";
  if (b[0] == 0x1a && b[1] == 0x45 && b[2] == 0xdf && b[3] == 0xa3) return L"movie.mkv";
  if (!memcmp(b, "RIFF", 4) && !memcmp(b + 8, "AVI ", 4)) return L"movie.avi";
  if (b[0] == 0 && b[1] == 0 && b[2] == 1 && (b[3] == 0xba || b[3] == 0xb3)) return L"movie.mpg";
  if (b[0] == 0x47 && got > 376 && b[188] == 0x47 && b[376] == 0x47) return L"movie.ts";
  if (got > 196 && b[4] == 0x47 && b[196] == 0x47) return L"movie.m2ts";
  if (b[0] == 0x30 && b[1] == 0x26 && b[2] == 0xb2 && b[3] == 0x75) return L"movie.wmv";
  return nullptr;
}
// one decoded frame: luma of the analysed area -> rows/columns with content
enum class Layout { Y8, Yuy2, P010, Rgb32 };
struct Accumulator {
  int w = 0, h = 0; std::vector<uint8_t> rows, cols; int frames = 0, content = 0;
  void Init(int width, int height) { w = width; h = height; rows.assign(size_t(h), 0); cols.assign(size_t(w), 0); }
  void Add(const uint8_t* scan0, LONG pitch, Layout layout) {
    // threshold above black: limited-range luma 16 + 18 (about 8 %); RGB (full range) 21
    const int threshold = layout == Layout::Rgb32 ? 21 : 34;
    auto luma = [&](int x, int y) -> int {
      const uint8_t* row = scan0 + ptrdiff_t(y) * pitch;
      switch (layout) {
        case Layout::Y8: return row[x];
        case Layout::Yuy2: return row[2 * x];
        case Layout::P010: return row[2 * x + 1];
        default: { const uint8_t* p = row + 4 * x; return (p[2] * 54 + p[1] * 183 + p[0] * 19) >> 8; }
      }
    };
    const int sx = (std::max)(1, w / 480), sy = (std::max)(1, h / 360);
    bool any = false;
    for (int y = 0; y < h; ++y) {
      int n = 0, hit = 0;
      for (int x = 0; x < w; x += sx) { ++n; if (luma(x, y) > threshold) ++hit; }
      if (hit >= (std::max)(2, n / 200)) { rows[size_t(y)] = 1; any = true; }
    }
    for (int x = 0; x < w; ++x) {
      int n = 0, hit = 0;
      for (int y = 0; y < h; y += sy) { ++n; if (luma(x, y) > threshold) ++hit; }
      if (hit >= (std::max)(2, n / 200)) { cols[size_t(x)] = 1; any = true; }
    }
    ++frames; if (any) ++content;
  }
  void Result(Scan& out) const {
    out.frameW = w; out.frameH = h; out.frames = frames; out.content = content;
    int y0 = 0, y1 = h, x0 = 0, x1 = w;
    while (y0 < h && !rows[size_t(y0)]) ++y0;
    while (y1 > y0 && !rows[size_t(y1 - 1)]) --y1;
    while (x0 < w && !cols[size_t(x0)]) ++x0;
    while (x1 > x0 && !cols[size_t(x1 - 1)]) --x1;
    out.x0 = x0; out.y0 = y0; out.x1 = x1; out.y1 = y1;
    out.ok = content >= 2 && x1 - x0 >= w / 8 && y1 - y0 >= h / 8;
    if (!out.ok && !out.error[0]) sprintf_s(out.error, "%d of %d frames with content", content, frames);
  }
};
}  // namespace detail

// Media Foundation scan of `path` (call on a thread that may initialise COM). wantW x wantH = the frame dshow decodes,
// if known (the area analysed when MF reports a padded frame, e.g. 1920x1088). budgetMs bounds the whole scan. After
// each seek (MF lands on the key frame before the point) frames are decoded forward to the point within that point's
// share of the budget, so files with few key frames are still sampled across their length. A 2:1 frame is not decoded
// (twoToOne = true: the native quad is used), nor a frame larger than 2048x2048 (dshow's file is refused anyway).
// QA 2026-11-09 (game runs): length of a file in seconds (MF presentation descriptor, nothing decoded), < 0 = unknown.
inline double ProbeSeconds(const wchar_t* path) {
  using namespace detail;
  Mf& mf = MfApi();
  if (!mf.startup || FAILED(mf.startup(MF_VERSION, MFSTARTUP_NOSOCKET))) return -1;
  struct Shutdown { Mf& mf; ~Shutdown() { mf.shutdown(); } } shutdown{mf};
  ComPtr<IMFByteStream> stream; ComPtr<IMFSourceResolver> resolver; ComPtr<IUnknown> object; ComPtr<IMFMediaSource> source;
  HRESULT hr = mf.file(MF_ACCESSMODE_READ, MF_OPENMODE_FAIL_IF_NOT_EXIST, MF_FILEFLAGS_NONE, path, &stream);
  if (SUCCEEDED(hr)) hr = mf.resolver(&resolver);
  if (SUCCEEDED(hr)) {
    const wchar_t* hint = ContainerHint(path); MF_OBJECT_TYPE type = MF_OBJECT_INVALID;
    hr = resolver->CreateObjectFromByteStream(stream.p, hint ? hint : path, MF_RESOLUTION_MEDIASOURCE | MF_RESOLUTION_CONTENT_DOES_NOT_HAVE_TO_MATCH_EXTENSION_OR_MIME_TYPE |
                                              MF_RESOLUTION_READ, nullptr, &type, &object);
    if (SUCCEEDED(hr)) hr = object->QueryInterface(__uuidof(IMFMediaSource), reinterpret_cast<void**>(&source));
  }
  if (FAILED(hr)) return -1;
  struct SourceShutdown { IMFMediaSource* s; ~SourceShutdown() { s->Shutdown(); } } sourceShutdown{source.p};
  ComPtr<IMFPresentationDescriptor> pd; UINT64 duration = 0;
  if (FAILED(source->CreatePresentationDescriptor(&pd)) || FAILED(pd->GetUINT64(MF_PD_DURATION, &duration)) || !duration) return -1;
  return double(duration) / 1e7;
}
inline Scan ScanFile(const wchar_t* path, int wantW, int wantH, double budgetMs = 2200, int samples = 16) {
  using namespace detail;
  Scan out; const double t0 = Now();
  auto elapsed = [&] { return (Now() - t0) * 1000; };
  auto finish = [&](const char* error) { if (error && !out.error[0]) strncpy_s(out.error, error, _TRUNCATE); out.ms = elapsed(); return out; };
  Mf& mf = MfApi();
  if (!mf.startup) return finish("Media Foundation not available");
  if (FAILED(mf.startup(MF_VERSION, MFSTARTUP_NOSOCKET))) return finish("MFStartup failed");
  struct Shutdown { Mf& mf; ~Shutdown() { mf.shutdown(); } } shutdown{mf};
  ComPtr<IMFAttributes> attributes;
  if (FAILED(mf.attributes(&attributes, 4))) return finish("MFCreateAttributes failed");
  attributes->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);
  attributes->SetUINT32(MF_SOURCE_READER_DISABLE_DXVA, TRUE);
  ComPtr<IMFByteStream> stream; ComPtr<IMFSourceResolver> resolver; ComPtr<IUnknown> object; ComPtr<IMFMediaSource> source;
  HRESULT hr = mf.file(MF_ACCESSMODE_READ, MF_OPENMODE_FAIL_IF_NOT_EXIST, MF_FILEFLAGS_NONE, path, &stream);
  if (SUCCEEDED(hr)) hr = mf.resolver(&resolver);
  if (SUCCEEDED(hr)) {
    const wchar_t* hint = ContainerHint(path);
    MF_OBJECT_TYPE type = MF_OBJECT_INVALID;
    hr = resolver->CreateObjectFromByteStream(stream.p, hint ? hint : path, MF_RESOLUTION_MEDIASOURCE | MF_RESOLUTION_CONTENT_DOES_NOT_HAVE_TO_MATCH_EXTENSION_OR_MIME_TYPE |
                                              MF_RESOLUTION_READ, nullptr, &type, &object);
    if (SUCCEEDED(hr)) hr = object->QueryInterface(__uuidof(IMFMediaSource), reinterpret_cast<void**>(&source));
  }
  if (FAILED(hr)) { sprintf_s(out.error, "MF cannot open the file (%08lx)", static_cast<unsigned long>(hr)); return finish(nullptr); }
  struct SourceShutdown { IMFMediaSource* s; ~SourceShutdown() { s->Shutdown(); } } sourceShutdown{source.p};
  ComPtr<IMFSourceReader> reader;
  if (FAILED(mf.reader(source.p, attributes.p, &reader))) return finish("MF: no source reader");
  out.openMs = elapsed();
  const DWORD video = static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM);
  reader->SetStreamSelection(static_cast<DWORD>(MF_SOURCE_READER_ALL_STREAMS), FALSE);
  if (FAILED(reader->SetStreamSelection(video, TRUE))) return finish("MF: no video stream");
  UINT32 frameW = 0, frameH = 0; MFVideoArea aperture{}; bool hasAperture = false;
  {
    ComPtr<IMFMediaType> native;
    if (SUCCEEDED(reader->GetNativeMediaType(video, 0, &native))) {
      GUID subtype = GUID_NULL;
      if (SUCCEEDED(native->GetGUID(MF_MT_SUBTYPE, &subtype))) out.fourcc = subtype.Data1;
      UINT64 v = 0;
      if (SUCCEEDED(native->GetUINT64(MF_MT_PIXEL_ASPECT_RATIO, &v)) && UINT32(v) && (v >> 32)) {
        const double par = double(v >> 32) / double(UINT32(v));
        if (par > 0.4 && par < 2.5) out.par = par;
      }
      hasAperture = SUCCEEDED(native->GetBlob(MF_MT_MINIMUM_DISPLAY_APERTURE, reinterpret_cast<UINT8*>(&aperture), sizeof aperture, nullptr));
      UINT64 size = 0;  // before any decoder is loaded: 2:1 and oversize frames need no scan
      if (SUCCEEDED(native->GetUINT64(MF_MT_FRAME_SIZE, &size))) {
        int nw = int(size >> 32), nh = int(UINT32(size));
        if (hasAperture && aperture.Area.cx > 0 && aperture.Area.cy > 0) { nw = aperture.Area.cx; nh = aperture.Area.cy; }
        if (wantW > 0 && wantH > 0 && nw >= wantW && nh >= wantH && nw - wantW <= 32 && nh - wantH <= 32) { nw = wantW; nh = wantH; }
        out.frameW = nw; out.frameH = nh;
        if (IsTwoToOne(nw, nh) && out.par == 1.0) { out.twoToOne = true; return finish("2:1 frame (native quad, no scan)"); }
        if (nw > 2048 || nh > 2048) return finish("frame larger than 2048x2048 (not playable)");
      }
    }
  }
  static const GUID* const kFormats[] = {&MFVideoFormat_NV12, &MFVideoFormat_I420, &MFVideoFormat_IYUV, &MFVideoFormat_YV12,
                                         &MFVideoFormat_YUY2, &MFVideoFormat_P010, &MFVideoFormat_RGB32};
  GUID chosen = GUID_NULL;
  for (const GUID* format : kFormats) {
    ComPtr<IMFMediaType> type;
    if (FAILED(mf.mediaType(&type))) break;
    type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video); type->SetGUID(MF_MT_SUBTYPE, *format);
    if (SUCCEEDED(reader->SetCurrentMediaType(video, nullptr, type.p))) { chosen = *format; break; }
  }
  if (chosen == GUID_NULL) return finish("MF: no decoder for the video");
  const detail::Layout layout = chosen == MFVideoFormat_YUY2 ? Layout::Yuy2 : chosen == MFVideoFormat_P010 ? Layout::P010 :
                                chosen == MFVideoFormat_RGB32 ? Layout::Rgb32 : Layout::Y8;
  LONG defaultStride = 0;
  {
    ComPtr<IMFMediaType> current;
    if (FAILED(reader->GetCurrentMediaType(video, &current))) return finish("MF: no output type");
    UINT64 v = 0;
    if (SUCCEEDED(current->GetUINT64(MF_MT_FRAME_SIZE, &v))) { frameW = UINT32(v >> 32); frameH = UINT32(v); }
    UINT32 stride = 0;
    if (SUCCEEDED(current->GetUINT32(MF_MT_DEFAULT_STRIDE, &stride))) defaultStride = static_cast<LONG>(stride);
    MFVideoArea a{};
    if (!hasAperture && SUCCEEDED(current->GetBlob(MF_MT_MINIMUM_DISPLAY_APERTURE, reinterpret_cast<UINT8*>(&a), sizeof a, nullptr))) { aperture = a; hasAperture = true; }
  }
  if (!frameW || !frameH) return finish("MF: no frame size");
  if (!defaultStride) defaultStride = static_cast<LONG>(frameW) * (layout == Layout::Rgb32 ? 4 : layout == Layout::Yuy2 || layout == Layout::P010 ? 2 : 1);
  int ax = 0, ay = 0, aw = int(frameW), ah = int(frameH);
  if (hasAperture && aperture.Area.cx > 0 && aperture.Area.cy > 0 && aperture.OffsetX.value >= 0 && aperture.OffsetY.value >= 0 &&
      aperture.OffsetX.value + aperture.Area.cx <= LONG(frameW) && aperture.OffsetY.value + aperture.Area.cy <= LONG(frameH)) {
    ax = aperture.OffsetX.value; ay = aperture.OffsetY.value; aw = aperture.Area.cx; ah = aperture.Area.cy;
  }
  if (wantW > 0 && wantH > 0 && aw >= wantW && ah >= wantH && aw - wantW <= 32 && ah - wantH <= 32) { aw = wantW; ah = wantH; }  // coded padding
  Accumulator acc; acc.Init(aw, ah);
  LONGLONG duration = 0;
  {
    PROPVARIANT var; PropVariantInit(&var);
    if (SUCCEEDED(reader->GetPresentationAttribute(static_cast<DWORD>(MF_SOURCE_READER_MEDIASOURCE), MF_PD_DURATION, &var)) && var.vt == VT_UI8)
      duration = static_cast<LONGLONG>(var.uhVal.QuadPart);
    PropVariantClear(&var);
  }
  const int pixelBytes = layout == Layout::Rgb32 ? 4 : layout == Layout::Y8 ? 1 : 2;
  auto analyse = [&](IMFSample* sample) -> bool {
    ComPtr<IMFMediaBuffer> buffer;
    if (FAILED(sample->ConvertToContiguousBuffer(&buffer))) return false;
    ComPtr<IMF2DBuffer> buffer2d; BYTE* scan0 = nullptr; LONG pitch = 0;
    if (SUCCEEDED(buffer->QueryInterface(__uuidof(IMF2DBuffer), reinterpret_cast<void**>(&buffer2d))) && SUCCEEDED(buffer2d->Lock2D(&scan0, &pitch))) {
      acc.Add(scan0 + ptrdiff_t(ay) * pitch + ptrdiff_t(ax) * pixelBytes, pitch, layout);
      buffer2d->Unlock2D(); return true;
    }
    BYTE* data = nullptr; DWORD length = 0;
    if (FAILED(buffer->Lock(&data, nullptr, &length))) return false;
    const LONG absPitch = defaultStride < 0 ? -defaultStride : defaultStride;
    bool ok = false;
    if (length >= DWORD(absPitch) * frameH) {
      BYTE* first = defaultStride < 0 ? data + size_t(absPitch) * (frameH - 1) : data;
      acc.Add(first + ptrdiff_t(ay) * defaultStride + ptrdiff_t(ax) * pixelBytes, defaultStride, layout);
      ok = true;
    }
    buffer->Unlock(); return ok;
  };
  // 1. key frames: seek to `samples` points over the file; MF lands on the key frame before each (the first frame it
  //    decodes there); one not analysed yet is analysed, a repeated one skipped. Spread over the file at key-frame
  //    granularity, cheap even for slow decoders (HEVC). At most 60 % of the budget.
  // 2. if fewer than 4 of them had content (few key frames: one per 8 s is the x265 default, short or faded files):
  //    from a third of the file on, every 8th decoded frame until 24 of them had content or the budget is spent.
  std::vector<LONGLONG> taken;
  auto fresh = [&](LONGLONG stamp) { for (LONGLONG t : taken) if (std::llabs(t - stamp) < 200000) return false; return true; };  // 20 ms
  auto seek = [&](LONGLONG target) { PROPVARIANT p; PropVariantInit(&p); p.vt = VT_I8; p.hVal.QuadPart = target; return SUCCEEDED(reader->SetCurrentPosition(GUID_NULL, p)); };
  auto next = [&](ComPtr<IMFSample>& sample, LONGLONG& stamp) -> int {  // 1 a frame, 0 end of stream, -1 error
    for (int n = 0; n < 64; ++n) {
      DWORD index = 0, flags = 0;
      if (FAILED(reader->ReadSample(video, 0, &index, &flags, &stamp, &sample)) || (flags & MF_SOURCE_READERF_ERROR)) return -1;
      if (!out.firstMs) out.firstMs = elapsed();
      if (sample) return 1;
      if (flags & MF_SOURCE_READERF_ENDOFSTREAM) return 0;
    }
    return -1;
  };
  if (duration > 0) {
    for (int k = 0; k < samples && elapsed() < budgetMs * 0.6; ++k) {
      if (!seek(static_cast<LONGLONG>(double(duration) * (k + 0.5) / samples))) break;
      ComPtr<IMFSample> sample; LONGLONG stamp = 0; const int got = next(sample, stamp);
      if (got < 0) break;
      if (got == 0 || !fresh(stamp)) continue;
      taken.push_back(stamp); analyse(sample.p);
    }
    if (acc.content < 4 && elapsed() < budgetMs && seek(duration / 3)) {
      LONGLONG lastTaken = -1;
      for (int n = 0; elapsed() < budgetMs && acc.content < 24; ++n) {  // 24 frames with content: 6 s of a 30 fps movie
        ComPtr<IMFSample> sample; LONGLONG stamp = 0;
        if (next(sample, stamp) <= 0) break;
        if (n % 8 == 0 && stamp != lastTaken && fresh(stamp)) { taken.push_back(stamp); lastTaken = stamp; analyse(sample.p); }
      }
    }
  }
  if (acc.frames < 2) {  // no duration / not seekable: every 20th of the first 600 frames
    PROPVARIANT start; PropVariantInit(&start); start.vt = VT_I8; start.hVal.QuadPart = 0;
    reader->SetCurrentPosition(GUID_NULL, start);
    for (int n = 0; n < 600 && acc.frames < samples && elapsed() < budgetMs; ++n) {
      DWORD index = 0, flags = 0; LONGLONG stamp = 0; ComPtr<IMFSample> sample;
      if (FAILED(reader->ReadSample(video, 0, &index, &flags, &stamp, &sample)) || (flags & (MF_SOURCE_READERF_ENDOFSTREAM | MF_SOURCE_READERF_ERROR))) break;
      if (sample && n % 20 == 0) analyse(sample.p);
    }
  }
  acc.Result(out);
  return finish(nullptr);
}

// The picture to show, from a scan or from the frame shape alone. Fractions of the frame + display aspect.
struct Picture {
  bool fit = false;            // false: the native quad (2:1 frame, movie_fit=0, nothing known)
  double fx0 = 0, fy0 = 0, fx1 = 1, fy1 = 1;  // picture inside the frame, 0..1
  double aspect = 2.0;         // display aspect of the picture (width / height)
  char method[64] = "native";
};
inline Picture FromScan(const Scan& s, int frameW, int frameH) {
  Picture p; p.fit = true;
  int x0 = s.x0, y0 = s.y0, x1 = s.x1, y1 = s.y1;
  const int w = s.frameW, h = s.frameH;
  // bars on both sides: keep the smaller one on both (a dark picture edge or a caption in one bar never crops picture)
  if (y0 > 0 || y1 < h) { const int bar = (std::min)(y0, h - y1); y0 = bar; y1 = h - bar; }
  if (x0 > 0 || x1 < w) { const int bar = (std::min)(x0, w - x1); x0 = bar; x1 = w - bar; }
  if (y0 <= h / 200) { y0 = 0; y1 = h; }  // noise-size bars (<= 0.5 %) are no bars
  if (x0 <= w / 200) { x0 = 0; x1 = w; }
  const bool cropY = y0 > 0, cropX = x0 > 0;
  double aspect = double(x1 - x0) * s.par / double(y1 - y0);
  // RE3's picture is 2:1. Letterbox (bars top/bottom): within 5 % of 2:1 -> exactly 2:1 (codec ringing at the bar edge
  // reads as picture: VP9 at 1.6 Mbit/s gave 1.94:1); flatter, up to 3:1 -> grown to
  // 2:1 (dark picture edges read as bars: growing shows at most a few bar rows, cropping would cut picture). Pillarbox
  // the same on the width (within 5 %, or narrower down to 1.5:1). Never beyond the frame; centred on the found picture.
  auto setHeight = [&](double hh) { const double c = (y0 + y1) / 2.0;
    y0 = (std::max)(0, int(std::lround(c - hh / 2))); y1 = (std::min)(h, int(std::lround(c + hh / 2))); };
  auto setWidth = [&](double ww) { const double c = (x0 + x1) / 2.0;
    x0 = (std::max)(0, int(std::lround(c - ww / 2))); x1 = (std::min)(w, int(std::lround(c + ww / 2))); };
  if (cropY) {
    const double hh = double(x1 - x0) * s.par / 2.0;
    if (std::abs(aspect - 2.0) < 0.1 || (aspect > 2.0 && aspect <= 3.0 && hh <= h)) setHeight(hh);
  } else if (cropX) {
    const double ww = double(y1 - y0) * 2.0 / s.par;
    if (std::abs(aspect - 2.0) < 0.1 || (aspect < 2.0 && aspect >= 1.5 && ww <= w)) setWidth(ww);
  }
  p.fx0 = double(x0) / w; p.fx1 = double(x1) / w; p.fy0 = double(y0) / h; p.fy1 = double(y1) / h;
  // fractions of the scanned area apply to dshow's frame of the same shape; the display aspect is in its pixels
  p.aspect = (p.fx1 - p.fx0) * frameW * s.par / ((p.fy1 - p.fy0) * frameH);
  sprintf_s(p.method, "scan %d/%d frames%s", s.content, s.frames, s.par != 1.0 ? " +par" : "");
  return p;
}
inline Picture FromShape(int frameW, int frameH, const char* why) {
  Picture p; p.fit = true;
  const double a = double(frameW) / frameH;
  if (std::abs(a - 4.0 / 3.0) < 0.03) {  // 4:3 frame: the 2:1 RE3 picture letterboxed in it
    const double band = double(frameW) / 2.0 / frameH; p.fy0 = (1 - band) / 2; p.fy1 = 1 - p.fy0; p.aspect = 2.0;
    sprintf_s(p.method, "rules: 4:3 frame = 2:1 letterbox (%s)", why);
  } else { p.aspect = a; sprintf_s(p.method, "rules: whole frame (%s)", why); }
  return p;
}

// ---------------------------------------------------------------------------------------------------------------------
// game side
namespace detail {
// dshow.dll player (GOG build of 26.11.2025, the one movie_sync.h verifies): amms -> +8 -> +8 = player;
// player+0x140 audio codec context (0 = no sound), +0x164 its channel count, +0x174/+0x178 frame rate num/den.
struct DshowSig { uint32_t rva; const char* bytes; };
constexpr DshowSig kDshowSigs[] = {
    {0xC18E6, "8b 45 08 56 8b 40 08 8b 70 08"},                    // SetState: player = [[this+8]+8]
    {0xC43CB, "8b 01 89 87 74 01 00 00 8b 41 04 8b 8f 34 01 00 00 89 87 78"},  // open: frame rate -> +174/+178
    {0xC43F4, "8b 8f 40 01 00 00 85 c9 74 4b"},                    // open: audio context +140
    {0xC440A, "8b 81 3c 01 00 00 89 87 64 01 00 00"},              // open: channels -> +164
    {0xC4579, "ff b7 70 01 00 00 8b 87 64 01 00 00"},              // swr input layout: channels +164
};
inline bool HexMatch(const uint8_t* p, const char* pattern) {
  for (const char* c = pattern; *c;) {
    if (*c == ' ') { ++c; continue; }
    if (c[0] != '?') { char b[3] = {c[0], c[1], 0}; if (strtoul(b, nullptr, 16) != *p) return false; }
    ++p; c += 2;
  }
  return true;
}
// one file's scan, shared by the background queue and the movie open
struct Entry {
  std::string key; std::wstring path; HANDLE done = nullptr; Scan scan; volatile LONG finished = 0; bool urgent = false;
  ~Entry() { if (done) CloseHandle(done); }
};
struct State {
  bool installed = false, enabled = true, fit = true, dshowOk = false, uvHooked = false;
  std::string root, dir, gameDir; SinkFn sink = nullptr; UvFn uv = nullptr;
  Picture current; volatile bool playing = false, opening = false; char name[64] = "-";
  bool quadLogged = false; float lastQuad[4] = {0, 0, 0, 0}; volatile float screenAspect = 2.0f;
  std::map<std::string, std::shared_ptr<Entry>> entries; std::deque<std::shared_ptr<Entry>> queue; std::mutex lock;
  HANDLE wake = nullptr; bool worker = false; int prescans = 0;
};
inline State& S() { static State s; return s; }
inline void Log(const char* fmt, ...) {
  if (!S().sink) return;
  char b[1024]; va_list a; va_start(a, fmt); vsnprintf(b, sizeof b, fmt, a); va_end(a); S().sink(b);
}
inline bool FileInfo(const std::string& path, uint64_t& size, uint64_t& time) {
  WIN32_FILE_ATTRIBUTE_DATA d{};
  if (!GetFileAttributesExA(path.c_str(), GetFileExInfoStandard, &d) || (d.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) return false;
  size = (uint64_t(d.nFileSizeHigh) << 32) | d.nFileSizeLow; time = (uint64_t(d.ftLastWriteTime.dwHighDateTime) << 32) | d.ftLastWriteTime.dwLowDateTime;
  return size > 0;
}
inline uint64_t FileSize(const std::string& path) { uint64_t size = 0, time = 0; return FileInfo(path, size, time) ? size : 0; }
inline std::string Lower(std::string s) { for (char& c : s) c = static_cast<char>(tolower(static_cast<unsigned char>(c))); return s; }
inline const char* BaseOf(const char* path) { const char* b = path; for (const char* c = path; *c; ++c) if (*c == '\\' || *c == '/') b = c + 1; return b; }
inline std::string Stem(const char* path) { std::string s = Lower(BaseOf(path)); if (const size_t dot = s.rfind('.'); dot != std::string::npos) s.resize(dot); return s; }
inline std::string Full(const std::string& path) {
  char b[MAX_PATH * 2]; const DWORD n = GetFullPathNameA(path.c_str(), sizeof b, b, nullptr);
  return n && n < sizeof b ? std::string(b) : path;
}
constexpr const char* kExtensions[] = {".dat", ".mp4", ".mkv", ".avi", ".webm", ".mov", ".m4v", ".mpg", ".mpeg", ".ts", ".m2ts", ".wmv"};
// the movies ResidentEvil3.exe opens (table 51B978: 14 entries, the 14th "bdino" has no file in the GOG version), in
// the order they first play, with the GOG file sizes (zmovie\*.dat, 18.10.2026): another size = a replaced file
// QA 2026-11-09 (game runs): + the GOG lengths in seconds (ffprobe). An opening shorter than its original crashed the game
// at the movie's end (420720: the render queue the opening sets up later in its length is still empty, write to 000FFFFC;
// release 1.21 the same with a cut opn.dat) - a player file shorter than kMinShare of the original is refused.
struct Original { const char* name; uint64_t size; double seconds; };
constexpr Original kOriginals[] = {{"opn", 20665008, 89.98}, {"roopne", 3651004, 15.75}, {"ins01", 6065640, 26.28}, {"ins02", 3404660, 14.68},
                                   {"ins03", 4615464, 19.95}, {"ins04", 6790728, 29.48}, {"ins05", 2918944, 12.55}, {"ins06", 7076580, 30.75},
                                   {"ins07", 5340552, 23.15}, {"ins08", 4285456, 18.55}, {"ins09", 3734668, 16.15}, {"enda", 12463612, 54.22},
                                   {"endb", 12881932, 56.03}};
constexpr double kShortSlack = 0.5;   // seconds a player file may be shorter (the HD remasters differ by up to 0.06 s)
inline double OriginalSeconds(const std::string& stem) {
  const std::string s = stem == "roop_nee" || stem == "roop" ? "roopne" : stem;
  for (const auto& o : kOriginals) if (s == o.name) return o.seconds;
  return -1;
}
inline std::string FindPlayerFile(const std::string& stem) {
  const State& s = S();
  if (!s.enabled || s.dir.empty()) return {};
  std::vector<std::string> stems{stem};
  if (stem == "roopne") stems.push_back("roop_nee");
  for (const auto& name : stems)
    for (const char* ext : kExtensions) {
      const std::string path = s.dir + "\\" + name + ext; uint64_t size = 0, time = 0;
      if (FileInfo(path, size, time)) return path;
    }
  return {};
}
// a scan is wanted for every player file and for a game-folder movie that is not the GOG original (size)
inline bool NeedsScan(const std::string& path, bool playerFile) {
  if (playerFile) return true;
  const std::string stem = Stem(path.c_str()); const uint64_t size = FileSize(path);
  for (const auto& o : kOriginals) if (stem == o.name) return size != o.size;
  return size != 0;
}
struct Info { int w = 0, h = 0, texW = 0, texH = 0, channels = -1; double fps = 0; bool audio = false; };
inline uint8_t* Movie(void* renderer) { return static_cast<uint8_t*>(renderer) + 0xcb08; }
inline uint8_t* Player(uint8_t* movie) {
  if (!S().dshowOk) return nullptr;
  auto amms = *reinterpret_cast<uint8_t**>(movie);
  if (!amms) return nullptr;
  auto inner = *reinterpret_cast<uint8_t**>(amms + 8);
  return inner ? *reinterpret_cast<uint8_t**>(inner + 8) : nullptr;
}
inline Info Inspect(void* renderer) {
  Info i; uint8_t* m = Movie(renderer);
  i.w = *reinterpret_cast<int*>(m + 0x10); i.h = *reinterpret_cast<int*>(m + 0x14);
  i.texW = *reinterpret_cast<int*>(m + 0xc8); i.texH = *reinterpret_cast<int*>(m + 0xcc);
  if (uint8_t* p = Player(m)) {
    i.audio = *reinterpret_cast<void**>(p + 0x140) != nullptr;
    i.channels = i.audio ? *reinterpret_cast<int*>(p + 0x164) : 0;
    const int num = *reinterpret_cast<int*>(p + 0x174), den = *reinterpret_cast<int*>(p + 0x178);
    if (num > 0 && den > 0) i.fps = double(num) / den;
  }
  return i;
}
inline std::string Key(const std::string& path) {
  uint64_t size = 0, time = 0; FileInfo(path, size, time);
  char b[64]; sprintf_s(b, "|%llu|%llu", static_cast<unsigned long long>(size), static_cast<unsigned long long>(time));
  return Lower(Full(path)) + b;
}
// MF scans on one worker thread (COM MTA there; the game thread keeps its own COM state), below normal priority. Queued
// background scans wait while a movie plays; a scan the movie open asks for goes first.
inline DWORD WINAPI Worker(void*) {
  State& s = S();
  const HRESULT co = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  for (;;) {
    std::shared_ptr<Entry> e; bool empty = true;
    {
      std::lock_guard<std::mutex> guard(s.lock);
      empty = s.queue.empty();
      if (!empty && (s.queue.front()->urgent || (!s.playing && !s.opening))) { e = s.queue.front(); s.queue.pop_front(); }
    }
    if (!e) { WaitForSingleObject(s.wake, empty ? INFINITE : 250); continue; }
    e->scan = ScanFile(e->path.c_str(), 0, 0, e->urgent ? 2200 : 4000);  // the movie open waits for an urgent one
    InterlockedExchange(&e->finished, 1); SetEvent(e->done);
  }
  if (SUCCEEDED(co)) CoUninitialize();  // not reached: the thread lives as long as the game
  return 0;
}
// the entry of `path` (queued if new); urgent = the movie open waits for it (moved to the front of the queue)
inline std::shared_ptr<Entry> Request(const std::string& path, bool urgent) {
  State& s = S();
  const std::string key = Key(path);
  std::lock_guard<std::mutex> guard(s.lock);
  if (auto it = s.entries.find(key); it != s.entries.end()) {
    auto e = it->second;
    if (urgent && !e->finished && !e->urgent) {
      e->urgent = true;
      auto q = std::find(s.queue.begin(), s.queue.end(), e);
      if (q != s.queue.end()) { s.queue.erase(q); s.queue.push_front(e); SetEvent(s.wake); }
    }
    return e;
  }
  auto e = std::make_shared<Entry>(); e->key = key; e->urgent = urgent;
  e->done = CreateEventA(nullptr, TRUE, FALSE, nullptr);
  wchar_t wide[MAX_PATH * 2] = L""; MultiByteToWideChar(CP_ACP, 0, Full(path).c_str(), -1, wide, MAX_PATH * 2); e->path = wide;
  if (!s.wake) s.wake = CreateEventA(nullptr, FALSE, FALSE, nullptr);
  if (!s.worker && s.wake && e->done) {
    if (HANDLE t = CreateThread(nullptr, 256 * 1024, Worker, nullptr, 0, nullptr)) { SetThreadPriority(t, THREAD_PRIORITY_BELOW_NORMAL); CloseHandle(t); s.worker = true; }
  }
  if (!s.worker || !e->done) { strcpy_s(e->scan.error, "no scan thread"); e->finished = 1; if (e->done) SetEvent(e->done); }
  else { if (urgent) s.queue.push_front(e); else s.queue.push_back(e); SetEvent(s.wake); }
  s.entries[key] = e;
  return e;
}
// the scan of `path` for the movie open, waiting at most waitMs; false = not ready (the rules are used, the scan goes
// on and is ready for the next play)
inline bool Lookup(const std::string& path, Scan& out, double& waitedMs, DWORD waitMs = 3000) {
  auto e = Request(path, true);
  const double t0 = Now();
  if (!e->finished) WaitForSingleObject(e->done, waitMs);
  waitedMs = (Now() - t0) * 1000;
  if (!e->finished) return false;
  out = e->scan; return true;
}
inline void Rel(const std::string& path, char* out, size_t size) {  // path relative to the game folder for the log
  const std::string& folder = S().root;
  if (!folder.empty() && path.size() > folder.size() && _strnicmp(path.c_str(), folder.c_str(), folder.size()) == 0 &&
      (path[folder.size()] == '\\' || path[folder.size()] == '/'))
    strncpy_s(out, size, path.c_str() + folder.size() + 1, _TRUNCATE);
  else strncpy_s(out, size, path.c_str(), _TRUNCATE);
}
// the quad the picture gets on a screen of sw x sh whose band is (bx0,by0)-(bx1,by1)
inline void Quad(const Picture& p, double sw, double sh, double bx0, double by0, double bx1, double by1, double q[4]) {
  double w = sw, h = sw / p.aspect;
  if (h > sh) { h = sh; w = sh * p.aspect; }
  const double cx = (bx0 + bx1) / 2, cy = (by0 + by1) / 2;
  double x0 = std::floor(cx - w / 2 + 0.5), y0 = std::floor(cy - h / 2 + 0.5);
  x0 = (std::max)(0.0, (std::min)(x0, sw - std::floor(w + 0.5))); y0 = (std::max)(0.0, (std::min)(y0, sh - std::floor(h + 0.5)));
  q[0] = x0; q[1] = y0; q[2] = x0 + std::floor(w + 0.5); q[3] = y0 + std::floor(h + 0.5);
}
inline void ReadIni() {
  State& s = S(); const std::string ini = s.root + "\\pilot.ini";
  s.enabled = GetPrivateProfileIntA("pilot", "player_movies", s.enabled ? 1 : 0, ini.c_str()) != 0;
  s.fit = GetPrivateProfileIntA("pilot", "movie_fit", s.fit ? 1 : 0, ini.c_str()) != 0;
}
}  // namespace detail

// The UV call of the movie draw: native UVs first, then the quad for the picture (fit only).
inline void __fastcall UvHook(void* movie, void*, float* v) {
  using namespace detail;
  State& s = S();
  s.uv(movie, v);
  if (!s.playing || !s.current.fit || !v) return;
  const auto* m = static_cast<const uint8_t*>(movie);
  const auto* renderer = m - 0xcb08;
  const int sw = *reinterpret_cast<const int*>(renderer + 0x84cc), sh = *reinterpret_cast<const int*>(renderer + 0x84d0);
  const int texW = *reinterpret_cast<const int*>(m + 0xc8), texH = *reinterpret_cast<const int*>(m + 0xcc);
  const int right = *reinterpret_cast<const int*>(m + 0xd8), bottom = *reinterpret_cast<const int*>(m + 0xdc);
  if (sw <= 0 || sh <= 0 || texW <= 0 || texH <= 0 || right <= 2 || bottom <= 2) return;
  float* v0 = v; float* v1 = v + 8; float* v2 = v + 16; float* v3 = v + 24;  // x y z rhw color spec u v
  const double bx0 = v1[0], by0 = v1[1], bx1 = v2[0], by1 = v2[1];
  if (!(bx1 > bx0 && by1 > by0) || bx0 < -1 || by0 < -1 || bx1 > sw + 1 || by1 > sh + 1) return;
  const Picture& p = s.current;
  double q[4]; Quad(p, sw, sh, bx0, by0, bx1, by1, q);
  // the native 1-texel inset at every edge (no bilinear bleed from the bars either)
  const float u0 = float((p.fx0 * right + 1) / texW), u1 = float((p.fx1 * right - 1) / texW);
  const float t0 = float((p.fy0 * bottom + 1) / texH), t1 = float((p.fy1 * bottom - 1) / texH);
  v0[0] = float(q[0]); v0[1] = float(q[3]); v0[6] = u0; v0[7] = t1;
  v1[0] = float(q[0]); v1[1] = float(q[1]); v1[6] = u0; v1[7] = t0;
  v2[0] = float(q[2]); v2[1] = float(q[3]); v2[6] = u1; v2[7] = t1;
  v3[0] = float(q[2]); v3[1] = float(q[1]); v3[6] = u1; v3[7] = t0;
  if (!s.quadLogged) {  // the band the game really draws vs the one assumed in the PLAYER_MOVIE line: logged only if they differ
    s.quadLogged = true;
    bool same = true; for (int i = 0; i < 4; ++i) same = same && std::abs(q[i] - s.lastQuad[i]) < 1.5;
    if (!same) Log("PLAYER_MOVIE %s quad %.0f,%.0f-%.0f,%.0f (band %.0f,%.0f-%.0f,%.0f on %dx%d, texture %dx%d)", s.name, q[0], q[1], q[2], q[3], bx0, by0, bx1, by1, sw, sh, texW, texH);
  }
  for (int i = 0; i < 4; ++i) s.lastQuad[i] = float(q[i]);
  if (q[3] > q[1]) s.screenAspect = float((q[2] - q[0]) / (q[3] - q[1]));
}

// Live switch for the F5 menu (pilot.ini player_movies, which the menu also writes): takes effect at the next movie.
inline void SetEnabled(int on) { detail::S().enabled = on != 0; }
inline int Enabled() { return detail::S().enabled ? 1 : 0; }
// Aspect of the movie picture on the game's screen while a movie plays: 2.0 = the native 2:1 band (also when no movie
// plays); a fitted picture gives its quad's width / height (16:9 = 1.78, a 4:3 picture = 1.33 = the whole screen).
// widescreen.h: crop aspect while a movie plays = min(its aspect, ScreenAspect()) keeps the whole picture visible.
inline float ScreenAspect() { const auto& s = detail::S(); return s.playing && s.current.fit ? s.screenAspect : 2.0f; }
// Last quad on screen (x0, y0, x1, y1 in the game's screen pixels) of a fitted movie; false = native quad or no movie.
inline bool FittedQuad(float out[4]) {
  const auto& s = detail::S();
  if (!s.playing || !s.current.fit || !s.quadLogged) return false;
  memcpy(out, s.lastQuad, sizeof s.lastQuad); return true;
}

// Installed from proxy.cpp installGameHooks (after the exe check).
inline void Install(re3exe::Base exeBase, const std::string& gameFolder, SinkFn sink) {
  using namespace detail;
  State& s = S(); if (s.installed) return;
  s.installed = true; s.root = gameFolder; s.sink = sink;
  const std::string ini = gameFolder + "\\pilot.ini";
  ReadIni();
  char dir[MAX_PATH] = "";
  GetPrivateProfileStringA("pilot", "player_movies_dir", "3DD-Movies", dir, MAX_PATH, ini.c_str());
  std::string d = dir; while (!d.empty() && (d.back() == '\\' || d.back() == '/' || d.back() == ' ')) d.pop_back();
  if (d.empty()) d = "3DD-Movies";
  s.dir = (d.size() > 1 && (d[1] == ':' || (d[0] == '\\' && d[1] == '\\'))) ? d : gameFolder + "\\" + d;
  // the game's own movie folder (bio3.ini [General] Movie=, read only): background scans of replaced files
  char movie[MAX_PATH] = "";
  GetPrivateProfileStringA("General", "Movie", ".\\zmovie", movie, MAX_PATH, (gameFolder + "\\bio3.ini").c_str());
  std::string g = movie; while (!g.empty() && (g.back() == '\\' || g.back() == '/' || g.back() == ' ')) g.pop_back();
  s.gameDir = (g.size() > 1 && (g[1] == ':' || (g[0] == '\\' && g[1] == '\\'))) ? g : gameFolder + "\\" + g;
  // dshow.dll player fields (mono check, frame rate)
  if (auto ds = reinterpret_cast<uint8_t*>(GetModuleHandleA("dshow.dll"))) {
    auto nt = reinterpret_cast<IMAGE_NT_HEADERS*>(ds + reinterpret_cast<IMAGE_DOS_HEADER*>(ds)->e_lfanew);
    s.dshowOk = nt->OptionalHeader.SizeOfImage == 0x20A6000;
    for (const auto& sig : kDshowSigs) if (s.dshowOk && !HexMatch(ds + sig.rva, sig.bytes)) s.dshowOk = false;
  }
  // geometry: the UV call of the movie draw, 40EA4A call 411FA0 (ecx = movie = renderer+CB08, arg = the 4 vertices at
  // renderer+844C, 32 bytes each: x y z rhw color specular u v; v0 = left/bottom, v1 left/top, v2 right/bottom, v3 right/top)
  const uintptr_t call = exeBase + 0xea4a, target = exeBase + 0x11fa0;
  auto c = reinterpret_cast<uint8_t*>(call);
  static const uint8_t kUv[] = {0x8b, 0x41, 0x30, 0x85, 0xc0, 0x74, 0x5a, 0xdb, 0x81, 0xc8, 0x00, 0x00, 0x00};
  const bool callOk = c[-3] == 0x8b && c[-2] == 0xcd && c[-1] == 0x53 && c[0] == 0xe8 &&
                      reinterpret_cast<uintptr_t>(c + 5 + *reinterpret_cast<int32_t*>(c + 1)) == target &&
                      memcmp(reinterpret_cast<void*>(target), kUv, sizeof kUv) == 0 &&
                      memcmp(reinterpret_cast<void*>(exeBase + 0xe9c4), "\x8d\x9e\x4c\x84\x00\x00", 6) == 0 &&   // vertices = renderer+844C
                      memcmp(reinterpret_cast<void*>(exeBase + 0xe935), "\x8d\xae\x08\xcb\x00\x00", 6) == 0 &&   // movie = renderer+CB08
                      memcmp(reinterpret_cast<void*>(exeBase + 0xb9d9), "\xdb\x86\xcc\x84\x00\x00", 6) == 0 &&   // screen width +84CC
                      memcmp(reinterpret_cast<void*>(exeBase + 0xb9f7), "\xdb\x86\xd0\x84\x00\x00", 6) == 0;     // screen height +84D0
  if (callOk) {
    s.uv = reinterpret_cast<UvFn>(target);
    DWORD prot = 0, unused = 0;
    if (VirtualProtect(c, 5, PAGE_EXECUTE_READWRITE, &prot)) {
      *reinterpret_cast<int32_t*>(c + 1) = static_cast<int32_t>(reinterpret_cast<uintptr_t>(&UvHook) - (call + 5));
      VirtualProtect(c, 5, prot, &unused); FlushInstructionCache(GetCurrentProcess(), c, 5); s.uvHooked = true;
    }
  }
  // what is in the folder (the 13 movies the exe opens), background scans of the files that will need one
  std::string found; int count = 0;
  for (const auto& o : kOriginals) {
    const std::string f = FindPlayerFile(o.name);
    if (!f.empty()) { ++count; found += ' '; found += BaseOf(f.c_str()); }
    if (!s.fit || !s.uvHooked) continue;
    if (!f.empty()) { Request(f, false); ++s.prescans; }
    const std::string game = s.gameDir + "\\" + o.name + ".dat";
    if (FileSize(game) && NeedsScan(game, false)) { Request(game, false); ++s.prescans; }
  }
  const DWORD attributes = GetFileAttributesA(s.dir.c_str());
  const bool exists = attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY);
  Log("PLAYER_MOVIES player_movies=%d folder=%s (%s) files=%d/13%s; movie_fit=%d quad hook %s; dshow player fields %s; background scans %d",
      s.enabled ? 1 : 0, s.dir.c_str(), exists ? "present" : "absent", count, found.c_str(), s.fit ? 1 : 0,
      s.uvHooked ? "installed at 40EA4A" : "skipped (code signature mismatch, native quad)", s.dshowOk ? "verified" : "unknown (mono/fps checks off)",
      s.prescans);
}

// Replaces the native open call in proxy.cpp movieHook (path == nullptr: close).
inline int Open(void* renderer, const char* gamePath, OpenFn original) {
  using namespace detail;
  State& s = S();
  s.current = Picture{}; s.playing = false; s.quadLogged = false; s.screenAspect = 2.0f;
  if (!s.installed || !gamePath) return original(renderer, gamePath);
  if (*reinterpret_cast<const uint32_t*>(static_cast<const uint8_t*>(renderer) + 0xcc08)) {  // 40F084: movies off (bio3.ini DisableMovie, QA runs)
    Log("PLAYER_MOVIE %s skipped by the game (movies disabled, renderer+CC08)", BaseOf(gamePath));
    return original(renderer, gamePath);
  }
  struct Opening { State& s; Opening(State& st) : s(st) { s.opening = true; } ~Opening() { s.opening = false; } } opening{s};  // no background scan starts meanwhile
  ReadIni();  // player_movies / movie_fit: the F5 menu writes them, they apply from the next movie
  const char* base = BaseOf(gamePath);
  strncpy_s(s.name, base, _TRUNCATE);
  const std::string player = FindPlayerFile(Stem(gamePath));
  const bool scanning = s.fit && s.uvHooked;
  // the scan comes first (nothing open yet), normally from the background queue
  std::string used = player.empty() ? std::string(gamePath) : player, refused;
  const char* source = player.empty() ? "original" : "player";
  Scan scan; bool haveScan = false; double waited = 0;
  auto measure = [&](const std::string& path, bool playerFile) {
    haveScan = scanning && NeedsScan(path, playerFile) && Lookup(path, scan, waited);
    if (scanning && NeedsScan(path, playerFile) && !haveScan) strcpy_s(scan.error, "scan not ready");
  };
  measure(used, !player.empty());
  // AV1: dshow.dll opens it but decodes one frame, then ends the movie (offline, pm_test open) - refused before opening
  const bool av1 = !player.empty() && haveScan && scan.fourcc == MAKEFOURCC('A', 'V', '0', '1');
  // QA 2026-11-09: shorter than the original -> refused before opening (the opening crashed the game at its early end)
  double playerSeconds = -1; const double originalSeconds = OriginalSeconds(Stem(gamePath));
  if (!player.empty() && !av1 && originalSeconds > 0) {
    wchar_t wide[MAX_PATH * 2] = L""; MultiByteToWideChar(CP_ACP, 0, player.c_str(), -1, wide, MAX_PATH * 2);
    playerSeconds = ProbeSeconds(wide);
  }
  const bool tooShort = playerSeconds > 0 && playerSeconds < originalSeconds - kShortSlack;
  int result = av1 || tooShort ? 0 : original(renderer, used.c_str());
  Info info = av1 || tooShort ? Info{} : Inspect(renderer);
  if (!player.empty()) {
    char why[160] = "";
    if (av1) sprintf_s(why, "AV1 video: GOG dshow.dll cannot decode it, use H.264");
    else if (tooShort) sprintf_s(why, "%.1f s, shorter than the original %.1f s: the game needs the whole length", playerSeconds, originalSeconds);
    else if (!result) sprintf_s(why, "dshow.dll cannot open it");
    else if (info.w <= 0 || info.h <= 0) sprintf_s(why, "no picture size");
    else if (info.texW < info.w || info.texH < info.h)
      sprintf_s(why, "frame %dx%d does not fit the movie texture (%dx%d; at most 2048x2048)", info.w, info.h, info.texW, info.texH);
    else if (info.channels == 1) sprintf_s(why, "mono sound (GOG dshow.dll crashes on it: convert to stereo)");
    if (why[0]) {
      char rel[MAX_PATH]; Rel(player, rel, sizeof rel);
      refused = std::string(" refused=") + rel + " (" + why + ")";
      if (result) original(renderer, nullptr);
      used = gamePath; source = "original"; scan = Scan{}; haveScan = false;
      measure(used, false);
      result = original(renderer, gamePath); info = Inspect(renderer);
    }
  }
  char rel[MAX_PATH]; Rel(used, rel, sizeof rel);
  const double mb = double(FileSize(used)) / 1048576.0;
  if (!result) { Log("PLAYER_MOVIE %s used=%s (%s, %.1f MB) open failed%s", s.name, rel, source, mb, refused.c_str()); return result; }
  if (info.channels == 1) {  // a mono file in the game's own folder: skipping the movie is better than the crash
    original(renderer, nullptr);
    Log("PLAYER_MOVIE %s used=%s (%s, %.1f MB) SKIPPED: mono sound crashes GOG dshow.dll (convert it to stereo)%s", s.name, rel, source, mb, refused.c_str());
    return 0;
  }
  char note[200] = "";
  const bool anamorphic = haveScan && scan.ok && scan.par != 1.0;  // a 2:1 frame of non-square pixels is not a 2:1 picture
  if (scanning && (!IsTwoToOne(info.w, info.h) || anamorphic)) {
    if (haveScan && scan.ok) {
      s.current = FromScan(scan, info.w, info.h);
      sprintf_s(note, " scan_area=%dx%d bounds=%d,%d-%d,%d scan_ms=%.0f waited_ms=%.0f", scan.frameW, scan.frameH, scan.x0, scan.y0, scan.x1, scan.y1, scan.ms, waited);
    } else {
      s.current = FromShape(info.w, info.h, scan.error[0] ? scan.error : "no scan");
      if (waited >= 1) sprintf_s(note, " waited_ms=%.0f", waited);
    }
  }
  if (info.texW < info.w || info.texH < info.h) strcat_s(note, " WARNING: frame larger than the movie texture (game file, shown downscaled)");
  s.playing = true;
  const Picture& p = s.current;
  char picture[200];
  if (p.fit) {
    const int px0 = int(std::lround(p.fx0 * info.w)), py0 = int(std::lround(p.fy0 * info.h)), px1 = int(std::lround(p.fx1 * info.w)), py1 = int(std::lround(p.fy1 * info.h));
    sprintf_s(picture, "picture=%d,%d-%d,%d (%dx%d, %.3f:1, %s)", px0, py0, px1, py1, px1 - px0, py1 - py0, p.aspect, p.method);
  } else sprintf_s(picture, "picture=whole frame (%s)", !s.fit ? "movie_fit=0" : !s.uvHooked ? "no quad hook" : "2:1, native quad");
  char audio[32]; if (info.channels < 0) sprintf_s(audio, "?"); else if (!info.audio) sprintf_s(audio, "none"); else sprintf_s(audio, "%dch", info.channels);
  char quad[96] = "";
  if (p.fit) {  // expected on screen: the band of the PSX layout (rows 40..200 of 240) on the game's screen; UvHook logs a difference
    const auto* r = static_cast<const uint8_t*>(renderer);
    const int sw = *reinterpret_cast<const int*>(r + 0x84cc), sh = *reinterpret_cast<const int*>(r + 0x84d0);
    if (sw > 0 && sh > 0) {
      double q[4]; Quad(p, sw, sh, 0, sh / 6.0, sw, sh * 5.0 / 6.0, q);
      for (int i = 0; i < 4; ++i) s.lastQuad[i] = float(q[i]);
      if (q[3] > q[1]) s.screenAspect = float((q[2] - q[0]) / (q[3] - q[1]));
      sprintf_s(quad, " quad=%.0f,%.0f-%.0f,%.0f on %dx%d", q[0], q[1], q[2], q[3], sw, sh);
    }
  }
  Log("PLAYER_MOVIE %s used=%s (%s, %.1f MB) frame=%dx%d fps=%.3f sound=%s %s%s%s%s", s.name, rel, source, mb, info.w, info.h, info.fps, audio, picture, quad, note,
      refused.c_str());
  return result;
}

}  // namespace re3playermovies
