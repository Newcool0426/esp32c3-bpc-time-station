<#
.SYNOPSIS
    一键下载并刷写 ESP32-C3 BPC 授时站固件。

.DESCRIPTION
    从 GitHub 的 "latest" Release 下载最新固件，按版本号重命名后
    用 esptool 刷入设备。可自动探测串口。

.PARAMETER Repo
    GitHub 仓库，格式 owner/name。默认从 git remote origin 推断。

.PARAMETER Port
    串口，如 COM5。省略时自动探测。

.PARAMETER Token
    私有仓库所需的 GitHub Token。公有仓库可省略；
    也可通过环境变量 GITHUB_TOKEN 提供。

.PARAMETER OutDir
    固件保存目录，默认 firmware。

.PARAMETER Baud
    下载波特率，默认 921600。

.PARAMETER NoFlash
    只下载不刷写。

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
if (-not $Repo) { throw "无法确定仓库，请用 -Repo owner/name 指定。" }
Write-Host "Repo : $Repo" -ForegroundColor Cyan

$headers = @{ "User-Agent" = "esp32c3-bpc-flash" ; "Accept" = "application/vnd.github+json" }
if ($Token) { $headers["Authorization"] = "Bearer $Token" }

$apiBase = "https://api.github.com/repos/$Repo"
Write-Host "查询最新 Release ..."
$release = Invoke-RestMethod -Headers $headers -Uri "$apiBase/releases/latest"

$fwAsset  = $release.assets | Where-Object { $_.name -eq "bpc-time-station.bin" }
if (-not $fwAsset) { throw "在最新 Release($($release.tag_name)) 中找不到 bpc-time-station.bin。" }

$version = $release.tag_name
$verAsset = $release.assets | Where-Object { $_.name -eq "version.txt" }
if ($verAsset) {
    try {
        $txt = (Invoke-RestMethod -Headers $headers -Uri $verAsset.browser_download_url) -join "`n"
        if ($txt -match 'version=([^\r\n]+)') { $version = $Matches[1].Trim() }
    } catch { }
}
Write-Host "Version : $version" -ForegroundColor Cyan

New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$target = Join-Path $OutDir "bpc-time-station-$version.bin"

Write-Host "下载固件：$($fwAsset.browser_download_url)"
Invoke-WebRequest -Headers $headers -Uri $fwAsset.browser_download_url -OutFile $target
$size = [math]::Round((Get-Item $target).Length / 1KB, 1)
Write-Host "已保存：$target ($size KB)" -ForegroundColor Green

if ($NoFlash) { Write-Host "-NoFlash：仅下载，结束。"; return }

# ---- 确保 esptool 可用 ------------------------------------------------------
python -c "import esptool" 2>$null
if ($LASTEXITCODE -ne 0) {
    Write-Host "安装 esptool ..."
    python -m pip install --upgrade esptool
}

# ---- 探测串口 ----------------------------------------------------------------
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
    if (-not $pick) { throw "未找到串口，请用 -Port COMx 指定。" }
    $Port = $pick.Port
    Write-Host "自动探测串口：$Port ($($pick.Name))" -ForegroundColor Cyan
}

Write-Host "刷写 $target -> $Port ..." -ForegroundColor Yellow
python -m esptool --chip esp32c3 --port $Port --baud $Baud `
    --before default_reset --after hard_reset `
    write_flash -z 0x0 $target

if ($LASTEXITCODE -ne 0) { throw "刷写失败 (esptool 退出码 $LASTEXITCODE)。" }
Write-Host "完成！设备将自动重启并开始授时。" -ForegroundColor Green
