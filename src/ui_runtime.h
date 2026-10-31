#pragma once

// Native TIM/VRAM identity only. No Direct3D calls and no game memory writes.
// Feed native upload/clear/move events after the corresponding original call.
// Format (hex FNV-1a64 hashes are over raw little-endian payload bytes):
// IMAGE id bpp originalPixelWidth originalPixelHeight pixelPayloadHash
// PALETTE id paletteIndex entries rawPaletteWordsHash
// TEXTURE id paletteIndex scale relativePngPath relativeStpPath
// Lines beginning '#' are comments. Paths may be quoted; '-' means no STP file.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace re3ui {

struct Rect16 { std::int16_t x, y, w, h; };
static_assert(sizeof(Rect16) == 8, "Native VRAM rectangle must be 8 bytes");

struct Binding {
    std::string assetId, pngPath, stpPath;
    unsigned originalW = 0, originalH = 0;
    unsigned paletteIndex = 0, scale = 1;
    float offsetU = 0, offsetV = 0;
    std::uint64_t generation = 0;
};

// Read-only identity diagnostics; these objects cannot create a replacement.
struct PaletteSnapshot {
    unsigned x = 0, y = 0, entries = 0;
    std::uint64_t hash = 0;
    bool complete = false;
    std::vector<std::uint16_t> words;
    std::vector<std::uint8_t> known;
};
struct IdentityProbe {
    std::string assetId;
    unsigned originalW = 0, originalH = 0, bpp = 0;
    float offsetU = 0, offsetV = 0;
    float sourceMinU = 0, sourceMinV = 0, sourceMaxU = 0, sourceMaxV = 0;
    Rect16 placement{};
    std::uint64_t imagePayloadHash = 0, generation = 0;
    PaletteSnapshot palette;
};

class Registry {
    static constexpr int Width = 1024, Height = 512;
    static constexpr std::uint64_t FnvOffset = 14695981039346656037ull;
    static constexpr std::uint64_t FnvPrime = 1099511628211ull;
    struct Rect { int x, y, w, h; };
    struct Palette {
        unsigned index = 0, entries = 0, scale = 1;
        std::uint64_t hash = 0;
        std::string png, stp;
    };
    struct Image {
        std::string id;
        unsigned bpp = 0, width = 0, height = 0, wordWidth = 0;
        std::uint64_t hash = 0;
        std::vector<Palette> palettes;
    };
    struct Placement {
        std::size_t image;
        Rect rect;
        // A later glyph/icon upload may overwrite only part of the original
        // image. Preserve the remaining panel ownership word by word.
        std::vector<std::uint8_t> owned;
    };
    std::vector<Image> images_;
    std::vector<Placement> placements_;
    std::vector<std::uint16_t> words_ = std::vector<std::uint16_t>(Width * Height);
    std::vector<std::uint8_t> known_ = std::vector<std::uint8_t>(Width * Height);
    std::uint64_t generation_ = 0;

