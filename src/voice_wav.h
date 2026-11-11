#pragma once
// Voice packs (2026-11-11): reading a pack WAV and turning it into the bytes the game's own WAV parser takes.
//
// How the game plays a voice line (static analysis of ResidentEvil3.exe; RE3_MERCE.exe is the same code 32 bytes lower,
// exe_map.h range 0x8fb0..0x9de0 delta -32, bytes identical except rel32 call targets):
//   SCD voice opcode 46EE00 / scene code 441460, 449580 -> 427220(index, clip): path "/data_a/voice/" + name from a table
//   (room table [A5C94C] for index 0/1, S000/S001 table 517350 for odd index >= 2, M000 table 517288 for even), sets voice
//   state A59E51 = 1 and system flag A61F5C bit 0x20, then sound object [5349E0] (vtable 50A8A4), always channel 0:
//     +0x18 load(channel, path) = 4091C0: _strupr copy -> +0x64 4094E0 stops and frees the channel's buffer (a line still
//           playing is CUT here) -> +0x68 409410(holder [this+4], path, channel) -> +0x74 4098A0(holder, path) -> volume
//     +0x74 4098A0: file object [holder+0x24]: open(path) +0x20, seek end/start +0x30, malloc(size), read +0x28 (the whole
//           file, from the ROFS archives, no loose-file lookup), then +0x78 409960(buffer, size), free(buffer)
//     +0x78 409960: WAV parser with a FIXED layout: the 50 bytes at file+0x14 are the WAVEFORMATEX(+extension);
//           tag 2 (MS-ADPCM): data = file+0x5A (RIFF 12 + fmt 8+50 + fact 8+4 + data 8), length = size-0x5A (chunk sizes
//           are not read); ACM (acmDriverEnum -> acmFormatSuggest PCM16 -> acmStreamConvert BLOCKALIGN) converts the whole
//           clip to PCM16 at once; any other tag is taken as PCM: data = file+0x2C (44-byte header), length = size-0x2C,
//           nAvgBytesPerSec = nBlockAlign*rate, tag forced to 1; then one static DirectSound buffer (no streaming)
//     +0x1C play(channel) = 409240 -> +0x4C 409520: buffer Play (no loop)
//     +0x30 isPlaying(channel) = 409300: DirectSound GetStatus & DSBSTATUS_PLAYING
//   The voice state machine 4272F0 polls isPlaying(0) every frame; scene code waits on that state / flag (4306AE waits for
//   A59E51 == 0), so a longer line makes such a scene wait; where a scene starts the next line by its own timer, the next
//   load cuts the line still playing.
// A pack clip is served at +0x74: the file is read, converted (below) and handed to the native parser +0x78.
//
// Accepted: RIFF/WAVE (chunks in any order, LIST etc. ignored, odd chunk padding, WAVE_FORMAT_EXTENSIBLE, a data size of
// 0xFFFFFFFF / past the end = up to the end of the file) with PCM 16/24/32-bit integer, IEEE float 32 or MS-ADPCM (7
// coefficient sets), 1 or 2 channels, 8..48 kHz, up to 10 minutes and 256 MB. Served as: the MS-ADPCM data untouched in
// the game's 90-byte layout (MS-ADPCM file, gain 0), else PCM16 at the file's rate and channels in the 44-byte layout
// (24/32-bit integer: top 16 bits; float: x*32768 rounded, clipped - as ffmpeg converts without dither; gain applied in
// float, rounded, clipped). Only whole MS-ADPCM blocks are played (the game's ACM conversion drops a partial last block).
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace re3voice {

inline uint16_t Rd16(const uint8_t* p) { return uint16_t(p[0] | (p[1] << 8)); }
inline uint32_t Rd32(const uint8_t* p) { return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24); }
inline void Put16(std::vector<uint8_t>& v, uint32_t x) { v.push_back(uint8_t(x)); v.push_back(uint8_t(x >> 8)); }
inline void Put32(std::vector<uint8_t>& v, uint32_t x) { Put16(v, x & 0xffff); Put16(v, x >> 16); }
inline void PutId(std::vector<uint8_t>& v, const char* id) { v.insert(v.end(), id, id + 4); }

