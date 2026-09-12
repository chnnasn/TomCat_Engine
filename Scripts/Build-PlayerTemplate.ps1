param(
    [ValidateSet("Debug", "Release", "Dist")]
    [string]$Configuration = "Release",
    [string]$Destination = "",
    [switch]$Build
)

$ErrorActionPreference = "Stop"
$repositoryRoot = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot "ManagedReleaseTools.ps1")

function Resolve-MSBuild {
    $command = Get-Command msbuild.exe -ErrorAction SilentlyContinue
    if ($command) { return $command.Source }
    $programFilesX86 = [Environment]::GetFolderPath("ProgramFilesX86")
    $vswhere = Join-Path $programFilesX86 "Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path -LiteralPath $vswhere -PathType Leaf) {
        $resolved = & $vswhere -latest -requires Microsoft.Component.MSBuild -find "MSBuild\**\Bin\MSBuild.exe" |
            Select-Object -First 1
        if ($resolved) { return $resolved }
    }
    $known = "D:\Microsoft Visual Studio\Versions\2026 Pro\MSBuild\Current\Bin\MSBuild.exe"
    if (Test-Path -LiteralPath $known -PathType Leaf) { return $known }
    throw "MSBuild was not found. Install Visual Studio Desktop development with C++."
}

function Copy-TemplateFile {
    param(
        [Parameter(Mandatory)][string]$Source,
        [Parameter(Mandatory)][string]$RelativePath,
        [Parameter(Mandatory)][string]$Root
    )
    if (-not (Test-Path -LiteralPath $Source -PathType Leaf)) {
        throw "Required Player template input is missing: $Source"
    }
    $target = Join-Path $Root $RelativePath
    New-Item -ItemType Directory -Path (Split-Path -Parent $target) -Force | Out-Null
    Copy-Item -LiteralPath $Source -Destination $target -Force
}

function Resolve-VCReleaseRuntime {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path -LiteralPath $vswhere -PathType Leaf)) {
        throw "vswhere.exe is required to locate the x64 Visual C++ redistributable runtime."
    }
    $installation = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath |
        Select-Object -First 1
    if (-not $installation) {
        throw "Visual Studio C++ tools and their x64 redistributable runtime were not found."
    }
    $redistRoot = Join-Path $installation "VC\Redist\MSVC"
    $versions = @(Get-ChildItem -LiteralPath $redistRoot -Directory |
        Where-Object { $_.Name -match '^\d+\.\d+\.\d+$' } |
        Sort-Object { [version]$_.Name } -Descending)
    foreach ($version in $versions) {
        $candidates = @(Get-ChildItem -LiteralPath (Join-Path $version.FullName "x64") -Directory |
            Where-Object { $_.Name -match '^Microsoft\.VC\d+\.CRT$' })
        foreach ($candidate in $candidates) {
            $missing = @($script:PlayerCRuntimeFiles | Where-Object {
                -not (Test-Path -LiteralPath (Join-Path $candidate.FullName $_) -PathType Leaf)
            })
            if ($missing.Count -eq 0) { return $candidate.FullName }
        }
    }
    throw "The required x64 MSVCP140/VCRUNTIME140 redistributable DLLs were not found below $redistRoot."
}

$script:PlayerCRuntimeFiles = @("msvcp140.dll", "vcruntime140.dll", "vcruntime140_1.dll")

function Copy-TemplateDirectory {
    param(
        [Parameter(Mandatory)][string]$Source,
        [Parameter(Mandatory)][string]$RelativePath,
        [Parameter(Mandatory)][string]$Root
    )
    if (-not (Test-Path -LiteralPath $Source -PathType Container)) {
        throw "Required Player template directory is missing: $Source"
    }
    $target = Join-Path $Root $RelativePath
    New-Item -ItemType Directory -Path (Split-Path -Parent $target) -Force | Out-Null
    Copy-Item -LiteralPath $Source -Destination $target -Recurse -Force
}

