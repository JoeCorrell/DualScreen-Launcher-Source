// Floor-aware vanilla map rasters generated locally from DXHR's own SWFs.
//
// The runtime never starts Java, opens the game menu, or hooks Scaleform draw
// primitives. BuildMapCache.ps1 performs the vector rasterization once; this
// class only inflates the selected floor and creates an immutable D3D11 view.

#pragma once

#include <cstdint>

struct ID3D11Device;
struct ID3D11ShaderResourceView;

namespace dxs {

struct MapRasterTexture {
  // Acquire returns one caller-owned reference. Release it after the draw
  // queue has taken its own temporary reference.
  ID3D11ShaderResourceView* view = nullptr;
  uint32_t width = 0;
  uint32_t height = 0;
  uint32_t map_dtp_id = 0;
  int floor = 0;
  bool exact_floor = false;
};

struct MapRasterInfo {
  uint32_t width = 0;
  uint32_t height = 0;
  uint32_t map_dtp_id = 0;
  int floor = 0;
  bool exact_floor = false;
};

class MapRasterCache {
 public:
  MapRasterCache();
  ~MapRasterCache();

  MapRasterCache(const MapRasterCache&) = delete;
  MapRasterCache& operator=(const MapRasterCache&) = delete;

  // Uses <game_directory>\DeusExSecondScreen\MapCache. Passing nullptr locates
  // DXHRDC.exe and uses its containing directory. Calls are idempotent.
  bool Initialize(const wchar_t* game_directory = nullptr);

  // Loads map_<map_dtp_id>_f<floor>.dxm. If an exact floor is unavailable it
  // safely falls back to floor zero. Only the current and immediately previous
  // GPU views are retained; decompressed CPU pixels are always freed before
  // this function returns.
  bool Acquire(ID3D11Device* device, uint32_t map_dtp_id, int floor,
               MapRasterTexture* output);

  // Returns the actual selected .dxm dimensions without creating a GPU
  // resource. The same exact-floor/floor-zero fallback as Acquire is used so
  // presentation geometry and the subsequently drawn texture cannot disagree.
  bool Describe(uint32_t map_dtp_id, int floor, MapRasterInfo* output);

  // Enumerates the real authored floors present for one map. Values are
  // sorted from lowest to highest and every returned file has a validated
  // DXHR map-cache header. Returns the total number copied to `floors`.
  int AvailableFloors(uint32_t map_dtp_id, int* floors, int capacity);

  // Release cached COM resources while D3D11 is still alive.
  void Shutdown();

  bool ready() const;
  const wchar_t* directory() const;

 private:
  struct Impl;
  Impl* impl_ = nullptr;
};

MapRasterCache& TheMapRasterCache();

}  // namespace dxs
