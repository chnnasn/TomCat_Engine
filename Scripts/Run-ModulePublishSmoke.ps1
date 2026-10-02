param([ValidateSet('Debug','Release','Dist')][string]$Configuration = 'Release')
$ErrorActionPreference = 'Stop'
$repositoryRoot = Split-Path -Parent $PSScriptRoot
$outputName = "$Configuration-windows-x86_64"
$cli = Join-Path $repositoryRoot "Tools/bin/$outputName/TomCatCLI/TomCatCLI.exe"
$dll = Join-Path $repositoryRoot "Tests/bin/$outputName/TestModule/TestModule.dll"
$template = Join-Path $repositoryRoot "Tests/bin/$outputName/CoinRunnerTemplate"
$root = Join-Path $repositoryRoot ('build/ModulePublishSmoke-' + [Guid]::NewGuid().ToString('N'))
$project = Join-Path $root 'Authoring'
$previousHeadless = $env:TOMCAT_E2E_PLAYER_HEADLESS
$company = 'TomCatModuleSmoke-' + [Guid]::NewGuid().ToString('N')
& (Join-Path $PSScriptRoot 'Build-PlayerTemplate.ps1') -Configuration $Configuration -Destination $template
if ($LASTEXITCODE -ne 0) { throw 'Module Player template staging failed.' }
try {
    New-Item -ItemType Directory -Path $project -Force | Out-Null
    foreach ($item in @('Assets','ProjectSettings','Project.tcproj')) {
        Copy-Item -LiteralPath (Join-Path $repositoryRoot "Samples/CoinRunner/$item") -Destination $project -Recurse
    }
    $settingsPath = Join-Path $project 'ProjectSettings/PlayerSettings.json'
    $settings = Get-Content -LiteralPath $settingsPath -Raw | ConvertFrom-Json
    $settings.companyName = $company
    [IO.File]::WriteAllText($settingsPath, ($settings | ConvertTo-Json -Depth 10))
    $module = Join-Path $project 'Modules/TestWeather'
    New-Item -ItemType Directory -Path "$module/lib" -Force | Out-Null
    Copy-Item -LiteralPath $dll -Destination "$module/lib/TestModule.dll"
    $manifest = "ModuleVersion: 1`nName: TestWeather`nDisplayName: Test Weather`nVersion: 1.0.0`nLibrary: lib/TestModule.dll`nRuntime: true`n"
    [IO.File]::WriteAllText("$module/module.tomcat", $manifest)
    $scenePath = Join-Path $project 'Assets/Scene/level.tomcat'
    $scene = [IO.File]::ReadAllText($scenePath)
    $type = [Convert]::ToUInt64('7E57100000000001', 16)
    $wind = [Convert]::ToUInt64('7E57100000000002', 16)
    $gust = [Convert]::ToUInt64('7E57100000000003', 16)
    $component = @"
      - TypeId: $type
        StableName: TestModule.Weather
        SchemaVersion: 1
        Properties:
          - PropertyId: $wind
            StableName: WindSpeed
            Value: 2.5
          - PropertyId: $gust
            StableName: GustLevel
            Value: 3
"@
    $parent = $scene.IndexOf('    Parent: 0')
    if ($parent -lt 0) { throw 'Fixture scene has no target entity.' }
    $scene = $scene.Insert($parent, $component + "`n")
    [IO.File]::WriteAllText($scenePath, $scene)
    & $cli build --project "$project/Project.tcproj" --template $template
    if ($LASTEXITCODE -ne 0) { throw 'Module project build failed.' }
    $build = Join-Path $project 'Build/CoinRunner'
    $detached = Join-Path $root 'Detached'
    Copy-Item -LiteralPath $build -Destination $detached -Recurse
    $package = Join-Path $build 'Game.tcpak'
    $before = (Get-FileHash -LiteralPath $package -Algorithm SHA256).Hash
    [IO.File]::WriteAllText("$module/module.tomcat", $manifest + "Dependencies: [Absent]`n")
    & $cli cook --project "$project/Project.tcproj" --output $package
    if ($LASTEXITCODE -eq 0) { throw 'Missing module dependency was accepted.' }
    if ((Get-FileHash -LiteralPath $package -Algorithm SHA256).Hash -ne $before) {
        throw 'Failed cook modified the previous package.'
    }
    $bytes = [IO.File]::ReadAllBytes((Join-Path $detached 'Game.tcpak'))
    $header = [BitConverter]::ToUInt32($bytes, 12)
    $count = [BitConverter]::ToUInt64($bytes, 16)
    $moduleOffset = $null
    for ($index = 0; $index -lt $count; ++$index) {
        $position = [int]($header + $index * 64)
        if ([BitConverter]::ToUInt64($bytes, $position) -eq ([UInt64]::MaxValue - [UInt64]1)) {
            $moduleOffset = [BitConverter]::ToUInt64($bytes, $position + 16)
            break
        }
    }
    if ($null -eq $moduleOffset) { throw 'Published package has no native module entry.' }
    $bytes[[int]$moduleOffset] = $bytes[[int]$moduleOffset] -bxor 1
    $corrupt = Join-Path $detached 'Corrupt.tcpak'
    [IO.File]::WriteAllBytes($corrupt, $bytes)
    & "$detached/CoinRunner.exe" --validate-package $corrupt
    if ($LASTEXITCODE -eq 0) { throw 'Corrupt native module payload was accepted.' }
    Remove-Item -LiteralPath $corrupt
    Move-Item -LiteralPath $project -Destination (Join-Path $root 'HiddenAuthoring')
    $env:TOMCAT_E2E_PLAYER_HEADLESS = '1'
    $log = Join-Path $root 'player.log'
    $process = Start-Process -FilePath "$detached/CoinRunner.exe" -WorkingDirectory $detached -WindowStyle Hidden -PassThru -RedirectStandardOutput $log -RedirectStandardError "$log.err"
    $null = $process.Handle
    if (-not $process.WaitForExit(60000)) { $process.Kill(); $process.WaitForExit(); throw 'Module Player timed out.' }
    $process.WaitForExit()
    $text = Get-Content -LiteralPath $log -Raw
    if ($process.ExitCode -ne 0 -or $text -notmatch 'TestModule loaded' -or $text -notmatch 'MODULE_TYPED WindSpeed=2.500000' -or $text -notmatch 'MODULE_TYPED GustLevel=3' -or $text -notmatch 'WIN: all 4 coins collected') {
        Write-Host $text
        throw 'Detached module Player did not complete the game.'
    }
    $global:LASTEXITCODE = 0
    Write-Host 'PASS module publishing: CLI build, embedded DLL, detached Player, missing dependency and atomic failure.'
}
finally {
    if ($null -eq $previousHeadless) { Remove-Item Env:TOMCAT_E2E_PLAYER_HEADLESS -ErrorAction SilentlyContinue }
    else { $env:TOMCAT_E2E_PLAYER_HEADLESS = $previousHeadless }
    # All temporary files are beneath the explicitly allocated workspace root.
    $resolvedRoot = [IO.Path]::GetFullPath($root)
    $buildRoot = [IO.Path]::GetFullPath((Join-Path $repositoryRoot 'build')) + [IO.Path]::DirectorySeparatorChar
    if ($resolvedRoot.StartsWith($buildRoot, [StringComparison]::OrdinalIgnoreCase) -and (Test-Path -LiteralPath $resolvedRoot)) {
        Remove-Item -LiteralPath $resolvedRoot -Recurse -Force
    }
}
