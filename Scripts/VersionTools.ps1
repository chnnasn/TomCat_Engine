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
