// Executes the retained game's Reset/Remove/focus/command/array bodies.
// UObject metadata, engine allocator, imported CRT and reference barrier are
// fixtures. This is native-boundary validation, not a gameplay test.
#include "NativePointOfInterest.cpp"
#include <cassert>
#include <cstdio>
#include <cwchar>
#include <fstream>
#include <vector>
using namespace wfcds;
namespace {
struct Object { alignas(16) uint8_t bytes[0x800]{}; const wchar_t* type; uint32_t index=1; bool valid=true,dead=false; Object* cls=nullptr; };
Object pc{{},L"TnPlayerController"},pawn{{},L"TnPawn"},world{{},L"WorldInfo"},poi{{},L"TnPointOfInterestManager"},
    command{{},L"TnContextualCommandPointOfInterest"},handler{{},L"TnContextualCommandHandler"},
    strategy{{},L"TnCameraStrategyManager"},camera{{},L"TnCamera"},focus_class{{},L"Class"},command_class{{},L"Class"},
    target{{},L"TnPointOfInterest"},other_command{{},L"TnContextualCommand"},other_class{{},L"Class"},
    child_command{{},L"TnContextualCommand"},child_class{{},L"Class"};
Object* objects[]={&pc,&pawn,&world,&poi,&command,&handler,&strategy,&camera,&focus_class,&command_class,&target,&other_command,&other_class,&child_command,&child_class};
uint8_t* image=nullptr;
alignas(16) uint8_t allocator[24]{},allocator_table[24]{};
bool refs=true,bad_offset=false,bad_bool=false; unsigned barriers=0,dirty=0,allocations=0,frees=0;
Object* Lookup(void* p) { for(auto* o:objects) if(o->valid && o->bytes==p) return o; return nullptr; }
uint32_t Read(const void* p,size_t off) { uint32_t v=0; std::memcpy(&v,static_cast<const uint8_t*>(p)+off,4); return v; }
void Put(void* p,size_t off,uintptr_t v) { const uint32_t n=static_cast<uint32_t>(v); std::memcpy(static_cast<uint8_t*>(p)+off,&n,4); }
void Link(Object& o,size_t off,Object* p) { Put(o.bytes,off,reinterpret_cast<uintptr_t>(p ? p->bytes:nullptr)); }
uint8_t* At(uintptr_t va) { return image+va-native_reference_code::kImageBase; }
void Jump(uintptr_t va,void* target_function) { At(va)[0]=0xE9; Put(At(va),1,reinterpret_cast<uintptr_t>(target_function)-reinterpret_cast<uintptr_t>(At(va)+5)); }
void* __attribute__((fastcall)) Allocate(void*,void*,uint32_t size,uint32_t alignment) {
  assert(alignment==8); ++allocations; void* p=HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,size); assert(p); return p;
}
void* __attribute__((fastcall)) Reallocate(void* self,void* unused,void* old,uint32_t size,uint32_t alignment) {
  assert(!old); return Allocate(self,unused,size,alignment);
}
void __attribute__((fastcall)) Free(void*,void*,void* p) { assert(p); ++frees; assert(HeapFree(GetProcessHeap(),0,p)); }
LONG WINAPI Exchange(LONG volatile* target_word,LONG value) { return InterlockedExchange(target_word,value); }
void __cdecl Barrier(void*) {
  ++barriers; assert(g_poi_busy);
  assert(NativeResetPointOfInterest(pc.bytes,pawn.bytes,poi.bytes)==NativePlayerServiceResult::kRejected);
  if(barriers!=1) return;
  if(dirty==1) ++pc.index;
  if(dirty==2) ++target.index;
  if(dirty==3) Link(pc,0x300,nullptr);
  if(dirty==4) world.bytes[0x39C]=3;
  if(dirty==5) Link(pc,0x67C,nullptr);
  if(dirty==6) Put(camera.bytes,0x498,Read(camera.bytes,0x498)^4);
  if(dirty==7) ++other_command.index;
}
bool Offset(void* p,const wchar_t* name,uint32_t* out) {
  auto* o=Lookup(p); if(!o) return false;
  const auto f=[&](const wchar_t* n,uint32_t off) { if(std::wcscmp(name,n)) return false; *out=off; return true; };
  if(f(L"WorldInfo",0x98)) return true;
  if(o==&pc) return f(L"Pawn",0x1C8)||f(L"AcknowledgedPawn",0x300)||f(L"_PointOfInterestManager",0x67C)||
      f(L"ContextualCommander",0x674)||f(L"_CameraStrategyManager",0x60C)||f(L"PlayerCamera",0x2D4);
  if(o==&pawn) return f(L"Controller",0x1D4);
  if(o==&poi) return f(L"Owner",0x2C)||f(L"_Command",0x34)||f(L"ActivePointOfInterest",0x28);
  if(o==&command) return f(L"PointOfInterest",0x34);
  if(o==&strategy) return f(L"_Camera",0x30);
  if(o==&camera) return f(L"PCOwner",0x1C8);
  return false;
}
void ReleaseArray(Object& o,size_t off) {
  auto* data=reinterpret_cast<void*>(Read(o.bytes,off)); if(data) assert(HeapFree(GetProcessHeap(),0,data));
  std::memset(o.bytes+off,0,12);
}
void Array(Object& o,size_t off,std::initializer_list<Object*> items,bool notification=false,int capacity=16) {
  ReleaseArray(o,off); const uint32_t stride=notification ? 8:4;
  auto* data=static_cast<uint8_t*>(HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,capacity*stride)); assert(data);
  Put(o.bytes,off,reinterpret_cast<uintptr_t>(data)); Put(o.bytes,off+4,items.size()); Put(o.bytes,off+8,capacity);
  int i=0; for(auto* item:items) { Put(data,i*stride,reinterpret_cast<uintptr_t>(item->bytes)); ++i; }
}
void Reset(bool active=true,bool grow=false) {
  ReleaseArray(handler,0x28); ReleaseArray(handler,0x34); ReleaseArray(camera,0x49C);
  uint32_t id=1;
  for(auto* o:objects) { std::memset(o->bytes,0,sizeof(o->bytes)); o->index=id++; o->valid=true; o->dead=false; o->cls=nullptr; }
  command.type=L"TnContextualCommandPointOfInterest"; poi.type=L"TnPointOfInterestManager";
  for(auto* o:{&pc,&pawn,&camera,&target}) { Link(*o,0x98,&world); o->bytes[0x66]=3; }
  Link(pc,0x1C8,&pawn); Link(pc,0x300,&pawn); Link(pawn,0x1D4,&pc);
  Link(pc,0x67C,&poi); Link(pc,0x674,&handler); Link(pc,0x60C,&strategy); Link(pc,0x2D4,&camera);
  Link(poi,0x2C,&pc); Link(poi,0x34,&command); Link(poi,0x28,active ? &target:nullptr); Link(command,0x34,&target);
  Link(strategy,0x30,&camera); Link(camera,0x1C8,&pc);
  Put(poi.bytes,0x30,0xABCDEFAD); Put(camera.bytes,0x498,0xCAFEBAFE);
  Put(focus_class.bytes,4,1); Put(command_class.bytes,4,2); Put(other_class.bytes,4,3); Put(child_class.bytes,4,4);
  Link(child_class,0x28,&command_class);
  command.cls=&command_class; other_command.cls=&other_class; child_command.cls=&child_class;
  unsigned class_id=1;
  for(auto* o:{&command,&other_command,&child_command}) { Put(o->bytes,0x14,class_id); Put(At(0x12A29FD4),class_id*4,reinterpret_cast<uintptr_t>(o->cls->bytes)); ++class_id; }
  Put(At(0x12B6F460),0,reinterpret_cast<uintptr_t>(focus_class.bytes+0x10));
  Put(At(0x12B00204),0,reinterpret_cast<uintptr_t>(allocator)); Put(allocator,0,reinterpret_cast<uintptr_t>(allocator_table));
  Put(allocator_table,8,reinterpret_cast<uintptr_t>(&Allocate)); Put(allocator_table,12,reinterpret_cast<uintptr_t>(&Reallocate)); Put(allocator_table,16,reinterpret_cast<uintptr_t>(&Free));
  Put(At(0x11948544),0,reinterpret_cast<uintptr_t>(&std::memmove)); Put(At(0x119482F0),0,reinterpret_cast<uintptr_t>(&Exchange));
  Array(handler,0x28,{&other_command,&command,&other_command});
  if(grow) Array(handler,0x34,{&other_command},true,1);
  else {
    Array(handler,0x34,{&command,&other_command,&child_command,&command},true);
    auto* data=reinterpret_cast<uint8_t*>(Read(handler.bytes,0x34)); data[3*8+4]=1; // Preserve earlier remove notification.
  }
  Array(camera,0x49C,{&other_class,&focus_class,&focus_class,&other_class});
  refs=true; bad_offset=bad_bool=false; barriers=dirty=allocations=frees=0;
  g_poi_module=reinterpret_cast<HMODULE>(image); g_poi_thread=GetCurrentThreadId(); g_poi_busy=false;
}
NativePlayerServiceResult Apply() { return NativeResetPointOfInterest(pc.bytes,pawn.bytes,poi.bytes); }
void Rejected() { const auto p=poi,c=command,cam=camera; const auto n=barriers; assert(Apply()==NativePlayerServiceResult::kRejected && barriers==n);
  assert(!std::memcmp(p.bytes,poi.bytes,sizeof(p.bytes)) && !std::memcmp(c.bytes,command.bytes,sizeof(c.bytes)) && !std::memcmp(cam.bytes,camera.bytes,sizeof(cam.bytes))); }
