#include "GameState.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include <windows.h>

#include "Config.h"
#include "FieldCatalog.h"
#include "Log.h"
#include "Mem.h"
#include "Rtti.h"

namespace dis {
namespace {

GameState g_state;

// Exact Dead Island Definitive Edition gameplay build this mod targets. Raw
// gameplay values are used only after both the PE identity and the accessor's
// machine-code signature have matched; another revision simply leaves those
// values unavailable instead of reading an offset that may have moved.
constexpr uint32_t kGameTimestamp = 0x5745B64C;
constexpr uint32_t kGameImageSize = 0x014C8000;
constexpr uintptr_t kVariableAccessorRva = 0x548B79;
constexpr uintptr_t kDefaultVariablesLoadRva = 0x548B4D;
constexpr uintptr_t kLearnedSkillCountRva = 0x6DB580;
constexpr uintptr_t kMenuInventoryPlayerLoadRva = 0x455EC8;
constexpr uintptr_t kMenuExperienceReadRva = 0x53D2E9;
constexpr uintptr_t kGameplayRootRva = 0x127C2F8;
constexpr uint8_t kVariableAccessorSignature[] = {
    0x49, 0x8B, 0x80, 0x18, 0x0A, 0x00, 0x00,  // [player + 0xa18]
    0x48, 0x8B, 0x80, 0xB0, 0x00, 0x00, 0x00,  // [owner + 0xb0]
};
constexpr uint8_t kLearnedSkillCountSignature[] = {
    0x48, 0x8B, 0x81, 0xB8, 0x00, 0x00, 0x00,
    0x45, 0x33, 0xC0, 0x48, 0x85, 0xC0, 0x74, 0x1E,
};
constexpr uint8_t kMenuInventoryPlayerLoadSignature[] = {
    0x48, 0x8B, 0x05, 0x29, 0x64, 0xE2, 0x00,
    0x48, 0x85, 0xC0,
    0x0F, 0x84, 0x29, 0x1A, 0x00, 0x00,
    0x4D, 0x89, 0x6B, 0xD0,
    0x4C, 0x8B, 0xA8, 0x28, 0x06, 0x00, 0x00,
};
// MenuUpgradeCharacter's own XP presenter selects the same global/per-player
// progression source as PlayerDI's variable accessor, then reads current XP at
// +0x80 and the next-level total as the sum of +0x84 and +0x88.  Keep the raw
// layout behind the exact instructions that prove those offsets.
constexpr uint8_t kMenuExperienceReadSignature[] = {
    0x48, 0x8B, 0x05, 0x98, 0xE8, 0xD3, 0x00,  // global source
    0x41, 0x38, 0x75, 0x50,                    // PlayerDI source flag
    0x75, 0x07,
    0x49, 0x8B, 0x85, 0x18, 0x0A, 0x00, 0x00,  // per-player source
    0x8B, 0x88, 0x80, 0x00, 0x00, 0x00,        // current XP
    0x45, 0x8B, 0xAC, 0x24, 0x88, 0x00, 0x00, 0x00,
    0x0F, 0xB7, 0x85, 0xD0, 0x00, 0x00, 0x00,
    0x45, 0x03, 0xAC, 0x24, 0x84, 0x00, 0x00, 0x00,
};
constexpr size_t kPlayerVariablesOffset = 0xA18;
constexpr size_t kGameplayRootPlayerOffset = 0x628;
constexpr size_t kVariableOwnerTableOffset = 0xB0;
constexpr size_t kVariableValueOffset = 0x08;
constexpr uint32_t kPlayerVariableCount = 0x227;
constexpr size_t kVariableSourceFlagOffset = 0x50;

bool IsSupportedGameBuild() {
  static int cached = -1;
  if (cached >= 0) return cached != 0;
  HMODULE game = GetModuleHandleW(L"gamedll_x64_rwdi.dll");
  if (!game) return false;  // it may still be loading; do not cache this case

  auto* image = reinterpret_cast<uint8_t*>(game);
  IMAGE_DOS_HEADER dos = {};
  IMAGE_NT_HEADERS64 nt = {};
  uint8_t signature[sizeof(kVariableAccessorSignature)] = {};
  uint8_t learned_signature[sizeof(kLearnedSkillCountSignature)] = {};
  uint8_t menu_player_signature[sizeof(kMenuInventoryPlayerLoadSignature)] = {};
  uint8_t menu_experience_signature[sizeof(kMenuExperienceReadSignature)] = {};
  const bool matched =
      Read(image, &dos) && dos.e_magic == IMAGE_DOS_SIGNATURE &&
      dos.e_lfanew > 0 && Read(image + dos.e_lfanew, &nt) &&
      nt.Signature == IMAGE_NT_SIGNATURE &&
      nt.FileHeader.TimeDateStamp == kGameTimestamp &&
      nt.OptionalHeader.SizeOfImage == kGameImageSize &&
      Read(image + kVariableAccessorRva, &signature) &&
      std::memcmp(signature, kVariableAccessorSignature,
                  sizeof(signature)) == 0 &&
      Read(image + kLearnedSkillCountRva, &learned_signature) &&
      std::memcmp(learned_signature, kLearnedSkillCountSignature,
                  sizeof(learned_signature)) == 0 &&
       Read(image + kMenuInventoryPlayerLoadRva, &menu_player_signature) &&
       std::memcmp(menu_player_signature, kMenuInventoryPlayerLoadSignature,
                   sizeof(menu_player_signature)) == 0 &&
       Read(image + kMenuExperienceReadRva, &menu_experience_signature) &&
       std::memcmp(menu_experience_signature, kMenuExperienceReadSignature,
                   sizeof(menu_experience_signature)) == 0;
  cached = matched ? 1 : 0;
  if (matched) {
    DIS_INFO("[state] verified gameplay build; PlayerVariables enabled");
  } else {
    DIS_WARN("[state] unfamiliar gamedll build; versioned PlayerVariables"
             " disabled");
  }
  return matched;
}

const uint8_t* MenuPlayerObject() {
  if (!IsSupportedGameBuild()) return nullptr;
  auto* game = reinterpret_cast<uint8_t*>(
      GetModuleHandleW(L"gamedll_x64_rwdi.dll"));
  void* gameplay_root = nullptr;
  void* player = nullptr;
  if (!game || !Read(game + kGameplayRootRva, &gameplay_root) ||
      !gameplay_root ||
      !Read(static_cast<uint8_t*>(gameplay_root) +
                kGameplayRootPlayerOffset,
            &player) ||
      !player || !Readable(player, kPlayerVariablesOffset + sizeof(void*))) {
    return nullptr;
  }
  return static_cast<const uint8_t*>(player);
}

class PlayerVariablesView {
 public:
  bool Open(const CRTTIObject* reflected) {
    (void)reflected;
    table_ = nullptr;
    if (!IsSupportedGameBuild()) return false;
    // Vanilla inventory/progression menus use the gameplay manager's local
    // PlayerDI, not the first scene actor whose reflected class is PlayerDI.
    // The latter can legitimately have an empty progression owner and was the
    // reason XP, capacities, inventory and skills disagreed with the game.
    const uint8_t* complete = MenuPlayerObject();
    if (!complete) return false;

    void* variables = nullptr;
    void* owner = nullptr;
    void* table = nullptr;
    uint8_t use_default_variables = 0;
    if (!Read(complete + kVariableSourceFlagOffset,
              &use_default_variables)) {
      return false;
    }

    // Match PlayerDI's own variable accessor. Local-player state normally
    // comes from LogicalPlayer at +A18; the alternate branch uses the global
    // defaults object loaded by the RIP-relative instruction at 0x548B4D.
    // Following only the former made every variable disappear whenever the
    // byte at +0x50 selected the latter.
    if (use_default_variables) {
      auto* game = reinterpret_cast<uint8_t*>(
          GetModuleHandleW(L"gamedll_x64_rwdi.dll"));
      if (!game) return false;
      const uint8_t* instruction = game + kDefaultVariablesLoadRva;
      uint8_t opcode[3] = {};
      int32_t displacement = 0;
      if (!Read(instruction, &opcode) || opcode[0] != 0x4C ||
          opcode[1] != 0x8B || opcode[2] != 0x0D ||
          !Read(instruction + 3, &displacement)) {
        return false;
      }
      const void* pointer_location = instruction + 7 + displacement;
      if (!Read(pointer_location, &variables) || !variables) return false;
    } else if (!Read(complete + kPlayerVariablesOffset, &variables) ||
               !variables) {
      return false;
    }

    if (!Read(static_cast<uint8_t*>(variables) + kVariableOwnerTableOffset,
              &owner) ||
        !owner || !Read(owner, &table) || !table ||
        (reinterpret_cast<uintptr_t>(table) & (alignof(void*) - 1)) != 0 ||
        !Readable(table, sizeof(void*))) {
      return false;
    }
    table_ = static_cast<const uint8_t*>(table);
    source_object_ = variables;
    source_name_ = use_default_variables ? "global" : "logical-player";
    return true;
  }

