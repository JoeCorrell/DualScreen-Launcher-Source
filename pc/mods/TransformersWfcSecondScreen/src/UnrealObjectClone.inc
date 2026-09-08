// Included inside wfcds by UnrealBridge.cpp; private resolver/allocator access.
namespace {
thread_local bool g_object_clone_busy=false;
// An empty published staging array is otherwise indistinguishable from an
// unused native allocation. Serialize its lifetime, not just allocator calls:
// a second Begin must not adopt the first transaction's block as its original.
thread_local UnrealObjectCloneArray* g_active_array_clone=nullptr;
bool CloneWritable(const void* p,size_t size) {
  MEMORY_BASIC_INFORMATION m{};
  if (!p || !VirtualQuery(p,&m,sizeof(m)) || m.State!=MEM_COMMIT ||
      (m.Protect&(PAGE_GUARD|PAGE_NOACCESS))) return false;
  const auto off=reinterpret_cast<uintptr_t>(p)-reinterpret_cast<uintptr_t>(m.BaseAddress);
  return off<m.RegionSize && size<=m.RegionSize-off &&
      (m.Protect&(PAGE_READWRITE|PAGE_WRITECOPY|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY));
}
bool CloneArrayHeader(void* owner,const wchar_t* property,const wchar_t* name,
                      UnrealArrayView** header,void** definition) {
  UnrealStructValueView value{};UnrealArrayView array{};
  uint32_t property_offset=0;
  if (!UnrealReadStructProperty(owner,property,&value) ||
      !UnrealResolveStructPropertyOffset(owner,property,value.size,&property_offset) || (property_offset&3) ||
      value.data!=static_cast<uint8_t*>(owner)+property_offset ||
      !UnrealReadStructTypedArray(value,name,L"ObjectProperty",4,&array,64)) return false;
  void* field=FindStructField(value.struct_definition,name);
  if (!field || FieldArrayDim(field)!=1 || FieldElementSize(field)!=sizeof(UnrealArrayView)) return false;
  const auto offset=FieldOffset(field);
  if (offset==UINT32_MAX || (offset&3) || offset>value.size || sizeof(array)>value.size-offset) return false;
  auto* slot=reinterpret_cast<UnrealArrayView*>(
      const_cast<uint8_t*>(static_cast<const uint8_t*>(value.data))+offset);
  if (!CloneWritable(slot,sizeof(*slot)) || (array.capacity &&
      (!array.data || !CloneWritable(array.data,array.capacity*4))) ||
      (!array.capacity && array.data)) return false;
  *header=slot;*definition=value.struct_definition;return true;
}
bool CloneArrayMatches(const UnrealObjectCloneArray& j) {
  UnrealArrayView* slot=nullptr;void* definition=nullptr;
  if (!j.pending || g_active_array_clone!=&j || j.thread!=GetCurrentThreadId() ||
      !UnrealObjectValid(j.owner) || UnrealObjectIndex(j.owner)!=j.owner_index ||
      !UnrealObjectValid(j.definition) || UnrealObjectIndex(j.definition)!=j.definition_index ||
      j.staged.count<0 || j.staged.count>64 || j.staged.capacity<j.staged.count || j.staged.capacity>64 ||
      !CloneArrayHeader(j.owner,j.property.c_str(),j.field.c_str(),&slot,&definition) ||
      slot!=j.destination || definition!=j.definition || std::memcmp(slot,&j.staged,sizeof(*slot))) return false;
  for (int32_t i=0;i<j.staged.count;++i) {
    void* live=nullptr;std::memcpy(&live,static_cast<uint8_t*>(j.staged.data)+4*i,4);
    if (live!=j.objects[i] || !UnrealObjectValid(live) || UnrealObjectIndex(live)!=j.indices[i] ||
        UnrealObjectOuter(live)!=j.owner) return false;
  }
  return true;
}
struct CloneCallGuard {
  CloneCallGuard(){g_object_clone_busy=true;}
  ~CloneCallGuard(){g_object_clone_busy=false;}
};
bool ClonePropertySlot(void* owner,const wchar_t* property,void*** slot,
                       void** field,void** property_class) {
  if(!UnrealObjectValid(owner) || !property || !*property)return false;
  void* f=FindField(owner,property);
  if(!UnrealObjectValid(f) || !UnrealObjectIsA(f,L"ObjectProperty") ||
      FieldArrayDim(f)!=1 || FieldElementSize(f)!=sizeof(void*) ||
      !Readable(static_cast<uint8_t*>(f)+0x40,sizeof(void*)))return false;
  const auto offset=FieldOffset(f);
  if(offset==UINT32_MAX || offset<0x28 || (offset&3) ||
      !CloneWritable(static_cast<uint8_t*>(owner)+offset,sizeof(void*)))return false;
  void* type=nullptr;std::memcpy(&type,static_cast<uint8_t*>(f)+0x40,sizeof(type));
  if(!UnrealObjectValid(type) || !UnrealObjectIsA(type,L"Class"))return false;
  *slot=reinterpret_cast<void**>(static_cast<uint8_t*>(owner)+offset);
  *field=f;*property_class=type;return true;
}
bool CloneClassCompatible(void* type,void* property_class) {
  for(unsigned depth=0;type && depth<128;++depth) {
    if(!UnrealObjectValid(type) || !UnrealObjectIsA(type,L"Class"))return false;
    if(type==property_class)return true;
    type=ClassParent(type);
  }
  return false;
}
bool ClonePropertyMatches(const UnrealObjectCloneProperty& j,bool published) {
  if(j.journal_address!=&j || j.thread!=GetCurrentThreadId() ||
      !UnrealObjectValid(j.owner) || UnrealObjectIndex(j.owner)!=j.owner_index ||
      !UnrealObjectValid(j.field) || UnrealObjectIndex(j.field)!=j.field_index ||
      !UnrealObjectValid(j.property_class) || UnrealObjectIndex(j.property_class)!=j.property_class_index ||
      !UnrealObjectValid(j.source) || UnrealObjectIndex(j.source)!=j.source_index ||
      !UnrealObjectValid(j.source_class) || UnrealObjectIndex(j.source_class)!=j.source_class_index ||
      UnrealObjectClass(j.source)!=j.source_class ||
      !CloneClassCompatible(j.source_class,j.property_class))return false;
  void** slot=nullptr;void* field=nullptr;void* type=nullptr;
  if(!ClonePropertySlot(j.owner,j.property.data(),&slot,&field,&type) ||
      slot!=j.destination || field!=j.field || type!=j.property_class)return false;
  if(!published)return !j.pending && !*slot;
  return j.pending && j.clone && *slot==j.clone &&
      UnrealObjectValid(j.clone) && UnrealObjectIndex(j.clone)==j.clone_index &&
      UnrealObjectClass(j.clone)==j.source_class && UnrealObjectOuter(j.clone)==j.owner;
}
}
bool UnrealObjectCloningReady() {
  return !g_object_clone_busy && UnrealObjectReferenceWritesReady() &&
      g_clone_object && g_clone_allocation_verified && g_array_resize_allocation;
}
bool UnrealBeginObjectCloneArray(void* owner,const wchar_t* property,const wchar_t* field,
                                 int32_t capacity,UnrealObjectCloneArray* j) {
  if (!j || j->pending || g_active_array_clone || !UnrealObjectCloningReady() || !property || !field ||
      capacity<1 || capacity>64) return false;
  *j={};UnrealArrayView* slot=nullptr;void* definition=nullptr;
  if (!CloneArrayHeader(owner,property,field,&slot,&definition) || slot->count ||
      !UnrealObjectValid(owner) || !UnrealObjectIndex(owner) || !UnrealObjectIndex(definition)) return false;
  j->owner=owner;j->owner_index=UnrealObjectIndex(owner);
  j->definition=definition;j->definition_index=UnrealObjectIndex(definition);
  j->property=property;j->field=field;j->destination=slot;j->original=*slot;j->thread=GetCurrentThreadId();
  CloneCallGuard guard;
  // Independently allocate; never reallocate a published/live UObject array.
  g_array_resize_allocation(&j->staged,nullptr,0,capacity,4);
  if (!j->staged.data) { *j={};return false; }
  j->staged.capacity=capacity;
  UnrealArrayView* after=nullptr;void* after_definition=nullptr;
  if (!CloneWritable(j->staged.data,capacity*4) || !UnrealObjectValid(owner) ||
      UnrealObjectIndex(owner)!=j->owner_index || !UnrealObjectValid(definition) ||
      UnrealObjectIndex(definition)!=j->definition_index ||
      !CloneArrayHeader(owner,property,field,&after,&after_definition) || after!=slot || after_definition!=definition ||
      std::memcmp(slot,&j->original,sizeof(*slot))) {
    g_array_resize_allocation(&j->staged,nullptr,0,0,4);*j={};return false;
  }
  std::memset(j->staged.data,0,capacity*4);
  *slot=j->staged;j->pending=true;g_active_array_clone=j;return true;
}
bool UnrealAppendObjectClone(UnrealObjectCloneArray* j,void* source,void** output) {
  if (output) *output=nullptr;
  if (!output || !j || !UnrealObjectCloningReady() || !CloneArrayMatches(*j) ||
      j->staged.count==j->staged.capacity || !UnrealObjectValid(source)) return false;
  const auto source_index=UnrealObjectIndex(source);
  void* source_class=UnrealObjectClass(source);const auto class_index=UnrealObjectIndex(source_class);
  if (!source_index || !class_index || source==j->owner) return false;
  CloneCallGuard guard;
  // Core.Object.Clone: raw UObject in ECX; raw Outer and FName(None) on stack,
  // RET 8. No script-base +0x10 adjustment and no stripped ProcessEvent call.
  void* clone=g_clone_object(source,nullptr,j->owner,0);
  if (!CloneArrayMatches(*j) || !UnrealObjectValid(source) || UnrealObjectIndex(source)!=source_index ||
      !UnrealObjectValid(source_class) || UnrealObjectIndex(source_class)!=class_index ||
      UnrealObjectClass(source)!=source_class || clone==source || clone==j->owner ||
      !UnrealObjectValid(clone) || !UnrealObjectIndex(clone) || UnrealObjectClass(clone)!=source_class ||
      UnrealObjectOuter(clone)!=j->owner) return false;
  for (int32_t i=0;i<j->staged.count;++i) if (clone==j->objects[i]) return false;
  const auto n=j->staged.count;
  j->objects[n]=clone;j->indices[n]=UnrealObjectIndex(clone);
  std::memcpy(static_cast<uint8_t*>(j->staged.data)+4*n,&clone,4);
  ++j->staged.count;*j->destination=j->staged;
  // Publish the count/pointer before notifying GC, and before any subsequent
  // Clone/Initialize call. A C++ stack pointer alone is not an engine GC root.
  g_object_reference_barrier(clone);
  if (!CloneArrayMatches(*j)) return false;
  *output=clone;return true;
}
bool UnrealFinishObjectCloneArray(UnrealObjectCloneArray* j,bool commit) {
  if (!j || !UnrealObjectCloningReady() || !CloneArrayMatches(*j) ||
      (commit && j->staged.count!=j->staged.capacity)) return false;
  CloneCallGuard guard;
  UnrealArrayView released=commit ? j->original:j->staged;
  if (!commit) *j->destination=j->original;
  // The removed entries are engine UObjects, NOT heap allocations to delete.
  // Their collection is left to WFC; only our unowned backing block is freed.
  g_array_resize_allocation(&released,nullptr,released.count,0,4);
  g_active_array_clone=nullptr;*j={};return true;
}
bool UnrealBeginObjectCloneProperty(void* owner,const wchar_t* property,
                                    void* source,UnrealObjectCloneProperty* j) {
  if(!j || j->pending || !UnrealObjectCloningReady() || !property ||
      !UnrealObjectValid(source) || owner==source)return false;
  const size_t length=std::wcslen(property);
  if(!length || length>=j->property.size())return false;
  *j={};
  if(!ClonePropertySlot(owner,property,&j->destination,&j->field,&j->property_class) ||
      *j->destination)return false;
  j->owner=owner;j->owner_index=UnrealObjectIndex(owner);
  j->field_index=UnrealObjectIndex(j->field);
  j->property_class_index=UnrealObjectIndex(j->property_class);
  j->source=source;j->source_index=UnrealObjectIndex(source);
  j->source_class=UnrealObjectClass(source);j->source_class_index=UnrealObjectIndex(j->source_class);
  std::copy_n(property,length,j->property.data());
  j->journal_address=j;j->thread=GetCurrentThreadId();
  if(!ClonePropertyMatches(*j,false))return false;
  CloneCallGuard guard;
  void* clone=g_clone_object(source,nullptr,owner,0);
  // These checks do not dispatch engine code or allocate engine objects.
  // No subsequent allocating call occurs between Clone and reference publication.
  if(!ClonePropertyMatches(*j,false) || clone==source || clone==owner ||
      !UnrealObjectValid(clone) || UnrealObjectClass(clone)!=j->source_class ||
      UnrealObjectOuter(clone)!=owner)return false;
  if(InterlockedCompareExchangePointer(reinterpret_cast<void* volatile*>(j->destination),clone,nullptr))return false;
  j->clone=clone;j->clone_index=UnrealObjectIndex(clone);j->pending=true;
  g_object_reference_barrier(clone);
  return ClonePropertyMatches(*j,true);
}
bool UnrealFinishObjectCloneProperty(UnrealObjectCloneProperty* j,bool commit) {
  if(!j || !UnrealObjectCloningReady() || !ClonePropertyMatches(*j,true))return false;
  CloneCallGuard guard;
  if(!commit) {
    if(InterlockedCompareExchangePointer(reinterpret_cast<void* volatile*>(j->destination),nullptr,j->clone)!=j->clone)
      return false;
    g_object_reference_barrier(nullptr);
  }
  // On commit the typed property keeps owning the clone. On rollback only
  // that reference is removed; WFC's collector owns the UObject's lifetime.
  *j={};return true;
}
bool UnrealObjectClonePropertyPending(const UnrealObjectCloneProperty& j) {
  return UnrealObjectCloningReady() && ClonePropertyMatches(j,true);
}
