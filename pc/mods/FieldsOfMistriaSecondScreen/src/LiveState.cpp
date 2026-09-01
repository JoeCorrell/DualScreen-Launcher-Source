#include "LiveState.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "Log.h"

namespace fomds {
namespace {

constexpr char kProtocol[] = "fields-of-mistria-dualscreen";
constexpr std::uint32_t kSchema = 1;
constexpr std::size_t kMaxStateBytes = 4 * 1024 * 1024;
constexpr std::size_t kMaxAckBytes = 16 * 1024;
constexpr std::uint64_t kMaxExactJsonInteger = 9007199254740991ULL;
constexpr std::uint32_t kAllTopics = (1u << kLiveTopicCount) - 1u;

const char* const kTopicNames[kLiveTopicCount] = {
    "session", "status", "progression", "inventory", "armor", "hotbar",
    "location", "player", "map", "villagers", "animals", "magic",
    "almanac", "museum", "renown", "calendar_events", "calendar", "clock",
    "weather", "quests"};

bool Equal(const char* a, const char* b) { return std::strcmp(a, b) == 0; }

bool ValidUtf8(const char* data, std::size_t size) {
  for (std::size_t i = 0; i < size;) {
    const unsigned char first = static_cast<unsigned char>(data[i]);
    if (first <= 0x7f) { ++i; continue; }
    std::size_t count = 0; std::uint32_t cp = 0;
    if ((first & 0xe0) == 0xc0) { count = 2; cp = first & 0x1f; }
    else if ((first & 0xf0) == 0xe0) { count = 3; cp = first & 0x0f; }
    else if ((first & 0xf8) == 0xf0) { count = 4; cp = first & 7; }
    else return false;
    if (i + count > size) return false;
    for (std::size_t j = 1; j < count; ++j) {
      const unsigned char c = static_cast<unsigned char>(data[i + j]);
      if ((c & 0xc0) != 0x80) return false;
      cp = (cp << 6) | (c & 0x3f);
    }
    if ((count == 2 && cp < 0x80) || (count == 3 && cp < 0x800) ||
        (count == 4 && cp < 0x10000) || cp > 0x10ffff ||
        (cp >= 0xd800 && cp <= 0xdfff)) return false;
    i += count;
  }
  return true;
}

class JsonReader {
 public:
  JsonReader(const char* data, std::size_t size) : data_(data), size_(size) {}

  bool End() {
    Space();
    return ok_ && position_ == size_;
  }
  bool ok() const { return ok_; }
  std::size_t error_offset() const { return error_offset_; }

  bool Null() {
    Space();
    if (position_ + 4 <= size_ &&
        std::memcmp(data_ + position_, "null", 4) == 0) {
      position_ += 4;
      return true;
    }
    return false;
  }

  bool Boolean(bool* output) {
    Space();
    if (Match("true", 4)) { *output = true; return true; }
    if (Match("false", 5)) { *output = false; return true; }
    return Fail();
  }

  bool Number(double* output) {
    Space();
    const std::size_t begin = position_;
    if (Peek('-')) ++position_;
    if (Peek('0')) {
      ++position_;
    } else {
      if (!Digit()) return FailAt(begin);
      while (Digit()) ++position_;
    }
    if (Peek('.')) {
      ++position_;
      if (!Digit()) return FailAt(begin);
      while (Digit()) ++position_;
    }
    if (Peek('e') || Peek('E')) {
      ++position_;
      if (Peek('+') || Peek('-')) ++position_;
      if (!Digit()) return FailAt(begin);
      while (Digit()) ++position_;
    }
    if (position_ == begin || position_ - begin >= 96) return FailAt(begin);
    char text[96] = {};
    std::memcpy(text, data_ + begin, position_ - begin);
    char* end = nullptr;
    errno = 0;
    const double value = std::strtod(text, &end);
    if (errno == ERANGE || !end || *end || !std::isfinite(value))
      return FailAt(begin);
    *output = value;
    return true;
  }

  bool Integer(std::int64_t* output) {
    double value = 0.0;
    if (!Number(&value) || std::floor(value) != value ||
        value < static_cast<double>(INT64_MIN) ||
        value > static_cast<double>(INT64_MAX)) return Fail();
    *output = static_cast<std::int64_t>(value);
    return true;
  }

  bool Unsigned(std::uint64_t* output) {
    std::int64_t value = 0;
    if (!Integer(&value) || value < 0) return Fail();
    *output = static_cast<std::uint64_t>(value);
    return true;
  }

  template <std::size_t N>
  bool String(FixedUtf8<N>* output, bool* any_truncated = nullptr) {
    bool truncated = false;
    if (!StringBuffer(output->value, N, &truncated)) return false;
    output->truncated = truncated;
    if (truncated && any_truncated) *any_truncated = true;
    return true;
  }

  bool Key(char* output, std::size_t capacity) {
    bool ignored = false;
    return StringBuffer(output, capacity, &ignored);
  }

  template <typename F>
  bool Object(F&& field) {
    if (!Take('{')) return false;
    Space();
    if (TakeIf('}')) return true;
    for (;;) {
      char key[96] = {};
      if (!Key(key, sizeof(key)) || !Take(':') || !field(key)) return Fail();
      Space();
      if (TakeIf('}')) return true;
      if (!Take(',')) return false;
    }
  }

  template <typename F>
  bool Array(F&& element, std::size_t* length = nullptr) {
    if (!Take('[')) return false;
    std::size_t index = 0;
    Space();
    if (TakeIf(']')) { if (length) *length = 0; return true; }
    for (;;) {
      if (!element(index++)) return Fail();
      Space();
      if (TakeIf(']')) { if (length) *length = index; return true; }
      if (!Take(',')) return false;
    }
  }

  bool Skip(unsigned depth = 0) {
    if (depth > 32) return Fail();
    Space();
    if (position_ >= size_) return Fail();
    if (data_[position_] == '"') {
      char ignored[1]; bool truncated = false;
      return StringBuffer(ignored, sizeof(ignored), &truncated);
    }
    if (data_[position_] == '{')
      return Object([&](const char*) { return Skip(depth + 1); });
    if (data_[position_] == '[')
      return Array([&](std::size_t) { return Skip(depth + 1); });
    if (Null()) return true;
    bool value = false;
    if (position_ + 4 <= size_ && std::memcmp(data_ + position_, "true", 4) == 0)
      return Boolean(&value);
    if (position_ + 5 <= size_ && std::memcmp(data_ + position_, "false", 5) == 0)
      return Boolean(&value);
    double number = 0;
    return Number(&number);
  }

 private:
  void Space() {
    while (position_ < size_ &&
           (data_[position_] == ' ' || data_[position_] == '\t' ||
            data_[position_] == '\r' || data_[position_] == '\n')) ++position_;
  }
  bool Peek(char c) const { return position_ < size_ && data_[position_] == c; }
  bool Digit() const { return position_ < size_ && data_[position_] >= '0' && data_[position_] <= '9'; }
  bool Match(const char* text, std::size_t count) {
    if (position_ + count > size_ || std::memcmp(data_ + position_, text, count)) return false;
    position_ += count;
    return true;
  }
  bool Take(char c) { Space(); if (!TakeIf(c)) return Fail(); return true; }
  bool TakeIf(char c) { if (!Peek(c)) return false; ++position_; return true; }
  bool Fail() { return FailAt(position_); }
  bool FailAt(std::size_t at) { if (ok_) error_offset_ = at; ok_ = false; return false; }

  static bool Hex(char c, std::uint32_t* value) {
    if (c >= '0' && c <= '9') *value = c - '0';
    else if (c >= 'a' && c <= 'f') *value = c - 'a' + 10;
    else if (c >= 'A' && c <= 'F') *value = c - 'A' + 10;
    else return false;
    return true;
  }
  bool Hex4(std::uint32_t* value) {
    if (position_ + 4 > size_) return Fail();
    std::uint32_t result = 0;
    for (int i = 0; i < 4; ++i) {
      std::uint32_t digit = 0;
      if (!Hex(data_[position_++], &digit)) return Fail();
      result = (result << 4) | digit;
    }
    *value = result;
    return true;
  }
  static void PutUtf8(std::uint32_t cp, char* output, std::size_t capacity,
                      std::size_t* written, bool* truncated) {
    char encoded[4]; std::size_t count = 0;
    if (cp <= 0x7f) encoded[count++] = static_cast<char>(cp);
    else if (cp <= 0x7ff) { encoded[count++] = static_cast<char>(0xc0 | (cp >> 6)); encoded[count++] = static_cast<char>(0x80 | (cp & 63)); }
    else if (cp <= 0xffff) { encoded[count++] = static_cast<char>(0xe0 | (cp >> 12)); encoded[count++] = static_cast<char>(0x80 | ((cp >> 6) & 63)); encoded[count++] = static_cast<char>(0x80 | (cp & 63)); }
    else { encoded[count++] = static_cast<char>(0xf0 | (cp >> 18)); encoded[count++] = static_cast<char>(0x80 | ((cp >> 12) & 63)); encoded[count++] = static_cast<char>(0x80 | ((cp >> 6) & 63)); encoded[count++] = static_cast<char>(0x80 | (cp & 63)); }
    if (*written + count >= capacity) { *truncated = true; return; }
    std::memcpy(output + *written, encoded, count); *written += count;
  }
  bool StringBuffer(char* output, std::size_t capacity, bool* truncated) {
    Space();
    if (!TakeIf('"') || !output || !capacity) return Fail();
    std::size_t written = 0; *truncated = false;
    while (position_ < size_) {
      unsigned char c = static_cast<unsigned char>(data_[position_++]);
      if (c == '"') { output[std::min(written, capacity - 1)] = 0; return true; }
      if (c < 0x20) return Fail();
      if (c != '\\') { if (written + 1 < capacity) output[written++] = static_cast<char>(c); else *truncated = true; continue; }
      if (position_ >= size_) return Fail();
      c = static_cast<unsigned char>(data_[position_++]);
      if (c == '"' || c == '\\' || c == '/') PutUtf8(c, output, capacity, &written, truncated);
      else if (c == 'b') PutUtf8('\b', output, capacity, &written, truncated);
      else if (c == 'f') PutUtf8('\f', output, capacity, &written, truncated);
      else if (c == 'n') PutUtf8('\n', output, capacity, &written, truncated);
      else if (c == 'r') PutUtf8('\r', output, capacity, &written, truncated);
      else if (c == 't') PutUtf8('\t', output, capacity, &written, truncated);
      else if (c == 'u') {
        std::uint32_t cp = 0; if (!Hex4(&cp)) return false;
        if (cp >= 0xd800 && cp <= 0xdbff) {
          if (position_ + 2 > size_ || data_[position_++] != '\\' || data_[position_++] != 'u') return Fail();
          std::uint32_t low = 0; if (!Hex4(&low) || low < 0xdc00 || low > 0xdfff) return Fail();
          cp = 0x10000 + ((cp - 0xd800) << 10) + (low - 0xdc00);
        } else if (cp >= 0xdc00 && cp <= 0xdfff) return Fail();
        PutUtf8(cp, output, capacity, &written, truncated);
      } else return Fail();
    }
    return Fail();
  }

