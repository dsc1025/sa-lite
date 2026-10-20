param(
    [Parameter(Mandatory = $true)][string]$VersionHeader,
    [Parameter(Mandatory = $true)][string]$ExecutablePath
)

$ErrorActionPreference = 'Stop'
$versionText = [IO.File]::ReadAllText($VersionHeader)
$versionMatch = [regex]::Match($versionText, 'Version\[\]\s*=\s*L"([^"]+)"')
if (-not $versionMatch.Success) {
    throw 'Cannot read client version from AppInfo.h.'
}

$executable = Get-Item -LiteralPath $ExecutablePath
$manifest = [ordered]@{
    version = $versionMatch.Groups[1].Value
    url = 'https://cso.tomsworld.com.cn/upgrade/sa/SAClient.exe'
    size = $executable.Length
    sha256 = (Get-FileHash -LiteralPath $executable.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
}
$manifestPath = Join-Path $executable.DirectoryName 'version.json'
$encoding = New-Object Text.UTF8Encoding($false)
[IO.File]::WriteAllText($manifestPath, ($manifest | ConvertTo-Json) + "`n", $encoding)
Write-Output "Generated update manifest: $manifestPath"
