#pragma once

#include <cstdint>

namespace fomds {

inline constexpr wchar_t kGameExecutableName[] = L"FieldsOfMistria.exe";
inline constexpr wchar_t kOriginalSdlName[] =
    L"SDL3_FieldsOfMistriaOriginal.dll";
inline constexpr char kExpectedGameVersion[] = "1.0.4";
inline constexpr char kExpectedGameFileVersion[] = "1.0.4.0";
inline constexpr char kExpectedSdlVersion[] = "3.4.8.0";
inline constexpr std::uint64_t kExpectedGameSize = 13657696ull;
inline constexpr std::uint64_t kExpectedSdlSize = 2811488ull;
inline constexpr char kExpectedGameSha256[] =
    "aed5b1f5d5cd75afee86f0cf347655a7e62be361d775d6ffc0671b388660ee2e";
inline constexpr char kExpectedSdlSha256[] =
    "532b231761a6d6c391069865bde37f6d069f3beedbcf3cfbd0e314bf25ca3157";

inline constexpr wchar_t kPanelClassName[] =
    L"FieldsOfMistriaSecondScreenPanel";
inline constexpr wchar_t kPanelTitle[] = L"Fields of Mistria Dual-Screen";

}  // namespace fomds
