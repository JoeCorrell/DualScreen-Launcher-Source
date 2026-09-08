#include "NativePawnTargeting.h"
#include "NativePawnTargetingCode.h"
#include "Hook.h"
#include <cstring>
namespace wfcds {
extern "C" {
alignas(4) uintptr_t WfcTargetPlayer=0;
void* WfcPawnAimOriginal=nullptr;
void* WfcPawnBaseAim=nullptr;
void* WfcPawnLocationOriginal=nullptr;
void* WfcPawnActorLocation=nullptr;
void* WfcPawnSameTargetOriginal=nullptr;
void* WfcPawnSameTargetFalse=nullptr;
void WfcPawnAimGate();
void WfcPawnSameTargetGate();
void WfcPawnLocationGate();
}
namespace {
InlineHook g_target_hooks[3]{};
HMODULE g_target_image=nullptr;
DWORD g_target_thread=0;
NativePawnTargetingResolver g_target_resolver=nullptr;
alignas(4) uintptr_t g_target_location_table=0;
bool TargetCode(const void* p,size_t n,HMODULE image=nullptr) {
  MEMORY_BASIC_INFORMATION m{};
  if(!p || !VirtualQuery(p,&m,sizeof(m)) || m.State!=MEM_COMMIT || (m.Protect&(PAGE_GUARD|PAGE_NOACCESS)) ||
      (image && m.AllocationBase!=image) || n>m.RegionSize-(reinterpret_cast<uintptr_t>(p)-reinterpret_cast<uintptr_t>(m.BaseAddress)))return false;
  const auto v=m.Protect&255;
  return v==PAGE_EXECUTE || v==PAGE_EXECUTE_READ || v==PAGE_EXECUTE_READWRITE || v==PAGE_EXECUTE_WRITECOPY;
}
void TargetJump(uint8_t* out,void* at,void* target) {
  out[0]=0xE9;const uint32_t rel=reinterpret_cast<uintptr_t>(target)-reinterpret_cast<uintptr_t>(at)-5;
  std::memcpy(out+1,&rel,4);
}
bool TargetOwned(const InlineHook& h) {
  if(!h.target || !h.trampoline || !h.replacement || h.stolen_bytes!=6 ||
      !TargetCode(h.target,6,g_target_image) || !TargetCode(h.trampoline,11))return false;
  uint8_t patch[6]={0,0,0,0,0,0x90},tail[5]{};TargetJump(patch,h.target,h.replacement);
  TargetJump(tail,static_cast<uint8_t*>(h.trampoline)+6,static_cast<uint8_t*>(h.target)+6);
  return !std::memcmp(h.target,patch,6) && !std::memcmp(h.trampoline,h.original,6) &&
      !std::memcmp(static_cast<uint8_t*>(h.trampoline)+6,tail,5);
}
}
void PublishNativePawnTargetingPlayer(void* pawn) {
  // Admission-only memory queries; the hot path below only compares the
  // canonical pawn's table with this already validated image table.
  uintptr_t table=0;
  if(pawn && NativePawnLocationDispatchSupported(g_target_image,pawn))std::memcpy(&table,pawn,4);
  InterlockedExchange(reinterpret_cast<volatile LONG*>(&g_target_location_table),static_cast<LONG>(table));
  InterlockedExchange(reinterpret_cast<volatile LONG*>(&WfcTargetPlayer),
      pawn ? static_cast<LONG>(reinterpret_cast<uintptr_t>(pawn)+0x10):0);
}
extern "C" uint32_t WfcResolvePawnAim(uintptr_t pawn) {
  return GetCurrentThreadId()==g_target_thread && g_target_resolver && pawn>0x10 &&
      pawn==WfcTargetPlayer && g_target_resolver(reinterpret_cast<void*>(pawn-0x10));
}
bool NativePawnLocationDispatchSupported(HMODULE image,void* raw_pawn) {
  if(!image || !raw_pawn)return false;
  auto readable=[](const void* p,size_t n){
    MEMORY_BASIC_INFORMATION m{};
    if(!p || !VirtualQuery(p,&m,sizeof(m)) || m.State!=MEM_COMMIT || (m.Protect&(PAGE_GUARD|PAGE_NOACCESS)))return false;
    const auto v=m.Protect&255;
    return (v==PAGE_READONLY || v==PAGE_READWRITE || v==PAGE_WRITECOPY ||
        v==PAGE_EXECUTE_READ || v==PAGE_EXECUTE_READWRITE || v==PAGE_EXECUTE_WRITECOPY) &&
        n<=m.RegionSize-(reinterpret_cast<uintptr_t>(p)-reinterpret_cast<uintptr_t>(m.BaseAddress));
  };
  void* table=nullptr;void* entry=nullptr;
  if(!readable(raw_pawn,sizeof(table)))return false;
  std::memcpy(&table,raw_pawn,sizeof(table));
  const auto address=reinterpret_cast<uintptr_t>(table);
  if(!address || address>UINTPTR_MAX-0x1A4 || !readable(reinterpret_cast<void*>(address+0x1A0),4))return false;
  MEMORY_BASIC_INFORMATION m{};
  if(!VirtualQuery(reinterpret_cast<void*>(address+0x1A0),&m,sizeof(m)) || m.AllocationBase!=image)return false;
  std::memcpy(&entry,reinterpret_cast<void*>(address+0x1A0),4);
  return entry==reinterpret_cast<uint8_t*>(image)+native_pawn_targeting_code::kActorLocationVa-native_reference_code::kImageBase;
}
extern "C" uint32_t WfcResolvePawnLocation(uintptr_t pawn) {
  if(!WfcResolvePawnAim(pawn) || !g_target_location_table)return 0;
  uintptr_t table=0;void* entry=nullptr;
  std::memcpy(&table,reinterpret_cast<void*>(pawn-0x10),4);
  if(table!=g_target_location_table)return 0;
  std::memcpy(&entry,reinterpret_cast<void*>(table+0x1A0),4);
  return entry==WfcPawnActorLocation;
}
bool ValidateNativePawnTargeting(HMODULE image) {
  if(!image)return false;
  return native_pawn_targeting_code::Visit([&](uintptr_t va,const auto& code,std::initializer_list<size_t> operands){
    const auto bytes=native_reference_code::Relocate(code,reinterpret_cast<uintptr_t>(image),operands);
    auto* p=reinterpret_cast<uint8_t*>(image)+va-native_reference_code::kImageBase;
    if(!TargetCode(p,bytes.size(),image))return false;
    for(const auto& h:g_target_hooks)if(h.target && reinterpret_cast<uintptr_t>(h.target)>=reinterpret_cast<uintptr_t>(p) &&
        reinterpret_cast<uintptr_t>(h.target)<reinterpret_cast<uintptr_t>(p)+bytes.size()) {
      const auto offset=static_cast<uint8_t*>(h.target)-p;
      return image==g_target_image && TargetOwned(h) && !std::memcmp(p,bytes.data(),offset) &&
          !std::memcmp(h.original,bytes.data()+offset,6) && !std::memcmp(p+offset+6,bytes.data()+offset+6,bytes.size()-offset-6);
    }
    return !std::memcmp(p,bytes.data(),bytes.size());
  });
}
bool NativePawnTargetingInstalled(){return g_target_hooks[0].target || g_target_hooks[1].target || g_target_hooks[2].target;}
bool RemoveNativePawnTargeting() {
  if(g_target_thread && GetCurrentThreadId()!=g_target_thread)return false;
  PublishNativePawnTargetingPlayer(nullptr);
  for(const auto& h:g_target_hooks)if(h.target && !TargetOwned(h))return false;
  for(int i=2;i>=0;--i)if(g_target_hooks[i].target && !RemoveInlineHook(&g_target_hooks[i]))return false;
  WfcPawnAimOriginal=WfcPawnBaseAim=WfcPawnSameTargetOriginal=WfcPawnSameTargetFalse=nullptr;
  WfcPawnLocationOriginal=WfcPawnActorLocation=nullptr;
  g_target_image=nullptr;g_target_thread=0;g_target_resolver=nullptr;return true;
}
bool InstallNativePawnTargeting(HMODULE image,DWORD thread,NativePawnTargetingResolver resolver) {
  if(!thread || thread!=GetCurrentThreadId() || !resolver || NativePawnTargetingInstalled() || !ValidateNativePawnTargeting(image))return false;
  using namespace native_pawn_targeting_code;
  g_target_image=image;g_target_thread=thread;g_target_resolver=resolver;PublishNativePawnTargetingPlayer(nullptr);
  auto* base=reinterpret_cast<uint8_t*>(image);
  WfcPawnBaseAim=base+kBaseAimVa-native_reference_code::kImageBase;
  WfcPawnActorLocation=base+kActorLocationVa-native_reference_code::kImageBase;
  WfcPawnSameTargetFalse=base+kSameTargetVa-native_reference_code::kImageBase+kSameReturn;
  if(!InstallInlineHook(&g_target_hooks[0],base+kAimVa-native_reference_code::kImageBase,
      reinterpret_cast<void*>(&WfcPawnAimGate),kAim,6,&WfcPawnAimOriginal) ||
     !InstallInlineHook(&g_target_hooks[1],base+kSameTargetVa-native_reference_code::kImageBase+kSamePatch,
      reinterpret_cast<void*>(&WfcPawnSameTargetGate),kSameTarget+kSamePatch,6,&WfcPawnSameTargetOriginal) ||
     !InstallInlineHook(&g_target_hooks[2],base+kLocationVa-native_reference_code::kImageBase,
      reinterpret_cast<void*>(&WfcPawnLocationGate),kLocation,6,&WfcPawnLocationOriginal)) {
    RemoveNativePawnTargeting();return false;
  }
  return true;
}
}
