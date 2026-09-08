// Hex formatting only; do not link Eden's assertion/logging runtime into a module.
#pragma once
#include "host_shim.h"
#include <string>
namespace Common {
template<class Container> std::string HexToString(const Container& bytes, bool upper=true) {
    const char* digits=upper ? "0123456789ABCDEF" : "0123456789abcdef";
    std::string result; result.reserve(bytes.size()*2);
    for (const u8 value: bytes) { result+=digits[value>>4]; result+=digits[value&15]; }
    return result;
}
}
