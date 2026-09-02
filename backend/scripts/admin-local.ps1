[CmdletBinding()]
param(
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]] $AdminArguments
)

$bundlePath = Join-Path `
    ([Environment]::GetFolderPath('LocalApplicationData')) `
    'DualScreenLauncher\backend-secrets.dpapi'

if (-not [IO.File]::Exists($bundlePath)) {
    throw "The encrypted DualScreen Launcher administrator credential was not found at $bundlePath."
}

$protectedBytes = [IO.File]::ReadAllBytes($bundlePath)
$plainBytes = $null
$previousBaseUrl = $env:ACCESS_API_BASE_URL
$previousAdminToken = $env:ACCESS_ADMIN_TOKEN

try {
    $plainBytes = [Security.Cryptography.ProtectedData]::Unprotect(
        $protectedBytes,
        $null,
        [Security.Cryptography.DataProtectionScope]::CurrentUser
    )
    $bundle = [Text.Encoding]::UTF8.GetString($plainBytes) | ConvertFrom-Json
    if ([string]::IsNullOrWhiteSpace($bundle.PUBLIC_BASE_URL) -or
        [string]::IsNullOrWhiteSpace($bundle.ADMIN_API_TOKEN)) {
        throw 'The encrypted administrator credential is incomplete.'
    }

    $env:ACCESS_API_BASE_URL = $bundle.PUBLIC_BASE_URL
    $env:ACCESS_ADMIN_TOKEN = $bundle.ADMIN_API_TOKEN
    & npm.cmd run admin -- @AdminArguments
    $adminExitCode = $LASTEXITCODE
} finally {
    $env:ACCESS_API_BASE_URL = $previousBaseUrl
    $env:ACCESS_ADMIN_TOKEN = $previousAdminToken
    if ($null -ne $plainBytes) {
        [Array]::Clear($plainBytes, 0, $plainBytes.Length)
    }
    [Array]::Clear($protectedBytes, 0, $protectedBytes.Length)
    Remove-Variable bundle -ErrorAction SilentlyContinue
}

exit $adminExitCode
