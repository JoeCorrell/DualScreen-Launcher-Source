














[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string]$ModelInput,

    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string[]]$TextureInput,

    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string]$ModelName,

    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string]$OutputDirectory,

    [Parameter()]
    [string]$SwitchToolboxPath,

    [Parameter()]
    [string]$AnimationBfres,

    [Parameter()]
    [ValidateNotNullOrEmpty()]
    [string]$Pose = 'Nml_Wait',

    [Parameter()]
    [float]$PoseFrame = -1.0,

    [Parameter()]
    [switch]$Force
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Resolve-ExistingFile {
    param([Parameter(Mandatory = $true)][string]$Path)

    $resolved = Resolve-Path -LiteralPath $Path -ErrorAction Stop
    if (-not [IO.File]::Exists($resolved.ProviderPath)) {
        throw "File does not exist: $Path"
    }
    return $resolved.ProviderPath
}

function Resolve-ExistingDirectory {
    param([Parameter(Mandatory = $true)][string]$Path)

    $resolved = Resolve-Path -LiteralPath $Path -ErrorAction Stop
    if (-not [IO.Directory]::Exists($resolved.ProviderPath)) {
        throw "Directory does not exist: $Path"
    }
    return $resolved.ProviderPath.TrimEnd([IO.Path]::DirectorySeparatorChar)
}

function Get-UnresolvedFullPath {
    param([Parameter(Mandatory = $true)][string]$Path)

    return $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($Path)
}

function Invoke-QuietThirdParty {
    param([Parameter(Mandatory = $true)][scriptblock]$Action)

    $savedOutput = [Console]::Out
    try {
        [Console]::SetOut([IO.TextWriter]::Null)
        return & $Action
    }
    finally {
        [Console]::SetOut($savedOutput)
    }
}

function Get-StreamMagic {
    param([Parameter(Mandatory = $true)][IO.Stream]$Stream)

    if (-not $Stream.CanSeek) {
        throw 'The decompressed Nintendo resource stream is not seekable.'
    }
    $position = $Stream.Position
    try {
        $magicBytes = New-Object byte[] 4
        if ($Stream.Read($magicBytes, 0, $magicBytes.Length) -ne $magicBytes.Length) {
            return ''
        }
        return [Text.Encoding]::ASCII.GetString($magicBytes)
    }
    finally {
        $Stream.Position = $position
    }
}

function Open-NintendoResourceStream {
    param([Parameter(Mandatory = $true)][string]$Path)

    $inputStream = [IO.File]::OpenRead($Path)
    try {
        $magicBytes = New-Object byte[] 4
        if ($inputStream.Read($magicBytes, 0, $magicBytes.Length) -ne $magicBytes.Length) {
            throw "Resource is too small: $Path"
        }
        $inputStream.Position = 0
        if ([Text.Encoding]::ASCII.GetString($magicBytes) -eq 'Yaz0') {
            $result = Invoke-QuietThirdParty { ([Toolbox.Library.Yaz0]::new()).Decompress($inputStream) }
        }
        else {
            $result = [IO.MemoryStream]::new()
            $inputStream.CopyTo($result)
        }
    }
    finally {
        $inputStream.Dispose()
    }

    if ($null -eq $result) {
        throw "Switch Toolbox did not return decompressed data for: $Path"
    }
    if ($result.CanSeek) {
        $result.Position = 0
    }
    [void]$script:OwnedStreams.Add($result)
    return $result
}

function Open-BfresResource {
    param([Parameter(Mandatory = $true)][string]$Path)

    $stream = Open-NintendoResourceStream -Path $Path
    if ((Get-StreamMagic -Stream $stream) -ne 'FRES') {
        throw "Expected a BFRES resource after Yaz0 decompression: $Path"
    }
    return Invoke-QuietThirdParty {
        [Syroot.NintenTools.NSW.Bfres.ResFile]::new($stream, $true)
    }
}

function Test-PngFile {
    param([Parameter(Mandatory = $true)][string]$Path)

    if (-not [IO.File]::Exists($Path)) {
        return $false
    }
    $stream = [IO.File]::OpenRead($Path)
    try {
        $signature = New-Object byte[] 8
        if ($stream.Read($signature, 0, $signature.Length) -ne $signature.Length) {
            return $false
        }
        $expected = [byte[]](137, 80, 78, 71, 13, 10, 26, 10)
        for ($index = 0; $index -lt $expected.Length; $index++) {
            if ($signature[$index] -ne $expected[$index]) {
                return $false
            }
        }
        return $true
    }
    finally {
        $stream.Dispose()
    }
}

function Get-SafeLeafNameFromUri {
    param([Parameter(Mandatory = $true)][string]$UriText)

    if ([string]::IsNullOrWhiteSpace($UriText)) {
        throw 'An exported resource has an empty URI.'
    }
    $candidate = $UriText.Trim()
    $parsedUri = $null
    if ([Uri]::TryCreate($candidate, [UriKind]::Absolute, [ref]$parsedUri)) {
        if (-not $parsedUri.IsFile) {
            throw "An exported resource uses a non-file absolute URI: $candidate"
        }
        $candidate = $parsedUri.LocalPath
    }
    else {
        $candidate = [Uri]::UnescapeDataString($candidate)
    }
    $candidate = $candidate.Replace('/', [IO.Path]::DirectorySeparatorChar)
    $leafName = [IO.Path]::GetFileName($candidate)
    if ([string]::IsNullOrWhiteSpace($leafName) -or
        $leafName.IndexOfAny([IO.Path]::GetInvalidFileNameChars()) -ge 0) {
        throw "An exported resource has an unsafe filename: $UriText"
    }
    return $leafName
}

function ConvertTo-RelativeGltfUri {
    param([Parameter(Mandatory = $true)][string]$LeafName)

    if ($LeafName -ne [IO.Path]::GetFileName($LeafName)) {
        throw "Only flat relative glTF resources are supported: $LeafName"
    }
    return [Uri]::EscapeDataString($LeafName)
}

function Repair-MaterialTextureReferences {
    param(
        [AllowNull()][object]$Node,
        [Parameter(Mandatory = $true)]
        [Collections.Generic.Dictionary[int, int]]$TextureIndexMap
    )

    if ($null -eq $Node -or $Node -is [string] -or $Node -is [ValueType]) {
        return
    }
    if ($Node -is [Collections.IEnumerable] -and $Node -isnot [pscustomobject]) {
        foreach ($item in $Node) {
            Repair-MaterialTextureReferences -Node $item -TextureIndexMap $TextureIndexMap
        }
        return
    }

    foreach ($property in @($Node.PSObject.Properties)) {
        $value = $property.Value
        if ($property.Name -match 'Texture$' -and $null -ne $value -and
            $value.PSObject.Properties.Match('index').Count -gt 0) {
            $oldIndex = [int]$value.index
            if ($TextureIndexMap.ContainsKey($oldIndex)) {
                $value.index = $TextureIndexMap[$oldIndex]
            }
            else {
                $Node.PSObject.Properties.Remove($property.Name)
            }
        }
        else {
            Repair-MaterialTextureReferences -Node $value -TextureIndexMap $TextureIndexMap
        }
    }
}

