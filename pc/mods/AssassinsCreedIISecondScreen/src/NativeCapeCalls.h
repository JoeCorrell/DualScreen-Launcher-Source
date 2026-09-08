#pragma once
#include "NativeCapeAction.h"
namespace ac2ds::native {
// MSVC x86 thiscall: ECX points to the one-word entity wrapper, three stack
// arguments, callee pops 12 bytes. A fastcall bridge's unused EDX parameter
// produces that exact call layout without treating a C++ member-function
// pointer as a raw code address. Both event8231 and event8233 return zero
// when the player inventory subscriber handles the event (3 if unhandled).
static_assert(sizeof(void*)==4,"AC II event bridge requires x86");
using CapePermissionCall = uint32_t(__attribute__((fastcall)) *)(uint32_t*,void*,uint32_t,const uint32_t*,uint8_t*);
using CapeMutationCall = uint32_t(__attribute__((fastcall)) *)(uint32_t*,void*,uint32_t,const uint32_t*,uint32_t);
class CapeEventEngine final : public CapeEngine {
    CapePermissionCall permission_;CapeMutationCall mutation_;
public:
    CapeEventEngine(CapePermissionCall permission,CapeMutationCall mutation):permission_(permission),mutation_(mutation){}
    bool CanEquip(const CapeTarget& target)override {
        uint32_t entity=target.entity,handle=target.handle;uint8_t allowed{};
        return permission_ && permission_(&entity,nullptr,1,&handle,&allowed)==0 && allowed==1;
    }
    bool Equip(const CapeTarget& target)override {
        uint32_t entity=target.entity,handle=target.handle;
        // Last argument zero exactly matches InventoryEquipCape. The native
        // subscriber retains the handle for the equipment job before returning.
        return mutation_ && mutation_(&entity,nullptr,1,&handle,0)==0;
    }
};
}
