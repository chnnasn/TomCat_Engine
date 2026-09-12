$script:TomCatManagedReleaseFileMap = [ordered]@{
    "TomCat.Managed.dll" = "Managed\TomCat.ScriptHost\bin\Release\net10.0\TomCat.Managed.dll"
    "TomCat.ScriptHost.dll" = "Managed\TomCat.ScriptHost\bin\Release\net10.0\TomCat.ScriptHost.dll"
    "TomCat.ScriptHost.runtimeconfig.json" = "Managed\TomCat.ScriptHost\bin\Release\net10.0\TomCat.ScriptHost.runtimeconfig.json"
    "TomCat.ScriptHost.deps.json" = "Managed\TomCat.ScriptHost\bin\Release\net10.0\TomCat.ScriptHost.deps.json"
    "TomCat.ScriptGenerator.dll" = "Managed\TomCat.ScriptGenerator\bin\Release\net10.0\TomCat.ScriptGenerator.dll"
}

function Assert-DotNet10Sdk {
    $dotnet = Get-Command dotnet.exe -ErrorAction SilentlyContinue
    if (-not $dotnet) {
        throw ".NET 10 SDK is required to compile TomCat C# scripts, but dotnet.exe was not found on PATH. Install the .NET 10 SDK from https://dotnet.microsoft.com/download/dotnet/10.0."
    }

    $sdkOutput = @(& $dotnet.Source --list-sdks 2>&1)
    if ($LASTEXITCODE -ne 0) {
        throw "Could not query installed .NET SDKs with 'dotnet --list-sdks' (exit $LASTEXITCODE): $($sdkOutput -join ' ')"
    }

    $dotNet10Sdks = @($sdkOutput | Where-Object { $_ -match '^\s*10\.[0-9]+\.[^\s]+\s+\[' })
    if ($dotNet10Sdks.Count -eq 0) {
        $installed = @($sdkOutput | ForEach-Object {
            if ($_ -match '^\s*([^\s]+)\s+\[') { $Matches[1] }
        })
        $installedText = if ($installed.Count -eq 0) { "none" } else { $installed -join ", " }
        throw ".NET 10 SDK is required to compile TomCat C# scripts. Installed SDKs: $installedText. Install it from https://dotnet.microsoft.com/download/dotnet/10.0."
    }

    return $dotnet.Source
}

function Build-TomCatManagedRelease {
    param(
        [Parameter(Mandatory)]
        [string]$RepositoryRoot
    )

    $dotnet = Assert-DotNet10Sdk
    $solution = Join-Path $RepositoryRoot "Managed\TomCat.Managed.slnx"
    if (-not (Test-Path -LiteralPath $solution -PathType Leaf)) {
        throw "Managed solution was not found: $solution"
    }

    Write-Host "Building Managed/TomCat.Managed.slnx (Release) ..."
    & $dotnet build $solution --configuration Release --nologo
    if ($LASTEXITCODE -ne 0) {
        throw "Managed Release build failed (exit $LASTEXITCODE)."
    }
}

function Publish-TomCatManagedRelease {
    param(
        [Parameter(Mandatory)]
        [string]$RepositoryRoot,

        [Parameter(Mandatory)]
        [string]$Destination
    )

    $repositoryFull = [System.IO.Path]::GetFullPath($RepositoryRoot).TrimEnd('\')
    $destinationFull = [System.IO.Path]::GetFullPath($Destination)
    $repositoryPrefix = $repositoryFull + '\'
    if (-not $destinationFull.StartsWith(
            $repositoryPrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Managed release destination must stay inside the repository: $destinationFull"
    }

    $sources = [ordered]@{}
    foreach ($entry in $script:TomCatManagedReleaseFileMap.GetEnumerator()) {
        $source = Join-Path $RepositoryRoot $entry.Value
        if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
            throw "Required Managed Release output is missing: $source. Run a Managed Release build first."
        }
        $sources[$entry.Key] = $source
    }

    if (Test-Path -LiteralPath $destinationFull) {
        Remove-Item -LiteralPath $destinationFull -Recurse -Force
    }
    New-Item -ItemType Directory -Path $destinationFull -Force | Out-Null

    foreach ($entry in $sources.GetEnumerator()) {
        Copy-Item -LiteralPath $entry.Value -Destination (Join-Path $destinationFull $entry.Key) -Force
    }

    $actual = @(Get-ChildItem -LiteralPath $destinationFull -File -Force | Select-Object -ExpandProperty Name | Sort-Object)
    $expected = @($script:TomCatManagedReleaseFileMap.Keys | Sort-Object)
    $difference = @(Compare-Object -ReferenceObject $expected -DifferenceObject $actual)
    if ($difference.Count -ne 0) {
        throw "Managed release directory did not match the strict file whitelist: $($difference | Out-String)"
    }

    Write-Host "Published external Managed runtime/toolchain -> $destinationFull"
}
