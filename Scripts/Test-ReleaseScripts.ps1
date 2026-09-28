param()

$ErrorActionPreference = 'Stop'
$repositoryRoot = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot 'ReleaseArtifactTools.ps1')
. (Join-Path $PSScriptRoot 'ReleaseProvenanceTools.ps1')
. (Join-Path $PSScriptRoot 'EvbTools.ps1')

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
$regressionWorkflowPath = Join-Path $repositoryRoot '.github\workflows\regressions.yml'
$regressionWorkflow = Get-Content -LiteralPath $regressionWorkflowPath -Raw
if ($regressionWorkflow -notmatch "(?ms)^  push:\s*\r?\n    branches:\s*\r?\n      - '\*\*'\s*$") {
    throw 'Regressions must run for branch pushes and must not duplicate the full suite for tag pushes.'
}
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
    'head_sha=$env:RELEASE_SHA',
    'event=push',
    '$regressionDeadline = [DateTimeOffset]::UtcNow.AddMinutes(60)',
    'Start-Sleep -Seconds 30',
    '-RegressionRuns $regressionRuns'
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
    'Package-Editor.ps1',
    'dist/TomCat.exe',
    '--product-info-json',
    '--cli help',
    'tomcat-editor-probe-cwd',
    'tomcat-editor-cold-cwd',
    'tomcat-editor-warm-cwd',
    'Samples\PhysicsPlayground',
    'PhysicsPlayground\Project.tcproj',
    'Assert-NoExternalEditorRuntime'
)) {
    if ($buildJob -notmatch [regex]::Escape($requiredCliPackaging)) {
        throw "Release build does not satisfy the single-file Editor requirement '$requiredCliPackaging'."
    }
}
if ($workflow -match 'TomCat\.zip' -or $buildJob -match 'Compress-Archive') {
    throw 'Official releases must publish the EVB-boxed TomCat.exe directly, without an Editor ZIP.'
}
if ($buildJob -match '(?m)^\s*Copy-Item[^\r\n]+-Destination\s+"dist\\(?:TomCatCLI\.exe|Managed|Packages)') {
    throw 'Official releases must not stage Editor runtime payload files beside TomCat.exe.'
}
$localPackageScript = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'Package-Editor.ps1') -Raw
foreach ($requiredCliPackaging in @(
        'Tools\premake5.lua',
        'Tools\Tools.sln',
        'TomCatCLI.exe',
        'New-TomCatRuntimeManifest',
        'Set-EvbPackageTree',
        'PlayerTemplates',
        '.tomcat-runtime')) {
    if ($localPackageScript -notmatch [regex]::Escape($requiredCliPackaging)) {
        throw "Local Editor packaging does not embed the runtime requirement '$requiredCliPackaging'."
    }
}
$editorPremake = Get-Content -LiteralPath (Join-Path $repositoryRoot 'Editor\TomCatInut\premake5.lua') -Raw
$cliPremake = Get-Content -LiteralPath (Join-Path $repositoryRoot 'Tools\TomCatCLI\premake5.lua') -Raw
$requiredPackageAssets = @(
    'Resources\Sprites\TomCat\Circle.tga',
    'Resources\Sprites\TomCat\Square.tga',
    'fonts\opensans\OpenSans-Regular.ttf'
)
foreach ($requiredPackageAsset in $requiredPackageAssets) {
    if ($localPackageScript -notmatch [regex]::Escape($requiredPackageAsset)) {
        throw "Local Editor packaging does not require built-in package asset '$requiredPackageAsset'."
    }
    $premakeAsset = $requiredPackageAsset.Replace('\', '\\')
    $premakeGuard = 'if not exist \"$(ProjectDir)Packages\\' + $premakeAsset + '\"'
    if ([regex]::Matches($editorPremake, [regex]::Escape($premakeGuard)).Count -ne 3) {
        throw "Every Editor build configuration must require built-in package asset '$requiredPackageAsset'."
    }
    $cliAsset = ('Editor\TomCatInut\Packages\' + $requiredPackageAsset).Replace('\', '\\')
    if ([regex]::Matches($cliPremake, [regex]::Escape($cliAsset)).Count -lt 2) {
        throw "The development TomCatCLI build does not copy built-in package asset '$requiredPackageAsset'."
    }
}
if ($localPackageScript -notmatch [regex]::Escape(
        'Test-Path -LiteralPath $requiredPackageAssetPath -PathType Leaf')) {
    throw 'Local Editor packaging must reject missing built-in package assets.'
}
foreach ($runtimePackageCopyRequirement in @(
        '$payloadPackageRoot',
        'Join-Path $payloadRoot "Packages"',
        '$packageAssetDestination = Join-Path $payloadPackageRoot $requiredPackageAsset',
        '-Destination $packageAssetDestination -Force')) {
    if ($localPackageScript -notmatch [regex]::Escape($runtimePackageCopyRequirement)) {
        throw "Local Editor packaging does not preserve built-in package asset paths in .tomcat-runtime ('$runtimePackageCopyRequirement')."
    }
}
$runtimeBundleSource = Get-Content -LiteralPath `
    (Join-Path $repositoryRoot 'TomCat\src\TomCat\Core\EditorRuntimeBundle.cpp') -Raw
foreach ($requiredPackageAsset in $requiredPackageAssets) {
    $runtimePackageAssetPath = ('packages/' +
        $requiredPackageAsset.Replace('\', '/')).ToLowerInvariant()
    if ([regex]::Matches($runtimeBundleSource,
            [regex]::Escape($runtimePackageAssetPath)).Count -lt 2) {
        throw "Editor runtime extraction must both allow and require '$runtimePackageAssetPath'."
    }
}
if ($localPackageScript -match 'Compress-Archive' -or
    $localPackageScript -match '\$archiveInputs' -or
    $localPackageScript -match 'Final package -> \$outArchive') {
    throw 'Local Editor packaging must finish as one EVB executable, without an archive.'
}
foreach ($atomicPackagingRequirement in @(
        '$stagedBoxedExe',
        '[System.IO.File]::Move',
        '$packageSucceeded',
        'Join-Path $dist "TomCat"')) {
    if ($localPackageScript -notmatch [regex]::Escape($atomicPackagingRequirement)) {
        throw "Local Editor packaging is missing atomic publication/cleanup requirement '$atomicPackagingRequirement'."
    }
}
$evbToolsScript = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'EvbTools.ps1') -Raw
foreach ($runtimeLimit in @(
        '$maximumManifestBytes = [uint64]4194304',
        '$maximumRuntimeFiles = 10000',
        '$maximumRuntimeFileBytes = [uint64]2147483648',
        '$maximumRuntimeBytes = [uint64]4294967296')) {
    if ($evbToolsScript -notmatch [regex]::Escape($runtimeLimit)) {
        throw "Runtime manifest producer is missing C++ reader limit '$runtimeLimit'."
    }
}
$editorTemplate = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'editor.evb') -Raw
if ($editorTemplate -notmatch '<CompressFiles>True</CompressFiles>' -or
    $editorTemplate -notmatch '(?is)<Name>\s*Packages\s*</Name>' -or
    $editorTemplate -notmatch '(?is)<Name>\s*\.tomcat-runtime\s*</Name>') {
    throw 'Editor EVB template must compress and expose Packages plus .tomcat-runtime marker trees.'
}
foreach ($runtimeName in @(
        'shaderc_shared.dll', 'msvcp140.dll', 'vcruntime140.dll',
        'vcruntime140_1.dll')) {
    if ($editorTemplate -notmatch "(?is)<Name>\s*$([regex]::Escape($runtimeName))\s*</Name>") {
        throw "Editor EVB root does not virtualize required native runtime $runtimeName."
    }
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
if ($publishJob -notmatch [regex]::Escape('dist\TomCat.exe')) {
    throw 'Release publishing must attach the single-file TomCat.exe.'
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

$emptyRunError = ''
try {
    Assert-TomCatReleaseProvenance -Tag 'v1.2.3' -Version '1.2.3' `
        -CommitSha $commitSha -TagTargetSha $commitSha -DefaultBranch 'main' `
        -TagRefProtected $true -DefaultBranchProtected $true `
        -DefaultBranchComparisonStatus 'identical' -RegressionRuns @() | Out-Null
} catch {
    $emptyRunError = $_.Exception.Message
}
if ($emptyRunError -notmatch 'No successful full Regressions workflow run exists') {
    throw "An empty regression result did not reach the explicit provenance rejection: $emptyRunError"
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

    $packagesFixture = Join-Path $tempRoot 'PackagesFixture'
    $packageResources = Join-Path $packagesFixture 'Resources'
    $excludedTemplates = Join-Path $packagesFixture 'PlayerTemplates'
    New-Item -ItemType Directory -Path $packageResources, $excludedTemplates -Force | Out-Null
    Copy-Item -LiteralPath $fixture -Destination (Join-Path $packageResources 'a&b.bin')
    Copy-Item -LiteralPath $fixture -Destination (Join-Path $excludedTemplates 'must-not-be-virtual.bin')

    $payloadRoot = Join-Path $tempRoot 'RuntimePayload'
    $managedRoot = Join-Path $payloadRoot 'Managed'
    $templateRoot = Join-Path $payloadRoot 'Packages\PlayerTemplates\win-x64'
    $runtimePackageRoot = Join-Path $payloadRoot 'Packages'
    New-Item -ItemType Directory -Path $managedRoot, `
        (Join-Path $templateRoot 'Managed'), `
        (Join-Path $templateRoot 'dotnet\host\fxr\10.0.0'), `
        $runtimePackageRoot -Force | Out-Null
    foreach ($name in @(
            'TomCat.Managed.dll',
            'TomCat.ScriptGenerator.dll',
            'TomCat.ScriptHost.deps.json',
            'TomCat.ScriptHost.dll',
            'TomCat.ScriptHost.runtimeconfig.json')) {
        Copy-Item -LiteralPath $fixture -Destination (Join-Path $managedRoot $name)
    }
    foreach ($name in @(
            'TomCatCLI.exe', 'shaderc_shared.dll', 'msvcp140.dll',
            'vcruntime140.dll', 'vcruntime140_1.dll')) {
        Copy-Item -LiteralPath $fixture -Destination (Join-Path $payloadRoot $name)
    }
    foreach ($relativePath in @(
            'template.json',
            'TomCatPlayer.exe',
            'Managed\TomCat.Managed.dll',
            'dotnet\host\fxr\10.0.0\hostfxr.dll')) {
        Copy-Item -LiteralPath $fixture -Destination (Join-Path $templateRoot $relativePath)
    }
    foreach ($relativePath in $requiredPackageAssets) {
        $runtimePackageAsset = Join-Path $runtimePackageRoot $relativePath
        New-Item -ItemType Directory -Path (Split-Path -Parent $runtimePackageAsset) `
            -Force | Out-Null
        Copy-Item -LiteralPath $fixture -Destination $runtimePackageAsset
    }

    $runtimeManifestPath = New-TomCatRuntimeManifest `
        -PayloadRoot $payloadRoot -EngineBuildId 'TomCat-test'
    $runtimeManifest = Get-Content -LiteralPath $runtimeManifestPath -Raw | ConvertFrom-Json
    $manifestFields = @($runtimeManifest.PSObject.Properties.Name | Sort-Object)
    if (@(Compare-Object @('engineBuildId', 'files', 'schemaVersion') $manifestFields).Count -ne 0 -or
        $runtimeManifest.schemaVersion -ne 1 -or
        $runtimeManifest.engineBuildId -ne 'TomCat-test') {
        throw 'Runtime manifest does not have the strict schemaVersion/engineBuildId/files contract.'
    }
    $payloadFilesWithoutManifest = @(Get-ChildItem -LiteralPath $payloadRoot -Recurse -File |
        Where-Object { $_.FullName -ne $runtimeManifestPath })
    if (@($runtimeManifest.files).Count -ne $payloadFilesWithoutManifest.Count) {
        throw 'Runtime manifest does not enumerate every payload file exactly once.'
    }
    foreach ($record in @($runtimeManifest.files)) {
        $entryFields = @($record.PSObject.Properties.Name | Sort-Object)
        if (@(Compare-Object @('path', 'sha256', 'size') $entryFields).Count -ne 0 -or
            [string]$record.path -eq 'runtime-manifest.json' -or
            [string]$record.path -match '\\' -or
            [string]$record.path -match '(^|/)\.\.(/|$)') {
            throw "Runtime manifest contains an invalid entry: $($record | ConvertTo-Json -Compress)"
        }
        $payloadFile = Join-Path $payloadRoot ([string]$record.path).Replace('/', '\')
        if (-not (Test-Path -LiteralPath $payloadFile -PathType Leaf) -or
            [long]$record.size -ne (Get-Item -LiteralPath $payloadFile).Length -or
            -not ([string]$record.sha256).Equals(
                (Get-FileHash -LiteralPath $payloadFile -Algorithm SHA256).Hash,
                [System.StringComparison]::OrdinalIgnoreCase)) {
            throw "Runtime manifest entry does not match its payload file: $($record.path)"
        }
    }

    if (-not (Test-TomCatRuntimePathSegment -Value 'TomCat-test')) {
        throw 'Runtime path-segment validation rejected a valid build ID.'
    }
    foreach ($invalidBuildId in @('bad:id', 'CON', 'LPT1.txt', 'trailing.', ('x' * 129))) {
        $invalidBuildIdRejected = $false
        try {
            [void](New-TomCatRuntimeManifest -PayloadRoot $payloadRoot `
                -EngineBuildId $invalidBuildId)
        } catch {
            $invalidBuildIdRejected = $true
        }
        if (-not $invalidBuildIdRejected) {
            throw "Runtime manifest accepted unsafe engineBuildId '$invalidBuildId'."
        }
    }

    $evbFixture = @'
