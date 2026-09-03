


#include "jni/botw_companion.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <limits>
#include <mutex>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "common/hex_util.h"
#include "common/logging.h"
#include "core/core.h"
#include "core/hle/kernel/k_process.h"
#include "core/memory.h"
#include "jni/native.h"

namespace BotwCompanion {
namespace {

constexpr u64 BotwTitleId = 0x01007EF00011E000ULL;
constexpr std::array<u8, 0x20> BotwBuildId{
    0xCD, 0x57, 0xB2, 0x3F, 0xA4, 0xBB, 0xAD, 0x65,
    0x80, 0x3D, 0x97, 0x88, 0xC0, 0x18, 0x21, 0xEE,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};


constexpr VAddr PlayerSingletonOffset = 0x01D66A78;
constexpr VAddr PlayerComponentOffset = 0x60;
constexpr VAddr PlayerMaxHealthOffset = 0xA0;
constexpr VAddr PlayerStaminaOffset = 0xA4;
constexpr VAddr ComponentMaxHealthOffset = 0x1868;
constexpr VAddr ComponentMaxStaminaOffset = 0x186C;
constexpr VAddr HealthGetterVtableOffset = 0x2B8;
constexpr VAddr PlayerMatrixOffset = 0x398;
constexpr float HyruleWorldLimit = 20'000.0f;
// The pouch consume routine first writes requested effect level/duration pairs at
// Player+0x1180..0x11EC. Player::updateCookEffects then commits them to these live countdowns.
// Read the live values so the companion reflects actual gameplay state and decreasing timers.
constexpr VAddr PlayerSpeedEffectOneFramesOffset = 0x1EDC;
constexpr VAddr PlayerSpeedEffectOneLevelOffset = 0x1FF0;
constexpr VAddr PlayerSpeedEffectTwoFramesOffset = 0x1EE8;
constexpr VAddr PlayerSpeedEffectTwoLevelOffset = 0x1FF4;
constexpr VAddr PlayerSpeedEffectThreeFramesOffset = 0x1EF4;
constexpr VAddr PlayerSpeedEffectThreeLevelOffset = 0x1FF8;
constexpr VAddr PlayerAttackEffectFramesOffset = 0x1F18;
constexpr VAddr PlayerAttackEffectLevelOffset = 0x2004;
constexpr VAddr PlayerDefenseEffectFramesOffset = 0x1F24;
constexpr VAddr PlayerDefenseEffectLevelOffset = 0x2008;
constexpr VAddr PlayerHeatEffectFramesOffset = 0x1F30;
constexpr VAddr PlayerHeatEffectLevelOffset = 0x200C;
constexpr VAddr PlayerColdEffectFramesOffset = 0x1F3C;
constexpr VAddr PlayerColdEffectLevelOffset = 0x2010;
constexpr VAddr PlayerFireproofEffectFramesOffset = 0x1F48;
constexpr VAddr PlayerFireproofEffectLevelOffset = 0x2014;
constexpr VAddr PlayerShockEffectFramesOffset = 0x1F60;
constexpr VAddr PlayerShockEffectLevelOffset = 0x201C;
constexpr VAddr PlayerStealthEffectFramesOffset = 0x1F78;
constexpr VAddr PlayerStealthEffectLevelOffset = 0x2024;
constexpr s32 MaximumCookEffectSeconds = 86'400;
constexpr float CookEffectFramesPerSecond = 30.0f;

// qst::Manager::sInstance is direct storage in this exact executable. Manager::mQuests is a
// sead::ObjArray whose count and pointer buffer live at +0xB8/+0xC0. Quest and Step offsets are
// validated against their constructors and the 0x158/0x88 class sizes in the same build.
constexpr VAddr QuestManagerStaticOffset = 0x01E46720;
constexpr VAddr QuestArrayCountOffset = 0xB8;
constexpr VAddr QuestArrayPointersOffset = 0xC0;
constexpr VAddr QuestStateOffset = 0x0C;
constexpr VAddr QuestNamePointerOffset = 0x48;
constexpr VAddr QuestLocationPointerOffset = 0x38;
constexpr VAddr QuestStepCountOffset = 0xA0;
constexpr VAddr QuestStepPointersOffset = 0xA8;
constexpr VAddr QuestTypePointerOffset = 0xC0;
constexpr VAddr QuestCurrentStepOffset = 0x140;
constexpr VAddr QuestStepMessagePointerOffset = 0x48;
constexpr VAddr QuestStepNamePointerOffset = 0x50;
constexpr s32 MaxQuestCount = 512;

// PauseMenuDataMgr::sInstance is accessed through this relocated GOT cell in BOTW 1.9.0.
// The list and item offsets below are corroborated by the executable's own list traversal and

constexpr VAddr PouchSingletonGotOffset = 0x01D67AF0;
constexpr VAddr PouchListOffset = 0x68;
constexpr VAddr OffsetListNextOffset = 0x08;
constexpr VAddr OffsetListCountOffset = 0x10;
constexpr VAddr OffsetListNodeOffset = 0x14;
constexpr VAddr PouchItemListNodeOffset = 0x08;
constexpr VAddr PouchItemTypeOffset = 0x18;
constexpr VAddr PouchItemUseOffset = 0x1C;
constexpr VAddr PouchItemValueOffset = 0x20;
constexpr VAddr PouchItemEquippedOffset = 0x24;
constexpr VAddr PouchItemInInventoryOffset = 0x25;
constexpr VAddr PouchItemNamePointerOffset = 0x30;
constexpr VAddr PouchItemModifierValueOffset = 0x78;
constexpr VAddr PouchItemModifierFlagsOffset = 0x80;
constexpr VAddr PouchItemCookHealthRecoverOffset = 0x80;
constexpr VAddr PouchItemCookEffectDurationOffset = 0x84;
constexpr VAddr PouchItemCookSellPriceOffset = 0x88;
constexpr VAddr PouchItemCookEffectIdOffset = 0x8C;
constexpr VAddr PouchItemCookEffectLevelOffset = 0x90;
constexpr VAddr PouchTabsOffset = 0x44230;
constexpr size_t PouchTabCount = 50;
constexpr size_t PouchItemsPerTab = 20;
constexpr VAddr PouchGrabbedItemsOffset = 0x444A0;
constexpr VAddr PouchGrabbedItemStride = 0x10;
constexpr size_t PouchGrabbedItemCapacity = 5;
constexpr s32 MaxPouchItems = 420;

constexpr int ActionEquipItem = 1;
constexpr int ActionSelectRune = 2;
constexpr int ActionFastTravel = 3;
constexpr int ActionEatItem = 4;
constexpr int ActionHoldItem = 5;
constexpr int ActionUnequipItem = 6;
constexpr s32 PouchItemTypeMaterial = 7;
constexpr s32 PouchItemTypeFood = 8;
constexpr s32 ItemUseCureItem = 10;
constexpr size_t EquipMailboxSize = 0x4000;
constexpr size_t EquipMailboxRequestOffset = 0;
constexpr size_t EquipMailboxCompletedOffset = 8;
constexpr size_t EquipMailboxStateOffset = 16;
constexpr size_t EquipMailboxTypeOffset = 20;
constexpr size_t EquipMailboxReservedOffset = 24;
constexpr size_t EquipMailboxAttemptsOffset = 28;
constexpr size_t EatMailboxSlotOffset = 0x20;
constexpr size_t ActionStageOffset = 0x24;
constexpr size_t FastTravelMapNameOffset = 0x40;
constexpr size_t FastTravelMapNameSize = 0x40;
constexpr size_t FastTravelPositionNameOffset = 0x80;
constexpr size_t FastTravelPositionNameSize = 0x40;
constexpr u32 EquipMailboxIdle = 0;
constexpr u32 EquipMailboxQueued = 1;
constexpr u32 EquipMailboxRunning = 2;
constexpr u32 EquipMailboxSucceeded = 3;
constexpr u32 EquipMailboxRejected = 4;
constexpr u32 EquipMailboxClaimed = 5;
constexpr u32 EquipMailboxChecking = 6;
constexpr u32 RuneMailboxTypeSentinel = 0xFFFFFFFFU;
constexpr u32 FastTravelMailboxTypeSentinel = 0xFFFFFFFEU;
constexpr u32 HoldMailboxTypeSentinel = 0xFFFFFFFDU;
constexpr u32 EatMailboxTypeSentinel = 0xFFFFFFFCU;
constexpr u32 UnequipMailboxTypeSentinel = 0xFFFFFFFBU;
constexpr size_t HoldControllerMailboxOffset = 0xC8;
constexpr size_t ActionDiagnosticSequenceOffset = 0xD0;
constexpr size_t ActionDiagnosticValidationFlagsOffset = 0xD4;
constexpr size_t ActionDiagnosticItemTypeOffset = 0xD8;
constexpr size_t ActionDiagnosticItemUseOffset = 0xDC;
constexpr size_t ActionDiagnosticQuantityBeforeOffset = 0xE0;
constexpr size_t ActionDiagnosticInInventoryBeforeOffset = 0xE4;
constexpr size_t ActionDiagnosticTabOffset = 0xE8;
constexpr size_t ActionDiagnosticSlotOffset = 0xEC;
constexpr size_t ActionDiagnosticHeldBeforeOffset = 0xF0;
constexpr size_t ActionDiagnosticNativeResultOffset = 0xF4;
constexpr size_t ActionDiagnosticTargetOffset = 0x100;
constexpr size_t ActionDiagnosticPouchManagerOffset = 0x108;
constexpr size_t ActionDiagnosticPlayerOffset = 0x110;
constexpr size_t ActionDiagnosticControllerOffset = 0x118;
constexpr size_t ActionDiagnosticActiveFlagPointerOffset = 0x120;
constexpr size_t ActionDiagnosticControllerStateOffset = 0x128;
constexpr size_t ActionDiagnosticControllerActionOffset = 0x12C;
constexpr size_t ActionDiagnosticControllerTargetOffset = 0x130;
constexpr size_t ActionDiagnosticControllerActiveOffset = 0x138;
constexpr size_t ActionDiagnosticNodeOffsetOffset = 0x13C;
constexpr size_t ActionDiagnosticTabHeadOffset = 0x140;
constexpr size_t ActionDiagnosticTabItemCountOffset = 0x148;
constexpr size_t ActionDiagnosticGuestQuantityAfterOffset = 0x14C;
constexpr size_t ActionDiagnosticGuestInInventoryAfterOffset = 0x150;
constexpr u32 ActionDiagnosticUnavailable = 0xFFFFFFFFU;
constexpr VAddr EatNotifyStorageOffset = 0x01E15FB8;
constexpr VAddr EatScreenRegistryOffset = 0x01E374B8;
constexpr VAddr HoldControllerGotOffset = 0x01D6A550;
constexpr VAddr PlayerInfoStorageOffset = 0x01E13ED0;
constexpr VAddr PlayerInfoActorOffset = 0x60;
constexpr VAddr HoldControllerMessageSenderOffset = 0x150;
constexpr VAddr HoldControllerMessageDispatcherOffset =
    HoldControllerMessageSenderOffset + sizeof(u64);
constexpr VAddr CarryBoxManagerOffset = 0x01E0C828;
constexpr VAddr CarryBoxRootMatrixOffset = 0xD8;
constexpr VAddr CarryBoxAnchorOffset = 0x310;
constexpr VAddr CarryBoxFlagsOffset = 0xA78;

constexpr bool IsEquipMailboxBusy(u32 state) {
    return state == EquipMailboxQueued || state == EquipMailboxRunning ||
           state == EquipMailboxClaimed || state == EquipMailboxChecking;
}

constexpr std::string_view ItemActionName(int action) {
    switch (action) {
    case ActionEquipItem: return "Equip";
    case ActionSelectRune: return "SelectRune";
    case ActionFastTravel: return "FastTravel";
    case ActionEatItem: return "Eat";
    case ActionHoldItem: return "Hold";
    case ActionUnequipItem: return "Unequip";
    default: return "Unknown";
    }
}

constexpr VAddr RuneManagerStaticOffset = 0x01E0CD40;
constexpr VAddr RuneManagerSelectedTypeOffset = 0x240;
constexpr u32 RuneTypeRoundBomb = 0;
constexpr u32 RuneTypeCubeBomb = 1;
constexpr u32 RuneTypeMagnesis = 2;
constexpr u32 RuneTypeStasis = 3;
constexpr u32 RuneTypeCryonis = 4;
constexpr u32 RuneTypeCamera = 5;

constexpr u32 WeaponModifierAddAttack = 0x00000001;
constexpr u32 WeaponModifierAddGuard = 0x00000100;
constexpr u32 WeaponModifierKnownMask = 0x800001FF;

constexpr VAddr GameDataManagerGotOffset = 0x01D66478;
constexpr VAddr CurrentRupeeHandleOffset = 0x01DA6A28;
constexpr VAddr CameraRuneHandleOffset = 0x01DA6BF4;
constexpr VAddr CryonisRuneHandleOffset = 0x01DA6C08;
constexpr VAddr MagnesisRuneHandleOffset = 0x01DA6C0C;
constexpr VAddr RemoteBombRuneHandleOffset = 0x01DA6C18;
constexpr VAddr RemoteBombUpgradeHandleOffset = 0x01DA6C1C;
constexpr VAddr SheikSensorOwnedHandleOffset = 0x01DA6C20;
constexpr VAddr SheikSensorUpgradeHandleOffset = 0x01DA6C24;
constexpr VAddr StasisRuneHandleOffset = 0x01DA6C28;
constexpr VAddr StasisUpgradeHandleOffset = 0x01DA6C2C;
constexpr VAddr SheikSensorSearchModeHandleOffset = 0x01DA693C;


constexpr u32 SheikSensorEnabledHash = 0xCD357720;
constexpr VAddr GameDataFlagsOffset = 0xC18;
constexpr VAddr GameDataHandleGenerationOffset = 0xC28;
constexpr VAddr GameDataFlagBufferHolderOffset = 0xBE0;
constexpr VAddr GameDataFlagBufferGenerationOffset = 0xBE8;




constexpr VAddr ChampionTimerRootOffset = 0x01E13ED0;
constexpr VAddr ChampionTimerComponentOffset = 0x60;
constexpr VAddr RevaliGaleTimerOffset = 0x1DF8;
constexpr VAddr RevaliGaleTimerRateOffset = 0x1E00;
constexpr VAddr DarukProtectionTimerOffset = 0x1E04;
constexpr VAddr DarukProtectionTimerRateOffset = 0x1E0C;
constexpr VAddr UrbosaFuryTimerOffset = 0x1E10;
constexpr VAddr UrbosaFuryTimerRateOffset = 0x1E18;
constexpr VAddr MiphaGraceTimerOffset = 0x1E1C;
constexpr VAddr MiphaGraceTimerRateOffset = 0x1E24;
constexpr VAddr RevaliGaleUsesOffset = 0x1CCC;
constexpr VAddr UrbosaFuryUsesOffset = 0x1CD0;
constexpr VAddr DarukProtectionUsesOffset = 0x1CD4;

struct PouchItemSnapshot {
    VAddr address{};
    s32 type{};
    s32 item_use{};
    s32 value{};
    u32 modifier_value{};
    u32 modifier_flags{};
    s32 cook_health_recover{};
    s32 cook_effect_duration{};
    s32 cook_sell_price{};
    s32 cook_effect_id{-1};
    float cook_effect_level{};
    bool equipped{};
    std::string name;
};

constexpr bool IsHoldablePouchItem(const PouchItemSnapshot& item) {
    return item.type == PouchItemTypeMaterial && item.value > 0;
}

constexpr bool IsEdiblePouchItem(const PouchItemSnapshot& item) {
    return item.value > 0 &&
           (item.type == PouchItemTypeFood ||
            (item.type == PouchItemTypeMaterial && item.item_use == ItemUseCureItem));
}

constexpr s32 PouchTabGroup(s32 type) {
    if (type == 1) {
        return 2;
    }
    if (type >= 4 && type <= 6) {
        return 6;
    }
    return type;
}

struct ChampionPowerSnapshot {
    std::string_view id;
    std::string_view name;
    bool available{};
    bool enabled{};
    u32 uses{};
    u32 max_uses{};
    u32 cooldown_seconds{};
};

struct PlayerMapSnapshot {
    float x{};
    float y{};
    float z{};
    float heading{};
};

struct ActiveEffectSnapshot {
    std::string_view name;
    s32 seconds{};
    float level{};
};

struct ShrineDefinition {
    std::string_view id;
    std::string_view map_name;
    std::string_view position_name;
    u32 entered_hash{};
    u32 cleared_hash{};
};

#include "botw_shrines.inc"

struct ShrineStateSnapshot {
    std::array<bool, BotwShrines.size()> entered{};
    std::array<bool, BotwShrines.size()> cleared{};
    std::optional<bool> sheik_sensor_enabled;
};

struct QuestSnapshot {
    VAddr id{};
    std::string actor_name;
    std::string title;
    std::string objective;
    std::string step;
    std::string location;
    std::string type;
    bool complete{};
};

struct HealthAddressCache {
    std::mutex mutex;
    u64 process_id{};
    VAddr component{};
    VAddr getter{};
    VAddr address{};
};

HealthAddressCache HealthCache;
std::mutex EquipActionMutex;

struct ShrineFlagAddressCache {
    std::mutex mutex;
    u64 process_id{};
    VAddr buffer{};
    VAddr entries{};
    u32 count{};
    u32 generation{};
    std::array<VAddr, BotwShrines.size()> entered{};
    std::array<VAddr, BotwShrines.size()> cleared{};
    VAddr sheik_sensor_enabled{};
};

ShrineFlagAddressCache ShrineFlagCache;

void ResetSnapshotCaches() {
    {
        std::scoped_lock lock{HealthCache.mutex};
        HealthCache.process_id = 0;
        HealthCache.component = 0;
        HealthCache.getter = 0;
        HealthCache.address = 0;
    }
    {
        std::scoped_lock lock{ShrineFlagCache.mutex};
        ShrineFlagCache.process_id = 0;
        ShrineFlagCache.buffer = 0;
        ShrineFlagCache.entries = 0;
        ShrineFlagCache.count = 0;
        ShrineFlagCache.generation = 0;
        ShrineFlagCache.entered.fill(0);
        ShrineFlagCache.cleared.fill(0);
        ShrineFlagCache.sheik_sensor_enabled = 0;
    }
}

struct ActorStats {
    std::string_view name;
    s32 attack{};
    s32 guard{};
    s32 defense{};
};

#include "botw_actor_info.inc"

struct ItemText {
    std::string_view actor;
    std::string_view name;
    std::string_view description;
};

#include "botw_item_text.inc"

struct QuestText {
    std::string_view label;
    std::string_view text;
};

#include "botw_quest_text.inc"




constexpr std::array<ItemText, 42> BotwExpansionArmorText{{
    ItemText{"Armor_168_Head", "Vah Naboris Divine Helm", ""},
    ItemText{"Armor_169_Head", "Vah Naboris Divine Helm", ""},
    ItemText{"Armor_170_Upper", "Nintendo Switch Shirt", ""},
    ItemText{"Armor_171_Head", "Phantom Helmet", ""},
    ItemText{"Armor_171_Lower", "Phantom Greaves", ""},
    ItemText{"Armor_171_Upper", "Phantom Armor", ""},
    ItemText{"Armor_172_Head", "Majora's Mask", ""},
    ItemText{"Armor_173_Head", "Midna's Helmet", ""},
    ItemText{"Armor_174_Head", "Tingle's Hood", ""},
    ItemText{"Armor_174_Lower", "Tingle's Tights", ""},
    ItemText{"Armor_174_Upper", "Tingle's Shirt", ""},
    ItemText{"Armor_175_Upper", "Island Lobster Shirt", ""},
    ItemText{"Armor_176_Head", "Korok Mask", ""},
    ItemText{"Armor_177_Head", "Ravio's Hood", ""},
    ItemText{"Armor_178_Head", "Zant's Helmet", ""},
    ItemText{"Armor_179_Head", "Royal Guard Cap", ""},
    ItemText{"Armor_179_Lower", "Royal Guard Boots", ""},
    ItemText{"Armor_179_Upper", "Royal Guard Uniform", ""},
    ItemText{"Armor_180_Head", "Phantom Ganon Skull", ""},
    ItemText{"Armor_180_Lower", "Phantom Ganon Greaves", ""},
    ItemText{"Armor_180_Upper", "Phantom Ganon Armor", ""},
    ItemText{"Armor_181_Head", "Vah Ruta Divine Helm", ""},
    ItemText{"Armor_182_Head", "Vah Medoh Divine Helm", ""},
    ItemText{"Armor_183_Head", "Vah Rudania Divine Helm", ""},
    ItemText{"Armor_184_Head", "Vah Naboris Divine Helm", ""},
    ItemText{"Armor_185_Head", "Salvager Headwear", ""},
    ItemText{"Armor_185_Lower", "Salvager Trousers", ""},
    ItemText{"Armor_185_Upper", "Salvager Vest", ""},
    ItemText{"Armor_186_Head", "Vah Ruta Divine Helm", ""},
    ItemText{"Armor_187_Head", "Vah Ruta Divine Helm", ""},
    ItemText{"Armor_188_Head", "Vah Ruta Divine Helm", ""},
    ItemText{"Armor_189_Head", "Vah Ruta Divine Helm", ""},
    ItemText{"Armor_190_Head", "Vah Medoh Divine Helm", ""},
    ItemText{"Armor_191_Head", "Vah Medoh Divine Helm", ""},
    ItemText{"Armor_192_Head", "Vah Medoh Divine Helm", ""},
    ItemText{"Armor_193_Head", "Vah Medoh Divine Helm", ""},
    ItemText{"Armor_194_Head", "Vah Rudania Divine Helm", ""},
    ItemText{"Armor_195_Head", "Vah Rudania Divine Helm", ""},
    ItemText{"Armor_196_Head", "Vah Rudania Divine Helm", ""},
    ItemText{"Armor_197_Head", "Vah Rudania Divine Helm", ""},
    ItemText{"Armor_198_Head", "Vah Naboris Divine Helm", ""},
    ItemText{"Armor_199_Head", "Vah Naboris Divine Helm", ""},
}};

const ActorStats* FindActorStats(std::string_view name) {
    const auto iterator = std::lower_bound(
        BotwActorStats.begin(), BotwActorStats.end(), name,
        [](const ActorStats& actor, std::string_view value) { return actor.name < value; });
    return iterator != BotwActorStats.end() && iterator->name == name ? &*iterator : nullptr;
}

const QuestText* FindQuestText(std::string_view label) {
    const auto iterator = std::lower_bound(
        BotwQuestText.begin(), BotwQuestText.end(), label,
        [](const QuestText& row, std::string_view value) { return row.label < value; });
    return iterator != BotwQuestText.end() && iterator->label == label ? &*iterator : nullptr;
}

std::string QuestLabelBase(std::string_view actor_name) {
    if (actor_name.starts_with("QL_")) {
        return std::string{actor_name};
    }
    return "QL_" + std::string{actor_name};
}

const QuestText* FindQuestStepText(std::string_view label_base, std::string_view token) {
    if (token.empty()) {
        return nullptr;
    }
    if (const auto* exact = FindQuestText(token)) {
        return exact;
    }
    if (!token.starts_with("QL_")) {
        const std::string prefixed = "QL_" + std::string{token};
        if (const auto* exact = FindQuestText(prefixed)) {
            return exact;
        }
    }

    std::string suffix{token};
    const std::string base_prefix = std::string{label_base} + '_';
    if (suffix.starts_with(base_prefix)) {
        suffix.erase(0, base_prefix.size());
    }
    if (suffix == "Finished") {
        suffix = "Finish";
    }
    if (const auto* localized = FindQuestText(std::string{label_base} + '_' + suffix)) {
        return localized;
    }
    return nullptr;
}

const ItemText* FindItemText(std::string_view actor) {
    const auto find_exact = [](const auto& table, std::string_view value) -> const ItemText* {
        const auto iterator = std::lower_bound(
            table.begin(), table.end(), value,
            [](const ItemText& item, std::string_view candidate) {
                return item.actor < candidate;
            });
        return iterator != table.end() && iterator->actor == value ? &*iterator : nullptr;
    };
    if (const auto* item = find_exact(BotwItemText, actor)) {
        return item;
    }
    if (const auto* item = find_exact(BotwExpansionArmorText, actor)) {
        return item;
    }


    if (actor.ends_with("_B")) {
        const auto base_actor = actor.substr(0, actor.size() - 2);
        if (const auto* item = find_exact(BotwItemText, base_actor)) {
            return item;
        }
        return find_exact(BotwExpansionArmorText, base_actor);
    }
    return nullptr;
}

std::optional<s32> GetItemPower(const PouchItemSnapshot& item) {
    const auto* actor = FindActorStats(item.name);
    if (item.type >= 4 && item.type <= 6) {
        return actor ? std::optional<s32>{actor->defense} : std::nullopt;
    }
    if (!actor) {
        return std::nullopt;
    }
    const auto add_checked = [](s32 base, u32 bonus) -> std::optional<s32> {
        const s64 total = static_cast<s64>(base) + static_cast<s64>(bonus);
        if (total < std::numeric_limits<s32>::min() ||
            total > std::numeric_limits<s32>::max()) {
            return std::nullopt;
        }
        return static_cast<s32>(total);
    };
    if (item.type == 3) {
        return add_checked(
            actor->guard,
            (item.modifier_flags & WeaponModifierAddGuard) ? item.modifier_value : 0);
    }
    if (item.type <= 1) {
        return add_checked(
            actor->attack,
            (item.modifier_flags & WeaponModifierAddAttack) ? item.modifier_value : 0);
    }
    return std::nullopt;
}

template <typename T>
std::optional<T> Read(Core::Memory::Memory& memory, VAddr address) {
    if (!address || !memory.IsValidVirtualAddressRange(address, sizeof(T))) {
        return std::nullopt;
    }
    T value{};






    if (!memory.ReadBlockUnsafe(address, &value, sizeof(value))) {
        return std::nullopt;
    }
    return value;
}

template <size_t Size>
bool ReadBytes(Core::Memory::Memory& memory, VAddr address, std::array<u8, Size>& bytes) {
    return address && memory.IsValidVirtualAddressRange(address, Size) &&
           memory.ReadBlockUnsafe(address, bytes.data(), Size);
}

template <typename T, size_t Size>
T DecodeScalar(const std::array<u8, Size>& bytes, size_t offset) {
    T value{};
    std::memcpy(&value, bytes.data() + offset, sizeof(value));
    return value;
}

bool IsEquipMailboxAvailable(Core::System& system, VAddr address) {
    if (!address || (address & 7U) != 0) {
        return false;
    }

    auto& memory = system.ApplicationMemory();
    return memory.IsValidVirtualAddressRange(address, EquipMailboxSize) &&
           memory.GetPointerSilent(address) != nullptr &&
           memory.GetPointerSilent(address + EquipMailboxSize - 1) != nullptr;
}

template <typename T>
T LoadEquipMailbox(Core::System& system, VAddr mailbox, size_t offset, int ordering) {
    const auto* pointer =
        system.ApplicationMemory().GetPointer<T>(mailbox + static_cast<VAddr>(offset));
    return pointer ? __atomic_load_n(pointer, ordering) : T{};
}

template <typename T>
void StoreEquipMailbox(Core::System& system, VAddr mailbox, size_t offset, T value, int ordering) {
    auto* pointer =
        system.ApplicationMemory().GetPointer<T>(mailbox + static_cast<VAddr>(offset));
    if (pointer) {
        __atomic_store_n(pointer, value, ordering);
    }
}

bool StoreEquipMailboxString(Core::System& system, VAddr mailbox, size_t offset,
                             size_t capacity, std::string_view value) {
    if (value.empty() || value.size() >= capacity) {
        return false;
    }
    auto& memory = system.ApplicationMemory();
    auto* first = memory.GetPointer<u8>(mailbox + static_cast<VAddr>(offset));
    auto* last = memory.GetPointer<u8>(mailbox + static_cast<VAddr>(offset + capacity - 1));
    if (!first || !last || last != first + capacity - 1) {
        return false;
    }
    std::memset(first, 0, capacity);
    std::memcpy(first, value.data(), value.size());
    return true;
}

std::string HexAddress(VAddr address) {
    std::ostringstream out;
    out << "0x" << std::uppercase << std::hex << address;
    return out.str();
}

std::string JsonString(std::string_view value) {
    std::ostringstream out;
    out << '"';
    for (const char c : value) {
        switch (c) {
        case '\\': out << "\\\\"; break;
        case '"': out << "\\\""; break;
        case '\n': out << "\\n"; break;
        case '\r': out << "\\r"; break;
        case '\t': out << "\\t"; break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) {
                out << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                    << static_cast<unsigned>(static_cast<unsigned char>(c));
            } else {
                out << c;
            }
        }
    }
    out << '"';
    return out.str();
}

struct ActionDiagnosticSnapshot {
    u32 sequence{};
    u32 validation_flags{};
    s32 item_type{};
    s32 item_use{};
    s32 quantity_before{};
    u32 in_inventory_before{};
    u32 tab{ActionDiagnosticUnavailable};
    u32 slot{ActionDiagnosticUnavailable};
    u32 held_before{ActionDiagnosticUnavailable};
    u32 native_result{};
    VAddr target{};
    VAddr pouch_manager{};
    VAddr player{};
    VAddr controller{};
    VAddr active_flag_pointer{};
    u32 controller_state{ActionDiagnosticUnavailable};
    u32 controller_action{ActionDiagnosticUnavailable};
    VAddr controller_target{};
    u32 controller_active{ActionDiagnosticUnavailable};
    u32 node_offset{ActionDiagnosticUnavailable};
    VAddr tab_head{};
    u32 tab_item_count{ActionDiagnosticUnavailable};
    s32 guest_quantity_after{-1};
    u32 guest_in_inventory_after{ActionDiagnosticUnavailable};
};

ActionDiagnosticSnapshot ReadActionDiagnosticSnapshot(Core::System& system, VAddr mailbox,
                                                       bool available) {
    if (!available) {
        return {};
    }
    ActionDiagnosticSnapshot result;
    result.sequence = LoadEquipMailbox<u32>(system, mailbox, ActionDiagnosticSequenceOffset,
                                            __ATOMIC_ACQUIRE);
    if (result.sequence == 0) {
        return {};
    }
    result.validation_flags = LoadEquipMailbox<u32>(
        system, mailbox, ActionDiagnosticValidationFlagsOffset, __ATOMIC_RELAXED);
    result.item_type = LoadEquipMailbox<s32>(system, mailbox, ActionDiagnosticItemTypeOffset,
                                             __ATOMIC_RELAXED);
    result.item_use = LoadEquipMailbox<s32>(system, mailbox, ActionDiagnosticItemUseOffset,
                                            __ATOMIC_RELAXED);
    result.quantity_before = LoadEquipMailbox<s32>(
        system, mailbox, ActionDiagnosticQuantityBeforeOffset, __ATOMIC_RELAXED);
    result.in_inventory_before = LoadEquipMailbox<u32>(
        system, mailbox, ActionDiagnosticInInventoryBeforeOffset, __ATOMIC_RELAXED);
    result.tab = LoadEquipMailbox<u32>(system, mailbox, ActionDiagnosticTabOffset,
                                       __ATOMIC_RELAXED);
    result.slot = LoadEquipMailbox<u32>(system, mailbox, ActionDiagnosticSlotOffset,
                                        __ATOMIC_RELAXED);
    result.held_before = LoadEquipMailbox<u32>(system, mailbox,
                                               ActionDiagnosticHeldBeforeOffset,
                                               __ATOMIC_RELAXED);
    result.native_result = LoadEquipMailbox<u32>(
        system, mailbox, ActionDiagnosticNativeResultOffset, __ATOMIC_RELAXED);
    result.target = LoadEquipMailbox<u64>(system, mailbox, ActionDiagnosticTargetOffset,
                                          __ATOMIC_RELAXED);
    result.pouch_manager = LoadEquipMailbox<u64>(
        system, mailbox, ActionDiagnosticPouchManagerOffset, __ATOMIC_RELAXED);
    result.player = LoadEquipMailbox<u64>(system, mailbox, ActionDiagnosticPlayerOffset,
                                          __ATOMIC_RELAXED);
    result.controller = LoadEquipMailbox<u64>(system, mailbox, ActionDiagnosticControllerOffset,
                                              __ATOMIC_RELAXED);
    result.active_flag_pointer = LoadEquipMailbox<u64>(
        system, mailbox, ActionDiagnosticActiveFlagPointerOffset, __ATOMIC_RELAXED);
    result.controller_state = LoadEquipMailbox<u32>(
        system, mailbox, ActionDiagnosticControllerStateOffset, __ATOMIC_RELAXED);
    result.controller_action = LoadEquipMailbox<u32>(
        system, mailbox, ActionDiagnosticControllerActionOffset, __ATOMIC_RELAXED);
    result.controller_target = LoadEquipMailbox<u64>(
        system, mailbox, ActionDiagnosticControllerTargetOffset, __ATOMIC_RELAXED);
    result.controller_active = LoadEquipMailbox<u32>(
        system, mailbox, ActionDiagnosticControllerActiveOffset, __ATOMIC_RELAXED);
    result.node_offset = LoadEquipMailbox<u32>(
        system, mailbox, ActionDiagnosticNodeOffsetOffset, __ATOMIC_RELAXED);
    result.tab_head = LoadEquipMailbox<u64>(system, mailbox, ActionDiagnosticTabHeadOffset,
                                            __ATOMIC_RELAXED);
    result.tab_item_count = LoadEquipMailbox<u32>(
        system, mailbox, ActionDiagnosticTabItemCountOffset, __ATOMIC_RELAXED);
    result.guest_quantity_after = LoadEquipMailbox<s32>(
        system, mailbox, ActionDiagnosticGuestQuantityAfterOffset, __ATOMIC_RELAXED);
    result.guest_in_inventory_after = LoadEquipMailbox<u32>(
        system, mailbox, ActionDiagnosticGuestInInventoryAfterOffset, __ATOMIC_RELAXED);
    return result;
}

void AppendActionDiagnosticJson(std::ostringstream& out,
                                const ActionDiagnosticSnapshot& diagnostic,
                                std::optional<s32> quantity_after,
                                std::optional<bool> in_inventory_after,
                                std::optional<size_t> held_after) {
    const auto append_optional_u32 = [&](u32 value) {
        if (value == ActionDiagnosticUnavailable) {
            out << "null";
        } else {
            out << value;
        }
    };
    out << R"(,"actionDiagnostic":{"sequence":)" << diagnostic.sequence
        << R"(,"validationFlags":)" << diagnostic.validation_flags
        << R"(,"itemType":)" << diagnostic.item_type
        << R"(,"itemUse":)" << diagnostic.item_use
        << R"(,"quantityBefore":)" << diagnostic.quantity_before
        << R"(,"inInventoryBefore":)"
        << (diagnostic.in_inventory_before != 0 ? "true" : "false")
        << R"(,"tab":)";
    append_optional_u32(diagnostic.tab);
    out << R"(,"slot":)";
    append_optional_u32(diagnostic.slot);
    out << R"(,"nodeOffset":)";
    append_optional_u32(diagnostic.node_offset);
    out << R"(,"tabItemCount":)";
    append_optional_u32(diagnostic.tab_item_count);
    out << R"(,"guestQuantityAfter":)"
        << (diagnostic.guest_quantity_after >= 0
                ? std::to_string(diagnostic.guest_quantity_after)
                : "null")
        << R"(,"guestInInventoryAfter":)";
    if (diagnostic.guest_in_inventory_after == ActionDiagnosticUnavailable) {
        out << "null";
    } else {
        out << (diagnostic.guest_in_inventory_after != 0 ? "true" : "false");
    }
    out << R"(,"heldBefore":)";
    append_optional_u32(diagnostic.held_before);
    out << R"(,"nativeResult":)" << diagnostic.native_result
        << R"(,"target":)" << JsonString(HexAddress(diagnostic.target))
        << R"(,"pouchManager":)" << JsonString(HexAddress(diagnostic.pouch_manager))
        << R"(,"player":)" << JsonString(HexAddress(diagnostic.player))
        << R"(,"controller":)" << JsonString(HexAddress(diagnostic.controller))
        << R"(,"activeFlagPointer":)"
        << JsonString(HexAddress(diagnostic.active_flag_pointer))
        << R"(,"controllerStateBefore":)";
    append_optional_u32(diagnostic.controller_state);
    out << R"(,"controllerActionBefore":)";
    append_optional_u32(diagnostic.controller_action);
    out << R"(,"controllerActiveBefore":)";
    append_optional_u32(diagnostic.controller_active);
    out << R"(,"controllerTargetBefore":)"
        << JsonString(HexAddress(diagnostic.controller_target))
        << R"(,"tabHead":)" << JsonString(HexAddress(diagnostic.tab_head))
        << R"(,"quantityAfter":)"
        << (quantity_after ? std::to_string(*quantity_after) : "null")
        << R"(,"inInventoryAfter":)"
        << (in_inventory_after ? (*in_inventory_after ? "true" : "false") : "null")
        << R"(,"heldAfter":)"
        << (held_after ? std::to_string(*held_after) : "null") << '}';
}

std::string ReadCodeHex(Core::Memory::Memory& memory, VAddr address) {
    std::array<u8, 32> code{};
    if (!address || !memory.IsValidVirtualAddressRange(address, code.size()) ||
        !memory.ReadBlockUnsafe(address, code.data(), code.size())) {
        return {};
    }
    return Common::HexToString(code);
}

std::optional<std::string> ReadCString(Core::Memory::Memory& memory, VAddr address) {
    std::array<char, 96> buffer{};
    if (!address || !memory.IsValidVirtualAddressRange(address, buffer.size()) ||
        !memory.ReadBlockUnsafe(address, buffer.data(), buffer.size())) {
        return std::nullopt;
    }
    const auto end = std::find(buffer.begin(), buffer.end(), '\0');
    if (end == buffer.end() || end == buffer.begin()) {
        return std::nullopt;
    }
    for (auto it = buffer.begin(); it != end; ++it) {
        const auto value = static_cast<unsigned char>(*it);
        if (value < 0x20 || value > 0x7E) {
            return std::nullopt;
        }
    }
    return std::string(buffer.begin(), end);
}

std::optional<PlayerMapSnapshot> ReadPlayerMap(Core::Memory::Memory& memory, VAddr component) {
    std::array<u8, 12 * sizeof(float)> matrix{};
    if (!ReadBytes(memory, component + PlayerMatrixOffset, matrix)) {
        return std::nullopt;
    }
    const float x = DecodeScalar<float>(matrix, 3 * sizeof(float));
    const float y = DecodeScalar<float>(matrix, 7 * sizeof(float));
    const float z = DecodeScalar<float>(matrix, 11 * sizeof(float));
    const float forward_x = DecodeScalar<float>(matrix, 2 * sizeof(float));
    const float forward_z = DecodeScalar<float>(matrix, 10 * sizeof(float));
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) ||
        std::abs(x) > HyruleWorldLimit || std::abs(y) > HyruleWorldLimit ||
        std::abs(z) > HyruleWorldLimit) {
        return std::nullopt;
    }
    float heading = 0.0f;
    if (std::isfinite(forward_x) && std::isfinite(forward_z) &&
        std::hypot(forward_x, forward_z) > 0.01f) {
        constexpr float RadiansToDegrees = 57.29577951308232f;
        heading = std::atan2(forward_x, forward_z) * RadiansToDegrees;
    }
    return PlayerMapSnapshot{x, y, z, heading};
}