function Add-PoseToGltf {
    param(
        [Parameter(Mandatory = $true)][object]$Gltf,
        [Parameter(Mandatory = $true)][object]$WrappedModel,
        [Parameter(Mandatory = $true)][object]$Animation,
        [Parameter(Mandatory = $true)][string]$AnimationName,
        [Parameter(Mandatory = $true)][float]$RequestedFrame
    )

    $sample = Invoke-QuietThirdParty {
        [Eden.BotwCompanion.HeadlessConverter]::SamplePose(
            $WrappedModel,
            $Animation,
            $RequestedFrame
        )
    }

    $matchedNodes = 0
    $matchedAnimatedNodes = 0
    $changedAnimatedNodes = 0
    foreach ($node in @($Gltf.nodes)) {
        if ($null -eq $node -or $node.PSObject.Properties.Match('name').Count -eq 0) {
            continue
        }
        $nodeName = [string]$node.name
        if (-not $sample.Matrices.ContainsKey($nodeName)) {
            continue
        }

        $matchedNodes++
        $isAnimated = $sample.AnimatedNodes.Contains($nodeName)
        if ($isAnimated) {
            $matchedAnimatedNodes++
        }
        $matrix = @($sample.Matrices[$nodeName] | ForEach-Object { [double]$_ })
        $isChanged = $node.PSObject.Properties.Match('matrix').Count -eq 0
        if (-not $isChanged) {
            $oldMatrix = @($node.matrix)
            if ($oldMatrix.Count -ne 16) {
                $isChanged = $true
            }
            else {
                $difference = 0.0
                for ($index = 0; $index -lt 16; $index++) {
                    $difference += [Math]::Abs([double]$oldMatrix[$index] - $matrix[$index])
                }
                $isChanged = $difference -gt 0.0001
            }
        }
        if ($isAnimated -and $isChanged) {
            $changedAnimatedNodes++
        }

        $node.PSObject.Properties.Remove('translation')
        $node.PSObject.Properties.Remove('rotation')
        $node.PSObject.Properties.Remove('scale')
        $node | Add-Member -NotePropertyName matrix -NotePropertyValue $matrix -Force
    }

    if ($matchedAnimatedNodes -eq 0 -or $changedAnimatedNodes -eq 0) {
        throw "Pose '$AnimationName' did not change any skeleton nodes in model '$ModelName'."
    }

    if ($Gltf.PSObject.Properties.Match('extras').Count -eq 0 -or $null -eq $Gltf.extras) {
        $Gltf | Add-Member -NotePropertyName extras -NotePropertyValue ([pscustomobject]@{}) -Force
    }
    $Gltf.extras | Add-Member -NotePropertyName botwPose -NotePropertyValue ([pscustomobject]@{
        animation = $AnimationName
        frame = [double]$sample.Frame
        matchedNodes = $matchedNodes
        matchedAnimatedNodes = $matchedAnimatedNodes
        changedAnimatedNodes = $changedAnimatedNodes
    }) -Force

    return [pscustomobject]@{
        Animation = $AnimationName
        Frame = [double]$sample.Frame
        MatchedNodes = $matchedNodes
        ChangedAnimatedNodes = $changedAnimatedNodes
    }
}

function Repair-GltfTextures {
    param(
        [Parameter(Mandatory = $true)][object]$Gltf,
        [Parameter(Mandatory = $true)][string]$StageDirectory,
        [Parameter(Mandatory = $true)]
        [Collections.Generic.Dictionary[string, object]]$ExportedCandidates
    )

    $oldImages = @($Gltf.images)
    $imageIndexMap = [Collections.Generic.Dictionary[int, int]]::new()
    $newImages = [Collections.Generic.List[object]]::new()
    $publishedImageNames = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    $missingImages = [Collections.Generic.List[string]]::new()

    for ($oldImageIndex = 0; $oldImageIndex -lt $oldImages.Count; $oldImageIndex++) {
        $image = $oldImages[$oldImageIndex]
        if ($null -eq $image -or $image.PSObject.Properties.Match('uri').Count -eq 0) {
            [void]$missingImages.Add("image[$oldImageIndex]")
            continue
        }
        $leafName = Get-SafeLeafNameFromUri -UriText ([string]$image.uri)
        $candidate = $null
        if (-not $ExportedCandidates.TryGetValue($leafName, [ref]$candidate) -or
            -not (Test-PngFile -Path ([string]$candidate))) {
            [void]$missingImages.Add($leafName)
            continue
        }
        $image.uri = ConvertTo-RelativeGltfUri -LeafName $leafName
        $imageIndexMap.Add($oldImageIndex, $newImages.Count)
        [void]$newImages.Add($image)
        [void]$publishedImageNames.Add($leafName)
    }

    $oldTextures = @($Gltf.textures)
    $textureIndexMap = [Collections.Generic.Dictionary[int, int]]::new()
    $newTextures = [Collections.Generic.List[object]]::new()
    for ($oldTextureIndex = 0; $oldTextureIndex -lt $oldTextures.Count; $oldTextureIndex++) {
        $texture = $oldTextures[$oldTextureIndex]
        if ($null -eq $texture -or $texture.PSObject.Properties.Match('source').Count -eq 0) {
            continue
        }
        $oldImageIndex = [int]$texture.source
        if (-not $imageIndexMap.ContainsKey($oldImageIndex)) {
            continue
        }
        $texture.source = $imageIndexMap[$oldImageIndex]
        $textureIndexMap.Add($oldTextureIndex, $newTextures.Count)
        [void]$newTextures.Add($texture)
    }

    foreach ($material in @($Gltf.materials)) {
        Repair-MaterialTextureReferences -Node $material -TextureIndexMap $textureIndexMap
    }

    if ($newImages.Count -gt 0) {
        $Gltf.images = @($newImages.ToArray())
    }
    else {
        $Gltf.PSObject.Properties.Remove('images')
    }
    if ($newTextures.Count -gt 0) {
        $Gltf.textures = @($newTextures.ToArray())
    }
    else {
        $Gltf.PSObject.Properties.Remove('textures')
    }

    return [pscustomobject]@{
        ImageNames = @($publishedImageNames | Sort-Object)
        MissingImages = @($missingImages | Sort-Object -Unique)
        ImageCount = $newImages.Count
        TextureCount = $newTextures.Count
    }
}

