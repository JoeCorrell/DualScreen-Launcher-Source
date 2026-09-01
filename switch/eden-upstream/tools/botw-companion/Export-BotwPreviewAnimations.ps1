param(
    [Parameter(Mandatory = $true)][string]$ModelBfres,
    [Parameter(Mandatory = $true)][string]$AnimationBfres,
    [Parameter(Mandatory = $true)][string]$SwitchToolboxPath,
    [Parameter(Mandatory = $true)][string]$OutputPath
)

$ErrorActionPreference = 'Stop'
$originalDirectory = [Environment]::CurrentDirectory
$originalPath = $env:PATH
$ModelBfres = [IO.Path]::GetFullPath($ModelBfres)
$AnimationBfres = [IO.Path]::GetFullPath($AnimationBfres)
$SwitchToolboxPath = [IO.Path]::GetFullPath($SwitchToolboxPath)
$OutputPath = [IO.Path]::GetFullPath($OutputPath)
$streams = [Collections.Generic.List[IDisposable]]::new()

function Open-Resource([string]$path) {
    $input = [IO.File]::OpenRead($path)
    try {
        $magic = New-Object byte[] 4
        if ($input.Read($magic, 0, 4) -ne 4) { throw "Short resource: $path" }
        $input.Position = 0
        if ([Text.Encoding]::ASCII.GetString($magic) -eq 'Yaz0') {
            $saved = [Console]::Out
            try {
                [Console]::SetOut([IO.TextWriter]::Null)
                $stream = ([Toolbox.Library.Yaz0]::new()).Decompress($input)
            }
            finally { [Console]::SetOut($saved) }
        } else {
            $stream = [IO.MemoryStream]::new()
            $input.CopyTo($stream)
        }
    }
    finally { $input.Dispose() }
    $stream.Position = 0
    [void]$streams.Add($stream)
    $saved = [Console]::Out
    try {
        [Console]::SetOut([IO.TextWriter]::Null)
        return [Syroot.NintenTools.NSW.Bfres.ResFile]::new($stream, $true)
    }
    finally { [Console]::SetOut($saved) }
}

