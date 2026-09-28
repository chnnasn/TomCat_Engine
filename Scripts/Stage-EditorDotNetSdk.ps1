param(
    [Parameter(Mandatory)]
    [string]$SourceRoot,

    [Parameter(Mandatory)]
    [string]$Destination,

    [string]$SdkVersion = ""
)

$ErrorActionPreference = "Stop"

$sourceFull = [System.IO.Path]::GetFullPath($SourceRoot).TrimEnd('\', '/')
$destinationFull = [System.IO.Path]::GetFullPath($Destination).TrimEnd('\', '/')
$dotnet = Join-Path $sourceFull "dotnet.exe"
if (-not (Test-Path -LiteralPath $dotnet -PathType Leaf)) {
    throw "The .NET SDK source root has no dotnet.exe: $sourceFull"
}
if (-not $SdkVersion) {
    $SdkVersion = (& $dotnet --version 2>&1 | Select-Object -First 1).Trim()
    if ($LASTEXITCODE -ne 0) {
        throw "Could not query the SDK version from $dotnet"
    }
}
if ($SdkVersion -notmatch '^10\.[0-9]+\.[0-9A-Za-z.-]+$') {
    throw "TomCat Editor releases require a pinned .NET 10 SDK, got '$SdkVersion'."
}

$sdkSource = Join-Path $sourceFull "sdk\$SdkVersion"
$bundledVersions = Join-Path $sdkSource "Microsoft.NETCoreSdk.BundledVersions.props"
if (-not (Test-Path -LiteralPath $bundledVersions -PathType Leaf)) {
    throw "The selected SDK is incomplete: $sdkSource"
}
$bundledText = Get-Content -LiteralPath $bundledVersions -Raw
$runtimeMatch = [regex]::Match($bundledText,
    '<BundledNETCoreAppPackageVersion>([^<]+)</BundledNETCoreAppPackageVersion>')
if (-not $runtimeMatch.Success) {
    throw "Could not read the bundled runtime version from $bundledVersions"
}
$runtimeVersion = $runtimeMatch.Groups[1].Value.Trim()

if (Test-Path -LiteralPath $destinationFull) {
    Remove-Item -LiteralPath $destinationFull -Recurse -Force
}
New-Item -ItemType Directory -Path $destinationFull -Force | Out-Null

foreach ($file in @('dotnet.exe', 'LICENSE.txt', 'ThirdPartyNotices.txt')) {
    $source = Join-Path $sourceFull $file
    if (Test-Path -LiteralPath $source -PathType Leaf) {
        Copy-Item -LiteralPath $source -Destination (Join-Path $destinationFull $file) -Force
    }
}

function Copy-SdkTree {
    param([string]$RelativePath)
    $source = Join-Path $sourceFull $RelativePath
    if (-not (Test-Path -LiteralPath $source -PathType Container)) {
        throw "Required .NET SDK directory is missing: $source"
    }
    $destination = Join-Path $destinationFull $RelativePath
    New-Item -ItemType Directory -Path (Split-Path -Parent $destination) -Force | Out-Null
    Copy-Item -LiteralPath $source -Destination $destination -Recurse -Force
}

# Keep only the SDK/runtime/reference packs needed to compile TomCat class
# libraries. Templates, workloads and unrelated machine-wide SDK generations do
# not enter the Editor payload.
Copy-SdkTree "host"
Copy-SdkTree "sdk\$SdkVersion"
Copy-SdkTree "shared\Microsoft.NETCore.App\$runtimeVersion"
Copy-SdkTree "packs\Microsoft.NETCore.App.Ref\$runtimeVersion"

$netStandardRoot = Join-Path $sourceFull "packs\NETStandard.Library.Ref"
$netStandardVersion = Get-ChildItem -LiteralPath $netStandardRoot -Directory |
    Sort-Object Name -Descending | Select-Object -First 1
if (-not $netStandardVersion) {
    throw "NETStandard reference assemblies are missing below $netStandardRoot"
}
Copy-SdkTree "packs\NETStandard.Library.Ref\$($netStandardVersion.Name)"

$stagedDotNet = Join-Path $destinationFull "dotnet.exe"
$reported = @(& $stagedDotNet --list-sdks 2>&1)
if ($LASTEXITCODE -ne 0 -or -not ($reported -match "^$([regex]::Escape($SdkVersion))\s")) {
    throw "The staged TomCat SDK failed self-validation: $($reported -join ' ')"
}

Write-Host "Staged TomCat-owned .NET SDK $SdkVersion (runtime $runtimeVersion) -> $destinationFull"
