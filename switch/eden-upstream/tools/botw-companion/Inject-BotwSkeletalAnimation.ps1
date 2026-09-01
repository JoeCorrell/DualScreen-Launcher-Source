#requires -Version 5.1























[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateScript({ Test-Path -LiteralPath $_ -PathType Container })]
    [string] $ToolboxPath,

    [Parameter(Mandatory = $true)]
    [ValidateScript({ Test-Path -LiteralPath $_ -PathType Leaf })]
    [string] $AnimationFile,

    [Parameter(Mandatory = $true)]
    [ValidateScript({ Test-Path -LiteralPath $_ -PathType Leaf })]
    [string] $InputGltf,

    [Parameter(Mandatory = $true)]
    [string] $OutputGltf,

    [ValidateNotNullOrEmpty()]
    [string] $AnimationName = 'Nml_Wait',

    [ValidateNotNullOrEmpty()]
    [string] $ClipName = 'BOTW_Nml_Wait',

    [ValidateRange(1.0, 240.0)]
    [double] $FrameRate = 30.0,

    [ValidateNotNullOrEmpty()]
    [string] $NodePrefix = 'Armature_'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Resolve-FullPath {
    param(
        [Parameter(Mandatory = $true)][string] $Path,
        [switch] $AllowMissing
    )

    if ($AllowMissing) {
        return [IO.Path]::GetFullPath($ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($Path))
    }
    return (Resolve-Path -LiteralPath $Path).Path
}

function Set-JsonProperty {
    param(
        [Parameter(Mandatory = $true)] $Object,
        [Parameter(Mandatory = $true)][string] $Name,
        [AllowNull()] $Value
    )

    if ($null -eq $Object.PSObject.Properties[$Name]) {
        $Object | Add-Member -NotePropertyName $Name -NotePropertyValue $Value
    }
    else {
        $Object.$Name = $Value
    }
}

function Convert-MatrixNodeToTrs {
    param([Parameter(Mandatory = $true)] $Node)

    if ($null -eq $Node.PSObject.Properties['matrix']) {
        if ($null -eq $Node.PSObject.Properties['translation']) {
            Set-JsonProperty $Node 'translation' @([double]0, [double]0, [double]0)
        }
        if ($null -eq $Node.PSObject.Properties['rotation']) {
            Set-JsonProperty $Node 'rotation' @([double]0, [double]0, [double]0, [double]1)
        }
        if ($null -eq $Node.PSObject.Properties['scale']) {
            Set-JsonProperty $Node 'scale' @([double]1, [double]1, [double]1)
        }
        return
    }

    $v = @($Node.matrix)
    if ($v.Count -ne 16) {
        throw "Node '$($Node.name)' has a matrix with $($v.Count) values; glTF requires 16."
    }



    $m = [System.Numerics.Matrix4x4]::new(
        [single]$v[0],  [single]$v[1],  [single]$v[2],  [single]$v[3],
        [single]$v[4],  [single]$v[5],  [single]$v[6],  [single]$v[7],
        [single]$v[8],  [single]$v[9],  [single]$v[10], [single]$v[11],
        [single]$v[12], [single]$v[13], [single]$v[14], [single]$v[15])
    $scale = [System.Numerics.Vector3]::One
    $rotation = [System.Numerics.Quaternion]::Identity
    $translation = [System.Numerics.Vector3]::Zero
    if (-not [System.Numerics.Matrix4x4]::Decompose($m, [ref]$scale, [ref]$rotation, [ref]$translation)) {
        throw "Node '$($Node.name)' matrix cannot be decomposed into glTF TRS values."
    }
    $rotation = [System.Numerics.Quaternion]::Normalize($rotation)

    $Node.PSObject.Properties.Remove('matrix')
    Set-JsonProperty $Node 'translation' @([double]$translation.X, [double]$translation.Y, [double]$translation.Z)
    Set-JsonProperty $Node 'rotation' @([double]$rotation.X, [double]$rotation.Y, [double]$rotation.Z, [double]$rotation.W)
    Set-JsonProperty $Node 'scale' @([double]$scale.X, [double]$scale.Y, [double]$scale.Z)
}

