#include "Definitions.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <array>
#include <vector>

namespace dishonored {
namespace {
std::u16string_view Trim(std::u16string_view value) {
    while (!value.empty() && (value.front() == u' ' || value.front() == u'\t' || value.front() == u'\r')) value.remove_prefix(1);
    while (!value.empty() && (value.back() == u' ' || value.back() == u'\t' || value.back() == u'\r')) value.remove_suffix(1);
    return value;
}
std::u16string Decode(std::u16string_view text) {
    if (text.size() >= 2 && text.front() == u'"' && text.back() == u'"') text = text.substr(1, text.size() - 2);
    std::u16string result;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == u'\\' && i + 1 < text.size()) {
            char16_t next = text[i + 1];
            if (next == u'r' || next == u'n') { result += u'\n'; ++i; continue; }
            if (next == u'\\' || next == u'"') { result += next; ++i; continue; }
        }
        result += text[i];
    }
    return result;
}
bool LoadText(const std::wstring& path, std::u16string& text) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size{};
    bool valid = GetFileSizeEx(file, &size) && size.QuadPart > 0 && size.QuadPart <= 4 * 1024 * 1024;
    std::vector<uint8_t> bytes(valid ? std::size_t(size.QuadPart) : 0);
    DWORD read{};
    valid = valid && ReadFile(file, bytes.data(), DWORD(bytes.size()), &read, nullptr) && read == bytes.size();
    CloseHandle(file);
    if (!valid) return false;
    if (bytes.size() >= 2 && bytes[0] == 0xff && bytes[1] == 0xfe) {
        if (bytes.size() % 2) return false;
        text.clear(); text.reserve((bytes.size() - 2) / 2);
        for (std::size_t i = 2; i < bytes.size(); i += 2) text += char16_t(bytes[i] | (uint16_t(bytes[i + 1]) << 8));
    } else {
        const std::size_t skip = bytes.size() >= 3 && bytes[0] == 0xef && bytes[1] == 0xbb && bytes[2] == 0xbf ? 3 : 0;
        const char* data = reinterpret_cast<const char*>(bytes.data() + skip);
        const int count = int(bytes.size() - skip);
        UINT codepage = CP_UTF8; DWORD flags = MB_ERR_INVALID_CHARS;
        int length = MultiByteToWideChar(codepage, flags, data, count, nullptr, 0);
        if (!length && !skip) { codepage = CP_ACP; flags = 0; length = MultiByteToWideChar(codepage, flags, data, count, nullptr, 0); }
        if (!length) return false;
        std::wstring wide(std::size_t(length), L'\0');
        if (MultiByteToWideChar(codepage, flags, data, count, wide.data(), length) != length) return false;
        text.assign(wide.begin(), wide.end());
    }
    return text.find(u'\0') == std::u16string::npos;
}
}
bool Definitions::LoadRpg(const std::wstring& path) {
    std::u16string source;
    if (!LoadText(path, source)) return false;
    std::map<std::u16string, std::u16string, std::less<>> next;
    std::u16string_view remaining(source); uint8_t section = 0;
    while (!remaining.empty()) {
        std::size_t end = remaining.find(u'\n');
        auto line = Trim(remaining.substr(0, end));
        if (end == std::u16string_view::npos) remaining = {};
        else remaining.remove_prefix(end + 1);
        if (line.empty() || line.front() == u';') continue;
        if (line.front() == u'[') {
            section = line == u"[DisUISelectionType]" ? 1 : line == u"[DisAmmoType]" ? 2 : 0;
            continue;
        }
        if (!section) continue;
        const auto equals = line.find(u'=');
        if (equals == std::u16string_view::npos || equals == 0) continue;
        auto key = Trim(line.substr(0, equals)), value = Trim(line.substr(equals + 1));
        if (key.size() > 128 || value.size() > 65535 || next.size() >= 512) return false;
        next[(section == 2 ? u"Ammo/" : u"") + std::u16string(key)] = Decode(value);
    }
    if (next.empty()) return false;
    strings_ = std::move(next);
    struct Mapping { uint8_t id; const char16_t* key; };
    // Exact cooked eDisUISelectionType identifiers, also used by 00c4bec0.
    constexpr Mapping mappings[] = {{2,u"Blink"},{3,u"BendTime"},{4,u"WindBlast"},{5,u"Possess"},{6,u"DarkVision"},
        {7,u"DevouringSwarm"},{8,u"Vitality"},{9,u"Celerity"},{10,u"BloodThirsty"},{11,u"ShadowKill"},
        {20,u"Transversal"},{21,u"SummonAssassin"},{22,u"DaudDarkVision"},{23,u"ArcaneBond"},{27,u"Pull"}};
    for (auto& power : powers_) power = {};
    for (const auto& mapping : mappings) {
        auto& power = powers_[mapping.id];
        power.name = Lookup(mapping.key);
        power.first = Lookup(std::u16string(mapping.key) + u"_1_Description");
        power.second = Lookup(std::u16string(mapping.key) + u"_2_Description");
    }
    // Native 00c4c040/00c4c1c0 use the installed eDisAmmoType identifier
    // (without its prefix) and optional _Description in [DisAmmoType].
    constexpr const char16_t* ammunition[] = {u"Bullet",u"Bullet_Explosive",u"Arrow",u"Arrow_Sleep",
        u"Arrow_Flare",u"SpringRazor",u"Grenade",u"StickyGrenade",u"Arrow_Explosive",u"ArcMine",u"FlashSmoke",u"ArcMineStun"};
    for (uint8_t i = 0; i < 12; ++i) {
        const std::u16string key = u"Ammo/" + std::u16string(ammunition[i]);
        ammunition_[i] = {std::u16string(Lookup(key)), std::u16string(Lookup(key + u"_Description"))};
    }
    // Journal 00bd1bb0 maps the two live elixir counters to selection17/18;
    // 00c45300 obtains their plain _Description strings.
    for (uint8_t i = 0; i < 2; ++i) {
        const std::u16string key = i ? u"Mana" : u"Health";
        consumables_[i] = {std::u16string(Lookup(key)), std::u16string(Lookup(key + u"_Description"))};
    }
    return true;
}
std::u16string_view Definitions::Lookup(std::u16string_view key) const noexcept {
    const auto found = strings_.find(key);
    return found == strings_.end() ? std::u16string_view{} : std::u16string_view(found->second);
}
bool Definitions::LoadMapNames(const std::wstring& path) {
    std::u16string source;
    if (!LoadText(path, source)) return false;
    std::map<std::u16string, std::u16string, std::less<>> next;
    std::u16string_view remaining(source); bool section = false;
    while (!remaining.empty()) {
        const auto end = remaining.find(u'\n');
        auto line = Trim(remaining.substr(0, end));
        if (end == std::u16string_view::npos) remaining = {};
        else remaining.remove_prefix(end + 1);
        if (line.empty() || line.front() == u';') continue;
        if (line.front() == u'[') { section = line == u"[MapNames]"; continue; }
        if (!section) continue;
        const auto equals = line.find(u'=');
        if (equals == std::u16string_view::npos || !equals) continue;
        auto key = std::u16string(Trim(line.substr(0, equals)));
        const auto value = Trim(line.substr(equals + 1));
        if (key.empty() || key.size() >= 128 || value.size() > 1024 || next.size() >= 512) return false;
        for (auto& character : key) {
            if (character >= u'A' && character <= u'Z') character += u'a' - u'A';
            if (!((character >= u'a' && character <= u'z') || (character >= u'0' && character <= u'9') || character == u'_')) return false;
        }
        next[std::move(key)] = Decode(value);
    }
    if (next.empty()) return false;
    mapNames_ = std::move(next); return true;
}
std::u16string_view Definitions::MapName(std::u16string_view package) const noexcept {
    // Native MapNames lookup uses a case-insensitive FName. Normalize bounded
    // ASCII package identifiers on the stack, with no allocation on the tick.
    std::array<char16_t, 128> key{};
    if (package.empty() || package.size() >= key.size()) return {};
    for (std::size_t i = 0; i < package.size(); ++i) {
        const auto character = package[i];
        key[i] = character >= u'A' && character <= u'Z' ? character + (u'a' - u'A') : character;
    }
    const auto found = mapNames_.find(std::u16string_view(key.data(), package.size()));
    return found == mapNames_.end() ? std::u16string_view{} : std::u16string_view(found->second);
}
std::u16string_view Definitions::PowerName(uint8_t selection) const noexcept {
    return selection < 28 ? std::u16string_view(powers_[selection].name) : std::u16string_view{};
}
std::u16string_view Definitions::PowerDescription(uint8_t selection, int level) const noexcept {
    if (selection >= 28) return {};
    return level >= 2 ? std::u16string_view(powers_[selection].second) : std::u16string_view(powers_[selection].first);
}
uint64_t Definitions::PowerArtwork(uint8_t selection) noexcept {
    // Journal sprite140 frame labels -> shape bitmap IDs, retained in
    // power-artwork-evidence.json. Include the four native passive powers.
    return selection >= 2 && selection <= 11 ? uint64_t(100 + selection) : 0;
}
std::u16string_view Definitions::AmmoName(uint8_t type) const noexcept {
    return type < 12 ? std::u16string_view(ammunition_[type].name) : std::u16string_view{};
}
std::u16string_view Definitions::AmmoDescription(uint8_t type) const noexcept {
    return type < 12 ? std::u16string_view(ammunition_[type].description) : std::u16string_view{};
}
std::u16string_view Definitions::ConsumableName(uint8_t type) const noexcept {
    return type < 2 ? std::u16string_view(consumables_[type].name) : std::u16string_view{};
}
std::u16string_view Definitions::ConsumableDescription(uint8_t type) const noexcept {
    return type < 2 ? std::u16string_view(consumables_[type].description) : std::u16string_view{};
}
} // namespace dishonored