  const char* data_ = nullptr;
  std::size_t size_ = 0;
  std::size_t position_ = 0;
  std::size_t error_offset_ = 0;
  bool ok_ = true;
};

bool OptionalNum(JsonReader& r, OptionalNumber* out) {
  if (r.Null()) { *out = {}; return true; }
  out->present = true; return r.Number(&out->value);
}
bool OptionalInt(JsonReader& r, OptionalInteger* out) {
  if (r.Null()) { *out = {}; return true; }
  out->present = true; return r.Integer(&out->value);
}
bool OptionalBool(JsonReader& r, OptionalBoolean* out) {
  if (r.Null()) { *out = {}; return true; }
  out->present = true;
  return r.Boolean(&out->value);
}
template <std::size_t N>
bool OptionalString(JsonReader& r, OptionalText<N>* out, bool* truncated) {
  if (r.Null()) { *out = {}; return true; }
  out->present = true; return r.String(&out->text, truncated);
}

template <typename F>
bool Fields(JsonReader& r, F&& f) { return r.Object(std::forward<F>(f)); }

bool ParseCapabilities(JsonReader& r, CapabilityState* v) {
  std::uint32_t seen = 0;
  return Fields(r, [&](const char* k) {
    if (Equal(k,"select_hotbar")) { seen|=1; return r.Boolean(&v->select_hotbar); }
    if (Equal(k,"swap_slots")) { seen|=2; return r.Boolean(&v->swap_slots); }
    if (Equal(k,"consume_item")) { seen|=4; return r.Boolean(&v->consume_item); }
    if (Equal(k,"equip_armor")) { seen|=8; return r.Boolean(&v->equip_armor); }
    if (Equal(k,"unequip_armor")) { seen|=16; return r.Boolean(&v->unequip_armor); }
    if (Equal(k,"pin_spell")) { seen|=32; return r.Boolean(&v->pin_spell); }
    if (Equal(k,"unpin_spell")) { seen|=64; return r.Boolean(&v->unpin_spell); }
    if (Equal(k,"cast_pinned_spell")) { seen|=128; return r.Boolean(&v->cast_pinned_spell); }
    if (Equal(k,"generic_item_use")) { seen|=256; return r.Boolean(&v->item_use); }
    if (Equal(k,"drop_item")) { seen|=512; return r.Boolean(&v->drop_item); }
    if (Equal(k,"quest_mutation")) { seen|=1024; return r.Boolean(&v->quest_mutation); }
    if (Equal(k,"quest_tracking")) { seen|=2048; return r.Boolean(&v->quest_tracking); }
    return r.Skip();
  }) && seen == 4095 && !v->item_use && !v->drop_item &&
      !v->quest_mutation && !v->quest_tracking;
}

bool ParseSession(JsonReader& r, SessionTopic* v, bool* tr) {
  std::uint32_t seen=0;
  return Fields(r,[&](const char* k){
    if(Equal(k,"in_game")){seen|=1;return r.Boolean(&v->in_game);}
    if(Equal(k,"game_version")){seen|=2;return r.String(&v->game_version,tr);}
    if(Equal(k,"game_identifier")){seen|=4;return r.String(&v->game_identifier,tr);}
    if(Equal(k,"capabilities")){seen|=8;return ParseCapabilities(r,&v->capabilities);}
    return r.Skip();}) && seen==15;
}

bool ParseStatus(JsonReader& r, StatusTopic* v, bool* tr) {
  std::uint32_t seen=0;
  return Fields(r,[&](const char* k){
    if(Equal(k,"name")){seen|=1;return r.String(&v->name,tr);} if(Equal(k,"farm_name")){seen|=2;return r.String(&v->farm_name,tr);}
    double* n=nullptr; std::uint32_t bit=0;
    if(Equal(k,"health")){n=&v->health;bit=4;} else if(Equal(k,"health_max")){n=&v->health_max;bit=8;} else if(Equal(k,"stamina")){n=&v->stamina;bit=16;} else if(Equal(k,"stamina_max")){n=&v->stamina_max;bit=32;} else if(Equal(k,"mana")){n=&v->mana;bit=64;} else if(Equal(k,"mana_max")){n=&v->mana_max;bit=128;} else if(Equal(k,"tesserae")){n=&v->tesserae;bit=256;} else if(Equal(k,"essence")){n=&v->essence;bit=512;} else if(Equal(k,"renown")){n=&v->renown;bit=1024;}
    if(n){seen|=bit;return r.Number(n);} return r.Skip();}) && seen==2047;
}

bool ParseSkill(JsonReader& r, SkillState* v, bool* tr) {
  unsigned seen=0; return Fields(r,[&](const char* k){ if(Equal(k,"id")){seen|=1;return r.String(&v->id,tr);} if(Equal(k,"xp")){seen|=2;return r.Number(&v->xp);} if(Equal(k,"level")){seen|=4;return r.Integer(&v->level);}if(Equal(k,"icon_asset")){seen|=8;return r.String(&v->icon_asset,tr);} return r.Skip();})&&seen==15;
}
bool ParsePerk(JsonReader& r, PerkState* v, bool* tr) {
  unsigned seen=0; return Fields(r,[&](const char* k){if(Equal(k,"id")){seen|=1;return r.String(&v->id,tr);}if(Equal(k,"active")){seen|=2;return r.Boolean(&v->active);}return r.Skip();})&&seen==3;
}
bool ParseEffect(JsonReader& r, StatusEffectState* v, bool* tr) {
  unsigned seen=0; return Fields(r,[&](const char* k){
    if(Equal(k,"type")){seen|=1;return r.String(&v->type,tr);} double* n=nullptr; unsigned bit=0;
    if(Equal(k,"amount")){n=&v->amount;bit=2;}else if(Equal(k,"start")){n=&v->start;bit=4;}else if(Equal(k,"finish")){n=&v->finish;bit=8;}else if(Equal(k,"last_update")){n=&v->last_update;bit=32;}
    if(n){seen|=bit;return r.Number(n);}if(Equal(k,"stacks")){seen|=16;return r.Integer(&v->stacks);}return r.Skip();})&&seen==63;
}
bool ParseProgression(JsonReader& r, ProgressionTopic* v, bool* tr) {
  unsigned seen=0; return Fields(r,[&](const char* k){
    if(Equal(k,"skills")){seen|=1;std::size_t count=0;bool ok=r.Array([&](std::size_t i){if(i>=kMaxSkills)return false;return ParseSkill(r,&v->skills[i],tr);},&count);v->skill_count=static_cast<std::uint32_t>(count);return ok;}
    if(Equal(k,"perks")){seen|=2;std::size_t count=0;bool ok=r.Array([&](std::size_t i){if(i>=kMaxPerks)return false;return ParsePerk(r,&v->perks[i],tr);},&count);v->perk_count=static_cast<std::uint32_t>(count);return ok;}
    if(Equal(k,"status_effects")){seen|=4;std::size_t active=0;bool ok=r.Array([&](std::size_t i){if(r.Null())return true;if(active>=kMaxStatusEffects)return false;auto& e=v->status_effects[active++];e.source_index=static_cast<std::uint32_t>(i);return ParseEffect(r,&e,tr);});v->status_effect_count=static_cast<std::uint32_t>(active);return ok;}
    return r.Skip();})&&seen==7;
}

bool ParseMetadata(JsonReader& r, ItemMetadata* v, bool* tr) {
  unsigned seen=0; return Fields(r,[&](const char* k){
    if(Equal(k,"item_id")){seen|=1;return r.String(&v->item_id,tr);} if(Equal(k,"inner_item")){seen|=2;return OptionalString(r,&v->inner_item,tr);} if(Equal(k,"gold_to_gain")){seen|=4;return OptionalNum(r,&v->gold_to_gain);} if(Equal(k,"cosmetic")){seen|=8;return OptionalString(r,&v->cosmetic,tr);} if(Equal(k,"infusion")){seen|=16;return OptionalString(r,&v->infusion,tr);} if(Equal(k,"auto_use")){seen|=32;return r.Boolean(&v->auto_use);} if(Equal(k,"pet_cosmetic_set_name")){seen|=64;return OptionalString(r,&v->pet_cosmetic_set_name,tr);} if(Equal(k,"date_photo")){seen|=128;return OptionalInt(r,&v->date_photo);}
    if(Equal(k,"animal_cosmetic")){seen|=256;if(r.Null()){v->animal_cosmetic_present=false;return true;}v->animal_cosmetic_present=true;unsigned inner=0;return Fields(r,[&](const char* a){if(Equal(a,"animal")){inner|=1;return r.String(&v->animal_kind,tr);}if(Equal(a,"cosmetic")){inner|=2;return r.String(&v->animal_cosmetic,tr);}return r.Skip();})&&inner==3;}
    return r.Skip();})&&seen==511;
}

template <std::size_t N>
bool ParseStringArray(JsonReader& r, FixedUtf8<96> (&values)[N],
                      std::uint32_t* count, bool* tr) {
  std::size_t parsed = 0;
  const bool ok = r.Array([&](std::size_t i) {
    return i < N && r.String(&values[i], tr);
  }, &parsed);
  *count = static_cast<std::uint32_t>(parsed);
  return ok;
}

bool ParseIconLut(JsonReader& r, ItemSlotState* v, bool* tr) {
  if (r.Null()) {
    v->icon_lut_present = false;
    return true;
  }
  v->icon_lut_present = true;
  unsigned seen = 0;
  return Fields(r, [&](const char* k) {
    if (Equal(k, "asset")) {
      seen |= 1;
      return r.String(&v->icon_lut_asset, tr);
    }
    if (Equal(k, "index")) {
      seen |= 2;
      return r.Integer(&v->icon_lut_index);
    }
    return r.Skip();
  }) && seen == 3;
}

bool ParseSlot(JsonReader& r, ItemSlotState* v, bool* tr) {
  *v = {};
  v->index = -1;
  std::uint64_t seen = 0;
  const bool ok = Fields(r, [&](const char* k) {
    if (Equal(k,"index")){seen|=1ULL<<0;return r.Integer(&v->index);}
    if (Equal(k,"empty")){seen|=1ULL<<1;return r.Boolean(&v->empty);}
    if (Equal(k,"count")){seen|=1ULL<<2;return r.Number(&v->count);}
    if (Equal(k,"equipped")){seen|=1ULL<<3;return r.Boolean(&v->equipped);}
    if (Equal(k,"required_tags")){seen|=1ULL<<4;return ParseStringArray(r,v->required_tags,&v->required_tag_count,tr);}
    if (Equal(k,"item_id")){seen|=1ULL<<5;return r.String(&v->item_id,tr);}
    if (Equal(k,"display_name")){seen|=1ULL<<6;return r.String(&v->display_name,tr);}
    if (Equal(k,"description")){seen|=1ULL<<7;return r.String(&v->description,tr);}
    if (Equal(k,"instance_metadata")){seen|=1ULL<<8;return ParseMetadata(r,&v->instance_metadata,tr);}
    if (Equal(k,"instance_fingerprint")){seen|=1ULL<<9;return r.String(&v->instance_fingerprint,tr);}
    if (Equal(k,"use")){seen|=1ULL<<10;return OptionalInt(r,&v->use);}
    if (Equal(k,"use_name")){seen|=1ULL<<11;return OptionalString(r,&v->use_name,tr);}
    if (Equal(k,"can_consume_from_bridge")){seen|=1ULL<<12;return r.Boolean(&v->can_consume_from_bridge);}
    if (Equal(k,"ui_category")){seen|=1ULL<<13;return r.String(&v->ui_category,tr);}
    if (Equal(k,"tags")){seen|=1ULL<<14;return ParseStringArray(r,v->tags,&v->tag_count,tr);}
    if (Equal(k,"icon_asset")){seen|=1ULL<<15;return OptionalString(r,&v->icon_asset,tr);}
    if (Equal(k,"icon_frame")){seen|=1ULL<<16;return r.Integer(&v->icon_frame);}
    if (Equal(k,"sub_icon_asset")){seen|=1ULL<<17;return OptionalString(r,&v->sub_icon_asset,tr);}
    if (Equal(k,"icon_lut")){seen|=1ULL<<18;return ParseIconLut(r,v,tr);}
    if (Equal(k,"museum_eligible")){seen|=1ULL<<19;return r.Boolean(&v->museum_eligible);}
    if (Equal(k,"museum_donated")){seen|=1ULL<<20;return r.Boolean(&v->museum_donated);}
    OptionalNumber* n=nullptr; unsigned bit=0;
    if(Equal(k,"max_stack")){n=&v->max_stack;bit=21;}else if(Equal(k,"damage")){n=&v->damage;bit=22;}else if(Equal(k,"defense")){n=&v->defense;bit=23;}else if(Equal(k,"stamina_modifier")){n=&v->stamina_modifier;bit=24;}else if(Equal(k,"max_stamina_modifier")){n=&v->max_stamina_modifier;bit=25;}else if(Equal(k,"health_modifier")){n=&v->health_modifier;bit=26;}else if(Equal(k,"max_health_modifier")){n=&v->max_health_modifier;bit=27;}else if(Equal(k,"mana_modifier")){n=&v->mana_modifier;bit=28;}else if(Equal(k,"quality")){n=&v->quality;bit=29;}else if(Equal(k,"stars")){n=&v->stars;bit=30;}else if(Equal(k,"value_bin")){n=&v->value_bin;bit=31;}else if(Equal(k,"value_store")){n=&v->value_store;bit=32;}
    if(n){seen|=1ULL<<bit;return OptionalNum(r,n);}
    return r.Skip();
  });
  constexpr std::uint64_t kEmptyFields = (1ULL << 5) - 1;
  constexpr std::uint64_t kAllFields = (1ULL << 33) - 1;
  if (!ok || v->index < 0 || (seen & kEmptyFields) != kEmptyFields) return false;
  if (v->empty) return seen == kEmptyFields && v->count == 0.0;
  return seen == kAllFields && v->count > 0.0 &&
      v->item_id.value[0] && v->instance_fingerprint.value[0] &&
      !v->instance_fingerprint.truncated &&
      (!v->museum_donated || v->museum_eligible);
}

template <std::size_t N>
bool ParseInventory(JsonReader& r, InventoryTopic<N>* v, bool* tr) {
  *v = {};
  unsigned seen=0; std::int64_t declared=0;
  bool ok=Fields(r,[&](const char* k){if(Equal(k,"size")){seen|=1;return r.Integer(&declared);}if(Equal(k,"slots")){seen|=2;std::size_t count=0;bool a=r.Array([&](std::size_t i){if(i>=N)return false;bool parsed=ParseSlot(r,&v->slots[i],tr);return parsed&&v->slots[i].index==static_cast<std::int64_t>(i);},&count);v->slot_count=static_cast<std::uint32_t>(count);return a;}return r.Skip();});
  if (!ok || seen != 3 || declared < 0 ||
      declared > static_cast<std::int64_t>(N) ||
      declared != v->slot_count) return false;
  v->declared_size = static_cast<std::uint32_t>(declared);
  return true;
}

bool ParseHotbar(JsonReader& r, HotbarTopic* v){bool seen=false;return Fields(r,[&](const char* k){if(Equal(k,"selected_index")){seen=true;return r.Integer(&v->selected_index);}return r.Skip();})&&seen;}
bool ParseLocation(JsonReader& r,LocationTopic* v,bool* tr){unsigned s=0;return Fields(r,[&](const char*k){if(Equal(k,"id")){s|=1;return r.String(&v->id,tr);}if(Equal(k,"dynamic_index")){s|=2;return r.Integer(&v->dynamic_index);}if(Equal(k,"room")){s|=4;return r.String(&v->room,tr);}if(Equal(k,"diagnostic_name")){s|=8;return r.String(&v->diagnostic_name,tr);}if(Equal(k,"display_name")){s|=16;return r.String(&v->display_name,tr);}if(Equal(k,"outdoor")){s|=32;return r.Boolean(&v->outdoor);}return r.Skip();})&&s==63;}

bool ParseLivePosition(JsonReader& r, LivePosition* v, bool* tr) {
  if (r.Null()) { *v = {}; return true; }
  v->present = true;
  unsigned seen = 0;
  return Fields(r, [&](const char* k) {
    if (Equal(k, "id")) { seen |= 1; return r.String(&v->id, tr); }
    if (Equal(k, "dynamic_index")) {
      seen |= 2; return OptionalInt(r, &v->dynamic_index);
    }
    if (Equal(k, "x")) { seen |= 4; return OptionalNum(r, &v->x); }
    if (Equal(k, "y")) { seen |= 8; return OptionalNum(r, &v->y); }
    return r.Skip();
  }) && seen == 15;
}

bool ParseMine(JsonReader& r, MineState* v, bool* tr) {
  unsigned seen = 0;
  const bool ok = Fields(r, [&](const char* k) {
    if (Equal(k, "active")) { seen |= 1; return r.Boolean(&v->active); }
    if (Equal(k, "floor_index")) {
      seen |= 2; return OptionalInt(r, &v->floor_index);
    }
    if (Equal(k, "display_floor")) {
      seen |= 4; return OptionalInt(r, &v->display_floor);
    }
    if (Equal(k, "biome_id")) {
      seen |= 8; return OptionalInt(r, &v->biome_id);
    }
    if (Equal(k, "biome_name")) {
      seen |= 16; return OptionalString(r, &v->biome_name, tr);
    }
    return r.Skip();
  });
  if (!ok || seen != 31) return false;
  const bool all_details = v->floor_index.present && v->display_floor.present &&
      v->biome_id.present && v->biome_name.present;
  const bool no_details = !v->floor_index.present && !v->display_floor.present &&
      !v->biome_id.present && !v->biome_name.present;
  return v->active ? all_details : no_details;
}

bool ParsePlayer(JsonReader& r, PlayerTopic* v, bool* tr) {
  unsigned seen = 0;
  const bool ok = Fields(r, [&](const char* k) {
    if (Equal(k, "x")) { seen |= 1; return r.Number(&v->x); }
    if (Equal(k, "y")) { seen |= 2; return r.Number(&v->y); }
    if (Equal(k, "facing_degrees")) {
      seen |= 4; return r.Number(&v->facing_degrees);
    }
    if (Equal(k, "facing")) { seen |= 8; return r.String(&v->facing, tr); }
    if (Equal(k, "context")) { seen |= 16; return r.String(&v->context, tr); }
    if (Equal(k, "mine")) { seen |= 32; return ParseMine(r, &v->mine, tr); }
    return r.Skip();
  });
  if (!ok || seen != 63) return false;
  return v->mine.active ? Equal(v->context.value, "mines")
                        : Equal(v->context.value, "world");
}

bool ParseNumberPair(JsonReader& r, double (&values)[2]) {
  std::size_t count = 0;
  return r.Array([&](std::size_t i) {
    return i < 2 && r.Number(&values[i]);
  }, &count) && count == 2;
}

bool ParseRgb(JsonReader& r, std::int64_t (&values)[3]) {
  std::size_t count = 0;
  const bool ok = r.Array([&](std::size_t i) {
    return i < 3 && r.Integer(&values[i]);
  }, &count);
  if (!ok || count != 3) return false;
  return std::all_of(std::begin(values), std::end(values),
                     [](std::int64_t value) {
                       return value >= 0 && value <= 255;
                     });
}

bool ParseMapMarker(JsonReader& r, MapMarkerState* v, bool* tr,
                    std::int64_t expected_hub_index) {
  unsigned seen = 0;
  const bool ok = Fields(r, [&](const char* k) {
    if (Equal(k, "kind")) { seen |= 1; return r.String(&v->kind, tr); }
    if (Equal(k, "npc_id")) {
      seen |= 2; return OptionalString(r, &v->npc_id, tr);
    }
    if (Equal(k, "icon_asset")) {
      seen |= 4; return r.String(&v->icon_asset, tr);
    }
    if (Equal(k, "hub_index")) {
      seen |= 8; return r.Integer(&v->hub_index);
    }
    if (Equal(k, "offset_x")) {
      seen |= 16; return r.Number(&v->offset_x);
    }
    if (Equal(k, "offset_y")) {
      seen |= 32; return r.Number(&v->offset_y);
    }
    return r.Skip();
  });
  if (!ok || seen != 63 || v->hub_index != expected_hub_index) return false;
  if (Equal(v->kind.value, "npc")) return v->npc_id.present;
  if (Equal(v->kind.value, "unknown_npc") ||
      Equal(v->kind.value, "player")) return !v->npc_id.present;
  return false;
}

bool ParseMapHub(JsonReader& r, MapTopic* owner, MapHubState* v,
                 bool* tr, std::size_t local_index) {
  unsigned seen = 0;
  v->first_location = owner->hub_location_count;
  v->first_marker = owner->marker_count;
  const bool ok = Fields(r, [&](const char* k) {
    if (Equal(k, "id")) { seen |= 1; return r.String(&v->id, tr); }
    if (Equal(k, "index")) { seen |= 2; return r.Integer(&v->index); }
    if (Equal(k, "type")) { seen |= 4; return r.String(&v->type, tr); }
    if (Equal(k, "world_x")) { seen |= 8; return r.Number(&v->world_x); }
    if (Equal(k, "world_y")) { seen |= 16; return r.Number(&v->world_y); }
    if (Equal(k, "map_x")) { seen |= 32; return r.Number(&v->map_x); }
    if (Equal(k, "map_y")) { seen |= 64; return r.Number(&v->map_y); }
    if (Equal(k, "locations")) {
      seen |= 128;
      std::size_t count = 0;
      const bool valid = r.Array([&](std::size_t) {
        if (owner->hub_location_count >= kMaxMapHubLocations) return false;
        return r.String(&owner->hub_locations[owner->hub_location_count++], tr);
      }, &count);
      v->location_count = static_cast<std::uint32_t>(count);
      return valid;
    }
    if (Equal(k, "markers")) {
      seen |= 256;
      std::size_t count = 0;
      const bool valid = r.Array([&](std::size_t) {
        if (owner->marker_count >= kMaxMapMarkers) return false;
        return ParseMapMarker(r, &owner->markers[owner->marker_count++], tr,
                              static_cast<std::int64_t>(local_index));
      }, &count);
      v->marker_count = static_cast<std::uint32_t>(count);
      return valid;
    }
    return r.Skip();
  });
  return ok && seen == 511 &&
      v->index == static_cast<std::int64_t>(local_index) &&
      (Equal(v->type.value, "position") || Equal(v->type.value, "locations"));
}

bool ParseMapPage(JsonReader& r, MapTopic* owner, MapPageState* v, bool* tr) {
  unsigned seen = 0;
  v->first_hub = owner->hub_count;
  const bool ok = Fields(r, [&](const char* k) {
    if (Equal(k, "id")) { seen |= 1; return r.String(&v->id, tr); }
    if (Equal(k, "display_name")) {
      seen |= 2; return r.String(&v->display_name, tr);
    }
    if (Equal(k, "locked")) { seen |= 4; return r.Boolean(&v->locked); }
    if (Equal(k, "map_asset")) {
      seen |= 8; return r.String(&v->map_asset, tr);
    }
    if (Equal(k, "tab_icon_asset")) {
      seen |= 16; return r.String(&v->tab_icon_asset, tr);
    }
    if (Equal(k, "hubs")) {
      seen |= 32;
      std::size_t count = 0;
      const bool valid = r.Array([&](std::size_t local_index) {
        if (owner->hub_count >= kMaxMapHubs) return false;
        return ParseMapHub(r, owner, &owner->hubs[owner->hub_count++], tr,
                           local_index);
      }, &count);
      v->hub_count = static_cast<std::uint32_t>(count);
      return valid;
    }
    if (Equal(k, "player_hub_id")) {
      seen |= 64; return OptionalString(r, &v->player_hub_id, tr);
    }
    return r.Skip();
  });
  if (!ok || seen != 127) return false;
  return !v->locked || (v->hub_count == 0 && !v->player_hub_id.present);
}

bool ParseMap(JsonReader& r, MapTopic* v, bool* tr) {
  *v = {};
  unsigned seen = 0;
  const bool ok = Fields(r, [&](const char* k) {
    if (Equal(k, "selected_map_id")) {
      seen |= 1; return r.String(&v->selected_map_id, tr);
    }
    if (Equal(k, "base_map_id")) {
      seen |= 2; return r.String(&v->base_map_id, tr);
    }
    if (Equal(k, "pages")) {
      seen |= 4;
      std::size_t count = 0;
      const bool valid = r.Array([&](std::size_t i) {
        return i < kMaxMapPages && ParseMapPage(r, v, &v->pages[i], tr);
      }, &count);
      v->page_count = static_cast<std::uint32_t>(count);
      return valid;
    }
    if (Equal(k, "authored_coordinates")) {
      seen |= 8; return r.Boolean(&v->authored_coordinates);
    }
    if (Equal(k, "continuous_world_projection_supported")) {
      seen |= 16;
      return r.Boolean(&v->continuous_world_projection_supported);
    }
    return r.Skip();
  });
  return ok && seen == 31 && v->page_count > 0 &&
      v->authored_coordinates && !v->continuous_world_projection_supported;
}

bool ParseVillager(JsonReader& r, VillagerState* v, bool* tr) {
  unsigned seen = 0;
  const bool ok = Fields(r, [&](const char* k) {
    if (Equal(k, "id")) { seen |= 1; return r.String(&v->id, tr); }
    if (Equal(k, "display_name")) {
      seen |= 2; return r.String(&v->display_name, tr);
    }
    if (Equal(k, "icon_asset")) {
      seen |= 4; return r.String(&v->icon_asset, tr);
    }
    if (Equal(k, "portrait_asset")) {
      seen |= 8; return OptionalString(r, &v->portrait_asset, tr);
    }
    if (Equal(k, "portrait_offset")) {
      seen |= 16; return ParseNumberPair(r, v->portrait_offset);
    }
    if (Equal(k, "portrait_background_rgb")) {
      seen |= 32; return ParseRgb(r, v->portrait_background_rgb);
    }
    if (Equal(k, "heart_points")) {
      seen |= 64; return r.Number(&v->heart_points);
    }
    if (Equal(k, "heart_level")) {
      seen |= 128; return r.Integer(&v->heart_level);
    }
    if (Equal(k, "relationship")) {
      seen |= 256; return r.String(&v->relationship, tr);
    }
    if (Equal(k, "talked_today")) {
      seen |= 512; return r.Boolean(&v->talked_today);
    }
    if (Equal(k, "gifted_today")) {
      seen |= 1024; return r.Boolean(&v->gifted_today);
    }
    if (Equal(k, "location")) {
      seen |= 2048; return ParseLivePosition(r, &v->location, tr);
    }
    return r.Skip();
  });
  if (!ok || seen != 4095) return false;
  return Equal(v->relationship.value, "single") ||
      Equal(v->relationship.value, "spouse") ||
      Equal(v->relationship.value, "fiance") ||
      Equal(v->relationship.value, "dating") ||
      Equal(v->relationship.value, "best_friend");
}

bool ParseVillagers(JsonReader& r, VillagersTopic* v, bool* tr) {
  unsigned seen = 0;
  std::int64_t count = 0;
  const bool ok = Fields(r, [&](const char* k) {
    if (Equal(k, "villagers")) {
      seen |= 1; std::size_t parsed = 0;
      const bool valid = r.Array([&](std::size_t i) {
        if (i >= kMaxVillagers) return false;
        return ParseVillager(r, &v->villagers[i], tr);
      }, &parsed);
      v->villager_count = static_cast<std::uint32_t>(parsed);
      return valid;
    }
    if (Equal(k, "count")) { seen |= 2; return r.Integer(&count); }
    if (Equal(k, "discovery_policy")) {
      seen |= 4; return r.String(&v->discovery_policy, tr);
    }
    if (Equal(k, "gift_preferences_exposed")) {
      seen |= 8; return r.Boolean(&v->gift_preferences_exposed);
    }
    return r.Skip();
  });
  if (!ok || seen != 15 || count < 0 ||
      count != static_cast<std::int64_t>(v->villager_count)) return false;
  v->declared_count = static_cast<std::uint32_t>(count);
  return Equal(v->discovery_policy.value, "unlocked_and_met_only") &&
      !v->gift_preferences_exposed;
}

bool ParseAttention(JsonReader& r, AnimalAttention* v) {
  unsigned seen = 0;
  const bool ok = Fields(r, [&](const char* k) {
    if (Equal(k, "active")) { seen |= 1; return r.Boolean(&v->active); }
    if (Equal(k, "fed_today")) {
      seen |= 2; return OptionalBool(r, &v->fed_today);
    }
    if (Equal(k, "petted_today")) {
      seen |= 4; return OptionalBool(r, &v->petted_today);
    }
    if (Equal(k, "needs_food")) {
      seen |= 8; return r.Boolean(&v->needs_food);
    }
    if (Equal(k, "needs_pet")) {
      seen |= 16; return r.Boolean(&v->needs_pet);
    }
    if (Equal(k, "needed")) { seen |= 32; return r.Boolean(&v->needed); }
    return r.Skip();
  });
  if (!ok || seen != 63) return false;
  if (!v->active)
    return !v->fed_today.present && !v->petted_today.present &&
        !v->needs_food && !v->needs_pet && !v->needed;
  return v->fed_today.present && v->petted_today.present &&
      v->needs_food == !v->fed_today.value &&
      v->needs_pet == !v->petted_today.value &&
      v->needed == (v->needs_food || v->needs_pet);
}

bool ParseLivestock(JsonReader& r, LivestockState* v, bool* tr) {
  std::uint32_t seen = 0;
  const bool ok = Fields(r, [&](const char* k) {
    if (Equal(k, "id")) { seen |= 1; return r.Integer(&v->id); }
    if (Equal(k, "category")) { seen |= 2; return r.String(&v->category,tr); }
    if (Equal(k, "name")) { seen |= 4; return r.String(&v->name,tr); }
    if (Equal(k, "kind")) { seen |= 8; return r.String(&v->kind,tr); }
    if (Equal(k, "kind_name")) { seen |= 16; return r.String(&v->kind_name,tr); }
    if (Equal(k, "variant")) { seen |= 32; return r.String(&v->variant,tr); }
    if (Equal(k, "icon_asset")) { seen |= 64; return OptionalString(r,&v->icon_asset,tr); }
    if (Equal(k, "icon_lut_asset")) { seen |= 128; return OptionalString(r,&v->icon_lut_asset,tr); }
    if (Equal(k, "icon_lut_index")) { seen |= 256; return OptionalInt(r,&v->icon_lut_index); }
    if (Equal(k, "sex")) { seen |= 512; return r.String(&v->sex,tr); }
    if (Equal(k, "baby")) { seen |= 1024; return r.Boolean(&v->baby); }
    if (Equal(k, "days_old")) { seen |= 2048; return r.Integer(&v->days_old); }
    if (Equal(k, "heart_points")) { seen |= 4096; return r.Number(&v->heart_points); }
    if (Equal(k, "heart_level")) { seen |= 8192; return r.Integer(&v->heart_level); }
    if (Equal(k, "in_daycare")) { seen |= 16384; return r.Boolean(&v->in_daycare); }
    if (Equal(k, "at_home")) { seen |= 32768; return r.Boolean(&v->at_home); }
    if (Equal(k, "outside_today")) { seen |= 65536; return r.Boolean(&v->outside_today); }
    if (Equal(k, "incubating")) { seen |= 131072; return r.Boolean(&v->incubating); }
    if (Equal(k, "location")) { seen |= 262144; return ParseLivePosition(r,&v->location,tr); }
    if (Equal(k, "attention")) { seen |= 524288; return ParseAttention(r,&v->attention); }
    return r.Skip();
  });
  if (!ok || seen != 1048575 || !Equal(v->category.value, "livestock"))
    return false;
  return v->in_daycare ? (!v->location.present && !v->attention.active)
                       : v->attention.active;
}

bool ParsePet(JsonReader& r, PetState* v, bool* tr) {
  if (r.Null()) { *v = {}; return true; }
  v->present = true;
  unsigned seen = 0;
  const bool ok = Fields(r, [&](const char* k) {
    if (Equal(k, "id")) { seen |= 1; return r.Integer(&v->id); }
    if (Equal(k, "category")) { seen |= 2; return r.String(&v->category,tr); }
    if (Equal(k, "name")) { seen |= 4; return r.String(&v->name,tr); }
    if (Equal(k, "variant")) { seen |= 8; return r.String(&v->variant,tr); }
    if (Equal(k, "icon_asset")) { seen |= 16; return OptionalString(r,&v->icon_asset,tr); }
    if (Equal(k, "map_icon_asset")) { seen |= 32; return OptionalString(r,&v->map_icon_asset,tr); }
    if (Equal(k, "icon_lut_asset")) { seen |= 64; return OptionalString(r,&v->icon_lut_asset,tr); }
    if (Equal(k, "icon_lut_index")) { seen |= 128; return OptionalInt(r,&v->icon_lut_index); }
    if (Equal(k, "kind_id")) { seen |= 256; return r.Integer(&v->kind_id); }
    if (Equal(k, "sex")) { seen |= 512; return r.String(&v->sex,tr); }
    if (Equal(k, "heart_points")) { seen |= 1024; return r.Number(&v->heart_points); }
    if (Equal(k, "heart_level")) { seen |= 2048; return r.Integer(&v->heart_level); }
    if (Equal(k, "location")) { seen |= 4096; return ParseLivePosition(r,&v->location,tr); }
    if (Equal(k, "attention")) { seen |= 8192; return ParseAttention(r,&v->attention); }
    return r.Skip();
  });
  return ok && seen == 16383 && Equal(v->category.value, "pet") &&
      v->location.present && v->attention.active;
}

bool ParseAnimals(JsonReader& r, AnimalsTopic* v, bool* tr) {
  unsigned seen = 0; std::int64_t declared = 0;
  const bool ok = Fields(r, [&](const char* k) {
    if (Equal(k, "livestock")) {
      seen |= 1; std::size_t parsed = 0;
      const bool valid = r.Array([&](std::size_t i) {
        if (i >= kMaxLivestock) return false;
        return ParseLivestock(r, &v->livestock[i], tr);
      }, &parsed);
      v->livestock_count = static_cast<std::uint32_t>(parsed);
      return valid;
    }
    if (Equal(k, "livestock_count")) {
      seen |= 2; return r.Integer(&declared);
    }
    if (Equal(k, "pet")) { seen |= 4; return ParsePet(r, &v->pet, tr); }
    return r.Skip();
  });
  if (!ok || seen != 7 || declared < 0 ||
      declared != static_cast<std::int64_t>(v->livestock_count)) return false;
  v->declared_livestock_count = static_cast<std::uint32_t>(declared);
  return true;
}

bool ParseSpell(JsonReader& r, SpellState* v, bool* tr) {
  unsigned s=0; return Fields(r,[&](const char*k){
    if(Equal(k,"index")){s|=1;return r.Integer(&v->index);}if(Equal(k,"id")){s|=2;return r.String(&v->id,tr);}if(Equal(k,"display_name")){s|=4;return r.String(&v->display_name,tr);}if(Equal(k,"type")){s|=8;return r.String(&v->type,tr);}if(Equal(k,"description")){s|=16;return r.String(&v->description,tr);}if(Equal(k,"upper_icon_asset")){s|=32;return OptionalString(r,&v->upper_icon_asset,tr);}if(Equal(k,"lower_icon_key")){s|=64;return r.String(&v->lower_icon_key,tr);}if(Equal(k,"lower_icon_asset")){s|=128;return OptionalString(r,&v->lower_icon_asset,tr);}if(Equal(k,"ribbon_asset")){s|=256;return OptionalString(r,&v->ribbon_asset,tr);}if(Equal(k,"mana_cost")){s|=512;return r.Number(&v->mana_cost);}if(Equal(k,"display_cost_orbs")){s|=1024;return r.Integer(&v->display_cost_orbs);}return r.Skip();})&&s==2047&&v->index>=0&&v->index<static_cast<std::int64_t>(kMaxBridgeSpells);
}
bool ParseCooldown(JsonReader&r,SpellCooldownState*v,bool*tr){unsigned s=0;return Fields(r,[&](const char*k){if(Equal(k,"index")){s|=1;return r.Integer(&v->index);}if(Equal(k,"id")){s|=2;return r.String(&v->id,tr);}if(Equal(k,"remaining_frames")){s|=4;return r.Number(&v->remaining_frames);}if(Equal(k,"ready")){s|=8;return r.Boolean(&v->ready);}return r.Skip();})&&s==15&&v->index>=0&&v->index<static_cast<std::int64_t>(kMaxBridgeSpells);}
bool ParseMagic(JsonReader&r,MagicTopic*v,bool*tr){*v={};unsigned s=0;std::int64_t unlocked=0;bool ok=Fields(r,[&](const char*k){if(Equal(k,"spells")){s|=1;std::size_t n=0;bool valid=r.Array([&](std::size_t i){if(i>=kMaxSpells||!ParseSpell(r,&v->spells[i],tr))return false;for(std::size_t j=0;j<i;++j)if(v->spells[j].index==v->spells[i].index)return false;return true;},&n);v->spell_count=static_cast<std::uint32_t>(n);return valid;}if(Equal(k,"unlocked_count")){s|=2;return r.Integer(&unlocked);}if(Equal(k,"pinned_spell")){s|=4;return OptionalString(r,&v->pinned_spell,tr);}if(Equal(k,"pinned_spell_index")){s|=8;return OptionalInt(r,&v->pinned_spell_index);}if(Equal(k,"cooldowns")){s|=16;std::size_t n=0;bool valid=r.Array([&](std::size_t i){return i<kMaxSpellCooldowns&&ParseCooldown(r,&v->cooldowns[i],tr);},&n);v->cooldown_count=static_cast<std::uint32_t>(n);return valid;}if(Equal(k,"actions_supported")){s|=32;return r.Boolean(&v->actions_supported);}if(Equal(k,"supported_actions")){s|=64;std::size_t count=0;return r.Array([&](std::size_t){FixedUtf8<64> action;if(!r.String(&action,tr))return false;if(Equal(action.value,"pin_spell")){if(v->supports_pin_spell)return false;v->supports_pin_spell=true;}else if(Equal(action.value,"unpin_spell")){if(v->supports_unpin_spell)return false;v->supports_unpin_spell=true;}else if(Equal(action.value,"cast_pinned_spell")){if(v->supports_cast_pinned_spell)return false;v->supports_cast_pinned_spell=true;}else return false;++count;return count<=3;},&count)&&count==3;}return r.Skip();});if(!ok||s!=127||unlocked<0||unlocked!=v->spell_count||v->pinned_spell.present!=v->pinned_spell_index.present)return false;v->unlocked_count=static_cast<std::uint32_t>(unlocked);if(v->pinned_spell_index.present){bool matched=false;for(std::uint32_t i=0;i<v->spell_count;++i)if(v->spells[i].index==v->pinned_spell_index.value&&Equal(v->spells[i].id.value,v->pinned_spell.text.value))matched=true;if(!matched)return false;}return v->actions_supported&&v->supports_pin_spell&&v->supports_unpin_spell&&v->supports_cast_pinned_spell;}

bool ParseCollectionItem(JsonReader&r,CollectionItemState*v,bool*tr){unsigned s=0;return Fields(r,[&](const char*k){if(Equal(k,"id")){s|=1;return r.String(&v->id,tr);}if(Equal(k,"display_name")){s|=2;return r.String(&v->display_name,tr);}if(Equal(k,"description")){s|=4;return r.String(&v->description,tr);}if(Equal(k,"icon_asset")){s|=8;return OptionalString(r,&v->icon_asset,tr);}if(Equal(k,"icon_frame")){s|=16;return r.Integer(&v->icon_frame);}if(Equal(k,"donated")){s|=32;return r.Boolean(&v->donated);}return r.Skip();})&&s==63;}
bool ParseAlmanacCategory(JsonReader&r,AlmanacTopic*owner,AlmanacCategoryState*v,bool*tr){unsigned s=0;std::int64_t discovered=0,total=0;v->first_item=owner->item_count;bool ok=Fields(r,[&](const char*k){if(Equal(k,"key")){s|=1;return r.String(&v->key,tr);}if(Equal(k,"display_name")){s|=2;return r.String(&v->display_name,tr);}if(Equal(k,"discovered_count")){s|=4;return r.Integer(&discovered);}if(Equal(k,"total_count")){s|=8;return r.Integer(&total);}if(Equal(k,"discovered_items")){s|=16;std::size_t n=0;bool valid=r.Array([&](std::size_t){if(owner->item_count>=kMaxCollectionItems)return false;return ParseCollectionItem(r,&owner->discovered_items[owner->item_count++],tr);},&n);v->item_count=static_cast<std::uint32_t>(n);return valid;}return r.Skip();});if(!ok||s!=31||discovered<0||total<0||discovered!=v->item_count||discovered>total)return false;v->discovered_count=static_cast<std::uint32_t>(discovered);v->total_count=static_cast<std::uint32_t>(total);return true;}
bool ParseAlmanac(JsonReader&r,AlmanacTopic*v,bool*tr){unsigned s=0;std::int64_t discovered=0,total=0;bool ok=Fields(r,[&](const char*k){if(Equal(k,"categories")){s|=1;std::size_t n=0;bool valid=r.Array([&](std::size_t i){return i<kMaxAlmanacCategories&&ParseAlmanacCategory(r,v,&v->categories[i],tr);},&n);v->category_count=static_cast<std::uint32_t>(n);return valid;}if(Equal(k,"discovered_count")){s|=2;return r.Integer(&discovered);}if(Equal(k,"total_count")){s|=4;return r.Integer(&total);}if(Equal(k,"unknown_entries_exposed")){s|=8;return r.Boolean(&v->unknown_entries_exposed);}return r.Skip();});if(!ok||s!=15||discovered<0||total<0||discovered!=v->item_count||discovered>total||v->unknown_entries_exposed)return false;v->discovered_count=static_cast<std::uint32_t>(discovered);v->total_count=static_cast<std::uint32_t>(total);return true;}

bool ParseMuseumSet(JsonReader&r,MuseumTopic*owner,MuseumSetState*v,bool*tr){unsigned s=0;std::int64_t discovered=0,donated=0,total=0;v->first_item=owner->item_count;bool ok=Fields(r,[&](const char*k){if(Equal(k,"key")){s|=1;return r.String(&v->key,tr);}if(Equal(k,"display_name")){s|=2;return r.String(&v->display_name,tr);}if(Equal(k,"description")){s|=4;return r.String(&v->description,tr);}if(Equal(k,"discovered_count")){s|=8;return r.Integer(&discovered);}if(Equal(k,"donated_count")){s|=16;return r.Integer(&donated);}if(Equal(k,"total_count")){s|=32;return r.Integer(&total);}if(Equal(k,"complete")){s|=64;return r.Boolean(&v->complete);}if(Equal(k,"known_items")){s|=128;std::size_t n=0;bool valid=r.Array([&](std::size_t){if(owner->item_count>=kMaxCollectionItems)return false;return ParseCollectionItem(r,&owner->known_items[owner->item_count++],tr);},&n);v->item_count=static_cast<std::uint32_t>(n);return valid;}return r.Skip();});if(!ok||s!=255||discovered<0||donated<0||total<0||discovered!=v->item_count||donated>discovered||discovered>total||v->complete!=(total>0&&donated==total))return false;v->discovered_count=static_cast<std::uint32_t>(discovered);v->donated_count=static_cast<std::uint32_t>(donated);v->total_count=static_cast<std::uint32_t>(total);return true;}
bool ParseMuseumWing(JsonReader&r,MuseumTopic*owner,MuseumWingState*v,bool*tr){unsigned s=0;std::int64_t discovered=0,donated=0,total=0;v->first_set=owner->set_count;bool ok=Fields(r,[&](const char*k){if(Equal(k,"id")){s|=1;return r.String(&v->id,tr);}if(Equal(k,"display_name")){s|=2;return r.String(&v->display_name,tr);}if(Equal(k,"discovered_count")){s|=4;return r.Integer(&discovered);}if(Equal(k,"donated_count")){s|=8;return r.Integer(&donated);}if(Equal(k,"total_count")){s|=16;return r.Integer(&total);}if(Equal(k,"sets")){s|=32;std::size_t n=0;bool valid=r.Array([&](std::size_t){if(owner->set_count>=kMaxMuseumSets)return false;return ParseMuseumSet(r,owner,&owner->sets[owner->set_count++],tr);},&n);v->set_count=static_cast<std::uint32_t>(n);return valid;}return r.Skip();});if(!ok||s!=63||discovered<0||donated<0||total<0||donated>discovered||discovered>total)return false;v->discovered_count=static_cast<std::uint32_t>(discovered);v->donated_count=static_cast<std::uint32_t>(donated);v->total_count=static_cast<std::uint32_t>(total);return true;}
bool ParseMuseum(JsonReader&r,MuseumTopic*v,bool*tr){unsigned s=0;std::int64_t discovered=0,donated=0,total=0;bool ok=Fields(r,[&](const char*k){if(Equal(k,"wings")){s|=1;std::size_t n=0;bool valid=r.Array([&](std::size_t i){return i<kMaxMuseumWings&&ParseMuseumWing(r,v,&v->wings[i],tr);},&n);v->wing_count=static_cast<std::uint32_t>(n);return valid;}if(Equal(k,"discovered_count")){s|=2;return r.Integer(&discovered);}if(Equal(k,"donated_count")){s|=4;return r.Integer(&donated);}if(Equal(k,"total_count")){s|=8;return r.Integer(&total);}if(Equal(k,"unknown_entries_exposed")){s|=16;return r.Boolean(&v->unknown_entries_exposed);}if(Equal(k,"future_rewards_exposed")){s|=32;return r.Boolean(&v->future_rewards_exposed);}return r.Skip();});if(!ok||s!=63||discovered<0||donated<0||total<0||donated>discovered||discovered>total||v->unknown_entries_exposed||v->future_rewards_exposed)return false;v->discovered_count=static_cast<std::uint32_t>(discovered);v->donated_count=static_cast<std::uint32_t>(donated);v->total_count=static_cast<std::uint32_t>(total);return true;}

bool ParseRank(JsonReader&r,RenownRankState*v,bool*tr,bool include_level){unsigned s=0;bool ok=Fields(r,[&](const char*k){if(Equal(k,"rank_index")){s|=1;return r.Integer(&v->rank_index);}if(Equal(k,"level")){s|=2;return r.Integer(&v->level);}if(Equal(k,"display_name")){s|=4;return r.String(&v->display_name,tr);}return r.Skip();});return ok&&s==(include_level?7u:5u);}
bool ParseRenown(JsonReader&r,RenownTopic*v,bool*tr){std::uint32_t s=0;bool ok=Fields(r,[&](const char*k){double*n=nullptr;std::uint32_t b=0;if(Equal(k,"total")){n=&v->total;b=1;}else if(Equal(k,"pending")){n=&v->pending;b=2;}else if(Equal(k,"projected_total")){n=&v->projected_total;b=4;}else if(Equal(k,"level_start_total")){n=&v->level_start_total;b=128;}else if(Equal(k,"next_level_total")){n=&v->next_level_total;b=256;}else if(Equal(k,"progress")){n=&v->progress;b=512;}else if(Equal(k,"progress_required")){n=&v->progress_required;b=1024;}else if(Equal(k,"progress_ratio")){n=&v->progress_ratio;b=2048;}if(n){s|=b;return r.Number(n);}std::int64_t*i=nullptr;if(Equal(k,"projected_level")){i=&v->projected_level;b=8;}else if(Equal(k,"level")){i=&v->level;b=16;}else if(Equal(k,"max_level")){i=&v->max_level;b=32;}if(i){s|=b;return r.Integer(i);}if(Equal(k,"at_max")){s|=64;return r.Boolean(&v->at_max);}if(Equal(k,"rank")){s|=4096;return ParseRank(r,&v->rank,tr,false);}if(Equal(k,"next_rank")){s|=8192;if(r.Null()){v->next_rank_present=false;return true;}v->next_rank_present=true;return ParseRank(r,&v->next_rank,tr,true);}if(Equal(k,"claimable_rewards")){s|=16384;return ParseInventory(r,&v->claimable_rewards,tr);}if(Equal(k,"future_rewards_exposed")){s|=32768;return r.Boolean(&v->future_rewards_exposed);}if(Equal(k,"actions_supported")){s|=65536;return r.Boolean(&v->actions_supported);}return r.Skip();});return ok&&s==131071&&!v->future_rewards_exposed&&!v->actions_supported;}

bool ParseCalendarEvent(JsonReader&r,CalendarEventState*v,bool*tr){std::uint32_t s=0;return Fields(r,[&](const char*k){if(Equal(k,"type")){s|=1;return r.String(&v->type,tr);}if(Equal(k,"id")){s|=2;return r.String(&v->id,tr);}if(Equal(k,"display_name")){s|=4;return r.String(&v->display_name,tr);}if(Equal(k,"raw_date")){s|=16;return r.Number(&v->raw_date);}if(Equal(k,"season")){s|=128;return r.String(&v->season,tr);}std::int64_t*i=nullptr;std::uint32_t b=0;if(Equal(k,"days_from_now")){i=&v->days_from_now;b=8;}else if(Equal(k,"year")){i=&v->year;b=32;}else if(Equal(k,"display_year")){i=&v->display_year;b=64;}else if(Equal(k,"season_index")){i=&v->season_index;b=256;}else if(Equal(k,"day")){i=&v->day;b=512;}else if(Equal(k,"display_day")){i=&v->display_day;b=1024;}else if(Equal(k,"weekday")){i=&v->weekday;b=2048;}if(i){s|=b;return r.Integer(i);}return r.Skip();})&&s==4095;}
bool ParseCalendarEvents(JsonReader&r,CalendarEventsTopic*v,bool*tr){unsigned s=0;std::int64_t declared=0;bool ok=Fields(r,[&](const char*k){if(Equal(k,"horizon_days")){s|=1;return r.Integer(&v->horizon_days);}if(Equal(k,"events")){s|=2;std::size_t n=0;bool valid=r.Array([&](std::size_t i){return i<kMaxCalendarEvents&&ParseCalendarEvent(r,&v->events[i],tr);},&n);v->event_count=static_cast<std::uint32_t>(n);return valid;}if(Equal(k,"count")){s|=4;return r.Integer(&declared);}if(Equal(k,"private_date_availability_exposed")){s|=8;return r.Boolean(&v->private_date_availability_exposed);}if(Equal(k,"unreleased_festivals_exposed")){s|=16;return r.Boolean(&v->unreleased_festivals_exposed);}return r.Skip();});if(!ok||s!=31||v->horizon_days<0||declared<0||declared!=v->event_count||v->private_date_availability_exposed||v->unreleased_festivals_exposed)return false;v->declared_count=static_cast<std::uint32_t>(declared);return true;}

bool ParseCalendar(JsonReader&r,CalendarTopic*v,bool*tr){unsigned s=0;return Fields(r,[&](const char*k){if(Equal(k,"season")){s|=8;return r.String(&v->season,tr);}double*n=nullptr;unsigned b=0;if(Equal(k,"raw")){n=&v->raw;b=1;}else if(Equal(k,"total_days")){n=&v->total_days;b=256;}if(n){s|=b;return r.Number(n);}std::int64_t*i=nullptr;if(Equal(k,"year")){i=&v->year;b=2;}else if(Equal(k,"display_year")){i=&v->display_year;b=4;}else if(Equal(k,"season_index")){i=&v->season_index;b=16;}else if(Equal(k,"day")){i=&v->day;b=32;}else if(Equal(k,"display_day")){i=&v->display_day;b=64;}else if(Equal(k,"weekday")){i=&v->weekday;b=128;}if(i){s|=b;return r.Integer(i);}return r.Skip();})&&s==511;}
bool ParseClock(JsonReader&r,ClockTopic*v,bool*tr){unsigned s=0;return Fields(r,[&](const char*k){if(Equal(k,"raw")){s|=1;return r.Number(&v->raw);}if(Equal(k,"hour")){s|=2;return r.Integer(&v->hour);}if(Equal(k,"minute")){s|=4;return r.Integer(&v->minute);}if(Equal(k,"formatted")){s|=8;return r.String(&v->formatted,tr);}if(Equal(k,"is_day")){s|=16;return r.Boolean(&v->is_day);}if(Equal(k,"is_night")){s|=32;return r.Boolean(&v->is_night);}return r.Skip();})&&s==63;}
bool ParseWeather(JsonReader&r,WeatherTopic*v,bool*tr){unsigned s=0;return Fields(r,[&](const char*k){if(Equal(k,"current")){s|=1;return r.String(&v->current,tr);}if(Equal(k,"current_id")){s|=2;return r.Integer(&v->current_id);}if(Equal(k,"tomorrow")){s|=4;return r.String(&v->tomorrow,tr);}if(Equal(k,"tomorrow_id")){s|=8;return r.Integer(&v->tomorrow_id);}if(Equal(k,"inclement")){s|=16;return r.Boolean(&v->inclement);}if(Equal(k,"under_spell")){s|=32;return r.Boolean(&v->under_spell);}return r.Skip();})&&s==63;}
bool ParseQuestRequirement(JsonReader&r,QuestRequirementState*v,bool*tr){std::uint32_t s=0;const bool ok=Fields(r,[&](const char*k){if(Equal(k,"type")){s|=1;return r.String(&v->type,tr);}if(Equal(k,"source")){s|=2;return OptionalString(r,&v->source,tr);}if(Equal(k,"source_location")){s|=4;return ParseLivePosition(r,&v->source_location,tr);}if(Equal(k,"item_id")){s|=8;return OptionalString(r,&v->item_id,tr);}if(Equal(k,"tag")){s|=16;return OptionalString(r,&v->tag,tr);}if(Equal(k,"monster_id")){s|=32;return OptionalInt(r,&v->monster_id);}if(Equal(k,"display_name")){s|=64;return OptionalString(r,&v->display_name,tr);}if(Equal(k,"icon_asset")){s|=128;return OptionalString(r,&v->icon_asset,tr);}if(Equal(k,"required")){s|=256;return OptionalNum(r,&v->required);}if(Equal(k,"current")){s|=512;return OptionalNum(r,&v->current);}if(Equal(k,"complete")){s|=1024;return OptionalBool(r,&v->complete);}if(Equal(k,"identities_exposed")){s|=2048;return OptionalBool(r,&v->identities_exposed);}return r.Skip();});if(!ok)return false;constexpr std::uint32_t base=1|256|512|1024;constexpr std::uint32_t named=64|128;if(Equal(v->type.value,"item"))return s==(base|2|4|8|named)&&v->source.present&&v->item_id.present&&v->display_name.present&&v->required.present&&v->current.present&&v->complete.present;if(Equal(v->type.value,"gold"))return s==base&&v->required.present&&v->current.present&&v->complete.present;if(Equal(v->type.value,"shipped_tag"))return s==(base|16|named)&&v->tag.present&&v->display_name.present&&v->required.present&&v->current.present&&v->complete.present;if(Equal(v->type.value,"shipped_item"))return s==(base|8|named)&&v->item_id.present&&v->display_name.present&&v->required.present&&v->current.present&&v->complete.present;if(Equal(v->type.value,"defeated_monster"))return s==(base|32|named)&&v->monster_id.present&&v->display_name.present&&v->required.present&&v->current.present&&v->complete.present;if(Equal(v->type.value,"met_npcs"))return s==(base|2048)&&v->required.present&&v->current.present&&v->complete.present&&v->identities_exposed.present&&!v->identities_exposed.value;if(Equal(v->type.value,"gather_item"))return s==(base|8|named)&&v->item_id.present&&v->display_name.present&&!v->required.present&&v->current.present&&!v->complete.present;return false;}

bool ParseQuestTarget(JsonReader&r,QuestTargetState*v,bool*tr){unsigned s=0;const bool ok=Fields(r,[&](const char*k){if(Equal(k,"type")){s|=1;return r.String(&v->type,tr);}if(Equal(k,"npc_id")){s|=2;return OptionalString(r,&v->npc_id,tr);}if(Equal(k,"location_id")){s|=4;return OptionalString(r,&v->location_id,tr);}if(Equal(k,"location")){s|=8;return ParseLivePosition(r,&v->location,tr);}return r.Skip();});if(!ok)return false;if(Equal(v->type.value,"npc"))return s==(1|2|8)&&v->npc_id.present;if(Equal(v->type.value,"location"))return s==(1|4)&&v->location_id.present;if(Equal(v->type.value,"manual"))return s==1;return false;}

bool ParseQuestReward(JsonReader&r,QuestRewardState*v,bool*tr){unsigned s=0;const bool ok=Fields(r,[&](const char*k){if(Equal(k,"type")){s|=1;return r.String(&v->type,tr);}if(Equal(k,"item_id")){s|=2;return OptionalString(r,&v->item_id,tr);}if(Equal(k,"display_name")){s|=4;return OptionalString(r,&v->display_name,tr);}if(Equal(k,"icon_asset")){s|=8;return OptionalString(r,&v->icon_asset,tr);}if(Equal(k,"amount")){s|=16;return r.Number(&v->amount);}if(Equal(k,"instance_metadata")){s|=32;v->instance_metadata_present=true;return ParseMetadata(r,&v->instance_metadata,tr);}return r.Skip();});if(!ok)return false;if(Equal(v->type.value,"gold")||Equal(v->type.value,"renown"))return s==(1|16);const bool item_type=Equal(v->type.value,"item")||Equal(v->type.value,"recipe_scroll")||Equal(v->type.value,"crafting_scroll")||Equal(v->type.value,"animal_cosmetic")||Equal(v->type.value,"player_cosmetic")||Equal(v->type.value,"lookup_item");return item_type&&s==63&&v->item_id.present&&v->display_name.present&&v->instance_metadata_present;}

bool ParseQuestNpcIcon(JsonReader&r,QuestNpcIconState*v,bool*tr){unsigned s=0;return Fields(r,[&](const char*k){if(Equal(k,"id")){s|=1;return r.String(&v->id,tr);}if(Equal(k,"icon_asset")){s|=2;return r.String(&v->icon_asset,tr);}return r.Skip();})&&s==3;}

bool ParseQuest(JsonReader&r,QuestsTopic*owner,QuestState*v,bool*tr){std::uint32_t s=0;v->first_requirement=owner->requirement_count;v->first_target=owner->target_count;v->first_reward=owner->reward_count;const bool ok=Fields(r,[&](const char*k){if(Equal(k,"id")){s|=1;return r.String(&v->id,tr);}if(Equal(k,"name")){s|=2;return r.String(&v->name,tr);}if(Equal(k,"description")){s|=4;return r.String(&v->description,tr);}if(Equal(k,"category")){s|=8;return r.Integer(&v->category);}if(Equal(k,"current_stage")){s|=16;return r.Integer(&v->current_stage);}if(Equal(k,"stage_count")){s|=32;return r.Integer(&v->stage_count);}if(Equal(k,"objective")){s|=64;return r.String(&v->objective,tr);}if(Equal(k,"requirements")){s|=128;std::size_t n=0;const bool valid=r.Array([&](std::size_t){if(owner->requirement_count>=kMaxQuestRequirements)return false;return ParseQuestRequirement(r,&owner->requirements[owner->requirement_count++],tr);},&n);v->requirement_count=static_cast<std::uint32_t>(n);return valid;}if(Equal(k,"targets")){s|=256;std::size_t n=0;const bool valid=r.Array([&](std::size_t){if(owner->target_count>=kMaxQuestTargets)return false;return ParseQuestTarget(r,&owner->targets[owner->target_count++],tr);},&n);v->target_count=static_cast<std::uint32_t>(n);return valid;}if(Equal(k,"ready")){s|=512;return r.Boolean(&v->ready);}if(Equal(k,"npc_for_icon")){s|=1024;return ParseQuestNpcIcon(r,&v->npc_for_icon,tr);}if(Equal(k,"rewards")){s|=2048;std::size_t n=0;const bool valid=r.Array([&](std::size_t){if(owner->reward_count>=kMaxQuestRewards)return false;return ParseQuestReward(r,&owner->rewards[owner->reward_count++],tr);},&n);v->reward_count=static_cast<std::uint32_t>(n);return valid;}return r.Skip();});return ok&&s==4095&&v->current_stage>=0&&v->stage_count>=0&&v->current_stage<=v->stage_count;}

bool ParseQuests(JsonReader&r,QuestsTopic*v,bool*tr){*v={};unsigned s=0;std::int64_t ac=0,cc=0;bool ok=Fields(r,[&](const char*k){if(Equal(k,"active")){s|=1;std::size_t n=0;bool a=r.Array([&](std::size_t i){if(i>=kMaxQuests)return false;return ParseQuest(r,v,&v->active[i],tr);},&n);v->quest_count=static_cast<std::uint32_t>(n);return a;}if(Equal(k,"active_count")){s|=2;return r.Integer(&ac);}if(Equal(k,"completed_count")){s|=4;return r.Integer(&cc);}if(Equal(k,"tracked_supported")){s|=8;return r.Boolean(&v->tracked_supported);}return r.Skip();});if(!ok||s!=15||ac<0||cc<0||ac!=v->quest_count||v->tracked_supported)return false;v->active_count=static_cast<std::uint32_t>(ac);v->completed_count=static_cast<std::uint32_t>(cc);return true;}

int TopicIndex(const char* key){for(std::size_t i=0;i<kLiveTopicCount;++i)if(Equal(key,kTopicNames[i]))return static_cast<int>(i);return -1;}

struct Envelope {
  bool full=false;
  std::uint64_t base_sequence=0;
  std::uint64_t base_revisions[kLiveTopicCount]={};
  std::uint32_t base_revision_mask=0;
  std::uint32_t topic_mask=0;
  LiveStateSnapshot candidate;
};

bool ParseRevisions(JsonReader&r,std::uint64_t*out,std::uint32_t*mask){return Fields(r,[&](const char*k){int i=TopicIndex(k);if(i<0)return r.Skip();const std::uint32_t bit=1u<<i;if(*mask&bit)return false;*mask|=bit;return r.Unsigned(&out[i]);});}
bool ParseTopics(JsonReader&r,Envelope*e){return Fields(r,[&](const char*k){int i=TopicIndex(k);if(i<0)return r.Skip();std::uint32_t bit=1u<<i;if(e->topic_mask&bit)return false;e->topic_mask|=bit;bool*tr=&e->candidate.any_text_truncated;switch(static_cast<LiveTopic>(i)){case LiveTopic::kSession:return ParseSession(r,&e->candidate.session,tr);case LiveTopic::kStatus:return ParseStatus(r,&e->candidate.status,tr);case LiveTopic::kProgression:return ParseProgression(r,&e->candidate.progression,tr);case LiveTopic::kInventory:return ParseInventory(r,&e->candidate.inventory,tr);case LiveTopic::kArmor:return ParseInventory(r,&e->candidate.armor,tr);case LiveTopic::kHotbar:return ParseHotbar(r,&e->candidate.hotbar);case LiveTopic::kLocation:return ParseLocation(r,&e->candidate.location,tr);case LiveTopic::kPlayer:return ParsePlayer(r,&e->candidate.player,tr);case LiveTopic::kMap:return ParseMap(r,&e->candidate.map,tr);case LiveTopic::kVillagers:return ParseVillagers(r,&e->candidate.villagers,tr);case LiveTopic::kAnimals:return ParseAnimals(r,&e->candidate.animals,tr);case LiveTopic::kMagic:return ParseMagic(r,&e->candidate.magic,tr);case LiveTopic::kAlmanac:e->candidate.almanac.category_count=0;e->candidate.almanac.item_count=0;return ParseAlmanac(r,&e->candidate.almanac,tr);case LiveTopic::kMuseum:e->candidate.museum.wing_count=0;e->candidate.museum.set_count=0;e->candidate.museum.item_count=0;return ParseMuseum(r,&e->candidate.museum,tr);case LiveTopic::kRenown:return ParseRenown(r,&e->candidate.renown,tr);case LiveTopic::kCalendarEvents:return ParseCalendarEvents(r,&e->candidate.calendar_events,tr);case LiveTopic::kCalendar:return ParseCalendar(r,&e->candidate.calendar,tr);case LiveTopic::kClock:return ParseClock(r,&e->candidate.clock,tr);case LiveTopic::kWeather:return ParseWeather(r,&e->candidate.weather,tr);case LiveTopic::kQuests:return ParseQuests(r,&e->candidate.quests,tr);default:return false;}});}

bool ParseEnvelope(const char*data,std::size_t size,const LiveStateSnapshot&base,Envelope*out,std::size_t*error){if(!ValidUtf8(data,size))return false;out->full=false;out->base_sequence=0;std::fill(std::begin(out->base_revisions),std::end(out->base_revisions),0);out->base_revision_mask=0;out->topic_mask=0;out->candidate=base;JsonReader r(data,size);unsigned seen=0;FixedUtf8<64> protocol;std::uint64_t schema=0;std::uint32_t revmask=0;bool ok=Fields(r,[&](const char*k){if(Equal(k,"protocol")){seen|=1;return r.String(&protocol);}if(Equal(k,"schema")){seen|=2;return r.Unsigned(&schema);}if(Equal(k,"epoch")){seen|=4;return r.String(&out->candidate.epoch,&out->candidate.any_text_truncated);}if(Equal(k,"sequence")){seen|=8;return r.Unsigned(&out->candidate.sequence);}if(Equal(k,"full")){seen|=16;return r.Boolean(&out->full);}if(Equal(k,"base_sequence")){seen|=32;return r.Unsigned(&out->base_sequence);}if(Equal(k,"generated_tick")){seen|=64;return r.Number(&out->candidate.generated_tick);}if(Equal(k,"base_revisions")){seen|=128;return ParseRevisions(r,out->base_revisions,&out->base_revision_mask);}if(Equal(k,"revisions")){seen|=256;return ParseRevisions(r,out->candidate.revisions,&revmask);}if(Equal(k,"topics")){seen|=512;return ParseTopics(r,out);}return r.Skip();});
  if (error) *error = r.error_offset();
  if (!ok || !r.End() || seen != 1023 || !Equal(protocol.value, kProtocol) ||
      schema != kSchema || revmask != kAllTopics ||
      !out->candidate.epoch.value[0] || out->candidate.epoch.truncated ||
      !out->candidate.sequence ||
      out->candidate.sequence > kMaxExactJsonInteger ||
      out->base_sequence > kMaxExactJsonInteger) return false;
  for (std::size_t i = 0; i < kLiveTopicCount; ++i) {
    if (out->candidate.revisions[i] > kMaxExactJsonInteger ||
        out->base_revisions[i] > kMaxExactJsonInteger) return false;
  }
  out->candidate.schema = kSchema;
  return true;
}

bool ParseAck(const char* data, std::size_t size, const char* expected_epoch,
              BridgeActionAck* output, std::size_t* error) {
  if (!ValidUtf8(data, size)) return false;
  JsonReader r(data, size);
  FixedUtf8<64> protocol;
  FixedUtf8<128> epoch;
  FixedUtf8<64> action;
  FixedUtf8<96> code;
  std::uint64_t schema = 0, state_sequence = 0, command_sequence = 0;
  bool acknowledged = false;
  std::uint32_t seen = 0;
  auto mark = [&](std::uint32_t bit) {
    if (seen & bit) return false;
    seen |= bit;
    return true;
  };
  const bool ok = Fields(r, [&](const char* key) {
    if (Equal(key, "protocol")) return mark(1) && r.String(&protocol);
    if (Equal(key, "schema")) return mark(2) && r.Unsigned(&schema);
    if (Equal(key, "epoch")) return mark(4) && r.String(&epoch);
    if (Equal(key, "state_sequence"))
      return mark(8) && r.Unsigned(&state_sequence);
    if (Equal(key, "command_sequence"))
      return mark(16) && r.Unsigned(&command_sequence);
    if (Equal(key, "action")) return mark(32) && r.String(&action);
    if (Equal(key, "ok")) return mark(64) && r.Boolean(&acknowledged);
    if (Equal(key, "code")) return mark(128) && r.String(&code);
    return r.Skip();
  });
  if (error) *error = r.error_offset();
  if (!ok || !r.End() || seen != 255 || !Equal(protocol.value, kProtocol) ||
      schema != kSchema || !expected_epoch || !*expected_epoch ||
      !Equal(epoch.value, expected_epoch) || !state_sequence ||
      state_sequence > kMaxExactJsonInteger || !command_sequence ||
      command_sequence > kMaxExactJsonInteger || !action.value[0] ||
      !code.value[0] || action.truncated || code.truncated || epoch.truncated)
    return false;
  output->validated = true;
  output->protocol = protocol.value;
  output->schema = kSchema;
  output->epoch = epoch.value;
  output->state_sequence = state_sequence;
  output->command_sequence = command_sequence;
  output->action = action.value;
  output->ok = acknowledged;
  output->code = code.value;
  return true;
}

struct FileStamp { DWORD volume=0,high=0,low=0;std::uint64_t size=0;FILETIME write={};bool valid=false;};
bool SameStamp(const FileStamp&a,const FileStamp&b){return a.valid&&b.valid&&a.volume==b.volume&&a.high==b.high&&a.low==b.low&&a.size==b.size&&CompareFileTime(&a.write,&b.write)==0;}
enum class ReadResult {kOk,kMissing,kIo,kTooLarge};
ReadResult ReadStable(const std::wstring&path,std::vector<char>*buffer,FileStamp*stamp,std::size_t max_bytes=kMaxStateBytes){HANDLE h=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_SEQUENTIAL_SCAN,nullptr);if(h==INVALID_HANDLE_VALUE){DWORD e=GetLastError();return(e==ERROR_FILE_NOT_FOUND||e==ERROR_PATH_NOT_FOUND||e==ERROR_SHARING_VIOLATION)?ReadResult::kMissing:ReadResult::kIo;}BY_HANDLE_FILE_INFORMATION info={};LARGE_INTEGER sz={};if(!GetFileInformationByHandle(h,&info)||!GetFileSizeEx(h,&sz)||sz.QuadPart<=0||sz.QuadPart>static_cast<LONGLONG>(max_bytes)){CloseHandle(h);return ReadResult::kTooLarge;}buffer->resize(static_cast<std::size_t>(sz.QuadPart));std::size_t done=0;while(done<buffer->size()){DWORD got=0;DWORD ask=static_cast<DWORD>(std::min<std::size_t>(buffer->size()-done,1u<<20));if(!ReadFile(h,buffer->data()+done,ask,&got,nullptr)||!got){CloseHandle(h);return ReadResult::kIo;}done+=got;}LARGE_INTEGER after={};bool stable=GetFileSizeEx(h,&after)&&after.QuadPart==sz.QuadPart;CloseHandle(h);if(!stable)return ReadResult::kIo;stamp->volume=info.dwVolumeSerialNumber;stamp->high=info.nFileIndexHigh;stamp->low=info.nFileIndexLow;stamp->size=buffer->size();stamp->write=info.ftLastWriteTime;stamp->valid=true;return ReadResult::kOk;}

std::wstring Join(const std::wstring&a,const wchar_t*b){std::wstring r=a;if(!r.empty()&&r.back()!=L'\\'&&r.back()!=L'/')r+=L'\\';r+=b;return r;}
bool IsDirectory(const std::wstring&p){DWORD a=GetFileAttributesW(p.c_str());return a!=INVALID_FILE_ATTRIBUTES&&(a&FILE_ATTRIBUTE_DIRECTORY)&&!(a&FILE_ATTRIBUTE_REPARSE_POINT);}
bool FileExists(const std::wstring&p){DWORD a=GetFileAttributesW(p.c_str());return a!=INVALID_FILE_ATTRIBUTES&&!(a&FILE_ATTRIBUTE_DIRECTORY);}
std::wstring Env(const wchar_t*n){DWORD needed=GetEnvironmentVariableW(n,nullptr,0);if(!needed)return{};std::wstring v(needed,L'\0');DWORD got=GetEnvironmentVariableW(n,v.data(),needed);if(!got||got>=needed)return{};v.resize(got);return v;}

void FindStates(const std::wstring&dir,unsigned depth,std::size_t*budget,std::vector<std::wstring>*found){if(depth>5||!*budget||found->size()>=16)return;WIN32_FIND_DATAW fd={};HANDLE h=FindFirstFileW(Join(dir,L"*").c_str(),&fd);if(h==INVALID_HANDLE_VALUE)return;do{if(!*budget)break;--*budget;if(fd.cFileName[0]==L'.'&&(fd.cFileName[1]==0||(fd.cFileName[1]==L'.'&&fd.cFileName[2]==0)))continue;std::wstring p=Join(dir,fd.cFileName);if(fd.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)continue;if(fd.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY){if(_wcsicmp(fd.cFileName,L"dualscreen")==0){std::wstring state=Join(p,L"state.json");if(FileExists(state))found->push_back(std::move(state));}else FindStates(p,depth+1,budget,found);}}while(FindNextFileW(h,&fd));FindClose(h);}

}  // namespace

