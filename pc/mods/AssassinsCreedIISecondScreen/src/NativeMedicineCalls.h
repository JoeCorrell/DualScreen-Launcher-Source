#pragma once
#include "NativeMedicineAction.h"
namespace ac2ds::native {
static_assert(sizeof(void*)==4,"AC II event bridge requires x86");
// MSVC thiscall: ECX is a pointer to a one-word entity wrapper, one DWORD
// argument on the stack, callee pops4. The original wheel pushes literal1.
using MedicineRequestCall=uint32_t(__attribute__((fastcall)) *)(uint32_t*,void*,uint32_t);
class MedicineEventEngine final : public MedicineEngine {
    MedicineRequestCall request_;
public:
    explicit MedicineEventEngine(MedicineRequestCall request):request_(request){}
    bool Request(const MedicineTarget& target)override {
        uint32_t entity=target.entity;
        return request_ && request_(&entity,nullptr,1)==0;
    }
};
}