std::optional<std::array<float, 3>> ReadMatrixTranslation(Core::Memory::Memory& memory,
                                                          VAddr matrix_address) {
    std::array<u8, 12 * sizeof(float)> matrix{};
    if (!ReadBytes(memory, matrix_address, matrix)) {
        return std::nullopt;
    }
    std::array<float, 3> position{
        DecodeScalar<float>(matrix, 3 * sizeof(float)),
        DecodeScalar<float>(matrix, 7 * sizeof(float)),
        DecodeScalar<float>(matrix, 11 * sizeof(float)),
    };
    if (!std::all_of(position.begin(), position.end(), [](float value) {
            return std::isfinite(value) && std::abs(value) <= HyruleWorldLimit;
        })) {
        return std::nullopt;
    }
    return position;
}

std::vector<ActiveEffectSnapshot> ReadActiveCookEffects(Core::Memory::Memory& memory,
                                                        VAddr player_actor) {
    std::vector<ActiveEffectSnapshot> effects;
    effects.reserve(8);
    const auto add_effect = [&](std::string_view name, VAddr frames_offset, VAddr level_offset) {
        const auto frames = Read<float>(memory, player_actor + frames_offset);
        const auto level = Read<s32>(memory, player_actor + level_offset);
        if (frames && level && std::isfinite(*frames) && *frames > 0.0f &&
            *frames <= static_cast<float>(MaximumCookEffectSeconds) * CookEffectFramesPerSecond &&
            *level > 0 && *level <= 3) {
            effects.push_back({name,
                               static_cast<s32>(std::ceil(*frames / CookEffectFramesPerSecond)),
                               static_cast<float>(*level)});
        }
    };
    add_effect("Attack Up", PlayerAttackEffectFramesOffset, PlayerAttackEffectLevelOffset);
    add_effect("Defense Up", PlayerDefenseEffectFramesOffset, PlayerDefenseEffectLevelOffset);
    add_effect("Heat Resistance", PlayerHeatEffectFramesOffset, PlayerHeatEffectLevelOffset);
    add_effect("Cold Resistance", PlayerColdEffectFramesOffset, PlayerColdEffectLevelOffset);
    add_effect("Flame Guard", PlayerFireproofEffectFramesOffset, PlayerFireproofEffectLevelOffset);
    add_effect("Shock Resistance", PlayerShockEffectFramesOffset, PlayerShockEffectLevelOffset);
    add_effect("Stealth Up", PlayerStealthEffectFramesOffset, PlayerStealthEffectLevelOffset);

    float speed_frames = 0.0f;
    s32 speed_level = 0;
    for (const auto [frames_offset, level_offset] : std::array<std::pair<VAddr, VAddr>, 3>{{
             {PlayerSpeedEffectOneFramesOffset, PlayerSpeedEffectOneLevelOffset},
             {PlayerSpeedEffectTwoFramesOffset, PlayerSpeedEffectTwoLevelOffset},
             {PlayerSpeedEffectThreeFramesOffset, PlayerSpeedEffectThreeLevelOffset},
         }}) {
        const auto frames = Read<float>(memory, player_actor + frames_offset);
        const auto level = Read<s32>(memory, player_actor + level_offset);
        if (frames && level && std::isfinite(*frames) && *frames > 0.0f &&
            *frames <= static_cast<float>(MaximumCookEffectSeconds) * CookEffectFramesPerSecond &&
            *level > 0 && *level <= 3) {
            speed_frames = std::max(speed_frames, *frames);
            speed_level = std::max(speed_level, *level);
        }
    }
    if (speed_frames > 0.0f) {
        effects.push_back({"Speed Up",
                           static_cast<s32>(std::ceil(speed_frames / CookEffectFramesPerSecond)),
                           static_cast<float>(speed_level)});
    }
    return effects;
}

