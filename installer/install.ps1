# Installs the CLI tools bundled next to this script onto your user PATH.
#
# Usage: .\install.ps1 [-Dest <path>]
#   Dest defaults to $env:LOCALAPPDATA\nintoolbox\bin.
#
# Copies every .exe next to this script (skipping the GUI app and share\)
# into Dest, copies share\ (titles.txt etc.) alongside it, and adds Dest to
# the current user's PATH (via the registry, so it persists across
# sessions) if it isn't already there.

param(
    [string]$Dest = "$env:LOCALAPPDATA\nintoolbox\bin"
)

$here = Split-Path -Parent $MyInvocation.MyCommand.Path

New-Item -ItemType Directory -Force -Path $Dest | Out-Null

Write-Host "Installing to $Dest"
$installed = 0
Get-ChildItem -Path $here -Filter *.exe -File | Where-Object {
    $_.Name -ne "nintoolbox.exe"
} | ForEach-Object {
    Copy-Item $_.FullName -Destination (Join-Path $Dest $_.Name) -Force
    Write-Host "  $($_.Name)"
    $installed++
}

if ($installed -eq 0) {
    Write-Warning "No .exe files found next to this script -- nothing installed."
    exit 1
}

$shareSrc = Join-Path $here "share"
if (Test-Path $shareSrc) {
    $shareDest = Join-Path (Split-Path -Parent $Dest) "share"
    New-Item -ItemType Directory -Force -Path $shareDest | Out-Null
    Copy-Item "$shareSrc\*" -Destination $shareDest -Recurse -Force
    Write-Host "Installed shared data (titles.txt etc.) to $shareDest"
}

$userPath = [Environment]::GetEnvironmentVariable("Path", "User")
if (($userPath -split ";") -notcontains $Dest) {
    $newPath = if ($userPath) { "$userPath;$Dest" } else { $Dest }
    [Environment]::SetEnvironmentVariable("Path", $newPath, "User")
    Write-Host ""
    Write-Host "Added $Dest to your user PATH."
    Write-Host "Open a new terminal window for this to take effect."
} else {
    Write-Host ""
    Write-Host "$Dest is already on your PATH."
}

Write-Host ""
Write-Host "Done. In a new terminal, try: wszst --version"
