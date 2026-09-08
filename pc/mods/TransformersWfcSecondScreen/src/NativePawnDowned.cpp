#include "NativePawnDowned.h"
#include "NativePawnDownedCode.h"
#include "Hook.h"
#include <cstring>
namespace wfcds {
extern "C" {
alignas(4) uintptr_t WfcDownedPlayerPawn=0,WfcDownedAiPawn=0;
void* WfcDownedAiStartOriginal=nullptr;
void* WfcDownedAiStopOriginal=nullptr;
void* WfcDownedPlayerStartOriginal=nullptr;
void* WfcDownedPlayerStopOriginal=nullptr;
void* WfcDownedBoundStartOriginal=nullptr;
void* WfcDownedBoundReviveOriginal=nullptr;
void* WfcDownedBoundStopOriginal=nullptr;
void* WfcDownedBoundStartContinue=nullptr;
void* WfcDownedBoundReviveContinue=nullptr;
void* WfcDownedBoundStopContinue=nullptr;
void* WfcDownedAiStartReturn=nullptr;
void WfcDownedAiStart();void WfcDownedAiStop();
void WfcDownedPlayerStart();void WfcDownedPlayerStop();
void WfcDownedBoundStart();void WfcDownedBoundRevive();void WfcDownedBoundStop();
}
namespace {
using namespace native_pawn_downed_code;
struct DownedSite {uintptr_t va;size_t offset,size;void (*entry)();void** original;};
const DownedSite kDownedSites[]={
  {kAiStartVa,0,9,WfcDownedAiStart,&WfcDownedAiStartOriginal},
  {kAiStopVa,0,8,WfcDownedAiStop,&WfcDownedAiStopOriginal},
  {kPlayerStartVa,0,9,WfcDownedPlayerStart,&WfcDownedPlayerStartOriginal},
  {kPlayerStopVa,0,10,WfcDownedPlayerStop,&WfcDownedPlayerStopOriginal},
  {kAiStartVa,0x3F,6,WfcDownedBoundStart,&WfcDownedBoundStartOriginal},
  {kAiStartVa,0x61,6,WfcDownedBoundRevive,&WfcDownedBoundReviveOriginal},
  {kAiStopVa,0x28,6,WfcDownedBoundStop,&WfcDownedBoundStopOriginal},
};
InlineHook g_downed_hooks[7]{};
HMODULE g_downed_image=nullptr;
DWORD g_downed_thread=0;
NativePawnDownedResolver g_downed_resolver=nullptr;
bool DownedCode(const void* p,size_t n,HMODULE image=nullptr) {
  MEMORY_BASIC_INFORMATION m{};
  if(!p || !VirtualQuery(p,&m,sizeof(m)) || m.State!=MEM_COMMIT || (m.Protect&(PAGE_GUARD|PAGE_NOACCESS)) ||
      (image && m.AllocationBase!=image) || n>m.RegionSize-(reinterpret_cast<uintptr_t>(p)-reinterpret_cast<uintptr_t>(m.BaseAddress)))return false;
  const auto v=m.Protect&255;
  return v==PAGE_EXECUTE || v==PAGE_EXECUTE_READ || v==PAGE_EXECUTE_READWRITE || v==PAGE_EXECUTE_WRITECOPY;
}
void DownedJump(uint8_t* out,const void* at,const void* target) {
  out[0]=0xE9;const uint32_t rel=reinterpret_cast<uintptr_t>(target)-reinterpret_cast<uintptr_t>(at)-5;
  std::memcpy(out+1,&rel,4);
}
bool DownedOwned(size_t i) {
  const auto& h=g_downed_hooks[i];const auto& site=kDownedSites[i];
  if(!h.target || !h.trampoline || h.replacement!=reinterpret_cast<void*>(site.entry) || h.stolen_bytes!=site.size ||
      !DownedCode(h.target,site.size,g_downed_image) || !DownedCode(h.trampoline,site.size+5))return false;
  uint8_t patch[16],trampoline[21]{};std::memset(patch,0x90,sizeof(patch));
  DownedJump(patch,h.target,h.replacement);std::memcpy(trampoline,h.original,site.size);
  if(i==1){
    int32_t old=0;std::memcpy(&old,h.original+4,4);
    const auto target=reinterpret_cast<uintptr_t>(h.target)+8+uintptr_t(old);
    DownedJump(trampoline+3,static_cast<uint8_t*>(h.trampoline)+3,reinterpret_cast<void*>(target));trampoline[3]=0xE8;
  }
  DownedJump(trampoline+site.size,static_cast<uint8_t*>(h.trampoline)+site.size,static_cast<uint8_t*>(h.target)+site.size);
  return !std::memcmp(h.target,patch,site.size) && !std::memcmp(h.trampoline,trampoline,site.size+5);
}
}
void PublishNativePawnDownedPawns(void* player,void* ai) {
  InterlockedExchange(reinterpret_cast<volatile LONG*>(&WfcDownedPlayerPawn),player ? static_cast<LONG>(reinterpret_cast<uintptr_t>(player)+0x10):0);
  InterlockedExchange(reinterpret_cast<volatile LONG*>(&WfcDownedAiPawn),ai ? static_cast<LONG>(reinterpret_cast<uintptr_t>(ai)+0x10):0);
}
extern "C" uintptr_t WfcResolveDownedController(uintptr_t pawn,uint32_t ai) {
  if(ai>1 || pawn<=0x10 || pawn!=(ai ? WfcDownedAiPawn:WfcDownedPlayerPawn) ||
      GetCurrentThreadId()!=g_downed_thread || !g_downed_resolver)return 0;
  return reinterpret_cast<uintptr_t>(g_downed_resolver(reinterpret_cast<void*>(pawn-0x10),ai!=0));
}
bool ValidateNativePawnDowned(HMODULE image) {
  if(!image)return false;
  return Visit([&](uintptr_t va,const auto& code,std::initializer_list<size_t> r){
    auto expected=native_reference_code::Relocate(code,reinterpret_cast<uintptr_t>(image),r);
    auto* p=reinterpret_cast<uint8_t*>(image)+(va-native_reference_code::kImageBase);
    if(!DownedCode(p,expected.size(),image))return false;
    for(size_t i=0;i<7;++i)if(g_downed_hooks[i].target && kDownedSites[i].va==va){
      const auto& h=g_downed_hooks[i];const auto& site=kDownedSites[i];
      if(image!=g_downed_image || h.target!=p+site.offset || !DownedOwned(i) ||
          std::memcmp(h.original,expected.data()+site.offset,site.size))return false;
      std::memset(expected.data()+site.offset,0x90,site.size);
      DownedJump(expected.data()+site.offset,h.target,h.replacement);
    }
    return !std::memcmp(p,expected.data(),expected.size());
  });
}
bool NativePawnDownedInstalled(){for(const auto& h:g_downed_hooks)if(h.target)return true;return false;}
bool RemoveNativePawnDowned() {
  if(g_downed_thread && GetCurrentThreadId()!=g_downed_thread)return false;
  PublishNativePawnDownedPawns(nullptr,nullptr);
  for(size_t i=0;i<7;++i)if(g_downed_hooks[i].target && !DownedOwned(i))return false;
  for(size_t i=7;i>0;--i)if(g_downed_hooks[i-1].target && !RemoveInlineHook(&g_downed_hooks[i-1]))return false;
  for(const auto& site:kDownedSites)*site.original=nullptr;
  WfcDownedBoundStartContinue=WfcDownedBoundReviveContinue=WfcDownedBoundStopContinue=WfcDownedAiStartReturn=nullptr;
  g_downed_image=nullptr;g_downed_thread=0;g_downed_resolver=nullptr;return true;
}
bool InstallNativePawnDowned(HMODULE image,DWORD thread,NativePawnDownedResolver resolver) {
  if(!thread || thread!=GetCurrentThreadId() || !resolver || NativePawnDownedInstalled() || !ValidateNativePawnDowned(image))return false;
  g_downed_image=image;g_downed_thread=thread;g_downed_resolver=resolver;PublishNativePawnDownedPawns(nullptr,nullptr);
  const auto at=[image](uintptr_t va){return reinterpret_cast<uint8_t*>(image)+(va-native_reference_code::kImageBase);};
  WfcDownedBoundStartContinue=at(kAiStartVa+0x45);
  WfcDownedBoundReviveContinue=at(kAiStartVa+0x67);
  WfcDownedBoundStopContinue=at(kAiStopVa+0x2E);
  WfcDownedAiStartReturn=at(kAiStartVa+0x98);
  for(size_t i=0;i<7;++i){const auto& site=kDownedSites[i];
    // Relocated original bytes are read only after the whole-image signature
    // audit; publish no mapped identity until every hook has been installed.
    uint8_t expected[16]{};std::memcpy(expected,at(site.va+site.offset),site.size);
    if(!InstallInlineHook(&g_downed_hooks[i],at(site.va+site.offset),reinterpret_cast<void*>(site.entry),
        expected,site.size,site.original,i==1 ? std::initializer_list<size_t>{3}:std::initializer_list<size_t>{})){
      RemoveNativePawnDowned();return false;
    }
  }
  return true;
}
}
