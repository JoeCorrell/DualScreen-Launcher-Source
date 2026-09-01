# BOTW companion model converter

`Convert-BotwModel.ps1` converts a model from user-owned BOTW resources into portable glTF 2
and a self-contained GLB 2 for the Android companion renderer. It uses the headless Switch
Toolbox BFRES/BNTX exporters and AssimpNet included with a local Switch Toolbox installation.

```powershell
.\tools\botw-companion\Convert-BotwModel.ps1 `
  -ModelInput .work\botw-player-resources\Model\Armor_001.sbfres `
  -TextureInput .work\botw-player-resources\Model\Armor_001.Tex.sbfres `
  -ModelName Armor_001_Head_A `
  -AnimationBfres .work\botw-player-resources\Model\Player_Animation.sbfres `
  -Pose Nml_Wait `
  -OutputDirectory .work\botw-player-export\Armor_001_Head_A `
  -SwitchToolboxPath .work\switch-toolbox
```

`-PoseFrame` selects a specific animation frame. Its default value samples the middle of the
selected animation. Omit `-AnimationBfres` to retain the bind pose.

The converter accepts Yaz0-compressed or uncompressed BFRES texture containers and raw BNTX
texture containers. `-TextureInput` may be repeated or passed an array. Only textures referenced
by the selected model are decoded. If a model also references shared textures (for example,
Link's hair textures), pass their texture containers too. References that are unavailable are
removed from the affected material slots so the published glTF never points at missing files.

Outputs are `<model>.gltf`, its relative `.bin` and PNG resources, plus `<model>.glb`. The GLB is
validated as version 2 and must contain a skin, embedded PNG texture data, and—when requested—a
pose that changed matching animated bones. Existing output files are protected unless `-Force`
is specified. The converter also repairs the legacy Assimp exporter's floating-point `JOINTS`
accessors and removes its nonstandard skin fields before packaging the result.

The converter never accepts or reads key files. Do not redistribute extracted Nintendo assets.
