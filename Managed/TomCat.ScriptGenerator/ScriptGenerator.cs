using System.Collections.Immutable;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using System.Text.Json.Serialization;
using Microsoft.CodeAnalysis;
using Microsoft.CodeAnalysis.CSharp;
using Microsoft.CodeAnalysis.CSharp.Syntax;
using Microsoft.CodeAnalysis.Text;

namespace TomCat.ScriptGenerator;

[Generator(LanguageNames.CSharp)]
public sealed class ScriptGenerator : IIncrementalGenerator
{
    private const string BehaviourMetadataName = "TomCat.TomCatBehaviour";

    private static readonly DiagnosticDescriptor MissingAssets = Error("TCG001",
        "ScriptAssets.json is missing",
        "The project must provide exactly one ScriptAssets.json AdditionalFile");
    private static readonly DiagnosticDescriptor InvalidAssets = Error("TCG002",
        "ScriptAssets.json is invalid", "ScriptAssets.json is invalid: {0}");
    private static readonly DiagnosticDescriptor UnmappedScript = Error("TCG003",
        "Script has no AssetHandle", "TomCatBehaviour '{0}' is not mapped in ScriptAssets.json");
    private static readonly DiagnosticDescriptor MultipleScripts = Error("TCG004",
        "Only one script is allowed per file", "Source file '{0}' contains {1} TomCatBehaviour types");
    private static readonly DiagnosticDescriptor FileNameMismatch = Error("TCG005",
        "Script file and class names differ", "Script class '{0}' must be declared in '{0}.cs'");
    private static readonly DiagnosticDescriptor InvalidScriptType = Error("TCG006",
        "Script type is not mountable",
        "Script '{0}' must be top-level, non-abstract, non-generic, and declared in one source file");
    private static readonly DiagnosticDescriptor MissingConstructor = Error("TCG007",
        "Script needs a parameterless constructor", "Script '{0}' must have a parameterless constructor");
    private static readonly DiagnosticDescriptor UnsupportedField = Error("TCG008",
        "Serialized field type is unsupported", "Serialized field '{0}.{1}' has unsupported type '{2}'");
    private static readonly DiagnosticDescriptor DuplicateAsset = Error("TCG009",
        "Duplicate script AssetHandle", "AssetHandle {0} maps to more than one TomCatBehaviour");
    private static readonly DiagnosticDescriptor UnsupportedAssetMarker = Error("TCG010",
        "AssetRef marker type is unsupported",
        "Serialized field '{0}.{1}' uses unsupported AssetRef marker '{2}'; use a built-in TomCat asset marker");

    public void Initialize(IncrementalGeneratorInitializationContext context)
    {
        IncrementalValueProvider<ImmutableArray<AdditionalText>> assets = context.AdditionalTextsProvider
            .Where(static file => string.Equals(Path.GetFileName(file.Path), "ScriptAssets.json",
                StringComparison.OrdinalIgnoreCase))
            .Collect();
        IncrementalValueProvider<(Compilation Left, ImmutableArray<AdditionalText> Right)> input =
            context.CompilationProvider.Combine(assets);
        context.RegisterSourceOutput(input, static (production, value) =>
            Execute(value.Left, value.Right, production));
    }