try {
    [Environment]::CurrentDirectory = $SwitchToolboxPath
    $rid = if ([Environment]::Is64BitProcess) { 'win-x64' } else { 'win-x86' }
    $native = Join-Path $SwitchToolboxPath "runtimes\$rid\native"
    $env:PATH = "$native;$SwitchToolboxPath;$(Join-Path $SwitchToolboxPath 'Lib');$originalPath"
    Add-Type -AssemblyName System.Windows.Forms
    Add-Type -AssemblyName System.Drawing
    [void][Reflection.Assembly]::LoadFrom((Join-Path $SwitchToolboxPath 'Lib\OpenTK.dll'))
    $assemblies = @(
        Get-ChildItem -LiteralPath (Join-Path $SwitchToolboxPath 'Lib') -File -Filter '*.dll'
        Get-ChildItem -LiteralPath $SwitchToolboxPath -File -Filter '*.dll'
    )
    for ($pass = 0; $pass -lt 3; $pass++) {
        foreach ($assembly in $assemblies) {
            try { [void][Reflection.Assembly]::LoadFrom($assembly.FullName) }
            catch [BadImageFormatException] { }
            catch { }
        }
    }

    $helper = @'
using System;
using System.Collections.Generic;
using Bfres.Structs;
using FirstPlugin;
using OpenTK;
using Toolbox.Library;

namespace Eden.BotwCompanion
{
    public sealed class PreviewPose
    {
        public Dictionary<string, float[]> Matrices =
            new Dictionary<string, float[]>(StringComparer.Ordinal);
    }

    public static class PreviewAnimationExporter
    {
        public static object WrapModel(object rawModel)
        {
            FMDL model = new FMDL();
            BfresSwitch.ReadModel(model, (Syroot.NintenTools.NSW.Bfres.Model)rawModel);
            return model;
        }

        public static PreviewPose Sample(object wrappedModel, object rawAnimation, float frame)
        {
            FMDL model = (FMDL)wrappedModel;
            Syroot.NintenTools.NSW.Bfres.SkeletalAnim animation =
                (Syroot.NintenTools.NSW.Bfres.SkeletalAnim)rawAnimation;
            FSKA genericAnimation = new FSKA(animation);
            STSkeleton skeleton = model.GenericSkeleton;
            skeleton.reset();
            genericAnimation.SetFrame(Math.Max(0.0f, Math.Min(frame, animation.FrameCount)));
            genericAnimation.NextFrame(skeleton, false, false);
            HashSet<string> animated = new HashSet<string>(StringComparer.Ordinal);
            foreach (Toolbox.Library.Animations.Animation.KeyNode bone in genericAnimation.Bones)
                animated.Add("Armature_" + bone.Text);
            PreviewPose result = new PreviewPose();
            foreach (STBone bone in skeleton.bones)
            {
                string name = "Armature_" + bone.Text;
                if (!animated.Contains(name))
                    continue;
                Matrix4 matrix =
                    Matrix4.CreateScale(bone.sca) *
                    Matrix4.CreateFromQuaternion(bone.rot) *
                    Matrix4.CreateTranslation(bone.pos);
                result.Matrices.Add(name, new float[] {
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
    $references = @(
        [AppDomain]::CurrentDomain.GetAssemblies() |
            Where-Object { -not $_.IsDynamic -and -not [string]::IsNullOrWhiteSpace($_.Location) } |
            ForEach-Object { $_.Location } |
            Select-Object -Unique
    )
    [void](Add-Type -TypeDefinition $helper -ReferencedAssemblies $references)

    $modelResource = Open-Resource $ModelBfres
    $animationResource = Open-Resource $AnimationBfres
    $model = @($modelResource.Models | Where-Object { $_.Name -ceq 'Link' }) | Select-Object -First 1
    if ($null -eq $model) { throw 'Link model was not found.' }
    $wrapped = [Eden.BotwCompanion.PreviewAnimationExporter]::WrapModel($model)

    $specs = @(
        [pscustomobject]@{ Key = 'Hold'; Name = 'Lift_Lift_Pouch_Few'; Step = 1; Hold = $true },
        [pscustomobject]@{ Key = 'Eat'; Name = 'Eat_Normal'; Step = 2; Hold = $false }
    )
    $clips = [ordered]@{}
    foreach ($spec in $specs) {
        $animation = @($animationResource.SkeletalAnims |
            Where-Object { $_.Name -ceq $spec.Name }) | Select-Object -First 1
        if ($null -eq $animation) { throw "Animation $($spec.Name) was not found." }
        $sampleFrames = [Collections.Generic.List[int]]::new()
        for ($frame = 0; $frame -le [int]$animation.FrameCount; $frame += $spec.Step) {
            [void]$sampleFrames.Add($frame)
        }
        if ($sampleFrames[$sampleFrames.Count - 1] -ne [int]$animation.FrameCount) {
            [void]$sampleFrames.Add([int]$animation.FrameCount)
        }
        $frames = foreach ($frame in $sampleFrames) {
            $sample = [Eden.BotwCompanion.PreviewAnimationExporter]::Sample(
                $wrapped, $animation, [float]$frame)
            $bones = [ordered]@{}
            foreach ($name in @($sample.Matrices.Keys | Sort-Object)) {
                $bones[$name] = @($sample.Matrices[$name])
            }
            [ordered]@{
                timeMs = [int][Math]::Round($frame * 1000.0 / 30.0)
                bones = $bones
            }
        }
        $clips[$spec.Key] = [ordered]@{
            source = $spec.Name
            durationMs = [int][Math]::Round($animation.FrameCount * 1000.0 / 30.0)
            holdLastFrame = [bool]$spec.Hold
            frames = @($frames)
        }
    }
    $document = [ordered]@{ version = 1; clips = $clips }
    $directory = Split-Path -Parent $OutputPath
    if (-not [IO.Directory]::Exists($directory)) {
        [void][IO.Directory]::CreateDirectory($directory)
    }
    [IO.File]::WriteAllText(
        $OutputPath,
        ($document | ConvertTo-Json -Depth 12 -Compress),
        [Text.UTF8Encoding]::new($false)
    )
}
finally {
    foreach ($stream in $streams) { $stream.Dispose() }
    [Environment]::CurrentDirectory = $originalDirectory
    $env:PATH = $originalPath
}
