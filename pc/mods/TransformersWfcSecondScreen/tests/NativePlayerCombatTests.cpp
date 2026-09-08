// Full shipping fire-admission and melee-abort bodies. Service providers,
// interfaces, engine class/GC and the weapon subcalls are controlled fixtures.
#include "NativePlayerCombat.cpp"
#include <cassert>
#include <cstdio>
#include <fstream>
#include <vector>
using namespace wfcds;
extern "C" void RoleTestControllerCall(void*,void*,void*);
namespace {
alignas(16) uint8_t pawns[3][0x900]{},controllers[3][0x800]{},world[0x500]{},
    robots[3][0x100]{},vehicles[3][0x100]{},vehicle_meshes[3][0x200]{},state_frames[3][0x80]{},downed_state[0x80]{},
    targeting[0x100]{},melee_targeting[0x100]{},victim[0x200]{},transform[0x100]{},weapon[0x800]{},services[3][0x100]{},
    service_tables[3][0x100]{},interfaces[3][0x100]{},interface_tables[3][0x100]{},
    provider_table[0x100]{},weapon_table[0x400]{},player_class[0x100]{},output[560]{};
uint8_t* image=nullptr;
bool live=true,mapped=false;uint32_t variant=0,resolutions=0,aborts=0,service_calls=0,melee_starts=0,timer_calls=0;
int current=0;
template<class T>T Read(const void* p,size_t o){T v{};std::memcpy(&v,static_cast<const uint8_t*>(p)+o,sizeof(v));return v;}
template<class T>void Put(void* p,size_t o,T v){std::memcpy(static_cast<uint8_t*>(p)+o,&v,sizeof(v));}
void* At(uintptr_t va){return image+(va-native_reference_code::kImageBase);}
void* Compiled(void* p){return reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(p)+0x10);}
void Jump(uintptr_t va,void* fn){auto* p=static_cast<uint8_t*>(At(va));p[0]=0xE9;Put(p,1,uint32_t(reinterpret_cast<uintptr_t>(fn)-reinterpret_cast<uintptr_t>(p)-5));}
bool Bit(int bit){return (variant&(1u<<bit))!=0;}
bool Resolve(void* p,bool melee){++resolutions;assert(p==pawns[1]);
  asm volatile("fninit; fldz; pxor %%xmm0,%%xmm0; pxor %%xmm1,%%xmm1; pxor %%xmm2,%%xmm2; pxor %%xmm3,%%xmm3; pxor %%xmm4,%%xmm4; pxor %%xmm5,%%xmm5; pxor %%xmm6,%%xmm6; pxor %%xmm7,%%xmm7"
      :::"xmm0","xmm1","xmm2","xmm3","xmm4","xmm5","xmm6","xmm7","memory");
  return live && Read<void*>(p,0x1D4)==controllers[0] && Read<void*>(controllers[0],0x1C8)==p &&
      (!melee || (Read<void*>(p,0x580)==vehicles[1] && Read<void*>(vehicles[1],0x48)==vehicle_meshes[1]));
}
void Reset(bool mapping,int which,uint32_t flags){
  mapped=mapping;current=which;variant=flags;live=true;resolutions=aborts=service_calls=melee_starts=timer_calls=0;
  for(int i=0;i<3;++i){auto* c=controllers[mapping && i<2 ? 1-i:i];Put(pawns[i],0x1D4,static_cast<void*>(c));Put(c,0x1C8,static_cast<void*>(pawns[i]));
    Put(c,0x98,static_cast<void*>(world));Put(c,0x66,uint8_t{3});Put(c,0x614,uint32_t{0xA55A2200u|(Bit(9) ? 2u:0u)});
    Put(c,0x608,static_cast<void*>(targeting));Put(pawns[i],0x28,static_cast<void*>(state_frames[i]));
    Put(state_frames[i],0x38,Bit(16) ? static_cast<void*>(downed_state):nullptr);
    Put(pawns[i],0x544,uint32_t(Bit(11) ? 0x1000:0));Put(pawns[i],0x580,static_cast<void*>(vehicles[i]));
    Put(vehicles[i],0x48,static_cast<void*>(vehicle_meshes[i]));Put(vehicle_meshes[i],0xF0,uint32_t(Bit(13) ? 0:4));
    Put(pawns[i],0x428,Bit(6) ? static_cast<void*>(weapon):nullptr);Put(pawns[i],0x774,static_cast<void*>(transform));Put(pawns[i],0x57C,static_cast<void*>(robots[i]));}
  Put(targeting,0x34,static_cast<void*>(melee_targeting));Put(melee_targeting,0x18,Bit(15) ? static_cast<void*>(victim):nullptr);
  Put(world,0x39C,uint8_t{0});Put(transform,0x48,uint8_t(Bit(5)));
  PublishNativePlayerCombatPawns(mapping ? pawns[1]:nullptr,mapping ? pawns[0]:nullptr);
}
uint32_t Invoke(bool fire){
  const int controller=mapped && current<2 ? 1-current:current;
  RoleTestControllerCall(At(fire ? native_player_combat_code::kFireVa:native_player_combat_code::kAbortVa),Compiled(controllers[controller]),output);
  assert(Read<uint32_t>(output,0)==0x55667788 && Read<uint32_t>(output,4)==0x44556677 && Read<uint32_t>(output,16)==0x11223344);
  return Read<uint32_t>(output,28);
}
void InvokeMelee(){
  RoleTestControllerCall(At(native_player_combat_code::kMeleeVa),Compiled(controllers[mapped && current<2 ? 1-current:current]),output);
  assert(Read<uint32_t>(output,0)==0x55667788 && Read<uint32_t>(output,4)==0x44556677 && Read<uint32_t>(output,16)==0x11223344);
}
void PreservedFP(){for(size_t i=208;i<336;++i)assert(output[i]==255);assert(Read<uint16_t>(output,48)==0x37F && Read<uint16_t>(output,50)==0x3800);}
bool ExpectedFire(){return !(Bit(0)&&Bit(1)) && (Bit(5)||!Bit(2)||(!Bit(3)&&!Bit(4))) &&
    (!Bit(6)||(!Bit(7)&&Bit(8))) && Bit(9);}
