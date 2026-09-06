#pragma once

#include <cstddef>
#include <cstdint>

class EmulationSession;

namespace SshdCompanion {

#pragma pack(push, 1)
struct MapTextureMetadata {
    std::uint32_t magic;
    std::uint16_t version;
    std::uint16_t header_size;
    std::uint32_t sequence;
    std::uint16_t width;
    std::uint16_t height;
    float display_aspect;
    std::uint32_t pixel_format;
    std::uint8_t source_format;
    std::int8_t floor;
    std::uint16_t reserved;
    char stage_name[8];
};
#pragma pack(pop)

static_assert(sizeof(MapTextureMetadata) == 36);

// Writes one versioned, immutable Skyward Sword HD state frame into output.
int FillSnapshot(EmulationSession& session, void* output, std::size_t output_size);

// Applies a validated selection to the live 1.0.1 save/player state.
bool PerformAction(EmulationSession& session, int action, int argument);

// Read-only bottle predicates: -1 invalid, 0 stowed, 1 drawing, 2 held,
// 3 consuming. Uses the native player animation/item state, never save edits.
int GetBottleState(EmulationSession& session, int slot, int item);

// Publishes the native GPU map resource and returns a version-3 metadata header.
// No pixel payload or CPU/GPU readback is performed. Negative values describe
// unavailable game state or an invalid output buffer.
int CopyMapImage(EmulationSession& session, void* output, std::size_t output_size);
void SetMapView(EmulationSession& session, float zoom, float pan_x, float pan_y);

}