    static Rect rect(const Rect16& r) { return {r.x, r.y, r.w, r.h}; }
    static bool valid(const Rect& r) {
        return r.x >= 0 && r.x < Width && r.y >= 0 && r.y < Height &&
            r.w > 0 && r.h > 0 && r.w <= Width - r.x && r.h <= Height - r.y;
    }
    static Rect clipped(Rect r) {
        // The native upload/clear functions reduce width/height at negative
        // origins and do not advance the source pointer for the clipped part.
        if (r.x < 0) { r.w += r.x; r.x = 0; }
        if (r.y < 0) { r.h += r.y; r.y = 0; }
        r.w = (std::min)(r.w, Width - r.x);
        r.h = (std::min)(r.h, Height - r.y);
        return r;
    }
    static bool overlap(const Rect& a, const Rect& b) {
        return a.x < b.x + b.w && b.x < a.x + a.w &&
            a.y < b.y + b.h && b.y < a.y + a.h;
    }
    static bool contains(const Rect& a, const Rect& b) {
        return b.x >= a.x && b.y >= a.y &&
            b.x + b.w <= a.x + a.w && b.y + b.h <= a.y + a.h;
    }
    void invalidate(const Rect& r) {
        for (auto& p : placements_) if (overlap(p.rect, r)) {
            const int left = (std::max)(p.rect.x, r.x) - p.rect.x;
            const int right = (std::min)(p.rect.x + p.rect.w, r.x + r.w) - p.rect.x;
            const int top = (std::max)(p.rect.y, r.y) - p.rect.y;
            const int bottom = (std::min)(p.rect.y + p.rect.h, r.y + r.h) - p.rect.y;
            for (int row = top; row < bottom; ++row)
                std::fill(p.owned.begin() + row * p.rect.w + left,
                    p.owned.begin() + row * p.rect.w + right, std::uint8_t(0));
        }
        placements_.erase(std::remove_if(placements_.begin(), placements_.end(),
            [](const Placement& p) {
                return std::find(p.owned.begin(), p.owned.end(), std::uint8_t(1)) == p.owned.end();
            }), placements_.end());
    }
    static bool parseHash(const std::string& text, std::uint64_t& hash) {
        if (text.empty() || text.size() > 16 || text.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos) return false;
        std::istringstream input(text); input >> std::hex >> hash;
        return !input.fail();
    }
    Image* image(const std::string& id) {
        for (auto& value : images_) if (value.id == id) return &value;
        return nullptr;
    }
    static Palette* palette(Image& image, unsigned index) {
        for (auto& value : image.palettes) if (value.index == index) return &value;
        return nullptr;
    }
    bool paletteHash(unsigned x, unsigned y, unsigned count, std::uint64_t& hash) const {
        if (!count || x + count > Width || y >= Height) return false;
        hash = FnvOffset;
        for (unsigned n = 0; n < count; ++n) {
            const std::size_t at = std::size_t(y) * Width + x + n;
            if (!known_[at]) return false;
            const auto value = words_[at];
            hash = (hash ^ (value & 255u)) * FnvPrime;
            hash = (hash ^ (value >> 8)) * FnvPrime;
        }
        return true;
    }

public:
    static std::uint64_t hashBytes(const void* data, std::size_t size) {
        auto bytes = static_cast<const std::uint8_t*>(data);
        std::uint64_t result = FnvOffset;
        for (std::size_t n = 0; n < size; ++n) result = (result ^ bytes[n]) * FnvPrime;
        return result;
    }
    std::size_t imageCount() const { return images_.size(); }
    std::size_t placementCount() const { return placements_.size(); }
    std::uint64_t generation() const { return generation_; }
    std::size_t textureCount() const {
        std::size_t count = 0;
        for (const auto& image : images_) for (const auto& p : image.palettes) if (!p.png.empty()) ++count;
        return count;
    }
    // Snapshot only known shadow words. Unknown words are marked and zeroed;
    // their stale storage is never exposed as verified palette evidence.
    bool inspectPalette(unsigned clut, unsigned bpp, PaletteSnapshot& out) const {
        out = PaletteSnapshot{};
        if (bpp == 16) { out.complete = true; out.hash = FnvOffset; return true; }
        if (bpp != 4 && bpp != 8) return false;
        out.x = (clut & 63) * 16; out.y = clut >> 6;
        out.entries = bpp == 4 ? 16 : 256;
        if (out.x + out.entries > Width || out.y >= Height) return false;
        out.words.resize(out.entries, 0); out.known.resize(out.entries, 0);
        out.complete = true;
        for (unsigned n = 0; n < out.entries; ++n) {
            const std::size_t at = std::size_t(out.y) * Width + out.x + n;
            if (known_[at]) { out.known[n] = 1; out.words[n] = words_[at]; }
            else out.complete = false;
        }
        if (out.complete) return paletteHash(out.x, out.y, out.entries, out.hash);
        return true;
    }

