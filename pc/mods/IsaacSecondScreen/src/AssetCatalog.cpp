#include "AssetCatalog.h"

#include <windows.h>

#include <cerrno>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "Protocol.h"
#include "Log.h"

namespace isaacds {
namespace {

std::unordered_map<int, ItemMetadata> g_collectibles;
std::unordered_map<int, ItemMetadata> g_trinkets;
std::unordered_map<int, PlayerMetadata> g_players;
std::unordered_map<int, PocketMetadata> g_pockets;
std::unordered_map<std::string, SpriteFrame> g_sprites;
std::string g_source_build;

std::vector<std::string_view> Split(std::string_view value, char separator,
                                    size_t maximum) {
  std::vector<std::string_view> fields;
  size_t start = 0;
  for (;;) {
    if (fields.size() >= maximum) return {};
    const size_t end = value.find(separator, start);
    if (end == std::string_view::npos) {
      fields.push_back(value.substr(start));
      return fields;
    }
    fields.push_back(value.substr(start, end - start));
    start = end + 1;
  }
}

bool Integer(std::string_view input, int minimum, int maximum, int* output) {
  if (!output || input.empty() || input.size() >= 32) return false;
  char buffer[32] = {};
  std::memcpy(buffer, input.data(), input.size());
  char* end = nullptr;
  errno = 0;
  const long value = std::strtol(buffer, &end, 10);
  if (errno == ERANGE || end != buffer + input.size() || value < minimum ||
      value > maximum)
    return false;
  *output = static_cast<int>(value);
  return true;
}

std::string SpriteKey(std::string_view actor, std::string_view animation,
                      int frame) {
  return std::string(actor) + '\n' + std::string(animation) + '\n' +
         std::to_string(frame);
}

bool Decode(std::string_view input, std::string* output) {
  return PercentDecode(input, output);
}

bool ParseItem(const std::vector<std::string_view>& fields,
               std::unordered_map<int, ItemMetadata>* target) {
  if (fields.size() != 7 || !target) return false;
  ItemMetadata item;
  if (!Integer(fields[1], 1, 65535, &item.id) ||
      !Integer(fields[2], -1, 16, &item.quality) ||
      !Integer(fields[3], 0, 1000000, &item.max_charges) ||
      !Decode(fields[4], &item.asset) || !Decode(fields[5], &item.name) ||
      !Decode(fields[6], &item.description) || item.asset.empty())
    return false;
  return target->emplace(item.id, std::move(item)).second;
}

}  // namespace

bool LoadAssetCatalog(const wchar_t* path) {
  ClearAssetCatalog();
  if (!path || !*path) return false;
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    Log(LogLevel::kError,
        "[Assets] could not open catalog attributes=%08lX error=%lu",
        static_cast<unsigned long>(GetFileAttributesW(path)),
        static_cast<unsigned long>(GetLastError()));
    return false;
  }
  std::string line;
  size_t line_number = 0;
  while (std::getline(input, line)) {
    ++line_number;
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.empty()) continue;
    const auto fields = Split(line, '\t', 12);
    if (fields.empty()) {
      Log(LogLevel::kError, "[Assets] catalog split failed line=%zu",
          line_number);
      return false;
    }
    if (fields[0] == "V") {
      int version = 0;
      if (line_number != 1 || fields.size() != 3 ||
          !Integer(fields[1], 1, 1, &version) ||
          !Decode(fields[2], &g_source_build)) {
        Log(LogLevel::kError, "[Assets] invalid catalog version line=%zu",
            line_number);
        return false;
      }
    } else if (fields[0] == "C") {
      if (!ParseItem(fields, &g_collectibles)) {
        Log(LogLevel::kError, "[Assets] invalid collectible line=%zu",
            line_number);
        return false;
      }
    } else if (fields[0] == "T") {
      if (!ParseItem(fields, &g_trinkets)) {
        Log(LogLevel::kError, "[Assets] invalid trinket line=%zu", line_number);
        return false;
      }
    } else if (fields[0] == "P") {
      PlayerMetadata player;
      if (fields.size() != 4 ||
          !Integer(fields[1], 0, 255, &player.id) ||
          !Decode(fields[2], &player.asset) ||
          !Decode(fields[3], &player.name) || player.asset.empty() ||
          !g_players.emplace(player.id, std::move(player)).second) {
        Log(LogLevel::kError, "[Assets] invalid player line=%zu", line_number);
        return false;
      }
    } else if (fields[0] == "K") {
      PocketMetadata pocket;
      if (fields.size() != 5 ||
          !Integer(fields[1], 0, 65535, &pocket.id) ||
          !Integer(fields[2], 0, 65535, &pocket.pickup_frame) ||
          !Decode(fields[3], &pocket.name) ||
          !Decode(fields[4], &pocket.description) ||
          !g_pockets.emplace(pocket.id, std::move(pocket)).second) {
        Log(LogLevel::kError, "[Assets] invalid pocket line=%zu", line_number);
        return false;
      }
    } else if (fields[0] == "A") {
      std::string actor;
      std::string animation;
      SpriteFrame sprite;
      int frame = 0;
      if (fields.size() != 9 || !Decode(fields[1], &actor) ||
          !Decode(fields[2], &animation) ||
          !Integer(fields[3], 0, 65535, &frame) ||
          !Decode(fields[4], &sprite.asset) ||
          !Integer(fields[5], 0, 65535, &sprite.x) ||
          !Integer(fields[6], 0, 65535, &sprite.y) ||
          !Integer(fields[7], 1, 65535, &sprite.width) ||
          !Integer(fields[8], 1, 65535, &sprite.height) ||
          sprite.asset.empty() ||
          !g_sprites
               .emplace(SpriteKey(actor, animation, frame), std::move(sprite))
               .second) {
        Log(LogLevel::kError, "[Assets] invalid sprite line=%zu", line_number);
        return false;
      }
    } else {
      Log(LogLevel::kError, "[Assets] unknown catalog marker line=%zu",
          line_number);
      return false;
    }
  }
  const bool ready = input.eof() && !g_source_build.empty() &&
                     g_collectibles.size() >= 700 &&
                     g_trinkets.size() >= 180 && g_players.size() >= 40 &&
                     !g_sprites.empty();
  if (!ready) {
    Log(LogLevel::kError,
        "[Assets] incomplete catalog eof=%u build=%u collectibles=%zu "
        "trinkets=%zu players=%zu pockets=%zu sprites=%zu",
        input.eof() ? 1u : 0u, g_source_build.empty() ? 0u : 1u,
        g_collectibles.size(), g_trinkets.size(), g_players.size(),
        g_pockets.size(), g_sprites.size());
  }
  return ready;
}

void ClearAssetCatalog() {
  g_collectibles.clear();
  g_trinkets.clear();
  g_players.clear();
  g_pockets.clear();
  g_sprites.clear();
  g_source_build.clear();
}

const ItemMetadata* FindCollectible(int id) {
  const auto found = g_collectibles.find(id);
  return found == g_collectibles.end() ? nullptr : &found->second;
}

const ItemMetadata* FindTrinket(int id) {
  const auto found = g_trinkets.find(id);
  return found == g_trinkets.end() ? nullptr : &found->second;
}

const PlayerMetadata* FindPlayer(int id) {
  const auto found = g_players.find(id);
  return found == g_players.end() ? nullptr : &found->second;
}

const PocketMetadata* FindPocket(int id) {
  const auto found = g_pockets.find(id);
  return found == g_pockets.end() ? nullptr : &found->second;
}

const SpriteFrame* FindSprite(const char* actor, const char* animation,
                              int frame) {
  if (!actor || !animation || frame < 0) return nullptr;
  const auto found = g_sprites.find(SpriteKey(actor, animation, frame));
  return found == g_sprites.end() ? nullptr : &found->second;
}

const std::string& CatalogSourceBuild() { return g_source_build; }

}  // namespace isaacds
