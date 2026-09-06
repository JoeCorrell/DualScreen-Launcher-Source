namespace {
bool role_reference_ready=true;
unsigned role_fault=0,role_writes=0;
NativeWeaponRoleData role_plan{};
}
namespace wfcds {
bool UnrealObjectReferenceWritesReady(){return role_reference_ready;}
bool UnrealResolveBytePropertyOffset(void* p,const wchar_t* field,uint32_t* out){
  if(p!=weapon.bytes || std::wcscmp(field,L"WeaponDataType"))return false;
  *out=bad_offset ? 0x328:0x329;return true;
}
bool UnrealWriteObjectPropertyExpected(void* p,const wchar_t* field,void* expected,void* value){
  assert(p==weapon.bytes && !std::wcscmp(field,L"Data"));void* actual=nullptr;
  std::memcpy(&actual,weapon.bytes+0x240,4);assert(actual==expected);
  assert((weapon.bytes[0x30]&8)!=0);
  assert(weapon.bytes[0x329]==(value==role_plan.target ? role_plan.desired_role:role_plan.previous_role));
  assert(NativeAssignWeaponRoleData(role_plan,false)==NativePlayerServiceResult::kRejected);
  if(role_fault==1)return false;
  std::memcpy(weapon.bytes+0x240,&value,4);++role_writes;
  if(role_fault==2)return false; // Simulate a failure reported after publication.
  if(role_fault==3)Link(pawn,0x1D4,&ai);
  if(role_fault==4)++new_data.index;
  if(role_fault==5)Float(lerper,0x34,0);
  return true;
}
}
namespace {
DWORD WINAPI WrongRoleThread(void*){assert(NativeAssignWeaponRoleData(role_plan,false)==NativePlayerServiceResult::kRejected);return 0;}
void TestRoleData(const char* path){
  auto* image=static_cast<uint8_t*>(VirtualAlloc(nullptr,0x1000000,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE));assert(image);
  const auto base=reinterpret_cast<uintptr_t>(image);
  auto* role=image+(native_weapon_role_code::kRoleVa-native_reference_code::kImageBase);
  auto* data=image+(native_weapon_role_code::kDataVa-native_reference_code::kImageBase);
  std::memcpy(role,native_weapon_role_code::kRole,sizeof(native_weapon_role_code::kRole));
  const auto relocated=native_reference_code::Relocate(native_weapon_role_code::kData,base,{24,47});
  std::memcpy(data,relocated.data(),relocated.size());
  if(path){
    std::ifstream f(path,std::ios::binary);assert(f);std::array<uint8_t,69> code{};
    f.seekg(native_weapon_role_code::kRoleVa-0x10900C00);f.read(reinterpret_cast<char*>(code.data()),sizeof(native_weapon_role_code::kRole));
    assert(f && !std::memcmp(code.data(),native_weapon_role_code::kRole,sizeof(native_weapon_role_code::kRole)));
    f.seekg(native_weapon_role_code::kDataVa-0x10900C00);f.read(reinterpret_cast<char*>(code.data()),code.size());
    assert(f && !std::memcmp(code.data(),native_weapon_role_code::kData,code.size()));
  }
  assert(RoleDataImage(reinterpret_cast<HMODULE>(image)));
  for(size_t i=0;i<sizeof(native_weapon_role_code::kRole);++i){role[i]^=1;assert(!RoleDataImage(reinterpret_cast<HMODULE>(image)));role[i]^=1;}
  for(size_t i=0;i<sizeof(native_weapon_role_code::kData);++i){data[i]^=1;assert(!RoleDataImage(reinterpret_cast<HMODULE>(image)));data[i]^=1;}
  const auto prepare=[&](){
    Reset();role_fault=role_writes=0;role_reference_ready=true;role_plan={};
    Link(weapon,0x248,&old_data);Link(weapon,0x244,&new_data);
    const uint32_t packed=0xAABB02CC,flags=0xA5A50040;
    std::memcpy(weapon.bytes+0x328,&packed,4);std::memcpy(weapon.bytes+0x30,&flags,4);
    assert(NativeCaptureWeaponRoleData(reinterpret_cast<HMODULE>(image),GetCurrentThreadId(),pawn.bytes,weapon.bytes,1,&role_plan));
  };
  for(unsigned i=0;i<3000;++i){
    prepare();const auto w=weapon,l=lerper,p=pawn;
    assert(NativeAssignWeaponRoleData(role_plan,false)==NativePlayerServiceResult::kApplied && role_writes==1);
    assert(weapon.bytes[0x329]==1 && weapon.bytes[0x328]==0xCC && weapon.bytes[0x32A]==0xBB && weapon.bytes[0x32B]==0xAA);
    assert(NativeAssignWeaponRoleData(role_plan,false)==NativePlayerServiceResult::kApplied && role_writes==1);
    Link(pawn,0x1D4,&ai); // Other transaction stages may change the controller between calls.
    assert(NativeAssignWeaponRoleData(role_plan,true)==NativePlayerServiceResult::kApplied && role_writes==2);
    auto expected=w;expected.bytes[0x30]|=8;
    assert(!std::memcmp(expected.bytes,weapon.bytes,sizeof(w.bytes)) && !std::memcmp(l.bytes,lerper.bytes,sizeof(l.bytes)));
    Link(pawn,0x1D4,&pc);assert(!std::memcmp(p.bytes,pawn.bytes,sizeof(p.bytes)));
  }
  for(unsigned d=1;d<=5;++d){prepare();role_fault=d;
    assert(NativeAssignWeaponRoleData(role_plan,false)==NativePlayerServiceResult::kStateMismatch && !g_role_data_busy);
    role_fault=0;Link(pawn,0x1D4,&pc);new_data.index=role_plan.target_index;
    assert(NativeAssignWeaponRoleData(role_plan,true)==NativePlayerServiceResult::kApplied && weapon.bytes[0x329]==2);
  }
  prepare();role_reference_ready=false;assert(NativeAssignWeaponRoleData(role_plan,false)==NativePlayerServiceResult::kRejected && !role_writes);
  prepare();++weapon.index;assert(NativeAssignWeaponRoleData(role_plan,false)==NativePlayerServiceResult::kRejected && !role_writes);
  prepare();++new_data.index;assert(NativeAssignWeaponRoleData(role_plan,false)==NativePlayerServiceResult::kRejected && !role_writes);
  prepare();Link(weapon,0x244,&old_data);assert(NativeAssignWeaponRoleData(role_plan,false)==NativePlayerServiceResult::kRejected && !role_writes);
  prepare();weapon.bytes[0x329]=3;assert(NativeAssignWeaponRoleData(role_plan,false)==NativePlayerServiceResult::kRejected && !role_writes);
  prepare();bad_offset=true;assert(NativeAssignWeaponRoleData(role_plan,false)==NativePlayerServiceResult::kRejected && !role_writes);
  prepare();DWORD protection=0;assert(VirtualProtect(weapon.bytes,4096,PAGE_READONLY,&protection));
  assert(NativeAssignWeaponRoleData(role_plan,false)==NativePlayerServiceResult::kRejected && !role_writes);
  assert(VirtualProtect(weapon.bytes,4096,protection,&protection));
  prepare();HANDLE t=CreateThread(nullptr,0,WrongRoleThread,nullptr,0,nullptr);assert(t && WaitForSingleObject(t,10000)==WAIT_OBJECT_0);CloseHandle(t);
  g_role_data_image=nullptr;g_role_data_thread=0;VirtualFree(image,0,MEM_RELEASE);
  std::puts("Native weapon role/data assignment: 3000 guarded round trips, exact stores/GC-writer call, adjacent bits, mixed-header rollback, signature corruption and five dirty failures passed (metadata/reference-writer fixtures; NOT complete UpdateData or gameplay)");
}
}