    // Identify a fully owned image region even without any registered/matching
    // palette or PNG. This is evidence collection ONLY: resolve() stays strict.
    bool probe(unsigned tpage, unsigned clut, IdentityProbe& out,
        float minU = 0, float minV = 0, float maxU = 256, float maxV = 256) const {
        out = IdentityProbe{};
        if (!std::isfinite(minU) || !std::isfinite(minV) || !std::isfinite(maxU) || !std::isfinite(maxV) ||
            minU < 0 || minV < 0 || maxU <= minU || maxV <= minV || maxU > 256 || maxV > 256) return false;
        const unsigned mode = (tpage >> 7) & 3;
        if (mode > 2) return false;
        const unsigned bpp = 4u << mode, perWord = 16 / bpp;
        const int pageX = int(tpage & 15) * 64, pageY = int((tpage >> 4) & 1) * 256;
        bool found = false;
        for (const auto& placement : placements_) {
            const auto& value = images_[placement.image];
            if (value.bpp != bpp) continue;
            const float offsetU = float((pageX - placement.rect.x) * int(perWord));
            const float offsetV = float(pageY - placement.rect.y);
            if (offsetU + minU < 0 || offsetV + minV < 0 ||
                offsetU + maxU > unsigned(placement.rect.w) * perWord ||
                offsetV + maxV > unsigned(placement.rect.h)) continue;
            const int left = int(std::floor((offsetU + minU) / perWord));
            const int right = (std::min)(placement.rect.w,
                int(std::floor((offsetU + maxU) / perWord)) + 1);
            const int top = int(std::floor(offsetV + minV));
            const int bottom = (std::min)(placement.rect.h,
                int(std::floor(offsetV + maxV)) + 1);
            bool fullyOwned = true;
            for (int row = top; row < bottom && fullyOwned; ++row)
                for (int col = left; col < right; ++col)
                    if (!placement.owned[std::size_t(row) * placement.rect.w + col]) { fullyOwned = false; break; }
            if (!fullyOwned) continue;
            if (found) {
                if (out.assetId == value.id && out.offsetU == offsetU && out.offsetV == offsetV) continue;
                out = IdentityProbe{}; return false; // Ambiguous owners are not evidence.
            }
            out.assetId = value.id; out.originalW = value.width; out.originalH = value.height;
            out.bpp = bpp; out.imagePayloadHash = value.hash;
            out.offsetU = offsetU; out.offsetV = offsetV;
            out.sourceMinU = offsetU + minU; out.sourceMinV = offsetV + minV;
            out.sourceMaxU = offsetU + maxU; out.sourceMaxV = offsetV + maxV;
            out.placement = { std::int16_t(placement.rect.x), std::int16_t(placement.rect.y),
                std::int16_t(placement.rect.w), std::int16_t(placement.rect.h) };
            out.generation = generation_; found = true;
        }
        if (!found) return false;
        // Invalid/unknown palette does not erase the independently verified image.
        inspectPalette(clut, bpp, out.palette);
        return true;
    }

    void resetVram() {
        std::fill(known_.begin(), known_.end(), std::uint8_t(0));
        placements_.clear(); ++generation_;
    }
    // Failed parsing preserves the previously loaded registry and live owners.
    bool load(const std::string& path, std::string* error = nullptr) {
        std::ifstream input(path);
        if (!input) { if (error) *error = "Cannot open UI manifest: " + path; return false; }
        Registry parsed;
        std::string line;
        unsigned lineNumber = 0;
        auto fail = [&](const char* reason) {
            if (error) *error = "UI manifest line " + std::to_string(lineNumber) + ": " + reason;
            return false;
        };
        while (std::getline(input, line)) {
            ++lineNumber;
            std::istringstream row(line); std::string kind, id, extra, hash;
            if (!(row >> kind) || kind[0] == '#') continue;
            if (!(row >> id)) return fail("missing asset id");
            if (kind == "IMAGE") {
                Image value; value.id = id;
                if (!(row >> value.bpp >> value.width >> value.height >> hash) ||
                    (value.bpp != 4 && value.bpp != 8 && value.bpp != 16) ||
                    !value.width || !value.height || value.width > 4096 || value.height > Height ||
                    !parseHash(hash, value.hash) || parsed.image(id)) return fail("invalid/duplicate IMAGE");
                const unsigned perWord = 16 / value.bpp;
                if (value.width % perWord) return fail("IMAGE pixel width is not word aligned");
                value.wordWidth = value.width / perWord;
                if (value.wordWidth > Width) return fail("IMAGE exceeds VRAM width");
                parsed.images_.push_back(std::move(value));
            } else if (kind == "PALETTE") {
                auto target = parsed.image(id); Palette value;
                if (!target || !(row >> value.index >> value.entries >> hash) ||
                    !parseHash(hash, value.hash) || palette(*target, value.index) ||
                    value.entries != (target->bpp == 4 ? 16u : target->bpp == 8 ? 256u : 0u)) return fail("invalid/duplicate PALETTE");
                target->palettes.push_back(value);
            } else if (kind == "TEXTURE") {
                auto target = parsed.image(id); unsigned index = 0, scale = 0;
                std::string png, stp;
                if (!target || !(row >> index >> scale >> std::quoted(png) >> std::quoted(stp)) ||
                    !scale || scale > 32 || png.empty() || png == "-") return fail("invalid TEXTURE");
                auto selected = palette(*target, index);
                if (!selected || !selected->png.empty()) return fail("TEXTURE palette missing/duplicate");
                selected->scale = scale; selected->png = png; selected->stp = stp == "-" ? "" : stp;
            } else return fail("unknown record type");
            if (row >> extra) return fail("unexpected trailing tokens");
        }
        if (!input.eof()) return fail("read failure");
        if (parsed.images_.empty()) return fail("no images");
        images_ = std::move(parsed.images_); resetVram();
        if (error) error->clear();
        return true;
    }

