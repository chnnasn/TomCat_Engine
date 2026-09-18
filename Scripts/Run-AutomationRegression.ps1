param(
    [switch]$SkipBuild,
    [string]$Python = ""
)
$ErrorActionPreference = "Stop"
$repositoryRoot = Split-Path -Parent $PSScriptRoot
if (-not $Python) { $Python = Join-Path $repositoryRoot "Tools\TomCatMCP\.venv\Scripts\python.exe" }
if (-not (Test-Path -LiteralPath $Python)) {
    throw "Create Tools/TomCatMCP/.venv and install Tools/TomCatMCP/requirements.txt first."
}
if (-not $SkipBuild) {
    Push-Location $repositoryRoot
    try {
        & vendor/premake/bin/premake5.exe --file=Editor/premake5.lua vs2022
        if ($LASTEXITCODE -ne 0) { throw "Premake failed" }
        $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
        $msbuild = & $vswhere -latest -requires Microsoft.Component.MSBuild -find 'MSBuild\**\Bin\MSBuild.exe' | Select-Object -First 1
        & $msbuild Editor/Editor.sln -p:Configuration=Release -p:Platform=x64 -m -nologo -verbosity:minimal
        if ($LASTEXITCODE -ne 0) { throw "Editor build failed" }
    } finally { Pop-Location }
}
$testRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("TomCat-Automation-" + [guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Path $testRoot | Out-Null
Copy-Item -LiteralPath (Join-Path $repositoryRoot "Samples\PhysicsPlayground\Assets") -Destination $testRoot -Recurse
Copy-Item -LiteralPath (Join-Path $repositoryRoot "Samples\PhysicsPlayground\ProjectSettings") -Destination $testRoot -Recurse
Copy-Item -LiteralPath (Join-Path $repositoryRoot "Samples\PhysicsPlayground\Project.tcproj") -Destination $testRoot
$previousPort = $env:TOMCAT_AUTOMATION_PORT
$previousToken = $env:TOMCAT_AUTOMATION_TOKEN
$previousProject = $env:TOMCAT_PROJECT
$editorProcess = $null
try {
    $listener = [System.Net.Sockets.TcpListener]::new([System.Net.IPAddress]::Loopback, 0)
    $listener.Start()
    $env:TOMCAT_AUTOMATION_PORT = [string]$listener.LocalEndpoint.Port
    $listener.Stop()
    $env:TOMCAT_AUTOMATION_TOKEN = [guid]::NewGuid().ToString("N")
    $env:TOMCAT_PROJECT = Join-Path $testRoot "Project.tcproj"
    $editor = Join-Path $repositoryRoot "Editor\bin\Release-windows-x86_64\TomCatInut\TomCatInut.exe"
    $editorProcess = Start-Process -FilePath $editor -ArgumentList ('"' + $env:TOMCAT_PROJECT + '"') `
        -WorkingDirectory (Split-Path -Parent $editor) -WindowStyle Hidden -PassThru `
        -RedirectStandardOutput (Join-Path $testRoot "editor.stdout.log") -RedirectStandardError (Join-Path $testRoot "editor.stderr.log")
    & $Python (Join-Path $repositoryRoot "Tools\TomCatMCP\tests\smoke_live.py")
    if ($LASTEXITCODE -ne 0) { throw "Automation regression failed. Logs: $testRoot" }
    Write-Host "Automation regression passed. Isolated project and logs: $testRoot"
} finally {
    if ($editorProcess -and -not $editorProcess.HasExited) { Stop-Process -Id $editorProcess.Id }
    $env:TOMCAT_AUTOMATION_PORT = $previousPort
    $env:TOMCAT_AUTOMATION_TOKEN = $previousToken
    $env:TOMCAT_PROJECT = $previousProject
}
