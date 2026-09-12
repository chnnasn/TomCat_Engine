param(
    [ValidateSet("Debug", "Release", "Dist")]
    [string]$Configuration = "Release",
    [switch]$RequirePlayer
)

$ErrorActionPreference = "Stop"
$repositoryRoot = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot "ManagedReleaseTools.ps1")
$outputName = "$Configuration-windows-x86_64"
$testOutput = Join-Path $repositoryRoot `
    "Tests\bin\$outputName\ScriptCompilerRegression"
$testExecutable = Join-Path $testOutput "ScriptCompilerRegression.exe"
if (-not (Test-Path -LiteralPath $testExecutable -PathType Leaf)) {
    throw "ScriptCompilerRegression was not built: $testExecutable"
}

$managedSources = [ordered]@{}
foreach ($entry in $script:TomCatManagedReleaseFileMap.GetEnumerator()) {
    $managedSources[$entry.Key] = Join-Path $repositoryRoot $entry.Value
}
foreach ($source in $managedSources.Values) {
    if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
        throw "Managed Release output was not built: $source"
    }
}

$dotnetExecutable = Assert-DotNet10Sdk
$systemDotnetRoot = Split-Path -Parent $dotnetExecutable
if (-not (Test-Path -LiteralPath (Join-Path $systemDotnetRoot "host\fxr") -PathType Container)) {
    throw "Could not derive a usable private dotnet root from $dotnetExecutable."
}

$templateRoot = Join-Path $repositoryRoot `
    "Tests\bin\$outputName\PlayerTemplateRegression"
$templateManifest = Join-Path $templateRoot "template.json"
if ($RequirePlayer -and -not (Test-Path -LiteralPath $templateManifest -PathType Leaf)) {
    $templateBuilder = Join-Path $PSScriptRoot "Build-PlayerTemplate.ps1"
    if (-not (Test-Path -LiteralPath $templateBuilder -PathType Leaf)) {
        throw "Player Template builder was not found: $templateBuilder"
    }
    & $templateBuilder -Configuration $Configuration -Destination $templateRoot
    if ($LASTEXITCODE -ne 0) {
        throw "Player Template generation failed (exit $LASTEXITCODE)."
    }
}

$playerExecutable = $null
$playerTemplateRoot = $null
$privateDotNetRoot = $systemDotnetRoot
if (Test-Path -LiteralPath $templateManifest -PathType Leaf) {
    $candidatePlayer = Join-Path $templateRoot "TomCatPlayer.exe"
    $candidateDotNet = Join-Path $templateRoot "dotnet"
    $requiredRuntimeFiles = @(
        (Join-Path $templateRoot "msvcp140.dll"),
        (Join-Path $templateRoot "vcruntime140.dll"),
        (Join-Path $templateRoot "vcruntime140_1.dll"),
        (Join-Path $templateRoot "Managed\TomCat.Managed.dll"),
        (Join-Path $templateRoot "Managed\TomCat.ScriptHost.dll"),
        (Join-Path $templateRoot "Managed\TomCat.ScriptHost.runtimeconfig.json"),
        (Join-Path $templateRoot "Managed\TomCat.ScriptHost.deps.json")
    )
    foreach ($required in $requiredRuntimeFiles) {
        if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
            throw "Packaged Player runtime input is missing: $required"
        }
    }
    if (Test-Path -LiteralPath (Join-Path $templateRoot "Managed\TomCat.ScriptGenerator.dll")) {
        throw "Player Template contains the Editor-only TomCat.ScriptGenerator.dll."
    }
    $hostFxr = @(Get-ChildItem -LiteralPath (Join-Path $candidateDotNet "host\fxr") `
        -Filter "hostfxr.dll" -Recurse -File -ErrorAction SilentlyContinue)
    $coreClr = @(Get-ChildItem -LiteralPath (Join-Path $candidateDotNet "shared\Microsoft.NETCore.App") `
        -Filter "coreclr.dll" -Recurse -File -ErrorAction SilentlyContinue)
    if (-not (Test-Path -LiteralPath $candidatePlayer -PathType Leaf) -or
        $hostFxr.Count -ne 1 -or $coreClr.Count -ne 1) {
        throw "Player Template does not contain one complete private .NET runtime: $templateRoot"
    }
    $playerExecutable = $candidatePlayer
	$playerTemplateRoot = $templateRoot
    $privateDotNetRoot = $candidateDotNet
}
elseif ($RequirePlayer) {
    throw "A packaged TomCatPlayer.exe with its private runtime was required but no Player Template was available."
}