<>
  <Files>
    <Files>
      <File>
        <Type>3</Type>
        <Name>Packages</Name>
        <Action>0</Action>
        <Files></Files>
      </File>
      <File>
        <Type>3</Type>
        <Name>.tomcat-runtime</Name>
        <Action>0</Action>
        <Files></Files>
      </File>
    </Files>
  </Files>
</>
'@
    $packagesOnlyProject = Set-EvbPackageTree -TemplateText $evbFixture `
        -PackageSource $packagesFixture -ExcludeRelativePaths @('PlayerTemplates')
    if ($packagesOnlyProject -notmatch '<Name>a&amp;b\.bin</Name>' -or
        $packagesOnlyProject -match '<Name>must-not-be-virtual\.bin</Name>' -or
        $packagesOnlyProject -match '<Name>PlayerTemplates</Name>') {
        throw 'EVB Packages generation did not exclude PlayerTemplates or escape file names.'
    }
    $completeEditorProject = Set-EvbDirectoryTree -TemplateText $packagesOnlyProject `
        -NodeName '.tomcat-runtime' -SourceDirectory $payloadRoot `
        -FileAction 0 -DirectoryAction 3
    $requiredRuntimeEntries = @(
        'runtime-manifest.json', 'TomCatCLI.exe', 'PlayerTemplates'
    ) + @($requiredPackageAssets | ForEach-Object { Split-Path -Leaf $_ })
    foreach ($requiredRuntimeEntry in $requiredRuntimeEntries) {
        if ($completeEditorProject -notmatch "(?is)<Name>\s*$([regex]::Escape($requiredRuntimeEntry))\s*</Name>") {
            throw "EVB runtime tree is missing $requiredRuntimeEntry."
        }
    }
    $runtimeMarkerIndex = $completeEditorProject.IndexOf(
        '<Name>.tomcat-runtime</Name>',
        [System.StringComparison]::OrdinalIgnoreCase)
    if ($runtimeMarkerIndex -lt 0) {
        throw 'EVB runtime payload marker could not be located after tree generation.'
    }
    if ($completeEditorProject -notmatch
        '(?is)<File>\s*<Type>\s*3\s*</Type>\s*<Name>\s*\.tomcat-runtime\s*</Name>\s*<Action>\s*3\s*</Action>') {
        throw 'The .tomcat-runtime root directory must use Action=3.'
    }
    $runtimeFragment = $completeEditorProject.Substring($runtimeMarkerIndex)
    $runtimeDirectories = @([regex]::Matches($runtimeFragment,
        '(?is)<File>\s*<Type>\s*3\s*</Type>.*?<Action>\s*([0-3])\s*</Action>'))
    $runtimeFiles = @([regex]::Matches($runtimeFragment,
        '(?is)<File>\s*<Type>\s*2\s*</Type>.*?<Action>\s*([0-3])\s*</Action>'))
    if ($runtimeDirectories.Count -eq 0 -or
        @($runtimeDirectories | Where-Object { $_.Groups[1].Value -ne '3' }).Count -ne 0) {
        throw 'Every .tomcat-runtime directory must use Action=3 so the EVB source tree remains fully virtual.'
    }
    if ($runtimeFiles.Count -eq 0 -or
        @($runtimeFiles | Where-Object { $_.Groups[1].Value -ne '0' }).Count -ne 0) {
        throw 'Every .tomcat-runtime file must use Action=0 so the Editor controls physical extraction.'
    }

    $actionFixture = @'
<><Files><Files><File><Type>3</Type><Name>ActionFixture</Name><Action>3</Action><Files></Files></File></Files></Files></>
'@
    $customActionProject = Set-EvbDirectoryTree -TemplateText $actionFixture `
        -NodeName 'ActionFixture' -SourceDirectory $packageResources `
        -FileAction 2 -DirectoryAction 0
    if ($customActionProject -notmatch '(?is)<Name>\s*ActionFixture\s*</Name>\s*<Action>0</Action>' -or
        $customActionProject -notmatch '(?is)<Name>\s*a&amp;b\.bin\s*</Name>.*?<Action>2</Action>') {
        throw 'Generic EVB tree generation did not honor custom file/directory actions.'
    }

    Copy-Item -LiteralPath $fixture -Destination (Join-Path $tempRoot 'TomCat.exe')
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
