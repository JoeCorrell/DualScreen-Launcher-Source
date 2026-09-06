param([Parameter(Mandatory=$true)][string]$InputPath,
      [string]$VisibilityOutput,
      [string]$MaterialOutput,
      [string]$ToolboxPath='C:/Projects/Eden DS/.work/switch-toolbox')
$ErrorActionPreference='Stop'
$InputPath=[IO.Path]::GetFullPath($InputPath)
Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing
[void][Reflection.Assembly]::LoadFrom((Join-Path $ToolboxPath 'Lib/OpenTK.dll'))
$dlls=@(Get-ChildItem (Join-Path $ToolboxPath 'Lib') -File -Filter '*.dll'; Get-ChildItem $ToolboxPath -File -Filter '*.dll')
for ($i=0;$i -lt 3;$i++) { foreach ($dll in $dlls) { try { [void][Reflection.Assembly]::LoadFrom($dll.FullName) } catch {} } }
$stream=[IO.File]::OpenRead($InputPath)
try {
    $res=[Syroot.NintenTools.NSW.Bfres.ResFile]::new($stream,$true)
    if ($MaterialOutput) {
        $materials = @{}
        foreach ($model in $res.Models) {
            foreach ($material in $model.Materials) {
                $parameters = @{}
                foreach ($parameter in $material.ShaderParams) {
                    if ([string]$parameter.Type -match '^Float[234]?$') {
                        $parameters[$parameter.Name] = @(for ($offset=0; $offset -lt $parameter.DataSize; $offset+=4) {
                            [BitConverter]::ToSingle($material.ShaderParamData, $parameter.DataOffset + $offset)
                        })
                    }
                }
                $parameters['_textures'] = @($material.TextureRefs)
                $materials[$material.Name] = $parameters
            }
        }
        [IO.File]::WriteAllText([IO.Path]::GetFullPath($MaterialOutput), ($materials | ConvertTo-Json -Depth 8))
        return
    }
    if ($VisibilityOutput) {
        $wait=$res.BoneVisibilityAnims | Where-Object { $_.Name -eq 'Wait' } | Select-Object -First 1
        $hidden=@()
        if ($wait) {
            if ($wait.Curves.Count -ne 0) { throw 'Wait has animated visibility; implement curve sampling before exporting it' }
            for ($i=0;$i -lt $wait.Names.Count;$i++) {
                if (-not $wait.BaseDataList[$i]) { $hidden += [string]$wait.Names[$i] }
            }
        }
        $data=@{schema=1;clip='Wait';frame=0;hiddenBones=$hidden}
        [IO.File]::WriteAllText([IO.Path]::GetFullPath($VisibilityOutput), ($data | ConvertTo-Json -Depth 5))
        "Native Wait hidden bones: $($hidden -join ', ')"
        return
    }
    foreach ($property in @('BoneVisibilityAnims','ShapeAnims','MaterialAnims','SkeletalAnims')) {
        $table=$res.$property
        if ($table) {
            "$property count: $($table.Count)"
            foreach ($key in @('Wait','EyeOpen','EyeClose','FaceNormal')) {
                $value=$table | Where-Object { $_.Name -eq $key } | Select-Object -First 1
                if ($value) {
                    "${property}/${key}:"
                    $value | Format-List *
                    if ($property -eq 'BoneVisibilityAnims') {
                        $value.Curves | Format-List *
                    }
                }
            }
        }
    }
} finally { $stream.Dispose() }
