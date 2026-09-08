// Full shipping HUD/finder bodies. Class metadata, GFx callback and allocation
// backend are fixtures; the finder uses the shipping array set/append helpers.
#include "NativePlayerCombat.cpp"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <vector>
using namespace wfcds;
extern "C" void RoleTestOwnerCall(void*,void*,void*,void*,void*);
namespace {
alignas(16) uint8_t pawns[3][0x900]{},controllers[3][0x800]{},observer[0x100]{},movie[0x100]{},
    player_class[0x100]{},controller_class[0x100]{},output[560]{};
uint8_t* image=nullptr;bool live=true,mapped=false;int current=0;unsigned resolutions=0,notifications=0,allocations=0;
float observed=0;void* expected_array=nullptr;
template<class T>T Read(const void* p,size_t o){T v{};std::memcpy(&v,static_cast<const uint8_t*>(p)+o,sizeof(v));return v;}
template<class T>void Put(void* p,size_t o,T v){std::memcpy(static_cast<uint8_t*>(p)+o,&v,sizeof(v));}
void* At(uintptr_t va){return image+va-native_reference_code::kImageBase;}
void* Compiled(void* p){return reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(p)+0x10);}
void Jump(uintptr_t va,void* f){auto* p=static_cast<uint8_t*>(At(va));p[0]=0xE9;Put(p,1,uint32_t(reinterpret_cast<uintptr_t>(f)-reinterpret_cast<uintptr_t>(p)-5));}
bool Resolve(void* p,bool melee){assert(p==pawns[1] && !melee);++resolutions;
  asm volatile("fninit; fldz; pxor %%xmm0,%%xmm0; pxor %%xmm1,%%xmm1; pxor %%xmm2,%%xmm2; pxor %%xmm3,%%xmm3; pxor %%xmm4,%%xmm4; pxor %%xmm5,%%xmm5; pxor %%xmm6,%%xmm6; pxor %%xmm7,%%xmm7"
      :::"xmm0","xmm1","xmm2","xmm3","xmm4","xmm5","xmm6","xmm7","memory");
  return live && Read<void*>(p,0x1D4)==controllers[0] && Read<void*>(controllers[0],0x1C8)==p;
}
void Reset(bool mapping,int which){mapped=mapping;current=which;live=true;resolutions=notifications=allocations=0;
  for(int i=0;i<3;++i){auto* c=controllers[mapping && i<2 ? 1-i:i];Put(pawns[i],0x1D4,static_cast<void*>(c));Put(c,0x1C8,static_cast<void*>(pawns[i]));
    Put(pawns[i],0x544,uint32_t{0x1000});Put(pawns[i],0x35C,-25.0f);Put(pawns[i],0x79C,100.0f);}
  PublishNativePlayerCombatPawns(mapping ? pawns[1]:nullptr,mapping ? pawns[0]:nullptr);
}
void Abi(){assert(Read<uint32_t>(output,0)==0x55667788 && Read<uint32_t>(output,4)==0x44556677 &&
    Read<uint32_t>(output,16)==0x11223344 && Read<uint32_t>(output,12)==Read<uint32_t>(output,40));}
void Fp(){assert(Read<uint16_t>(output,48)==0x37F && Read<uint16_t>(output,50)==0x3800);
  for(size_t i=224;i<336;++i)assert(output[i]==255);}
float Hud(){auto* c=controllers[mapped && current<2 ? 1-current:current];
  RoleTestOwnerCall(At(native_player_combat_code::kDownedHudVa),Compiled(observer),output,Compiled(movie),Compiled(c));Abi();Fp();assert(notifications==1);return observed;}
