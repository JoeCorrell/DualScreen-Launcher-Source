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

bool InitializeUnrealBridge(HMODULE executable);
void ShutdownUnrealBridge();
bool UnrealBridgeReady();
// Starts a game-thread bridge pulse. This only clears short-lived memory-region
// validation state; reflected class metadata remains cached for the process.
void UnrealBeginGameThreadPulse();

int32_t UnrealObjectCount();
void* UnrealObjectAt(int32_t index);
bool UnrealObjectValid(void* object);
bool UnrealObjectIsA(void* object, const wchar_t* class_name);
uint32_t UnrealObjectIndex(void* object);
bool UnrealObjectName(void* object, char* output, size_t capacity);
bool UnrealObjectClassName(void* object, char* output, size_t capacity);
bool UnrealHasFunction(void* object, const wchar_t* function_name);
bool UnrealCanCallVoid(void* object, const wchar_t* function_name);
bool UnrealCanCallVoidInt(void* object, const wchar_t* function_name,
                          const wchar_t* parameter_name);

bool UnrealReadObjectProperty(void* object, const wchar_t* property_name,
                              void** output);
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
bool UnrealAssignStructArrayStringField(
    const UnrealStructArrayView& destination, int32_t destination_index,
    const wchar_t* destination_field, const UnrealStructValueView& source,
    const wchar_t* source_field);

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
bool UnrealCallObjectArray(void* object, const wchar_t* function_name,
                           UnrealArrayView* output);
bool UnrealCallVoidInt(void* object, const wchar_t* function_name,
                       const wchar_t* parameter_name, int32_t value);
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