std::optional<std::vector<QuestSnapshot>> ReadQuests(Core::Memory::Memory& memory,
                                                     VAddr main_base) {
    const auto manager = Read<u64>(memory, main_base + QuestManagerStaticOffset);
    if (!manager || !*manager) {
        return std::nullopt;
    }
    const auto count = Read<s32>(memory, *manager + QuestArrayCountOffset);
    const auto pointers = Read<u64>(memory, *manager + QuestArrayPointersOffset);
    if (!count || *count < 0 || *count > MaxQuestCount ||
        (*count > 0 && (!pointers || !*pointers))) {
        return std::nullopt;
    }

    std::vector<u64> quest_pointers(static_cast<size_t>(*count));
    if (*count > 0 &&
        (!memory.IsValidVirtualAddressRange(*pointers, quest_pointers.size() * sizeof(u64)) ||
         !memory.ReadBlockUnsafe(*pointers, quest_pointers.data(),
                                 quest_pointers.size() * sizeof(u64)))) {
        return std::nullopt;
    }

    std::vector<QuestSnapshot> quests;
    quests.reserve(quest_pointers.size());
    for (const VAddr quest : quest_pointers) {
        std::array<u8, QuestCurrentStepOffset + sizeof(s32)> fields{};
        if (!quest || !ReadBytes(memory, quest, fields)) {
            continue;
        }
        const u32 state = DecodeScalar<u32>(fields, QuestStateOffset);
        if (state != 1) {
            continue;
        }
        const s32 current_step = DecodeScalar<s32>(fields, QuestCurrentStepOffset);
        const s32 step_count = DecodeScalar<s32>(fields, QuestStepCountOffset);
        const VAddr step_pointers = DecodeScalar<u64>(fields, QuestStepPointersOffset);
        if (current_step < 0 || step_count <= 0 || current_step >= step_count ||
            step_count > 256 || !step_pointers) {
            continue;
        }

        const VAddr name_pointer = DecodeScalar<u64>(fields, QuestNamePointerOffset);
        const auto actor_name = ReadCString(memory, name_pointer);
        const auto step_pointer =
            Read<u64>(memory, step_pointers + static_cast<u64>(current_step) * sizeof(u64));
        if (!actor_name || !step_pointer || !*step_pointer) {
            continue;
        }
        std::array<u8, QuestStepNamePointerOffset + sizeof(u64)> step_fields{};
        if (!ReadBytes(memory, *step_pointer, step_fields)) {
            continue;
        }
        const auto message_name = ReadCString(
            memory, DecodeScalar<u64>(step_fields, QuestStepMessagePointerOffset));
        const auto step_name =
            ReadCString(memory, DecodeScalar<u64>(step_fields, QuestStepNamePointerOffset));
        const std::string message = message_name.value_or("");
        const std::string step = step_name.value_or("");




        const std::string label_base = QuestLabelBase(*actor_name);
        const std::string title_label = label_base + "_Name";
        const QuestText* localized_title = FindQuestText(title_label);
        const QuestText* localized_objective = FindQuestStepText(label_base, message);
        if (!localized_objective && !step.empty()) {
            localized_objective = FindQuestStepText(label_base, step);
        }
        if (!localized_objective) {
            localized_objective = FindQuestText(label_base + "_Desc");
        }
        const auto location = ReadCString(
            memory, DecodeScalar<u64>(fields, QuestLocationPointerOffset));
        const auto type =
            ReadCString(memory, DecodeScalar<u64>(fields, QuestTypePointerOffset));
        quests.push_back(QuestSnapshot{
            quest,
            *actor_name,
            localized_title ? std::string(localized_title->text) : *actor_name,
            localized_objective ? std::string(localized_objective->text) : std::string{},
            "In Progress",
            location.value_or(""),
            type.value_or(""),
            false,
        });
    }
    std::stable_sort(quests.begin(), quests.end(), [](const auto& left, const auto& right) {
        if (left.complete != right.complete) {
            return !left.complete;
        }
        return left.title < right.title;
    });
    return quests;
}

std::optional<VAddr> CachedHealthAddress(u64 process_id, VAddr component, VAddr getter) {
    std::scoped_lock lock{HealthCache.mutex};
    if (HealthCache.process_id == process_id && HealthCache.component == component &&
        HealthCache.getter == getter) {
        return HealthCache.address;
    }
    return std::nullopt;
}

void CacheHealthAddress(u64 process_id, VAddr component, VAddr getter, VAddr address) {
    std::scoped_lock lock{HealthCache.mutex};
    HealthCache.process_id = process_id;
    HealthCache.component = component;
    HealthCache.getter = getter;
    HealthCache.address = address;
}

const char* PouchCategoryName(s32 type) {
    switch (type) {
    case 0: return "Weapons";
    case 1:
    case 2: return "Bows";
    case 3: return "Shields";
    case 4:
    case 5:
    case 6: return "Armor";
    case 7: return "Materials";
    case 8: return "Food";
    case 9: return "Key Items";
    default: return "";
    }
}

const char* EquipmentSlotName(s32 type) {
    switch (type) {
    case 0: return "Weapon";
    case 1: return "Bow";
    case 3: return "Shield";
    case 4: return "Head";
    case 5: return "Chest";
    case 6: return "Legs";
    default: return "";
    }
}

std::optional<size_t> ReadGrabbedItemCount(Core::Memory::Memory& memory, VAddr manager) {
    size_t count = 0;
    for (size_t index = 0; index < PouchGrabbedItemCapacity; ++index) {
        const auto item = Read<u64>(
            memory, manager + PouchGrabbedItemsOffset + index * PouchGrabbedItemStride);
        if (!item) {
            return std::nullopt;
        }
        if (*item != 0) {
            ++count;
        }
    }
    return count;
}

struct PouchSelection {
    u32 tab{};
    u32 slot{};
    u32 node_offset{};
    u32 tab_item_count{};
    VAddr tab_head{};
};

std::optional<PouchSelection> FindPouchSelection(Core::Memory::Memory& memory, VAddr manager,
                                                  VAddr target) {
    const VAddr list_sentinel = manager + PouchListOffset;
    const auto raw_node_offset = Read<s32>(memory, manager + PouchListOffset +
                                                       OffsetListNodeOffset);
    if (!raw_node_offset || *raw_node_offset <= 0 || *raw_node_offset > 0x100) {
        return std::nullopt;
    }
    const auto node_offset = static_cast<u32>(*raw_node_offset);
    for (u32 tab = 0; tab < PouchTabCount; ++tab) {
        const auto head = Read<u64>(memory, manager + PouchTabsOffset + tab * sizeof(u64));
        if (!head) {
            return std::nullopt;
        }
        if (*head == 0) {
            continue;
        }
        const auto head_type = Read<s32>(memory, *head + PouchItemTypeOffset);
        if (!head_type) {
            return std::nullopt;
        }
        const s32 tab_group = PouchTabGroup(*head_type);
        VAddr item = *head;
        for (u32 slot = 0; item && slot < PouchItemsPerTab; ++slot) {
            const auto item_type = Read<s32>(memory, item + PouchItemTypeOffset);
            if (!item_type) {
                return std::nullopt;
            }
            if (PouchTabGroup(*item_type) != tab_group) {
                break;
            }
            if (item == target) {
                u32 tab_item_count = slot + 1;
                VAddr count_item = item;
                while (tab_item_count < PouchItemsPerTab) {
                    const auto next_node = Read<u64>(
                        memory, count_item + node_offset + OffsetListNextOffset);
                    if (!next_node || *next_node == list_sentinel ||
                        *next_node < node_offset) {
                        break;
                    }
                    const VAddr next_item = *next_node - node_offset;
                    const auto next_type =
                        Read<s32>(memory, next_item + PouchItemTypeOffset);
                    if (!next_type || PouchTabGroup(*next_type) != tab_group) {
                        break;
                    }
                    count_item = next_item;
                    ++tab_item_count;
                }
                return PouchSelection{tab, slot, node_offset, tab_item_count, *head};
            }
            const auto next_node =
                Read<u64>(memory, item + node_offset + OffsetListNextOffset);
            if (!next_node) {
                return std::nullopt;
            }
            if (*next_node == list_sentinel) {
                break;
            }
            if (*next_node < node_offset) {
                return std::nullopt;
            }
            item = *next_node - node_offset;
        }
    }
    return std::nullopt;
}