constexpr double kMaxSeconds = 600.0;
constexpr uint64_t kMaxFileBytes = 256ull * 1024 * 1024;

enum class Codec { None, Pcm16, Pcm24, Pcm32, Float32, Adpcm };
inline const char* CodecName(Codec c) {
    switch (c) { case Codec::Pcm16: return "pcm16"; case Codec::Pcm24: return "pcm24"; case Codec::Pcm32: return "pcm32";
                 case Codec::Float32: return "float32"; case Codec::Adpcm: return "ms-adpcm"; default: return "?"; }
}

struct WavInfo {
    Codec codec = Codec::None;
    uint16_t channels = 0, blockAlign = 0, samplesPerBlock = 0;
    uint32_t rate = 0, frames = 0;   // frames = sample frames that play (whole blocks for MS-ADPCM)
    const uint8_t* fmt = nullptr; uint32_t fmtSize = 0;
    const uint8_t* data = nullptr; uint32_t dataBytes = 0;   // whole frames / blocks only
    uint32_t factFrames = 0; bool hasFact = false;
    double Seconds() const { return rate ? double(frames) / double(rate) : 0.0; }
};

// RIFF/WAVE -> WavInfo (pointers into file). false + error for anything the game could not play as described above.
inline bool ParseWav(const uint8_t* file, size_t size, WavInfo& w, std::string& error) {
    w = WavInfo{};
    auto fail = [&](const char* text) { error = text; return false; };
    if (size < 12 || memcmp(file, "RIFF", 4) || memcmp(file + 8, "WAVE", 4)) return fail(size >= 4 && !memcmp(file, "RF64", 4) ? "RF64 (over 4 GB) is not supported" : "not a RIFF/WAVE file");
    if (size > kMaxFileBytes) return fail("file larger than 256 MB");
    const uint8_t* fmt = nullptr; uint32_t fmtSize = 0; const uint8_t* data = nullptr; uint64_t dataSize = 0;
    size_t pos = 12; unsigned chunks = 0;
    while (pos + 8 <= size && chunks < 256) {
        ++chunks; const uint8_t* id = file + pos; const uint64_t declared = Rd32(file + pos + 4); const size_t body = pos + 8;
        const uint64_t available = size - body;
        if (!memcmp(id, "data", 4)) {
            if (data) return fail("two data chunks");
            data = file + body; dataSize = declared > available ? available : declared;   // 0xFFFFFFFF / too long: up to the end
            pos = size_t(body + dataSize + (dataSize & 1));
            continue;
        }
        if (declared > available) { if (fmt && data) break; return fail("a chunk runs past the end of the file"); }
        if (!memcmp(id, "fmt ", 4)) { if (fmt) return fail("two fmt chunks"); fmt = file + body; fmtSize = uint32_t(declared); }
        else if (!memcmp(id, "fact", 4) && declared >= 4) { w.hasFact = true; w.factFrames = Rd32(file + body); }
        pos = size_t(body + declared + (declared & 1));
    }
    if (!fmt || fmtSize < 16) return fail("no fmt chunk");
    if (!data || !dataSize) return fail("no audio data");
    uint16_t tag = Rd16(fmt); const uint16_t channels = Rd16(fmt + 2); const uint32_t rate = Rd32(fmt + 4);
    const uint16_t blockAlign = Rd16(fmt + 12), bits = Rd16(fmt + 14);
    const uint16_t cbSize = fmtSize >= 18 ? Rd16(fmt + 16) : 0;
    if (tag == 0xfffe) {   // WAVE_FORMAT_EXTENSIBLE: sub-format GUID at +24, its first word is the plain tag
        if (fmtSize < 40 || cbSize < 22) return fail("short WAVE_FORMAT_EXTENSIBLE header");
        tag = Rd16(fmt + 24);
        if (tag != 1 && tag != 3) return fail("WAVE_FORMAT_EXTENSIBLE with a sub-format other than PCM or float");
    }
    if (channels < 1 || channels > 2) return fail("not mono or stereo");
    if (rate < 8000 || rate > 48000) return fail("sample rate outside 8000..48000 Hz");
    w.channels = channels; w.rate = rate; w.fmt = fmt; w.fmtSize = fmtSize; w.data = data; w.blockAlign = blockAlign;
    uint64_t frames = 0;
    if (tag == 1 || tag == 3) {
        if (tag == 1) w.codec = bits == 16 ? Codec::Pcm16 : bits == 24 ? Codec::Pcm24 : bits == 32 ? Codec::Pcm32 : Codec::None;
        else w.codec = bits == 32 ? Codec::Float32 : Codec::None;
        if (w.codec == Codec::None) return fail(tag == 1 ? "PCM that is not 16, 24 or 32-bit" : "float that is not 32-bit");
        if (blockAlign != channels * (bits / 8)) return fail("block size does not match channels and bits");
        frames = dataSize / blockAlign;
        w.dataBytes = uint32_t((std::min<uint64_t>)(frames * blockAlign, 0xffffffffull));
    } else if (tag == 2) {
        // MS-ADPCM: WAVEFORMATEX + wSamplesPerBlock + wNumCoef + 7 coefficient pairs = the 50 bytes 409960 copies
        if (fmtSize < 50 || cbSize < 32) return fail("MS-ADPCM header shorter than 50 bytes");
        const uint16_t spb = Rd16(fmt + 18), coefs = Rd16(fmt + 20);
        if (coefs != 7) return fail("MS-ADPCM with other than 7 coefficient sets");
        if (blockAlign < 8u * channels || blockAlign > 8192) return fail("bad MS-ADPCM block size");
        if (spb != (blockAlign - 7u * channels) * 2 / channels + 2) return fail("MS-ADPCM samples per block do not match the block size");
        w.codec = Codec::Adpcm; w.samplesPerBlock = spb;
        const uint64_t blocks = dataSize / blockAlign;
        frames = blocks * spb; w.dataBytes = uint32_t(blocks * blockAlign);
    } else return fail("codec is not PCM, IEEE float or MS-ADPCM");
    if (!frames) return fail("shorter than one sample frame / ADPCM block");
    if (double(frames) / double(rate) > kMaxSeconds) return fail("longer than 10 minutes");
    w.frames = uint32_t(frames);
    error.clear();
    return true;
}

