#pragma once
// Glock 18C, a Mercenaries reward for the main game (round 2026-10-28 glock18c; v2 the same evening: AUTO hits every
// round (M4A1 AUTO hit cooldown), softened AUTO recoil, its own shot sound - see the sections below). Static facts, exact GOG EXEs; addresses
// are ResidentEvil3.exe RVAs (gameBase+RVA, translated for RE3_MERCE.exe by exe_map.h):
// Items 53 (single fire) / 54 (auto) were unused placeholders ("BOTU", SIGPRO picture). Attribute table 52AC50, 4 bytes
//   per id {type 1=weapon, capacity, alternate-name flag, default slot flags}: -> {1,19,0,11} / {1,19,0,15}. Slot flags 0x10
//   give the 4th inventory action (state 9 = 530D40 -> 4DBEF0, hard-wired M4A1 0E/0F) whose AUTO/MANUAL word follows slot
//   bit 0x100 (4DDF57); bit 0x100 set = single (like the M4A1 MANUAL id 0E). Slot flags &3: 1 = one round per shot, 3 = infinite.
// Names: 42F290 (5 calls) -> 51AF10+word[51B7EC+2*id], 0xF7 ends a name. Descriptions: system message 0x10+id, shown by
//   the Check state at 4DBDA3 (42DA10 mode 0x1100), text pointer stored to A57470.
// Pictures: ITEMI.PIX/ITEMG.PIX records read by 4265A0 at 4DCC11/4DCC7E (id<<11, 0x800) and 4DCDF1 (id*0x2800);
//   ITEMA.SLD icons 4DCCE0 (45DC31, 45DDE8; rect 530C68[slot]) and 4DCD50 (45E125, 4E4B42, 4E4C95; rect 531900[slot&7]);
//   icons use the shared inventory CLUT (STMAIN0U palette 1), 40x30 8bpp uploaded by 423200.
// Combine 52AF00 ({type,a,b,result,..} 8 bytes, 4DCB50) and auto reload weapon->ammo 52B300 (4 bytes, 4DCBC0, called by
//   4DEB60 with the equipped item A623E4[+129]): relocated copies with the Glock rows (reload with handgun bullets 15,
//   type 6 with the infinite-bullets case 6E like the other guns). Reload fills up to the slot item's capacity (4DEBD4).
// Weapon: player+46 must stay a weapon id <= 14 (52A3AC/52A35C/529BC8/52A43B.. and the enemies' per-weapon tables), so the
//   equipped Glock loads as M92F 03 (alias in front of 452620/452310, the only loaders after player+46 is set): same
//   PLW, damage, sounds, casings. One more writer: the SCD weapon restore 46BE00 (end of a scene that only hid the weapon,
//   e.g. the Rusted Crank shutter R20E) puts the saved item id A35F08 into player+46 and, while A4CFC9 == 0, calls
//   452690 = 529CF0[player+46] at 46BE9B without a reload -> 529CF0[53] is 0 (1.11 crash, EIP 0): aliased there too.
//   Fire states (52A35C[03] = 464340 -> 52A508[player+6]): 1 = aim (fire = held bit 0x40 of
//   A5C864, 4th argument), 2 = shot (sub-state +7: 0 starts the shot animation, the shot happens at frame +C9 == 0).
//   Single: no fire bit until the trigger is released after a shot. Auto: state 2 restarts at frame >= N while fire and aim
//   are held and a round is left (4DEC10(0) peek): N=2 -> 15 shots/s at the 30 Hz logic.
// New game 45D750 (called 451380/451FF8): boxes cleared and refilled; flags 6/7/8 put rewards into box slots 29-31 of both
//   character blocks (box = A6218C + block*0x140) for modes 0/1 only; flag 9 = infinite ammo (slot flags |3).
// Purchases: system flags A61F60 (6..9 native, 0B = Glock here); data.sav Data01..04 hold them (426380 load at 443639/
//   45137B, 4262A0 save at 42716B/442E13), Data05 = 0B is ours (read 405FC0, write 406210).
// Shop (same code in both exes; RE3_MERCE: title RESULT, and the end of a stage): handler 4447E0, cursor ctx(A456C0)+16
//   0..4 (4 = EXIT), prices word 5266B4, flags byte 5266BC, descriptions 526204+word[52634A]; sprites A44F90+i*0x50 (EXIT
//   A450D0, cursor A45120), SOLD OUT stamps A453F0+i*0x50, layouts 526454.. (title) and 52660C.. (results). The Glock is
//   cursor 4 (its stamp A45530 = the native index formula, a free object), EXIT moves to 5; one row of 6 cells 52 px apart.
//   Shop picture: RES0_OBU.TIM (loaded at 4447B5/44588D/445A35, uploaded by 426650) gets the native Glock cell at x312 y160
//   (u56 v160 of the shop texture page) before the upload; ui-pack registers the HD atlas for that payload.
#include <algorithm>
#include <iterator>
namespace glock {
using re3bridge::detail::Patch;
using re3bridge::detail::Read;
using re3bridge::detail::Accessible;
static constexpr uint8_t kSingle = 0x53, kAuto = 0x54, kFlag = 0x0b, kRounds = 19;
static constexpr int kDataIndex = 5;
static bool itemsInstalled = false, shopInstalled = false, shopObjectsReady = false;
static unsigned autoFrames = 2;
static std::vector<uint8_t> itemiRecord, itemgRecord, shopCell;
static std::vector<Patch> patches;

static std::vector<uint8_t> encode(const char* s, std::initializer_list<uint8_t> tail) {
    std::vector<uint8_t> out;
    for (; *s; ++s) {
        const char c = *s;
        if (c >= 'A' && c <= 'Z') out.push_back(uint8_t(0x1d + c - 'A'));
        else if (c >= 'a' && c <= 'z') out.push_back(uint8_t(0x3d + c - 'a'));
        else if (c >= '0' && c <= '9') out.push_back(uint8_t(0x0c + c - '0'));
        else if (c == ' ') out.push_back(0);
        else if (c == '.') out.push_back(1);
        else if (c == '\'') out.push_back(0x3a);
        else if (c == '-') out.push_back(0x3b);
        else if (c == '?') out.push_back(0x1b);
        else if (c == '\n') out.push_back(0xfc);
        else if (c == '|') { out.push_back(0xfd); out.push_back(0); }   // next page after a button
    }
    out.insert(out.end(), tail);
    return out;
}
static const std::vector<uint8_t> nameSingle = encode("Glock 18C", {0xf7});
static const std::vector<uint8_t> nameAuto = encode("Glock 18C Auto", {0xf7});
static const std::vector<uint8_t> descSingle = encode("Glock 18C.\nThe gun is set in\nMANUAL mode.|Its magazine holds\n19 rounds of 9mm\nParabellum.\n", {0xfe, 0});
static const std::vector<uint8_t> descAuto = encode("Glock 18C.\nThe gun is set in\nAUTO mode.|Its magazine holds\n19 rounds of 9mm\nParabellum.\n", {0xfe, 0});
static const std::vector<uint8_t> shopText = [] {
    std::vector<uint8_t> t{0xfa, 0}; const auto body = encode("    A Glock 18C automatic \n    handgun for RE3.", {0xfe, 0});
    t.insert(t.end(), body.begin(), body.end()); return t; }();

inline uint32_t* systemFlags() { return reinterpret_cast<uint32_t*>(gameBase + 0x661f60); }
inline bool testFlag(const uint32_t* group, unsigned n) { return (group[n >> 5] & (0x80000000u >> (n & 31))) != 0; }
inline void setFlag(uint32_t* group, unsigned n) { group[n >> 5] |= 0x80000000u >> (n & 31); }
inline bool isGlock(unsigned id) { return id == kSingle || id == kAuto; }
inline uint8_t* inventory() {
    auto inv = Read<uint8_t*>(reinterpret_cast<void*>(gameBase + 0x6623e4));
    return Accessible(inv, 0x12b, true) ? inv : nullptr;
}
inline uint8_t equipped() {
    if (!itemsInstalled) return 0;
    const auto inv = inventory();
    return inv && inv[0x128] != 0xff && isGlock(inv[0x129]) ? inv[0x129] : 0;
}

static bool callPatch(uint32_t site, uint32_t target, void* hook) {
    Patch p; if (!re3bridge::detail::BuildCallPatch(p, gameBase + site, gameBase + target, hook)) { log("GLOCK_PATCH_MISMATCH call %x -> %x", site, target); return false; }
    patches.push_back(p); return true;
}
static bool slotPatch(uint32_t slot, uint32_t expected, void* hook) {
    Patch p; if (!re3bridge::detail::BuildSlotPatch(p, gameBase + slot, reinterpret_cast<void*>(gameBase + expected), hook)) { log("GLOCK_PATCH_MISMATCH slot %x", slot); return false; }
    patches.push_back(p); return true;
}
// imm32/disp32 operand inside an instruction: replaced only when it holds the expected absolute address.
static bool operandPatch(uint32_t site, uint32_t expected, const void* replacement) {
    const auto at = reinterpret_cast<uint8_t*>(gameBase + site); Patch p;
    if (!Accessible(at, 4) || Read<uint32_t>(at) != expected) { log("GLOCK_PATCH_MISMATCH operand %x", site); return false; }
    const uint32_t value = uint32_t(reinterpret_cast<uintptr_t>(replacement));
    p.address = at; p.size = 4; std::memcpy(p.original.data(), &expected, 4); std::memcpy(p.replacement.data(), &value, 4);
    patches.push_back(p); return true;
}
// up to 5 code bytes, guarded by the exact original bytes
static bool bytesPatch(uint32_t site, std::initializer_list<uint8_t> original, std::initializer_list<uint8_t> replacement) {
    const auto at = reinterpret_cast<uint8_t*>(gameBase + site); Patch p;
    if (original.size() != replacement.size() || original.size() > 5 || !Accessible(at, original.size()) ||
        !std::equal(original.begin(), original.end(), at)) { log("GLOCK_PATCH_MISMATCH bytes %x", site); return false; }
    p.address = at; p.size = original.size(); std::copy(original.begin(), original.end(), p.original.begin());
    std::copy(replacement.begin(), replacement.end(), p.replacement.begin());
    patches.push_back(p); return true;
}
// installs patches[from..] all or nothing
static bool writeFrom(size_t from) {
    size_t done = from;
    for (; done < patches.size(); ++done) if (!re3bridge::detail::WritePatch(patches[done], true)) break;
    if (done == patches.size()) return true;
    while (done > from) re3bridge::detail::WritePatch(patches[--done], false);
    patches.resize(from); return false;
}
static bool writeData(uintptr_t at, const void* value, size_t bytes, const void* expected) {
    DWORD protection = 0, ignored = 0; auto p = reinterpret_cast<void*>(at);
    if (!Accessible(p, bytes) || (expected && std::memcmp(p, expected, bytes))) return false;
    if (!VirtualProtect(p, bytes, PAGE_READWRITE, &protection)) return false;
    std::memcpy(p, value, bytes); VirtualProtect(p, bytes, protection, &ignored); return true;
}
static std::vector<uint8_t> readFile(const std::string& path, size_t size) {
    std::ifstream in(path, std::ios::binary); std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (data.size() != size) data.clear();
    return data;
}
static uint64_t fnv(const uint8_t* p, size_t n) { uint64_t h = 0xcbf29ce484222325ull; for (size_t i = 0; i < n; ++i) h = (h ^ p[i]) * 0x100000001b3ull; return h; }

// ---------------------------------------------------------------- data.sav (both exes)
using VoidFn = void(__cdecl*)();
static VoidFn savLoadOriginal = nullptr, savSaveOriginal = nullptr;
static void __cdecl savLoadHook() {
    savLoadOriginal();
    int value = -1;
    const int found = reinterpret_cast<int(__cdecl*)(int, int*)>(gameBase + 0x5fc0)(kDataIndex, &value);
    if (found && value == kFlag) setFlag(systemFlags(), kFlag);
    static unsigned logged = 0; if (logged++ < 4) log("GLOCK_DATA load Data%02d=%d bought=%u", kDataIndex, found ? value : -1, unsigned(testFlag(systemFlags(), kFlag)));
}
static void __cdecl savSaveHook() {
    savSaveOriginal();
    if (testFlag(systemFlags(), kFlag)) { reinterpret_cast<void(__cdecl*)(int, int)>(gameBase + 0x6210)(kDataIndex, kFlag); log("GLOCK_DATA saved Data%02d=%d", kDataIndex, kFlag); }
}

// ---------------------------------------------------------------- shop (both exes)
static int16_t shopPrices[6] = {2000, 3000, 4000, 9999, 500, 0};
static int8_t shopFlags[6] = {8, 7, 6, 9, int8_t(kFlag), 0};
static uint8_t shopPictureDesc[14], shopStampDesc[14];
inline uint8_t* gameObject(uint32_t rva) { return reinterpret_cast<uint8_t*>(gameBase + rva); }
inline uint8_t* exitObject() { return gameObject(0x6450d0); }
inline uint8_t* item3Object() { return gameObject(0x645080); }
inline uint8_t* stamp3Object() { return gameObject(0x6454e0); }
inline uint8_t* glockStamp() { return gameObject(0x645530); }     // A453F0 + 4*0x50: the native SOLD OUT formula
inline uint8_t* glockPicture() { return gameObject(0x645580); }   // next free object (nothing references A45530..A456BF)
using ShopInitFn = int(__cdecl*)(uint8_t*, const uint8_t*, uint32_t, uint32_t);
using ShopDrawFn = int(__cdecl*)(uint8_t*, int);
using ShopDarkenFn = void(__cdecl*)(uint8_t*, uint32_t);
using MessageFn = int(__cdecl*)(int, int, uintptr_t, int);
using UploadTimFn = int(__cdecl*)(uint8_t*);
static ShopInitFn shopInitOriginal = nullptr; static ShopDrawFn shopDrawOriginal = nullptr; static ShopDarkenFn shopDarkenOriginal = nullptr;
static MessageFn shopMessageOriginal = nullptr; static UploadTimFn timUploadOriginal = nullptr;
static int __cdecl shopInitHook(uint8_t* object, const uint8_t* desc, uint32_t page, uint32_t colour) {
    const int result = shopInitOriginal(object, desc, page, colour);
    if (object != exitObject() || !Accessible(desc, 14)) return result;
    // EXIT is set up right after item 3 in all three layouts: same row y; the Glock cell is the 5th of the row
    const int16_t y = Read<int16_t>(desc, 2), pictureX = 4 + 52 * 4, stampX = pictureX + 3, stampY = int16_t(y - 4), h = 48;
    std::memcpy(shopPictureDesc, desc, 14); std::memcpy(shopPictureDesc, &pictureX, 2); std::memcpy(shopPictureDesc + 6, &h, 2);
    shopPictureDesc[8] = 56; shopPictureDesc[9] = 160;
    std::memcpy(shopStampDesc, shopPictureDesc, 14); std::memcpy(shopStampDesc, &stampX, 2); std::memcpy(shopStampDesc + 2, &stampY, 2);
    shopStampDesc[8] = 0; shopStampDesc[9] = 132;
    shopInitOriginal(glockPicture(), shopPictureDesc, page, colour);
    shopInitOriginal(glockStamp(), shopStampDesc, page, colour);
    glockPicture()[0] &= 0xfe; glockStamp()[0] &= 0xfe;
    const bool bought = testFlag(systemFlags(), kFlag);
    if (bought) { shopDarkenOriginal(glockPicture(), 0x404040); glockStamp()[0] |= 1; }
    shopObjectsReady = true;
    static unsigned logged = 0; if (logged++ < 8) log("GLOCK_SHOP cell y=%d bought=%u money=%u", y, unsigned(bought), Read<uint32_t>(gameObject(0x66261c)));
    return result;
}
static int __cdecl shopDrawHook(uint8_t* object, int priority) {
    const int result = shopDrawOriginal(object, priority);
    if (!shopObjectsReady) return result;
    if (object == item3Object()) { glockPicture()[0] = uint8_t((glockPicture()[0] & 0xfe) | (object[0] & 1)); shopDrawOriginal(glockPicture(), priority); }
    else if (object == stamp3Object()) shopDrawOriginal(glockStamp(), priority);
    return result;
}
static void __cdecl shopDarkenHook(uint8_t* object, uint32_t colour) {
    // purchase of cursor 4: the native formula A44F90+4*0x50 is the EXIT sprite
    if (object == exitObject()) { if (shopObjectsReady) shopDarkenOriginal(glockPicture(), colour); log("GLOCK_SHOP bought money=%u", Read<uint32_t>(gameObject(0x66261c))); return; }
    shopDarkenOriginal(object, colour);
}
static int __cdecl shopMessageHook(int a, int mode, uintptr_t text, int d) {
    const uint8_t cursor = *gameObject(0x6456d6);   // shop context A456C0 +16
    const auto base = gameBase + 0x126204; const auto table = reinterpret_cast<const uint16_t*>(gameBase + 0x12634a);
    if (cursor == 4 && text == base + table[4]) text = reinterpret_cast<uintptr_t>(shopText.data());
    else if (cursor == 5 && text == base + table[5]) text = base + table[4];
    return shopMessageOriginal(a, mode, text, d);
}
static int __cdecl timUploadHook(uint8_t* tim) {
    // RES0_OBU.TIM straight from the file: 8bpp, CLUT block 256x3 at (0,480), 384x256 pixels at (0,0)
    static unsigned patched = 0, refused = 0;
    if (shopCell.size() == 56 * 48 && Accessible(tim, 8 + 1548 + 12 + 384 * 256, true) && Read<uint32_t>(tim, 0) == 0x10 &&
        Read<uint32_t>(tim, 4) == 9 && Read<uint32_t>(tim, 8) == 1548 && Read<uint32_t>(tim, 8 + 1548) == 98316 &&
        Read<uint16_t>(tim, 8 + 1548 + 8) == 192 && Read<uint16_t>(tim, 8 + 1548 + 10) == 256) {
        uint8_t* pixels = tim + 8 + 1548 + 12;
        const uint64_t before = fnv(pixels, 384 * 256);
        if (before == 0x39d1c832b4e15373ull) {
            for (int y = 0; y < 48; ++y) std::memcpy(pixels + (160 + y) * 384 + 312, shopCell.data() + y * 56, 56);
            if (patched++ < 4) log("GLOCK_SHOP RES0_OBU cell written payload=%016llx", static_cast<unsigned long long>(fnv(pixels, 384 * 256)));
        } else if (refused++ < 4) log("GLOCK_SHOP RES0_OBU payload %016llx is not the original, cell not written", static_cast<unsigned long long>(before));
    }
    return timUploadOriginal(tim);
}
static void installShop() {
    // descriptors: one row of 6 cells, item x = 4 + 52*i (cursor -2, stamp +3), EXIT = cell 5; title layout 526454..,
    // results layout 52660C..
    struct Row { uint32_t items[4], exit, cursor, stamps[4]; } rows[2] = {
        {{0x126454, 0x126462, 0x126470, 0x12647e}, 0x12648c, 0x12649a, {0x126526, 0x126534, 0x126542, 0x126550}},
        {{0x12660c, 0x12661a, 0x126628, 0x126636}, 0x126644, 0x126652, {0x12667c, 0x12668a, 0x126698, 0x1266a6}}};
    auto x = [](uint32_t rva) { return Read<int16_t>(gameObject(rva)); };
    for (const auto& row : rows) {
        bool ok = x(row.exit) == 246 && x(row.cursor) == 20;
        for (int i = 0; i < 4 && ok; ++i) ok = x(row.items[i]) == 22 + 56 * i && x(row.stamps[i]) == 25 + 56 * i;
        if (!ok) { log("GLOCK_SHOP layout signature mismatch, the shop stays native"); return; }
    }
    const size_t from = patches.size();
    bool ok = true;
    for (uint32_t site : {0x447d3u, 0x458abu, 0x45a53u}) ok = ok && callPatch(site, 0x26650, reinterpret_cast<void*>(timUploadHook));
    for (uint32_t site : {0x4397au, 0x44333u, 0x4516eu}) ok = ok && callPatch(site, 0x48970, reinterpret_cast<void*>(shopInitHook));
    for (uint32_t site : {0x43579u, 0x43d2cu, 0x44c33u, 0x435ccu, 0x43da7u, 0x44caeu}) ok = ok && callPatch(site, 0x45ef0, reinterpret_cast<void*>(shopDrawHook));
    ok = ok && callPatch(0x44a8e, 0x46060, reinterpret_cast<void*>(shopDarkenHook));
    for (uint32_t site : {0x4481du, 0x44866u, 0x448e4u}) ok = ok && callPatch(site, 0x2da10, reinterpret_cast<void*>(shopMessageHook));
    ok = ok && operandPatch(0x449cf, 0x5266b4, shopPrices) && operandPatch(0x44a5a, 0x5266b4, shopPrices) &&
        operandPatch(0x44978, 0x5266bc, shopFlags) && operandPatch(0x44a69, 0x5266bc, shopFlags);
    // cursor 0..5: wrap left to 5, right after 5, EXIT = 5
    ok = ok && bytesPatch(0x4483d, {0xc6, 0x46, 0x16, 0x04}, {0xc6, 0x46, 0x16, 0x05}) && bytesPatch(0x448b7, {0x3c, 0x04}, {0x3c, 0x05}) &&
        bytesPatch(0x44952, {0x3c, 0x04}, {0x3c, 0x05});
    // cursor x = idx*56+20 -> idx*52+2: lea r,[eax*8]; push ebx; sub r,eax ... lea s,[r*8+0x14] -> lea r,[eax+eax*2]; lea r,[eax+r*4]; push ebx; nop*3 ... lea s,[r*4+2]
    ok = ok && bytesPatch(0x44872, {0x8d, 0x0c, 0xc5, 0x00, 0x00}, {0x8d, 0x0c, 0x40, 0x8d, 0x0c}) && bytesPatch(0x44877, {0x00, 0x00, 0x53, 0x2b, 0xc8}, {0x88, 0x53, 0x90, 0x90, 0x90}) &&
        bytesPatch(0x44880, {0xcd, 0x14}, {0x8d, 0x02}) &&
        bytesPatch(0x448f0, {0x8d, 0x14, 0xc5, 0x00, 0x00}, {0x8d, 0x14, 0x40, 0x8d, 0x14}) && bytesPatch(0x448f5, {0x00, 0x00, 0x53, 0x2b, 0xd0}, {0x90, 0x53, 0x90, 0x90, 0x90}) &&
        bytesPatch(0x448fe, {0xd5, 0x14}, {0x95, 0x02});
    if (!ok) { patches.resize(from); log("GLOCK_SHOP hooks skipped: signature mismatch, the shop stays native"); return; }
    shopInitOriginal = reinterpret_cast<ShopInitFn>(gameBase + 0x48970); shopDrawOriginal = reinterpret_cast<ShopDrawFn>(gameBase + 0x45ef0);
    shopDarkenOriginal = reinterpret_cast<ShopDarkenFn>(gameBase + 0x46060); shopMessageOriginal = reinterpret_cast<MessageFn>(gameBase + 0x2da10);
    timUploadOriginal = reinterpret_cast<UploadTimFn>(gameBase + 0x26650);
    if (!writeFrom(from)) { log("GLOCK_SHOP hooks failed: restored"); return; }
    for (const auto& row : rows) {
        for (int i = 0; i < 4; ++i) {
            const int16_t item = int16_t(4 + 52 * i), stamp = int16_t(7 + 52 * i);
            writeData(gameBase + row.items[i], &item, 2, nullptr); writeData(gameBase + row.stamps[i], &stamp, 2, nullptr);
        }
        const int16_t exitX = 4 + 52 * 5, cursorX = 2;
        writeData(gameBase + row.exit, &exitX, 2, nullptr); writeData(gameBase + row.cursor, &cursorX, 2, nullptr);
    }
    shopInstalled = true;
    log("GLOCK_SHOP installed: 6 cells (Glock $%d, flag %02x, Data%02d), picture cell %s", shopPrices[4], kFlag, kDataIndex, shopCell.empty() ? "MISSING (native cell stays empty)" : "loaded");
}

// ---------------------------------------------------------------- items (main game)
static uint8_t combineTable[0x480], reloadTable[0x50];
using NameFn = const uint8_t*(__cdecl*)(int);
using ReadFn = int(__cdecl*)(const char*, void*, uint32_t, uint32_t);
using IconFn = void(__cdecl*)(int, int, uint32_t, void*);
using PlayerFn = int(__cdecl*)(uint8_t*);
using WeaponStateFn = void(__cdecl*)(uint8_t*, uint32_t, uint32_t, uint32_t);
using MenuStateFn = void(__cdecl*)(uint8_t*);
static NameFn nameOriginal = nullptr; static MessageFn checkMessageOriginal = nullptr; static ReadFn readOriginal = nullptr;
static IconFn slotIconOriginal = nullptr, boxIconOriginal = nullptr; static PlayerFn weaponInitOriginal = nullptr, weaponLoadOriginal = nullptr, weaponResumeOriginal = nullptr;
static WeaponStateFn weaponStateOriginal = nullptr; static MenuStateFn modeSwitchOriginal = nullptr; static VoidFn newGameOriginal = nullptr;
static const uint8_t* __cdecl nameHook(int id) {
    if (uint8_t(id) == kSingle) return nameSingle.data();
    if (uint8_t(id) == kAuto) return nameAuto.data();
    return nameOriginal(id);
}
static int __cdecl checkMessageHook(int a, int mode, uintptr_t id, int d) {
    const int result = checkMessageOriginal(a, mode, id, d);
    if (result == 0 && (mode & 0x3000) == 0x1000 && (id == 0x10u + kSingle || id == 0x10u + kAuto))
        *reinterpret_cast<const uint8_t**>(gameBase + 0x657470) = id == 0x10u + kSingle ? descSingle.data() : descAuto.data();
    return result;
}
static int __cdecl readHook(const char* name, void* buffer, uint32_t offset, uint32_t size) {
    const auto n = reinterpret_cast<uintptr_t>(name);
    if (n == gameBase + 0x130dc8 && size == 0x800 && itemiRecord.size() == 0x800 && (offset == uint32_t(kSingle) << 11 || offset == uint32_t(kAuto) << 11)) {
        std::memcpy(buffer, itemiRecord.data(), 0x800); return 0x800; }
    if (n == gameBase + 0x130de4 && size == 0x2800 && itemgRecord.size() == 0x2800 && (offset == kSingle * 0x2800u || offset == kAuto * 0x2800u)) {
        std::memcpy(buffer, itemgRecord.data(), 0x2800); return 0x2800; }
    return readOriginal(name, buffer, offset, size);
}
static void loadIconImage(const int16_t* xy, const void* pixels) {   // 40x30 8bpp = 0x14 x 0x1e VRAM words, 423200 LoadImage
    int16_t rect[4] = {xy[0], xy[1], 0x14, 0x1e};
    reinterpret_cast<int(__cdecl*)(void*, const void*)>(gameBase + 0x23200)(rect, pixels);
}
static void uploadIcon(const int16_t* xy, void* buffer) { std::memcpy(buffer, itemiRecord.data(), 40 * 30); loadIconImage(xy, buffer); }
static void __cdecl slotIconHook(int slot, int id, uint32_t base, void* buffer) {
    if (isGlock(uint8_t(id)) && itemiRecord.size() == 0x800) { uploadIcon(reinterpret_cast<const int16_t*>(gameBase + 0x130c68) + (slot & 0xff) * 2, buffer); return; }
    slotIconOriginal(slot, id, base, buffer);
}
static void __cdecl boxIconHook(int slot, int id, uint32_t base, void* buffer) {
    if (isGlock(uint8_t(id)) && itemiRecord.size() == 0x800) { uploadIcon(reinterpret_cast<const int16_t*>(gameBase + 0x131900) + ((slot & 0xff) % 8) * 2, buffer); return; }
    boxIconOriginal(slot, id, base, buffer);
}
static void alias(uint8_t* player) {
    if (!Accessible(player, 0x48, true) || !isGlock(player[0x46])) return;
    static unsigned logged = 0; if (logged++ < 16) log("GLOCK_WEAPON item %02x loads as M92F 03", player[0x46]);
    player[0x46] = 3;
}
static int __cdecl weaponInitHook(uint8_t* player) { alias(player); return weaponInitOriginal(player); }
static int __cdecl weaponLoadHook(uint8_t* player) { alias(player); return weaponLoadOriginal(player); }
static int __cdecl weaponResumeHook(uint8_t* player) {
    static unsigned logged = 0;
    if (Accessible(player, 0x48, true) && isGlock(player[0x46]) && logged++ < 16) log("GLOCK_WEAPON scene restore (46BE9B) item %02x resumes as M92F 03", player[0x46]);
    alias(player); return weaponResumeOriginal(player);
}
static bool debugWarp() { static const bool on = GetPrivateProfileIntA("pilot", "debug_warp", 0, (root + "\\pilot.ini").c_str()) != 0; return on; }

// ---------------------------------------------------------------- hits (AUTO)
// Enemy shot test 452DB0(muzzle, combat = 529BC8[player+4A] (all 529948), weapon), called by the handgun shot 465E30 at
// 465E6E, replaces its combat argument by combat + (weapon-1)*0x20 (20 range blocks up to 529BC8); block+1C -> 529888[enemy
// type - 10] -> 8-byte records per weapon (1..14) {dword 3 x 10-bit damage, word 3 x 5-bit
// cooldown -> enemy+12D, word reaction}. An enemy with +12D != 0 is skipped by the test, so with the M92F's 5 frames most
// rounds of a Glock AUTO burst (one every 2 frames) passed through: no hit, no blood. Glock shots (AUTO, and since 1.11
// MANUAL, which can fire as fast) use a copy of the tables
// whose weapon 03 records take the cooldown word of the M4A1 AUTO 0F (2 frames on zombies, 5 on the big ones, the game's
// own automatic-fire rule); damage and reactions stay the M92F's.
static constexpr unsigned kEnemyTypes = 48, kWeaponRecords = 0x14, kCombatBytes = kWeaponRecords * 0x20;
alignas(4) static uint8_t autoCombat[kCombatBytes];
static const uint8_t* autoTypes[kEnemyTypes];
static uint8_t autoRecords[kEnemyTypes][kWeaponRecords * 8];
static bool autoCombatReady = false;
static void buildAutoCombat() {
    const uint8_t* combat = gameObject(0x129948);
    if (!Accessible(combat, kCombatBytes) || Read<uintptr_t>(combat, (3 - 1) * 0x20 + 0x1c) != gameBase + 0x129888) { log("GLOCK_HITS combat table signature mismatch, AUTO keeps the M92F cooldown"); return; }
    std::memcpy(autoCombat, combat, sizeof(autoCombat));
    const auto types = reinterpret_cast<const uint8_t* const*>(gameBase + 0x129888);
    unsigned copied = 0, before = 0;
    for (unsigned i = 0; i < kEnemyTypes; ++i) {
        autoTypes[i] = types[i];
        if (!Accessible(types[i], sizeof(autoRecords[i]))) continue;
        std::memcpy(autoRecords[i], types[i], sizeof(autoRecords[i]));
        if (!copied) before = autoRecords[i][(3 - 1) * 8 + 4] & 31u;
        std::memcpy(autoRecords[i] + (3 - 1) * 8 + 4, types[i] + (0x0f - 1) * 8 + 4, 2);
        autoTypes[i] = autoRecords[i]; ++copied;
    }
    const auto table = autoTypes; std::memcpy(autoCombat + (3 - 1) * 0x20 + 0x1c, &table, 4);   // the M92F range block
    autoCombatReady = copied > 0;
    log("GLOCK_HITS AUTO combat copy: %u enemy types, zombie cooldown %u -> %u frames", copied, before, autoRecords[0][(3 - 1) * 8 + 4] & 31u);
}
// The calls are already wrapped by the wounds hook (model_integration.h shotTestHook), which asks combatFor() for the
// table and reports its hits to shotDone().
static uint32_t stateTick = 0;
static const void* combatFor(const void* combat, int weapon) {
    return autoCombatReady && weapon == 3 && combat == gameObject(0x129948) && equipped() ? autoCombat : combat;   // MANUAL too (1.11): it fires as fast
}
static void shotDone(int weapon, const uint8_t* actor, int lifeBefore) {
    const uint8_t item = equipped(); static unsigned logged = 0;
    if (!debugWarp() || !item || weapon != 3 || logged++ >= 400) return;
    if (!actor) { log("GLOCK_HIT tick=%u item=%02x none", stateTick, item); return; }
    const unsigned type = actor[0x4a]; uint32_t record[2] = {};
    const uint8_t* table = type >= 0x10 && type < 0x10 + kEnemyTypes ? (autoCombatReady ? autoTypes : reinterpret_cast<const uint8_t* const*>(gameBase + 0x129888))[type - 0x10] : nullptr;
    if (table && Accessible(table + 16, 8)) std::memcpy(record, table + 16, 8);
    log("GLOCK_HIT tick=%u item=%02x type=%02x life %d -> %d cooldown=%u record %p %08x %08x", stateTick, item, type, lifeBefore, Read<int16_t>(actor, 0xcc), actor[0x12d], table, record[0], record[1]);
}

// ---------------------------------------------------------------- shot sound
// Weapon sound banks: 42A5B0(type 1 'A', weapon) reads bio19/data/sound/A_<id>.vh/.vb; the VB (SPU ADPCM) comes through
// 4264E0 at 42A724 into [A34E34] and 42AAF0 uploads it with the emulated libspu calls (507FD0 mode, 508010 address =
// [A59318+8*type] (A bank 0xBE00, static 517704), 5080D0 write of the size rounded to 64, 508110 done). A_03 (M92F, the
// Glock's alias): VAG 6 = bank 0x3790..0x5AC0 (9008 bytes, tones 1/2 at key 60, 32 kHz) is the shot. While a Glock is in
// hand the SPU copy of the bank carries ui-pack\glock18c\shot.vag there instead: written into the read buffer when A_03
// loads, or the whole bank re-uploaded like 42AAF0 does when the weapon in hand changes between M92F and Glock.
static constexpr uint32_t kShotVag = 0x3790, kShotVagEnd = 0x5ac0, kA03Size = 30592;
static std::vector<uint8_t> shotVag, bankNative, bankGlock;   // bank copies, size rounded up to 64 like 42AAF0
static bool bankIsA03 = false, bankHasGlock = false;
using SoundReadFn = int(__cdecl*)(const char*, void*);
static SoundReadFn soundReadOriginal = nullptr;
static int __cdecl soundReadHook(const char* name, void* buffer) {
    const int size = soundReadOriginal(name, buffer);
    if (size < 0 || !name) return size;
    const char* slash = std::strrchr(name, '/'); const char* file = slash ? slash + 1 : name;
    if ((file[0] | 0x20) != 'a' || file[1] != '_') return size;   // not the weapon bank
    bankIsA03 = false;
    if (_stricmp(file, "a_03.vb") || size != int(kA03Size) || shotVag.size() != kShotVagEnd - kShotVag) return size;
    const auto bytes = static_cast<const uint8_t*>(buffer);
    if (bytes[kShotVagEnd - 31] != 1 || bytes[kShotVagEnd - 15] != 7) { log("GLOCK_SOUND A_03.VB shot signature mismatch, M92F shot kept"); return size; }
    bankNative.assign((size_t(size) + 0x3f) & ~size_t(0x3f), 0); std::memcpy(bankNative.data(), buffer, size_t(size));
    bankGlock = bankNative; std::memcpy(bankGlock.data() + kShotVag, shotVag.data(), shotVag.size());
    bankIsA03 = true; bankHasGlock = equipped() != 0;
    if (bankHasGlock) std::memcpy(buffer, bankGlock.data(), size_t(size));
    log("GLOCK_SOUND A_03 loaded, shot = %s", bankHasGlock ? "Glock" : "M92F");
    return size;
}
static void syncShotSound() {
    const bool glock = equipped() != 0;
    if (!bankIsA03 || glock == bankHasGlock) return;
    const uint32_t address = Read<uint32_t>(gameObject(0x659320));
    if (address != Read<uint32_t>(gameObject(0x117704))) { bankIsA03 = false; log("GLOCK_SOUND A bank address %x unexpected, left alone", address); return; }
    const auto& bank = glock ? bankGlock : bankNative;
    reinterpret_cast<void(__cdecl*)(int)>(gameBase + 0x107fd0)(0);
    reinterpret_cast<void(__cdecl*)(uint32_t)>(gameBase + 0x108010)(address);
    reinterpret_cast<void(__cdecl*)(const void*, uint32_t)>(gameBase + 0x1080d0)(bank.data(), uint32_t(bank.size()));
    for (int i = 0; i < 200 && !reinterpret_cast<int(__cdecl*)(int)>(gameBase + 0x108110)(0); ++i) Sleep(1);
    bankHasGlock = glock;
    log("GLOCK_SOUND shot -> %s", glock ? "Glock" : "M92F");
}

// AUTO recoil. The M92F shot animation (PLW03 weapon anims 1/3/5 by aim height, 20 frames, frame 0 = the aim pose of
// anims 2/4/6, then the kick) restarted every 2 frames kept the arms flicking up. While a Glock AUTO shot animation
// runs, the part angles the animation step left this frame (4364B0 -> 436570: part i at [player+108] + i*BC; keyframe
// target X/Y/Z words +88/+8A/+8C, copied (blend counter +CA == 0, 4368C5) or eased (436905) into the posed angles
// +60/+62/+64, then rotation matrix +14 by 507040 at 436A31) are pulled back towards the shot's frame 0 and the matrix is
// rebuilt the same way. Skeleton (hierarchy measured from recorded poses): part 0 = root and torso, its children head 1,
// right arm 2-3-4, left arm 5-6-7 and the pelvis 8 (same pivot), whose children are the legs 9-10-11 / 12-13-14.
// kickPercent of the native motion stays for the torso, head and arms, handPercent for the wrists (4/7: frame 1 alone
// turns the hands 18 degrees, the barrel climb), and every round adds a small alternating sideways sway of the torso.
// Recorded M4A1 AUTO loop: body and arms move up to ~10 degrees, the hands under 3 - it shakes, it does not climb.
// The stance is not the upper body's: the pelvis and legs (re-posed after 4364B0 also by the second channel 45A5D0 +1A0:
// thigh Y angles 9/12, feet 11/14 with their own matrix call 5070A0) and the actor position that follows the planted
// foot (+34/+3C) differ between shot frames 0 and 1 - the native burst shook Jill ~80 units sideways every frame, and
// 1.7 (pelvis treated as torso, legs rebuilt from +60) changed the stance and threw her ~330 units at the start and the
// end of a burst. During a Glock AUTO shot the stance of the aim pose the burst started from is held: legs 9-14 (local
// matrix and translation +14..+39, angles +60/+88), the pelvis kept at its aim orientation in actor space (local matrix =
// torso^T * torso_aim * pelvis_aim, as the torso turns), the root translation (part 0 +28..+39), the second channel's
// +1A4..+1B1 and the actor X/Z - after the weapon state and again at the end of the player update (45FB80: the calls
// after the routine move the actor by the animation's root motion; the last call, 4578B0 at 45FCCC, is wrapped).
static constexpr unsigned kMaxParts = 24, kPartStride = 0xbc, kUpperLast = 7, kPelvis = 8, kParts = 15;
struct LegPart { uint8_t local[0x26], posed[6], target[6]; };
static LegPart legs[kParts - kPelvis - 1];
static uint8_t rootTranslation[0x12], pelvisLocal[0x26], pelvisPosed[6], pelvisTarget[6];
static int16_t torsoAim[9];
static int32_t stanceX = 0, stanceZ = 0;
static uint8_t stanceChannel[0x0e];   // player+1A4..+1B1, the second channel's per-frame root/foot values (45A64D..45A67E)
static uint16_t aimAngles[kMaxParts][3];
static bool aimValid = false, stanceValid = false;
static unsigned kickPercent = 50, handPercent = 20, tailPercent = 30, swayAngle = 12, swayPart = 0;
static uint32_t autoRounds = 0;
static void holdStance(uint8_t* player, uint8_t* parts, bool capture) {
    uint8_t* torso = parts; uint8_t* pelvis = parts + kPelvis * kPartStride;
    if (capture) {
        for (unsigned i = kPelvis + 1; i < kParts; ++i) {
            uint8_t* part = parts + i * kPartStride; LegPart& s = legs[i - kPelvis - 1];
            std::memcpy(s.local, part + 0x14, sizeof(s.local)); std::memcpy(s.posed, part + 0x60, 6); std::memcpy(s.target, part + 0x88, 6);
        }
        std::memcpy(rootTranslation, torso + 0x28, sizeof(rootTranslation)); std::memcpy(torsoAim, torso + 0x14, sizeof(torsoAim));
        std::memcpy(pelvisLocal, pelvis + 0x14, sizeof(pelvisLocal)); std::memcpy(pelvisPosed, pelvis + 0x60, 6); std::memcpy(pelvisTarget, pelvis + 0x88, 6);
        stanceX = Read<int32_t>(player, 0x34); stanceZ = Read<int32_t>(player, 0x3c); std::memcpy(stanceChannel, player + 0x1a4, sizeof(stanceChannel));
        stanceValid = true; return;
    }
    for (unsigned i = kPelvis + 1; i < kParts; ++i) {
        uint8_t* part = parts + i * kPartStride; const LegPart& s = legs[i - kPelvis - 1];
        std::memcpy(part + 0x14, s.local, sizeof(s.local)); std::memcpy(part + 0x60, s.posed, 6); std::memcpy(part + 0x88, s.target, 6);
    }
    std::memcpy(torso + 0x28, rootTranslation, sizeof(rootTranslation));
    std::memcpy(pelvis + 0x14, pelvisLocal, sizeof(pelvisLocal)); std::memcpy(pelvis + 0x60, pelvisPosed, 6); std::memcpy(pelvis + 0x88, pelvisTarget, 6);
    // pelvis = torso^T * torso_aim * pelvis_aim (PSX MATRIX m[3][3], 4096 = 1.0, world = parent * local)
    int16_t torsoNow[9], pelvisAim[9]; std::memcpy(torsoNow, torso + 0x14, sizeof(torsoNow)); std::memcpy(pelvisAim, pelvisLocal, sizeof(pelvisAim));
    double rel[9], out[9];
    for (int r = 0; r < 3; ++r) for (int c = 0; c < 3; ++c) { double v = 0; for (int k = 0; k < 3; ++k) v += double(torsoNow[k * 3 + r]) * torsoAim[k * 3 + c]; rel[r * 3 + c] = v / 4096.0; }
    for (int r = 0; r < 3; ++r) for (int c = 0; c < 3; ++c) { double v = 0; for (int k = 0; k < 3; ++k) v += rel[r * 3 + k] * pelvisAim[k * 3 + c]; out[r * 3 + c] = v / 4096.0; }
    int16_t pelvisNow[9]; for (int k = 0; k < 9; ++k) pelvisNow[k] = int16_t((std::max)(-32768.0, (std::min)(32767.0, std::floor(out[k] + 0.5))));
    std::memcpy(pelvis + 0x14, pelvisNow, sizeof(pelvisNow));
    std::memcpy(player + 0x34, &stanceX, 4); std::memcpy(player + 0x3c, &stanceZ, 4); std::memcpy(player + 0x1a4, stanceChannel, sizeof(stanceChannel));
}
// Kneeling shots after the roll dodge (controls round 2026-11-09, Nexus report on 1.20: "with the roll dodge the Glock
// doesn't shoot nearly as fast"). The dodge is player routine 1 sub-routine 7 (52A278[7] = 460FF0); its roll variants
// (clips 13/14 of the PLD set, +C7 & 0x20) end in firearm state 8 = kneel (52A508[8] = 464F50) when 52A450[weapon] & 0x80
// (handguns 02 03 0D 11 12), the others in state 6 (blend into the aim, then state 1 as usual). Kneel: entry clip 0D,
// then the 1-frame kneel aim 0F with a budget of 15 shots at player+E3; held fire with a round left -> state 9
// (465130, budget - 1) = kneeling shot: PLW clip 0E (13 frames, player+F0/F4 bank via 437040 -> 437110, blend counter 0),
// the shot function 52A3AC[weapon] (465E30, a round at frame +C9 == 0) every tick, at the clip end state 8 sub-state 2
// again (0x208 at 465182). The Glock restart only knew the standing shot state 2, so in the kneel it fired the native
// way: one round per 13 + 1 ticks (~2/s) instead of one per autoFrames. Clip 0E frame 0 = the kneel aim 0F exactly;
// frame 1 turns the hands 24/47 degrees, lifts the root 16 units and carries root speed (-13,0,-4) (Carlos PL08W03 the
// same shape), so a restarted kneel shot gets the same soft recoil and held stance as the standing one. Each restart
// spends the native budget +E3 like a native kneeling shot (15 rounds, then she stands up via state A and fires on
// standing; pilot.ini glock_kneel_budget=0: restarts do not spend it, a burst stays kneeling to its end). Code signature
// checked once (kneelReady); on a mismatch the kneel stays native.
// Round 2 (09.11, user: "she rolls and quickly fires singles from the knee while fire is held; the Glock should do the
// same, as effective and without bugs"): native state 8 starts a kneeling shot whenever fire is HELD (464FD0), one per
// clip (13 + 1 ticks). MANUAL used to swallow a held trigger after its first round (the single-shot latch, also in
// state 8), so fire held through the roll gave no kneeling shots at all and a held trigger stopped after one. In the
// kneel the latch is off: a held trigger repeats a single every kneelHoldFrames ticks (pilot.ini glock_kneel_hold_frames,
// 14 = the native M92F rhythm), taps still fire as fast as autoFrames; AUTO keeps its burst rate.
static constexpr unsigned kKneelBudget = 0xe3;
static bool kneelReady = false, kneelBudget = true;
static unsigned kneelHoldFrames = 5;
inline bool shotState(uint8_t s) { return s == 2 || (kneelReady && s == 9); }   // standing shot 2, kneeling shot 9
inline bool aimState(uint8_t s) { return s == 1 || (kneelReady && s == 8); }    // standing aim 1, kneel 8
static bool codeIs(uint32_t rva, std::initializer_list<uint8_t> bytes) {
    const auto at = reinterpret_cast<const uint8_t*>(gameBase + rva);
    return Accessible(at, bytes.size()) && std::equal(bytes.begin(), bytes.end(), at);
}
static void checkKneel() {
    const auto states = reinterpret_cast<const uintptr_t*>(gameBase + 0x12a508);   // 52A508: firearm states by player+6
    kneelReady = Accessible(states, 11 * 4) && states[2] == gameBase + 0x64860 && states[8] == gameBase + 0x64f50 &&
        states[9] == gameBase + 0x65130 &&
        codeIs(0x64feb, {0x66, 0xc7, 0x46, 0x06, 0x09, 0x00, 0xfe, 0xc8, 0x88, 0x86, 0xe3, 0x00, 0x00, 0x00}) &&   // [6]=9, E3-1
        codeIs(0x65144, {0xc7, 0x86, 0xc8, 0x00, 0x00, 0x00, 0x0e, 0x00, 0x00, 0x00, 0xc6, 0x46, 0x07, 0x01}) &&   // clip 0E, [7]=1
        codeIs(0x65182, {0x66, 0xc7, 0x46, 0x06, 0x08, 0x02});   // clip end -> kneel 8/2
    if (!kneelReady) log("GLOCK_KNEEL firearm states 8/9 signature mismatch: kneeling shots after the roll dodge stay native");
}

// ---------------------------------------------------------------- r6 dodge (round 6, 2026-11-09): TEST switch, not for release
// User: "can it be set so that this mechanic (the roll dodge) always triggers with Nemesis, to reproduce and watch it".
// pilot.ini glock_dodge_always=1 (0 by default; the F5 menu flips dodgeAlways live): every attack of Nemesis that opens a
// dodge window is dodged by the game's own code, as if the player had pressed in time. Facts (ResidentEvil3.exe):
//  - Frame order 42B8F0..42B9D3: every enemy routine A5E508[model] runs, then the player 45FB70 (call 42B9C2), then
//    player+0 &= 0x3FFFFFFF (42B9CC). An attack with a dodge window calls 452C00(attack point, attacker position,
//    {position, box, yaw, kind}, target) on its window frames (animation frame flag [[+E4]+1] & 4, or a range of +C9);
//    target inside the box: +0 |= 0x80000000, +170 = attacker position, +16C = attack point, +C7 = kind (452CBB). So the
//    flag lives for the one frame in which the box check passed, and the player's code of that same frame tests it:
//      movement handlers 52A238[0 1 2 3 4 A E F] (46002E 46044A 4606B2 46095D 460DF2 461B08 461EA1 462184): aim held
//        (65C864 & 0x100 / 0x400), a weapon in hand (+46), life +CC > 0x14 -> +4 = 0x20701 (routine 1, dodge 7, state 2;
//        from the run 0x701);
//      aim routine 5: state 0 (464370, first 5 frames of the raise, no button, 4644CE), state 1 (464640: fire held and
//        a round left 4DEC10(0), 464791); no check in the shot 2, lowering 3, empty 4, reload 5, kneel 8/9/A;
//      Easy mode (65C880 & 0x100) only: 463C60 enters the dodge by itself in aim states <= 5 while +12D == 0, on every
//        5th tick with a random draw (463CCC: +4 = 0x20701, then 52A278[7]). No luck or difficulty anywhere else.
//  - Kind +C7 (dodge 460FF0, state 2): low nibble = direction order from 529D50 - 0/1/2 a step (clips E/F/10), 3 the push
//    (clip 11), 5/6/7 = 0/1/2 as a ROLL (461129: clips 13/14); bit 0x20 turns the side steps F/10 into 13/14 too (46136A);
//    bits 0xC0 pick the reference direction. Only clips 13/14 end in the kneel (46155E: +4 = 0x80501).
//    Nemesis draws the kind at random on every window frame: EM34/35 (4A6A30) state 5 kinds 0..3 and state 6 kind 1
//    (never a roll), state 7 0x40/42/43 or 0x62 (1 of 4), state 17 kind 3 or 6/7 (2 of 8), state 18 kind 1/3; EM36/3A
//    (49CAD0) state 5 kind 1, state 10 kind 1/6, state 12 0/5, state 15 3/6, state 18 1/3; EM38 (470220) state 5 0x67,
//    states 6/9 0x42, state 14 0x40; tentacle EM39 0x43. No 452C00 call = no dodge at all (the rocket).
// What the switch does, in the hook of the player call 42B9C2 (after the enemies, before the player's code of the frame):
//  - feeds the check that is about to run: the aim bit for the movement handlers, the fire bit for aim state 1 with a
//    round left (65C864, for this one call); in the other aim states <= 5 with +12D == 0 (shot in progress - a Glock AUTO
//    burst never leaves it -, lowering, empty, reload) it enters the dodge the way the game's own Easy-mode auto dodge
//    does (463CCC: +4 = 0x20701, here set before the player's routine runs). Life <= 0x14, no weapon, kneeling, hit: native.
//  - glock_dodge_always_roll=1 (default): the kind the attack registered becomes its roll variant (0/1/2 -> 5/6/7, the
//    push 3 -> 7), because the roll is what the user wants to watch and the game draws it at random. 0 = native kinds.
//  - only attacks of Nemesis (models 34..3B = the enemy that owns +170); glock_dodge_always=2: every enemy.
//  - glock_dodge_always_kneel=1 (default 0, NOT a rule of the game): the same entry also out of the kneel 8/9/A, where
//    the game has no dodge at all (QA 09.11: 8 of 29 windows came while Jill knelt after a roll, 3 of them hit her).
//  - glock_dodge_always_wide=N (default 0, NOT a rule of the game): on a window frame of Nemesis' animation (frame
//    function byte [[enemy+E4]+1] & 4 - what his attack states test before they call 452C00) with Jill within N units
//    (1 = 3000) the flag, +170, +16C and kind 0 are set here although his dodge box (EM34: 500..2500 ahead, +-1000
//    sideways, 52DED0) does not hold her. QA 09.11: 2 hits landed with no flagged frame at all and 2 with the first
//    flagged frame = the frame of the hit (the enemy's hit test runs before the player's code).
// Everything after the entry - direction, clip, invulnerability, the kneel and its budget - is native code. Main exe only.
static bool dodgeAlways = false;      // pilot.ini glock_dodge_always (0 by default)
static bool dodgeAnyEnemy = false, dodgeRoll = true, dodgeKneel = false, dodgeReady = false;
static int dodgeWide = 0;             // pilot.ini glock_dodge_always_wide: 0 or the range in units
static bool dodgeFeed = false;        // this tick's fire bit is the switch's: the MANUAL single-shot latch must not swallow it
using PlayerMainFn = void(__cdecl*)(uint8_t*);
static PlayerMainFn playerMainOriginal = nullptr;
inline bool nemesisModel(uint8_t m) { return m >= 0x34 && m <= 0x3b; }
// the enemy whose attack set the flag: +170 points into it (enemy+0x34 at every Nemesis call of 452C00) or into its parts
static const uint8_t* dodgeAttacker(const uint8_t* player) {
    const uintptr_t at = Read<uint32_t>(player, 0x170);
    const uintptr_t first = gameBase + 0x65cdc8, end = Read<uint32_t>(gameObject(0x65ce6c));   // 42B91E: pointers up to [A5CE6C]
    if (!at || end < first || end > first + 41 * 4) return nullptr;
    for (uintptr_t p = first; p < end; p += 4) {
        const auto enemy = reinterpret_cast<const uint8_t*>(uintptr_t(Read<uint32_t>(reinterpret_cast<const void*>(p))));
        if (!enemy || enemy == player || !Accessible(enemy, 0x200)) continue;
        const uintptr_t e = reinterpret_cast<uintptr_t>(enemy), parts = Read<uint32_t>(enemy, 0x108);
        if ((at >= e && at < e + 0x200) || (parts && at >= parts && at < parts + size_t(enemy[8]) * kPartStride)) return enemy;
    }
    return nullptr;
}
// glock_dodge_always_wide: a living Nemesis on a dodge-window frame of his attack animation, within the range
static const uint8_t* wideAttacker(const uint8_t* player) {
    const uintptr_t first = gameBase + 0x65cdc8, end = Read<uint32_t>(gameObject(0x65ce6c));
    if (end < first || end > first + 41 * 4) return nullptr;
    for (uintptr_t p = first; p < end; p += 4) {
        const auto enemy = reinterpret_cast<const uint8_t*>(uintptr_t(Read<uint32_t>(reinterpret_cast<const void*>(p))));
        if (!enemy || enemy == player || !Accessible(enemy, 0x200) || !nemesisModel(enemy[0x4a]) || !(enemy[0] & 1) || enemy[4] != 1 || Read<int16_t>(enemy, 0xcc) <= 0) continue;
        const auto entry = reinterpret_cast<const uint8_t*>(uintptr_t(Read<uint32_t>(enemy, 0xe4)));   // animation frame {keyframe, function}
        if (!Accessible(entry, 2) || !(entry[1] & 4)) continue;
        const double dx = double(Read<int32_t>(enemy, 0x34)) - Read<int32_t>(player, 0x34), dz = double(Read<int32_t>(enemy, 0x3c)) - Read<int32_t>(player, 0x3c);
        if (dx * dx + dz * dz <= double(dodgeWide) * dodgeWide) return enemy;
    }
    return nullptr;
}
static void __cdecl playerMainHook(uint8_t* player) {
    if (player != gameObject(0x65cbf4)) { playerMainOriginal(player); return; }
    struct Auto { bool on = false, wide = false; uint32_t n = 0, tick = 0, enemyRoutine = 0; uint8_t kind = 0, kindNow = 0, model = 0, enemyClip = 0, enemyFrame = 0, from[3] = {}, weapon = 0, item = 0; int life = 0; const char* via = ""; };
    static Auto pending;
    static uint32_t tick = 0, windows = 0, autos = 0, windowLines = 0, noteLines = 0, endLines = 0, notedWindow = 0, lastFlagTick = 0, frames = 0;
    static uint8_t kinds[8]; static unsigned kindCount = 0; static bool open = false;
    ++tick;
    bool wide = false;
    if (dodgeAlways && dodgeWide && dodgeReady && player[4] == 1 && !(Read<uint32_t>(player) & 0x80000000u)) {
        if (const uint8_t* enemy = wideAttacker(player)) {   // what 452CBB writes when the box holds the target; kind 0 = step back / to a side
            const uint32_t parts = Read<uint32_t>(enemy, 0x108), origin = uint32_t(reinterpret_cast<uintptr_t>(enemy + 0x34));
            *reinterpret_cast<uint32_t*>(player) |= 0x80000000u; *reinterpret_cast<uint32_t*>(player + 0x170) = origin;
            *reinterpret_cast<uint32_t*>(player + 0x16c) = parts && enemy[8] > 8 && Accessible(reinterpret_cast<const void*>(uintptr_t(parts) + 0x634), 12) ? parts + 0x634 : origin;
            player[0xc7] = 0; wide = true;
        }
    }
    const bool debug = debugWarp(), flag = (Read<uint32_t>(player) & 0x80000000u) != 0;
    const bool wasDodge = player[4] == 1 && player[5] == 7;
    const uint8_t* attacker = flag ? dodgeAttacker(player) : nullptr;
    const uint8_t model = attacker ? attacker[0x4a] : 0, kind = player[0xc7];
    const int life = Read<int16_t>(player, 0xcc);
    if (flag) {
        if (!open) {
            ++windows; frames = 0; kindCount = 0;
            if (debug && windowLines < 400) {
                ++windowLines;
                log("GLOCK_DODGE_WINDOW %u tick=%u gap=%u kind=%02x enemy=%02x routine=%08x clip=%02x frame=%u jill=%u/%u/%u life=%d switch=%u%s",
                    windows, tick, lastFlagTick ? tick - lastFlagTick : 0, kind, model, attacker ? Read<uint32_t>(attacker, 4) : 0, attacker ? attacker[0xc8] : 0,
                    attacker ? attacker[0xc9] : 0, player[4], player[5], player[6], life, unsigned(dodgeAlways), wide ? " wide (set by the switch, not by the game)" : "");
            }
        }
        ++frames; lastFlagTick = tick;
        if (std::find(kinds, kinds + kindCount, kind) == kinds + kindCount && kindCount < 8) kinds[kindCount++] = kind;
    } else if (open && debug && windowLines < 400) {
        char text[40] = ""; size_t used = 0;
        for (unsigned i = 0; i < kindCount && used + 4 < sizeof(text); ++i) used += size_t(std::snprintf(text + used, sizeof(text) - used, "%s%02x", i ? " " : "", kinds[i]));
        log("GLOCK_DODGE_WINDOW_END %u tick=%u frames=%u kinds=%s jill=%u/%u/%u life=%d", windows, tick, frames, text, player[4], player[5], player[6], life);
    }
    open = flag;
    int via = 0; uint32_t added = 0; uint8_t kindNow = kind;
    const auto held = reinterpret_cast<uint32_t*>(gameBase + 0x65c864);
    if (flag && dodgeAlways && dodgeReady && player[4] == 1 && (dodgeAnyEnemy || nemesisModel(model))) {
        if (dodgeRoll) {   // also while the run-stop states 0/1 of the dodge play: the attack keeps rewriting +C7
            const uint8_t low = kind & 0x0f;
            if (low <= 2) kindNow = uint8_t(kind + 5); else if (low == 3) kindNow = uint8_t((kind & 0xf0) | 7);
            player[0xc7] = kindNow;
        }
        const uint8_t sub = player[5], weapon = player[0x46]; const char* why = nullptr;
        if (wasDodge) {}
        else if (life <= 0x14) why = "life <= 20: the game allows no dodge";
        else if (!weapon) why = "no weapon in hand";
        else if (sub == 0 || sub == 1 || sub == 2 || sub == 3 || sub == 4 || sub == 10 || sub == 14 || sub == 15) via = 1;
        else if (sub == 5 && player[6] <= 5 && !player[0x12d]) {
            const bool generic = weapon < 0x18 && (weapon == 3 || Read<uint32_t>(gameObject(0x12a35c + weapon * 4)) == uint32_t(gameBase + 0x64340));
            const bool round = reinterpret_cast<uint16_t(__cdecl*)(int)>(gameBase + 0xdec10)(0) != 0xffff;
            via = player[6] == 1 && generic && round ? 2 : 3;
        } else if (sub == 5 && dodgeKneel && player[6] >= 8 && player[6] <= 10 && !player[0x12d]) via = 4;   // not native
        else why = "no dodge from this state";
        if (via == 1 && !(*held & 0x500)) { *held |= 0x100; added = 0x100; }
        if (via == 2) { dodgeFeed = true; if (!(*held & 0x40)) { *held |= 0x40; added = 0x40; } }
        if (via) {
            pending = Auto{}; pending.on = true; pending.n = ++autos; pending.tick = tick; pending.kind = kind; pending.kindNow = kindNow; pending.model = model;
            pending.enemyRoutine = attacker ? Read<uint32_t>(attacker, 4) : 0; pending.enemyClip = attacker ? attacker[0xc8] : 0; pending.enemyFrame = attacker ? attacker[0xc9] : 0;
            pending.from[0] = player[5]; pending.from[1] = player[6]; pending.from[2] = player[7]; pending.weapon = weapon; pending.item = equipped(); pending.life = life;
            pending.via = via == 1 ? (added ? "aim fed" : "aim held") : via == 2 ? (added ? "fire fed" : "fire held") : via == 3 ? "auto entry 463CCC" : "auto entry out of the kneel (not native)";
            pending.wide = wide;
            if (via >= 3) *reinterpret_cast<uint32_t*>(player + 4) = 0x20701;   // routine 1, dodge 7, state 2
        } else if (why && debug && notedWindow != windows && noteLines < 120) { ++noteLines; notedWindow = windows; log("GLOCK_DODGE_ALWAYS skipped: %s (jill=%u/%u/%u +12D=%02x weapon=%02x life=%d kind=%02x enemy=%02x)", why, player[4], sub, player[6], player[0x12d], weapon, life, kind, model); }
    }
    playerMainOriginal(player);
    dodgeFeed = false;
    if (added) *held &= ~added;
    const bool nowDodge = player[4] == 1 && player[5] == 7;
    if (via && !nowDodge) {
        --autos; pending.on = false;
        if (debug && noteLines < 120) { ++noteLines; log("GLOCK_DODGE_ALWAYS fed (%s) but the game did not dodge: jill 1/%u/%u -> %u/%u/%u", pending.via, pending.from[0], pending.from[1], player[4], player[5], player[6]); }
    }
    // one line per automatic dodge, once its clip is chosen (state 3; a dodge out of the run plays states 0/1 first)
    if (pending.on && (!nowDodge || player[6] == 3)) {
        const uint8_t clip = player[0xc8]; pending.on = false;
        if (pending.n <= (debug ? 400u : 60u))
            log("GLOCK_DODGE_ALWAYS %u tick=%u kind=%02x%s enemy=%02x routine=%08x clip=%02x frame=%u via=%s%s jill=1/%u/%u weapon=%02x item=%02x life=%d -> %s (clip %02x)",
                pending.n, pending.tick, pending.kindNow, pending.kindNow != pending.kind ? " (roll variant)" : "", pending.model, pending.enemyRoutine, pending.enemyClip, pending.enemyFrame,
                pending.via, pending.wide ? ", wide window" : "", pending.from[0], pending.from[1], pending.weapon, pending.item, pending.life,
                !nowDodge ? "dodge ended early" : clip == 0x13 || clip == 0x14 ? "ROLL" : clip == 0x11 ? "push" : "step", clip);
    }
    if (debug && endLines < 400) {
        if (!via && !wasDodge && nowDodge) { ++endLines; log("GLOCK_DODGE player's own dodge: tick=%u kind=%02x enemy=%02x life=%d", tick, player[0xc7], model, life); }
        if (wasDodge && !nowDodge) { ++endLines; log("GLOCK_DODGE_END tick=%u +4=%08x (%s) weapon=%02x item=%02x", tick, Read<uint32_t>(player, 4), player[5] == 5 && player[6] == 8 ? "kneel" : player[5] == 5 ? "aim" : "other", player[0x46], equipped()); }
    }
}
static void installDodgeAlways(int ini) {
    dodgeAlways = ini != 0; dodgeAnyEnemy = ini == 2;
    if (re3exe::gRunning != re3exe::Exe::Main) { if (ini) log("GLOCK_DODGE_ALWAYS off: main game only"); dodgeAlways = false; return; }
    const auto routines = reinterpret_cast<const uint32_t*>(gameBase + 0x12a218), handlers = reinterpret_cast<const uint32_t*>(gameBase + 0x12a238),
        steps = reinterpret_cast<const uint32_t*>(gameBase + 0x12a278);
    static const uint8_t kindTable[32] = {1, 2, 3, 0, 2, 1, 3, 0, 3, 1, 2, 0, 4, 0, 0, 0, 5, 0, 0, 0, 6, 7, 8, 0, 7, 6, 8, 0, 8, 6, 7, 0};
    const bool ok = Accessible(routines, 0x80) && routines[1] == uint32_t(gameBase + 0x5fec0) && handlers[0] == uint32_t(gameBase + 0x5ff50) &&
        handlers[5] == uint32_t(gameBase + 0x5ea0) && handlers[7] == uint32_t(gameBase + 0x5ea0) && steps[5] == uint32_t(gameBase + 0x63c60) &&
        steps[7] == uint32_t(gameBase + 0x60ff0) && Accessible(gameObject(0x129d50), 32) && !std::memcmp(gameObject(0x129d50), kindTable, 32) &&
        codeIs(0x52cbb, {0x8b, 0x07, 0x8b, 0x4c, 0x24, 0x4c, 0x0d, 0x00, 0x00, 0x00, 0x80, 0x89, 0x8f, 0x70, 0x01, 0x00, 0x00}) &&   // flag, +170
        codeIs(0x2b9c7, {0x8b, 0x07, 0x83, 0xc4, 0x04, 0x25, 0xff, 0xff, 0xff, 0x3f, 0x89, 0x07}) &&                               // flag cleared after the player
        codeIs(0x6002e, {0x66, 0x83, 0xb8, 0xcc, 0x00, 0x00, 0x00, 0x14, 0x7e, 0x07, 0xc7, 0x40, 0x04, 0x01, 0x07, 0x02, 0x00}) && // idle: life, 0x20701
        codeIs(0x64791, {0xf7, 0x06, 0x00, 0x00, 0x00, 0x80}) && codeIs(0x647b9, {0xc7, 0x46, 0x04, 0x01, 0x07, 0x02, 0x00}) &&    // aim state 1
        codeIs(0x63ccc, {0xc7, 0x46, 0x04, 0x01, 0x07, 0x02, 0x00, 0x8a, 0x56, 0x05}) &&                                           // Easy auto dodge entry
        codeIs(0x61129, {0x3c, 0x04, 0x76, 0x09, 0x3c, 0x08, 0x73, 0x05, 0xba, 0x08, 0x00, 0x00, 0x00}) &&                         // kinds 5..7: roll clips
        codeIs(0x6136a, {0xf6, 0x86, 0xc7, 0x00, 0x00, 0x00, 0x20}) && codeIs(0x6155e, {0xc7, 0x46, 0x04, 0x01, 0x05, 0x08, 0x00}); // roll bit, kneel
    const size_t from = patches.size();
    if (!ok || !callPatch(0x2b9c2, 0x5fb70, reinterpret_cast<void*>(playerMainHook))) { patches.resize(from); log("GLOCK_DODGE_ALWAYS off: code signature mismatch"); dodgeAlways = false; return; }
    playerMainOriginal = reinterpret_cast<PlayerMainFn>(gameBase + 0x5fb70);
    if (!writeFrom(from)) { log("GLOCK_DODGE_ALWAYS off: hook failed"); dodgeAlways = false; return; }
    dodgeReady = true;
    log("GLOCK_DODGE_ALWAYS installed: switch %s (pilot.ini glock_dodge_always=%d, %s), roll variant %s (glock_dodge_always_roll), out of the kneel %s (glock_dodge_always_kneel), wide window %d (glock_dodge_always_wide, 0 = the box of the game)",
        dodgeAlways ? "ON" : "off", ini, dodgeAnyEnemy ? "every enemy" : "Nemesis only", dodgeRoll ? "forced" : "native", dodgeKneel ? "yes (not native)" : "no (native)", dodgeWide);
}

using PlayerEndFn = void(__cdecl*)(uint8_t*, int);
static PlayerEndFn playerEndOriginal = nullptr;
static void __cdecl playerEndHook(uint8_t* player, int yaw) {
    playerEndOriginal(player, yaw);
    // routine 1/5 = weapon in hand, state 2 = shot (9 = kneeling shot): only a Glock shot that captured its stance
    if (!stanceValid || player != gameObject(0x65cbf4) || player[4] != 1 || player[5] != 5 || !shotState(player[6]) || !equipped()) return;
    uint8_t* parts = Read<uint8_t*>(player, 0x108);
    if (player[8] >= kParts && Accessible(parts, kParts * kPartStride, true)) holdStance(player, parts, false);
}
static void softenRecoil(uint8_t* player, bool newRound, bool burstStart) {
    uint8_t* parts = Read<uint8_t*>(player, 0x108); const unsigned count = (std::min)(unsigned(player[8]), kMaxParts);
    if (count < kParts || !Accessible(parts, size_t(count) * kPartStride, true)) { aimValid = stanceValid = false; return; }
    if (burstStart) holdStance(player, parts, true);   // still the aim pose this frame: the stance to keep
    auto posed = [&](unsigned i, unsigned a) { return reinterpret_cast<uint16_t*>(parts + i * kPartStride + 0x60) + a; };
    if (newRound) {   // frame 0 of this round's shot animation was posed this frame: the aim pose of the upper body
        for (unsigned i = 0; i <= kUpperLast; ++i) for (unsigned a = 0; a < 3; ++a) aimAngles[i][a] = *posed(i, a) & 4095;
        aimValid = true; ++autoRounds;
    } else if (aimValid) {
        // inside a burst only frames 0/1 are posed; frames 2.. (the full kick and its dip) are the last round's tail
        const int body = int(player[0xc9] <= 2 ? kickPercent : tailPercent), hands = int(player[0xc9] <= 2 ? handPercent : (std::min)(handPercent, tailPercent));
        for (unsigned i = 0; i <= kUpperLast; ++i) {
            for (unsigned a = 0; a < 3; ++a) {
                const int delta = ((int(*posed(i, a)) - aimAngles[i][a] + 2048) & 4095) - 2048;
                int value = aimAngles[i][a] + delta * (i == 4 || i == 7 ? hands : body) / 100;
                if (i == swayPart && a == 1) value += (autoRounds & 1) ? int(swayAngle) : -int(swayAngle);
                *posed(i, a) = uint16_t(value & 4095);
            }
            // the step already turned the angles into the part's rotation matrix (436A31: 507040(+60 -> +14)); redo it
            uint8_t* part = parts + i * kPartStride;
            if (!(part[0] & 0x10)) reinterpret_cast<void(__cdecl*)(const void*, void*)>(gameBase + 0x107040)(part + 0x60, part + 0x14);
        }
    }
    if (stanceValid && !burstStart) holdStance(player, parts, false);
}

// Both modes restart the shot animation (sub-state 0 next frame = a round) once autoFrames of it have played: AUTO while
// fire is held, MANUAL for each new pull - a pull during the animation is kept until then, so tapping fires as fast as
// the taps but never faster than AUTO. Both use the soft recoil and the held stance of an AUTO round (1.11; before, MANUAL
// played the whole native M92F kick and took the next pull only back in the aim state). The same holds for the kneeling
// shot 9 after the roll dodge (2026-11-09, see kneelReady): restarts there also spend the kneel budget +E3.
static bool latched = false, owed = false;   // single: this pull already fired / a pull waiting for the next round
static void __cdecl weaponStateHook(uint8_t* player, uint32_t a2, uint32_t a3, uint32_t held) {
    if (player == gameObject(0x65cbf4)) syncShotSound();
    const uint8_t item = player == gameObject(0x65cbf4) ? equipped() : 0;
    if (!item) { latched = owed = false; weaponStateOriginal(player, a2, a3, held); return; }
    const bool fire = (held & 0x40) != 0;
    if (!fire) latched = false;
    const uint8_t before = player[6];
    const bool shooting = shotState(before);
    if (item == kSingle && shooting && fire && !latched) { owed = true; latched = true; }   // a new pull during the shot
    static uint32_t shots = 0; ++stateTick;
    if (shooting && player[7] == 0 && shots++ < (debugWarp() ? 400u : 48u))   // the shot animation starts: a round this tick
        log("GLOCK_SHOT item=%02x tick=%u%s", item, stateTick, before == 9 ? " kneel" : "");
    const bool restart = shooting && player[7] == 0;
    weaponStateOriginal(player, a2, a3, item == kSingle && latched && before == 1 && !dodgeFeed ? held & ~0x40u : held);   // kneel 8: held fire = native; r6 dodge: the switch's fire bit passes
    const uint8_t now = player[6];
    if (shotState(now) && !shooting) { latched = true; owed = false; }   // a shot started: single mode needs a new pull
    if (!shotState(now) && owed) { latched = owed = false; }   // the shot ended before the pull was served: the aim state takes it
    if (shotState(now)) softenRecoil(player, restart || !shooting, !shooting); else aimValid = stanceValid = false;
    const bool kneel = now == 9;   // shotState(9) only with kneelReady
    const bool kneelRepeat = item == kSingle && kneel && fire && player[0xc9] >= kneelHoldFrames;   // MANUAL held in the kneel
    if ((item == kAuto ? fire : owed || kneelRepeat) && shotState(now) && player[7] == 1 && (held & 0x500) && player[0xc9] >= autoFrames &&
        (!kneel || !kneelBudget || player[kKneelBudget])) {
        const uint16_t left = reinterpret_cast<uint16_t(__cdecl*)(int)>(gameBase + 0xdec10)(0);
        if (left != 0xffff) {   // next frame starts the shot animation again: the next round
            player[7] = 0;
            if (kneel && kneelBudget && player[kKneelBudget]) --player[kKneelBudget];   // as 464FF1 does for every native kneeling shot
        }
        owed = false;
    }
}
// 4DBEF0 for the Glock ids: bit 0x100 set = single; low byte from the table, infinite kept, equipped id follows
static void switchMode(uint8_t* inv, int slot) {
    uint8_t* s = inv + slot * 4; const uint8_t old = s[0]; uint16_t flags = Read<uint16_t>(s, 2);
    const bool infinite = (flags & 3) == 3; uint8_t now;
    if (flags & 0x100) { now = kAuto; flags &= ~0x100; } else { now = kSingle; flags |= 0x100; }
    flags = uint16_t((flags & 0xff00) | gameObject(0x12ac53)[now * 4] | (infinite ? 3 : 0));
    s[0] = now; std::memcpy(s + 2, &flags, 2);
    if (inv[0x129] == old && inv[0x128] == slot) inv[0x129] = now;
    log("GLOCK_MODE slot=%d %02x -> %02x flags=%04x", slot, old, now, flags);
}
static void __cdecl modeSwitchHook(uint8_t* menu) {
    auto inv = inventory(); const int slot = int8_t(menu[0x1c]);
    if (!inv || slot < 0 || slot >= inv[0x12a] || !isGlock(inv[slot * 4])) { modeSwitchOriginal(menu); return; }
    switchMode(inv, slot);
    menu[0x11] = 2;
}
static void __cdecl newGameHook() {
    newGameOriginal();
    if (!testFlag(systemFlags(), kFlag)) return;
    const auto modes = reinterpret_cast<const uint32_t*>(gameBase + 0x65c880);
    int mode = testFlag(modes, 0x18) ? 2 : testFlag(modes, 0x17) ? 1 : 0;
    const int32_t sub = Read<int32_t>(gameObject(0x64cff0)); if (sub >= 1 && sub <= 3) mode = 2 + sub;
    if (mode > 1) { log("GLOCK_NEWGAME mode=%d: no rewards in this mode", mode); return; }
    const bool infinite = testFlag(systemFlags(), 9);
    for (int block = 0; block < 2; ++block) {
        uint8_t* box = gameObject(0x66218c + block * 0x140); int slot = -1;
        for (int k = 0; k < 64 && slot < 0; ++k) { const int i = (32 + k) % 64; if (!box[i * 4]) slot = i; }
        if (slot < 0) { log("GLOCK_NEWGAME block %d: item box full", block); continue; }
        const uint16_t flags = uint16_t(0x111 | (infinite ? 3 : 0));
        box[slot * 4] = kSingle; box[slot * 4 + 1] = kRounds; std::memcpy(box + slot * 4 + 2, &flags, 2);
        log("GLOCK_NEWGAME block %d: Glock 18C in item box slot %d (infinite=%u)", block, slot, unsigned(infinite));
    }
}
static void installItems() {
    const uint8_t placeholder[4] = {4, 1, 0, 0}, single[4] = {1, kRounds, 0, 0x11}, autoFire[4] = {1, kRounds, 0, 0x15};
    if (std::memcmp(gameObject(0x12ac50 + kSingle * 4), placeholder, 4) || std::memcmp(gameObject(0x12ac50 + kAuto * 4), placeholder, 4) ||
        Read<uint8_t>(gameObject(0x12af01)) == 0xff || Read<uint8_t>(gameObject(0x12b300)) != 2) { log("GLOCK_ITEMS table signature mismatch, no Glock"); return; }
    // relocated combine / reload tables with the Glock rows in front of the terminators
    size_t n = 0; while (n < 0x3f0 / 8 && gameObject(0x12af00)[n * 8 + 1] != 0xff) ++n;
    std::memcpy(combineTable, gameObject(0x12af00), n * 8);
    const uint8_t extra[4][8] = {{0, kSingle, 0x15}, {0, kAuto, 0x15}, {6, kSingle, 0x6e}, {6, kAuto, 0x6e}};
    std::memcpy(combineTable + n * 8, extra, sizeof(extra)); std::memcpy(combineTable + n * 8 + sizeof(extra), gameObject(0x12af00) + n * 8, 8);
    size_t r = 0; while (r < 14 && gameObject(0x12b300)[r * 4] != 0xff) ++r;
    std::memcpy(reloadTable, gameObject(0x12b300), r * 4);
    const uint8_t reload[2][4] = {{kSingle, 0x15}, {kAuto, 0x15}};
    std::memcpy(reloadTable + r * 4, reload, sizeof(reload)); std::memcpy(reloadTable + r * 4 + sizeof(reload), gameObject(0x12b300) + r * 4, 4);
    const size_t from = patches.size(); bool ok = true;
    for (uint32_t site : {0x2dca7u, 0x2dcbcu, 0x2ea8au, 0x2eaa6u, 0x2ecdcu}) ok = ok && callPatch(site, 0x2f290, reinterpret_cast<void*>(nameHook));
    ok = ok && callPatch(0xdbda3, 0x2da10, reinterpret_cast<void*>(checkMessageHook));
    for (uint32_t site : {0xdcc11u, 0xdcc7eu, 0xdcdf1u}) ok = ok && callPatch(site, 0x265a0, reinterpret_cast<void*>(readHook));
    for (uint32_t site : {0x5dc31u, 0x5dde8u}) ok = ok && callPatch(site, 0xdcce0, reinterpret_cast<void*>(slotIconHook));
    for (uint32_t site : {0x5e125u, 0xe4b42u, 0xe4c95u}) ok = ok && callPatch(site, 0xdcd50, reinterpret_cast<void*>(boxIconHook));
    ok = ok && operandPatch(0xdcb52, 0x52af01, combineTable + 1) && operandPatch(0xdcb5b, 0x52af00, combineTable) &&
        operandPatch(0xdcbc2, 0x52b300, reloadTable) && operandPatch(0xdcbc7, 0x52b300, reloadTable);
    ok = ok && slotPatch(0x130d40, 0xdbef0, reinterpret_cast<void*>(modeSwitchHook));
    for (uint32_t site : {0x5dd5au, 0x5e492u, 0x6bd34u, 0x6bd80u, 0x6be71u, 0xda923u}) ok = ok && callPatch(site, 0x52620, reinterpret_cast<void*>(weaponInitHook));
    for (uint32_t site : {0x5dd64u, 0x5e49cu, 0x5fb37u, 0x6bd3eu, 0x6bd8au, 0x6be7bu, 0xda92du}) ok = ok && callPatch(site, 0x52310, reinterpret_cast<void*>(weaponLoadHook));
    ok = ok && callPatch(0x6be9b, 0x52690, reinterpret_cast<void*>(weaponResumeHook));
    ok = ok && slotPatch(0x12a368, 0x64340, reinterpret_cast<void*>(weaponStateHook));
    for (uint32_t site : {0x51380u, 0x51ff8u}) ok = ok && callPatch(site, 0x5d750, reinterpret_cast<void*>(newGameHook));
    ok = ok && callPatch(0x2a724, 0x264e0, reinterpret_cast<void*>(soundReadHook)) && callPatch(0x5fccc, 0x578b0, reinterpret_cast<void*>(playerEndHook));
    if (!ok) { patches.resize(from); log("GLOCK_ITEMS hooks skipped: signature mismatch, no Glock"); return; }
    soundReadOriginal = reinterpret_cast<SoundReadFn>(gameBase + 0x264e0); playerEndOriginal = reinterpret_cast<PlayerEndFn>(gameBase + 0x578b0);
    buildAutoCombat(); checkKneel();
    nameOriginal =reinterpret_cast<NameFn>(gameBase + 0x2f290); checkMessageOriginal = reinterpret_cast<MessageFn>(gameBase + 0x2da10);
    readOriginal = reinterpret_cast<ReadFn>(gameBase + 0x265a0); slotIconOriginal = reinterpret_cast<IconFn>(gameBase + 0xdcce0);
    boxIconOriginal = reinterpret_cast<IconFn>(gameBase + 0xdcd50); weaponInitOriginal = reinterpret_cast<PlayerFn>(gameBase + 0x52620);
    weaponLoadOriginal = reinterpret_cast<PlayerFn>(gameBase + 0x52310); weaponResumeOriginal = reinterpret_cast<PlayerFn>(gameBase + 0x52690); weaponStateOriginal = reinterpret_cast<WeaponStateFn>(gameBase + 0x64340);
    modeSwitchOriginal = reinterpret_cast<MenuStateFn>(gameBase + 0xdbef0); newGameOriginal = reinterpret_cast<VoidFn>(gameBase + 0x5d750);
    if (!writeFrom(from)) { log("GLOCK_ITEMS hooks failed: restored"); return; }
    if (!writeData(gameBase + 0x12ac50 + kSingle * 4, single, 4, placeholder) || !writeData(gameBase + 0x12ac50 + kAuto * 4, autoFire, 4, placeholder)) {
        for (size_t i = patches.size(); i > from; --i) re3bridge::detail::WritePatch(patches[i - 1], false);
        patches.resize(from); log("GLOCK_ITEMS attribute write failed: restored"); return;
    }
    itemsInstalled = true;
    log("GLOCK_ITEMS installed: items %02x/%02x, %u rounds, combine rows %zu+4, reload rows %zu+2, auto every %u frames, pictures %s/%s, shot sound %s, AUTO recoil %u/%u/%u%% sway %u, kneel shots %s",
        kSingle, kAuto, kRounds, n, r, autoFrames, itemiRecord.empty() ? "MISSING" : "icon", itemgRecord.empty() ? "MISSING" : "examine",
        shotVag.empty() ? "MISSING (M92F shot)" : "shot.vag", kickPercent, handPercent, tailPercent, swayAngle, !kneelReady ? "native" : kneelBudget ? "Glock rate (15-round kneel budget)" : "Glock rate (burst keeps the kneel)");
    if (kneelReady) log("GLOCK_KNEEL MANUAL: fire held in the kneel repeats a single every %u ticks (native M92F 14), taps every %u+; AUTO every %u", kneelHoldFrames, autoFrames, autoFrames);
}

// ---------------------------------------------------------------- diagnostics (debug_warp, game thread)
static void diagnostic() {
    if (glockRequest.empty()) return;
    const std::string command = glockRequest; glockRequest.clear();
    if (command == "glock-flag") { setFlag(systemFlags(), kFlag); log("GLOCK_DIAGNOSTIC flag %02x set (bought)", kFlag); return; }
    if (command == "glock-money") { *reinterpret_cast<uint32_t*>(gameObject(0x66261c)) = 99999; log("GLOCK_DIAGNOSTIC money 99999"); return; }
    if (command == "glock-give") {
        auto inv = inventory();
        if (!itemsInstalled || !inv) { log("GLOCK_DIAGNOSTIC give rejected: items not installed / no inventory"); return; }
        int given = 0;
        const uint8_t items[2][4] = {{kSingle, kRounds, 0x11, 0x01}, {0x15, 60, 0x0d, 0x00}};
        for (const auto& item : items)
            for (int i = 0; i < inv[0x12a]; ++i) if (!inv[i * 4]) {
                std::memcpy(inv + i * 4, item, 4); ++given;
                // the slot icon as 4DCBF0 loads it (ITEMI.PIX record -> slot rect), into our own buffer: the game's
                // item buffer A35EE8 exists only while the inventory runs
                static uint8_t icon[0x800];
                std::memset(icon, 0, sizeof(icon));
                readHook(reinterpret_cast<const char*>(gameBase + 0x130dc8), icon, uint32_t(item[0]) << 11, 0x800);
                loadIconImage(reinterpret_cast<const int16_t*>(gameBase + 0x130c68) + i * 2, icon);
                break;
            }
        log("GLOCK_DIAGNOSTIC give: %d items (Glock 18C 19, handgun bullets 60)", given);
        return;
    }
    auto inv = inventory(); int slot = -1;
    for (int i = 0; inv && i < inv[0x12a] && slot < 0; ++i) if (isGlock(inv[i * 4])) slot = i;
    if (!itemsInstalled || slot < 0) { log("GLOCK_DIAGNOSTIC %s rejected: no Glock in the inventory", command.c_str()); return; }
    if (command == "glock-equip") {   // what the inventory EQUIP does at 45DD20
        uint8_t* player = gameObject(0x65cbf4);
        inv[0x128] = uint8_t(slot); inv[0x129] = inv[slot * 4]; player[0x46] = inv[slot * 4];
        weaponInitHook(player); weaponLoadHook(player);
        log("GLOCK_DIAGNOSTIC equip slot %d item %02x", slot, inv[slot * 4]);
    }
    if (command == "glock-mode") switchMode(inv, slot);
    if (command == "glock-kneel") {   // QA (2026-11-09): the kneel a roll dodge ends in (46155E: routine 1/5, firearm state 8/0)
        uint8_t* player = gameObject(0x65cbf4);
        if (!kneelReady || !equipped() || player[4] != 1 || player[5] != 5 || player[6] != 1) {
            log("GLOCK_DIAGNOSTIC kneel rejected: needs the Glock aimed (routine %u/%u state %u), kneel %s", player[4], player[5], player[6], kneelReady ? "ready" : "native");
            return;
        }
        // QA 2026-11-09 (game runs): the dodge also sets the attacker the kneel turns to (461111: +15C = +170; state 8 reads
        // [+15C]+0/+8 at 4650B7 -> access violation when it is empty) and the 15-shot kneel budget (460AED)
        // no attack in progress (+170 empty): a point 2000 units straight ahead (yaw +6E, step (cos, -sin) as 455E90), x/y/z
        static int32_t ahead[3];
        uint32_t attacker = *reinterpret_cast<const uint32_t*>(player + 0x170);
        if (!attacker) {
            const double a = double(*reinterpret_cast<const uint16_t*>(player + 0x6e) & 0xfff) * 2.0 * 3.14159265358979 / 4096.0;
            ahead[0] = *reinterpret_cast<const int32_t*>(player + 0x34) + int32_t(2000.0 * std::cos(a));
            ahead[1] = *reinterpret_cast<const int32_t*>(player + 0x38);
            ahead[2] = *reinterpret_cast<const int32_t*>(player + 0x3c) - int32_t(2000.0 * std::sin(a));
            attacker = uint32_t(reinterpret_cast<uintptr_t>(ahead));
        }
        *reinterpret_cast<uint32_t*>(player + 0x15c) = attacker;
        player[6] = 8; player[7] = 0;
        player[kKneelBudget] = 15;
        log("GLOCK_DIAGNOSTIC kneel: firearm state 8 as after the roll dodge, budget 15");
    }
    if (command == "glock-dodge") {
        // QA round 2 (09.11): the REAL roll dodge. In a game run the dodge flag ([player] & 0x80000000, set by an enemy's
        // attack check 452C00 and consumed in the same frame) never reaches the input poll, and zombie grabs (kind 0x43)
        // are dodged with the push clip 11, not the roll. So set what 452C00 sets for a roll attack (kind 0x67 = the
        // attack registered at 471BE9; +170 attacker position, +16C attack point: a point 1500 ahead) and enter the dodge
        // the way the idle/aim checks do (46002E / 4644E3: +4 = 0x20701 = routine 1, dodge 7, state 2). Everything after
        // that - the roll clip, the kneel (state 8 for a handgun at 46155E), its budget and attacker - is native code.
        uint8_t* player = gameObject(0x65cbf4);
        const bool idle = player[4] == 1 && player[5] == 0, aimed = player[4] == 1 && player[5] == 5 && player[6] == 1;
        if (!equipped() || !(idle || aimed)) {
            log("GLOCK_DIAGNOSTIC dodge rejected: needs the Glock equipped and Jill idle or aimed (routine %u/%u state %u)", player[4], player[5], player[6]);
            return;
        }
        static int32_t point[3];
        const double a = double(*reinterpret_cast<const uint16_t*>(player + 0x6e) & 0xfff) * 2.0 * 3.14159265358979 / 4096.0;
        point[0] = *reinterpret_cast<const int32_t*>(player + 0x34) + int32_t(1500.0 * std::cos(a));
        point[1] = *reinterpret_cast<const int32_t*>(player + 0x38);
        point[2] = *reinterpret_cast<const int32_t*>(player + 0x3c) - int32_t(1500.0 * std::sin(a));
        const uint32_t at = uint32_t(reinterpret_cast<uintptr_t>(point));
        *reinterpret_cast<uint32_t*>(player + 0x16c) = at; *reinterpret_cast<uint32_t*>(player + 0x170) = at;
        player[0xc7] = 0x67; *reinterpret_cast<uint32_t*>(player) |= 0x80000000u;
        *reinterpret_cast<uint32_t*>(player + 4) = 0x20701;
        log("GLOCK_DIAGNOSTIC dodge: routine 1/7 state 2 (roll kind 67) from %s, weapon %02x item %02x", idle ? "idle" : "aim", player[0x46], equipped());
    }
    if (command == "glock-m92") {   // the Glock slot becomes a loaded M92F, equipped like 45DD20: the shot sound must go back
        uint8_t* player = gameObject(0x65cbf4); const uint8_t m92[4] = {3, 15, 0x01, 0x00};
        std::memcpy(inv + slot * 4, m92, 4); inv[0x128] = uint8_t(slot); inv[0x129] = 3; player[0x46] = 3;
        weaponInitHook(player); weaponLoadHook(player);
        log("GLOCK_DIAGNOSTIC slot %d is an M92F now, equipped", slot);
    }
    if (command == "glock-recoil-native" || command == "glock-recoil-soft") {   // A/B of the AUTO recoil in one session
        static const unsigned kick = kickPercent, hands = handPercent, tail = tailPercent, sway = swayAngle;
        const bool native = command == "glock-recoil-native";
        kickPercent = native ? 100 : kick; handPercent = native ? 100 : hands; tailPercent = native ? 100 : tail; swayAngle = native ? 0 : sway;
        log("GLOCK_DIAGNOSTIC recoil kick=%u%% hands=%u%% tail=%u%% sway=%u", kickPercent, handPercent, tailPercent, swayAngle);
    }
}
static void install() {
    const std::string folder = root + "\\ui-pack\\glock18c\\";
    itemiRecord = readFile(folder + "itemi.bin", 0x800); itemgRecord = readFile(folder + "itemg.bin", 0x2800); shopCell = readFile(folder + "shop-cell.bin", 56 * 48);
    shotVag = readFile(folder + "shot.vag", kShotVagEnd - kShotVag);
    const std::string ini = root + "\\pilot.ini";
    autoFrames = (std::max)(1u, (std::min)(6u, unsigned(GetPrivateProfileIntA("pilot", "glock_auto_frames", 2, ini.c_str()))));
    kickPercent = (std::min)(100u, unsigned(GetPrivateProfileIntA("pilot", "glock_auto_kick", int(kickPercent), ini.c_str())));
    handPercent = (std::min)(100u, unsigned(GetPrivateProfileIntA("pilot", "glock_auto_kick_hands", int(handPercent), ini.c_str())));
    tailPercent = (std::min)(100u, unsigned(GetPrivateProfileIntA("pilot", "glock_auto_kick_tail", int(tailPercent), ini.c_str())));
    swayAngle = (std::min)(64u, unsigned(GetPrivateProfileIntA("pilot", "glock_auto_sway", int(swayAngle), ini.c_str())));
    swayPart = (std::min)(kUpperLast, unsigned(GetPrivateProfileIntA("pilot", "glock_auto_sway_part", int(swayPart), ini.c_str())));
    kneelBudget = GetPrivateProfileIntA("pilot", "glock_kneel_budget", 1, ini.c_str()) != 0;
    kneelHoldFrames = (std::max)(2u, (std::min)(14u, unsigned(GetPrivateProfileIntA("pilot", "glock_kneel_hold_frames", int(kneelHoldFrames), ini.c_str()))));
    dodgeRoll = GetPrivateProfileIntA("pilot", "glock_dodge_always_roll", 1, ini.c_str()) != 0;   // r6 dodge (test switch)
    dodgeKneel = GetPrivateProfileIntA("pilot", "glock_dodge_always_kneel", 0, ini.c_str()) != 0;
    dodgeWide = GetPrivateProfileIntA("pilot", "glock_dodge_always_wide", 0, ini.c_str());
    dodgeWide = dodgeWide <= 0 ? 0 : dodgeWide < 100 ? 3000 : (std::min)(dodgeWide, 20000);
    installDodgeAlways(GetPrivateProfileIntA("pilot", "glock_dodge_always", 0, ini.c_str()));
    const size_t from = patches.size(); bool ok = true;
    for (uint32_t site : {0x43639u, 0x5137bu}) ok = ok && callPatch(site, 0x26380, reinterpret_cast<void*>(savLoadHook));
    for (uint32_t site : {0x2716bu, 0x42e13u}) ok = ok && callPatch(site, 0x262a0, reinterpret_cast<void*>(savSaveHook));
    if (!ok) { patches.resize(from); log("GLOCK_DATA hooks skipped: signature mismatch, no Glock reward"); return; }
    savLoadOriginal = reinterpret_cast<VoidFn>(gameBase + 0x26380); savSaveOriginal = reinterpret_cast<VoidFn>(gameBase + 0x262a0);
    if (!writeFrom(from)) { log("GLOCK_DATA hooks failed"); return; }
    installShop();
    if (re3exe::gRunning == re3exe::Exe::Main) installItems();
}
} // namespace glock
static uint8_t glockEquippedItem() { return glock::equipped(); }
static const void* glockCombatFor(const void* combat, int weapon) { return glock::combatFor(combat, weapon); }
static void glockShotDone(int weapon, const uint8_t* actor, int lifeBefore) { glock::shotDone(weapon, actor, lifeBefore); }
static void glockDiagnostic() { glock::diagnostic(); }
