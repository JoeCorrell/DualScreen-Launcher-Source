function Read-IniProfile {
    param([Parameter(Mandatory = $true)][string]$Path)

    $sections = [ordered]@{}
    $section = ''
    foreach ($line in [IO.File]::ReadAllLines($Path)) {
        if ($line -match '^\s*\[([^\]]+)\]\s*$') {
            $section = $Matches[1].Trim()
            if (-not $sections.Contains($section)) { $sections[$section] = [ordered]@{} }
            continue
        }
        if ($line -match '^\s*([^;#][^=]*?)\s*=\s*(.*?)\s*$') {
            if (-not $section) { throw "Profile setting appears before a section: $line" }
            $key = $Matches[1].Trim()
            $value = $Matches[2]
            if (-not $sections.Contains($section)) { $sections[$section] = [ordered]@{} }
            $sections[$section][$key] = $value
        }
    }
    return $sections
}

function Merge-IniProfile {
    param(
        [Parameter(Mandatory = $true)][string]$InputPath,
        [Parameter(Mandatory = $true)][string]$ProfilePath,
        [Parameter(Mandatory = $true)][string]$OutputPath
    )

    $profile = Read-IniProfile $ProfilePath
    $remaining = [ordered]@{}
    foreach ($sectionName in $profile.Keys) {
        $settings = [ordered]@{}
        foreach ($key in $profile[$sectionName].Keys) {
            $settings[$key] = $profile[$sectionName][$key]
        }
        $remaining[$sectionName] = $settings
    }

    $input = if (Test-Path -LiteralPath $InputPath) {
        [IO.File]::ReadAllText($InputPath)
    } else { '' }
    $newline = if ($input.Contains("`r`n")) { "`r`n" } else { "`n" }
    $lines = if ($input) { $input -split '\r?\n' } else { @() }
    $output = [Collections.Generic.List[string]]::new()
    $current = ''

    function Flush-IniSection([string]$name) {
        if (-not $name -or -not $remaining.Contains($name)) { return }
        foreach ($key in @($remaining[$name].Keys)) {
            $output.Add("$key=$($remaining[$name][$key])")
            $remaining[$name].Remove($key)
        }
        $remaining.Remove($name)
    }

    foreach ($line in $lines) {
        if ($line -match '^\s*\[([^\]]+)\]\s*$') {
            Flush-IniSection $current
            $current = $Matches[1].Trim()
            $output.Add($line)
            continue
        }
        if ($current -and $remaining.Contains($current) -and
            $line -match '^\s*([^;#][^=]*?)\s*=') {
            $existingKey = $Matches[1].Trim()
            $matchedKey = @($remaining[$current].Keys) |
                Where-Object { $_ -ieq $existingKey } | Select-Object -First 1
            if ($null -ne $matchedKey) {
                $output.Add("$existingKey=$($remaining[$current][$matchedKey])")
                $remaining[$current].Remove($matchedKey)
                continue
            }
        }
        $output.Add($line)
    }
    Flush-IniSection $current

    foreach ($sectionName in @($remaining.Keys)) {
        if ($output.Count -and $output[$output.Count - 1] -ne '') { $output.Add('') }
        $output.Add("[$sectionName]")
        foreach ($key in $remaining[$sectionName].Keys) {
            $output.Add("$key=$($remaining[$sectionName][$key])")
        }
    }

    while ($output.Count -and $output[$output.Count - 1] -eq '') {
        $output.RemoveAt($output.Count - 1)
    }
    $text = if ($output.Count) { ($output -join $newline) + $newline } else { '' }
    [IO.File]::WriteAllText($OutputPath, $text, [Text.UTF8Encoding]::new($false))
}