struct LiveStateIngestor::Impl {
  explicit Impl(const wchar_t* config) {
    InitializeSRWLock(&lock);
    if(config&&*config) state_path=Join(config,L"dualscreen\\state.json");
    buffer.reserve(256*1024);
  }
  SRWLOCK lock={};
  std::mutex io_mutex;
  std::wstring state_path;
  std::vector<char> buffer;
  LiveStateSnapshot snapshot={};
  std::unique_ptr<LiveStateSnapshot> base_scratch =
      std::make_unique<LiveStateSnapshot>();
  std::unique_ptr<Envelope> envelope = std::make_unique<Envelope>();
  bool available=false;
  bool needs_resync=false;
  FileStamp primary_observed={};
  FileStamp fallback_observed={};
  FileStamp ack_observed={};
  FileStamp ack_fallback_observed={};
  ULONGLONG next_discovery=0;

  bool Discover(){
    if(!state_path.empty())return true;
    auto select = [&](std::wstring path) {
      AcquireSRWLockExclusive(&lock);
      state_path = std::move(path);
      ReleaseSRWLockExclusive(&lock);
      return true;
    };
    std::wstring explicit_file=Env(L"FOMDS_STATE_FILE");if(!explicit_file.empty()&&FileExists(explicit_file))return select(std::move(explicit_file));
    std::wstring config=Env(L"FOMDS_CONFIG_DIRECTORY");if(!config.empty()){std::wstring p=Join(config,L"dualscreen\\state.json");if(FileExists(p))return select(std::move(p));}
    std::wstring root=Env(L"LOCALAPPDATA");if(root.empty()||!IsDirectory(root))return false;
    const wchar_t* candidates[]={L"FieldsOfMistria\\dualscreen\\state.json",L"Fields of Mistria\\dualscreen\\state.json"};for(auto*c:candidates){std::wstring p=Join(root,c);if(FileExists(p))return select(std::move(p));}
    std::vector<std::wstring> found;found.reserve(8);std::size_t budget=20000;FindStates(root,0,&budget,&found);if(found.empty())return false;
    // Prefer the most recently written bridge file. Every candidate still has
    // to pass protocol/schema validation before it can publish.
    std::sort(found.begin(),found.end(),[](const std::wstring&a,const std::wstring&b){WIN32_FILE_ATTRIBUTE_DATA x={},y={};GetFileAttributesExW(a.c_str(),GetFileExInfoStandard,&x);GetFileAttributesExW(b.c_str(),GetFileExInfoStandard,&y);return CompareFileTime(&x.ftLastWriteTime,&y.ftLastWriteTime)>0;});return select(std::move(found.front()));
  }

