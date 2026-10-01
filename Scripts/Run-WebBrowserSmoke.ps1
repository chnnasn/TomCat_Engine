param([string]$OutputDirectory = '', [string]$ResultsDirectory = '')
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
if(!$OutputDirectory){$OutputDirectory=Join-Path $root 'build/web-managed'}
if(!$ResultsDirectory){$ResultsDirectory=Join-Path $root 'build/web-smoke-results'}
if(!(Test-Path (Join-Path $OutputDirectory '_framework/dotnet.js'))){throw 'Run Build-WebManaged.ps1 first.'}
Copy-Item (Join-Path $root 'Web/tests/managed-browser-smoke.html') $OutputDirectory -Force
$refs=Join-Path $OutputDirectory 'smoke-refs'
New-Item -ItemType Directory $refs -Force | Out-Null
$dotnetRoot=Split-Path (Get-Command dotnet).Source
$pack=Get-ChildItem (Join-Path $dotnetRoot 'packs/Microsoft.NETCore.App.Ref') -Directory | Where-Object {$_.Name -like '10.*'} | Sort-Object {[version]$_.Name} -Descending | Select-Object -First 1
if(!$pack){throw '.NET 10 reference pack is missing.'}
foreach($name in @('mscorlib.dll','System.Runtime.dll')) {Copy-Item (Join-Path $pack.FullName "ref/net10.0/$name") $refs -Force}
Copy-Item (Join-Path $root 'Managed/TomCat.Managed/bin/Release/net10.0/TomCat.Managed.dll') $refs -Force
& node (Join-Path $root 'Web/tests/managed-browser-smoke.cjs') $OutputDirectory $ResultsDirectory
if($LASTEXITCODE -ne 0){throw 'Managed browser smoke failed.'}
