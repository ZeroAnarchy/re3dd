#pragma once
// Voice packs (2026-11-11): <game>\3DD-Voice\<pack folder>\ = <CLIP>.wav files under the game's clip names (M101A040.wav,
// S001A010.wav ...), optional pack.ini, optional lipsync.r3lip; <game>\3DD-Voice\speakers.ini = speaker fixes for all packs.
// Discovery (folders sorted by name, at most kMaxPacks), pack.ini / speakers.ini parsing and the pilot.ini voice_pack value.
// Pure Win32 + STL, no game state: model_integration.h owns the runtime side, the offline tests use this file directly.
//
// pack.ini (UTF-8, BOM optional; ';' or '#' start a comment line; keys case-insensitive; a missing file = the folder name):
//   [pack]
//   name=Russian (AI)        display name in the F5 menu (any Unicode the menu font has); default: the folder name
//   name_ru=...              shown instead of name while the menu is in Russian (optional)
//   language=ru              code shown in the log (informational)
//   author=..., version=...  informational
//   gain_db=0                -20..+20 dB applied to every clip of the pack (0 keeps MS-ADPCM clips untouched)
//   [speakers]
//   M101A090=0               lip-sync speaker of a clip: 0 Jill, 1 Dario, 2 Brad, 3 Carlos, 4 Nikolai, 5 Mikhail, 6..15
//                            others; -1 = no lip sync for that clip
// speakers.ini: the same [speakers] section (fixes of the English model-pack\lipsync.r3lip for every pack).
#include <windows.h>
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>
#include "voice_clips.h"

namespace re3voice {

constexpr size_t kMaxPacks = 30;

inline const GameClip* FindGameClip(const char* name) {
    size_t lo = 0, hi = sizeof(kGameClips) / sizeof(kGameClips[0]);
    while (lo < hi) { const size_t mid = (lo + hi) / 2; const int c = strcmp(kGameClips[mid].name, name); if (!c) return &kGameClips[mid]; if (c < 0) lo = mid + 1; else hi = mid; }
    return nullptr;
}
inline size_t GameClipCount() { return sizeof(kGameClips) / sizeof(kGameClips[0]); }

// "/DATA_A/VOICE/M101A010.WAV" (the uppercase copy 4091C0 hands down) -> "M101A010"; empty when the path is not a voice
// clip or the name is not a plain clip name (1..15 of A-Z 0-9 _).
inline std::string VoiceClipName(const char* path) {
    if (!path) return {};
    std::string up(path, strnlen(path, 260));
    for (auto& c : up) { c = char(toupper(static_cast<unsigned char>(c))); if (c == '\\') c = '/'; }
    const auto at = up.find("/DATA_A/VOICE/");
    if (at == std::string::npos) return {};
    std::string base = up.substr(at + 14);
    if (base.size() < 5 || base.compare(base.size() - 4, 4, ".WAV") != 0) return {};
    base.resize(base.size() - 4);
    if (base.empty() || base.size() > 15) return {};
    for (char c : base) if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_')) return {};
    return base;
}

inline std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), int(s.size()), nullptr, 0);
    UINT cp = CP_UTF8; DWORD flags = MB_ERR_INVALID_CHARS;
    if (n <= 0) { cp = CP_ACP; flags = 0; n = MultiByteToWideChar(cp, flags, s.data(), int(s.size()), nullptr, 0); }   // not UTF-8: the ANSI code page
    std::wstring w(size_t((std::max)(0, n)), L'\0');
    if (n > 0) MultiByteToWideChar(cp, flags, s.data(), int(s.size()), &w[0], n);
    return w;
}
inline std::string WideToUtf8(const std::wstring& w) {
    if (w.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(size_t((std::max)(0, n)), '\0');
    if (n > 0) WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), &s[0], n, nullptr, nullptr);
    return s;
}
inline bool SameFolder(const std::wstring& a, const std::wstring& b) {
    return CompareStringOrdinal(a.c_str(), int(a.size()), b.c_str(), int(b.size()), TRUE) == CSTR_EQUAL;
}

// pilot.ini voice_pack=<folder>: printable ASCII stays as it is, other bytes of the UTF-8 name (and '%') as %XX, so the value
// survives the ANSI profile functions on any code page. Reading also takes a raw name (a hand edit: UTF-8 or ANSI).
inline std::string EncodePackKey(const std::wstring& folder) {
    static const char* hex = "0123456789ABCDEF"; std::string out;
    for (unsigned char c : WideToUtf8(folder)) { if (c >= 0x20 && c < 0x7f && c != '%' && c != ';' && c != '"') out += char(c); else { out += '%'; out += hex[c >> 4]; out += hex[c & 15]; } }
    return out;
}
inline std::wstring DecodePackKey(const std::string& value) {
    std::string bytes;
    for (size_t i = 0; i < value.size(); ++i) {
        auto hexv = [](char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'A' && c <= 'F' ? c - 'A' + 10 : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1; };
        if (value[i] == '%' && i + 2 < value.size() && hexv(value[i + 1]) >= 0 && hexv(value[i + 2]) >= 0) {
            bytes += char(hexv(value[i + 1]) * 16 + hexv(value[i + 2])); i += 2;
        } else bytes += value[i];
    }
    return Utf8ToWide(bytes);
}