    void upload(const Rect16& originalRect, const void* data) {
        const Rect r = clipped(rect(originalRect));
        if (!valid(r)) return;
        invalidate(r); ++generation_;
        if (!data) {
            for (int y = 0; y < r.h; ++y) std::fill_n(known_.begin() + (r.y + y) * Width + r.x, r.w, std::uint8_t(0));
            return;
        }
        const auto bytes = static_cast<const std::uint8_t*>(data);
        // Native 40B1B0 rounds its source row advance down to a DWORD boundary.
        // All 58 extracted UI TIMs have even VRAM word widths.
        const std::size_t stride = (std::size_t(r.w) * 2) & ~std::size_t(3);
        for (int y = 0; y < r.h; ++y) {
            const std::size_t dest = std::size_t(r.y + y) * Width + r.x;
            std::memcpy(&words_[dest], bytes + std::size_t(y) * stride, std::size_t(r.w) * 2);
            std::fill_n(known_.begin() + dest, r.w, std::uint8_t(1));
        }
        // Inventory STMAIN TIMs are 272 rows tall but are commonly placed at
        // Y=256: native VRAM clips their final 16 rows. Identify the original
        // input payload, then claim only the actually copied visible extent.
        // Width clipping changes native source stride, so leave it unclaimed.
        if (r.w != originalRect.w || originalRect.h <= 0 || (r.w & 1)) return;
        bool possible = false;
        for (const auto& value : images_) if (value.wordWidth == unsigned(originalRect.w) && value.height == unsigned(originalRect.h)) { possible = true; break; }
        if (!possible) return;
        const auto hash = hashBytes(data, std::size_t(originalRect.w) * originalRect.h * 2);
        for (std::size_t n = 0; n < images_.size(); ++n) {
            const auto& value = images_[n];
            if (value.wordWidth == unsigned(originalRect.w) && value.height == unsigned(originalRect.h) && value.hash == hash)
                placements_.push_back({n, r, std::vector<std::uint8_t>(std::size_t(r.w) * r.h, 1)});
        }
    }

    void clear(const Rect16& originalRect) {
        const Rect r = clipped(rect(originalRect));
        if (!valid(r)) return;
        invalidate(r); ++generation_;
        for (int y = 0; y < r.h; ++y) {
            const std::size_t dest = std::size_t(r.y + y) * Width + r.x;
            std::fill_n(words_.begin() + dest, r.w, std::uint16_t(0));
            std::fill_n(known_.begin() + dest, r.w, std::uint8_t(1));
        }
    }

