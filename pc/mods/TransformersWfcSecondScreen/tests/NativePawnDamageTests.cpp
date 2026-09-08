// Shipping AI flash/block and shared health-on-block bodies; engine class,
// buff activation, death and planner callbacks are fixtures, not gameplay.
#include "NativePawnDamage.cpp"
#include <cassert>
#include <cstdio>
#include <fstream>
#include <vector>
using namespace wfcds;
extern "C" __attribute__((naked)) void FlashCall(void*,void*,void*,void*) {
  asm volatile("pushl %ebp; movl %esp,%ebp; pushl %esi; movl 12(%ebp),%esi; pushl 20(%ebp); pushl 16(%ebp); call *8(%ebp); popl %esi; leave; ret");
}
extern "C" __attribute__((naked)) void BlockCall(void*,void*,float,float) {
  asm volatile("pushl %ebp; movl %esp,%ebp; movl 12(%ebp),%ecx; pushl 20(%ebp); pushl 16(%ebp); call *8(%ebp); leave; ret");
}
namespace {
alignas(16) uint8_t pawn[0x900]{},controller[0x500]{},world[0x500]{},blueprint[0x100]{},
    manager[0x100]{},buff[0x100]{},classes[3][32]{},collection[0x100]{},instigator[32]{};
uint8_t* image=nullptr;
uint32_t hit[3]={0x41200000,0x41A00000,0x41F00000};
unsigned buff_reads=0,activations=0,deaths=0,replans=0,reactions=0;
bool has_buff=true;
float blocked=0;
template<class T>T Read(const void* p,size_t off){T v{};std::memcpy(&v,static_cast<const uint8_t*>(p)+off,sizeof(v));return v;}
template<class T>void Put(void* p,size_t off,T v){std::memcpy(static_cast<uint8_t*>(p)+off,&v,sizeof(v));}
void* At(uintptr_t va){return image+va-native_reference_code::kImageBase;}
void Jump(uintptr_t va,void* target){DamageJump(static_cast<uint8_t*>(At(va)),At(va),target);}
void Reset(bool mapped,bool fast_die=false,bool network=false) {
  buff_reads=activations=deaths=replans=reactions=0;blocked=0;has_buff=true;
  Put(pawn,0x87C,mapped ? static_cast<void*>(nullptr):static_cast<void*>(controller));
  Put(blueprint,0xA4,uint8_t(fast_die ? 10:0));Put(controller,0x39C,4.0f);
  Put(world,0x39C,uint8_t(network));Put(pawn,0x66,uint8_t(network ? 1:3));
  PublishNativePawnDamagePlayer(mapped ? pawn:nullptr);
}
void Flash(bool mapped,bool fast_die=false,bool network=false) {
  Reset(mapped,fast_die,network);auto before=pawn[0];
  FlashCall(At(native_pawn_damage_code::kFlashVa),pawn+0x10,instigator+0x10,hit);
  assert(deaths==unsigned(!mapped && fast_die && !network));
  assert(reactions==unsigned(!mapped && !fast_die && !network));assert(replans==reactions);
  assert(pawn[0]==before && Read<float>(controller,0x39C)==4);
}
void Block(bool mapped,bool present=true,bool network=false,float pre=12.5f,float post=4.75f) {
  Reset(mapped,false,network);has_buff=present;
  BlockCall(At(native_pawn_damage_code::kBlockVa),pawn+0x530,pre,post);
  assert(buff_reads==unsigned(!network));assert(activations==unsigned(!network && present));
  assert(blocked==((!network && present) ? pre-post:0));
  assert(Read<float>(controller,0x39C)==(mapped || network ? 4:4+pre-post));
  assert(!reactions && !replans && !deaths);
}
DWORD WINAPI OtherThread(void*){Flash(true);Block(true);assert(!RemoveNativePawnDamage());return 0;}
}
extern "C" void* DamageClass(void* cache,void*) {
  const auto va=reinterpret_cast<uintptr_t>(cache)-reinterpret_cast<uintptr_t>(image)+native_reference_code::kImageBase;
  const uintptr_t keys[]={0x12B6FF08,0x12B6EBE0,0x12B6EA0C};
  for(size_t i=0;i<3;++i)if(va==keys[i])return classes[i]+0x10;
  assert(false);return nullptr;
}
extern "C" void* DamageBuffImpl(void* self,void* type){assert(self==manager+0x38 && type==classes[2]+0x10);++buff_reads;return has_buff ? buff+0x10:reinterpret_cast<void*>(0x10);}
extern "C" __attribute__((naked)) void DamageBuff(){asm volatile("pushl 4(%esp); pushl %ecx; call _DamageBuffImpl; addl $8,%esp; ret $4");}
extern "C" void DamageActivateImpl(void* self,void* target,float amount){assert(self==buff+0x10 && target==pawn+0x10);++activations;blocked=amount;}
extern "C" __attribute__((naked)) void DamageActivate(){asm volatile("pushl 4(%esp); pushl %eax; pushl %edi; call _DamageActivateImpl; addl $12,%esp; ret $4");}
extern "C" void DamageDeathImpl(void* self,void* cause,void* type,void* location){assert(self==pawn+0x10 && cause==instigator+0x10 && type==classes[0]+0x10 && location==hit);++deaths;}
extern "C" __attribute__((naked)) void DamageDeath(){asm volatile("movl %esp,%eax; pushl 12(%eax); pushl 8(%eax); pushl 4(%eax); pushl %ecx; call _DamageDeathImpl; addl $16,%esp; ret $12");}
extern "C" void DamageReactionImpl(void* self,void* type,uint32_t value,void* subject){assert(self==collection+0x10 && type==classes[1]+0x10 && value==5 && subject==reinterpret_cast<void*>(0x10));++reactions;}
extern "C" __attribute__((naked)) void DamageReaction(){asm volatile("movl %esp,%edx; pushl 8(%edx); pushl 4(%edx); pushl %eax; pushl %ecx; call _DamageReactionImpl; addl $16,%esp; ret $8");}
extern "C" void DamageReplanImpl(void* self,uint32_t priority){assert(self==controller+0x10 && priority==1 && reactions==1);++replans;}
extern "C" __attribute__((naked)) void DamageReplan(){asm volatile("pushl 4(%esp); pushl %ecx; call _DamageReplanImpl; addl $8,%esp; ret $4");}
int main(int argc,char** argv) {
  image=static_cast<uint8_t*>(VirtualAlloc(nullptr,0x2400000,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE));assert(image);
  const auto base=reinterpret_cast<uintptr_t>(image);const auto module=reinterpret_cast<HMODULE>(image);
  native_pawn_damage_code::Visit([&](uintptr_t va,const auto& code,std::initializer_list<size_t> r){
    const auto b=native_reference_code::Relocate(code,base,r);std::memcpy(At(va),b.data(),b.size());return true;});
  Jump(0x110031A0,reinterpret_cast<void*>(&DamageClass));Jump(0x10BD6020,reinterpret_cast<void*>(&DamageActivate));
  Jump(0x11386910,reinterpret_cast<void*>(&DamageReaction));Jump(0x10BBA850,reinterpret_cast<void*>(&DamageReplan));
  Put(pawn,0x98,static_cast<void*>(world));Put(controller,0x98,static_cast<void*>(world));Put(controller,0x66,uint8_t{3});
  Put(controller,0x3D4,static_cast<void*>(blueprint));Put(controller,0x300,static_cast<void*>(collection));
  Put(pawn,0x714,static_cast<void*>(manager));Put(manager,0x38,At(0x12B20000));Put(At(0x12B20000),0x14,reinterpret_cast<void*>(&DamageBuff));
  Put(pawn,0x10,At(0x12B21000));Put(At(0x12B21000),0x28C,reinterpret_cast<void*>(&DamageDeath));
  if(argc>1){std::ifstream f(argv[1],std::ios::binary);std::vector<uint8_t> exe{std::istreambuf_iterator<char>(f),{}};assert(exe.size()==37667328);
    native_pawn_damage_code::Visit([&](uintptr_t va,const auto& code,std::initializer_list<size_t> r){
      assert(!std::memcmp(exe.data()+va-0x10900C00,code,sizeof(code)));
      auto expected=native_reference_code::Relocate(code,native_reference_code::kImageBase,{});
      for(size_t p=0x2148A00;p<0x2148A00+0x243ABC;){auto page=Read<uint32_t>(exe.data(),p),size=Read<uint32_t>(exe.data(),p+4);assert(size>=8);
        for(size_t q=p+8;q<p+size;q+=2){auto e=Read<uint16_t>(exe.data(),q);uintptr_t a=native_reference_code::kImageBase+page+(e&4095);
          if((e>>12)==3 && a>=va && a+4<=va+sizeof(code))Put(expected.data(),a-va,Read<uint32_t>(expected.data(),a-va)+uint32_t(base-native_reference_code::kImageBase));}p+=size;}
      assert(expected==native_reference_code::Relocate(code,base,r));return true;});}
  assert(ValidateNativePawnDamage(module));Flash(false);Flash(false,true);Block(false);
  assert(InstallNativePawnDamage(module,GetCurrentThreadId()));assert(ValidateNativePawnDamage(module));
  assert(!InstallNativePawnDamage(module,GetCurrentThreadId()));
  for(int i=0;i<2000;++i){
    Flash(false);Flash(false,true);Flash(true);Flash(true,true);Flash(false,false,true);Flash(true,false,true);
    Block(false);Block(true);Block(false,false);Block(true,false);Block(true,true,true);Block(false,true,true);
    Block(false,true,false,8,8);Block(true,true,false,8,8);
  }
  HANDLE t=CreateThread(nullptr,0,OtherThread,nullptr,0,nullptr);assert(t);assert(WaitForSingleObject(t,10000)==WAIT_OBJECT_0);CloseHandle(t);
  native_pawn_damage_code::Visit([&](uintptr_t va,const auto& code,std::initializer_list<size_t>){auto* p=static_cast<uint8_t*>(At(va));
    for(size_t i=0;i<sizeof(code);++i){p[i]^=1;assert(!ValidateNativePawnDamage(module));p[i]^=1;}return true;});
  for(const auto& h:g_damage_hooks){auto* p=static_cast<uint8_t*>(h.trampoline);DWORD previous=0,ignored=0;assert(VirtualProtect(p,11,PAGE_EXECUTE_READWRITE,&previous));
    for(size_t i=0;i<11;++i){p[i]^=1;assert(!ValidateNativePawnDamage(module));assert(!RemoveNativePawnDamage());p[i]^=1;}
    assert(VirtualProtect(p,11,previous,&ignored));}
  for(const auto& h:g_damage_hooks){auto* p=static_cast<uint8_t*>(h.target);p[1]^=1;assert(!RemoveNativePawnDamage());p[1]^=1;}
  assert(RemoveNativePawnDamage());assert(!NativePawnDamageInstalled());assert(ValidateNativePawnDamage(module));Flash(false);Block(false);
  native_pawn_damage_code::Visit([&](uintptr_t va,const auto& code,std::initializer_list<size_t>){auto* p=static_cast<uint8_t*>(At(va));
    for(size_t i=0;i<sizeof(code);++i){p[i]^=1;assert(!InstallNativePawnDamage(module,GetCurrentThreadId()));p[i]^=1;}return true;});
  assert(VirtualFree(image,0,MEM_RELEASE));std::puts("NativePawnDamageTests passed: 2,000 stock/logical flash/block cycles, shared buff callback preserved; native code/ABI with class/buff/planner/death fixtures, not gameplay");
}
