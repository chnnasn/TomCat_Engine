param(
    [ValidateSet("Debug", "Release", "Dist")]
    [string]$Configuration = "Release"
)

# Complete-game verification for the CoinRunner sample: cook the project with
# TomCatCLI, run the packaged game headless in TomCatPlayer, and verify the
# engine save system across three runs (fresh start, save reload, corruption
# recovery from the rotated backup).

$ErrorActionPreference = "Stop"
$repositoryRoot = Split-Path -Parent $PSScriptRoot
$outputName = "$Configuration-windows-x86_64"

$cliExecutable = Join-Path $repositoryRoot "Tools\bin\$outputName\TomCatCLI\TomCatCLI.exe"
if (-not (Test-Path -LiteralPath $cliExecutable -PathType Leaf)) {
    throw "TomCatCLI was not built: $cliExecutable"
}
# Rebuild the strict template from current native/Managed build outputs and
# copy the resolved .NET 10 host/runtime. Never depend on development-output
# DLL copies or a manually created dotnet junction.
$templateRoot = Join-Path $repositoryRoot "Tests\bin\$outputName\CoinRunnerTemplate"
& (Join-Path $PSScriptRoot "Build-PlayerTemplate.ps1") `
    -Configuration $Configuration -Destination $templateRoot
if ($LASTEXITCODE -ne 0) { throw "CoinRunner Player template staging failed." }
$playerExecutable = Join-Path $templateRoot "TomCatPlayer.exe"

$projectPath = Join-Path $repositoryRoot "Samples\CoinRunner\Project.tcproj"
$packagePath = Join-Path $repositoryRoot "Samples\CoinRunner\Build\Game.tcpak"

Write-Host "== Cooking CoinRunner =="
& $cliExecutable cook --project $projectPath --output $packagePath --migrate
if ($LASTEXITCODE -ne 0) {
    throw "Cook failed with exit $LASTEXITCODE."
}
if (-not (Test-Path -LiteralPath $packagePath -PathType Leaf)) {
    throw "Cook did not produce $packagePath."
}

$savesDirectory = Join-Path $env:LOCALAPPDATA "TomCat\Games\TomCatSamples\CoinRunner\Saves"
$saveSlot = Join-Path $savesDirectory "progress.tcsav"
# Preserve pre-existing sample progress and touch only the two test slot files.
$savedSlots = @{}
foreach ($slotPath in @($saveSlot, "$saveSlot.bak")) {
    if (Test-Path -LiteralPath $slotPath -PathType Leaf) {
        $savedSlots[$slotPath] = [System.IO.File]::ReadAllBytes($slotPath)
    }
}

function Invoke-HeadlessRun {
    param([string]$LogPath)
    $previousHeadless = $env:TOMCAT_E2E_PLAYER_HEADLESS
    $env:TOMCAT_E2E_PLAYER_HEADLESS = "1"
    try {
        $process = Start-Process -FilePath $playerExecutable `
            -ArgumentList @("--package", ('"' + $packagePath + '"')) `
            -WorkingDirectory $repositoryRoot -WindowStyle Hidden -PassThru `
            -RedirectStandardOutput $LogPath -RedirectStandardError "$LogPath.err"
        # Windows PowerShell 5.1 needs the handle retained before the process
        # exits, otherwise Start-Process -PassThru may expose a null ExitCode.
        $null = $process.Handle
        if (-not $process.WaitForExit(60000)) {
            $process.Kill()
            $process.WaitForExit()
            throw "Headless run exceeded the 60-second timeout."
        }
        # The timed wait succeeded; this drains redirected output without
        # extending the process timeout.
        $process.WaitForExit()
    }
    finally {
        if ($null -ne $previousHeadless) {
            $env:TOMCAT_E2E_PLAYER_HEADLESS = $previousHeadless
        }
        else {
            Remove-Item Env:\TOMCAT_E2E_PLAYER_HEADLESS -ErrorAction SilentlyContinue
        }
    }
    if ($process.ExitCode -ne 0) {
        Get-Content -LiteralPath $LogPath | Select-Object -Last 30 | Write-Host
        throw "Headless run returned exit $($process.ExitCode); expected 0."
    }
    $runLog = Get-Content -LiteralPath $LogPath -Raw
    if ($runLog -notmatch "WIN: all 4 coins collected" -or
        $runLog -match "save failed:") {
        throw "Headless run did not complete the win/save loop: $LogPath"
    }
}

try {
    foreach ($slotPath in @($saveSlot, "$saveSlot.bak")) {
        if (Test-Path -LiteralPath $slotPath -PathType Leaf) {
            Remove-Item -LiteralPath $slotPath -Force
        }
    }
    Write-Host "== Run 1: fresh start, collect all coins, save =="
    Invoke-HeadlessRun -LogPath (Join-Path $env:TEMP "coinrunner-run1.log")
    if (-not (Test-Path -LiteralPath $saveSlot -PathType Leaf)) {
        throw "Run 1 did not create the save slot: $saveSlot"
    }

    Write-Host "== Run 2: reload the save (high score must be restored) =="
    Invoke-HeadlessRun -LogPath (Join-Path $env:TEMP "coinrunner-run2.log")
    $run2Log = Get-Content -LiteralPath (Join-Path $env:TEMP "coinrunner-run2.log") -Raw
    if ($run2Log -notmatch "high score 4, runs") {
        throw "Run 2 did not restore the persisted high score from the save slot."
    }

    Write-Host "== Run 3: corrupted primary save must recover from the backup =="
    $bytes = [System.IO.File]::ReadAllBytes($saveSlot)
    $bytes[$bytes.Length - 40] = $bytes[$bytes.Length - 40] -bxor 0xFF
    [System.IO.File]::WriteAllBytes($saveSlot, $bytes)
    Invoke-HeadlessRun -LogPath (Join-Path $env:TEMP "coinrunner-run3.log")
    $run3Log = Get-Content -LiteralPath (Join-Path $env:TEMP "coinrunner-run3.log") -Raw
    if ($run3Log -notmatch "high score 4, runs") {
        throw "Run 3 did not recover the save from the rotated backup."
    }

    Write-Host "PASS CoinRunner complete-game smoke: cook, headless packaged run, "
    Write-Host "scoring, save persistence, save reload and corruption recovery."
}
finally {
    foreach ($slotPath in @($saveSlot, "$saveSlot.bak")) {
        if ($savedSlots.ContainsKey($slotPath)) {
            [System.IO.File]::WriteAllBytes($slotPath, $savedSlots[$slotPath])
        }
        elseif (Test-Path -LiteralPath $slotPath -PathType Leaf) {
            Remove-Item -LiteralPath $slotPath -Force
        }
    }
}
