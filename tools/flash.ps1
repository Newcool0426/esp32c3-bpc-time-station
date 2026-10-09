<#
.SYNOPSIS
    One-click download + rename + flash of the ESP32-C3 BPC time-station firmware.

.DESCRIPTION
    Downloads the newest firmware from the GitHub "latest" release, then flashes
    it with esptool. The serial port is auto-detected.

    By default it flashes the individual regions:
        0x0     bpc-bootloader.bin
        0x8000  bpc-partitions.bin
        0xe000  bpc-boot_app0.bin
        0x10000 bpc-app.bin
    This leaves the NVS partition (0x9000) untouched, so the saved Wi-Fi
    provisioning is preserved across firmware updates.

    With -Full it writes the single merged image at 0x0 instead. That is handy
    for a first flash or recovery, but it ERASES the NVS partition and therefore
    wipes the saved Wi-Fi credentials.

    Release assets are fetched through the GitHub REST API (api.github.com)
    instead of github.com/releases/download, so it also works where the
    github.com host is slow or blocked.

    This script is ASCII-only and runs on both Windows PowerShell 5.1 and
    PowerShell 7 (pwsh).

.PARAMETER Repo
    GitHub repository as owner/name. Defaults to the "origin" git remote.

.PARAMETER Port
    Serial port such as COM5. Auto-detected when omitted.

.PARAMETER Token
    GitHub token for private repositories. Public repos do not need it.
    Can also be supplied via the GITHUB_TOKEN environment variable.

.PARAMETER OutDir
    Directory to save the firmware into. Default: firmware

.PARAMETER Baud
    Download baud rate. Default: 921600

.PARAMETER Full
    Flash the single merged image at 0x0 (erases NVS / Wi-Fi config).

.PARAMETER NoFlash
    Download only, do not flash.

.EXAMPLE
    pwsh -File tools\flash.ps1
.EXAMPLE
    pwsh -File tools\flash.ps1 -Port COM5
.EXAMPLE
    pwsh -File tools\flash.ps1 -Full
#>
[CmdletBinding()]
param(
    [string]$Repo,
    [string]$Port,
    [string]$Token = $env:GITHUB_TOKEN,
    [string]$OutDir = "firmware",
    [int]$Baud = 921600,
    [switch]$Full,
    [switch]$NoFlash
)

$ErrorActionPreference = "Stop"
$ProgressPreference    = "SilentlyContinue"

function Get-RepoFromGit {
    try {
        $url = git remote get-url origin 2>$null
        if ($url -match 'github\.com[:/](?<owner>[^/]+)/(?<repo>[^/.]+)') {
            return "$($Matches.owner)/$($Matches.repo)"
        }
    } catch { }
    return $null
}

if (-not $Repo) { $Repo = Get-RepoFromGit }
if (-not $Repo) { throw "Cannot determine repository. Pass -Repo owner/name." }
Write-Host "Repo    : $Repo" -ForegroundColor Cyan

$jsonHeaders = @{ "User-Agent" = "esp32c3-bpc-flash"; "Accept" = "application/vnd.github+json" }
if ($Token) { $jsonHeaders["Authorization"] = "Bearer $Token" }
$apiBase = "https://api.github.com/repos/$Repo"

# Download a release asset through the API (works even if github.com is blocked).
function Save-ReleaseAsset([object]$asset, [string]$dest) {
    $assetApi = "$apiBase/releases/assets/$($asset.id)"
    $curl = Get-Command curl.exe -ErrorAction SilentlyContinue
    if ($curl) {
        $a = @("-sSL", "--fail", "-o", $dest,
               "-H", "Accept: application/octet-stream",
               "-H", "User-Agent: esp32c3-bpc-flash")
        if ($Token) { $a += @("-H", "Authorization: Bearer $Token") }
        $a += $assetApi
        & curl.exe @a
        if ($LASTEXITCODE -ne 0) { throw "Failed to download $($asset.name)." }
    } else {
        $h = @{ "User-Agent" = "esp32c3-bpc-flash"; "Accept" = "application/octet-stream" }
        if ($Token) { $h["Authorization"] = "Bearer $Token" }
        Invoke-WebRequest -Headers $h -Uri $assetApi -OutFile $dest
    }
}

function Get-ReleaseAssetString([object]$asset) {
    $tmp = [IO.Path]::Combine($env:TEMP, [IO.Path]::GetRandomFileName())
    try {
        Save-ReleaseAsset $asset $tmp
        return (Get-Content -Raw $tmp)
    } finally {
        Remove-Item $tmp -Force -ErrorAction SilentlyContinue
    }
}