function Add-BinaryFloats {
    param(
        [Parameter(Mandatory = $true)][IO.MemoryStream] $Stream,
        [Parameter(Mandatory = $true)][System.Collections.IEnumerable] $Values
    )

    while (($Stream.Position % 4) -ne 0) {
        $Stream.WriteByte(0)
    }
    $offset = [int64]$Stream.Position
    $writer = [IO.BinaryWriter]::new($Stream, [Text.Encoding]::UTF8, $true)
    $count = 0
    foreach ($value in $Values) {
        $writer.Write([single]$value)
        $count++
    }
    $writer.Flush()
    $writer.Dispose()
    return [pscustomobject]@{
        Offset = $offset
        Length = [int64]($count * 4)
        Count = $count
    }
}

function Add-BufferViewAndAccessor {
    param(
        [Parameter(Mandatory = $true)] $Gltf,
        [Parameter(Mandatory = $true)][int64] $ByteOffset,
        [Parameter(Mandatory = $true)][int64] $ByteLength,
        [Parameter(Mandatory = $true)][int] $Count,
        [Parameter(Mandatory = $true)][ValidateSet('SCALAR', 'VEC3', 'VEC4')][string] $Type,
        [double[]] $Minimum,
        [double[]] $Maximum
    )

    $viewIndex = @($Gltf.bufferViews).Count
    $view = [pscustomobject]@{
        buffer = 0
        byteOffset = $ByteOffset
        byteLength = $ByteLength
    }
    $Gltf.bufferViews = @($Gltf.bufferViews) + @($view)

    $accessor = [pscustomobject]@{
        bufferView = $viewIndex
        byteOffset = 0
        componentType = 5126
        count = $Count
        type = $Type
    }
    if ($null -ne $Minimum) {
        Set-JsonProperty $accessor 'min' @($Minimum)
    }
    if ($null -ne $Maximum) {
        Set-JsonProperty $accessor 'max' @($Maximum)
    }
    $accessorIndex = @($Gltf.accessors).Count
    $Gltf.accessors = @($Gltf.accessors) + @($accessor)
    return $accessorIndex
}

function Get-NormalizedQuaternion {
    param(
        [Parameter(Mandatory = $true)] $Bone,
        [Parameter(Mandatory = $true)][single] $Frame,
        [Parameter(Mandatory = $true)] $FromEulerMethod,
        [Parameter(Mandatory = $true)][Type] $Vector3Type
    )

    if ($Bone.RotType.ToString() -eq 'EULER') {
        $x = [single]$Bone.XROT.GetValue($Frame)
        $y = [single]$Bone.YROT.GetValue($Frame)
        $z = [single]$Bone.ZROT.GetValue($Frame)
        $euler = [Activator]::CreateInstance($Vector3Type, [object[]]@($x, $y, $z))
        $invokeArguments = [object[]]::new(1)
        $invokeArguments[0] = $euler.psobject.BaseObject
        $q = $FromEulerMethod.Invoke($null, $invokeArguments)
    }
    else {
        $q = $Bone.GetRotation($Frame)
    }

    $length = [Math]::Sqrt(
        ([double]$q.X * [double]$q.X) +
        ([double]$q.Y * [double]$q.Y) +
        ([double]$q.Z * [double]$q.Z) +
        ([double]$q.W * [double]$q.W))
    if ($length -lt 1.0e-12) {
        throw "Bone '$($Bone.Text)' produced a zero-length quaternion at frame $Frame."
    }
    return @(
        ([double]$q.X / $length),
        ([double]$q.Y / $length),
        ([double]$q.Z / $length),
        ([double]$q.W / $length))
}