std::optional<std::vector<PouchItemSnapshot>> ReadPouchItemsOnce(Core::Memory::Memory& memory,
                                                                  VAddr manager) {
    const VAddr list = manager + PouchListOffset;
    constexpr size_t ListFieldsSize =
        OffsetListNodeOffset + sizeof(s32) - OffsetListNextOffset;
    std::array<u8, ListFieldsSize> list_fields{};
    if (!ReadBytes(memory, list + OffsetListNextOffset, list_fields)) {
        return std::nullopt;
    }
    auto node = DecodeScalar<u64>(list_fields, 0);
    const auto count =
        DecodeScalar<s32>(list_fields, OffsetListCountOffset - OffsetListNextOffset);
    const auto node_offset =
        DecodeScalar<s32>(list_fields, OffsetListNodeOffset - OffsetListNextOffset);
    if (count < 0 || count > MaxPouchItems || node_offset != 8) {
        return std::nullopt;
    }

    std::vector<PouchItemSnapshot> items;
    items.reserve(static_cast<size_t>(count));



    std::unordered_map<VAddr, std::string> actor_names;
    actor_names.reserve(static_cast<size_t>(count));




    constexpr VAddr ItemFieldsOffset = 0x10;
    constexpr size_t ItemFieldsSize =
        PouchItemNamePointerOffset + sizeof(u64) - ItemFieldsOffset;
    constexpr size_t ModifierFieldsSize =
        PouchItemModifierFlagsOffset + sizeof(u32) - PouchItemModifierValueOffset;



    const size_t expected_nodes = static_cast<size_t>(count);
    size_t visited_nodes = 0;
    while (node != list && visited_nodes < expected_nodes) {
        ++visited_nodes;
        if (node < static_cast<u64>(node_offset)) {
            return std::nullopt;
        }
        const VAddr item = node - static_cast<u64>(node_offset);
        std::array<u8, ItemFieldsSize> item_fields{};
        if (!ReadBytes(memory, item + ItemFieldsOffset, item_fields)) {
            return std::nullopt;
        }
        const auto next_node = DecodeScalar<u64>(item_fields, 0);
        const auto type =
            DecodeScalar<s32>(item_fields, PouchItemTypeOffset - ItemFieldsOffset);
        const auto item_use =
            DecodeScalar<s32>(item_fields, PouchItemUseOffset - ItemFieldsOffset);
        const auto value =
            DecodeScalar<s32>(item_fields, PouchItemValueOffset - ItemFieldsOffset);
        const auto equipped =
            DecodeScalar<u8>(item_fields, PouchItemEquippedOffset - ItemFieldsOffset);
        const auto in_inventory =
            DecodeScalar<u8>(item_fields, PouchItemInInventoryOffset - ItemFieldsOffset);
        const auto name_pointer =
            DecodeScalar<u64>(item_fields, PouchItemNamePointerOffset - ItemFieldsOffset);
        if (type < 0 || type > 9 || equipped > 1 || in_inventory > 1) {
            return std::nullopt;
        }
        auto name = actor_names.find(name_pointer);
        if (name == actor_names.end()) {
            const auto actor_name = ReadCString(memory, name_pointer);
            if (!actor_name) {
                return std::nullopt;
            }
            name = actor_names.emplace(name_pointer, *actor_name).first;
        }
        u32 modifier_value = 0;
        u32 modifier_flags = 0;
        s32 cook_health_recover = 0;
        s32 cook_effect_duration = 0;
        s32 cook_sell_price = 0;
        s32 cook_effect_id = -1;
        float cook_effect_level = 0.0f;
        if (type <= 3) {
            std::array<u8, ModifierFieldsSize> modifier_fields{};
            if (!ReadBytes(memory, item + PouchItemModifierValueOffset, modifier_fields)) {
                return std::nullopt;
            }
            modifier_value = DecodeScalar<u32>(modifier_fields, 0);
            modifier_flags = DecodeScalar<u32>(
                modifier_fields, PouchItemModifierFlagsOffset - PouchItemModifierValueOffset);
            if ((modifier_flags & ~WeaponModifierKnownMask) != 0) {
                return std::nullopt;
            }
        } else if (type == 8) {
            constexpr size_t CookFieldsSize =
                PouchItemCookEffectLevelOffset + sizeof(float) - PouchItemCookHealthRecoverOffset;
            std::array<u8, CookFieldsSize> cook_fields{};
            if (!ReadBytes(memory, item + PouchItemCookHealthRecoverOffset, cook_fields)) {
                return std::nullopt;
            }
            cook_health_recover = DecodeScalar<s32>(cook_fields, 0);
            cook_effect_duration = DecodeScalar<s32>(
                cook_fields, PouchItemCookEffectDurationOffset - PouchItemCookHealthRecoverOffset);
            cook_sell_price = DecodeScalar<s32>(
                cook_fields, PouchItemCookSellPriceOffset - PouchItemCookHealthRecoverOffset);
            const float effect_id = DecodeScalar<float>(
                cook_fields, PouchItemCookEffectIdOffset - PouchItemCookHealthRecoverOffset);
            cook_effect_level = DecodeScalar<float>(
                cook_fields, PouchItemCookEffectLevelOffset - PouchItemCookHealthRecoverOffset);
            if (!std::isfinite(effect_id) || !std::isfinite(cook_effect_level) ||
                effect_id < -1.0f || effect_id > 16.0f || std::trunc(effect_id) != effect_id ||
                cook_effect_level < 0.0f || cook_effect_level > 10000.0f ||
                cook_health_recover < 0 || cook_health_recover > 10000 ||
                cook_effect_duration < 0 || cook_effect_duration > 86400 ||
                cook_sell_price < 0 || cook_sell_price > 1000000) {
                return std::nullopt;
            }
            cook_effect_id = static_cast<s32>(effect_id);
        }
        if (in_inventory) {
            items.push_back({item,
                             type,
                             item_use,
                             value,
                             modifier_value,
                             modifier_flags,
                             cook_health_recover,
                             cook_effect_duration,
                             cook_sell_price,
                             cook_effect_id,
                             cook_effect_level,
                             equipped != 0,
                             name->second});
        }
        node = next_node;
    }
    if (node != list || visited_nodes != expected_nodes || items.size() > expected_nodes) {
        return std::nullopt;
    }
    std::array<u8, ListFieldsSize> final_list_fields{};
    if (!ReadBytes(memory, list + OffsetListNextOffset, final_list_fields) ||
        final_list_fields != list_fields) {
        return std::nullopt;
    }
    return items;
}

std::optional<std::vector<PouchItemSnapshot>> ReadPouchItems(Core::Memory::Memory& memory,
                                                              VAddr manager) {
    for (int attempt = 0; attempt < 2; ++attempt) {
        if (auto items = ReadPouchItemsOnce(memory, manager)) {
            return items;
        }
    }
    return std::nullopt;
}

u32 ChampionCooldownSeconds(const std::optional<float>& timer,
                            const std::optional<float>& rate) {


    if (!timer || !rate || !std::isfinite(*timer) || !std::isfinite(*rate) || *timer <= 0.0f ||
        *timer > 2592000.0f || *rate >= 0.0f || *rate < -64.0f) {
        return 0;
    }
    const double frames_per_tick = -static_cast<double>(*rate);
    const double seconds = std::ceil(static_cast<double>(*timer) / (30.0 * frames_per_tick));
    return static_cast<u32>(std::min<double>(seconds, std::numeric_limits<u32>::max()));
}

std::array<ChampionPowerSnapshot, 4> ReadChampionPowers(
    Core::Memory::Memory& memory, VAddr main_base,
    const std::vector<PouchItemSnapshot>& pouch_items) {
    const auto timer_root = Read<u64>(memory, main_base + ChampionTimerRootOffset);
    const auto timer_component = timer_root && *timer_root
                                     ? Read<u64>(memory, *timer_root + ChampionTimerComponentOffset)
                                     : std::nullopt;

    const auto read_timer = [&](VAddr offset) -> std::optional<float> {
        return timer_component && *timer_component
                   ? Read<float>(memory, *timer_component + offset)
                   : std::nullopt;
    };
    const auto read_uses = [&](VAddr offset) -> u32 {
        const auto value = timer_component && *timer_component
                               ? Read<u32>(memory, *timer_component + offset)
                               : std::nullopt;
        return value && *value <= 3 ? *value : 0;
    };
    const auto soul_state = [&](std::string_view base_name,
                                std::string_view dlc_name) -> std::pair<bool, bool> {
        bool available = false;
        bool enabled = false;
        for (const auto& item : pouch_items) {
            if (item.name == base_name || item.name == dlc_name) {
                available = true;
                enabled = enabled || item.equipped;
            }
        }
        return {available, enabled};
    };

    const auto gale_state = soul_state("Obj_HeroSoul_Rito", "Obj_DLC_HeroSoul_Rito");
    const auto fury_state = soul_state("Obj_HeroSoul_Gerudo", "Obj_DLC_HeroSoul_Gerudo");
    const auto protection_state =
        soul_state("Obj_HeroSoul_Goron", "Obj_DLC_HeroSoul_Goron");
    const auto grace_state = soul_state("Obj_HeroSoul_Zora", "Obj_DLC_HeroSoul_Zora");
    const u32 gale_cooldown = ChampionCooldownSeconds(
        read_timer(RevaliGaleTimerOffset), read_timer(RevaliGaleTimerRateOffset));
    const u32 fury_cooldown = ChampionCooldownSeconds(
        read_timer(UrbosaFuryTimerOffset), read_timer(UrbosaFuryTimerRateOffset));
    const u32 protection_cooldown = ChampionCooldownSeconds(
        read_timer(DarukProtectionTimerOffset), read_timer(DarukProtectionTimerRateOffset));
    const u32 grace_cooldown = ChampionCooldownSeconds(
        read_timer(MiphaGraceTimerOffset), read_timer(MiphaGraceTimerRateOffset));

    return {{
        {"gale", "Revali's Gale", gale_state.first, gale_state.second,
         gale_state.first && gale_cooldown == 0 ? read_uses(RevaliGaleUsesOffset) : 0U,
         gale_state.first ? 3U : 0U,
         gale_state.first ? gale_cooldown : 0U},
        {"fury", "Urbosa's Fury", fury_state.first, fury_state.second,
         fury_state.first && fury_cooldown == 0 ? read_uses(UrbosaFuryUsesOffset) : 0U,
         fury_state.first ? 3U : 0U,
         fury_state.first ? fury_cooldown : 0U},
        {"protection", "Daruk's Protection", protection_state.first, protection_state.second,
         protection_state.first && protection_cooldown == 0
             ? read_uses(DarukProtectionUsesOffset)
             : 0U,
         protection_state.first ? 3U : 0U,
         protection_state.first ? protection_cooldown : 0U},
        {"grace", "Mipha's Grace", grace_state.first, grace_state.second,
         grace_state.first && grace_cooldown == 0 ? 1U : 0U, grace_state.first ? 1U : 0U,
         grace_state.first ? grace_cooldown : 0U},
    }};
}

void AppendChampionPowerJson(std::ostringstream& out, const ChampionPowerSnapshot& power) {
    out << R"({"id":)" << JsonString(power.id) << R"(,"name":)" << JsonString(power.name)
        << R"(,"available":)" << (power.available ? "true" : "false")
        << R"(,"enabled":)" << (power.enabled ? "true" : "false")
        << R"(,"uses":)" << power.uses << R"(,"maxUses":)" << power.max_uses
        << R"(,"cooldownSeconds":)" << power.cooldown_seconds << '}';
}

void AppendPlayerMapJson(std::ostringstream& out, const PlayerMapSnapshot& map) {
    out << R"({"x":)" << map.x << R"(,"y":)" << map.y << R"(,"z":)" << map.z
        << R"(,"heading":)" << map.heading << '}';
}

void AppendPositionJson(std::ostringstream& out,
                        const std::optional<std::array<float, 3>>& position) {
    if (!position) {
        out << "null";
        return;
    }
    out << R"({"x":)" << position->at(0) << R"(,"y":)" << position->at(1)
        << R"(,"z":)" << position->at(2) << '}';
}

void AppendQuestJson(std::ostringstream& out, const QuestSnapshot& quest) {
    out << R"({"id":)" << quest.id << R"(,"actorName":)" << JsonString(quest.actor_name)
        << R"(,"name":)" << JsonString(quest.title) << R"(,"objective":)"
        << JsonString(quest.objective) << R"(,"step":)" << JsonString(quest.step)
        << R"(,"location":)" << JsonString(quest.location) << R"(,"type":)"
        << JsonString(quest.type) << R"(,"complete":)"
        << (quest.complete ? "true" : "false") << '}';
}

void AppendPouchItemJson(std::ostringstream& out, const PouchItemSnapshot& item) {
    const auto* localized = FindItemText(item.name);
    const bool equippable = item.type == 0 || item.type == 1 || item.type == 3 ||
                           (item.type >= 4 && item.type <= 6);
    std::string icon_actor_name = item.name;
    if (item.name == "Item_Cook_C_17") {
        switch (item.cook_effect_id) {
        case 2: icon_actor_name += "_LifeMaxUp"; break;
        case 4: icon_actor_name += "_ResistHot"; break;
        case 5: icon_actor_name += "_ResistCold"; break;
        case 6: icon_actor_name += "_ResistElectric"; break;
        case 10: icon_actor_name += "_AttackUp"; break;
        case 11: icon_actor_name += "_DefenseUp"; break;
        case 12: icon_actor_name += "_Quietness"; break;
        case 13: icon_actor_name += "_AllSpeed"; break;
        case 14: icon_actor_name += "_GutsRecover"; break;
        case 15: icon_actor_name += "_ExGutsMaxUp"; break;
        case 16: icon_actor_name += "_Fireproof"; break;
        default: break;
        }
    }
    out << R"({"id":)" << item.address << R"(,"itemType":)" << item.type
        << R"(,"itemUse":)" << item.item_use
        << R"(,"actorName":)" << JsonString(item.name)
        << R"(,"iconActorName":)" << JsonString(icon_actor_name)
        << R"(,"name":)" << JsonString(localized ? localized->name : item.name)
        << R"(,"category":)" << JsonString(PouchCategoryName(item.type))
        << R"(,"equippable":)" << (equippable ? "true" : "false")
        << R"(,"holdable":)" << (IsHoldablePouchItem(item) ? "true" : "false")
        << R"(,"edible":)" << (IsEdiblePouchItem(item) ? "true" : "false");
    if (item.type <= 3) {
        out << R"(,"durability":)" << item.value << R"(,"modifierValue":)"
            << item.modifier_value << R"(,"modifierFlags":)" << item.modifier_flags;
        if (const auto power = GetItemPower(item)) {
            out << R"(,"power":)" << *power;
        }
    } else if (item.type <= 6) {
        if (const auto defense = GetItemPower(item)) {
            out << R"(,"defense":)" << *defense;
        }
    } else {
        out << R"(,"count":)" << item.value;
        if (item.type == 8) {
            out << R"(,"healthRecover":)" << item.cook_health_recover
                << R"(,"effectDuration":)" << item.cook_effect_duration
                << R"(,"sellPrice":)" << item.cook_sell_price
                << R"(,"effectId":)" << item.cook_effect_id
                << R"(,"effectLevel":)" << item.cook_effect_level;
        }
    }
    out << R"(,"equipped":)" << (item.equipped ? "true" : "false")
        << R"(,"description":)" << JsonString(localized ? localized->description : "") << '}';
}

struct EquippedStats {
    std::optional<s32> attack;
    std::optional<s32> bow_attack;
    std::optional<s32> shield_guard;
    std::optional<s32> defense;
};

EquippedStats GetEquippedStats(const std::vector<PouchItemSnapshot>& items) {
    EquippedStats result;
    s32 defense = 0;
    for (const auto& item : items) {
        if (!item.equipped) {
            continue;
        }
        const auto power = GetItemPower(item);
        switch (item.type) {
        case 0: result.attack = power; break;
        case 1: result.bow_attack = power; break;
        case 3: result.shield_guard = power; break;
        case 4:
        case 5:
        case 6:
            if (power) {
                defense += *power;
            }
            break;
        default: break;
        }
    }
    result.defense = defense;
    return result;
}



std::optional<VAddr> ResolvePointerGetter(Core::Memory::Memory& memory, VAddr object,
                                          VAddr function) {
    std::array<u64, 32> regs{};
    regs[0] = object;

    for (u32 index = 0; index < 12; ++index) {
        const auto instruction = Read<u32>(memory, function + index * sizeof(u32));
        if (!instruction) {
            return std::nullopt;
        }
        const u32 word = *instruction;
        if (word == 0xD65F03C0) {
            return regs[0];
        }

        if ((word & 0xFF000000) == 0x91000000) {
            const u32 destination = word & 0x1F;
            const u32 source = (word >> 5) & 0x1F;
            u64 immediate = (word >> 10) & 0xFFF;
            if ((word >> 22) & 1) {
                immediate <<= 12;
            }
            regs[destination] = regs[source] + immediate;
            continue;
        }

        if ((word & 0xFFC00000) == 0xF9400000) {
            const u32 destination = word & 0x1F;
            const u32 source = (word >> 5) & 0x1F;
            const u64 immediate = ((word >> 10) & 0xFFF) * sizeof(u64);
            const auto value = Read<u64>(memory, regs[source] + immediate);
            if (!value) {
                return std::nullopt;
            }
            regs[destination] = *value;
            continue;
        }

        return std::nullopt;
    }
    return std::nullopt;
}