    private static void Execute(Compilation compilation, ImmutableArray<AdditionalText> assetFiles,
        SourceProductionContext context)
    {
        if (assetFiles.Length != 1)
        {
            context.ReportDiagnostic(Diagnostic.Create(MissingAssets, Location.None));
            EmitManifest(context, []);
            return;
        }

        Dictionary<string, ulong> assets;
        try
        {
            assets = ParseAssets(assetFiles[0], context.CancellationToken);
        }
        catch (Exception exception)
        {
            context.ReportDiagnostic(Diagnostic.Create(InvalidAssets, Location.None, exception.Message));
            EmitManifest(context, []);
            return;
        }

        INamedTypeSymbol? behaviour = compilation.GetTypeByMetadataName(BehaviourMetadataName);
        if (behaviour is null)
        {
            context.ReportDiagnostic(Diagnostic.Create(InvalidAssets, Location.None,
                $"the compilation does not reference {BehaviourMetadataName}"));
            EmitManifest(context, []);
            return;
        }

        var candidatesByTree = new Dictionary<SyntaxTree, List<INamedTypeSymbol>>();
        var seenSymbols = new HashSet<INamedTypeSymbol>(SymbolEqualityComparer.Default);
        foreach (SyntaxTree tree in compilation.SyntaxTrees)
        {
            SemanticModel model = compilation.GetSemanticModel(tree);
            foreach (ClassDeclarationSyntax declaration in tree.GetRoot(context.CancellationToken)
                         .DescendantNodes().OfType<ClassDeclarationSyntax>())
            {
                if (model.GetDeclaredSymbol(declaration, context.CancellationToken) is not INamedTypeSymbol symbol ||
                    !seenSymbols.Add(symbol) || !DerivesFrom(symbol, behaviour))
                    continue;
                if (!candidatesByTree.TryGetValue(tree, out List<INamedTypeSymbol>? list))
                    candidatesByTree.Add(tree, list = []);
                list.Add(symbol);
            }
        }

        var manifests = new List<ManifestScript>();
        var usedAssets = new HashSet<ulong>();
        foreach ((SyntaxTree tree, List<INamedTypeSymbol> candidates) in candidatesByTree)
        {
            string fileName = Path.GetFileName(tree.FilePath);
            if (candidates.Count != 1)
            {
                context.ReportDiagnostic(Diagnostic.Create(MultipleScripts,
                    candidates[0].Locations.FirstOrDefault(), fileName, candidates.Count));
                continue;
            }

            INamedTypeSymbol script = candidates[0];
            Location location = script.Locations.FirstOrDefault() ?? Location.None;
            ulong assetHandle = FindAssetHandle(tree.FilePath, assets);
            if (assetHandle == 0)
            {
                context.ReportDiagnostic(Diagnostic.Create(UnmappedScript, location,
                    script.ToDisplayString()));
                continue;
            }
            if (!usedAssets.Add(assetHandle))
            {
                context.ReportDiagnostic(Diagnostic.Create(DuplicateAsset, location, assetHandle));
                continue;
            }

            if (script.IsAbstract || script.IsGenericType || script.ContainingType is not null ||
                script.DeclaringSyntaxReferences.Length != 1)
            {
                context.ReportDiagnostic(Diagnostic.Create(InvalidScriptType, location,
                    script.ToDisplayString()));
                continue;
            }
            if (!string.Equals(Path.GetFileNameWithoutExtension(tree.FilePath), script.Name,
                    StringComparison.Ordinal))
            {
                context.ReportDiagnostic(Diagnostic.Create(FileNameMismatch, location, script.Name));
                continue;
            }
            if (!script.InstanceConstructors.Any(static constructor =>
                    !constructor.IsStatic && constructor.Parameters.Length == 0))
            {
                context.ReportDiagnostic(Diagnostic.Create(MissingConstructor, location,
                    script.ToDisplayString()));
                continue;
            }

            List<ManifestField> fields = BuildFields(script, assetHandle, context);
            manifests.Add(new ManifestScript
            {
                AssetHandle = assetHandle,
                TypeName = script.ToDisplayString(SymbolDisplayFormat.CSharpErrorMessageFormat),
                ExecutionOrder = ReadExecutionOrder(script),
                DisallowMultiple = HasAttribute(script, "TomCat.DisallowMultipleComponentAttribute"),
                Lifecycle = ReadLifecycle(script),
                Fields = fields
            });
        }

        manifests.Sort(static (left, right) => left.AssetHandle.CompareTo(right.AssetHandle));
        EmitManifest(context, manifests);
    }

