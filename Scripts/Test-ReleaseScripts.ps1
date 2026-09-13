param()

$ErrorActionPreference = 'Stop'
$repositoryRoot = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot 'ReleaseArtifactTools.ps1')
. (Join-Path $PSScriptRoot 'ReleaseProvenanceTools.ps1')

$parseFailures = New-Object 'System.Collections.Generic.List[string]'
foreach ($script in Get-ChildItem -LiteralPath $PSScriptRoot -Filter '*.ps1' -File | Sort-Object Name) {
    $tokens = $null
    $errors = $null
    [void][System.Management.Automation.Language.Parser]::ParseFile(
        $script.FullName,
        [ref]$tokens,
        [ref]$errors
    )
    foreach ($error in @($errors)) {
        [void]$parseFailures.Add("$($script.Name): $($error.Message) at $($error.Extent.StartLineNumber):$($error.Extent.StartColumnNumber)")
    }
}
if ($parseFailures.Count -gt 0) {
    throw "PowerShell syntax check failed:`n$($parseFailures -join "`n")"
}

$toolManifestPath = Join-Path $PSScriptRoot 'ReleaseToolVersions.json'
$toolManifest = Get-Content -LiteralPath $toolManifestPath -Raw | ConvertFrom-Json
if ($toolManifest.schemaVersion -ne 1) { throw 'Unsupported ReleaseToolVersions.json schema.' }
foreach ($tool in @($toolManifest.premake, $toolManifest.enigmaVirtualBox)) {
    if (-not $tool.version) { throw 'A release tool is missing its fixed version.' }
    if ($tool.url -notmatch '^https://') { throw "Release tool URL must use HTTPS: $($tool.url)" }
    if ($tool.sha256 -notmatch '^[A-Fa-f0-9]{64}$') { throw "Release tool has an invalid SHA-256: $($tool.version)" }
}
if ($toolManifest.premake.url -notmatch [regex]::Escape($toolManifest.premake.version)) {
    throw 'Premake URL must include its fixed version.'
}

$workflowPath = Join-Path $repositoryRoot '.github\workflows\package-editor.yml'
$workflow = Get-Content -LiteralPath $workflowPath -Raw
if ($workflow -match '(?i)--clobber') { throw 'Release workflow must never overwrite existing assets with --clobber.' }
if ($workflow -match '(?m)^  workflow_dispatch:\s*$') {
    throw 'Publishing must be triggered by an existing version tag, not an arbitrary manual ref.'
}
if ($workflow -notmatch "(?ms)^  push:\s*\r?\n    tags:\s*\r?\n      - 'v\*\.\*\.\*'\s*$") {
    throw 'Release workflow must be triggered by semantic version tags.'
}
if ($workflow -notmatch '(?m)^  build-package:\s*$' -or $workflow -notmatch '(?m)^  publish-release:\s*$') {
    throw 'Release workflow must keep build and publish in separate jobs.'
}
$policyJob = [regex]::Match($workflow, '(?ms)^  release-policy:\s*$.*?(?=^  build-package:\s*$)').Value
$buildJob = [regex]::Match($workflow, '(?ms)^  build-package:\s*$.*?(?=^  publish-release:\s*$)').Value
$publishJob = [regex]::Match($workflow, '(?ms)^  publish-release:\s*$.*\z').Value
foreach ($requiredPolicy in @(
    'github.ref_protected',
    'github.event.repository.default_branch',
    'ReleaseProvenanceTools.ps1',
    'actions/workflows/regressions.yml/runs',
    'head_sha=$env:RELEASE_SHA'
)) {
    if ($policyJob -notmatch [regex]::Escape($requiredPolicy)) {
        throw "Release policy job is missing the provenance gate '$requiredPolicy'."
    }
}
if ($buildJob -notmatch '(?m)^    permissions:\s*\r?\n      contents: read\s*$') {
    throw 'Build job must have read-only repository contents permission.'
}
foreach ($requiredCliPackaging in @(
    'Invoke-Gen "Tools"',
    'Invoke-Build "Tools\Tools.sln"',
    'dist\TomCatCLI.exe'
)) {
    if ($buildJob -notmatch [regex]::Escape($requiredCliPackaging)) {
        throw "Release build does not package the headless CLI requirement '$requiredCliPackaging'."
    }
}
if ($buildJob -notmatch '(?m)^\s*Copy-Item -LiteralPath \$cliOutput -Destination "dist\\TomCatCLI\.exe" -Force\s*$') {
    throw 'Release build must stage the freshly built TomCatCLI.exe in dist.'
}
if ($buildJob -notmatch '(?m)^\s*\$archiveInputs\s*=\s*@\([^\r\n]*"dist\\TomCatCLI\.exe"[^\r\n]*\)\s*$' -or
    $buildJob -notmatch '(?m)^\s*Compress-Archive -LiteralPath \$archiveInputs\b') {
    throw 'Official Editor archive inputs must contain TomCatCLI.exe.'
}
$localPackageScript = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'Package-Editor.ps1') -Raw
foreach ($requiredCliPackaging in @('Tools\premake5.lua', 'Tools\Tools.sln', 'TomCatCLI.exe')) {
    if ($localPackageScript -notmatch [regex]::Escape($requiredCliPackaging)) {
        throw "Local Editor packaging does not include the headless CLI requirement '$requiredCliPackaging'."
    }
}
if ($localPackageScript -notmatch '(?m)^\s*Copy-Item -LiteralPath \$cliSource -Destination \$outCli -Force\s*$') {
    throw 'Local Editor packaging must stage the freshly built headless CLI output.'
}
if ($localPackageScript -notmatch '(?m)^\s*\$archiveInputs\s*=\s*@\([^\r\n]*\$outCli[^\r\n]*\)\s*$' -or
    $localPackageScript -notmatch '(?m)^\s*Compress-Archive -LiteralPath \$archiveInputs\b') {
    throw 'Local Editor archive inputs must contain TomCatCLI.exe.'
}
if ($publishJob -notmatch '(?m)^    permissions:\s*\r?\n      contents: write\s*$') {
    throw 'Publish job must hold the repository write permission separately.'
}
foreach ($requiredAsset in @('SHA256SUMS', 'THIRD_PARTY_NOTICES.txt', 'TomCat.spdx.json')) {
    if ($workflow -notmatch [regex]::Escape($requiredAsset)) { throw "Release workflow does not publish $requiredAsset." }
}
if ($workflow -notmatch 'RELEASE_PROVENANCE\.json') {
    throw 'Release workflow must publish a checksummed same-SHA provenance attestation.'
}
if ($publishJob -notmatch '(?s)gh release create.*--verify-tag.*--target \$env:RELEASE_SHA') {
    throw 'Release publishing must verify the existing tag and bind it to the checked SHA.'
}

$commitSha = '0123456789abcdef0123456789abcdef01234567'
$goodRun = [pscustomobject]@{
    id = 42
    run_number = 7
    html_url = 'https://example.invalid/actions/runs/42'
    head_sha = $commitSha
    head_branch = 'main'
    event = 'push'
    status = 'completed'
    conclusion = 'success'
}
$selectedRun = Assert-TomCatReleaseProvenance -Tag 'v1.2.3' -Version '1.2.3' `
    -CommitSha $commitSha -TagTargetSha $commitSha -DefaultBranch 'main' `
    -TagRefProtected $true -DefaultBranchProtected $true `
    -DefaultBranchComparisonStatus 'ahead' -RegressionRuns @($goodRun)
if ($selectedRun.id -ne 42) {
    throw 'Release provenance gate did not return the qualifying same-SHA regression run.'
}

function Assert-ProvenanceRejected {
    param([Parameter(Mandatory)][scriptblock]$Action)
    $rejected = $false
    try { & $Action | Out-Null } catch { $rejected = $true }
    if (-not $rejected) { throw 'Release provenance policy accepted an unsafe fixture.' }
}

Assert-ProvenanceRejected {
    Assert-TomCatReleaseProvenance -Tag 'v1.2.3' -Version '1.2.3' `
        -CommitSha $commitSha -TagTargetSha $commitSha -DefaultBranch 'main' `
        -TagRefProtected $false -DefaultBranchProtected $true `
        -DefaultBranchComparisonStatus 'ahead' -RegressionRuns @($goodRun)
}
Assert-ProvenanceRejected {
    Assert-TomCatReleaseProvenance -Tag 'v1.2.3' -Version '1.2.3' `
        -CommitSha $commitSha -TagTargetSha $commitSha -DefaultBranch 'main' `
        -TagRefProtected $true -DefaultBranchProtected $false `
        -DefaultBranchComparisonStatus 'ahead' -RegressionRuns @($goodRun)
}
Assert-ProvenanceRejected {
    Assert-TomCatReleaseProvenance -Tag 'v1.2.3' -Version '1.2.3' `
        -CommitSha $commitSha -TagTargetSha ('f' * 40) -DefaultBranch 'main' `
        -TagRefProtected $true -DefaultBranchProtected $true `
        -DefaultBranchComparisonStatus 'ahead' -RegressionRuns @($goodRun)
}
Assert-ProvenanceRejected {
    Assert-TomCatReleaseProvenance -Tag 'v1.2.3' -Version '1.2.3' `
        -CommitSha $commitSha -TagTargetSha $commitSha -DefaultBranch 'main' `
        -TagRefProtected $true -DefaultBranchProtected $true `
        -DefaultBranchComparisonStatus 'diverged' -RegressionRuns @($goodRun)
}
$wrongShaRun = $goodRun.PSObject.Copy()
$wrongShaRun.head_sha = 'f' * 40
Assert-ProvenanceRejected {
    Assert-TomCatReleaseProvenance -Tag 'v1.2.3' -Version '1.2.3' `
        -CommitSha $commitSha -TagTargetSha $commitSha -DefaultBranch 'main' `
        -TagRefProtected $true -DefaultBranchProtected $true `
        -DefaultBranchComparisonStatus 'identical' -RegressionRuns @($wrongShaRun)
}

$setupScript = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'Setup.py') -Raw
if ($setupScript -notmatch 'ReleaseToolVersions\.json' -or $setupScript -notmatch 'hashlib\.sha256') {
    throw 'Setup.py must verify Premake with the shared pinned-tool manifest.'
}

$tempRoot = Join-Path ([System.IO.Path]::GetTempPath()) ('TomCatReleaseScriptTests-' + [guid]::NewGuid().ToString('N'))
[void](New-Item -ItemType Directory -Path $tempRoot)
try {
    $fixture = Join-Path $tempRoot 'fixture.bin'
    [System.IO.File]::WriteAllText($fixture, 'abc', (New-Object System.Text.UTF8Encoding($false)))
    $expectedFixtureHash = 'BA7816BF8F01CFEA414140DE5DAE2223B00361A396177A9CB410FF61F20015AD'
    [void](Assert-TomCatFileSha256 -Path $fixture -ExpectedHash $expectedFixtureHash)
    $mismatchRejected = $false
    try { [void](Assert-TomCatFileSha256 -Path $fixture -ExpectedHash ('0' * 64)) }
    catch { $mismatchRejected = $true }
    if (-not $mismatchRejected) { throw 'Checksum helper accepted a mismatched hash.' }

    Copy-Item -LiteralPath $fixture -Destination (Join-Path $tempRoot 'TomCat.zip')
    Copy-Item -LiteralPath $fixture -Destination (Join-Path $tempRoot 'TomCatHub.exe')
    Copy-Item -LiteralPath $fixture -Destination (Join-Path $tempRoot 'RELEASE_PROVENANCE.json')
    $metadata = New-TomCatReleaseMetadata -RepositoryRoot $repositoryRoot `
        -DistPath $tempRoot -Target both -Version '0.0.0-test' `
        -AdditionalChecksumFiles @('RELEASE_PROVENANCE.json')
    foreach ($path in @($metadata.Checksums, $metadata.Notices, $metadata.Sbom)) {
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Metadata output was not created: $path" }
    }
    $sbom = Get-Content -LiteralPath $metadata.Sbom -Raw | ConvertFrom-Json
    if ($sbom.spdxVersion -ne 'SPDX-2.3' -or @($sbom.files).Count -ne 2) {
        throw 'Generated SPDX document does not describe both test artifacts.'
    }
    if (@($sbom.packages).Count -lt 2 -or @($sbom.relationships).Count -lt 2) {
        throw 'Generated SPDX document is missing dependency or relationship records.'
    }
    $sumLines = @(Get-Content -LiteralPath $metadata.Checksums)
    if ($sumLines.Count -ne 5) { throw "Expected five checksum entries, got $($sumLines.Count)." }
    foreach ($line in $sumLines) {
        if ($line -notmatch '^([a-f0-9]{64})  (.+)$') { throw "Invalid SHA256SUMS line: $line" }
        [void](Assert-TomCatFileSha256 -Path (Join-Path $tempRoot $Matches[2]) -ExpectedHash $Matches[1])
    }
} finally {
    $expectedTempParent = [System.IO.Path]::GetFullPath([System.IO.Path]::GetTempPath()).TrimEnd('\') + '\'
    $resolvedTemp = [System.IO.Path]::GetFullPath($tempRoot)
    if ($resolvedTemp.StartsWith($expectedTempParent, [System.StringComparison]::OrdinalIgnoreCase) -and
        (Split-Path -Leaf $resolvedTemp).StartsWith('TomCatReleaseScriptTests-', [System.StringComparison]::Ordinal)) {
        Remove-Item -LiteralPath $resolvedTemp -Recurse -Force
    }
}

Write-Host 'PASS Release scripts: protected refs, same-SHA regressions, provenance, checksums, SPDX, notices, and release policy.'
