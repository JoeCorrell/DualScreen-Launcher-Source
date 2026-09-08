#include "NativePawnTransform.h"
#include "NativePawnTransformCode.h"
#include "Hook.h"
#include <cstring>
namespace wfcds {
extern "C" {
alignas(4) uintptr_t WfcPawnTransformBlocked=0;
void* WfcPawnTransformOriginal=nullptr;
void* WfcPawnStartVehicleOriginal=nullptr;
void WfcPawnTransformGuard();
void WfcPawnStartVehicleGuard();
}
namespace {
InlineHook g_transform_hooks[2]{};
HMODULE g_transform_image=nullptr;
DWORD g_transform_thread=0;
bool TransformMemory(const void* p,size_t size,HMODULE image=nullptr){
  MEMORY_BASIC_INFORMATION m{};
  if(!p || !size || !VirtualQuery(p,&m,sizeof(m)) || m.State!=MEM_COMMIT ||
      (m.Protect&(PAGE_GUARD|PAGE_NOACCESS)) || (image && m.AllocationBase!=image))return false;
  const auto offset=reinterpret_cast<uintptr_t>(p)-reinterpret_cast<uintptr_t>(m.BaseAddress);
  if(offset>m.RegionSize || size>m.RegionSize-offset)return false;
  const DWORD access=m.Protect&255;
  return access==PAGE_EXECUTE || access==PAGE_EXECUTE_READ || access==PAGE_EXECUTE_READWRITE || access==PAGE_EXECUTE_WRITECOPY;
}
void TransformJump(uint8_t* bytes,const void* site,const void* target){
  bytes[0]=0xE9;const uint32_t rel=reinterpret_cast<uintptr_t>(target)-reinterpret_cast<uintptr_t>(site)-5;
  std::memcpy(bytes+1,&rel,4);
}
bool TransformOwned(size_t index){
  const auto& h=g_transform_hooks[index];const size_t n=index ? 9:10;
  const void* replacement=index ? reinterpret_cast<void*>(&WfcPawnStartVehicleGuard):reinterpret_cast<void*>(&WfcPawnTransformGuard);
  if(!h.target || !h.trampoline || h.replacement!=replacement || h.stolen_bytes!=n ||
      !TransformMemory(h.target,n,g_transform_image) || !TransformMemory(h.trampoline,n+5))return false;
  uint8_t patch[10],tail[5];std::memset(patch,0x90,sizeof(patch));TransformJump(patch,h.target,replacement);
  TransformJump(tail,static_cast<uint8_t*>(h.trampoline)+n,static_cast<uint8_t*>(h.target)+n);
  return !std::memcmp(h.target,patch,n) && !std::memcmp(h.original,h.trampoline,n) &&
      !std::memcmp(static_cast<uint8_t*>(h.trampoline)+n,tail,5);
}
}
void PublishNativePawnTransformGuard(void* pawn){
  // Zero is not compiled None (16). A stale published identity stays denied,
  // never permits a physical player through AI-only vehicle initialization.
  InterlockedExchange(reinterpret_cast<volatile LONG*>(&WfcPawnTransformBlocked),
      pawn ? static_cast<LONG>(reinterpret_cast<uintptr_t>(pawn)+0x10):0);
}
bool ValidateNativePawnTransform(HMODULE image){
  if(!image)return false;
  size_t index=0;
  return native_pawn_transform_code::Visit([&](uintptr_t va,const auto& code,std::initializer_list<size_t> r){
    auto expected=native_reference_code::Relocate(code,reinterpret_cast<uintptr_t>(image),r);
    auto* p=reinterpret_cast<uint8_t*>(image)+va-native_reference_code::kImageBase;
    if(!TransformMemory(p,expected.size(),image))return false;
    const auto& h=g_transform_hooks[index];
    if(h.target){
      if(image!=g_transform_image || h.target!=p || !TransformOwned(index) ||
          std::memcmp(h.original,expected.data(),h.stolen_bytes))return false;
      std::memset(expected.data(),0x90,h.stolen_bytes);TransformJump(expected.data(),p,h.replacement);
    }
    ++index;return !std::memcmp(p,expected.data(),expected.size());
  });
}
bool NativePawnTransformInstalled(){return g_transform_hooks[0].target || g_transform_hooks[1].target;}
bool NativePawnTransformDispatchSupported(HMODULE image,void* pawn){
  auto readable=[](const void* p,size_t n,HMODULE owner){
    MEMORY_BASIC_INFORMATION m{};
    if(!p || !VirtualQuery(p,&m,sizeof(m)) || m.State!=MEM_COMMIT ||
        (m.Protect&(PAGE_GUARD|PAGE_NOACCESS)) || (owner && m.AllocationBase!=owner))return false;
    const auto o=reinterpret_cast<uintptr_t>(p)-reinterpret_cast<uintptr_t>(m.BaseAddress);
    const DWORD access=m.Protect&255;
    return o<=m.RegionSize && n<=m.RegionSize-o && (access==PAGE_READONLY || access==PAGE_READWRITE ||
        access==PAGE_WRITECOPY || access==PAGE_EXECUTE_READ || access==PAGE_EXECUTE_READWRITE || access==PAGE_EXECUTE_WRITECOPY);
  };
  if(!image || !pawn || reinterpret_cast<uintptr_t>(pawn)>UINTPTR_MAX-0x14 ||
      !readable(static_cast<uint8_t*>(pawn)+0x10,4,nullptr))return false;
  uintptr_t table=0;void* method=nullptr;
  std::memcpy(&table,static_cast<uint8_t*>(pawn)+0x10,4);
  if(!table || table>UINTPTR_MAX-0x310 || !readable(reinterpret_cast<void*>(table+0x30C),4,image))return false;
  std::memcpy(&method,reinterpret_cast<void*>(table+0x30C),4);
  return method==reinterpret_cast<uint8_t*>(image)+native_pawn_transform_code::kTransformVa-native_reference_code::kImageBase;
}
bool RemoveNativePawnTransform(){
  if(g_transform_thread && GetCurrentThreadId()!=g_transform_thread)return false;
  // Keep the deny guard intact if patch ownership has been lost.
  for(size_t i=0;i<2;++i)if(g_transform_hooks[i].target && !TransformOwned(i))return false;
  for(int i=1;i>=0;--i)if(g_transform_hooks[i].target && !RemoveInlineHook(&g_transform_hooks[i]))return false;
  PublishNativePawnTransformGuard(nullptr);
  WfcPawnTransformOriginal=WfcPawnStartVehicleOriginal=nullptr;
  g_transform_image=nullptr;g_transform_thread=0;return true;
}
bool InstallNativePawnTransform(HMODULE image,DWORD thread){
  if(!thread || thread!=GetCurrentThreadId() || NativePawnTransformInstalled() || !ValidateNativePawnTransform(image))return false;
  using namespace native_pawn_transform_code;
  g_transform_image=image;g_transform_thread=thread;PublishNativePawnTransformGuard(nullptr);
  const uintptr_t vas[]={kTransformVa,kStartVehicleVa};
  const uint8_t* bytes[]={kTransform,kStartVehicle};
  void* replacements[]={reinterpret_cast<void*>(&WfcPawnTransformGuard),reinterpret_cast<void*>(&WfcPawnStartVehicleGuard)};
  void** originals[]={&WfcPawnTransformOriginal,&WfcPawnStartVehicleOriginal};
  for(size_t i=0;i<2;++i)if(!InstallInlineHook(&g_transform_hooks[i],
      reinterpret_cast<uint8_t*>(image)+vas[i]-native_reference_code::kImageBase,
      replacements[i],bytes[i],i ? 9:10,originals[i])){RemoveNativePawnTransform();return false;}
  return true;
}
}