    private static Dictionary<string, ulong> ParseAssets(AdditionalText file,
        CancellationToken cancellationToken)
    {
        SourceText text = file.GetText(cancellationToken) ??
            throw new InvalidDataException("file could not be read");
        using JsonDocument document = JsonDocument.Parse(text.ToString());
        JsonElement root = document.RootElement;
        if (root.ValueKind != JsonValueKind.Object || root.GetProperty("version").GetUInt32() != 1)
            throw new InvalidDataException("version must be 1");
        JsonElement entries = root.GetProperty("assets");
        if (entries.ValueKind != JsonValueKind.Object)
            throw new InvalidDataException("assets must be an object mapping source paths to handles");

        var result = new Dictionary<string, ulong>(StringComparer.OrdinalIgnoreCase);
        var handles = new HashSet<ulong>();
        foreach (JsonProperty entry in entries.EnumerateObject())
        {
            string path = NormalizePath(entry.Name);
            ulong handle = entry.Value.GetUInt64();
            if (string.IsNullOrWhiteSpace(path) || handle == 0 || !result.TryAdd(path, handle) ||
                !handles.Add(handle))
                throw new InvalidDataException($"duplicate, empty, or zero mapping '{entry.Name}'");
        }
        return result;
    }

    private static ulong FindAssetHandle(string syntaxPath, Dictionary<string, ulong> assets)
    {
        string normalized = NormalizePath(syntaxPath);
        if (assets.TryGetValue(normalized, out ulong exact))
            return exact;
        ulong result = 0;
        foreach ((string path, ulong handle) in assets)
        {
            if (!normalized.EndsWith('/' + path, StringComparison.OrdinalIgnoreCase) &&
                !string.Equals(Path.GetFileName(normalized), Path.GetFileName(path),
                    StringComparison.OrdinalIgnoreCase))
                continue;
            if (result != 0)
                return 0;
            result = handle;
        }
        return result;
    }

    private static List<ManifestField> BuildFields(INamedTypeSymbol script, ulong assetHandle,
        SourceProductionContext context)
    {
        var fields = new List<ManifestField>();
        foreach (IFieldSymbol field in script.GetMembers().OfType<IFieldSymbol>()
                     .OrderBy(static value => value.Locations.FirstOrDefault()?.SourceSpan.Start ?? int.MaxValue))
        {
            if (field.IsImplicitlyDeclared || field.IsStatic || field.IsConst || field.IsReadOnly ||
                HasAttribute(field, "System.NonSerializedAttribute"))
                continue;
            bool isPublic = field.DeclaredAccessibility == Accessibility.Public;
            if (!isPublic && !HasAttribute(field, "TomCat.SerializeFieldAttribute"))
                continue;

            if (TryGetAssetRefMarker(field.Type, out string assetMarker) &&
                !IsSupportedAssetMarker(assetMarker))
            {
                context.ReportDiagnostic(Diagnostic.Create(UnsupportedAssetMarker,
                    field.Locations.FirstOrDefault(), script.ToDisplayString(), field.Name,
                    assetMarker));
                continue;
            }
            if (!TryGetFieldType(field.Type, out string token, out string? typeName))
            {
                context.ReportDiagnostic(Diagnostic.Create(UnsupportedField,
                    field.Locations.FirstOrDefault(), script.ToDisplayString(), field.Name,
                    field.Type.ToDisplayString()));
                continue;
            }

            (float? minimum, float? maximum) = ReadRange(field);
            fields.Add(new ManifestField
            {
                Id = CreateFieldId(assetHandle, script, field.Name),
                Name = field.Name,
                Type = token,
                TypeName = typeName,
                IsPublic = isPublic,
                Hidden = HasAttribute(field, "TomCat.HideInInspectorAttribute"),
                Header = ReadStringArgument(field, "TomCat.HeaderAttribute"),
                Tooltip = ReadStringArgument(field, "TomCat.TooltipAttribute"),
                RangeMin = minimum,
                RangeMax = maximum,
                FormerNames = ReadFormerNames(field)
            });
        }
        return fields;
    }

