using System.Reflection;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Text;
using TomCat.Interop;
using TomCat.ScriptHost;

namespace TomCat.Managed.Regression;

// The managed interop structs are the source of truth for two generated files:
//
//   TomCat/src/TomCat/Scripting/ManagedAbiLayout.generated.h
//     sizeof/offsetof/field-type static_asserts compiled into the engine, so a
//     C++ table that drifts from the C# table fails the native build.
//   Managed/TomCat.WebHost/NativeCallSignatures.cs
//     every wasm signature of a native function that managed code calls through
//     a function pointer, so the browser build has an interp-to-native stub.
//
// `dotnet run --project Managed/TomCat.Managed.Regression -- --write-interop-abi`
// regenerates both. The normal regression run fails when either is stale.
internal static partial class Program
{
	internal const string WriteInteropAbiArgument = "--write-interop-abi";
	private const string AbiHeaderPath = "TomCat/src/TomCat/Scripting/ManagedAbiLayout.generated.h";
	private const string WebSignaturesPath = "Managed/TomCat.WebHost/NativeCallSignatures.cs";
	private const string RegenerateHint =
		"Run: dotnet run --project Managed/TomCat.Managed.Regression -- " + WriteInteropAbiArgument;

	// C# names whose C++ declaration has a different name.
	private static readonly Dictionary<string, string> s_nativeTypeNames = new()
	{
		["NativeEntityHandleV1"] = "EntityHandleV1",
		["BusApi"] = "NativeAudioBusApiV1",
		["MixerApi"] = "NativeAudioMixerApiV1",
	};

	// C# structs whose C++ declaration uses an array instead of named fields.
	private static readonly Dictionary<string, (string Field, string Type)> s_nativeArrayStructs = new()
	{
		["NativeMatrix4"] = ("Values", "float[16]"),
	};

	// C# fields that C++ declares inside a nested struct with the same layout.
	private static readonly Dictionary<string, string> s_nativeFieldPaths = new()
	{
		["NativePropertyValueV1.X"] = "Vector.X",
		["NativePropertyValueV1.Y"] = "Vector.Y",
		["NativePropertyValueV1.Z"] = "Vector.Z",
		["NativePropertyValueV1.W"] = "Vector.W",
	};

	// Native code calls these tables; managed code implements them.
	private static readonly HashSet<Type> s_managedImplementedTables = [typeof(ManagedApiV1)];

	internal static int WriteInteropAbi()
	{
		string root = FindRepositoryRoot();
		foreach ((string path, string content) in GenerateInteropAbiFiles())
		{
			string fullPath = Path.Combine(root, path);
			File.WriteAllText(fullPath, content, new UTF8Encoding(false));
			Console.WriteLine($"Generated {path}");
		}
		return 0;
	}

	private static void VerifyInteropAbiIsCurrent()
	{
		string root = FindRepositoryRoot();
		foreach ((string path, string expected) in GenerateInteropAbiFiles())
		{
			string fullPath = Path.Combine(root, path);
			Check(File.Exists(fullPath), $"{path} is missing. {RegenerateHint}");
			string existing = File.ReadAllText(fullPath).Replace("\r\n", "\n");
			Check(existing == expected, $"{path} is stale; the C# interop structs changed. {RegenerateHint}");
		}
		Console.WriteLine("PASS interop ABI layout and Web call signatures match the C# tables");
	}

	private static IEnumerable<(string Path, string Content)> GenerateInteropAbiFiles()
	{
		Check(IntPtr.Size == 8, "interop ABI generation requires a 64-bit process");
		List<Type> structs = DiscoverInteropStructs();
		yield return (AbiHeaderPath, GenerateAbiHeader(structs));
		yield return (WebSignaturesPath, GenerateWebSignatures(structs));
	}

	private static string FindRepositoryRoot()
	{
		for (DirectoryInfo? directory = new(AppContext.BaseDirectory); directory is not null;
			directory = directory.Parent)
		{
			if (File.Exists(Path.Combine(directory.FullName, "Managed", "TomCat.Managed.slnx")))
				return directory.FullName;
		}
		throw new InvalidOperationException(
			"Could not find the repository root (Managed/TomCat.Managed.slnx) above " +
			AppContext.BaseDirectory);
	}

	// Every struct in TomCat.Interop, plus any other struct that holds an
	// unmanaged function pointer (the private capability tables in NativeBridge).
	private static List<Type> DiscoverInteropStructs()
	{
		const BindingFlags all = BindingFlags.Public | BindingFlags.NonPublic;
		var result = new List<Type>();
		foreach (Assembly assembly in new[] { typeof(NativeApiV1).Assembly, typeof(ManagedApiV1).Assembly })
		{
			foreach (Type type in assembly.GetTypes())
			{
				if (!type.IsValueType || type.IsEnum || type.Name.Contains('<') ||
					type.IsDefined(typeof(CompilerGeneratedAttribute)))
					continue;
				bool interopNamespace = type.Namespace == "TomCat.Interop";
				bool hasFunctionPointer = type.GetFields(BindingFlags.Instance | all)
					.Any(field => field.FieldType.IsFunctionPointer);
				if (interopNamespace || hasFunctionPointer)
					result.Add(type);
			}
		}
		result.Sort((a, b) => string.CompareOrdinal(NativeName(a), NativeName(b)));
		return result;
	}

