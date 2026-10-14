# Sports Car GT: 2026 Edition — installs the scgt2026 renderer (and HD textures, if present)
# into an existing Sports Car GT 1.551 installation.
#
#   powershell -ExecutionPolicy Bypass -File .\install.ps1 -GamePath "C:\Games\Sports Car GT"
#
param(
    [Parameter(Mandatory = $true)][string]$GamePath
)
$ErrorActionPreference = "Stop"
$kit = Split-Path -Parent $MyInvocation.MyCommand.Path

if (-not (Test-Path (Join-Path $GamePath "Spcar.exe"))) {
    throw "Spcar.exe not found in '$GamePath'. Point -GamePath at your Sports Car GT folder."
}

# The renderer is Glide: the game loads glide2x.dll from its own folder.
$dll = Join-Path $GamePath "glide2x.dll"
if ((Test-Path $dll) -and -not (Test-Path (Join-Path $GamePath "glide2x.original.dll"))) {
    Copy-Item $dll (Join-Path $GamePath "glide2x.original.dll")
    Write-Host "Backed up the existing glide2x.dll as glide2x.original.dll"
}
Copy-Item (Join-Path $kit "glide2x.dll") $dll -Force
Write-Host "Installed glide2x.dll (scgt2026 renderer)"

$ini = Join-Path $GamePath "scgt2026.ini"
if (-not (Test-Path $ini)) {
    Copy-Item (Join-Path $kit "scgt2026.ini") $ini
    Write-Host "Wrote scgt2026.ini with default settings"
} else {
    Write-Host "Kept your existing scgt2026.ini"
}

# HD textures (optional part of the kit)
$tex = Join-Path $kit "textures"
if (Test-Path $tex) {
    $dst = Join-Path $GamePath "textures"
    New-Item -ItemType Directory -Force -Path $dst | Out-Null
    Copy-Item (Join-Path $tex "*.png") $dst -Force
    Write-Host ("Installed {0} HD textures" -f (Get-ChildItem $dst -Filter *.png).Count)
}

# Ask Windows to treat the game as DPI aware so the picture isn't scaled twice.
$exe = Join-Path $GamePath "Spcar.exe"
$layers = "HKCU:\Software\Microsoft\Windows NT\CurrentVersion\AppCompatFlags\Layers"
New-Item -Path $layers -Force | Out-Null
Set-ItemProperty -Path $layers -Name $exe -Value "~ HIGHDPIAWARE"

Write-Host ""
Write-Host "Done. Start Spcar.exe, choose '3Dfx Voodoo' under Select Video Driver, and click Play."