    private static bool TryGetFieldType(ITypeSymbol type, out string token, out string? typeName)
    {
        typeName = null;
        token = type.SpecialType switch
        {
            SpecialType.System_Boolean => "Bool",
            SpecialType.System_Int32 => "Int32",
            SpecialType.System_Int64 => "Int64",
            SpecialType.System_Single => "Float",
            SpecialType.System_Double => "Double",
            SpecialType.System_String => "String",
            _ => string.Empty
        };
        if (!string.IsNullOrEmpty(token))
            return true;
        if (type.TypeKind == TypeKind.Enum)
        {
            token = "Enum";
            typeName = type.ToDisplayString(SymbolDisplayFormat.CSharpErrorMessageFormat);
            return true;
        }

        string metadataName = type.ToDisplayString(SymbolDisplayFormat.CSharpErrorMessageFormat);
        token = metadataName switch
        {
            "TomCat.Vector2" => "Vector2",
            "TomCat.Vector3" => "Vector3",
            "TomCat.Vector4" => "Vector4",
            "TomCat.Color" => "Color",
            "TomCat.Entity" => "Entity",
			"TomCat.SceneAsset" => "AssetRef",
			"TomCat.PrefabAsset" => "AssetRef",
            _ => string.Empty
        };
		if (!string.IsNullOrEmpty(token))
		{
			if (token == "AssetRef")
				typeName = metadataName;
            return true;
		}
        if (TryGetAssetRefMarker(type, out string assetMarker) &&
            IsSupportedAssetMarker(assetMarker))
        {
            token = "AssetRef";
            typeName = metadataName;
            return true;
        }
        return false;
    }

    private static bool TryGetAssetRefMarker(ITypeSymbol type, out string markerName)
    {
        markerName = string.Empty;
        if (type is not INamedTypeSymbol { Name: "AssetRef", Arity: 1 } named ||
            named.ContainingNamespace.ToDisplayString() != "TomCat")
            return false;
        markerName = named.TypeArguments[0]
            .ToDisplayString(SymbolDisplayFormat.CSharpErrorMessageFormat);
        return true;
    }

    private static bool IsSupportedAssetMarker(string markerName) => markerName is
        "TomCat.Texture2DAsset" or
        "TomCat.ShaderAsset" or
        "TomCat.AudioAsset" or
        "TomCat.FontAsset" or
        "TomCat.MeshAsset" or
        "TomCat.MaterialAsset" or
        "TomCat.SceneAsset" or
        "TomCat.PrefabAsset";

    private static uint ReadLifecycle(INamedTypeSymbol script)
    {
        uint result = 0;
        (string Name, uint Flag)[] callbacks =
        [
            ("OnCreate", 1u << 0), ("OnEnable", 1u << 1), ("OnUpdate", 1u << 2),
            ("OnFixedUpdate", 1u << 3), ("OnCollisionEnter2D", 1u << 4),
            ("OnCollisionExit2D", 1u << 5), ("OnTriggerEnter2D", 1u << 6),
            ("OnTriggerExit2D", 1u << 7), ("OnDisable", 1u << 8), ("OnDestroy", 1u << 9)
        ];
        foreach ((string name, uint flag) in callbacks)
        {
            if (script.GetMembers(name).OfType<IMethodSymbol>().Any(static method => method.IsOverride))
                result |= flag;
        }
        return result;
    }

    private static int ReadExecutionOrder(INamedTypeSymbol script)
    {
        AttributeData? attribute = FindAttribute(script, "TomCat.DefaultExecutionOrderAttribute");
        return attribute is { ConstructorArguments.Length: 1 }
            ? (int)(attribute.ConstructorArguments[0].Value ?? 0)
            : 0;
    }

    private static (float? Minimum, float? Maximum) ReadRange(IFieldSymbol field)
    {
        AttributeData? attribute = FindAttribute(field, "TomCat.RangeAttribute");
        if (attribute is not { ConstructorArguments.Length: 2 })
            return (null, null);
        return (Convert.ToSingle(attribute.ConstructorArguments[0].Value),
            Convert.ToSingle(attribute.ConstructorArguments[1].Value));
    }

