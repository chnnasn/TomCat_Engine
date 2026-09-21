# Package-Editor.ps1
# Builds the Editor and boxes its complete authoring/runtime payload into one
# self-contained TomCat.exe. Child-process inputs stay virtual until the Editor
# extracts the verified .tomcat-runtime payload into its per-build cache.
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

# EVB projects have a legacy, non-standard root element. Shared helpers update
# the generated virtual Packages and .tomcat-runtime trees as text.
. (Join-Path $PSScriptRoot "EvbTools.ps1")
. (Join-Path $PSScriptRoot "ManagedReleaseTools.ps1")
. (Join-Path $PSScriptRoot "VersionTools.ps1")
. (Join-Path $PSScriptRoot "ReleaseArtifactTools.ps1")

$versionInfo = Get-TomCatVersionInfo -RepositoryRoot $RepoRoot
if ($Version -and $Version -ne $versionInfo.ProductVersion) {
    throw "Requested release version '$Version' does not match Version.h ($($versionInfo.ProductVersion))."
}
$Version = $versionInfo.ProductVersion
Write-Host "[Version] TomCat $Version"

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
    Write-Host "[Tools] Generating headless CLI solution ..."
    & $PremakePath "--file=$(Join-Path $RepoRoot 'Tools\premake5.lua')" vs2022
    if ($LASTEXITCODE -ne 0) { throw "Tools project generation failed (exit $LASTEXITCODE)" }
    Write-Host "[Tools] Building Release x64 ..."
    & $MsBuildPath (Join-Path $RepoRoot "Tools\Tools.sln") -p:Configuration=Release -p:Platform=x64 -m -v:m -nologo
    if ($LASTEXITCODE -ne 0) { throw "Headless CLI build failed (exit $LASTEXITCODE)" }
} else {
    Write-Host "[Build] Using existing Managed, Player, Editor and headless CLI Release outputs"
}

if (-not (Test-Path $SourceDir -PathType Container)) { throw "Source directory not found: $SourceDir" }
$SourceDir = (Resolve-Path -LiteralPath $SourceDir).Path
$packageSource = Resolve-EvbPackageDirectory -SourceDir $SourceDir
$requiredPackageAssets = @(
    "Resources\Sprites\TomCat\Circle.tga",
    "Resources\Sprites\TomCat\Square.tga",
    "fonts\opensans\OpenSans-Regular.ttf"
)
foreach ($requiredPackageAsset in $requiredPackageAssets) {
    $requiredPackageAssetPath = Join-Path $packageSource $requiredPackageAsset
    if (-not (Test-Path -LiteralPath $requiredPackageAssetPath -PathType Leaf)) {
        throw "Required Editor package asset was not found: $requiredPackageAssetPath"
    }
}
$playerTemplate = Join-Path $SourceDir "Packages\PlayerTemplates\win-x64"
Write-Host "[Template] Generating and validating win-x64 Player Template ..."
& (Join-Path $PSScriptRoot "Build-PlayerTemplate.ps1") -Configuration Release -Destination $playerTemplate
if ($LASTEXITCODE -ne 0) { throw "Player Template generation failed (exit $LASTEXITCODE)" }
$packageFileCount = @(Get-ChildItem -LiteralPath $packageSource -Recurse -File -Force).Count
Write-Host "Scanning $packageFileCount package candidate file(s) from $packageSource"

