# Dependency-free helpers shared by local release checks and GitHub Actions.
# Tool pins live in ReleaseToolVersions.json so setup and CI cannot drift.

function Assert-TomCatFileSha256 {
    param(
        [Parameter(Mandatory)]
        [string]$Path,

        [Parameter(Mandatory)]
        [ValidatePattern('^[A-Fa-f0-9]{64}$')]
        [string]$ExpectedHash
    )

    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "File to verify was not found: $Path"
    }
    $actual = (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash
    if (-not $actual.Equals($ExpectedHash, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "SHA-256 mismatch for '$Path'. Expected $ExpectedHash, got $actual."
    }
    return $actual.ToUpperInvariant()
}

function Get-TomCatReleaseDependencies {
    return @(
        [ordered]@{ Name = 'Box2D'; Version = ''; License = 'MIT'; Notice = 'TomCat\vendor\Box2D\LICENSE'; Mode = 'Full' },
        [ordered]@{ Name = 'EnTT'; Version = ''; License = 'MIT'; Notice = 'TomCat\vendor\entt\LICENSE.txt'; Mode = 'Full' },
        [ordered]@{ Name = 'GLFW'; Version = ''; License = 'Zlib'; Notice = 'TomCat\vendor\GLFW\LICENSE.md'; Mode = 'Full' },
        [ordered]@{ Name = 'GLM'; Version = ''; License = 'NOASSERTION'; Notice = 'TomCat\vendor\glm\copying.txt'; Mode = 'Full' },
        [ordered]@{ Name = 'Dear ImGui'; Version = ''; License = 'MIT'; Notice = 'TomCat\vendor\ImGui\LICENSE.txt'; Mode = 'Full' },
        [ordered]@{ Name = 'ImGuizmo'; Version = ''; License = 'MIT'; Notice = 'TomCat\vendor\ImGuizmo\LICENSE'; Mode = 'Full' },
        [ordered]@{ Name = 'spdlog'; Version = ''; License = 'MIT'; Notice = 'TomCat\vendor\spdlog\LICENSE'; Mode = 'Full' },
        [ordered]@{ Name = 'stb_image'; Version = '2.30'; License = '(MIT OR Unlicense)'; Notice = 'TomCat\vendor\stb_image\stb_image.h'; Mode = 'StbLicense' },
        [ordered]@{ Name = 'yaml-cpp'; Version = ''; License = 'MIT'; Notice = 'TomCat\vendor\yaml-cpp\LICENSE'; Mode = 'Full' },
        [ordered]@{ Name = 'Khronos KHR platform header'; Version = ''; License = 'MIT'; Notice = 'TomCat\vendor\Glad\include\KHR\khrplatform.h'; Mode = 'FirstComment' },
        [ordered]@{ Name = 'Glad generated OpenGL loader'; Version = '0.1.36'; License = 'NOASSERTION'; Notice = ''; Mode = 'None' }
    )
}

function Get-TomCatNoticeText {
    param(
        [Parameter(Mandatory)]
        [string]$Path,

        [Parameter(Mandatory)]
        [ValidateSet('Full', 'FirstComment', 'StbLicense')]
        [string]$Mode
    )

    $text = Get-Content -LiteralPath $Path -Raw -ErrorAction Stop
    if ($Mode -eq 'Full') { return $text.Trim() }
    if ($Mode -eq 'FirstComment') {
        $match = [regex]::Match($text, '(?s)/\*.*?\*/')
        if (-not $match.Success) { throw "Could not find the license comment in $Path." }
        return $match.Value.Trim()
    }

    $marker = 'This software is available under 2 licenses'
    $start = $text.IndexOf($marker, [System.StringComparison]::Ordinal)
    if ($start -lt 0) { throw "Could not find the stb license marker in $Path." }
    $commentStart = $text.LastIndexOf('/*', $start, [System.StringComparison]::Ordinal)
    if ($commentStart -lt 0) { throw "Could not find the stb license comment start in $Path." }
    return $text.Substring($commentStart).Trim()
}

function ConvertTo-TomCatSpdxId {
    param([Parameter(Mandatory)][string]$Value)
    $sanitized = [regex]::Replace($Value, '[^A-Za-z0-9.-]', '-')
    return $sanitized.Trim('-')
}

function New-TomCatReleaseMetadata {
    param(
        [Parameter(Mandatory)]
        [string]$RepositoryRoot,

        [Parameter(Mandatory)]
        [string]$DistPath,

        [Parameter(Mandatory)]
        [ValidateSet('editor', 'hub', 'both')]
        [string]$Target,

        [Parameter(Mandatory)]
        [string]$Version
    )

    $RepositoryRoot = (Resolve-Path -LiteralPath $RepositoryRoot -ErrorAction Stop).Path
    $DistPath = (Resolve-Path -LiteralPath $DistPath -ErrorAction Stop).Path
    $artifactNames = switch ($Target) {
        'editor' { @('TomCat.zip') }
        'hub' { @('TomCatHub.exe') }
        'both' { @('TomCat.zip', 'TomCatHub.exe') }
    }
    foreach ($name in $artifactNames) {
        $artifactPath = Join-Path $DistPath $name
        if (-not (Test-Path -LiteralPath $artifactPath -PathType Leaf)) {
            throw "Release artifact was not found: $artifactPath"
        }
    }

    $dependencies = @(Get-TomCatReleaseDependencies)
    $noticeParts = New-Object 'System.Collections.Generic.List[string]'
    [void]$noticeParts.Add('TomCat Engine - Third-Party Notices')
    [void]$noticeParts.Add("Release: $Version")
    [void]$noticeParts.Add('')
    [void]$noticeParts.Add('TomCat Engine license')
    [void]$noticeParts.Add(('=' * 78))
    [void]$noticeParts.Add((Get-Content -LiteralPath (Join-Path $RepositoryRoot 'LICENSE') -Raw -ErrorAction Stop).Trim())

    foreach ($dependency in $dependencies) {
        [void]$noticeParts.Add('')
        [void]$noticeParts.Add($dependency.Name)
        [void]$noticeParts.Add(('=' * 78))
        if ($dependency.Mode -eq 'None') {
            [void]$noticeParts.Add('No standalone license text is present in the vendored directory; license is recorded as NOASSERTION in the SBOM.')
            continue
        }
        $noticePath = Join-Path $RepositoryRoot $dependency.Notice
        if (-not (Test-Path -LiteralPath $noticePath -PathType Leaf)) {
            throw "Required third-party notice was not found: $noticePath"
        }
        [void]$noticeParts.Add((Get-TomCatNoticeText -Path $noticePath -Mode $dependency.Mode))
    }

    $utf8 = New-Object System.Text.UTF8Encoding($false)
    $noticesPath = Join-Path $DistPath 'THIRD_PARTY_NOTICES.txt'
    [System.IO.File]::WriteAllText($noticesPath, ($noticeParts -join "`r`n") + "`r`n", $utf8)

    $artifactRecords = @()
    foreach ($name in $artifactNames) {
        $path = Join-Path $DistPath $name
        $artifactRecords += [ordered]@{
            Name = $name
            Hash = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()
        }
    }
    $namespaceSeed = (($artifactRecords | ForEach-Object { $_.Name + ':' + $_.Hash }) -join '|')
    $seedBytes = [System.Text.Encoding]::UTF8.GetBytes("$Version|$namespaceSeed")
    $seedHasher = [System.Security.Cryptography.SHA256]::Create()
    try { $namespaceHash = ([System.BitConverter]::ToString($seedHasher.ComputeHash($seedBytes))).Replace('-', '').ToLowerInvariant() }
    finally { $seedHasher.Dispose() }

    $packages = New-Object 'System.Collections.Generic.List[object]'
    [void]$packages.Add([ordered]@{
        SPDXID = 'SPDXRef-Package-TomCat'
        name = 'TomCat Engine'
        versionInfo = $Version
        downloadLocation = 'NOASSERTION'
        filesAnalyzed = $false
        licenseConcluded = 'NOASSERTION'
        licenseDeclared = 'MIT'
        copyrightText = 'Copyright (c) 2026 chnnasn'
    })
    foreach ($dependency in $dependencies) {
        $package = [ordered]@{
            SPDXID = 'SPDXRef-Package-' + (ConvertTo-TomCatSpdxId -Value $dependency.Name)
            name = $dependency.Name
            downloadLocation = 'NOASSERTION'
            filesAnalyzed = $false
            licenseConcluded = 'NOASSERTION'
            licenseDeclared = $dependency.License
            copyrightText = 'NOASSERTION'
        }
        if ($dependency.Version) { $package.versionInfo = $dependency.Version }
        [void]$packages.Add($package)
    }

    $files = New-Object 'System.Collections.Generic.List[object]'
    $relationships = New-Object 'System.Collections.Generic.List[object]'
    foreach ($record in $artifactRecords) {
        $fileId = 'SPDXRef-File-' + (ConvertTo-TomCatSpdxId -Value $record.Name)
        [void]$files.Add([ordered]@{
            SPDXID = $fileId
            fileName = './' + $record.Name
            checksums = @([ordered]@{ algorithm = 'SHA256'; checksumValue = $record.Hash })
            licenseConcluded = 'NOASSERTION'
            copyrightText = 'NOASSERTION'
        })
        [void]$relationships.Add([ordered]@{
            spdxElementId = 'SPDXRef-Package-TomCat'
            relationshipType = 'CONTAINS'
            relatedSpdxElement = $fileId
        })
    }
    foreach ($dependency in $dependencies) {
        [void]$relationships.Add([ordered]@{
            spdxElementId = 'SPDXRef-Package-TomCat'
            relationshipType = 'DEPENDS_ON'
            relatedSpdxElement = 'SPDXRef-Package-' + (ConvertTo-TomCatSpdxId -Value $dependency.Name)
        })
    }

    $spdx = [ordered]@{
        spdxVersion = 'SPDX-2.3'
        dataLicense = 'CC0-1.0'
        SPDXID = 'SPDXRef-DOCUMENT'
        name = "TomCat Engine $Version release artifacts"
        documentNamespace = "https://tomcat-engine.invalid/spdx/$Version/$namespaceHash"
        creationInfo = [ordered]@{
            created = [DateTime]::UtcNow.ToString('yyyy-MM-ddTHH:mm:ssZ')
            creators = @('Tool: TomCat ReleaseArtifactTools.ps1')
        }
        documentDescribes = @('SPDXRef-Package-TomCat')
        packages = $packages.ToArray()
        files = $files.ToArray()
        relationships = $relationships.ToArray()
    }
    $sbomPath = Join-Path $DistPath 'TomCat.spdx.json'
    [System.IO.File]::WriteAllText($sbomPath, ($spdx | ConvertTo-Json -Depth 16), $utf8)

    $hashNames = @($artifactNames) + @('THIRD_PARTY_NOTICES.txt', 'TomCat.spdx.json')
    $sumLines = foreach ($name in $hashNames | Sort-Object) {
        $hash = (Get-FileHash -LiteralPath (Join-Path $DistPath $name) -Algorithm SHA256).Hash.ToLowerInvariant()
        "$hash  $($name.Replace('\', '/'))"
    }
    $sumsPath = Join-Path $DistPath 'SHA256SUMS'
    [System.IO.File]::WriteAllText($sumsPath, ($sumLines -join "`n") + "`n", $utf8)

    return [ordered]@{
        Checksums = $sumsPath
        Notices = $noticesPath
        Sbom = $sbomPath
    }
}
