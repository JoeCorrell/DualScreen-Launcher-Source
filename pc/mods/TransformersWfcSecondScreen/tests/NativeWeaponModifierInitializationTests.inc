namespace wfcds {
bool NativeWeaponHeatInitializationStable(void* p,void* w){
  return p==pawn.bytes && w==weapon.bytes && control_valid && heat_owner==pawn.bytes && Get(heat,0x3C)==0;
}
// Real capacity validation is tested in HeatTests; this suite checks composition.
NativePlayerServiceResult NativeSeedWeaponHeatCapacity(void* p,void* w,void* source,bool rollback){
  assert(p==pawn.bytes && w==weapon.bytes && source==player_data.bytes);
  const float capacity=Get(heat,0x2C),target=Get(player_data,0x2C);
  if(!control_valid || array.count || heat_owner!=p || target<=0 ||
      (capacity!=0 && capacity!=target))return NativePlayerServiceResult::kRejected;
  if(rollback && seed_fault==1)return NativePlayerServiceResult::kRejected;
  Put(heat,0x2C,rollback ? 0:target);
  return !rollback && seed_fault==2 ? NativePlayerServiceResult::kStateMismatch:NativePlayerServiceResult::kApplied;
}
bool UnrealObjectCloningReady(){return cloning_ready;}
bool UnrealBeginObjectCloneArray(void* owner,const wchar_t* property,const wchar_t* field,int32_t count,UnrealObjectCloneArray* j){
  assert(owner==heat.bytes && !std::wcscmp(property,L"HeatProperties") && !std::wcscmp(field,L"Modifiers"));
  assert(!array.count && count>0 && count<=64 && !j->pending);
  j->owner=owner;j->owner_index=heat.index;j->original=array;
  j->staged={cloned_slots.data(),0,count};array=j->staged;j->pending=true;return true;
}
bool UnrealAppendObjectClone(UnrealObjectCloneArray* j,void* source,void** out){
  *out=nullptr;assert(j->pending && array.data==j->staged.data && array.count==j->staged.count);
  assert(g_modifiers_busy);NativeWeaponModifierInitialization nested;
  assert(NativeBeginWeaponModifierInitialization(GetCurrentThreadId(),pawn.bytes,weapon.bytes,player_data.bytes,&nested)==NativePlayerServiceResult::kRejected);
  ++clone_calls;if(clone_fault==1)return false;
  const int n=j->staged.count;auto& m=mods[n];const auto index=m.index;m=*Lookup(source);m.index=index;m.outer=heat.bytes;
  j->objects[n]=m.bytes;j->indices[n]=m.index;cloned_slots[n]=m.bytes;
  ++j->staged.count;array=j->staged;*out=m.bytes;
  if(clone_fault==3)context.objects[5]=other.bytes;
  if(clone_fault==4)Put(templates[0],0x30,99);
  if(clone_fault==5)m.type="UnknownModifier";
  if(clone_fault==6)m.outer=other.bytes;
  if(clone_fault==7)Put(heat,0x3C,1);
  return true;
}
bool UnrealFinishObjectCloneArray(UnrealObjectCloneArray* j,bool commit){
  if(!j->pending || array.data!=j->staged.data || array.count!=j->staged.count || array.capacity!=j->staged.capacity)return false;
  for(int i=0;i<array.count;++i)
    if(cloned_slots[i]!=j->objects[i] || !Lookup(j->objects[i]) || UnrealObjectIndex(j->objects[i])!=j->indices[i] || UnrealObjectOuter(j->objects[i])!=heat.bytes)return false;
  if(commit && array.count!=array.capacity)return false;
  if(!commit)array=j->original;
  *j={};return true;
}
bool UnrealWriteObjectPropertyExpected(void* p,const wchar_t* name,void* expected,void* value){
  auto* o=Lookup(p);
  if(!o || std::wcscmp(name,L"WeaponOwner") || o->owner!=expected || clone_fault==2)return false;
  o->owner=value;return true;
}
}
namespace {
NativePlayerServiceResult BeginInit(NativeWeaponModifierInitialization* j){
  return NativeBeginWeaponModifierInitialization(GetCurrentThreadId(),pawn.bytes,weapon.bytes,player_data.bytes,j);
}
DWORD WINAPI WrongInitThread(void*){NativeWeaponModifierInitialization j;assert(BeginInit(&j)==NativePlayerServiceResult::kRejected);return 0;}
void TestModifierInitialization(){
  for(unsigned pass=0;pass<2000;++pass){
    Reset(0);template_array.count=pass&1 ? 64:5;
    const bool seed=(pass&2)!=0;if(seed)Put(heat,0x2C,0);
    const auto original_array=array;const auto original_pawn=pawn;const auto original_weapon=weapon;const auto original_heat=heat;
    NativeWeaponModifierInitialization j;assert(BeginInit(&j)==NativePlayerServiceResult::kApplied && j.clones.pending);
    assert(j.capacity_seeded==seed && j.before.heat_maximum==(seed ? 0:100) && j.initialized.heat_maximum==100);
    assert(array.count==template_array.count && clone_calls==static_cast<unsigned>(array.count));
    for(int n=0;n<array.count;++n){
      assert(mods[n].owner==pawn.bytes && mods[n].outer==heat.bytes);
      assert(!std::memcmp(mods[n].bytes,templates[n].bytes,sizeof(mods[n].bytes)));
    }
    auto expected_heat=original_heat;Put(expected_heat,0x2C,100);
    assert(!std::memcmp(&pawn,&original_pawn,sizeof(pawn)) && !std::memcmp(&weapon,&original_weapon,sizeof(weapon)) &&
        !std::memcmp(&heat,&expected_heat,sizeof(heat)));
    const bool commit=(pass&1)!=0;
    assert(NativeFinishWeaponModifierInitialization(&j,commit)==NativePlayerServiceResult::kApplied && !j.clones.pending && !j.capacity_seeded);
    if(!commit)assert(array.data==original_array.data && array.count==0 && array.capacity==original_array.capacity &&
        !std::memcmp(&heat,&original_heat,sizeof(heat)));
    else {
      const auto originals=mods;NativeWeaponModifierInitialization again;
      assert(BeginInit(&again)==NativePlayerServiceResult::kApplied && !again.clones.pending && clone_calls==64);
      assert(!std::memcmp(originals.data(),mods.data(),sizeof(mods)));
    }
  }
  for(unsigned fault=1;fault<=6;++fault){
    Reset(0);clone_fault=fault;NativeWeaponModifierInitialization j;
    const auto result=BeginInit(&j);
    assert(result==(fault==3 || fault==4 || fault==6 ? NativePlayerServiceResult::kStateMismatch:NativePlayerServiceResult::kRejected));
    if(j.clones.pending){
      context.objects[5]=controller.bytes;Put(templates[0],0x30,4);clone_fault=0;
      for(auto& m:mods)m.outer=heat.bytes;
      assert(NativeFinishWeaponModifierInitialization(&j,false)==NativePlayerServiceResult::kApplied);
    }
    assert(array.data==slots.data() && array.count==0 && !g_modifiers_busy);
  }
  Reset();const auto original=mods;NativeWeaponModifierInitialization j;
  assert(BeginInit(&j)==NativePlayerServiceResult::kApplied && !clone_calls && !j.clones.pending);
  assert(!std::memcmp(original.data(),mods.data(),sizeof(mods)));
  Reset(0);cloning_ready=false;j={};assert(BeginInit(&j)==NativePlayerServiceResult::kRejected && !clone_calls);
  Reset(0);player_role_data=other.bytes;j={};assert(BeginInit(&j)==NativePlayerServiceResult::kRejected && !clone_calls);
  Reset(0);templates[0].type="UnknownModifier";j={};assert(BeginInit(&j)==NativePlayerServiceResult::kRejected && !clone_calls);
  Reset(0);templates[0].owner=pawn.bytes;j={};assert(BeginInit(&j)==NativePlayerServiceResult::kRejected && !clone_calls);
  Reset(0);heat_owner=nullptr;j={};assert(BeginInit(&j)==NativePlayerServiceResult::kRejected && !clone_calls);
  Reset(0);template_array.count=0;j={};assert(BeginInit(&j)==NativePlayerServiceResult::kApplied && !clone_calls);
  Reset();Put(mods[0],0x30,10);j={};assert(BeginInit(&j)==NativePlayerServiceResult::kRejected && !clone_calls);
  Reset(0);j={};assert(BeginInit(&j)==NativePlayerServiceResult::kApplied);
  heat_owner=other.bytes;assert(NativeFinishWeaponModifierInitialization(&j,false)==NativePlayerServiceResult::kStateMismatch && j.clones.pending);
  heat_owner=pawn.bytes;assert(NativeFinishWeaponModifierInitialization(&j,false)==NativePlayerServiceResult::kApplied);
  Reset(0);DWORD protection=0;assert(VirtualProtect(templates[2].bytes,4096,PAGE_READONLY,&protection));
  j={};assert(BeginInit(&j)==NativePlayerServiceResult::kApplied);
  assert(NativeFinishWeaponModifierInitialization(&j,false)==NativePlayerServiceResult::kApplied);
  assert(VirtualProtect(templates[2].bytes,4096,protection,&protection));
  Reset(0);HANDLE t=CreateThread(nullptr,0,WrongInitThread,nullptr,0,nullptr);assert(t && WaitForSingleObject(t,10000)==WAIT_OBJECT_0);CloseHandle(t);
  // Seed-only setup has rollback ownership even with no clone allocation.
  Reset(0);Put(heat,0x2C,0);template_array.count=0;j={};
  assert(BeginInit(&j)==NativePlayerServiceResult::kApplied && j.capacity_seeded && !j.clones.pending);
  assert(BeginInit(&j)==NativePlayerServiceResult::kRejected);
  assert(NativeFinishWeaponModifierInitialization(&j,false)==NativePlayerServiceResult::kApplied && Get(heat,0x2C)==0);
  // Allocation/clone failure cannot leave the previously zero service retuned.
  for(unsigned fault=1;fault<=6;++fault){
    Reset(0);Put(heat,0x2C,0);clone_fault=fault;j={};const auto result=BeginInit(&j);
    assert(result==(fault==3 || fault==4 || fault==6 ? NativePlayerServiceResult::kStateMismatch:NativePlayerServiceResult::kRejected));
    if(j.capacity_seeded || j.clones.pending){
      context.objects[5]=controller.bytes;Put(templates[0],0x30,4);clone_fault=0;for(auto& m:mods)m.outer=heat.bytes;
      assert(NativeFinishWeaponModifierInitialization(&j,false)==NativePlayerServiceResult::kApplied);
    }
    assert(Get(heat,0x2C)==0 && !j.capacity_seeded && !j.clones.pending && array.count==0);
  }
  Reset(0);Put(heat,0x2C,0);cloning_ready=false;j={};
  assert(BeginInit(&j)==NativePlayerServiceResult::kRejected && Get(heat,0x2C)==0 && !j.capacity_seeded);
  Reset();Put(player_data,0x2C,50);j={};assert(BeginInit(&j)==NativePlayerServiceResult::kRejected && Get(heat,0x2C)==100);
  Reset(0);Put(player_data,0x2C,0);Put(heat,0x2C,0);j={};assert(BeginInit(&j)==NativePlayerServiceResult::kRejected);
  Reset(0);Put(heat,0x2C,0);j={};assert(BeginInit(&j)==NativePlayerServiceResult::kApplied);
  seed_fault=1;
  assert(NativeFinishWeaponModifierInitialization(&j,false)==NativePlayerServiceResult::kStateMismatch &&
      !j.clones.pending && j.capacity_seeded && array.count==0 && Get(heat,0x2C)==100);
  assert(BeginInit(&j)==NativePlayerServiceResult::kRejected); // Do not erase the remaining seed journal.
  seed_fault=0;
  assert(NativeFinishWeaponModifierInitialization(&j,false)==NativePlayerServiceResult::kApplied && !j.capacity_seeded && Get(heat,0x2C)==0);
  Reset(0);Put(heat,0x2C,0);seed_fault=2;j={};
  assert(BeginInit(&j)==NativePlayerServiceResult::kStateMismatch && j.capacity_seeded && !j.clones.pending);
  seed_fault=0;
  assert(NativeFinishWeaponModifierInitialization(&j,false)==NativePlayerServiceResult::kApplied && Get(heat,0x2C)==0);
  Reset();j={};assert(BeginInit(&j)==NativePlayerServiceResult::kApplied);
  context.objects[5]=other.bytes;
  assert(NativeFinishWeaponModifierInitialization(&j,true)==NativePlayerServiceResult::kStateMismatch);
  Reset(0);Put(heat,0x2C,0);clone_fault=7;j={};
  assert(BeginInit(&j)==NativePlayerServiceResult::kStateMismatch && j.clones.pending && j.capacity_seeded);
  assert(Get(heat,0x3C)==1 && NativeFinishWeaponModifierInitialization(&j,false)==NativePlayerServiceResult::kStateMismatch);
  assert(Get(heat,0x3C)==1); // Never erase a heat event to force rollback through.
  Put(heat,0x3C,0);clone_fault=0;
  assert(NativeFinishWeaponModifierInitialization(&j,false)==NativePlayerServiceResult::kApplied && Get(heat,0x2C)==0);
}
}