// Standard MS-ADPCM decoder (the Microsoft reference: header predictor / delta / sample1 / sample2 per channel, samples
// sample2, sample1, then 4-bit codes high nibble first, channels interleaved). Whole blocks only.
inline void DecodeAdpcm(const WavInfo& w, std::vector<int16_t>& out) {
    static const int kAdapt[16] = {230, 230, 230, 230, 307, 409, 512, 614, 768, 614, 512, 409, 307, 230, 230, 230};
    int coef1[7], coef2[7];
    for (int i = 0; i < 7; ++i) { coef1[i] = int16_t(Rd16(w.fmt + 22 + 4 * i)); coef2[i] = int16_t(Rd16(w.fmt + 24 + 4 * i)); }
    const unsigned ch = w.channels, spb = w.samplesPerBlock, blocks = w.dataBytes / w.blockAlign;
    out.assign(size_t(blocks) * spb * ch, 0);
    int16_t* o = out.data();
    for (unsigned b = 0; b < blocks; ++b) {
        const uint8_t* p = w.data + size_t(b) * w.blockAlign;
        int pred[2], delta[2], s1[2], s2[2];
        for (unsigned c = 0; c < ch; ++c) pred[c] = (std::min)(6, int(p[c]));
        p += ch;
        for (unsigned c = 0; c < ch; ++c) { delta[c] = int16_t(Rd16(p)); p += 2; }
        for (unsigned c = 0; c < ch; ++c) { s1[c] = int16_t(Rd16(p)); p += 2; }
        for (unsigned c = 0; c < ch; ++c) { s2[c] = int16_t(Rd16(p)); p += 2; }
        for (unsigned c = 0; c < ch; ++c) *o++ = int16_t(s2[c]);
        for (unsigned c = 0; c < ch; ++c) *o++ = int16_t(s1[c]);
        const unsigned codes = (spb - 2) * ch;
        for (unsigned i = 0; i < codes; ++i) {
            const unsigned c = ch == 2 ? (i & 1) : 0;
            const int nibble = (i & 1) ? (p[i >> 1] & 0x0f) : (p[i >> 1] >> 4);
            const int signedNibble = nibble >= 8 ? nibble - 16 : nibble;
            int predicted = ((s1[c] * coef1[pred[c]]) + (s2[c] * coef2[pred[c]])) / 256;
            predicted += signedNibble * delta[c];
            predicted = (std::max)(-32768, (std::min)(32767, predicted));
            s2[c] = s1[c]; s1[c] = predicted;
            delta[c] = (kAdapt[nibble] * delta[c]) / 256;
            if (delta[c] < 16) delta[c] = 16;
            *o++ = int16_t(predicted);
        }
    }
}