function Repair-GltfSkinData {
    param(
        [Parameter(Mandatory = $true)][object]$Gltf,
        [Parameter(Mandatory = $true)][string]$StageDirectory
    )

    $accessors = @($Gltf.accessors)
    $bufferViews = [Collections.Generic.List[object]]::new()
    foreach ($view in @($Gltf.bufferViews)) {
        [void]$bufferViews.Add($view)
    }
    $buffers = @($Gltf.buffers)
    $jointAccessorIndices = [Collections.Generic.HashSet[int]]::new()

    foreach ($mesh in @($Gltf.meshes)) {
        foreach ($primitive in @($mesh.primitives)) {
            if ($null -eq $primitive.attributes) {
                continue
            }
            foreach ($attribute in @($primitive.attributes.PSObject.Properties)) {
                if ($attribute.Name -match '^JOINTS_[0-9]+$') {
                    [void]$jointAccessorIndices.Add([int]$attribute.Value)
                    $weightName = $attribute.Name -replace '^JOINTS_', 'WEIGHTS_'
                    if ($primitive.attributes.PSObject.Properties.Match($weightName).Count -eq 0) {
                        throw "A skinned primitive has $($attribute.Name) without $weightName."
                    }
                }
            }
        }
    }
    if ($jointAccessorIndices.Count -eq 0) {
        throw 'The glTF scene has a skin but no JOINTS attributes.'
    }

    $positionAccessorIndices = [Collections.Generic.HashSet[int]]::new()
    foreach ($mesh in @($Gltf.meshes)) {
        foreach ($primitive in @($mesh.primitives)) {
            if ($null -ne $primitive.attributes -and
                $primitive.attributes.PSObject.Properties.Match('POSITION').Count -gt 0) {
                [void]$positionAccessorIndices.Add([int]$primitive.attributes.POSITION)
            }
        }
    }

    foreach ($accessorIndex in $jointAccessorIndices) {
        if ($accessorIndex -lt 0 -or $accessorIndex -ge $accessors.Count) {
            throw "A JOINTS attribute refers to invalid accessor $accessorIndex."
        }
        $accessor = $accessors[$accessorIndex]
        if ([string]$accessor.type -ne 'VEC4') {
            throw "JOINTS accessor $accessorIndex is not VEC4."
        }
        $componentType = [int]$accessor.componentType
        if ($componentType -eq 5121 -or $componentType -eq 5123) {
            continue
        }
        if ($componentType -ne 5126) {
            throw "JOINTS accessor $accessorIndex uses unsupported component type $componentType."
        }

        $oldViewIndex = [int]$accessor.bufferView
        if ($oldViewIndex -lt 0 -or $oldViewIndex -ge $bufferViews.Count) {
            throw "JOINTS accessor $accessorIndex has an invalid bufferView."
        }
        $oldView = $bufferViews[$oldViewIndex]
        $bufferIndex = [int]$oldView.buffer
        if ($bufferIndex -lt 0 -or $bufferIndex -ge $buffers.Count) {
            throw "JOINTS accessor $accessorIndex has an invalid buffer."
        }
        $bufferLeafName = Get-SafeLeafNameFromUri -UriText ([string]$buffers[$bufferIndex].uri)
        $bufferPath = Join-Path $StageDirectory $bufferLeafName
        $bufferBytes = [IO.File]::ReadAllBytes($bufferPath)
        $viewOffset = 0
        if ($oldView.PSObject.Properties.Match('byteOffset').Count -gt 0) {
            $viewOffset = [int]$oldView.byteOffset
        }
        $accessorOffset = 0
        if ($accessor.PSObject.Properties.Match('byteOffset').Count -gt 0) {
            $accessorOffset = [int]$accessor.byteOffset
        }
        $sourceStride = 16
        if ($oldView.PSObject.Properties.Match('byteStride').Count -gt 0) {
            $sourceStride = [int]$oldView.byteStride
        }
        if ($sourceStride -lt 16) {
            throw "JOINTS accessor $accessorIndex has an invalid byte stride."
        }
        $count = [int]$accessor.count
        $lastByte = $viewOffset + $accessorOffset + (($count - 1) * $sourceStride) + 16
        if ($count -lt 0 -or $lastByte -gt $bufferBytes.Length) {
            throw "JOINTS accessor $accessorIndex exceeds its buffer."
        }

        $packedStream = [IO.MemoryStream]::new()
        $packedWriter = [IO.BinaryWriter]::new(
            $packedStream,
            [Text.UTF8Encoding]::new($false),
            $true
        )
        try {
            for ($element = 0; $element -lt $count; $element++) {
                $elementOffset = $viewOffset + $accessorOffset + ($element * $sourceStride)
                for ($component = 0; $component -lt 4; $component++) {
                    $value = [double][BitConverter]::ToSingle(
                        $bufferBytes,
                        $elementOffset + ($component * 4)
                    )
                    $rounded = [Math]::Round($value)
                    if ([double]::IsNaN($value) -or [double]::IsInfinity($value) -or
                        [Math]::Abs($value - $rounded) -gt 0.0001 -or
                        $rounded -lt 0 -or $rounded -gt [uint16]::MaxValue) {
                        throw "JOINTS accessor $accessorIndex contains invalid joint index $value."
                    }
                    $packedWriter.Write([uint16]$rounded)
                }
            }
        }
        finally {
            $packedWriter.Dispose()
        }
        $packedBytes = $packedStream.ToArray()
        $packedStream.Dispose()

        $padding = (4 - ($bufferBytes.Length % 4)) % 4
        $newOffset = $bufferBytes.Length + $padding
        $newBufferBytes = New-Object byte[] ($newOffset + $packedBytes.Length)
        [Buffer]::BlockCopy($bufferBytes, 0, $newBufferBytes, 0, $bufferBytes.Length)
        [Buffer]::BlockCopy($packedBytes, 0, $newBufferBytes, $newOffset, $packedBytes.Length)
        [IO.File]::WriteAllBytes($bufferPath, $newBufferBytes)
        $buffers[$bufferIndex].byteLength = $newBufferBytes.Length

        $newView = [pscustomobject]@{
            buffer = $bufferIndex
            byteOffset = $newOffset
            byteLength = $packedBytes.Length
            target = 34962
        }
        $newViewIndex = $bufferViews.Count
        [void]$bufferViews.Add($newView)
        $accessor.bufferView = $newViewIndex
        $accessor.byteOffset = 0
        $accessor.componentType = 5123
        $accessor.PSObject.Properties.Remove('normalized')
        $accessor.PSObject.Properties.Remove('min')
        $accessor.PSObject.Properties.Remove('max')
    }



    for ($accessorIndex = 0; $accessorIndex -lt $accessors.Count; $accessorIndex++) {
        if (-not $positionAccessorIndices.Contains($accessorIndex)) {
            $accessors[$accessorIndex].PSObject.Properties.Remove('min')
            $accessors[$accessorIndex].PSObject.Properties.Remove('max')
        }
    }

    foreach ($node in @($Gltf.nodes)) {

        $node.PSObject.Properties.Remove('jointName')
        $node.PSObject.Properties.Remove('skeletons')
    }
    $skins = @($Gltf.skins)
    if ($skins.Count -eq 0) {
        throw 'The glTF scene does not contain a skin.'
    }
    foreach ($skin in $skins) {

        $skin.PSObject.Properties.Remove('bindShapeMatrix')
        if (@($skin.joints).Count -eq 0) {
            throw 'A glTF skin has no joints.'
        }
        foreach ($jointNodeIndex in @($skin.joints)) {
            if ([int]$jointNodeIndex -lt 0 -or [int]$jointNodeIndex -ge @($Gltf.nodes).Count) {
                throw 'A glTF skin refers to an invalid joint node.'
            }
        }
        if ($skin.PSObject.Properties.Match('inverseBindMatrices').Count -eq 0) {
            throw 'A glTF skin has no inverse bind matrices.'
        }
        $inverseBindAccessorIndex = [int]$skin.inverseBindMatrices
        if ($inverseBindAccessorIndex -lt 0 -or $inverseBindAccessorIndex -ge $accessors.Count -or
            [string]$accessors[$inverseBindAccessorIndex].type -ne 'MAT4' -or
            [int]$accessors[$inverseBindAccessorIndex].componentType -ne 5126) {
            throw 'A glTF skin has invalid inverse bind matrices.'
        }
        $inverseBindViewIndex = [int]$accessors[$inverseBindAccessorIndex].bufferView
        if ($inverseBindViewIndex -lt 0 -or $inverseBindViewIndex -ge $bufferViews.Count) {
            throw 'A glTF skin has an invalid inverse bind matrix bufferView.'
        }

        $bufferViews[$inverseBindViewIndex].PSObject.Properties.Remove('target')
    }

    $skinnedNodeCount = 0
    $skinnedNodeIndices = [Collections.Generic.HashSet[int]]::new()
    $nodes = @($Gltf.nodes)
    for ($nodeIndex = 0; $nodeIndex -lt $nodes.Count; $nodeIndex++) {
        $node = $nodes[$nodeIndex]
        if ($node.PSObject.Properties.Match('skin').Count -gt 0) {
            $skinIndex = [int]$node.skin
            if ($skinIndex -lt 0 -or $skinIndex -ge $skins.Count -or
                $node.PSObject.Properties.Match('mesh').Count -eq 0) {
                throw 'A glTF node has an invalid skin assignment.'
            }
            $skinnedNodeCount++
            [void]$skinnedNodeIndices.Add($nodeIndex)
        }
    }
    if ($skinnedNodeCount -eq 0) {
        throw 'The glTF scene has no mesh node assigned to its skin.'
    }



    foreach ($node in $nodes) {
        if ($node.PSObject.Properties.Match('children').Count -eq 0) {
            continue
        }
        $remainingChildren = @($node.children | Where-Object {
            -not $skinnedNodeIndices.Contains([int]$_)
        })
        if ($remainingChildren.Count -gt 0) {
            $node.children = $remainingChildren
        }
        else {
            $node.PSObject.Properties.Remove('children')
        }
    }
    foreach ($scene in @($Gltf.scenes)) {
        $sceneRoots = [Collections.Generic.List[int]]::new()
        foreach ($rootIndex in @($scene.nodes)) {
            if (-not $sceneRoots.Contains([int]$rootIndex)) {
                [void]$sceneRoots.Add([int]$rootIndex)
            }
        }
        foreach ($skinnedNodeIndex in $skinnedNodeIndices) {
            if (-not $sceneRoots.Contains($skinnedNodeIndex)) {
                [void]$sceneRoots.Add($skinnedNodeIndex)
            }
        }
        $scene.nodes = @($sceneRoots.ToArray())
    }

    $Gltf.bufferViews = @($bufferViews.ToArray())
    $Gltf.buffers = @($buffers)
    return [pscustomobject]@{
        JointAccessorCount = $jointAccessorIndices.Count
        SkinCount = $skins.Count
        SkinnedNodeCount = $skinnedNodeCount
    }
}

