// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstddef>

class EmulationSession;

namespace XcxCompanion {

// Writes one immutable Xenoblade Chronicles X: Definitive Edition telemetry frame.
int FillSnapshot(EmulationSession& session, void* output, std::size_t output_size);

// Queues a validated request for the exact-build game-thread bridge. Actions are dispatched
// through the game's native party/map systems rather than by mutating actor state.
bool PerformAction(EmulationSession& session, int action, int argument);

// Reads a bounded batch of XCX's native save/game flags. This follows the exact flag-bank
// layout used by the 1.0.2 executable and never mutates guest state.
int ReadGameFlags(EmulationSession& session, unsigned int bit_width,
                  const unsigned int* ids, std::size_t count, unsigned char* output,
                  std::size_t output_size);

}
