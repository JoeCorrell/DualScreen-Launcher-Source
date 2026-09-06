namespace {
UnrealObjectCloneArray clone_journal;
std::array<void*,64> clone_pool{};
void* clone_source=nullptr;
unsigned clone_fault=0;
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
    for (int n=0;n<count;++n) {
      void* result=nullptr;
      assert(UnrealAppendObjectClone(&clone_journal,clone_source,&result) && result==clone_pool[n]);
      assert(header->count==n+1 && CloneArrayMatches(clone_journal));
    }
    assert(UnrealFinishObjectCloneArray(&clone_journal,(pass&1)!=0));
    assert(!clone_journal.pending);
    if (pass&1) { assert(header->count==count);g_array_resize_allocation(header,nullptr,count,0,4);*header={}; }
    assert(!header->data && live_allocations==0);
  }
  // Failed clone/invalid returned object/duplicate never replace old entries.
  for (clone_fault=1;clone_fault<=6;++clone_fault) {
    assert(UnrealBeginObjectCloneArray(owner,L"HeatProperties",L"Modifiers",2,&clone_journal));
    const auto fault=clone_fault;clone_fault=0;void* result=nullptr;
    assert(UnrealAppendObjectClone(&clone_journal,clone_source,&result));clone_fault=fault;
    assert(!UnrealAppendObjectClone(&clone_journal,clone_source,&result) && !result);
    if (fault==6) { assert(!UnrealFinishObjectCloneArray(&clone_journal,false));*header=clone_journal.staged; }
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
  assert(UnrealFinishObjectCloneArray(&clone_journal,true) && header->data!=old && live_allocations==1);
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
}
}
