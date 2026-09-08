namespace {
UnrealObjectCloneArray clone_journal;
std::array<void*,64> clone_pool{};
void* clone_source=nullptr;
unsigned clone_fault=0;
UnrealObjectCloneProperty scalar_clone_journal;
void* scalar_owner=nullptr;
void* scalar_field=nullptr;
unsigned scalar_fault=0,scalar_clone_calls=0;
void* WFCDS_UNREAL_EXEC_CALL CloneScalarTestObject(void* source,void*,void* outer,uint32_t name) {
  assert(source==clone_source && outer==scalar_owner && name==0);
  assert(!UnrealObjectCloningReady());
  UnrealObjectCloneProperty nested;
  assert(!UnrealBeginObjectCloneProperty(outer,L"OwnerAnimator",source,&nested));
  ++scalar_clone_calls;
  void* result=clone_pool[0];
  Put(result,0x14,uint32_t{21 | (UnrealObjectIndex(outer)<<12)});
  barrier_slot=reinterpret_cast<void**>(static_cast<uint8_t*>(outer)+0x120);
  if(scalar_fault==1)return nullptr;
  if(scalar_fault==2)return source;
  if(scalar_fault==3)Put(result,0x14,uint32_t{21});
  if(scalar_fault==4)Put(result,0x14,uint32_t{20 | (UnrealObjectIndex(outer)<<12)});
  if(scalar_fault==5)*barrier_slot=clone_pool[1];
  if(scalar_fault==6)Put(outer,0x18,UnrealObjectIndex(outer)+1);
  if(scalar_fault==7)Put(scalar_field,0x40,classes[20]);
  return result;
}
DWORD WINAPI WrongScalarCloneThread(void*) {
  UnrealObjectCloneProperty nested;
  assert(!UnrealBeginObjectCloneProperty(scalar_owner,L"OwnerAnimator",clone_source,&nested));
  assert(!UnrealFinishObjectCloneProperty(&scalar_clone_journal,false));
  assert(!UnrealFinishObjectCloneProperty(&scalar_clone_journal,true));
  return 0;
}
void TestScalarObjectClones(void* owner) {
  scalar_owner=owner;scalar_field=MakeObject(L"OwnerAnimator",13,classes[20]);
  Put(scalar_field,0x30,uint32_t{1 | (4<<10) | (0x120<<20)});
  Put(scalar_field,0x40,classes[21]);
  Link(classes[20],scalar_field);ClearWorldReflectionCaches();
  auto** slot=reinterpret_cast<void**>(static_cast<uint8_t*>(owner)+0x120);
  g_clone_object=&CloneScalarTestObject;
  for(unsigned pass=0;pass<2000;++pass) {
    assert(!*slot && !scalar_clone_journal.pending);
    const auto before_calls=scalar_clone_calls;
    assert(UnrealBeginObjectCloneProperty(owner,L"OwnerAnimator",clone_source,&scalar_clone_journal));
    assert(scalar_clone_calls==before_calls+1 && *slot==clone_pool[0] && scalar_clone_journal.pending);
    assert(UnrealObjectClonePropertyPending(scalar_clone_journal));
    assert(!UnrealBeginObjectCloneProperty(owner,L"OwnerAnimator",clone_source,&scalar_clone_journal));
    UnrealObjectCloneProperty duplicate;
    assert(!UnrealBeginObjectCloneProperty(owner,L"OwnerAnimator",clone_source,&duplicate));
    // Copying the journal must not duplicate the right to remove its GC root.
    auto copied=scalar_clone_journal;
    assert(!UnrealObjectClonePropertyPending(copied));
    assert(!UnrealFinishObjectCloneProperty(&copied,false));
    assert(!UnrealFinishObjectCloneProperty(&copied,true));
    assert(scalar_clone_calls==before_calls+1 && *slot==clone_pool[0]);
    const bool commit=(pass&1)!=0;
    assert(UnrealFinishObjectCloneProperty(&scalar_clone_journal,commit));
    assert(!scalar_clone_journal.pending && *slot==(commit ? clone_pool[0]:nullptr));
    assert(!UnrealObjectClonePropertyPending(scalar_clone_journal));
    assert(!UnrealFinishObjectCloneProperty(&scalar_clone_journal,true));
    assert(!UnrealFinishObjectCloneProperty(&scalar_clone_journal,false));
    if(commit)assert(UnrealWriteObjectPropertyExpected(owner,L"OwnerAnimator",clone_pool[0],nullptr));
  }
  const auto owner_index=UnrealObjectIndex(owner);
  for(scalar_fault=1;scalar_fault<=7;++scalar_fault) {
    assert(!UnrealBeginObjectCloneProperty(owner,L"OwnerAnimator",clone_source,&scalar_clone_journal));
    assert(!scalar_clone_journal.pending);
    assert(*slot==(scalar_fault==5 ? clone_pool[1]:nullptr));
    // Fixture resets injected native callback mutations, never production recovery.
    *slot=nullptr;Put(owner,0x18,owner_index);Put(scalar_field,0x40,classes[21]);ClearWorldReflectionCaches();
  }
  scalar_fault=0;
  assert(UnrealBeginObjectCloneProperty(owner,L"OwnerAnimator",clone_source,&scalar_clone_journal));
  HANDLE thread=CreateThread(nullptr,0,WrongScalarCloneThread,nullptr,0,nullptr);
  assert(thread && WaitForSingleObject(thread,10000)==WAIT_OBJECT_0);CloseHandle(thread);
  const auto clone_index=UnrealObjectIndex(clone_pool[0]);
  Put(clone_pool[0],0x18,clone_index+1);
  assert(!UnrealFinishObjectCloneProperty(&scalar_clone_journal,false) && scalar_clone_journal.pending);
  Put(clone_pool[0],0x18,clone_index);
  *slot=clone_pool[1];
  assert(!UnrealFinishObjectCloneProperty(&scalar_clone_journal,false) && scalar_clone_journal.pending);
  *slot=clone_pool[0];
  assert(UnrealFinishObjectCloneProperty(&scalar_clone_journal,false) && !*slot);
  const auto before_calls=scalar_clone_calls;
  Put(scalar_field,0x40,classes[20]);ClearWorldReflectionCaches();
  assert(!UnrealBeginObjectCloneProperty(owner,L"OwnerAnimator",clone_source,&scalar_clone_journal));
  Put(scalar_field,0x40,classes[21]);
  Put(scalar_field,0x30,uint32_t{2 | (4<<10) | (0x120<<20)});ClearWorldReflectionCaches();
  assert(!UnrealBeginObjectCloneProperty(owner,L"OwnerAnimator",clone_source,&scalar_clone_journal));
  assert(scalar_clone_calls==before_calls);
  Put(scalar_field,0x30,uint32_t{1 | (4<<10) | (0x120<<20)});ClearWorldReflectionCaches();
  // A template derived from the property's declared type is supported too.
  Put(classes[21],0x28,classes[5]);Put(scalar_field,0x40,classes[5]);ClearWorldReflectionCaches();
  assert(UnrealBeginObjectCloneProperty(owner,L"OwnerAnimator",clone_source,&scalar_clone_journal));
  assert(UnrealFinishObjectCloneProperty(&scalar_clone_journal,false));
}
void* WFCDS_UNREAL_EXEC_CALL CloneTestObject(void* source,void*,void* outer,uint32_t name) {
  assert(source==clone_source && outer==clone_journal.owner && name==0);
  assert(!UnrealObjectCloningReady());
  // Earlier entries must be rooted before the next allocating engine call.
  assert(CloneArrayMatches(clone_journal));
  void* result=clone_pool[clone_journal.staged.count];
  Put(result,0x14,uint32_t{21 | (UnrealObjectIndex(outer)<<12)});
  barrier_slot=static_cast<void**>(clone_journal.staged.data)+clone_journal.staged.count;
  if (clone_fault==1) return nullptr;
  if (clone_fault==2) return source;
  if (clone_fault==3) Put(result,0x14,uint32_t{21});
  if (clone_fault==4) Put(result,0x14,uint32_t{20 | (UnrealObjectIndex(outer)<<12)});
  if (clone_fault==5) return clone_pool[0];
  if (clone_fault==6) ++clone_journal.destination->count;
  return result;
}
DWORD WINAPI WrongCloneThread(void*) {
  void* result=nullptr;
  assert(!UnrealObjectCloningReady());
  assert(!UnrealAppendObjectClone(&clone_journal,clone_source,&result));
  assert(!UnrealFinishObjectCloneArray(&clone_journal,false));
  return 0;
}
void TestObjectClones(const char* exe) {
  if (exe) {
    std::ifstream file(exe,std::ios::binary);assert(file);
    const auto verify=[&](uintptr_t va,const uint8_t* expected,size_t size){
      std::array<uint8_t,256> actual{};assert(size<=actual.size());
      file.seekg(va-0x10900C00u);file.read(reinterpret_cast<char*>(actual.data()),size);
      assert(file && !std::memcmp(actual.data(),expected,size));
    };
    verify(native_clone_code::kCloneVa,native_clone_code::kClone,sizeof(native_clone_code::kClone));
    verify(native_clone_code::kArrayAllocationVa,native_clone_code::kArrayAllocation,sizeof(native_clone_code::kArrayAllocation));
    // Verify every relocation operand against PE base relocation records, not
    // a guessed list of numbers that merely resemble module addresses.
    const auto check_relocations=[&](uintptr_t va,size_t size,std::initializer_list<size_t> offsets){
      std::array<bool,256> found{};size_t consumed=0;
      // Data-directory length excludes the section's trailing padding/data.
      while (consumed<0x243ABCu) {
        uint32_t page=0,bytes=0;file.seekg(0x2148A00u+consumed);
        file.read(reinterpret_cast<char*>(&page),4);file.read(reinterpret_cast<char*>(&bytes),4);
        assert(file && bytes>=8 && bytes<=0x243ABCu-consumed);
        for (uint32_t n=8;n<bytes;n+=2) {
          uint16_t entry=0;file.read(reinterpret_cast<char*>(&entry),2);assert(file);
          const uintptr_t address=0x10900000u+page+(entry&0xFFFu);
          if ((entry>>12)==3 && address>=va && address<va+size) found[address-va]=true;
        }
        consumed+=bytes;
      }
      for (auto off:offsets) { assert(found[off]);found[off]=false; }
      for (bool f:found) assert(!f);
    };
    check_relocations(native_clone_code::kCloneVa,sizeof(native_clone_code::kClone),{37,88,105});
    check_relocations(native_clone_code::kArrayAllocationVa,sizeof(native_clone_code::kArrayAllocation),{21,38,95,101,116,146,182,197});
  }
  classes[19]=MakeObject(L"StructProperty",1);
  // Real nested float-field reader: strict scalar metadata and bounds, but
  // no write access is required for borrowed/read-only weapon templates.
  void* range_definition=MakeObject(L"FloatRange",3);Put(range_definition,0x34,uint32_t{8});
  void* minimum=MakeObject(L"Min",16,range_definition);
  Put(minimum,0x30,uint32_t{1 | (4<<10)});Link(range_definition,minimum);
  auto* range_values=static_cast<float*>(VirtualAlloc(nullptr,4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
  assert(range_values);range_values[0]=1.25f;range_values[1]=4;
  UnrealStructValueView range={range_values,8,range_definition};float result_float=0;
  assert(UnrealReadStructFloatField(range,L"Min",&result_float) && result_float==1.25f);
  DWORD protection=0;assert(VirtualProtect(range_values,4096,PAGE_READONLY,&protection));
  assert(UnrealReadStructFloatField(range,L"Min",&result_float) && result_float==1.25f);
  for(auto layout:{uint32_t{2 | (4<<10)},uint32_t{1 | (8<<10)},uint32_t{1 | (4<<10) | (8<<20)},uint32_t{1 | (4<<10) | (1<<20)}}){
    Put(minimum,0x30,layout);ClearWorldReflectionCaches();
    result_float=99;assert(!UnrealReadStructFloatField(range,L"Min",&result_float) && result_float==0);
  }
  Put(minimum,0x30,uint32_t{1 | (4<<10)});Put(minimum,0x14,uint32_t{11});ClearWorldReflectionCaches();
  assert(!UnrealReadStructFloatField(range,L"Min",&result_float));
  Put(minimum,0x14,uint32_t{16});ClearWorldReflectionCaches();
  range.size=4;assert(!UnrealReadStructFloatField(range,L"Min",&result_float));
  assert(VirtualFree(range_values,0,MEM_RELEASE));
  classes[20]=MakeObject(L"CloneHeat",1);
  classes[21]=MakeObject(L"CloneModifier",1);
  void* owner=MakeObject(L"CloneOwner",20);
  clone_source=MakeObject(L"CloneTemplate",21);
  void* definition=MakeObject(L"CloneProperties",3);
  Put(definition,0x34,uint32_t{16});
  void* property=MakeObject(L"HeatProperties",19,classes[20]);
  Put(property,0x30,uint32_t{1 | (16<<10) | (0x100<<20)});Put(property,0x40,definition);Link(classes[20],property);
  void* field=MakeObject(L"Modifiers",9,definition);
  void* inner=MakeObject(L"CloneElement",13);
  Put(inner,0x30,uint32_t{1 | (4<<10)});
  Put(field,0x30,uint32_t{1 | (12<<10) | (4<<20)});Put(field,0x40,inner);Link(definition,field);
  for (auto& p:clone_pool) p=MakeObject(L"CloneInstance",21,owner);
  auto* header=reinterpret_cast<UnrealArrayView*>(static_cast<uint8_t*>(owner)+0x104);
  g_clone_object=&CloneTestObject;g_clone_allocation_verified=true;
  g_array_resize_allocation=&ResizeTestArray;g_reference_write_thread=GetCurrentThreadId();
  g_object_reference_barrier=&TestReferenceBarrier;assert(UnrealObjectCloningReady());
  for (unsigned pass=0;pass<2000;++pass) {
    const int count=(pass&1) ? 64:5;
    assert(UnrealBeginObjectCloneArray(owner,L"HeatProperties",L"Modifiers",count,&clone_journal));
    assert(!UnrealBeginObjectCloneArray(owner,L"HeatProperties",L"Modifiers",count,&clone_journal));
    // The staged array has count zero here. A second transaction must not
    // mistake its backing block for an unused original allocation.
    UnrealObjectCloneArray duplicate;
    const auto staged=*header;
    assert(!UnrealBeginObjectCloneArray(owner,L"HeatProperties",L"Modifiers",count,&duplicate));
    assert(!std::memcmp(header,&staged,sizeof(staged)) && live_allocations==1);
    auto copied=clone_journal;
    void* copied_result=nullptr;
    assert(!UnrealAppendObjectClone(&copied,clone_source,&copied_result) && !copied_result);
    assert(!UnrealFinishObjectCloneArray(&copied,false));
    for (int n=0;n<count;++n) {
      void* result=nullptr;
      assert(UnrealAppendObjectClone(&clone_journal,clone_source,&result) && result==clone_pool[n]);
      assert(header->count==n+1 && CloneArrayMatches(clone_journal));
    }
    copied=clone_journal;
    assert(!UnrealFinishObjectCloneArray(&copied,true));
    assert(!UnrealFinishObjectCloneArray(&copied,false));
    assert(UnrealFinishObjectCloneArray(&clone_journal,(pass&1)!=0));
    assert(!clone_journal.pending);
    assert(!UnrealFinishObjectCloneArray(&copied,true));
    assert(!UnrealFinishObjectCloneArray(&copied,false));
    if (pass&1) { assert(header->count==count);g_array_resize_allocation(header,nullptr,count,0,4);*header={}; }
    assert(!header->data && live_allocations==0);
  }
  // Failed clone/invalid returned object/duplicate never replace old entries.
  for (clone_fault=1;clone_fault<=6;++clone_fault) {
    assert(UnrealBeginObjectCloneArray(owner,L"HeatProperties",L"Modifiers",2,&clone_journal));
    const auto fault=clone_fault;clone_fault=0;void* result=nullptr;
    assert(UnrealAppendObjectClone(&clone_journal,clone_source,&result));clone_fault=fault;
    assert(!UnrealAppendObjectClone(&clone_journal,clone_source,&result) && !result);
    if (fault==6) {
      assert(!UnrealFinishObjectCloneArray(&clone_journal,false));
      UnrealObjectCloneArray recovery_conflict;
      assert(!UnrealBeginObjectCloneArray(owner,L"HeatProperties",L"Modifiers",2,&recovery_conflict));
      *header=clone_journal.staged;
    }
    assert(UnrealFinishObjectCloneArray(&clone_journal,false) && !header->data && live_allocations==0);
  }
  clone_fault=0;fail_resize_after=0;
  assert(!UnrealBeginObjectCloneArray(owner,L"HeatProperties",L"Modifiers",5,&clone_journal));
  assert(!clone_journal.pending && !header->data && !live_allocations);fail_resize_after=-1;
  // Original nonzero empty allocation survives rollback and is freed on commit.
  g_array_resize_allocation(header,nullptr,0,8,4);header->capacity=8;
  void* old=header->data;
  assert(UnrealBeginObjectCloneArray(owner,L"HeatProperties",L"Modifiers",1,&clone_journal));
  assert(!UnrealFinishObjectCloneArray(&clone_journal,true));
  HANDLE t=CreateThread(nullptr,0,WrongCloneThread,nullptr,0,nullptr);
  assert(t && WaitForSingleObject(t,10000)==WAIT_OBJECT_0);CloseHandle(t);
  assert(UnrealFinishObjectCloneArray(&clone_journal,false) && header->data==old && live_allocations==1);
  assert(UnrealBeginObjectCloneArray(owner,L"HeatProperties",L"Modifiers",1,&clone_journal));
  void* result=nullptr;assert(UnrealAppendObjectClone(&clone_journal,clone_source,&result));
  auto stale_commit=clone_journal;
  assert(UnrealFinishObjectCloneArray(&clone_journal,true) && header->data!=old && live_allocations==1);
  // Original backing storage was non-null and has just been freed. The copy
  // still matches the committed live array, but must not free original twice.
  assert(!UnrealFinishObjectCloneArray(&stale_commit,true) && live_allocations==1);
  assert(!UnrealFinishObjectCloneArray(&stale_commit,false) && header->count==1);
  assert(!UnrealBeginObjectCloneArray(owner,L"HeatProperties",L"Modifiers",1,&clone_journal));
  g_array_resize_allocation(header,nullptr,header->count,0,4);*header={};assert(!live_allocations);
  g_clone_object=nullptr;assert(!UnrealObjectCloningReady());g_clone_object=&CloneTestObject;
  g_clone_allocation_verified=false;assert(!UnrealObjectCloningReady());g_clone_allocation_verified=true;
  assert(!UnrealBeginObjectCloneArray(owner,L"HeatProperties",L"Modifiers",65,&clone_journal));
  Put(property,0x30,uint32_t{2 | (16<<10) | (0x100<<20)});ClearWorldReflectionCaches();
  assert(!UnrealBeginObjectCloneArray(owner,L"HeatProperties",L"Modifiers",5,&clone_journal));
  Put(property,0x30,uint32_t{1 | (16<<10) | (0x100<<20)});ClearWorldReflectionCaches();
  Put(field,0x30,uint32_t{2 | (12<<10) | (4<<20)});ClearWorldReflectionCaches();
  assert(!UnrealBeginObjectCloneArray(owner,L"HeatProperties",L"Modifiers",5,&clone_journal));
  Put(field,0x30,uint32_t{1 | (12<<10) | (4<<20)});ClearWorldReflectionCaches();
  assert(UnrealBeginObjectCloneArray(owner,L"HeatProperties",L"Modifiers",1,&clone_journal));
  assert(UnrealAppendObjectClone(&clone_journal,clone_source,&result));
  const auto id=UnrealObjectIndex(result);Put(result,0x18,id+1);
  assert(!UnrealFinishObjectCloneArray(&clone_journal,false) && clone_journal.pending);
  Put(result,0x18,id);assert(UnrealFinishObjectCloneArray(&clone_journal,false) && !live_allocations);
  Put(inner,0x14,uint32_t{10});ClearWorldReflectionCaches();
  assert(!UnrealBeginObjectCloneArray(owner,L"HeatProperties",L"Modifiers",5,&clone_journal));
  Put(inner,0x14,uint32_t{13});ClearWorldReflectionCaches();
  TestScalarObjectClones(owner);
}
}
