param(
 [ValidateRange(1,86400)][int]$DurationSeconds=600,
 [ValidateRange(1,100000)][double]$MaxP99Milliseconds=50,
 [ValidateRange(1,100000)][double]$MaxPeakMilliseconds=250,
 [ValidateRange(1,65536)][double]$MaxMemoryGrowthMiB=32,
 [string]$OutputDirectory=''
)
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
if(!$OutputDirectory){$OutputDirectory=Join-Path $root 'build/production-soak'}
$OutputDirectory=[IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory $OutputDirectory -Force | Out-Null
$previousSeconds=$env:TOMCAT_SOAK_SECONDS; $previousOutput=$env:TOMCAT_SOAK_OUTPUT
try {
 $env:TOMCAT_SOAK_SECONDS="$DurationSeconds"; $env:TOMCAT_SOAK_OUTPUT=$OutputDirectory
 $process=Start-Process -FilePath (Join-Path $root 'Tests/bin/Release-windows-x86_64/PhysicsRegression/PhysicsRegression.exe') -WorkingDirectory $root -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $OutputDirectory 'run.log') -RedirectStandardError (Join-Path $OutputDirectory 'stderr.log')
 if(!$process.WaitForExit(($DurationSeconds+600)*1000)) {
  $process.Kill(); $process.WaitForExit()
  throw 'Native production soak exceeded duration plus ten-minute setup/cleanup allowance.'
 }
 if($process.ExitCode -ne 0){throw 'Native production soak failed; see run.log and stderr.log.'}
} finally { $env:TOMCAT_SOAK_SECONDS=$previousSeconds; $env:TOMCAT_SOAK_OUTPUT=$previousOutput }
$frames=@(Import-Csv (Join-Path $OutputDirectory 'frames.csv'))
$cycles=@(Import-Csv (Join-Path $OutputDirectory 'cycles.csv'))
$culture=[Globalization.CultureInfo]::InvariantCulture
$metrics=@{}
foreach($phase in @('decode','activate','runtime','unload')) {
 $values=@($frames | Where-Object phase -eq $phase | ForEach-Object {[double]::Parse($_.frame_ms,$culture)} | Sort-Object)
 if(!$values.Count){throw "Missing $phase samples"}
 $metrics[$phase]=@{count=$values.Count;p95=$values[[Math]::Max(0,[Math]::Ceiling($values.Count*.95)-1)];p99=$values[[Math]::Max(0,[Math]::Ceiling($values.Count*.99)-1)];peak=$values[-1]}
}
$steady=@($cycles | Select-Object -Skip 2)
$growth=$null
if($steady.Count -ge 6) {
 $first=($steady | Select-Object -First 3 | ForEach-Object {[double]$_.private_bytes} | Measure-Object -Average).Average
 $last=($steady | Select-Object -Last 3 | ForEach-Object {[double]$_.private_bytes} | Measure-Object -Average).Average
 $growth=($last-$first)/1MB
}
$passed=$null -ne $growth -and $growth -le $MaxMemoryGrowthMiB
foreach($metric in $metrics.Values){$passed=$passed -and $metric.p99 -le $MaxP99Milliseconds -and $metric.peak -le $MaxPeakMilliseconds}
$report=@{passed=$passed;requestedSeconds=$DurationSeconds;cycles=$cycles.Count;frames=$frames.Count;entityCount=5000;resolution='1280x720';measurement='hidden OpenGL workload frame, includes glFinish; excludes OS Present and parsing';phases=$metrics;privateGrowthMiB=$growth;limits=@{p99Ms=$MaxP99Milliseconds;peakMs=$MaxPeakMilliseconds;privateGrowthMiB=$MaxMemoryGrowthMiB};commit=(& git -C $root rev-parse HEAD);dirty=(@(& git -C $root status --porcelain).Count -gt 0)}
$report | ConvertTo-Json -Depth 6 | Set-Content (Join-Path $OutputDirectory 'summary.json')
$report | ConvertTo-Json -Depth 6 | Write-Output
if(!$passed){throw 'Soak acceptance failed or fewer than eight complete cycles were measured.'}