function Write-JsonWithoutBom {
    param(
        [Parameter(Mandatory = $true)][object]$Value,
        [Parameter(Mandatory = $true)][string]$Path
    )

    $json = $Value | ConvertTo-Json -Depth 100
    [IO.File]::WriteAllText($Path, $json, [Text.UTF8Encoding]::new($false))
}

function Add-AlignedBytes {
    param(
        [Parameter(Mandatory = $true)][IO.MemoryStream]$Destination,
        [Parameter(Mandatory = $true)][byte[]]$Bytes
    )

    while (($Destination.Length % 4) -ne 0) {
        $Destination.WriteByte(0)
    }
    $offset = $Destination.Length
    $Destination.Write($Bytes, 0, $Bytes.Length)
    return [int64]$offset
}

function New-Glb2 {
    param(
        [Parameter(Mandatory = $true)][object]$Gltf,
        [Parameter(Mandatory = $true)][string]$StageDirectory,
        [Parameter(Mandatory = $true)][string]$OutputPath
    )

    $copyJson = $Gltf | ConvertTo-Json -Depth 100
    $glbDocument = $copyJson | ConvertFrom-Json
    $combinedBinary = [IO.MemoryStream]::new()
    try {
        $oldBuffers = @($glbDocument.buffers)
        $bufferBaseOffsets = [Collections.Generic.Dictionary[int, long]]::new()
        for ($bufferIndex = 0; $bufferIndex -lt $oldBuffers.Count; $bufferIndex++) {
            $buffer = $oldBuffers[$bufferIndex]
            if ($buffer.PSObject.Properties.Match('uri').Count -eq 0) {
                throw "glTF buffer $bufferIndex has no external URI before GLB packing."
            }
            $leafName = Get-SafeLeafNameFromUri -UriText ([string]$buffer.uri)
            $bufferPath = Join-Path $StageDirectory $leafName
            if (-not [IO.File]::Exists($bufferPath)) {
                throw "glTF buffer is missing: $leafName"
            }
            $baseOffset = Add-AlignedBytes -Destination $combinedBinary -Bytes ([IO.File]::ReadAllBytes($bufferPath))
            $bufferBaseOffsets.Add($bufferIndex, $baseOffset)
        }

        $bufferViews = [Collections.Generic.List[object]]::new()
        foreach ($view in @($glbDocument.bufferViews)) {
            $oldBufferIndex = [int]$view.buffer
            if (-not $bufferBaseOffsets.ContainsKey($oldBufferIndex)) {
                throw "A bufferView refers to missing buffer $oldBufferIndex."
            }
            $oldOffset = 0L
            if ($view.PSObject.Properties.Match('byteOffset').Count -gt 0) {
                $oldOffset = [int64]$view.byteOffset
            }
            $view.buffer = 0
            $view | Add-Member -NotePropertyName byteOffset -NotePropertyValue (
                $bufferBaseOffsets[$oldBufferIndex] + $oldOffset
            ) -Force
            [void]$bufferViews.Add($view)
        }

        foreach ($image in @($glbDocument.images)) {
            if ($image.PSObject.Properties.Match('uri').Count -eq 0) {
                throw 'A glTF image has no external PNG URI before GLB packing.'
            }
            $leafName = Get-SafeLeafNameFromUri -UriText ([string]$image.uri)
            $imagePath = Join-Path $StageDirectory $leafName
            if (-not (Test-PngFile -Path $imagePath)) {
                throw "Cannot embed invalid or missing PNG: $leafName"
            }
            $imageBytes = [IO.File]::ReadAllBytes($imagePath)
            $imageOffset = Add-AlignedBytes -Destination $combinedBinary -Bytes $imageBytes
            $imageBufferView = [pscustomobject]@{
                buffer = 0
                byteOffset = $imageOffset
                byteLength = $imageBytes.Length
            }
            $imageBufferViewIndex = $bufferViews.Count
            [void]$bufferViews.Add($imageBufferView)
            $image.PSObject.Properties.Remove('uri')
            $image | Add-Member -NotePropertyName bufferView -NotePropertyValue $imageBufferViewIndex -Force
            $image | Add-Member -NotePropertyName mimeType -NotePropertyValue 'image/png' -Force
        }

        $glbDocument.bufferViews = @($bufferViews.ToArray())
        $glbDocument.buffers = @([pscustomobject]@{ byteLength = $combinedBinary.Length })
        $glbJson = $glbDocument | ConvertTo-Json -Depth 100
        $jsonBytes = [Text.UTF8Encoding]::new($false).GetBytes($glbJson)
        $jsonPadding = (4 - ($jsonBytes.Length % 4)) % 4
        $binaryBytes = $combinedBinary.ToArray()
        $binaryPadding = (4 - ($binaryBytes.Length % 4)) % 4
        $totalLength = 12L + 8L + $jsonBytes.Length + $jsonPadding + 8L + $binaryBytes.Length + $binaryPadding
        if ($totalLength -gt [uint32]::MaxValue) {
            throw 'The generated GLB exceeds the GLB 2 size limit.'
        }

        $fileStream = [IO.File]::Open($OutputPath, [IO.FileMode]::Create, [IO.FileAccess]::Write, [IO.FileShare]::None)
        $writer = [IO.BinaryWriter]::new($fileStream, [Text.UTF8Encoding]::new($false), $false)
        try {
            $writer.Write([byte[]](103, 108, 84, 70))
            $writer.Write([uint32]2)
            $writer.Write([uint32]$totalLength)
            $writer.Write([uint32]($jsonBytes.Length + $jsonPadding))
            $writer.Write([uint32]0x4E4F534A)
            $writer.Write($jsonBytes)
            for ($index = 0; $index -lt $jsonPadding; $index++) {
                $writer.Write([byte]0x20)
            }
            $writer.Write([uint32]($binaryBytes.Length + $binaryPadding))
            $writer.Write([uint32]0x004E4942)
            $writer.Write($binaryBytes)
            for ($index = 0; $index -lt $binaryPadding; $index++) {
                $writer.Write([byte]0)
            }
        }
        finally {
            $writer.Dispose()
        }
    }
    finally {
        $combinedBinary.Dispose()
    }
}