$dist = Join-Path $RepoRoot "dist"
New-Item -ItemType Directory -Force -Path $dist | Out-Null
$outExe = Join-Path $dist "$FinalName.exe"
$outArchive = Join-Path $dist "$FinalName.zip"
$stagingRoot = Join-Path $dist ".tomcat-editor-staging"
$payloadRoot = Join-Path $stagingRoot "payload"
$payloadManaged = Join-Path $payloadRoot "Managed"
$payloadTemplateParent = Join-Path $payloadRoot "Packages\PlayerTemplates"
$payloadPackageRoot = Join-Path $payloadRoot "Packages"
$stagedInputExe = Join-Path $stagingRoot "input\TomCatInut.exe"
$stagedBoxedExe = Join-Path $stagingRoot "boxed\$FinalName.exe"
$cliSource = Join-Path $RepoRoot "Tools\bin\Release-windows-x86_64\TomCatCLI\TomCatCLI.exe"
$cliShaderSource = Join-Path (Split-Path -Parent $cliSource) "shaderc_shared.dll"
if (-not (Test-Path -LiteralPath $cliSource -PathType Leaf)) {
    throw "Headless CLI executable not found: $cliSource. Run with -Build or build Tools\Tools.sln first."
}
if (-not (Test-Path -LiteralPath $cliShaderSource -PathType Leaf)) {
    throw "Headless CLI shader runtime was not found: $cliShaderSource. Run with -Build or rebuild Tools\Tools.sln first."
}
$nativeRuntimeFiles = @("msvcp140.dll", "vcruntime140.dll", "vcruntime140_1.dll")
$evbInput = ""
$evbOutput = ""
$generatedEnigmaProject = ""
$packageSucceeded = $false
$releaseMetadataFiles = @(
    (Join-Path $dist "SHA256SUMS"),
    (Join-Path $dist "THIRD_PARTY_NOTICES.txt"),
    (Join-Path $dist "TomCat.spdx.json"),
    (Join-Path $dist "RELEASE_PROVENANCE.json")
)

# Remove outputs from the former ZIP layout before constructing the private EVB
# staging tree. These fixed paths are all direct children of the repository's
# dist directory.
$staleOutputs = @(
        $outExe,
        $outArchive,
        (Join-Path $dist "TomCat"),
        (Join-Path $dist "TomCatInut.exe"),
        (Join-Path $dist "$FinalName.generated.evb"),
        (Join-Path $dist "TomCatCLI.exe"),
        (Join-Path $dist "shaderc_shared.dll"),
        (Join-Path $dist "msvcp140.dll"),
        (Join-Path $dist "vcruntime140.dll"),
        (Join-Path $dist "vcruntime140_1.dll"),
        (Join-Path $dist "Packages"),
        (Join-Path $dist "Managed")) + $releaseMetadataFiles + @($stagingRoot)
foreach ($stalePath in $staleOutputs) {
    if (Test-Path -LiteralPath $stalePath) {
        Remove-Item -LiteralPath $stalePath -Recurse -Force
    }
}

