// Whole shipping pawn/controller downed bodies; interaction, PRI, dialog,
// camera, weapon and world-state writer services are controlled fixtures.
#include "NativePawnDowned.cpp"
#include <cassert>
#include <cstdio>
#include <fstream>
#include <vector>
using namespace wfcds;
extern "C" __attribute__((naked)) void DownedCall(void*,void*) {
  asm volatile("movl 8(%esp),%ecx; jmp *4(%esp)");
}
namespace {
alignas(16) uint8_t pawns[2][0xA00]{},controllers[2][0x800]{},world[0x500]{},
    dialogs[2][0x200]{},camera[0x200]{},interactions[2][0x300]{},forms[2][0x100]{},
    pri[0x100]{},types[3][0x100]{},type_defaults[3][0x100]{},collections[2][0x100]{},
    entries[2][48]{},dialog_events[2][32]{};
uint8_t* image=nullptr;
enum Event {Clear,Equip,NotifyFriendly,NotifyEnemy,PlayDowned,CameraDowned,OnStart,
    EndUse,PriStart,TeamStart,StateStart,InteractedFalse,ReviveTrue,
    PlayRevived,CameraStart,OnStop,PriStop,TeamStop,StateStop};
std::vector<Event> events;
int current_pawn=0;bool mapped=false,live=true,notify=true,fail_state=false,fail_dialog=false;
unsigned resolutions=0;
template<class T>T Read(const void* p,size_t off){T v{};std::memcpy(&v,static_cast<const uint8_t*>(p)+off,sizeof(v));return v;}
template<class T>void Put(void* p,size_t off,T v){std::memcpy(static_cast<uint8_t*>(p)+off,&v,sizeof(v));}
void* At(uintptr_t va){return image+va-native_reference_code::kImageBase;}
void Jump(uintptr_t va,void* target){DownedJump(static_cast<uint8_t*>(At(va)),At(va),target);}
void Record(Event e) {events.push_back(e);if((fail_state && (e==StateStart || e==StateStop)) || (fail_dialog && e==PlayDowned))live=false;}
bool IsPlayerRole(){return (current_pawn==0)!=mapped;}
void* Resolve(void* p,bool ai) {
  ++resolutions;assert(p==(ai ? static_cast<void*>(pawns[0]):static_cast<void*>(pawns[1])));
  void* c=controllers[ai ? 1:0];
  return live && Read<void*>(p,0x1D4)==c && Read<void*>(c,0x1C8)==p ? c:nullptr;
}
void Reset(bool mapping,int which) {
  mapped=mapping;current_pawn=which;live=true;notify=true;fail_state=fail_dialog=false;resolutions=0;events.clear();
  Put(world,0x39C,uint8_t{0});
  for(int i=0;i<2;++i){
    const int c=mapping ? 1-i:i;
    Put(pawns[i],0x1D4,static_cast<void*>(controllers[c]));Put(controllers[c],0x1C8,static_cast<void*>(pawns[i]));
    Put(pawns[i],0x758,static_cast<void*>(interactions[i]));
    Put(controllers[i],0x66,uint8_t{3});
    Put(entries[i],0,static_cast<void*>(types[2]));Put(entries[i],16,uint32_t{0xA5});
    Put(entries[i],24,static_cast<void*>(types[1]));Put(entries[i],40,uint32_t{0xA4});
  }
  Put(pawns[0],0x87C,reinterpret_cast<void*>(1)); // Physical player has no AI field.
  Put(pawns[1],0x87C,mapping ? nullptr:static_cast<void*>(controllers[1]));
  Put(controllers[0],0x60C,static_cast<void*>(camera));
  PublishNativePawnDownedPawns(mapping ? pawns[1]:nullptr,mapping ? pawns[0]:nullptr);
}
void Invoke(bool start) {
  const uintptr_t fn=current_pawn==0 ? (start ? native_pawn_downed_code::kPlayerStartVa:native_pawn_downed_code::kPlayerStopVa):
      (start ? native_pawn_downed_code::kAiStartVa:native_pawn_downed_code::kAiStopVa);
  DownedCall(At(fn),pawns[current_pawn]+0x10);
}
void Cycle(bool mapping,int which,bool start) {
  Reset(mapping,which);
  const auto before=std::vector<uint8_t>(pawns[0],pawns[0]+sizeof(pawns));
  Invoke(start);assert(!std::memcmp(pawns[0],before.data(),before.size()));
  const std::vector<Event> player_start={Clear,Equip,NotifyFriendly,NotifyEnemy,PlayDowned,CameraDowned,OnStart,EndUse,PriStart,TeamStart,StateStart};
  const std::vector<Event> ai_start={EndUse,PriStart,TeamStart,StateStart,NotifyFriendly,NotifyEnemy,PlayDowned,InteractedFalse,ReviveTrue};
  const std::vector<Event> player_stop={PlayRevived,CameraStart,OnStop,PriStop,TeamStop,StateStop};
  const std::vector<Event> ai_stop={PriStop,TeamStop,StateStop,PlayRevived};
  assert(events==(IsPlayerRole() ? (start ? player_start:player_stop):(start ? ai_start:ai_stop)));
  assert(Read<uint32_t>(entries[1],16)==uint32_t(!IsPlayerRole() && start ? 0xA4:0xA5));
  assert(Read<uint32_t>(entries[1],40)==uint32_t(!IsPlayerRole() && start ? 0xA5:0xA4));
}
DWORD WINAPI OtherThread(void*) {for(int i=0;i<2;++i){Reset(true,i);Invoke(true);Invoke(false);assert(events.empty() && resolutions==0);}assert(!RemoveNativePawnDowned());return 0;}
}
extern "C" void* DownedClass(void* cache,void*) {
  const uintptr_t caches[]={0x12B6F3FC,0x12B6EBDC,0x12B6EBF8};
  for(int i=0;i<3;++i)if(cache==At(caches[i]))return types[i]+0x10;
  assert(false);return nullptr;
}
extern "C" uint32_t DownedIsAImpl(void* object,void* type){
  if(object!=controllers[0] || type!=types[0]){
    std::printf("IsA object=%p expected=%p type=%p expected=%p mapped=%d pawn=%d events=%zu image=%p\n",object,controllers[0],type,types[0],mapped,current_pawn,events.size(),image);
    for(auto e:events)std::printf("%u ",unsigned(e));
    std::puts("");
  }
  assert(object==controllers[0] && type==types[0]);return 1;
}
extern "C" __attribute__((naked)) void DownedIsA(){asm volatile("pushl 4(%esp); pushl %ecx; call _DownedIsAImpl; addl $8,%esp; ret $4");}
extern "C" void* DownedPri(){return notify ? pri+0x10:reinterpret_cast<void*>(0x10);}
extern "C" void PriStartImpl(void* self,void* pawn){assert(self==pri+0x10 && pawn==pawns[current_pawn]+0x10);Record(PriStart);}
extern "C" __attribute__((naked)) void PriStartCall(){asm volatile("pushl 8(%esp); pushl 8(%esp); call _PriStartImpl; addl $8,%esp; ret $8");}
extern "C" void PriStopImpl(void* self,void* pawn){assert(self==pri+0x10 && pawn==pawns[current_pawn]+0x10);Record(PriStop);}
extern "C" __attribute__((naked)) void PriStopCall(){asm volatile("pushl %eax; pushl %ecx; call _PriStopImpl; addl $8,%esp; ret");}
extern "C" void PawnEventImpl(void* self,uint32_t event){assert(self==pawns[current_pawn]+0x10);Record(static_cast<Event>(event));}
extern "C" __attribute__((naked)) void StateStartCall(){asm volatile("pushl $10; pushl %ecx; call _PawnEventImpl; addl $8,%esp; ret");}
extern "C" __attribute__((naked)) void StateStopCall(){asm volatile("pushl $18; pushl %ecx; call _PawnEventImpl; addl $8,%esp; ret");}
extern "C" __attribute__((naked)) void TeamStartCall(){asm volatile("pushl $9; pushl 8(%esp); call _PawnEventImpl; addl $8,%esp; ret $4");}
extern "C" __attribute__((naked)) void TeamStopCall(){asm volatile("pushl $17; pushl 8(%esp); call _PawnEventImpl; addl $8,%esp; ret $4");}
extern "C" void EndUseImpl(void* self){assert(self==interactions[current_pawn]+0x10);Record(EndUse);}
extern "C" __attribute__((naked)) void EndUseCall(){asm volatile("pushl %ecx; call _EndUseImpl; addl $4,%esp; ret");}
extern "C" void ControllerEventImpl(void* self,uint32_t event){assert(self==controllers[0]+0x10 && IsPlayerRole());Record(static_cast<Event>(event));}
extern "C" __attribute__((naked)) void ClearCall(){asm volatile("pushl $0; pushl %ecx; call _ControllerEventImpl; addl $8,%esp; ret");}
extern "C" __attribute__((naked)) void EquipCall(){asm volatile("pushl $1; pushl %ecx; call _ControllerEventImpl; addl $8,%esp; ret");}
extern "C" __attribute__((naked)) void OnStartCall(){asm volatile("pushl $6; pushl %ecx; call _ControllerEventImpl; addl $8,%esp; ret");}
extern "C" __attribute__((naked)) void OnStopCall(){asm volatile("pushl $15; pushl %ecx; call _ControllerEventImpl; addl $8,%esp; ret");}
extern "C" void DialogNotifyImpl(void* self,uint32_t value,uint32_t event){assert(self==dialogs[IsPlayerRole() ? 0:1]+0x10 && value==2);Record(static_cast<Event>(event));}
extern "C" __attribute__((naked)) void FriendlyCall(){asm volatile("pushl $2; pushl 12(%esp); pushl 12(%esp); call _DialogNotifyImpl; addl $12,%esp; ret $8");}
extern "C" __attribute__((naked)) void EnemyCall(){asm volatile("pushl $3; pushl 12(%esp); pushl 12(%esp); call _DialogNotifyImpl; addl $12,%esp; ret $8");}
extern "C" void DialogPlayImpl(void* self,void* value){assert(self==dialogs[IsPlayerRole() ? 0:1]+0x10);assert(value==dialog_events[0]+0x10 || value==dialog_events[1]+0x10);Record(value==dialog_events[0]+0x10 ? PlayDowned:PlayRevived);}
extern "C" __attribute__((naked)) void DialogPlayCall(){asm volatile("pushl 4(%esp); pushl %eax; call _DialogPlayImpl; addl $8,%esp; ret $4");}
extern "C" void CameraDownedImpl(void* self){assert(self==camera+0x10 && IsPlayerRole());Record(CameraDowned);}
extern "C" __attribute__((naked)) void CameraDownedCall(){asm volatile("pushl %eax; call _CameraDownedImpl; addl $4,%esp; ret");}
extern "C" void* PawnCastImpl(void* field){assert(field==controllers[0]+0x1C8);return static_cast<uint8_t*>(Read<void*>(field,0))+0x10;}
extern "C" __attribute__((naked)) void PawnCastCall(){asm volatile("pushl %eax; call _PawnCastImpl; addl $4,%esp; ret");}
extern "C" uint32_t FormImpl(void* self){assert(self==forms[current_pawn]+0x10);return current_pawn;}
extern "C" __attribute__((naked)) void FormCall(){asm volatile("pushl %ecx; call _FormImpl; addl $4,%esp; ret");}
extern "C" void CameraStartImpl(void* self,uint32_t form){assert(self==camera+0x10 && form==uint32_t(current_pawn));Record(CameraStart);}
extern "C" __attribute__((naked)) void CameraStartCall(){asm volatile("pushl 4(%esp); pushl %ecx; call _CameraStartImpl; addl $8,%esp; ret $4");}
extern "C" void WorldBoolImpl(void* self,void* type,uint32_t value,void* subject){
  assert(self==collections[1]+0x10 && subject==reinterpret_cast<void*>(0x10));
  const int index=type==types[2]+0x10 ? 0:1;assert(type==types[2]+0x10 || type==types[1]+0x10);
  assert(value==uint32_t(index));Record(index ? ReviveTrue:InteractedFalse);
  Put(entries[1],24*index+16,(Read<uint32_t>(entries[1],24*index+16)&~1u)|value);
}
extern "C" __attribute__((naked)) void WorldBoolCall(){asm volatile("movl %esp,%edx; pushl 8(%edx); pushl 4(%edx); pushl %eax; pushl %ecx; call _WorldBoolImpl; addl $16,%esp; ret $8");}
int main(int argc,char** argv){
  image=static_cast<uint8_t*>(VirtualAlloc(nullptr,0x2400000,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));assert(image);
  const auto base=reinterpret_cast<uintptr_t>(image);const auto module=reinterpret_cast<HMODULE>(image);
  native_pawn_downed_code::Visit([&](uintptr_t va,const auto& code,std::initializer_list<size_t> r){auto bytes=native_reference_code::Relocate(code,base,r);std::memcpy(At(va),bytes.data(),bytes.size());return true;});
  struct Stub {uintptr_t va;void(*fn)();};
  const Stub stubs[]={
    {0x110031A0,reinterpret_cast<void(*)()>(&DownedClass)},{0x10901430,DownedIsA},
    {0x10B0A100,reinterpret_cast<void(*)()>(&DownedPri)},{0x10BA60F0,PriStartCall},{0x10BA6190,PriStopCall},
    {0x10B0A320,TeamStartCall},{0x10B0A490,TeamStopCall},{0x10B22A10,EquipCall},
    {0x10A8BEB0,FriendlyCall},{0x10A8BF40,EnemyCall},{0x10A8BB80,DialogPlayCall},
    {0x10BDB1E0,CameraDownedCall},{0x1094B8F0,PawnCastCall},{0x11386840,WorldBoolCall}};
  for(const auto& s:stubs)Jump(s.va,reinterpret_cast<void*>(s.fn));
  for(int i=0;i<2;++i){
    Put(pawns[i],0x10,At(0x12B20000));Put(pawns[i],0x584,static_cast<void*>(forms[i]));
    Put(pawns[i],0x98,static_cast<void*>(world));Put(controllers[i],0x98,static_cast<void*>(world));
    Put(controllers[i],0x10,At(0x12B21000));Put(controllers[i],0x300,static_cast<void*>(collections[i]));
    Put(controllers[i],0x3AC,static_cast<void*>(dialogs[i]));Put(controllers[i],0x6A4,static_cast<void*>(dialogs[i]));
    Put(collections[i],0x28,static_cast<void*>(entries[i]));Put(collections[i],0x2C,uint32_t{2});
    Put(dialogs[i],0xC0,static_cast<void*>(dialog_events[0]));Put(dialogs[i],0xC4,static_cast<void*>(dialog_events[1]));
    Put(interactions[i],0x10,At(0x12B22000));Put(forms[i],0x10,At(0x12B23000));
  }
  for(int i=0;i<3;++i){Put(types[i],0x9C,static_cast<void*>(type_defaults[i]));Put(type_defaults[i],0x28,uint32_t(i==1 ? 1:0));}
  Put(At(0x12B20000),0x31C,reinterpret_cast<void*>(&StateStartCall));Put(At(0x12B20000),0x320,reinterpret_cast<void*>(&StateStopCall));
  Put(At(0x12B21000),0x444,reinterpret_cast<void*>(&ClearCall));Put(At(0x12B21000),0x484,reinterpret_cast<void*>(&OnStartCall));Put(At(0x12B21000),0x488,reinterpret_cast<void*>(&OnStopCall));
  Put(At(0x12B22000),0x204,reinterpret_cast<void*>(&EndUseCall));Put(At(0x12B23000),0x70,reinterpret_cast<void*>(&FormCall));
  Put(camera,0x10,At(0x12B24000));Put(At(0x12B24000),0x48,reinterpret_cast<void*>(&CameraStartCall));
  if(argc>1){std::ifstream f(argv[1],std::ios::binary);std::vector<uint8_t> exe{std::istreambuf_iterator<char>(f),{}};assert(exe.size()==37667328);
    native_pawn_downed_code::Visit([&](uintptr_t va,const auto& code,std::initializer_list<size_t> r){
      assert(!std::memcmp(exe.data()+va-0x10900C00,code,sizeof(code)));
      auto expected=native_reference_code::Relocate(code,native_reference_code::kImageBase,{});
      for(size_t p=0x2148A00;p<0x2148A00+0x243ABC;){auto page=Read<uint32_t>(exe.data(),p),size=Read<uint32_t>(exe.data(),p+4);assert(size>=8);
        for(size_t q=p+8;q<p+size;q+=2){auto e=Read<uint16_t>(exe.data(),q);uintptr_t a=native_reference_code::kImageBase+page+(e&4095);
          if((e>>12)==3 && a>=va && a+4<=va+sizeof(code))Put(expected.data(),a-va,Read<uint32_t>(expected.data(),a-va)+uint32_t(base-native_reference_code::kImageBase));}p+=size;}
      assert(expected==native_reference_code::Relocate(code,base,r));return true;});}
  assert(ValidateNativePawnDowned(module));for(int i=0;i<2;++i){Cycle(false,i,true);Cycle(false,i,false);}
  assert(InstallNativePawnDowned(module,GetCurrentThreadId(),Resolve));assert(ValidateNativePawnDowned(module));
  for(int n=0;n<2000;++n)for(bool mapping:{false,true})for(int i=0;i<2;++i){Cycle(mapping,i,true);Cycle(mapping,i,false);}
  Reset(true,0);fail_state=true;Invoke(true);assert((events==std::vector<Event>{EndUse,PriStart,TeamStart,StateStart}));
  Reset(true,0);fail_state=true;Invoke(false);assert((events==std::vector<Event>{PriStop,TeamStop,StateStop}));
  Reset(true,0);fail_dialog=true;Invoke(true);assert(events.back()==InteractedFalse && Read<uint32_t>(entries[1],40)==0xA4);
  for(int i=0;i<2;++i){Reset(true,i);live=false;Invoke(true);Invoke(false);assert(events.empty());}
  HANDLE thread=CreateThread(nullptr,0,OtherThread,nullptr,0,nullptr);assert(thread);assert(WaitForSingleObject(thread,10000)==WAIT_OBJECT_0);CloseHandle(thread);
  native_pawn_downed_code::Visit([&](uintptr_t va,const auto& code,std::initializer_list<size_t>){auto* p=static_cast<uint8_t*>(At(va));for(size_t i=0;i<sizeof(code);++i){p[i]^=1;assert(!ValidateNativePawnDowned(module));p[i]^=1;}return true;});
  for(size_t n=0;n<7;++n){const auto& h=g_downed_hooks[n];auto* p=static_cast<uint8_t*>(h.trampoline);DWORD old=0,ignored=0;assert(VirtualProtect(p,h.stolen_bytes+5,PAGE_EXECUTE_READWRITE,&old));
    for(size_t i=0;i<h.stolen_bytes+5;++i){p[i]^=1;assert(!ValidateNativePawnDowned(module));assert(!RemoveNativePawnDowned());p[i]^=1;}
    assert(VirtualProtect(p,h.stolen_bytes+5,old,&ignored));}
  assert(RemoveNativePawnDowned());assert(ValidateNativePawnDowned(module));assert(!NativePawnDownedInstalled());
  for(int i=0;i<2;++i){Cycle(false,i,true);Cycle(false,i,false);}
  native_pawn_downed_code::Visit([&](uintptr_t va,const auto& code,std::initializer_list<size_t>){auto* p=static_cast<uint8_t*>(At(va));for(size_t i=0;i<sizeof(code);++i){p[i]^=1;assert(!InstallNativePawnDowned(module,GetCurrentThreadId(),Resolve));p[i]^=1;}return true;});
  assert(VirtualFree(image,0,MEM_RELEASE));std::puts("NativePawnDownedTests passed: 16,000 native client/controller transitions, role ordering/revive state, partial invalidation and relocated CALL; engine/identity fixtures, not gameplay");
}
