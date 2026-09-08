// Shipping TnGame.Killed body and IsA decision path. Engine scoring, mission
// callbacks, class lookup, achievements and respawn callbacks are fixtures.
#include "NativeGameDeath.cpp"
#include <cassert>
#include <cstdio>
#include <fstream>
#include <vector>
using namespace wfcds;
namespace {
alignas(16) uint8_t pawns[3][0x300]{},controllers[3][0x300]{},game[0x500]{},world[0x500]{},
    helper[0x100]{},helper_table[0x100]{},classes[2][0x100]{},damage[0x100]{};
uint8_t* image=nullptr;
enum Event {Base,LogicalDeath,Respawn,Achievements};
std::vector<Event> events;
bool mapped=false,valid=true,invalidate=false,detach=false,reenter=false,reassign=false;
unsigned generation=7,captures=0,validations=0;
void* expected_pawn=nullptr;void* expected_controller=nullptr;
template<class T>T Read(const void* p,size_t o){T v{};std::memcpy(&v,static_cast<const uint8_t*>(p)+o,sizeof(v));return v;}
template<class T>void Put(void* p,size_t o,T v){std::memcpy(static_cast<uint8_t*>(p)+o,&v,sizeof(v));}
void* At(uintptr_t va){return image+(va-native_reference_code::kImageBase);}
void Jump(uintptr_t va,void* fn){DeathBranch(static_cast<uint8_t*>(At(va)),At(va),fn);}
void* Compiled(void* p){return reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(p)+0x10);}
NativeDeathIdentity Capture(void* pawn,void* c){
  ++captures;assert(mapped && (pawn==pawns[0] || pawn==pawns[1]));
  const bool player=pawn==pawns[1];void* expected=controllers[player ? 0:1];
  void* pc=Read<void*>(pawn,0x1D4);void* cp=Read<void*>(c,0x1C8);
  if(!valid || c!=expected || (pc && pc!=c) || (cp && cp!=pawn))return {};
  return {player ? NativeDeathRole::kPlayer:NativeDeathRole::kAi,generation};
}
bool Validate(void* pawn,void* c,NativeDeathIdentity id){
  ++validations;assert(pawn==pawns[0] || pawn==pawns[1]);
  void* pc=Read<void*>(pawn,0x1D4);void* cp=Read<void*>(c,0x1C8);
  return valid && id.generation==generation && c==controllers[id.role==NativeDeathRole::kPlayer ? 0:1] &&
      (!pc || pc==c) && (!cp || cp==pawn);
}
void Notify(){events.push_back(LogicalDeath);}
void Invoke(int victim,int killer=0){
  auto fn=reinterpret_cast<DeathFn>(At(native_game_death_code::kKilledVa));
  expected_pawn=victim<0 ? nullptr:pawns[victim];
  expected_controller=victim<0 ? nullptr:controllers[mapped && victim<2 ? 1-victim:victim];
  fn(Compiled(game),nullptr,killer<0 ? Compiled(nullptr):Compiled(controllers[killer]),Compiled(expected_controller),Compiled(expected_pawn),Compiled(damage));
  assert(!g_death_scope);
}
void Reset(bool mapping){
  mapped=mapping;valid=true;invalidate=detach=reenter=reassign=false;events.clear();captures=validations=0;generation=7;
  for(int i=0;i<3;++i){auto* c=controllers[mapping && i<2 ? 1-i:i];Put(pawns[i],0x1D4,static_cast<void*>(c));Put(c,0x1C8,static_cast<void*>(pawns[i]));}
  Put(game,0x98,static_cast<void*>(world));Put(game,0x66,uint8_t{3});Put(world,0x39C,uint8_t{0});
  Put(game,0x38C,static_cast<void*>(helper));
  PublishNativeGameDeathPawns(mapping ? pawns[1]:nullptr,mapping ? pawns[0]:nullptr);
}
DWORD WINAPI OffThread(void*){
  Reset(true);Invoke(1);assert((events==std::vector<Event>{Base,Achievements}));assert(!captures && !validations);
  assert(!RemoveNativeGameDeath());return 0;
}
}
extern "C" void* DeathClass(void* cache,void*){
  if(cache==At(0x12B6EDDC))return classes[0]+0x10;
  assert(cache==At(0x12B6F3FC));return classes[1]+0x10;
}
extern "C" uint32_t __attribute__((fastcall)) DeathIsA(void* object,void*,void* type){
  assert(type==classes[0] || type==classes[1]);
  return type==classes[0] ? object==pawns[0]:object==controllers[0];
}
extern "C" void __attribute__((fastcall)) DeathBase(void* self,void*,void* killer,void* c,void* pawn,void* type){
  assert(self==Compiled(game) && type==Compiled(damage));
  assert(killer==Compiled(nullptr) || killer==Compiled(controllers[0]) || killer==Compiled(controllers[1]));
  assert(c==Compiled(expected_controller) && pawn==Compiled(expected_pawn));events.push_back(Base);
  if(detach && expected_pawn){Put(expected_pawn,0x1D4,static_cast<void*>(nullptr));Put(expected_controller,0x1C8,static_cast<void*>(nullptr));}
  if(reassign && expected_controller)Put(expected_controller,0x1C8,static_cast<void*>(pawns[2]));
  if(invalidate)++generation;
  if(reenter){
    reenter=false;auto* old=g_death_scope;auto* p=expected_pawn;auto* owner=expected_controller;
    expected_pawn=pawns[2];expected_controller=controllers[2];
    reinterpret_cast<DeathFn>(At(native_game_death_code::kKilledVa))(Compiled(game),nullptr,Compiled(nullptr),Compiled(expected_controller),Compiled(expected_pawn),Compiled(damage));
    assert(g_death_scope==old);expected_pawn=p;expected_controller=owner;
  }
}
extern "C" void __attribute__((fastcall)) DeathRespawn(void* self,void*,void* c){
  assert(self==Compiled(helper) && c==Compiled(expected_controller));events.push_back(Respawn);
}
extern "C" void __attribute__((stdcall)) DeathAchievements(void* self,void* killer,void* c,void* pawn,void* type){
  assert(self==Compiled(game) && killer==Compiled(controllers[0]) && c==Compiled(expected_controller) && pawn==Compiled(expected_pawn) && type==Compiled(damage));
  events.push_back(Achievements);
}
int main(int argc,char** argv){
  image=static_cast<uint8_t*>(VirtualAlloc(nullptr,0x2400000,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));assert(image);
  const auto base=reinterpret_cast<uintptr_t>(image);const auto module=reinterpret_cast<HMODULE>(image);
  native_game_death_code::Visit([&](uintptr_t va,const auto& code,std::initializer_list<size_t> r){auto b=native_reference_code::Relocate(code,base,r);std::memcpy(At(va),b.data(),b.size());return true;});
  Jump(0x1176C600,reinterpret_cast<void*>(&DeathBase));Jump(0x10901430,reinterpret_cast<void*>(&DeathIsA));
  Jump(0x110031A0,reinterpret_cast<void*>(&DeathClass));Jump(0x10A97E10,reinterpret_cast<void*>(&DeathAchievements));
  Put(helper,0x10,static_cast<void*>(helper_table));Put(helper_table,0x44,reinterpret_cast<void*>(&DeathRespawn));
  if(argc>1){std::ifstream f(argv[1],std::ios::binary);std::vector<uint8_t> exe{std::istreambuf_iterator<char>(f),{}};assert(exe.size()==37667328);
    native_game_death_code::Visit([&](uintptr_t va,const auto& code,std::initializer_list<size_t> r){
      assert(!std::memcmp(exe.data()+va-0x10900C00,code,sizeof(code)));
      auto expected=native_reference_code::Relocate(code,native_reference_code::kImageBase,{});
      for(size_t p=0x2148A00;p<0x2148A00+0x243ABC;){auto page=Read<uint32_t>(exe.data(),p),size=Read<uint32_t>(exe.data(),p+4);assert(size>=8);
        for(size_t q=p+8;q<p+size;q+=2){auto e=Read<uint16_t>(exe.data(),q);uintptr_t a=native_reference_code::kImageBase+page+(e&4095);
          if((e>>12)==3 && a>=va && a+4<=va+sizeof(code))Put(expected.data(),a-va,Read<uint32_t>(expected.data(),a-va)+uint32_t(base-native_reference_code::kImageBase));}p+=size;}
      assert(expected==native_reference_code::Relocate(code,base,r));return true;});}
  assert(ValidateNativeGameDeath(module));Reset(false);Invoke(0);assert((events==std::vector<Event>{Base,Respawn,Achievements}));
  assert(!InstallNativeGameDeath(module,0,Capture,Validate,Notify));
  assert(!InstallNativeGameDeath(module,GetCurrentThreadId(),nullptr,Validate,Notify));
  assert(InstallNativeGameDeath(module,GetCurrentThreadId(),Capture,Validate,Notify));assert(ValidateNativeGameDeath(module));
  assert(!InstallNativeGameDeath(module,GetCurrentThreadId(),Capture,Validate,Notify));
  for(int n=0;n<2000;++n)for(bool mapping:{false,true})for(int i=0;i<3;++i){
    Reset(mapping);const auto before=std::vector<uint8_t>(pawns[0],pawns[0]+sizeof(pawns));Invoke(i);
    std::vector<Event> expected{Base};if(mapping && i==1)expected.push_back(LogicalDeath);
    if((!mapping && i==0)||(mapping && i==1))expected.push_back(Respawn);
    expected.push_back(Achievements);
    assert(events==expected);assert(!std::memcmp(pawns[0],before.data(),before.size()));
  }
  Reset(true);detach=true;Invoke(1);assert((events==std::vector<Event>{Base,LogicalDeath,Respawn,Achievements}));
  Reset(true);Put(pawns[1],0x1D4,static_cast<void*>(nullptr));Put(controllers[0],0x1C8,static_cast<void*>(nullptr));
  Invoke(1);assert((events==std::vector<Event>{Base,LogicalDeath,Respawn,Achievements}));
  Reset(true);reassign=true;Invoke(1);assert((events==std::vector<Event>{Base,Achievements}));
  Reset(true);invalidate=true;Invoke(1);assert((events==std::vector<Event>{Base,Achievements}));
  Reset(true);valid=false;Invoke(1);assert((events==std::vector<Event>{Base,Achievements}));
  Reset(true);reenter=true;Invoke(1);assert((events==std::vector<Event>{Base,Base,LogicalDeath,Respawn,Achievements}));
  Reset(true);Put(game,0x38C,static_cast<void*>(nullptr));Invoke(1);assert((events==std::vector<Event>{Base,LogicalDeath,Achievements}));
  Reset(true);Invoke(1,-1);assert((events==std::vector<Event>{Base,LogicalDeath,Respawn}));
  Reset(true);Invoke(1,1);assert((events==std::vector<Event>{Base,LogicalDeath,Respawn}));
  Reset(true);Invoke(-1,-1);assert((events==std::vector<Event>{Base}));
  Reset(true);Put(world,0x39C,uint8_t{1});Put(game,0x66,uint8_t{1});Invoke(1);assert(events.empty());
  HANDLE thread=CreateThread(nullptr,0,OffThread,nullptr,0,nullptr);assert(thread);assert(WaitForSingleObject(thread,10000)==WAIT_OBJECT_0);CloseHandle(thread);
  native_game_death_code::Visit([&](uintptr_t va,const auto& code,std::initializer_list<size_t>){auto* p=static_cast<uint8_t*>(At(va));for(size_t i=0;i<sizeof(code);++i){p[i]^=1;assert(!ValidateNativeGameDeath(module));p[i]^=1;}return true;});
  {auto* p=static_cast<uint8_t*>(g_death_hook.trampoline);DWORD old=0,ignored=0;assert(VirtualProtect(p,14,PAGE_EXECUTE_READWRITE,&old));
    for(size_t i=0;i<14;++i){p[i]^=1;assert(!ValidateNativeGameDeath(module));assert(!RemoveNativeGameDeath());p[i]^=1;}
    assert(VirtualProtect(p,14,old,&ignored));}
  assert(RemoveNativeGameDeath());assert(ValidateNativeGameDeath(module));assert(!NativeGameDeathInstalled());
  Reset(false);Invoke(0);assert((events==std::vector<Event>{Base,Respawn,Achievements}));
  native_game_death_code::Visit([&](uintptr_t va,const auto& code,std::initializer_list<size_t>){auto* p=static_cast<uint8_t*>(At(va));for(size_t i=0;i<sizeof(code);++i){p[i]^=1;assert(!InstallNativeGameDeath(module,GetCurrentThreadId(),Capture,Validate,Notify));p[i]^=1;}return true;});
  assert(VirtualFree(image,0,MEM_RELEASE));std::puts("NativeGameDeathTests passed: 12,000 shipping death branches; role-aware respawn, native ordering, nested notifications, detach/generation/authority guards and patch ownership (engine services are fixtures, not gameplay)");
}
