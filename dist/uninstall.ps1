# Removes the scgt2026 renderer from a Sports Car GT folder (HD textures are left in place).
#   powershell -ExecutionPolicy Bypass -File .\uninstall.ps1 -GamePath "C:\Games\Sports Car GT"
param(
    [Parameter(Mandatory = $true)][string]$GamePath
)
$ErrorActionPreference = "Stop"
$dll = Join-Path $GamePath "glide2x.dll"
$orig = Join-Path $GamePath "glide2x.original.dll"
if (Test-Path $orig) {
    Move-Item $orig $dll -Force
    Write-Host "Restored the original glide2x.dll"
} elseif (Test-Path $dll) {
    Remove-Item $dll
    Write-Host "Removed glide2x.dll"
}
$layers = "HKCU:\Software\Microsoft\Windows NT\CurrentVersion\AppCompatFlags\Layers"
Remove-ItemProperty -Path $layers -Name (Join-Path $GamePath "Spcar.exe") -ErrorAction SilentlyContinue
Write-Host "Done. Delete the 'textures' folder too if you want to remove the HD textures."
