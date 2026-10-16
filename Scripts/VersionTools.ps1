function Get-TomCatVersionInfo {
    param(
        [Parameter(Mandatory)]
        [string]$RepositoryRoot
    )

    $headerPath = Join-Path $RepositoryRoot "TomCat\src\TomCat\Core\Version.h"
    if (-not (Test-Path -LiteralPath $headerPath -PathType Leaf)) {
        throw "TomCat version source was not found: $headerPath"
    }
    $header = Get-Content -LiteralPath $headerPath -Raw

    function Read-StringValue([string]$name) {
        $match = [regex]::Match(
            $header,
            ('(?m)\b' + [regex]::Escape($name) + '\s*=\s*"([^"]+)"\s*;')
        )
        if (-not $match.Success) {
            throw "Could not read $name from $headerPath."
        }
        return $match.Groups[1].Value
    }

    function Read-UInt32Value([string]$name) {
        $match = [regex]::Match(
            $header,
            ('(?m)\b' + [regex]::Escape($name) + '\s*=\s*([0-9]+)\s*;')
        )
        if (-not $match.Success) {
            throw "Could not read $name from $headerPath."
        }
        return [uint32]$match.Groups[1].Value
    }

    return [pscustomobject]@{
        ProductVersion = Read-StringValue "ProductVersion"
        EngineBuildID = Read-StringValue "EngineBuildID"
        ProjectFormat = Read-UInt32Value "ProjectFormatCurrent"
        TcpakVersion = Read-UInt32Value "TcpakFormatCurrent"
        PlayerTemplateSchemaVersion = Read-UInt32Value "PlayerTemplateFormatCurrent"
        PlayerAbiVersion = Read-UInt32Value "PlayerAbiCurrent"
    }
}

function Assert-TomCatPlayerTemplateVersion {
    param(
        [Parameter(Mandatory)][AllowEmptyString()][string]$VersionOutput,
        [Parameter(Mandatory)]$VersionInfo
    )

    # Read the executable's own compatibility stamp. A source-only version bump
    # must not relabel an older Player binary as a compatible new template.
    $reported = [regex]::Match($VersionOutput,
        '(?m)^TomCatPlayer (?<product>\S+) build=(?<build>\S+) tcpak=(?<tcpak>\d+) abi=(?<abi>\d+) template=(?<template>\d+)(?:\s|$)')
    if (-not $reported.Success -or
        $reported.Groups['product'].Value -ne [string]$VersionInfo.ProductVersion -or
        $reported.Groups['build'].Value -ne [string]$VersionInfo.EngineBuildID -or
        $reported.Groups['tcpak'].Value -ne [string]$VersionInfo.TcpakVersion -or
        $reported.Groups['abi'].Value -ne [string]$VersionInfo.PlayerAbiVersion -or
        $reported.Groups['template'].Value -ne [string]$VersionInfo.PlayerTemplateSchemaVersion) {
        throw 'TomCatPlayer binary compatibility does not match the current source. Rebuild it with Scripts/Build-PlayerTemplate.ps1 -Build.'
    }
}
