# Sports Car GT: 2026 Edition installer.
#
# Installs Sports Car GT from your own disc (an extracted copy of the CD, or the mounted ISO),
# patches it to 1.551, and adds the scgt2026 renderer and the HD texture pack.
#
#   Double-click "Install Sports Car GT 2026.cmd", or:
#   powershell -ExecutionPolicy Bypass -File .\install.ps1 [-Source "D:\"] [-GamePath "C:\Games\Sports Car GT"]
#
#   -Source      the CD contents (folder with S.EXE, MAS\, DAT\ ...). Default: looked for next to the kit.
#   -GamePath    where to install. Default: C:\Games\Sports Car GT (keep it outside Program Files).
#   -RendererOnly  only (re)install glide2x.dll, scgt2026.ini and the HD textures into an existing install.
#   -NoTextures  skip the HD texture pack.
param(
    [string]$Source = "",
    [string]$GamePath = "C:\Games\Sports Car GT",
    [switch]$RendererOnly,
    [switch]$NoTextures
)
$ErrorActionPreference = "Stop"
$kit = Split-Path -Parent $MyInvocation.MyCommand.Path

function Say($text) { Write-Host $text }
function Step($text) { Write-Host ""; Write-Host "== $text" -ForegroundColor Cyan }

# ---- administrator rights (the game's settings live in HKLM) --------------------------------
$admin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $admin) {
    $argList = @("-NoProfile", "-ExecutionPolicy", "Bypass", "-File", "`"$($MyInvocation.MyCommand.Path)`"", "-GamePath", "`"$GamePath`"")
    if ($Source) { $argList += @("-Source", "`"$Source`"") }
    if ($RendererOnly) { $argList += "-RendererOnly" }
    if ($NoTextures) { $argList += "-NoTextures" }
    Start-Process powershell -Verb RunAs -ArgumentList $argList -Wait
    exit
}

if ($GamePath -match "\\Program Files") {
    Say "Note: installing under Program Files makes Windows redirect the game's saves and settings. C:\Games\Sports Car GT is recommended."
}

# ---- find the disc ------------------------------------------------------------------------
function Test-Disc($p) { return $p -and (Test-Path (Join-Path $p "MAS\CDB.MAS")) -and (Test-Path (Join-Path $p "DAT")) }
if (-not $RendererOnly) {
    if (-not (Test-Disc $Source)) {
        $candidates = @(
            (Join-Path $kit "..\sportscargt\Sports Car GT"),
            (Join-Path $kit "..\Sports Car GT"),
            (Join-Path $kit "Sports Car GT")
        )
        foreach ($d in (Get-PSDrive -PSProvider FileSystem)) { $candidates += $d.Root }
        foreach ($c in $candidates) { if (Test-Disc $c) { $Source = (Resolve-Path $c).Path; break } }
    }
    if (-not (Test-Disc $Source)) {
        throw "Couldn't find the Sports Car GT disc. Mount the ISO (double-click it) or pass -Source with the folder that has MAS\ and DAT\."
    }
    Say "Disc: $Source"
}

New-Item -ItemType Directory -Force -Path $GamePath | Out-Null
$GamePath = (Resolve-Path $GamePath).Path

if (-not $RendererOnly) {
    # ---- 1. game files from the disc ------------------------------------------------------
    Step "Copying the game from the disc"
    $lines = Get-Content (Join-Path $kit "files.txt") | Where-Object { $_ -and -not $_.StartsWith("#") }
    $n = 0
    foreach ($l in $lines) {
        $dst, $src = $l.Split("|")
        $from = Join-Path $Source $src
        $to = Join-Path $GamePath $dst
        if (-not (Test-Path $from)) { Say "  missing on the disc: $src"; continue }
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $to) | Out-Null
        Copy-Item $from $to -Force
        $n++
        if ($n % 50 -eq 0) { Write-Progress -Activity "Copying game files" -Status $dst -PercentComplete (100 * $n / $lines.Count) }
    }
    Write-Progress -Activity "Copying game files" -Completed
    foreach ($d in @("SAVE", "Patch")) { New-Item -ItemType Directory -Force -Path (Join-Path $GamePath $d) | Out-Null }
    Copy-Item (Join-Path $kit "runtime\*.dll") $GamePath -Force   # Miles sound + Smacker video runtimes
    Say "  $n files"

    # ---- 2. official 1.551 patch, then the 1.551 No-CD exe from the disc's CRACK folder ----
    Step "Applying the 1.551 patch"
    $zip = @((Join-Path $kit "patch\Patch-1551a.zip"), (Join-Path $Source "..\Patch-1551a.zip")) | Where-Object { Test-Path $_ } | Select-Object -First 1
    if ($zip) {
        $tmp = Join-Path $env:TEMP "scgt1551"
        Remove-Item $tmp -Recurse -Force -ErrorAction SilentlyContinue
        Expand-Archive $zip $tmp -Force
        $p = Get-ChildItem $tmp -Directory | Select-Object -First 1
        Copy-Item (Join-Path $p.FullName "Mas\*") (Join-Path $GamePath "MAS") -Force
        Copy-Item (Join-Path $p.FullName "Dat\*") (Join-Path $GamePath "DAT") -Force
        Copy-Item (Join-Path $p.FullName "Spcar.exe") $GamePath -Force
        Remove-Item $tmp -Recurse -Force
        Say "  patched to 1.551"
    } else {
        Say "  Patch-1551a.zip not found: the game stays at the disc version (widescreen needs 1.551)"
    }
    $nocd = Join-Path $Source "CRACK\Spcar.exe"
    if (Test-Path $nocd) {
        Copy-Item $nocd $GamePath -Force
        Say "  1.551 exe that runs without the disc"
    } elseif (-not (Test-Path (Join-Path $GamePath "Spcar.exe"))) {
        throw "No Spcar.exe: the 1.551 patch is needed."
    }

    # files copied from a disc or ISO are read-only; the game writes saves and settings
    Get-ChildItem $GamePath -Recurse -File | ForEach-Object { if ($_.IsReadOnly) { $_.IsReadOnly = $false } }

    # ---- 3. registry (what the original setup writes; the game reads its folders from here)
    Step "Registering the game"
    $hklm = [Microsoft.Win32.RegistryKey]::OpenBaseKey([Microsoft.Win32.RegistryHive]::LocalMachine, [Microsoft.Win32.RegistryView]::Registry32)
    $key = $hklm.CreateSubKey("SOFTWARE\Image Space Inc\Sports Car GT")
    $dir = $GamePath.TrimEnd("\") + "\"
    foreach ($v in @("DirAnim", "DirDAT", "DirMAS", "DirOptions", "DirPatch", "DirSounds", "DirVCR")) { $key.SetValue($v, $dir) }
    $key.SetValue("InstallPath", $GamePath)
    $key.SetValue("InstallFromPath", $Source)
    $key.SetValue("Name", "Sports Car GT")
    $key.SetValue("SKU", "5214583053")
    $key.SetValue("Version", "1.48")
    $key.Close()
    Say "  HKLM\SOFTWARE\Image Space Inc\Sports Car GT"
}

if (-not (Test-Path (Join-Path $GamePath "Spcar.exe"))) {
    throw "Spcar.exe not found in '$GamePath'."
}

# ---- 4. renderer ------------------------------------------------------------------------------
Step "Installing the scgt2026 renderer"
$dll = Join-Path $GamePath "glide2x.dll"
$backup = Join-Path $GamePath "glide2x.original.dll"
if ((Test-Path $dll) -and -not (Test-Path $backup) -and (Get-FileHash $dll).Hash -ne (Get-FileHash (Join-Path $kit "glide2x.dll")).Hash) {
    Copy-Item $dll $backup   # someone else's Glide wrapper (nGlide, dgVoodoo...): keep it
}
Copy-Item (Join-Path $kit "glide2x.dll") $dll -Force
$ini = Join-Path $GamePath "scgt2026.ini"
if (-not (Test-Path $ini)) { Copy-Item (Join-Path $kit "scgt2026.ini") $ini; Say "  settings: scgt2026.ini (defaults)" }
else { Say "  kept your scgt2026.ini" }

# ---- 5. HD textures -------------------------------------------------------------------------
if (-not $NoTextures) {
    $tex = Join-Path $GamePath "textures"
    $zips = Get-ChildItem $kit -Filter "textures-*.zip" -ErrorAction SilentlyContinue | Sort-Object Name
    $loose = Join-Path $kit "textures"
    if ($zips -or (Test-Path $loose)) {
        Step "Installing the HD texture pack"
        New-Item -ItemType Directory -Force -Path $tex | Out-Null
        foreach ($z in $zips) { Expand-Archive $z.FullName $tex -Force; Say "  $($z.Name)" }
        if (Test-Path $loose) { Copy-Item (Join-Path $loose "*.png") $tex -Force }
        Say ("  {0} HD textures" -f (Get-ChildItem $tex -Filter *.png).Count)
    }
}

# ---- 6. Windows integration -------------------------------------------------------------------
$exe = Join-Path $GamePath "Spcar.exe"
$layers = "HKCU:\Software\Microsoft\Windows NT\CurrentVersion\AppCompatFlags\Layers"
New-Item -Path $layers -Force | Out-Null
Set-ItemProperty -Path $layers -Name $exe -Value "~ HIGHDPIAWARE"
$shell = New-Object -ComObject WScript.Shell
foreach ($folder in @([Environment]::GetFolderPath("Desktop"), [Environment]::GetFolderPath("Programs"))) {
    $lnk = $shell.CreateShortcut((Join-Path $folder "Sports Car GT 2026.lnk"))
    $lnk.TargetPath = $exe
    $lnk.WorkingDirectory = $GamePath
    $lnk.IconLocation = "$exe,0"
    $lnk.Save()
}

Write-Host ""
Write-Host "Done. Sports Car GT is installed in $GamePath" -ForegroundColor Green
Write-Host "Start it from the 'Sports Car GT 2026' shortcut. In the launcher, pick '3Dfx Voodoo' under Select Video Driver and click 'Play Sports Car'."
Write-Host "In the game: Options > Graphics > 1024x768, E-Mapping and MIP Mapping on, for the sharpest picture."
Write-Host "Settings (resolution, widescreen, anti-aliasing): $ini"
if ($Host.Name -eq "ConsoleHost") { Read-Host "Press Enter to close" | Out-Null }