inline std::string Trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r' || s[a] == '\n')) ++a;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r' || s[b - 1] == '\n')) --b;
    return s.substr(a, b - a);
}
inline std::string Lower(std::string s) { for (auto& c : s) c = char(tolower(static_cast<unsigned char>(c))); return s; }
inline std::string Upper(std::string s) { for (auto& c : s) c = char(toupper(static_cast<unsigned char>(c))); return s; }

struct PackInfo {
    std::wstring folder, dir;              // folder name; full path of the folder
    std::wstring name, nameRu;             // display names
    std::string language, author, version;
    double gainDb = 0;
    std::map<std::string, int> speakers;   // CLIP -> -1..15
    std::vector<std::string> clips;        // game clip names with a WAV in the folder (sorted)
    std::vector<std::string> notGameClips; // other *.wav names (never played)
    bool hasIni = false, hasLipsync = false;
    std::vector<std::string> warnings;     // pack.ini lines that were ignored or clamped
    bool HasClip(const std::string& clip) const { return std::binary_search(clips.begin(), clips.end(), clip); }
};

// [speakers] lines into map; returns the number of entries taken; warnings for the rest
inline void ParseSpeakerLine(const std::string& key, const std::string& value, std::map<std::string, int>& speakers, std::vector<std::string>& warnings) {
    const std::string clip = Upper(key); char* end = nullptr; const long id = strtol(value.c_str(), &end, 10);
    if (!FindGameClip(clip.c_str())) { warnings.push_back("[speakers] " + key + ": not a game clip name"); return; }
    if (value.empty() || *end || id < -1 || id > 15) { warnings.push_back("[speakers] " + key + "=" + value + ": speaker must be -1..15"); return; }
    speakers[clip] = int(id);
}
// pack.ini / speakers.ini text (UTF-8, BOM optional). speakersOnly: speakers.ini (only the [speakers] section counts).
inline void ParsePackIni(std::string text, PackInfo& p, bool speakersOnly = false) {
    if (text.size() >= 3 && (unsigned char)text[0] == 0xef && (unsigned char)text[1] == 0xbb && (unsigned char)text[2] == 0xbf) text.erase(0, 3);
    std::string section; size_t pos = 0; unsigned lineNo = 0;
    while (pos <= text.size()) {
        size_t nl = text.find('\n', pos); if (nl == std::string::npos) nl = text.size();
        const std::string line = Trim(text.substr(pos, nl - pos)); pos = nl + 1; ++lineNo;
        if (line.empty() || line[0] == ';' || line[0] == '#') { if (nl == text.size()) break; continue; }
        if (line[0] == '[') { const auto close = line.find(']'); section = Lower(Trim(line.substr(1, close == std::string::npos ? std::string::npos : close - 1))); if (nl == text.size()) break; continue; }
        const auto eq = line.find('=');
        if (eq == std::string::npos) { p.warnings.push_back("line " + std::to_string(lineNo) + ": no '='"); if (nl == text.size()) break; continue; }
        const std::string key = Lower(Trim(line.substr(0, eq))); std::string value = Trim(line.substr(eq + 1));
        if (value.size() >= 2 && value.front() == '"' && value.back() == '"') value = value.substr(1, value.size() - 2);
        if (section == "speakers") ParseSpeakerLine(Trim(line.substr(0, eq)), value, p.speakers, p.warnings);
        else if (section == "pack") {
            if (speakersOnly) { if (nl == text.size()) break; continue; }
            if (key == "name") p.name = Utf8ToWide(value);
            else if (key == "name_ru") p.nameRu = Utf8ToWide(value);
            else if (key == "language") p.language = value;
            else if (key == "author") p.author = value;
            else if (key == "version") p.version = value;
            else if (key == "gain_db") {
                char* end = nullptr; double g = strtod(value.c_str(), &end);
                if (value.empty() || *end || !(g == g)) p.warnings.push_back("gain_db=" + value + ": not a number, 0 used");
                else { if (g < -20 || g > 20) { p.warnings.push_back("gain_db=" + value + ": clamped to -20..20"); g = (std::max)(-20.0, (std::min)(20.0, g)); } p.gainDb = g; }
            } else p.warnings.push_back("[pack] " + key + ": unknown key");
        } else p.warnings.push_back("line " + std::to_string(lineNo) + ": outside [pack] / [speakers]");
        if (nl == text.size()) break;
    }
    // display names: no control characters, at most 60 characters
    for (std::wstring* n : {&p.name, &p.nameRu}) { n->erase(std::remove_if(n->begin(), n->end(), [](wchar_t c) { return c < 0x20; }), n->end()); if (n->size() > 60) n->resize(60); }
}