if (-not $Destination) {
    $Destination = Join-Path $repositoryRoot "Editor\bin\$Configuration-windows-x86_64\TomCatInut\Packages\PlayerTemplates\win-x64"
}
$destinationFull = [System.IO.Path]::GetFullPath($Destination).TrimEnd('\', '/')
$repositoryFull = [System.IO.Path]::GetFullPath($repositoryRoot).TrimEnd('\')
if (-not $destinationFull.StartsWith(
        $repositoryFull + '\', [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "Player template destination must stay inside the repository: $destinationFull"
}

if ($Build) {
    Build-TomCatManagedRelease -RepositoryRoot $repositoryRoot
    $premake = Join-Path $repositoryRoot "vendor\premake\bin\premake5.exe"
    if (-not (Test-Path -LiteralPath $premake -PathType Leaf)) {
        throw "Premake5 was not found: $premake"
    }
    & $premake "--file=$(Join-Path $repositoryRoot 'Player\premake5.lua')" vs2022
    if ($LASTEXITCODE -ne 0) { throw "Player project generation failed." }
    $msbuild = Resolve-MSBuild
    & $msbuild (Join-Path $repositoryRoot "Player\Player.sln") "-p:Configuration=$Configuration" "-p:Platform=x64" -m -v:m -nologo
    if ($LASTEXITCODE -ne 0) { throw "TomCatPlayer build failed." }
}

$outputName = "$Configuration-windows-x86_64"
$playerRoot = Join-Path $repositoryRoot "Player\bin\$outputName\TomCatPlayer"
$playerExecutable = Join-Path $playerRoot "TomCatPlayer.exe"
$compatibilityHeader = Get-Content -LiteralPath (Join-Path $repositoryRoot "TomCat\src\TomCat\Runtime\RuntimeCompatibility.h") -Raw
if ($compatibilityHeader -notmatch 'EngineBuildID\s*=\s*"([^"]+)"') {
    throw "Could not read EngineBuildID from RuntimeCompatibility.h."
}
$engineBuildId = $Matches[1]
if ($compatibilityHeader -notmatch 'TcpakVersion\s*=\s*([0-9]+)') {
    throw "Could not read TcpakVersion from RuntimeCompatibility.h."
}
$tcpakVersion = [uint32]$Matches[1]
if ($compatibilityHeader -notmatch 'PlayerAbiVersion\s*=\s*([0-9]+)') {
    throw "Could not read PlayerAbiVersion from RuntimeCompatibility.h."
}
$playerAbiVersion = [uint32]$Matches[1]
if ($compatibilityHeader -notmatch 'PlayerTemplateSchemaVersion\s*=\s*([0-9]+)') {
    throw "Could not read PlayerTemplateSchemaVersion from RuntimeCompatibility.h."
}
$templateSchemaVersion = [uint32]$Matches[1]

$managedRoot = Join-Path $repositoryRoot "Managed\TomCat.ScriptHost\bin\Release\net10.0"
$vcRuntimeRoot = Resolve-VCReleaseRuntime
$dotnetExecutable = Assert-DotNet10Sdk
$dotnetRoot = Split-Path -Parent $dotnetExecutable
$hostFxr = Get-ChildItem -LiteralPath (Join-Path $dotnetRoot "host\fxr") -Directory |
    Where-Object { $_.Name -match '^10\.[0-9]+\.[0-9]+$' } |
    Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1
$runtime = Get-ChildItem -LiteralPath (Join-Path $dotnetRoot "shared\Microsoft.NETCore.App") -Directory |
    Where-Object { $_.Name -match '^10\.[0-9]+\.[0-9]+$' } |
    Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1
if (-not $hostFxr -or -not $runtime) {
    throw "A private .NET 10 host/runtime could not be resolved below $dotnetRoot."
}

$operationId = [guid]::NewGuid().ToString("N")
$stagingFull = "$destinationFull.staging-$operationId"
$backupFull = "$destinationFull.previous-$operationId"
foreach ($internalPath in @($stagingFull, $backupFull)) {
    if (-not $internalPath.StartsWith(
            $repositoryFull + '\', [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to use a Player template work directory outside the repository: $internalPath"
    }
}

$published = $false
New-Item -ItemType Directory -Path $stagingFull -Force | Out-Null
try {
    Copy-TemplateFile -Source $playerExecutable -RelativePath "TomCatPlayer.exe" -Root $stagingFull
    $shaderDll = Join-Path $playerRoot "shaderc_shared.dll"
    if (-not (Test-Path -LiteralPath $shaderDll -PathType Leaf)) {
        $shaderDll = Join-Path $repositoryRoot "vendor\VulkanSDK\Bin\shaderc_shared.dll"
    }
    Copy-TemplateFile -Source $shaderDll -RelativePath "shaderc_shared.dll" -Root $stagingFull
    foreach ($name in $script:PlayerCRuntimeFiles) {
        Copy-TemplateFile -Source (Join-Path $vcRuntimeRoot $name) -RelativePath $name -Root $stagingFull
    }

    foreach ($name in @(
            "TomCat.Managed.dll",
            "TomCat.ScriptHost.dll",
            "TomCat.ScriptHost.runtimeconfig.json",
            "TomCat.ScriptHost.deps.json")) {
        Copy-TemplateFile -Source (Join-Path $managedRoot $name) -RelativePath (Join-Path "Managed" $name) -Root $stagingFull
    }

    Copy-TemplateDirectory -Source $hostFxr.FullName -RelativePath (Join-Path "dotnet\host\fxr" $hostFxr.Name) -Root $stagingFull
    Copy-TemplateDirectory -Source $runtime.FullName -RelativePath (Join-Path "dotnet\shared\Microsoft.NETCore.App" $runtime.Name) -Root $stagingFull
    Copy-TemplateFile -Source (Join-Path $repositoryRoot "Editor\TomCatInut\Packages\Shaders\Texture.glsl") -RelativePath "Packages\Shaders\Texture.glsl" -Root $stagingFull
    Copy-TemplateFile -Source (Join-Path $repositoryRoot "Editor\TomCatInut\Packages\Shaders\FlatColor.glsl") -RelativePath "Packages\Shaders\FlatColor.glsl" -Root $stagingFull

    $forbidden = @(Get-ChildItem -LiteralPath $stagingFull -Recurse -File -Force |
        Where-Object {
            $_.Extension -in @(".cs", ".csproj", ".sln", ".slnx", ".pdb", ".tcproj") -or
            $_.Name -ieq "TomCat.ScriptGenerator.dll" -or
            $_.Name -ieq "last-good.json"
        })
    if ($forbidden.Count -ne 0) {
        throw "Forbidden authoring files entered the Player template: $($forbidden.FullName -join ', ')"
    }

    $stagingPrefix = $stagingFull.TrimEnd('\', '/') + '\'
    $files = @(Get-ChildItem -LiteralPath $stagingFull -Recurse -File -Force |
        Sort-Object FullName |
        ForEach-Object {
            if (-not $_.FullName.StartsWith(
                    $stagingPrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
                throw "Template file escaped its staging directory: $($_.FullName)"
            }
            # Windows PowerShell 5.1 runs on .NET Framework, which does not
            # expose Path.GetRelativePath. All enumerated files are below the
            # verified staging root, so a checked prefix removal is equivalent.
            $relative = $_.FullName.Substring($stagingPrefix.Length).Replace('\', '/')
            [ordered]@{
                Path = $relative
                SHA256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
            }
        })
    $manifest = [ordered]@{
        SchemaVersion = $templateSchemaVersion
        EngineBuildID = $engineBuildId
        TcpakVersion = $tcpakVersion
        PlayerAbiVersion = $playerAbiVersion
        Files = $files
    }
    $manifestPath = Join-Path $stagingFull "template.json"
    $manifestJson = $manifest | ConvertTo-Json -Depth 6
    $utf8NoBom = New-Object System.Text.UTF8Encoding($false)
    [System.IO.File]::WriteAllText($manifestPath, $manifestJson, $utf8NoBom)

    $actualFiles = @(Get-ChildItem -LiteralPath $stagingFull -Recurse -File -Force)
    if ($actualFiles.Count -ne $files.Count + 1) {
        throw "Player template changed while template.json was written."
    }

    $movedPrevious = $false
    try {
        if (Test-Path -LiteralPath $destinationFull) {
            [System.IO.Directory]::Move($destinationFull, $backupFull)
            $movedPrevious = $true
        }
        [System.IO.Directory]::Move($stagingFull, $destinationFull)
        $published = $true
    }
    catch {
        $publishError = $_.Exception.Message
        if ($movedPrevious -and
            -not (Test-Path -LiteralPath $destinationFull) -and
            (Test-Path -LiteralPath $backupFull -PathType Container)) {
            try {
                [System.IO.Directory]::Move($backupFull, $destinationFull)
            }
            catch {
                throw "Could not publish the Player template: $publishError. Rollback also failed: $($_.Exception.Message)"
            }
        }
        throw "Could not publish the Player template: $publishError"
    }

    if ($movedPrevious -and (Test-Path -LiteralPath $backupFull -PathType Container)) {
        try {
            Remove-Item -LiteralPath $backupFull -Recurse -Force
        }
        catch {
            Write-Warning "The previous Player template could not be removed: $($_.Exception.Message)"
        }
    }
    Write-Host "Player Template ready: $destinationFull ($($files.Count) hashed files)"
}
finally {
    if (-not $published -and (Test-Path -LiteralPath $stagingFull -PathType Container)) {
        Remove-Item -LiteralPath $stagingFull -Recurse -Force
    }
}
