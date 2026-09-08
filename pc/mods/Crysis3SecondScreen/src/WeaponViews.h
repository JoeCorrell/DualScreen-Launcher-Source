#pragma once
#include "Artwork.h"

namespace c3 {
// Fixed build-owned paths only. These are cached renders of extracted game
// meshes, not arbitrary artwork selected by an untrusted runtime file path.
// Default appearance only: no claim of live skin/attachment synchronization.
inline constexpr const char* WeaponViewKeys[]{"SCAR","Hammer"};
inline size_t LoadWeaponViews(ArtworkCatalog& catalog,const std::filesystem::path& root) {
    size_t loaded=0;
    for(const char* key:WeaponViewKeys)
        if(catalog.LoadModelView(key,root/"model-views"/(std::string(key)+".rgba")))++loaded;
    return loaded;
}
} // namespace c3
