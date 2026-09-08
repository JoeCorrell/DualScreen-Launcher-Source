// Shipping fire, base-pawn cast and ExposeSelf machine code. Class/identity
// resolution and buff interface callbacks are fixtures, NOT gameplay tests.
#include "NativeWeaponExposure.cpp"
#include <cassert>
#include <cstdio>
#include <fstream>
#include <vector>
using namespace wfcds;
extern "C" void ExposureTestCall(void*,void*,void*);
extern "C" __attribute__((naked)) void PickupInvoke(void*,void*,void*,uint32_t) {
  asm volatile("pushl %ebp; movl %esp,%ebp; movl 12(%ebp),%ecx; pushl 20(%ebp); pushl 16(%ebp); call *8(%ebp); leave; ret");
}
extern "C" __attribute__((naked)) void* DropInvoke(void*,void*,void*,void*,void*) {
  asm volatile("pushl %ebp; movl %esp,%ebp; movl 12(%ebp),%ecx; pushl 24(%ebp); pushl 20(%ebp); pushl 16(%ebp); call *8(%ebp); leave; ret");
}
namespace {
alignas(16) uint8_t player[0x900]{},ai[0x900]{},enemy[0x900]{},weapon[0x900]{},world[0x500]{};
alignas(16) uint8_t pc[0x300]{},ac[0x300]{},buff[0x100]{},types[10][32]{},output[560]{};
alignas(16) uint8_t drop_pickup[0x300]{},drop_weapon[0x900]{},drop_data[0x200]{},drop_mesh[0x100]{},drop_animator[0x100]{},drop_sound[32]{};
uint32_t drop_position[3]={1,2,3},drop_velocity[3]={4,5,6},drop_rotation[3]={7,8,9};
uint32_t drop_calls=0,hidden_calls=0,mesh_calls=0,animator_calls=0;
int drop_variant=0;bool clear_drop_instigator=false;
alignas(16) uint8_t game_class[0x100]{},game_defaults[0x100]{},weapon_data[5][32]{};
uint8_t* image=nullptr;
uint32_t resolutions=0,casts=0,has_calls=0;
bool live=true,disguised=false;
bool game_class_available=true,selected_data_available=true;
uint32_t base_given_calls=0,update_calls=0;
bool expected_no_activate=false;
std::vector<int> events;
void* expected_pawn=nullptr;
template<class T>T Read(const void* p,size_t n){T v{};std::memcpy(&v,static_cast<const uint8_t*>(p)+n,sizeof(v));return v;}
template<class T>void Put(void* p,size_t n,T v){std::memcpy(static_cast<uint8_t*>(p)+n,&v,sizeof(v));}
void* At(uintptr_t va){return image+va-native_reference_code::kImageBase;}
void Jump(uintptr_t va,void* fn){JumpBytes(static_cast<uint8_t*>(At(va)),At(va),fn);}
bool Resolver(void* pawn){++resolutions;assert(pawn==player || pawn==ai);
  asm volatile("fninit; fldz; pxor %%xmm0,%%xmm0; pxor %%xmm1,%%xmm1; pxor %%xmm2,%%xmm2; pxor %%xmm3,%%xmm3; pxor %%xmm4,%%xmm4; pxor %%xmm5,%%xmm5; pxor %%xmm6,%%xmm6; pxor %%xmm7,%%xmm7"
      ::: "xmm0","xmm1","xmm2","xmm3","xmm4","xmm5","xmm6","xmm7","memory");
  return live && pawn==ai && Read<void*>(pc,0x1C8)==ai && Read<void*>(ai,0x1D4)==pc;
}
void Call(void* pawn,bool cloak,bool disguise,std::initializer_list<int> expected) {
  expected_pawn=pawn;disguised=disguise;events.clear();
  Put(weapon,0x94,pawn);if(pawn)Put(pawn,0x5BC,cloak ? 8u:0u);
  const auto prior_casts=casts,prior_has=has_calls;
  ExposureTestCall(At(native_weapon_exposure_code::kFireVa),weapon+0x10,output);
  assert(events==std::vector<int>(expected));
  assert(Read<uint32_t>(output,0)==0x55667788 && Read<uint32_t>(output,4)==0x44556677 &&
      Read<uint32_t>(output,16)==0x11223344 && Read<void*>(output,24)==weapon+0x10);
  // Native calls and fixture STL callbacks may themselves clobber FP state.
  // Check the hook's exact preservation when no downstream callback ran.
  if(casts==prior_casts && has_calls==prior_has && events.empty()) {
    for(size_t i=48+160;i<48+160+128;++i)assert(output[i]==0xff);
    assert(Read<uint16_t>(output,48)==0x37f && Read<uint16_t>(output,50)==0x3800);
    assert(Read<uint64_t>(output,80)==0x8000000000000000ull && Read<uint16_t>(output,88)==0x3fff);
  }
}
void GivenTo(void* pawn,uint8_t desired,uint8_t expected,bool no_activate=false) {
  auto* desired_code=static_cast<uint8_t*>(At(0x12B00000));
  desired_code[0]=0xB8;Put(desired_code,1,uint32_t{desired});desired_code[5]=0xC3;
  expected_pawn=pawn;expected_no_activate=no_activate;base_given_calls=update_calls=0;
  PickupInvoke(At(native_weapon_exposure_code::kGivenToVa),
      weapon+0x10,pawn ? static_cast<uint8_t*>(pawn)+0x10:reinterpret_cast<void*>(0x10),no_activate);
  assert(base_given_calls==1 && update_calls==1); // No second correction/reset.
  assert(Read<uint8_t>(weapon,0x329)==expected);
  assert(Read<void*>(weapon,0x240)==weapon_data[expected]);
}
void Drop(void* pawn,bool preserve,int clip=17,int reserve=71,int amount=65,int maximum_clip=30,int maximum_reserve=90) {
  Put(weapon,0x94,pawn);Put(weapon,0x2A0,clip);Put(weapon,0x2A4,reserve);
  Put(drop_weapon,0x2A0,-91);Put(drop_weapon,0x2A4,-92);Put(drop_weapon,0x364,1234);
  Put(drop_weapon,0x244,static_cast<void*>(drop_data));
  Put(drop_data,0xD4,amount);Put(drop_data,0x98,maximum_clip);Put(drop_data,0x94,maximum_reserve);
  Put(drop_pickup,0x1C8,drop_variant==2 ? static_cast<void*>(nullptr):static_cast<void*>(drop_weapon));
  Put(drop_pickup,0x1DC,static_cast<void*>(nullptr));
  drop_calls=hidden_calls=mesh_calls=animator_calls=0;
  void* result=DropInvoke(At(native_weapon_drop_code::kDropVa),weapon+0x10,drop_position,drop_velocity,drop_rotation);
  const bool suppressed=Read<uint8_t>(world,0x39C)!=0 && Read<uint8_t>(weapon,0x66)<=1;
  assert(drop_calls==uint32_t(!suppressed) && mesh_calls==uint32_t(!suppressed) && animator_calls==uint32_t(!suppressed));
  assert(result==((suppressed || drop_variant==1) ? reinterpret_cast<void*>(0x10):drop_pickup+0x10));
  assert(hidden_calls==uint32_t(!suppressed && drop_variant==0));
  assert(Read<int>(weapon,0x2A0)==clip && Read<int>(weapon,0x2A4)==reserve);
  if(!suppressed && drop_variant==0) {
    assert(Read<void*>(drop_pickup,0x1DC)==drop_sound && Read<int>(drop_weapon,0x364)==0);
    const int c=preserve ? clip:std::min(amount,maximum_clip);
    const int r=preserve ? reserve:std::max(0,std::min(amount-c,maximum_reserve));
    assert(Read<int>(drop_weapon,0x2A0)==c && Read<int>(drop_weapon,0x2A4)==r);
  } else { assert(Read<int>(drop_weapon,0x2A0)==-91 && Read<int>(drop_weapon,0x2A4)==-92); }
}
DWORD WINAPI OtherThread(void*){const auto count=resolutions;Call(ai,true,true,{});Call(player,true,true,{});
  GivenTo(ai,3,2);GivenTo(player,3,2);
  Drop(ai,false);Drop(player,false);
  assert(resolutions==count);assert(!RemoveNativeWeaponExposure());return 0;}
}
extern "C" void* ExposureClass(void* cache,void*) {
  const auto va=reinterpret_cast<uintptr_t>(cache)-reinterpret_cast<uintptr_t>(image)+native_reference_code::kImageBase;
  const uintptr_t keys[]={0x12B6EDDC,0x12B6F4A4,0x12B6EAC4,0x12B6EAA0,0x12B6EA64,0x12B6EA60,
      0x12B6FCC0,0x12B70270,0x12B7025C,0x12B7020C};
  for(size_t i=0;i<10;++i)if(va==keys[i])return types[i]+0x10;
  assert(false);return nullptr;
}
extern "C" int ExposureCastImpl(void* pawn,void* type){++casts;
  if(type==types[0])return pawn==player;
  if(type==types[6])return pawn==drop_pickup;
  if(type==types[7])return pawn==drop_weapon;
  if(type==types[8])return pawn==drop_data;
  if(type==types[9])return pawn==drop_mesh;
  assert(type==types[1]);return pawn==player || pawn==ai || pawn==enemy;
}
extern "C" __attribute__((naked)) void ExposureCast(){asm volatile("pushl 4(%esp); pushl %ecx; call _ExposureCastImpl; addl $8,%esp; ret $4");}
extern "C" void ExposureRemoveImpl(void* self,void* type) {
  assert(self==buff+0x38);
  if(type==types[2]+0x10)events.push_back(2);
  else {assert(type==types[4]+0x10);assert(disguised);events.push_back(4);disguised=false;}
}
extern "C" __attribute__((naked)) void ExposureRemove(){asm volatile("pushl 4(%esp); pushl %ecx; call _ExposureRemoveImpl; addl $8,%esp; ret $4");}
extern "C" int ExposureHasImpl(void* self,void* type){++has_calls;assert(self==buff+0x38 && type==types[4]+0x10);return disguised;}
extern "C" __attribute__((naked)) void ExposureHas(){asm volatile("pushl 4(%esp); pushl %ecx; call _ExposureHasImpl; addl $8,%esp; ret $4");}
extern "C" void ExposureAddImpl(void* self,void* type,uint32_t time,void* pawn) {
  assert(self==buff+0x38 && time==0 && pawn==static_cast<uint8_t*>(expected_pawn)+0x10);
  if(type==types[3]+0x10)events.push_back(3);else {assert(type==types[5]+0x10);events.push_back(5);}
}
extern "C" __attribute__((naked)) void ExposureAdd(){asm volatile("movl %esp,%eax; pushl 12(%eax); pushl 8(%eax); pushl 4(%eax); pushl %ecx; call _ExposureAddImpl; addl $16,%esp; ret $12");}
extern "C" void PickupBaseImpl(void* self,void* pawn,uint32_t no_activate) {
  ++base_given_calls;assert(self==weapon+0x10 && no_activate==expected_no_activate);
  assert(pawn==(expected_pawn ? static_cast<uint8_t*>(expected_pawn)+0x10:reinterpret_cast<void*>(0x10)));
  Put(weapon,0x94,expected_pawn);
}
extern "C" __attribute__((naked)) void PickupBase(){asm volatile("movl %esp,%eax; pushl 8(%eax); pushl 4(%eax); pushl %ecx; call _PickupBaseImpl; addl $12,%esp; ret $8");}
extern "C" void* PickupGameClassImpl(void* self) {
  assert(self==world+0x10);return game_class_available ? game_class+0x10:reinterpret_cast<void*>(0x10);
}
extern "C" __attribute__((naked)) void PickupGameClass(){asm volatile("pushl %ecx; call _PickupGameClassImpl; addl $4,%esp; ret");}
extern "C" __attribute__((naked)) void PickupCastGame(){asm volatile("movl %esi,%eax; ret");}
extern "C" void* PickupDataImpl(uint32_t type) {
  assert(type>=1 && type<=4);
  return (type>2 && !selected_data_available) ? reinterpret_cast<void*>(0x10):weapon_data[type]+0x10;
}
extern "C" __attribute__((naked)) void PickupData(){asm volatile("pushl %ecx; pushl %edx; pushl 12(%esp); call _PickupDataImpl; addl $4,%esp; popl %edx; popl %ecx; ret $4");}
extern "C" void PickupUpdateImpl(void* self,void* data) {
  assert(self==weapon+0x10 && data!=reinterpret_cast<void*>(0x10));++update_calls;
  Put(weapon,0x240,static_cast<void*>(static_cast<uint8_t*>(data)-0x10));
}
extern "C" __attribute__((naked)) void PickupUpdate(){asm volatile("pushl %eax; pushl %ecx; call _PickupUpdateImpl; addl $8,%esp; ret");}
extern "C" void* DropBaseImpl(void* self,void* p,void* v,void* r) {
  ++drop_calls;assert(self==weapon+0x10 && p==drop_position && v==drop_velocity && r==drop_rotation);
  assert(mesh_calls==1 && animator_calls==1);
  if(clear_drop_instigator)Put(weapon,0x94,static_cast<void*>(nullptr));
  return drop_variant==1 ? reinterpret_cast<void*>(0x10):drop_pickup+0x10;
}
extern "C" __attribute__((naked)) void DropBase(){asm volatile("movl %esp,%eax; pushl 12(%eax); pushl 8(%eax); pushl 4(%eax); pushl %ecx; call _DropBaseImpl; addl $16,%esp; ret $12");}
extern "C" void DropHiddenImpl(void* self,uint32_t hide){assert(self==drop_weapon+0x10 && hide==1);++hidden_calls;}
extern "C" __attribute__((naked)) void DropHidden(){asm volatile("pushl 4(%esp); pushl %ecx; call _DropHiddenImpl; addl $8,%esp; ret $4");}
extern "C" void DropMeshImpl(void* self){assert(self==drop_mesh+0x10);++mesh_calls;}
extern "C" __attribute__((naked)) void DropMesh(){asm volatile("pushl %ecx; call _DropMeshImpl; addl $4,%esp; ret");}
extern "C" void DropAnimatorImpl(void* self,uint32_t blend,uint32_t time){assert(self==drop_animator+0x4C && blend==0 && time==0x3F800000);++animator_calls;}
extern "C" __attribute__((naked)) void DropAnimator(){asm volatile("movl %esp,%eax; pushl 8(%eax); pushl 4(%eax); pushl %esi; call _DropAnimatorImpl; addl $12,%esp; ret $8");}
int main(int argc,char** argv) {
  image=static_cast<uint8_t*>(VirtualAlloc(nullptr,0x2400000,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE));assert(image);
  const auto base=reinterpret_cast<uintptr_t>(image);const auto module=reinterpret_cast<HMODULE>(image);
  native_weapon_exposure_code::Visit([&](uintptr_t va,const auto& code,std::initializer_list<size_t> relocs){
    const auto bytes=native_reference_code::Relocate(code,base,relocs);std::memcpy(At(va),bytes.data(),bytes.size());return true;});
  Jump(0x110031A0,reinterpret_cast<void*>(&ExposureClass));Jump(0x10901430,reinterpret_cast<void*>(&ExposureCast));
  Jump(0x11369710,reinterpret_cast<void*>(&PickupBase));Jump(0x117A3E30,reinterpret_cast<void*>(&PickupGameClass));
  Jump(0x10A6F1F0,reinterpret_cast<void*>(&PickupCastGame));Jump(0x109E64A0,reinterpret_cast<void*>(&PickupData));
  Jump(0x117A4430,reinterpret_cast<void*>(&PickupUpdate));
  Jump(0x117B2FB0,reinterpret_cast<void*>(&DropBase));Jump(0x109F21E0,reinterpret_cast<void*>(&DropAnimator));
  Put(drop_mesh,0x10,At(0x12B02000));Put(At(0x12B02000),0x50,reinterpret_cast<void*>(&DropMesh));
  Put(drop_weapon,0x10,At(0x12B03000));Put(At(0x12B03000),0x6C,reinterpret_cast<void*>(&DropHidden));
  Put(At(0x12B04000),0,static_cast<void*>(drop_mesh));Put(weapon,0x290,At(0x12B04000));Put(weapon,0x294,1);
  Put(weapon,0x30C,static_cast<void*>(drop_animator));Put(weapon,0x2FC,static_cast<void*>(drop_sound));
  Put(drop_sound,8,0x800000u);Put(drop_weapon,0x66,uint8_t{3});Put(drop_pickup,0x66,uint8_t{3});
  Put(game_class,0x9C,static_cast<void*>(game_defaults));Put(game_defaults,0x10,At(0x12B01000));
  Put(At(0x12B01000),0x350,At(0x12B00000));
  if(argc>1){std::ifstream stream(argv[1],std::ios::binary);std::vector<uint8_t> exe{std::istreambuf_iterator<char>(stream),{}};assert(exe.size()==37667328);
    native_weapon_exposure_code::Visit([&](uintptr_t va,const auto& code,std::initializer_list<size_t> relocs){
      assert(!std::memcmp(exe.data()+va-0x10900C00,code,sizeof(code)));
      auto expected=native_reference_code::Relocate(code,native_reference_code::kImageBase,{});
      for(size_t p=0x2148A00;p<0x2148A00+0x243ABC;){const auto page=Read<uint32_t>(exe.data(),p),size=Read<uint32_t>(exe.data(),p+4);assert(size>=8);
        for(size_t q=p+8;q<p+size;q+=2){const auto entry=Read<uint16_t>(exe.data(),q);const uintptr_t address=native_reference_code::kImageBase+page+(entry&4095);
          if((entry>>12)==3 && address>=va && address+4<=va+sizeof(code))Put(expected.data(),address-va,Read<uint32_t>(expected.data(),address-va)+static_cast<uint32_t>(base-native_reference_code::kImageBase));}p+=size;
      }assert(expected==native_reference_code::Relocate(code,base,relocs));return true;});
  }
  void* vtable[7]={nullptr,reinterpret_cast<void*>(&ExposureAdd),nullptr,reinterpret_cast<void*>(&ExposureRemove),nullptr,nullptr,reinterpret_cast<void*>(&ExposureHas)};
  Put(buff,0x38,static_cast<void*>(vtable));
  for(auto* pawn:{player,ai,enemy}){Put(pawn,0x714,static_cast<void*>(buff));Put(pawn,0x66,uint8_t{3});}
  Put(weapon,0x66,uint8_t{3});Put(pc,0x1C8,static_cast<void*>(ai));Put(ai,0x1D4,static_cast<void*>(pc));
  Put(ac,0x1C8,static_cast<void*>(player));Put(player,0x1D4,static_cast<void*>(ac));
  assert(ValidateNativeWeaponExposureCode(module));
  Call(player,true,true,{2,3,4,5});Call(ai,true,true,{});Call(nullptr,false,false,{});
  assert(InstallNativeWeaponExposure(module,GetCurrentThreadId(),&Resolver));assert(ValidateNativeWeaponExposureCode(module));
  assert(!InstallNativeWeaponExposure(module,GetCurrentThreadId(),&Resolver));
  for(int i=0;i<2000;++i) {
    PublishNativeWeaponExposurePawns(nullptr,nullptr);const auto before=resolutions;
    Call(player,true,true,{2,3,4,5});Call(ai,true,true,{});Call(enemy,true,true,{});Call(nullptr,false,false,{});
    assert(resolutions==before);
    PublishNativeWeaponExposurePawns(ai,player);
    const auto stock_casts=casts;Call(player,true,true,{});assert(casts==stock_casts);
    Call(ai,true,true,{2,3,4,5});Call(ai,false,true,{4,5});Call(ai,true,false,{2,3});Call(ai,false,false,{});
    const auto count=resolutions;Call(enemy,true,true,{});Call(nullptr,false,false,{});assert(resolutions==count);
    live=false;Call(ai,true,true,{});Call(player,true,true,{});live=true;
    Put(ai,0x1D4,static_cast<void*>(ac));Call(ai,true,true,{});Put(ai,0x1D4,static_cast<void*>(pc));
    // Native ExposeSelf retains authority restrictions even for mapped pawns.
    Put(ai,0x66,uint8_t{2});Call(ai,true,true,{});Put(ai,0x66,uint8_t{3});
    // Native fire early-out runs BEFORE any custom mapping work.
    Put(weapon,0x98,static_cast<void*>(world));Put(world,0x39C,uint8_t{1});Put(weapon,0x66,uint8_t{1});
    const auto suppressed=resolutions;Call(ai,true,true,{});assert(resolutions==suppressed);
    Put(weapon,0x66,uint8_t{3});Put(weapon,0x98,static_cast<void*>(nullptr));
  }
  Put(weapon,0x98,static_cast<void*>(world));Put(world,0x39C,uint8_t{0});
  for(int i=0;i<2000;++i) {
    PublishNativeWeaponExposurePawns(nullptr,nullptr);const auto count=resolutions;
    GivenTo(player,3,3);GivenTo(ai,3,2);GivenTo(enemy,3,2);assert(resolutions==count);
    PublishNativeWeaponExposurePawns(ai,player);
    GivenTo(ai,3,3);GivenTo(ai,4,4,true);GivenTo(player,3,2);
    selected_data_available=false;GivenTo(ai,3,1);selected_data_available=true;
    game_class_available=false;GivenTo(ai,3,1);game_class_available=true;
    live=false;GivenTo(ai,3,2);GivenTo(player,3,2);live=true;
    Put(ai,0x1D4,static_cast<void*>(ac));GivenTo(ai,3,2);Put(ai,0x1D4,static_cast<void*>(pc));
  }
  for(int i=0;i<2000;++i) {
    PublishNativeWeaponExposurePawns(nullptr,nullptr);const auto count=resolutions;
    Drop(player,true);Drop(ai,false);Drop(enemy,false);Drop(nullptr,false);assert(resolutions==count);
    PublishNativeWeaponExposurePawns(ai,player);
    Drop(ai,true,i%29,10+i%70);Drop(player,false);Drop(enemy,false);
    clear_drop_instigator=true;Drop(ai,true);Drop(player,false);clear_drop_instigator=false;
    live=false;Drop(ai,false);Drop(player,false);live=true;
    Put(ai,0x1D4,static_cast<void*>(ac));Drop(ai,false);Put(ai,0x1D4,static_cast<void*>(pc));
    drop_variant=1;Drop(ai,true);drop_variant=2;Drop(ai,true);drop_variant=0;
    Drop(player,false,17,71,7,30,90);Drop(player,false,17,71,1000,30,90);
    Put(world,0x39C,uint8_t{1});Put(weapon,0x66,uint8_t{1});const auto ignored=resolutions;Drop(ai,false);assert(resolutions==ignored);
    Put(world,0x39C,uint8_t{0});Put(weapon,0x66,uint8_t{3});
  }
  HANDLE thread=CreateThread(nullptr,0,OtherThread,nullptr,0,nullptr);assert(thread);assert(WaitForSingleObject(thread,10000)==WAIT_OBJECT_0);CloseHandle(thread);
  native_weapon_exposure_code::Visit([&](uintptr_t va,const auto& code,std::initializer_list<size_t>){auto* p=static_cast<uint8_t*>(At(va));
    for(size_t i=0;i<sizeof(code);++i){p[i]^=1;assert(!ValidateNativeWeaponExposureCode(module));p[i]^=1;}return true;});
  auto* trampoline=static_cast<uint8_t*>(g_exposure_hook.trampoline);DWORD protect=0;
  assert(VirtualProtect(trampoline,11,PAGE_EXECUTE_READWRITE,&protect));
  for(size_t i=0;i<11;++i){trampoline[i]^=1;assert(!ValidateNativeWeaponExposureCode(module));assert(!RemoveNativeWeaponExposure());trampoline[i]^=1;}
  DWORD ignored=0;assert(VirtualProtect(trampoline,11,protect,&ignored));
  auto* target=static_cast<uint8_t*>(g_exposure_hook.target);target[1]^=1;assert(!RemoveNativeWeaponExposure());target[1]^=1;
  g_pickup_site[1]^=1;assert(!RemoveNativeWeaponExposure());assert(NativeWeaponExposureInstalled());g_pickup_site[1]^=1;
  g_drop_site[1]^=1;assert(!RemoveNativeWeaponExposure());assert(NativeWeaponExposureInstalled());g_drop_site[1]^=1;
  assert(RemoveNativeWeaponExposure());assert(!NativeWeaponExposureInstalled());assert(ValidateNativeWeaponExposureCode(module));
  Call(player,true,true,{2,3,4,5});Call(ai,true,true,{});
  GivenTo(player,3,3);GivenTo(ai,3,2);
  Drop(player,true);Drop(ai,false);
  native_weapon_exposure_code::Visit([&](uintptr_t va,const auto& code,std::initializer_list<size_t>){auto* p=static_cast<uint8_t*>(At(va));
    for(size_t i=0;i<sizeof(code);++i){p[i]^=1;assert(!InstallNativeWeaponExposure(module,GetCurrentThreadId(),&Resolver));p[i]^=1;}return true;});
  assert(has_calls>0);assert(VirtualFree(image,0,MEM_RELEASE));
  std::puts("NativeWeaponExposureTests passed (2,000 exposure + 2,000 pickup + 2,000 drop role cycles; native branches/GC/ammo, fixture mapping/buffs/inventory/mesh/animation, not gameplay)");
}
