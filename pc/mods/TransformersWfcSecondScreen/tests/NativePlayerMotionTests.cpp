// Execute complete shipping controller motion/record bodies. The physical
// class lookup, game input backend and virtual form/death tests are fixtures.
#include "NativePlayerCombat.cpp"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <vector>
using namespace wfcds;
extern "C" void RoleTestBuddyStateCall(void*,void*,void*);
extern "C" void RoleTestVehicleStickCall(void*,void*,void*,float,float*,float*);
extern "C" void RoleTestCameraSetCall(void*,void*,void*,void*);
extern "C" void RoleTestCameraFlyingCall(void*,void*,void*,float,void*);
namespace {
alignas(16) uint8_t pawn[3][0x900]{},controller[3][0x800]{},world[0x500]{},
    vehicle[0x100]{},handler[0x100]{},input[0x100]{},actor_base[0x100]{},
    pawn_table[0x400]{},controller_table[0x400]{},handler_table[0x100]{},
    player_class[0x100]{},controller_class[0x100]{},camera_behavior[0x200]{},output[560]{};
size_t camera_field=0;void* expected_camera_pawn=nullptr;unsigned camera_stores=0;
uint8_t* image=nullptr;bool live=true,mapping=false,mouse=true,dead=false,grounded=true;
uint8_t form=1;int current=0;unsigned resolutions=0,records=0,dead_calls=0,mouse_calls=0;
template<class T>T Read(const void* p,size_t o){T v{};std::memcpy(&v,static_cast<const uint8_t*>(p)+o,sizeof(v));return v;}
template<class T>void Put(void* p,size_t o,T v){std::memcpy(static_cast<uint8_t*>(p)+o,&v,sizeof(v));}
void* At(uintptr_t va){return image+(va-native_reference_code::kImageBase);}
void* Compiled(void* p){return reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(p)+0x10);}
void Jump(uintptr_t va,void* f){auto* p=static_cast<uint8_t*>(At(va));p[0]=0xE9;Put(p,1,uint32_t(reinterpret_cast<uintptr_t>(f)-reinterpret_cast<uintptr_t>(p)-5));}
uint8_t* Controller(){return controller[mapping && current<2 ? 1-current:current];}
bool Resolve(void* p,bool melee){assert(!melee && p==pawn[1]);++resolutions;
  asm volatile("fninit; fldz; pxor %%xmm0,%%xmm0; pxor %%xmm1,%%xmm1; pxor %%xmm2,%%xmm2; pxor %%xmm3,%%xmm3; pxor %%xmm4,%%xmm4; pxor %%xmm5,%%xmm5; pxor %%xmm6,%%xmm6; pxor %%xmm7,%%xmm7"
      :::"xmm0","xmm1","xmm2","xmm3","xmm4","xmm5","xmm6","xmm7","memory");
  return live && Read<void*>(p,0x1D4)==controller[0] && Read<void*>(controller[0],0x1C8)==p;
}
void Reset(bool mapped,int which){
  mapping=mapped;current=which;live=mouse=grounded=true;dead=false;form=1;
  resolutions=records=dead_calls=mouse_calls=0;Put(world,0x39C,uint8_t{0});Put(actor_base,0x64,uint8_t{0});
  for(int i=0;i<3;++i){auto* c=controller[mapped && i<2 ? 1-i:i];
    Put(pawn[i],0x1D4,static_cast<void*>(c));Put(c,0x1C8,static_cast<void*>(pawn[i]));
    Put(pawn[i],0x10,static_cast<void*>(pawn_table));Put(c,0x10,static_cast<void*>(controller_table));
    Put(pawn[i],0x98,static_cast<void*>(world));Put(c,0x98,static_cast<void*>(world));
    Put(pawn[i],0x66,uint8_t{3});Put(c,0x66,uint8_t{3});Put(pawn[i],0x68,static_cast<void*>(actor_base));
    Put(pawn[i],0x580,static_cast<void*>(vehicle));Put(pawn[i],0x584,static_cast<void*>(handler));
    Put(c,0x764,static_cast<void*>(input));
  }
  PublishNativePlayerCombatPawns(mapped ? pawn[1]:nullptr,mapped ? pawn[0]:nullptr);
}
void Abi(){assert(Read<void*>(output,0)==Compiled(Controller()) && Read<uint32_t>(output,4)==0x44556677 &&
    Read<uint32_t>(output,16)==0x11223344 && Read<uint32_t>(output,12)==Read<uint32_t>(output,40));}
