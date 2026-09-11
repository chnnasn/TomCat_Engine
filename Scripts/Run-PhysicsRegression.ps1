param(
    [ValidateSet("Debug", "Release", "Dist")]
    [string]$Configuration = "Release",
    [ValidateSet("x64")]
    [string]$Platform = "x64"
)

$ErrorActionPreference = "Stop"
$repositoryRoot = Split-Path -Parent $PSScriptRoot
$premake = Join-Path $repositoryRoot "vendor\premake\bin\premake5.exe"
if (-not (Test-Path -LiteralPath $premake)) {
    throw "premake5 was not found. Run Scripts\Setup.bat first."
}

Push-Location $repositoryRoot
try {
    & $premake "--file=Tests\premake5.lua" vs2022
    if ($LASTEXITCODE -ne 0) {
        throw "Could not generate Tests\Tests.sln."
    }

    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    $msbuild = $null
    if (Test-Path -LiteralPath $vswhere) {
        $msbuild = & $vswhere -latest -requires Microsoft.Component.MSBuild `
            -find "MSBuild\**\Bin\MSBuild.exe" | Select-Object -First 1
    }
    if (-not $msbuild) {
        $msbuildCommand = Get-Command msbuild.exe -ErrorAction SilentlyContinue
        if ($msbuildCommand) {
            $msbuild = $msbuildCommand.Source
        }
    }
    if (-not $msbuild) {
        throw "MSBuild was not found. Install Visual Studio with Desktop development with C++."
    }

    & $msbuild "Tests\Tests.sln" "-p:Configuration=$Configuration" `
        "-p:Platform=$Platform" -m -nologo
    if ($LASTEXITCODE -ne 0) {
        throw "Regression test build failed."
    }

    $outputDirectory = "$Configuration-windows-x86_64"
    $regressions = @("PhysicsRegression", "SpriteAssetRegression")
    foreach ($regression in $regressions) {
        $testExecutable = Join-Path $repositoryRoot `
            "Tests\bin\$outputDirectory\$regression\$regression.exe"
        if (-not (Test-Path -LiteralPath $testExecutable)) {
            throw "$regression executable was not produced: $testExecutable"
        }

        & $testExecutable
        if ($LASTEXITCODE -ne 0) {
            throw "$regression reported one or more failures."
        }
    }
}
finally {
    Pop-Location
}