function Test-Glb2 {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][bool]$ExpectPose
    )

    $bytes = [IO.File]::ReadAllBytes($Path)
    if ($bytes.Length -lt 28 -or [Text.Encoding]::ASCII.GetString($bytes, 0, 4) -ne 'glTF') {
        throw 'Generated GLB has an invalid header.'
    }
    if ([BitConverter]::ToUInt32($bytes, 4) -ne 2 -or
        [BitConverter]::ToUInt32($bytes, 8) -ne $bytes.Length) {
        throw 'Generated GLB is not a complete GLB 2 container.'
    }

    $offset = 12
    $jsonOffset = -1
    $jsonLength = 0
    $binaryOffset = -1
    $binaryLength = 0
    while ($offset -lt $bytes.Length) {
        if ($offset + 8 -gt $bytes.Length) {
            throw 'Generated GLB contains a truncated chunk header.'
        }
        $chunkLength = [BitConverter]::ToUInt32($bytes, $offset)
        $chunkType = [BitConverter]::ToUInt32($bytes, $offset + 4)
        $chunkDataOffset = $offset + 8
        if ($chunkDataOffset + $chunkLength -gt $bytes.Length) {
            throw 'Generated GLB contains a truncated chunk.'
        }
        if ($chunkType -eq 0x4E4F534A) {
            $jsonOffset = $chunkDataOffset
            $jsonLength = $chunkLength
        }
        elseif ($chunkType -eq 0x004E4942) {
            $binaryOffset = $chunkDataOffset
            $binaryLength = $chunkLength
        }
        $offset = $chunkDataOffset + $chunkLength
    }
    if ($jsonOffset -lt 0 -or $binaryOffset -lt 0) {
        throw 'Generated GLB must contain JSON and BIN chunks.'
    }

    $json = [Text.Encoding]::UTF8.GetString($bytes, $jsonOffset, $jsonLength).TrimEnd([char]0, [char]32)
    $document = $json | ConvertFrom-Json
    if ([string]$document.asset.version -ne '2.0') {
        throw 'Generated GLB JSON does not declare glTF 2.0.'
    }
    if (@($document.skins).Count -eq 0) {
        throw 'Generated GLB does not contain a skin.'
    }
    if (@($document.textures).Count -eq 0 -or @($document.images).Count -eq 0) {
        throw 'Generated GLB does not contain textures.'
    }
    if (@($document.buffers).Count -ne 1 -or
        $document.buffers[0].PSObject.Properties.Match('uri').Count -ne 0) {
        throw 'Generated GLB still contains an external buffer URI.'
    }
    foreach ($image in @($document.images)) {
        if ($image.PSObject.Properties.Match('uri').Count -ne 0 -or
            [string]$image.mimeType -ne 'image/png') {
            throw 'Generated GLB still contains an external or non-PNG image.'
        }
        $viewIndex = [int]$image.bufferView
        $views = @($document.bufferViews)
        if ($viewIndex -lt 0 -or $viewIndex -ge $views.Count) {
            throw 'Generated GLB image has an invalid bufferView.'
        }
        $view = $views[$viewIndex]
        $viewOffset = 0
        if ($view.PSObject.Properties.Match('byteOffset').Count -gt 0) {
            $viewOffset = [int]$view.byteOffset
        }
        $pngOffset = $binaryOffset + $viewOffset
        if ($pngOffset + 8 -gt $binaryOffset + $binaryLength -or
            $bytes[$pngOffset] -ne 137 -or $bytes[$pngOffset + 1] -ne 80 -or
            $bytes[$pngOffset + 2] -ne 78 -or $bytes[$pngOffset + 3] -ne 71) {
            throw 'Generated GLB image bufferView does not contain PNG data.'
        }
    }
    if ($ExpectPose -and
        ($document.PSObject.Properties.Match('extras').Count -eq 0 -or
         $document.extras.PSObject.Properties.Match('botwPose').Count -eq 0 -or
         [int]$document.extras.botwPose.changedAnimatedNodes -le 0)) {
        throw 'Generated GLB does not contain the requested baked pose.'
    }

    return [pscustomobject]@{
        MeshCount = @($document.meshes).Count
        SkinCount = @($document.skins).Count
        TextureCount = @($document.textures).Count
        EmbeddedImageCount = @($document.images).Count
        ByteLength = $bytes.Length
    }
}

$modelPath = Resolve-ExistingFile -Path $ModelInput
$texturePaths = @($TextureInput | ForEach-Object { Resolve-ExistingFile -Path $_ })
$animationPath = $null
if (-not [string]::IsNullOrWhiteSpace($AnimationBfres)) {
    $animationPath = Resolve-ExistingFile -Path $AnimationBfres
}

