# Device deployment record — 2026-08-29

This record covers the first Fallout 3 Second Screen device installation. It
does not claim a successful loaded-save runtime test.

- ADB endpoint: `100.83.63.70:34023`
- Target executable: root `Fallout3.exe`, 16,855,040 bytes
- Target SHA-256: `c3f97c2255fa041a851c17cf372d69aaadd8694e2dc4230ba556001bbfbd2f3e`
- Installed mod SHA-256: `2f35154fae6cee1a2ddb69697e73389568aabc1b1b0e025fcbf798ce1394df09`
- Existing game-local `dinput8.dll`: absent
- Prior Fallout3.exe AppDefaults `dinput8` value: absent
- Installed override: `native,builtin`
- Game/Wine process preflight: inactive
- Required container flags: `displaySecondScreen=1`, `wemuOwnsDesktop=1`

The installer verified the final device DLL hash and private Wine registry
value after publication.

Backups:

- Game namespace: `/storage/emulated/0/Download/Fallout-3-GOTY-SteamRIP.com/Fallout 3/.fallout3-dualscreen-backups/20260830-015336`
- Wine namespace: `/data/user/0/com.wemu/files/imagefs/home/xuser-2/.wine/.fallout3-dualscreen-backups/20260830-015336`

Only original mod files and one executable-scoped registry value were added.
No Fallout executable, archive, asset, master, runtime, DXVK, Proton, or
container setting was replaced.

## Launch prerequisite repair

The first post-install launch investigation proved two pre-existing prefix
defects: the Fallout 3 `Installed Path` registry value and active-user
`FalloutPrefs.ini` were both absent. The game directory itself contains a
dedicated registry-fix batch and a supplied preferences file, corroborating
both requirements.

After an inactive-Wine preflight, the repair installed:

- `HKLM\Software\Wow6432Node\Bethesda Softworks\Fallout3` / `Installed Path`
  = `F:\Download\Fallout-3-GOTY-SteamRIP.com\Fallout 3\`
- The device-owned `Fallout3/FalloutPrefs.ini` into the active Wine user's
  `Documents\My Games\Fallout3` directory. Its verified SHA-256 is
  `2a6f7dc257fd2e81b713b8a60dcd4e6cf438c6a31a0097dc4ae986a2cca959e4`.

Repair backup:

`/data/user/0/com.wemu/files/imagefs/home/xuser-2/.wine/.fallout3-dualscreen-backups/20260830-020333/launch-repair`
