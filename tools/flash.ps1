<#
.SYNOPSIS
    One-click download + rename + flash of the ESP32-C3 BPC time-station firmware.

.DESCRIPTION
    Downloads the newest firmware from the GitHub "latest" release, renames it
    with its version, then flashes it with esptool. The serial port is
    auto-detected.

    Release assets are fetched through the GitHub REST API (api.github.com)
    instead of github.com/releases/download, so it also works on networks
    where the github.com host is slow or blocked.

    This script is intentionally ASCII-only so it parses correctly under
    Windows PowerShell 5.1 regardless of the system codepage.

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

.PARAMETER NoFlash
    Download only, do not flash.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File tools\flash.ps1
.EXAMPLE
    powershell -ExecutionPolicy Bypass -File tools\flash.ps1 -Port COM5
#>
[CmdletBinding()]
param(
    [string]$Repo,
    [string]$Port,
    [string]$Token = $env:GITHUB_TOKEN,
    [string]$OutDir = "firmware",
    [int]$Baud = 921600,
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
$release  = Invoke-RestMethod -Headers $jsonHeaders -Uri "$apiBase/releases/latest"
$fwAsset  = $release.assets | Where-Object { $_.name -eq "bpc-time-station.bin" }
if (-not $fwAsset) { throw "bpc-time-station.bin not found in latest release ($($release.tag_name))." }

$version  = $release.tag_name
$verAsset = $release.assets | Where-Object { $_.name -eq "version.txt" }
if ($verAsset) {
    try {
        $txt = Get-ReleaseAssetString $verAsset
        if ($txt -match 'version=([^\r\n]+)') { $version = $Matches[1].Trim() }
    } catch { }
}
Write-Host "Version : $version" -ForegroundColor Cyan

New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$target = Join-Path $OutDir "bpc-time-station-$version.bin"

Write-Host "Downloading asset id $($fwAsset.id) ..."
Save-ReleaseAsset $fwAsset $target
$size = [math]::Round((Get-Item $target).Length / 1KB, 1)
Write-Host "Saved   : $target ($size KB)" -ForegroundColor Green

if ($NoFlash) { Write-Host "-NoFlash set: download only, done."; exit 0 }

# ---- ensure esptool ---------------------------------------------------------
# Native commands that write to stderr (e.g. an ImportError traceback) would
# otherwise terminate the script while $ErrorActionPreference is "Stop".
$prevEA = $ErrorActionPreference
$ErrorActionPreference = "Continue"

& python -c "import esptool" 2>&1 | Out-Null
if ($LASTEXITCODE -ne 0) {
    Write-Host "Installing esptool ..."
    & python -m pip install --upgrade esptool
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

Write-Host "Flashing $target -> $Port ..." -ForegroundColor Yellow
& python -m esptool --chip esp32c3 --port $Port --baud $Baud `
    --before default_reset --after hard_reset `
    write_flash -z 0x0 $target
$flashCode = $LASTEXITCODE
$ErrorActionPreference = $prevEA

if ($flashCode -ne 0) { throw "Flashing failed (esptool exit code $flashCode)." }
Write-Host "Done. The device reboots and starts broadcasting." -ForegroundColor Green
exit 0
