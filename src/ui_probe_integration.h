#pragma once

// Include after root, uiRegistry, and log are declared. Call under the same
// native-render lock used by Registry.upload/move/clear and resolve.
// Only writes diagnostic files; never registers or selects a replacement.
#include <windows.h>
#include <array>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <unordered_set>

static void diagnosticUiProbe(unsigned tpage, unsigned clut,
                              float minU, float minV, float maxU, float maxV) {
    try {
        re3ui::IdentityProbe probe;
        if (!uiRegistry.probe(tpage, clut, probe, minU, minV, maxU, maxV)) return;
        // Manifest identities are data: accept only the exact ITEMIxx spelling
        // before using an ID as part of a local filename.
        if (probe.assetId.size() != 7 || probe.assetId.compare(0, 5, "ITEMI") != 0 ||
            probe.assetId.find_first_not_of("0123456789ABCDEF", 5) != std::string::npos) return;
        std::ostringstream key;
        key << probe.assetId << "-clut-" << std::hex << std::setfill('0') << std::setw(4) << clut
            << "-palette-";
        if (probe.palette.complete) key << std::setw(16) << probe.palette.hash;
        else key << "unknown";
        static std::unordered_set<std::string> seen;
        if (!seen.insert(key.str()).second) return;
        if (!probe.palette.complete || probe.palette.words.size() != 256 || probe.bpp != 8) {
            unsigned known = 0;
            for (auto value : probe.palette.known) known += value != 0;
            log("UI_PROBE_PENDING %s tpage=%04x clut=%04x paletteKnown=%u/%u generation=%llu",
                probe.assetId.c_str(), tpage, clut, known, probe.palette.entries,
                static_cast<unsigned long long>(probe.generation));
            return;
        }
        // Explicit LE16 serialization makes the evidence independent of host
        // endian assumptions. Complete palettes only; unknown shadow is omitted.
        std::array<unsigned char, 512> bytes{};
        for (size_t i = 0; i < probe.palette.words.size(); ++i) {
            bytes[i * 2] = static_cast<unsigned char>(probe.palette.words[i] & 255);
            bytes[i * 2 + 1] = static_cast<unsigned char>(probe.palette.words[i] >> 8);
        }
        if (re3ui::Registry::hashBytes(bytes.data(), bytes.size()) != probe.palette.hash) {
            log("UI_PROBE_HASH_REJECTED %s", probe.assetId.c_str()); return;
        }
        const std::string directory = root + "\\ui-probes";
        if (!CreateDirectoryA(directory.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) {
            log("UI_PROBE_WRITE_FAILED %s directory error=%lu", probe.assetId.c_str(), GetLastError()); return;
        }
        const std::string path = directory + "\\" + key.str() + ".le16";
        HANDLE file = CreateFileA(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                                  CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        DWORD written = 0;
        bool existing = false, okay = false;
        if (file != INVALID_HANDLE_VALUE) {
            okay = WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) != FALSE && written == bytes.size();
            CloseHandle(file);
        } else {
            existing = GetLastError() == ERROR_FILE_EXISTS || GetLastError() == ERROR_ALREADY_EXISTS;
            // A previous run may have captured this exact key. Verify its bytes
            // before calling it evidence; never overwrite an unknown old file.
            if (existing) {
                std::ifstream input(path, std::ios::binary);
                std::array<unsigned char, 512> previous{};
                input.read(reinterpret_cast<char*>(previous.data()), previous.size());
                okay = input.gcount() == static_cast<std::streamsize>(previous.size()) && input.peek() == std::char_traits<char>::eof() && previous == bytes;
            }
        }
        if (!okay) { log("UI_PROBE_WRITE_FAILED %s file=%s bytes=%lu", probe.assetId.c_str(), path.c_str(), written); return; }
        log("UI_PROBE %s tpage=%04x clut=%04x paletteFNV=%016llx imageFNV=%016llx entries=%u paletteXY=%u,%u sourceUV=%.1f,%.1f,%.1f,%.1f generation=%llu file=%s existing=%u",
            probe.assetId.c_str(), tpage, clut,
            static_cast<unsigned long long>(probe.palette.hash),
            static_cast<unsigned long long>(probe.imagePayloadHash), probe.palette.entries,
            probe.palette.x, probe.palette.y, probe.sourceMinU, probe.sourceMinV,
            probe.sourceMaxU, probe.sourceMaxV, static_cast<unsigned long long>(probe.generation),
            path.c_str(), existing ? 1u : 0u);
    } catch (...) {
        // Evidence collection cannot disrupt the original or replacement draw.
        static bool reported = false;
        if (!reported) { reported = true; log("UI_PROBE_EXCEPTION diagnostics skipped; draw unchanged"); }
    }
}
