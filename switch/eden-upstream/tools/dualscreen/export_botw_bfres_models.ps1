param(
    [Parameter(Mandatory = $true)][string] $ManifestPath,
    [Parameter(Mandatory = $true)][string] $ResourceRoot,
    [Parameter(Mandatory = $true)][string] $ToolboxRoot,
    [Parameter(Mandatory = $true)][string] $OutputRoot,
    [string[]] $ModelName = @(),
    [string] $Python = 'python',
    [string] $PackerPath = '',
    [switch] $Force
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$ManifestPath = (Resolve-Path -LiteralPath $ManifestPath).Path
$ResourceRoot = (Resolve-Path -LiteralPath $ResourceRoot).Path
$ToolboxRoot = (Resolve-Path -LiteralPath $ToolboxRoot).Path
if (-not $PackerPath) {
    $PackerPath = Join-Path $PSScriptRoot 'pack_gltf_glb.py'
}
$PackerPath = (Resolve-Path -LiteralPath $PackerPath).Path
[void][IO.Directory]::CreateDirectory($OutputRoot)
$OutputRoot = (Resolve-Path -LiteralPath $OutputRoot).Path

$savedDirectory = [Environment]::CurrentDirectory
$savedPath = $env:PATH
$binding = [Reflection.BindingFlags]'Instance,NonPublic'

function Open-BotwResource([string] $Path) {
    $input = [IO.File]::OpenRead($Path)
    try {
        $magicBytes = New-Object byte[] 4
        if ($input.Read($magicBytes, 0, 4) -ne 4) {
            throw "Short resource: $Path"
        }
        $input.Position = 0
        if ([Text.Encoding]::ASCII.GetString($magicBytes) -eq 'Yaz0') {
            $savedOutput = [Console]::Out
            try {
                [Console]::SetOut([IO.TextWriter]::Null)
                $stream = ([Toolbox.Library.Yaz0]::new()).Decompress($input)
            }
            finally {
                [Console]::SetOut($savedOutput)
            }
        }
        else {
            $stream = [IO.MemoryStream]::new()
            $input.CopyTo($stream)
        }
    }
    finally {
        $input.Dispose()
    }
    $stream.Position = 0
    return $stream
}

function Add-TextureResources(
    [Syroot.NintenTools.NSW.Bfres.ResFile] $Resource,
    [Collections.Generic.List[Toolbox.Library.STGenericTexture]] $Textures,
    [Collections.Generic.List[IDisposable]] $Streams,
    [Collections.Generic.List[object]] $Owners
) {
    foreach ($external in @($Resource.ExternalFiles)) {
        $data = $external.Data
        if ($null -eq $data -or $data.Length -lt 4) { continue }
        if ([Text.Encoding]::ASCII.GetString($data, 0, 4) -ne 'BNTX') { continue }
        $stream = [IO.MemoryStream]::new($data, $false)
        [void]$Streams.Add($stream)
        $container = [FirstPlugin.BNTX]::new()
        $savedOutput = [Console]::Out
        try {
            [Console]::SetOut([IO.TextWriter]::Null)
            $container.Load($stream)
        }
        finally {
            [Console]::SetOut($savedOutput)
        }
        [void]$Owners.Add($container)
        foreach ($texture in @($container.TextureList)) {
            if (-not ($Textures | Where-Object { $_.Text -eq $texture.Text })) {
                [void]$Textures.Add($texture)
            }
        }
    }
}

try {
    [Environment]::CurrentDirectory = $ToolboxRoot
    $runtime = Join-Path $ToolboxRoot 'runtimes\win-x64\native'
    $library = Join-Path $ToolboxRoot 'Lib'
    $env:PATH = "$runtime;$ToolboxRoot;$library;$savedPath"
    Add-Type -AssemblyName System.Windows.Forms
    Add-Type -AssemblyName System.Drawing
    [void][Reflection.Assembly]::LoadFrom((Join-Path $library 'OpenTK.dll'))
    $assemblies = @(
        Get-ChildItem -LiteralPath $library -File -Filter '*.dll'
        Get-ChildItem -LiteralPath $ToolboxRoot -File -Filter '*.dll'
    )
    for ($pass = 0; $pass -lt 3; $pass++) {
        foreach ($assembly in $assemblies) {
            try { [void][Reflection.Assembly]::LoadFrom($assembly.FullName) }
            catch [BadImageFormatException] { }
            catch { }
        }
    }

    $manifest = Get-Content -LiteralPath $ManifestPath -Raw | ConvertFrom-Json
    $models = @($manifest.models.PSObject.Properties | Sort-Object Name)
    if ($ModelName.Count -gt 0) {
        $wanted = [Collections.Generic.HashSet[string]]::new(
            [string[]]$ModelName,
            [StringComparer]::Ordinal
        )
        $models = @($models | Where-Object { $wanted.Contains($_.Name) })
        if ($models.Count -ne $wanted.Count) {
            $found = [Collections.Generic.HashSet[string]]::new(
                [string[]]@($models.Name),
                [StringComparer]::Ordinal
            )
            $missing = @($wanted | Where-Object { -not $found.Contains($_) })
            throw "Unknown model names: $([string]::Join(', ', $missing))"
        }
    }

    $saverType = [Toolbox.Library.AssimpSaver]
    $saveSkeleton = $saverType.GetMethod('SaveSkeleton', $binding)
    $saveMaterials = $saverType.GetMethod('SaveMaterials', $binding)
    $saveMeshes = $saverType.GetMethod('SaveMeshes', $binding)
    $writeExtraSkinning = $saverType.GetMethod('WriteExtraSkinningInfo', $binding)
    $progressField = $saverType.GetField('progressBar', $binding)
    $publicInstance = [Reflection.BindingFlags]'Instance,Public'
    $materialsProperty = [Bfres.Structs.FMDL].GetProperty('Materials', $publicInstance)
    $objectsProperty = [Bfres.Structs.FMDL].GetProperty('Objects', $publicInstance)
    $skeletonProperty = [Bfres.Structs.FMDL].GetProperty('GenericSkeleton', $publicInstance)
    $errors = [Collections.Generic.List[object]]::new()
    $completed = 0

    foreach ($modelProperty in $models) {
        $unit = $modelProperty.Name
        $folder = [string]$modelProperty.Value.folder
        $destination = Join-Path $OutputRoot $unit
        [void][IO.Directory]::CreateDirectory($destination)
        $daePath = Join-Path $destination "$unit.dae"
        $gltfPath = Join-Path $destination "$unit.gltf"
        $glbPath = Join-Path $destination "$unit.glb"
        if ([IO.File]::Exists($glbPath) -and -not $Force) {
            $completed++
            Write-Output "[$completed/$($models.Count)] Reusing $unit"
            continue
        }

        $streams = [Collections.Generic.List[IDisposable]]::new()
        $owners = [Collections.Generic.List[object]]::new()
        $progress = $null
        $context = $null
        try {
            $modelPath = Join-Path $ResourceRoot "Model\$folder.sbfres"
            $texturePath = Join-Path $ResourceRoot "Model\$folder.Tex.sbfres"
            if (-not [IO.File]::Exists($modelPath)) { throw "Missing $modelPath" }
            if (-not [IO.File]::Exists($texturePath)) { throw "Missing $texturePath" }

            $modelStream = Open-BotwResource $modelPath
            [void]$streams.Add($modelStream)
            $savedOutput = [Console]::Out
            try {
                [Console]::SetOut([IO.TextWriter]::Null)
                $modelResource = [Syroot.NintenTools.NSW.Bfres.ResFile]::new($modelStream, $true)
            }
            finally {
                [Console]::SetOut($savedOutput)
            }
            $rawModels = @($modelResource.Models)
            $rawModel = @($rawModels | Where-Object { $_.Name -eq $unit } | Select-Object -First 1)
            if ($rawModel.Count -eq 0 -and $rawModels.Count -eq 1) {
                $rawModel = @($rawModels[0])
            }
            if ($rawModel.Count -ne 1) {
                throw "Could not select $unit from BFRES models: $([string]::Join(', ', @($rawModels.Name)))"
            }

            $wrapped = [Bfres.Structs.FMDL]::new()
            $savedOutput = [Console]::Out
            try {
                [Console]::SetOut([IO.TextWriter]::Null)
                [FirstPlugin.BfresSwitch]::ReadModel($wrapped, $rawModel[0])
            }
            finally {
                [Console]::SetOut($savedOutput)
            }

            $textureStream = Open-BotwResource $texturePath
            [void]$streams.Add($textureStream)
            $savedOutput = [Console]::Out
            try {
                [Console]::SetOut([IO.TextWriter]::Null)
                $textureResource = [Syroot.NintenTools.NSW.Bfres.ResFile]::new($textureStream, $true)
            }
            finally {
                [Console]::SetOut($savedOutput)
            }
            $textures = [Collections.Generic.List[Toolbox.Library.STGenericTexture]]::new()
            Add-TextureResources $modelResource $textures $streams $owners
            Add-TextureResources $textureResource $textures $streams $owners

            $materials = [Collections.Generic.List[Toolbox.Library.STGenericMaterial]]::new()
            foreach ($material in @($materialsProperty.GetValue($wrapped, $null))) {
                [void]$materials.Add($material)
            }
            $meshes = [Collections.Generic.List[Toolbox.Library.STGenericObject]]::new()
            foreach ($mesh in @($objectsProperty.GetValue($wrapped, $null))) {
                [void]$meshes.Add($mesh)
            }
            if ($meshes.Count -eq 0) { throw "$unit contains no meshes" }
            $skeleton = $skeletonProperty.GetValue($wrapped, $null)

            $scene = [Assimp.Scene]::new()
            $scene.RootNode = [Assimp.Node]::new('RootNode')
            $saver = [Toolbox.Library.AssimpSaver]::new()
            $progress = [Toolbox.Library.STProgressBar]::new()
            $progressField.SetValue($saver, $progress)
            $skeletonArguments = New-Object 'object[]' 2
            $skeletonArguments[0] = $skeleton
            $skeletonArguments[1] = $scene.RootNode
            $materialArguments = New-Object 'object[]' 4
            $materialArguments[0] = $scene
            $materialArguments[1] = $materials
            $materialArguments[2] = [string]$daePath
            $materialArguments[3] = $textures
            $meshArguments = New-Object 'object[]' 5
            $meshArguments[0] = $scene
            $meshArguments[1] = $meshes
            $meshArguments[2] = $skeleton
            $meshArguments[3] = [string]$daePath
            $meshArguments[4] = $null
            $savedOutput = [Console]::Out
            try {
                [Console]::SetOut([IO.TextWriter]::Null)
                [void]$saveSkeleton.Invoke($saver, $skeletonArguments)
                [void]$saveMaterials.Invoke($saver, $materialArguments)
                [void]$saveMeshes.Invoke($saver, $meshArguments)
            }
            finally {
                [Console]::SetOut($savedOutput)
            }

            $context = [Assimp.AssimpContext]::new()
            $exported = $context.ExportFile(
                $scene,
                $daePath,
                'collada',
                [Assimp.PostProcessSteps]::FlipUVs
            )
            if (-not $exported) { throw "Assimp failed to export $unit to COLLADA" }
            $skinningArguments = New-Object 'object[]' 3
            $skinningArguments[0] = [string]$daePath
            $skinningArguments[1] = $scene
            $skinningArguments[2] = $meshes
            [void]$writeExtraSkinning.Invoke($saver, $skinningArguments)
            $roundTripped = $context.ImportFile($daePath)
            if ($null -eq $roundTripped) { throw "Assimp failed to re-import $unit COLLADA" }
            $exported = $context.ExportFile(
                $roundTripped,
                $gltfPath,
                'gltf2',
                [Assimp.PostProcessSteps]::None
            )
            if (-not $exported) { throw "Assimp failed to export $unit to glTF 2.0" }
            & $Python $PackerPath $gltfPath $glbPath | Out-Null
            if ($LASTEXITCODE -ne 0 -or -not [IO.File]::Exists($glbPath)) {
                throw "GLB packer failed for $unit"
            }
            $completed++
            Write-Output "[$completed/$($models.Count)] Exported $unit ($((Get-Item -LiteralPath $glbPath).Length) bytes)"
        }
        catch {
            [void]$errors.Add([pscustomobject]@{ Model = $unit; Error = $_.Exception.Message })
            Write-Warning "$unit`: $($_.Exception.Message)"
        }
        finally {
            if ($null -ne $context) { $context.Dispose() }
            if ($null -ne $progress) { $progress.Dispose() }
            foreach ($stream in $streams) { $stream.Dispose() }
        }
    }

    if ($errors.Count -gt 0) {
        $errors | ConvertTo-Json -Depth 4
        throw "$($errors.Count) of $($models.Count) model exports failed"
    }
    Write-Output "Exported or reused $completed models with zero errors"
}
finally {
    [Environment]::CurrentDirectory = $savedDirectory
    $env:PATH = $savedPath
}