DWORD WINAPI WrongThread(void*) { Rejected(); return 0; }
}
namespace wfcds {
bool UnrealObjectIsA(void* p,const wchar_t* type) { auto* o=Lookup(p); return o && (!std::wcscmp(o->type,type) || (!std::wcscmp(type,L"TnContextualCommand") && (o==&command||o==&other_command||o==&child_command))); }
uint32_t UnrealObjectIndex(void* p) { auto* o=Lookup(p); return o ? o->index:0; }
void* UnrealObjectClass(void* p) { auto* o=Lookup(p); return o && o->cls && o->cls->valid ? o->cls->bytes:nullptr; }
bool UnrealObjectClassName(void* p,char* out,size_t n) { auto* o=Lookup(p); if(!o) return false; std::snprintf(out,n,"%ls",o->type); return true; }
bool UnrealResolveClassByPath(const wchar_t* path,void** out) { if(std::wcscmp(path,L"TransGame.TnPCS_FocusingOnPointOfInterest") || !focus_class.valid) return false; *out=focus_class.bytes; return true; }
bool UnrealObjectReferenceWritesReady() { return refs; }
bool UnrealResolveObjectPropertyOffset(void* p,const wchar_t* name,uint32_t* out) { if(!Offset(p,name,out)) return false; if(bad_offset) ++*out; return true; }
bool UnrealReadObjectProperty(void* p,const wchar_t* name,void** out) { uint32_t off=0; if(!Offset(p,name,&off)) return false; *out=reinterpret_cast<void*>(Read(p,off)); return true; }
bool UnrealResolveBytePropertyOffset(void* p,const wchar_t* name,uint32_t* out) { if(!Lookup(p)) return false; if(!std::wcscmp(name,L"Role")) *out=0x66; else if(!std::wcscmp(name,L"NetMode")) *out=0x39C; else return false; return true; }
bool UnrealReadByteProperty(void* p,const wchar_t* name,uint8_t* out) { uint32_t off=0; if(!UnrealResolveBytePropertyOffset(p,name,&off)) return false; *out=static_cast<uint8_t*>(p)[off]; return true; }
bool UnrealReadBoolProperty(void* p,const wchar_t* name,bool* out) { auto* o=Lookup(p); if(!o) return false;
  if(!std::wcscmp(name,L"bDeleteMe")) *out=o->dead;
  else if(o==&poi && !std::wcscmp(name,L"bFocusing")) *out=(Read(p,0x30)&1)!=0;
  else if(o==&camera && !std::wcscmp(name,L"bUseFovSettings")) *out=(Read(p,0x498)&2)!=0;
  else return false;
  if(bad_bool && o==&poi) *out=!*out;
  return true;
}
bool UnrealReadArrayProperty(void* p,const wchar_t* name,UnrealArrayView* out) {
  uint32_t off=0;
  if(p==handler.bytes && !std::wcscmp(name,L"ContextualCommands")) off=0x28;
  else if(p==camera.bytes && !std::wcscmp(name,L"PlayerCameraStates")) off=0x49C;
  else return false;
  std::memcpy(out,static_cast<uint8_t*>(p)+off,12); return true;
}
bool UnrealReadStructArrayProperty(void* p,const wchar_t* name,UnrealStructArrayView* out) {
  if(p!=handler.bytes || std::wcscmp(name,L"UINotifications")) return false;
  UnrealArrayView a{}; std::memcpy(&a,handler.bytes+0x34,12); out->data=a.data; out->count=a.count; out->capacity=a.capacity; out->element_size=8; return true;
}
}
int main(int argc,char** argv) {
  image=static_cast<uint8_t*>(VirtualAlloc(nullptr,0x2300000,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE)); assert(image);
  std::vector<uint8_t> exe;
  if(argc>1) { std::ifstream f(argv[1],std::ios::binary); exe.assign(std::istreambuf_iterator<char>(f),{}); assert(exe.size()==37667328); }
  poi_code::Visit([&](uintptr_t va,const auto& code,std::initializer_list<size_t> operands) {
    if(!exe.empty()) assert(!std::memcmp(exe.data()+va-0x10900C00,code,sizeof(code)));
    const auto relocated=native_reference_code::Relocate(code,reinterpret_cast<uintptr_t>(image),operands);
    std::memcpy(At(va),relocated.data(),relocated.size()); return true;
  });
  assert(BindPoiImage(reinterpret_cast<HMODULE>(image)));
  poi_code::Visit([&](uintptr_t va,const auto& code,std::initializer_list<size_t>) {
    for(size_t i=0;i<sizeof(code);++i) { At(va)[i]^=1; assert(!BindPoiImage(reinterpret_cast<HMODULE>(image))); At(va)[i]^=1; } return true;
  });
  Jump(0x116FC820,reinterpret_cast<void*>(&Barrier)); Jump(0x11011B24,reinterpret_cast<void*>(&std::memcpy));
  for(unsigned i=0;i<4000;++i) {
    Reset((i&1)!=0,(i&2)!=0); PoiCapture before,after;
    assert(CapturePoi(pc.bytes,pawn.bytes,poi.bytes,&before)); const auto p=pc,pw=pawn,s=strategy;
    assert(Apply()==NativePlayerServiceResult::kApplied && barriers==2);
    assert(CapturePoi(pc.bytes,pawn.bytes,poi.bytes,&after) && PoiResult(before,after));
    assert(!Read(poi.bytes,0x28) && !Read(command.bytes,0x34) && !(Read(poi.bytes,0x30)&1) && !(Read(camera.bytes,0x498)&2));
    assert(Read(handler.bytes,0x2C)==2 && Read(camera.bytes,0x4A0)==2 && Read(handler.bytes,0x38)==((i&2) ? 2u:3u));
    if(i&2) assert(allocations>0 && frees>0); // Notification capacity grows through shipping resize body.
    assert(Apply()==NativePlayerServiceResult::kApplied && barriers==4); // No duplicate UI remove notification.
    assert(!std::memcmp(pc.bytes,p.bytes,sizeof(p.bytes)) && !std::memcmp(pawn.bytes,pw.bytes,sizeof(pw.bytes)) && !std::memcmp(strategy.bytes,s.bytes,sizeof(s.bytes)));
  }
  // Zero-length array free paths, stale Command.POI after ordinary Remove,
  // and replacement class objects are all legitimate reset cases.
  Reset(false); Array(handler,0x28,{&command},false,1); Array(handler,0x34,{&command},true,1); Array(camera,0x49C,{&focus_class},false,1);
  assert(Apply()==NativePlayerServiceResult::kApplied && frees>=3);
  Reset(); Array(handler,0x28,{&child_command,&other_command}); assert(Apply()==NativePlayerServiceResult::kApplied);
  Reset(); bad_offset=true; Rejected(); Reset(); bad_bool=true; Rejected(); Reset(); refs=false; Rejected();
  Reset(); pc.bytes[0x66]=1; Rejected(); Reset(); world.bytes[0x39C]=3; Rejected();
  Reset(); Link(pawn,0x1D4,nullptr); Rejected(); Reset(); Link(pc,0x300,nullptr); Rejected();
  Reset(); Link(poi,0x2C,nullptr); Rejected(); Reset(); Link(strategy,0x30,nullptr); Rejected();
  Reset(); Link(camera,0x1C8,nullptr); Rejected(); Reset(); Put(At(0x12B6F460),0,0); Rejected();
  Reset(); Link(child_class,0x28,&child_class); Rejected(); Reset(); Put(command.bytes,0x14,0); Rejected();
  Reset(); Put(handler.bytes,0x2C,257); Rejected(); Reset(); Put(handler.bytes,0x3C,513); Rejected();
  Reset(); Put(At(0x12B00204),0,0); Rejected(); Reset(); Put(allocator_table,12,0); Rejected();
  Reset(); pc.dead=true; Rejected(); Reset(); command.type=L"UnknownCommandOverride"; Rejected();
  for(auto* o:objects) { Reset(); o->valid=false; Rejected(); }
  Reset(); g_poi_busy=true; Rejected(); g_poi_busy=false;
  Reset(); HANDLE thread=CreateThread(nullptr,0,WrongThread,nullptr,0,nullptr); assert(thread);
  assert(WaitForSingleObject(thread,10000)==WAIT_OBJECT_0); CloseHandle(thread);
  for(unsigned d=1;d<=7;++d) { Reset(); dirty=d; assert(Apply()==NativePlayerServiceResult::kStateMismatch && !g_poi_busy); }
  Reset(); assert(!PrepareNativePointOfInterest(reinterpret_cast<HMODULE>(image),GetCurrentThreadId(),pc.bytes,poi.bytes));
  ReleaseArray(handler,0x28); ReleaseArray(handler,0x34); ReleaseArray(camera,0x49C); VirtualFree(image,0,MEM_RELEASE);
  std::puts("NativePointOfInterestTests passed: 4000 native resets and repeat calls, arrays/heap growth and shrink, focus/FOV, command notifications, signatures, guards and dirty-state checks");
}
