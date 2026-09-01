# Installed asset renderer

## Boundary and ownership

`src/InstalledAssetRenderer.{h,cpp}` is the bounded raster layer above
`InstalledAssetProvider`. Fixed production callers provide `InstalledAssetKey`;
dynamic callers provide only a typed engine logical ID. The renderer has no
filename, archive-entry, extraction, or arbitrary-memory loading API.
Consequently every production byte first passes the provider's
exact v1.0.4 executable/archive trust gate, paired metadata checks, CRC check,
and 52-resource whitelist. No proprietary image or font data is compiled into
the mod or copied into its package.

The sole dynamic exception is the provider's constrained typed logical-ID
resolver, used only after the bridge has resolved a discovery-safe public
visual. `PrefetchDynamicImage`/`DrawDynamicFrame` key the decoded cache by
explicit asset class plus the `asset_to_string` logical ID. Item-only wrappers
are also provided. The provider resolves that identifier through its private,
collision-rejecting allowlisted-root index; the renderer never accepts an
archive path and cannot perform discovery/search.

The test-only resource injection API is guarded by
`FOMDS_ASSET_RENDERER_TESTING` and is absent from the production DLL.

## Image path

PNG bytes stay in process memory. The renderer copies a provider-owned PNG to
a delete-on-release `HGLOBAL` stream and decodes it with GDI+. The copied stream
remains alive until after the `Gdiplus::Bitmap` is destroyed, satisfying GDI+'s
stream lifetime rule. The provider handle is then released so the provider's
raw LRU is never pinned by the decoded cache. External image codecs are disabled
at GDI+ startup.

Before decode, audited frame geometry is checked against configurable maximum
dimensions and pixel count. After decode, the bitmap must be exactly
`frame_width * frame_count` by `frame_height`; Fields of Mistria animation
sheets are horizontal strips. `DrawFrame` validates the HDC, destination, and
frame index and uses source-over compositing. Nearest-neighbor scaling is the
default for the game's pixel art; bicubic scaling and bounded opacity are
explicit options.

## Font path

Whitelisted TTF/sfnt bytes are passed directly to a GDI+
`PrivateFontCollection::AddMemoryFont`. A renderer-owned bounded copy of the
font bytes, private collection, and selected family are retained together; the
provider handle is released after load so its raw LRU remains independent. Text is drawn and
measured through GDI+ without installing a font file or registering a
machine/user font. Font size and text length are bounded. Unsupported bold or
italic variants fall back to the installed family's regular face.

## Cache, lifecycle, and errors

Decoded images and private fonts share an LRU cache. Defaults are 16 objects,
48 MiB charged bytes, 16 million pixels per sheet, 16,384 pixels per dimension,
4,096 UTF-16 code units per text call, and 4-160 pixel fonts. Image charge is
the retained PNG plus four bytes per decoded pixel; font charge is the retained
sfnt byte count. Inserts evict the least recently used decoded object and never
exceed either configured limit.

All public operations are serialized. `Initialize` must run outside `DllMain`;
`Shutdown` destroys bitmaps before their streams, fonts before their private
collections/resources, clears accounting, and then shuts down GDI+. Calls made
before initialization fail closed. Provider, kind, decode, geometry, font,
frame, argument, allocation, and GDI+ draw failures are returned separately;
the latest diagnostic and non-frame-spam counters are available to the caller.

`PrefetchImage` and `PrefetchFont` allow the panel owner to pay archive/decode
cost before first paint (ideally on a controlled initialization worker). Once
cached, drawing performs no archive I/O or decode work.

## Tests and runtime caveats

`tests/InstalledAssetRendererTests.cpp` uses a synthetic two-frame PNG to prove
in-memory decode, exact frame selection, pixel output, frame/HDC bounds,
geometry rejection, font rejection, LRU eviction, accounting, and deterministic
shutdown. When the exact local v1.0.4 directory is available, the build also
decodes all 46 whitelisted animation sheets, loads all six installed fonts
under LRU pressure, draws the journal backplate and all 11 canonical base maps,
loads/draws the bridge-selected turnip icon through the constrained dynamic
path, then measures/draws the installed Birdseed font through the private
in-memory font path.

Windows tests prove the Win32/GDI+ path and exact installed resources. The
panel now initializes this renderer on a below-normal-priority worker after the
first valid live state, retains bounded caches, uses custom primitive fallbacks,
and rate-limits diagnostics/performance warnings. A real device launch is still
required to measure PNG/font behavior and cold-decode latency under its exact
Wine/FEX versions.
