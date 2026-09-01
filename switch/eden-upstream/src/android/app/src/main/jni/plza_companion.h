#pragma once

#include <cstddef>

class EmulationSession;

namespace PlzaCompanion {

enum class ItemActionResult : int {
    Queued = 0,
    SessionNotRunning = 1,
    WrongTitle = 2,
    UnsupportedBuild = 3,
    InvalidRequest = 4,
    BagNotReady = 5,
    BagReadFailed = 6,
    ItemNotInBag = 7,
    TargetRequired = 8,
    InvalidTarget = 9,
    UnsupportedItemClass = 10,
    PresenterUnavailable = 11,
    ContextUnavailable = 12,
    GuestThreadBridgeUnavailable = 13,
    ActionBusy = 14,
};

int FillSnapshot(EmulationSession& session, void* output, std::size_t output_size);
ItemActionResult PerformItemAction(EmulationSession& session, int operation, int item_id,
                                   int party_target);
bool PerformAction(EmulationSession& session, int action, int argument);

}
