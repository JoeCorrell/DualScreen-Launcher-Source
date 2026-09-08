// Complete shipping fine-aim manager and pawn delegates. Weapon backends,
// camera, speed modifiers, class metadata and buff services remain fixtures.
#include "NativePlayerCombat.cpp"
#include <cassert>
#include <cstdio>
#include <fstream>
#include <vector>
using namespace wfcds;
extern "C" void RoleTestCall(void*,void*,void*);
extern "C" void RoleTestBuddyStateCall(void*,void*,void*);
namespace {
alignas(16) uint8_t pawns[3][0x900]{},controllers[3][0x800]{},manager[0x100]{},
    camera[0x100]{},targeting[0x100]{},picked[0x30]{},target[0x100]{},weapon[0x800]{},
    weapon_table[0x400]{},player_class[0x100]{},weapon_class[0x100]{},buff_class[0x100]{},
    service[0x100]{},provider_table[0x100]{},service_table[0x100]{},interface_value[0x100]{},
    interface_table[0x100]{},output[560]{};
uint8_t* image=nullptr;bool mapping=false,live=true,local=true,snap=true,overheated=false,has_buff=true,dirty_overheat=false;
int current=0;unsigned weapon_starts=0,weapon_stops=0,speed_starts=0,speed_stops=0,camera_starts=0,camera_stops=0,buffs=0,resolutions=0;
std::vector<int> events;
template<class T>T Read(const void* p,size_t o){T v{};std::memcpy(&v,static_cast<const uint8_t*>(p)+o,sizeof(v));return v;}
template<class T>void Put(void* p,size_t o,T v){std::memcpy(static_cast<uint8_t*>(p)+o,&v,sizeof(v));}
void* At(uintptr_t va){return image+(va-native_reference_code::kImageBase);}
void* Compiled(void* p){return reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(p)+0x10);}
void Jump(uintptr_t va,void* f){auto* p=static_cast<uint8_t*>(At(va));p[0]=0xE9;Put(p,1,uint32_t(reinterpret_cast<uintptr_t>(f)-reinterpret_cast<uintptr_t>(p)-5));}
bool Resolve(void* p,bool melee){assert(!melee && p==pawns[1]);++resolutions;
  asm volatile("fninit; fldz; pxor %%xmm0,%%xmm0; pxor %%xmm1,%%xmm1; pxor %%xmm2,%%xmm2; pxor %%xmm3,%%xmm3; pxor %%xmm4,%%xmm4; pxor %%xmm5,%%xmm5; pxor %%xmm6,%%xmm6; pxor %%xmm7,%%xmm7"
      :::"xmm0","xmm1","xmm2","xmm3","xmm4","xmm5","xmm6","xmm7","memory");
  return live && Read<void*>(p,0x1D4)==controllers[0] && Read<void*>(controllers[0],0x1C8)==p;
}
void ClearCounts(){weapon_starts=weapon_stops=speed_starts=speed_stops=camera_starts=camera_stops=buffs=resolutions=0;events.clear();}
void Reset(bool mapped,int which){mapping=mapped;current=which;live=local=snap=has_buff=true;overheated=dirty_overheat=false;ClearCounts();
  for(int i=0;i<3;++i){auto* c=controllers[mapped && i<2 ? 1-i:i];Put(pawns[i],0x1D4,static_cast<void*>(c));Put(c,0x1C8,static_cast<void*>(pawns[i]));Put(pawns[i],0x428,static_cast<void*>(weapon));Put(pawns[i],0x538,static_cast<void*>(provider_table));}
  Put(manager,0x28,uint32_t{0xA55A2202});Put(manager,0x34,static_cast<void*>(pawns[which]));Put(manager,0x30,static_cast<void*>(camera));Put(manager,0x2C,static_cast<void*>(targeting));
  Put(manager,0x38,0.25f);Put(manager,0x3C,0.75f);Put(manager,0x40,0.0f);Put(targeting,0x34,static_cast<void*>(picked));Put(picked,0x10,static_cast<void*>(target));
  Put(weapon,0x310,uint32_t{2});PublishNativePlayerCombatPawns(mapped ? pawns[1]:nullptr,mapped ? pawns[0]:nullptr);
}
void Call(int op){using namespace native_player_combat_code;
  const uintptr_t va[]={kFineStartVa,kFineStopVa,kSnipingVa,kFineHitVa};
  if(op<2)RoleTestBuddyStateCall(At(va[op]),Compiled(manager),output);else RoleTestCall(At(va[op]),Compiled(manager),output);
  assert(Read<uint32_t>(output,4)==0x44556677 && Read<uint32_t>(output,16)==0x11223344 && Read<uint32_t>(output,12)==Read<uint32_t>(output,40));
  assert(Read<uintptr_t>(output,0)==(op<2 ? reinterpret_cast<uintptr_t>(Compiled(manager)):0x55667788));
}
void FP(){for(size_t i=208;i<336;++i)assert(output[i]==255);assert(Read<uint16_t>(output,48)==0x37F && Read<uint16_t>(output,50)==0x3800);}
void Reject(){const auto before=std::vector<uint8_t>(manager,manager+sizeof(manager));Call(0);FP();assert(!std::memcmp(manager,before.data(),before.size()));
  Put(manager,0x28,uint32_t{0xA55A2203});Call(1);FP();assert(Read<uint32_t>(manager,0x28)==0xA55A2203);
  Call(2);FP();Call(3);FP();assert(events.empty() && !weapon_starts && !speed_starts && !camera_starts && !weapon_stops && !speed_stops && !camera_stops && !buffs);
}
DWORD WINAPI OffThread(void*){Reset(true,1);Reject();assert(!resolutions && !RemoveNativePlayerCombat());return 0;}
}
extern "C" void* FineClass(void* cache,void*){assert(cache==At(0x12B6EDDC) || cache==At(0x12B70270));return Compiled(cache==At(0x12B6EDDC) ? player_class:weapon_class);}
extern "C" uint32_t __attribute__((fastcall)) FineIsA(void* p,void*,void* type){return (type==player_class && p==pawns[0]) || (type==weapon_class && p==weapon);}
extern "C" void FineWeaponStartImpl(void* p){assert(p==Compiled(weapon));++weapon_starts;events.push_back(1);}
extern "C" __attribute__((naked)) void FineWeaponStart(){asm volatile("pushl %esi; call _FineWeaponStartImpl; addl $4,%esp; ret");}
extern "C" void __attribute__((regparm(1))) FineWeaponStop(void* p){assert(p==Compiled(weapon));++weapon_stops;events.push_back(4);}
extern "C" void __attribute__((stdcall)) FineSpeedStart(void* p,float scale,void* source){assert(p==Compiled(pawns[current]) && scale==0.75f && source==Compiled(manager));++speed_starts;events.push_back(2);}
extern "C" void __attribute__((fastcall)) FineSpeedStop(void* source,void*,void* p){assert(p==Compiled(pawns[current]) && source==Compiled(manager));++speed_stops;events.push_back(5);}
extern "C" uint32_t FineLocalImpl(void* p){assert(p==pawns[current]);return local;}
extern "C" __attribute__((naked)) void FineLocal(){asm volatile("pushl %esi; call _FineLocalImpl; addl $4,%esp; ret");}
extern "C" uint32_t __attribute__((regparm(1))) FineSnap(void* m){assert(m==Compiled(manager));return snap;}
extern "C" void __attribute__((regparm(1),stdcall)) FineCameraStart(void* c,void* t){assert(c==Compiled(camera) && t==(snap ? Compiled(Read<void*>(picked,0x10)):Compiled(nullptr)));++camera_starts;events.push_back(3);}
extern "C" void __attribute__((stdcall)) FineCameraStop(void* c){assert(c==Compiled(camera));++camera_stops;events.push_back(6);}
extern "C" uint32_t __attribute__((fastcall)) FineOverheat(void* w,void*){assert(w==Compiled(weapon));if(dirty_overheat)live=false;return overheated;}
extern "C" void* __attribute__((regparm(1))) FineWeaponField(void* field){assert(field==pawns[current]+0x428);return Compiled(Read<void*>(field,0));}
extern "C" void* __attribute__((fastcall)) FineGetService(void* p,void*,uint32_t name){assert(p==pawns[current]+0x538 && name==101);return Compiled(has_buff ? service:nullptr);}
extern "C" void* __attribute__((fastcall)) FineInterface(void* s,void*,void* name){assert(s==Compiled(service) && name==At(0x126665BC));return interface_value;}
extern "C" void FineGCImpl(void* p){assert(!p || p==service);}
extern "C" __attribute__((naked)) void FineGC(){asm volatile("pushal; pushl 36(%esp); call _FineGCImpl; addl $4,%esp; popal; ret");}
extern "C" void* __attribute__((regparm(1))) FineBuffField(void* field){auto* p=Read<void*>(field,0);assert(!p || p==service);return Compiled(p);}
extern "C" void* FineBuffClass(){return Compiled(buff_class);}
extern "C" void __attribute__((fastcall)) FineBuff(void* s,void*,void* type,uint32_t unused){assert(s==interface_value && type==Compiled(buff_class) && !unused);++buffs;events.push_back(7);}
int main(int argc,char** argv){
  image=static_cast<uint8_t*>(VirtualAlloc(nullptr,0x2400000,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE));assert(image);
  const auto base=reinterpret_cast<uintptr_t>(image);const auto module=reinterpret_cast<HMODULE>(image);
  native_player_combat_code::Visit([&](uintptr_t va,const auto& bytes,std::initializer_list<size_t> r){auto b=native_reference_code::Relocate(bytes,base,r);std::memcpy(At(va),b.data(),b.size());return true;});
  Jump(0x110031A0,reinterpret_cast<void*>(&FineClass));Jump(0x10901430,reinterpret_cast<void*>(&FineIsA));
  Jump(0x109E7670,reinterpret_cast<void*>(&FineWeaponStart));Jump(0x109E76C0,reinterpret_cast<void*>(&FineWeaponStop));
  Jump(0x10B08DA0,reinterpret_cast<void*>(&FineSpeedStart));Jump(0x10B08E50,reinterpret_cast<void*>(&FineSpeedStop));Jump(0x11437D60,reinterpret_cast<void*>(&FineLocal));
  Jump(0x10A93F50,reinterpret_cast<void*>(&FineSnap));Jump(0x10BDB6C0,reinterpret_cast<void*>(&FineCameraStart));Jump(0x10BDB730,reinterpret_cast<void*>(&FineCameraStop));
  Jump(0x10980BA0,reinterpret_cast<void*>(&FineWeaponField));Jump(0x1094B840,reinterpret_cast<void*>(&FineBuffField));Jump(0x10BD8C60,reinterpret_cast<void*>(&FineBuffClass));
  // Interface builders share the same ABI/body; redirect fixture imports to
  // the independently signature-tested, relocated native implementations.
  Jump(0x1094B800,At(native_player_combat_code::kInterfaceBuildVa));Jump(0x10922780,At(native_player_combat_code::kInterfaceCopyVa));Jump(0x116FC820,reinterpret_cast<void*>(&FineGC));
  Put(At(0x12B33A08),0,uint32_t{101});Put(weapon,0x10,static_cast<void*>(weapon_table));Put(weapon_table,0x350,reinterpret_cast<void*>(&FineOverheat));
  Put(provider_table,4,reinterpret_cast<void*>(&FineGetService));Put(service,0x10,static_cast<void*>(service_table));Put(service_table,4,reinterpret_cast<void*>(&FineInterface));
  Put(interface_value,0,static_cast<void*>(interface_table));Put(interface_table,8,reinterpret_cast<void*>(&FineBuff));
  if(argc>1){std::ifstream f(argv[1],std::ios::binary);std::vector<uint8_t> exe{std::istreambuf_iterator<char>(f),{}};assert(exe.size()==37667328);
    native_player_combat_code::VisitFineAim([&](uintptr_t va,const auto& bytes,std::initializer_list<size_t> r){assert(!std::memcmp(exe.data()+va-0x10900C00,bytes,sizeof(bytes)));
      auto expected=native_reference_code::Relocate(bytes,native_reference_code::kImageBase,{});
      for(size_t p=0x2148A00;p<0x2148A00+0x243ABC;){auto page=Read<uint32_t>(exe.data(),p),size=Read<uint32_t>(exe.data(),p+4);assert(size>=8);
        for(size_t q=p+8;q<p+size;q+=2){auto e=Read<uint16_t>(exe.data(),q);uintptr_t a=native_reference_code::kImageBase+page+(e&4095);
          if((e>>12)==3 && a>=va && a+4<=va+sizeof(bytes))Put(expected.data(),a-va,Read<uint32_t>(expected.data(),a-va)+uint32_t(base-native_reference_code::kImageBase));}p+=size;}
      assert(expected==native_reference_code::Relocate(bytes,base,r));return true;});
  }
  assert(ValidateNativePlayerCombat(module));assert(InstallNativePlayerCombat(module,GetCurrentThreadId(),Resolve,+[](void*,void*,void*){return false;}));
  unsigned cycles=0;
  for(unsigned repeat=0;repeat<32;++repeat)for(unsigned flags=0;flags<64;++flags)for(bool mapped:{false,true}){
    Reset(mapped,mapped ? 1:0);local=(flags&1)!=0;snap=(flags&2)!=0;has_buff=(flags&4)!=0;overheated=(flags&8)!=0;
    const bool armed=(flags&16)!=0,scoped=(flags&32)!=0;
    Put(pawns[current],0x428,armed ? static_cast<void*>(weapon):nullptr);Put(weapon,0x310,uint32_t(scoped ? 2:0));
    const auto pawn_before=std::vector<uint8_t>(pawns[0],pawns[0]+sizeof(pawns));
    Call(0);assert(Read<uint32_t>(manager,0x28)==0xA55A2203);assert(weapon_starts==unsigned(armed) && speed_starts==1 && camera_starts==unsigned(local));
    std::vector<int> expected;
    if(armed)expected.push_back(1);
    expected.push_back(2);
    if(local)expected.push_back(3);
    assert(events==expected);
    assert(Read<float>(manager,0x40)==(local && snap ? 0.25f:0.0f));
    const auto first=events;Call(0);assert(events==first); // Rapid repeat, original native no-op.
    Call(3);assert(buffs==unsigned(armed && scoped && has_buff));
    if(armed && scoped && has_buff)expected.push_back(7);
    assert(events==expected);
    Call(2);const bool stopped=armed && scoped && overheated;
    assert(weapon_stops==unsigned(stopped) && speed_stops==unsigned(stopped) && camera_stops==unsigned(stopped && local));
    Call(1);assert(Read<uint32_t>(manager,0x28)==0xA55A2202);
    assert(weapon_stops==unsigned(armed) && speed_stops==1 && camera_stops==unsigned(local));
    if(armed)expected.push_back(4);
    expected.push_back(5);
    if(local)expected.push_back(6);
    assert(events==expected);
    const auto last=events;Call(1);assert(events==last);assert(!std::memcmp(pawns[0],pawn_before.data(),pawn_before.size()));++cycles;
  }
  Reset(true,1);live=false;Reject();Reset(true,0);Reject();
  Reset(true,1);Put(pawns[1],0x1D4,static_cast<void*>(controllers[2]));Reject();
  Reset(true,1);Call(0);ClearCounts();overheated=dirty_overheat=true;Call(2);assert(Read<uint32_t>(manager,0x28)==0xA55A2203 && events.empty());
  Reset(true,1);Put(manager,0x34,static_cast<void*>(nullptr));Call(0);assert(events.empty());
  // Ordinary unmodified AI behavior still runs the original local-camera tail
  // when our fixture says locally controlled, but never the player weapon path.
  Reset(false,1);Call(0);assert(!weapon_starts && !speed_starts && camera_starts==1);Call(1);assert(camera_stops==1);
  HANDLE thread=CreateThread(nullptr,0,OffThread,nullptr,0,nullptr);assert(thread);assert(WaitForSingleObject(thread,10000)==WAIT_OBJECT_0);CloseHandle(thread);
  native_player_combat_code::VisitFineAim([&](uintptr_t va,const auto& code,std::initializer_list<size_t>){auto* p=static_cast<uint8_t*>(At(va));for(size_t i=0;i<sizeof(code);++i){p[i]^=1;assert(!ValidateNativePlayerCombat(module));p[i]^=1;}return true;});
  for(size_t i=5;i<9;++i){auto& patch=g_combat_patches[i];patch.target[0]^=1;assert(!RemoveNativePlayerCombat());patch.target[0]^=1;}
  assert(RemoveNativePlayerCombat());assert(ValidateNativePlayerCombat(module));Reset(false,0);Call(0);assert(weapon_starts==1 && camera_starts==1);
  assert(VirtualFree(image,0,MEM_RELEASE));std::printf("NativePlayerFineAimTests passed: %u native aim/scope/damage cycles; existing weapon delegates, speed/camera order, repeated input, stale-role flag restoration, nested overheat rejection, ABI/FP and signatures (deep engine/services are fixtures, not gameplay)\n",cycles);
}