  LivePollResult PollPath(const std::wstring&path,bool fallback){
    FileStamp stamp={};ReadResult rr=ReadStable(path,&buffer,&stamp);if(rr==ReadResult::kMissing)return LivePollResult::kNoFile;if(rr!=ReadResult::kOk){LogRateLimited("live-state-read",5000,LogLevel::kWarning,"[STATE] read failed result=%u fallback=%d",static_cast<unsigned>(rr),fallback);return LivePollResult::kIoError;}FileStamp& observed=fallback?fallback_observed:primary_observed;if(SameStamp(stamp,observed))return LivePollResult::kNoChange;observed=stamp;
    AcquireSRWLockShared(&lock);if(available)*base_scratch=snapshot;bool had=available;ReleaseSRWLockShared(&lock);LiveStateSnapshot& base=*base_scratch;
    std::size_t error=0;if(!ParseEnvelope(buffer.data(),buffer.size(),base,envelope.get(),&error)){LogRateLimited("live-state-json",5000,LogLevel::kWarning,"[STATE] rejected malformed/schema-mismatched JSON bytes=%zu offset=%zu fallback=%d",buffer.size(),error,fallback);return LivePollResult::kInvalidState;}Envelope& env=*envelope;
    // state.old.json is expected to be one generation behind during the
    // writer's replacement window. It is a safe bootstrap fallback, but must
    // not manufacture a gap after that generation is already cached.
    if (fallback && had && Equal(base.epoch.value, env.candidate.epoch.value) &&
        env.candidate.sequence <= base.sequence) return LivePollResult::kNoChange;
    if(env.full){if(env.base_sequence!=0||env.topic_mask!=kAllTopics||env.base_revision_mask!=0){LogRateLimited("live-state-full",5000,LogLevel::kWarning,"[STATE] rejected invalid full snapshot sequence=%llu topics=0x%x",static_cast<unsigned long long>(env.candidate.sequence),env.topic_mask);return LivePollResult::kInvalidState;}if(had&&Equal(base.epoch.value,env.candidate.epoch.value)&&env.candidate.sequence<=base.sequence)return LivePollResult::kNoChange;env.candidate.present_topics=kAllTopics;
    }else{
      if(!had||!Equal(base.epoch.value,env.candidate.epoch.value)||env.base_sequence!=base.sequence||env.candidate.sequence!=base.sequence+1||env.base_revision_mask!=kAllTopics){AcquireSRWLockExclusive(&lock);needs_resync=true;ReleaseSRWLockExclusive(&lock);LogRateLimited("live-state-gap",5000,LogLevel::kWarning,"[STATE] delta gap cached=%llu base=%llu incoming=%llu",static_cast<unsigned long long>(base.sequence),static_cast<unsigned long long>(env.base_sequence),static_cast<unsigned long long>(env.candidate.sequence));return LivePollResult::kNeedsResync;}
      for(std::size_t i=0;i<kLiveTopicCount;++i){bool changed=(env.topic_mask&(1u<<i))!=0;if(env.base_revisions[i]!=base.revisions[i]||env.candidate.revisions[i]<env.base_revisions[i]||(changed?(env.candidate.revisions[i]==env.base_revisions[i]):(env.candidate.revisions[i]!=env.base_revisions[i]))){AcquireSRWLockExclusive(&lock);needs_resync=true;ReleaseSRWLockExclusive(&lock);LogRateLimited("live-state-revision-gap",5000,LogLevel::kWarning,"[STATE] revision gap topic=%s",kTopicNames[i]);return LivePollResult::kNeedsResync;}}
      env.candidate.present_topics=base.present_topics|env.topic_mask;
    }
    AcquireSRWLockExclusive(&lock);snapshot=env.candidate;available=true;needs_resync=false;ReleaseSRWLockExclusive(&lock);LogRateLimited("live-state-published",5000,LogLevel::kInfo,"[STATE] synchronized epoch=%s sequence=%llu full=%d topics=0x%x",env.candidate.epoch.value,static_cast<unsigned long long>(env.candidate.sequence),env.full,env.topic_mask);return LivePollResult::kPublished;
  }