function Test-AppendedGltfData {
    param(
        [Parameter(Mandatory = $true)] $Gltf,
        [Parameter(Mandatory = $true)][int64] $BufferLength,
        [Parameter(Mandatory = $true)][int] $FirstView,
        [Parameter(Mandatory = $true)][int] $FirstAccessor
    )

    for ($i = $FirstView; $i -lt @($Gltf.bufferViews).Count; $i++) {
        $view = @($Gltf.bufferViews)[$i]
        $start = if ($null -eq $view.PSObject.Properties['byteOffset']) { 0L } else { [int64]$view.byteOffset }
        $end = $start + [int64]$view.byteLength
        if ($start -lt 0 -or $end -gt $BufferLength) {
            throw "Appended bufferView $i range [$start,$end) exceeds buffer length $BufferLength."
        }
    }

    $componentCounts = @{ SCALAR = 1; VEC3 = 3; VEC4 = 4 }
    for ($i = $FirstAccessor; $i -lt @($Gltf.accessors).Count; $i++) {
        $accessor = @($Gltf.accessors)[$i]
        if ([int]$accessor.componentType -ne 5126) {
            throw "Appended accessor $i is not FLOAT (5126)."
        }
        $view = @($Gltf.bufferViews)[[int]$accessor.bufferView]
        $required = [int64]$accessor.count * [int64]$componentCounts[[string]$accessor.type] * 4L
        $accessorOffset = if ($null -eq $accessor.PSObject.Properties['byteOffset']) { 0L } else { [int64]$accessor.byteOffset }
        if (($accessorOffset + $required) -gt [int64]$view.byteLength) {
            throw "Appended accessor $i requires $required bytes outside bufferView $($accessor.bufferView)."
        }
    }
}

$toolboxFullPath = Resolve-FullPath $ToolboxPath
$animationFullPath = Resolve-FullPath $AnimationFile
$inputGltfFullPath = Resolve-FullPath $InputGltf
$outputGltfFullPath = Resolve-FullPath $OutputGltf -AllowMissing
$outputDirectory = Split-Path -Parent $outputGltfFullPath
if ([string]::IsNullOrWhiteSpace($outputDirectory)) {
    $outputDirectory = (Get-Location).Path
}
[IO.Directory]::CreateDirectory($outputDirectory) | Out-Null

if ([IO.Path]::GetExtension($inputGltfFullPath) -ine '.gltf' -or
    [IO.Path]::GetExtension($outputGltfFullPath) -ine '.gltf') {
    throw 'This tool accepts JSON .gltf input/output. Convert .glb to .gltf first.'
}

$gltf = Get-Content -LiteralPath $inputGltfFullPath -Raw | ConvertFrom-Json
if ([string]$gltf.asset.version -ne '2.0') {
    throw "Expected glTF 2.0, found '$($gltf.asset.version)'."
}
if (@($gltf.buffers).Count -ne 1) {
    throw "Expected exactly one external glTF buffer, found $(@($gltf.buffers).Count)."
}
if ($null -eq $gltf.PSObject.Properties['bufferViews']) {
    Set-JsonProperty $gltf 'bufferViews' @()
}
if ($null -eq $gltf.PSObject.Properties['accessors']) {
    Set-JsonProperty $gltf 'accessors' @()
}

$inputBufferUri = [string]@($gltf.buffers)[0].uri
if ([string]::IsNullOrWhiteSpace($inputBufferUri) -or $inputBufferUri.StartsWith('data:', [StringComparison]::OrdinalIgnoreCase)) {
    throw 'The input glTF must reference one external BIN file.'
}
$decodedBufferUri = [Uri]::UnescapeDataString($inputBufferUri).Replace('/', [IO.Path]::DirectorySeparatorChar)
$inputBufferPath = [IO.Path]::GetFullPath((Join-Path (Split-Path -Parent $inputGltfFullPath) $decodedBufferUri))
if (-not (Test-Path -LiteralPath $inputBufferPath -PathType Leaf)) {
    throw "Input BIN not found: $inputBufferPath"
}
$originalBuffer = [IO.File]::ReadAllBytes($inputBufferPath)

