#pragma once
#include "Model.h"
#include <cstring>
#include <string_view>

namespace c3 {
// Value-only capture of map_sp.gfx's clearAll/addIcon/addObjective/update calls.
// Coordinates remain movie-local; this neither calibrates a world map nor
// grants enemy discovery, player identity, native IDs, actions or a hooking ABI.
struct MovieMapIcon { float x{},y{},rotation{},scale{}; Text frame{}; };
struct MovieMapObjective {
    Text message{},description{};
    bool secondary{},showOnRadar{};
    int32_t rawStatus{}; // 1/2 select different movie frames; no native enum inferred.
    uint32_t iconSlot{};
};
struct MovieMapBackground { Text path{}; Number rotation{}; };
class MapCapture {
    uint64_t generation_{},iconsAt_{},objectivesAt_{};
    Key movie_{};
    bool building_{},valid_{},iconsSealed_{},objectivesSealed_{};
    MovieMapBackground background_{};
    List<MovieMapIcon,32> icons_{};
    List<MovieMapObjective,64> objectives_{};
    bool Owner(uint64_t generation,Key movie) const { return generation && movie && generation==generation_ && movie==movie_; }
    bool Fail() { valid_=false; iconsAt_=objectivesAt_=0; return false; }
    bool Adding(uint64_t generation,Key movie) {
        return (Owner(generation,movie) && building_ && valid_ && !iconsSealed_ && !objectivesSealed_) || Fail();
    }
    static bool Copy(Text& target,std::string_view text) {
        if(text.size()>=target.bytes.size() || text.find('\0')!=std::string_view::npos) return false;
        target={}; std::memcpy(target.bytes.data(),text.data(),text.size()); return true;
    }
    static bool Fresh(uint64_t sampled,uint64_t now) { return sampled && now>=sampled && now-sampled<=MaxAgeMs; }
public:
    void Invalidate(uint64_t generation,Key movie) {
        generation_=generation; movie_=movie; background_={}; icons_={}; objectives_={};
        iconsAt_=objectivesAt_=0; building_=valid_=iconsSealed_=objectivesSealed_=false;
    }
    bool SetBackground(uint64_t generation,Key movie,std::string_view path,Number rotation={}) {
        if(!Owner(generation,movie) || (rotation.known && !std::isfinite(rotation.value))) return false;
        // Empty/undefined path preserves the previous background in the movie.
        MovieMapBackground next=background_;
        if(!path.empty() && !Copy(next.path,path)) return false;
        if(rotation.known) next.rotation=rotation;
        background_=next; return true;
    }
    void ClearAll(uint64_t generation,Key movie) {
        if(!Owner(generation,movie)) Invalidate(generation,movie);
        icons_={}; objectives_={}; iconsAt_=objectivesAt_=0;
        building_=valid_=generation && movie; iconsSealed_=objectivesSealed_=false;
        // clearAll deliberately does not clear m_mapBackground/m_mapRotation.
    }
    bool AddIcon(uint64_t generation,Key movie,float x,float y,float rotation,float scale,std::string_view frame) {
        if(!Adding(generation,movie) || !std::isfinite(x) || !std::isfinite(y) ||
            !std::isfinite(rotation) || !std::isfinite(scale) || scale<0 || icons_.count>=icons_.entries.size()) return Fail();
        MovieMapIcon icon{x,y,rotation,scale,{}};
        if(!Copy(icon.frame,frame)) return Fail();
        icons_.entries[icons_.count++]=icon; return true;
    }
    bool AddObjective(uint64_t generation,Key movie,float x,float y,float scale,std::string_view message,
        std::string_view description,bool secondary,int32_t status,bool showOnRadar) {
        if(!Adding(generation,movie)) return false;
        // The movie rejects finished secondary entries before adding an icon.
        if(secondary && status!=0) return true;
        if(objectives_.count>=objectives_.entries.size()) return Fail();
        MovieMapObjective objective{};
        if(!Copy(objective.message,message) || !Copy(objective.description,description)) return Fail();
        objective.secondary=secondary; objective.rawStatus=status; objective.showOnRadar=showOnRadar;
        objective.iconSlot=icons_.count;
        if(!AddIcon(generation,movie,x,y,0,scale,status!=0?"done":secondary?"secondary":"primary")) return false;
        // m_objectivesData.unshift preserves reverse insertion order.
        for(size_t i=objectives_.count;i>0;--i) objectives_.entries[i]=objectives_.entries[i-1];
        objectives_.entries[0]=objective; ++objectives_.count; return true;
    }
    bool UpdateIcons(uint64_t generation,Key movie,uint64_t now) {
        if(!Owner(generation,movie) || !building_ || !valid_ || iconsSealed_ || !now) return false;
        icons_.known=icons_.complete=true; iconsAt_=now; iconsSealed_=true; return true;
    }
    bool UpdateObjectives(uint64_t generation,Key movie,uint64_t now) {
        if(!Owner(generation,movie) || !building_ || !valid_ || objectivesSealed_ || !now) return false;
        if(!iconsSealed_ && !UpdateIcons(generation,movie,now)) return false;
        objectives_.known=objectives_.complete=true; objectivesAt_=now; objectivesSealed_=true;
        building_=false; return true;
    }
    MovieMapBackground Background(uint64_t generation,Key movie) const {
        return Owner(generation,movie)?background_:MovieMapBackground{};
    }
    List<MovieMapIcon,32> Icons(uint64_t generation,Key movie,uint64_t now) const {
        return Owner(generation,movie) && valid_ && Fresh(iconsAt_,now)?icons_:List<MovieMapIcon,32>{};
    }
    List<MovieMapObjective,64> Objectives(uint64_t generation,Key movie,uint64_t now) const {
        return Owner(generation,movie) && valid_ && Fresh(objectivesAt_,now)?objectives_:List<MovieMapObjective,64>{};
    }
};
} // namespace c3
