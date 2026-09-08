#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <string_view>

namespace dishonored {

// Immutable original localized definitions, loaded on the bootstrap worker.
// The path must come from the game's verified active localization directory.
// This contains no saved/player state and never grants an ability or item.
class Definitions {
public:
    bool LoadRpg(const std::wstring& path);
    bool LoadMapNames(const std::wstring& path);
    std::u16string_view MapName(std::u16string_view package) const noexcept;
    std::u16string_view PowerName(uint8_t selection) const noexcept;
    std::u16string_view PowerDescription(uint8_t selection, int level) const noexcept;
    static uint64_t PowerArtwork(uint8_t selection) noexcept;
    std::u16string_view AmmoName(uint8_t type) const noexcept;
    std::u16string_view AmmoDescription(uint8_t type) const noexcept;
    std::u16string_view ConsumableName(uint8_t type) const noexcept;
    std::u16string_view ConsumableDescription(uint8_t type) const noexcept;
private:
    std::u16string_view Lookup(std::u16string_view key) const noexcept;
    struct Power { std::u16string name, first, second; };
    std::map<std::u16string, std::u16string, std::less<>> strings_;
    std::map<std::u16string, std::u16string, std::less<>> mapNames_;
    Power powers_[28]{};
    struct Item { std::u16string name, description; };
    Item ammunition_[12]{}, consumables_[2]{};
};

} // namespace dishonored