std::optional<s32> ResolveS32Getter(Core::Memory::Memory& memory, VAddr object, VAddr function) {
    std::array<u64, 32> regs{};
    regs[0] = object;
    for (u32 index = 0; index < 12; ++index) {
        const auto instruction = Read<u32>(memory, function + index * sizeof(u32));
        if (!instruction) {
            return std::nullopt;
        }
        const u32 word = *instruction;
        if (word == 0xD65F03C0) {
            return static_cast<s32>(static_cast<u32>(regs[0]));
        }
        if ((word & 0xFF000000) == 0x91000000) {
            const u32 destination = word & 0x1F;
            const u32 source = (word >> 5) & 0x1F;
            u64 immediate = (word >> 10) & 0xFFF;
            if ((word >> 22) & 1) {
                immediate <<= 12;
            }
            regs[destination] = regs[source] + immediate;
            continue;
        }
        if ((word & 0xFFC00000) == 0xF9400000) {
            const u32 destination = word & 0x1F;
            const u32 source = (word >> 5) & 0x1F;
            const auto value = Read<u64>(memory, regs[source] + ((word >> 10) & 0xFFF) * 8);
            if (!value) {
                return std::nullopt;
            }
            regs[destination] = *value;
            continue;
        }
        if ((word & 0xFFC00000) == 0xB9400000) {
            const u32 destination = word & 0x1F;
            const u32 source = (word >> 5) & 0x1F;
            const auto value = Read<u32>(memory, regs[source] + ((word >> 10) & 0xFFF) * 4);
            if (!value) {
                return std::nullopt;
            }
            regs[destination] = *value;
            continue;
        }
        if ((word & 0xFFC00000) == 0x39400000) {
            const u32 destination = word & 0x1F;
            const u32 source = (word >> 5) & 0x1F;
            const auto value = Read<u8>(memory, regs[source] + ((word >> 10) & 0xFFF));
            if (!value) {
                return std::nullopt;
            }
            regs[destination] = *value;
            continue;
        }
        if ((word & 0xFFFFFC00) == 0x12000000) {
            const u32 destination = word & 0x1F;
            const u32 source = (word >> 5) & 0x1F;
            regs[destination] = static_cast<u32>(regs[source]) & 1U;
            continue;
        }
        return std::nullopt;
    }
    return std::nullopt;
}

std::optional<s32> ReadGameDataS32(Core::Memory::Memory& memory, VAddr main_base,
                                   VAddr handle_offset) {
    const auto manager_storage = Read<u64>(memory, main_base + GameDataManagerGotOffset);
    const auto manager = manager_storage ? Read<u64>(memory, *manager_storage) : std::nullopt;
    const auto handle = Read<u32>(memory, main_base + handle_offset);
    if (!manager || !*manager || !handle || *handle == 0xFFFFFFFF) {
        return std::nullopt;
    }

    u32 index = *handle;
    const auto flags = Read<u32>(memory, *manager + GameDataFlagsOffset);
    if (!flags) {
        return std::nullopt;
    }
    if (*flags & 0x8000) {
        const auto generation = Read<u32>(memory, *manager + GameDataHandleGenerationOffset);
        if (!generation || *generation != (*handle >> 24)) {
            return std::nullopt;
        }
        index &= 0x00FFFFFF;
    }

    const auto buffer_holder = Read<u64>(memory, *manager + GameDataFlagBufferHolderOffset);
    const auto buffer = buffer_holder ? Read<u64>(memory, *buffer_holder) : std::nullopt;
    if (!buffer || !*buffer) {
        return std::nullopt;
    }
    const auto count = Read<u32>(memory, *buffer + 0x18);
    const auto entries = Read<u64>(memory, *buffer + 0x20);
    if (!count || !entries || index >= *count) {


        index = *handle & 0x00FFFFFF;
        if (!count || !entries || index >= *count) {
            return std::nullopt;
        }
    }
    const auto entry = Read<u64>(memory, *entries + static_cast<u64>(index) * sizeof(u64));
    const auto vtable = entry && *entry ? Read<u64>(memory, *entry) : std::nullopt;
    const auto getter = vtable ? Read<u64>(memory, *vtable + 0x88) : std::nullopt;
    const auto value = getter ? ResolveS32Getter(memory, *entry, *getter) : std::nullopt;
    if (!value || *value < 0 || *value > 9999999) {
        return std::nullopt;
    }
    return value;
}

std::optional<bool> ReadGameDataBool(Core::Memory::Memory& memory, VAddr main_base,
                                     VAddr handle_offset) {
    const auto manager_storage = Read<u64>(memory, main_base + GameDataManagerGotOffset);
    const auto manager = manager_storage ? Read<u64>(memory, *manager_storage) : std::nullopt;
    const auto handle = Read<u32>(memory, main_base + handle_offset);
    if (!manager || !*manager || !handle || *handle == 0xFFFFFFFF) {
        return std::nullopt;
    }

    u32 index = *handle;
    const auto flags = Read<u32>(memory, *manager + GameDataFlagsOffset);
    if (!flags) {
        return std::nullopt;
    }
    if (*flags & 0x8000) {
        const auto generation = Read<u32>(memory, *manager + GameDataHandleGenerationOffset);
        if (!generation || *generation != (*handle >> 24)) {
            return std::nullopt;
        }
        index &= 0x00FFFFFF;
    }

    const auto buffer_holder = Read<u64>(memory, *manager + GameDataFlagBufferHolderOffset);
    const auto buffer = buffer_holder ? Read<u64>(memory, *buffer_holder) : std::nullopt;
    if (!buffer || !*buffer) {
        return std::nullopt;
    }


    const auto count = Read<u32>(memory, *buffer + 0x08);
    const auto entries = Read<u64>(memory, *buffer + 0x10);
    if (!count || !entries || index >= *count) {
        index = *handle & 0x00FFFFFF;
        if (!count || !entries || index >= *count) {
            return std::nullopt;
        }
    }
    const auto entry = Read<u64>(memory, *entries + static_cast<u64>(index) * sizeof(u64));
    const auto vtable = entry && *entry ? Read<u64>(memory, *entry) : std::nullopt;
    const auto getter = vtable ? Read<u64>(memory, *vtable + 0x88) : std::nullopt;
    const auto value = getter ? ResolveS32Getter(memory, *entry, *getter) : std::nullopt;
    if (!value) {
        return std::nullopt;
    }
    return (*value & 1) != 0;
}

std::optional<ShrineStateSnapshot> ReadShrineStates(Core::Memory::Memory& memory,
                                                     VAddr main_base, u64 process_id) {
    const auto manager_storage = Read<u64>(memory, main_base + GameDataManagerGotOffset);
    const auto manager = manager_storage ? Read<u64>(memory, *manager_storage) : std::nullopt;
    const auto buffer_holder = manager && *manager
                                   ? Read<u64>(memory, *manager + GameDataFlagBufferHolderOffset)
                                   : std::nullopt;
    const auto buffer = buffer_holder && *buffer_holder
                            ? Read<u64>(memory, *buffer_holder)
                            : std::nullopt;
    if (!buffer || !*buffer) {
        return std::nullopt;
    }




    const auto count = Read<u32>(memory, *buffer + 0x08);
    const auto entries = Read<u64>(memory, *buffer + 0x10);
    const auto generation = Read<u32>(memory, *manager + GameDataFlagBufferGenerationOffset);
    constexpr u32 MaxBoolFlagCount = 100'000;
    if (!count || !entries || !generation || *count == 0 || *count > MaxBoolFlagCount ||
        !*entries) {
        return std::nullopt;
    }

    std::scoped_lock cache_lock{ShrineFlagCache.mutex};
    if (ShrineFlagCache.process_id != process_id || ShrineFlagCache.buffer != *buffer ||
        ShrineFlagCache.entries != *entries || ShrineFlagCache.count != *count ||
        ShrineFlagCache.generation != *generation) {
        std::vector<u64> pointers(*count);
        if (!memory.IsValidVirtualAddressRange(*entries, pointers.size() * sizeof(u64)) ||
            !memory.ReadBlockUnsafe(*entries, pointers.data(), pointers.size() * sizeof(u64))) {
            return std::nullopt;
        }

        const auto find_flag = [&](u32 target_hash) -> VAddr {
            size_t first = 0;
            size_t last = pointers.size();
            while (first < last) {
                const size_t middle = first + (last - first) / 2;
                const VAddr flag = pointers[middle];
                const auto hash = flag ? Read<u32>(memory, flag + 0x0C) : std::nullopt;
                if (!hash) {
                    return 0;
                }
                if (*hash < target_hash) {
                    first = middle + 1;
                } else {
                    last = middle;
                }
            }
            if (first >= pointers.size() || !pointers[first]) {
                return 0;
            }
            const auto hash = Read<u32>(memory, pointers[first] + 0x0C);
            return hash && *hash == target_hash ? pointers[first] : 0;
        };

        ShrineFlagCache.process_id = process_id;
        ShrineFlagCache.buffer = *buffer;
        ShrineFlagCache.entries = *entries;
        ShrineFlagCache.count = *count;
        ShrineFlagCache.generation = *generation;
        for (size_t index = 0; index < BotwShrines.size(); ++index) {
            ShrineFlagCache.entered[index] = find_flag(BotwShrines[index].entered_hash);
            ShrineFlagCache.cleared[index] = find_flag(BotwShrines[index].cleared_hash);
        }
        ShrineFlagCache.sheik_sensor_enabled = find_flag(SheikSensorEnabledHash);
    }

    ShrineStateSnapshot result;
    for (size_t index = 0; index < BotwShrines.size(); ++index) {
        if (ShrineFlagCache.entered[index]) {
            const auto value = Read<u8>(memory, ShrineFlagCache.entered[index] + 0x0B);
            result.entered[index] = value && ((*value & 1U) != 0);
        }
        if (ShrineFlagCache.cleared[index]) {
            const auto value = Read<u8>(memory, ShrineFlagCache.cleared[index] + 0x0B);
            result.cleared[index] = value && ((*value & 1U) != 0);
        }
    }
    if (ShrineFlagCache.sheik_sensor_enabled) {
        const auto value = Read<u8>(memory, ShrineFlagCache.sheik_sensor_enabled + 0x0B);
        if (value) {
            result.sheik_sensor_enabled = (*value & 1U) != 0;
        }
    }
    return result;
}

std::optional<u32> ReadSelectedRune(Core::Memory::Memory& memory, VAddr main_base) {
    const auto manager = Read<u64>(memory, main_base + RuneManagerStaticOffset);
    const auto selected = manager && *manager
                              ? Read<u32>(memory, *manager + RuneManagerSelectedTypeOffset)
                              : std::nullopt;
    // BOTW 1.9.0 uses 0..7 for round/cube bombs, Magnesis, Stasis, Cryonis, Camera,
    // Master Cycle Zero, and Amiibo respectively.
    return selected && *selected <= 7 ? selected : std::nullopt;
}

bool IsSelectableRuneAvailable(Core::Memory::Memory& memory, VAddr main_base, u32 rune_type) {
    VAddr handle_offset{};
    switch (rune_type) {
    case RuneTypeRoundBomb:
        handle_offset = RemoteBombRuneHandleOffset;
        break;
    case RuneTypeMagnesis:
        handle_offset = MagnesisRuneHandleOffset;
        break;
    case RuneTypeStasis:
        handle_offset = StasisRuneHandleOffset;
        break;
    case RuneTypeCryonis:
        handle_offset = CryonisRuneHandleOffset;
        break;
    case RuneTypeCamera:
        handle_offset = CameraRuneHandleOffset;
        break;
    default:
        return false;
    }
    const auto available = ReadGameDataBool(memory, main_base, handle_offset);
    return available && *available;
}

bool IsExpectedBuild(const Core::System& system) {
    const auto build_id = system.GetApplicationProcessBuildID();
    return build_id == BotwBuildId;
}

} // namespace