$originalSkinsJson = if ($null -eq $gltf.PSObject.Properties['skins']) { $null } else { $gltf.skins | ConvertTo-Json -Depth 100 -Compress }
$originalMaterialsJson = if ($null -eq $gltf.PSObject.Properties['materials']) { $null } else { $gltf.materials | ConvertTo-Json -Depth 100 -Compress }
$firstView = @($gltf.bufferViews).Count
$firstAccessor = @($gltf.accessors).Count

$requiredAssemblies = @(
    (Join-Path $toolboxFullPath 'Toolbox.Library.dll'),
    (Join-Path $toolboxFullPath 'Syroot.NintenTools.NSW.Bfres.dll'),
    (Join-Path $toolboxFullPath 'FirstPlugin.Plg.dll'),
    (Join-Path $toolboxFullPath 'Lib\OpenTK.dll'))
foreach ($assemblyPath in $requiredAssemblies) {
    if (-not (Test-Path -LiteralPath $assemblyPath -PathType Leaf)) {
        throw "Required Switch Toolbox assembly not found: $assemblyPath"
    }
}

$assemblyResolver = [ResolveEventHandler] {
    param($sender, $eventArgs)
    $fileName = $eventArgs.Name.Split(',')[0] + '.dll'
    foreach ($directory in @($toolboxFullPath, (Join-Path $toolboxFullPath 'Lib'))) {
        $candidate = Join-Path $directory $fileName
        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
            return [Reflection.Assembly]::LoadFrom($candidate)
        }
    }
    return $null
}