  LivePollResult PollAckPath(const std::wstring& path, bool fallback,
                             const char* expected_epoch,
                             BridgeActionAck* output) {
    FileStamp stamp = {};
    const ReadResult rr = ReadStable(path, &buffer, &stamp, kMaxAckBytes);
    if (rr == ReadResult::kMissing) return LivePollResult::kNoFile;
    if (rr != ReadResult::kOk) {
      LogRateLimited("live-ack-read", 5000, LogLevel::kWarning,
                     "[ACTION] ack read failed result=%u fallback=%d",
                     static_cast<unsigned>(rr), fallback);
      return LivePollResult::kIoError;
    }
    FileStamp& observed = fallback ? ack_fallback_observed : ack_observed;
    if (SameStamp(stamp, observed)) return LivePollResult::kNoChange;
    observed = stamp;
    BridgeActionAck parsed;
    std::size_t error = 0;
    if (!ParseAck(buffer.data(), buffer.size(), expected_epoch, &parsed, &error)) {
      LogRateLimited("live-ack-json", 5000, LogLevel::kWarning,
                     "[ACTION] rejected malformed/stale ack bytes=%zu offset=%zu fallback=%d",
                     buffer.size(), error, fallback);
      return LivePollResult::kInvalidState;
    }
    *output = std::move(parsed);
    return LivePollResult::kPublished;
  }
};