// One automatic lip track as an R3LIP1 file in memory (model_face_rig.h LipSync::LoadBytes checks it like a file): header
// 32 bytes, one 32-byte clip record (name[16], speaker, frameCount, firstFrame 0, duration), frames 4 bytes each at 100 Hz.
inline std::vector<uint8_t> OneClipR3lip(const std::string& clip, int speaker, const std::vector<uint8_t>& frames, uint32_t frameCount, float duration) {
    std::vector<uint8_t> b(64, 0);
    const uint32_t head[6] = {1, 32, 1, 100, 64, uint32_t(64 + frames.size())};
    memcpy(b.data(), "R3LIP1\0\0", 8); memcpy(b.data() + 8, head, 24);
    memcpy(b.data() + 32, clip.c_str(), (std::min<size_t>)(clip.size(), 15));
    const uint32_t rec[3] = {uint32_t(speaker), frameCount, 0}; memcpy(b.data() + 48, rec, 12); memcpy(b.data() + 60, &duration, 4);
    b.insert(b.end(), frames.begin(), frames.end());
    return b;
}

inline bool ReadSmallFile(const std::wstring& path, std::string& out, size_t maximum = 1 << 20) {
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size{}; bool ok = GetFileSizeEx(f, &size) && size.QuadPart >= 0 && uint64_t(size.QuadPart) <= maximum;
    if (ok) { out.resize(size_t(size.QuadPart)); DWORD got = 0; ok = out.empty() || (ReadFile(f, &out[0], DWORD(out.size()), &got, nullptr) && got == out.size()); }
    CloseHandle(f);
    return ok;
}

// <root>\3DD-Voice\*: folders (not hidden, not '.' / '..'), sorted case-insensitively by name, at most kMaxPacks. A folder
// without any *.wav of a game clip is still listed (its log line says so) but offered in the menu only when it has clips.
inline std::vector<PackInfo> DiscoverPacks(const std::wstring& voiceRoot, size_t* skipped = nullptr) {
    std::vector<PackInfo> packs; std::vector<std::wstring> folders;
    WIN32_FIND_DATAW fd; HANDLE h = FindFirstFileW((voiceRoot + L"\\*").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || (fd.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN)) continue;
            if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) continue;
            folders.push_back(fd.cFileName);
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    std::sort(folders.begin(), folders.end(), [](const std::wstring& a, const std::wstring& b) {
        return CompareStringOrdinal(a.c_str(), int(a.size()), b.c_str(), int(b.size()), TRUE) == CSTR_LESS_THAN; });
    if (skipped) *skipped = folders.size() > kMaxPacks ? folders.size() - kMaxPacks : 0;
    if (folders.size() > kMaxPacks) folders.resize(kMaxPacks);
    for (const auto& folder : folders) {
        PackInfo p; p.folder = folder; p.dir = voiceRoot + L"\\" + folder;
        WIN32_FIND_DATAW f; HANDLE w = FindFirstFileW((p.dir + L"\\*.wav").c_str(), &f);
        if (w != INVALID_HANDLE_VALUE) {
            do {
                if (f.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
                std::wstring stem = f.cFileName; const auto dot = stem.rfind(L'.'); if (dot != std::wstring::npos) stem.resize(dot);
                std::string name; bool ascii = true;
                for (wchar_t c : stem) { if (c > 0x7e || c < 0x20) { ascii = false; break; } name += char(toupper(int(c))); }
                if (ascii && FindGameClip(name.c_str())) p.clips.push_back(name);
                else p.notGameClips.push_back(ascii ? std::string(name) : WideToUtf8(stem));
            } while (FindNextFileW(w, &f));
            FindClose(w);
        }
        std::sort(p.clips.begin(), p.clips.end()); p.clips.erase(std::unique(p.clips.begin(), p.clips.end()), p.clips.end());
        std::sort(p.notGameClips.begin(), p.notGameClips.end());
        std::string ini;
        if (ReadSmallFile(p.dir + L"\\pack.ini", ini)) { p.hasIni = true; ParsePackIni(ini, p); }
        if (p.name.empty()) p.name = folder;
        p.hasLipsync = GetFileAttributesW((p.dir + L"\\lipsync.r3lip").c_str()) != INVALID_FILE_ATTRIBUTES;
        packs.push_back(std::move(p));
    }
    return packs;
}

} // namespace re3voice
