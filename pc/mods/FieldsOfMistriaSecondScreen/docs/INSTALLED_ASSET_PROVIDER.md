# Installed archive asset provider

## Status

`src/InstalledAssetProvider.{h,cpp}` is a bounded, read-only provider for the
audited Fields of Mistria v1.0.4 UI resources. It has been proven against
deterministic synthetic ZIP64 fixtures and the local exact v1.0.4 archive, but
is now connected to the panel's background asset worker and the bounded
renderer. The provider itself does not decode images, rasterize fonts, extract
files, modify the game directory, or contain proprietary asset bytes.

## Trust gate

Production opens only the absolute directory of the loaded mod DLL. It rejects
directory or managed-file reparse points and never consults the current working
directory. The adjacent executable must match the audited 1.0.4 size and SHA-256.

Two install states are accepted:

- Fresh: `assets.zip` is the exact 625,619,136-byte original with SHA-256
  `e55ef6bcaa7430008a6fcae7980ef06ca6912bb10431c177807b366a0fc5a450`,
  and all ownership/backup paths are absent.
- Owned: the live archive is the exact audited GML-patched generation, the
  owner marker is exact, all 18 deployment-manifest fields match the audited
  installer contract, and the permanent backup is the exact original. The
  current accepted live SHA-256 is
  `8faf44883c3756d317bed2ddaf1881c79c85402a67374e7dcccbc54998667b59`.

All files in the trust chain are opened without write/delete sharing while
they are identified. For an owned install, assets are read only from the
verified permanent original backup. This keeps the resource index independent
of appended GML records while still proving the live owned generation.

Unknown, partial, unowned, tampered, symlink/reparse, and future archive states
fail closed to the caller's non-proprietary diagnostic UI.

## ZIP and resource validation

The provider parses the terminal EOCD, mandatory ZIP64 locator/EOCD where
present, and the bounded central directory once. Limits default to 130,000
entries, 64 MiB of central-directory data, and 1,024 bytes per name. It rejects
multi-disk archives, unsafe/absolute/dot-segment names, alternate separators,
case-folded duplicates, malformed ZIP64 extras, and out-of-bounds offsets.

The public API takes `InstalledAssetKey`; there is no arbitrary path-loading
method. The production manifest currently contains 52 audited resource pairs:
the diagnostic UI/icon/portrait entries recorded in `UI_ASSET_RECON.md`, all
11 canonical base-map sprites, and all six installed locale fonts. Every
selected PNG/TTF and its exact adjacent
`.meta.toml` must exist as a non-empty `ZIP_STORED` entry within configured
limits.

On first load the provider validates the local header/name, exact payload size,
central-directory CRC-32, paired metadata asset ID/kind, frame dimensions/count
and atlas or font metrics/shading, PNG signature/IHDR sheet dimensions, and
sfnt font signature. It exposes only immutable process-memory copies of the raw
PNG/font bytes, original metadata text, and parsed audited metadata. No image
decoder dependency has been introduced.

### Bridge-selected public visuals

The source-confirmed GML operation is
`asset_to_string(item.get_ui_icon())`, which yields the engine-resolved logical
animation name, not an archive path or metadata hex ID. The constrained
`LoadDynamicAnimation({class, logical_id})` API accepts that lowercase
`[a-z0-9_]` identifier together with a typed context. `LoadItemIcon` is the
item-only convenience wrapper.

During the already-required verified ZIP central-directory pass, the provider
internally indexes PNG/meta pairs only under these explicit classes:

| Class | Allowed installed roots |
|---|---|
| Item icon | `assets/animations/Item Icons/` |
| NPC portrait | `assets/animations/NPCs/**/Portraits/` |
| NPC map icon | `assets/animations/UI NEW/Generic/Icons/NPC Small/` |
| Animal icon | `assets/animations/UI NEW/Ranching/`, `assets/animations/UI NEW/Pets/` |
| Skill/perk icon | `assets/animations/UI NEW/Generic/Icons/Skill/`, `assets/animations/UI NEW/Skills/` |
| Spell UI | `assets/animations/UI NEW/Journal/Magic/` |

The basename-to-path resolver is private and has no list, count, prefix-search,
or path-read API. A logical name is admitted only when its basename is unique
within the requested class and an exact adjacent PNG exists. Collisions are
marked ambiguous and fail closed. Selecting a different class cannot cross
roots. This lets discovery-safe live topics render already-selected art without
giving the UI a mechanism to enumerate hidden items, NPCs, spells, or perks.

Each resolved pair receives the same local-header, stored-entry, size, and CRC
checks as fixed assets. Paired metadata must contain a canonical internal
16-hex ID and `Animation` kind. Non-portrait classes require the `UI` atlas;
portraits require a bounded `Portraits*` atlas name. Metadata must describe a
nonzero horizontal frame strip whose PNG IHDR matches exactly. Dynamic sheets
are additionally capped at 16,384 pixels per dimension and 16 million pixels
by default. A successful load is returned once rather than retained in the
provider's raw LRU; the renderer's bounded decoded LRU handles repeat use.
Success/failure counters expose no identities.

The bridge field serialization is not fixed here. Until a typed topic emits
the source-owned logical animation ID and its discovery gate is proven, callers
must keep the primitive fallback and must not derive an ID from display text.

## Cache

The default lazy cache is limited to 16 resource pairs and 16 MiB, with an
8 MiB data-entry limit and 64 KiB metadata limit. Least-recently-used entries
are evicted only when the renderer holds no external `shared_ptr`. If retained
resources prevent compliance with either limit, the new load returns
`kCacheFull`; limits are never exceeded. Hit, miss, eviction, item, and byte
counters are available through `CacheStats()`.

## Tests and remaining integration

`tests/InstalledAssetProviderTests.cpp` creates deterministic stored ZIP64
fixtures and covers fresh/owned trust, owner tampering, exact JSON manifest
validation, unsafe paths, unsupported compression, CRC corruption, metadata
mismatch, PNG/font validation, arbitrary-key rejection, immutable cache hits,
bounded eviction, retained-handle pressure, dynamic class/logical-ID/collision/
CRC/geometry gates, and absence of fallback lookup. `build.ps1` also loads and
validates every production whitelist pair when the local exact archive and
executable are available.

Panel integration, below-normal-priority background initialization, bounded
provider/renderer caches, graceful primitive fallback, and rate-limited
performance/error logging are complete. Remaining runtime work is measuring
FEX/Wine cold-hash/decode latency and testing file sharing during a real game
launch.
