namespace {
NativeWeaponAmmo ammo_plan{};
void Int(Object& o,uint32_t off,int32_t v){std::memcpy(o.bytes+off,&v,4);}
int32_t IntValue(const Object& o,uint32_t off){int32_t v=0;std::memcpy(&v,o.bytes+off,4);return v;}
}
namespace wfcds {
bool UnrealReadIntProperty(void* p,const wchar_t* field,int32_t* out){
  auto* o=Lookup(p);if(!o)return false;uint32_t off=0;
  if(o==&weapon && !std::wcscmp(field,L"AmmoIncrease"))off=0x504;
  else if(o==&weapon && !std::wcscmp(field,L"AmmoCount"))off=0x508;
  else if(o==&weapon && !std::wcscmp(field,L"AmmoClipCount"))off=0x50C;
  else if((o==&old_data || o==&new_data) && !std::wcscmp(field,L"MaxAmmoCount"))off=0x500;
  else if((o==&old_data || o==&new_data) && !std::wcscmp(field,L"MaxAmmoClipCount"))off=0x504;
  else return false;
  *out=IntValue(*o,off);return true;
}
bool UnrealReadStructTypedArray(const UnrealStructValueView& v,const wchar_t* field,const wchar_t* type,uint32_t size,UnrealArrayView* out,uint32_t bound){
  if(v.data!=pawn.bytes+0x400 || std::wcscmp(field,L"Skills") || std::wcscmp(type,L"ClassProperty") || size!=4 || bound!=64)return false;
  *out=ammo_skills;return true;
}
bool UnrealReadObjectArrayElement(const UnrealArrayView& a,int32_t i,void** out){
  if(a.data!=ammo_skill_slots.data() || i<0 || i>=a.count || i>=64)return false;
  *out=ammo_skill_slots[i];return !*out || Lookup(*out);
}
bool UnrealResolveClassByPath(const wchar_t* name,void** out){
  *out=nullptr;
  if(!std::wcscmp(name,L"TransGame.TnSkillIncreaseAmmo"))*out=base_skill.bytes;
  else if(!std::wcscmp(name,L"TransGame.TnSkillExtraGrenade"))*out=grenade_skill.bytes;
  else if(!std::wcscmp(name,L"TransGame.TnSkillIncreaseAmmoFromClips"))*out=clips_skill.bytes;
  return *out!=nullptr;
}
bool UnrealWriteIntPropertyExpected(void* p,const wchar_t* field,int32_t expected,int32_t value){
  assert(p==weapon.bytes && !std::wcscmp(field,L"AmmoIncrease"));
  assert(NativeRetargetWeaponAmmo(ammo_plan,false)==NativePlayerServiceResult::kRejected);
  if(ammo_fault==1 || IntValue(weapon,0x504)!=expected)return false;
  Int(weapon,0x504,value);++ammo_writes;
  if(ammo_fault==2)++weapon.index;
  if(ammo_fault==3)Link(pawn,0x1D4,&ai);
  if(ammo_fault==4)Int(weapon,0x508,1);
  if(ammo_fault==5)Int(new_data,0x504,9);
  if(ammo_fault==6)ammo_skill_slots[0]=unknown_skill.bytes;
  if(ammo_fault==7)Float(lerper,0x34,0);
  if(ammo_fault==8)++clips_skill.index;
  return true;
}
}
namespace {
bool CaptureTestAmmo(){return NativeCaptureWeaponAmmo(GetCurrentThreadId(),pawn.bytes,weapon.bytes,new_data.bytes,&ammo_plan);}
void AmmoReset(Object* skill=&clips_skill,bool holstered=false){
  Reset();ammo_plan={};ammo_fault=ammo_writes=0;ammo_affected=true;ammo_skill_slots.fill(nullptr);
  ammo_skills={ammo_skill_slots.data(),1,64};ammo_skill_slots[0]=skill ? skill->bytes:nullptr;
  Link(weapon,0x500,skill);if(holstered)Link(weapon,0x94,nullptr);
  Int(weapon,0x504,6);Int(weapon,0x508,177);Int(weapon,0x50C,17);
  Int(new_data,0x500,120);Int(new_data,0x504,30);
}
void AmmoReject(){const auto w=weapon;
  assert(NativeRetargetWeaponAmmo(ammo_plan,false)==NativePlayerServiceResult::kRejected);
  assert(!std::memcmp(w.bytes,weapon.bytes,sizeof(w.bytes)));}
DWORD WINAPI WrongAmmoThread(void*){AmmoReject();return 0;}
void TestWeaponAmmo(){
  for(unsigned i=0;i<3000;++i){
    Object* skill=i%3==0 ? &base_skill:i%3==1 ? &grenade_skill:&clips_skill;
    const bool holstered=(i&4)!=0;AmmoReset(skill,holstered);assert(CaptureTestAmmo());
    const int desired=holstered ? 6:i%3==0 ? 0:i%3==1 ? 1:60;
    assert(ammo_plan.desired_increase==desired);Link(weapon,0x240,&new_data);
    // Both untouched custom Data assignment and stock already-computed bonus.
    if(i&8)Int(weapon,0x504,desired);
    const auto p=pawn,l=lerper;auto expected=weapon;Int(expected,0x504,desired);
    assert(NativeRetargetWeaponAmmo(ammo_plan,false)==NativePlayerServiceResult::kApplied);
    assert(!std::memcmp(expected.bytes,weapon.bytes,sizeof(weapon.bytes)));
    assert(NativeRetargetWeaponAmmo(ammo_plan,false)==NativePlayerServiceResult::kApplied);
    Link(weapon,0x240,&old_data);Link(pawn,0x1D4,&ai);
    assert(NativeRetargetWeaponAmmo(ammo_plan,true)==NativePlayerServiceResult::kApplied && IntValue(weapon,0x504)==6);
    assert(IntValue(weapon,0x508)==177 && IntValue(weapon,0x50C)==17);
    Link(pawn,0x1D4,&pc);assert(!std::memcmp(p.bytes,pawn.bytes,sizeof(p.bytes)) && !std::memcmp(l.bytes,lerper.bytes,sizeof(l.bytes)));
  }
  for(unsigned f=1;f<=8;++f){AmmoReset();assert(CaptureTestAmmo());Link(weapon,0x240,&new_data);ammo_fault=f;
    assert(NativeRetargetWeaponAmmo(ammo_plan,false)==(f==1 ? NativePlayerServiceResult::kRejected:NativePlayerServiceResult::kStateMismatch));
    assert(!g_weapon_ammo_busy);
  }
  AmmoReset(&unknown_skill);assert(!CaptureTestAmmo());
  AmmoReset();ammo_affected=false;assert(CaptureTestAmmo() && ammo_plan.desired_increase==6);
  AmmoReset();ammo_skills.count=0;assert(CaptureTestAmmo() && ammo_plan.desired_increase==6);
  AmmoReset();ammo_skills.count=ammo_skills.capacity=65;assert(!CaptureTestAmmo());
  AmmoReset();Int(new_data,0x504,std::numeric_limits<int32_t>::max());assert(!CaptureTestAmmo());
  AmmoReset();Int(new_data,0x500,std::numeric_limits<int32_t>::max());assert(!CaptureTestAmmo());
  AmmoReset();Int(new_data,0x504,-1);assert(!CaptureTestAmmo());
  AmmoReset();assert(CaptureTestAmmo());Link(weapon,0x240,&new_data);Int(weapon,0x504,999);AmmoReject();
  AmmoReset();assert(CaptureTestAmmo());Link(weapon,0x240,&new_data);Int(weapon,0x50C,16);AmmoReject();
  AmmoReset();assert(CaptureTestAmmo());Link(weapon,0x240,&new_data);++old_data.index;AmmoReject();
  AmmoReset();assert(CaptureTestAmmo());Link(weapon,0x240,&new_data);Link(weapon,0x94,nullptr);AmmoReject();
  AmmoReset();assert(CaptureTestAmmo());Link(weapon,0x240,&new_data);g_weapon_ammo_busy=true;AmmoReject();g_weapon_ammo_busy=false;
  HANDLE t=CreateThread(nullptr,0,WrongAmmoThread,nullptr,0,nullptr);assert(t && WaitForSingleObject(t,10000)==WAIT_OBJECT_0);CloseHandle(t);
  AmmoReset();assert(CaptureTestAmmo());NativeWeaponAmmo copy=ammo_plan;copy.ammo=1;copy.clip=2;
  assert(SameWeaponAmmoIdentity(ammo_plan,copy));++copy.skill_indices[0];assert(!SameWeaponAmmoIdentity(ammo_plan,copy));
  std::puts("Native weapon ammo: 3000 capacity/rollback cycles, three cooked skill paths, preserved magazines/reserve, overflow and eight failure guards passed (metadata/control/writer fixtures, not gameplay)");
}
}
