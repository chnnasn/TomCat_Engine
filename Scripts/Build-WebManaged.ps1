param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release',

    [string]$BuildDirectory = '',

    [string]$OutputDirectory = '',

    [string]$DotNetPath = 'dotnet'
)

$ErrorActionPreference = 'Stop'
$repositoryRoot = [System.IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot))
if (-not $BuildDirectory) {
    $BuildDirectory = Join-Path $repositoryRoot 'build\web-managed-native'
}
if (-not $OutputDirectory) {
    $OutputDirectory = Join-Path $repositoryRoot 'build\web-managed'
}
$BuildDirectory = [System.IO.Path]::GetFullPath($BuildDirectory)
$OutputDirectory = [System.IO.Path]::GetFullPath($OutputDirectory)

function Invoke-Checked {
    param(
        [Parameter(Mandatory)] [string]$Executable,
        [Parameter(Mandatory)] [string[]]$Arguments,
        [Parameter(Mandatory)] [string]$Description
    )
    & $Executable @Arguments
    $commandSucceeded = $?
    $nativeExitCode = $LASTEXITCODE
    if (-not $commandSucceeded -or ($null -ne $nativeExitCode -and $nativeExitCode -ne 0)) {
        throw "$Description failed with exit code $nativeExitCode."
    }
}

$dotnet = Get-Command $DotNetPath -ErrorAction SilentlyContinue
if (-not $dotnet) {
    throw "The .NET SDK executable was not found: $DotNetPath"
}
$sdkVersionOutput = @(& $dotnet.Source --version 2>&1)
$sdkVersionSucceeded = $?
$sdkVersionExitCode = $LASTEXITCODE
$sdkVersion = ($sdkVersionOutput | Select-Object -First 1).Trim()
if (-not $sdkVersionSucceeded -or
    ($null -ne $sdkVersionExitCode -and $sdkVersionExitCode -ne 0) -or
    $sdkVersion -notmatch '^10\.') {
    throw "TomCat Web requires a .NET 10 SDK; resolved version was '$sdkVersion'."
}
$workloads = @(& $dotnet.Source workload list 2>&1)
$workloadListSucceeded = $?
$workloadListExitCode = $LASTEXITCODE
if (-not $workloadListSucceeded -or
    ($null -ne $workloadListExitCode -and $workloadListExitCode -ne 0) -or
    -not ($workloads -match '(?m)^wasm-tools\s')) {
    throw "The .NET 10 wasm-tools workload is missing. Install it in the engine build environment with 'dotnet workload install wasm-tools'; browser users do not install it."
}

$cmake = Get-Command 'cmake' -ErrorAction SilentlyContinue
$ninja = Get-Command 'ninja' -ErrorAction SilentlyContinue
if (-not $cmake) {
    throw 'CMake must be available in the engine build environment.'
}
if (-not $ninja) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path -LiteralPath $vswhere -PathType Leaf) {
        $installation = (& $vswhere -latest -products * -property installationPath |
            Select-Object -First 1).Trim()
        $bundledNinja = Join-Path $installation 'Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe'
        if (Test-Path -LiteralPath $bundledNinja -PathType Leaf) {
            $env:PATH = (Split-Path -Parent $bundledNinja) + ';' + $env:PATH
            $ninja = Get-Command 'ninja' -ErrorAction SilentlyContinue
        }
    }
}
if (-not $ninja) {
    throw 'Ninja was not found on PATH or in the latest Visual Studio installation.'
}

