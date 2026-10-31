#pragma once

// Read-only identity route for native opcode 0x14 raw16 backgrounds. These
// packets bypass TIM/VRAM uploads. No game-memory reads, writes or D3D calls.
// Caller snapshots the 28-byte packet and exactly width*height*2 source bytes.
// Do not cache identity by raw pointer alone: the game's arena is reused.
#include "ui_runtime.h"
#include <limits>

namespace re3ui {
struct Raw16Packet {
    std::uint32_t sourceAddress = 0;
    std::int32_t x = 0, y = 0, width = 0, height = 0;
};

class Raw16Registry {
    struct Entry {
        unsigned width = 0, height = 0;
        std::uint64_t hash = 0;
        Binding binding;
    };
    std::vector<Entry> entries_;
    static bool hashValue(const std::string& text, std::uint64_t& value) {
        if (text.size() != 16 || text.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos) return false;
        std::istringstream in(text); in >> std::hex >> value; return !in.fail();
    }
    static bool relativePath(const std::string& value) {
        return !value.empty() && value != "-" && value.find(':') == std::string::npos &&
            value.front() != '/' && value.front() != '\\' && value.find("..") == std::string::npos;
    }
public:
    // RAW16 id width height exactPayloadFNV64 scale relativePNG relativeSTP
    bool load(const std::string& path, std::string* error = nullptr) {
        std::ifstream file(path); if (!file) { if (error) *error = "Cannot open RAW16 manifest"; return false; }
        std::vector<Entry> next; std::string line; unsigned number = 0;
        auto fail = [&]() { if (error) *error = "Invalid RAW16 manifest line " + std::to_string(number); return false; };
        while (std::getline(file,line)) {
            ++number; std::istringstream row(line); std::string kind, hash, extra; Entry entry;
            if (!(row >> kind) || kind.front() == '#') continue;
            if (kind != "RAW16" || !(row >> entry.binding.assetId >> entry.width >> entry.height >> hash >> entry.binding.scale >>
                std::quoted(entry.binding.pngPath) >> std::quoted(entry.binding.stpPath)) || (row >> extra)) return fail();
            if (!entry.width || !entry.height || entry.width > 1024 || entry.height > 512 ||
                !entry.binding.scale || entry.binding.scale > 16 || !hashValue(hash,entry.hash) ||
                !relativePath(entry.binding.pngPath) || (entry.binding.stpPath != "-" && !relativePath(entry.binding.stpPath))) return fail();
            for (const auto& previous : next) if (previous.binding.assetId == entry.binding.assetId ||
                (previous.width == entry.width && previous.height == entry.height && previous.hash == entry.hash)) return fail();
            entry.binding.originalW = entry.width; entry.binding.originalH = entry.height;
            if (entry.binding.stpPath == "-") entry.binding.stpPath.clear();
            next.push_back(entry); if (next.size() > 128) return fail();
        }
        if (!file.eof() || next.empty()) return fail();
        entries_ = std::move(next); if (error) error->clear(); return true;
    }
    std::size_t count() const { return entries_.size(); }
    static bool decodePacket(const void* data, std::size_t size, Raw16Packet& out) {
        out = Raw16Packet{}; if (!data || size < 28) return false;
        const auto p = static_cast<const std::uint8_t*>(data);
        if (p[7] != 0x14) return false; // no opcode/tint/ABR wildcard
        std::memcpy(&out.sourceAddress,p+8,4); std::memcpy(&out.x,p+12,4); std::memcpy(&out.y,p+16,4);
        std::memcpy(&out.width,p+20,4); std::memcpy(&out.height,p+24,4);
        if (!out.sourceAddress || out.width <= 0 || out.height <= 0 || out.width > 1024 || out.height > 512 ||
            out.x < -32768 || out.x > 32767 || out.y < -32768 || out.y > 32767) return false;
        return true;
    }
    // Every successful call proves complete raw content and dimensions. A
    // changed byte, unregistered image, partial snapshot or wrong size fails.
    bool resolve(const Raw16Packet& packet, const void* snapshot, std::size_t bytes, Binding& out) const {
        out = Binding{}; if (!snapshot || !packet.sourceAddress || packet.width <= 0 || packet.height <= 0) return false;
        bool possible = false;
        for (const auto& entry : entries_) if (entry.width == unsigned(packet.width) && entry.height == unsigned(packet.height)) possible = true;
        if (!possible || bytes != std::size_t(packet.width)*std::size_t(packet.height)*2u) return false;
        const auto hash = Registry::hashBytes(snapshot,bytes);
        for (const auto& entry : entries_) if (entry.width == unsigned(packet.width) && entry.height == unsigned(packet.height) && entry.hash == hash) {
            out = entry.binding; return true;
        }
        return false;
    }
};
} // namespace re3ui
