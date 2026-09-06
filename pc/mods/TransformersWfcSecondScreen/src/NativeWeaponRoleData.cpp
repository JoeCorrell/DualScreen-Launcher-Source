#include "NativeWeaponRoleData.h"
#include "NativeWeaponRoleDataCode.h"
#include "UnrealBridge.h"
#include <cstring>
namespace wfcds {
namespace {
HMODULE g_role_data_image=nullptr;
DWORD g_role_data_thread=0;
thread_local bool g_role_data_busy=false;
bool RoleDataMemory(const void* p,size_t n,bool write=false,HMODULE image=nullptr) {
  MEMORY_BASIC_INFORMATION m{};
  if (!p || !VirtualQuery(p,&m,sizeof(m)) || m.State!=MEM_COMMIT || (m.Protect&(PAGE_GUARD|PAGE_NOACCESS)) ||
      n>m.RegionSize-(reinterpret_cast<uintptr_t>(p)-reinterpret_cast<uintptr_t>(m.BaseAddress))) return false;
  const auto protection=m.Protect&0xFF;
  if (image && (m.AllocationBase!=image || (protection!=PAGE_EXECUTE && protection!=PAGE_EXECUTE_READ &&
      protection!=PAGE_EXECUTE_READWRITE && protection!=PAGE_EXECUTE_WRITECOPY))) return false;
  return !write || protection==PAGE_READWRITE || protection==PAGE_WRITECOPY ||
      protection==PAGE_EXECUTE_READWRITE || protection==PAGE_EXECUTE_WRITECOPY;
}
bool RoleDataImage(HMODULE image) {
  if (!image) return false;
  const auto base=reinterpret_cast<uintptr_t>(image);
  const auto* role=reinterpret_cast<const void*>(base+native_weapon_role_code::kRoleVa-native_reference_code::kImageBase);
  const auto* data=reinterpret_cast<const void*>(base+native_weapon_role_code::kDataVa-native_reference_code::kImageBase);
  const auto relocated=native_reference_code::Relocate(native_weapon_role_code::kData,base,{24,47});
  return RoleDataMemory(role,sizeof(native_weapon_role_code::kRole),false,image) &&
      !std::memcmp(role,native_weapon_role_code::kRole,sizeof(native_weapon_role_code::kRole)) &&
      RoleDataMemory(data,relocated.size(),false,image) && !std::memcmp(data,relocated.data(),relocated.size());
}
bool ReadRoleData(void* weapon,uint8_t role,void** data) {
  return (role==1 || role==2) && UnrealReadObjectProperty(weapon,role==1 ? L"PlayerData":L"AiData",data) &&
      UnrealObjectIsA(*data,L"TnWeaponData") && UnrealObjectIndex(*data);
}
bool RoleDataLayout(void* weapon,uint8_t* role) {
  uint32_t offset=0;
  return UnrealResolveBytePropertyOffset(weapon,L"WeaponDataType",&offset) && offset==0x329 &&
      UnrealReadByteProperty(weapon,L"WeaponDataType",role) &&
      RoleDataMemory(static_cast<uint8_t*>(weapon)+0x328,4,true) &&
      static_cast<uint8_t*>(weapon)[0x329]==*role &&
      UnrealResolveObjectPropertyOffset(weapon,L"Data",&offset) && offset==0x240 &&
      RoleDataMemory(static_cast<uint8_t*>(weapon)+0x240,4,true) &&
      RoleDataMemory(static_cast<uint8_t*>(weapon)+0x30,4,true);
}
bool RoleDataRoots(const NativeWeaponRoleData& s) {
  void *previous=nullptr,*desired=nullptr;
  return s.captured && s.context.captured &&
      ReadRoleData(s.context.objects[1],s.previous_role,&previous) && previous==s.context.objects[4] &&
      UnrealObjectIndex(previous)==s.context.indices[4] &&
      ReadRoleData(s.context.objects[1],s.desired_role,&desired) && desired==s.target && UnrealObjectIndex(desired)==s.target_index;
}
}
bool NativeCaptureWeaponRoleData(HMODULE image,DWORD thread,void* pawn,void* weapon,uint8_t role,NativeWeaponRoleData* out) {
  if (!out) return false;
  *out={};
  if (g_role_data_busy || !thread || thread!=GetCurrentThreadId() || (g_role_data_thread && g_role_data_thread!=thread) ||
      (g_role_data_image && image!=g_role_data_image) || !UnrealObjectReferenceWritesReady() || !RoleDataImage(image) ||
      !NativeCaptureWeaponProgress(thread,pawn,weapon,&out->context) || !RoleDataLayout(weapon,&out->previous_role) ||
      !ReadRoleData(weapon,role,&out->target)) return false;
  out->desired_role=role;out->target_index=UnrealObjectIndex(out->target);out->captured=true;
  if (!RoleDataRoots(*out)) { out->captured=false;return false; }
  g_role_data_image=image;g_role_data_thread=thread;return true;
}
NativePlayerServiceResult NativeAssignWeaponRoleData(const NativeWeaponRoleData& s,bool rollback) {
  NativeWeaponProgress current{};uint8_t role=0;
  if (g_role_data_busy || !g_role_data_thread || GetCurrentThreadId()!=g_role_data_thread || s.context.thread!=g_role_data_thread ||
      !UnrealObjectReferenceWritesReady() || !RoleDataImage(g_role_data_image) || !RoleDataRoots(s) ||
      !NativeCaptureWeaponProgress(s.context.thread,s.context.objects[0],s.context.objects[1],&current) ||
      !RoleDataLayout(s.context.objects[1],&role)) return NativePlayerServiceResult::kRejected;
  for (size_t i=0;i<4;++i)
    if (current.objects[i]!=s.context.objects[i] || current.indices[i]!=s.context.indices[i]) return NativePlayerServiceResult::kRejected;
  const uint8_t target_role=rollback ? s.previous_role:s.desired_role;
  void* target=rollback ? s.context.objects[4]:s.target;
  if ((role!=s.previous_role && role!=s.desired_role) ||
      (current.objects[4]!=s.context.objects[4] && current.objects[4]!=s.target)) return NativePlayerServiceResult::kRejected;
  if (role==target_role && current.objects[4]==target) return NativePlayerServiceResult::kApplied;
  if (!rollback && (role!=s.previous_role || current.objects[4]!=s.context.objects[4])) return NativePlayerServiceResult::kRejected;
  g_role_data_busy=true;struct Clear { ~Clear(){g_role_data_busy=false;} } clear;
  auto* weapon=static_cast<uint8_t*>(current.objects[1]);
  // Exact native role setter ORs bit 3 at raw +30 before assigning role. Keep
  // it set on rollback too, just as another native SetWeaponDataType would.
  InterlockedOr(reinterpret_cast<volatile LONG*>(weapon+0x30),8);
  auto* packed=reinterpret_cast<volatile LONG*>(weapon+0x328);
  LONG observed=InterlockedCompareExchange(packed,0,0);bool written=false;
  for (unsigned attempt=0;attempt<8;++attempt) {
    if (((static_cast<uint32_t>(observed)>>8)&0xFF)!=role) break;
    const auto next=static_cast<LONG>((static_cast<uint32_t>(observed)&~0xFF00u)|(static_cast<uint32_t>(target_role)<<8));
    const LONG actual=InterlockedCompareExchange(packed,next,observed);
    if (actual==observed) { written=true;break; }
    observed=actual;
  }
  if (!written || !UnrealWriteObjectPropertyExpected(weapon,L"Data",current.objects[4],target))
    return NativePlayerServiceResult::kStateMismatch;
  NativeWeaponProgress after{};uint8_t after_role=0;
  if (!RoleDataRoots(s) || !RoleDataLayout(weapon,&after_role) || after_role!=target_role ||
      !NativeCaptureWeaponProgress(s.context.thread,current.objects[0],weapon,&after) || after.objects[4]!=target ||
      after.interval!=current.interval || after.spread!=current.spread) return NativePlayerServiceResult::kStateMismatch;
  for (size_t i=0;i<current.objects.size();++i)
    if (i!=4 && (after.objects[i]!=current.objects[i] || after.indices[i]!=current.indices[i])) return NativePlayerServiceResult::kStateMismatch;
  return NativePlayerServiceResult::kApplied;
}
}