$emcmake = Get-Command 'emcmake' -ErrorAction SilentlyContinue
$emcmakeExecutable = if ($emcmake) { $emcmake.Source } else { '' }
if (-not $emcmake) {
    $dotnetRoot = Split-Path -Parent $dotnet.Source
    $sdkPackRoot = Join-Path $dotnetRoot 'packs\Microsoft.NET.Runtime.Emscripten.*.Sdk.win-x64'
    $sdkPack = Get-ChildItem -Path $sdkPackRoot -Directory -ErrorAction SilentlyContinue |
        Sort-Object Name -Descending | Select-Object -First 1
    if (-not $sdkPack) {
        throw 'The wasm-tools workload did not expose an Emscripten SDK pack.'
    }
    $sdkVersionRoot = Get-ChildItem -LiteralPath $sdkPack.FullName -Directory |
        Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1
    $tools = Join-Path $sdkVersionRoot.FullName 'tools'
    $nodePack = Get-ChildItem -Path (Join-Path $dotnetRoot 'packs\Microsoft.NET.Runtime.Emscripten.*.Node.win-x64') -Directory |
        Sort-Object Name -Descending | Select-Object -First 1
    $pythonPack = Get-ChildItem -Path (Join-Path $dotnetRoot 'packs\Microsoft.NET.Runtime.Emscripten.*.Python.win-x64') -Directory |
        Sort-Object Name -Descending | Select-Object -First 1
    $cachePack = Get-ChildItem -Path (Join-Path $dotnetRoot 'packs\Microsoft.NET.Runtime.Emscripten.*.Cache.win-x64') -Directory |
        Sort-Object Name -Descending | Select-Object -First 1
    $nodeVersion = Get-ChildItem -LiteralPath $nodePack.FullName -Directory |
        Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1
    $pythonVersion = Get-ChildItem -LiteralPath $pythonPack.FullName -Directory |
        Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1
    $cacheVersion = Get-ChildItem -LiteralPath $cachePack.FullName -Directory |
        Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1
    $env:EMSDK_PATH = $tools.TrimEnd('\') + '\'
    $env:EMSDK_PYTHON = Join-Path $pythonVersion.FullName 'tools\python.exe'
    $env:DOTNET_EMSCRIPTEN_LLVM_ROOT = Join-Path $tools 'bin'
    $env:DOTNET_EMSCRIPTEN_NODE_JS = Join-Path $nodeVersion.FullName 'tools\bin\node.exe'
    $env:DOTNET_EMSCRIPTEN_BINARYEN_ROOT = $tools
    $env:EM_CACHE = Join-Path $cacheVersion.FullName 'tools\emscripten\cache'
    $env:FROZEN_CACHE = 'true'
    $emcmakePath = Join-Path $tools 'emscripten\emcmake.bat'
    if (-not (Test-Path -LiteralPath $emcmakePath -PathType Leaf)) {
        throw "The wasm-tools Emscripten launcher is missing: $emcmakePath"
    }
    $emcmakeExecutable = $emcmakePath
}

New-Item -ItemType Directory -Path $BuildDirectory -Force | Out-Null
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
Invoke-Checked $emcmakeExecutable @(
    $cmake.Source, '-S', (Join-Path $repositoryRoot 'Web'), '-B', $BuildDirectory,
    '-G', 'Ninja', "-DCMAKE_BUILD_TYPE=$Configuration"
) 'Emscripten CMake configure'
Invoke-Checked $cmake.Source @(
    '--build', $BuildDirectory, '--target', 'tomcat_managed_web_entrypoints',
    '--config', $Configuration, '--parallel'
) 'TomCat Web native archive build'

$requiredArchives = @(
    'libtomcat_managed_web_entrypoints.a',
    'libtc_player_core.a',
    'libtc_yaml.a',
    'libbox2d.a'
)
$archives = New-Object 'System.Collections.Generic.List[string]'
foreach ($archiveName in $requiredArchives) {
    $matches = @(Get-ChildItem -LiteralPath $BuildDirectory -Recurse -File -Filter $archiveName)
    if ($matches.Count -ne 1) {
        throw "Expected exactly one $archiveName below $BuildDirectory; found $($matches.Count)."
    }
    [void]$archives.Add($matches[0].FullName.Replace('\', '/'))
}

$project = Join-Path $repositoryRoot 'Managed\TomCat.WebHost\TomCat.WebHost.csproj'
$rootForEmcc = $repositoryRoot.Replace('\', '/')
$outputForMsbuild = $OutputDirectory.Replace('\', '/')
Invoke-Checked $dotnet.Source @(
    'publish', $project, '-c', $Configuration, '-r', 'browser-wasm',
    '--nologo', '-nodeReuse:false',
    '-p:UseSharedCompilation=false',
    '-p:NuGetAudit=false',
    "-p:WasmAppDir=$outputForMsbuild",
    "-p:TomCatWebEntrypointsArchive=$($archives[0])",
    "-p:TomCatWebPlayerCoreArchive=$($archives[1])",
    "-p:TomCatWebYamlArchive=$($archives[2])",
    "-p:TomCatWebBox2dArchive=$($archives[3])",
    "-p:TomCatRepositoryRoot=$rootForEmcc"
) '.NET browser-wasm publish'

$frameworkDirectory = Join-Path $OutputDirectory '_framework'
if (-not (Test-Path -LiteralPath $frameworkDirectory -PathType Container)) {
    throw "The browser framework output directory was not generated: $frameworkDirectory"
}

Write-Host "TomCat managed Web runtime published to $OutputDirectory"
Write-Host 'Deploy the complete directory with COOP: same-origin and COEP: require-corp headers.'