    void move(const Rect16& sourceRect, int x, int y) {
        const Rect src = rect(sourceRect), dest{x, y, src.w, src.h};
        if (!valid(src) || !valid(dest)) { resetVram(); return; }
        // Snapshot handles overlapping moves while retaining partial palette
        // knowledge. Only fully copied known images retain their identity.
        std::vector<std::uint16_t> copied(std::size_t(src.w) * src.h);
        std::vector<std::uint8_t> known(copied.size());
        std::vector<Placement> moved;
        for (const auto& p : placements_) if (contains(src, p.rect)) {
            moved.push_back(p);
            moved.back().rect.x += x - src.x;
            moved.back().rect.y += y - src.y;
        }
        for (int row = 0; row < src.h; ++row) {
            const std::size_t from = std::size_t(src.y + row) * Width + src.x, to = std::size_t(row) * src.w;
            std::copy_n(words_.begin() + from, src.w, copied.begin() + to);
            std::copy_n(known_.begin() + from, src.w, known.begin() + to);
        }
        invalidate(dest); ++generation_;
        for (int row = 0; row < dest.h; ++row) {
            const std::size_t from = std::size_t(row) * src.w, to = std::size_t(dest.y + row) * Width + dest.x;
            std::copy_n(copied.begin() + from, dest.w, words_.begin() + to);
            std::copy_n(known.begin() + from, dest.w, known_.begin() + to);
        }
        placements_.insert(placements_.end(), moved.begin(), moved.end());
    }

    // UV bounds are in the unadjusted native page coordinates. Passing actual
    // primitive bounds is necessary for sub-page images and shared VRAM pages.
    // Returned offsets map those page coordinates into the original TIM atlas;
    // caller adds the verified primitive UV bias, then divides by originalW/H.
    bool resolve(unsigned tpage, unsigned clut, Binding& out,
        float minU = 0, float minV = 0, float maxU = 256, float maxV = 256) const {
        out = Binding{};
        if (!std::isfinite(minU) || !std::isfinite(minV) || !std::isfinite(maxU) || !std::isfinite(maxV) ||
            minU < 0 || minV < 0 || maxU <= minU || maxV <= minV || maxU > 256 || maxV > 256) return false;
        const unsigned mode = (tpage >> 7) & 3;
        if (mode > 2) return false;
        const unsigned bpp = 4u << mode, perWord = 16 / bpp;
        const int pageX = int(tpage & 15) * 64, pageY = int((tpage >> 4) & 1) * 256;
        std::uint64_t hash = 0;
        if (bpp != 16 && !paletteHash((clut & 63) * 16, clut >> 6, bpp == 4 ? 16 : 256, hash)) return false;
        bool found = false;
        for (const auto& placement : placements_) {
            const auto& value = images_[placement.image];
            if (value.bpp != bpp) continue;
            const float offsetU = float((pageX - placement.rect.x) * int(perWord));
            const float offsetV = float(pageY - placement.rect.y);
            if (offsetU + minU < 0 || offsetV + minV < 0 ||
                offsetU + maxU > unsigned(placement.rect.w) * perWord ||
                offsetV + maxV > unsigned(placement.rect.h)) continue;
            const int left = int(std::floor((offsetU + minU) / perWord));
            const int right = (std::min)(placement.rect.w,
                int(std::floor((offsetU + maxU) / perWord)) + 1);
            const int top = int(std::floor(offsetV + minV));
            const int bottom = (std::min)(placement.rect.h,
                int(std::floor(offsetV + maxV)) + 1);
            bool fullyOwned = true;
            for (int row = top; row < bottom && fullyOwned; ++row)
                for (int col = left; col < right; ++col)
                    if (!placement.owned[std::size_t(row) * placement.rect.w + col]) { fullyOwned = false; break; }
            if (!fullyOwned) continue;
            const Palette* selected = nullptr;
            for (const auto& p : value.palettes) if (!p.png.empty() && (bpp == 16 || p.hash == hash)) { selected = &p; break; }
            if (!selected) continue;
            if (found) {
                if (out.assetId == value.id && out.offsetU == offsetU && out.offsetV == offsetV) continue;
                out = Binding{}; return false; // An ambiguous owner must remain original.
            }
            out.assetId = value.id; out.pngPath = selected->png; out.stpPath = selected->stp;
            out.originalW = value.width; out.originalH = value.height;
            out.paletteIndex = selected->index; out.scale = selected->scale;
            out.offsetU = offsetU; out.offsetV = offsetV; out.generation = generation_;
            found = true;
        }
        return found;
    }
};

} // namespace re3ui
