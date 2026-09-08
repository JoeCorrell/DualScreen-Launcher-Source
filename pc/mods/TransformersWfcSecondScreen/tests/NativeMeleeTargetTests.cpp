#include "NativePlayerCombat.cpp"
#include "NativeMeleeTargetGuard.cpp"
#include <cassert>
#include <cstdio>
#include <cwchar>
#include <fstream>
#include <vector>
using namespace wfcds;
extern "C" void RoleTestMeleeTargetCall(void*,void*,void*,void*);
namespace {
alignas(16) uint8_t pawns[2][0x900]{},controllers[2][0x800]{},targeting[0x100]{},victims[2][0x100]{},player_class[0x100]{},output[560]{};
struct Picked {void* actor;float score;};Picked buffers[2][8]{};
void* slots[7]{};uint8_t* image=nullptr;bool mapping=false,live=true,deleting=false,bad_layout=false,bad_inner=false,bad_stride=false;
int current=0;uint32_t generation=1;unsigned metadata_reads=0,barriers=0,target_checks=0;
NativeMeleeTargetGuard guard;
template<class T>T Read(const void* p,size_t o){T v{};std::memcpy(&v,static_cast<const uint8_t*>(p)+o,sizeof(v));return v;}
template<class T>void Put(void* p,size_t o,T v){std::memcpy(static_cast<uint8_t*>(p)+o,&v,sizeof(v));}
void* At(uintptr_t va){return image+(va-native_reference_code::kImageBase);}
void* Compiled(void* p){return reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(p)+0x10);}
void Jump(uintptr_t va,void* f){auto* p=static_cast<uint8_t*>(At(va));p[0]=0xE9;Put(p,1,uint32_t(reinterpret_cast<uintptr_t>(f)-reinterpret_cast<uintptr_t>(p)-5));}
uint32_t Index(void* p){for(unsigned i=0;i<7;++i)if(slots[i] && slots[i]==p)return i+1;return 0;}
bool Resolve(void* p,bool melee){assert(p==pawns[1] && !melee);return live && Read<void*>(p,0x1D4)==controllers[0] && Read<void*>(controllers[0],0x1C8)==p;}
bool TargetResolve(void* p,void* t,void* v){++target_checks;assert(t==targeting);
  const bool safe=NativeMeleeTargetSafe(&guard,GetCurrentThreadId(),generation,p,controllers[0],t,v);
  asm volatile("fninit; fldz; pxor %%xmm0,%%xmm0; pxor %%xmm1,%%xmm1; pxor %%xmm2,%%xmm2; pxor %%xmm3,%%xmm3; pxor %%xmm4,%%xmm4; pxor %%xmm5,%%xmm5; pxor %%xmm6,%%xmm6; pxor %%xmm7,%%xmm7"
      :::"xmm0","xmm1","xmm2","xmm3","xmm4","xmm5","xmm6","xmm7","memory");return safe;
}
void Reset(bool mapped,int which){mapping=mapped;current=which;live=true;deleting=bad_layout=bad_inner=bad_stride=false;generation=1;guard={};metadata_reads=barriers=target_checks=0;
  slots[0]=pawns[0];slots[1]=pawns[1];slots[2]=controllers[0];slots[3]=controllers[1];slots[4]=targeting;slots[5]=victims[0];slots[6]=victims[1];
  for(int i=0;i<2;++i){auto* c=controllers[mapped ? 1-i:i];Put(pawns[i],0x1D4,static_cast<void*>(c));Put(c,0x1C8,static_cast<void*>(pawns[i]));Put(c,0x608,static_cast<void*>(targeting));}
  Put(targeting,0x48,static_cast<void*>(pawns[which]));Put(targeting,0x34,UnrealArrayView{buffers[0],8,8});
  for(auto& buffer:buffers)for(unsigned i=0;i<8;++i)buffer[i]={nullptr,float(i)+0.25f};
  PublishNativePlayerCombatPawns(mapped ? pawns[1]:nullptr,mapped ? pawns[0]:nullptr);
}
void Call(void* victim){RoleTestMeleeTargetCall(At(native_player_combat_code::kTargetSetVa),Compiled(victim),output,Compiled(targeting));
  assert(Read<void*>(output,0)==Compiled(victim) && Read<uint32_t>(output,4)==0x44556677 && Read<uint32_t>(output,16)==0x11223344 && Read<uint32_t>(output,12)==Read<uint32_t>(output,40));
  for(size_t i=208;i<336;++i)assert(output[i]==255);
  assert(Read<uint16_t>(output,48)==0x37F && Read<uint16_t>(output,50)==0x3800);
}
void Reject(){const auto before=std::vector<uint8_t>(reinterpret_cast<uint8_t*>(buffers),reinterpret_cast<uint8_t*>(buffers)+sizeof(buffers));Call(victims[0]);assert(!barriers && !std::memcmp(buffers,before.data(),before.size()));}
DWORD WINAPI OffThread(void*){Reset(true,1);Reject();assert(!metadata_reads && !target_checks && !RemoveNativePlayerCombat());return 0;}
}
namespace wfcds {
void* UnrealObjectAt(int32_t i){return i>=0 && i<7 ? slots[i]:nullptr;}
uint32_t UnrealObjectIndex(void* p){++metadata_reads;return Index(p);}
bool UnrealObjectIsA(void* p,const wchar_t* type){++metadata_reads;if(!Index(p))return false;
  if(!std::wcscmp(type,L"TnPawn"))return p==pawns[0] || p==pawns[1];
  if(!std::wcscmp(type,L"TnPlayerController"))return p==controllers[0];
  if(!std::wcscmp(type,L"TnPlayerTargeting"))return p==targeting;
  return !std::wcscmp(type,L"Actor") && (p==victims[0] || p==victims[1]);
}
bool UnrealResolveObjectPropertyOffset(void* p,const wchar_t* name,uint32_t* offset){++metadata_reads;if(!Index(p) || !offset)return false;
  if(p==pawns[0] || p==pawns[1]){assert(!std::wcscmp(name,L"Controller"));*offset=0x1D4;}
  else if(p==targeting){assert(!std::wcscmp(name,L"_TargetingOwner"));*offset=0x48;}
  else if(!std::wcscmp(name,L"Pawn"))*offset=0x1C8;
  else {assert(!std::wcscmp(name,L"PlayerTargeting"));*offset=0x608;}
  if(bad_layout)*offset+=4;
  return true;
}
bool UnrealReadObjectProperty(void* p,const wchar_t* name,void** value){uint32_t offset=0;if(!UnrealResolveObjectPropertyOffset(p,name,&offset))return false;*value=Read<void*>(p,offset);return !*value || Index(*value);}
bool UnrealResolveArrayPropertyOffset(void* p,const wchar_t* name,uint32_t* value){++metadata_reads;assert(p==targeting && !std::wcscmp(name,L"PickedTargets"));*value=bad_layout ? 0x38:0x34;return true;}
bool UnrealReadStructArrayProperty(void* p,const wchar_t* name,UnrealStructArrayView* out){++metadata_reads;assert(p==targeting && !std::wcscmp(name,L"PickedTargets"));
  const auto a=Read<UnrealArrayView>(p,0x34);if(a.data!=buffers[0] && a.data!=buffers[1])return false;
  *out={a.data,a.count,a.capacity,bad_stride ? 4u:8u,player_class};return true;
}
bool UnrealResolveStructFieldOffset(const UnrealStructValueView& value,const wchar_t* field,const wchar_t* type,uint32_t size,uint32_t* offset){++metadata_reads;
  assert(value.struct_definition==player_class && !std::wcscmp(field,L"TargetActor") && !std::wcscmp(type,L"ObjectProperty") && size==4);*offset=bad_inner ? 4:0;return true;
}
bool UnrealReadBoolProperty(void* p,const wchar_t* name,bool* value){++metadata_reads;assert((p==victims[0] || p==victims[1]) && !std::wcscmp(name,L"bDeleteMe"));*value=deleting;return true;}
}
extern "C" void* MeleeClass(void* cache,void*){assert(cache==At(0x12B6EDDC));return Compiled(player_class);}
extern "C" uint32_t __attribute__((fastcall)) MeleeIsA(void* p,void*,void* type){assert(type==player_class);return p==pawns[0];}
extern "C" void MeleeGCImpl(void* p){const auto a=Read<UnrealArrayView>(targeting,0x34);assert(Read<void*>(a.data,24)==p);++barriers;}
extern "C" __attribute__((naked)) void MeleeGC(){asm volatile("pushal; pushl 36(%esp); call _MeleeGCImpl; addl $4,%esp; popal; ret");}
extern "C" void UnexpectedResize(){assert(false && "retained targeting must never grow inside this setter");}
int main(int argc,char** argv){
  image=static_cast<uint8_t*>(VirtualAlloc(nullptr,0x2400000,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE));assert(image);
  const auto base=reinterpret_cast<uintptr_t>(image);const auto module=reinterpret_cast<HMODULE>(image);
  native_player_combat_code::Visit([&](uintptr_t va,const auto& code,std::initializer_list<size_t> r){auto b=native_reference_code::Relocate(code,base,r);std::memcpy(At(va),b.data(),b.size());return true;});
  Jump(0x110031A0,reinterpret_cast<void*>(&MeleeClass));Jump(0x10901430,reinterpret_cast<void*>(&MeleeIsA));Jump(0x116FC820,reinterpret_cast<void*>(&MeleeGC));Jump(0x10BB21A0,reinterpret_cast<void*>(&UnexpectedResize));
  if(argc>1){std::ifstream f(argv[1],std::ios::binary);std::vector<uint8_t> exe{std::istreambuf_iterator<char>(f),{}};assert(exe.size()==37667328);
    native_player_combat_code::VisitTargetSet([&](uintptr_t va,const auto& code,std::initializer_list<size_t> r){assert(!std::memcmp(exe.data()+va-0x10900C00,code,sizeof(code)));
      auto expected=native_reference_code::Relocate(code,native_reference_code::kImageBase,{});
      for(size_t p=0x2148A00;p<0x2148A00+0x243ABC;){auto page=Read<uint32_t>(exe.data(),p),size=Read<uint32_t>(exe.data(),p+4);assert(size>=8);
        for(size_t q=p+8;q<p+size;q+=2){auto e=Read<uint16_t>(exe.data(),q);uintptr_t a=native_reference_code::kImageBase+page+(e&4095);
          if((e>>12)==3 && a>=va && a+4<=va+sizeof(code))Put(expected.data(),a-va,Read<uint32_t>(expected.data(),a-va)+uint32_t(base-native_reference_code::kImageBase));}p+=size;}
      assert(expected==native_reference_code::Relocate(code,base,r));return true;});
  }
  assert(ValidateNativePlayerCombat(module));assert(!InstallNativePlayerCombat(module,GetCurrentThreadId(),Resolve,nullptr));assert(InstallNativePlayerCombat(module,GetCurrentThreadId(),Resolve,TargetResolve));
  for(unsigned repeat=0;repeat<500;++repeat)for(bool mapped:{false,true}){
    Reset(mapped,mapped ? 1:0);const auto pawn_before=std::vector<uint8_t>(pawns[0],pawns[0]+sizeof(pawns));
    for(void* v:{static_cast<void*>(victims[0]),static_cast<void*>(victims[1]),static_cast<void*>(nullptr)}){
      Call(v);assert(buffers[0][3].actor==v);const auto work=metadata_reads,writes=barriers;Call(v);assert(metadata_reads==work && barriers==writes);
      for(unsigned i=0;i<8;++i){assert(buffers[0][i].score==float(i)+0.25f);if(i!=3)assert(!buffers[0][i].actor);}
    }
    assert(barriers==3 && !std::memcmp(pawns[0],pawn_before.data(),pawn_before.size()));
  }
  for(const auto a:{UnrealArrayView{nullptr,8,8},UnrealArrayView{buffers[0],0,8},UnrealArrayView{buffers[0],3,8},
      UnrealArrayView{buffers[0],8,3},UnrealArrayView{buffers[0],65,65}}){Reset(true,1);Put(targeting,0x34,a);Reject();}
  Reset(true,1);Call(victims[0]);barriers=0;const auto warm=metadata_reads;Put(targeting,0x34,UnrealArrayView{buffers[1],8,8});Call(victims[1]);assert(buffers[1][3].actor==victims[1] && metadata_reads>warm);
  Reset(true,1);Call(victims[0]);barriers=0;Put(targeting,0x34,UnrealArrayView{buffers[0],3,8});Reject();
  Reset(true,1);bad_layout=true;Reject();Reset(true,1);bad_inner=true;Reject();Reset(true,1);bad_stride=true;Reject();
  Reset(true,1);deleting=true;Reject();Reset(true,1);slots[5]=nullptr;Reject();Reset(true,1);slots[4]=nullptr;Reject();
  Reset(true,1);Put(controllers[0],0x608,static_cast<void*>(nullptr));Reject();
  Reset(true,1);generation=0;Reject();Reset(true,1);live=false;Reject();Reset(true,0);Reject();Reset(false,1);Reject();
  Reset(true,1);Call(victims[0]);const auto previous=metadata_reads;++generation;Call(victims[0]);assert(metadata_reads>previous);
  HANDLE thread=CreateThread(nullptr,0,OffThread,nullptr,0,nullptr);assert(thread);assert(WaitForSingleObject(thread,10000)==WAIT_OBJECT_0);CloseHandle(thread);
  native_player_combat_code::VisitTargetSet([&](uintptr_t va,const auto& code,std::initializer_list<size_t>){auto* p=static_cast<uint8_t*>(At(va));for(size_t i=0;i<sizeof(code);++i){p[i]^=1;assert(!ValidateNativePlayerCombat(module));p[i]^=1;}return true;});
  auto& patch=g_combat_patches[11];patch.target[0]^=1;assert(!RemoveNativePlayerCombat());patch.target[0]^=1;
  assert(RemoveNativePlayerCombat());assert(ValidateNativePlayerCombat(module));assert(VirtualFree(image,0,MEM_RELEASE));
  std::puts("NativeMeleeTargetTests passed: 3,000 native target changes + 3,000 zero-reflection repeats; actual array/store/GC order, cached guard invalidation, bounds/owner/victim/thread rejection, ABI/FP and signatures (metadata/engine fixtures, not gameplay)");
}
