#include "NativeGameDeath.h"
#include "NativeGameDeathCode.h"
#include "Hook.h"
#include <cstring>
namespace wfcds {
extern "C" {
void WfcGameDeathCast();
void* WfcGameDeathOriginalCast=nullptr;
}
namespace {
// Dummy EDX keeps the four real parameters on the native stack. This entry
// consumes ECX only; it does not read the incoming EDX register.
using DeathFn=void (__attribute__((fastcall)) *)(void*,void*,void*,void*,void*,void*);
InlineHook g_death_hook{};
void* g_death_original=nullptr;
uint8_t* g_death_cast=nullptr;
uint8_t g_death_cast_saved[5]{},g_death_cast_patch[5]{};
HMODULE g_death_image=nullptr;
DWORD g_death_thread=0;
NativeDeathCapture g_death_capture=nullptr;
NativeDeathValidate g_death_validate=nullptr;
NativeDeathNotify g_death_notify=nullptr;
alignas(4) uintptr_t g_death_player=0,g_death_ai=0;
struct DeathScope {void* pawn;void* controller;NativeDeathIdentity identity;DeathScope* previous;};
thread_local DeathScope* g_death_scope=nullptr;
void* DeathRaw(void* p){auto v=reinterpret_cast<uintptr_t>(p);return v>0x10 ? reinterpret_cast<void*>(v-0x10):nullptr;}
bool DeathExecutable(const void* p,size_t size,HMODULE owner=nullptr){
  MEMORY_BASIC_INFORMATION m{};
  if(!p || !VirtualQuery(p,&m,sizeof(m)) || m.State!=MEM_COMMIT ||
      (owner && m.AllocationBase!=owner) || (m.Protect&(PAGE_GUARD|PAGE_NOACCESS)))return false;
  const auto offset=reinterpret_cast<uintptr_t>(p)-reinterpret_cast<uintptr_t>(m.BaseAddress);
  if(offset>m.RegionSize || size>m.RegionSize-offset)return false;
  const auto access=m.Protect&255;
  return access==PAGE_EXECUTE || access==PAGE_EXECUTE_READ || access==PAGE_EXECUTE_READWRITE || access==PAGE_EXECUTE_WRITECOPY;
}
void DeathBranch(uint8_t* out,const void* at,const void* target,uint8_t opcode=0xE9){
  out[0]=opcode;const uint32_t rel=reinterpret_cast<uintptr_t>(target)-reinterpret_cast<uintptr_t>(at)-5;
  std::memcpy(out+1,&rel,4);
}
bool DeathWrite(uint8_t* at,const uint8_t* bytes){
  DWORD old=0,ignored=0;if(!VirtualProtect(at,5,PAGE_EXECUTE_READWRITE,&old))return false;
  std::memcpy(at,bytes,5);FlushInstructionCache(GetCurrentProcess(),at,5);
  VirtualProtect(at,5,old,&ignored);return true;
}
bool DeathCastOwned(){return g_death_cast && DeathExecutable(g_death_cast,5,g_death_image) &&
    !std::memcmp(g_death_cast,g_death_cast_patch,5);}
void __attribute__((fastcall)) HookedGameDeath(void* game,void*,void* killer,void* controller,void* pawn,void* damage){
  auto original=reinterpret_cast<DeathFn>(g_death_original);
  DeathScope scope{DeathRaw(pawn),DeathRaw(controller),{NativeDeathRole::kStock,0},g_death_scope};
  const auto victim=reinterpret_cast<uintptr_t>(scope.pawn);
  if(victim && (victim==g_death_player || victim==g_death_ai)){
    scope.identity={NativeDeathRole::kSuppress,0};
    if(GetCurrentThreadId()==g_death_thread && g_death_capture)
      scope.identity=g_death_capture(scope.pawn,scope.controller);
    // An unrecognized/stale mapped identity must never silently become the
    // physical-player death. The ordinary game/base notifications still run.
    if(scope.identity.role!=NativeDeathRole::kPlayer && scope.identity.role!=NativeDeathRole::kAi)
      scope.identity={NativeDeathRole::kSuppress,0};
  }
  g_death_scope=&scope;
  original(game,nullptr,killer,controller,pawn,damage);
  g_death_scope=scope.previous;
}
bool DeathEntryOwned(){
  const auto& h=g_death_hook;
  if(!h.target || !h.trampoline || h.stolen_bytes!=9 || h.replacement!=reinterpret_cast<void*>(&HookedGameDeath) ||
      !DeathExecutable(h.target,9,g_death_image) || !DeathExecutable(h.trampoline,14))return false;
  uint8_t patch[9],tail[5];std::memset(patch,0x90,sizeof(patch));
  DeathBranch(patch,h.target,h.replacement);DeathBranch(tail,static_cast<uint8_t*>(h.trampoline)+9,static_cast<uint8_t*>(h.target)+9);
  return !std::memcmp(h.target,patch,9) && !std::memcmp(h.trampoline,h.original,9) &&
      !std::memcmp(static_cast<uint8_t*>(h.trampoline)+9,tail,5);
}
}
extern "C" uint32_t WfcResolveGameDeath(void* pawn){
  const auto* scope=g_death_scope;
  if(!scope || scope->pawn!=pawn || scope->identity.role==NativeDeathRole::kStock)return UINT32_MAX;
  if(GetCurrentThreadId()!=g_death_thread || !g_death_validate || !g_death_notify ||
      scope->identity.role==NativeDeathRole::kSuppress ||
      !g_death_validate(scope->pawn,scope->controller,scope->identity))return 0;
  if(scope->identity.role!=NativeDeathRole::kPlayer)return 0;
  g_death_notify();return 1;
}
void PublishNativeGameDeathPawns(void* player,void* ai){
  InterlockedExchange(reinterpret_cast<volatile LONG*>(&g_death_player),reinterpret_cast<LONG>(player));
  InterlockedExchange(reinterpret_cast<volatile LONG*>(&g_death_ai),reinterpret_cast<LONG>(ai));
}
bool ValidateNativeGameDeath(HMODULE image){
  if(!image)return false;
  using namespace native_game_death_code;
  return Visit([&](uintptr_t va,const auto& code,std::initializer_list<size_t> r){
    auto expected=native_reference_code::Relocate(code,reinterpret_cast<uintptr_t>(image),r);
    auto* p=reinterpret_cast<uint8_t*>(image)+(va-native_reference_code::kImageBase);
    if(!DeathExecutable(p,expected.size(),image))return false;
    if(g_death_hook.target){
      if(image!=g_death_image || g_death_hook.target!=p || !DeathEntryOwned() ||
          std::memcmp(g_death_hook.original,expected.data(),9))return false;
      std::memset(expected.data(),0x90,9);DeathBranch(expected.data(),p,g_death_hook.replacement);
    }
    if(g_death_cast){
      if(image!=g_death_image || g_death_cast!=p+kCastOffset || !DeathCastOwned() ||
          std::memcmp(g_death_cast_saved,expected.data()+kCastOffset,5))return false;
      std::memcpy(expected.data()+kCastOffset,g_death_cast_patch,5);
    }
    return !std::memcmp(p,expected.data(),expected.size());
  });
}
bool NativeGameDeathInstalled(){return g_death_hook.target || g_death_cast;}
bool RemoveNativeGameDeath(){
  if((g_death_thread && GetCurrentThreadId()!=g_death_thread) || g_death_scope)return false;
  PublishNativeGameDeathPawns(nullptr,nullptr);
  if((g_death_hook.target && !DeathEntryOwned()) || (g_death_cast && !DeathCastOwned()))return false;
  if(g_death_cast){if(!DeathWrite(g_death_cast,g_death_cast_saved))return false;g_death_cast=nullptr;}
  if(g_death_hook.target && !RemoveInlineHook(&g_death_hook))return false;
  g_death_original=WfcGameDeathOriginalCast=nullptr;g_death_image=nullptr;g_death_thread=0;
  g_death_capture=nullptr;g_death_validate=nullptr;g_death_notify=nullptr;return true;
}
bool InstallNativeGameDeath(HMODULE image,DWORD thread,NativeDeathCapture capture,NativeDeathValidate validate,NativeDeathNotify notify){
  if(!thread || thread!=GetCurrentThreadId() || !capture || !validate || !notify ||
      NativeGameDeathInstalled() || !ValidateNativeGameDeath(image))return false;
  using namespace native_game_death_code;
  auto* target=reinterpret_cast<uint8_t*>(image)+(kKilledVa-native_reference_code::kImageBase);
  g_death_image=image;g_death_thread=thread;g_death_capture=capture;g_death_validate=validate;g_death_notify=notify;
  PublishNativeGameDeathPawns(nullptr,nullptr);
  WfcGameDeathOriginalCast=reinterpret_cast<uint8_t*>(image)+(0x10901430-native_reference_code::kImageBase);
  auto* site=target+kCastOffset;std::memcpy(g_death_cast_saved,site,5);
  DeathBranch(g_death_cast_patch,site,reinterpret_cast<void*>(&WfcGameDeathCast),0xE8);
  if(!InstallInlineHook(&g_death_hook,target,reinterpret_cast<void*>(&HookedGameDeath),kKilled,9,&g_death_original)){
    RemoveNativeGameDeath();return false;
  }
  if(!DeathWrite(site,g_death_cast_patch)){RemoveNativeGameDeath();return false;}
  g_death_cast=site;return true;
}
}
