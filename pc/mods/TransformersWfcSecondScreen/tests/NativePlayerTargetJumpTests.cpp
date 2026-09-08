#include "NativePlayerCombat.cpp"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <vector>
using namespace wfcds;
extern "C" void RoleTestCall(void*,void*,void*);
extern "C" void RoleTestTargetAngleCall(void*,void*,void*,void*);
namespace {
alignas(16) uint8_t pawns[3][0x900]{},controllers[3][0x800]{},targeting[0x100]{},actor[0x100]{},player_class[0x100]{},output[560]{};
uint8_t* image=nullptr;bool live=true,mapping=false;int current=0;float base_angle=12.0f;unsigned resolutions=0,base_calls=0;
template<class T>T Read(const void* p,size_t o){T v{};std::memcpy(&v,static_cast<const uint8_t*>(p)+o,sizeof(v));return v;}
template<class T>void Put(void* p,size_t o,T v){std::memcpy(static_cast<uint8_t*>(p)+o,&v,sizeof(v));}
void* At(uintptr_t va){return image+(va-native_reference_code::kImageBase);}
void* Compiled(void* p){return reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(p)+0x10);}
void Jump(uintptr_t va,void* f){auto* p=static_cast<uint8_t*>(At(va));p[0]=0xE9;Put(p,1,uint32_t(reinterpret_cast<uintptr_t>(f)-reinterpret_cast<uintptr_t>(p)-5));}
bool Resolve(void* p,bool melee){assert(p==pawns[1] && !melee);++resolutions;
  asm volatile("fninit; fldz; pxor %%xmm0,%%xmm0; pxor %%xmm1,%%xmm1; pxor %%xmm2,%%xmm2; pxor %%xmm3,%%xmm3; pxor %%xmm4,%%xmm4; pxor %%xmm5,%%xmm5; pxor %%xmm6,%%xmm6; pxor %%xmm7,%%xmm7"
      :::"xmm0","xmm1","xmm2","xmm3","xmm4","xmm5","xmm6","xmm7","memory");
  return live && Read<void*>(p,0x1D4)==controllers[0] && Read<void*>(controllers[0],0x1C8)==p;
}
void Reset(bool mapped,int which){mapping=mapped;current=which;live=true;resolutions=base_calls=0;base_angle=12;
  for(int i=0;i<3;++i){auto* c=controllers[mapped && i<2 ? 1-i:i];Put(pawns[i],0x1D4,static_cast<void*>(c));Put(c,0x1C8,static_cast<void*>(pawns[i]));Put(pawns[i],0x64,uint8_t{2});}
  Put(targeting,0x48,static_cast<void*>(pawns[which]));Put(targeting,0x94,2.0f);
  PublishNativePlayerCombatPawns(mapped ? pawns[1]:nullptr,mapped ? pawns[0]:nullptr);
}
void Abi(){assert(Read<uint32_t>(output,0)==0x55667788 && Read<uint32_t>(output,4)==0x44556677 && Read<uint32_t>(output,16)==0x11223344 && Read<uint32_t>(output,12)==Read<uint32_t>(output,40));}
bool OwnerJump(){RoleTestCall(At(native_player_combat_code::kTargetJumpVa),Compiled(targeting),output);Abi();return Read<uint32_t>(output,28)!=0;}
float Angle(){RoleTestTargetAngleCall(At(native_player_combat_code::kTargetAngleVa),Compiled(targeting),output,Compiled(actor));Abi();return Read<float>(output,36);}
void Reject(){assert(!OwnerJump());for(size_t i=208;i<336;++i)assert(output[i]==255);assert(Angle()==base_angle && base_calls==1);}
DWORD WINAPI OffThread(void*){Reset(true,1);Reject();assert(!resolutions && !RemoveNativePlayerCombat());return 0;}
}
extern "C" void* TargetClass(void* cache,void*){assert(cache==At(0x12B6EDDC));return Compiled(player_class);}
extern "C" uint32_t __attribute__((fastcall)) TargetIsA(void* p,void*,void* type){assert(type==player_class);return p==pawns[0];}
extern "C" float __attribute__((fastcall)) BaseAngle(void* owner,void*,void* selected){assert(owner==Compiled(targeting) && selected==Compiled(actor));++base_calls;return base_angle;}
int main(int argc,char** argv){
  image=static_cast<uint8_t*>(VirtualAlloc(nullptr,0x2400000,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));assert(image);
  const auto base=reinterpret_cast<uintptr_t>(image);const auto module=reinterpret_cast<HMODULE>(image);
  native_player_combat_code::Visit([&](uintptr_t va,const auto& code,std::initializer_list<size_t> r){auto b=native_reference_code::Relocate(code,base,r);std::memcpy(At(va),b.data(),b.size());return true;});
  Jump(0x110031A0,reinterpret_cast<void*>(&TargetClass));Jump(0x10901430,reinterpret_cast<void*>(&TargetIsA));Jump(0x11364F60,reinterpret_cast<void*>(&BaseAngle));
  Put(At(0x1262720C),0,0.0f);Put(At(0x129D2A90),0,25.0);
  if(argc>1){std::ifstream f(argv[1],std::ios::binary);std::vector<uint8_t> exe{std::istreambuf_iterator<char>(f),{}};assert(exe.size()==37667328);
    native_player_combat_code::VisitTargetJump([&](uintptr_t va,const auto& code,std::initializer_list<size_t> r){assert(!std::memcmp(exe.data()+va-0x10900C00,code,sizeof(code)));
      auto expected=native_reference_code::Relocate(code,native_reference_code::kImageBase,{});
      for(size_t p=0x2148A00;p<0x2148A00+0x243ABC;){auto page=Read<uint32_t>(exe.data(),p),size=Read<uint32_t>(exe.data(),p+4);assert(size>=8);
        for(size_t q=p+8;q<p+size;q+=2){auto e=Read<uint16_t>(exe.data(),q);uintptr_t a=native_reference_code::kImageBase+page+(e&4095);
          if((e>>12)==3 && a>=va && a+4<=va+sizeof(code))Put(expected.data(),a-va,Read<uint32_t>(expected.data(),a-va)+uint32_t(base-native_reference_code::kImageBase));}p+=size;}
      assert(expected==native_reference_code::Relocate(code,base,r));return true;});
    assert(!std::memcmp(At(0x129D2A90),exe.data()+0x129D2A90-0x10901200,8));
  }
  assert(ValidateNativePlayerCombat(module));assert(InstallNativePlayerCombat(module,GetCurrentThreadId(),Resolve,+[](void*,void*,void*){return false;}));unsigned cases=0;
  for(unsigned repeat=0;repeat<16;++repeat)for(bool mapped:{false,true})for(uint8_t physics:{0,1,2,3})for(float factor:{-2.0f,0.0f,0.5f,2.0f})for(float angle:{-30.0f,-25.0f,-24.0f,0.0f,12.0f,24.0f,25.0f,30.0f}){
    Reset(mapped,mapped ? 1:0);Put(pawns[current],0x64,physics);Put(targeting,0x94,factor);base_angle=angle;
    const auto before=std::vector<uint8_t>(pawns[0],pawns[0]+sizeof(pawns));
    assert(OwnerJump()==(physics==2));const float expected=physics==2 && factor>0 && std::fabs(angle)<25 ? angle/factor:angle;
    assert(Angle()==expected && base_calls==1);assert(!std::memcmp(pawns[0],before.data(),before.size()));++cases;
  }
  Reset(true,1);live=false;Reject();Reset(true,0);Reject();Reset(true,2);Reject();Reset(false,1);Reject();
  Reset(true,1);Put(pawns[1],0x1D4,static_cast<void*>(controllers[2]));Reject();
  Reset(true,1);Put(targeting,0x48,static_cast<void*>(nullptr));Reject();assert(!resolutions);
  HANDLE thread=CreateThread(nullptr,0,OffThread,nullptr,0,nullptr);assert(thread);assert(WaitForSingleObject(thread,10000)==WAIT_OBJECT_0);CloseHandle(thread);
  native_player_combat_code::VisitTargetJump([&](uintptr_t va,const auto& code,std::initializer_list<size_t>){auto* p=static_cast<uint8_t*>(At(va));for(size_t i=0;i<sizeof(code);++i){p[i]^=1;assert(!ValidateNativePlayerCombat(module));p[i]^=1;}return true;});
  for(size_t i=9;i<11;++i){auto& patch=g_combat_patches[i];patch.target[0]^=1;assert(!RemoveNativePlayerCombat());patch.target[0]^=1;}
  assert(RemoveNativePlayerCombat());assert(ValidateNativePlayerCombat(module));Reset(false,0);assert(OwnerJump() && Angle()==6.0f);
  assert(VirtualFree(image,0,MEM_RELEASE));std::printf("NativePlayerTargetJumpTests passed: %u shipping jump/angle pairs, inlined role check, native threshold/division/FAbs, base targeting preserved, stale-role/ABI/FP/signature guards (engine/class/base-angle fixtures, not gameplay)\n",cases);
}