[AppDomain]::CurrentDomain.add_AssemblyResolve($assemblyResolver)
$previousCurrentDirectory = [Environment]::CurrentDirectory
$stream = $null
try {
    [Environment]::CurrentDirectory = $toolboxFullPath
    [Reflection.Assembly]::LoadFrom((Join-Path $toolboxFullPath 'Lib\OpenTK.dll')) | Out-Null
    $toolboxAssembly = [Reflection.Assembly]::LoadFrom((Join-Path $toolboxFullPath 'Toolbox.Library.dll'))
    $bfresAssembly = [Reflection.Assembly]::LoadFrom((Join-Path $toolboxFullPath 'Syroot.NintenTools.NSW.Bfres.dll'))
    $pluginAssembly = [Reflection.Assembly]::LoadFrom((Join-Path $toolboxFullPath 'FirstPlugin.Plg.dll'))

    try {
        $toolboxTypes = $toolboxAssembly.GetTypes()
    }
    catch [Reflection.ReflectionTypeLoadException] {
        $toolboxTypes = @($_.Exception.Types | Where-Object { $null -ne $_ })
    }

    $animationBytes = [IO.File]::ReadAllBytes($animationFullPath)
    $magic = if ($animationBytes.Length -ge 4) { [Text.Encoding]::ASCII.GetString($animationBytes, 0, 4) } else { '' }
    if ($magic -eq 'Yaz0') {
        $yaz0Type = @($toolboxTypes | Where-Object FullName -eq 'Toolbox.Library.Yaz0')[0]
        $yaz0 = [Activator]::CreateInstance($yaz0Type)
        $decompressMethod = $yaz0Type.GetMethod(
            'Decompress',
            [Reflection.BindingFlags]'Public,NonPublic,Instance',
            $null,
            [Type[]]@([byte[]]),
            $null)
        $decompressArguments = [object[]]::new(1)
        $decompressArguments[0] = $animationBytes
        $animationBytes = [byte[]]$decompressMethod.Invoke($yaz0, $decompressArguments)
    }
    if ($animationBytes.Length -lt 4 -or [Text.Encoding]::ASCII.GetString($animationBytes, 0, 4) -ne 'FRES') {
        throw "Animation input is neither Yaz0-compressed nor raw Switch BFRES: $animationFullPath"
    }

    $resFileType = $bfresAssembly.GetType('Syroot.NintenTools.NSW.Bfres.ResFile', $true)
    $resFileArguments = [object[]]::new(2)
    $resFileArguments[0] = [IO.MemoryStream]::new($animationBytes, $false)
    $resFileArguments[1] = $false
    $resFile = [Activator]::CreateInstance($resFileType, $resFileArguments)

    $sourceAnimation = $null
    foreach ($candidateAnimation in $resFile.SkeletalAnims) {
        if ($candidateAnimation.Name -ceq $AnimationName) {
            $sourceAnimation = $candidateAnimation
            break
        }
    }
    if ($null -eq $sourceAnimation) {
        throw "Skeletal animation '$AnimationName' was not found in $animationFullPath."
    }

    $fskaType = $pluginAssembly.GetType('Bfres.Structs.FSKA', $true)
    $fskaConstructor = @($fskaType.GetConstructors() | Where-Object {
        $_.GetParameters().Count -eq 1 -and
        $_.GetParameters()[0].ParameterType.FullName -eq 'Syroot.NintenTools.NSW.Bfres.SkeletalAnim'
    })[0]
    $fskaArguments = [object[]]::new(1)
    $fskaArguments[0] = $sourceAnimation.psobject.BaseObject
    $fska = $fskaConstructor.Invoke($fskaArguments)
    if (@($fska.Bones).Count -ne @($sourceAnimation.BoneAnims).Count) {
        throw "FSKA bone count $(@($fska.Bones).Count) does not match raw animation bone count $(@($sourceAnimation.BoneAnims).Count)."
    }

    $openTkAssembly = [Reflection.Assembly]::LoadFrom((Join-Path $toolboxFullPath 'Lib\OpenTK.dll'))
    $vector3Type = $openTkAssembly.GetType('OpenTK.Vector3', $true)
    $stMathType = @($toolboxTypes | Where-Object FullName -eq 'Toolbox.Library.STMath')[0]
    $fromEulerMethod = $stMathType.GetMethod(
        'FromEulerAngles',
        [Reflection.BindingFlags]'Public,Static',
        $null,
        [Type[]]@($vector3Type),
        $null)

    $nodeIndices = @{}
    for ($i = 0; $i -lt @($gltf.nodes).Count; $i++) {
        $nodeObject = @($gltf.nodes)[$i]
        $nodeName = if ($null -eq $nodeObject.PSObject.Properties['name']) { '' } else { [string]$nodeObject.name }
        if (-not [string]::IsNullOrEmpty($nodeName) -and -not $nodeIndices.ContainsKey($nodeName)) {
            $nodeIndices[$nodeName] = $i
        }
    }

    $frameCount = [int]$fska.FrameCount
    if ($frameCount -lt 1) {
        throw "Animation '$AnimationName' reports invalid frame count $frameCount."
    }


    $sampleCount = $frameCount + 1
    $times = [System.Collections.Generic.List[double]]::new($sampleCount)
    for ($frame = 0; $frame -le $frameCount; $frame++) {
        $times.Add([double]$frame / $FrameRate)
    }

    $stream = [IO.MemoryStream]::new()
    $stream.Write($originalBuffer, 0, $originalBuffer.Length)
    $timeBlock = Add-BinaryFloats $stream $times
    $timeAccessor = Add-BufferViewAndAccessor `
        -Gltf $gltf `
        -ByteOffset $timeBlock.Offset `
        -ByteLength $timeBlock.Length `
        -Count $sampleCount `
        -Type 'SCALAR' `
        -Minimum @([double]0) `
        -Maximum @([double]($frameCount / $FrameRate))

    $samplers = [System.Collections.ArrayList]::new()
    $channels = [System.Collections.ArrayList]::new()
    $matchedBones = [System.Collections.Generic.List[string]]::new()
    $convertedNodes = @{}

    for ($boneIndex = 0; $boneIndex -lt @($fska.Bones).Count; $boneIndex++) {
        $bone = @($fska.Bones)[$boneIndex]
        $rawBone = @($sourceAnimation.BoneAnims)[$boneIndex]
        if ($bone.Text -cne $rawBone.Name) {
            throw "FSKA/raw bone mismatch at index ${boneIndex}: '$($bone.Text)' vs '$($rawBone.Name)'."
        }

        $nodeName = $NodePrefix + [string]$bone.Text
        if (-not $nodeIndices.ContainsKey($nodeName)) {
            continue
        }
        if (-not ([bool]$rawBone.UseTranslation -or [bool]$rawBone.UseRotation -or [bool]$rawBone.UseScale)) {



            continue
        }
        $nodeIndex = [int]$nodeIndices[$nodeName]
        $matchedBones.Add([string]$bone.Text)
        if (-not $convertedNodes.ContainsKey($nodeIndex)) {
            Convert-MatrixNodeToTrs @($gltf.nodes)[$nodeIndex]
            $convertedNodes[$nodeIndex] = $true
        }

        if ([bool]$rawBone.UseTranslation) {
            $values = [System.Collections.Generic.List[double]]::new($sampleCount * 3)
            for ($frame = 0; $frame -le $frameCount; $frame++) {
                $fska.SetFrame([single]$frame)
                $position = $bone.GetPosition([single]$frame)
                $values.Add([double]$position.X)
                $values.Add([double]$position.Y)
                $values.Add([double]$position.Z)
            }
            $block = Add-BinaryFloats $stream $values
            $outputAccessor = Add-BufferViewAndAccessor $gltf $block.Offset $block.Length $sampleCount 'VEC3'
            $samplerIndex = $samplers.Count
            [void]$samplers.Add([pscustomobject]@{ input = $timeAccessor; output = $outputAccessor; interpolation = 'LINEAR' })
            [void]$channels.Add([pscustomobject]@{ sampler = $samplerIndex; target = [pscustomobject]@{ node = $nodeIndex; path = 'translation' } })
        }

        if ([bool]$rawBone.UseRotation) {
            $values = [System.Collections.Generic.List[double]]::new($sampleCount * 4)
            $previous = $null
            for ($frame = 0; $frame -le $frameCount; $frame++) {
                $fska.SetFrame([single]$frame)
                $q = @(Get-NormalizedQuaternion $bone ([single]$frame) $fromEulerMethod $vector3Type)
                if ($null -ne $previous) {
                    $dot = ($q[0] * $previous[0]) + ($q[1] * $previous[1]) + ($q[2] * $previous[2]) + ($q[3] * $previous[3])
                    if ($dot -lt 0) {
                        for ($component = 0; $component -lt 4; $component++) {
                            $q[$component] = -$q[$component]
                        }
                    }
                }
                foreach ($componentValue in $q) {
                    $values.Add([double]$componentValue)
                }
                $previous = @($q)
            }
            $block = Add-BinaryFloats $stream $values
            $outputAccessor = Add-BufferViewAndAccessor $gltf $block.Offset $block.Length $sampleCount 'VEC4'
            $samplerIndex = $samplers.Count
            [void]$samplers.Add([pscustomobject]@{ input = $timeAccessor; output = $outputAccessor; interpolation = 'LINEAR' })
            [void]$channels.Add([pscustomobject]@{ sampler = $samplerIndex; target = [pscustomobject]@{ node = $nodeIndex; path = 'rotation' } })
        }

        if ([bool]$rawBone.UseScale) {
            $values = [System.Collections.Generic.List[double]]::new($sampleCount * 3)
            for ($frame = 0; $frame -le $frameCount; $frame++) {
                $fska.SetFrame([single]$frame)
                $scale = $bone.GetScale([single]$frame)
                $values.Add([double]$scale.X)
                $values.Add([double]$scale.Y)
                $values.Add([double]$scale.Z)
            }
            $block = Add-BinaryFloats $stream $values
            $outputAccessor = Add-BufferViewAndAccessor $gltf $block.Offset $block.Length $sampleCount 'VEC3'
            $samplerIndex = $samplers.Count
            [void]$samplers.Add([pscustomobject]@{ input = $timeAccessor; output = $outputAccessor; interpolation = 'LINEAR' })
            [void]$channels.Add([pscustomobject]@{ sampler = $samplerIndex; target = [pscustomobject]@{ node = $nodeIndex; path = 'scale' } })
        }
    }

    if ($matchedBones.Count -eq 0 -or $channels.Count -eq 0) {
        throw "No '$NodePrefix<bone>' nodes matched animation '$AnimationName'."
    }

    $clip = [pscustomobject]@{
        name = $ClipName
        samplers = @($samplers.ToArray())
        channels = @($channels.ToArray())
        extras = [pscustomobject]@{
            source = 'BOTW Player_Animation BFRES'
            sourceAnimation = $AnimationName
            frameRate = $FrameRate
            sourceFrameCount = $frameCount
            looping = [bool]$fska.CanLoop
        }
    }
    if ($null -eq $gltf.PSObject.Properties['animations']) {
        Set-JsonProperty $gltf 'animations' @($clip)
    }
    else {
        $gltf.animations = @($gltf.animations) + @($clip)
    }

    while (($stream.Position % 4) -ne 0) {
        $stream.WriteByte(0)
    }
    $outputBuffer = $stream.ToArray()
    $outputBufferName = [IO.Path]::GetFileNameWithoutExtension($outputGltfFullPath) + '.bin'
    $outputBufferPath = Join-Path $outputDirectory $outputBufferName
    @($gltf.buffers)[0].uri = $outputBufferName.Replace('\', '/')
    @($gltf.buffers)[0].byteLength = $outputBuffer.Length

    Test-AppendedGltfData $gltf $outputBuffer.Length $firstView $firstAccessor

    [IO.File]::WriteAllBytes($outputBufferPath, $outputBuffer)
    $json = $gltf | ConvertTo-Json -Depth 100
    [IO.File]::WriteAllText($outputGltfFullPath, $json, [Text.UTF8Encoding]::new($false))



    $validated = Get-Content -LiteralPath $outputGltfFullPath -Raw | ConvertFrom-Json
    if ([int64]@($validated.buffers)[0].byteLength -ne (Get-Item -LiteralPath $outputBufferPath).Length) {
        throw 'Emitted buffer byteLength does not match the output BIN size.'
    }
    $validatedSkinsJson = if ($null -eq $validated.PSObject.Properties['skins']) { $null } else { $validated.skins | ConvertTo-Json -Depth 100 -Compress }
    $validatedMaterialsJson = if ($null -eq $validated.PSObject.Properties['materials']) { $null } else { $validated.materials | ConvertTo-Json -Depth 100 -Compress }
    if ($validatedSkinsJson -cne $originalSkinsJson) {
        throw 'Skin data changed during animation injection.'
    }
    if ($validatedMaterialsJson -cne $originalMaterialsJson) {
        throw 'Material data changed during animation injection.'
    }
    Test-AppendedGltfData $validated (Get-Item -LiteralPath $outputBufferPath).Length $firstView $firstAccessor

    [pscustomobject]@{
        OutputGltf = $outputGltfFullPath
        OutputBin = $outputBufferPath
        Animation = $AnimationName
        Looping = [bool]$fska.CanLoop
        SourceFrames = $frameCount
        Samples = $sampleCount
        FrameRate = $FrameRate
        MatchedBones = $matchedBones.Count
        Channels = $channels.Count
        AddedAccessors = @($gltf.accessors).Count - $firstAccessor
        AddedBufferViews = @($gltf.bufferViews).Count - $firstView
        BufferBytes = $outputBuffer.Length
    }
}
finally {
    if ($null -ne $stream) {
        $stream.Dispose()
    }
    [Environment]::CurrentDirectory = $previousCurrentDirectory
    [AppDomain]::CurrentDomain.remove_AssemblyResolve($assemblyResolver)
}