LiveStateIngestor::LiveStateIngestor(const wchar_t* config_directory):impl_(std::make_unique<Impl>(config_directory)){}
LiveStateIngestor::~LiveStateIngestor()=default;
LivePollResult LiveStateIngestor::Poll(){std::lock_guard<std::mutex> io(impl_->io_mutex);ULONGLONG now=GetTickCount64();if(impl_->state_path.empty()&&now<impl_->next_discovery)return LivePollResult::kNoFile;if(!impl_->Discover()){impl_->next_discovery=now+5000;LogRateLimited("live-state-discovery",10000,LogLevel::kInfo,"[STATE] waiting for CONFIG_DIRECTORY/dualscreen/state.json");return LivePollResult::kNoFile;}LivePollResult result=impl_->PollPath(impl_->state_path,false);if(result==LivePollResult::kInvalidState||result==LivePollResult::kIoError||result==LivePollResult::kNoFile){std::wstring parent=impl_->state_path;std::size_t slash=parent.find_last_of(L"\\/");if(slash!=std::wstring::npos){std::wstring old=parent.substr(0,slash+1)+L"state.old.json";LivePollResult fallback=impl_->PollPath(old,true);if(fallback==LivePollResult::kPublished||fallback==LivePollResult::kNeedsResync||fallback==LivePollResult::kNoChange)return fallback;}}return result;}
LivePollResult LiveStateIngestor::PollAcknowledgement(BridgeActionAck* output) {
  if (!output) return LivePollResult::kInvalidState;
  std::lock_guard<std::mutex> io(impl_->io_mutex);
  *output = {};
  std::wstring state_path;
  char epoch[128] = {};
  AcquireSRWLockShared(&impl_->lock);
  const bool ready = impl_->available && !impl_->state_path.empty();
  if (ready) {
    state_path = impl_->state_path;
    std::snprintf(epoch, sizeof(epoch), "%s", impl_->snapshot.epoch.value);
  }
  ReleaseSRWLockShared(&impl_->lock);
  if (!ready) return LivePollResult::kNoFile;
  const std::size_t slash = state_path.find_last_of(L"\\/");
  if (slash == std::wstring::npos) return LivePollResult::kIoError;
  const std::wstring directory = state_path.substr(0, slash + 1);
  LivePollResult result = impl_->PollAckPath(directory + L"ack.json", false,
                                             epoch, output);
  if (result == LivePollResult::kInvalidState ||
      result == LivePollResult::kIoError || result == LivePollResult::kNoFile) {
    BridgeActionAck fallback;
    LivePollResult prior = impl_->PollAckPath(directory + L"ack.old.json", true,
                                              epoch, &fallback);
    if (prior == LivePollResult::kPublished) *output = std::move(fallback);
    if (prior == LivePollResult::kPublished || prior == LivePollResult::kNoChange)
      return prior;
  }
  return result;
}
bool LiveStateIngestor::CopyLatest(LiveStateSnapshot*out)const{if(!out)return false;AcquireSRWLockShared(&impl_->lock);bool ok=impl_->available;if(ok)*out=impl_->snapshot;ReleaseSRWLockShared(&impl_->lock);return ok;}
bool LiveStateIngestor::NeedsResync()const{AcquireSRWLockShared(&impl_->lock);bool value=impl_->needs_resync;ReleaseSRWLockShared(&impl_->lock);return value;}
bool LiveStateIngestor::CopyStatePath(wchar_t*out,std::size_t capacity)const{if(!out||!capacity)return false;AcquireSRWLockShared(&impl_->lock);bool fit=!impl_->state_path.empty()&&impl_->state_path.size()<capacity;if(fit)std::wmemcpy(out,impl_->state_path.c_str(),impl_->state_path.size()+1);else out[0]=0;ReleaseSRWLockShared(&impl_->lock);return fit;}
const char* LivePollResultName(LivePollResult r){switch(r){case LivePollResult::kNoFile:return"no_file";case LivePollResult::kNoChange:return"no_change";case LivePollResult::kPublished:return"published";case LivePollResult::kNeedsResync:return"needs_resync";case LivePollResult::kInvalidState:return"invalid_state";case LivePollResult::kIoError:return"io_error";}return"unknown";}

}  // namespace fomds
