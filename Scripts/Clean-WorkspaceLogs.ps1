# Deletes stray *.log files (including dot-prefixed ones) from the repository
# root. Manual build/test redirects such as ".PhysicsRegression.log" or
# ".3d-build.log" accumulate there over time; scripted regression output should
# instead use Run-Regressions.ps1 -Log, which writes below build\logs.
# Only the repository root is scanned; build trees are left untouched.
[CmdletBinding(SupportsShouldProcess)]
param()

$ErrorActionPreference = "Stop"
$repositoryRoot = Split-Path -Parent $PSScriptRoot

$strayLogs = @(Get-ChildItem -LiteralPath $repositoryRoot -File -Filter "*.log")
if ($strayLogs.Count -eq 0) {
    Write-Host "No stray log files found in the repository root."
    return
}

foreach ($log in $strayLogs) {
    if ($PSCmdlet.ShouldProcess($log.Name, "Delete stray log")) {
        Remove-Item -LiteralPath $log.FullName
        Write-Host "Removed $($log.Name)"
    }
}
Write-Host "Cleaned $($strayLogs.Count) log file(s) from the repository root."
