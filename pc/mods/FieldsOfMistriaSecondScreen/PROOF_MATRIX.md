# Implementation proof matrix

| Claim | Automated evidence | Device evidence |
| --- | --- | --- |
| Exact current build only | `CompatibilityTests`; exact SHA/size/file-version gate | Exact executable accepted; both deployers classify the install as `Owned` |
| Heap-safe file hashing | `HashTests` validates both private binaries and a missing-file negative; read buffer is heap-backed | N/A |
| Exact SDL ABI surface | `ProxyAbiTests` compares all 1,271 names and ordinals and each forwarder target | Installed proxy and retained original hashes verified |
| Proxy loads and resolves | `ProxyRuntimeProbe` loads the staged pair and resolves every name and ordinal to the original address | Exact package installed; post-install game launch pending |
| Unknown hosts fail closed | `CompatibilityTests` negative host and runtime-probe log evidence | Pending |
| Input grammar and swipe isolation | `PanelInputTests` malformed, finite/kind-range, drag-return, swipe/scroll, cancel, reserved-handle coverage | Pending |
| Page wrapping | `PageCarouselTests` | Pending |
| Bounded panel startup ownership | `PanelLifecycleTests` covers signaled/joined and startup/cancellation timeout handle disposition | Pending |
| Spanning-root lower geometry | Strict Config/Panel compile and full deterministic build; prefers a real secondary monitor and otherwise validates configured `(0,720) 1240x1080` bounds | Prior launch proved Wine exposes one monitor; fixed generation installed, relaunch pending |
| Typed live-state lifecycle | `StateStoreTests` and `LiveStateTests` validate full/delta chains, all 20 bounded topics, revisions, privacy gates, and malformed/stale rejection | Pending live-save observation |
| Real action safety | `ActionTransportTests` validate eight typed actions, capability/revision/index/fingerprint/session gates, JSON encoding, ACKs, queue bounds, and stale rejection | Pending live-save observation |
| Installed-art safety/rendering | `InstalledAssetProviderTests` and `InstalledAssetRendererTests` cover the fixed manifest, six typed dynamic classes, collision/path/CRC/geometry rejection, GDI+ decode, font rendering, and bounded caches | Exact current archive installed; FEX/Wine render observation pending |
| Deterministic proxy | `build.ps1` compiles twice and requires identical SHA-256 | N/A |
| No undeployed toolchain runtime | build import allow-list rejects non-system dependencies such as `libwinpthread-1.dll` | N/A |
| Transactional native deploy defaults safe | `deploy.ps1 -SelfTest` proves pristine/owned classification, partial/unowned refusal, and runtime-process detection; default path is read-only | Commit succeeded; subsequent `Owned` dry-run passed |
| Transactional GML deploy defaults safe | `deploy-assets.ps1 -SelfTest`, package identity/CRC tests, exact rollback contract | Corrected commit succeeded; post-install no-op audit pending after ADB reconnect |
| Save-load-safe location state | 18 bridge/archive policy tests include guarded optional location/map localization; full 122,404-entry CRC | Exact game error identified the former `local_get(undefined)` path; corrected archive installed, retest pending |
| No fake game data/actions | 18 bridge/archive policy tests; only live model reads and eight gated real mutations | Runtime state/action proof pending |
| No proprietary art | Source/package review | Installed package contains code/config/metadata only; patched archive was derived on the user's machine |

The replacement native package is installed and proved by the full
deterministic gate and an exact-current device no-op audit as proxy SHA-256
`d751b9d49380e426635cbec0fa81604162c5b66c19c51349698fbef71bce8f19`
and hidden host SHA-256
`19a7354d8cea5e08baa00750f7cb3f5853668a7dfa515be92a8adbfdff07a53b`.
Installation and a separate exact-current device no-op audit are also proved
for the corrected patched archive SHA-256
`8faf44883c3756d317bed2ddaf1881c79c85402a67374e7dcccbc54998667b59`.
Both transactions retained verified rollback material and the exact originals.
An in-game save reload, live changes, panel presentation under FEX/Wine,
actions, and long-session behavior remain pending a real launch on the
supported device.
