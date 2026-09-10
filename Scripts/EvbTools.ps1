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

        [string]$Indent = "              "
    )

    $items = Get-ChildItem -LiteralPath $RootPath -Force -ErrorAction Stop |
        Sort-Object @{ Expression = { -not $_.PSIsContainer }; Ascending = $true }, Name
    $parts = New-Object 'System.Collections.Generic.List[string]'

    foreach ($item in $items) {
        # Runtime/user state must always remain outside the virtual filesystem.
        # The root default imgui.ini is declared explicitly by each checked-in
        # EVB template; never pick up another layout or settings file while
        # recursively mirroring Packages.
        $isUserStateDirectory = $item.PSIsContainer -and
            ($item.Name -ieq "UserSettings" -or $item.Name -ieq "TomCatSettings")
        $isUserStateFile = -not $item.PSIsContainer -and
            ($item.Name -ieq "hub.json" -or $item.Name -ieq "editor.json" -or
             $item.Name -ieq "editor-layout.ini" -or
             $item.Name -ieq "imgui.ini")
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
            $children = Get-EvbFileTreeXml -RootPath $item.FullName -Indent $childIndent

            [void]$parts.Add("$Indent<File>")
            [void]$parts.Add("$Indent  <Type>3</Type>")
            [void]$parts.Add("$Indent  <Name>$name</Name>")
            # EVB uses the Action field on directory nodes for the policy that
            # controls *new* children.  Action=0 means "new folders and files
            # become real", which would materialize Packages/cache when the
            # application writes its shader cache.  Keep package directories
            # fully virtual so a boxed executable stays self-contained.
            [void]$parts.Add("$Indent  <Action>3</Action>")
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
            [void]$parts.Add("$Indent  <Action>0</Action>")
            [void]$parts.Add("$Indent  <OverwriteDateTime>False</OverwriteDateTime>")
            [void]$parts.Add("$Indent  <OverwriteAttributes>False</OverwriteAttributes>")
            [void]$parts.Add("$Indent  <PassCommandLine>False</PassCommandLine>")
            [void]$parts.Add("$Indent  <HideFromDialogs>0</HideFromDialogs>")
            [void]$parts.Add("$Indent</File>")
        }
    }

    return ($parts -join "`r`n")
}

function Assert-EvbUserStateExcluded {
    param(
        [Parameter(Mandatory)]
        [string]$TemplateText
    )

    $forbiddenNamePattern = '(?is)<Name>\s*(?:hub\.json|editor\.json|editor-layout\.ini|UserSettings|TomCatSettings)\s*</Name>'
    if ($TemplateText -match $forbiddenNamePattern) {
        throw "EVB project contains runtime/user settings; JSON, editor-layout.ini, UserSettings and TomCatSettings must remain external"
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
                $leaf -ieq "editor-layout.ini") {
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
        [string]$PackageSource
    )

    if (-not (Test-Path -LiteralPath $PackageSource -PathType Container)) {
        throw "Packages directory not found: $PackageSource"
    }

    # Locate the Packages folder node.  The root of an EVB file is `<>`, so a
    # normal [xml] cast cannot be used here.  Its Action controls how files
    # created below the virtual folder are handled; force it to the fully
    # virtual mode before replacing the generated children.
    $packageActionPattern = '(?is)(<File\b[^>]*>\s*<Type>\s*3\s*</Type>\s*<Name>\s*Packages\s*</Name>\s*<Action>)\s*\d+\s*(</Action>)'
    $packageActionMatch = [regex]::Match($TemplateText, $packageActionPattern)
    if (-not $packageActionMatch.Success) {
        throw "EVB template does not contain an Action field for the Packages node"
    }
    $packageActionEvaluator = [System.Text.RegularExpressions.MatchEvaluator]{
        param([System.Text.RegularExpressions.Match]$Match)
        return $Match.Groups[1].Value + "3" + $Match.Groups[2].Value
    }
    $TemplateText = [regex]::Replace($TemplateText, $packageActionPattern, $packageActionEvaluator, 1)

    $packageMarker = [regex]::Match(
        $TemplateText,
        '(?is)<File\b[^>]*>\s*<Type>\s*3\s*</Type>\s*<Name>\s*Packages\s*</Name>'
    )
    if (-not $packageMarker.Success) {
        throw "EVB template does not contain a Type=3 Packages node"
    }

    $openTag = "<Files>"
    $closeTag = "</Files>"
    $openIndex = $TemplateText.IndexOf(
        $openTag,
        $packageMarker.Index + $packageMarker.Length,
        [System.StringComparison]::OrdinalIgnoreCase
    )
    if ($openIndex -lt 0) {
        throw "Packages node is missing its <Files> element"
    }

    # Find the matching closing </Files>, accounting for nested directories.
    $contentStart = $openIndex + $openTag.Length
    $cursor = $contentStart
    $depth = 1
    $contentEnd = -1
    while ($depth -gt 0) {
        $nextOpen = $TemplateText.IndexOf(
            $openTag,
            $cursor,
            [System.StringComparison]::OrdinalIgnoreCase
        )
        $nextClose = $TemplateText.IndexOf(
            $closeTag,
            $cursor,
            [System.StringComparison]::OrdinalIgnoreCase
        )
        if ($nextClose -lt 0) {
            throw "Packages node has an unterminated <Files> element"
        }

        if ($nextOpen -ge 0 -and $nextOpen -lt $nextClose) {
            $depth++
            $cursor = $nextOpen + $openTag.Length
        } else {
            $depth--
            if ($depth -eq 0) {
                $contentEnd = $nextClose
            }
            $cursor = $nextClose + $closeTag.Length
        }
    }

    $lineStart = $TemplateText.LastIndexOf("`n", $openIndex)
    if ($lineStart -lt 0) { $lineStart = 0 } else { $lineStart++ }
    $filesIndent = $TemplateText.Substring($lineStart, $openIndex - $lineStart)
    $entryIndent = $filesIndent + "  "

    $tree = Get-EvbFileTreeXml -RootPath (Resolve-Path -LiteralPath $PackageSource).Path -Indent $entryIndent
    if ([string]::IsNullOrEmpty($tree)) {
        $replacement = "`r`n$filesIndent"
    } else {
        $replacement = "`r`n$tree`r`n$filesIndent"
    }

    return $TemplateText.Substring(0, $contentStart) +
        $replacement +
        $TemplateText.Substring($contentEnd)
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