inline int16_t Clip16(double x) { const double r = std::nearbyint(x); return int16_t(r > 32767 ? 32767 : r < -32768 ? -32768 : r); }

// Any accepted format -> interleaved PCM16 at the file's rate and channels, gain (linear factor) applied.
inline void ToPcm16(const WavInfo& w, double gain, std::vector<int16_t>& out) {
    const size_t n = size_t(w.frames) * w.channels; const bool unity = gain == 1.0;
    if (w.codec == Codec::Adpcm) { DecodeAdpcm(w, out); if (!unity) for (auto& s : out) s = Clip16(s * gain); return; }
    out.resize(n); const uint8_t* p = w.data;
    switch (w.codec) {
    case Codec::Pcm16: for (size_t i = 0; i < n; ++i) { const int16_t s = int16_t(Rd16(p + 2 * i)); out[i] = unity ? s : Clip16(s * gain); } break;
    case Codec::Pcm24:
        for (size_t i = 0; i < n; ++i) { const uint8_t* q = p + 3 * i; const int32_t s = int32_t((uint32_t(q[0]) << 8) | (uint32_t(q[1]) << 16) | (uint32_t(q[2]) << 24)) >> 8;
            out[i] = unity ? int16_t(s >> 8) : Clip16(double(s) / 256.0 * gain); }
        break;
    case Codec::Pcm32: for (size_t i = 0; i < n; ++i) { const int32_t s = int32_t(Rd32(p + 4 * i)); out[i] = unity ? int16_t(s >> 16) : Clip16(double(s) / 65536.0 * gain); } break;
    case Codec::Float32:
        for (size_t i = 0; i < n; ++i) { const uint32_t u = Rd32(p + 4 * i); float f; memcpy(&f, &u, 4); if (!std::isfinite(f)) f = 0;
            out[i] = Clip16(double(f) * 32768.0 * gain); }
        break;
    default: out.clear(); break;
    }
}

