// Real compiled HUD bodies execute. Native Scaleform allocation/index/Invoke/
// destructor dependencies are fixtures; this is NOT a rendered gameplay test.
#include "NativeAbilityHud.cpp"
#include <cassert>
#include <cstdio>
#include <cwchar>
#include <fstream>
#include <vector>
using namespace wfcds;
namespace {
struct Object { alignas(16) uint8_t bytes[0x600]{}; const wchar_t* type; uint32_t index=1; bool valid=true,dead=false; };
Object player{{},L"TnPlayerController"},pawn{{},L"TnPawn"},hud{{},L"TnHUD"},
    world{{},L"WorldInfo"},movie{{},L"GFxMovie"},foreign{{},L"WorldInfo"};
Object* objects[]={&player,&pawn,&hud,&world,&movie,&foreign};
uint8_t* image=nullptr;
uint8_t native_movie[0x40]{},scaleform[4]{};
wchar_t callback[]=L"updateAbilityCooldown";
NativePlayerInputLease move,look;
AbilityHudPlan active_plan;
std::vector<AbilityHudCommand> sent;
unsigned allocations=0,frees=0,mutation=0;
bool bad_offset=false,bad_string=false;
Object* Lookup(void* p) { for (auto* o:objects) if (o->valid && p==o->bytes) return o; return nullptr; }
uint32_t Read(const void* p,size_t offset) { uint32_t x; std::memcpy(&x,static_cast<const uint8_t*>(p)+offset,4); return x; }
void Put(void* p,size_t offset,uintptr_t value) { uint32_t x=static_cast<uint32_t>(value); std::memcpy(static_cast<uint8_t*>(p)+offset,&x,4); }
void Link(Object& o,size_t offset,Object* v) { Put(o.bytes,offset,reinterpret_cast<uintptr_t>(v ? v->bytes:nullptr)); }
uint8_t* At(uintptr_t va) { return image+va-native_reference_code::kImageBase; }
void Jump(uintptr_t va,void* target) { At(va)[0]=0xE9; Put(At(va),1,reinterpret_cast<uintptr_t>(target)-reinterpret_cast<uintptr_t>(At(va)+5)); }
bool Offset(void* p,const wchar_t* name,uint32_t* out) {
  auto* o=Lookup(p); if (!o) return false;
  const auto field=[&](const wchar_t* n,uint32_t offset) { if (std::wcscmp(n,name)) return false; *out=offset; return true; };
  if (field(L"WorldInfo",0x98)) return true;
  if (o==&player) return field(L"Pawn",0x1C8) || field(L"AcknowledgedPawn",0x300) || field(L"myHUD",0x400);
  if (o==&pawn) return field(L"Controller",0x1D4);
  if (o==&hud) return field(L"PlayerOwner",0x200) || field(L"HudMovie",0x454);
  return false;
}
NativeAbilityHudResult Dispatch() { return NativeDispatchAbilityHud(player.bytes,pawn.bytes,hud.bytes,active_plan,move,look); }
void Reset() {
  uint32_t id=1; for (auto* o:objects) { std::memset(o->bytes,0,sizeof(o->bytes)); o->index=id++; o->valid=true; o->dead=false; }
  Link(player,0x1C8,&pawn); Link(player,0x300,&pawn); Link(pawn,0x1D4,&player);
  Link(player,0x98,&world); Link(pawn,0x98,&world); Link(hud,0x98,&world);
  Link(player,0x400,&hud); Link(hud,0x200,&player); Link(hud,0x454,&movie);
  player.bytes[0x66]=pawn.bytes[0x66]=hud.bytes[0x66]=3;
  Put(movie.bytes,0x3C,reinterpret_cast<uintptr_t>(native_movie));
  Put(At(0x12B000B4),0,reinterpret_cast<uintptr_t>(scaleform));
  Put(hud.bytes,0x4A0,reinterpret_cast<uintptr_t>(callback));
  Put(hud.bytes,0x4A4,std::size(callback)); Put(hud.bytes,0x4A8,std::size(callback));
  move={}; look={}; move.controller=look.controller=player.bytes; move.world=look.world=world.bytes;
  move.held=look.held=true; look.axis=NativeInputAxis::kLook;
  mutation=0; bad_offset=bad_string=false; sent.clear();
  assert(BuildAbilityHudPlan({AbilityHudSlot{true,0,0},{true,4,8},{true,10,20}},2,30,50,&active_plan));
}
void Rejected() { const auto count=sent.size(); assert(Dispatch()==NativeAbilityHudResult::kRejected && sent.size()==count); }
DWORD WINAPI WrongThread(void*) { Rejected(); return 0; }
void Add(UnrealArrayView* a,int index,int count) {
  assert(count==1 && index==a->count && a->count<3);
  auto* next=static_cast<uint8_t*>(HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,(a->count+1)*40)); assert(next); ++allocations;
  if (a->data) { std::memcpy(next,a->data,a->count*40); assert(HeapFree(GetProcessHeap(),0,a->data)); ++frees; }
  a->data=next; ++a->count; a->capacity=a->count;
}
void* __attribute__((regparm(1))) Element(UnrealArrayView* a,int index) {
  assert(index>=0 && index<a->count); return static_cast<uint8_t*>(a->data)+index*40;
}
void __attribute__((fastcall)) Destroy(void* array,void*) {
  auto* a=static_cast<UnrealArrayView*>(array); assert(a->count==3);
  assert(HeapFree(GetProcessHeap(),0,a->data)); ++frees; *a={};
}
void __attribute__((fastcall)) Invoke(void* array,void*,void* m,void* output,void* name) {
  assert(m==movie.bytes && name==hud.bytes+0x4A0);
  auto* a=static_cast<UnrealArrayView*>(array); assert(a->count==3);
  float values[3]{};
  for (unsigned i=0;i<3;++i) {
    const auto* v=static_cast<uint8_t*>(a->data)+i*40; assert(v[0]==2); std::memcpy(&values[i],v+8,4);
  }
  sent.push_back({static_cast<uint32_t>(values[0]),values[1]==-1,values[1],values[2]});
  std::memset(output,0,40);
  assert(Dispatch()==NativeAbilityHudResult::kRejected); // Callback recursion.
  if (mutation==1) look.held=false;
  if (mutation==2) ++pawn.index;
  if (mutation==3) Link(hud,0x200,nullptr);
  if (mutation==4) Put(movie.bytes,0x3C,0);
  if (mutation==5) Link(player,0x98,&foreign);
  if (mutation==6) callback[0]=L'X';
  if (mutation==7) active_plan={}; // Caller storage cannot alter an in-flight batch.
}
void Plans() {
  constexpr float maximum=std::numeric_limits<float>::max();
  const float nan=std::numeric_limits<float>::quiet_NaN();
  AbilityHudPlan plan;
  std::array<AbilityHudSlot,3> slots={AbilityHudSlot{true,4,8},{true,maximum,0},{true,0,0}};
  assert(BuildAbilityHudPlan(slots,2,0,0,&plan) && plan.count==3);
  assert(!plan.commands[0].ready && plan.commands[0].duration==2 && plan.commands[0].progress==0.5f);
  assert(plan.commands[1].duration==maximum && plan.commands[1].progress==0);
  assert(plan.commands[2].ready);
  assert(BuildAbilityHudPlan(slots,0,0,0,&plan) && plan.commands[0].duration==maximum && plan.commands[0].progress==0.5f);
  assert(BuildAbilityHudPlan(slots,1,25,50,&plan) && plan.commands[0].duration==0 && plan.commands[0].progress==0.5f);
  assert(BuildAbilityHudPlan(slots,1,50,50,&plan) && plan.count==4 && plan.commands[1].ready && plan.commands[1].slot==0);
  assert(!BuildAbilityHudPlan(slots,nan,0,0,&plan) && !plan.count);
  assert(!BuildAbilityHudPlan(slots,-1,0,0,&plan));
  assert(!BuildAbilityHudPlan(slots,1,nan,0,&plan));
  assert(!BuildAbilityHudPlan(slots,1,0,nan,&plan));
  slots[0].effective_duration=0; assert(!BuildAbilityHudPlan(slots,1,0,0,&plan));
  slots[0].remaining=nan; assert(!BuildAbilityHudPlan(slots,1,0,0,&plan));
  slots[0]={false,nan,nan}; assert(BuildAbilityHudPlan(slots,1,0,0,&plan) && plan.commands[0].ready);
  slots[0]={true,4,8}; assert(!BuildAbilityHudPlan(slots,std::numeric_limits<float>::denorm_min(),0,0,&plan));
  assert(!BuildAbilityHudPlan(slots,1,0,0,nullptr));
}
} // namespace
namespace wfcds {
uint32_t UnrealObjectIndex(void* p) { auto* o=Lookup(p); return o ? o->index:0; }
bool UnrealObjectIsA(void* p,const wchar_t* type) { auto* o=Lookup(p); return o && !std::wcscmp(type,o->type); }
bool UnrealReadBoolProperty(void* p,const wchar_t* name,bool* out) {
  auto* o=Lookup(p); if (!o || std::wcscmp(name,L"bDeleteMe")) return false; *out=o->dead; return true;
}
bool UnrealResolveObjectPropertyOffset(void* p,const wchar_t* name,uint32_t* out) {
  if (!Offset(p,name,out)) return false;
  if (bad_offset) *out+=4;
  return true;
}
bool UnrealReadObjectProperty(void* p,const wchar_t* name,void** out) {
  uint32_t offset=0; if (!Offset(p,name,&offset)) return false;
  *out=reinterpret_cast<void*>(Read(p,offset)); return !*out || Lookup(*out);
}
bool UnrealReadByteProperty(void* p,const wchar_t* name,uint8_t* out) {
  if (!Lookup(p)) return false;
  if (!std::wcscmp(name,L"Role")) { *out=static_cast<uint8_t*>(p)[0x66]; return true; }
  if (p==world.bytes && !std::wcscmp(name,L"NetMode")) { *out=world.bytes[0x39C]; return true; }
  return false;
}
bool UnrealReadStringProperty(void* p,const wchar_t* name,char* out,size_t cap) {
  if (p!=hud.bytes || std::wcscmp(name,L"UpdateAbilityCooldownCallBack") || cap<32) return false;
  std::strcpy(out,bad_string ? "other":"updateAbilityCooldown"); return true;
}
bool UnrealReadStringValue(const void* value,char* out,size_t cap) {
  if (value!=hud.bytes+0x4A0 || cap<32) return false;
  std::strcpy(out,"updateAbilityCooldown");
  return true;
}
bool NativePlayerInputHeld(const NativePlayerInputLease& lease) { return (&lease==&move || &lease==&look) && lease.held && !lease.invalidated; }
} // namespace wfcds
int main(int argc,char** argv) {
  Plans();
  image=static_cast<uint8_t*>(VirtualAlloc(nullptr,0x2300000,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE)); assert(image);
  std::vector<uint8_t> exe;
  if (argc==2) {
    std::ifstream file(argv[1],std::ios::binary|std::ios::ate); assert(file && file.tellg()==37667328);
    exe.resize(static_cast<size_t>(file.tellg())); file.seekg(0); assert(file.read(reinterpret_cast<char*>(exe.data()),exe.size()));
  }
  const auto module=reinterpret_cast<HMODULE>(image); const auto base=reinterpret_cast<uintptr_t>(image);
  native_ability_hud_code::Visit([&](uintptr_t va,const auto& code,std::initializer_list<size_t> operands) {
    if (!exe.empty()) assert(!std::memcmp(exe.data()+va-0x10900C00,code,sizeof(code)));
    const auto bytes=native_reference_code::Relocate(code,base,operands); std::memcpy(At(va),bytes.data(),bytes.size()); return true;
  });
  const float minus_one=-1; std::memcpy(At(0x12678790),&minus_one,4);
  HudBinding bound; assert(BindAbilityHud(module,GetCurrentThreadId(),&bound));
  native_ability_hud_code::Visit([&](uintptr_t va,const auto& code,std::initializer_list<size_t>) {
    for (size_t byte=0;byte<sizeof(code);++byte) {
      At(va)[byte]^=1; HudBinding rejected; assert(!BindAbilityHud(module,GetCurrentThreadId(),&rejected) && !rejected.module); At(va)[byte]^=1;
    } return true;
  });
  Jump(0x10AAC240,reinterpret_cast<void*>(&Add)); Jump(0x10AAC490,reinterpret_cast<void*>(&Element));
  // Native Invoke takes EDI=args; destructor takes ESI=array. Adapt ONLY these
  // deep dependencies to fixtures; complete HUD method instructions still run.
  At(0x10C502A0)[0]=0x8B; At(0x10C502A0)[1]=0xCF; Jump(0x10C502A2,reinterpret_cast<void*>(&Invoke));
  At(0x10A67950)[0]=0x8B; At(0x10A67950)[1]=0xCE; Jump(0x10A67952,reinterpret_cast<void*>(&Destroy));
  FlushInstructionCache(GetCurrentProcess(),image,0x2300000); g_ability_hud=bound;
  for (unsigned i=0;i<3000;++i) {
    Reset(); if (i%2) assert(BuildAbilityHudPlan({AbilityHudSlot{true,0,0},{true,0,0},{true,0,0}},1,100,50,&active_plan));
    const auto before=pawn; const auto before_hud=hud;
    assert(Dispatch()==NativeAbilityHudResult::kDispatched && sent.size()==active_plan.count);
    for (size_t j=0;j<sent.size();++j) { const auto& a=sent[j]; const auto& b=active_plan.commands[j];
      assert(a.slot==b.slot && a.ready==b.ready && a.duration==b.duration && a.progress==b.progress); }
    assert(!std::memcmp(before.bytes,pawn.bytes,sizeof(pawn.bytes)) && !std::memcmp(before_hud.bytes,hud.bytes,sizeof(hud.bytes)));
    assert(allocations==frees);
  }
  Reset(); bad_offset=true; Rejected();
  Reset(); bad_string=true; Rejected();
  Reset(); world.bytes[0x39C]=1; Rejected();
  Reset(); pawn.bytes[0x66]=1; Rejected();
  Reset(); Link(player,0x300,nullptr); Rejected();
  Reset(); Link(hud,0x200,nullptr); Rejected();
  Reset(); movie.valid=false; Rejected();
  Reset(); Put(movie.bytes,0x3C,0); Rejected();
  Reset(); Put(At(0x12B000B4),0,0); Rejected();
  Reset(); Put(hud.bytes,0x4A4,300); Rejected();
  Reset(); look.held=false; Rejected();
  Reset(); look.axis=NativeInputAxis::kMove; Rejected();
  Reset(); active_plan.commands[0].slot=3; Rejected();
  Reset(); active_plan.count=5; Rejected();
  Reset(); active_plan.count=0; Rejected();
  Reset(); active_plan.commands[0].duration=-2; Rejected();
  Reset(); active_plan.commands[0].progress=std::numeric_limits<float>::quiet_NaN(); Rejected();
  Reset(); active_plan.commands[0].duration=std::numeric_limits<float>::infinity(); Rejected();
  Reset(); active_plan.commands[0].ready=true; Rejected();
  Reset(); active_plan.commands[0]={0,true,-1,1}; Rejected();
  Reset(); HANDLE thread=CreateThread(nullptr,0,WrongThread,nullptr,0,nullptr); assert(thread);
  assert(WaitForSingleObject(thread,5000)==WAIT_OBJECT_0); CloseHandle(thread);
  for (unsigned change=1;change<=6;++change) {
    Reset(); mutation=change; assert(Dispatch()==NativeAbilityHudResult::kStateMismatch && sent.size()==1 && allocations==frees);
    callback[0]=L'u';
  }
  Reset(); mutation=7; const auto pending=active_plan;
  assert(Dispatch()==NativeAbilityHudResult::kDispatched && sent.size()==pending.count && !active_plan.count);
  for (size_t i=0;i<sent.size();++i) {
    assert(sent[i].slot==pending.commands[i].slot && sent[i].duration==pending.commands[i].duration &&
           sent[i].progress==pending.commands[i].progress);
  }
  Reset(); assert(BuildAbilityHudPlan({AbilityHudSlot{true,4,8},{true,std::numeric_limits<float>::max(),0},{false,0,0}},
                                    0,0,0,&active_plan));
  assert(Dispatch()==NativeAbilityHudResult::kDispatched && sent.size()==3 &&
         sent[0].duration==std::numeric_limits<float>::max() && sent[0].progress==0.5f &&
         sent[1].duration==std::numeric_limits<float>::max() && sent[1].progress==0 && sent[2].ready);
  assert(allocations==frees);
  // Production owner guard must be checked BEFORE callbacks and after each
  // callback, so a dirty ability manager stops the remaining animation batch.
  for (unsigned limit=0;limit<=4;++limit) {
    Reset();
    const NativeAbilityHudValidation validation{[](void* context) {
      return sent.size()<*static_cast<unsigned*>(context);
    },&limit};
    const auto result=NativeDispatchAbilityHud(player.bytes,pawn.bytes,hud.bytes,active_plan,move,look,validation);
    assert(result==(limit==0 ? NativeAbilityHudResult::kRejected:
        limit<=active_plan.count ? NativeAbilityHudResult::kStateMismatch:NativeAbilityHudResult::kDispatched));
    assert(sent.size()==(limit<active_plan.count ? limit:active_plan.count) && allocations==frees);
  }
  Reset(); assert(!PrepareNativeAbilityHud(module,GetCurrentThreadId(),player.bytes,pawn.bytes,hud.bytes)); // Not process EXE.
  assert(VirtualFree(image,0,MEM_RELEASE));
  std::puts("NativeAbilityHudTests passed: 3000 native HUD plans; exact marshalling/cleanup, cooldown/resource policy, guards and dirty callbacks. Scaleform backend and metadata are fixtures, not gameplay.");
}