    private static string? ReadStringArgument(ISymbol symbol, string attributeName)
    {
        AttributeData? attribute = FindAttribute(symbol, attributeName);
        return attribute is { ConstructorArguments.Length: 1 }
            ? attribute.ConstructorArguments[0].Value as string
            : null;
    }

    private static List<string> ReadFormerNames(IFieldSymbol field) => field.GetAttributes()
        .Where(static attribute => attribute.AttributeClass?.ToDisplayString() ==
            "TomCat.FormerlySerializedAsAttribute")
        .Select(static attribute => attribute.ConstructorArguments[0].Value as string)
        .Where(static value => !string.IsNullOrWhiteSpace(value))
        .Cast<string>()
        .ToList();

    private static string CreateFieldId(ulong assetHandle, INamedTypeSymbol script, string fieldName)
    {
        string identity = $"{assetHandle}:{script.ToDisplayString(SymbolDisplayFormat.CSharpErrorMessageFormat)}:{fieldName}";
        byte[] hash = SHA256.HashData(Encoding.UTF8.GetBytes(identity));
        return Convert.ToHexString(hash, 0, 16).ToLowerInvariant();
    }

    private static bool DerivesFrom(INamedTypeSymbol type, INamedTypeSymbol expectedBase)
    {
        for (INamedTypeSymbol? current = type.BaseType; current is not null; current = current.BaseType)
        {
            if (SymbolEqualityComparer.Default.Equals(current, expectedBase))
                return true;
        }
        return false;
    }

    private static bool HasAttribute(ISymbol symbol, string metadataName) =>
        FindAttribute(symbol, metadataName) is not null;

    private static AttributeData? FindAttribute(ISymbol symbol, string metadataName) =>
        symbol.GetAttributes().FirstOrDefault(attribute =>
            attribute.AttributeClass?.ToDisplayString() == metadataName);

    private static void EmitManifest(SourceProductionContext context, List<ManifestScript> scripts)
    {
        string json = JsonSerializer.Serialize(new ManifestRoot { Version = 1, Scripts = scripts },
            new JsonSerializerOptions
            {
                PropertyNamingPolicy = JsonNamingPolicy.CamelCase,
                DefaultIgnoreCondition = JsonIgnoreCondition.WhenWritingNull
            });
        string literal = SymbolDisplay.FormatLiteral(json, quote: true);
        string source = $$"""
            // <auto-generated />
            namespace TomCat.Generated;

            [global::System.CodeDom.Compiler.GeneratedCode("TomCat.ScriptGenerator", "1.0.0")]
            public static class ScriptManifest
            {
                public static string Json => {{literal}};
            }
            """;
        context.AddSource("TomCat.ScriptManifest.g.cs", SourceText.From(source, Encoding.UTF8));
    }

    private static string NormalizePath(string value) => value.Replace('\\', '/').TrimStart('/');

    private static DiagnosticDescriptor Error(string id, string title, string message) =>
        new(id, title, message, "TomCat.Scripting", DiagnosticSeverity.Error, isEnabledByDefault: true);

    private sealed class ManifestRoot
    {
        public uint Version { get; init; }
        public List<ManifestScript> Scripts { get; init; } = [];
    }

    private sealed class ManifestScript
    {
        public ulong AssetHandle { get; init; }
        public string TypeName { get; init; } = string.Empty;
        public int ExecutionOrder { get; init; }
        public bool DisallowMultiple { get; init; }
        public uint Lifecycle { get; init; }
        public List<ManifestField> Fields { get; init; } = [];
    }

    private sealed class ManifestField
    {
        public string Id { get; init; } = string.Empty;
        public string Name { get; init; } = string.Empty;
        public string Type { get; init; } = string.Empty;
        public string? TypeName { get; init; }
        public bool IsPublic { get; init; }
        public bool Hidden { get; init; }
        public string? Header { get; init; }
        public string? Tooltip { get; init; }
        public float? RangeMin { get; init; }
        public float? RangeMax { get; init; }
        public List<string> FormerNames { get; init; } = [];
    }
}
