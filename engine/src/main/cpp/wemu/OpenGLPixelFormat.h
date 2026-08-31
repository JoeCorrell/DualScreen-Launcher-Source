#pragma once

#include <android/hardware_buffer.h>

#include <cstdint>

namespace wemu::opengl {

// HAL_PIXEL_FORMAT_BGRA_8888 is value 5 in Android's pixel-format ABI. It is
// returned by AHardwareBuffer_describe on the supported devices even though
// the public NDK AHardwareBuffer_Format enum does not give it a symbol.
inline constexpr uint32_t kHardwareBufferFormatBgra8888 = 5;

constexpr bool isKnownWindowBufferFormat(uint32_t format) {
    return format == AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM
            || format == AHARDWAREBUFFER_FORMAT_R8G8B8X8_UNORM
            || format == kHardwareBufferFormatBgra8888;
}

/**
 * Whether sampling this AHardwareBuffer as an EGLImage still needs R/B fixed.
 *
 * WEMU's X11 pixels are BGRA bytes.  A local buffer is deliberately allocated
 * as R8G8B8A8, so EGL exposes byte zero as red and the compositor must swap it
 * with byte two.  A DRI3 producer may instead export B8G8R8A8; EGL then applies
 * that format interpretation itself and the sampled value is already RGBA.
 * Treat unknown formats like the historical R8 path so this classifier cannot
 * turn an otherwise importable vendor format into a black window.
 */
constexpr bool importedBufferNeedsRedBlueSwap(uint32_t format) {
    return format != kHardwareBufferFormatBgra8888;
}

/** The user option is an override of the correct native interpretation. */
constexpr bool effectiveRedBlueSwap(bool sourceNeedsSwap, bool userSwapOverride) {
    return sourceNeedsSwap != userSwapOverride;
}

// Compile-time coverage of the complete channel-order truth table.  These are
// evaluated for every Android ABI that builds the OpenGL compositor.
static_assert(importedBufferNeedsRedBlueSwap(AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM));
static_assert(importedBufferNeedsRedBlueSwap(AHARDWAREBUFFER_FORMAT_R8G8B8X8_UNORM));
static_assert(!importedBufferNeedsRedBlueSwap(kHardwareBufferFormatBgra8888));
static_assert(importedBufferNeedsRedBlueSwap(0x100)); // Conservative vendor-format fallback.
static_assert(effectiveRedBlueSwap(true, false));
static_assert(!effectiveRedBlueSwap(false, false));
static_assert(!effectiveRedBlueSwap(true, true));
static_assert(effectiveRedBlueSwap(false, true));

} // namespace wemu::opengl
