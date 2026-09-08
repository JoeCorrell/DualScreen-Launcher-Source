#pragma once
#include "NativeInventory.h"
#include <algorithm>
#include <cmath>

namespace ac2ds::native {
// Exact-build evidence: 0046cb50/012ab680 (owner), 0129ef60 (projection),
// 0129fbf0 (discovery stamp), 00b5f330 (A8 CPU shadow), 004161f0 (position).
// No engine lock/refresh/allocation is called. In particular, an absent CPU
// shadow or unverified/dirty transform never falls back to exposing the whole city.
struct MapState {
    uint32_t manager{}, data{}, artwork{}, title{}, maskWidth{}, maskHeight{};
    float width{}, height{}, scaleX{}, scaleY{}, offsetX{}, offsetY{}, x{}, y{};
    std::optional<float> heading; // Radians clockwise from map-up.
    std::vector<uint8_t> discovery; // Top-down A8, 255 means discovered.
};
inline std::optional<MapState> ReadMap(const Memory& memory, const Roots& roots) {
    if (!roots.world || !roots.player) return {};
    PointerArray components{};
    if (!ArrayHeader(memory, roots.world, 0x60, 256, components)) return {};
    std::vector<uint32_t> pointers(components.Count()), afterPointers(pointers.size());
    if (!pointers.empty() && !memory.Copy(components.data, pointers.data(), pointers.size() * 4)) return {};
    MapState result;
    for (auto pointer : pointers) {
        uint32_t type{};
        if (!memory.Read(pointer, type)) return {};
        if (type != 0x01e7682c) continue;
        if (result.manager) return {}; // Ambiguous owners are never guessed.
        result.manager = pointer;
    }
    std::array<uint32_t, 3> manager{}, afterManager{};
    std::array<uint32_t, 34> data{}, afterData{};
    std::array<uint32_t, 4> mapHandle{}, maskHandle{}, afterHandle{};
    std::array<uint32_t, 22> texture{}, afterTexture{};
    std::array<uint32_t, 17> player{}, afterPlayer{};
    std::array<uint32_t,6> controller{},afterController{};
    uint32_t providerFlags{},afterProviderFlags{};
    uint8_t managerFlags{},afterManagerFlags{};
    if (!memory.Read(result.manager, manager) || manager[0] != 0x01e7682c || manager[1] != roots.world ||
        !memory.Reference(manager[2], result.data) || !result.data || !memory.Read(result.data, data) ||
        data[0] != 0x01e7579c || !memory.Read(data[3], mapHandle) || !mapHandle[3] ||
        !memory.Read(data[0x6c/4], maskHandle) || !memory.Member(result.manager,0x24,managerFlags) ||
        !memory.Member(roots.player, 0x10, player)) return {};
    // 004161f0 resolves +50 through 0045f880. Its 0045f770 predicate returns
    // false for source kind 0, or kind 1 with zero provider flags. For kind 1
    // its predicate ANDs [provider] with a TLS-dependent mask, so zero is false
    // on every game thread. The canonical matrix is [controller+40]+10 and
    // must belong to this player. No engine refresh/composition is necessary.
    // Nonzero provider flags and dynamic source kind 2 remain guarded.
    if(player[16] && (!memory.Member(player[16],0x40,controller) ||
        controller[0]!=roots.player || (controller[4]!=0 && controller[4]!=1))) return {};
    if(player[16] && controller[4]==1 &&
        (!controller[5] || !memory.Read(controller[5],providerFlags) || providerFlags!=0)) return {};
    result.artwork = mapHandle[3]; result.title = data[0x74/4];
    auto scalar = [&](size_t index) { float v; std::memcpy(&v, &data[index], 4); return v; };
    result.width = scalar(0x34/4); result.height = scalar(0x38/4);
    result.scaleX = scalar(0x3c/4); result.scaleY = scalar(0x40/4);
    result.offsetX = scalar(0x44/4); result.offsetY = scalar(0x48/4);
    for (float v : {result.width,result.height,result.scaleX,result.scaleY,result.offsetX,result.offsetY})
        if (!std::isfinite(v) || std::abs(v) > 1000000) return {};
    if (result.width <= 0 || result.height <= 0 || !result.scaleX || !result.scaleY) return {};
    float px{}, py{}; std::memcpy(&px,&player[12],4); std::memcpy(&py,&player[13],4);
    result.x = float((double(px) * result.scaleX + result.offsetX) / result.width);
    result.y = float((double(py) * result.scaleY + result.offsetY) / result.height);
    if (!std::isfinite(result.x) || !std::isfinite(result.y) || result.x < 0 || result.x > 1 || result.y < 0 || result.y > 1) return {};
    // 013241a0 updates MiniMap:HeroIcon from the world transform's second
    // row (player+20/+24 when +50 is null), separately from camera rotation.
    // Project this world direction into our fixed map instead of copying the
    // rotating minimap movie's final angle. Existing player recheck covers it.
    float forwardX{},forwardY{};
    std::memcpy(&forwardX,&player[4],4);std::memcpy(&forwardY,&player[5],4);
    if(std::isfinite(forwardX) && std::isfinite(forwardY) && std::abs(forwardX)<=1000000 && std::abs(forwardY)<=1000000){
        const double dx=double(forwardX)*result.scaleX,dy=double(forwardY)*result.scaleY;
        if(std::hypot(dx,dy)>1e-6)result.heading=float(std::atan2(dx,-dy));
    }
    // 012a2394 disables fog when +81 or manager+24 bit1 requests it, or no
    // mask is configured (Mountains). An unloaded nonzero UID NEVER qualifies.
    const bool declaredWithoutFog=result.artwork==0xd8508c6e && !maskHandle[3] && !maskHandle[0];
    bool fogDisabled=((data[0x80/4]>>8)&255) || (managerFlags&2) || declaredWithoutFog;
    if(fogDisabled){result.maskWidth=result.maskHeight=1;result.discovery={255};}
    else{
        if(!(maskHandle[2]&0x80000000u) || !memory.Read(maskHandle[0],texture) ||
            texture[0]!=0x01db7ed0 || texture[4]!=1 || texture[5]!=7)return {};
        result.maskWidth=texture[2];result.maskHeight=texture[3];
        if(!result.maskWidth || !result.maskHeight || result.maskWidth>512 || result.maskHeight>512)return {};
        const size_t length=result.maskWidth*result.maskHeight;const uint32_t shadow=texture[0x54/4];
        if(shadow<0x10000 || shadow>UINT32_MAX-length)return {};
        result.discovery.resize(length);std::vector<uint8_t> afterMask(length);
        auto copy=[&](std::vector<uint8_t>& bytes){
            for(size_t at=0;at<length;at+=65536)if(!memory.Copy(shadow+uint32_t(at),bytes.data()+at,std::min(size_t(65536),length-at)))return false;
            return true;
        };
        if(!copy(result.discovery) || !copy(afterMask) || result.discovery!=afterMask ||
            !memory.Read(maskHandle[0],afterTexture) || texture!=afterTexture)return {};
    }
    if (!memory.Read(data[0x6c/4], afterHandle) || maskHandle != afterHandle ||
        !memory.Read(data[3], afterHandle) || mapHandle != afterHandle ||
        !memory.Member(roots.player, 0x10, afterPlayer) || player != afterPlayer ||
        !memory.Read(result.data, afterData) || data != afterData ||
        !memory.Read(result.manager, afterManager) || manager != afterManager ||
        !memory.Member(result.manager,0x24,afterManagerFlags) || managerFlags!=afterManagerFlags) return {};
    if(player[16] && (!memory.Member(player[16],0x40,afterController) || controller!=afterController))return {};
    if(player[16] && controller[4]==1 &&
        (!memory.Read(controller[5],afterProviderFlags) || providerFlags!=afterProviderFlags))return {};
    uint32_t afterReference{}; Roots afterRoots{};
    if (!memory.Reference(manager[2], afterReference) || afterReference != result.data ||
        !SameArray(memory, roots.world, 0x60, components) ||
        (!pointers.empty() && (!memory.Copy(components.data, afterPointers.data(), pointers.size() * 4) || pointers != afterPointers)) ||
        !ReadRoots(memory, afterRoots) || !(roots == afterRoots)) return {};
    for (uint32_t y = 0; y < result.maskHeight / 2; ++y)
        for (uint32_t x = 0; x < result.maskWidth; ++x)
            std::swap(result.discovery[y * result.maskWidth + x], result.discovery[(result.maskHeight - 1 - y) * result.maskWidth + x]);
    return result;
}
}