DWORD WINAPI OtherThread(void*){Reset(true,1,0x7FF);assert(Invoke(true)==0);Invoke(false);assert(!resolutions && !service_calls && !aborts);assert(!RemoveNativePlayerCombat());return 0;}
}
extern "C" void* CombatClass(void* cache,void*){assert(cache==At(0x12B6EDDC));return Compiled(player_class);}
extern "C" uint32_t __attribute__((fastcall)) CombatIsA(void* p,void*,void* type){assert(type==player_class);return p==pawns[0];}
extern "C" void CombatGCImpl(void* p){assert(!p || p==services[0] || p==services[1] || p==services[2]);}
extern "C" __attribute__((naked)) void CombatGC(){asm volatile("pushal; pushl 36(%esp); call _CombatGCImpl; addl $4,%esp; popal; ret");}
extern "C" void* __attribute__((fastcall)) CombatGetService(void* self,void*,uint32_t name){
  ++service_calls;int index=-1;
  if(self==pawns[current]+0x538){assert(name==101 || name==102);index=int(name)-101;}
  else {assert(self==robots[current]+0x38 && name==103);index=2;}
  return Bit(index==0 ? 0:index==1 ? 2:10) ? Compiled(services[index]):Compiled(nullptr);
}
extern "C" void* __attribute__((fastcall)) CombatInterface(void* self,void*,void* name){
  const uintptr_t names[]={0x12648950,0x12648970,0x12648998};
  for(int i=0;i<3;++i)if(self==Compiled(services[i])){assert(name==At(names[i]) || (i==2 && name==At(0x12649AB4)));return interfaces[i];}
  assert(false);return nullptr;
}
extern "C" void* __attribute__((regparm(1))) CombatInterfaceObject(void* field){
  auto* p=Read<void*>(field,0);assert(!p || p==services[0] || p==services[1] || p==services[2]);return Compiled(p);
}
extern "C" uint32_t __attribute__((fastcall)) CombatShield(void* self,void*){assert(self==interfaces[0]);return Bit(1);}
extern "C" uint32_t __attribute__((fastcall)) CombatAnimation(void* self,void*,uint32_t type){assert(self==interfaces[1] && type<2);return Bit(3+int(type));}
extern "C" void __attribute__((fastcall)) CombatAbort(void* self,void*){assert(self==interfaces[2]);++aborts;}
extern "C" void* __attribute__((regparm(1))) CombatWeaponCast(void* field){assert(field==pawns[current]+0x428);return Compiled(Read<void*>(field,0));}
extern "C" uint32_t __attribute__((fastcall)) CombatOverheat(void* self,void*){assert(self==Compiled(weapon));return Bit(7);}
extern "C" uint32_t __attribute__((regparm(1))) CombatAttached(void* self){assert(self==Compiled(weapon));return Bit(8);}
extern "C" uint32_t CombatTimerImpl(void* controller,uint32_t name,uint32_t object){
  assert(controller==controllers[mapped && current<2 ? 1-current:current] && name==301 && object==0);++timer_calls;return Bit(12);
}
extern "C" __attribute__((naked)) void CombatTimer(){asm volatile("pushl 8(%esp); pushl 8(%esp); pushl %edx; call _CombatTimerImpl; addl $12,%esp; ret $8");}
extern "C" uint32_t CombatStompImpl(void* controller,void* target){
  assert(controller==Compiled(controllers[mapped && current<2 ? 1-current:current]) && target==(Bit(15) ? Compiled(victim):Compiled(nullptr)));return Bit(14);
}
extern "C" __attribute__((naked)) void CombatStomp(){asm volatile("pushl %eax; pushl %edi; call _CombatStompImpl; addl $8,%esp; ret");}
extern "C" void __attribute__((fastcall)) CombatMelee(void* self,void*,uint32_t type,void* target,void* callback){
  assert(self==interfaces[2] && type==uint32_t(Bit(14) ? 8:1) && target==(Bit(15) ? Compiled(victim):Compiled(nullptr)));
  auto* controller=controllers[mapped && current<2 ? 1-current:current];
  assert(Read<void*>(callback,0)==controller && Read<uint32_t>(callback,4)==302 &&
      Read<void*>(callback,8)==Compiled(controller) && Read<void*>(callback,12)==At(0x10B1CAA0));
  assert(Read<uint32_t>(controller,0x614)&0x80);++melee_starts;
}
int main(int argc,char** argv){
  image=static_cast<uint8_t*>(VirtualAlloc(nullptr,0x2400000,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));assert(image);
  const auto base=reinterpret_cast<uintptr_t>(image);const auto module=reinterpret_cast<HMODULE>(image);
  native_player_combat_code::Visit([&](uintptr_t va,const auto& code,std::initializer_list<size_t> r){auto b=native_reference_code::Relocate(code,base,r);std::memcpy(At(va),b.data(),b.size());return true;});
  Jump(0x110031A0,reinterpret_cast<void*>(&CombatClass));Jump(0x10901430,reinterpret_cast<void*>(&CombatIsA));Jump(0x116FC820,reinterpret_cast<void*>(&CombatGC));
  for(uintptr_t va:{0x10A6D9F0u,0x10A6D720u,0x10A6FAB0u})Jump(va,reinterpret_cast<void*>(&CombatInterfaceObject));
  Jump(0x10980BA0,reinterpret_cast<void*>(&CombatWeaponCast));Jump(0x109E7E90,reinterpret_cast<void*>(&CombatAttached));
  Jump(0x11482780,reinterpret_cast<void*>(&CombatTimer));Jump(0x10B24FD0,reinterpret_cast<void*>(&CombatStomp));
  Put(At(0x12B32D78),0,uint32_t{301});Put(At(0x12B31814),0,uint32_t{302});Put(At(0x12B324A4),0,uint32_t{303});Put(downed_state,4,uint32_t{303});
  Put(At(0x12B34C08),0,uint32_t{101});Put(At(0x12B33760),0,uint32_t{102});Put(At(0x12B34588),0,uint32_t{103});
  Put(provider_table,4,reinterpret_cast<void*>(&CombatGetService));
  for(int i=0;i<3;++i){Put(pawns[i],0x538,static_cast<void*>(provider_table));Put(robots[i],0x38,static_cast<void*>(provider_table));
    Put(services[i],0x10,static_cast<void*>(service_tables[i]));Put(service_tables[i],4,reinterpret_cast<void*>(&CombatInterface));Put(interfaces[i],0,static_cast<void*>(interface_tables[i]));}
  Put(interface_tables[0],8,reinterpret_cast<void*>(&CombatShield));Put(interface_tables[1],0x20,reinterpret_cast<void*>(&CombatAnimation));Put(interface_tables[2],0xC,reinterpret_cast<void*>(&CombatAbort));
  Put(interface_tables[2],8,reinterpret_cast<void*>(&CombatMelee));
  Put(weapon,0x10,static_cast<void*>(weapon_table));Put(weapon_table,0x350,reinterpret_cast<void*>(&CombatOverheat));
  if(argc>1){std::ifstream f(argv[1],std::ios::binary);std::vector<uint8_t> exe{std::istreambuf_iterator<char>(f),{}};assert(exe.size()==37667328);
    native_player_combat_code::Visit([&](uintptr_t va,const auto& code,std::initializer_list<size_t> r){assert(!std::memcmp(exe.data()+va-0x10900C00,code,sizeof(code)));
      auto expected=native_reference_code::Relocate(code,native_reference_code::kImageBase,{});
      for(size_t p=0x2148A00;p<0x2148A00+0x243ABC;){auto page=Read<uint32_t>(exe.data(),p),size=Read<uint32_t>(exe.data(),p+4);assert(size>=8);
        for(size_t q=p+8;q<p+size;q+=2){auto e=Read<uint16_t>(exe.data(),q);uintptr_t a=native_reference_code::kImageBase+page+(e&4095);
          if((e>>12)==3 && a>=va && a+4<=va+sizeof(code))Put(expected.data(),a-va,Read<uint32_t>(expected.data(),a-va)+uint32_t(base-native_reference_code::kImageBase));}p+=size;}
      assert(expected==native_reference_code::Relocate(code,base,r));return true;});}
  assert(ValidateNativePlayerCombat(module));Reset(false,0,0);assert(Invoke(true)==0);
  auto reject_target=+[](void*,void*,void*){return false;};
  assert(InstallNativePlayerCombat(module,GetCurrentThreadId(),Resolve,reject_target));assert(ValidateNativePlayerCombat(module));
  assert(!InstallNativePlayerCombat(module,GetCurrentThreadId(),Resolve,reject_target));
  for(uint32_t flags=0;flags<2048;++flags)for(bool mapping:{false,true}){
    Reset(mapping,mapping ? 1:0,flags);const auto before=std::vector<uint8_t>(pawns[0],pawns[0]+sizeof(pawns));
    assert(Invoke(true)==uint32_t(ExpectedFire()));Invoke(false);assert(aborts==uint32_t(Bit(10)));
    assert(!std::memcmp(pawns[0],before.data(),before.size()));
  }
  for(uint32_t flags=0;flags<128;++flags)for(bool mapping:{false,true}){
    // Each native gate, None victim, both attack types and RobotDowned state.
    Reset(mapping,mapping ? 1:0,flags<<10);
    const auto before=std::vector<uint8_t>(pawns[0],pawns[0]+sizeof(pawns));
    InvokeMelee();const bool requested=!Bit(11)&&!Bit(16),started=requested&&!Bit(12)&&!Bit(13)&&Bit(10);
    assert(melee_starts==uint32_t(started) && timer_calls==uint32_t(requested));
    assert(Read<uint32_t>(controllers[0],0x614)==(0xA55A2200u|((requested&&!started) ? 0x80u:0u)));
    assert(!std::memcmp(pawns[0],before.data(),before.size()));
  }
  Reset(true,1,0x400);live=false;InvokeMelee();PreservedFP();assert(!melee_starts && !timer_calls && !service_calls);
  Reset(true,1,0x400);Put(pawns[1],0x580,static_cast<void*>(nullptr));InvokeMelee();assert(!timer_calls && !melee_starts);
  Reset(true,1,0x400);Put(vehicles[1],0x48,static_cast<void*>(nullptr));InvokeMelee();assert(!timer_calls && !melee_starts);
  Reset(true,0,0x400);InvokeMelee();assert(!timer_calls && !melee_starts);
  Reset(true,1,0x7FF);live=false;assert(Invoke(true)==0);PreservedFP();Invoke(false);PreservedFP();assert(!service_calls && !aborts);
  Reset(true,0,0x7FF);assert(Invoke(true)==0);Invoke(false);assert(!service_calls && !aborts && !resolutions);
  Reset(true,2,0x7FF);assert(Invoke(true)==1);Invoke(false);assert(!service_calls && !aborts && !resolutions); // Original nonplayer semantics unchanged.
  Reset(false,1,0x7FF);assert(Invoke(true)==1);Invoke(false);assert(!service_calls && !aborts);
  Reset(true,1,0x7FF);Put(controllers[0],0x1C8,static_cast<void*>(pawns[2]));assert(Invoke(true)==1); // Ordinary third pawn, not mapped.
  Reset(true,1,0x7FF);Put(pawns[1],0x1D4,static_cast<void*>(controllers[2]));assert(Invoke(true)==0);Invoke(false);assert(!service_calls && !aborts);
  Reset(true,1,0x7FF);Put(controllers[0],0x1C8,static_cast<void*>(nullptr));assert(Invoke(true)==1);Invoke(false);assert(!service_calls && !aborts);
  Reset(true,1,0x7FF);Put(world,0x39C,uint8_t{1});Put(controllers[0],0x66,uint8_t{1});assert(Invoke(true)==0);Invoke(false);assert(!resolutions && !service_calls);
  Reset(true,1,0x7FF);Put(pawns[1],0x57C,static_cast<void*>(nullptr));Invoke(false);assert(!aborts);
  HANDLE thread=CreateThread(nullptr,0,OtherThread,nullptr,0,nullptr);assert(thread);assert(WaitForSingleObject(thread,10000)==WAIT_OBJECT_0);CloseHandle(thread);
  native_player_combat_code::Visit([&](uintptr_t va,const auto& code,std::initializer_list<size_t>){auto* p=static_cast<uint8_t*>(At(va));for(size_t i=0;i<sizeof(code);++i){p[i]^=1;assert(!ValidateNativePlayerCombat(module));p[i]^=1;}return true;});
  for(auto& patch:g_combat_patches){patch.target[0]^=1;assert(!RemoveNativePlayerCombat());patch.target[0]^=1;}
  assert(RemoveNativePlayerCombat());assert(ValidateNativePlayerCombat(module));assert(!NativePlayerCombatInstalled());
  Reset(false,0,0x600);assert(Invoke(true)==1);Invoke(false);assert(aborts==1);
  native_player_combat_code::Visit([&](uintptr_t va,const auto& code,std::initializer_list<size_t>){auto* p=static_cast<uint8_t*>(At(va));for(size_t i=0;i<sizeof(code);++i){p[i]^=1;assert(!InstallNativePlayerCombat(module,GetCurrentThreadId(),Resolve,reject_target));p[i]^=1;}return true;});
  assert(VirtualFree(image,0,MEM_RELEASE));std::puts("NativePlayerCombatTests passed: 4,096 firing/abort + 256 native melee activation combinations; shield/animation/form/weapon, downed/revive/timer gates, original delegate, stale-role/mesh rejection, SIMD/stack and signatures (services/engine are fixtures, not gameplay)");
}