if ($ModelName -ne [IO.Path]::GetFileName($ModelName) -or
    $ModelName.IndexOfAny([IO.Path]::GetInvalidFileNameChars()) -ge 0) {
    throw "ModelName is not safe as an output filename: $ModelName"
}

if ([string]::IsNullOrWhiteSpace($SwitchToolboxPath)) {
    $SwitchToolboxPath = $env:SWITCH_TOOLBOX_PATH
}
if ([string]::IsNullOrWhiteSpace($SwitchToolboxPath)) {
    $repositoryRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
    $localToolbox = Join-Path $repositoryRoot '.work\switch-toolbox'
    if ([IO.Directory]::Exists($localToolbox)) {
        $SwitchToolboxPath = $localToolbox
    }
    else {
        throw 'Set -SwitchToolboxPath or the SWITCH_TOOLBOX_PATH environment variable.'
    }
}
$toolboxPath = Resolve-ExistingDirectory -Path $SwitchToolboxPath

$requiredToolboxFiles = @(
    'Toolbox.Library.dll',
    'FirstPlugin.Plg.dll',
    'Syroot.NintenTools.NSW.Bfres.dll',
    'Syroot.NintenTools.NSW.Bntx.dll',
    'AssimpNet.dll',
    'Lib\OpenTK.dll'
)
foreach ($relativePath in $requiredToolboxFiles) {
    $requiredPath = Join-Path $toolboxPath $relativePath
    if (-not [IO.File]::Exists($requiredPath)) {
        throw "Switch Toolbox dependency is missing: $relativePath"
    }
}
$nativeRid = if ([Environment]::Is64BitProcess) { 'win-x64' } else { 'win-x86' }
$assimpNativeDirectory = Join-Path $toolboxPath "runtimes\$nativeRid\native"
if (-not [IO.File]::Exists((Join-Path $assimpNativeDirectory 'assimp.dll'))) {
    throw "Switch Toolbox Assimp runtime is missing for $nativeRid."
}

$outputPath = Get-UnresolvedFullPath -Path $OutputDirectory
if (-not [IO.Directory]::Exists($outputPath)) {
    [void][IO.Directory]::CreateDirectory($outputPath)
}
$outputPath = Resolve-ExistingDirectory -Path $outputPath
$stageDirectory = Join-Path $outputPath ('.botw-model-' + [Guid]::NewGuid().ToString('N'))
$stageInfo = [IO.Directory]::CreateDirectory($stageDirectory)
$stageDirectory = $stageInfo.FullName

$originalProcessDirectory = [Environment]::CurrentDirectory
$originalPathEnvironment = $env:PATH
$script:OwnedStreams = [Collections.Generic.List[IDisposable]]::new()
$textureContainers = [Collections.Generic.List[object]]::new()

