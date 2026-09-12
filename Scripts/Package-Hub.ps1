# Package-Hub.ps1
# Builds the Hub (Manager), boxes it into a single exe with Enigma Virtual Box,
# and outputs ONLY the boxed package into dist/.
#
# Usage:
#   .\Scripts\Package-Hub.ps1 -Build -EnigmaProject hub.evb
#
param(
    [string]$SourceDir = "",
    [string]$Version = "",
    [string]$MsBuildPath = "D:\Microsoft Visual Studio\Versions\2026 Pro\MSBuild\Current\Bin\MSBuild.exe",
    [string]$EnigmaProject = "hub.evb",
    [string]$EnigmaConsole = "",
    [switch]$Build
)

$ErrorActionPreference = "Stop"

$ExeName = "Manager.exe"
$FinalName = "TomCatHub"

# EVB projects have a legacy, non-standard root element.  Keep the tree
# generation in one place so local packaging and CI package exactly the same
# files (including every nested Packages entry).
. (Join-Path $PSScriptRoot "EvbTools.ps1")

$RepoRoot = Split-Path -Parent $PSScriptRoot
if (-not $SourceDir) { $SourceDir = Join-Path $RepoRoot "Builder\bin\Release-windows-x86_64\Manager" }

# 1) Optional rebuild
if ($Build) {
    if (-not (Test-Path $MsBuildPath)) { throw "MSBuild not found: $MsBuildPath" }
    $builderSolution = Join-Path $RepoRoot "Builder\Builder.sln"
    Write-Host "[1/4] Building Release x64 ..."
    & $MsBuildPath $builderSolution -p:Configuration=Release -p:Platform=x64 -m -v:m -nologo
    if ($LASTEXITCODE -ne 0) { throw "Build failed (exit $LASTEXITCODE)" }
} else {
    Write-Host "[1/4] Skipping build (use -Build to rebuild first)"
}

if (-not (Test-Path $SourceDir -PathType Container)) { throw "Source directory not found: $SourceDir" }
$SourceDir = (Resolve-Path -LiteralPath $SourceDir).Path
$packageSource = Resolve-EvbPackageDirectory -SourceDir $SourceDir
$packageFileCount = @(Get-ChildItem -LiteralPath $packageSource -Recurse -File -Force).Count
Write-Host "Scanning $packageFileCount package candidate file(s) from $packageSource"
if (-not (Test-Path -LiteralPath (Join-Path $SourceDir "TomCat.log") -PathType Leaf)) {
    New-Item -ItemType File -Path (Join-Path $SourceDir "TomCat.log") -Force | Out-Null
}

$dist = Join-Path $RepoRoot "dist"
New-Item -ItemType Directory -Force -Path $dist | Out-Null
$outExe = Join-Path $dist "$FinalName.exe"

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
        throw "Enigma project not found: $EnigmaProject`nPut hub.evb in Scripts/, or pass a full path via -EnigmaProject."
    }
    $templatePath = (Resolve-Path -LiteralPath $EnigmaProject).Path
    $evbText = Get-Content -LiteralPath $templatePath -Raw

    # The checked-in projects contain the default build output path.  Replace
    # that prefix before generating the package tree so a custom -SourceDir is
    # also supported by the local script.
    $defaultSourceDir = Join-Path $RepoRoot "Builder\bin\Release-windows-x86_64\Manager"
    $evbText = $evbText.Replace('E:\Github\TomCat_Engine', $RepoRoot)
    $evbText = $evbText.Replace($defaultSourceDir, $SourceDir)
    $evbText = Set-EvbProperty -TemplateText $evbText -ElementName "InputFile" -Value (Join-Path $dist $ExeName)
    $evbText = Set-EvbProperty -TemplateText $evbText -ElementName "OutputFile" -Value $outExe
    $evbText = Set-EvbPackageTree -TemplateText $evbText -PackageSource $packageSource

    $generatedEnigmaProject = Join-Path $dist "$FinalName.generated.evb"
    Write-EvbProject -Path $generatedEnigmaProject -Text $evbText
    $EnigmaProject = $generatedEnigmaProject

    if ($evbText -match '<InputFile>(.*?)</InputFile>') { $evbInput = $Matches[1] }
    if ($evbText -match '<OutputFile>(.*?)</OutputFile>') { $evbOutput = $Matches[1] }
}

# 3) Output exe name is intentionally stable; Version is release metadata only.
if (Test-Path $outExe) { Remove-Item $outExe -Force }

# 4) Stage the built exe where .evb expects its input (e.g. dist\Manager.exe)
if ($evbInput) {
    $srcExe = Join-Path $SourceDir $ExeName
    if (-not (Test-Path $srcExe)) { throw "Built exe not found: $srcExe" }
    New-Item -ItemType Directory -Force -Path (Split-Path $evbInput) | Out-Null
    Copy-Item $srcExe $evbInput -Force
    Write-Host "[2/4] Staged input exe -> $evbInput"
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

    Write-Host "[3/4] Boxing with Enigma Virtual Box ($EnigmaProject) ..."
    Push-Location (Split-Path -Parent $EnigmaProject)
    try {
        & $EnigmaConsole (Split-Path -Leaf $EnigmaProject)
    } finally {
        Pop-Location
    }
    if ($LASTEXITCODE -ne 0) { throw "Enigma Virtual Box failed (exit $LASTEXITCODE)" }
} else {
    Write-Host "[3/4] Skipping Enigma boxing (pass -EnigmaProject <file.evb> to enable)"
}

# 6) Keep the boxed exe as the final package (GitHub Actions compresses on upload)
if ($evbOutput -and (Test-Path $evbOutput)) {
    $evbOutputFull = [System.IO.Path]::GetFullPath($evbOutput)
    $outExeFull = [System.IO.Path]::GetFullPath($outExe)
    if ($evbOutputFull -ne $outExeFull) {
        Copy-Item $evbOutput $outExe -Force
    }
    Write-Host "[4/4] Final package -> $outExe"
    $sizeMb = [math]::Round((Get-Item $outExe).Length / 1MB, 1)
    Write-Host "Done: $outExe ($sizeMb MB)"
} else {
    throw "Boxed Hub executable not found at '$evbOutput'"
}

# Cleanup intermediate files inside dist/ (input copy + .evb-named boxed exe); keep the final exe
$distPrefix = $dist.TrimEnd('\') + '\'
if ($evbInput -and (Test-Path $evbInput) -and $evbInput.StartsWith($distPrefix)) { Remove-Item $evbInput -Force }
if ($evbOutput -and (Test-Path $evbOutput) -and $evbOutput.StartsWith($distPrefix) -and
    ([System.IO.Path]::GetFullPath($evbOutput) -ne [System.IO.Path]::GetFullPath($outExe))) {
    Remove-Item $evbOutput -Force
}
if ($generatedEnigmaProject -and (Test-Path $generatedEnigmaProject)) { Remove-Item $generatedEnigmaProject -Force }