void FP(){for(size_t i=208;i<336;++i)assert(output[i]==255);assert(Read<uint16_t>(output,48)==0x37F && Read<uint16_t>(output,50)==0x3800);}
bool Record(){RoleTestBuddyStateCall(At(native_player_combat_code::kRecordVa),Compiled(Controller()),output);Abi();return Read<uint32_t>(output,28)!=0;}
void Stick(float delta,float* x,float* y){RoleTestVehicleStickCall(At(native_player_combat_code::kStickVa),Compiled(Controller()),output,delta,x,y);Abi();}
void CameraAbi(){assert(Read<uint32_t>(output,0)==0x55667788 && Read<uint32_t>(output,4)==0x44556677 &&
    Read<uint32_t>(output,16)==0x11223344 && Read<uint32_t>(output,12)==Read<uint32_t>(output,40));}
void Flying(float delta,float* ci){RoleTestCameraFlyingCall(At(native_player_combat_code::kFlyingStickVa),Compiled(camera_behavior),output,delta,ci);CameraAbi();}
void CameraSet(void* p,bool accepted){
  using namespace native_player_combat_code;
  const uintptr_t methods[]={kConstrainSetVa,kLocationSetVa,kOrbitSetVa};
  const size_t fields[]={0x2C,0x4C,0x98};
  for(size_t i=0;i<3;++i){
    std::memset(camera_behavior,0xA5,sizeof(camera_behavior));camera_field=fields[i];expected_camera_pawn=accepted ? p:nullptr;camera_stores=0;
    auto expected=std::vector<uint8_t>(camera_behavior,camera_behavior+sizeof(camera_behavior));Put(expected.data(),camera_field,expected_camera_pawn);
    RoleTestCameraSetCall(At(methods[i]),Compiled(camera_behavior),output,Compiled(p));CameraAbi();FP();
    assert(camera_stores==1 && !std::memcmp(camera_behavior,expected.data(),expected.size()));
  }
}
void Rejected(){assert(!Record());FP();float x=0.2f,y=-0.7f;
  const uint32_t before_x=Read<uint32_t>(&x,0),before_y=Read<uint32_t>(&y,0);
  Stick(0.1f,&x,&y);Abi();FP();
  assert(Read<uint32_t>(&x,0)==before_x && Read<uint32_t>(&y,0)==before_y && !records && !dead_calls && !mouse_calls);}
