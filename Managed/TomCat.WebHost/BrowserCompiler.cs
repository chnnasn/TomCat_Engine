using System.Collections.Immutable;
using System.Runtime.InteropServices.JavaScript;
using System.Text;
using System.Text.Json;
using System.Text.Json.Serialization;
using Microsoft.CodeAnalysis;
using Microsoft.CodeAnalysis.CSharp;
using Microsoft.CodeAnalysis.Emit;
using Microsoft.CodeAnalysis.Text;
using TomCat.ScriptGenerator;

namespace TomCat.WebHost;

public static partial class BrowserCompiler
{
    private sealed record SourceInput(string Path, string Text);
    private sealed record ReferenceInput(string Name, string Base64);
    private sealed record CompileRequest(SourceInput[] Sources, ReferenceInput[] References,
        string ScriptAssetsJson);
    private sealed record DiagnosticOutput(string Severity, string Code, string Message,
        string? File, int Line, int Column);
    private sealed record CompileResponse(bool Succeeded, string? Assembly, string? Pdb,
        DiagnosticOutput[] Diagnostics);

    [JsonSourceGenerationOptions(PropertyNameCaseInsensitive = true,
        PropertyNamingPolicy = JsonKnownNamingPolicy.CamelCase)]
    [JsonSerializable(typeof(CompileRequest))]
    [JsonSerializable(typeof(CompileResponse))]
    private sealed partial class BrowserJsonContext : JsonSerializerContext;

    private sealed class AssetsText(string json) : AdditionalText
    {
        public override string Path => "ScriptAssets.json";
        public override SourceText GetText(CancellationToken cancellationToken = default) =>
            SourceText.From(json, Encoding.UTF8);
    }

    [JSExport]
    public static string Compile(string requestJson)
    {
        try
        {
            CompileRequest request = JsonSerializer.Deserialize(requestJson,
                BrowserJsonContext.Default.CompileRequest) ??
                throw new InvalidDataException("Compile request is empty.");
            if (request.Sources.Length == 0 || request.Sources.Length > 10_000 ||
                request.References.Length == 0 || request.References.Length > 2_000)
                throw new InvalidDataException("Compile request source/reference count is invalid.");
            if (request.ScriptAssetsJson.Length > 8 * 1024 * 1024)
                throw new InvalidDataException("ScriptAssets.json is too large.");

            long totalSourceBytes = 0;
            long totalReferenceBytes = 0;

            var parseOptions = new CSharpParseOptions(LanguageVersion.Latest);
            SyntaxTree[] trees = request.Sources.Select(source =>
            {
                if (string.IsNullOrWhiteSpace(source.Path) || source.Text.Length > 8 * 1024 * 1024)
                    throw new InvalidDataException("A source path is empty or its text is too large.");
                totalSourceBytes = checked(totalSourceBytes + Encoding.UTF8.GetByteCount(source.Text));
                if (totalSourceBytes > 64 * 1024 * 1024)
                    throw new InvalidDataException("Combined source text exceeds 64 MiB.");
                return CSharpSyntaxTree.ParseText(SourceText.From(source.Text, Encoding.UTF8),
                    parseOptions, source.Path);
            }).ToArray();
            MetadataReference[] references = request.References.Select(reference =>
            {
                if (string.IsNullOrWhiteSpace(reference.Name) ||
                    reference.Base64.Length > 96 * 1024 * 1024)
                    throw new InvalidDataException("A reference name is empty or its payload is too large.");
                byte[] bytes = Convert.FromBase64String(reference.Base64);
                if (bytes.Length == 0 || bytes.Length > 64 * 1024 * 1024)
                    throw new InvalidDataException($"Reference '{reference.Name}' has an invalid size.");
                totalReferenceBytes = checked(totalReferenceBytes + bytes.Length);
                if (totalReferenceBytes > 256 * 1024 * 1024)
                    throw new InvalidDataException("Combined metadata references exceed 256 MiB.");
                return MetadataReference.CreateFromImage(bytes, filePath: reference.Name);
            }).ToArray();

            CSharpCompilation compilation = CSharpCompilation.Create("Assembly-CSharp", trees,
                references, new CSharpCompilationOptions(OutputKind.DynamicallyLinkedLibrary,
                    optimizationLevel: OptimizationLevel.Debug, deterministic: true,
                    concurrentBuild: false,
                    nullableContextOptions: NullableContextOptions.Enable));
            GeneratorDriver driver = CSharpGeneratorDriver.Create(
                [new TomCat.ScriptGenerator.ScriptGenerator().AsSourceGenerator()],
                [new AssetsText(request.ScriptAssetsJson)], parseOptions);
            driver.RunGeneratorsAndUpdateCompilation(compilation, out Compilation generated,
                out ImmutableArray<Diagnostic> generatorDiagnostics);

            using var assembly = new MemoryStream();
            // Portable PDB emission synchronously waits for Roslyn's debug-source Task.
            // Single-threaded browser WASM cannot block on monitors, so browser builds
            // intentionally install without a PDB. The native ABI already accepts it.
            EmitResult emit = generated.Emit(assembly);
            Diagnostic[] diagnostics = generatorDiagnostics.Concat(emit.Diagnostics).ToArray();
            var result = new CompileResponse(emit.Success,
                emit.Success ? Convert.ToBase64String(assembly.ToArray()) : null,
                emit.Success ? string.Empty : null,
                diagnostics.Select(ToOutput).ToArray());
            return JsonSerializer.Serialize(result, BrowserJsonContext.Default.CompileResponse);
        }
        catch (Exception exception)
        {
            var result = new CompileResponse(false, null, null,
                [new DiagnosticOutput("error", "TCWEB0001", exception.ToString(), null, 0, 0)]);
            return JsonSerializer.Serialize(result, BrowserJsonContext.Default.CompileResponse);
        }
    }

    private static DiagnosticOutput ToOutput(Diagnostic diagnostic)
    {
        FileLinePositionSpan span = diagnostic.Location.GetLineSpan();
        return new DiagnosticOutput(diagnostic.Severity.ToString().ToLowerInvariant(),
            diagnostic.Id, diagnostic.GetMessage(), span.Path,
            span.IsValid ? span.StartLinePosition.Line + 1 : 0,
            span.IsValid ? span.StartLinePosition.Character + 1 : 0);
    }
}
