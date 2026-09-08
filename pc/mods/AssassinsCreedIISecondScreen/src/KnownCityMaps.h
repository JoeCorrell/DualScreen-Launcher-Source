#pragma once
#include <cstdint>
namespace ac2ds::assets {
// Native world/FEU/MapDesc/texture chain verified in all six original city archives.
// Sizes and FNV fingerprints are compatibility checks, not a security boundary.
struct CityMapBinding { const wchar_t* file; uint32_t world,art,description,texture;
    uint32_t artSize,artHash,descriptionSize,descriptionHash,textureSize,textureHash; };
inline constexpr CityMapBinding CityMapBindings[]{
    {L"DataPC_Firenze.forge",0x5b29460a,0x52a019fa,0x220049ee,0x220049f1,481,0x182a6367,242,0x2b1631df,1048754,0x92206d36},
    {L"DataPC_Venezia.forge",0x48b659dc,0xa1d3c38c,0x5e5d5665,0x5e5d5668,488,0x6a420489,237,0x8913ec34,2097325,0x07cb3d20},
    {L"DataPC_Wetlands.forge",0x701bc87d,0xa9ff302c,0xa1d3c38e,0xa1d3c391,482,0x78c19a2e,243,0x2b91375b,1048755,0x0f3a0e41},
    {L"DataPC_Mountains.forge",0x6ad1d250,0xd8508c6e,0xca8ef908,0xca8ef90b,490,0x60bd723b,244,0x1e2ed2ec,2097332,0x0146f9b8},
    {L"DataPC_Toscana.forge",0x7c82d813,0x2f78336d,0x2d8bdf45,0x2d8bdf48,481,0x529ad5ba,242,0x4fa49d40,1048754,0xdd06c720},
    {L"DataPC_Villa.forge",0x83ac7022,0xa2e385f2,0xa268487b,0xa268487e,479,0x37f12af3,240,0x247ef62c,1048752,0x5270c06e},
};
}
