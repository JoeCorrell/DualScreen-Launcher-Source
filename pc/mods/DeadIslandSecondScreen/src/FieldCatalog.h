// Candidate field names, lifted from the game's own binary.
//
// Every string in this file was extracted from `gamedll_x64_rwdi.dll`
// (21,069,760 bytes, 2016-05-31) -- 18,403 identifier-shaped literals, filtered
// down to the ones that name something this panel displays. None of them were
// invented, and none were transcribed from a wiki or another mod.
//
// They are *candidates*, not answers, and the distinction is the point. A
// string being present in the binary proves the name exists somewhere in the
// game; it does not prove that the player object declares a field by that name.
// Only the live game can settle that, and it settles it cheaply: Discovery
// walks these lists against the real object through
// `CRTTIObject::FindField`, and whichever names come back are written to the
// log with their current values. After one run the guessing is over and the
// hits become the catalog.
//
// This is why the lists are ordered rather than alphabetical. GameState asks
// for the first name that resolves, so the most likely spelling for this build
// leads. `CurentHealth` is spelled the way the engine spells it -- the typo is
// Techland's and it is load-bearing, because a corrected spelling matches
// nothing.
//
// Adding a name here costs one failed string compare inside the engine per
// frame it is probed, and probing happens once at bind time rather than per
// frame, so a generous list is close to free. Deleting a name that turned out
// to be wrong is the cheap edit; hard-coding an offset would not have been.

#pragma once

