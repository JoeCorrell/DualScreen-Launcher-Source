param(
    [Parameter(Mandatory = $true)][string]$Dumper,
    [Parameter(Mandatory = $true)][string]$PackageDirectory
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot

function Read-FunctionMetadata([string]$Package, [string[]]$Names) {
    $lines = & $Dumper (Join-Path $PackageDirectory ($Package + '.xxx')) --metadata @Names
    if ($LASTEXITCODE -ne 0) { throw "Metadata dump failed: $Package" }
    $functions = @{}
    $current = $null
    foreach ($line in $lines) {
        $exportMatch = [regex]::Match($line, "^// OBJECT Function'([^']+)'")
        if ($exportMatch.Success) {
            $current = @{ Flags = [uint64]0; Token = -1; Params = @{} }
            $functions[$exportMatch.Groups[1].Value] = $current
        }
        $flagMatch = [regex]::Match($line, '^// FUNCTION flags=0x([0-9A-F]+) native=(\d+)')
        if ($flagMatch.Success -and $null -ne $current) {
            $current.Flags = [Convert]::ToUInt64($flagMatch.Groups[1].Value, 16)
            $current.Token = [int]$flagMatch.Groups[2].Value
        }
        $paramMatch = [regex]::Match($line, '^// PARAM (\S+) class=(\S+) flags=0x([0-9A-F]+)')
        if ($paramMatch.Success -and $null -ne $current) {
            $current.Params[$paramMatch.Groups[1].Value] = $paramMatch.Groups[2].Value
        }
    }
    return $functions
}

$critical = @(
    @{ Package='Core'; Name='Core.Object.Clone'; Flags=[uint64]0x00020400; Params=@{InOuter='ObjectProperty'; InName='NameProperty'; ReturnValue='ObjectProperty'} },
    @{ Package='TransGame'; Name='TransGame.TnCharacterScriptBinding.SelectCharacter'; Flags=[uint64]0x00024002; Params=@{CharacterName='StrProperty'; Ignored='IntProperty'} },
    @{ Package='TransGame'; Name='TransGame.TnAiWeaponShooter.IsFiring'; Flags=[uint64]0x02020000; Params=@{ReturnValue='BoolProperty'} },
    @{ Package='TransGame'; Name='TransGame.TnAiController.CancelMove'; Flags=[uint64]0x02020000; Params=@{} },
    @{ Package='TransGame'; Name='TransGame.TnAiController.MovingToPosition.CancelMove'; Flags=[uint64]0x04020002; Params=@{} },
    @{ Package='TransGame'; Name='TransGame.TnAiController.MovingToActor.CancelMove'; Flags=[uint64]0x04020002; Params=@{} },
    @{ Package='TransGame'; Name='TransGame.TnAiController.MoveDelegatesCleared'; Flags=[uint64]0x00020002; Params=@{ReturnValue='BoolProperty'} },
    @{ Package='TransGame'; Name='TransGame.TnAiController.RequestReplan'; Flags=[uint64]0x00020002; Params=@{Priority='ByteProperty'} },
    @{ Package='TransGame'; Name='TransGame.TnAiPlanningScheduler.SubmitRequest'; Flags=[uint64]0x00020002; Params=@{Priority='ByteProperty'; PlanRunner='ObjectProperty'} },
    @{ Package='TransGame'; Name='TransGame.TnAiWeaponShooter.Stopped.StopFire'; Flags=[uint64]0x04020000; Params=@{} },
    @{ Package='TransGame'; Name='TransGame.TnAiWeaponShooter.Activate'; Flags=[uint64]0x00020002; Params=@{WeaponClass='ClassProperty'; OnShooterFailed='DelegateProperty'} },
    @{ Package='TransGame'; Name='TransGame.TnFineAimManager.Initialize'; Flags=[uint64]0x00020002; Params=@{P='ObjectProperty'; pt='ObjectProperty'; CSM='ObjectProperty'} },
    @{ Package='TransGame'; Name='TransGame.TnPickupManager.Initialize'; Flags=[uint64]0x00020102; Params=@{OwnerP='ObjectProperty'; ContextualCommander_='ObjectProperty'} },
    @{ Package='HM_Engine'; Name='HM_Engine.HmPlayerTargeting.set_TargetingOwner'; Flags=[uint64]0x800030102; Params=@{Value='ObjectProperty'} },
    @{ Package='Engine'; Name='Engine.PlayerController.SetViewTarget'; Flags=[uint64]0x02020400; Params=@{NewViewTarget='ObjectProperty'; TransitionParams='StructProperty'} },
    @{ Package='Engine'; Name='Engine.PrimitiveComponent.SetRBCollidesWithChannel'; Flags=[uint64]0x00020400; Params=@{Channel='ByteProperty'; bNewCollides='BoolProperty'} },
    @{ Package='TransGame'; Name='TransGame.TnPawnCollisionController.SetRBCollidesWithChannel'; Flags=[uint64]0x00040002; Params=@{Channel='ByteProperty'; Value='BoolProperty'} },
    @{ Package='Engine'; Name='Engine.PlayerController.IgnoreMoveInput'; Flags=[uint64]0x00020002; Params=@{bNewMoveInput='BoolProperty'} },
    @{ Package='Engine'; Name='Engine.PlayerController.IgnoreLookInput'; Flags=[uint64]0x00020002; Params=@{bNewLookInput='BoolProperty'} },
    @{ Package='HM_Engine'; Name='HM_Engine.CheckpointManager.SaveActiveCheckpoint'; Flags=[uint64]0x00820002; Params=@{CheckpointIndex='IntProperty'; WriteDataCompleteCallback='DelegateProperty'} },
    @{ Package='HM_Engine'; Name='HM_Engine.HmGoapPlanRunner.set_ActionSet'; Flags=[uint64]0x800040102; Params=@{Value='ObjectProperty'} },
    @{ Package='HM_Engine'; Name='HM_Engine.HmGoapPlanRunner.set_GoalSet'; Flags=[uint64]0x800040102; Params=@{Value='ObjectProperty'} }
)
foreach ($packageName in @('TransGame', 'Engine', 'HM_Engine', 'Core')) {
    $contracts = @($critical | Where-Object { $_.Package -eq $packageName })
    $names = @($contracts | ForEach-Object { $_.Name })
    $metadata = Read-FunctionMetadata -Package $packageName -Names $names
    foreach ($contract in $contracts) {
        $actual = $metadata[$contract.Name]
        if ($null -eq $actual -or $actual.Token -ne 0 -or $actual.Flags -ne $contract.Flags -or
            $actual.Params.Count -ne $contract.Params.Count) {
            throw "Cooked call signature changed: $($contract.Name)"
        }
        foreach ($name in $contract.Params.Keys) {
            if ($actual.Params[$name] -ne $contract.Params[$name]) {
                throw "Cooked parameter changed: $($contract.Name).$name"
            }
        }
    }
}

# Verify every existing role-adapter cast against the actual decompiled opcode,
# not a second hand-maintained list of offsets. This never patches the package.
$source = Get-Content -LiteralPath (Join-Path $projectRoot 'src/CharacterRoleAdapter.cpp') -Raw
$start = $source.IndexOf('const std::array<CastPatchSpec')
$end = $source.IndexOf('bool metadata_valid', $start)
$section = $source.Substring($start, $end - $start)
$casts = [regex]::Matches($section, 'L"([^"\r\n]+)"\s*,\s*L"([^"\r\n]+)"\s*,\s*(kCurrentPawnCastOpcodeOffset|0x[0-9A-Fa-f]+u)')
if ($casts.Count -ne 20) { throw 'Update the audit when the role-adapter cast set changes.' }
$filters = @($casts | ForEach-Object { $_.Groups[1].Value + '.' + $_.Groups[2].Value })
$oldCastDump = $env:WFC_DUMP_CASTS
try {
    $env:WFC_DUMP_CASTS = '1'
    $lines = & $Dumper (Join-Path $PackageDirectory 'TransGame.xxx') @filters
    if ($LASTEXITCODE -ne 0) { throw 'Cast decompilation failed' }
} finally {
    if ($null -eq $oldCastDump) { Remove-Item Env:\WFC_DUMP_CASTS }
    else { $env:WFC_DUMP_CASTS = $oldCastDump }
}
$observed = @{}
$owner = ''
foreach ($line in $lines) {
    $exportMatch = [regex]::Match($line, "^// OBJECT Function'([^']+)'")
    if ($exportMatch.Success) { $owner = $exportMatch.Groups[1].Value }
    $castMatch = [regex]::Match($line, "^// CAST position=0x([0-9A-F]+).*opcode=0x2E class=Class'TransGame.TnPlayerPawn'")
    if ($castMatch.Success) {
        $observed[$owner + ':' + [Convert]::ToUInt32($castMatch.Groups[1].Value, 16)] = $true
    }
}
foreach ($cast in $casts) {
    $name = $cast.Groups[1].Value + '.' + $cast.Groups[2].Value
    $offset = if ($cast.Groups[3].Value -eq 'kCurrentPawnCastOpcodeOffset') { 6 }
              else { [Convert]::ToUInt32($cast.Groups[3].Value.TrimEnd('u').Substring(2), 16) }
    if (-not $observed.ContainsKey($name + ':' + $offset)) { throw "Cast mismatch: ${name}:$offset" }
}
Write-Host "Verified $($critical.Count) critical cooked call signatures and $($casts.Count) role-adapter cast sites."

# Behavioral contracts, not just names/offsets. These are the native semantics
# the custom campaign adapter must preserve; no game/device writes are made.
$behavior = @(
    @{ Name='TnAiController.ShouldUpdateAiming'; Patterns=@('__NFUN_119__\(Enemy, none\)', 'AiWorldState.GetBoolValue\(Class''TransGame.TnAiWorldStateLOSToEnemy''\)') },
    @{ Package='HM_Engine'; Name='HmGoapWorldStateCollection.GetBoolValue'; Patterns=@('Type.default.HashIndex, Properties.Length', 'Properties\[Type.default.HashIndex\].Type, Type', 'return false', 'return Properties\[Type.default.HashIndex\].Value.BoolVal') },
    @{ Name='TnAiWeaponAimer.get_OwnerIsAiming'; Patterns=@('return IsAiming\(\)') },
    @{ Name='TnAiWeaponAimer.IsAiming'; Patterns=@('_CheckAimFromWeaponRotation', 'Location = _Owner.Weapon.Location', 'Rotation = _Owner.Weapon.Rotation', 'Location = _Owner.Location', 'Rotation = _Owner.Rotation', 'Vector\(CalculateAdjustedAimFor\(Location\)\)', '0.8660000') },
    @{ Name='TnAiWeaponAimer.AimAtLocation'; Patterns=@('(?s)_AimingMode = 1;\s*_TargetLocation = Location;') },
    @{ Name='TnAiWeaponAimer.CalculateAdjustedAimFor'; Patterns=@('_AimingMode', '_TargetDirection, _Owner.Rotation', 'get_TargetLocation\(\), Location') },
    @{ Name='TnAiWeaponAimer.get_TargetLocation'; Patterns=@('case 0:', 'GetTargetActorTargetableLocation\(\)', '(?s)case 1:\s*return _TargetLocation;', 'case 2:') },
    @{ Name='TnBuffManager.SetHud'; Patterns=@('(?s)\{\s*local @NULL NewHud;\s*OwnerHud = NewHud;\s*//return;\s*\}') },
    @{ Name='TnBuffManager.Initialize'; Patterns=@('SavedReplicateToOwnerBuffClasses.Insert\(0, 64\)', 'SavedReplicateToAllBuffClasses.Insert\(0, 16\)', 'OwnerPawn = OwnerDude') },
    @{ Name='TnAbilityManager.SetHud'; Patterns=@('OwnerHud = NewHud', 'UpdateResourceUI\(\)', 'FinishAbilityCooldown\(0\)', 'FinishAbilityCooldown\(1\)', 'FinishAbilityCooldown\(2\)') },
    @{ Name='TnAbilityManager.FinishAbilityCooldown'; Patterns=@('OwnerHud.SetAbilityResourceReady\(AbilityNumber\)') },
    @{ Name='TnAbilityManager.StartAbilityCooldown'; Patterns=@('Cooldowns\[AbilityNumber\]', 'Abilities\[AbilityNumber\].GetCooldownTime\(\)', 'CooldownMultiplier', 'OwnerHud.SetAbilityResource\(AbilityNumber, AdjustedCooldownTime, NormalizedCooldownStart\)') },
    @{ Name='TnAbilityManager.UpdateResourceUI'; Patterns=@('ResourceRequired = GetResourceRequired\(\)', 'OwnerHud.SetAbilityResource\(0, 0.0000000, __NFUN_172__\(Resource, ResourceRequired\)\)', 'OwnerHud.SetAbilityResourceReady\(0\)') },
    @{ Name='TnAbilityManager.SetCooldownMultiplier'; Patterns=@('CooldownMultiplier = NewMultiplier', 'StartAbilityCooldown\(I, Cooldowns\[I\]\)') },
    @{ Name='TnAbilityManager.Tick'; Patterns=@('Abilities\[I\].CanStartCooldown\(\)', 'CooldownTime = Abilities\[I\].GetCooldownTime\(\)', 'Cooldowns\[I\] = CooldownTime', 'StartAbilityCooldown\(I, CooldownTime\)', 'FinishAbilityCooldown\(I\)', '__NFUN_185__\(SpamPreventionCooldowns\[I\], DeltaTime\)') },
    @{ Name='TnAbilityManager.AddAbility'; Patterns=@('(?s)if\(__NFUN_119__\(AbilityClass, none\)\).*AddedAbility = CreateAbility\(AbilityClass\);.*AddedAbility.Initialize\(OwnerPawn\);.*\}.*Abilities.AddItem\(AddedAbility\)', 'Cooldowns.AddItem\(0.0000000\)', 'SpamPreventionCooldowns.AddItem\(0.0000000\)') },
    @{ Name='TnAbilityManager.GetResourceRequired'; Patterns=@('Abilities.Length, 1', 'Abilities\[0\], none', 'return 0.0000000', 'return Abilities\[0\].GetResourceRequired\(\)') },
    @{ Name='TnPropertyBuffModifier.ModifyFloatInternal'; Patterns=@('PawnOwner.GetService\(''TnBuffService''\)', 'BuffService.GetBuff\(BuffClass\)', 'B.ShouldBuffEffect\(PawnOwner, OptionalInfo\)', 'AggregateFloatModifier\(Modifier, B.GetFloatModifier\(\)\)', 'ApplyFloatModifier\(PropertyValue, Modifier\)') },
    @{ Name='TnPawn.GetService'; Patterns=@('Service = _Services.GetService\(Type\)', 'return Service', '_CurrentForm.GetService\(Type\)') },
    @{ Name='TnServiceContainer.FindServiceIndex'; Patterns=@('I = 0', '__NFUN_254__\(_ServiceDescriptions\[I\].Type, Type\)', 'return I', 'return -1') },
    @{ Name='TnServiceContainer.GetService'; Patterns=@('Index = FindServiceIndex\(Type\)', 'return _ServiceDescriptions\[Index\].Service', 'return none') },
    @{ Name='TnTransformerForm.GetService'; Patterns=@('Service = _Services.GetService\(Type\)', 'if\(__NFUN_114__\(Service, none\)\)', 'Service = OnGetService\(Type\)') },
    @{ Name='TnRobotForm.OnGetService'; Patterns=@('(?s)case ''TnBuffService'':\s*Service = _BuffManager;', '_Services.Add\(Type, Service\)') },
    @{ Name='TnVehicleForm.OnGetService'; Patterns=@('case ''TnAnimatorService''', 'case ''TnAdditiveAnimatorService''', 'case ''TnTargetableService''', '_Services.Add\(Type, Service\)'); Absent=@('case ''TnBuffService''') },
    @{ Name='TnAbility.GetCooldownTime'; Patterns=@('OwnerPawn.GetService\(''TnBuffService''\)', 'GetCurrentSkillDataIndex\(\)', 'BaseCooldown = Cooldown\[DataIndex\]', 'BaseCooldown = Cooldown\[0\]', 'BS.HasBuff\(Class''TransGame.TnBuffAbilityCooldown''\)', 'CooldownReductionModifier') },
    @{ Name='TnGame.GetCurrentSkillDataIndex'; Patterns=@('GetGameClass\(\)', 'static.GetSkillDataIndex\(\)') },
    @{ Name='TnGame.GetSkillDataIndex'; Patterns=@('return 1;') },
    @{ Package='Engine'; Name='WorldInfo.GetGameClass'; Patterns=@('return WorldInfo.Game.__NFUN_400__\(\)', 'return GRI.GameClass', 'return none') },
    @{ Name='TnBuffManager.GetBuff'; Patterns=@('foreach Buffs\(B\)', '__NFUN_114__\(B.__NFUN_400__\(\), BuffClass\)', 'return B', 'return none') },
    @{ Name='TnBuffManager.HasBuff'; Patterns=@('__NFUN_119__\(GetBuff\(BuffClass\), none\)') },
    @{ Name='TnAbility.GetResourceRequired'; Patterns=@('__NFUN_155__\(Class''TransGame.TnGame''.static.GetCurrentSkillDataIndex\(\), 1\)', 'ResourceAmountModifier.ModifyFloat\(ResourceRequired, OwnerPawn\)') },
    @{ Name='TnPropertyBuffModifier.ModifyFloat'; Patterns=@('ModifyFloatInternal\(PropertyValue, BuffsWhichModifyProperty, PawnOwner, OptionalInfo\)') },
    @{ Name='TnValueTimesProductBuffModifier.InitializeFloatModifier'; Patterns=@('return 1.0000000;') },
    @{ Name='TnValueTimesProductBuffModifier.AggregateFloatModifier'; Patterns=@('return __NFUN_171__\(NewModifier, CurrentModifier\)') },
    @{ Name='TnValueTimesProductBuffModifier.ApplyFloatModifier'; Patterns=@('return __NFUN_171__\(Modifier, UntaintedValue\)') },
    @{ Name='TnBuff.ShouldBuffEffect'; Patterns=@('foreach Conditions\(Condition\)', 'if\(__NFUN_129__\(Condition.ShouldEffect\(P, OptionalInfo\)\)\)', 'return false', 'return true') },
    @{ Name='TnBuffFloatModifier.GetFloatModifierHelper'; Patterns=@('if\(__NFUN_150__\(BuffLevel, Modifiers.Length\)\)', 'return Modifiers\[BuffLevel\]', 'return Modifiers\[__NFUN_147__\(Modifiers.Length, 1\)\]') },
    @{ Name='TnBuffFloatModifier.GetSpFloatModifier'; Patterns=@('if\(__NFUN_154__\(FloatModifierSP.Length, 0\)\)', 'return GetFloatModifierHelper\(FloatModifier\)', 'return GetFloatModifierHelper\(FloatModifierSP\)') },
    @{ Name='TnBuffFloatModifier.GetFloatModifier'; Patterns=@('__NFUN_154__\(Class''TransGame.TnGame''.static.GetCurrentSkillDataIndex\(\), 0\)', 'return GetFloatModifierHelper\(FloatModifier\)', 'return GetSpFloatModifier\(\)') },
    @{ Name='TnBuffConditionDowned.ShouldEffect'; Patterns=@('return P._IsDowned;') },
    @{ Name='TnBuffConditionBehindTarget.ShouldEffect'; Patterns=@('if\(__NFUN_119__\(Actor\(OptionalInfo\), none\)\)', 'return false;') },
    @{ Name='TnBuffConditionTargetIsAi.ShouldEffect'; Patterns=@('return __NFUN_119__\(TnAiPawn\(OptionalInfo\), none\)') },
    @{ Name='TnBuffConditionTargetIsPlayer.ShouldEffect'; Patterns=@('return __NFUN_119__\(TnPlayerPawn\(OptionalInfo\), none\)') },
    @{ Name='TnBuffConditionTargetIsSentry.ShouldEffect'; Patterns=@('return __NFUN_119__\(TnSentryPawn\(OptionalInfo\), none\)') },
    @{ Name='TnHUD.SetAbilityResource'; Patterns=@('(?s)AddFloatArgument\(args, float\(AbilityNumber\)\);.*AddFloatArgument\(args, CooldownTime\);.*AddFloatArgument\(args, NormalizedCooldownStart\);.*HudMovie.Invoke\(UpdateAbilityCooldownCallBack, args\)') },
    @{ Name='TnHUD.SetAbilityResourceReady'; Patterns=@('(?s)AddFloatArgument\(args, float\(AbilityNumber\)\);.*AddFloatArgument\(args, -1.0000000\);.*AddFloatArgument\(args, 0.0000000\);.*HudMovie.Invoke\(UpdateAbilityCooldownCallBack, args\)') },
    @{ Package='Engine'; Name='Pawn.PossessedBy'; Patterns=@(
        'Controller = C', 'PlayerReplicationInfo = C.PlayerReplicationInfo',
        'UpdateControllerOnPossess\(bVehicleTransition\)', 'Weapon.ClientWeaponSet\(false\)') },
    @{ Name='TnAiPawn.PossessedBy'; Patterns=@(
        'super\(Pawn\).PossessedBy\(C, bVehicleTransition\)', '_BoundController = TnAiController\(C\)') },
    @{ Package='Engine'; Name='Weapon.ClientWeaponSet'; Patterns=@(
        'ClientWeaponSetTime =', 'TryToSetWeapon\(bOptionalSet\)') },
    @{ Package='Engine'; Name='Weapon.TryToSetWeapon'; Patterns=@(
        'PendingClientWeaponSet', 'InvManager.ClientWeaponSet\(self, bOptionalSet\)') },
    @{ Name='TnInventoryManager.ClientWeaponSet'; Patterns=@(
        'OldWeapon = Instigator.Weapon', 'LastClientWeaponSetTime = NewWeapon.ClientWeaponSetTime',
        'ShouldForceClientWeaponSet\(NewWeapon\)', "OldWeapon.__NFUN_281__\('Inactive'\)",
        '(?s)if\(__NFUN_114__\(OldWeapon, NewWeapon\)\)\s*\{\s*return;\s*\}') },
    @{ Name='TnInventoryManager.ShouldForceClientWeaponSet'; Patterns=@(
        'TnWeaponBombBase\(NewWeapon\)', 'TnWeaponFlagBase\(NewWeapon\)') },
    @{ Package='Engine'; Name='PlayerController.UnPossess'; Patterns=@(
        'Pawn.RemoteRole = ROLE_SimulatedProxy', 'Pawn.UnPossessed\(\)',
        'CleanOutSavedMoves\(\)', 'SetViewTarget\(self\)', 'Pawn = none') },
    @{ Package='Engine'; Name='Pawn.UnPossessed'; Patterns=@(
        'bForceNetUpdate = true', 'PlayerReplicationInfo = none', 'Controller = none') },
    @{ Name='TnAiController.UnPossess'; Patterns=@(
        'TacticalAnalyzer.RemoveNewEnemyObserver\(OnNewEnemy\)', 'super\(Controller\).UnPossess\(\)') },
    @{ Name='TnAiController.Possess'; Patterns=@(
        'super\(Controller\).Possess\(inPawn, bVehicleTransition\)',
        '_IconDrawer.Initialize\(get_TransformerPawn\(\)\)',
        'TacticalAnalyzer.AddNewEnemyObserver\(OnNewEnemy\)',
        'WeaponAimer.Initialize\(get_TransformerPawn\(\)\)',
        'WeaponShooter.Initialize\(get_TransformerPawn\(\)\)', '_RangeDetector.Initialize\(RangeSet\)') },
    @{ Name='TnAiController.get_TransformerPawn'; Patterns=@('return TnAiPawn\(Pawn\)') },
    @{ Name='TnAiWeaponAimer.Initialize'; Patterns=@(
        '_Owner = Owner;', '_OwnerController = TnAiController\(_Owner.Controller\)', 'TnPawn Owner') },
    @{ Name='TnAiWeaponShooter.Initialize'; Patterns=@('Owner = OwningPawn;', 'Pawn OwningPawn') },
    @{ Package='HM_Engine'; Name='HmPawnIconDrawer.Initialize'; Patterns=@('_OwnerPawn = OwnerPawn;', 'Pawn OwnerPawn') },
    @{ Name='TnTacticalAnalyzer.AddNewEnemyObserver'; Patterns=@(
        'NewEnemyObservers.Find\(N\)', 'NewEnemyObservers\[NewEnemyObservers.Length\] = N') },
    @{ Name='TnAiRangeDetector.Initialize'; Patterns=@(
        '__NFUN_155__\(RangeSet.Length, 8\)', 'Range = RangeSet;', 'FloatRange RangeSet') },
    @{ Name='TnAiWeaponShooter.Reset'; Patterns=@(
        '_Weapon.__ReloadCompleteCallback__Delegate = None', '_Weapon.__FiredCallback__Delegate = None',
        '_Weapon = none', 'ClearNotifies\(\)', '__NFUN_113__\(''Stopped''\)') },
    @{ Name='TnAiWeaponShooter.ClearNotifies'; Patterns=@(
        '__ShooterFailedNotify__Delegate = None', '__ReloadCompleteNotify__Delegate = None') },
    @{ Name='TnPlayerPawn.UnPossessed'; Patterns=@(
        'ClearInteractState\(\)', 'super\(Pawn\).UnPossessed\(\)') },
    @{ Name='TnPlayerPawn.ClearInteractState'; Patterns=@(
        'SetInteractState\(none, 0\)', 'ClearPlayerInteractState\(\)') },
    @{ Name='TnTacticalAnalyzer.RemoveNewEnemyObserver'; Patterns=@(
        'NewEnemyObservers.Find\(N\)', 'NewEnemyObservers.Remove\(Index, 1\)') },
    @{ Package='Engine'; Name='PlayerController.Possess'; Patterns=@(
        'aPawn.PossessedBy\(self, bVehicleTransition\)', 'Restart\(bVehicleTransition\)',
        'FireTouchEvents\(\)', 'FirePlayerSpawnEvents\(\)', 'Deleting both the pawn and the controller') },
    @{ Package='Engine'; Name='PlayerController.Restart'; Patterns=@(
        'EnterStartState\(\)', 'ClientRestart\(Pawn\)', 'SetViewTarget\(Pawn\)', 'ResetCameraMode\(\)') },
    @{ Package='Engine'; Name='PlayerController.ClientRestart'; Patterns=@(
        'ResetPlayerMovementInput\(\)', 'AcknowledgePossession\(Pawn\)', 'Pawn.ClientRestart\(\)') },
    @{ Package='Engine'; Name='PlayerController.AcknowledgePossession'; Patterns=@(
        'LocalPlayer\(Player\)', 'AcknowledgedPawn = P', 'P.SetBaseEyeheight\(\)',
        'P.EyeHeight = P.BaseEyeHeight', 'ServerAcknowledgePossession\(P\)') },
    @{ Package='Engine'; Name='PlayerController.ServerAcknowledgePossession'; Patterns=@(
        '__NFUN_114__\(P, Pawn\)', '__NFUN_119__\(P, AcknowledgedPawn\)', 'ResetTimeMargin\(\)', 'AcknowledgedPawn = P') },
    @{ Package='Engine'; Name='Pawn.SetBaseEyeheight'; Patterns=@(
        'bIsCrouched', 'BaseEyeHeight = GetOriginalBaseEyeheight\(\)', 'CrouchHeight') },
    @{ Name='TnPawn.GetOriginalBaseEyeheight'; Patterns=@(
        'return _Blueprint.BaseEyeHeight', 'super\(Pawn\).GetOriginalBaseEyeheight\(\)') },
    @{ Package='Engine'; Name='PlayerController.ResetPlayerMovementInput'; Patterns=@(
        'bIgnoreMoveInput = default.bIgnoreMoveInput', 'bIgnoreLookInput = default.bIgnoreLookInput') },
    @{ Package='Engine'; Name='Pawn.ClientRestart'; Patterns=@(
        'Velocity = vect\(0.0000000, 0.0000000, 0.0000000\)',
        'Acceleration = vect\(0.0000000, 0.0000000, 0.0000000\)') },
    @{ Name='TnPlayerController.EnterStartState'; Patterns=@('GetNewFormStateName\(CD.StartingForm\)') },
    @{ Name='TnPlayerController.get_TransformerPawn'; Patterns=@('return TnPlayerPawn\(Pawn\)') },
    @{ Name='TnPlayerController.GetNewFormStateName'; Patterns=@(
        'get_TransformerPawn\(\).get_VehicleType\(\)', 'return ''PlayerWalking''',
        'return ''PlayerInCarForm''', 'return ''PlayerInPlaneForm''', 'return ''PlayerInTankForm''') },
    @{ Package='Engine'; Name='PlayerController.ClientGotoState'; Patterns=@(
        '__NFUN_281__\(NewState\)', '__NFUN_113__\(NewState\)', '__NFUN_113__\(NewState, NewLabel\)') },
    @{ Name='TnPlayerController.UpdateInvertMouseByRobotForm'; Patterns=@(
        'PlayerInput.bInvertMouse = TnProfileSettings\(ProfileSettings\).GetInvertY_Robot\(\)') },
    @{ Name='TnPlayerController.UpdateInvertMouseByVehicleForm'; Patterns=@(
        'get_TransformerPawn\(\).get_VehicleType\(\)', 'GetInvertY_Car\(\)', 'GetInvertY_Plane\(\)', 'GetInvertY_Tank\(\)') },
    @{ Name='TnProfileSettings.GetInvertY_Robot'; Patterns=@('GetProfileSettingValueBool\(41, Value\)', 'return __NFUN_155__\(Value, 0\)') },
    @{ Name='TnProfileSettings.GetInvertY_Car'; Patterns=@('GetProfileSettingValueBool\(42, Value\)', 'return __NFUN_155__\(Value, 0\)') },
    @{ Name='TnProfileSettings.GetInvertY_Tank'; Patterns=@('GetProfileSettingValueBool\(43, Value\)', 'return __NFUN_155__\(Value, 0\)') },
    @{ Name='TnProfileSettings.GetInvertY_Plane'; Patterns=@('GetProfileSettingValueBool\(44, Value\)', 'return __NFUN_155__\(Value, 0\)') },
    @{ Name='TnClassFormStatHelper.SetForm'; Patterns=@(
        'CurrentTime = GetCurrentTime\(\)', 'switch\(ClassName\)',
        'AddToGameStat\(__NFUN_175__\(CurrentTime, LastFormChangeTime\), 336, 0\)',
        'LastFormChangeTime = CurrentTime', 'CurrentDistanceRegular = 0.0000000',
        'CurrentDistanceBoosting = 0.0000000', 'CurrentForm = NewForm') },
    @{ Name='TnPlayerReplicationInfo.PostBeginPlay'; Patterns=@('_ClassFormTracker.SetValueDumpDelegate\(IncrementGameStat\)') },
    @{ Name='TnClassFormStatHelper.SetValueDumpDelegate'; Patterns=@('__AddToGameStat__Delegate = AddValueDelegate') },
    @{ Name='TnPlayerController.GotoNewFormState'; Patterns=@(
        '__NFUN_113__\(GetNewFormStateName\(TargetForm\)\)', 'ClientGotoState\(GetNewFormStateName\(TargetForm\)\)') },
    @{ Name='TnPlayerController.PlayerWalking.BeginState'; Patterns=@(
        'DoubleClickDir = 0', 'Pawn.SetPhysics\(1\)', '_CameraStrategyManager.StartForm\(0\)',
        'UpdateInvertMouseByRobotForm\(\)', 'PRI._ClassFormTracker.SetForm\(0\)') },
    @{ Name='TnPlayerController.PlayerWalking.EndState'; Patterns=@('StopFineAim\(\)', '_PressedHover = false', 'Pawn.StopFire\(0\)') },
    @{ Name='TnPlayerController.PlayerInVehicleForm.BeginState'; Patterns=@(
        'CleanOutSavedMoves\(\)', 'UpdateInvertMouseByVehicleForm\(\)', 'PRI._ClassFormTracker.SetForm\(1\)',
        'OnStartBoost\(\)', '_CameraStrategyManager.StartForm\(1\)') },
    @{ Name='TnPlayerController.PlayerInVehicleForm.EndState'; Patterns=@('ClearTimer\(''DisplayVehicleTutorialMessage''\)', 'CleanOutSavedMoves\(\)') },
    @{ Name='TnPlayerController.PlayerInCarForm.BeginState'; Patterns=@('CarForm = TnCarForm\(get_TransformerPawn\(\)._VehicleForm\)', 'super.BeginState') },
    @{ Name='TnPlayerController.PlayerInPlaneForm.BeginState'; Patterns=@('PlaneForm = TnPlaneForm\(get_TransformerPawn\(\)._VehicleForm\)', 'super.BeginState') },
    @{ Name='TnPlayerController.PlayerInTankForm.BeginState'; Patterns=@('TankForm = TnTankForm\(get_TransformerPawn\(\)._VehicleForm\)', 'super.BeginState') },
    @{ Name='TnPlayerController.PlayerInCarForm.EndState'; Patterns=@('CarForm._InputState', 'ClearInputs\(\)', 'SetRemoteInputs\(\)', 'super.EndState') },
    @{ Name='TnPlayerController.PlayerInPlaneForm.EndState'; Patterns=@('PlaneForm._InputState', 'ClearInputs\(\)', 'SetRemoteInputs\(\)', 'super.EndState') },
    @{ Name='TnPlayerController.PlayerInTankForm.EndState'; Patterns=@('TankForm._InputState', 'ClearInputs\(\)', 'SetRemoteInputs\(\)', 'super.EndState') },
    @{ Name='TnPlayerController.PlayerWalking.ClearInputs'; Patterns=@(
        'EndBlock\(\)', 'AbortMelee\(\)', 'StopFineAim\(\)', 'global.ClearInputs\(\)') },
    @{ Name='TnPlayerController.PlayerInTankForm.ClearInputs'; Patterns=@('TankForm._InputState.Reset\(\)', 'super.ClearInputs\(\)') },
    @{ Name='TnPlayerController.PlayerInPlaneForm.ClearInputs'; Patterns=@('PlaneForm._InputState.Reset\(\)', 'super.ClearInputs\(\)') },
    @{ Name='TnPlayerController.PlayerInCarForm.ClearInputs'; Patterns=@('CarForm._InputState.Reset\(\)', 'super.ClearInputs\(\)') },
    @{ Name='TnPlayerController.ClearInputs'; Patterns=@('(?s)StopDeferredFire\(\);\s*IsBoostPressed = false;') },
    @{ Name='TnPlayerController.PlayerInVehicleForm.ClearInputs'; Patterns=@('(?s)StopBoost\(\);\s*super\(TnPlayerController\).ClearInputs\(\);') },
    @{ Name='TnPlayerController.StopDeferredFire'; Patterns=@('(?s)StopFire\(\);\s*bDeferredFire = false;\s*IsAllowedToFire = true;') },
    @{ Name='TnPlayerController.EndBlock'; Patterns=@('TP = get_TransformerPawn\(\)', 'TP.GetService\(''TnShieldService''\)', 'ShieldService.StopShield\(\)') },
    @{ Name='TnPlayerController.AbortMelee'; Patterns=@('PlayerPawn = TnPlayerPawn\(Pawn\)', 'PlayerPawn.GetRobotService\(''TnMeleeService''\)', 'MeleeService.AbortMeleeAttack\(\)') },
    @{ Name='TnPlayerController.StopBoost'; Patterns=@('(?s)IsBoostPressed = false;\s*OnStopBoost\(\);') },
    @{ Name='TnPlayerController.PlayerInCarForm.OnStopBoost'; Patterns=@('CarForm.set_BoostingInput\(false\)') },
    @{ Name='TnPlayerController.PlayerInPlaneForm.OnStopBoost'; Patterns=@('PlaneForm.set_BoostingInput\(false\)') },
    @{ Name='TnPlayerController.PlayerInTankForm.OnStopBoost'; Patterns=@('TankForm.set_BoostingInput\(false\)') },
    @{ Name='TnFineAimManager.Initialize'; Patterns=@('Owner = P;', 'CameraStrategyManager = CSM;', 'PlayerTargeting = pt;') },
    @{ Name='TnCameraStrategyManager.set_CameraOwner'; Patterns=@(
        '_TransformerPawn = Value', 'InitializeForms\(\)', 'set_CameraStrategy\(_CameraStrategy\)') },
    @{ Name='TnCameraStrategyManager.InitializeForms'; Patterns=@(
        '_TankForm = none', '_CarForm = none', '_PlaneForm = none',
        '_CarForm = TnCarForm\(_TransformerPawn._VehicleForm\)',
        '_TankForm = TnTankForm\(_TransformerPawn._VehicleForm\)',
        '_PlaneForm = TnPlaneForm\(_TransformerPawn._VehicleForm\)') },
    @{ Name='TnCameraStrategyManager.StartForm'; Patterns=@(
        '__NFUN_113__\(''None''\)', '__NFUN_113__\(''RobotForm''\)',
        '__NFUN_113__\(''CarFormHovering''\)', '__NFUN_113__\(''TankForm''\)',
        '__NFUN_113__\(''PlaneFormHovering''\)') },
    @{ Name='TnCameraStrategyManager.RobotForm.BeginState'; Patterns=@('set_CameraStrategy\(Class''TransGame.TnOverTheShoulderCameraStrategyType''\)') },
    @{ Name='TnCameraStrategyManager.CarFormHovering.BeginState'; Patterns=@('set_CameraStrategy\(Class''TransGame.TnHoveringCameraStrategyType''\)') },
    @{ Name='TnCameraStrategyManager.TankForm.BeginState'; Patterns=@('set_CameraStrategy\(Class''TransGame.TnDrivingCameraStrategyType''\)') },
    @{ Name='TnCameraStrategyManager.PlaneFormHovering.BeginState'; Patterns=@('set_CameraStrategy\(Class''TransGame.TnDrivingCameraStrategyType''\)') },
    @{ Name='TnCarForm.get_VehicleType'; Patterns=@('return 0;') },
    @{ Name='TnTruckForm.get_VehicleType'; Patterns=@('return 1;') },
    @{ Name='TnPlaneForm.get_VehicleType'; Patterns=@('return 2;') },
    @{ Name='TnTankForm.get_VehicleType'; Patterns=@('return 3;') },
    @{ Name='TnPickupManager.Initialize'; Patterns=@('OwnerPawn = OwnerP;', 'ContextualCommander = ContextualCommander_;', 'TnContextualCommandHandler ContextualCommander_') },
    @{ Package='HM_Engine'; Name='HmPlayerTargeting.set_TargetingOwner'; Patterns=@(
        '(?s)\{\s*local @NULL Value;\s*_TargetingOwner = Value;\s*//return;\s*\}') },
    @{ Name='TnCharacterScriptBinding.CommitCharacter'; Patterns=@(
        'Character\.WeaponTypes\[0\] = WeaponTypesTemp\[0\]',
        'Character\.WeaponTypes\[1\] = WeaponTypesTemp\[1\]',
        'Character\.Abilities = Class''Core.Strings''\.static.Split',
        'Character\.Skills = Class''Core.Strings''\.static.Split') },
    @{ Name='TnCharacterScriptBinding.GetCharacterWeaponTypes'; Patterns=@('Character\.WeaponTypes.Insert\(2, 1\)') },
    @{ Name='TnCharacterDataHelpers.ReplicateCharacterData'; Patterns=@(
        'CDOut\.Abilities.AddItem\(none\)', 'CDOut\.SkillLevels.AddItem') },
    @{ Name='TnCharacterDataHelpers.VerifyCharacterData'; Patterns=@('No Grenades found') },
    @{ Name='TnCharacterApplier.ApplyWeapons'; Patterns=@(
        'PC\.HasCheckpointOverridePlayerData\(\)',
        'RobotWeaponClasses = PC\._CheckpointOverrideCharacterData.WeaponTypes') },
    @{ Name='TnAiController.InitializeFromTemplate'; Patterns=@(
        'get_TransformerPawn\(\).ApplyCharacter\(AiC.AiCharacterDataResolved\)',
        'get_TransformerPawn\(\).InvManager.DestroyInventory\(\)') },
    @{ Name='TnPlayerController.OnSetPawn'; Patterns=@('PlayerPawn = TnPlayerPawn\(aPawn\)', '_PickupManager.Initialize\(PlayerPawn') },
    @{ Name='TnAiSquad.RestartGame'; Patterns=@('CharacterController = none', 'ShouldSpawnFromCheckpoint = true') },
    @{ Name='TnAiController.RequestReplan'; Patterns=@(
        'PlanRunner\._EvaluateCurrentGoalOnReplan = true',
        'static.RequestReplan\(PlanRunner, Priority\)') },
    @{ Name='TnAiPlanningScheduler.SubmitRequest'; Patterns=@(
        '_Activated', 'PlanRunner.get_IsSuspended\(\)', 'FindRequest\(PlanRunner, 1\)',
        'LowIndex = FindRequest\(PlanRunner, 0\)', 'RemoveRequest\(LowIndex, 0\)',
        'AddRequest\(PlanRunner, Priority\)') },
    @{ Name='TnAiPlanningScheduler.Update'; Patterns=@(
        'PlanRunner.TryBetterPlan\(\)', '_MaxProcessingTimeMs') },
    @{ Package='Engine'; Name='Weapon.SetData'; Patterns=@('WD, none', 'Data = WD', 'UpdateData\(\)') },
    @{ Name='TnWeapon.SetWeaponDataType'; Patterns=@('WeaponDataType = Type', 'SetData\(GetWeaponDataByType\(Type\)\)') },
    @{ Package='HM_Engine'; Name='HmWeapon.GetMaxAmmoClipCount'; Patterns=@('return HmWeaponData\(Data\).MaxAmmoClipCount') },
    @{ Package='HM_Engine'; Name='HmWeapon.GetMaxAmmoCount'; Patterns=@('return HmWeaponData\(Data\).MaxAmmoCount') },
    @{ Name='TnPawn.HasSkill'; Patterns=@('_CurrentCharacter.Skills.Find\(Skill\)') },
    @{ Name='TnWeapon.InitializeFromInstigator'; Patterns=@('TransformerPawn.HasSkill\(IncreaseAmmoSkill\)', 'IsEffectedByIncreaseAmmoSkill', 'AmmoIncrease = IncreaseAmmoSkill.static.GetAmmoIncrease\(self\)') },
    @{ Name='TnSkillIncreaseAmmo.GetAmmoIncrease'; Patterns=@('return 0') },
    @{ Name='TnSkillExtraGrenade.GetAmmoIncrease'; Patterns=@('return 1') },
    @{ Name='TnSkillIncreaseAmmoFromClips.GetAmmoIncrease'; Patterns=@('__NFUN_144__\(2, W.GetMaxAmmoClipCount\(\)\)') },
    @{ Package='HM_Engine'; Name='HmValueLerper.Initialize'; Patterns=@('StartValue = Start', 'EndValue = End', 'CurTime = 0.0000000') },
    @{ Package='HM_Engine'; Name='HmValueLerper.SetRampTime'; Patterns=@(
        '__NFUN_180__\(RampTime, float\(0\)\)', 'NormalizedTime = __NFUN_172__\(CurTime, RampTime\)',
        'RampTime = NewRampTime', 'CurTime = __NFUN_171__\(RampTime, NormalizedTime\)') },
    @{ Package='HM_Engine'; Name='HmValueLerper.GetValue'; Patterns=@(
        '__NFUN_180__\(RampTime, float\(0\)\)', 'return StartValue', '__NFUN_172__\(CurTime, RampTime\)') },
    @{ Package='HM_Engine'; Name='HmValueLerper.Update'; Patterns=@(
        '__NFUN_184__\(CurTime, DeltaTime\)', '__NFUN_246__\(CurTime, 0.0000000, RampTime\)') },
    @{ Name='TnWeapon.InitializeFireInterval'; Patterns=@(
        'FireIntervalLerper.Initialize\(WD.FireIntervalModifier.IntervalRange.Min, WD.FireIntervalModifier.IntervalRange.Max\)',
        'FireIntervalLerper.SetRampTime\(WD.FireIntervalModifier.RampTime\)') },
    @{ Name='TnWeaponBeam.UpdateData'; Patterns=@('BeamData = TnWeaponDataBeam\(Data\)', 'super.UpdateData\(\)') },
    @{ Name='TnWeaponGrenadeLauncherBase.UpdateData'; Patterns=@('GrenadeData = TnWeaponDataGrenadeLauncher\(Data\)', 'super.UpdateData\(\)') },
    @{ Name='TnWeaponRepair.UpdateData'; Patterns=@('RepairData = TnWeaponDataRepair\(Data\)', 'super.UpdateData\(\)') },
    @{ Name='TnGrenadeBag.UpdateData'; Patterns=@('GrenadeBagData = TnWeaponDataGrenadeBag\(Data\)', 'super.UpdateData\(\)') },
    @{ Name='TnWeapon.IsHeatBased'; Patterns=@('return __NFUN_177__\(TnWeaponData\(Data\).HeatProperties.HeatMax, float\(0\)\)') },
    @{ Name='TnGrenadeBag.Equip'; Patterns=@('WeaponMeshTemplate.Clone\(self\)', 'GrenadeBagMesh.SetWeaponOwner\(GrenadeOwner\)',
        'GrenadeThrower.Initialize\(TnPawn\(GrenadeOwner\), GrenadeBagData\)', 'GrenadeThrower.TossAnimation.AnimName = CustomTossAnimation') },
    @{ Name='TnGrenadeThrower.Initialize'; Patterns=@('ThrowerPawn = Thrower', 'GrenadeData = Data'); Absent=@('Clone','Spawn','__NFUN_280__') },
    @{ Name='TnGrenadeThrower.ServerToss'; Patterns=@('TargetLocation = TargetLocation_',
        'EnqueueReplicatedAnimation\(TossAnimation', 'ThrowerPawn.__NFUN_280__\(TossDelay, false, ''SpawnGrenade'', self\)') },
    @{ Name='TnGrenadeThrower.SpawnGrenade'; Patterns=@('ThrowerPawn.Spawn\(GrenadeData.WeaponProjectiles\[0\]', 'GrenadeData.TossStrength', 'Grenade.Init\(TossVelocity\)') },
    @{ Package='Engine'; Name='Actor.TimerData'; Patterns=@('Object TimerObj', 'name FuncName', 'float Rate', 'float Count', 'bool bLoop') },
    @{ Name='TnWeapon.UpdateData'; Patterns=@(
        'CurrentSpread = GetInitialSpread\(\)', 'InitializeFireInterval\(\)',
        'WeaponHeat.ResetHeatProperties\(HeatProps, TransformerPawn', 'InitializeFromInstigator\(\)', 'SetPickupMeshes\(TnWeaponMesh\(WeaponMeshTemplate\)\)') },
    @{ Name='TnWeaponHeat.ResetHeatProperties'; Patterns=@(
        'SetHeatProperties\(Properties, WeaponOwner, RunModifiers\)', '__NFUN_113__\(''Cool''\)') },
    @{ Name='TnWeaponHeat.SetHeatProperties'; Patterns=@(
        'OwnerPawn = WeaponOwner', 'HeatProperties.HeatMax = Properties.HeatMax', 'HeatProperties.Modifiers.Length = 0',
        'CurrentMod.Clone\(self\)', '.Initialize\(TnPawn\(WeaponOwner\)\)') },
    @{ Name='TnWeaponHeat.SyncCurrentHeat'; Patterns=@('CurrentHeat = SyncHeat', 'ClampHeatAndNotify\(\)') },
    @{ Name='TnHeatModifier.Initialize'; Patterns=@('WeaponOwner = P') },
    @{ Name='TnWeaponHeat.EWeaponHeatEventType'; Patterns=@(
        'WHET_StartFire,\s*// 0', 'WHET_StopFire,\s*// 1', 'WHET_Overheat,\s*// 5',
        'WHET_EndOverheat,\s*// 6', 'WHET_FullyCharged,\s*// 7', 'WHET_LoadShot,\s*// 8', 'WHET_MAX\s*// 9') },
    @{ Name='TnChargeHeatModifier.OnHeatEvent'; Patterns=@(
        'case 7:', 'IsCharging = true', 'case 1:', 'IsCharging = false',
        'case 4:', '__NFUN_171__\(GainRate, DeltaTime\)', 'IsOverheated = true', 'IsOverheated = false') },
    @{ Name='TnHoldHeatModifier.OnHeatEvent'; Patterns=@(
        'case 0:', 'IsHoldingFire = true', 'case 1:', 'IsHoldingFire = false',
        'case 4:', '__NFUN_171__\(GainRate, DeltaTime\)', 'IsOverheated = true', 'IsOverheated = false') },
    @{ Name='TnLoseHeatModifier.OnHeatEvent'; Patterns=@(
        '__NFUN_184__\(TimeSinceLastShot, DeltaTime\)', 'TimeSinceLastShot = 0.0000000',
        '__NFUN_175__\(TimeSinceLastShot, LossDelay\)', '__NFUN_171__\(LossRate,',
        'IsHoldingFire = true', 'IsHoldingFire = false', 'IsOverheated = true', 'IsOverheated = false') },
    @{ Name='TnOverheatHeatModifier.OnHeatEvent'; Patterns=@(
        '__NFUN_185__\(TimeLeftOnOverheat, DeltaTime\)', '__NFUN_178__\(TimeLeftOnOverheat, float\(0\)\)',
        'Heat = 0.0000000', 'TimeLeftOnOverheat = OverheatDelay', 'TimeLeftOnOverheat = 0.0000000'); Absent=@('__NFUN_246__','SetTimer') },
    @{ Name='TnPerShotHeatModifier.OnHeatEvent'; Patterns=@(
        'int\(EventTypeToAddOn\), int\(Type\)', '__NFUN_184__\(Heat, AddPerShot\)') },
    @{ Name='TnWeapon.Tick'; Patterns=@('IsHeatBased\(\)', 'WeaponHeat.OnWeaponHeatEvent\(4, DeltaTime\)') },
    @{ Name='TnWeapon.EndFire'; Patterns=@('WeaponHeat.OnWeaponHeatEvent\(1\)', 'super\(Weapon\).EndFire\(FireModeNum\)') },
    @{ Name='TnWeaponHeat.ClampHeatAndNotify'; Patterns=@(
        'HeatMax = GetHeatMax\(\)', 'CurrentHeat = Class''Core.Math''.static.__NFUN_246__\(CurrentHeat, 0.0000000, HeatMax\)',
        'CheckForStateChange\(HeatMax\)') },
    @{ Name='TnWeaponHeat.Cool.CheckForStateChange'; Patterns=@('__NFUN_179__\(CurrentHeat, HeatMax\)', '__NFUN_113__\(''Overheated''\)') },
    @{ Name='TnWeaponHeat.Overheated.CheckForStateChange'; Patterns=@('__NFUN_176__\(CurrentHeat, HeatMax\)', '__NFUN_113__\(''Cool''\)') },
    @{ Name='TnWeaponHeat.Cool.BeginState'; Patterns=@('NotifyWeaponCooledObservers\(\)') },
    @{ Name='TnWeaponHeat.Overheated.BeginState'; Patterns=@('NotifyWeaponOverheatedObservers\(\)') },
    @{ Name='TnWeaponHeat.NotifyWeaponCooledObservers'; Patterns=@('I, WeaponCooledObservers.Length', 'N = WeaponCooledObservers\[I\]', 'WeaponCooledObserver\(\)') },
    @{ Name='TnWeaponHeat.NotifyWeaponOverheatedObservers'; Patterns=@('I, WeaponOverheatedObservers.Length', 'N = WeaponOverheatedObservers\[I\]', 'WeaponOverheatedObserver\(\)') },
    @{ Name='TnWeapon.Overheated.WeaponCooled'; Patterns=@('OwnerAnimator.OnEndOverheat\(OverheatEndComplete\)', 'OverheatEndComplete\(\)') },
    @{ Name='TnWeapon.GivenTo'; Patterns=@('TnPlayerPawn\(ThisPawn\)', 'DesiredWeaponDataType\(\)', 'WDT = 1', 'WDT = 2', 'SetWeaponDataType\(WDT\)') },
    @{ Name='TnPawnCollisionController.SetRBCollidesWithChannel'; Patterns=@(
        '_RobotMeshComponent.SetRBCollidesWithChannel\(Channel, Value\)', '_VehicleMeshComponent, none',
        '_VehicleMeshComponent.SetRBCollidesWithChannel\(Channel, Value\)'); Absent=@('SetComponentRBFixed','UpdateCollisionState','SetupTeamBlockingChannels') },
    @{ Name='TnPawnCollisionController.Initialize'; Patterns=@(
        '_Owner = Owner_', '_RobotMeshComponent = RobotMeshComponent', '_VehicleMeshComponent = VehicleMeshComponent',
        'IsPlayerPawn = __NFUN_119__\(TnPlayerPawn\(_Owner\), none\)', 'SetRBCollidesWithChannel\(15, IsPlayerPawn\)') },
    @{ Package='Engine'; Name='PrimitiveComponent.ERBCollisionChannel'; Patterns=@(
        'RBCC_PlayerOnlyBlockingVolume,\s*// 15', 'RBCC_Team0BlockingVolume,\s*// 16', 'RBCC_MAX\s*// 21') },
    @{ Name='TnPointOfInterestManager.Reset'; Patterns=@(
        'Remove\(ActivePointOfInterest\)', '_Command.PointOfInterest = none') },
    @{ Name='TnPointOfInterestManager.Remove'; Patterns=@(
        'PointOfInterest, ActivePointOfInterest', 'StopFocus\(\)',
        'Owner.ContextualCommander.UnregisterContextualCommand', 'ActivePointOfInterest = none'); Absent=@('_Command.PointOfInterest =') },
    @{ Name='TnPointOfInterestManager.Add'; Patterns=@(
        'ActivePointOfInterest = PointOfInterest', '_Command.PointOfInterest = PointOfInterest',
        'Owner.ContextualCommander.RegisterContextualCommand\(_Command\)') },
    @{ Name='TnPointOfInterestManager.StopFocus'; Patterns=@(
        'CameraStrategyManager = Owner._CameraStrategyManager', 'CameraStrategyManager.StopFocus\(\)', 'bFocusing = false') },
    @{ Name='TnCameraStrategyManager.StopFocus'; Patterns=@(
        '_Camera.RemovePlayerCameraState\(Class''TransGame.TnPCS_FocusingOnPointOfInterest''\)', '_Camera.RemoveFovSettings\(\)') },
    @{ Name='TnCamera.RemovePlayerCameraState'; Patterns=@('PlayerCameraStates.RemoveItem\(PCS\)') },
    @{ Name='TnCamera.RemoveFovSettings'; Patterns=@('bUseFovSettings = false') },
    @{ Name='TnContextualCommandHandler.UnregisterContextualCommand'; Patterns=@(
        'UINotifications\[I\].Command.__NFUN_197__\(CommandClass.Name\)',
        'UINotifications.Remove\(__NFUN_166__\(I\), 1\)', 'Index = FindCommandByClass\(CommandClass\)',
        'PushUINotification\(ContextualCommands\[Index\], 1\)', 'ContextualCommands.Remove\(Index, 1\)') },
    @{ Name='TnContextualCommandHandler.FindCommandByClass'; Patterns=@(
        'ContextualCommands\[I\].__NFUN_400__\(\), CommandClass', 'return I', 'return -1') },
    @{ Name='TnContextualCommandHandler.PushUINotification'; Patterns=@(
        'Notification.Command = Command', 'Notification.Action = Action', 'UINotifications.AddItem\(Notification\)') },
    @{ Name='TnDialogManager.Initialize'; Patterns=@(
        '(?s)local @NULL _OwnerPawn;\s*OwnerPawn = _OwnerPawn;\s*//return;') },
    @{ Name='TnDialogManagerPlayer.InitializeWithPlayerPawn'; Patterns=@(
        '(?s)Initialize\(_OwnerPawn\);\s*OwnerPlayerPawn = _OwnerPawn;\s*//return;') },
    @{ Name='TnDialogManagerAi.InitializeWithAiPawn'; Patterns=@(
        '(?s)Initialize\(_OwnerPawn\);\s*OwnerAiPawn = _OwnerPawn;\s*//return;') },
    @{ Name='TnDialogManagerAi.DialogNotifyFromTeammate'; Patterns=@(
        'TeammateAiPawn = TnAiPawn\(TeammatePawn\)', 'OwnerAiPawn.HasSameKillTarget\(TeammateAiPawn\)') },
    @{ Package='Engine'; Name='Actor.BecomeViewTarget'; Patterns=@(
        '(?s)protected event BecomeViewTarget\(\)\s*\{\s*local @NULL PC;\s*//return;\s*\}') },
    @{ Package='Engine'; Name='Actor.EndViewTarget'; Patterns=@(
        '(?s)protected event EndViewTarget\(\)\s*\{\s*local @NULL PC;\s*//return;\s*\}') },
    @{ Package='Engine'; Name='Camera.ViewTargetTransitionParams'; Patterns=@(
        'BlendTime=0.0000000', 'BlendFunction=VTBlend_Cubic', 'BlendExp=2.0000000') },
    @{ Package='Engine'; Name='PlayerController.ClientSetViewTarget'; Patterns=@(
        'ServerVerifyViewTarget\(\)', 'SetViewTarget\(A, TransitionParams\)') },
    @{ Name='BecomeViewTarget'; Patterns=@('// MATCHED 0') },
    @{ Name='TnAiController.StopMovement'; Patterns=@(
        'get_TransformerPawn\(\).get_IsInVehicleForm\(\)', '_VehicleAiControl.StopMovement\(\)',
        'get_TransformerPawn\(\).Acceleration = vect\(0.0000000, 0.0000000, 0.0000000\)',
        'get_TransformerPawn\(\).DesiredRotation = get_TransformerPawn\(\).Rotation'); Absent=@('Velocity =', 'Destroy', 'Spawn') },
    @{ Name='TnVehicleAiControlStrategy.StopMovement'; Patterns=@(
        '_TransformerPawn._VehicleForm.set_BoostingInput\(false\)', '_StopMovement = true') },
    @{ Name='TnCarForm.set_BoostingInput'; Patterns=@('_InputState._Boosting = Value') },
    @{ Name='TnPlaneForm.set_BoostingInput'; Patterns=@('_InputState._Boosting = Value') },
    @{ Name='TnTankForm.set_BoostingInput'; Patterns=@('_InputState._Boosting = Value') },
    @{ Name='TnPawn.get_IsInVehicleForm'; Patterns=@('_CurrentForm, none', '_CurrentForm.get_FormType\(\)', 'int\(1\)') },
    @{ Name='TnAiController.MovingToPosition.CancelMove'; Patterns=@('PopState\(\)') },
    @{ Name='TnAiController.MovingToActor.CancelMove'; Patterns=@('PopState\(\)') },
    @{ Name='TnAiController.MovingToPosition.PoppedState'; Patterns=@(
        'PathFocus = none', '__MoveFailedNotify__Delegate = None', '__NearDestinationNotify__Delegate = None') },
    @{ Name='TnAiController.MovingToActor.PoppedState'; Patterns=@(
        'PathFocus = none', '__MoveFailedNotify__Delegate = None', '__NearDestinationNotify__Delegate = None') }
)
foreach ($contract in $behavior) {
    $package = if ($contract.ContainsKey('Package')) { $contract.Package } else { 'TransGame' }
    $dump = (& $Dumper (Join-Path $PackageDirectory ($package + '.xxx')) $contract.Name) -join "`n"
    if ($LASTEXITCODE -ne 0 -or $dump -match 'DECOMPILE FAILED') { throw "Behavior dump failed: $($contract.Name)" }
    foreach ($pattern in $contract.Patterns) {
        if ($dump -notmatch $pattern) { throw "Native behavior changed: $($contract.Name): $pattern" }
    }
    if ($contract.ContainsKey('Absent')) {
        foreach ($pattern in $contract.Absent) {
            if ($dump -match $pattern) { throw "Unsupported native override appeared: $($contract.Name): $pattern" }
        }
    }
}
Write-Host "Verified $($behavior.Count) native campaign/swap behavior contracts."