Write-Host "Querying latest release ..."
$release = Invoke-RestMethod -Headers $jsonHeaders -Uri "$apiBase/releases/latest"
$assets  = $release.assets
function Find-Asset([string]$name) { $assets | Where-Object { $_.name -eq $name } }

$version = $release.tag_name
$verAsset = Find-Asset "version.txt"
if ($verAsset) {
    try {
        $txt = Get-ReleaseAssetString $verAsset
        if ($txt -match 'version=([^\r\n]+)') { $version = $Matches[1].Trim() }
    } catch { }
}
Write-Host "Version : $version" -ForegroundColor Cyan

$boot   = Find-Asset "bpc-bootloader.bin"
$parts  = Find-Asset "bpc-partitions.bin"
$ota    = Find-Asset "bpc-boot_app0.bin"
$app    = Find-Asset "bpc-app.bin"
$merged = Find-Asset "bpc-time-station.bin"

$incremental = ($boot -and $parts -and $ota -and $app -and -not $Full)
if (-not $incremental -and -not $merged) {
    throw "No firmware assets found in the latest release ($version)."
}

New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

$bootPath  = Join-Path $OutDir "bpc-bootloader.bin"
$partsPath = Join-Path $OutDir "bpc-partitions.bin"
$otaPath   = Join-Path $OutDir "bpc-boot_app0.bin"
$appPath   = Join-Path $OutDir "bpc-app-$version.bin"
$fullPath  = Join-Path $OutDir "bpc-time-station-$version.bin"

if ($incremental) {
    Write-Host "Downloading app image (v$version) ..."
    Save-ReleaseAsset $boot  $bootPath
    Save-ReleaseAsset $parts $partsPath
    Save-ReleaseAsset $ota   $otaPath
    Save-ReleaseAsset $app   $appPath
    Write-Host "Saved   : $appPath ($([math]::Round((Get-Item $appPath).Length/1KB,1)) KB)" -ForegroundColor Green
} else {
    Write-Host "Downloading full merged image (v$version) ..."
    Save-ReleaseAsset $merged $fullPath
    Write-Host "Saved   : $fullPath ($([math]::Round((Get-Item $fullPath).Length/1KB,1)) KB)" -ForegroundColor Green
}

if ($NoFlash) { Write-Host "-NoFlash set: download only, done."; exit 0 }

# ---- ensure esptool ---------------------------------------------------------
# Native commands that write to stderr would otherwise terminate the script
# while $ErrorActionPreference is "Stop".
$prevEA = $ErrorActionPreference
$ErrorActionPreference = "Continue"

& python -c "import esptool" 2>&1 | Out-Null
if ($LASTEXITCODE -ne 0) {
    Write-Host "Installing esptool ..."
    & python -m pip install --upgrade esptool 2>&1
    if ($LASTEXITCODE -ne 0) {
        $ErrorActionPreference = $prevEA
        throw "Failed to install esptool."
    }
}

# ---- auto-detect serial port ------------------------------------------------
if (-not $Port) {
    $ports = Get-CimInstance Win32_PnPEntity |
        Where-Object { $_.Name -match '\((COM\d+)\)' } |
        ForEach-Object {
            $null = $_.Name -match '\((COM\d+)\)'
            [pscustomobject]@{ Port = $Matches[1]; Name = $_.Name }
        }
    $pick = $ports | Where-Object { $_.Name -match 'USB|CP210|CH34|Silicon|JTAG|Serial' } |
            Select-Object -First 1
    if (-not $pick) { $pick = $ports | Select-Object -First 1 }
    if (-not $pick) { throw "No serial port found. Pass -Port COMx." }
    $Port = $pick.Port
    Write-Host "Port    : $Port ($($pick.Name))" -ForegroundColor Cyan
}

if ($incremental) {
    Write-Host "Flashing (incremental, keeps NVS/Wi-Fi config) -> $Port ..." -ForegroundColor Yellow
    & python -m esptool --chip esp32c3 --port $Port --baud $Baud `
        --before default_reset --after hard_reset `
        write_flash -z `
        0x0     $bootPath `
        0x8000  $partsPath `
        0xe000  $otaPath `
        0x10000 $appPath 2>&1
} else {
    Write-Host "Flashing FULL merged image (ERASES NVS/Wi-Fi config) -> $Port ..." -ForegroundColor Yellow
    & python -m esptool --chip esp32c3 --port $Port --baud $Baud `
        --before default_reset --after hard_reset `
        write_flash -z 0x0 $fullPath 2>&1
}
$flashCode = $LASTEXITCODE
$ErrorActionPreference = $prevEA

if ($flashCode -ne 0) { throw "Flashing failed (esptool exit code $flashCode)." }
Write-Host "Done. The device reboots and starts broadcasting." -ForegroundColor Green
exit 0
