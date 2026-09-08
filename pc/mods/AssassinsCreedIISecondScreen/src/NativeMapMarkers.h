#pragma once
#include "NativeMap.h"
#include "NativeReadTransaction.h"
#include <set>

namespace ac2ds::native {
struct MapMarker { uint32_t id{},type{};float x{},y{}; };
inline uint32_t MapMarkerTitle(uint32_t type){
    // Original MapIcon.MapMarker_Names localization IDs, including empty slots.
    constexpr uint32_t ids[]{282632,276380,276381,276382,282904,282904,282904,282904,282904,282904,282904,
        276373,276369,276372,276371,276388,276388,276370,282633,0,276391,276392,276393,276394,276395,0,
        276396,276399,276397,276398,276400,276401,0,276402,276403,276404,276405,276406,276407,276408,
        276409,276410,276411,276412,0,276433,276431,276375,276435,276437,285017,0,0,0,282904,0,0,0,0,286448,276391};
    return type<61?ids[type]:0;
}
inline uint32_t MapMarkerArtwork(uint32_t type){
    // Authored AC II v2 icon resources. Unknown/special variants keep the neutral
    // marker fallback rather than displaying an unrelated category's artwork.
    switch(type){
        case 1:return 0x5e5d556e;case 2:return 0x5e5d5546;case 3:return 0x5e5d555a;
        case 4:case 5:case 6:case 7:case 8:case 9:case 10:case 54:return 0x5e5d5596;
        case 12:return 0x5e5d55d2;case 13:return 0x5e5d55b4;case 14:return 0x5e5d55be;
        case 15:case 16:return 0x5e5d5528;case 17:return 0x5e5d55c8;
        case 20:case 21:case 60:return 0x5e5d558c;case 22:return 0x5e5d55e6;
        case 23:return 0x5e5d55dc;case 24:return 0x5e5d560e;case 26:return 0x5e5d55a0;
        case 27:return 0x18f37170;case 28:return 0x18f3715c;case 29:return 0x18f37166;
        case 30:return 0x5e5d5582;case 31:return 0x5e5d55f0;case 33:return 0x5e5d5532;
        case 34:return 0x5e5d564a;case 35:return 0x5e5d562c;case 36:return 0x5e5d5640;
        case 37:return 0x5e5d5636;case 38:return 0x5e5d5622;case 39:return 0x5e5d5618;
        case 41:return 0x5e5d5550;case 42:return 0x5e5d5604;case 43:return 0x5e5d55fa;
        case 45:return 0x77b3426a;case 46:return 0x18f370ee;case 47:return 0x77b3424c;
        case 48:return 0x77b34256;case 49:return 0x18f37102;case 50:return 0x9dce802e;
        case 51:return 0x7bf60a99;case 52:return 0x7bf60aa3;case 53:return 0x7bf60aad;
        case 59:return 0x42843bef;default:return 0;
    }
}
struct MarkerContext { uint32_t global{};uint8_t flags{};std::optional<bool> notoriety,region; };
inline bool MarkerClass(uint32_t type){return type==0x1e765c0 || type==0x1e598a0 || type==0x1e7f7f0 || type==0x1e9a510;}
inline std::optional<MarkerContext> ReadMarkerContext(const Memory& memory,const Roots& roots,uint32_t manager){
    MarkerContext result;uint32_t type{};
    if(!memory.Read(0x223f820,result.global) || !memory.Member(manager,0x24,result.flags))return {};
    if(result.global && (!memory.Read(result.global,type) || type!=0x1e60898))return {};
    PointerArray components{};if(!ArrayHeader(memory,roots.world,0x60,256,components))return {};
    result.notoriety=false;
    for(uint32_t i=0;i<components.Count();++i){
        uint32_t p{};if(!memory.Field(components.data,i*4,p) || !memory.Read(p,type))return {};
        if(type!=0x1e575c8)continue;
        uint8_t override{},forced{};float value{};
        if(!memory.Read(0x223f648,override) || !memory.Member(p,0x64,forced) ||
            !(override?memory.Read(0x211c058,value):memory.Member(p,0xc,value)) || !std::isfinite(value))return {};
        result.notoriety=value>0 || forced;
    }
    uint32_t playerWorld{},component{};
    if(!memory.Field(roots.player,0x6c,playerWorld))return {};
    result.region=false;
    if(playerWorld){
        if(!memory.Field(playerWorld,0x610,component))return {};
        if(!component)result.region.reset(); // Native lazy cache refresh is intentionally not invoked.
        else{
            uint32_t descriptor{},entity{},bits{},word{};uint16_t bit{};
            if(!memory.Read(0x21b1904,descriptor) || !memory.Member(descriptor,0x42,bit) || bit>=4096 ||
                !memory.Field(component,8,entity) || !memory.Field(entity,0x40,bits) || !memory.Field(bits,(bit/32)*4,word))return {};
            result.region=(word&(1u<<(bit%32)))!=0;
        }
    }
    return result;
}
inline std::optional<bool> MarkerVisible(const Memory& memory,uint32_t marker,const MarkerContext& context){
    uint32_t cls{},type{};uint8_t flags{},published{};
    if(!memory.Read(marker,cls) || !memory.Field(marker,0x20,type) || !memory.Member(marker,0x3a,flags) ||
        !memory.Member(marker,0x3b,published))return {};
    if(!(published&1) || type==18)return false; // 012a1d80 city-map visitor.
    if(!MarkerClass(cls))return {};
    if(context.flags&1)return true;
    if(cls==0x1e598a0){ // 010efe60 -> 0114e850 city gate unlock.
        if(!context.global)return false;uint32_t city{};if(!memory.Field(marker,0xb0,city))return {};
        for(uint32_t i=0;i<6;++i){uint32_t id{};uint8_t visible{};
            if(!memory.Field(context.global,0x60+i*24,id) || !memory.Member(context.global,0x74+i*24,visible))return {};
            if(id==city)return visible!=0;
        }
        PointerArray extra{};if(!ArrayHeader(memory,context.global,0xf0,128,extra) ||
            (extra.Count() && extra.data>UINT32_MAX-extra.Count()*24))return {};
        for(uint32_t i=0;i<extra.Count();++i){uint32_t id{};uint8_t visible{};
            if(!memory.Field(extra.data,i*24,id) || !memory.Member(extra.data,i*24+0x14,visible))return {};
            if(id==city)return visible!=0;
        }
        return true;
    }
    if(flags&2)return false;
    if(cls==0x1e7f7f0){ // 01341630: active members of a native marker group.
        PointerArray entities{},states{};
        if(!ArrayHeader(memory,marker,0x28,1024,entities) || !ArrayHeader(memory,marker,0xb0,1024,states))return {};
        if(!entities.Count())return false;
        for(uint32_t i=0;i<states.Count();++i){uint8_t state{};if(!memory.Member(states.data,i,state))return {};if(!state)return true;}
        return false;
    }
    if(cls==0x1e9a510){uint32_t state{};if(!memory.Field(marker,0xb0,state))return {};return (state==2 || state==3) && (flags&1);}
    if(type>=61)return false;
    // Literal dispatch table from 012a6af0, accepted only by the full PE hash gate.
    constexpr uint8_t rules[]{0,2,2,2,7,7,7,7,7,7,7,1,4,4,4,4,4,4,1,0,1,1,2,2,2,0,1,4,4,4,1,3,2,5,5,5,5,5,2,2,2,2,3,3,1,5,5,5,5,5,5,4,4,4,1,1,1,1,1,2,1};
    bool discovery=!(context.flags&2);
    if(context.global && type){
        PointerArray overrides{};if(!ArrayHeader(memory,context.global,0x138,128,overrides) ||
            (overrides.Count() && overrides.data>UINT32_MAX-overrides.Count()*8))return {};
        for(uint32_t i=0;i<overrides.Count();++i){uint32_t id{};uint8_t value{};
            if(!memory.Field(overrides.data,i*8,id) || !memory.Member(overrides.data,i*8+4,value))return {};
            if(id==type){if(value)discovery=false;break;}
        }
    }
    bool discovered=!discovery || (flags&4);
    switch(rules[type]){
        case 1:return true;case 2:return (flags&1)!=0;
        case 3:return context.notoriety?std::optional<bool>{!*context.notoriety}:std::nullopt;
        case 4:return discovered;case 5:return (flags&1) && discovered;
        case 6:return context.region;
        case 7:if(!discovered)return false;return context.region;
        default:return false;
    }
}
inline std::optional<std::array<float,4>> MapEntityPosition(const Memory& memory,uint32_t entity){
    uint32_t parent{};std::array<float,4> position{};
    if(!memory.Field(entity,0x50,parent) || parent || !memory.Member(entity,0x40,position))return {};
    for(unsigned i=0;i<3;++i)if(!std::isfinite(position[i]))return {};return position;
}
inline std::optional<std::array<float,4>> MapMarkerPosition(const Memory& memory,uint32_t marker,unsigned depth=0){
    uint32_t cls{},type{};if(depth>=8 || !memory.Read(marker,cls) || !MarkerClass(cls) || !memory.Field(marker,0x20,type))return {};
    uint32_t state{};bool special=cls==0x1e9a510 && type>=27 && type<=29;
    if(special && !memory.Field(marker,0xb0,state))return {};special=special && state==3;
    PointerArray entities{},states{};if(!ArrayHeader(memory,marker,0x28,1024,entities))return {};
    if(cls==0x1e7f7f0 && !ArrayHeader(memory,marker,0xb0,1024,states))return {};
    std::array<float,4> position{};uint32_t count=0;
    for(uint32_t i=0;i<entities.Count();++i){
        if(cls==0x1e7f7f0 && type!=1 && type!=2 && type!=3 && type!=40 && type!=41 && type!=32){
            uint8_t excluded{};if(i>=states.Count() || !memory.Member(states.data,i,excluded))return {};if(excluded)continue;
        }
        uint32_t handle{},entity{};if(!memory.Field(entities.data,i*4,handle) || !memory.Reference(handle,entity))return {};
        if(!entity)continue;auto p=MapEntityPosition(memory,entity);if(!p)return {};
        for(unsigned axis=0;axis<3;++axis)position[axis]+=(*p)[axis];++count;
    }
    if(count){for(unsigned axis=0;axis<3;++axis)position[axis]/=count;return position;}
    if(special){if(!memory.Member(marker,0xc0,position))return {};return position;}
    uint32_t handle{},entity{},linked{};
    if(!memory.Field(marker,0x30,handle) || !memory.Reference(handle,entity))return {};
    if(entity)return MapEntityPosition(memory,entity);
    if(!memory.Field(marker,0x84,linked))return {};
    if(linked && type!=26)return MapMarkerPosition(memory,linked,depth+1);
    if(!memory.Member(marker,0x10,position))return {};return position;
}
inline std::optional<std::vector<MapMarker>> ReadMapMarkers(const Memory& source,const Roots& roots,const MapState& map){
    ReadTransaction memory(source);Roots current{};
    if(!ReadRoots(memory,current) || !(current==roots))return {};
    uint32_t owner{},handle{},data{};
    if(!memory.Field(map.manager,4,owner) || owner!=roots.world || !memory.Field(map.manager,8,handle) ||
        !memory.Reference(handle,data) || data!=map.data)return {};
    std::array<float,6> projection{};
    if(!memory.Member(map.data,0x34,projection) || projection!=std::array<float,6>{map.width,map.height,map.scaleX,map.scaleY,map.offsetX,map.offsetY})return {};
    auto context=ReadMarkerContext(memory,roots,map.manager);if(!context)return {};
    std::vector<uint32_t> pointers;
    auto append=[&](uint32_t owner,uint32_t offset,bool wrapper){
        PointerArray array{};if(!ArrayHeader(memory,owner,offset,4096,array) || pointers.size()+array.Count()>4096)return false;
        for(uint32_t i=0;i<array.Count();++i){uint32_t p{};
            if(!memory.Field(array.data,i*4,p) || (wrapper && p && !memory.Read(p,p)))return false;
            if(p)pointers.push_back(p);
        }return true;
    };
    if(context->global){uint32_t singleton{};if(!memory.Field(context->global,0x18,singleton))return {};if(singleton)pointers.push_back(singleton);}
    if(!append(map.data,0x10,false) || !append(map.manager,0xc,false) ||
        (context->global && !append(context->global,0x20,true)))return {};
    std::set<uint32_t> seen;std::vector<MapMarker> result;
    for(auto p:pointers){
        if(!seen.insert(p).second)continue;auto visible=MarkerVisible(memory,p,*context);if(!visible || !*visible)continue;
        auto position=MapMarkerPosition(memory,p);if(!position)continue;
        MapMarker item;item.id=p;if(!memory.Field(p,0x20,item.type))return {};
        item.x=float((double((*position)[0])*map.scaleX+map.offsetX)/map.width);
        item.y=float((double((*position)[1])*map.scaleY+map.offsetY)/map.height);
        if(std::isfinite(item.x) && std::isfinite(item.y) && item.x>=0 && item.x<=1 && item.y>=0 && item.y<=1)result.push_back(item);
    }
    if(!memory.Validate())return {};return result;
}
}
