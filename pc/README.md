# PC dual-screen mods

This directory is the canonical source tree for Dualscreen Launcher's PC
dual-screen mods. Keeping the mod implementations beside the Android launcher
and the embedded Switch runtime makes the complete supported-game stack
available from one project checkout.

| Directory | Game/component |
| --- | --- |
| `SkyrimSecondScreen` | Skyrim Special Edition |
| `Fallout3SecondScreen` | Fallout 3 |
| `Fallout4SecondScreen` | Fallout 4 |
| `GrimDawnSecondScreen` | Grim Dawn |
| `DeusExSecondScreen` | Deus Ex: Human Revolution Director's Cut |
| `DeadIslandSecondScreen` | Dead Island Definitive Edition |
| `ValheimSecondScreen` | Valheim dual-screen panel |
| `ValheimPerformance` | Valheim performance companion patch |
| `StardewNative` | Stardew native game-state bridge/patch source |
| `StardewSecondScreen` | Stardew second-screen source |
| `FieldsOfMistriaSecondScreen` | Fields of Mistria work-in-progress mod |

Generated build trees and deployable DLL/EXE/PDB artifacts are intentionally
not stored here. Build each mod with its own checked-in build script, then run
`tools/import-mod-payloads.ps1`. The importer now defaults to this directory;
it no longer depends on a WEMU checkout or an absolute developer-machine path.

Stardew's binary-delta packer requires legally supplied original game
assemblies and intermediate builds. Those proprietary inputs must stay outside
Git. Pass their containing directory to `-StardewBuildInputRoot` only when the
checked-in, verified deltas need to be regenerated. The expected layout is:

```text
device-original/Stardew Valley.dll
steam-original/Stardew Valley.dll
display-sync/gog/Stardew Valley.dll
display-sync/steam/Stardew Valley.dll
restored/gog/Stardew Valley.dll
restored/steam/Stardew Valley.dll
```

The packaged, hash-pinned PC payloads consumed at runtime remain under
`launcher/src/main/assets/mods`. Game executables and proprietary game assets
are never part of this repository.
