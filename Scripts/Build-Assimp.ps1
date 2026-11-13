# Builds the Assimp submodule (TomCat/vendor/Assimp) into TomCat/vendor/Assimp-build
# Output: shared DLL + import lib + generated config.h/revision.h
$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot 'ReleaseArtifactTools.ps1')
$root = Split-Path -Parent $PSScriptRoot
$src = Join-Path $root "TomCat\vendor\Assimp"
$build = Join-Path $root "TomCat\vendor\Assimp-build"

if (-not (Test-Path (Join-Path $src "CMakeLists.txt"))) {
    Write-Error "Assimp submodule is not initialized. Run: git submodule update --init"
}

if (-not (Test-Path (Join-Path $build "Assimp.sln"))) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    if (-not (Test-Path -LiteralPath $vswhere)) { throw 'Visual Studio discovery tool vswhere.exe is unavailable.' }
    $instances = & $vswhere -all -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -format json | ConvertFrom-Json
    if ($LASTEXITCODE -ne 0) { throw 'Could not discover Visual Studio C++ installations.' }
    $capabilities = cmake -E capabilities | ConvertFrom-Json
    if ($LASTEXITCODE -ne 0) { throw 'Could not query CMake generators.' }
    $selection = Select-TomCatVisualStudioGenerator -Instances @($instances) -Generators $capabilities.generators
    # Use the same v143 ABI and output names as the engine's Premake projects,
    # including when MSBuild comes from Visual Studio 2026.
    cmake -S $src -B $build -G $selection.Generator -A x64 -T v143 `
        "-DCMAKE_GENERATOR_INSTANCE=$($selection.InstallationPath)" `
        -DBUILD_SHARED_LIBS=ON `
        -DASSIMP_BUILD_TESTS=OFF `
        -DASSIMP_BUILD_SAMPLES=OFF `
        -DASSIMP_BUILD_ASSIMP_TOOLS=OFF `
        -DASSIMP_INSTALL=OFF `
        -DASSIMP_BUILD_ZLIB=ON `
        -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL
    if ($LASTEXITCODE -ne 0) { throw "Assimp configure failed ($LASTEXITCODE)" }
}

cmake --build $build --config Release --target assimp -j 8
if ($LASTEXITCODE -ne 0) { throw "Assimp build failed ($LASTEXITCODE)" }
Write-Host "Assimp built -> $build"
