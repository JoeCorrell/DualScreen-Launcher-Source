// Native texture fallback backed by DXHR Director's Cut's own BIGFILE archive.
//
// Some UI RenderResources are streamed only while the vanilla Scaleform menu is
// open.  The second-screen renderer must not open that menu (it pauses gameplay),
// so this loader reads the matching PCD9 payload from pickup_database.drm and
// uploads it to the active D3D11 device.  It never writes to the game archives.

#pragma once

#include <cstdint>

struct ID3D11Device;
struct ID3D11ShaderResourceView;

namespace dxs {

struct GameArchiveTexture {
  // AcquireTexture returns one caller-owned reference.  Release it after the
  // draw system has taken its own reference.
  ID3D11ShaderResourceView* view = nullptr;
  uint32_t width = 0;
  uint32_t height = 0;
  uint32_t texture_id = 0;
};

class GameTextureArchive {
 public:
  GameTextureArchive();
  ~GameTextureArchive();

  GameTextureArchive(const GameTextureArchive&) = delete;
  GameTextureArchive& operator=(const GameTextureArchive&) = delete;

  // Locate BIGFILE.000 beneath game_directory and index the type-5 PCD9
  // sections in pc-w\pickup_database.drm plus the global Scaleform and player
  // databases (which own lock/augmentation UI art). Passing nullptr uses the
  // directory containing DXHRDC.exe. Calls are idempotent and thread-safe.
  bool Initialize(const wchar_t* game_directory = nullptr);

  // Returns the game's exact BC1/BC3 texture for a RenderResource domain ID.
  // The returned view has one caller-owned COM reference.  On failure, output
  // is reset and no partially-created resource escapes.
  bool AcquireTexture(ID3D11Device* device, uint32_t texture_id,
                      GameArchiveTexture* output);

  // Releases archive bytes and cached GPU resources.  Safe during teardown or
  // device loss; future Initialize/AcquireTexture calls may rebuild the cache.
  void Shutdown();

  bool ready() const;
  uint32_t indexed_texture_count() const;

 private:
  struct Impl;
  Impl* impl_ = nullptr;
};

GameTextureArchive& TheGameTextureArchive();

}  // namespace dxs
