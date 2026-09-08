$ErrorActionPreference = 'Stop'
$source = Get-Content -LiteralPath (Join-Path $PSScriptRoot '../src/GameBridge.cpp') -Raw
$apply = [regex]::Match($source, '(?s)case CharacterSwapMutation::kDetachOriginalControlLinks: \{(.*?)case CharacterSwapMutation::kBindPlayerControllerToTarget:').Groups[1].Value
if (-not $apply) { throw 'Atomic detachment stage not found.' }
$prepare = $apply.IndexOf('if(!PrepareInventoryWeaponAnimators())', [StringComparison]::Ordinal)
$leave = $apply.IndexOf('NativeLeavePlayerForm(', [StringComparison]::Ordinal)
$detach = $apply.IndexOf('NativeDetachPlayer(', [StringComparison]::Ordinal)
if ($prepare -lt 0 -or $leave -le $prepare -or $detach -le $leave) {
    throw 'Animator initialization must run inside atomic commit before form exit and controller detachment.'
}
if ([regex]::Matches($source, 'if\(!PrepareInventoryWeaponAnimators\(\)\)').Count -ne 1) {
    throw 'Expected exactly one game-side animator bootstrap call.'
}
$journal = [regex]::Match($source, '(?s)struct WfcSwapJournal \{(.*?)\n\};').Groups[1].Value
if ($journal -match 'UnrealObjectCloneProperty') { throw 'Owning clone journals must not live in copied swap snapshots.' }
if ($source -notmatch 'UnrealObjectCloneProperty g_weapon_animator_recovery;' -or
    $source -notmatch '(?s)case CharacterSwapMutation::kDetachOriginalControlLinks: \{\s*if\(g_swap_journal.animator_preparation_dirty.*?graph_restore_failed=true;\s*return false;') {
    throw 'Dirty animator preparation must retain recovery and prevent input/AI resume.'
}
Write-Host 'Animator integration source checks passed: atomic placement, single caller, fixed-address recovery and dirty rollback gate (not gameplay).'
