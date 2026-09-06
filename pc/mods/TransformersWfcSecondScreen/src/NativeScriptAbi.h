#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace wfcds {

// High Moon's compiled script object is a secondary C++ base at UObject+0x10.
// TWFC 116e6a59..116e6a86 adjusts ECX for FUNC_NativeScript (0x20).
// This is NOT UE3 FUNC_Native (0x400), and cooked script flags alone do not
// tell us which executor ABI will be installed by the shipping linker.
constexpr uint32_t kFunctionNativeScript = 0x20u;
constexpr uintptr_t kNativeScriptThisOffset = 0x10u;

// A tiny tail-call adapter; preserves EDX, stack parameters and return ABI.
// x86 rel32 arithmetic is modulo 2^32, including across the 2GB boundary.
inline void WriteScriptContextThunk(uint8_t* code, int8_t adjustment,
                                    uintptr_t target) {
  code[0] = 0x83;  // add ecx, imm8
  code[1] = 0xC1;
  code[2] = static_cast<uint8_t>(adjustment);
  code[3] = 0xE9;  // jmp rel32
  const uint32_t relative = static_cast<uint32_t>(target) -
      static_cast<uint32_t>(reinterpret_cast<uintptr_t>(code) + 8);
  std::memcpy(code + 4, &relative, sizeof(relative));
}

}  // namespace wfcds