// The 44-byte PCM16 layout 409960 reads (data at +0x2C, nothing after it).
inline void BuildPcm16Image(const int16_t* pcm, size_t frames, unsigned channels, unsigned rate, std::vector<uint8_t>& image) {
    const uint32_t blockAlign = 2 * channels, bytes = uint32_t(frames * blockAlign);
    image.clear(); image.reserve(44 + size_t(bytes) + 64);   // 409960 copies 50 bytes from +0x14: keep slack readable
    PutId(image, "RIFF"); Put32(image, 36 + bytes); PutId(image, "WAVE");
    PutId(image, "fmt "); Put32(image, 16);
    Put16(image, 1); Put16(image, channels); Put32(image, rate); Put32(image, rate * blockAlign); Put16(image, blockAlign); Put16(image, 16);
    PutId(image, "data"); Put32(image, bytes);
    const uint8_t* b = reinterpret_cast<const uint8_t*>(pcm); image.insert(image.end(), b, b + bytes);
}
// The 90-byte MS-ADPCM layout 409960 reads: RIFF + fmt(50) + fact(4) + data, data untouched (whole blocks); the original
// clips come out byte-identical. nAvgBytesPerSec = rate*block/spb as in the originals; fact = the file's when plausible.
inline void BuildAdpcmImage(const WavInfo& w, std::vector<uint8_t>& image) {
    const uint16_t spb = w.samplesPerBlock;
    image.clear(); image.reserve(0x5a + size_t(w.dataBytes) + 64);
    PutId(image, "RIFF"); Put32(image, 0x5a - 8 + w.dataBytes); PutId(image, "WAVE");
    PutId(image, "fmt "); Put32(image, 50);
    Put16(image, 2); Put16(image, w.channels); Put32(image, w.rate); Put32(image, uint32_t(uint64_t(w.rate) * w.blockAlign / spb));
    Put16(image, w.blockAlign); Put16(image, 4); Put16(image, 32); Put16(image, spb); Put16(image, 7);
    image.insert(image.end(), w.fmt + 22, w.fmt + 50);
    const bool factOk = w.hasFact && w.factFrames && w.factFrames <= w.frames && w.factFrames + spb > w.frames;
    PutId(image, "fact"); Put32(image, 4); Put32(image, factOk ? w.factFrames : w.frames);
    PutId(image, "data"); Put32(image, w.dataBytes);
    image.insert(image.end(), w.data, w.data + w.dataBytes);
}

struct Prepared {
    std::vector<uint8_t> image;   // what goes to the native parser
    std::vector<int16_t> pcm;     // interleaved PCM16 (filled when asked for, e.g. for the automatic lip sync)
    Codec source = Codec::None; bool servedAdpcm = false;
    unsigned channels = 0, rate = 0; uint32_t frames = 0; double seconds = 0;
};
// gainDb outside -20..20 is clamped by the caller (pack.ini parser).
inline bool PrepareClip(const uint8_t* file, size_t size, double gainDb, bool wantPcm, Prepared& out, std::string& error) {
    out = Prepared{};
    WavInfo w; if (!ParseWav(file, size, w, error)) return false;
    out.source = w.codec; out.channels = w.channels; out.rate = w.rate; out.frames = w.frames; out.seconds = w.Seconds();
    const double gain = gainDb == 0 ? 1.0 : std::pow(10.0, gainDb / 20.0);
    try {
        if (w.codec == Codec::Adpcm && gain == 1.0) {
            BuildAdpcmImage(w, out.image); out.servedAdpcm = true;
            if (wantPcm) DecodeAdpcm(w, out.pcm);
        } else {
            ToPcm16(w, gain, out.pcm);
            if (out.pcm.size() < size_t(w.frames) * w.channels) { error = "conversion failed"; return false; }
            BuildPcm16Image(out.pcm.data(), w.frames, w.channels, w.rate, out.image);
            if (!wantPcm) { out.pcm.clear(); out.pcm.shrink_to_fit(); }
        }
    } catch (const std::bad_alloc&) { out = Prepared{}; error = "out of memory"; return false; }
    return true;
}

inline bool ReadFileW(const std::wstring& path, std::vector<uint8_t>& out, std::string& error) {
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (f == INVALID_HANDLE_VALUE) { error = "cannot open the file"; return false; }
    LARGE_INTEGER size{}; bool ok = GetFileSizeEx(f, &size) != 0;
    if (!ok || size.QuadPart < 12 || uint64_t(size.QuadPart) > kMaxFileBytes) { CloseHandle(f); error = ok ? (size.QuadPart < 12 ? "file too short" : "file larger than 256 MB") : "no file size"; return false; }
    try { out.resize(size_t(size.QuadPart)); } catch (const std::bad_alloc&) { CloseHandle(f); error = "out of memory"; return false; }
    DWORD got = 0; ok = ReadFile(f, out.data(), DWORD(out.size()), &got, nullptr) && got == out.size();
    CloseHandle(f);
    if (!ok) error = "read failed";
    return ok;
}

} // namespace re3voice
