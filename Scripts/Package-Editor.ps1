# Package-Editor.ps1
# Packages the TomCatInut (Editor) Release build output into a zip,
# excluding linker artifacts (*.exp, *.pdb, *.lib). Runtime files such as
# TomCat.log and the shader cache under Packages/ are packaged as-is.
#
# Optional: after zipping, box the editor into a single exe with
# Enigma Virtual Box (enigmavbconsole.exe) using a .evb project.
#
# Usage:
#   .\Scripts\Package-Editor.ps1                          # package existing build
#   .\Scripts\Package-Editor.ps1 -Version 0.2.0           # custom zip name
#   .\Scripts\Package-Editor.ps1 -Build                   # rebuild Release x64 first
#   .\Scripts\Package-Editor.ps1 -EnigmaProject editor.evb   # also box into single exe
#
param(
    [string]$SourceDir = "",
    [string]$Version = "",
    [string]$MsBuildPath = "D:\Microsoft Visual Studio\Versions\2026 Pro\MSBuild\Current\Bin\MSBuild.exe",
    [string]$EnigmaProject = "",
    [string]$EnigmaConsole = "",
    [switch]$Build
)

$ErrorActionPreference = "Stop"

# Repo root = parent of the scripts/ folder
$RepoRoot = Split-Path -Parent $PSScriptRoot
if (-not $SourceDir) { $SourceDir = Join-Path $RepoRoot "Editor\bin\Release-windows-x86_64\TomCatInut" }

# 1) Optional rebuild
if ($Build) {
    if (-not (Test-Path $MsBuildPath)) { throw "MSBuild not found: $MsBuildPath" }
    $proj = Join-Path $RepoRoot "Editor\TomCatInut\TomCatInut.vcxproj"
    Write-Host "[1/5] Building Release x64 ..."
    & $MsBuildPath $proj -p:Configuration=Release -p:Platform=x64 -m -v:m -nologo
    if ($LASTEXITCODE -ne 0) { throw "Build failed (exit $LASTEXITCODE)" }
} else {
    Write-Host "[1/5] Skipping build (use -Build to rebuild first)"
}

if (-not (Test-Path $SourceDir)) { throw "Source directory not found: $SourceDir" }

# 2) Zip name
if (-not $Version) { $Version = Get-Date -Format "yyyyMMdd-HHmm" }
$dist = Join-Path $RepoRoot "dist"
New-Item -ItemType Directory -Force -Path $dist | Out-Null
$zip = Join-Path $dist "TomCatEditor-$Version.zip"
if (Test-Path $zip) { Remove-Item $zip -Force }

# 3) Stage files (exclude linker artifacts & runtime logs)
$staging = Join-Path $env:TEMP ("TomCatPackage_" + $PID)
if (Test-Path $staging) { Remove-Item $staging -Recurse -Force }
New-Item -ItemType Directory -Force -Path $staging | Out-Null

Write-Host "[2/5] Copying files from $SourceDir"
robocopy $SourceDir $staging /E /XF "*.exp" "*.pdb" "*.lib" /NFL /NDL /NJH /NJS /NP | Out-Null
if ($LASTEXITCODE -ge 8) { throw "robocopy failed (exit $LASTEXITCODE)" }

# 4) Compress
Write-Host "[3/5] Compressing ..."
Compress-Archive -Path "$staging\*" -DestinationPath $zip -CompressionLevel Optimal

# 5) Optional: Enigma Virtual Box single-exe boxing
if ($EnigmaProject) {
    if (-not (Test-Path $EnigmaProject)) { throw "Enigma project not found: $EnigmaProject" }

    if (-not $EnigmaConsole) {
        $candidates = @(
            "D:\Enigma Virtual Box\enigmavbconsole.exe",
            "$env:ProgramFiles\Enigma Virtual Box\enigmavbconsole.exe",
            "${env:ProgramFiles(x86)}\Enigma Virtual Box\enigmavbconsole.exe",
            "$env:LOCALAPPDATA\Enigma Virtual Box\enigmavbconsole.exe"
        )
        $EnigmaConsole = $candidates | Where-Object { Test-Path $_ } | Select-Object -First 1
        if (-not $EnigmaConsole) { throw "enigmavbconsole.exe not found. Pass -EnigmaConsole <path>" }
    }
    if (-not (Test-Path $EnigmaConsole)) { throw "Enigma console not found: $EnigmaConsole" }

    Write-Host "[4/5] Boxing with Enigma Virtual Box ($EnigmaProject) ..."
    Push-Location (Split-Path -Parent $EnigmaProject)
    try {
        & $EnigmaConsole (Split-Path -Leaf $EnigmaProject)
    } finally {
        Pop-Location
    }
    if ($LASTEXITCODE -ne 0) { throw "Enigma Virtual Box failed (exit $LASTEXITCODE)" }
    Write-Host "      -> Boxed exe output is configured inside $EnigmaProject"
} else {
    Write-Host "[4/5] Skipping Enigma boxing (pass -EnigmaProject <file.evb> to enable)"
}

Remove-Item $staging -Recurse -Force

$sizeMb = [math]::Round((Get-Item $zip).Length / 1MB, 1)
Write-Host "[5/5] Done: $zip ($sizeMb MB)"