DWORD WINAPI OffThread(void*){Reset(true,1);Rejected();CameraSet(pawn[1],false);Put(camera_behavior,0x88,static_cast<void*>(pawn[1]));
  float ci[]={0.25f,-0.25f,9,10};Flying(0.1f,ci);FP();assert(ci[0]==0.25f && ci[1]==-0.25f && !resolutions && !RemoveNativePlayerCombat());return 0;}
}
extern "C" void* MotionClass(void* cache,void*){if(cache==At(0x12B6F3FC))return Compiled(controller_class);assert(cache==At(0x12B6EDDC));return Compiled(player_class);}
extern "C" uint32_t __attribute__((fastcall)) MotionIsA(void* p,void*,void* type){if(type==controller_class)return p==controller[0];assert(type==player_class);return p==pawn[0];}
extern "C" void CameraStore(void* p){assert(p==expected_camera_pawn && Read<void*>(camera_behavior,camera_field)==p);++camera_stores;}
extern "C" uint32_t __attribute__((fastcall)) MotionDead(void* c,void*){assert(c==Compiled(Controller()));++dead_calls;return dead;}
extern "C" uint32_t __attribute__((fastcall)) MotionRecord(void* p,void*){assert(p==Compiled(pawn[current]));++records;return grounded;}
extern "C" uint32_t __attribute__((fastcall)) MotionForm(void* h,void*){assert(h==Compiled(handler));return form;}
extern "C" uint32_t __attribute__((fastcall)) MotionMouse(void* i,void*){assert(i==Compiled(input));++mouse_calls;return mouse;}
int main(int argc,char** argv){
  image=static_cast<uint8_t*>(VirtualAlloc(nullptr,0x2400000,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE));assert(image);
  const auto base=reinterpret_cast<uintptr_t>(image);const auto module=reinterpret_cast<HMODULE>(image);
  native_player_combat_code::Visit([&](uintptr_t va,const auto& bytes,std::initializer_list<size_t> r){auto b=native_reference_code::Relocate(bytes,base,r);std::memcpy(At(va),b.data(),b.size());return true;});
  Jump(0x110031A0,reinterpret_cast<void*>(&MotionClass));Jump(0x10901430,reinterpret_cast<void*>(&MotionIsA));Jump(0x11351B50,reinterpret_cast<void*>(&MotionMouse));
  Jump(0x116FC820,reinterpret_cast<void*>(&CameraStore));
  Put(controller_table,0x280,reinterpret_cast<void*>(&MotionDead));Put(pawn_table,0x328,reinterpret_cast<void*>(&MotionRecord));
  Put(handler,0x10,static_cast<void*>(handler_table));Put(handler_table,0x70,reinterpret_cast<void*>(&MotionForm));
  Put(At(0x1262720C),0,0.0f);Put(At(0x125848B4),0,1.0f);Put(At(0x12678790),0,-1.0f);
  if(argc>1){std::ifstream f(argv[1],std::ios::binary);std::vector<uint8_t> exe{std::istreambuf_iterator<char>(f),{}};assert(exe.size()==37667328);
    const auto check=[&](uintptr_t va,const auto& bytes,std::initializer_list<size_t> r){assert(!std::memcmp(exe.data()+va-0x10900C00,bytes,sizeof(bytes)));
      auto expected=native_reference_code::Relocate(bytes,native_reference_code::kImageBase,{});
      for(size_t p=0x2148A00;p<0x2148A00+0x243ABC;){auto page=Read<uint32_t>(exe.data(),p),size=Read<uint32_t>(exe.data(),p+4);assert(size>=8);
        for(size_t q=p+8;q<p+size;q+=2){auto e=Read<uint16_t>(exe.data(),q);uintptr_t a=native_reference_code::kImageBase+page+(e&4095);
          if((e>>12)==3 && a>=va && a+4<=va+sizeof(bytes))Put(expected.data(),a-va,Read<uint32_t>(expected.data(),a-va)+uint32_t(base-native_reference_code::kImageBase));}p+=size;}
      assert(expected==native_reference_code::Relocate(bytes,base,r));return true;};
    native_player_combat_code::VisitMotion(check);native_player_combat_code::VisitCameraRole(check);
    for(uintptr_t va:{0x1262720Cu,0x125848B4u,0x12678790u})assert(!std::memcmp(At(va),exe.data()+va-0x10901200,4));
  }
  assert(ValidateNativePlayerCombat(module));assert(InstallNativePlayerCombat(module,GetCurrentThreadId(),Resolve,+[](void*,void*,void*){return false;}));
  unsigned record_cases=0,stick_cases=0,camera_cases=0;
  for(unsigned repeat=0;repeat<64;++repeat)for(unsigned flags=0;flags<16;++flags)for(bool mapped:{false,true}){
    Reset(mapped,mapped ? 1:0);dead=(flags&1)!=0;grounded=(flags&2)!=0;
    Put(pawn[current],0x68,(flags&4) ? nullptr:static_cast<void*>(actor_base));Put(actor_base,0x64,uint8_t((flags&8) ? 7:0));
    const auto before=std::vector<uint8_t>(pawn[0],pawn[0]+sizeof(pawn));
    const bool delegated=!dead && ((flags&4) || !(flags&8));
    assert(Record()==(delegated && grounded));assert(records==unsigned(delegated));assert(dead_calls==1);
    FP();assert(!std::memcmp(pawn[0],before.data(),before.size()));++record_cases;
  }
  for(unsigned flags=0;flags<32;++flags)for(bool mapped:{false,true})for(float dt:{-0.1f,0.0f,0.001f,1.0f/60,0.1f,2.0f})for(float value:{-2.0f,-0.003f,0.003f,2.0f}){
    Reset(mapped,mapped ? 1:0);form=(flags&1) ? 1:0;mouse=(flags&2)!=0;
    if(flags&4)Put(pawn[current],0x580,static_cast<void*>(nullptr));
    if(flags&8)Put(pawn[current],0x584,static_cast<void*>(nullptr));
    if(flags&16)Put(Controller(),0x764,static_cast<void*>(nullptr));
    float x=value,y=-value;Stick(dt,&x,&y);
    const bool adjusted=form==1 && mouse && !(flags&28);
    const float expected=adjusted ? std::clamp(value*(dt>0 ? 1.0f/dt:1.0f),-1.0f,1.0f):value;
    assert(std::fabs(x-expected)<0.00001f && std::fabs(y+expected)<0.00001f);++stick_cases;
    CameraSet(pawn[current],true);camera_cases+=3;
    Put(camera_behavior,0x88,static_cast<void*>(pawn[current]));float ci[]={value,-value,9,10};
    const auto before=std::vector<uint8_t>(pawn[0],pawn[0]+sizeof(pawn));Flying(dt,ci);
    assert(std::fabs(ci[0]-expected)<0.00001f && std::fabs(ci[1]+expected)<0.00001f && ci[2]==9 && ci[3]==10);
    assert(!std::memcmp(pawn[0],before.data(),before.size()));++camera_cases;
  }
  Reset(true,1);live=false;CameraSet(pawn[1],false);Reset(true,0);CameraSet(pawn[0],false);
  Reset(false,1);CameraSet(pawn[1],false);CameraSet(nullptr,false);Reset(true,2);CameraSet(pawn[2],false);
  for(unsigned invalid=0;invalid<5;++invalid){Reset(true,1);Put(camera_behavior,0x88,static_cast<void*>(pawn[1]));
    if(invalid==0)live=false;
    if(invalid==1)Put(camera_behavior,0x88,static_cast<void*>(nullptr));
    if(invalid==2)Put(pawn[1],0x1D4,static_cast<void*>(nullptr));
    if(invalid==3)Put(pawn[1],0x1D4,static_cast<void*>(controller[2]));
    if(invalid==4)Put(controller[0],0x1C8,static_cast<void*>(pawn[0]));
    float ci[]={0.25f,-0.25f,9,10};Flying(0.1f,ci);FP();assert(ci[0]==0.25f && ci[1]==-0.25f && !mouse_calls);
  }
  Reset(true,1);live=false;Rejected();assert(resolutions==2);
  Reset(true,0);Rejected();assert(!resolutions);
  Reset(true,2);Rejected();assert(!resolutions);
  Reset(false,1);Rejected();assert(!resolutions);
  Reset(true,1);Put(pawn[1],0x1D4,static_cast<void*>(controller[2]));Rejected();
  Reset(true,1);Put(controller[0],0x1C8,static_cast<void*>(nullptr));Rejected();assert(!resolutions);
  Reset(true,1);Put(world,0x39C,uint8_t{1});Put(controller[0],0x66,uint8_t{1});Rejected();assert(!resolutions);
  HANDLE thread=CreateThread(nullptr,0,OffThread,nullptr,0,nullptr);assert(thread);assert(WaitForSingleObject(thread,10000)==WAIT_OBJECT_0);CloseHandle(thread);
  native_player_combat_code::VisitMotion([&](uintptr_t va,const auto& code,std::initializer_list<size_t>){auto* p=static_cast<uint8_t*>(At(va));for(size_t i=0;i<sizeof(code);++i){p[i]^=1;assert(!ValidateNativePlayerCombat(module));p[i]^=1;}return true;});
  native_player_combat_code::VisitCameraRole([&](uintptr_t va,const auto& code,std::initializer_list<size_t>){auto* p=static_cast<uint8_t*>(At(va));for(size_t i=0;i<sizeof(code);++i){p[i]^=1;assert(!ValidateNativePlayerCombat(module));p[i]^=1;}return true;});
  for(size_t i=3;i<5;++i){auto& patch=g_combat_patches[i];patch.target[0]^=1;assert(!RemoveNativePlayerCombat());patch.target[0]^=1;}
  for(size_t i=12;i<16;++i){auto& patch=g_combat_patches[i];patch.target[0]^=1;assert(!RemoveNativePlayerCombat());patch.target[0]^=1;}
  assert(RemoveNativePlayerCombat());assert(ValidateNativePlayerCombat(module));Reset(false,0);assert(Record());
  Reset(false,1);Rejected();assert(VirtualFree(image,0,MEM_RELEASE));
  std::printf("NativePlayerMotionTests passed: %u record + %u vehicle-input + %u camera setter/flying combinations; full native camera-to-controller route, GC stores, unchanged gamepad input, stale-role/network guards, ABI/FP/signatures (deep input/form/engine/GC callbacks are fixtures, not gameplay)\n",record_cases,stick_cases,camera_cases);
}
