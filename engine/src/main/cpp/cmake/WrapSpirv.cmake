# Turns glslc's hex output into the header glslangValidator would have written.
#
# Invoked with `cmake -P`, because the wrapping has to happen at build time
# rather than at configure time — the .frag files change, and a configure-time
# `execute_process` would not rebuild the header when they do.
#
# Expects: GLSLC, SOURCE, VARNAME, OUTPUT.

execute_process(
    COMMAND "${GLSLC}" --target-env=vulkan1.0 -mfmt=num -o "${OUTPUT}.words" "${SOURCE}"
    RESULT_VARIABLE compile_result
    ERROR_VARIABLE compile_error
)

if(NOT compile_result EQUAL 0)
    message(FATAL_ERROR "glslc failed on ${SOURCE}:\n${compile_error}")
endif()

file(READ "${OUTPUT}.words" spirv_words)
file(REMOVE "${OUTPUT}.words")

# glslc emits a trailing newline; a stray one inside the initialiser is
# harmless, but stripping it keeps the generated header readable when someone
# inevitably opens it to find out why a shader is misbehaving.
string(STRIP "${spirv_words}" spirv_words)

file(WRITE "${OUTPUT}"
"// Generated from ${SOURCE}. Do not edit.
#pragma once
#include <stdint.h>
const uint32_t ${VARNAME}[] = {
${spirv_words}
};
")