try {
New-Item -ItemType Directory -Path $payloadRoot -Force | Out-Null
New-Item -ItemType Directory -Path (Split-Path -Parent $stagedBoxedExe) -Force | Out-Null
Copy-Item -LiteralPath $cliSource -Destination (Join-Path $payloadRoot "TomCatCLI.exe") -Force
Copy-Item -LiteralPath $cliShaderSource -Destination (Join-Path $payloadRoot "shaderc_shared.dll") -Force
Copy-Item -LiteralPath (Join-Path (Split-Path -Parent $cliSource) "assimp-vc143-mt.dll") -Destination (Join-Path $payloadRoot "assimp-vc143-mt.dll") -Force
foreach ($name in $nativeRuntimeFiles) {
    $runtimeSource = Join-Path $playerTemplate $name
    if (-not (Test-Path -LiteralPath $runtimeSource -PathType Leaf)) {
        throw "Player template native runtime was not found: $runtimeSource"
    }
    Copy-Item -LiteralPath $runtimeSource -Destination (Join-Path $payloadRoot $name) -Force
}

Publish-TomCatManagedRelease -RepositoryRoot $RepoRoot -Destination $payloadManaged
New-Item -ItemType Directory -Path $payloadTemplateParent -Force | Out-Null
Copy-Item -LiteralPath $playerTemplate -Destination $payloadTemplateParent -Recurse -Force
foreach ($requiredPackageAsset in $requiredPackageAssets) {
    $requiredPackageAssetPath = Join-Path $packageSource $requiredPackageAsset
    $packageAssetDestination = Join-Path $payloadPackageRoot $requiredPackageAsset
    New-Item -ItemType Directory -Path (Split-Path -Parent $packageAssetDestination) `
        -Force | Out-Null
    Copy-Item -LiteralPath $requiredPackageAssetPath `
        -Destination $packageAssetDestination -Force
}
[void](New-TomCatRuntimeManifest -PayloadRoot $payloadRoot `
    -EngineBuildId $versionInfo.EngineBuildID)

# 2) Locate .evb and parse its input/output paths
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
    $evbText = Set-EvbProperty -TemplateText $evbText -ElementName "InputFile" -Value $stagedInputExe
    $evbText = Set-EvbProperty -TemplateText $evbText -ElementName "OutputFile" -Value $stagedBoxedExe
    $evbText = Set-EvbPackageTree -TemplateText $evbText `
        -PackageSource $packageSource -ExcludeRelativePaths @("PlayerTemplates")
    $evbText = Set-EvbDirectoryTree -TemplateText $evbText `
        -NodeName ".tomcat-runtime" -SourceDirectory $payloadRoot `
        -FileAction 0 -DirectoryAction 3

    $generatedEnigmaProject = Join-Path $stagingRoot "$FinalName.generated.evb"
    Write-EvbProject -Path $generatedEnigmaProject -Text $evbText
    $EnigmaProject = $generatedEnigmaProject
    # Keep physical staging paths separately from their XML-escaped text so a
    # repository path containing '&' or another XML metacharacter remains valid.
    $evbInput = $stagedInputExe
    $evbOutput = $stagedBoxedExe
}

# 3) Stage the built exe inside the private tree referenced by the generated EVB.
if ($evbInput) {
    $srcExe = Join-Path $SourceDir "TomCatInut.exe"
    if (-not (Test-Path $srcExe)) { throw "Built exe not found: $srcExe" }
    New-Item -ItemType Directory -Force -Path (Split-Path $evbInput) | Out-Null
    Copy-Item $srcExe $evbInput -Force
    Write-Host "[EVB] Staged input exe -> $evbInput"
}

# 4) Enigma boxing
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

# 5) Validate the boxed file while it is still private, then publish it with a
# same-volume rename. No partial TomCat.exe is ever visible in dist.
if ($evbOutput -and (Test-Path -LiteralPath $evbOutput -PathType Leaf)) {
    $evbOutputFull = [System.IO.Path]::GetFullPath($evbOutput)
    $outExeFull = [System.IO.Path]::GetFullPath($outExe)
    $boxedLength = (Get-Item -LiteralPath $evbOutputFull).Length
    if ($boxedLength -le 0) {
        throw "Enigma Virtual Box produced an empty Editor executable: $evbOutputFull"
    }
    if ([System.IO.Path]::GetPathRoot($evbOutputFull) -ne
        [System.IO.Path]::GetPathRoot($outExeFull)) {
        throw "Editor staging and final output must stay on the same volume for atomic publication."
    }
    [System.IO.File]::Move($evbOutputFull, $outExeFull)
    New-TomCatReleaseMetadata -RepositoryRoot $RepoRoot -DistPath $dist -Target editor -Version $Version | Out-Null
    $packageSucceeded = $true
    Write-Host "[Package] Single-file Editor -> $outExe"
    $sizeMb = [math]::Round((Get-Item $outExe).Length / 1MB, 1)
    Write-Host "Done: $outExe ($sizeMb MB)"
} else {
    throw "Boxed Editor executable not found at '$evbOutput'"
}

}
finally {
    # A failed run owns no publishable output. Remove the just-published file and
    # any metadata that may have been partially written before cleaning staging.
    if (-not $packageSucceeded) {
        foreach ($failedOutput in @($outExe) + $releaseMetadataFiles) {
            if (Test-Path -LiteralPath $failedOutput) {
                Remove-Item -LiteralPath $failedOutput -Recurse -Force -ErrorAction SilentlyContinue
            }
        }
    }
    if (Test-Path -LiteralPath $stagingRoot) {
        Remove-Item -LiteralPath $stagingRoot -Recurse -Force -ErrorAction SilentlyContinue
    }
}
