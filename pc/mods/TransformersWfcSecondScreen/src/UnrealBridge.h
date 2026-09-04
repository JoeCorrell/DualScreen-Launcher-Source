#pragma once

#include <windows.h>

#include <cstddef>
#include <cstdint>

namespace wfcds {

struct UnrealArrayView {
  void* data = nullptr;
  int32_t count = 0;
  int32_t capacity = 0;
};

struct UnrealStructArrayView {
  void* data = nullptr;
  int32_t count = 0;
  int32_t capacity = 0;
  uint32_t element_size = 0;
  void* struct_definition = nullptr;
};

struct UnrealStructValueView {
  const void* data = nullptr;
  uint32_t size = 0;
  void* struct_definition = nullptr;
};

// Stable identity captured for a process-wide script UFunction.  Handles are
// produced only by UnrealResolveExactScriptFunction and are revalidated before
// every exact invocation or executor patch.  Object indices use the same
// one-based convention returned by UnrealObjectIndex.
struct UnrealFunctionHandle {
  void* function = nullptr;
  // Immediate declaring UStruct (a UClass for ordinary functions or UState
  // for state-scoped functions).
  void* owner = nullptr;
  // UClass whose instances may execute the function. Equal to owner for an
  // ordinary class function and the owning class for a state function.
  void* context_class = nullptr;
  uint32_t function_object_index = 0;
  uint32_t owner_object_index = 0;
  uint32_t context_class_object_index = 0;
  uint32_t function_name_index = 0;
  uint64_t owner_path_hash = 0;
};

#if defined(_MSC_VER)
#define WFCDS_UNREAL_EXEC_CALL __fastcall
#elif defined(__GNUC__) && defined(__i386__)
#define WFCDS_UNREAL_EXEC_CALL __attribute__((fastcall))
#else
#define WFCDS_UNREAL_EXEC_CALL
#endif
// UFunction+0x68 executor ABI in the supported 32-bit TWFC build: Context is
// passed in ECX, the second register argument is unused EDX, followed by
// FFrame* and result storage on the stack. Use WFCDS_UNREAL_EXEC_CALL when
// declaring a replacement executor so the definition has this same ABI.
using UnrealFunctionExec = void(WFCDS_UNREAL_EXEC_CALL *)(
    void* context, void* unused_edx, void* frame, void* result);

struct UnrealFunctionExecDetour {
  UnrealFunctionHandle function = {};
  UnrealFunctionExec original_executor = nullptr;
  UnrealFunctionExec replacement_executor = nullptr;
  bool installed = false;
};

struct UnrealScriptObjectOperandPatch {
  UnrealFunctionHandle function = {};
  uint32_t opcode_offset = 0;
  uint8_t expected_opcode = 0;
  void* original_object = nullptr;
  void* replacement_object = nullptr;
  bool installed = false;
};

// Prevalidated reflected call shape used by executor detours that must forward
// frequently without repeating reflection, allocation, or memory-region
// queries on the hot path.
struct UnrealPreparedObjectStructStructCall {
  void* function = nullptr;
  uint16_t parameter_size = 0;
  uint16_t object_offset = 0;
  uint16_t struct_offset = 0;
  uint16_t return_offset = 0;
  uint16_t struct_size = 0;
  uint16_t return_size = 0;
  bool prepared = false;
};

bool InitializeUnrealBridge(HMODULE executable);
void ShutdownUnrealBridge();
bool UnrealBridgeReady();
// Starts a game-thread bridge pulse. This only clears short-lived memory-region
// validation state; reflected class metadata remains cached for the process.
void UnrealBeginGameThreadPulse();
// Drops reflected UObject metadata tied to the prior world while preserving
// the process-wide FName cache. Call only from the game thread at an epoch
// transition.
void UnrealResetWorldCaches();

int32_t UnrealObjectCount();
void* UnrealObjectAt(int32_t index);
bool UnrealObjectValid(void* object);
bool UnrealObjectIsA(void* object, const wchar_t* class_name);
uint32_t UnrealObjectIndex(void* object);
bool UnrealObjectName(void* object, char* output, size_t capacity);
bool UnrealObjectClassName(void* object, char* output, size_t capacity);
// Returns the live reflected UClass for a validated UObject. The caller must
// treat it as an engine-owned reference and revalidate the source object at
// each game-thread use.
void* UnrealObjectClass(void* object);
bool UnrealResolveClassInHierarchy(void* object, const wchar_t* class_name,
                                   void** output);

// Resolves a script UFunction by its exact dotted owner path and function
// name. If expected_object_index is non-zero, only that one-based GObjects
// slot is considered. Passing zero performs one bounded GObjects scan and
// captures the resolved runtime index in output.
bool UnrealResolveExactScriptFunction(uint32_t expected_object_index,
                                      const wchar_t* full_owner_path,
                                      const wchar_t* function_name,
                                      UnrealFunctionHandle* output);
// Resolves the exact declaring class through object's inheritance chain and
// walks only that class's child fields. This avoids a full GObjects scan when
// a live controller/pawn instance is already available.
bool UnrealResolveExactScriptFunctionForObject(
    void* object, const wchar_t* full_owner_path,
    const wchar_t* function_name, UnrealFunctionHandle* output);
bool UnrealExactScriptFunctionValid(const UnrealFunctionHandle& function);

// Executes the exact UFunction stored in the handle, bypassing most-derived
// name lookup. These are intentionally narrow call shapes needed by the
// Controller/PlayerController possession lifecycle.
bool UnrealCallExactVoid(void* object,
                         const UnrealFunctionHandle& function);
bool UnrealCallExactVoidObject(
    void* object, const UnrealFunctionHandle& function,
    const wchar_t* object_parameter, void* object_value);
bool UnrealCallExactVoidInt(
    void* object, const UnrealFunctionHandle& function,
    const wchar_t* int_parameter, int32_t int_value);
bool UnrealCallExactVoidString(
    void* object, const UnrealFunctionHandle& function,
    const wchar_t* string_parameter, const wchar_t* string_value);
bool UnrealCallExactVoidStruct(
    void* object, const UnrealFunctionHandle& function,
    const wchar_t* struct_parameter,
    const UnrealStructValueView& struct_value);
// Executes one exact struct-in/struct-out call into caller-owned parameter
// storage. The returned view remains valid only while that storage and any
// dynamic allocations produced by WFC remain retained by the caller. Invoke
// a given storage block only once; this bridge intentionally does not guess
// at UE3 script-struct destruction semantics.
bool UnrealCallExactStructResultPersistent(
    void* object, const UnrealFunctionHandle& function,
    const wchar_t* struct_parameter,
    const UnrealStructValueView& struct_value, void* parameter_storage,
    size_t parameter_storage_size, UnrealStructValueView* output);
bool UnrealCallExactStringStructResultPersistent(
    void* object, const UnrealFunctionHandle& function,
    const wchar_t* string_parameter, const wchar_t* string_value,
    void* parameter_storage, size_t parameter_storage_size,
    UnrealStructValueView* output);
// Executes an exact string-in/UObject-out call. The returned object is
// accepted only when it is still a live UE3 object after the synchronous
// invocation; no ownership is transferred to the caller.
bool UnrealCallExactStringObjectResult(
    void* object, const UnrealFunctionHandle& function,
    const wchar_t* string_parameter, const wchar_t* string_value,
    void** output);
// Executes one exact FString-in/bool-out UFunction. This is used for private
// native menu predicates whose result must not be confused with another
// same-named function in a derived class or state.
bool UnrealCallExactStringBoolResult(
    void* object, const UnrealFunctionHandle& function,
    const wchar_t* string_parameter, const wchar_t* string_value,
    bool* output);
bool UnrealCallExactVoidObjectBool(
    void* object, const UnrealFunctionHandle& function,
    const wchar_t* object_parameter, void* object_value,
    const wchar_t* bool_parameter, bool bool_value);
bool UnrealResolveExactObjectParameterOffset(
    const UnrealFunctionHandle& function, const wchar_t* parameter_name,
    uint32_t* output);
bool UnrealResolveExactIntParameterOffset(
    const UnrealFunctionHandle& function, const wchar_t* parameter_name,
    uint32_t* output);
bool UnrealResolveExactStringParameterOffset(
    const UnrealFunctionHandle& function, const wchar_t* parameter_name,
    uint32_t* output);
bool UnrealResolveExactStructParameterLayout(
    const UnrealFunctionHandle& function, const wchar_t* parameter_name,
    uint32_t expected_size, uint32_t* output_offset,
    void** output_struct_definition);
bool UnrealResolveExactStructReturnLayout(
    const UnrealFunctionHandle& function, uint32_t* output_size,
    void** output_struct_definition);
bool UnrealCallExactObjectStructStructResult(
    void* object, const UnrealFunctionHandle& function,
    const wchar_t* object_parameter, void* object_value,
    const wchar_t* struct_parameter, const UnrealStructValueView& struct_value,
    void* output, uint32_t output_size);
bool UnrealPrepareObjectStructStructCall(
    void* object, const UnrealFunctionHandle& function,
    const wchar_t* object_parameter, const wchar_t* struct_parameter,
    uint32_t struct_size, uint32_t return_size,
    UnrealPreparedObjectStructStructCall* output);
bool UnrealCallPreparedObjectStructStructResult(
    void* object, const UnrealPreparedObjectStructStructCall& call,
    void* object_value, const void* struct_value, void* output);

// Installs/restores a process-wide UFunction+0x68 executor detour. Calls must
// be serialized on the game thread. The original executor must already be
// initialized and executable so replacement code can safely delegate for
// contexts it does not own. ShutdownUnrealBridge restores every registered
// detour on a best-effort basis before dropping engine pointers.
bool UnrealInstallFunctionExecDetour(
    const UnrealFunctionHandle& function,
    UnrealFunctionExec replacement_executor,
    UnrealFunctionExecDetour* output);
bool UnrealRestoreFunctionExecDetour(UnrealFunctionExecDetour* detour);
bool UnrealRestoreAllFunctionExecDetours();

// Replaces the four-byte UObject operand immediately following one exact
// script opcode. This is intended for narrow, exact-build cast adapters; both
// install and restore verify the opcode, current operand, UFunction identity,
// Script bounds, and UObject identities before writing. Calls must be
// serialized on the game thread.
bool UnrealInstallScriptObjectOperandPatch(
    const UnrealFunctionHandle& function, uint32_t opcode_offset,
    uint8_t expected_opcode, void* expected_object, void* replacement_object,
    UnrealScriptObjectOperandPatch* output);
bool UnrealRestoreScriptObjectOperandPatch(
    UnrealScriptObjectOperandPatch* patch);
bool UnrealRestoreAllScriptObjectOperandPatches();

bool UnrealHasFunction(void* object, const wchar_t* function_name);
bool UnrealCanCallVoid(void* object, const wchar_t* function_name);
bool UnrealCanCallVoidInt(void* object, const wchar_t* function_name,
                          const wchar_t* parameter_name);

bool UnrealReadObjectProperty(void* object, const wchar_t* property_name,
                              void** output);
// Resolves and validates a reflected object-property offset for later raw
// reads on a hot exact-build path. Resolution itself is not a hot-path API.
bool UnrealResolveObjectPropertyOffset(void* object,
                                       const wchar_t* property_name,
                                       uint32_t* output);
bool UnrealResolveStructPropertyOffset(void* object,
                                       const wchar_t* property_name,
                                       uint32_t expected_size,
                                       uint32_t* output);
bool UnrealReadBoolProperty(void* object, const wchar_t* property_name,
                            bool* output);
bool UnrealReadIntProperty(void* object, const wchar_t* property_name,
                           int32_t* output);
bool UnrealReadByteProperty(void* object, const wchar_t* property_name,
                            uint8_t* output);
bool UnrealReadFloatProperty(void* object, const wchar_t* property_name,
                             float* output);
bool UnrealReadStringProperty(void* object, const wchar_t* property_name,
                               char* output, size_t capacity);
bool UnrealReadStringArrayElementProperty(void* object,
                                          const wchar_t* property_name,
                                          int32_t index, char* output,
                                          size_t capacity);
bool UnrealReadArrayProperty(void* object, const wchar_t* property_name,
                             UnrealArrayView* output);
bool UnrealReadFloatArrayElement(const UnrealArrayView& array, int32_t index,
                                 float* output);
bool UnrealReadObjectArrayElement(const UnrealArrayView& array, int32_t index,
                                  void** output);
bool UnrealReadStructArrayProperty(void* object, const wchar_t* property_name,
                                   UnrealStructArrayView* output);
bool UnrealReadFixedStructArrayProperty(void* object,
                                        const wchar_t* property_name,
                                        UnrealStructArrayView* output);
bool UnrealReadStructArrayObjectField(const UnrealStructArrayView& array,
                                      int32_t index,
                                      const wchar_t* field_name,
                                      void** output);
bool UnrealReadStructArrayStringField(const UnrealStructArrayView& array,
                                      int32_t index,
                                      const wchar_t* field_name, char* output,
                                      size_t capacity);
bool UnrealReadStructArrayFloatField(const UnrealStructArrayView& array,
                                     int32_t index,
                                     const wchar_t* field_name,
                                     float* output);
bool UnrealReadStructArrayIntField(const UnrealStructArrayView& array,
                                   int32_t index,
                                   const wchar_t* field_name,
                                   int32_t* output);
bool UnrealReadStructFloatProperty(void* object,
                                   const wchar_t* property_name,
                                   const wchar_t* field_name, float* output);
bool UnrealReadStructProperty(void* object, const wchar_t* property_name,
                              UnrealStructValueView* output);
bool UnrealReadStructStringField(const UnrealStructValueView& value,
                                 const wchar_t* field_name, char* output,
                                 size_t capacity);
bool UnrealReadStructStructField(const UnrealStructValueView& value,
                                 const wchar_t* field_name,
                                 UnrealStructValueView* output);
bool UnrealReadStructIntField(const UnrealStructValueView& value,
                              const wchar_t* field_name, int32_t* output);
bool UnrealAssignStructIntField(const UnrealStructValueView& value,
                                const wchar_t* field_name, int32_t input);
bool UnrealAssignStructBoolField(const UnrealStructValueView& value,
                                 const wchar_t* field_name, bool input);
// Copies one reflected byte/enum field between two instances of the same
// script struct. This is used to preserve WFC-authored transient form state
// while substituting otherwise fully resolved character data.
bool UnrealCopyStructByteField(const UnrealStructValueView& source,
                               const UnrealStructValueView& destination,
                               const wchar_t* field_name);
// Shrinks (never grows) a reflected dynamic-array count in caller-owned
// struct storage. The allocation and capacity remain untouched.
bool UnrealShrinkStructArray(const UnrealStructValueView& value,
                             const wchar_t* field_name,
                             int32_t new_count);
bool UnrealReadStructByteArrayElement(const UnrealStructValueView& value,
                                      const wchar_t* field_name,
                                      int32_t index, uint8_t* output);
bool UnrealAssignStructStringArrayElementWide(
    const UnrealStructValueView& value, const wchar_t* field_name,
    int32_t index, const wchar_t* text);
bool UnrealReadStructBoolProperty(void* object,
                                  const wchar_t* property_name,
                                  const wchar_t* field_name, bool* output);
// Checked game-thread writes used only by the squad-control transaction. The
// destination is changed only when its current object still equals
// expected_value, preventing a stale transaction from overwriting a newer
// native state transition.
bool UnrealWriteObjectPropertyExpected(void* object,
                                       const wchar_t* property_name,
                                       void* expected_value,
                                       void* new_value);
bool UnrealWriteBoolPropertyExpected(void* object,
                                     const wchar_t* property_name,
                                     bool expected_value, bool new_value);
bool UnrealWriteIntProperty(void* object, const wchar_t* property_name,
                            int32_t value);
bool UnrealWriteStructProperty(void* object, const wchar_t* property_name,
                               const UnrealStructValueView& value);
// Exchanges two instances of the same reflected struct without duplicating or
// destroying its FString/object members. This is intentionally a swap rather
// than a copy so ownership of nested allocations remains one-to-one.
bool UnrealSwapStructProperties(void* left, const wchar_t* left_property,
                                void* right,
                                const wchar_t* right_property);
// Executes the exact-build SelectCharacter assignment body with its loading,
// validation, observer reset, and replication side effects bypassed. The
// temporary bytecode patch is restored before this function returns. This is
// the only ownership-safe way used by the mod to deep-copy WFC's dynamic
// CharacterDataReplicated fields.
bool UnrealSynchronouslyAssignCharacterPair(
    void* first_pri, void* second_pri,
    const UnrealFunctionHandle& select_character,
    const UnrealFunctionHandle& client_start_character_load,
    const UnrealFunctionHandle& confirm_selected_character,
    const UnrealFunctionHandle& refresh_provider,
    const UnrealStructValueView& first_character,
    const UnrealStructValueView& second_character,
    uint32_t expected_game_thread, bool* mutation_started);
// Single-PRI campaign variant. Campaign AI squad controllers normally do not
// own a player replication record; their identity lives on the pawn and the
// TnAiSquad member record. This performs the same guarded deep assignment for
// the one real local-player PRI without manufacturing an AI PRI.
bool UnrealSynchronouslyAssignCharacter(
    void* pri, const UnrealFunctionHandle& select_character,
    const UnrealFunctionHandle& client_start_character_load,
    const UnrealFunctionHandle& confirm_selected_character,
    const UnrealFunctionHandle& refresh_provider,
    const UnrealStructValueView& character,
    uint32_t expected_game_thread, bool* mutation_started);
bool UnrealAssignObjectStringPropertyFromStruct(
    void* destination, const wchar_t* destination_property,
    const UnrealStructValueView& source, const wchar_t* source_field);
bool UnrealAssignStructArrayStringField(
    const UnrealStructArrayView& destination, int32_t destination_index,
    const wchar_t* destination_field, const UnrealStructValueView& source,
    const wchar_t* source_field);
// Safe helpers for exact UFunction executor detours. FFrame parameters are
// copied out as UTF-8 before a hook returns, and FString results are assigned
// through the target build's native deep-copy operator so UE3 retains normal
// ownership and cleanup semantics.
bool UnrealReadFrameStringParameter(void* frame, uint32_t parameter_offset,
                                    char* output, size_t capacity);
bool UnrealAssignFrameStringParameter(void* frame, uint32_t parameter_offset,
                                      const wchar_t* value);
bool UnrealReadFrameStructParameter(
    void* frame, uint32_t parameter_offset, uint32_t parameter_size,
    void* struct_definition, UnrealStructValueView* output);
bool UnrealReadStringValue(const void* value, char* output, size_t capacity);
bool UnrealAssignWideStringValue(void* destination,
                                 const wchar_t* value);
bool UnrealAssignStructWideStringField(const UnrealStructValueView& value,
                                       const wchar_t* field_name,
                                       const wchar_t* text);
// Assigns ActionScript `undefined` to a GFx variable through the native
// SetVariable(Path, ASValue) API. The ASValue is validated from reflected
// metadata and zero-initialized (ASType::AS_Undefined).
bool UnrealSetVariableUndefined(void* movie, const wchar_t* path);

bool UnrealCallVoid(void* object, const wchar_t* function_name);
bool UnrealCallBool(void* object, const wchar_t* function_name, bool* output);
bool UnrealCallInt(void* object, const wchar_t* function_name,
                   int32_t* output);
bool UnrealCallByte(void* object, const wchar_t* function_name,
                    uint8_t* output);
bool UnrealCallFloat(void* object, const wchar_t* function_name,
                     float* output);
bool UnrealCallObject(void* object, const wchar_t* function_name,
                      void** output);
bool UnrealCallString(void* object, const wchar_t* function_name,
                      char* output, size_t capacity);
bool UnrealCallStringBool(void* object, const wchar_t* function_name,
                          const wchar_t* parameter_name, bool value,
                          char* output, size_t capacity);
bool UnrealCallBoolString(void* object, const wchar_t* function_name,
                          const wchar_t* parameter_name,
                          const wchar_t* value, bool* output);
bool UnrealCallObjectArray(void* object, const wchar_t* function_name,
                           UnrealArrayView* output);
bool UnrealCallVoidInt(void* object, const wchar_t* function_name,
                       const wchar_t* parameter_name, int32_t value);
bool UnrealCallVoidBool(void* object, const wchar_t* function_name,
                        const wchar_t* parameter_name, bool value);
bool UnrealCallVoidByte(void* object, const wchar_t* function_name,
                        const wchar_t* parameter_name, uint8_t value);
bool UnrealCallVoidFloat(void* object, const wchar_t* function_name,
                         const wchar_t* parameter_name, float value);
// Calls a void function containing a named FString input. Other optional
// parameters remain zero-initialized, which is the normal UE3 representation
// for an omitted array/object/bool argument.
bool UnrealCallVoidString(void* object, const wchar_t* function_name,
                          const wchar_t* parameter_name,
                          const wchar_t* value);
bool UnrealCallVoidStringString(
    void* object, const wchar_t* function_name,
    const wchar_t* first_parameter, const wchar_t* first_value,
    const wchar_t* second_parameter, const wchar_t* second_value);
bool UnrealCallVoidStringBool(
    void* object, const wchar_t* function_name,
    const wchar_t* string_parameter, const wchar_t* string_value,
    const wchar_t* bool_parameter, bool bool_value);
bool UnrealCallVoidByteBool(void* object, const wchar_t* function_name,
                            const wchar_t* byte_parameter, uint8_t byte_value,
                            const wchar_t* bool_parameter, bool bool_value);
bool UnrealCallBoolInt(void* object, const wchar_t* function_name,
                       const wchar_t* parameter_name, int32_t value,
                       bool* output);
bool UnrealCallFloatInt(void* object, const wchar_t* function_name,
                        const wchar_t* parameter_name, int32_t value,
                        float* output);
bool UnrealCallObjectInt(void* object, const wchar_t* function_name,
                         const wchar_t* parameter_name, int32_t value,
                         void** output);
bool UnrealCallObjectByte(void* object, const wchar_t* function_name,
                          const wchar_t* parameter_name, uint8_t value,
                          void** output);
bool UnrealCanCallObject(void* object, const wchar_t* function_name,
                         const wchar_t* object_parameter);
bool UnrealCallVoidObject(void* object, const wchar_t* function_name,
                          const wchar_t* object_parameter,
                          void* object_value);
bool UnrealCallVoidObjectObject(
    void* object, const wchar_t* function_name,
    const wchar_t* first_parameter, void* first_value,
    const wchar_t* second_parameter, void* second_value);
bool UnrealCallVoidObjectObjectObject(
    void* object, const wchar_t* function_name,
    const wchar_t* first_parameter, void* first_value,
    const wchar_t* second_parameter, void* second_value,
    const wchar_t* third_parameter, void* third_value);
bool UnrealCallVoidStruct(void* object, const wchar_t* function_name,
                          const wchar_t* parameter_name,
                          const UnrealStructValueView& value);
bool UnrealCanCallObjectBool(void* object, const wchar_t* function_name,
                             const wchar_t* object_parameter,
                             const wchar_t* bool_parameter);
bool UnrealCallVoidObjectBool(void* object, const wchar_t* function_name,
                              const wchar_t* object_parameter,
                              void* object_value,
                              const wchar_t* bool_parameter,
                              bool bool_value);

}  // namespace wfcds
