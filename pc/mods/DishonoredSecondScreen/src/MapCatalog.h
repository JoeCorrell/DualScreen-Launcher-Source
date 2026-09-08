#pragma once
#include <array>
#include <cstdint>
#include <string_view>

namespace dishonored {
// EDisUIMap IDs and original Texture2D exports, not discovery predicates.
// Native m_ImagePath must match before a renderer asset ID is published.
struct MapAsset { uint8_t native{}; std::u16string_view object; const wchar_t* file{}; };
inline constexpr std::array<MapAsset, 13> MapAssets = {{
    {1, u"UI_Map_Boyle_SF.LevelMap_Ext_Boyle", L"map-boyle.png"},
    {2, u"UI_Map_Bridge_SF.LevelMap_Ext_Bridge", L"map-bridge.png"},
    {3, u"UI_Map_BrothelExt_SF.LevelMap_Ext_Brothel", L"map-brothel-exterior.png"},
    {4, u"UI_Map_BrothelInt_SF.LevelMap_Int_Brothel", L"map-brothel-interior.png"},
    {5, u"UI_Map_LightHouseExt1_SF.LevelMap_Ext_LightHouse_1", L"map-lighthouse-1.png"},
    {6, u"UI_Map_OverseerExt_SF.LevelMap_Ext_Overseer", L"map-overseer-exterior.png"},
    {7, u"UI_Map_OverseerInt1_SF.LevelMap_Int_Overseer_1", L"map-overseer-interior-1.png"},
    {8, u"UI_Map_OverseerInt2_SF.LevelMap_Int_Overseer_2", L"map-overseer-interior-2.png"},
    {9, u"UI_Map_Streets_SF.LevelMap_Ext_Streets", L"map-streets.png"},
    {10, u"UI_Map_TowerReturnInt_SF.LevelMap_Int_TowerReturn", L"map-tower-interior-1.png"},
    {11, u"UI_Map_LightHouseExt2_SF.LevelMap_Ext_LightHouse_2", L"map-lighthouse-2.png"},
    {12, u"UI_Map_LightHouseExt3_SF.LevelMap_Ext_LightHouse_3", L"map-lighthouse-3.png"},
    {13, u"UI_Map_TowerReturnInt2_SF.LevelMap_Int_TowerReturn_02", L"map-tower-interior-2.png"},
}};
}