void Finder(void* p,bool accepted,void* expected_controller){
  alignas(16) uint32_t result[5]={0x11111111,0x22222222,0x33333333,0x44444444,0x55555555};expected_array=result;const auto count=allocations;
  RoleTestOwnerCall(At(native_player_combat_code::kOwnerFinderVa),Compiled(observer),output,result,Compiled(p));Abi();Fp();
  assert(Read<void*>(output,28)==result && result[3]==0x44444444 && result[4]==0x55555555);
  if(accepted){assert(result[1]==1 && result[2]==6 && allocations==count+1);auto* items=reinterpret_cast<void**>(result[0]);
    assert(items[0]==expected_controller);for(int i=1;i<6;++i)assert(items[i]==reinterpret_cast<void*>(0xA5A5A5A5));assert(HeapFree(GetProcessHeap(),0,items));}
  else assert(!result[0] && !result[1] && !result[2] && allocations==count);
  expected_array=nullptr;
}
DWORD WINAPI OffThread(void*){Reset(true,1);assert(Hud()==1);Finder(pawns[1],false,nullptr);assert(!resolutions && !RemoveNativePlayerCombat());return 0;}
}
extern "C" void* OwnerClass(void* cache,void*){if(cache==At(0x12B6F3FC))return Compiled(controller_class);assert(cache==At(0x12B6EDDC));return Compiled(player_class);}
extern "C" uint32_t __attribute__((fastcall)) OwnerIsA(void* p,void*,void* type){if(type==controller_class)return p==controllers[0];assert(type==player_class);return p==pawns[0];}
extern "C" void __attribute__((stdcall)) OwnerNotify(void* receiver,void* target,float value){assert(receiver==Compiled(observer) && target==Compiled(movie));++notifications;observed=value;}
extern "C" void OwnerNotifyThunk();
asm(".text\n.globl _OwnerNotifyThunk\n_OwnerNotifyThunk:\n popl %edx\n pushl %eax\n pushl %edx\n jmp _OwnerNotify@12\n");
extern "C" void __attribute__((stdcall)) OwnerAllocate(void* array,int old_count){assert(array==expected_array && old_count==0 && !Read<void*>(array,0) && Read<int>(array,4)==1 && Read<int>(array,8)==6);
  auto* buffer=HeapAlloc(GetProcessHeap(),0,24);assert(buffer);std::memset(buffer,0xA5,24);Put(array,0,buffer);++allocations;}
