#pragma once

namespace dxs {

struct SmartGlassSnapshot;

// Reads the live mission and discovered-media containers used by DXHR's own
// mission/media SmartGlass controllers without requiring the retired Xbox
// SmartGlass service to be active. Call only from the primary Present thread.
bool MergeNativeJournalSnapshot(SmartGlassSnapshot* snapshot,
                                bool capture_objectives,
                                bool capture_media);

// Toggles the same tracked-mission flag and locator registration used by the
// retail SmartGlass mission action handler. Present-thread only.
bool ToggleNativeObjectiveTracking(int objective_id);

}  // namespace dxs
