[CmdletBinding()]
param(
    [string]$ManifestPath = (Join-Path $PSScriptRoot '..\Managed\TomCat.Managed\ComponentProxies.json'),
    [string]$OutputPath = (Join-Path $PSScriptRoot '..\Managed\TomCat.Managed\ComponentProxy.Generated.cs'),
    [switch]$Check
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function ConvertTo-ComponentId {
    param(
        [Parameter(Mandatory = $true)][string]$Text,
        [Parameter(Mandatory = $true)][string]$Context
    )
    if ($Text -match '^0x[0-9a-fA-F]{1,16}$') {
        return [Convert]::ToUInt64($Text.Substring(2), 16)
    }
    if ($Text -match '^[0-9]+$') {
        return [Convert]::ToUInt64($Text, 10)
    }
    throw "$Context has invalid unsigned 64-bit ID '$Text'."
}

$manifest = Get-Content -LiteralPath $ManifestPath -Raw | ConvertFrom-Json
if ($manifest.schemaVersion -ne 1) {
    throw "Unsupported component proxy manifest version '$($manifest.schemaVersion)'."
}

$entries = @()
$managedTypes = @{}
$typeIds = @{}
foreach ($component in $manifest.components) {
    $managedType = [string]$component.managedType
    $stableName = [string]$component.stableName
    $typeIdText = [string]$component.typeId
    if ($managedType -notmatch '^[A-Za-z_][A-Za-z0-9_]*$') {
        throw "Invalid managed component type '$managedType'."
    }
    if ([string]::IsNullOrWhiteSpace($stableName)) {
        throw "Component '$managedType' has no stable name."
    }
    if ($typeIdText -notmatch '^0x[0-9a-fA-F]{16}$') {
        throw "Component '$managedType' has invalid 64-bit type ID '$typeIdText'."
    }
    $typeId = ConvertTo-ComponentId $typeIdText "Component '$managedType'"
    if ($typeId -eq 0) {
        throw "Component '$managedType' uses the reserved zero type ID."
    }
    if ($managedTypes.ContainsKey($managedType)) {
        throw "Duplicate managed component type '$managedType'."
    }
    if ($typeIds.ContainsKey($typeId)) {
        throw "Duplicate component type ID '$typeIdText'."
    }
    $managedTypes[$managedType] = $true
    $typeIds[$typeId] = $true
    $generateProxy = $false
    if ($component.PSObject.Properties.Name -contains 'generateProxy') {
        $generateProxy = [bool]$component.generateProxy
    }
    $properties = @()
    $propertyNames = @{}
    $propertyIds = @{}
    if ($component.PSObject.Properties.Name -contains 'properties') {
        foreach ($property in $component.properties) {
            $propertyName = [string]$property.name
            $propertyIdText = [string]$property.propertyId
            $kind = [string]$property.kind
            $conversion = if ($property.PSObject.Properties.Name -contains 'conversion') {
                [string]$property.conversion
            } else {
                'Direct'
            }
            $propertyManagedType =
                if ($property.PSObject.Properties.Name -contains 'managedType') {
                    [string]$property.managedType
                } else {
                    ''
                }
            if ($propertyName -notmatch '^[A-Za-z_][A-Za-z0-9_]*$') {
                throw "Component '$managedType' has invalid property name '$propertyName'."
            }
            $propertyId = ConvertTo-ComponentId $propertyIdText "Component '$managedType' property '$propertyName'"
            if ($propertyId -eq 0) {
                throw "Component '$managedType' property '$propertyName' uses reserved ID zero."
            }
            if ($propertyNames.ContainsKey($propertyName)) {
                throw "Component '$managedType' has duplicate property '$propertyName'."
            }
            if ($propertyIds.ContainsKey($propertyId)) {
                throw "Component '$managedType' has duplicate property ID '$propertyIdText'."
            }
            if ($kind -notin @('Bool', 'Int32', 'Int64', 'UInt32', 'UInt64',
				'Float', 'Double', 'String', 'Vector2', 'Vector3', 'Vector4',
				'Color')) {
                throw "Component '$managedType' property '$propertyName' has unsupported kind '$kind'."
            }
            if ($conversion -notin @('Direct', 'Enum', 'AssetRef')) {
                throw "Component '$managedType' property '$propertyName' has unsupported conversion '$conversion'."
            }
            if ($conversion -ne 'Direct' -and
                [string]::IsNullOrWhiteSpace($propertyManagedType)) {
                throw "Component '$managedType' property '$propertyName' conversion '$conversion' requires managedType."
            }
            $propertyNames[$propertyName] = $true
            $propertyIds[$propertyId] = $true
            $properties += [pscustomobject]@{
                Name = $propertyName
                PropertyId = $propertyId
                Kind = $kind
                Conversion = $conversion
                ManagedType = $propertyManagedType
            }
        }
    }
    if (-not $generateProxy -and $properties.Count -ne 0) {
        throw "Component '$managedType' declares properties but generateProxy is false."
    }
    $entries += [pscustomobject]@{
        ManagedType = $managedType
        StableName = $stableName
        TypeId = $typeId
        GenerateProxy = $generateProxy
        Properties = @($properties)
    }
}

$entries = @($entries | Sort-Object TypeId, ManagedType)
$lines = [System.Collections.Generic.List[string]]::new()
$lines.Add('// <auto-generated />')
$lines.Add('// Generated by Scripts/Generate-Component-Proxies.ps1 from ComponentProxies.json.')
$lines.Add('#nullable enable')
$lines.Add('')
$lines.Add('namespace TomCat;')
$lines.Add('')
$lines.Add('internal static class GeneratedRegisteredComponentProxies')
$lines.Add('{')
$lines.Add('    private readonly record struct Entry(')
$lines.Add('        ulong TypeId, Func<Entity, IEntityComponent> Factory);')
$lines.Add('')
$lines.Add('    private static readonly IReadOnlyDictionary<Type, Entry> s_entries =')
$lines.Add('        new Dictionary<Type, Entry>')
$lines.Add('        {')
foreach ($entry in $entries) {
    $typeIdLiteral = '0x{0:x16}UL' -f [uint64]$entry.TypeId
    $lines.Add("            [typeof($($entry.ManagedType))] = new($typeIdLiteral,")
    $lines.Add("                static entity => new $($entry.ManagedType)(entity)),")
}
$lines.Add('        };')
$lines.Add('')
$lines.Add('    internal static bool TryCreate<T>(Entity entity, out T component)')
$lines.Add('        where T : class, IEntityComponent')
$lines.Add('    {')
$lines.Add('        if (s_entries.TryGetValue(typeof(T), out Entry entry))')
$lines.Add('        {')
$lines.Add('            component = (T)entry.Factory(entity);')
$lines.Add('            return true;')
$lines.Add('        }')
$lines.Add('        component = null!;')
$lines.Add('        return false;')
$lines.Add('    }')
$lines.Add('')
$lines.Add('    internal static bool TryGetTypeId<T>(out ulong typeId)')
$lines.Add('        where T : class, IEntityComponent')
$lines.Add('    {')
$lines.Add('        if (s_entries.TryGetValue(typeof(T), out Entry entry))')
$lines.Add('        {')
$lines.Add('            typeId = entry.TypeId;')
$lines.Add('            return true;')
$lines.Add('        }')
$lines.Add('        typeId = 0;')
$lines.Add('        return false;')
$lines.Add('    }')
$lines.Add('}')

foreach ($entry in $entries | Where-Object GenerateProxy) {
    $typeIdLiteral = '0x{0:x16}UL' -f [uint64]$entry.TypeId
    $lines.Add('')
    $lines.Add("[RegisteredComponent($typeIdLiteral)]")
    $lines.Add("public sealed partial class $($entry.ManagedType) : IEntityComponent")
    $lines.Add('{')
    $lines.Add("    public const ulong RegisteredTypeId = $typeIdLiteral;")
    $lines.Add('')
    $lines.Add("    internal $($entry.ManagedType)(Entity entity) => Entity = entity;")
    $lines.Add('    public Entity Entity { get; }')
    foreach ($property in $entry.Properties) {
        $propertyIdLiteral = '0x{0:x16}UL' -f [uint64]$property.PropertyId
        $nativeType = switch ($property.Kind) {
            'Bool' { 'bool' }
            'Int32' { 'int' }
			'Int64' { 'long' }
            'UInt32' { 'uint' }
            'UInt64' { 'ulong' }
            'Float' { 'float' }
			'Double' { 'double' }
			'String' { 'string' }
            'Vector2' { 'Vector2' }
			'Vector3' { 'Vector3' }
            'Vector4' { 'Vector4' }
            'Color' { 'Color' }
        }
        $managedType = if ([string]::IsNullOrWhiteSpace($property.ManagedType)) {
            $nativeType
        } else {
            $property.ManagedType
        }
        $bridgeSuffix = $property.Kind
		$getter = 'RegisteredComponentProperties.Get{0}(Entity, RegisteredTypeId, {1})' -f $bridgeSuffix, $propertyIdLiteral
        $setterValue = 'value'
        if ($property.Conversion -eq 'Enum') {
            $getter = "($managedType)$getter"
            $setterValue = if ($property.Kind -eq 'Int32') {
                '(int)value'
            } elseif ($property.Kind -eq 'UInt32') {
                '(uint)value'
            } else {
                throw "Enum property '$managedType.$($property.Name)' must use Int32 or UInt32."
            }
        } elseif ($property.Conversion -eq 'AssetRef') {
            if ($property.Kind -ne 'UInt64') {
                throw "AssetRef property '$managedType.$($property.Name)' must use UInt64."
            }
            $getter = "new($getter)"
            $setterValue = 'value.Handle'
        }
        $lines.Add('')
        $lines.Add("    public $managedType $($property.Name)")
        $lines.Add('    {')
        $lines.Add("        get => $getter;")
		$lines.Add("        set => RegisteredComponentProperties.Set$bridgeSuffix(Entity, RegisteredTypeId,")
		$lines.Add(('            {0}, {1});' -f $propertyIdLiteral, $setterValue))
        $lines.Add('    }')
    }
    $lines.Add('}')
}

$newline = [string][char]10
$content = [string]::Join($newline, $lines) + $newline
if ($Check) {
    if (-not (Test-Path -LiteralPath $OutputPath)) {
        throw "Generated proxy file is missing: $OutputPath"
    }
	# Git may materialize the generated source with CRLF on Windows while this
	# generator deliberately builds canonical LF content. Compare normalized text
	# so the stale check detects schema/code changes instead of checkout policy.
    $existing = [IO.File]::ReadAllText((Resolve-Path -LiteralPath $OutputPath)).Replace("`r`n", "`n").Replace("`r", "`n")
    if ($existing -cne $content) {
        throw 'ComponentProxy.Generated.cs is stale. Run Scripts/Generate-Component-Proxies.ps1.'
    }
    Write-Host 'Component proxy bindings are up to date.'
    exit 0
}

$resolvedOutput = [IO.Path]::GetFullPath($OutputPath)
[IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($resolvedOutput)) |
    Out-Null
[IO.File]::WriteAllText($resolvedOutput, $content,
    [Text.UTF8Encoding]::new($false))
Write-Host "Generated $resolvedOutput"
