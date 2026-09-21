# Helpers shared by the local and GitHub Actions Enigma Virtual Box packaging
# scripts.  EVB project files use a legacy, non-standard root element (`<>`),
# so the package tree is updated as text instead of being parsed as XML.

function ConvertTo-EvbXmlText {
    param(
        [AllowNull()]
        [string]$Value
    )

    if ($null -eq $Value) { return "" }
    return [System.Security.SecurityElement]::Escape($Value)
}

function Get-EvbFileTreeXml {
    param(
        [Parameter(Mandatory)]
        [string]$RootPath,

        [string]$Indent = "              ",

        [ValidateRange(0, 3)]
        [int]$FileAction = 0,

        [ValidateRange(0, 3)]
        [int]$DirectoryAction = 3,

        [string[]]$ExcludeRelativePaths = @(),

        [string]$RelativePathPrefix = ""
    )

    $items = Get-ChildItem -LiteralPath $RootPath -Force -ErrorAction Stop |
        Sort-Object @{ Expression = { -not $_.PSIsContainer }; Ascending = $true }, Name
    $parts = New-Object 'System.Collections.Generic.List[string]'

    $normalizedExclusions = @($ExcludeRelativePaths | ForEach-Object {
        ([string]$_).Replace('\', '/').Trim('/')
    } | Where-Object { $_ })

    foreach ($item in $items) {
        $relativePath = if ([string]::IsNullOrEmpty($RelativePathPrefix)) {
            $item.Name
        } else {
            $RelativePathPrefix.TrimEnd('/', '\') + '/' + $item.Name
        }
        $relativePath = $relativePath.Replace('\', '/')
        $excluded = $false
        foreach ($candidate in $normalizedExclusions) {
            if ($relativePath.Equals($candidate,
                    [System.StringComparison]::OrdinalIgnoreCase) -or
                $relativePath.StartsWith($candidate + '/',
                    [System.StringComparison]::OrdinalIgnoreCase)) {
                $excluded = $true
                break
            }
        }
        if ($excluded) { continue }

        # Runtime/user state must always remain outside the virtual filesystem.
        # The root default imgui.ini is declared explicitly by each checked-in
        # EVB template; never pick up another layout or settings file while
        # recursively mirroring Packages.
        $isUserStateDirectory = $item.PSIsContainer -and
            ($item.Name -ieq "UserSettings" -or $item.Name -ieq "TomCatSettings")
        $isUserStateFile = -not $item.PSIsContainer -and
            ($item.Name -ieq "hub.json" -or $item.Name -ieq "editor.json" -or
             $item.Name -ieq "editor-layout.ini" -or
             $item.Name -ieq "imgui.ini" -or $item.Name -ieq "TomCat.log")
        if ($isUserStateDirectory -or $isUserStateFile) {
            Write-Warning "Excluded runtime/user settings from EVB: $($item.FullName)"
            continue
        }

        # Do not follow junctions/symlinks while walking build output.  Besides
        # avoiding loops, this keeps the EVB manifest inside the source tree.
        if (($item.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
            continue
        }

        $name = ConvertTo-EvbXmlText $item.Name
        if ($item.PSIsContainer) {
            $childIndent = $Indent + "  "
            $children = Get-EvbFileTreeXml -RootPath $item.FullName `
                -Indent $childIndent -FileAction $FileAction `
                -DirectoryAction $DirectoryAction `
                -ExcludeRelativePaths $normalizedExclusions `
                -RelativePathPrefix $relativePath

            [void]$parts.Add("$Indent<File>")
            [void]$parts.Add("$Indent  <Type>3</Type>")
            [void]$parts.Add("$Indent  <Name>$name</Name>")
            # EVB uses the Action field on directory nodes for the policy that
            # controls *new* children.  Action=0 means "new folders and files
            # become real", which would materialize Packages/cache when the
            # application writes its shader cache.  Keep package directories
            # fully virtual so a boxed executable stays self-contained.
            [void]$parts.Add("$Indent  <Action>$DirectoryAction</Action>")
            [void]$parts.Add("$Indent  <OverwriteDateTime>False</OverwriteDateTime>")
            [void]$parts.Add("$Indent  <OverwriteAttributes>False</OverwriteAttributes>")
            [void]$parts.Add("$Indent  <HideFromDialogs>0</HideFromDialogs>")
            [void]$parts.Add("$Indent  <Files>")
            if (-not [string]::IsNullOrEmpty($children)) {
                [void]$parts.Add($children)
            }
            [void]$parts.Add("$Indent  </Files>")
            [void]$parts.Add("$Indent</File>")
        } else {
            $path = ConvertTo-EvbXmlText $item.FullName

            [void]$parts.Add("$Indent<File>")
            [void]$parts.Add("$Indent  <Type>2</Type>")
            [void]$parts.Add("$Indent  <Name>$name</Name>")
            [void]$parts.Add("$Indent  <File>$path</File>")
            [void]$parts.Add("$Indent  <ActiveX>False</ActiveX>")
            [void]$parts.Add("$Indent  <ActiveXInstall>False</ActiveXInstall>")
            [void]$parts.Add("$Indent  <Action>$FileAction</Action>")
            [void]$parts.Add("$Indent  <OverwriteDateTime>False</OverwriteDateTime>")
            [void]$parts.Add("$Indent  <OverwriteAttributes>False</OverwriteAttributes>")
            [void]$parts.Add("$Indent  <PassCommandLine>False</PassCommandLine>")
            [void]$parts.Add("$Indent  <HideFromDialogs>0</HideFromDialogs>")
            [void]$parts.Add("$Indent</File>")
        }
    }

    return ($parts -join "`r`n")
}

function Set-EvbDirectoryTree {
    param(
        [Parameter(Mandatory)]
        [string]$TemplateText,

        [Parameter(Mandatory)]
        [string]$NodeName,

        [Parameter(Mandatory)]
        [string]$SourceDirectory,

        [ValidateRange(0, 3)]
        [int]$FileAction = 0,

        [ValidateRange(0, 3)]
        [int]$DirectoryAction = 3,

        [string[]]$ExcludeRelativePaths = @()
    )

    if (-not (Test-Path -LiteralPath $SourceDirectory -PathType Container)) {
        throw "EVB tree source directory not found: $SourceDirectory"
    }

    $escapedNodeName = [regex]::Escape((ConvertTo-EvbXmlText $NodeName))
    $nodePattern = "(?is)<File\b[^>]*>\s*<Type>\s*3\s*</Type>\s*<Name>\s*$escapedNodeName\s*</Name>"
    $nodeMatches = [regex]::Matches($TemplateText, $nodePattern)
    if ($nodeMatches.Count -ne 1) {
        throw "EVB template must contain exactly one Type=3 '$NodeName' node (found $($nodeMatches.Count))"
    }
    $nodeMarker = $nodeMatches[0]

    $actionPattern = "(?is)(<File\b[^>]*>\s*<Type>\s*3\s*</Type>\s*<Name>\s*$escapedNodeName\s*</Name>\s*<Action>)\s*\d+\s*(</Action>)"
    $actionMatch = [regex]::Match($TemplateText, $actionPattern)
    if (-not $actionMatch.Success) {
        throw "EVB template node '$NodeName' is missing its Action field"
    }
    $actionEvaluator = [System.Text.RegularExpressions.MatchEvaluator]{
        param([System.Text.RegularExpressions.Match]$Match)
        return $Match.Groups[1].Value + $DirectoryAction + $Match.Groups[2].Value
    }
    $TemplateText = [regex]::Replace(
        $TemplateText, $actionPattern, $actionEvaluator, 1)

    # Re-resolve the marker after replacing its Action because offsets may have
    # changed when a multi-digit or hand-edited value was normalized.
    $nodeMarker = [regex]::Match($TemplateText, $nodePattern)
    $openTag = "<Files>"
    $closeTag = "</Files>"
    $openIndex = $TemplateText.IndexOf(
        $openTag,
        $nodeMarker.Index + $nodeMarker.Length,
        [System.StringComparison]::OrdinalIgnoreCase
    )
    if ($openIndex -lt 0) {
        throw "EVB template node '$NodeName' is missing its <Files> element"
    }

    # Find the matching closing element while allowing arbitrary nested trees.
    $contentStart = $openIndex + $openTag.Length
    $cursor = $contentStart
    $depth = 1
    $contentEnd = -1
    while ($depth -gt 0) {
        $nextOpen = $TemplateText.IndexOf(
            $openTag, $cursor, [System.StringComparison]::OrdinalIgnoreCase)
        $nextClose = $TemplateText.IndexOf(
            $closeTag, $cursor, [System.StringComparison]::OrdinalIgnoreCase)
        if ($nextClose -lt 0) {
            throw "EVB template node '$NodeName' has an unterminated <Files> element"
        }
        if ($nextOpen -ge 0 -and $nextOpen -lt $nextClose) {
            $depth++
            $cursor = $nextOpen + $openTag.Length
        } else {
            $depth--
            if ($depth -eq 0) { $contentEnd = $nextClose }
            $cursor = $nextClose + $closeTag.Length
        }
    }

    $lineStart = $TemplateText.LastIndexOf("`n", $openIndex)
    if ($lineStart -lt 0) { $lineStart = 0 } else { $lineStart++ }
    $filesIndent = $TemplateText.Substring($lineStart, $openIndex - $lineStart)
    $entryIndent = $filesIndent + "  "
    $tree = Get-EvbFileTreeXml `
        -RootPath (Resolve-Path -LiteralPath $SourceDirectory).Path `
        -Indent $entryIndent -FileAction $FileAction `
        -DirectoryAction $DirectoryAction `
        -ExcludeRelativePaths $ExcludeRelativePaths
    $replacement = if ([string]::IsNullOrEmpty($tree)) {
        "`r`n$filesIndent"
    } else {
        "`r`n$tree`r`n$filesIndent"
    }

    return $TemplateText.Substring(0, $contentStart) +
        $replacement + $TemplateText.Substring($contentEnd)
}

function Assert-EvbUserStateExcluded {
    param(
        [Parameter(Mandatory)]
        [string]$TemplateText
    )

    $forbiddenNamePattern = '(?is)<Name>\s*(?:hub\.json|editor\.json|editor-layout\.ini|TomCat\.log|UserSettings|TomCatSettings)\s*</Name>'
    if ($TemplateText -match $forbiddenNamePattern) {
        throw "EVB project contains runtime/user state; logs, JSON, editor-layout.ini, UserSettings and TomCatSettings must remain external"
    }

    $fileSources = [regex]::Matches($TemplateText, '(?is)<File>\s*([^<]*?)\s*</File>')
    $layoutSources = New-Object 'System.Collections.Generic.List[string]'
    foreach ($sourceMatch in $fileSources) {
        $source = [System.Net.WebUtility]::HtmlDecode($sourceMatch.Groups[1].Value.Trim())
        $components = @($source -split '[\\/]' | Where-Object { $_ -ne '' })
        foreach ($component in $components) {
            if ($component -ieq "UserSettings" -or $component -ieq "TomCatSettings") {
                throw "EVB project contains runtime/user settings path: $source"
            }
        }
        if ($components.Count -gt 0) {
            $leaf = $components[$components.Count - 1]
            if ($leaf -ieq "hub.json" -or $leaf -ieq "editor.json" -or
                $leaf -ieq "editor-layout.ini" -or $leaf -ieq "TomCat.log") {
                throw "EVB project contains runtime/user settings file: $source"
            }
            if ($leaf -ieq "imgui.ini") {
                [void]$layoutSources.Add($source)
            }
        }
    }

    $layoutEntries = [regex]::Matches(
        $TemplateText,
        '(?is)<Name>\s*imgui\.ini\s*</Name>'
    )
    if ($layoutEntries.Count -ne 1) {
        throw "EVB project must contain exactly one packaged default imgui.ini (found $($layoutEntries.Count))"
    }
    if ($layoutSources.Count -ne 1 -or
        $layoutSources[0] -notmatch '(?i)(?:^|[\\/])(?:Editor[\\/]TomCatInut|Builder[\\/]Manager)[\\/]imgui\.ini$') {
        throw "EVB imgui.ini must come from the checked-in Editor or Hub default layout"
    }
}

function Resolve-EvbPackageDirectory {
    param(
        [Parameter(Mandatory)]
        [string]$SourceDir
    )

    $resolvedSource = Resolve-Path -LiteralPath $SourceDir -ErrorAction Stop
    $package = Get-ChildItem -LiteralPath $resolvedSource.Path -Directory -Force -ErrorAction Stop |
        Where-Object { $_.Name -ieq "Packages" } |
        Select-Object -First 1
    if ($null -eq $package) {
        throw "Packages directory not found under: $($resolvedSource.Path)"
    }
    return $package.FullName
}

function Set-EvbPackageTree {
    param(
        [Parameter(Mandatory)]
        [string]$TemplateText,

        [Parameter(Mandatory)]
        [string]$PackageSource,

        [string[]]$ExcludeRelativePaths = @()
    )

    return Set-EvbDirectoryTree -TemplateText $TemplateText `
        -NodeName "Packages" -SourceDirectory $PackageSource `
        -FileAction 0 -DirectoryAction 3 `
        -ExcludeRelativePaths $ExcludeRelativePaths
}

function Test-TomCatRuntimePathSegment {
    param([AllowEmptyString()][string]$Value)

    if ([string]::IsNullOrEmpty($Value) -or $Value -eq '.' -or $Value -eq '..' -or
        $Value.EndsWith(' ') -or $Value.EndsWith('.')) {
        return $false
    }
    $invalidCharacters = '<>:"/\|?*'
    foreach ($character in $Value.ToCharArray()) {
        $codePoint = [int]$character
        if ($codePoint -lt 0x20 -or $codePoint -eq 0x7f -or
            $invalidCharacters.IndexOf($character) -ge 0) {
            return $false
        }
    }
    $baseName = $Value.Split('.')[0].ToUpperInvariant()
    if ($baseName -in @('CON', 'PRN', 'AUX', 'NUL') -or
        $baseName -match '^(?:COM|LPT)[1-9]$') {
        return $false
    }
    return $true
}

function New-TomCatRuntimeManifest {
    param(
        [Parameter(Mandatory)]
        [string]$PayloadRoot,

        [Parameter(Mandatory)]
        [string]$EngineBuildId
    )

    $maximumManifestBytes = [uint64]4194304
    $maximumRuntimeFiles = 10000
    $maximumRuntimeFileBytes = [uint64]2147483648
    $maximumRuntimeBytes = [uint64]4294967296
    if ([System.Text.Encoding]::UTF8.GetByteCount($EngineBuildId) -gt 128 -or
        -not (Test-TomCatRuntimePathSegment -Value $EngineBuildId)) {
        throw "Engine build ID is not safe for a runtime payload: '$EngineBuildId'"
    }
    $payload = (Resolve-Path -LiteralPath $PayloadRoot -ErrorAction Stop).Path.TrimEnd('\', '/')
    $payloadPrefix = $payload + '\'
    $manifestPath = Join-Path $payload 'runtime-manifest.json'
    if (Test-Path -LiteralPath $manifestPath) {
        Remove-Item -LiteralPath $manifestPath -Force
    }

    $expectedRootEntries = @(
        'Managed',
        'Packages',
        'TomCatCLI.exe',
        'msvcp140.dll',
        'shaderc_shared.dll',
        'assimp-vc143-mt.dll',
        'vcruntime140.dll',
        'vcruntime140_1.dll'
    ) | Sort-Object
    $actualRootEntries = @(Get-ChildItem -LiteralPath $payload -Force |
        Select-Object -ExpandProperty Name | Sort-Object)
    $rootDifference = @(Compare-Object -ReferenceObject $expectedRootEntries `
        -DifferenceObject $actualRootEntries)
    if ($rootDifference.Count -ne 0) {
        throw "Runtime payload root does not match its strict whitelist: $($rootDifference | Out-String)"
    }

    $managedRoot = Join-Path $payload 'Managed'
    $expectedManagedFiles = @(
        'TomCat.Managed.dll',
        'TomCat.ScriptGenerator.dll',
        'TomCat.ScriptHost.deps.json',
        'TomCat.ScriptHost.dll',
        'TomCat.ScriptHost.runtimeconfig.json'
    ) | Sort-Object
    $actualManagedEntries = @(Get-ChildItem -LiteralPath $managedRoot -Force |
        Select-Object -ExpandProperty Name | Sort-Object)
    $managedDifference = @(Compare-Object -ReferenceObject $expectedManagedFiles `
        -DifferenceObject $actualManagedEntries)
    if ($managedDifference.Count -ne 0) {
        throw "Runtime Managed directory does not match its strict whitelist: $($managedDifference | Out-String)"
    }

    $templateRoot = Join-Path $payload 'Packages\PlayerTemplates\win-x64'
    foreach ($requiredPath in @(
            'template.json',
            'TomCatPlayer.exe',
            'Managed\TomCat.Managed.dll',
            'dotnet\host\fxr')) {
        $candidate = Join-Path $templateRoot $requiredPath
        if (-not (Test-Path -LiteralPath $candidate)) {
            throw "Runtime Player template is missing required content: $candidate"
        }
    }

    $allEntries = @(Get-ChildItem -LiteralPath $payload -Recurse -Force)
    $reparsePoints = @($allEntries | Where-Object {
        ($_.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0
    })
    if ($reparsePoints.Count -ne 0) {
        throw "Runtime payload must not contain reparse points: $($reparsePoints.FullName -join ', ')"
    }
    $pdbFiles = @($allEntries | Where-Object {
        -not $_.PSIsContainer -and $_.Extension -ieq '.pdb'
    })
    if ($pdbFiles.Count -ne 0) {
        throw "Runtime payload must not contain PDB files: $($pdbFiles.FullName -join ', ')"
    }

    $records = New-Object 'System.Collections.Generic.List[object]'
    $payloadFiles = @($allEntries | Where-Object { -not $_.PSIsContainer } |
        Sort-Object FullName)
    if ($payloadFiles.Count -gt $maximumRuntimeFiles) {
        throw "Runtime payload contains more than $maximumRuntimeFiles files."
    }
    $runtimeBytes = [uint64]0
    $pathKeys = [System.Collections.Generic.HashSet[string]]::new(
        [System.StringComparer]::OrdinalIgnoreCase)
    foreach ($file in $payloadFiles) {
        if (-not $file.FullName.StartsWith(
                $payloadPrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
            throw "Runtime payload file escaped its root: $($file.FullName)"
        }
        $relativePath = $file.FullName.Substring($payloadPrefix.Length).Replace('\', '/')
        if ([string]::IsNullOrWhiteSpace($relativePath) -or
            $relativePath.StartsWith('/') -or
            [System.Text.Encoding]::UTF8.GetByteCount($relativePath) -gt 4096 -or
            @($relativePath.Split('/') | Where-Object {
                -not (Test-TomCatRuntimePathSegment -Value $_)
            }).Count -ne 0 -or
            -not $pathKeys.Add($relativePath)) {
            throw "Runtime payload contains an unsafe relative path: $relativePath"
        }
        $fileLength = [uint64]$file.Length
        if ($fileLength -gt $maximumRuntimeFileBytes -or
            $runtimeBytes -gt $maximumRuntimeBytes - $fileLength) {
            throw "Runtime payload file or aggregate size exceeds its limit: $relativePath"
        }
        $runtimeBytes += $fileLength
        [void]$records.Add([ordered]@{
            path = $relativePath
            size = [long]$fileLength
            sha256 = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
        })
    }
    if ($records.Count -eq 0) {
        throw 'Runtime payload manifest cannot be empty.'
    }

    $manifest = [ordered]@{
        schemaVersion = 1
        engineBuildId = $EngineBuildId
        files = $records.ToArray()
    }
    $encoding = New-Object System.Text.UTF8Encoding($false)
    $manifestJson = $manifest | ConvertTo-Json -Depth 6
    if ([uint64]$encoding.GetByteCount($manifestJson) -gt $maximumManifestBytes) {
        throw "Runtime manifest exceeds its $maximumManifestBytes-byte limit."
    }
    [System.IO.File]::WriteAllText($manifestPath, $manifestJson, $encoding)
    return $manifestPath
}

function Set-EvbProperty {
    param(
        [Parameter(Mandatory)]
        [string]$TemplateText,

        [Parameter(Mandatory)]
        [string]$ElementName,

        [Parameter(Mandatory)]
        [string]$Value
    )

    $escapedValue = ConvertTo-EvbXmlText $Value
    $pattern = "(?is)(<$([regex]::Escape($ElementName))>).*?(</$([regex]::Escape($ElementName))>)"
    $matches = [regex]::Matches($TemplateText, $pattern)
    if ($matches.Count -ne 1) {
        throw "EVB template must contain exactly one <$ElementName> element (found $($matches.Count))"
    }
    $evaluator = [System.Text.RegularExpressions.MatchEvaluator]{
        param([System.Text.RegularExpressions.Match]$Match)
        return $Match.Groups[1].Value + $escapedValue + $Match.Groups[2].Value
    }
    return [regex]::Replace($TemplateText, $pattern, $evaluator)
}

function Write-EvbProject {
    param(
        [Parameter(Mandatory)]
        [string]$Path,

        [Parameter(Mandatory)]
        [string]$Text
    )

    Assert-EvbUserStateExcluded -TemplateText $Text
    $encoding = New-Object System.Text.UTF8Encoding($true)
    [System.IO.File]::WriteAllText($Path, $Text, $encoding)
}
