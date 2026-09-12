# Package-Editor.ps1
# Builds the Editor, boxes the executable with Enigma Virtual Box, and keeps
# Packages as an external, inspectable directory beside TomCat.exe.
#
# Usage:
#   .\Scripts\Package-Editor.ps1 -Build -EnigmaProject editor.evb
#
param(
    [string]$SourceDir = "",
    [string]$Version = "",
    [string]$MsBuildPath = "D:\Microsoft Visual Studio\Versions\2026 Pro\MSBuild\Current\Bin\MSBuild.exe",
    [string]$PremakePath = "",
    [string]$EnigmaProject = "editor.evb",
    [string]$EnigmaConsole = "",
    [switch]$Build
)

$ErrorActionPreference = "Stop"

$RepoRoot = Split-Path -Parent $PSScriptRoot
if (-not $SourceDir) { $SourceDir = Join-Path $RepoRoot "Editor\bin\Release-windows-x86_64\TomCatInut" }
$FinalName = "TomCat"

# EVB projects have a legacy, non-standard root element. Shared helpers still
# handle template properties and validation; Editor Packages stay external.
. (Join-Path $PSScriptRoot "EvbTools.ps1")
. (Join-Path $PSScriptRoot "ManagedReleaseTools.ps1")

# 1) Optional rebuild
if ($Build) {
    Write-Host "[Managed] Building Release toolchain ..."
    Build-TomCatManagedRelease -RepositoryRoot $RepoRoot
    if (-not (Test-Path $MsBuildPath)) { throw "MSBuild not found: $MsBuildPath" }
    if (-not $PremakePath) {
        $PremakePath = Join-Path $RepoRoot "vendor\premake\bin\premake5.exe"
    }
    if (-not (Test-Path -LiteralPath $PremakePath -PathType Leaf)) {
        throw "Premake5 not found: $PremakePath. Run Scripts\Setup.bat or pass -PremakePath."
    }
    Write-Host "[Player] Generating Player solution ..."
    & $PremakePath "--file=$(Join-Path $RepoRoot 'Player\premake5.lua')" vs2022
    if ($LASTEXITCODE -ne 0) { throw "Player project generation failed (exit $LASTEXITCODE)" }
    Write-Host "[Player] Building Release x64 ..."
    & $MsBuildPath (Join-Path $RepoRoot "Player\Player.sln") -p:Configuration=Release -p:Platform=x64 -m -v:m -nologo
    if ($LASTEXITCODE -ne 0) { throw "Player build failed (exit $LASTEXITCODE)" }
    Write-Host "[Editor] Generating Editor solution ..."
    & $PremakePath "--file=$(Join-Path $RepoRoot 'Editor\premake5.lua')" vs2022
    if ($LASTEXITCODE -ne 0) { throw "Editor project generation failed (exit $LASTEXITCODE)" }
    $editorSolution = Join-Path $RepoRoot "Editor\Editor.sln"
    Write-Host "[Editor] Building Release x64 ..."
    & $MsBuildPath $editorSolution -p:Configuration=Release -p:Platform=x64 -m -v:m -nologo
    if ($LASTEXITCODE -ne 0) { throw "Build failed (exit $LASTEXITCODE)" }
} else {
    Write-Host "[Build] Using existing Managed, Player and Editor Release outputs"
}

if (-not (Test-Path $SourceDir -PathType Container)) { throw "Source directory not found: $SourceDir" }
$SourceDir = (Resolve-Path -LiteralPath $SourceDir).Path
$playerTemplate = Join-Path $SourceDir "Packages\PlayerTemplates\win-x64"
Write-Host "[Template] Generating and validating win-x64 Player Template ..."
& (Join-Path $PSScriptRoot "Build-PlayerTemplate.ps1") -Configuration Release -Destination $playerTemplate
if ($LASTEXITCODE -ne 0) { throw "Player Template generation failed (exit $LASTEXITCODE)" }
$packageSource = Resolve-EvbPackageDirectory -SourceDir $SourceDir
$packageFileCount = @(Get-ChildItem -LiteralPath $packageSource -Recurse -File -Force).Count
Write-Host "Scanning $packageFileCount package candidate file(s) from $packageSource"
if (-not (Test-Path -LiteralPath (Join-Path $SourceDir "TomCat.log") -PathType Leaf)) {
    New-Item -ItemType File -Path (Join-Path $SourceDir "TomCat.log") -Force | Out-Null
}

$dist = Join-Path $RepoRoot "dist"
New-Item -ItemType Directory -Force -Path $dist | Out-Null
$outExe = Join-Path $dist "$FinalName.exe"
$outPackages = Join-Path $dist "Packages"
$outManaged = Join-Path $dist "Managed"
$outArchive = Join-Path $dist "$FinalName.zip"
$nativeRuntimeFiles = @("msvcp140.dll", "vcruntime140.dll", "vcruntime140_1.dll")
foreach ($name in $nativeRuntimeFiles) {
    Copy-Item -LiteralPath (Join-Path $playerTemplate $name) -Destination (Join-Path $dist $name) -Force
}

# Managed is a real, external directory. It must never be embedded into EVB;
# the Editor needs these exact Release artifacts to compile project scripts.
Publish-TomCatManagedRelease -RepositoryRoot $RepoRoot -Destination $outManaged

