#pragma once
#include "NativePlayerServices.h"

namespace wfcds {
// Binds/validates the resolved-current identity setter. Does not change chapter
// selection, spawn a pawn, or initialize its inventory/abilities. Ready permits
// a prospective pawn; assignment requires the PRI owner's actual current pawn.
bool NativeCharacterIdentityReady(HMODULE image, DWORD game_thread,
                                  void* pri, void* pawn);
NativePlayerServiceResult NativeAssignCurrentCharacter(void* pri, void* pawn);
}
