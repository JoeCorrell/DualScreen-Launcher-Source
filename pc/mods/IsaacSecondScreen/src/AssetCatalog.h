#pragma once

#include <string>
#include <map>

namespace isaacds {

struct ItemMetadata {
  int id = 0;
  int quality = -1;
  int max_charges = 0;
  std::string kind, affected_stats;
  std::string asset;
  std::string name;
  std::string description;
};

struct PlayerMetadata {
  int id = 0;
  std::string asset;
  std::string name;
};

struct PocketMetadata {
  int id = 0;
  int pickup_frame = 0;
  std::string name;
  std::string description;
};

struct ProgressMetadata {
  int id = 0;
  std::string name, requirement, asset;
};
const std::map<int, ProgressMetadata>& AchievementMetadata();
const std::map<int, ProgressMetadata>& ChallengeMetadata();
const std::map<int, ProgressMetadata>& BossMetadata();

struct SpriteFrame {
  std::string asset;
  int x = 0;
  int y = 0;
  int width = 0;
  int height = 0;
  int pivot_x = 0, pivot_y = 0;
};

bool LoadAssetCatalog(const wchar_t* path);
void ClearAssetCatalog();
const ItemMetadata* FindCollectible(int id);
const ItemMetadata* FindTrinket(int id);
const PlayerMetadata* FindPlayer(int id);
const PocketMetadata* FindPocket(int id);
const SpriteFrame* FindSprite(const char* actor, const char* animation,
                              int frame = 0);
const std::string& CatalogSourceBuild();

}  // namespace isaacds
