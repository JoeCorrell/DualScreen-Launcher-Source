// Executes the full shipping OnSetPawn body. Deep pickup/fine-aim/HUD/POI
// services and metadata are fixtures, not proof of a complete in-game swap.
#include "NativePlayerCombat.cpp"
#include <cassert>
#include <cstdio>
#include <fstream>
#include <vector>
using namespace wfcds;
extern "C" void RoleTestAssignmentCall(void*,void*,void*,void*);
namespace {
alignas(16) uint8_t pawns[3][0x900]{},controllers[3][0x800]{},world[0x500]{},
    services[6][0x100]{},vtable[0x300]{},player_class[0x100]{},output[560]{};
uint8_t* image=nullptr;bool live=true,local=true;unsigned resolved=0,count=0,local_calls=0;
unsigned order[8]{};void* receiver=nullptr;void* argument=nullptr;void* typed=nullptr;
template<class T>T Read(const void* p,size_t o){T v{};std::memcpy(&v,static_cast<const uint8_t*>(p)+o,sizeof(v));return v;}
template<class T>void Put(void* p,size_t o,T v){std::memcpy(static_cast<uint8_t*>(p)+o,&v,sizeof(v));}
void* At(uintptr_t va){return image+va-native_reference_code::kImageBase;}
void* Compiled(void* p){return reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(p)+16);}
void Jump(uintptr_t va,void* f){auto* p=static_cast<uint8_t*>(At(va));p[0]=0xE9;Put(p,1,uint32_t(reinterpret_cast<uintptr_t>(f)-reinterpret_cast<uintptr_t>(p)-5));}
void Event(unsigned n){assert(count<8);order[count++]=n;}
bool Resolve(void* p,bool melee){assert(p==pawns[1] && !melee);++resolved;
  asm volatile("fninit; fldz; pxor %%xmm0,%%xmm0; pxor %%xmm1,%%xmm1; pxor %%xmm2,%%xmm2; pxor %%xmm3,%%xmm3; pxor %%xmm4,%%xmm4; pxor %%xmm5,%%xmm5; pxor %%xmm6,%%xmm6; pxor %%xmm7,%%xmm7"
      :::"xmm0","xmm1","xmm2","xmm3","xmm4","xmm5","xmm6","xmm7","memory");
  return live && Read<void*>(p,0x1D4)==controllers[0] && Read<void*>(controllers[0],0x1C8)==p;
}
void Reset(bool mapping){live=true;local=true;resolved=count=local_calls=0;
  for(int i=0;i<3;++i){auto* c=controllers[mapping && i<2 ? 1-i:i];Put(pawns[i],0x1D4,static_cast<void*>(c));Put(c,0x1C8,static_cast<void*>(pawns[i]));
    Put(c,0x10,static_cast<void*>(vtable));Put(c,0x98,static_cast<void*>(world));Put(c,0x66,uint8_t{3});Put(c,0x614,uint32_t{0xFFFFFFFF});
    Put(c,0x674,static_cast<void*>(services[0]));Put(c,0x690,static_cast<void*>(services[1]));
    Put(c,0x608,static_cast<void*>(services[2]));Put(c,0x60C,static_cast<void*>(services[3]));
    Put(c,0x620,static_cast<void*>(services[4]));Put(c,0x67C,static_cast<void*>(services[5]));}
  Put(world,0x39C,uint8_t{0});Put(services[5],0x28,static_cast<void*>(services[0]));
  Put(services[5],0x34,static_cast<void*>(services[1]));Put(services[1],0x34,static_cast<void*>(pawns[2]));
  PublishNativePlayerCombatPawns(mapping ? pawns[1]:nullptr,mapping ? pawns[0]:nullptr);
}
void Run(void* controller,void* pawn,void* cast_pawn,bool executes){receiver=Compiled(controller);argument=Compiled(pawn);typed=Compiled(cast_pawn);
  const auto before=std::vector<uint8_t>(pawns[0],pawns[0]+sizeof(pawns));
  const auto prior=Read<uint32_t>(controller,0x614);
  RoleTestAssignmentCall(At(native_player_combat_code::kAssignmentVa),receiver,output,argument);
  assert(Read<uint32_t>(output,0)==0x55667788 && Read<void*>(output,4)==receiver && Read<uint32_t>(output,16)==0x11223344);
  assert(Read<uint32_t>(output,12)==Read<uint32_t>(output,40));
  assert(Read<uint16_t>(output,48)==0x37F && (Read<uint16_t>(output,50)&0x3800)==0x3800);
  assert(!std::memcmp(pawns[0],before.data(),before.size()));
  if(!executes){assert(!count && !local_calls && Read<uint32_t>(controller,0x614)==prior);assert(Read<void*>(services[1],0x34)==pawns[2]);return;}
  assert(local_calls==2 && count==(local ? (cast_pawn ? 6u:5u):2u));
  for(unsigned i=0;i<count;++i)assert(order[i]==i+1);
  assert(Read<uint32_t>(controller,0x614)==(prior & ~(local ? 0x88u:0x80u)));
}
DWORD WINAPI OffThread(void*){Reset(true);Run(controllers[0],pawns[1],nullptr,false);assert(!resolved && !RemoveNativePlayerCombat());return 0;}
}
extern "C" void* AssignmentClass(void* cache,void*){assert(cache==At(0x12B6EDDC));return Compiled(player_class);}
extern "C" uint32_t __attribute__((fastcall)) AssignmentIsA(void* p,void*,void* type){assert(type==player_class);return p==pawns[0];}
extern "C" uint32_t __attribute__((fastcall)) AssignmentLocal(void* c,void*){assert(c==receiver);++local_calls;return local;}
extern "C" void __attribute__((stdcall)) AssignmentPickup(void* p,void* manager,void* commander){assert(p==typed && manager==Compiled(services[1]) && commander==Compiled(local ? services[0]:nullptr));Event(1);}
extern "C" void __attribute__((stdcall)) AssignmentFine(void* p,void* manager,void* targeting,void* camera){assert(p==argument && manager==Compiled(services[4]) && targeting==Compiled(services[2]) && camera==Compiled(services[3]));Event(2);}
extern "C" void __attribute__((stdcall)) AssignmentHud(void* c,void* p){assert(c==receiver && p==typed);Event(3);}
extern "C" void __attribute__((stdcall)) AssignmentPoi(void* command,void* manager){assert(command==Compiled(services[0]) && manager==Compiled(services[5]));Event(4);}
extern "C" void AssignmentGc(void* value){assert(!value && !Read<void*>(services[1],0x34));Event(5);}
extern "C" void __attribute__((stdcall)) AssignmentKill(void* c){assert(c==receiver && typed!=Compiled(nullptr));Event(6);}
extern "C" void AssignmentPickupThunk();extern "C" void AssignmentFineThunk();extern "C" void AssignmentHudThunk();extern "C" void AssignmentPoiThunk();
asm(".text\n.globl _AssignmentPickupThunk\n_AssignmentPickupThunk:\n popl %ecx\n pushl %edx\n pushl %eax\n pushl %ecx\n jmp _AssignmentPickup@12\n"
    ".globl _AssignmentFineThunk\n_AssignmentFineThunk:\n popl %ecx\n pushl %edx\n pushl %eax\n pushl %ecx\n jmp _AssignmentFine@16\n"
    ".globl _AssignmentHudThunk\n_AssignmentHudThunk:\n popl %edx\n pushl %eax\n pushl %edx\n jmp _AssignmentHud@8\n"
    ".globl _AssignmentPoiThunk\n_AssignmentPoiThunk:\n popl %ecx\n pushl %edi\n pushl %eax\n pushl %ecx\n jmp _AssignmentPoi@8\n");
