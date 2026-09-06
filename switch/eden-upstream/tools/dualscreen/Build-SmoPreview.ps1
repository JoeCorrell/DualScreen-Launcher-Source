param(
    [Parameter(Mandatory=$true)][string]$SourceDirectory,
    [Parameter(Mandatory=$true)][string]$OutputDirectory,
    [Parameter(Mandatory=$true)][string]$SwitchToolboxPath,
    [string[]]$Models = @('Mario','MarioFace','MarioEye','MarioHandL','MarioHandR','MarioHead','MarioCaptainHead'),
    [string]$Plan,
    [switch]$Resume
)
$ErrorActionPreference = 'Stop'
$SourceDirectory = [IO.Path]::GetFullPath($SourceDirectory)
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
$metadata = Get-Content -LiteralPath (Join-Path $SourceDirectory 'native-parts.json') -Raw -Encoding UTF8 | ConvertFrom-Json
if ($Plan) { $Models = @( (Get-Content -LiteralPath $Plan -Raw | ConvertFrom-Json).models ) }
$converter = Join-Path $PSScriptRoot '../botw-companion/Convert-BotwModel.ps1'
$animator = Join-Path $PSScriptRoot '../botw-companion/Export-BotwPreviewAnimations.ps1'
foreach ($model in $Models) {
    $output = Join-Path $OutputDirectory $model
    if ($Resume -and (Test-Path -LiteralPath (Join-Path $output 'complete.json'))) { continue }
    $entry = $metadata.$model.metadata
    $init = $entry.'InitModel.byml'
    if ($null -eq $init) { $init = $entry.'InitModelPauseMenu.byml' }
    # PlayerFunction's common body initializer (base 0x444028) explicitly pairs
    # ObjectData/<costume> with ObjectData/PlayerAnimation. Costume bodies such
    # as MarioTailCoat deliberately have no InitModel.byml.
    if (-not $init.AnimArc -and $entry.'InitPartsFixInfoHead.byml') {
        if ($null -eq $init) { $init = [pscustomobject]@{} }
        $init | Add-Member -NotePropertyName AnimArc -NotePropertyValue 'PlayerAnimation' -Force
    }
    $source = Join-Path $SourceDirectory "$model.bfres"
    $texture = if ($init.TextureArc) { Join-Path $SourceDirectory "$($init.TextureArc).bfres" } else { $source }
    $animation = if ($init.AnimArc) { Join-Path $SourceDirectory "$($init.AnimArc).bfres" } else { $null }
    # Separate low-memory processes avoid accumulating Toolbox model/texture owners.
    $poseArgs = @()
    if ($animation) { $poseArgs = @('-AnimationBfres', $animation, '-Pose', 'Wait', '-PoseFrame', '0') }
    $materialArgs = @()
    if ($model -like 'Mario64Metal*' -or $model -like 'MarioColorGold*') { $materialArgs = @('-AllowUntexturedMaterials') }
    & powershell -NoProfile -ExecutionPolicy Bypass -File $converter -ModelInput $source -TextureInput $texture -ModelName $model -OutputDirectory $output -SwitchToolboxPath $SwitchToolboxPath @poseArgs @materialArgs -Force
    if ($LASTEXITCODE -ne 0) { throw "Preview conversion failed: $model" }
    if ($animation) {
    & powershell -NoProfile -ExecutionPolicy Bypass -File $animator -ModelBfres $source -AnimationBfres $animation -SwitchToolboxPath $SwitchToolboxPath -OutputPath (Join-Path $output 'idle.json') -ModelName $model -ClipName Wait -SourceFps 60
    if ($LASTEXITCODE -ne 0) { throw "Preview animation failed: $model" }
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'Inspect-SmoBfres.ps1') -InputPath $animation -ToolboxPath $SwitchToolboxPath -VisibilityOutput (Join-Path $output 'visibility.json')
    if ($LASTEXITCODE -ne 0) { throw "Preview visibility failed: $model" }
    }
    # No AnimArc means a native rigid/static part, not a missing body animation.
    @{ model=$model; animated=[bool]$animation } | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $output 'complete.json') -Encoding UTF8
}
