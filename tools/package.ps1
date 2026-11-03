<#
  Builds the distribution of Laser Lords:

    dist\LaserLords-<version>-portable.zip   unzip anywhere and run
    dist\LaserLords-<version>-Setup.exe      installer (Inno Setup), asks for the disc
    dist\SHA256SUMS.txt

  The version comes from the VERSION file.  The disc image is never packaged.

  Usage:  powershell -ExecutionPolicy Bypass -File tools\package.ps1 [-NoBuild]
#>
param([switch]$NoBuild)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
Set-Location $root
$ver = (Get-Content (Join-Path $root 'VERSION') -TotalCount 1).Trim()
$name = "LaserLords-$ver"
$dist = Join-Path $root 'dist'
$stage = Join-Path $dist $name

if (-not $NoBuild) {
    Write-Host "Building $ver..."
    & (Join-Path $root 'build.bat')
    if ($LASTEXITCODE -ne 0) { throw 'build failed' }
}
foreach ($exe in 'laserlords.exe', 'laserlords_hd.exe') {
    if (-not (Test-Path (Join-Path $root $exe))) { throw "$exe is missing: run build.bat" }
}

# staging folder: what the installer installs and the zip contains
if (Test-Path $stage) { Remove-Item $stage -Recurse -Force }
New-Item -ItemType Directory -Force $stage, (Join-Path $stage 'docs'), (Join-Path $stage 'rom') | Out-Null
Copy-Item laserlords.exe, laserlords_hd.exe, README.md, VERSION -Destination $stage
Copy-Item docs\CDI_NOTES.md -Destination (Join-Path $stage 'docs')
@"
Put your copy of the Laser Lords CD-i disc image here
(the .cue file and its .bin, raw 2352-byte sectors), or tell the game where
it is: it asks when it starts, and remembers it in laserlords.ini.
"@ | Set-Content -Encoding ascii (Join-Path $stage 'rom\PUT_THE_DISC_IMAGE_HERE.txt')
@"
Laser Lords $ver - CD-i static recompilation

  laserlords_hd.exe   HD edition: Cartoon HD upscaling, run-ahead, sprite
                      interpolation at 60 fps
  laserlords.exe      the game as the CD-i showed it

Both: quicksave (F2 save, F3 slot, F4 load) and invincibility (F5).

Everything the game writes (saves, settings, quicksaves, log) stays in this
folder, in saves\ and laserlords.ini; nothing goes to AppData.  To move the
game, move the folder.  The disc image is not included: see rom\.

At start the game checks for a newer release and offers to install it
(laserlords.ini: update_check=1 ask, auto, or 0 off).  See README.md.
"@ | Set-Content -Encoding ascii (Join-Path $stage 'READ ME FIRST.txt')

# portable zip
$zip = Join-Path $dist "$name-portable.zip"
if (Test-Path $zip) { Remove-Item $zip -Force }
Compress-Archive -Path $stage -DestinationPath $zip -CompressionLevel Optimal
Write-Host "  $zip"

# installer
$iscc = @("$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe",
          "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe",
          "$env:ProgramFiles\Inno Setup 6\ISCC.exe") | Where-Object { Test-Path $_ } | Select-Object -First 1
if ($iscc) {
    & $iscc /Q "/DAppVersion=$ver" "/DStageDir=$stage" (Join-Path $root 'installer\laserlords.iss')
    if ($LASTEXITCODE -ne 0) { throw 'Inno Setup failed' }
    Write-Host "  $(Join-Path $dist "$name-Setup.exe")"
} else {
    Write-Warning 'Inno Setup (ISCC.exe) not found: only the portable zip was made. winget install JRSoftware.InnoSetup'
}

# checksums
Get-ChildItem $dist -File | Where-Object { $_.Name -like "$name-*" } | ForEach-Object {
    "{0}  {1}" -f (Get-FileHash $_.FullName -Algorithm SHA256).Hash.ToLower(), $_.Name
} | Set-Content -Encoding ascii (Join-Path $dist 'SHA256SUMS.txt')
Write-Host "Done: $dist"
