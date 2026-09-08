#include "NativeWeaponExposure.h"
#include "NativeWeaponExposureCode.h"
#include "Hook.h"
#include <cstring>

namespace wfcds {
extern "C" {
alignas(4) uintptr_t WfcExposurePlayerPawn=0,WfcExposureAiPawn=0;
void* WfcExposureOriginal=nullptr;
void* WfcExposureContinue=nullptr;
void* WfcExposureReturn=nullptr;
void WfcWeaponExposureGate();
void* WfcPickupCastOriginal=nullptr;
void WfcWeaponPickupCast();
}
namespace {
InlineHook g_exposure_hook{};
uint8_t* g_pickup_site=nullptr;
uint8_t g_pickup_patch[5]{};
uint8_t* g_drop_site=nullptr;
uint8_t g_drop_patch[5]{};
HMODULE g_exposure_image=nullptr;
DWORD g_exposure_thread=0;
NativeWeaponExposureResolver g_exposure_resolver=nullptr;
bool Executable(const void* p,size_t size,HMODULE image=nullptr) {
  MEMORY_BASIC_INFORMATION m{};
  if(!p || !size || !VirtualQuery(p,&m,sizeof(m)) || m.State!=MEM_COMMIT ||
      (m.Protect&(PAGE_GUARD|PAGE_NOACCESS)) || (image && m.AllocationBase!=image) ||
      size>m.RegionSize-(reinterpret_cast<uintptr_t>(p)-reinterpret_cast<uintptr_t>(m.BaseAddress))) return false;
  const DWORD v=m.Protect&255;
  return v==PAGE_EXECUTE || v==PAGE_EXECUTE_READ || v==PAGE_EXECUTE_READWRITE || v==PAGE_EXECUTE_WRITECOPY;
}
void JumpBytes(uint8_t* bytes,const void* site,const void* destination) {
  bytes[0]=0xE9;
  const uint32_t delta=reinterpret_cast<uintptr_t>(destination)-reinterpret_cast<uintptr_t>(site)-5;
  std::memcpy(bytes+1,&delta,4);
}
bool Owned() {
  const auto& h=g_exposure_hook;
  if(!h.target || !h.replacement || !h.trampoline || h.stolen_bytes!=6 ||
      !Executable(h.target,6,g_exposure_image) || !Executable(h.trampoline,11)) return false;
  uint8_t patch[6]={0,0,0,0,0,0x90},tail[5]{};
  JumpBytes(patch,h.target,h.replacement);
  JumpBytes(tail,static_cast<uint8_t*>(h.trampoline)+6,static_cast<uint8_t*>(h.target)+6);
  return !std::memcmp(h.target,patch,6) && !std::memcmp(h.trampoline,h.original,6) &&
      !std::memcmp(static_cast<uint8_t*>(h.trampoline)+6,tail,5);
}
bool PickupOwned() {
  return g_pickup_site && Executable(g_pickup_site,5,g_exposure_image) &&
      !std::memcmp(g_pickup_site,g_pickup_patch,5);
}
bool DropOwned() {
  return g_drop_site && Executable(g_drop_site,5,g_exposure_image) &&
      !std::memcmp(g_drop_site,g_drop_patch,5);
}
bool WriteRoleCall(uint8_t* site,const uint8_t* bytes) {
  DWORD previous=0;
  if(!VirtualProtect(site,5,PAGE_EXECUTE_READWRITE,&previous)) return false;
  std::memcpy(site,bytes,5);
  FlushInstructionCache(GetCurrentProcess(),site,5);
  DWORD ignored=0;VirtualProtect(site,5,previous,&ignored);
  return true;
}
}
void PublishNativeWeaponExposurePawns(void* player,void* ai) {
  InterlockedExchange(reinterpret_cast<volatile LONG*>(&WfcExposurePlayerPawn),reinterpret_cast<LONG>(player));
  InterlockedExchange(reinterpret_cast<volatile LONG*>(&WfcExposureAiPawn),reinterpret_cast<LONG>(ai));
}
extern "C" uint32_t WfcResolveWeaponExposure(void* pawn) {
  // Mapped-but-stale/off-thread must not fall back to physical-player behavior.
  return GetCurrentThreadId()==g_exposure_thread && g_exposure_resolver && pawn &&
      g_exposure_resolver(pawn);
}
bool ValidateNativeWeaponExposureCode(HMODULE image) {
  if(!image) return false;
  return native_weapon_exposure_code::Visit([&](uintptr_t va,const auto& code,std::initializer_list<size_t> relocs){
    const auto bytes=native_reference_code::Relocate(code,reinterpret_cast<uintptr_t>(image),relocs);
    auto* p=reinterpret_cast<uint8_t*>(image)+va-native_reference_code::kImageBase;
    if(!Executable(p,bytes.size(),image)) return false;
    if(va==native_weapon_drop_code::kDropVa && g_drop_site) {
      constexpr size_t offset=native_weapon_drop_code::kDropCastOffset;
      return image==g_exposure_image && DropOwned() && !std::memcmp(p,bytes.data(),offset) &&
          !std::memcmp(p+offset+5,bytes.data()+offset+5,bytes.size()-offset-5);
    }
    if(va==native_weapon_exposure_code::kGivenToVa && g_pickup_site) {
      constexpr size_t offset=native_weapon_exposure_code::kGivenToCastOffset;
      return image==g_exposure_image && PickupOwned() && !std::memcmp(p,bytes.data(),offset) &&
          !std::memcmp(p+offset+5,bytes.data()+offset+5,bytes.size()-offset-5);
    }
    if(va==native_weapon_exposure_code::kFireVa && g_exposure_hook.target) {
      constexpr size_t offset=native_weapon_exposure_code::kPatchOffset;
      return image==g_exposure_image && Owned() && !std::memcmp(p,bytes.data(),offset) &&
          !std::memcmp(g_exposure_hook.original,bytes.data()+offset,6) &&
          !std::memcmp(p+offset+6,bytes.data()+offset+6,bytes.size()-offset-6);
    }
    return !std::memcmp(p,bytes.data(),bytes.size());
  });
}
bool NativeWeaponExposureInstalled(){return g_exposure_hook.target || g_pickup_site || g_drop_site;}
bool RemoveNativeWeaponExposure() {
  if(g_exposure_thread && GetCurrentThreadId()!=g_exposure_thread) return false;
  PublishNativeWeaponExposurePawns(nullptr,nullptr);
  if((g_exposure_hook.target && !Owned()) || (g_pickup_site && !PickupOwned()) || (g_drop_site && !DropOwned())) return false;
  if(g_drop_site) {
    if(!WriteRoleCall(g_drop_site,native_weapon_drop_code::kDrop+native_weapon_drop_code::kDropCastOffset)) return false;
    g_drop_site=nullptr;
  }
  if(g_pickup_site) {
    if(!WriteRoleCall(g_pickup_site,native_weapon_exposure_code::kGivenTo+native_weapon_exposure_code::kGivenToCastOffset)) return false;
    g_pickup_site=nullptr;WfcPickupCastOriginal=nullptr;
  }
  if(g_exposure_hook.target && !RemoveInlineHook(&g_exposure_hook)) return false;
  g_exposure_image=nullptr;g_exposure_thread=0;g_exposure_resolver=nullptr;
  WfcExposureOriginal=WfcExposureContinue=WfcExposureReturn=nullptr;
  return true;
}
bool InstallNativeWeaponExposure(HMODULE image,DWORD thread,NativeWeaponExposureResolver resolver) {
  if(!thread || thread!=GetCurrentThreadId() || !resolver || NativeWeaponExposureInstalled() ||
      !ValidateNativeWeaponExposureCode(image)) return false;
  using namespace native_weapon_exposure_code;
  auto* fire=reinterpret_cast<uint8_t*>(image)+kFireVa-native_reference_code::kImageBase;
  g_exposure_image=image;g_exposure_thread=thread;g_exposure_resolver=resolver;
  PublishNativeWeaponExposurePawns(nullptr,nullptr);
  WfcExposureContinue=fire+kExposeOffset;WfcExposureReturn=fire+kReturnOffset;
  if(!InstallInlineHook(&g_exposure_hook,fire+kPatchOffset,reinterpret_cast<void*>(&WfcWeaponExposureGate),
      kFire+kPatchOffset,kPrefix,&WfcExposureOriginal)) {RemoveNativeWeaponExposure();return false;}
  // Replace one CALL, not the global IsA implementation. Never copy a relative
  // CALL into an unrelocated trampoline. Original ABI is ECX=raw pawn, stack
  // raw class, EAX=bool, RET4; the caller still runs its original branch code.
  g_pickup_site=reinterpret_cast<uint8_t*>(image)+kGivenToVa-native_reference_code::kImageBase+kGivenToCastOffset;
  WfcPickupCastOriginal=reinterpret_cast<uint8_t*>(image)+0x10901430u-native_reference_code::kImageBase;
  JumpBytes(g_pickup_patch,g_pickup_site,reinterpret_cast<void*>(&WfcWeaponPickupCast));g_pickup_patch[0]=0xE8;
  if(!WriteRoleCall(g_pickup_site,g_pickup_patch)) {
    g_pickup_site=nullptr;WfcPickupCastOriginal=nullptr;RemoveNativeWeaponExposure();return false;
  }
  // DropFrom captures this role before mesh/animation/base-drop callbacks.
  // Keep its own native ammo copy/default-pickup branch; no corrective second
  // assignment or reflected SetupAmmoForDefaultPlayerPickup afterward.
  g_drop_site=reinterpret_cast<uint8_t*>(image)+native_weapon_drop_code::kDropVa-
      native_reference_code::kImageBase+native_weapon_drop_code::kDropCastOffset;
  JumpBytes(g_drop_patch,g_drop_site,reinterpret_cast<void*>(&WfcWeaponPickupCast));g_drop_patch[0]=0xE8;
  if(!WriteRoleCall(g_drop_site,g_drop_patch)) {
    g_drop_site=nullptr;RemoveNativeWeaponExposure();return false;
  }
  return true;
}
}
