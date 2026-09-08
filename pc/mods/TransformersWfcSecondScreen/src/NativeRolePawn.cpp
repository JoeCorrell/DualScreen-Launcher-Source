#include "NativeRolePawn.h"
#include "NativeRolePawnCode.h"
#include "Hook.h"
#include <atomic>
#include <cstring>

namespace wfcds {
extern "C" {
alignas(4) uintptr_t WfcRoleNativePlayerController=0;
alignas(4) uintptr_t WfcRoleNativeAiController=0;
void* WfcRolePlayerOriginal=nullptr;
void* WfcRoleAiOriginal=nullptr;
void* WfcRoleBuddyOriginal=nullptr;
void* WfcRoleBuddySetOriginal=nullptr;
void WfcRolePlayerGetter();
void WfcRoleAiGetter();
void WfcRoleBuddyGetter();
void WfcRoleBuddySetter();
}
namespace {
InlineHook g_role_getters[4]{};
HMODULE g_role_image=nullptr;
DWORD g_role_thread=0;
NativeRolePawnResolver g_role_resolver=nullptr;
bool RoleMemory(const void* p,size_t size,HMODULE image=nullptr) {
  MEMORY_BASIC_INFORMATION m{};
  if (!p || !size || !VirtualQuery(p,&m,sizeof(m)) || m.State!=MEM_COMMIT ||
      (m.Protect&(PAGE_GUARD|PAGE_NOACCESS)) || (image && m.AllocationBase!=image) ||
      size>m.RegionSize-(reinterpret_cast<uintptr_t>(p)-reinterpret_cast<uintptr_t>(m.BaseAddress))) return false;
  const DWORD v=m.Protect&255;
  return v==PAGE_EXECUTE || v==PAGE_EXECUTE_READ || v==PAGE_EXECUTE_READWRITE || v==PAGE_EXECUTE_WRITECOPY;
}
void JumpBytes(uint8_t* bytes,const void* site,const void* destination) {
  bytes[0]=0xE9;
  const auto relative=static_cast<uint32_t>(reinterpret_cast<uintptr_t>(destination)-reinterpret_cast<uintptr_t>(site)-5);
  std::memcpy(bytes+1,&relative,4);
}
bool Owned(const InlineHook& hook) {
  if (!hook.target || !hook.replacement || !hook.trampoline || hook.stolen_bytes!=native_role_pawn_code::kPrefix ||
      !RoleMemory(hook.target,hook.stolen_bytes,g_role_image) || !RoleMemory(hook.trampoline,hook.stolen_bytes+5)) return false;
  uint8_t patch[7]={0,0,0,0,0,0x90,0x90},tail[5]{};
  JumpBytes(patch,hook.target,hook.replacement);
  JumpBytes(tail,static_cast<uint8_t*>(hook.trampoline)+7,static_cast<uint8_t*>(hook.target)+7);
  return !std::memcmp(patch,hook.target,7) && !std::memcmp(hook.original,hook.trampoline,7) &&
      !std::memcmp(tail,static_cast<uint8_t*>(hook.trampoline)+7,5);
}
}
void PublishNativeRolePawnControllers(void* player,void* ai) {
  // Aligned interlocked stores also publish the completed mapping to the x86
  // fast path. Zero can never equal a valid compiled UObject receiver.
  InterlockedExchange(reinterpret_cast<volatile LONG*>(&WfcRoleNativePlayerController),
      player ? static_cast<LONG>(reinterpret_cast<uintptr_t>(player)+0x10):0);
  InterlockedExchange(reinterpret_cast<volatile LONG*>(&WfcRoleNativeAiController),
      ai ? static_cast<LONG>(reinterpret_cast<uintptr_t>(ai)+0x10):0);
}
extern "C" uintptr_t WfcResolveNativeRolePawn(uintptr_t compiled,uint32_t ai) {
  if (GetCurrentThreadId()!=g_role_thread || !g_role_resolver || compiled<=0x10 || ai>1) return 0x10;
  void* pawn=g_role_resolver(reinterpret_cast<void*>(compiled-0x10),ai!=0);
  return pawn ? reinterpret_cast<uintptr_t>(pawn)+0x10:0x10;
}
bool NativeRolePawnCodeMatches(HMODULE image,uintptr_t va,const uint8_t* expected,size_t size) {
  if (!image || !expected || size<7 || va<native_reference_code::kImageBase) return false;
  auto* p=reinterpret_cast<uint8_t*>(image)+va-native_reference_code::kImageBase;
  if (!RoleMemory(p,size,image)) return false;
  for(const auto& hook:g_role_getters)if(hook.target==p) {
    return image==g_role_image && Owned(hook) && !std::memcmp(expected,hook.original,7) &&
        !std::memcmp(expected+7,p+7,size-7);
  }
  return !std::memcmp(expected,p,size);
}
bool ValidateNativeRolePawnCode(HMODULE image) {
  return native_role_pawn_code::Visit([&](uintptr_t va,const auto& code,std::initializer_list<size_t> operands){
    const auto bytes=native_reference_code::Relocate(code,reinterpret_cast<uintptr_t>(image),operands);
    return NativeRolePawnCodeMatches(image,va,bytes.data(),bytes.size());
  });
}
bool NativeRolePawnGettersInstalled() {
  for(const auto& hook:g_role_getters)if(hook.target) return true;
  return false;
}
bool RemoveNativeRolePawnGetters() {
  if (g_role_thread && GetCurrentThreadId()!=g_role_thread) return false;
  PublishNativeRolePawnControllers(nullptr,nullptr);
  // Do not overwrite another hook, even during partial-install rollback.
  for(const auto& hook:g_role_getters)if(hook.target && !Owned(hook)) return false;
  for(int i=3;i>=0;--i)if(g_role_getters[i].target && !RemoveInlineHook(&g_role_getters[i])) return false;
  WfcRolePlayerOriginal=WfcRoleAiOriginal=WfcRoleBuddyOriginal=WfcRoleBuddySetOriginal=nullptr;
  g_role_resolver=nullptr;g_role_image=nullptr;g_role_thread=0;
  return true;
}
bool InstallNativeRolePawnGetters(HMODULE image,DWORD thread,NativeRolePawnResolver resolver) {
  if (!image || !resolver || !thread || thread!=GetCurrentThreadId() || NativeRolePawnGettersInstalled() ||
      !ValidateNativeRolePawnCode(image)) return false;
  g_role_image=image;g_role_thread=thread;g_role_resolver=resolver;
  PublishNativeRolePawnControllers(nullptr,nullptr);
  const uintptr_t vas[]={native_role_pawn_code::kPlayerVa,native_role_pawn_code::kAiVa,native_role_pawn_code::kBuddyVa,native_role_pawn_code::kBuddySetVa};
  const uint8_t* prefixes[]={native_role_pawn_code::kPlayer,native_role_pawn_code::kAi,native_role_pawn_code::kBuddy,native_role_pawn_code::kBuddySet};
  void* replacements[]={reinterpret_cast<void*>(&WfcRolePlayerGetter),reinterpret_cast<void*>(&WfcRoleAiGetter),reinterpret_cast<void*>(&WfcRoleBuddyGetter),reinterpret_cast<void*>(&WfcRoleBuddySetter)};
  void** originals[]={&WfcRolePlayerOriginal,&WfcRoleAiOriginal,&WfcRoleBuddyOriginal,&WfcRoleBuddySetOriginal};
  for(size_t i=0;i<4;++i) {
    auto* target=reinterpret_cast<uint8_t*>(image)+vas[i]-native_reference_code::kImageBase;
    if (!InstallInlineHook(&g_role_getters[i],target,replacements[i],prefixes[i],7,originals[i])) {
      RemoveNativeRolePawnGetters();return false;
    }
  }
  return true;
}
}
