# Installs the CLI tools bundled with this script onto your user PATH.
#
# Usage: .\install.ps1 [-Dest <path>]
#   Dest defaults to $env:LOCALAPPDATA\nintoolbox\bin.
#
# Copies every .exe and .dll from the bin\ folder next to this script
# (skipping the GUI app) into Dest, copies bin\share\ (titles.txt etc.)
# alongside it, and adds Dest to the current user's PATH (via the registry,
# so it persists across sessions) if it isn't already there.

param(
    [string]$Dest = "$env:LOCALAPPDATA\nintoolbox\bin"
)

$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$binSrc = Join-Path $here "bin"
if (-not (Test-Path $binSrc)) {
    $binSrc = Join-Path $here "_internal\bin"
}
if (-not (Test-Path $binSrc)) {
    $binSrc = Join-Path $here "_internal"
}
if (-not (Test-Path $binSrc)) {
    # Fall back to $here itself, for older bundles that shipped everything flat.
    $binSrc = $here
}

New-Item -ItemType Directory -Force -Path $Dest | Out-Null

Write-Host "Installing to $Dest"
$installed = 0
Get-ChildItem -Path $binSrc -Include *.exe, *.dll, *.keys, *.txt, *.bin, *.jar, *.bat -File -Recurse | Where-Object {
    $_.Name -ne "nintoolbox.exe" -and $_.Name -notmatch '^python'
} | ForEach-Object {
    Copy-Item $_.FullName -Destination (Join-Path $Dest $_.Name) -Force
    Write-Host "  $($_.Name)"
    $installed++
}

if ($installed -eq 0) {
    Write-Warning "No .exe/.dll files found under $binSrc -- nothing installed."
    exit 1
}

$libSrc = Join-Path $binSrc "lib"
if (Test-Path $libSrc) {
    $libDest = Join-Path $Dest "lib"
    New-Item -ItemType Directory -Force -Path $libDest | Out-Null
    Copy-Item "$libSrc\*" -Destination $libDest -Recurse -Force
    Write-Host "Installed lib\ (bundled Java libraries)"
}

$wiiuKeysSrc = Join-Path $binSrc "wiiu_keys"
if (-not (Test-Path $wiiuKeysSrc)) {
    $wiiuKeysSrc = Join-Path $here "_internal\bin\wiiu_keys"
}
if (-not (Test-Path $wiiuKeysSrc)) {
    $wiiuKeysSrc = Join-Path $here "wiiu_keys"
}
if (Test-Path $wiiuKeysSrc) {
    $wiiuKeysDest = Join-Path $Dest "wiiu_keys"
    New-Item -ItemType Directory -Force -Path $wiiuKeysDest | Out-Null
    Copy-Item "$wiiuKeysSrc\*" -Destination $wiiuKeysDest -Recurse -Force
    Write-Host "Installed wiiu_keys\ (Wii U retail disc keys)"
}

$shareSrc = Join-Path $binSrc "share"
if (-not (Test-Path $shareSrc)) {
    $shareSrc = Join-Path $here "_internal\bin\share"
}
if (-not (Test-Path $shareSrc)) {
    $shareSrc = Join-Path $here "share"
}
if (Test-Path $shareSrc) {
    $shareDest = Join-Path (Split-Path -Parent $Dest) "share"
    New-Item -ItemType Directory -Force -Path $shareDest | Out-Null
    Copy-Item "$shareSrc\*" -Destination $shareDest -Recurse -Force
    # Also copy next to Dest for tools looking in relative share/
    $winShare = Join-Path $Dest "share"
    New-Item -ItemType Directory -Force -Path $winShare | Out-Null
    Copy-Item "$shareSrc\*" -Destination $winShare -Recurse -Force
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