int main(int argc,char** argv){
  image=static_cast<uint8_t*>(VirtualAlloc(nullptr,0x2400000,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));assert(image);
  const auto base=reinterpret_cast<uintptr_t>(image);const auto module=reinterpret_cast<HMODULE>(image);
  native_player_combat_code::Visit([&](uintptr_t va,const auto& code,std::initializer_list<size_t> r){auto b=native_reference_code::Relocate(code,base,r);std::memcpy(At(va),b.data(),b.size());return true;});
  Jump(0x110031A0,reinterpret_cast<void*>(&AssignmentClass));Jump(0x10901430,reinterpret_cast<void*>(&AssignmentIsA));
  Jump(0x10B11C30,reinterpret_cast<void*>(&AssignmentPickupThunk));Jump(0x10A93E00,reinterpret_cast<void*>(&AssignmentFineThunk));
  Jump(0x10B21FE0,reinterpret_cast<void*>(&AssignmentHudThunk));Jump(0x10BA90D0,reinterpret_cast<void*>(&AssignmentPoiThunk));
  Jump(0x116FC820,reinterpret_cast<void*>(&AssignmentGc));Jump(0x10B1B060,reinterpret_cast<void*>(&AssignmentKill));
  Put(vtable,0x1F8,reinterpret_cast<void*>(&AssignmentLocal));
  if(argc>1){std::ifstream f(argv[1],std::ios::binary);std::vector<uint8_t> exe{std::istreambuf_iterator<char>(f),{}};assert(exe.size()==37667328);
    native_player_combat_code::VisitAssignment([&](uintptr_t va,const auto& code,std::initializer_list<size_t> r){assert(!std::memcmp(exe.data()+va-0x10900C00,code,sizeof(code)));
      auto expected=native_reference_code::Relocate(code,native_reference_code::kImageBase,{});
      for(size_t p=0x2148A00;p<0x2148A00+0x243ABC;){auto page=Read<uint32_t>(exe.data(),p),size=Read<uint32_t>(exe.data(),p+4);assert(size>=8);
        for(size_t q=p+8;q<p+size;q+=2){auto e=Read<uint16_t>(exe.data(),q);uintptr_t a=native_reference_code::kImageBase+page+(e&4095);
          if((e>>12)==3 && a>=va && a+4<=va+sizeof(code))Put(expected.data(),a-va,Read<uint32_t>(expected.data(),a-va)+uint32_t(base-native_reference_code::kImageBase));}p+=size;}
      assert(expected==native_reference_code::Relocate(code,base,r));return true;});
  }
  assert(ValidateNativePlayerCombat(module));assert(InstallNativePlayerCombat(module,GetCurrentThreadId(),Resolve,+[](void*,void*,void*){return false;}));unsigned cases=0;
  for(unsigned repeat=0;repeat<512;++repeat)for(bool mapping:{false,true})for(bool is_local:{false,true})for(uint8_t net_mode:{0,1}){
    Reset(mapping);local=is_local;Put(world,0x39C,net_mode);auto* p=pawns[mapping ? 1:0];Run(controllers[0],p,p,true);++cases;
  }
  Reset(true);live=false;Run(controllers[0],pawns[1],nullptr,false);
  Reset(true);Run(controllers[1],pawns[1],nullptr,false); // valid pawn, WRONG receiver
  Reset(true);Run(controllers[1],pawns[0],nullptr,false); // former player is now AI
  Reset(true);Put(pawns[1],0x1D4,static_cast<void*>(controllers[2]));Run(controllers[0],pawns[1],nullptr,false);
  Reset(true);Put(controllers[0],0x1C8,static_cast<void*>(nullptr));Run(controllers[0],pawns[1],nullptr,false);
  for(bool mapping:{false,true})for(uint8_t role:{0,1}){Reset(mapping);Put(world,0x39C,uint8_t{1});Put(controllers[0],0x66,role);Run(controllers[0],pawns[mapping ? 1:0],nullptr,false);assert(!resolved);}
  Reset(false);Run(controllers[0],pawns[2],nullptr,true); // stock failed cast still initializes None
  Reset(false);Run(controllers[0],nullptr,nullptr,true);
  HANDLE thread=CreateThread(nullptr,0,OffThread,nullptr,0,nullptr);assert(thread);assert(WaitForSingleObject(thread,10000)==WAIT_OBJECT_0);CloseHandle(thread);
  native_player_combat_code::VisitAssignment([&](uintptr_t va,const auto& code,std::initializer_list<size_t>){auto* p=static_cast<uint8_t*>(At(va));for(size_t i=0;i<sizeof(code);++i){p[i]^=1;assert(!ValidateNativePlayerCombat(module));p[i]^=1;}return true;});
  auto& patch=g_combat_patches[20];patch.target[0]^=1;assert(!RemoveNativePlayerCombat());patch.target[0]^=1;
  assert(RemoveNativePlayerCombat());assert(ValidateNativePlayerCombat(module));Reset(false);Run(controllers[0],pawns[0],pawns[0],true);
  assert(VirtualFree(image,0,MEM_RELEASE));std::printf("NativePlayerAssignmentTests passed: %u native assignment paths; receiver identity, pre-mutation rejection, service order, authority/local/None, state/ABI/FP/signature guards (service fixtures, not gameplay)\n",cases);
}
