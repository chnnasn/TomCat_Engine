param(
    [ValidateSet("Debug", "Release", "Dist")]
    [string]$Configuration = "Release",

    [ValidateSet("x64")]
    [string]$Platform = "x64",

    [string]$PremakePath = "",

    [string]$MsBuildPath = ""
)

$ErrorActionPreference = "Stop"
$repositoryRoot = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot "ManagedReleaseTools.ps1")

function Resolve-Premake {
    if ($PremakePath) {
        if (-not (Test-Path -LiteralPath $PremakePath -PathType Leaf)) {
            throw "Premake5 was not found: $PremakePath"
        }
        return (Resolve-Path -LiteralPath $PremakePath).Path
    }
    if ($env:PREMAKE -and (Test-Path -LiteralPath $env:PREMAKE -PathType Leaf)) {
        return (Resolve-Path -LiteralPath $env:PREMAKE).Path
    }
    $repositoryPremake = Join-Path $repositoryRoot "vendor\premake\bin\premake5.exe"
    if (Test-Path -LiteralPath $repositoryPremake -PathType Leaf) {
        return $repositoryPremake
    }
    $command = Get-Command premake5.exe -ErrorAction SilentlyContinue
    if ($command) { return $command.Source }
    throw "Premake5 was not found. Run Scripts\Setup.bat or pass -PremakePath."
}

function Resolve-MSBuild {
    if ($MsBuildPath) {
        if (-not (Test-Path -LiteralPath $MsBuildPath -PathType Leaf)) {
            throw "MSBuild was not found: $MsBuildPath"
        }
        return (Resolve-Path -LiteralPath $MsBuildPath).Path
    }
    $command = Get-Command msbuild.exe -ErrorAction SilentlyContinue
    if ($command) { return $command.Source }
    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path -LiteralPath $vswhere) {
        $resolved = & $vswhere -latest -requires Microsoft.Component.MSBuild `
            -find "MSBuild\**\Bin\MSBuild.exe" | Select-Object -First 1
        if ($resolved) { return $resolved }
    }
    throw "MSBuild was not found. Install Visual Studio with Desktop development with C++."
}

function Invoke-Checked {
    param(
        [Parameter(Mandatory)]
        [string]$Name,

        [Parameter(Mandatory)]
        [scriptblock]$Action
    )

    Write-Host ""
    Write-Host "== $Name =="
    & $Action
    if ($LASTEXITCODE -ne 0) {
        throw "$Name failed (exit $LASTEXITCODE)."
    }
}

function Invoke-NativeRegression {
    param([Parameter(Mandatory)][string]$Name)
    $outputDirectory = "$Configuration-windows-x86_64"
    $executable = Join-Path $repositoryRoot "Tests\bin\$outputDirectory\$Name\$Name.exe"
    if (-not (Test-Path -LiteralPath $executable -PathType Leaf)) {
        throw "$Name executable was not produced: $executable"
    }
    Invoke-Checked -Name $Name -Action { & $executable }
}

Push-Location $repositoryRoot
try {
    Invoke-Checked -Name "Managed Release build" -Action {
        Build-TomCatManagedRelease -RepositoryRoot $repositoryRoot
    }

    $dotnet = Assert-DotNet10Sdk
    Invoke-Checked -Name "TomCat.Managed.Regression" -Action {
        & $dotnet run --project "Managed\TomCat.Managed.Regression\TomCat.Managed.Regression.csproj" `
            --configuration Release --no-build
    }

    $premake = Resolve-Premake
    Invoke-Checked -Name "Generate native regression solution" -Action {
        & $premake "--file=Tests\premake5.lua" vs2022
    }
    Invoke-Checked -Name "Generate independent Player solution" -Action {
        & $premake "--file=Player\premake5.lua" vs2022
    }

    $msbuild = Resolve-MSBuild
    Invoke-Checked -Name "Build native regressions" -Action {
        & $msbuild "Tests\Tests.sln" "-p:Configuration=$Configuration" `
            "-p:Platform=$Platform" -m -v:m -nologo
    }
    Invoke-Checked -Name "Build independent Player" -Action {
        & $msbuild "Player\Player.sln" "-p:Configuration=$Configuration" `
            "-p:Platform=$Platform" -m -v:m -nologo
    }
    $templateBuilder = Join-Path $PSScriptRoot "Build-PlayerTemplate.ps1"
    if (-not (Test-Path -LiteralPath $templateBuilder -PathType Leaf)) {
        throw "Player Template builder was not found: $templateBuilder"
    }
    $templateOutput = Join-Path $repositoryRoot "Tests\bin\$Configuration-windows-x86_64\PlayerTemplateRegression"
    Invoke-Checked -Name "Generate and validate Player Template" -Action {
        & $templateBuilder -Configuration $Configuration -Destination $templateOutput
    }

    Invoke-NativeRegression -Name "PhysicsRegression"
    Invoke-NativeRegression -Name "SpriteAssetRegression"
    Invoke-NativeRegression -Name "ScriptCompilerRegression"

    $playerSmoke = Join-Path $PSScriptRoot "Run-CSharpPlayerSmoke.ps1"
    if (-not (Test-Path -LiteralPath $playerSmoke -PathType Leaf)) {
        throw "Player cook/start smoke runner was not found: $playerSmoke"
    }
    Invoke-Checked -Name "Player cook/start smoke" -Action {
        & $playerSmoke -Configuration $Configuration -RequirePlayer
    }

    Write-Host ""
    Write-Host "All TomCat regressions passed."
}
finally {
    Pop-Location
}
