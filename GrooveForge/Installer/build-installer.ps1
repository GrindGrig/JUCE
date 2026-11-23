<#
.SYNOPSIS
    Builds the Groove Forge Windows installer from an existing Release build.

.DESCRIPTION
    1. Build the plug-in first (from the JUCE checkout):
         cmake -S GrooveForge -B build -G "Visual Studio 17 2022" -A x64
         cmake --build build --config Release
    2. Then run this script:
         powershell -ExecutionPolicy Bypass -File GrooveForge\Installer\build-installer.ps1 -BuildDir build

    Needs Inno Setup 6 (https://jrsoftware.org/isinfo.php, or: winget install JRSoftware.InnoSetup).
    The installer is written to GrooveForge\Installer\Output.
#>
param(
    [string] $BuildDir  = "build",
    [string] $Config    = "Release",
    [string] $Version   = "",
    [string] $SourceUrl = "",
    [string] $OutputDir = ""
)

$ErrorActionPreference = "Stop"
$here     = Split-Path -Parent $MyInvocation.MyCommand.Path
$project  = Split-Path -Parent $here            # GrooveForge
$juceRoot = Split-Path -Parent $project         # JUCE checkout

if (-not [System.IO.Path]::IsPathRooted($BuildDir)) { $BuildDir = Join-Path (Get-Location) $BuildDir }
if ($OutputDir -eq "") { $OutputDir = Join-Path $here "Output" }

# Version: from the project() call in GrooveForge/CMakeLists.txt unless given
if ($Version -eq "") {
    $cmake = Get-Content (Join-Path $project "CMakeLists.txt") -Raw
    if ($cmake -match 'project\s*\(\s*\w+\s+VERSION\s+([0-9.]+)') { $Version = $Matches[1] } else { $Version = "0.0.0" }
}

$artefacts = Join-Path $BuildDir "GrooveForge_artefacts\$Config"
foreach ($p in @("VST3\Groove Forge.vst3", "Standalone\Groove Forge.exe")) {
    if (-not (Test-Path (Join-Path $artefacts $p))) {
        throw "Missing build output: $(Join-Path $artefacts $p). Build the '$Config' configuration first."
    }
}

# License.txt = notice (with the source location) + the full AGPLv3 text shipped with JUCE
$url = if ($SourceUrl -ne "") { $SourceUrl } else { "available from the publisher on request" }
$notice = (Get-Content (Join-Path $here "Notice.txt") -Raw).Replace("@SOURCE_URL@", $url)
$agpl = Get-Content (Join-Path $juceRoot "modules\juce_gui_extra\native\typescript\webview-interop\LICENSE-AGPL.md") -Raw
Set-Content -Path (Join-Path $here "License.txt") -Value ($notice + $agpl) -Encoding UTF8

# Find the Inno Setup compiler
$iscc = (Get-Command iscc.exe -ErrorAction SilentlyContinue).Source
if (-not $iscc) {
    foreach ($c in @("${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe", "$env:ProgramFiles\Inno Setup 6\ISCC.exe", "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe")) {
        if (Test-Path $c) { $iscc = $c; break }
    }
}
if (-not $iscc) { throw "Inno Setup 6 not found. Install it (winget install JRSoftware.InnoSetup) and run this script again." }

Write-Host "Building installer $Version with $iscc"
& $iscc "/DAppVersion=$Version" "/DArtefactsDir=$artefacts" "/DOutputDir=$OutputDir" "/DSourceUrl=$SourceUrl" (Join-Path $here "GrooveForge.iss")
if ($LASTEXITCODE -ne 0) { throw "ISCC failed with exit code $LASTEXITCODE" }

Get-ChildItem $OutputDir -Filter "GrooveForge-*-Setup.exe" | ForEach-Object { Write-Host "Installer: $($_.FullName)" }