extern "C" void OwnerAllocateThunk();
asm(".text\n.globl _OwnerAllocateThunk\n_OwnerAllocateThunk:\n popl %edx\n pushl %esi\n pushl %edx\n jmp _OwnerAllocate@8\n");
int main(int argc,char** argv){
  image=static_cast<uint8_t*>(VirtualAlloc(nullptr,0x2400000,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));assert(image);
  const auto base=reinterpret_cast<uintptr_t>(image);const auto module=reinterpret_cast<HMODULE>(image);
  native_player_combat_code::Visit([&](uintptr_t va,const auto& code,std::initializer_list<size_t> r){auto b=native_reference_code::Relocate(code,base,r);std::memcpy(At(va),b.data(),b.size());return true;});
  Jump(0x110031A0,reinterpret_cast<void*>(&OwnerClass));Jump(0x10901430,reinterpret_cast<void*>(&OwnerIsA));
  Jump(0x10AC1B80,reinterpret_cast<void*>(&OwnerNotifyThunk));Jump(0x10AF1D00,reinterpret_cast<void*>(&OwnerAllocateThunk));
  // A fresh return array must never use gap insertion or existing-slot replacement.
  Put(At(0x10AF7BA0),0,uint16_t{0x0B0F});Put(At(0x10AF4AA0),0,uint16_t{0x0B0F});Put(At(0x125848B4),0,1.0f);
  if(argc>1){std::ifstream f(argv[1],std::ios::binary);std::vector<uint8_t> exe{std::istreambuf_iterator<char>(f),{}};assert(exe.size()==37667328);
    native_player_combat_code::VisitOwnerRole([&](uintptr_t va,const auto& code,std::initializer_list<size_t> r){assert(!std::memcmp(exe.data()+va-0x10900C00,code,sizeof(code)));
      auto expected=native_reference_code::Relocate(code,native_reference_code::kImageBase,{});
      for(size_t p=0x2148A00;p<0x2148A00+0x243ABC;){auto page=Read<uint32_t>(exe.data(),p),size=Read<uint32_t>(exe.data(),p+4);assert(size>=8);
        for(size_t q=p+8;q<p+size;q+=2){auto e=Read<uint16_t>(exe.data(),q);uintptr_t a=native_reference_code::kImageBase+page+(e&4095);
          if((e>>12)==3 && a>=va && a+4<=va+sizeof(code))Put(expected.data(),a-va,Read<uint32_t>(expected.data(),a-va)+uint32_t(base-native_reference_code::kImageBase));}p+=size;}
      assert(expected==native_reference_code::Relocate(code,base,r));return true;});
  }
  assert(ValidateNativePlayerCombat(module));assert(InstallNativePlayerCombat(module,GetCurrentThreadId(),Resolve,+[](void*,void*,void*){return false;}));unsigned cases=0;
  for(unsigned repeat=0;repeat<64;++repeat)for(bool mapping:{false,true})for(bool downed:{false,true})for(float maximum:{25.0f,100.0f,200.0f})for(float fraction:{0.0f,0.25f,0.5f,0.75f,1.0f}){
    Reset(mapping,mapping ? 1:0);Put(pawns[current],0x544,uint32_t(downed ? 0x1000:0));Put(pawns[current],0x35C,-maximum*fraction);Put(pawns[current],0x79C,maximum);
    auto before=std::vector<uint8_t>(pawns[0],pawns[0]+sizeof(pawns));assert(Hud()==(downed ? 1.0f-fraction:1.0f));
    Finder(pawns[current],true,controllers[0]);assert(!std::memcmp(pawns[0],before.data(),before.size()));++cases;
  }
  Reset(true,1);live=false;assert(Hud()==1);Finder(pawns[1],false,nullptr);
  Reset(true,0);assert(Hud()==1);Finder(pawns[0],false,nullptr);
  Reset(false,1);assert(Hud()==1);Finder(pawns[1],false,nullptr);Finder(nullptr,false,nullptr);
  Reset(true,2);assert(Hud()==1);Finder(pawns[2],false,nullptr);
  Reset(true,1);Put(pawns[1],0x1D4,static_cast<void*>(controllers[2]));assert(Hud()==1);Finder(pawns[1],false,nullptr);
  Reset(true,1);Put(controllers[0],0x1C8,static_cast<void*>(nullptr));assert(Hud()==1);Finder(pawns[1],false,nullptr);
  Reset(false,0);Put(pawns[0],0x1D4,static_cast<void*>(nullptr));Finder(pawns[0],false,nullptr);
  // Stock finder really returns one null entry for a nonplayer controller.
  Reset(false,0);Put(pawns[0],0x1D4,static_cast<void*>(controllers[2]));Finder(pawns[0],true,nullptr);
  HANDLE thread=CreateThread(nullptr,0,OffThread,nullptr,0,nullptr);assert(thread);assert(WaitForSingleObject(thread,10000)==WAIT_OBJECT_0);CloseHandle(thread);
  native_player_combat_code::VisitOwnerRole([&](uintptr_t va,const auto& code,std::initializer_list<size_t>){auto* p=static_cast<uint8_t*>(At(va));for(size_t i=0;i<sizeof(code);++i){p[i]^=1;assert(!ValidateNativePlayerCombat(module));p[i]^=1;}return true;});
  for(size_t i=16;i<18;++i){auto& p=g_combat_patches[i];p.target[0]^=1;assert(!RemoveNativePlayerCombat());p.target[0]^=1;}
  assert(RemoveNativePlayerCombat());assert(ValidateNativePlayerCombat(module));Reset(false,0);assert(Hud()==0.75f);Finder(pawns[0],true,controllers[0]);
  assert(VirtualFree(image,0,MEM_RELEASE));std::printf("NativePlayerOwnerTests passed: %u native HUD/finder pairs, signed downed-health math, native controller cast/array allocation ownership, stale/offthread/ABI/FP/signature guards (metadata/GFx/heap fixtures, not gameplay)\n",cases);
}