  bool Get(uint32_t index, double minimum, double maximum, double* out) const {
    if (!table_ || !out || index >= kPlayerVariableCount) return false;
    void* entry = nullptr;
    if (!Read(table_ + static_cast<size_t>(index) * sizeof(void*), &entry) ||
        !entry ||
        (reinterpret_cast<uintptr_t>(entry) & (alignof(float) - 1)) != 0) {
      return false;
    }
    float value = 0.0f;
    if (!Read(static_cast<uint8_t*>(entry) + kVariableValueOffset, &value) ||
        !std::isfinite(value) || value < minimum || value > maximum) {
      return false;
    }
    *out = value;
    return true;
  }

  bool GetSourceInt(size_t offset, int minimum, int maximum, int* out) const {
    if (!source_object_ || !out) return false;
    int value = 0;
    if (!Read(static_cast<const uint8_t*>(source_object_) + offset, &value) ||
        value < minimum || value > maximum) {
      return false;
    }
    *out = value;
    return true;
  }

  const char* source_name() const { return source_name_; }
  void* source_object() const { return source_object_; }

 private:
  const uint8_t* table_ = nullptr;
  void* source_object_ = nullptr;
  const char* source_name_ = nullptr;
};

bool ReadAvailableSkillPoints(const PlayerVariablesView& variables,
                              Value* out) {
  if (!out || !variables.source_object() || !IsSupportedGameBuild()) {
    return false;
  }

  // MenuUpgradeCharacter uses precisely this formula:
  //   int(PlayerVariable[AllowedSkillsCount]) - CountLearnedSkills(player)
  // The counter is a read-only tree walk at the verified RVA below. Calling
  // the game's implementation avoids duplicating its treatment of respecs,
  // imported characters and multi-rank skills.
  double allowed = 0.0;
  if (!variables.Get(0x001, 0.0, 4096.0, &allowed)) return false;
  auto* game = reinterpret_cast<uint8_t*>(
      GetModuleHandleW(L"gamedll_x64_rwdi.dll"));
  if (!game) return false;
  using CountLearnedSkillsFn = int (*)(void*);
  void* address = game + kLearnedSkillCountRva;
  if (!Executable(address, 1)) return false;
  const int learned = reinterpret_cast<CountLearnedSkillsFn>(address)(
      variables.source_object());
  const int available = static_cast<int>(allowed) - learned;
  if (learned < 0 || learned > 4096 || available < 0 || available > 4096) {
    return false;
  }
  out->v = available;
  out->valid = true;
  out->bound_to = "PlayerSkills.Available";
  return true;
}

CRTTIObject* LogicalPlayerObject(const CRTTIObject* reflected) {
  (void)reflected;
  if (!IsSupportedGameBuild()) return nullptr;
  const uint8_t* complete = MenuPlayerObject();
  if (!complete) return nullptr;
  uint8_t use_default_variables = 0;
  if (!Read(complete + kVariableSourceFlagOffset,
            &use_default_variables) || use_default_variables) {
    // The global owner intentionally has no CRTTI registration, so it cannot
    // be passed to the reflected-field fallback below. PlayerVariablesView
    // still reads its authoritative values directly.
    return nullptr;
  }
  CRTTIObject* logical = nullptr;
  if (!Read(complete + kPlayerVariablesOffset, &logical) || !logical ||
      !TheRtti().Validate(logical) ||
      TheRtti().ClassName(logical) != "LogicalPlayer") {
    return nullptr;
  }
  return logical;
}

void ReadPlayerVariable(const PlayerVariablesView& variables, uint32_t index,
                        double minimum, double maximum, const char* name,
                        Value* out) {
  double value = 0.0;
  if (!out || !variables.Get(index, minimum, maximum, &value)) return;
  out->v = value;
  out->valid = true;
  out->bound_to = name;
}

void ReadPlayerScalar(const CRTTIObject* reflected, size_t offset,
                      double minimum, double maximum, const char* name,
                      Value* out) {
  (void)reflected;
  if (!out || !IsSupportedGameBuild()) return;
  const uint8_t* complete = MenuPlayerObject();
  if (!complete) return;
  float value = 0.0f;
  if (!Read(complete + offset, &value) || !std::isfinite(value) ||
      value < minimum || value > maximum) {
    return;
  }
  out->v = value;
  out->valid = true;
  out->bound_to = name;
}

void ReadPlayerInteger(const CRTTIObject* reflected, size_t offset,
                       int minimum, int maximum, const char* name,
                       Value* out) {
  (void)reflected;
  if (!out || !IsSupportedGameBuild()) return;
  const uint8_t* complete = MenuPlayerObject();
  if (!complete) return;
  int value = 0;
  if (!Read(complete + offset, &value) || value < minimum || value > maximum) {
    return;
  }
  out->v = value;
  out->valid = true;
  out->bound_to = name;
}

// Reads the first name in `names` that the object answers to, and remembers
// which one that was. The remembered name is not used to skip the search on
// later samples -- GetNumber is a string compare inside the engine and costs
// less than the bookkeeping would -- it is kept so the log can say what this
// build actually calls each value.
void ReadFirst(const CRTTIObject* player, const char* const* names, int count,
               const char* cached_name, Value* out) {
  double v = 0.0;
  const char* which = nullptr;
  if (cached_name && TheRtti().GetNumber(player, cached_name, &v)) {
    which = cached_name;
  } else if (!TheRtti().GetFirstNumber(player, names, count, &v, &which)) {
    out->valid = false;
    out->bound_to = nullptr;
    return;
  }
  {
    out->v = v;
    out->valid = true;
    out->bound_to = which;
  }
}

void LogBinding(const char* label, const Value& value) {
  if (value.valid) {
    DIS_INFO("[bind]   %-14s <- %-28s = %.3f", label,
             value.bound_to ? value.bound_to : "?", value.v);
  } else {
    DIS_INFO("[bind]   %-14s <- (none matched)", label);
  }
}

}  // namespace

float Value::Ratio(const Value& cur, const Value& max) {
  if (!cur.valid || !max.valid || max.v <= 0.0) return -1.0f;
  double r = cur.v / max.v;
  if (r < 0.0) r = 0.0;
  if (r > 1.0) r = 1.0;
  return static_cast<float>(r);
}

GameState& TheGameState() { return g_state; }

#ifdef DIS_UI_PREVIEW
void GameState::SetPreviewData() {
  auto set = [](Value* value, double number, const char* name) {
    value->v = number;
    value->valid = true;
    value->bound_to = name;
  };
  bound_ = true;
  set(&vitals_.health, 124, "preview");
  set(&vitals_.max_health, 150, "preview");
  set(&vitals_.stamina, 98, "preview");
  set(&vitals_.max_stamina, 120, "preview");
  set(&vitals_.fury, 72, "preview");
  set(&vitals_.max_fury, 100, "preview");
  set(&vitals_.fury_active, 0, "preview");
  set(&vitals_.level, 16, "preview");
  set(&vitals_.experience, 5420, "preview");
  set(&vitals_.exp_to_next, 12000, "preview");
  set(&vitals_.money, 8742, "preview");
  set(&vitals_.skill_points, 2, "preview");
  set(&inventory_capacity_.slots, 18, "preview");
  set(&inventory_capacity_.max_slots, 30, "preview");
  set(&inventory_capacity_.quick_slots, 8, "preview");
  set(&inventory_capacity_.max_quick_slots, 8, "preview");
}
#endif

void GameState::Unbind(const char* reason) {
  if (!bound_) return;
  DIS_INFO("[bind] dropped (%s)", reason ? reason : "unspecified");
  bound_ = false;
  bound_player_ = nullptr;
  max_health_observed_ = 0.0;
  vitals_ = Vitals{};
  inventory_capacity_ = InventoryCapacity{};
}

void GameState::Bind(CRTTIObject* player) {
  if (!player) { Unbind("no player"); return; }
  if (bound_ && bound_player_ == player) return;

  bound_player_ = player;
  bound_ = true;
  Sample(player);

  DIS_INFO("[bind] binding vitals against player %p", player);
  LogBinding("health", vitals_.health);
  LogBinding("max health", vitals_.max_health);
  LogBinding("stamina", vitals_.stamina);
  LogBinding("max stamina", vitals_.max_stamina);
  LogBinding("fury", vitals_.fury);
  LogBinding("max fury", vitals_.max_fury);
  LogBinding("level", vitals_.level);
  LogBinding("experience", vitals_.experience);
  LogBinding("exp to next", vitals_.exp_to_next);
  LogBinding("money", vitals_.money);
  LogBinding("skill points", vitals_.skill_points);
  LogBinding("inventory", inventory_capacity_.slots);
  LogBinding("inventory max", inventory_capacity_.max_slots);
  LogBinding("quick slots", inventory_capacity_.quick_slots);
  LogBinding("quick max", inventory_capacity_.max_quick_slots);

  if (!vitals_.usable()) {
    // Nothing bound. That is the interesting case, so say why loudly and then
    // enumerate everything the object does have -- the answer is in that list.
    // Bind runs once per player pointer, and the output is capped, so a failed
    // field map cannot become per-frame log spam.
    DIS_ERR("[bind] no health field matched on class of player %p", player);
    if (TheRtti().vector_layout().known()) {
      const std::vector<Rtti::Probe> fields = TheRtti().DumpAllFields(player);
      if (fields.empty()) {
        DIS_WARN("[bind] live field enumeration returned no fields");
      } else {
        const size_t shown = fields.size() < 256 ? fields.size() : 256;
        DIS_INFO("[bind] player declares %d fields; logging %d",
                 static_cast<int>(fields.size()), static_cast<int>(shown));
        for (size_t i = 0; i < shown; ++i) {
          const Rtti::Probe& field = fields[i];
          DIS_INFO("[bind]   %-30s %-7s %s", field.name.c_str(), field.kind,
                   field.value.c_str());
        }
        if (shown < fields.size()) {
          DIS_INFO("[bind]   ... %d additional fields omitted",
                   static_cast<int>(fields.size() - shown));
        }
      }
    }
  }
  if (Cfg().GetBool("advanced.dump_player", false)) Dump(player, "player");
}

void GameState::Sample(CRTTIObject* player) {
  if (!player) {
    vitals_ = Vitals{};
    inventory_capacity_ = InventoryCapacity{};
    return;
  }
  Vitals v;
  ReadFirst(player, DIS_NAMES(catalog::kHealth), vitals_.health.bound_to, &v.health);
  ReadFirst(player, DIS_NAMES(catalog::kMaxHealth), vitals_.max_health.bound_to, &v.max_health);
  ReadFirst(player, DIS_NAMES(catalog::kStamina), vitals_.stamina.bound_to, &v.stamina);
  ReadFirst(player, DIS_NAMES(catalog::kMaxStamina), vitals_.max_stamina.bound_to, &v.max_stamina);
  ReadFirst(player, DIS_NAMES(catalog::kFury), vitals_.fury.bound_to, &v.fury);
  ReadFirst(player, DIS_NAMES(catalog::kMaxFury), vitals_.max_fury.bound_to, &v.max_fury);
  ReadFirst(player, DIS_NAMES(catalog::kFuryActive), vitals_.fury_active.bound_to, &v.fury_active);
  ReadFirst(player, DIS_NAMES(catalog::kLevel), vitals_.level.bound_to, &v.level);
  ReadFirst(player, DIS_NAMES(catalog::kExperience), vitals_.experience.bound_to, &v.experience);
  ReadFirst(player, DIS_NAMES(catalog::kExpToNext), vitals_.exp_to_next.bound_to, &v.exp_to_next);
  ReadFirst(player, DIS_NAMES(catalog::kMoney), vitals_.money.bound_to, &v.money);
  ReadFirst(player, DIS_NAMES(catalog::kSkillPoints), vitals_.skill_points.bound_to, &v.skill_points);

  // PlayerDI mirrors the level for replication, but the authoritative
  // progression fields live on its LogicalPlayer object.
  CRTTIObject* logical_player = LogicalPlayerObject(player);
  if (logical_player) {
    if (!v.experience.valid) {
      ReadFirst(logical_player, DIS_NAMES(catalog::kExperience),
                vitals_.experience.bound_to, &v.experience);
    }
    if (!v.level.valid) {
      ReadFirst(logical_player, DIS_NAMES(catalog::kLevel),
                vitals_.level.bound_to, &v.level);
    }
    if (!v.money.valid) {
      ReadFirst(logical_player, DIS_NAMES(catalog::kMoney),
                vitals_.money.bound_to, &v.money);
    }
    if (!v.skill_points.valid) {
      ReadFirst(logical_player, DIS_NAMES(catalog::kSkillPoints),
                vitals_.skill_points.bound_to, &v.skill_points);
    }
  }

  // These are the exact scalar locations used by PlayerDI's own health and
  // stamina code in the verified build. They are not registered as reflected
  // fields, which is why name probing can never discover them.
  if (!v.max_health.valid) {
    ReadPlayerScalar(player, 0xD34, 0.01, 1.0e6,
                     "PlayerDI.HealthCap", &v.max_health);
  }
  if (!v.stamina.valid) {
    ReadPlayerScalar(player, 0xDBC, 0.0, 1.0e6,
                     "PlayerDI.CurrentStamina", &v.stamina);
  }
  if (!v.fury.valid) {
    ReadPlayerScalar(player, 0x1CD8, 0.0, 1.0e9,
                     "PlayerDI.CurrentFuryPoints", &v.fury);
  }
  if (!v.money.valid) {
    ReadPlayerInteger(player, 0xBC8, 0, 2000000000,
                      "PlayerDI.SpendableCash", &v.money);
  }

  // Maxima and capacity are not CRTTI fields on PlayerDI. They live in the
  // game's PlayerVariables table; read the exact, version-gated table the game
  // itself uses. A reflected value always wins if a future compatible build
  // exposes one directly.
  PlayerVariablesView variables;
  const bool variables_ready = variables.Open(player);
  if (variables_ready) {
    auto fill = [&](Value* value, uint32_t index, double minimum,
                    double maximum, const char* name) {
      if (!value->valid) {
        ReadPlayerVariable(variables, index, minimum, maximum, name, value);
      }
    };
    fill(&v.exp_to_next, 0x071, 0.0, 1.0e12,
         "PlayerVariables.ExpToNextLevel");
    fill(&v.max_stamina, 0x0F4, 0.01, 1.0e6,
         "PlayerVariables.MaxStamina");
    fill(&v.max_health, 0x18D, 0.01, 1.0e6,
         "PlayerVariables.MaxHealth");
    fill(&v.max_fury, 0x1B1, 0.01, 1.0e9,
         "PlayerVariables.FuryPointsNeededToActivate");
    // Mirror MenuUpgradeCharacter's XP presenter exactly.  The source selected
    // by PlayerDI+0x50 is a variables/progression object, not LogicalPlayer's
    // reflected complete object.  LogicalPlayer::m_Experience is indeed at
    // +0x50, but applying that unrelated reflected offset to this source is why
    // the panel always displayed zero.  Vanilla reads current XP at +0x80 and
    // computes the next-level total from +0x84 + +0x88.
    int experience = 0;
    if (variables.GetSourceInt(0x80, 0, 2000000000, &experience)) {
      v.experience.v = experience;
      v.experience.valid = true;
      v.experience.bound_to = "MenuUpgradeCharacter.ProgressionSource+0x80";
    }
    int level_base = 0;
    int level_delta = 0;
    if (variables.GetSourceInt(0x84, 0, 2000000000, &level_base) &&
        variables.GetSourceInt(0x88, 0, 2000000000, &level_delta)) {
      const long long next = static_cast<long long>(level_base) + level_delta;
      if (next >= 0 && next <= 2000000000LL) {
        v.exp_to_next.v = static_cast<double>(next);
        v.exp_to_next.valid = true;
        v.exp_to_next.bound_to =
            "MenuUpgradeCharacter.ProgressionSource+0x84/+0x88";
      }
    }
    if (!v.skill_points.valid) {
      ReadAvailableSkillPoints(variables, &v.skill_points);
    }
  }

  // Current stamina is a normalized 0..1 charge. Rendering it directly with
  // Whole() rounded 0.8 to "1"; present the same state as a 0..100 meter.
  if (v.stamina.valid && v.stamina.v >= 0.0 && v.stamina.v <= 1.001) {
    v.stamina.v *= 100.0;
    v.max_stamina.v = 100.0;
    v.max_stamina.valid = true;
    v.max_stamina.bound_to = "PlayerDI.StaminaPercent";
  }
  // Some character bonuses are already included in m_Health while the
  // exported base cap remains 100. Preserve the highest proven live health as
  // a lower bound for the session maximum; otherwise a healthy character can
  // be shown as 120 / 100 and every segmented bar stays permanently full.
  if (v.health.valid) {
    max_health_observed_ = std::max(max_health_observed_, v.health.v);
    if (!v.max_health.valid || v.max_health.v < max_health_observed_) {
      v.max_health.v = max_health_observed_;
      v.max_health.valid = true;
      v.max_health.bound_to = "PlayerDI.ObservedHealthCap";
    }
  }
  if (v.fury.valid && v.max_fury.valid && v.max_fury.v <= 1.001 &&
      v.fury.v <= 1.001) {
    v.fury.v *= 100.0;
    v.max_fury.v = 100.0;
    v.max_fury.bound_to = "PlayerDI.FuryPercent";
  }
  vitals_ = v;

  InventoryCapacity capacity;
  ReadFirst(player, DIS_NAMES(catalog::kInventorySize),
            inventory_capacity_.slots.bound_to, &capacity.slots);
  ReadFirst(player, DIS_NAMES(catalog::kMaxInventorySize),
            inventory_capacity_.max_slots.bound_to, &capacity.max_slots);
  ReadFirst(player, DIS_NAMES(catalog::kQuickSlotsCount),
            inventory_capacity_.quick_slots.bound_to, &capacity.quick_slots);
  ReadFirst(player, DIS_NAMES(catalog::kQuickSlotsMaxCount),
            inventory_capacity_.max_quick_slots.bound_to,
            &capacity.max_quick_slots);
  if (variables_ready) {
    auto fill = [&](Value* value, uint32_t index, const char* name) {
      if (!value->valid) {
        ReadPlayerVariable(variables, index, 0.0, 4096.0, name, value);
      }
    };
    fill(&capacity.slots, 0x0D2, "PlayerVariables.InventorySize");
    fill(&capacity.max_slots, 0x0D3, "PlayerVariables.MaxInventorySize");
    fill(&capacity.quick_slots, 0x0D4, "PlayerVariables.QuickSlotsCount");
    fill(&capacity.max_quick_slots, 0x0D5,
         "PlayerVariables.QuickSlotsMaxCount");
  }
  inventory_capacity_ = capacity;
}

void GameState::Dump(CRTTIObject* player, const char* label) const {
  if (!player) return;
  const Rtti& rtti = TheRtti();
  const std::string class_name = rtti.ClassName(player);
  DIS_INFO("[dump] %s: class '%s' at %p", label ? label : "object",
           class_name.c_str(), player);

  // `dump_player` is a discovery switch, so enumerate the class's real field
  // table when the engine layout has been proven. The old implementation only
  // retried the candidate dictionary, which could report two matches while
  // hiding every unknown field -- exactly the information the switch exists
  // to discover.
  std::vector<Rtti::Probe> hits;
  bool enumerated = false;
  if (rtti.vector_layout().known()) {
    hits = rtti.DumpAllFields(player);
    enumerated = !hits.empty();
  }
  if (!enumerated) {
    hits = rtti.ProbeFields(player, DIS_NAMES(catalog::kSweep));
  }
  if (hits.empty()) {
    DIS_ERR("[dump]   no fields were returned by enumeration or the fallback sweep");
    return;
  }
  const size_t shown = std::min<size_t>(hits.size(), 512);
  DIS_INFO("[dump]   %d fields (%s); logging %d:",
           static_cast<int>(hits.size()), enumerated ? "enumerated" : "sweep fallback",
           static_cast<int>(shown));
  for (size_t i = 0; i < shown; ++i) {
    const Rtti::Probe& hit = hits[i];
    DIS_INFO("[dump]     %-30s %-7s %s", hit.name.c_str(), hit.kind,
             hit.value.c_str());
  }
  if (shown < hits.size()) {
    DIS_INFO("[dump]     ... %d additional fields omitted",
             static_cast<int>(hits.size() - shown));
  }
}

}  // namespace dis
