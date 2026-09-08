#include "NativeCharacterIdentity.h"
#include "NativeCharacterIdentityCode.h"
#include "UnrealBridge.h"
#include <cstring>

namespace wfcds {
extern "C" void WfcCallEdiEax(void* function, void* edi, void* eax);
namespace {
HMODULE g_identity_image=nullptr;
DWORD g_identity_thread=0;
thread_local bool g_identity_busy=false;
constexpr uint32_t kResolvedSize=0x88;
struct IdentityField { const wchar_t *name,*type; uint32_t offset,size; };
constexpr IdentityField kIdentityFields[]={
  {L"CharacterName",L"StrProperty",0,12},{L"CharacterType",L"ByteProperty",0xC,1},
  {L"Specialty",L"StrProperty",0x10,12},{L"StartingForm",L"ByteProperty",0x1C,1},
  {L"RevivedForm",L"ByteProperty",0x1D,1},{L"Skills",L"ArrayProperty",0x20,12},
  {L"SkillLevels",L"ArrayProperty",0x2C,12},{L"WeaponTypes",L"ArrayProperty",0x38,12},
  {L"Abilities",L"ArrayProperty",0x44,12},{L"VehicleWeapons",L"ArrayProperty",0x50,12},
  {L"ChassisType",L"ObjectProperty",0x5C,4},{L"Faction",L"ByteProperty",0x60,1},
  {L"PrimaryColor",L"StructProperty",0x64,4},{L"SecondaryColor",L"StructProperty",0x68,4},
  {L"MeleeWeapon",L"ClassProperty",0x6C,4},{L"EnergonColor",L"StructProperty",0x70,16},
  {L"bEnableOpacity",L"BoolProperty",0x80,4},{L"Opacity",L"FloatProperty",0x84,4}
};
bool IdentityMemory(const void* p,size_t n,bool write=false,HMODULE image=nullptr) {
  MEMORY_BASIC_INFORMATION m{};
  if (!p || !n || !VirtualQuery(p,&m,sizeof(m)) || m.State!=MEM_COMMIT ||
      (m.Protect&(PAGE_GUARD|PAGE_NOACCESS)) ||
      n>m.RegionSize-(reinterpret_cast<uintptr_t>(p)-reinterpret_cast<uintptr_t>(m.BaseAddress))) return false;
  const DWORD protection=m.Protect&0xFF;
  if (image && (m.AllocationBase!=image || (protection!=PAGE_EXECUTE && protection!=PAGE_EXECUTE_READ &&
      protection!=PAGE_EXECUTE_READWRITE && protection!=PAGE_EXECUTE_WRITECOPY))) return false;
  return !write || protection==PAGE_READWRITE || protection==PAGE_WRITECOPY ||
      protection==PAGE_EXECUTE_READWRITE || protection==PAGE_EXECUTE_WRITECOPY;
}
bool IdentityImage(HMODULE image) {
  if (!image) return false;
  const auto base=reinterpret_cast<uintptr_t>(image);
  const auto match=[&](uintptr_t va,const auto& code) {
    const auto* p=reinterpret_cast<const void*>(base+va-native_reference_code::kImageBase);
    return IdentityMemory(p,code.size(),false,image) && !std::memcmp(p,code.data(),code.size());
  };
  using namespace native_character_identity_code;
  return match(kSetterVa,Setter(base)) && match(kCopyVa,Copy(base)) &&
      match(kBaseCopyVa,BaseCopy(base)) && match(kRefreshVa,Refresh(base)) &&
      match(native_reference_code::kBarrierVa,
            native_reference_code::Relocate(native_reference_code::kBarrier,base,{15,38}));
}
struct IdentityValue {
  UnrealStructValueView view{};
  std::array<uint8_t,kResolvedSize> scalar{},raw{};
  std::array<UnrealArrayView,7> storage{};
  // FString UTF-16 (bounded 1024 code units), then five class/byte arrays.
  std::array<std::array<uint8_t,2048>,7> values{};
  std::array<uint32_t,7> lengths{};
  std::array<uint32_t,322> object_indices{};
};
bool IdentityOverlap(const void* a,size_t an,const void* b,size_t bn) {
  if (!a || !b || !an || !bn) return false;
  const auto av=reinterpret_cast<uintptr_t>(a),bv=reinterpret_cast<uintptr_t>(b);
  return av<=bv ? bv-av<an:av-bv<bn;
}
constexpr uint32_t kStorageOffsets[]={0,0x10,0x20,0x2C,0x38,0x44,0x50};
constexpr uint32_t kStorageWidths[]={2,2,4,1,4,4,4};
bool IdentityDisjoint(const IdentityValue& a,const IdentityValue& b) {
  if (IdentityOverlap(a.view.data,a.view.size,b.view.data,b.view.size)) return false;
  for (size_t i=0;i<7;++i) {
    const size_t n=a.storage[i].capacity*kStorageWidths[i];
    if (IdentityOverlap(a.storage[i].data,n,b.view.data,b.view.size)) return false;
    for (size_t j=0;j<7;++j)
      if (IdentityOverlap(a.storage[i].data,n,b.storage[j].data,b.storage[j].capacity*kStorageWidths[j])) return false;
  }
  return true;
}
bool IdentityValueRead(void* object,bool destination,IdentityValue* out) {
  *out={};auto& s=*out;
  if (!UnrealReadStructProperty(object,L"_CurrentCharacter",&s.view) || s.view.size!=kResolvedSize ||
      !UnrealObjectIndex(s.view.struct_definition) || !IdentityMemory(s.view.data,s.view.size,destination)) return false;
  for (const auto& field:kIdentityFields) {
    uint32_t actual=0;
    if (!UnrealResolveStructFieldOffset(s.view,field.name,field.type,field.size,&actual) || actual!=field.offset) return false;
  }
  std::memcpy(s.raw.data(),s.view.data,kResolvedSize);s.scalar=s.raw;
  // Assignment deliberately preserves native padding and unrelated packed bits.
  for (uint32_t offset:{0xDu,0xEu,0xFu,0x1Eu,0x1Fu,0x61u,0x62u,0x63u,0x81u,0x82u,0x83u}) s.scalar[offset]=0;
  s.scalar[0x80]&=1;
  size_t object_slot=0;
  for (size_t i=0;i<7;++i) {
    auto& a=s.storage[i];std::memcpy(&a,s.raw.data()+kStorageOffsets[i],sizeof(a));
    const int32_t limit=i<2 ? 1024:64;
    if (a.count<0 || a.capacity<a.count || a.capacity>limit ||
        (a.capacity && !IdentityMemory(a.data,a.capacity*kStorageWidths[i],destination)) ||
        (!a.capacity && a.data)) return false;
    s.lengths[i]=a.count*kStorageWidths[i];
    if (s.lengths[i]) std::memcpy(s.values[i].data(),a.data,s.lengths[i]);
    if (i<2) {
      if (a.count && (s.values[i][s.lengths[i]-1] || s.values[i][s.lengths[i]-2])) return false;
    } else {
      UnrealArrayView typed{};
      const auto* field=i==2 ? L"Skills":i==3 ? L"SkillLevels":i==4 ? L"WeaponTypes":i==5 ? L"Abilities":L"VehicleWeapons";
      if (!UnrealReadStructTypedArray(s.view,field,i==3 ? L"ByteProperty":L"ClassProperty",kStorageWidths[i],&typed,64) ||
          std::memcmp(&a,&typed,sizeof(a))) return false;
      if (i!=3) for (int32_t j=0;j<a.count;++j) {
        void* item=nullptr;std::memcpy(&item,s.values[i].data()+j*4,4);
        const uint32_t index=UnrealObjectIndex(item);
        if (item && (!index || !UnrealObjectIsA(item,L"Class"))) return false;
        s.object_indices[object_slot++]=index;
      }
    }
    const size_t n=a.capacity*kStorageWidths[i];
    if (IdentityOverlap(a.data,n,s.view.data,s.view.size)) return false;
    for (size_t j=0;j<i;++j)
      if (IdentityOverlap(a.data,n,s.storage[j].data,s.storage[j].capacity*kStorageWidths[j])) return false;
    std::memset(s.scalar.data()+kStorageOffsets[i],0,12);
  }
  for (uint32_t offset:{0x5Cu,0x6Cu}) {
    void* item=nullptr;std::memcpy(&item,s.raw.data()+offset,4);
    const uint32_t index=UnrealObjectIndex(item);
    if (item && (!index || !UnrealObjectIsA(item,offset==0x5C ? L"TnTransformer":L"Class"))) return false;
    s.object_indices[object_slot++]=index;
  }
  return true;
}
bool IdentityValueSame(const IdentityValue& a,const IdentityValue& b,bool same_storage) {
  return a.view.struct_definition==b.view.struct_definition && a.scalar==b.scalar &&
      a.lengths==b.lengths && a.values==b.values && a.object_indices==b.object_indices &&
      (!same_storage || (a.view.data==b.view.data && a.raw==b.raw));
}
struct IdentityCapture {
  std::array<void*,5> objects{}; // PRI, pawn, PRI owner, world, player provider
  std::array<uint32_t,5> indices{};
  IdentityValue current{},source{};
  std::array<uint8_t,0xB0> selected{};
  std::array<uint8_t,12> selected_string{};
  std::array<uint8_t,0xB4> replicated{};
  UnrealStructValueView replicated_view{};
  UnrealArrayView display{};
};
bool IdentityActor(void* object,const wchar_t* type) {
  bool deleting=true;
  return UnrealObjectIsA(object,type) && UnrealObjectIndex(object) &&
      UnrealReadBoolProperty(object,L"bDeleteMe",&deleting) && !deleting;
}
bool IdentityObjectField(void* object,const wchar_t* field,uint32_t offset,void** value) {
  uint32_t actual=0;void* raw=nullptr;
  if (!UnrealResolveObjectPropertyOffset(object,field,&actual) || actual!=offset ||
      !UnrealReadObjectProperty(object,field,value) || !IdentityMemory(static_cast<uint8_t*>(object)+offset,4)) return false;
  std::memcpy(&raw,static_cast<uint8_t*>(object)+offset,4);return raw==*value;
}
bool CaptureIdentity(void* pri,void* pawn,bool require_pair,IdentityCapture* out) {
  *out={};auto& s=*out;s.objects[0]=pri;s.objects[1]=pawn;
  void *pawn_world=nullptr,*owner_world=nullptr;uint8_t mode=255,role=0;uint32_t offset=0;
  if (!IdentityActor(pri,L"TnPlayerReplicationInfo") || !IdentityActor(pawn,L"TnPawn") ||
      !IdentityObjectField(pri,L"Owner",0x2C,&s.objects[2]) || !IdentityActor(s.objects[2],L"Controller") ||
      !IdentityObjectField(pri,L"WorldInfo",0x98,&s.objects[3]) || !IdentityActor(s.objects[3],L"WorldInfo") ||
      !UnrealReadObjectProperty(pawn,L"WorldInfo",&pawn_world) || pawn_world!=s.objects[3] ||
      !IdentityObjectField(s.objects[2],L"WorldInfo",0x98,&owner_world) || owner_world!=pawn_world ||
      !UnrealReadByteProperty(pawn_world,L"NetMode",&mode) || mode!=0 ||
      !UnrealReadByteProperty(s.objects[2],L"Role",&role) || role!=3 ||
      !IdentityMemory(static_cast<uint8_t*>(s.objects[2])+0x66,1) || static_cast<uint8_t*>(s.objects[2])[0x66]!=role ||
      !IdentityValueRead(pri,true,&s.current) || !IdentityValueRead(pawn,false,&s.source) ||
      s.current.view.struct_definition!=s.source.view.struct_definition ||
      !UnrealResolveStructPropertyOffset(pri,L"_CurrentCharacter",kResolvedSize,&offset) || offset!=0x3C4 ||
      s.current.view.data!=static_cast<uint8_t*>(pri)+offset ||
      !IdentityDisjoint(s.current,s.source) || !IdentityDisjoint(s.source,s.current)) return false;
  if (require_pair) {
    void *controlled=nullptr,*controller=nullptr,*owner_pri=nullptr;
    if (!UnrealReadObjectProperty(s.objects[2],L"Pawn",&controlled) || controlled!=pawn ||
        !UnrealReadObjectProperty(pawn,L"Controller",&controller) || controller!=s.objects[2] ||
        !UnrealReadObjectProperty(s.objects[2],L"PlayerReplicationInfo",&owner_pri) || owner_pri!=pri) return false;
  }
  if (UnrealObjectIsA(s.objects[2],L"TnPlayerController")) {
    if (!IdentityObjectField(s.objects[2],L"CurrentPlayerData",0x2E8,&s.objects[4]) ||
        (s.objects[4] && (!UnrealObjectIsA(s.objects[4],L"PlayerOwnerDataStore") || !UnrealObjectIndex(s.objects[4])))) return false;
  } else if (!UnrealObjectIsA(s.objects[2],L"TnAiController")) return false;
  UnrealStructValueView selected{};
  // These are NOT mutation targets. Retain their headers/scalars to detect a
  // provider callback trying to initiate selection/loading during the handoff.
  if (!UnrealReadStructProperty(pri,L"_SelectedCharacter",&selected) || selected.size!=s.selected.size() ||
      selected.data!=static_cast<uint8_t*>(pri)+0x314 || !IdentityMemory(selected.data,selected.size) ||
      !UnrealReadStructProperty(pri,L"ReplicatedCharacterData",&s.replicated_view) ||
      s.replicated_view.size!=s.replicated.size() || !IdentityMemory(s.replicated_view.data,s.replicated.size()) ||
      !IdentityMemory(static_cast<uint8_t*>(pri)+0x484,24,true)) return false;
  const UnrealStructValueView pri_view{pri,0x49C,UnrealObjectClass(pri)};
  if (!UnrealResolveStructFieldOffset(pri_view,L"CurrentCharacterString",L"StrProperty",12,&offset) || offset!=0x484 ||
      !UnrealResolveStructFieldOffset(pri_view,L"SelectedCharacterString",L"StrProperty",12,&offset) || offset!=0x490) return false;
  std::memcpy(&s.display,static_cast<uint8_t*>(pri)+0x484,12);
  if (s.display.count<0 || s.display.capacity<s.display.count || s.display.capacity>1024 ||
      (s.display.capacity && !IdentityMemory(s.display.data,s.display.capacity*2,true)) ||
      (!s.display.capacity && s.display.data)) return false;
  if (s.display.count) {
    uint16_t terminal=1;
    std::memcpy(&terminal,static_cast<uint8_t*>(s.display.data)+(s.display.count-1)*2,2);
    if (terminal) return false;
  }
  // FString assignment may free its old allocation. Reject any pre-existing
  // shallow copy of the display string into a pawn/PRI character buffer.
  for (const auto* value:{&s.current,&s.source}) {
    if (IdentityOverlap(s.display.data,s.display.capacity*2,value->view.data,value->view.size)) return false;
    for (size_t i=0;i<7;++i)
      if (IdentityOverlap(s.display.data,s.display.capacity*2,value->storage[i].data,
                          value->storage[i].capacity*kStorageWidths[i])) return false;
  }
  std::memcpy(s.selected.data(),selected.data,s.selected.size());
  std::memcpy(s.selected_string.data(),static_cast<uint8_t*>(pri)+0x490,12);
  std::memcpy(s.replicated.data(),s.replicated_view.data,s.replicated.size());
  for (size_t i=0;i<s.objects.size();++i) {
    s.indices[i]=UnrealObjectIndex(s.objects[i]);if (s.objects[i] && !s.indices[i]) return false;
  }
  return true;
}
bool IdentityGraphSame(const IdentityCapture& a,const IdentityCapture& b) {
  return a.objects==b.objects && a.indices==b.indices && a.selected==b.selected &&
      a.selected_string==b.selected_string && a.replicated==b.replicated &&
      a.replicated_view.data==b.replicated_view.data &&
      a.replicated_view.struct_definition==b.replicated_view.struct_definition;
}
}
bool NativeCharacterIdentityReady(HMODULE image,DWORD thread,void* pri,void* pawn) {
  if (g_identity_busy || !thread || thread!=GetCurrentThreadId() ||
      (g_identity_thread && g_identity_thread!=thread) || (g_identity_image && g_identity_image!=image) ||
      !UnrealObjectReferenceWritesReady() || !IdentityImage(image)) return false;
  IdentityCapture a{},b{};
  if (!CaptureIdentity(pri,pawn,false,&a) || !CaptureIdentity(pri,pawn,false,&b) ||
      !IdentityGraphSame(a,b) || !IdentityValueSame(a.current,b.current,true) || !IdentityValueSame(a.source,b.source,true)) return false;
  g_identity_image=image;g_identity_thread=thread;return true;
}
NativePlayerServiceResult NativeAssignCurrentCharacter(void* pri,void* pawn) {
  if (g_identity_busy || !g_identity_thread || GetCurrentThreadId()!=g_identity_thread ||
      !UnrealObjectReferenceWritesReady() || !IdentityImage(g_identity_image)) return NativePlayerServiceResult::kRejected;
  IdentityCapture before{},check{};
  if (!CaptureIdentity(pri,pawn,true,&before) || !CaptureIdentity(pri,pawn,true,&check) ||
      !IdentityGraphSame(before,check) || !IdentityValueSame(before.current,check.current,true) ||
      !IdentityValueSame(before.source,check.source,true) ||
      std::memcmp(&before.display,&check.display,sizeof(before.display))) return NativePlayerServiceResult::kRejected;
  g_identity_busy=true;struct Clear { ~Clear(){g_identity_busy=false;} } clear;
  // Independently confirmed by TnCharacterApplier call at 10BE272D: EAX is
  // PRI+10, EDI points to CharacterDataResolved, no stack args, plain RET.
  WfcCallEdiEax(reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(g_identity_image)+
      native_character_identity_code::kSetterVa-native_reference_code::kImageBase),
      const_cast<void*>(before.source.view.data),static_cast<uint8_t*>(pri)+0x10);
  IdentityCapture after{};
  if (!CaptureIdentity(pri,pawn,true,&after) || !IdentityGraphSame(before,after) ||
      !IdentityValueSame(before.source,after.source,true) || !IdentityValueSame(before.source,after.current,false))
    return NativePlayerServiceResult::kStateMismatch;
  UnrealArrayView display{};std::memcpy(&display,static_cast<uint8_t*>(pri)+0x484,12);
  const auto& specialty=after.current.storage[1];
  if (display.count!=specialty.count || display.capacity<display.count || display.capacity>1024 ||
      (display.capacity && !IdentityMemory(display.data,display.capacity*2,true)) ||
      (!display.capacity && display.data) ||
      (display.count && std::memcmp(display.data,specialty.data,display.count*2))) return NativePlayerServiceResult::kStateMismatch;
  return NativePlayerServiceResult::kApplied;
}
}