	private static string NativeName(Type type) =>
		s_nativeTypeNames.TryGetValue(type.Name, out string? name) ? name : type.Name;

	private static FieldInfo[] InstanceFields(Type type) =>
		type.GetFields(BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic)
			.OrderBy(field => Marshal.OffsetOf(type, field.Name).ToInt64())
			.ToArray();

	private static int ManagedSize(Type type) =>
		(int)typeof(Unsafe).GetMethod(nameof(Unsafe.SizeOf))!
			.MakeGenericMethod(type).Invoke(null, null)!;

	private static string GenerateAbiHeader(List<Type> structs)
	{
		var text = new StringBuilder();
		text.Append("// <auto-generated>\n");
		text.Append("// Generated from the C# interop structs by\n");
		text.Append("//   dotnet run --project Managed/TomCat.Managed.Regression -- ").Append(WriteInteropAbiArgument).Append('\n');
		text.Append("// Do not edit. Included only by ManagedAbiLayout.cpp. Layouts are for 64-bit targets.\n");
		text.Append("// </auto-generated>\n\n");
		text.Append($"TC_ABI_CONSTANT(NativeApiVersion, {ManagedAbi.NativeApiVersion}u)\n");
		text.Append($"TC_ABI_CONSTANT(ManagedApiVersion, {ManagedAbi.ManagedApiVersion}u)\n");
		text.Append($"TC_ABI_CONSTANT(ScriptManifestVersion, {ManagedAbi.ScriptManifestVersion}u)\n");

		foreach (Type type in structs)
		{
			int size = Marshal.SizeOf(type);
			Check(size == ManagedSize(type),
				$"{type.FullName} is not blittable: managed size {ManagedSize(type)} != native size {size}");
			string name = NativeName(type);
			text.Append('\n').Append($"TC_ABI_STRUCT({name}, {size})\n");
			if (s_nativeArrayStructs.TryGetValue(type.Name, out var array))
			{
				text.Append($"TC_ABI_FIELD({name}, {array.Field}, 0, {array.Type})\n");
				continue;
			}
			foreach (FieldInfo field in InstanceFields(type))
			{
				long offset = Marshal.OffsetOf(type, field.Name).ToInt64();
				string path = s_nativeFieldPaths.TryGetValue($"{type.Name}.{field.Name}", out string? nested)
					? nested : field.Name;
				text.Append($"TC_ABI_FIELD({name}, {path}, {offset}, {NativeFieldType(field)})\n");
			}
		}
		return text.ToString();
	}

	private static string NativeFieldType(FieldInfo field)
	{
		FixedBufferAttribute? buffer = field.GetCustomAttribute<FixedBufferAttribute>();
		if (buffer is not null)
			return $"{NativeType(buffer.ElementType)}[{buffer.Length}]";
		if (field.FieldType.IsFunctionPointer)
			RequireCdecl(field.GetModifiedFieldType(), $"{field.DeclaringType!.Name}.{field.Name}");
		return NativeType(field.FieldType);
	}

	private static void RequireCdecl(Type modifiedFunctionPointer, string context)
	{
		Check(modifiedFunctionPointer.IsUnmanagedFunctionPointer,
			$"{context} must be an unmanaged function pointer");
		Check(modifiedFunctionPointer.GetFunctionPointerCallingConventions()
			.SequenceEqual([typeof(CallConvCdecl)]), $"{context} must use unmanaged[Cdecl]");
	}

	// C++ spelling of a C# type after the canonicalization ManagedAbiLayout.cpp
	// applies to the native side: no const, enums as their underlying type.
	private static string NativeType(Type type)
	{
		if (type.IsEnum)
			return NativeType(Enum.GetUnderlyingType(type));
		if (type.IsPointer)
			return NativeType(type.GetElementType()!) + "*";
		if (type.IsFunctionPointer)
		{
			string parameters = string.Join(", ",
				type.GetFunctionPointerParameterTypes().Select(NativeType));
			return $"{NativeType(type.GetFunctionPointerReturnType())}(*)({parameters})";
		}
		if (type == typeof(void)) return "void";
		if (type == typeof(byte)) return "uint8_t";
		if (type == typeof(sbyte)) return "int8_t";
		if (type == typeof(ushort)) return "uint16_t";
		if (type == typeof(short)) return "int16_t";
		if (type == typeof(uint)) return "uint32_t";
		if (type == typeof(int)) return "int32_t";
		if (type == typeof(ulong)) return "uint64_t";
		if (type == typeof(long)) return "int64_t";
		if (type == typeof(float)) return "float";
		if (type == typeof(double)) return "double";
		if (type == typeof(nint)) return "intptr_t";
		if (type == typeof(nuint)) return "uintptr_t";
		if (type.IsValueType && !type.IsPrimitive)
			return NativeName(type);
		throw new InvalidOperationException($"{type.FullName} has no fixed native ABI spelling");
	}

