#pragma once

#include <windows.h>

namespace fo3ds {

bool StartPanel(HMODULE module);
void RequestPanelStop();
bool IsRadioPageRequested();
bool IsEffectsPageRequested();
bool IsMapPageRequested();
bool IsLocalMapPageRequested();
bool IsNotesPageRequested();
bool IsConditionStatsRequested();
bool IsSpecialStatsRequested();
bool IsSkillsStatsRequested();
bool IsGeneralStatsRequested();
bool IsGameDataRequested();

}  // namespace fo3ds
