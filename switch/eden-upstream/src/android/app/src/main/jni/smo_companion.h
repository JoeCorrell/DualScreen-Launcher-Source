#pragma once
#include <cstddef>
#include <cstdint>
#include <string_view>
class EmulationSession;
namespace SmoCompanion {
int FillSnapshot(EmulationSession& session, void* output, std::size_t capacity);
int FillMoons(EmulationSession& session, void* output, std::size_t capacity);
int FillMarkers(EmulationSession& session, void* output, std::size_t capacity);
int FillCollection(EmulationSession& session, void* output, std::size_t capacity, int kind);
int QueueOutfit(EmulationSession& session, int kind, std::string_view name, std::uint64_t expected_generation);
int PollOutfit(EmulationSession& session, int ticket);
}
