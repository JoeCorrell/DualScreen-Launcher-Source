function Set-FalloutDinputOverride {
    param(
        [Parameter(Mandatory = $true)][string]$Source,
        [Parameter(Mandatory = $true)][string]$Destination
    )

    $bytes = [IO.File]::ReadAllBytes($Source)
    $hadBom = $bytes.Length -ge 3 -and $bytes[0] -eq 0xEF -and
              $bytes[1] -eq 0xBB -and $bytes[2] -eq 0xBF
    $offset = if ($hadBom) { 3 } else { 0 }
    $text = [Text.Encoding]::UTF8.GetString($bytes, $offset,
                                           $bytes.Length - $offset)
    $newline = if ($text.Contains("`r`n")) { "`r`n" } else { "`n" }
    $lines = [Collections.Generic.List[string]]::new()
    foreach ($line in ($text -split "`r?`n")) { [void]$lines.Add($line) }
    $header = '[Software\\Wine\\AppDefaults\\Fallout3.exe\\DllOverrides]'
    $valuePrefix = '"dinput8"='
    $section = -1
    for ($index = 0; $index -lt $lines.Count; ++$index) {
        if ($lines[$index].StartsWith($header, [StringComparison]::Ordinal)) {
            $section = $index
            break
        }
    }
    $prior = $null
    if ($section -ge 0) {
        $end = $lines.Count
        for ($index = $section + 1; $index -lt $lines.Count; ++$index) {
            if ($lines[$index].StartsWith('[', [StringComparison]::Ordinal)) {
                $end = $index
                break
            }
        }
        $valueIndex = -1
        for ($index = $section + 1; $index -lt $end; ++$index) {
            if ($lines[$index].StartsWith($valuePrefix,
                                          [StringComparison]::Ordinal)) {
                $valueIndex = $index
                break
            }
        }
        if ($valueIndex -ge 0) {
            $prior = $lines[$valueIndex]
            $lines[$valueIndex] = '"dinput8"="native,builtin"'
        } else {
            $lines.Insert($end, '"dinput8"="native,builtin"')
        }
    } else {
        if ($lines.Count -and $lines[$lines.Count - 1] -ne '') {
            [void]$lines.Add('')
        }
        [void]$lines.Add($header)
        [void]$lines.Add('"dinput8"="native,builtin"')
    }
    $result = [string]::Join($newline, $lines)
    $encoding = [Text.UTF8Encoding]::new($hadBom)
    [IO.File]::WriteAllText($Destination, $result, $encoding)
    return $prior
}

function Set-FalloutInstalledPath {
    param(
        [Parameter(Mandatory = $true)][string]$Source,
        [Parameter(Mandatory = $true)][string]$Destination
    )

    $bytes = [IO.File]::ReadAllBytes($Source)
    $hadBom = $bytes.Length -ge 3 -and $bytes[0] -eq 0xEF -and
              $bytes[1] -eq 0xBB -and $bytes[2] -eq 0xBF
    $offset = if ($hadBom) { 3 } else { 0 }
    $text = [Text.Encoding]::UTF8.GetString($bytes, $offset,
                                           $bytes.Length - $offset)
    $newline = if ($text.Contains("`r`n")) { "`r`n" } else { "`n" }
    $lines = [Collections.Generic.List[string]]::new()
    foreach ($line in ($text -split "`r?`n")) { [void]$lines.Add($line) }
    $header = '[Software\\Wow6432Node\\Bethesda Softworks\\Fallout3]'
    $valuePrefix = '"Installed Path"='
    $desired = '"Installed Path"="F:\\Download\\Fallout-3-GOTY-SteamRIP.com\\Fallout 3\\"'
    $section = -1
    for ($index = 0; $index -lt $lines.Count; ++$index) {
        if ($lines[$index].StartsWith($header, [StringComparison]::Ordinal)) {
            $section = $index
            break
        }
    }
    $prior = $null
    if ($section -ge 0) {
        $end = $lines.Count
        for ($index = $section + 1; $index -lt $lines.Count; ++$index) {
            if ($lines[$index].StartsWith('[', [StringComparison]::Ordinal)) {
                $end = $index
                break
            }
        }
        $valueIndex = -1
        for ($index = $section + 1; $index -lt $end; ++$index) {
            if ($lines[$index].StartsWith($valuePrefix,
                                          [StringComparison]::Ordinal)) {
                $valueIndex = $index
                break
            }
        }
        if ($valueIndex -ge 0) {
            $prior = $lines[$valueIndex]
            $lines[$valueIndex] = $desired
        } else {
            $lines.Insert($end, $desired)
        }
    } else {
        if ($lines.Count -and $lines[$lines.Count - 1] -ne '') {
            [void]$lines.Add('')
        }
        [void]$lines.Add($header)
        [void]$lines.Add($desired)
    }
    $result = [string]::Join($newline, $lines)
    $encoding = [Text.UTF8Encoding]::new($hadBom)
    [IO.File]::WriteAllText($Destination, $result, $encoding)
    return $prior
}