std::string GetSnapshot(EmulationSession& session, bool lightweight) {
    // shutdownNow() cannot interrupt a JNI memory read. ShutdownEmulation owns this same lock
    // while it unmaps the application process, making the lock the actual lifetime barrier.
    [[maybe_unused]] auto system_access = session.LockSystemAccess();
    if (!session.IsRunning()) {
        ResetSnapshotCaches();
        return R"({"schema":1,"status":"not_running"})";
    }

    auto& system = session.System();
    const u64 title_id = system.GetApplicationProcessProgramID();
    if (title_id != BotwTitleId) {
        ResetSnapshotCaches();
        std::ostringstream out;
        out << R"({"schema":1,"status":"wrong_title","titleId":")" << std::uppercase
            << std::hex << std::setw(16) << std::setfill('0') << title_id << R"("})";
        return out.str();
    }

    const auto build_id = system.GetApplicationProcessBuildID();
    const std::string build = Common::HexToString(
        std::span<const u8>{build_id.data(), BotwBuildId.size()});
    if (!IsExpectedBuild(system)) {
        ResetSnapshotCaches();
        return R"({"schema":1,"status":"unsupported_build","titleId":"01007EF00011E000","buildId":)" +
               JsonString(build) + "}";
    }

    const auto [main_base, main_size] = system.GetApplicationProcessMainModule();
    if (!main_base || main_size <= PouchSingletonGotOffset) {
        return R"({"schema":1,"status":"loading","titleId":"01007EF00011E000","buildId":)" +
               JsonString(build) + "}";
    }

    auto& memory = system.ApplicationMemory();
    const auto singleton = Read<u64>(memory, main_base + PlayerSingletonOffset);
    const auto player = singleton ? Read<u64>(memory, *singleton) : std::nullopt;
    if (!player || !*player) {
        return R"({"schema":1,"status":"loading","titleId":"01007EF00011E000","buildId":)" +
               JsonString(build) + "}";
    }

    const auto component = Read<u64>(memory, *player + PlayerComponentOffset);
    const auto maximum_health = Read<float>(memory, *player + PlayerMaxHealthOffset);
    const auto stamina = Read<float>(memory, *player + PlayerStaminaOffset);
    const auto component_max_health = component
                                          ? Read<s32>(memory, *component + ComponentMaxHealthOffset)
                                          : std::nullopt;
    const auto maximum_stamina = component
                                     ? Read<float>(memory, *component + ComponentMaxStaminaOffset)
                                     : std::nullopt;
    const auto player_map = component && *component
                                ? ReadPlayerMap(memory, *component)
                                : std::nullopt;
    const auto active_effects = component && *component
                                    ? ReadActiveCookEffects(memory, *component)
                                    : std::vector<ActiveEffectSnapshot>{};

    std::optional<s32> health;
    std::optional<VAddr> health_getter;
    std::optional<VAddr> health_address;
    const auto* process = system.ApplicationProcess();
    const u64 process_id = process ? process->GetProcessId() : 0;
    if (component && *component) {
        const auto vtable = Read<u64>(memory, *component);
        health_getter = vtable ? Read<u64>(memory, *vtable + HealthGetterVtableOffset)
                               : std::nullopt;
        health_address = health_getter ? CachedHealthAddress(process_id, *component, *health_getter)
                                       : std::nullopt;
        if (!health_address && health_getter) {
            health_address = ResolvePointerGetter(memory, *component, *health_getter);
            if (health_address) {
                CacheHealthAddress(process_id, *component, *health_getter, *health_address);
            }
        }
        health = health_address ? Read<s32>(memory, *health_address) : std::nullopt;
    }

    const auto rupees = ReadGameDataS32(memory, main_base, CurrentRupeeHandleOffset);
    const bool valid_health = health && component_max_health && *health >= 0 &&
                               *component_max_health > 0 &&
                               static_cast<s64>(*health) <=
                                   static_cast<s64>(*component_max_health) * 4;
    const bool valid_stamina =
        stamina && maximum_stamina && std::isfinite(*stamina) &&
        std::isfinite(*maximum_stamina) && *stamina >= 0.0f && *maximum_stamina > 0.0f &&
        *maximum_stamina <= 100000.0f && *stamina <= *maximum_stamina * 4.0f;
    const auto selected_rune = ReadSelectedRune(memory, main_base);
    const auto pouch_instance_storage = Read<u64>(memory, main_base + PouchSingletonGotOffset);
    const auto pouch_manager = pouch_instance_storage
                                   ? Read<u64>(memory, *pouch_instance_storage)
                                   : std::nullopt;
    const auto grabbed_item_count = pouch_manager && *pouch_manager
                                        ? ReadGrabbedItemCount(memory, *pouch_manager)
                                        : std::nullopt;
    const auto carry_box_manager = Read<u64>(memory, main_base + CarryBoxManagerOffset);
    const auto carry_box_anchor = carry_box_manager && *carry_box_manager
                                      ? Read<u64>(memory, *carry_box_manager + CarryBoxAnchorOffset)
                                      : std::nullopt;
    const auto carry_box_flags = carry_box_manager && *carry_box_manager
                                     ? Read<u32>(memory, *carry_box_manager + CarryBoxFlagsOffset)
                                     : std::nullopt;
    const auto carry_box_position = carry_box_manager && *carry_box_manager
                                        ? ReadMatrixTranslation(
                                              memory, *carry_box_manager +
                                                          CarryBoxRootMatrixOffset)
                                        : std::nullopt;
    const VAddr action_mailbox = system.GetApplicationProcessCompanionMailbox();
    const bool action_bridge = IsEquipMailboxAvailable(system, action_mailbox);
    const u32 action_state =
        action_bridge ? LoadEquipMailbox<u32>(system, action_mailbox, EquipMailboxStateOffset,
                                              __ATOMIC_ACQUIRE)
                      : EquipMailboxIdle;
    const u32 action_attempts =
        action_bridge ? LoadEquipMailbox<u32>(system, action_mailbox, EquipMailboxAttemptsOffset,
                                              __ATOMIC_ACQUIRE)
                      : 0;
    const u64 action_request =
        action_bridge ? LoadEquipMailbox<u64>(system, action_mailbox, EquipMailboxRequestOffset,
                                              __ATOMIC_ACQUIRE)
                      : 0;
    const u64 action_completed =
        action_bridge ? LoadEquipMailbox<u64>(system, action_mailbox, EquipMailboxCompletedOffset,
                                              __ATOMIC_ACQUIRE)
                      : 0;
    const u32 action_type =
        action_bridge ? LoadEquipMailbox<u32>(system, action_mailbox, EquipMailboxTypeOffset,
                                              __ATOMIC_ACQUIRE)
                      : 0;
    const u32 action_stage =
        action_bridge ? LoadEquipMailbox<u32>(system, action_mailbox, ActionStageOffset,
                                              __ATOMIC_ACQUIRE)
                      : 0;
    const auto action_diagnostic =
        ReadActionDiagnosticSnapshot(system, action_mailbox, action_bridge);
    if (lightweight) {
        // The 1 Hz path deliberately avoids PauseMenuDataMgr, its intrusive pouch traversal,
        // actor-name reads, GameData rune-availability lookups, and large JSON construction. A
        // single RuneMgr field read keeps selection/highlighting responsive without another pouch
        // scan; the low-frequency full snapshot still supplies inventory and equipment.
        std::ostringstream out;
        out << R"({"schema":1,"status":"ready","saveLoaded":true,"stats":{)";
        bool needs_comma = false;
        const auto add_number = [&](std::string_view name, auto value) {
            if (needs_comma) {
                out << ',';
            }
            out << JsonString(name) << ':' << value;
            needs_comma = true;
        };
        if (valid_health) {
            add_number("health", *health);
        }
        if (valid_health && component_max_health && *component_max_health > 0) {
            add_number("maxHealth", *component_max_health);
        } else if (maximum_health && std::isfinite(*maximum_health) &&
                   *maximum_health > 0.0f &&
                   *maximum_health <= static_cast<float>(std::numeric_limits<s32>::max())) {
            add_number("maxHealth", static_cast<s32>(*maximum_health));
        }
        if (valid_stamina) {
            add_number("stamina", *stamina);
            add_number("maxStamina", *maximum_stamina);
        }
        if (rupees) {
            add_number("rupees", *rupees);
        }
        out << R"(},"selectedRune":)"
            << (selected_rune ? std::to_string(*selected_rune) : "null")
            << R"(,"heldItemCount":)"
            << (grabbed_item_count ? std::to_string(*grabbed_item_count) : "null")
            << R"(,"map":)";
        if (player_map) {
            AppendPlayerMapJson(out, *player_map);
        } else {
            out << "null";
        }
        out << R"(,"debug":{"actionState":)" << action_state
            << R"(,"actionAttempts":)" << action_attempts
            << R"(,"actionRequest":)" << JsonString(HexAddress(action_request))
            << R"(,"actionCompleted":)" << JsonString(HexAddress(action_completed))
            << R"(,"actionType":)" << action_type
            << R"(,"actionStage":)" << action_stage
            << R"(,"carryBoxManager":)"
            << JsonString(HexAddress(carry_box_manager.value_or(0)))
            << R"(,"carryBoxAnchor":)"
            << JsonString(HexAddress(carry_box_anchor.value_or(0)))
            << R"(,"carryBoxFlags":)"
            << (carry_box_flags ? std::to_string(*carry_box_flags) : "null")
            << R"(,"carryBoxPosition":)";
        AppendPositionJson(out, carry_box_position);
        AppendActionDiagnosticJson(out, action_diagnostic, std::nullopt, std::nullopt,
                                   grabbed_item_count);
        out << "}}";
        return out.str();
    }

    const auto pouch_items = pouch_manager && *pouch_manager
                                 ? ReadPouchItems(memory, *pouch_manager)
                                 : std::nullopt;
    std::optional<s32> diagnostic_quantity_after;
    std::optional<bool> diagnostic_in_inventory_after;
    if (pouch_items && action_diagnostic.target != 0) {
        const auto diagnostic_item = std::find_if(
            pouch_items->begin(), pouch_items->end(), [&](const auto& item) {
                return item.address == action_diagnostic.target;
            });
        if (diagnostic_item != pouch_items->end()) {
            diagnostic_quantity_after = diagnostic_item->value;
            diagnostic_in_inventory_after = true;
        } else {
            diagnostic_quantity_after = 0;
            diagnostic_in_inventory_after = false;
        }
    }
    const VAddr equip_mailbox = action_mailbox;
    const bool equip_bridge = pouch_items && action_bridge;
    const bool player_alive = valid_health && *health > 0;
    const auto hold_controller_storage = Read<u64>(memory, main_base + HoldControllerGotOffset);
    const auto hold_controller = hold_controller_storage && *hold_controller_storage
                                     ? Read<u64>(memory, *hold_controller_storage)
                                     : std::nullopt;
    const auto hold_sender_vtable =
        hold_controller && *hold_controller
            ? Read<u64>(memory, *hold_controller + HoldControllerMessageSenderOffset)
            : std::nullopt;
    const auto hold_message_dispatcher =
        hold_controller && *hold_controller
            ? Read<u64>(memory, *hold_controller + HoldControllerMessageDispatcherOffset)
            : std::nullopt;
    const bool hold_bridge = equip_bridge && player_alive && hold_controller &&
                             *hold_controller && hold_sender_vtable && *hold_sender_vtable &&
                             hold_message_dispatcher && *hold_message_dispatcher;
    const auto eat_screen_registry = Read<u64>(memory, main_base + EatScreenRegistryOffset);
    const auto eat_screen_count = eat_screen_registry && *eat_screen_registry
                                      ? Read<u32>(memory, *eat_screen_registry + 0x28)
                                      : std::nullopt;
    const auto eat_screen_array = eat_screen_registry && *eat_screen_registry
                                      ? Read<u64>(memory, *eat_screen_registry + 0x30)
                                      : std::nullopt;
    const bool eat_bridge = equip_bridge && player_alive && eat_screen_registry &&
                            *eat_screen_registry && eat_screen_count && eat_screen_array &&
                            *eat_screen_array;
    const u32 equip_state = equip_bridge
                                ? LoadEquipMailbox<u32>(system, equip_mailbox,
                                                        EquipMailboxStateOffset, __ATOMIC_ACQUIRE)
                                : EquipMailboxIdle;
    const u32 equip_attempts = equip_bridge
                                   ? LoadEquipMailbox<u32>(system, equip_mailbox,
                                                           EquipMailboxAttemptsOffset,
                                                           __ATOMIC_ACQUIRE)
                                   : 0;
    const auto equipped_stats = pouch_items ? GetEquippedStats(*pouch_items) : EquippedStats{};
    const auto champion_powers = pouch_items
                                     ? std::make_optional(
                                           ReadChampionPowers(memory, main_base, *pouch_items))
                                     : std::nullopt;
    const auto quests = ReadQuests(memory, main_base);
    const auto shrine_states = ReadShrineStates(memory, main_base, process_id);
    const auto sheik_sensor_owned =
        ReadGameDataBool(memory, main_base, SheikSensorOwnedHandleOffset);
    const auto sheik_sensor_upgraded =
        ReadGameDataBool(memory, main_base, SheikSensorUpgradeHandleOffset);
    const auto sheik_sensor_search_mode =
        ReadGameDataS32(memory, main_base, SheikSensorSearchModeHandleOffset);
    const bool valid_sheik_sensor = sheik_sensor_owned && sheik_sensor_upgraded &&
                                    sheik_sensor_search_mode && shrine_states &&
                                    shrine_states->sheik_sensor_enabled;
    const auto camera_rune = ReadGameDataBool(memory, main_base, CameraRuneHandleOffset);
    const auto cryonis_rune = ReadGameDataBool(memory, main_base, CryonisRuneHandleOffset);
    const auto magnesis_rune = ReadGameDataBool(memory, main_base, MagnesisRuneHandleOffset);
    const auto remote_bomb_rune =
        ReadGameDataBool(memory, main_base, RemoteBombRuneHandleOffset);
    const auto remote_bomb_upgrade =
        ReadGameDataBool(memory, main_base, RemoteBombUpgradeHandleOffset);
    const auto stasis_rune = ReadGameDataBool(memory, main_base, StasisRuneHandleOffset);
    const auto stasis_upgrade = ReadGameDataBool(memory, main_base, StasisUpgradeHandleOffset);
    const bool valid_runes = camera_rune && cryonis_rune && magnesis_rune && remote_bomb_rune &&
                             remote_bomb_upgrade && stasis_rune && stasis_upgrade;

    u32 capabilities = 0;
    // A valid player component plus an initialized pouch list is a save-load signal that also
    // works for a brand-new save whose inventory is legitimately empty.
    const bool save_loaded = component && *component && pouch_items.has_value();
    if (valid_health || valid_stamina || rupees || equipped_stats.attack ||
        equipped_stats.defense || equipped_stats.bow_attack || equipped_stats.shield_guard) {
        capabilities |= 1U << 0;
    }
    if (valid_stamina) {
        capabilities |= 1U << 1;
    }
    if (pouch_items) {
        capabilities |= 1U << 2;
        capabilities |= 1U << 3;
    }
    if (equip_bridge) {
        capabilities |= 1U << 6;
    }
    if (hold_bridge) {
        capabilities |= 1U << 11;
    }
    if (eat_bridge) {
        capabilities |= 1U << 12;
    }
    // A framebuffer crop is not BOTW's pause-menu player model. Keep this capability disabled

    if (valid_runes) {
        capabilities |= 1U << 8;
    }
    if (quests) {
        capabilities |= 1U << 4;
    }
    if (player_map) {
        capabilities |= 1U << 5;
    }
    if (shrine_states && equip_bridge) {
        capabilities |= 1U << 9;
    }
    if (valid_sheik_sensor) {
        capabilities |= 1U << 10;
    }

    std::ostringstream out;
    out << R"({"schema":1,"status":"ready","titleId":"01007EF00011E000","buildId":)"
        << JsonString(build) << R"(,"saveLoaded":)" << (save_loaded ? "true" : "false")
        << R"(,"capabilities":)" << capabilities << R"(,"heldItemCount":)"
        << (grabbed_item_count ? std::to_string(*grabbed_item_count) : "null")
        << R"(,"stats":{)";
    bool needs_comma = false;
    const auto add_number = [&](std::string_view name, auto value) {
        if (needs_comma) {
            out << ',';
        }
        out << JsonString(name) << ':' << value;
        needs_comma = true;
    };
    if (valid_health) {
        add_number("health", *health);
    }
    if (valid_health && component_max_health && *component_max_health > 0) {
        add_number("maxHealth", *component_max_health);
    } else if (maximum_health && std::isfinite(*maximum_health) && *maximum_health > 0.0f &&
               *maximum_health <= static_cast<float>(std::numeric_limits<s32>::max())) {
        add_number("maxHealth", static_cast<s32>(*maximum_health));
    }
    if (valid_stamina) {
        add_number("stamina", *stamina);
        add_number("maxStamina", *maximum_stamina);
    }
    if (rupees) {
        add_number("rupees", *rupees);
    }
    if (equipped_stats.attack) {
        add_number("attack", *equipped_stats.attack);
    }
    if (equipped_stats.defense) {
        add_number("defense", *equipped_stats.defense);
    }
    if (equipped_stats.bow_attack) {
        add_number("bowAttack", *equipped_stats.bow_attack);
    }
    if (equipped_stats.shield_guard) {
        add_number("shieldGuard", *equipped_stats.shield_guard);
    }
    out << R"(},"inventory":[)";
    if (pouch_items) {
        for (size_t index = 0; index < pouch_items->size(); ++index) {
            if (index) {
                out << ',';
            }
            AppendPouchItemJson(out, pouch_items->at(index));
        }
    }
    out << R"(],"equipment":[)";
    if (pouch_items) {
        bool first_equipment = true;
        for (const auto& item : *pouch_items) {
            const std::string_view slot = EquipmentSlotName(item.type);
            if (!item.equipped || slot.empty()) {
                continue;
            }
            if (!first_equipment) {
                out << ',';
            }
            out << R"({"slot":)" << JsonString(slot) << R"(,"item":)";
            AppendPouchItemJson(out, item);
            out << '}';
            first_equipment = false;
        }
    }
    out << R"(],"championPowers":[)";
    if (champion_powers) {
        for (size_t index = 0; index < champion_powers->size(); ++index) {
            if (index) {
                out << ',';
            }
            AppendChampionPowerJson(out, champion_powers->at(index));
        }
    }
    out << R"(],"runes":[)";
    if (valid_runes) {
        const auto is_selected = [&](u32 rune_type) {
            return selected_rune && *selected_rune == rune_type;
        };
        out << R"({"id":"magnesis","name":"Magnesis","type":2,"available":)"
            << (*magnesis_rune ? "true" : "false") << R"(,"upgraded":false,"selected":)"
            << (is_selected(RuneTypeMagnesis) ? "true" : "false") << "},"
            << R"({"id":"stasis","name":"Stasis","type":3,"available":)"
            << (*stasis_rune ? "true" : "false") << R"(,"upgraded":)"
            << (*stasis_upgrade ? "true" : "false") << R"(,"selected":)"
            << (is_selected(RuneTypeStasis) ? "true" : "false") << "},"
            << R"({"id":"cryonis","name":"Cryonis","type":4,"available":)"
            << (*cryonis_rune ? "true" : "false") << R"(,"upgraded":false,"selected":)"
            << (is_selected(RuneTypeCryonis) ? "true" : "false") << "},"
            << R"({"id":"bombs","name":"Remote Bombs","type":0,"available":)"
            << (*remote_bomb_rune ? "true" : "false") << R"(,"upgraded":)"
            << (*remote_bomb_upgrade ? "true" : "false") << R"(,"selected":)"
            << ((is_selected(RuneTypeRoundBomb) || is_selected(RuneTypeCubeBomb)) ? "true"
                                                                                  : "false")
            << "},"
            << R"({"id":"camera","name":"Camera","type":5,"available":)"
            << (*camera_rune ? "true" : "false") << R"(,"upgraded":false,"selected":)"
            << (is_selected(RuneTypeCamera) ? "true" : "false") << "}";
    }
    out << R"(],"enteredShrines":[)";
    if (shrine_states) {
        bool first = true;
        for (size_t index = 0; index < BotwShrines.size(); ++index) {
            if (!shrine_states->entered[index] && !shrine_states->cleared[index]) {
                continue;
            }
            if (!first) {
                out << ',';
            }
            out << JsonString(BotwShrines[index].id);
            first = false;
        }
    }
    out << R"(],"clearedShrines":[)";
    if (shrine_states) {
        bool first = true;
        for (size_t index = 0; index < BotwShrines.size(); ++index) {
            if (!shrine_states->cleared[index]) {
                continue;
            }
            if (!first) {
                out << ',';
            }
            out << JsonString(BotwShrines[index].id);
            first = false;
        }
    }
    out << R"(],"map":)";
    if (player_map) {
        AppendPlayerMapJson(out, *player_map);
    } else {
        out << "null";
    }
    out << R"(,"sensor":)";
    if (valid_sheik_sensor) {
        out << R"({"unlocked":)" << (*sheik_sensor_owned ? "true" : "false")
            << R"(,"upgraded":)" << (*sheik_sensor_upgraded ? "true" : "false")
            << R"(,"enabled":)"
            << (*shrine_states->sheik_sensor_enabled ? "true" : "false")
            << R"(,"searchMode":)" << *sheik_sensor_search_mode << '}';
    } else {
        out << "null";
    }
    out << R"(,"quests":[)";
    if (quests) {
        for (size_t index = 0; index < quests->size(); ++index) {
            if (index) {
                out << ',';
            }
            AppendQuestJson(out, quests->at(index));
        }
    }
    out << R"(],"effects":[)";
    for (size_t index = 0; index < active_effects.size(); ++index) {
        if (index) {
            out << ',';
        }
        out << R"({"name":)" << JsonString(active_effects[index].name)
            << R"(,"seconds":)" << active_effects[index].seconds
            << R"(,"level":)" << active_effects[index].level << '}';
    }
    out << R"(],"debug":{)"
        << R"("mainBase":)" << JsonString(HexAddress(main_base)) << R"(,"player":)"
        << JsonString(HexAddress(*player)) << R"(,"component":)"
        << JsonString(HexAddress(component.value_or(0))) << R"(,"healthGetter":)"
        << JsonString(HexAddress(health_getter.value_or(0))) << R"(,"healthAddress":)"
        << JsonString(HexAddress(health_address.value_or(0))) << R"(,"healthGetterCode":)"
        << JsonString(ReadCodeHex(memory, health_getter.value_or(0)))
        << R"(,"pouchInstanceStorage":)"
        << JsonString(HexAddress(pouch_instance_storage.value_or(0)))
        << R"(,"pouchManager":)" << JsonString(HexAddress(pouch_manager.value_or(0)))
        << R"(,"pouchItemCount":)" << (pouch_items ? pouch_items->size() : 0)
        << R"(,"questCount":)" << (quests ? quests->size() : 0)
        << R"(,"shrineStateCount":)" << (shrine_states ? BotwShrines.size() : 0)
        << R"(,"sensorData":)" << (valid_sheik_sensor ? "true" : "false")
        << R"(,"equipBridge":)" << (equip_bridge ? "true" : "false")
        << R"(,"eatBridge":)" << (eat_bridge ? "true" : "false")
        << R"(,"inventoryMenuOpen":null,"inventoryMenuState":"not-consulted")"
        << R"(,"menuContextRequired":false,"eatScreenRegistry":)"
        << JsonString(HexAddress(eat_screen_registry.value_or(0)))
        << R"(,"eatScreenCount":)"
        << (eat_screen_count ? std::to_string(*eat_screen_count) : "null")
        << R"(,"eatScreenArray":)" << JsonString(HexAddress(eat_screen_array.value_or(0)))
        << R"(,"equipState":)" << equip_state
        << R"(,"equipAttempts":)" << equip_attempts
        << R"(,"actionRequest":)" << JsonString(HexAddress(action_request))
        << R"(,"actionCompleted":)" << JsonString(HexAddress(action_completed))
        << R"(,"actionType":)" << action_type
        << R"(,"actionStage":)" << action_stage
        << R"(,"carryBoxManager":)"
        << JsonString(HexAddress(carry_box_manager.value_or(0)))
        << R"(,"carryBoxAnchor":)"
        << JsonString(HexAddress(carry_box_anchor.value_or(0)))
        << R"(,"carryBoxFlags":)"
        << (carry_box_flags ? std::to_string(*carry_box_flags) : "null")
        << R"(,"carryBoxPosition":)";
    AppendPositionJson(out, carry_box_position);
    out
        << R"(,"selectedRune":)" << (selected_rune ? std::to_string(*selected_rune) : "null");
    AppendActionDiagnosticJson(out, action_diagnostic, diagnostic_quantity_after,
                               diagnostic_in_inventory_after, grabbed_item_count);
    out << "}}";
    return out.str();
}

