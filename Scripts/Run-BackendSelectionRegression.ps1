param(
    [ValidateSet('Debug', 'Release', 'Dist')][string]$Configuration = 'Release',
    [ValidateSet('opengl', 'vulkan')][string]$ExpectedAutoBackend = '',
    [string]$MsBuildPath = '',
    [switch]$SkipBuild
)
$ErrorActionPreference = 'Stop'
$repositoryRoot = Split-Path -Parent $PSScriptRoot
$executable = Join-Path $repositoryRoot "Tests/bin/$Configuration-windows-x86_64/RHIRegression/RHIRegression.exe"
$logDirectory = Join-Path $repositoryRoot ('build/logs/backend-selection-' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
$savedEnvironment = @{}
foreach ($name in @('TC_RENDERER', 'TC_VULKAN_VALIDATION', 'TC_VULKAN_DEVICE', 'TC_RHI_TEST_VIEWPORTS', 'TC_IMGUI_VIEWPORTS', 'PATH')) {
    $savedEnvironment[$name] = [Environment]::GetEnvironmentVariable($name, 'Process')
}

function Invoke-BackendCase {
    param(
        [Parameter(Mandatory)][string]$Name,
        [AllowEmptyString()][string]$Backend = '',
        [string]$Device = '',
        [string]$Expected = '',
        [ValidateSet('selection', 'initialization', 'full')][string]$Mode = 'selection',
        [switch]$SimulateLateVulkanUnavailable,
        [string]$ExpectedError = ''
    )
    $env:TC_RENDERER = if ($Backend) { $Backend } else { $null }
    $env:TC_VULKAN_DEVICE = if ($Device) { $Device } else { $null }
    $arguments = @()
    if ($Mode -eq 'selection') { $arguments += '--backend-selection-only' }
    elseif ($Mode -eq 'initialization') { $arguments += '--backend-initialization-only' }
    if ($Expected) { $arguments += @('--expect-backend', $Expected) }
    if ($SimulateLateVulkanUnavailable) { $arguments += '--simulate-late-vulkan-unavailable' }
    $stdoutPath = Join-Path $logDirectory "$Name.stdout.log"
    $stderrPath = Join-Path $logDirectory "$Name.stderr.log"
    $startArguments = @{
        FilePath = $executable
        WorkingDirectory = $repositoryRoot
        WindowStyle = 'Hidden'
        Wait = $true
        PassThru = $true
        RedirectStandardOutput = $stdoutPath
        RedirectStandardError = $stderrPath
    }
    if ($arguments.Count) { $startArguments.ArgumentList = $arguments }
    $process = Start-Process @startArguments
    $output = (Get-Content -LiteralPath $stdoutPath -Raw) + (Get-Content -LiteralPath $stderrPath -Raw)
    if ($ExpectedError) {
        if ($process.ExitCode -eq 0 -or $output -notmatch $ExpectedError) {
            throw "$Name did not report its expected failure (exit $($process.ExitCode)). See $logDirectory"
        }
        Write-Host "PASS $Name (expected failure)"
        return
    }
    if ($process.ExitCode -ne 0) { throw "$Name failed (exit $($process.ExitCode)). See $logDirectory" }
    if ($output -notmatch 'Selected backend: (opengl|vulkan)') { throw "$Name did not report its backend. See $logDirectory" }
    $selected = $Matches[1]
    Write-Host "PASS $Name ($selected)"
    return $selected
}

Push-Location $repositoryRoot
try {
    if (-not $SkipBuild) {
        if (-not $MsBuildPath) {
            $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
            $MsBuildPath = & $vswhere -latest -requires Microsoft.Component.MSBuild -find 'MSBuild/**/Bin/MSBuild.exe' | Select-Object -First 1
        }
        if (-not $MsBuildPath -or -not (Test-Path -LiteralPath $MsBuildPath)) { throw 'MSBuild is unavailable' }
        & ./vendor/premake/bin/premake5.exe --file=Tests/premake5.lua vs2022
        if ($LASTEXITCODE -ne 0) { throw 'Premake failed' }
        & $MsBuildPath Tests/RHIRegression/RHIRegression.vcxproj /m:4 "/p:Configuration=$Configuration" /p:Platform=x64 /v:minimal /nologo
        if ($LASTEXITCODE -ne 0) { throw 'RHI regression build failed' }
    }
    if (-not (Test-Path -LiteralPath $executable -PathType Leaf)) { throw "Missing RHI regression executable: $executable" }
    New-Item -ItemType Directory -Force -Path $logDirectory | Out-Null
    $env:TC_VULKAN_VALIDATION = $null
    $env:TC_RHI_TEST_VIEWPORTS = $null
    $env:TC_IMGUI_VIEWPORTS = $null
    $env:PATH = (Join-Path $repositoryRoot 'vendor/VulkanSDK/Bin') + ';' + $env:PATH

    # Detect this machine's capabilities, or assert a known CI hardware contract.
    $automatic = Invoke-BackendCase -Name 'default-headless' -Expected $ExpectedAutoBackend
    $null = Invoke-BackendCase -Name 'auto-headless' -Backend 'auto' -Expected $automatic
    $null = Invoke-BackendCase -Name 'opengl-headless' -Backend 'opengl' -Expected 'opengl'
    $missingDevice = 'TomCat-regression-no-device-' + [guid]::NewGuid().ToString('N')
    $null = Invoke-BackendCase -Name 'default-fallback-headless' -Device $missingDevice -Expected 'opengl'
    $null = Invoke-BackendCase -Name 'auto-fallback-headless' -Backend 'auto' -Device $missingDevice -Expected 'opengl'
    $null = Invoke-BackendCase -Name 'forced-vulkan-unsupported' -Backend 'vulkan' -Device $missingDevice -ExpectedError 'Vulkan'
    $null = Invoke-BackendCase -Name 'invalid-backend' -Backend 'invalid' -ExpectedError 'TC_RENDERER'

    # These also exercise real context creation and teardown. The fallback case
    # executes the complete RHI drawing/readback suite, rather than just choosing GL.
    $initialized = Invoke-BackendCase -Name 'default-initialization' -Mode 'initialization' -Expected $ExpectedAutoBackend
    $null = Invoke-BackendCase -Name 'auto-initialization' -Backend 'auto' -Mode 'initialization' -Expected $initialized
    $null = Invoke-BackendCase -Name 'forced-opengl-initialization' -Backend 'opengl' -Mode 'initialization' -Expected 'opengl'
    $null = Invoke-BackendCase -Name 'automatic-fallback-rhi' -Device $missingDevice -Mode 'full' -Expected 'opengl'
    if ($automatic -eq 'vulkan') {
        $null = Invoke-BackendCase -Name 'forced-vulkan-headless' -Backend 'vulkan' -Expected 'vulkan'
        $null = Invoke-BackendCase -Name 'late-vulkan-fallback' -Mode 'initialization' -SimulateLateVulkanUnavailable -Expected 'opengl'
        $null = Invoke-BackendCase -Name 'forced-late-vulkan-failure' -Backend 'vulkan' -Mode 'initialization' -SimulateLateVulkanUnavailable -ExpectedError 'Vulkan'
        if ($initialized -eq 'vulkan') {
            $null = Invoke-BackendCase -Name 'forced-vulkan-initialization' -Backend 'vulkan' -Mode 'initialization' -Expected 'vulkan'
        }
        else {
            $null = Invoke-BackendCase -Name 'forced-vulkan-unsupported-surface' -Backend 'vulkan' -Mode 'initialization' -ExpectedError 'Vulkan'
        }
    }
    Write-Host "Backend selection regressions passed. Logs: $logDirectory"
}
finally {
    foreach ($name in $savedEnvironment.Keys) { [Environment]::SetEnvironmentVariable($name, $savedEnvironment[$name], 'Process') }
    Pop-Location
}