# 2) Locate .evb and parse its input/output paths
$evbInput = ""
$evbOutput = ""
$generatedEnigmaProject = ""
if ($EnigmaProject) {
    if (-not (Test-Path $EnigmaProject)) {
        $candidates = @(
            (Join-Path (Get-Location) $EnigmaProject),
            (Join-Path $RepoRoot $EnigmaProject),
            (Join-Path (Join-Path $RepoRoot "Scripts") $EnigmaProject)
        )
        $found = $candidates | Where-Object { Test-Path $_ -PathType Leaf } | Select-Object -First 1
        if ($found) { $EnigmaProject = $found }
    }
    if (-not (Test-Path $EnigmaProject)) {
        throw "Enigma project not found: $EnigmaProject`nPut a .evb in the repo root or Scripts/, or pass a full path via -EnigmaProject."
    }
    $templatePath = (Resolve-Path -LiteralPath $EnigmaProject).Path
    $evbText = Get-Content -LiteralPath $templatePath -Raw

    # Support a custom -SourceDir when running the local packaging script.
    $defaultSourceDir = Join-Path $RepoRoot "Editor\bin\Release-windows-x86_64\TomCatInut"
    $evbText = $evbText.Replace('E:\Github\TomCat_Engine', $RepoRoot)
    $evbText = $evbText.Replace($defaultSourceDir, $SourceDir)
    $evbText = Set-EvbProperty -TemplateText $evbText -ElementName "InputFile" -Value (Join-Path $dist "TomCatInut.exe")
    $evbText = Set-EvbProperty -TemplateText $evbText -ElementName "OutputFile" -Value $outExe
    if ($evbText -match '(?is)<Name>\s*Packages\s*</Name>') {
        throw "Editor EVB template must not embed Packages; distribute it beside TomCat.exe"
    }
    if ($evbText -match '(?is)<Name>\s*Managed\s*</Name>') {
        throw "Editor EVB template must not embed Managed; distribute it beside TomCat.exe"
    }

    $generatedEnigmaProject = Join-Path $dist "$FinalName.generated.evb"
    Write-EvbProject -Path $generatedEnigmaProject -Text $evbText
    $EnigmaProject = $generatedEnigmaProject

    if ($evbText -match '<InputFile>(.*?)</InputFile>') { $evbInput = $Matches[1] }
    if ($evbText -match '<OutputFile>(.*?)</OutputFile>') { $evbOutput = $Matches[1] }
}

# 3) Output exe name is intentionally stable; Version is release metadata only.
if (Test-Path $outExe) { Remove-Item $outExe -Force }

# 4) Stage the built exe where .evb expects its input (e.g. dist\TomCatInut.exe)
if ($evbInput) {
    $srcExe = Join-Path $SourceDir "TomCatInut.exe"
    if (-not (Test-Path $srcExe)) { throw "Built exe not found: $srcExe" }
    New-Item -ItemType Directory -Force -Path (Split-Path $evbInput) | Out-Null
    Copy-Item $srcExe $evbInput -Force
    Write-Host "[EVB] Staged input exe -> $evbInput"
}

# 5) Enigma boxing
if ($EnigmaProject) {
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

    Write-Host "[EVB] Boxing with Enigma Virtual Box ($EnigmaProject) ..."
    Push-Location (Split-Path -Parent $EnigmaProject)
    try {
        & $EnigmaConsole (Split-Path -Leaf $EnigmaProject)
    } finally {
        Pop-Location
    }
    if ($LASTEXITCODE -ne 0) { throw "Enigma Virtual Box failed (exit $LASTEXITCODE)" }
} else {
    Write-Host "[EVB] Skipping Enigma boxing (pass -EnigmaProject <file.evb> to enable)"
}

# 6) Keep the boxed executable and publish the real Packages directory beside it.
if ($evbOutput -and (Test-Path $evbOutput)) {
    $evbOutputFull = [System.IO.Path]::GetFullPath($evbOutput)
    $outExeFull = [System.IO.Path]::GetFullPath($outExe)
    if ($evbOutputFull -ne $outExeFull) {
        Copy-Item $evbOutput $outExe -Force
    }
    if (Test-Path -LiteralPath $outPackages) {
        Remove-Item -LiteralPath $outPackages -Recurse -Force
    }
    Copy-Item -LiteralPath $packageSource -Destination $outPackages -Recurse -Force
    if (Test-Path -LiteralPath $outArchive) {
        Remove-Item -LiteralPath $outArchive -Force
    }
    $archiveInputs = @($outExe, $outPackages, $outManaged)
    $archiveInputs += @($nativeRuntimeFiles | ForEach-Object { Join-Path $dist $_ })
    Compress-Archive -LiteralPath $archiveInputs -DestinationPath $outArchive -CompressionLevel Optimal
    Write-Host "[Package] Final package -> $outArchive"
    $sizeMb = [math]::Round((Get-Item $outExe).Length / 1MB, 1)
    Write-Host "Done: $outExe ($sizeMb MB) + $outPackages + $outManaged"
} else {
    throw "Boxed Editor executable not found at '$evbOutput'"
}

# Cleanup intermediate files inside dist/ (input copy + .evb-named boxed exe); keep the final exe
$distPrefix = $dist.TrimEnd('\') + '\'
if ($evbInput -and (Test-Path $evbInput) -and $evbInput.StartsWith($distPrefix)) { Remove-Item $evbInput -Force }
if ($evbOutput -and (Test-Path $evbOutput) -and $evbOutput.StartsWith($distPrefix) -and
    ([System.IO.Path]::GetFullPath($evbOutput) -ne [System.IO.Path]::GetFullPath($outExe))) {
    Remove-Item $evbOutput -Force
}
if ($generatedEnigmaProject -and (Test-Path $generatedEnigmaProject)) { Remove-Item $generatedEnigmaProject -Force }
