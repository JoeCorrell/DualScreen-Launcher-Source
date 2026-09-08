#include "NativePawnDamage.h"
#include "NativePawnDamageCode.h"
#include "Hook.h"
#include <cstring>
namespace wfcds {
extern "C" {
alignas(4) uintptr_t WfcDamagePawn=0,WfcDamageInterface=0;
void* WfcDamageFlashOriginal=nullptr;
void* WfcDamageBlockOriginal=nullptr;
void* WfcDamageFlashReturn=nullptr;
void* WfcDamageBlockReturn=nullptr;
void WfcDamageFlashGate();
void WfcDamageBlockGate();
}
namespace {
InlineHook g_damage_hooks[2]{};
HMODULE g_damage_image=nullptr;
DWORD g_damage_thread=0;
bool DamageCode(const void* p,size_t n,HMODULE image=nullptr) {
  MEMORY_BASIC_INFORMATION m{};
  if(!p || !VirtualQuery(p,&m,sizeof(m)) || m.State!=MEM_COMMIT || (m.Protect&(PAGE_GUARD|PAGE_NOACCESS)) ||
      (image && m.AllocationBase!=image) || n>m.RegionSize-(reinterpret_cast<uintptr_t>(p)-reinterpret_cast<uintptr_t>(m.BaseAddress)))return false;
  const auto v=m.Protect&255;
  return v==PAGE_EXECUTE || v==PAGE_EXECUTE_READ || v==PAGE_EXECUTE_READWRITE || v==PAGE_EXECUTE_WRITECOPY;
}
void DamageJump(uint8_t* out,void* at,void* target) {
  out[0]=0xE9;const uint32_t rel=reinterpret_cast<uintptr_t>(target)-reinterpret_cast<uintptr_t>(at)-5;
  std::memcpy(out+1,&rel,4);
}
bool DamageOwned(const InlineHook& h) {
  if(!h.target || !h.trampoline || !h.replacement || h.stolen_bytes!=6 ||
      !DamageCode(h.target,6,g_damage_image) || !DamageCode(h.trampoline,11))return false;
  uint8_t patch[6]={0,0,0,0,0,0x90},tail[5]{};DamageJump(patch,h.target,h.replacement);
  DamageJump(tail,static_cast<uint8_t*>(h.trampoline)+6,static_cast<uint8_t*>(h.target)+6);
  return !std::memcmp(h.target,patch,6) && !std::memcmp(h.trampoline,h.original,6) &&
      !std::memcmp(static_cast<uint8_t*>(h.trampoline)+6,tail,5);
}
}
void PublishNativePawnDamagePlayer(void* pawn) {
  InterlockedExchange(reinterpret_cast<volatile LONG*>(&WfcDamagePawn),pawn ? reinterpret_cast<LONG>(pawn)+0x10:0);
  InterlockedExchange(reinterpret_cast<volatile LONG*>(&WfcDamageInterface),pawn ? reinterpret_cast<LONG>(pawn)+0x530:0);
}
bool ValidateNativePawnDamage(HMODULE image) {
  if(!image)return false;
  return native_pawn_damage_code::Visit([&](uintptr_t va,const auto& code,std::initializer_list<size_t> operands){
    const auto bytes=native_reference_code::Relocate(code,reinterpret_cast<uintptr_t>(image),operands);
    auto* p=reinterpret_cast<uint8_t*>(image)+va-native_reference_code::kImageBase;
    if(!DamageCode(p,bytes.size(),image))return false;
    for(const auto& h:g_damage_hooks)if(h.target && h.target>=p && h.target<p+bytes.size()) {
      const auto offset=static_cast<uint8_t*>(h.target)-p;
      return image==g_damage_image && DamageOwned(h) && !std::memcmp(p,bytes.data(),offset) &&
          !std::memcmp(h.original,bytes.data()+offset,6) && !std::memcmp(p+offset+6,bytes.data()+offset+6,bytes.size()-offset-6);
    }
    return !std::memcmp(p,bytes.data(),bytes.size());
  });
}
bool NativePawnDamageInstalled(){return g_damage_hooks[0].target || g_damage_hooks[1].target;}
bool RemoveNativePawnDamage() {
  if(g_damage_thread && GetCurrentThreadId()!=g_damage_thread)return false;
  PublishNativePawnDamagePlayer(nullptr);
  for(const auto& h:g_damage_hooks)if(h.target && !DamageOwned(h))return false;
  for(int i=1;i>=0;--i)if(g_damage_hooks[i].target && !RemoveInlineHook(&g_damage_hooks[i]))return false;
  WfcDamageFlashOriginal=WfcDamageBlockOriginal=WfcDamageFlashReturn=WfcDamageBlockReturn=nullptr;
  g_damage_image=nullptr;g_damage_thread=0;return true;
}
bool InstallNativePawnDamage(HMODULE image,DWORD thread) {
  if(!thread || thread!=GetCurrentThreadId() || NativePawnDamageInstalled() || !ValidateNativePawnDamage(image))return false;
  using namespace native_pawn_damage_code;
  g_damage_image=image;g_damage_thread=thread;PublishNativePawnDamagePlayer(nullptr);
  auto* base=reinterpret_cast<uint8_t*>(image);
  WfcDamageFlashReturn=base+kFlashVa-native_reference_code::kImageBase+kFlashReturn;
  WfcDamageBlockReturn=base+kBlockVa-native_reference_code::kImageBase+kBlockReturn;
  if(!InstallInlineHook(&g_damage_hooks[0],base+kFlashVa-native_reference_code::kImageBase+kFlashPatch,
      reinterpret_cast<void*>(&WfcDamageFlashGate),kFlash+kFlashPatch,6,&WfcDamageFlashOriginal) ||
     !InstallInlineHook(&g_damage_hooks[1],base+kBlockVa-native_reference_code::kImageBase+kBlockPatch,
      reinterpret_cast<void*>(&WfcDamageBlockGate),kBlock+kBlockPatch,6,&WfcDamageBlockOriginal)) {
    RemoveNativePawnDamage();return false;
  }
  return true;
}
}
