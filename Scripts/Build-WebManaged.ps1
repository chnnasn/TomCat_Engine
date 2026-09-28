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
    if ($LASTEXITCODE -ne 0) {
        throw "$Description failed with exit code $LASTEXITCODE."
    }
}

$dotnet = Get-Command $DotNetPath -ErrorAction SilentlyContinue
if (-not $dotnet) {
    throw "The .NET SDK executable was not found: $DotNetPath"
}
$sdkVersion = (& $dotnet.Source --version 2>&1 | Select-Object -First 1).Trim()
if ($LASTEXITCODE -ne 0 -or $sdkVersion -notmatch '^10\.') {
    throw "TomCat Web requires a .NET 10 SDK; resolved version was '$sdkVersion'."
}
$workloads = @(& $dotnet.Source workload list 2>&1)
if ($LASTEXITCODE -ne 0 -or -not ($workloads -match '(?m)^wasm-tools\s')) {
    throw "The .NET 10 wasm-tools workload is missing. Install it in the engine build environment with 'dotnet workload install wasm-tools'; browser users do not install it."
}

$emcmake = Get-Command 'emcmake' -ErrorAction SilentlyContinue
$cmake = Get-Command 'cmake' -ErrorAction SilentlyContinue
$ninja = Get-Command 'ninja' -ErrorAction SilentlyContinue
if (-not $emcmake -or -not $cmake -or -not $ninja) {
    throw 'Emscripten (emcmake), CMake and Ninja must be active in the engine build environment.'
}

New-Item -ItemType Directory -Path $BuildDirectory -Force | Out-Null
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
Invoke-Checked $emcmake.Source @(
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
$archiveProperty = $archives -join ';'
Invoke-Checked $dotnet.Source @(
    'publish', $project, '-c', $Configuration, '-r', 'browser-wasm',
    '-o', $OutputDirectory, '--nologo', '-nodeReuse:false',
    '-p:UseSharedCompilation=false',
    "-p:TomCatWebNativeArchives=$archiveProperty",
    "-p:TomCatRepositoryRoot=$rootForEmcc"
) '.NET browser-wasm publish'

Write-Host "TomCat managed Web runtime published to $OutputDirectory"
Write-Host 'Deploy the complete directory with COOP: same-origin and COEP: require-corp headers.'
