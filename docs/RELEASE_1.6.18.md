# DualScreen Launcher 1.6.18

Release APK built from the current Android workspace, version code 69, signed
with the existing release certificate.

- Includes current launcher, controller, audio transport, CPU-affinity and
  secondary-display changes.
- Preserves AC II's Performance preset with full x87 precision. The user reported
  the falling-state problem resolved and approximately 30 FPS for most gameplay;
  this is not a guarantee of locked frame rate on every device or scene.
- Includes current bundled Crysis 3, Isaac and Transformers WFC payload assets,
  plus current Switch companion runtime and launcher integration, including
  Pokémon Legends Z-A, BOTW and Xenoblade work.
- Crysis 3 remains a read-only development companion with gameplay actions disabled.
- AC II remains an independently delivered development payload. Its live 3D Ezio
  preview and additional native actions are unfinished. Installing this APK does
  not publish or replace separate remote PC-mod payload packages.
- All current source changes, including unfinished native-mod work and research,
  are checkpointed in the private source repository. The public repository hosts
  the signed APK and checksum; experimental source is not represented as completed
  gameplay functionality.

Validation results and APK checksum are recorded with the GitHub release.

## Verification

- Launcher tests: 198 passed; engine tests: 173 passed, 5 skipped; no failures.
- All 1,560 checked current source assets match the APK. Bundled manifest hashes
  verified, including 91 Crysis 3, 1,309 Isaac and 135 WFC entries.
- Packaged Eden native allocated sections match the current workspace library.
- Signed with the existing release certificate. SHA-256 is in SHA256SUMS.txt.
- R8 reported Kotlin metadata warnings; the release build and verification passed.
- This APK has not received a fresh on-device gameplay acceptance test. Prior
  development-build observations do not establish that every feature is complete.