namespace dis {
namespace catalog {

// Convenience for passing a static array as (pointer, count).
#define DIS_NAMES(x) (x), static_cast<int>(sizeof(x) / sizeof((x)[0]))

// ---- vitals ---------------------------------------------------------------
// `Health` and `MaxHealth` are the plain forms. `CurentHealth` is the engine's
// own misspelling and appears in the binary exactly as written here.
inline const char* const kHealth[] = {
    "m_Health", "m_ReplHealth", "m_replHealth", "Health", "CurentHealth",
    "CurrentHealth", "PlayerHealth", "DesiredHealth", "fHealth",
};
inline const char* const kMaxHealth[] = {
    "m_MaxHealth", "MaxHealth", "BaseMaxHealth", "HealthLimit", "fMaxHealth",
};
inline const char* const kStamina[] = {
    "m_Stamina", "Stamina", "CurrentStamina", "HudStamina",
};
inline const char* const kMaxStamina[] = {
    "MaxStamina", "MaxFistsFightStamina",
};
// Fury is a charge that fills and then a state that is active, and the game
// keeps those as separate values -- points accumulate, level gates activation.
inline const char* const kFury[] = {
    "FuryPoints", "FuryLevel", "Fury",
};
inline const char* const kMaxFury[] = {
    "FuryPointsNeededToActivate", "MaxFuryPoints", "MaxFury",
};
inline const char* const kFuryActive[] = {
    "m_IsFuryActive", "m_ReplIsGroupFuryActive", "FuryActive", "IsFury",
    "InFury", "ForcedFury",
};

// ---- progression ----------------------------------------------------------
inline const char* const kLevel[] = {
    "m_RepliPlayerLevel", "m_XpLevel", "Level", "CurrentLevel", "PlayerLevel",
};
inline const char* const kExperience[] = {
    "m_Experience", "Experience", "Exp", "XP", "CurrentExp",
};
inline const char* const kExpToNext[] = {
    "ExpToNextLevel", "ExperienceToNextLevel", "NextLevelExp",
};
inline const char* const kMoney[] = {
    "Money", "Cash", "PlayerCash", "CashAmount",
};
inline const char* const kSkillPoints[] = {
    "SkillPoints", "AvailableSkillPoints", "FreeSkillPoints",
};

// ---- the equipped weapon --------------------------------------------------
// Durability and Condition are distinct in Dead Island: condition is the wear
// bar the HUD draws, durability the underlying maximum.
inline const char* const kItemName[] = {
    "ItemName", "Name", "DisplayName",
};
inline const char* const kCondition[] = {
    "Condition", "CurrentCondition",
};
inline const char* const kDurability[] = {
    "Durability", "MaxCondition", "MaxDurability",
};
inline const char* const kAmmoCount[] = {
    "AmmoCount", "Ammo", "CurrentAmmo", "AmmoInClip",
};
inline const char* const kAmmoType[] = {
    "AmmoType",
};
inline const char* const kItemCount[] = {
    "ItemCount", "Count", "Quantity", "StackSize",
};
inline const char* const kItemId[] = {
    "ItemID", "Id",
};

// ---- inventory ------------------------------------------------------------
inline const char* const kInventory[] = {
    "Inventory", "PlayerInventory", "InventoryItem",
};
inline const char* const kInventorySize[] = {
    "InventorySize", "BackpackSize",
};
inline const char* const kMaxInventorySize[] = {
    "MaxInventorySize",
};
inline const char* const kQuickSlotsCount[] = {
    "QuickSlotsCount",
};
inline const char* const kQuickSlotsMaxCount[] = {
    "QuickSlotsMaxCount",
};

// ---- skills ---------------------------------------------------------------
// The three trees Dead Island ships, plus the per-weapon proficiencies that
// appear alongside them in the binary.
inline const char* const kSkillList[] = {
    "AllSkillsList", "ActiveSkillData", "SkillList", "Skills",
};
inline const char* const kSkillCount[] = {
    "AllowedSkillsCount", "SkillCount",
};
inline const char* const kWeaponSkills[] = {
    "BluntWeaponsSkill", "SharpWeaponsSkill", "FireWeaponsSkill",
    "ThrownWeaponsSkill", "ButcherSkill",
};

// ---- quests ---------------------------------------------------------------
// Quests are addressed by UID (Quests::QuestUid / PhaseUid, both exported from
// the engine), and a phase is the objective-level unit inside a quest.
inline const char* const kQuestName[] = {
    "QuestName", "Quest", "Name", "Title",
};
inline const char* const kPhaseName[] = {
    "PhaseName", "Phase", "ObjectiveText", "Objective",
};
inline const char* const kQuestState[] = {
    "QuestComplete", "QuestFinished", "QuestFailed", "PhaseType",
};

// ---- the whole sweep ------------------------------------------------------
// What Discovery probes when asked for a full dump of an object. Deliberately
// wider than anything above: the point of a dump is to find the names nobody
// predicted, so it includes the shapes that neighbour the ones already known.
inline const char* const kSweep[] = {
    // vitals
    "m_Health", "m_ReplHealth", "m_replHealth", "m_MaxHealth", "Health",
    "CurentHealth", "CurrentHealth", "MaxHealth", "BaseMaxHealth",
    "HealthLimit", "DesiredHealth", "PlayerHealth",
    "m_Stamina", "Stamina", "MaxStamina", "CurrentStamina",
    "MaxFistsFightStamina", "m_IsFuryActive", "Fury", "FuryPoints",
    "FuryLevel", "FuryActive", "ForcedFury",
    "FuryPointsNeededToActivate", "FuryGainMul", "FuryUsage", "FuryRange",
    // progression
    "m_RepliPlayerLevel", "Level", "CurrentLevel", "PlayerLevel",
    "m_Experience", "Experience", "Exp", "XP",
    "ExpToNextLevel", "ExperienceFactor", "Money", "Cash", "SkillPoints",
    // the equipped item
    "ItemName", "ItemID", "ItemCount", "ItemGroup", "ItemInfo",
    "Condition", "Durability", "AmmoCount", "AmmoType", "Name",
    // inventory
    "Inventory", "PlayerInventory", "InventoryItem", "InventorySize",
    "MaxInventorySize", "BackpackSize", "QuickSlotsCount",
    "QuickSlotsMaxCount", "InventoryTransaction", "InventoryAmmoCountAfter",
    // skills
    "AllSkillsList", "ActiveSkillData", "AllowedSkillsCount", "DelayedSkill",
    "BluntWeaponsSkill", "FireWeaponsSkill", "ButcherSkill",
    // quests
    "Quest", "QuestName", "QuestComplete", "QuestFinished", "QuestFailed",
    "QuestCash", "QuestExperienceBonus",
    "Phase", "PhaseId", "PhaseName", "PhaseType", "Objective", "ObjectiveText",
    // position, for the map
    "Position", "Pos", "WorldPosition", "Rotation", "Yaw", "Angle",
};

// ---- the level ------------------------------------------------------------
// Swept against the level object rather than the player.
//
// This list exists because of what the first device run actually returned:
// walking `ILevel::GetObjectByID` across ids 0..65535 found exactly one object,
// of class `LevelDI`. So object ids are not a dense range -- the same shape of
// wrong assumption as "screen ids are 0..255" was, one layer down -- and the
// player cannot be found by counting.
//
// But the level *was* found, it is a gamedll class, and it is a CRTTIObject.
// Whatever holds the player is reachable from it. These are the names that
// would hold it, and the sweep says which of them this build declares.
inline const char* const kLevelSweep[] = {
    "Player", "Players", "LocalPlayer", "MainPlayer", "PlayerDI", "PlayerObject",
    "Hero", "MainCharacter", "Character", "PlayerController", "PlayerCamera",
    "PlayerStart", "LocalHero", "CurrentPlayer", "ActivePlayer",
    // The containers a player might be an element of.
    "Objects", "AllObjects", "GameObjects", "Characters", "Actors", "Entities",
    "ObjectList", "ObjectsList", "SpawnedObjects",
    // Identity and bookkeeping, useful for confirming the object is the level.
    "Name", "LevelName", "MapName", "Id", "ID", "ObjectId",
    // Anything that names a count tells us a container exists even when the
    // container itself is a type this mod cannot read yet.
    "PlayersCount", "NumPlayers", "ObjectsCount", "NumObjects",
};

// ---- the game's own UI element ids ----------------------------------------
// Not fields -- these are element names inside the HUD screen (id 1024), and
// they are here because hiding the top-screen HUD is done by name too. Taken
// from the same string extraction; the `G_`, `H_`, `I_`, `L_`, `B_` prefixes
// are the engine's convention for group, hud-value, image, label and button.
inline const char* const kHudElements[] = {
    "G_HealthBar", "G_HealthCircle", "H_Health", "H_Stamina", "HudStamina",
    "HudNetSkills",
};

}  // namespace catalog
}  // namespace dis
