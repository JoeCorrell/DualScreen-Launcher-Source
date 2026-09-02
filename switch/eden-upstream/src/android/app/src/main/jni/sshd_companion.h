#pragma once

#include <cstddef>
#include <cstdint>

class EmulationSession;

namespace SshdCompanion {

#pragma pack(push, 1)
struct MapImageHeaderV1 {
    std::uint32_t magic;
    std::uint16_t version;
    std::uint16_t header_size;
    std::uint32_t sequence;
    std::uint16_t width;
    std::uint16_t height;
    std::uint32_t row_stride;
    std::uint32_t pixel_format;
    std::uint8_t source_format;
    std::int8_t floor;
    std::uint16_t reserved;
    char stage_name[8];
};
#pragma pack(pop)

static_assert(sizeof(MapImageHeaderV1) == 36);

// Writes one versioned, immutable Skyward Sword HD state frame into output.
int FillSnapshot(EmulationSession& session, void* output, std::size_t output_size);

// Applies a validated selection to the live 1.0.1 save/player state.
bool PerformAction(EmulationSession& session, int action, int argument);

// Requests/copies one native dStage map capture. Returns 0 while the guest
// game-thread bridge is pending, a positive byte count on success, or a
// negative error/required-capacity value. Pixels follow MapImageHeaderV1 as
// tightly packed RGBA8888 rows.
int CopyMapImage(EmulationSession& session, void* output, std::size_t output_size);

}
