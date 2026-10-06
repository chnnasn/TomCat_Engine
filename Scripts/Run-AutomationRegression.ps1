param(
    [ValidateSet('Debug','Release','Dist')][string]$Configuration = 'Release',
    [switch]$SkipBuild,
    [switch]$SkipTemplate,
    [string]$Python = 'python',
    [string]$PremakePath = ''
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$editorDirectory = Join-Path $root "Editor/bin/$Configuration-windows-x86_64/TomCatInut"
$editor = Join-Path $editorDirectory 'TomCatInut.exe'
if (-not $SkipBuild) {
    if (-not $PremakePath) { $PremakePath = Join-Path $root 'vendor/premake/bin/premake5.exe' }
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    $builder = & $vswhere -latest -requires Microsoft.Component.MSBuild -find 'MSBuild\**\Bin\MSBuild.exe' | Select-Object -First 1
    Push-Location $root
    try {
        & $PremakePath --file=Editor/premake5.lua vs2022
        if ($LASTEXITCODE -ne 0) { throw 'Premake failed' }
        & $builder Editor/Editor.sln "-p:Configuration=$Configuration" -p:Platform=x64 -m -nologo -verbosity:minimal
        if ($LASTEXITCODE -ne 0) { throw 'Editor build failed' }
    } finally { Pop-Location }
}
if (-not $SkipTemplate) {
    & (Join-Path $PSScriptRoot 'Build-PlayerTemplate.ps1') -Configuration $Configuration -Build:(-not $SkipBuild) `
        -Destination (Join-Path $editorDirectory 'Packages/PlayerTemplates/win-x64')
    if ($LASTEXITCODE -ne 0) { throw 'Player template staging failed' }
}
$testRoot = Join-Path ([IO.Path]::GetTempPath()) ('TomCat-Automation-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $testRoot | Out-Null
$variables = @('TOMCAT_AUTOMATION_PORT','TOMCAT_AUTOMATION_TOKEN','TOMCAT_AUTOMATION_WORKSPACE','TOMCAT_PROJECT','PYTHONIOENCODING')
$previous = @{}
foreach ($name in $variables) { $previous[$name] = [Environment]::GetEnvironmentVariable($name, 'Process') }
$process = $null
$copiedOpenGL = @()
try {
    if ($env:TC_TEST_OPENGL_DIRECTORY) {
        foreach ($driver in (Get-ChildItem -LiteralPath $env:TC_TEST_OPENGL_DIRECTORY -Filter '*.dll' -File)) {
            $destination = Join-Path $editorDirectory $driver.Name
            if (-not (Test-Path -LiteralPath $destination)) {
                Copy-Item -LiteralPath $driver.FullName -Destination $destination
                $copiedOpenGL += $destination
            }
        }
    }
    $listener = [Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback, 0)
    $listener.Start()
    $env:TOMCAT_AUTOMATION_PORT = [string]$listener.LocalEndpoint.Port
    $listener.Stop()
    $env:TOMCAT_AUTOMATION_TOKEN = [guid]::NewGuid().ToString('N')
    $env:TOMCAT_AUTOMATION_WORKSPACE = $testRoot
    $env:TOMCAT_PROJECT = ''
    $env:PYTHONIOENCODING = 'utf-8'
    $process = Start-Process -FilePath $editor -WorkingDirectory $editorDirectory -WindowStyle Hidden -PassThru `
        -RedirectStandardOutput (Join-Path $testRoot 'editor.stdout.log') -RedirectStandardError (Join-Path $testRoot 'editor.stderr.log')
    & $Python (Join-Path $root 'Tests/Automation/workflow_test.py')
    if ($LASTEXITCODE -ne 0) { throw "Automation regression failed. Project and diagnostics: $testRoot" }
    Write-Host "Automation regression passed. Project and artifacts: $testRoot"
} finally {
    if ($process -and -not $process.HasExited) { Stop-Process -Id $process.Id }
    foreach ($name in $variables) { [Environment]::SetEnvironmentVariable($name, $previous[$name], 'Process') }
    foreach ($driver in $copiedOpenGL) { Remove-Item -LiteralPath $driver }
}
