# Removes the scgt2026 renderer and HD textures (the game itself stays).
#   powershell -ExecutionPolicy Bypass -File .\uninstall.ps1 [-GamePath "C:\Games\Sports Car GT"] [-RemoveGame]
#   -RemoveGame  also delete the whole game folder, its registry key and the shortcuts.
param(
    [string]$GamePath = "C:\Games\Sports Car GT",
    [switch]$RemoveGame
)
$ErrorActionPreference = "Stop"
if (-not (Test-Path (Join-Path $GamePath "Spcar.exe"))) { throw "Spcar.exe not found in '$GamePath'." }

if ($RemoveGame) {
    $admin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
    if (-not $admin) {
        Start-Process powershell -Verb RunAs -Wait -ArgumentList @("-NoProfile", "-ExecutionPolicy", "Bypass", "-File", "`"$($MyInvocation.MyCommand.Path)`"", "-GamePath", "`"$GamePath`"", "-RemoveGame")
        exit
    }
    Remove-Item $GamePath -Recurse -Force
    $hklm = [Microsoft.Win32.RegistryKey]::OpenBaseKey([Microsoft.Win32.RegistryHive]::LocalMachine, [Microsoft.Win32.RegistryView]::Registry32)
    $hklm.DeleteSubKeyTree("SOFTWARE\Image Space Inc\Sports Car GT", $false)
    foreach ($folder in @([Environment]::GetFolderPath("Desktop"), [Environment]::GetFolderPath("Programs"))) {
        Remove-Item (Join-Path $folder "Sports Car GT 2026.lnk") -ErrorAction SilentlyContinue
    }
    Write-Host "Removed Sports Car GT from $GamePath"
    exit
}

$dll = Join-Path $GamePath "glide2x.dll"
$backup = Join-Path $GamePath "glide2x.original.dll"
Remove-Item $dll -ErrorAction SilentlyContinue
if (Test-Path $backup) { Move-Item $backup $dll; Write-Host "Restored your previous glide2x.dll" }
Remove-Item (Join-Path $GamePath "textures") -Recurse -Force -ErrorAction SilentlyContinue
Remove-Item (Join-Path $GamePath "scgt2026.log") -ErrorAction SilentlyContinue
Write-Host "Removed the scgt2026 renderer and HD textures (scgt2026.ini kept)."
