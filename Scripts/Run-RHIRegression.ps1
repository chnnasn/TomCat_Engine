param(
    [ValidateSet('opengl', 'vulkan')][string]$Backend = 'vulkan',
    [ValidateSet('Debug', 'Release', 'Dist')][string]$Configuration = 'Release',
    [switch]$Validation,
    [switch]$MultiViewport,
    [string]$Device = '',
    [string]$MsBuildPath = ''
)
$ErrorActionPreference = 'Stop'
$repositoryRoot = Split-Path -Parent $PSScriptRoot
if (-not $MsBuildPath) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    $MsBuildPath = & $vswhere -latest -requires Microsoft.Component.MSBuild -find 'MSBuild/**/Bin/MSBuild.exe' | Select-Object -First 1
}
if (-not $MsBuildPath -or -not (Test-Path -LiteralPath $MsBuildPath)) { throw 'MSBuild is unavailable' }
$savedEnvironment = @{}
foreach ($name in @('TC_RENDERER', 'TC_VULKAN_VALIDATION', 'TC_VULKAN_DEVICE', 'TC_RHI_TEST_VIEWPORTS', 'TC_IMGUI_VIEWPORTS', 'PATH')) {
    $savedEnvironment[$name] = [Environment]::GetEnvironmentVariable($name, 'Process')
}
Push-Location $repositoryRoot
try {
    & ./vendor/premake/bin/premake5.exe --file=Tests/premake5.lua vs2022
    if ($LASTEXITCODE -ne 0) { throw 'Premake failed' }
    & $MsBuildPath Tests/RHIRegression/RHIRegression.vcxproj /m:4 "/p:Configuration=$Configuration" /p:Platform=x64 /v:minimal /nologo
    if ($LASTEXITCODE -ne 0) { throw 'RHI regression build failed' }
    $env:TC_RENDERER = $Backend
    $env:TC_VULKAN_VALIDATION = if ($Validation -and $Backend -eq 'vulkan') { '1' } else { $null }
    $env:TC_VULKAN_DEVICE = $Device
    $env:TC_RHI_TEST_VIEWPORTS = if ($MultiViewport) { '1' } else { $null }
    $env:TC_IMGUI_VIEWPORTS = $null
    $env:PATH = (Join-Path $repositoryRoot 'vendor/VulkanSDK/Bin') + ';' + $env:PATH
    & "Tests/bin/$Configuration-windows-x86_64/RHIRegression/RHIRegression.exe"
    if ($LASTEXITCODE -ne 0) { throw "RHI regression failed ($Backend, exit $LASTEXITCODE)" }
}
finally {
    foreach ($name in $savedEnvironment.Keys) { [Environment]::SetEnvironmentVariable($name, $savedEnvironment[$name], 'Process') }
    Pop-Location
}