bool PerformAction(EmulationSession& session, int action, long long argument) {

    [[maybe_unused]] auto system_access = session.LockSystemAccess();
    const auto action_name = ItemActionName(action);
    LOG_INFO(Frontend,
             "[BOTW-ITEM-ACTION] request action={} code={} argument={:016X} running={} "
             "hostThreadQueueOnly=true guestGameThreadHook=true",
             action_name, action, static_cast<u64>(argument), session.IsRunning());
    if (!session.IsRunning() ||
        (action != ActionEquipItem && action != ActionSelectRune &&
         action != ActionFastTravel && action != ActionEatItem &&
         action != ActionHoldItem && action != ActionUnequipItem) ||
        ((action == ActionEquipItem || action == ActionEatItem ||
          action == ActionHoldItem || action == ActionUnequipItem) && argument <= 0) ||
        (action == ActionFastTravel && argument < 0)) {
        LOG_WARNING(Frontend,
                    "[BOTW-ITEM-ACTION] rejected action={} reason=invalid-request running={} "
                    "argument={:016X}",
                    action_name, session.IsRunning(), static_cast<u64>(argument));
        return false;
    }

    auto& system = session.System();
    if (system.GetApplicationProcessProgramID() != BotwTitleId || !IsExpectedBuild(system)) {
        LOG_WARNING(Frontend,
                    "[BOTW-ITEM-ACTION] rejected action={} reason=wrong-title-or-build "
                    "title={:016X}",
                    action_name, system.GetApplicationProcessProgramID());
        return false;
    }
    const auto [main_base, main_size] = system.GetApplicationProcessMainModule();
    if (!main_base || main_size <= PouchSingletonGotOffset) {
        LOG_WARNING(Frontend,
                    "[BOTW-ITEM-ACTION] rejected action={} reason=invalid-main-module "
                    "base={:016X} size={}",
                    action_name, main_base, main_size);
        return false;
    }

    auto& memory = system.ApplicationMemory();
    const VAddr equip_mailbox = system.GetApplicationProcessCompanionMailbox();

    if (action == ActionEatItem || action == ActionHoldItem) {
        const auto player_singleton = Read<u64>(memory, main_base + PlayerSingletonOffset);
        const auto player_info = player_singleton && *player_singleton
                                     ? Read<u64>(memory, *player_singleton)
                                     : std::nullopt;
        const auto player_actor = player_info && *player_info
                                      ? Read<u64>(memory, *player_info + PlayerComponentOffset)
                                      : std::nullopt;
        const auto player_vtable = player_actor && *player_actor
                                       ? Read<u64>(memory, *player_actor)
                                       : std::nullopt;
        const auto health_getter = player_vtable && *player_vtable
                                       ? Read<u64>(memory, *player_vtable +
                                                             HealthGetterVtableOffset)
                                       : std::nullopt;
        const auto health_address = health_getter && *health_getter
                                        ? ResolvePointerGetter(memory, *player_actor,
                                                               *health_getter)
                                        : std::nullopt;
        const auto health = health_address ? Read<s32>(memory, *health_address) : std::nullopt;
        if (!player_info || !*player_info || !player_actor || !*player_actor || !health ||
            *health <= 0) {
            LOG_WARNING(Frontend,
                        "[BOTW-ITEM-ACTION] rejected action={} reason=player-unavailable-or-dead "
                        "playerInfo={:016X} player={:016X} health={}",
                        action_name, player_info.value_or(0), player_actor.value_or(0),
                        health ? std::to_string(*health) : "unavailable");
            return false;
        }
    }

    if (action == ActionFastTravel) {
        if (argument >= static_cast<long long>(BotwShrines.size())) {
            return false;
        }
        const size_t shrine_index = static_cast<size_t>(argument);
        const auto* process = system.ApplicationProcess();
        const u64 process_id = process ? process->GetProcessId() : 0;
        const auto states = ReadShrineStates(memory, main_base, process_id);
        if (!states ||
            (!states->entered[shrine_index] && !states->cleared[shrine_index])) {

            return false;
        }

        const u64 payload = shrine_index + 1;
        std::scoped_lock action_lock{EquipActionMutex};
        if (!IsEquipMailboxAvailable(system, equip_mailbox)) {
            return false;
        }
        const u32 state = LoadEquipMailbox<u32>(system, equip_mailbox,
                                                EquipMailboxStateOffset, __ATOMIC_ACQUIRE);
        const u64 pending = LoadEquipMailbox<u64>(system, equip_mailbox,
                                                  EquipMailboxRequestOffset, __ATOMIC_ACQUIRE);
        const u32 pending_type = LoadEquipMailbox<u32>(system, equip_mailbox,
                                                       EquipMailboxTypeOffset, __ATOMIC_ACQUIRE);
        if (IsEquipMailboxBusy(state)) {
            return pending == payload && pending_type == FastTravelMailboxTypeSentinel;
        }
        if ((state != EquipMailboxIdle && state != EquipMailboxSucceeded &&
             state != EquipMailboxRejected) ||
            pending != 0) {
            return false;
        }

        const auto& shrine = BotwShrines[shrine_index];
        if (!StoreEquipMailboxString(system, equip_mailbox, FastTravelMapNameOffset,
                                     FastTravelMapNameSize, shrine.map_name) ||
            !StoreEquipMailboxString(system, equip_mailbox, FastTravelPositionNameOffset,
                                     FastTravelPositionNameSize, shrine.position_name)) {
            return false;
        }
        StoreEquipMailbox<u64>(system, equip_mailbox, EquipMailboxCompletedOffset, 0,
                               __ATOMIC_RELAXED);
        StoreEquipMailbox<u32>(system, equip_mailbox, EquipMailboxTypeOffset,
                               FastTravelMailboxTypeSentinel, __ATOMIC_RELAXED);
        StoreEquipMailbox<u32>(system, equip_mailbox, EquipMailboxReservedOffset, 0,
                               __ATOMIC_RELAXED);
        StoreEquipMailbox<u32>(system, equip_mailbox, EquipMailboxAttemptsOffset, 0,
                               __ATOMIC_RELAXED);
        StoreEquipMailbox<u32>(system, equip_mailbox, ActionStageOffset, 0,
                               __ATOMIC_RELAXED);
        StoreEquipMailbox<u64>(system, equip_mailbox, EquipMailboxRequestOffset, payload,
                               __ATOMIC_RELAXED);
        StoreEquipMailbox<u32>(system, equip_mailbox, EquipMailboxStateOffset,
                               EquipMailboxQueued, __ATOMIC_RELEASE);
        LOG_INFO(Frontend, "[BOTW Companion] Queued shrine travel to {} ({}/{})", shrine.id,
                 shrine.map_name, shrine.position_name);
        return true;
    }

    if (action == ActionSelectRune) {
        if (argument < 0 || argument > std::numeric_limits<u32>::max()) {
            return false;
        }
        const u32 rune_type = static_cast<u32>(argument);
        if (!IsSelectableRuneAvailable(memory, main_base, rune_type)) {
            return false;
        }
        if (const auto selected = ReadSelectedRune(memory, main_base);
            selected && *selected == rune_type) {
            return true;
        }

        const u64 payload = static_cast<u64>(rune_type) + 1;
        std::scoped_lock action_lock{EquipActionMutex};
        if (!IsEquipMailboxAvailable(system, equip_mailbox)) {
            return false;
        }
        const u32 state = LoadEquipMailbox<u32>(system, equip_mailbox,
                                                EquipMailboxStateOffset, __ATOMIC_ACQUIRE);
        const u64 pending = LoadEquipMailbox<u64>(system, equip_mailbox,
                                                  EquipMailboxRequestOffset, __ATOMIC_ACQUIRE);
        const u32 pending_type = LoadEquipMailbox<u32>(system, equip_mailbox,
                                                       EquipMailboxTypeOffset, __ATOMIC_ACQUIRE);
        if (IsEquipMailboxBusy(state)) {
            return pending == payload && pending_type == RuneMailboxTypeSentinel;
        }
        if ((state != EquipMailboxIdle && state != EquipMailboxSucceeded &&
             state != EquipMailboxRejected) ||
            pending != 0) {
            return false;
        }

        StoreEquipMailbox<u64>(system, equip_mailbox, EquipMailboxCompletedOffset, 0,
                               __ATOMIC_RELAXED);
        StoreEquipMailbox<u32>(system, equip_mailbox, EquipMailboxTypeOffset,
                               RuneMailboxTypeSentinel, __ATOMIC_RELAXED);
        StoreEquipMailbox<u32>(system, equip_mailbox, EquipMailboxReservedOffset, 0,
                               __ATOMIC_RELAXED);
        StoreEquipMailbox<u32>(system, equip_mailbox, EquipMailboxAttemptsOffset, 0,
                               __ATOMIC_RELAXED);
        StoreEquipMailbox<u32>(system, equip_mailbox, ActionStageOffset, 0,
                               __ATOMIC_RELAXED);
        StoreEquipMailbox<u64>(system, equip_mailbox, EquipMailboxRequestOffset, payload,
                               __ATOMIC_RELAXED);
        StoreEquipMailbox<u32>(system, equip_mailbox, EquipMailboxStateOffset,
                               EquipMailboxQueued, __ATOMIC_RELEASE);
        LOG_INFO(Frontend, "[BOTW Companion] Queued guest rune selection {}", rune_type);
        return true;
    }

    const auto pouch_instance_storage = Read<u64>(memory, main_base + PouchSingletonGotOffset);
    const auto pouch_manager = pouch_instance_storage
                                   ? Read<u64>(memory, *pouch_instance_storage)
                                   : std::nullopt;
    const auto items = pouch_manager && *pouch_manager
                           ? ReadPouchItems(memory, *pouch_manager)
                           : std::nullopt;
    if (!items) {
        LOG_WARNING(Frontend,
                    "[BOTW-INVENTORY] rejected action={} reason=pouch-unavailable "
                    "storage={:016X} manager={:016X}",
                    action_name, pouch_instance_storage.value_or(0),
                    pouch_manager.value_or(0));
        return false;
    }

    const VAddr requested_address = static_cast<VAddr>(argument);
    const auto target = std::find_if(items->begin(), items->end(), [&](const auto& item) {
        return item.address == requested_address;
    });
    if (target == items->end()) {
        LOG_WARNING(Frontend,
                    "[BOTW-INVENTORY] rejected action={} reason=item-reference-not-in-pouch "
                    "target={:016X} itemCount={}",
                    action_name, requested_address, items->size());
        return false;
    }
    const auto equipped_in_slot = std::find_if(items->begin(), items->end(), [&](const auto& item) {
        return item.type == target->type && item.equipped;
    });
    LOG_INFO(Frontend,
             "[BOTW-INVENTORY] selected action={} actor={} target={:016X} type={} category={} "
             "itemUse={} quantity={} equipped={} modifierFlags={:08X} modifierValue={} "
             "cookHealth={} cookEffect={} cookLevel={} cookDuration={} "
             "equippedSlot={} equippedActor={} equippedTarget={:016X}",
             action_name, target->name, target->address, target->type,
             PouchCategoryName(target->type), target->item_use, target->value, target->equipped,
             target->modifier_flags, target->modifier_value, target->cook_health_recover,
             target->cook_effect_id, target->cook_effect_level,
             target->cook_effect_duration, EquipmentSlotName(target->type),
             equipped_in_slot != items->end() ? equipped_in_slot->name : "none",
             equipped_in_slot != items->end() ? equipped_in_slot->address : 0);
    const bool valid_equip_type =
        (target->type == 0 || target->type == 1 || target->type == 3 ||
         target->type == 4 || target->type == 5 || target->type == 6);
    const auto held_count = pouch_manager && *pouch_manager
                                ? ReadGrabbedItemCount(memory, *pouch_manager)
                                : std::nullopt;
    const bool valid_hold = IsHoldablePouchItem(*target) && held_count &&
                            *held_count < PouchGrabbedItemCapacity;
    const bool valid_eat = IsEdiblePouchItem(*target);
    const bool valid_unequip = valid_equip_type && target->equipped;
    if ((action == ActionEquipItem && !valid_equip_type) ||
        (action == ActionEatItem && !valid_eat) ||
        (action == ActionHoldItem && !valid_hold) ||
        (action == ActionUnequipItem && !valid_unequip)) {
        LOG_WARNING(Frontend,
                    "[BOTW-ITEM-ACTION] rejected action={} reason=action-not-valid-for-item "
                    "actor={} type={} quantity={} equipped={} held={}",
                    action_name, target->name, target->type, target->value, target->equipped,
                    held_count ? std::to_string(*held_count) : "unavailable");
        return false;
    }

    std::optional<PouchSelection> eat_selection;
    u64 hold_controller_address = 0;
    u64 action_player_address = 0;
    u64 hold_active_flag_pointer_address = 0;
    u64 hold_controller_target_before = 0;
    u64 hold_controller_link_first = 0;
    u64 hold_controller_link_second = 0;
    u32 hold_controller_state_before = ActionDiagnosticUnavailable;
    u32 hold_controller_action_before = ActionDiagnosticUnavailable;
    u32 hold_controller_active_before = ActionDiagnosticUnavailable;
    if (action == ActionHoldItem) {
        const auto player_info = Read<u64>(memory, main_base + PlayerInfoStorageOffset);
        const auto player_actor = player_info && *player_info
                                      ? Read<u64>(memory, *player_info + PlayerInfoActorOffset)
                                      : std::nullopt;
        if (!player_info || !*player_info || !player_actor || !*player_actor) {
            LOG_WARNING(Frontend,
                        "[BOTW-HOLD] rejected reason=player-context-unavailable "
                        "playerInfo={:016X} player={:016X}",
                        player_info.value_or(0), player_actor.value_or(0));
            return false;
        }
        const auto controller_storage = Read<u64>(memory, main_base + HoldControllerGotOffset);
        const auto controller = controller_storage && *controller_storage
                                    ? Read<u64>(memory, *controller_storage)
                                    : std::nullopt;
        const auto active_flag_pointer =
            controller && *controller ? Read<u64>(memory, *controller + 0x40) : std::nullopt;
        const auto active_flag = active_flag_pointer && *active_flag_pointer
                                     ? Read<u8>(memory, *active_flag_pointer)
                                     : std::nullopt;
        const auto controller_state =
            controller && *controller ? Read<s32>(memory, *controller + 0xA0) : std::nullopt;
        const auto controller_action =
            controller && *controller ? Read<s32>(memory, *controller + 0x30) : std::nullopt;
        const auto controller_target =
            controller && *controller ? Read<u64>(memory, *controller + 0xA8) : std::nullopt;
        const auto controller_link_first =
            controller && *controller
                ? Read<u64>(memory, *controller + HoldControllerMessageSenderOffset)
                : std::nullopt;
        const auto controller_link_second =
            controller && *controller
                ? Read<u64>(memory, *controller + HoldControllerMessageDispatcherOffset)
                : std::nullopt;
        if (!controller || !*controller || !controller_link_first || !*controller_link_first ||
            !controller_link_second || !*controller_link_second) {
            LOG_WARNING(Frontend,
                        "[BOTW-HOLD] rejected reason=controller-sender-unavailable "
                        "controllerStorage={:016X} controller={:016X} sender={:016X} "
                        "senderVtable={:016X} dispatcher={:016X}",
                        controller_storage.value_or(0), controller.value_or(0),
                        controller.value_or(0) + HoldControllerMessageSenderOffset,
                        controller_link_first.value_or(0), controller_link_second.value_or(0));
            return false;
        }
        LOG_INFO(Frontend,
                 "[BOTW-HOLD] validated playerInfo={:016X} player={:016X} "
                 "controller={:016X} messageSender={:016X} senderVtable={:016X} "
                 "messageDispatcher={:016X} senderIdentity=pause-controller "
                 "heldBefore={}/{} nativeMutation=main+00D9F0E8 "
                 "nativeCapture=main+00CC12F0 nativeAttachMessage=08000003 "
                 "menuControllerRequired=false gameThreadDispatch=guest-hook",
                 *player_info, *player_actor, *controller,
                 *controller + HoldControllerMessageSenderOffset, *controller_link_first,
                 *controller_link_second, *held_count, PouchGrabbedItemCapacity);
        hold_controller_address = controller.value_or(0);
        hold_active_flag_pointer_address = active_flag_pointer.value_or(0);
        hold_controller_target_before = controller_target.value_or(0);
        hold_controller_link_first = controller_link_first.value_or(0);
        hold_controller_link_second = controller_link_second.value_or(0);
        if (controller_state) {
            hold_controller_state_before = static_cast<u32>(*controller_state);
        }
        if (controller_action) {
            hold_controller_action_before = static_cast<u32>(*controller_action);
        }
        if (active_flag) {
            hold_controller_active_before = *active_flag;
        }
        action_player_address = *player_actor;
    }
    if (action == ActionEatItem) {
        eat_selection = FindPouchSelection(memory, *pouch_manager, target->address);
        const auto notifier = Read<u64>(memory, main_base + EatNotifyStorageOffset);
        const auto notifier_actor = notifier && *notifier
                                        ? Read<u64>(memory, *notifier + 0x48)
                                        : std::nullopt;
        const auto player_info = Read<u64>(memory, main_base + PlayerInfoStorageOffset);
        const auto player_actor = player_info && *player_info
                                      ? Read<u64>(memory, *player_info + PlayerInfoActorOffset)
                                      : std::nullopt;
        const auto screen_registry = Read<u64>(memory, main_base + EatScreenRegistryOffset);
        const auto screen_count = screen_registry && *screen_registry
                                      ? Read<u32>(memory, *screen_registry + 0x28)
                                      : std::nullopt;
        const auto screen_array = screen_registry && *screen_registry
                                      ? Read<u64>(memory, *screen_registry + 0x30)
                                      : std::nullopt;
        const size_t screen_slot_offset = screen_count && *screen_count > 0x2F ? 0x178 : 0;
        const auto screen_candidate = screen_array && *screen_array
                                          ? Read<u64>(memory, *screen_array + screen_slot_offset)
                                          : std::nullopt;
        if (!eat_selection || eat_selection->tab >= PouchTabCount ||
            eat_selection->slot >= PouchItemsPerTab || !player_info || !*player_info ||
            !player_actor || !*player_actor || !screen_registry || !*screen_registry ||
            !screen_count || !screen_array || !*screen_array) {
            LOG_WARNING(Frontend,
                        "[BOTW-EAT] rejected reason=native-context-invalid actor={} "
                        "target={:016X} selection={} tab={} slot={} notifier={:016X} "
                        "notifierActor={:016X} "
                        "playerInfo={:016X} player={:016X} screenRegistry={:016X} "
                        "screenArray={:016X} screenCount={} inventoryMenuState=not-consulted "
                        "inventoryMenuRequired=false",
                        target->name, target->address, eat_selection.has_value(),
                        eat_selection ? eat_selection->tab : std::numeric_limits<u32>::max(),
                        eat_selection ? eat_selection->slot : std::numeric_limits<u32>::max(),
                        notifier.value_or(0), notifier_actor.value_or(0), player_info.value_or(0),
                        player_actor.value_or(0), screen_registry.value_or(0),
                        screen_array.value_or(0),
                        screen_count ? std::to_string(*screen_count) : "unavailable");
            return false;
        }
        action_player_address = *player_actor;
        LOG_INFO(Frontend,
                 "[BOTW-EAT] validated actor={} target={:016X} type={} quantityBefore={} "
                 "tab={} slot={} tabHead={:016X} tabItemCount={} nodeOffset={} "
                 "player={:016X} pouchManager={:016X} notifier={:016X} "
                 "notifierActor={:016X} "
                 "screenRegistry={:016X} screenArray={:016X} screenCount={} "
                 "screenCandidate={:016X} inventoryMenuState=not-consulted "
                 "menuContextRequired=false nativeGameplay=00E3EDE4 nativePouchUse=00D97B84 "
                 "nativeLiveEffectUpdate=00CF05F0 nativeAnimationEvent=00D4279C "
                 "gameThreadDispatch=guest-hook",
                 target->name, target->address, target->type, target->value, eat_selection->tab,
                 eat_selection->slot, eat_selection->tab_head, eat_selection->tab_item_count,
                 eat_selection->node_offset, *player_actor, *pouch_manager, notifier.value_or(0),
                 notifier_actor.value_or(0), *screen_registry, *screen_array, *screen_count,
                 screen_candidate.value_or(0));
    }

    const u64 mailbox_request = target->address;
    const u32 mailbox_type = action == ActionEatItem       ? EatMailboxTypeSentinel
                             : action == ActionHoldItem    ? HoldMailboxTypeSentinel
                             : action == ActionUnequipItem ? UnequipMailboxTypeSentinel
                                                           : static_cast<u32>(target->type);

    std::scoped_lock action_lock{EquipActionMutex};
    if (!IsEquipMailboxAvailable(system, equip_mailbox)) {
        LOG_WARNING(Frontend,
                    "[BOTW-ITEM-ACTION] rejected action={} reason=mailbox-unavailable "
                    "mailbox={:016X} requiredSize={}",
                    action_name, equip_mailbox, EquipMailboxSize);
        return false;
    }

    const u32 state = LoadEquipMailbox<u32>(system, equip_mailbox, EquipMailboxStateOffset,
                                            __ATOMIC_ACQUIRE);
    const u64 pending = LoadEquipMailbox<u64>(system, equip_mailbox, EquipMailboxRequestOffset,
                                               __ATOMIC_ACQUIRE);
    const u64 completed = LoadEquipMailbox<u64>(system, equip_mailbox,
                                                 EquipMailboxCompletedOffset,
                                                 __ATOMIC_ACQUIRE);
    if (IsEquipMailboxBusy(state)) {
        const u32 pending_type = LoadEquipMailbox<u32>(system, equip_mailbox,
                                                       EquipMailboxTypeOffset,
                                                       __ATOMIC_ACQUIRE);
        const bool duplicate = pending == mailbox_request && pending_type == mailbox_type;
        LOG_WARNING(Frontend,
                    "[BOTW-ITEM-ACTION] {} action={} reason=mailbox-busy state={} "
                    "pending={:016X} pendingType={:08X} requested={:016X} "
                    "requestedType={:08X}",
                    duplicate ? "coalesced" : "rejected", action_name, state, pending,
                    pending_type, mailbox_request, mailbox_type);
        return duplicate;
    }

    if (action == ActionEquipItem && target->equipped &&
        !(state == EquipMailboxRejected && completed == target->address)) {
        LOG_INFO(Frontend,
                 "[BOTW-EQUIP] no-op actor={} target={:016X} type={} reason=already-equipped",
                 target->name, target->address, target->type);
        return true;
    }
    if ((state != EquipMailboxIdle && state != EquipMailboxSucceeded &&
         state != EquipMailboxRejected) ||
        pending != 0) {
        LOG_WARNING(Frontend,
                    "[BOTW-ITEM-ACTION] rejected action={} reason=mailbox-terminal-invalid "
                    "state={} pending={:016X} completed={:016X}",
                    action_name, state, pending, completed);
        return false;
    }
    if (action_player_address == 0) {
        const auto player_info = Read<u64>(memory, main_base + PlayerInfoStorageOffset);
        const auto player_actor = player_info && *player_info
                                      ? Read<u64>(memory, *player_info + PlayerInfoActorOffset)
                                      : std::nullopt;
        action_player_address = player_actor.value_or(0);
    }
    u32 diagnostic_validation_flags = 0x01U | 0x02U | 0x04U | 0x80U;
    if (eat_selection) {
        diagnostic_validation_flags |= 0x08U;
    }
    if (action_player_address != 0) {
        diagnostic_validation_flags |= 0x10U;
    }
    if (hold_controller_address != 0) {
        diagnostic_validation_flags |= 0x20U;
    }
    if (hold_active_flag_pointer_address != 0) {
        diagnostic_validation_flags |= 0x40U;
    }
    u32 diagnostic_sequence =
        LoadEquipMailbox<u32>(system, equip_mailbox, ActionDiagnosticSequenceOffset,
                              __ATOMIC_RELAXED) + 1;
    if (diagnostic_sequence == 0) {
        diagnostic_sequence = 1;
    }
    StoreEquipMailbox<u64>(system, equip_mailbox, EquipMailboxCompletedOffset, 0,
                           __ATOMIC_RELAXED);
    StoreEquipMailbox<u32>(system, equip_mailbox, EquipMailboxTypeOffset,
                           mailbox_type, __ATOMIC_RELAXED);
    StoreEquipMailbox<u32>(system, equip_mailbox, EquipMailboxReservedOffset,
                           eat_selection ? eat_selection->tab : 0,
                           __ATOMIC_RELAXED);
    StoreEquipMailbox<u32>(system, equip_mailbox, EatMailboxSlotOffset,
                           eat_selection ? eat_selection->slot : 0, __ATOMIC_RELAXED);
    StoreEquipMailbox<u64>(system, equip_mailbox, HoldControllerMailboxOffset,
                           action == ActionHoldItem ? hold_controller_address : 0,
                           __ATOMIC_RELAXED);
    StoreEquipMailbox<u32>(system, equip_mailbox, ActionDiagnosticSequenceOffset,
                           diagnostic_sequence, __ATOMIC_RELAXED);
    StoreEquipMailbox<u32>(system, equip_mailbox, ActionDiagnosticValidationFlagsOffset,
                           diagnostic_validation_flags, __ATOMIC_RELAXED);
    StoreEquipMailbox<s32>(system, equip_mailbox, ActionDiagnosticItemTypeOffset,
                           target->type, __ATOMIC_RELAXED);
    StoreEquipMailbox<s32>(system, equip_mailbox, ActionDiagnosticItemUseOffset,
                           target->item_use, __ATOMIC_RELAXED);
    StoreEquipMailbox<s32>(system, equip_mailbox, ActionDiagnosticQuantityBeforeOffset,
                           target->value, __ATOMIC_RELAXED);
    StoreEquipMailbox<u32>(system, equip_mailbox, ActionDiagnosticInInventoryBeforeOffset,
                           1, __ATOMIC_RELAXED);
    StoreEquipMailbox<u32>(system, equip_mailbox, ActionDiagnosticTabOffset,
                           eat_selection ? eat_selection->tab : ActionDiagnosticUnavailable,
                           __ATOMIC_RELAXED);
    StoreEquipMailbox<u32>(system, equip_mailbox, ActionDiagnosticSlotOffset,
                           eat_selection ? eat_selection->slot : ActionDiagnosticUnavailable,
                           __ATOMIC_RELAXED);
    StoreEquipMailbox<u32>(system, equip_mailbox, ActionDiagnosticHeldBeforeOffset,
                           held_count ? static_cast<u32>(*held_count)
                                      : ActionDiagnosticUnavailable,
                           __ATOMIC_RELAXED);
    StoreEquipMailbox<u32>(system, equip_mailbox, ActionDiagnosticNativeResultOffset,
                           0, __ATOMIC_RELAXED);
    StoreEquipMailbox<u64>(system, equip_mailbox, ActionDiagnosticTargetOffset,
                           target->address, __ATOMIC_RELAXED);
    StoreEquipMailbox<u64>(system, equip_mailbox, ActionDiagnosticPouchManagerOffset,
                           *pouch_manager, __ATOMIC_RELAXED);
    StoreEquipMailbox<u64>(system, equip_mailbox, ActionDiagnosticPlayerOffset,
                           action_player_address, __ATOMIC_RELAXED);
    StoreEquipMailbox<u64>(system, equip_mailbox, ActionDiagnosticControllerOffset,
                           hold_controller_address, __ATOMIC_RELAXED);
    StoreEquipMailbox<u64>(system, equip_mailbox, ActionDiagnosticActiveFlagPointerOffset,
                           hold_active_flag_pointer_address, __ATOMIC_RELAXED);
    StoreEquipMailbox<u32>(system, equip_mailbox, ActionDiagnosticControllerStateOffset,
                           hold_controller_state_before, __ATOMIC_RELAXED);
    StoreEquipMailbox<u32>(system, equip_mailbox, ActionDiagnosticControllerActionOffset,
                           hold_controller_action_before, __ATOMIC_RELAXED);
    StoreEquipMailbox<u64>(system, equip_mailbox, ActionDiagnosticControllerTargetOffset,
                           hold_controller_target_before, __ATOMIC_RELAXED);
    StoreEquipMailbox<u32>(system, equip_mailbox, ActionDiagnosticControllerActiveOffset,
                           hold_controller_active_before, __ATOMIC_RELAXED);
    StoreEquipMailbox<u32>(system, equip_mailbox, ActionDiagnosticNodeOffsetOffset,
                           eat_selection ? eat_selection->node_offset
                                         : ActionDiagnosticUnavailable,
                           __ATOMIC_RELAXED);
    StoreEquipMailbox<u64>(system, equip_mailbox, ActionDiagnosticTabHeadOffset,
                           eat_selection ? eat_selection->tab_head : 0, __ATOMIC_RELAXED);
    StoreEquipMailbox<u32>(system, equip_mailbox, ActionDiagnosticTabItemCountOffset,
                           eat_selection ? eat_selection->tab_item_count
                                         : ActionDiagnosticUnavailable,
                           __ATOMIC_RELAXED);
    StoreEquipMailbox<s32>(system, equip_mailbox, ActionDiagnosticGuestQuantityAfterOffset,
                           -1, __ATOMIC_RELAXED);
    StoreEquipMailbox<u32>(system, equip_mailbox,
                           ActionDiagnosticGuestInInventoryAfterOffset,
                           ActionDiagnosticUnavailable, __ATOMIC_RELAXED);
    StoreEquipMailbox<u32>(system, equip_mailbox, EquipMailboxAttemptsOffset, 0,
                           __ATOMIC_RELAXED);
    StoreEquipMailbox<u32>(system, equip_mailbox, ActionStageOffset, 0,
                           __ATOMIC_RELAXED);
    StoreEquipMailbox<u64>(system, equip_mailbox, EquipMailboxRequestOffset, mailbox_request,
                           __ATOMIC_RELAXED);
    StoreEquipMailbox<u32>(system, equip_mailbox, EquipMailboxStateOffset, EquipMailboxQueued,
                           __ATOMIC_RELEASE);
    LOG_INFO(Frontend,
             "[BOTW-ITEM-ACTION] diagnostic-sequence={} validationFlags={:08X} "
             "actor={} target={:016X} manager={:016X} player={:016X} type={} itemUse={} "
             "quantityBefore={} inInventoryBefore=true tab={} slot={} nodeOffset={} "
             "tabHead={:016X} tabItemCount={} heldBefore={} controller={:016X} "
             "controllerState={} controllerAction={} controllerActive={} "
             "controllerTargetBefore={:016X} controllerLinkFirst={:016X} "
             "controllerLinkSecond={:016X} mailbox={:016X}",
             diagnostic_sequence, diagnostic_validation_flags, target->name, target->address,
             *pouch_manager, action_player_address, target->type, target->item_use, target->value,
             eat_selection ? std::to_string(eat_selection->tab) : "unavailable",
             eat_selection ? std::to_string(eat_selection->slot) : "unavailable",
             eat_selection ? std::to_string(eat_selection->node_offset) : "unavailable",
             eat_selection ? eat_selection->tab_head : 0,
             eat_selection ? std::to_string(eat_selection->tab_item_count) : "unavailable",
             held_count ? std::to_string(*held_count) : "unavailable", hold_controller_address,
             hold_controller_state_before == ActionDiagnosticUnavailable
                 ? "unavailable"
                 : std::to_string(static_cast<s32>(hold_controller_state_before)),
             hold_controller_action_before == ActionDiagnosticUnavailable
                 ? "unavailable"
                 : std::to_string(static_cast<s32>(hold_controller_action_before)),
             hold_controller_active_before == ActionDiagnosticUnavailable
                 ? "unavailable"
                 : std::to_string(hold_controller_active_before),
             hold_controller_target_before, hold_controller_link_first, hold_controller_link_second,
             equip_mailbox);
    if (action == ActionEatItem) {
        LOG_INFO(Frontend,
                 "[BOTW-EAT] queued actor={} target={:016X} type={} quantityBefore={} "
                 "tab={} slot={} state={} nativeGameplay=00E3EDE4 nativePouchUse=00D97B84 "
                 "nativeLiveEffectUpdate=00CF05F0 nativeAnimationEvent=00D4279C",
                 target->name, target->address, target->type, target->value, eat_selection->tab,
                 eat_selection->slot, EquipMailboxQueued);
        LOG_INFO(Frontend,
                 "[BOTW-ANIMATION] queued action=Eat actor={} nativeEvent=main+00D4279C "
                 "dispatchAfterNativeConsume=true",
                 target->name);
    } else if (action == ActionHoldItem) {
        LOG_INFO(Frontend,
                 "[BOTW-ITEM-ACTION] queued action=Hold actor={} target={:016X} "
                 "controller={:016X} quantityBefore={} heldBefore={}/{} native=00DA46A0",
                 target->name, target->address, hold_controller_address, target->value, *held_count,
                 PouchGrabbedItemCapacity);
    } else if (action == ActionUnequipItem) {
        LOG_INFO(Frontend,
                 "[BOTW-EQUIP] queued action=Unequip actor={} target={:016X} type={} "
                 "slot={} equippedBefore=true nativeUnequip=00D9A590 "
                 "nativeDefaultEquipment=00D940C4 nativePlayerSwitch=00CBE4D0",
                 target->name, target->address, target->type,
                 EquipmentSlotName(target->type));
    } else {
        LOG_INFO(Frontend,
                 "[BOTW-EQUIP] queued action=Equip actor={} target={:016X} type={} slot={} "
                 "equippedBefore={} nativeQuickEquip=00D9A4E0 nativeAutoEquip=00D93168",
                 target->name, target->address, target->type,
                 EquipmentSlotName(target->type), target->equipped);
    }
    return true;
}

}