	private static string GenerateWebSignatures(List<Type> structs)
	{
		var signatures = new SortedSet<string>(StringComparer.Ordinal);
		foreach (Type type in structs)
		{
			bool nativeImplemented = !s_managedImplementedTables.Contains(type);
			foreach (FieldInfo field in InstanceFields(type))
			{
				if (field.FieldType.IsFunctionPointer)
					CollectNativeCallees(field.FieldType, nativeImplemented, signatures);
			}
		}

		var text = new StringBuilder();
		text.Append("// <auto-generated>\n");
		text.Append("// Generated from the C# interop structs by\n");
		text.Append("//   dotnet run --project Managed/TomCat.Managed.Regression -- ").Append(WriteInteropAbiArgument).Append('\n');
		text.Append("// Do not edit.\n");
		text.Append("// </auto-generated>\n");
		text.Append("using System.Runtime.InteropServices;\n\n");
		text.Append("namespace TomCat.WebHost;\n\n");
		text.Append("// The .NET WASM SDK discovers P/Invoke and attributed delegate signatures, but\n");
		text.Append("// not delegate* calli sites in NativeBridge. Declare every native function\n");
		text.Append("// shape managed code calls in this rooted host assembly so game scripts loaded\n");
		text.Append("// after publish can call them. The first letter is the return type; the rest\n");
		text.Append("// are parameters. I = wasm32 pointer/int/indirect struct, L = int64,\n");
		text.Append("// F = float32, D = float64, V = void.\n");
		text.Append("// These are signature declarations only; calls still use the native API tables.\n");
		text.Append("internal static class NativeCallSignatures\n{\n");
		foreach (string signature in signatures)
		{
			string parameters = string.Join(", ", signature.Skip(1)
				.Select((letter, index) => $"{WebParameterType(letter)} {(char)('a' + index)}"));
			text.Append("    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]\n");
			text.Append($"    internal delegate {(signature[0] == 'I' ? "int" : WebParameterType(signature[0]))} {signature}({parameters});\n");
		}
		text.Append("}\n");
		return text.ToString();
	}

	// Records the wasm signature of every function pointer that native code
	// implements. Function-pointer parameters flip the direction: a callback
	// handed to a native function is implemented by managed code, and a
	// receiver handed to a managed entry point is implemented by native code.
	private static void CollectNativeCallees(Type functionPointer, bool nativeImplemented,
		SortedSet<string> signatures)
	{
		if (nativeImplemented)
		{
			var signature = new StringBuilder();
			signature.Append(WasmLetter(functionPointer.GetFunctionPointerReturnType(), isReturn: true));
			foreach (Type parameter in functionPointer.GetFunctionPointerParameterTypes())
				signature.Append(WasmLetter(parameter, isReturn: false));
			signatures.Add(signature.ToString());
		}
		foreach (Type parameter in functionPointer.GetFunctionPointerParameterTypes())
		{
			if (parameter.IsFunctionPointer)
				CollectNativeCallees(parameter, !nativeImplemented, signatures);
		}
	}

	// Matches the WasmAppBuilder signature mapping for wasm32.
	private static char WasmLetter(Type type, bool isReturn)
	{
		if (type.IsEnum)
			return WasmLetter(Enum.GetUnderlyingType(type), isReturn);
		if (type == typeof(void)) return 'V';
		if (type == typeof(long) || type == typeof(ulong)) return 'L';
		if (type == typeof(float)) return 'F';
		if (type == typeof(double)) return 'D';
		if (type.IsPointer || type.IsFunctionPointer || type == typeof(nint) || type == typeof(nuint) ||
			type == typeof(int) || type == typeof(uint) || type == typeof(short) ||
			type == typeof(ushort) || type == typeof(byte) || type == typeof(sbyte))
			return 'I';
		if (type.IsValueType)
		{
			FieldInfo[] fields = InstanceFields(type);
			if (fields.Length == 1 && !fields[0].IsDefined(typeof(FixedBufferAttribute)))
				return WasmLetter(fields[0].FieldType, isReturn);
			Check(!isReturn, $"{type.FullName} returned by value has no supported wasm signature");
			return 'I';  // multi-field structs are passed indirectly
		}
		throw new InvalidOperationException($"{type.FullName} has no wasm signature letter");
	}

	private static string WebParameterType(char letter) => letter switch
	{
		'I' => "nint",
		'L' => "long",
		'F' => "float",
		'D' => "double",
		'V' => "void",
		_ => throw new InvalidOperationException($"unknown wasm signature letter '{letter}'")
	};
}
