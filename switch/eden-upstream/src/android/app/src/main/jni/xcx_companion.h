// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstddef>
#include <vector>

class EmulationSession;

namespace XcxCompanion {

// Writes one immutable Xenoblade Chronicles X: Definitive Edition telemetry frame.
int FillSnapshot(EmulationSession& session, void* output, std::size_t output_size);

// Queues a validated request for the exact-build game-thread bridge. Actions are dispatched
// through the game's native party/map systems rather than by mutating actor state.
bool PerformAction(EmulationSession& session, int action, int argument, unsigned int context = 0);

// Copies the bounded native segment records used by the map's 0x563920 iterator.
std::vector<unsigned char> ReadMapSegments(EmulationSession& session, unsigned int field);
std::vector<unsigned char> ReadProbeInventory(EmulationSession& session);
std::vector<unsigned char> ReadEquipment(EmulationSession& session, unsigned int character);
std::vector<unsigned char> ReadEquipmentInventory(EmulationSession& session, unsigned int category);
std::vector<unsigned char> ReadConditions(EmulationSession& session,
    const std::vector<unsigned int>& ids, unsigned int ignored_types);

// Reads a bounded batch of XCX's native save/game flags. This follows the exact flag-bank
// layout used by the 1.0.2 executable and never mutates guest state.
int ReadGameFlags(EmulationSession& session, unsigned int bit_width,
                  const unsigned int* ids, std::size_t count, unsigned char* output,
                  std::size_t output_size);

}
