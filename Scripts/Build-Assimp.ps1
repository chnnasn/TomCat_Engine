# Builds the Assimp submodule (TomCat/vendor/Assimp) into TomCat/vendor/Assimp-build
# Output: shared DLL + import lib + generated config.h/revision.h
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$src = Join-Path $root "TomCat\vendor\Assimp"
$build = Join-Path $root "TomCat\vendor\Assimp-build"

if (-not (Test-Path (Join-Path $src "CMakeLists.txt"))) {
    Write-Error "Assimp submodule is not initialized. Run: git submodule update --init"
}

if (-not (Test-Path (Join-Path $build "CMakeCache.txt"))) {
    cmake -S $src -B $build -G "Visual Studio 17 2022" -A x64 `
        -DBUILD_SHARED_LIBS=ON `
        -DASSIMP_BUILD_TESTS=OFF `
        -DASSIMP_BUILD_SAMPLES=OFF `
        -DASSIMP_BUILD_ASSIMP_TOOLS=OFF `
        -DASSIMP_INSTALL=OFF `
        -DASSIMP_BUILD_ZLIB=ON `
        -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL
}

cmake --build $build --config Release --target assimp -j 8
Write-Host "Assimp built -> $build"
