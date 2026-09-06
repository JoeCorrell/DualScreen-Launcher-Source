// Production wrapper/guards; component executor and UObject metadata fixtures.
// The retained Engine executor is invoked by the real bridge at runtime; these
// tests do not create PhysX scenes or claim in-game collision verification.
#include "NativeCollisionRoles.cpp"
#include <cassert>
#include <cstdio>
#include <cwchar>
using namespace wfcds;
namespace {
struct Object { const wchar_t* type; uint32_t index=1; bool valid=true,dead=false; void* owner=nullptr; std::array<bool,21> channels{}; };
Object pawn{L"TnPawn"},pc{L"TnPlayerController"},ai{L"TnAiController"},world{L"WorldInfo"},
    service{L"TnPawnCollisionController"},robot{L"SkeletalMeshComponent"},vehicle{L"SkeletalMeshComponent"},foreign{L"WorldInfo"};
Object* objects[]={&pawn,&pc,&ai,&world,&service,&robot,&vehicle,&foreign};
void *controller=&pc,*current_pawn=&pawn,*ack=&pawn,*current_world=&world,*pawn_service=&service,*robot_mesh=&robot,*vehicle_mesh=&vehicle;
uint8_t mode=0,role=3; bool available=true,missing_channel=false; unsigned calls=0,fail_call=0,dirty=0;
Object* Lookup(void* p) { for(auto* o:objects) if(o->valid && p==o) return o; return nullptr; }
void Reset(bool player=true,bool with_vehicle=true) {
  uint32_t id=1; for(auto* o:objects) { o->index=id++;o->valid=true;o->dead=false;o->owner=nullptr;o->channels={}; }
  service.type=L"TnPawnCollisionController"; robot.owner=vehicle.owner=service.owner=&pawn;
  controller=player ? &pc:&ai;current_pawn=ack=&pawn;current_world=&world;pawn_service=&service;robot_mesh=&robot;vehicle_mesh=with_vehicle ? &vehicle:nullptr;
  for(size_t i=0;i<21;++i) { robot.channels[i]=(i%3)==0;vehicle.channels[i]=(i%4)==0; }
  robot.channels[15]=vehicle.channels[15]=!player; // Pending role conversion.
  mode=0;role=3;available=true;missing_channel=false;calls=fail_call=dirty=0;
  g_collision_thread=GetCurrentThreadId();g_collision_busy=false;
}
NativePlayerServiceResult Apply(bool player=true) { return NativeSetCollisionRole(&pawn,&service,player); }
void Reject(bool player=true) { const auto r=robot.channels,v=vehicle.channels;const auto n=calls;
  assert(Apply(player)==NativePlayerServiceResult::kRejected && calls==n && robot.channels==r && vehicle.channels==v); }
DWORD WINAPI WrongThread(void*) { Reject();return 0; }
}
namespace wfcds {
bool UnrealObjectIsA(void* p,const wchar_t* name) { auto* o=Lookup(p);return o && !std::wcscmp(o->type,name); }
uint32_t UnrealObjectIndex(void* p) { auto* o=Lookup(p);return o ? o->index:0; }
bool UnrealObjectClassName(void* p,char* out,size_t size) { auto* o=Lookup(p);if(!o)return false;std::snprintf(out,size,"%ls",o->type);return true; }
bool UnrealReadBoolProperty(void* p,const wchar_t* name,bool* out) { auto* o=Lookup(p);if(!o || std::wcscmp(name,L"bDeleteMe"))return false;*out=o->dead;return true; }
bool UnrealReadByteProperty(void* p,const wchar_t* name,uint8_t* out) { if(!Lookup(p))return false;
  if(!std::wcscmp(name,L"Role"))*out=role;else if(!std::wcscmp(name,L"NetMode"))*out=mode;else return false;return true; }
bool UnrealReadObjectProperty(void* p,const wchar_t* name,void** out) {
  auto* o=Lookup(p);if(!o)return false;
  if(!std::wcscmp(name,L"WorldInfo"))*out=current_world;
  else if(p==&pawn && !std::wcscmp(name,L"Controller"))*out=controller;
  else if(p==&pawn && !std::wcscmp(name,L"_CollisionController"))*out=pawn_service;
  else if((p==&pc||p==&ai) && !std::wcscmp(name,L"Pawn"))*out=current_pawn;
  else if(p==&pc && !std::wcscmp(name,L"AcknowledgedPawn"))*out=ack;
  else if(!std::wcscmp(name,L"Owner")||!std::wcscmp(name,L"_Owner"))*out=o->owner;
  else if(p==&service && !std::wcscmp(name,L"_RobotMeshComponent"))*out=robot_mesh;
  else if(p==&service && !std::wcscmp(name,L"_VehicleMeshComponent"))*out=vehicle_mesh;
  else return false;
  return true;
}
bool UnrealValidateCallContract(void* p,const wchar_t* fn,const wchar_t* result,std::initializer_list<UnrealCallParameterContract> args) {
  assert(p==&robot||p==&vehicle);assert(!std::wcscmp(fn,L"SetRBCollidesWithChannel") && !result && args.size()==2);
  auto* a=args.begin();assert(!std::wcscmp(a[0].name,L"Channel") && !std::wcscmp(a[0].property_class,L"ByteProperty") && a[0].size==1);
  assert(!std::wcscmp(a[1].name,L"bNewCollides") && !std::wcscmp(a[1].property_class,L"BoolProperty") && a[1].size==4);return available;
}
bool UnrealReadStructBoolProperty(void* p,const wchar_t* prop,const wchar_t* field,bool* out) {
  auto* o=Lookup(p);if(!o || std::wcscmp(prop,L"RBCollideWithChannels") || missing_channel)return false;
  for(size_t i=0;i<kCollisionChannels.size();++i) {
    if(!std::wcscmp(field,kCollisionChannels[i])) { *out=o->channels[i];return true; }
  }
  return false;
}
bool UnrealCallVoidByteBool(void* p,const wchar_t* fn,const wchar_t* byte,uint8_t channel,const wchar_t* boolean,bool value) {
  assert(g_collision_busy && (p==&robot||p==&vehicle));assert(!std::wcscmp(fn,L"SetRBCollidesWithChannel") && !std::wcscmp(byte,L"Channel") && channel==15 && !std::wcscmp(boolean,L"bNewCollides"));
  assert(Apply(value)==NativePlayerServiceResult::kRejected);++calls;
  static_cast<Object*>(p)->channels[15]=value; // A failed executor may already have mutated.
  if(calls==1) {
    if(dirty==1)++pawn.index;
    if(dirty==2)vehicle_mesh=nullptr;
    if(dirty==3)ack=nullptr;
    if(dirty==4)mode=3;
    if(dirty==5)robot.channels[16]=!robot.channels[16];
    if(dirty==6)vehicle.channels[15]=!vehicle.channels[15];
    if(dirty==7)service.owner=nullptr;
    if(dirty==8)controller=&ai;
  }
  return calls!=fail_call;
}
}
int main() {
  for(unsigned i=0;i<6000;++i) {
    const bool player=(i&1)!=0,with_vehicle=(i&2)!=0;Reset(player,with_vehicle);
    const auto p=pawn;auto r=robot.channels,v=vehicle.channels;
    assert(PrepareNativeCollisionRole(GetCurrentThreadId(),&pawn,&service) && !calls);
    assert(Apply(player)==NativePlayerServiceResult::kApplied && calls==(with_vehicle ? 2u:1u));
    r[15]=player;if(with_vehicle)v[15]=player;assert(robot.channels==r && vehicle.channels==v && pawn.index==p.index);
    assert(Apply(player)==NativePlayerServiceResult::kApplied); // Still calls native setter for synchronized shapes.
    controller=player ? static_cast<void*>(&ai):&pc;
    assert(Apply(!player)==NativePlayerServiceResult::kApplied);r[15]=!player;if(with_vehicle)v[15]=!player;
    assert(robot.channels==r && vehicle.channels==v);
  }
  for(unsigned failure=1;failure<=2;++failure) { Reset();fail_call=failure;
    assert(Apply()==NativePlayerServiceResult::kStateMismatch && calls==failure && !g_collision_busy);
    fail_call=0;controller=&ai;assert(Apply(false)==NativePlayerServiceResult::kApplied && !robot.channels[15] && !vehicle.channels[15]);
  }
  for(unsigned d=1;d<=8;++d) { Reset();dirty=d;assert(Apply()==NativePlayerServiceResult::kStateMismatch && calls==1 && !g_collision_busy); }
  Reset();Reject(false);Reset();ack=nullptr;Reject();Reset();role=1;Reject();Reset();mode=3;Reject();
  Reset();service.owner=&foreign;Reject();Reset();vehicle.owner=&foreign;Reject();Reset();pawn_service=nullptr;Reject();
  Reset();robot_mesh=nullptr;Reject();Reset();vehicle_mesh=&robot;Reject();Reset();available=false;Reject();
  Reset();missing_channel=true;Reject();Reset();service.type=L"UnknownCollisionOverride";Reject();
  for(auto* o:{&pawn,&pc,&world,&service,&robot,&vehicle}){Reset();o->valid=false;Reject();}
  Reset();g_collision_busy=true;Reject();g_collision_busy=false;
  Reset();HANDLE t=CreateThread(nullptr,0,WrongThread,nullptr,0,nullptr);assert(t);assert(WaitForSingleObject(t,10000)==WAIT_OBJECT_0);CloseHandle(t);
  std::puts("NativeCollisionRolesTests passed: 6000 role cycles, robot/vehicle and robot-only paths, all channels preserved, partial-failure rollback and dirty callback guards (native physics/metadata fixtures, not gameplay)");
}