$temporaryBase = [System.IO.Path]::GetFullPath([System.IO.Path]::GetTempPath())
$stageRoot = Join-Path $temporaryBase ("TomCat-Packaged-Smoke-" + [guid]::NewGuid().ToString("N"))
$stageRoot = [System.IO.Path]::GetFullPath($stageRoot)
if (-not $stageRoot.StartsWith($temporaryBase, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "Refusing to stage outside the operating-system temporary directory: $stageRoot"
}

$savedEnvironment = @{
	DOTNET_ROOT = $env:DOTNET_ROOT
	DOTNET_ROOT_X64 = $env:DOTNET_ROOT_X64
    TOMCAT_E2E_MANAGED_DIR = $env:TOMCAT_E2E_MANAGED_DIR
    TOMCAT_E2E_DOTNET_ROOT = $env:TOMCAT_E2E_DOTNET_ROOT
    TOMCAT_E2E_ISOLATE_DOTNET = $env:TOMCAT_E2E_ISOLATE_DOTNET
    TOMCAT_E2E_REQUIRE_DETACHED = $env:TOMCAT_E2E_REQUIRE_DETACHED
    TOMCAT_E2E_PLAYER_EXE = $env:TOMCAT_E2E_PLAYER_EXE
	TOMCAT_E2E_PLAYER_TEMPLATE = $env:TOMCAT_E2E_PLAYER_TEMPLATE
    TOMCAT_E2E_REQUIRE_PLAYER = $env:TOMCAT_E2E_REQUIRE_PLAYER
}

function Invoke-PlayerCliProbe {
    param(
        [Parameter(Mandatory)][string]$Name,
        [Parameter(Mandatory)][string[]]$Arguments,
        [Parameter(Mandatory)][int]$ExpectedExitCode,
        [Parameter(Mandatory)][string]$WorkingDirectory
    )
    $process = Start-Process -FilePath $playerExecutable -ArgumentList $Arguments `
        -WorkingDirectory $WorkingDirectory -NoNewWindow -Wait -PassThru
    if ($process.ExitCode -ne $ExpectedExitCode) {
        throw "$Name returned exit $($process.ExitCode); expected $ExpectedExitCode."
    }
}

try {
    $stageHarness = Join-Path $stageRoot "Harness"
    $stageManaged = Join-Path $stageHarness "Managed"
    New-Item -ItemType Directory -Path $stageManaged -Force | Out-Null
    Copy-Item -LiteralPath $testExecutable -Destination $stageHarness
    Get-ChildItem -LiteralPath $testOutput -Filter "*.dll" -File |
        Copy-Item -Destination $stageHarness
	foreach ($entry in $managedSources.GetEnumerator()) {
		Copy-Item -LiteralPath $entry.Value -Destination `
			(Join-Path $stageManaged $entry.Key)
	}

    if ($playerExecutable) {
		# Player startup writes TomCat.log beside its executable. Exercise CLI/runtime
		# from a disposable copy so the template passed to PlayerBuilder remains an
		# exact, hash-checked file set.
		$stagedPlayerRoot = Join-Path $stageRoot "PlayerRuntime"
		New-Item -ItemType Directory -Path $stagedPlayerRoot -Force | Out-Null
		Get-ChildItem -LiteralPath $playerTemplateRoot -Force |
			Copy-Item -Destination $stagedPlayerRoot -Recurse -Force
		$playerExecutable = Join-Path $stagedPlayerRoot "TomCatPlayer.exe"
		$privateDotNetRoot = Join-Path $stagedPlayerRoot "dotnet"
        Invoke-PlayerCliProbe -Name "Player --help" -Arguments @("--help") `
            -ExpectedExitCode 0 -WorkingDirectory $stageHarness
        Invoke-PlayerCliProbe -Name "Player --version" -Arguments @("--version") `
            -ExpectedExitCode 0 -WorkingDirectory $stageHarness
        Invoke-PlayerCliProbe -Name "Player unsupported CLI" -Arguments @("--unsupported") `
            -ExpectedExitCode 2 -WorkingDirectory $stageHarness
		Invoke-PlayerCliProbe -Name "Player undocumented short help alias" -Arguments @("-h") `
			-ExpectedExitCode 2 -WorkingDirectory $stageHarness
        $missingRelativePackage = "Missing-$([guid]::NewGuid().ToString('N')).tcpak"
        Invoke-PlayerCliProbe -Name "Player missing package" `
            -Arguments @("--validate-package", $missingRelativePackage) `
            -ExpectedExitCode 3 -WorkingDirectory $stageHarness
        $malformedPackage = Join-Path $stageHarness "Malformed.tcpak"
        [System.IO.File]::WriteAllBytes($malformedPackage, [byte[]](0x54, 0x43, 0x00))
        Invoke-PlayerCliProbe -Name "Player malformed package" `
            -Arguments @("--validate-package", ('"' + $malformedPackage + '"')) `
            -ExpectedExitCode 4 -WorkingDirectory $stageHarness
    }

    $env:TOMCAT_E2E_MANAGED_DIR = $stageManaged
    $env:TOMCAT_E2E_DOTNET_ROOT = $privateDotNetRoot
    $env:TOMCAT_E2E_ISOLATE_DOTNET = "1"
    $env:TOMCAT_E2E_REQUIRE_DETACHED = "1"
	# Candidate validation initializes CoreCLR before the regression deliberately
	# hides global discovery. Select the packaged runtime from the first host call
	# so the process-wide CoreCLR identity remains the same throughout the test.
	$env:DOTNET_ROOT = $privateDotNetRoot
	$env:DOTNET_ROOT_X64 = $privateDotNetRoot
    if ($playerExecutable) {
        $env:TOMCAT_E2E_PLAYER_EXE = $playerExecutable
		$env:TOMCAT_E2E_PLAYER_TEMPLATE = $playerTemplateRoot
        $env:TOMCAT_E2E_REQUIRE_PLAYER = "1"
    }
    else {
        Remove-Item Env:TOMCAT_E2E_PLAYER_EXE -ErrorAction SilentlyContinue
		Remove-Item Env:TOMCAT_E2E_PLAYER_TEMPLATE -ErrorAction SilentlyContinue
        Remove-Item Env:TOMCAT_E2E_REQUIRE_PLAYER -ErrorAction SilentlyContinue
    }

    Push-Location $stageHarness
    try {
        & (Join-Path $stageHarness "ScriptCompilerRegression.exe") --e2e-only
        if ($LASTEXITCODE -ne 0) {
            throw "Detached C#/Player smoke failed with exit code $LASTEXITCODE."
        }
    }
    finally {
        Pop-Location
    }
}
finally {
    foreach ($name in $savedEnvironment.Keys) {
        $saved = $savedEnvironment[$name]
        if ($null -eq $saved) {
            Remove-Item "Env:$name" -ErrorAction SilentlyContinue
        }
        else {
            Set-Item "Env:$name" $saved
        }
    }
    if (Test-Path -LiteralPath $stageRoot) {
        $resolvedStage = [System.IO.Path]::GetFullPath($stageRoot)
        if ($resolvedStage.StartsWith($temporaryBase,
                [System.StringComparison]::OrdinalIgnoreCase) -and
            $resolvedStage -ne $temporaryBase) {
            Remove-Item -LiteralPath $resolvedStage -Recurse -Force
        }
    }
}