try {
    [Environment]::CurrentDirectory = $toolboxPath
    $env:PATH = "$assimpNativeDirectory;$toolboxPath;$(Join-Path $toolboxPath 'Lib');$originalPathEnvironment"
    Add-Type -AssemblyName System.Windows.Forms
    Add-Type -AssemblyName System.Drawing
    [void][Reflection.Assembly]::LoadFrom((Join-Path $toolboxPath 'Lib\OpenTK.dll'))

    $managedCandidates = @(
        Get-ChildItem -LiteralPath (Join-Path $toolboxPath 'Lib') -File -Filter '*.dll'
        Get-ChildItem -LiteralPath $toolboxPath -File -Filter '*.dll'
    )
    for ($pass = 0; $pass -lt 3; $pass++) {
        foreach ($candidate in $managedCandidates) {
            try {
                [void][Reflection.Assembly]::LoadFrom($candidate.FullName)
            }
            catch [BadImageFormatException] {

            }
            catch {

            }
        }
    }

    $requiredTypes = @(
        'Toolbox.Library.Yaz0',
        'Toolbox.Library.DAE',
        'Bfres.Structs.FMDL',
        'Bfres.Structs.FSKA',
        'FirstPlugin.BfresSwitch',
        'FirstPlugin.TextureData',
        'Syroot.NintenTools.NSW.Bfres.ResFile',
        'Syroot.NintenTools.NSW.Bntx.BntxFile',
        'Assimp.AssimpContext'
    )
    foreach ($typeName in $requiredTypes) {
        $loadedType = $null
        foreach ($assembly in [AppDomain]::CurrentDomain.GetAssemblies()) {
            $loadedType = $assembly.GetType($typeName, $false)
            if ($null -ne $loadedType) {
                break
            }
        }
        if ($null -eq $loadedType) {
            throw "Switch Toolbox type failed to load: $typeName"
        }
    }

    if ($null -eq ('Eden.BotwCompanion.HeadlessConverter' -as [type])) {
        $helperSource = @'
using System;
using System.Collections.Generic;
using Assimp;
using Bfres.Structs;
using FirstPlugin;
using OpenTK;
using Toolbox.Library;

namespace Eden.BotwCompanion
{
    public sealed class PoseSample
    {
        public float Frame;
        public Dictionary<string, float[]> Matrices =
            new Dictionary<string, float[]>(StringComparer.Ordinal);
        public HashSet<string> AnimatedNodes =
            new HashSet<string>(StringComparer.Ordinal);
    }

    public static class HeadlessConverter
    {
        public static object WrapModel(object rawModel)
        {
            FMDL model = new FMDL();
            BfresSwitch.ReadModel(model, (Syroot.NintenTools.NSW.Bfres.Model)rawModel);
            return model;
        }

        public static void ExportDae(string path, object wrappedModel)
        {
            FMDL model = (FMDL)wrappedModel;
            DAE.ExportSettings settings = new DAE.ExportSettings();
            settings.SuppressConfirmDialog = true;
            settings.ExportTextures = false;
            settings.ImageExtension = ".png";
            DAE.Export(
                path,
                settings,
                (STGenericModel)model,
                new List<STGenericTexture>(),
                (STSkeleton)model.GenericSkeleton,
                new List<int>());
        }

        public static void ExportTexture(object rawTexture, object bntxFile, string path)
        {
            TextureData texture = new TextureData(
                (Syroot.NintenTools.NSW.Bntx.Texture)rawTexture,
                (Syroot.NintenTools.NSW.Bntx.BntxFile)bntxFile);
            texture.Export(path);
        }

        public static void ExportGltf2(string daePath, string gltfPath, string outputDirectory)
        {
            using (AssimpContext context = new AssimpContext())
            {
                Scene scene = context.ImportFile(daePath);
                if (scene == null)
                    throw new InvalidOperationException("Assimp failed to import the COLLADA scene.");
                string originalDirectory = Environment.CurrentDirectory;
                try
                {
                    Environment.CurrentDirectory = outputDirectory;
                    if (!context.ExportFile(scene, System.IO.Path.GetFileName(gltfPath), "gltf2"))
                        throw new InvalidOperationException("Assimp failed to export glTF 2.");
                }
                finally
                {
                    Environment.CurrentDirectory = originalDirectory;
                }
            }
        }

        public static PoseSample SamplePose(
            object wrappedModel,
            object rawAnimation,
            float requestedFrame)
        {
            FMDL model = (FMDL)wrappedModel;
            Syroot.NintenTools.NSW.Bfres.SkeletalAnim animation =
                (Syroot.NintenTools.NSW.Bfres.SkeletalAnim)rawAnimation;
            FSKA genericAnimation = new FSKA(animation);
            STSkeleton skeleton = model.GenericSkeleton;
            skeleton.reset();
            float frame = requestedFrame < 0.0f
                ? animation.FrameCount * 0.5f
                : Math.Max(0.0f, Math.Min(requestedFrame, animation.FrameCount));
            genericAnimation.SetFrame(frame);
            genericAnimation.NextFrame(skeleton, false, false);

            PoseSample result = new PoseSample();
            result.Frame = frame;
            foreach (Toolbox.Library.Animations.Animation.KeyNode animatedBone in genericAnimation.Bones)
                result.AnimatedNodes.Add("Armature_" + animatedBone.Text);
            foreach (STBone bone in skeleton.bones)
            {
                Matrix4 matrix =
                    Matrix4.CreateScale(bone.sca) *
                    Matrix4.CreateFromQuaternion(bone.rot) *
                    Matrix4.CreateTranslation(bone.pos);
                // Assimp transposes the COLLADA matrix into this glTF column-major ordering.
                result.Matrices.Add("Armature_" + bone.Text, new float[] {
                    matrix.M11, matrix.M12, matrix.M13, matrix.M14,
                    matrix.M21, matrix.M22, matrix.M23, matrix.M24,
                    matrix.M31, matrix.M32, matrix.M33, matrix.M34,
                    matrix.M41, matrix.M42, matrix.M43, matrix.M44
                });
            }
            return result;
        }
    }
}
'@
        $compilerReferences = @(
            [AppDomain]::CurrentDomain.GetAssemblies() |
                Where-Object { -not $_.IsDynamic -and -not [string]::IsNullOrWhiteSpace($_.Location) } |
                ForEach-Object { $_.Location } |
                Select-Object -Unique
        )
        [void](Add-Type -TypeDefinition $helperSource -ReferencedAssemblies $compilerReferences)
    }

    Write-Host "Reading model '$ModelName'..."
    $modelResource = Open-BfresResource -Path $modelPath
    $availableModelNames = @($modelResource.Models | ForEach-Object { [string]$_.Name })
    $rawModel = @($modelResource.Models | Where-Object { $_.Name -ceq $ModelName }) | Select-Object -First 1
    if ($null -eq $rawModel) {
        throw "Model '$ModelName' was not found. Available models: $($availableModelNames -join ', ')"
    }
    $wrappedModel = Invoke-QuietThirdParty {
        [Eden.BotwCompanion.HeadlessConverter]::WrapModel($rawModel)
    }

    $textureCatalog = [Collections.Generic.Dictionary[string, object]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach ($texturePath in $texturePaths) {
        $resourceStream = Open-NintendoResourceStream -Path $texturePath
        $magic = Get-StreamMagic -Stream $resourceStream
        $bntxStreams = [Collections.Generic.List[IO.Stream]]::new()
        if ($magic -eq 'BNTX') {
            [void]$bntxStreams.Add($resourceStream)
        }
        elseif ($magic -eq 'FRES') {
            $textureResource = Invoke-QuietThirdParty {
                [Syroot.NintenTools.NSW.Bfres.ResFile]::new($resourceStream, $true)
            }
            foreach ($externalFile in @($textureResource.ExternalFiles)) {
                if ($null -eq $externalFile.Data -or $externalFile.Data.Length -lt 4) {
                    continue
                }
                $externalStream = [IO.MemoryStream]::new($externalFile.Data, $false)
                [void]$script:OwnedStreams.Add($externalStream)
                if ((Get-StreamMagic -Stream $externalStream) -eq 'BNTX') {
                    [void]$bntxStreams.Add($externalStream)
                }
            }
        }
        else {
            throw "Texture input is neither BFRES nor BNTX after Yaz0 decompression: $texturePath"
        }

        if ($bntxStreams.Count -eq 0) {
            throw "No BNTX texture container was found in: $texturePath"
        }
        foreach ($bntxStream in $bntxStreams) {
            $bntx = Invoke-QuietThirdParty {
                [Syroot.NintenTools.NSW.Bntx.BntxFile]::new($bntxStream, $true)
            }
            [void]$textureContainers.Add($bntx)
            foreach ($rawTexture in @($bntx.Textures)) {
                $textureName = [string]$rawTexture.Name
                if (-not $textureCatalog.ContainsKey($textureName)) {
                    $textureCatalog.Add($textureName, [pscustomobject]@{
                        Raw = $rawTexture
                        Container = $bntx
                        Source = [IO.Path]::GetFileName($texturePath)
                    })
                }
            }
        }
    }

    $daePath = Join-Path $stageDirectory "$ModelName.dae"
    Invoke-QuietThirdParty {
        [Eden.BotwCompanion.HeadlessConverter]::ExportDae($daePath, $wrappedModel)
    }
    if (-not [IO.File]::Exists($daePath)) {
        throw 'Switch Toolbox did not produce the intermediate COLLADA scene.'
    }

    [xml]$daeDocument = [IO.File]::ReadAllText($daePath)
    $namespaceManager = [Xml.XmlNamespaceManager]::new($daeDocument.NameTable)
    $namespaceManager.AddNamespace('c', $daeDocument.DocumentElement.NamespaceURI)
    $imageNodes = @($daeDocument.SelectNodes('//c:library_images/c:image/c:init_from', $namespaceManager))
    $referencedTextures = [Collections.Generic.Dictionary[string, string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach ($imageNode in $imageNodes) {
        $leafName = Get-SafeLeafNameFromUri -UriText ([string]$imageNode.InnerText)
        if ([IO.Path]::GetExtension($leafName) -ine '.png') {
            $leafName = [IO.Path]::GetFileNameWithoutExtension($leafName) + '.png'
        }
        $textureName = [IO.Path]::GetFileNameWithoutExtension($leafName)
        if ($referencedTextures.ContainsKey($textureName) -and
            $referencedTextures[$textureName] -ine $leafName) {
            throw "COLLADA maps texture '$textureName' to multiple filenames."
        }
        $referencedTextures[$textureName] = $leafName
        $imageNode.InnerText = $leafName
    }
    $xmlSettings = [Xml.XmlWriterSettings]::new()
    $xmlSettings.Indent = $true
    $xmlSettings.Encoding = [Text.UTF8Encoding]::new($false)
    $xmlWriter = [Xml.XmlWriter]::Create($daePath, $xmlSettings)
    try {
        $daeDocument.Save($xmlWriter)
    }
    finally {
        $xmlWriter.Dispose()
    }

    $exportedCandidates = [Collections.Generic.Dictionary[string, object]]::new([StringComparer]::OrdinalIgnoreCase)
    $missingDaeTextures = [Collections.Generic.List[string]]::new()
    foreach ($reference in $referencedTextures.GetEnumerator()) {
        $catalogEntry = $null
        if (-not $textureCatalog.TryGetValue($reference.Key, [ref]$catalogEntry)) {
            [void]$missingDaeTextures.Add($reference.Key)
            continue
        }
        $pngPath = Join-Path $stageDirectory $reference.Value
        Invoke-QuietThirdParty {
            [Eden.BotwCompanion.HeadlessConverter]::ExportTexture(
                $catalogEntry.Raw,
                $catalogEntry.Container,
                $pngPath
            )
        }
        if (-not (Test-PngFile -Path $pngPath)) {
            throw "Switch Toolbox did not export a valid PNG for texture '$($reference.Key)'."
        }
        $exportedCandidates[$reference.Value] = $pngPath
    }

    $gltfPath = Join-Path $stageDirectory "$ModelName.gltf"
    Invoke-QuietThirdParty {
        [Eden.BotwCompanion.HeadlessConverter]::ExportGltf2($daePath, $gltfPath, $stageDirectory)
    }
    if (-not [IO.File]::Exists($gltfPath)) {
        throw 'Assimp did not produce glTF 2 output.'
    }
    $gltf = [IO.File]::ReadAllText($gltfPath) | ConvertFrom-Json
    if ([string]$gltf.asset.version -ne '2.0') {
        throw 'Assimp output is not glTF 2.0.'
    }

    $textureRepair = Repair-GltfTextures -Gltf $gltf -StageDirectory $stageDirectory `
        -ExportedCandidates $exportedCandidates
    if ($textureRepair.ImageCount -eq 0 -or $textureRepair.TextureCount -eq 0) {
        throw 'No referenced BNTX textures survived glTF material conversion.'
    }

    $poseResult = $null
    if ($null -ne $animationPath) {
        Write-Host "Baking pose '$Pose'..."
        $animationResource = Open-BfresResource -Path $animationPath
        $rawAnimation = @($animationResource.SkeletalAnims | Where-Object { $_.Name -ceq $Pose }) |
            Select-Object -First 1
        if ($null -eq $rawAnimation) {
            throw "Skeletal animation '$Pose' was not found in the animation BFRES."
        }
        $poseResult = Add-PoseToGltf -Gltf $gltf -WrappedModel $wrappedModel `
            -Animation $rawAnimation -AnimationName $Pose -RequestedFrame $PoseFrame
    }

    foreach ($buffer in @($gltf.buffers)) {
        if ($buffer.PSObject.Properties.Match('uri').Count -eq 0) {
            throw 'Assimp glTF buffer has no URI.'
        }
        $bufferLeafName = Get-SafeLeafNameFromUri -UriText ([string]$buffer.uri)
        $buffer.uri = ConvertTo-RelativeGltfUri -LeafName $bufferLeafName
        if (-not [IO.File]::Exists((Join-Path $stageDirectory $bufferLeafName))) {
            throw "Assimp glTF buffer is missing: $bufferLeafName"
        }
    }
    $skinRepair = Repair-GltfSkinData -Gltf $gltf -StageDirectory $stageDirectory
    Write-JsonWithoutBom -Value $gltf -Path $gltfPath

    $glbPath = Join-Path $stageDirectory "$ModelName.glb"
    New-Glb2 -Gltf $gltf -StageDirectory $stageDirectory -OutputPath $glbPath
    $glbValidation = Test-Glb2 -Path $glbPath -ExpectPose ($null -ne $animationPath)

    $artifactNames = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    [void]$artifactNames.Add([IO.Path]::GetFileName($gltfPath))
    [void]$artifactNames.Add([IO.Path]::GetFileName($glbPath))
    foreach ($buffer in @($gltf.buffers)) {
        [void]$artifactNames.Add((Get-SafeLeafNameFromUri -UriText ([string]$buffer.uri)))
    }
    foreach ($imageName in @($textureRepair.ImageNames)) {
        [void]$artifactNames.Add([string]$imageName)
    }

    foreach ($artifactName in $artifactNames) {
        $destination = Join-Path $outputPath $artifactName
        if ([IO.File]::Exists($destination) -and -not $Force) {
            throw "Output already exists (use -Force to replace it): $destination"
        }
    }
    foreach ($artifactName in $artifactNames) {
        Copy-Item -LiteralPath (Join-Path $stageDirectory $artifactName) `
            -Destination (Join-Path $outputPath $artifactName) -Force:$Force
    }

    if ($missingDaeTextures.Count -gt 0) {
        Write-Warning (
            'Shared textures not present in the supplied BNTX inputs were removed from material slots: ' +
            (($missingDaeTextures | Sort-Object -Unique) -join ', ')
        )
    }
    if ($textureRepair.MissingImages.Count -gt 0) {
        Write-Warning (
            'Unresolved glTF image references were removed to keep the output self-contained: ' +
            ($textureRepair.MissingImages -join ', ')
        )
    }

    $publishedGltf = Join-Path $outputPath "$ModelName.gltf"
    $publishedGlb = Join-Path $outputPath "$ModelName.glb"
    Write-Host (
        "Converted '$ModelName': $($glbValidation.MeshCount) mesh(es), " +
        "$($glbValidation.SkinCount) skin(s), $($glbValidation.EmbeddedImageCount) embedded PNG(s)."
    )
    [pscustomobject]@{
        Model = $ModelName
        Gltf = $publishedGltf
        Glb = $publishedGlb
        Textures = @($textureRepair.ImageNames | ForEach-Object { Join-Path $outputPath $_ })
        MeshCount = $glbValidation.MeshCount
        SkinCount = $glbValidation.SkinCount
        JointAccessorCount = $skinRepair.JointAccessorCount
        EmbeddedTextureCount = $glbValidation.EmbeddedImageCount
        Pose = if ($null -ne $poseResult) { $poseResult.Animation } else { $null }
        PoseFrame = if ($null -ne $poseResult) { $poseResult.Frame } else { $null }
    }
}
finally {
    foreach ($stream in $script:OwnedStreams) {
        try {
            $stream.Dispose()
        }
        catch {
        }
    }
    [Environment]::CurrentDirectory = $originalProcessDirectory
    $env:PATH = $originalPathEnvironment

    $normalizedOutput = $outputPath.TrimEnd([IO.Path]::DirectorySeparatorChar) + [IO.Path]::DirectorySeparatorChar
    $normalizedStage = $stageDirectory.TrimEnd([IO.Path]::DirectorySeparatorChar) + [IO.Path]::DirectorySeparatorChar
    if ($normalizedStage.StartsWith($normalizedOutput, [StringComparison]::OrdinalIgnoreCase) -and
        [IO.Path]::GetFileName($stageDirectory).StartsWith('.botw-model-', [StringComparison]::Ordinal)) {
        try {
            if ([IO.Directory]::Exists($stageDirectory)) {
                [IO.Directory]::Delete($stageDirectory, $true)
            }
        }
        catch {
            Write-Warning "Could not remove temporary conversion directory: $stageDirectory"
        }
    }
}
