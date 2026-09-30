using System.Text.Json;
using TomCat.Interop;

namespace TomCat;

/// <summary>Outcome of reading a save slot from the running game's data.</summary>
public enum SaveReadStatus
{
    /// <summary>The primary slot file was read and verified.</summary>
    Ok = 0,
    /// <summary>The primary file was unreadable; the rotated backup answered the
    /// read and has been restored over it.</summary>
    RecoveredFromBackup = 1,
    /// <summary>No file for this slot exists.</summary>
    Missing = 2,
    /// <summary>Files exist for this slot but none of them passed verification.</summary>
    Corrupted = 3
}

/// <summary>Immutable metadata for one save slot, as reported by ListSlots.</summary>
public sealed record SaveSlotInfo(string Name, uint FormatVersion,
    uint DataVersion, long SavedAtUtcUnixSeconds, ulong PayloadBytes,
    bool Corrupted)
{
    public DateTimeOffset SavedAtUtc => DateTimeOffset.FromUnixTimeSeconds(
        SavedAtUtcUnixSeconds);
}

/// <summary>The result of SaveData.ReadBytes: status, envelope metadata and payload bytes.</summary>
public sealed record SaveSlotReadResult(SaveReadStatus Status, uint DataVersion,
    long SavedAtUtcUnixSeconds, byte[] Bytes)
{
    internal static SaveSlotReadResult Missing() => new(SaveReadStatus.Missing,
        0, 0, []);
    public DateTimeOffset SavedAtUtc => DateTimeOffset.FromUnixTimeSeconds(
        SavedAtUtcUnixSeconds);
    public bool HasBytes => Bytes.Length != 0;
}

/// <summary>
/// A typed key-value document games persist inside one save slot. Values are
/// booleans, 64-bit integers, finite doubles or strings; keys are unique and
/// case-sensitive. The document serializes to UTF-8 JSON, which the engine
/// stores inside a checksummed, versioned, atomically-installed slot file.
/// Use <see cref="Version"/> as your payload schema version and migrate older
/// payloads after <see cref="SaveData.ReadBytes"/> reports them.
/// </summary>
public sealed class SaveDocument
{
    private enum ValueKind : byte
    {
        Boolean,
        Long,
        Double,
        String
    }

    private readonly struct StoredValue(ValueKind kind, bool boolean,
        long integer, double number, string? text)
    {
        public ValueKind Kind { get; } = kind;
        public bool Boolean { get; } = boolean;
        public long Integer { get; } = integer;
        public double Number { get; } = number;
        public string Text { get; } = text ?? string.Empty;

        public static StoredValue From(bool value) =>
            new(ValueKind.Boolean, value, 0, 0, null);
        public static StoredValue From(long value) =>
            new(ValueKind.Long, false, value, 0, null);
        public static StoredValue From(double value) =>
            new(ValueKind.Double, false, 0, value, null);
        public static StoredValue From(string value) =>
            new(ValueKind.String, false, 0, 0, value);
    }

    private const string KindBoolean = "b";
    private const string KindNumber = "n";
    private const string KindString = "s";

    private readonly Dictionary<string, StoredValue> entries =
        new(StringComparer.Ordinal);

    public uint Version { get; set; }
    public int Count => entries.Count;
    public IEnumerable<string> Keys => entries.Keys;

    public void Set(string key, bool value) => entries[key] = StoredValue.From(value);
    public void Set(string key, long value) => entries[key] = StoredValue.From(value);
    public void Set(string key, int value) => entries[key] = StoredValue.From((long)value);
    public void Set(string key, double value)
    {
        if (!double.IsFinite(value))
            throw new ArgumentOutOfRangeException(nameof(value),
                "Save documents only store finite doubles.");
        entries[key] = StoredValue.From(value);
    }
    public void Set(string key, string value)
    {
        ArgumentNullException.ThrowIfNull(value);
        entries[key] = StoredValue.From(value);
    }

    public bool Contains(string key) => entries.ContainsKey(key);
    public bool Remove(string key) => entries.Remove(key);
    public void Clear() => entries.Clear();

    private StoredValue Read(string key, string operation)
    {
        ArgumentNullException.ThrowIfNull(key);
        if (entries.TryGetValue(key, out StoredValue value))
            return value;
        throw new KeyNotFoundException(
            $"{operation}: save document has no key '{key}'.");
    }

    public bool GetBool(string key)
    {
        StoredValue value = Read(key, nameof(GetBool));
        return value.Kind == ValueKind.Boolean ? value.Boolean
            : throw Mismatch(key, "bool");
    }
    public long GetLong(string key)
    {
        StoredValue value = Read(key, nameof(GetLong));
        return value.Kind == ValueKind.Long ? value.Integer
            : throw Mismatch(key, "long");
    }
    public double GetDouble(string key)
    {
        StoredValue value = Read(key, nameof(GetDouble));
        return value.Kind is ValueKind.Double ? value.Number
            : value.Kind == ValueKind.Long ? value.Integer
            : throw Mismatch(key, "double");
    }
    public string GetString(string key)
    {
        StoredValue value = Read(key, nameof(GetString));
        return value.Kind == ValueKind.String ? value.Text
            : throw Mismatch(key, "string");
    }

    public bool TryGetBool(string key, out bool value)
    {
        if (entries.TryGetValue(key, out StoredValue stored)
            && stored.Kind == ValueKind.Boolean)
        {
            value = stored.Boolean;
            return true;
        }
        value = default;
        return false;
    }
    public bool TryGetLong(string key, out long value)
    {
        if (entries.TryGetValue(key, out StoredValue stored)
            && stored.Kind == ValueKind.Long)
        {
            value = stored.Integer;
            return true;
        }
        value = default;
        return false;
    }
    public bool TryGetDouble(string key, out double value)
    {
        if (entries.TryGetValue(key, out StoredValue stored))
        {
            if (stored.Kind == ValueKind.Double)
            {
                value = stored.Number;
                return true;
            }
            if (stored.Kind == ValueKind.Long)
            {
                value = stored.Integer;
                return true;
            }
        }
        value = default;
        return false;
    }
    public bool TryGetString(string key, out string value)
    {
        if (entries.TryGetValue(key, out StoredValue stored)
            && stored.Kind == ValueKind.String)
        {
            value = stored.Text;
            return true;
        }
        value = string.Empty;
        return false;
    }

    private static TomCatException Mismatch(string key, string expected) =>
        new($"Save document key '{key}' does not store a {expected}.");

    /// <summary>Serializes the document to the canonical UTF-8 JSON payload.</summary>
    public byte[] Serialize()
    {
        var encoded = new Dictionary<string, object?>(entries.Count,
            StringComparer.Ordinal);
        foreach ((string key, StoredValue value) in entries)
        {
            encoded[key] = value.Kind switch
            {
                ValueKind.Boolean => new object?[] { KindBoolean, value.Boolean },
                ValueKind.Long => new object?[] { KindNumber, value.Integer },
                ValueKind.Double => new object?[] { KindNumber, value.Number },
                _ => new object?[] { KindString, value.Text }
            };
        }
        return JsonSerializer.SerializeToUtf8Bytes(new SerializedPayload
        {
            Version = Version,
            Entries = encoded
        }, SerializerOptions);
    }

    /// <summary>Parses a canonical payload produced by Serialize. Foreign or
    /// malformed payloads throw before any state is applied.</summary>
    public static SaveDocument Deserialize(byte[] bytes)
    {
        ArgumentNullException.ThrowIfNull(bytes);
        PayloadEnvelope? envelope;
        try
        {
            envelope = JsonSerializer.Deserialize<PayloadEnvelope>(bytes,
                SerializerOptions);
        }
        catch (JsonException error)
        {
            throw new TomCatException($"Save payload is not valid: {error.Message}");
        }
        if (envelope?.Entries is null)
            throw new TomCatException("Save payload has no document entries.");
        var document = new SaveDocument { Version = envelope.Version };
        foreach ((string key, JsonElement value) in envelope.Entries)
        {
            if (value.ValueKind != JsonValueKind.Array || value.GetArrayLength() != 2)
                throw new TomCatException(
                    $"Save payload entry '{key}' is not a [kind, value] pair.");
            JsonElement kindElement = value[0];
            JsonElement valueElement = value[1];
            if (kindElement.ValueKind != JsonValueKind.String)
                throw new TomCatException(
                    $"Save payload entry '{key}' has a non-string kind tag.");
            switch (kindElement.GetString())
            {
                case KindBoolean
                    when valueElement.ValueKind is JsonValueKind.True
                        or JsonValueKind.False:
                    document.Set(key, valueElement.GetBoolean());
                    break;
                case KindNumber when valueElement.ValueKind == JsonValueKind.Number
                    && valueElement.TryGetInt64(out long integer):
                    document.Set(key, integer);
                    break;
                case KindNumber when valueElement.ValueKind == JsonValueKind.Number
                    && valueElement.TryGetDouble(out double number)
                    && double.IsFinite(number):
                    document.Set(key, number);
                    break;
                case KindString when valueElement.ValueKind == JsonValueKind.String:
                    document.Set(key, valueElement.GetString()!);
                    break;
                default:
                    throw new TomCatException(
                        $"Save payload entry '{key}' has an unsupported value.");
            }
        }
        return document;
    }

    private sealed class PayloadEnvelope
    {
        public uint Version { get; set; }
        public Dictionary<string, JsonElement>? Entries { get; set; }
    }

    private sealed class SerializedPayload
    {
        public uint Version { get; set; }
        public Dictionary<string, object?> Entries { get; set; } = new();
    }

    private static readonly JsonSerializerOptions SerializerOptions = new(
        JsonSerializerDefaults.Web);
}

/// <summary>
/// Game save slots for the running game. Slots live in the per-game save
/// directory published by the Player (or the editor Play session); every write
/// is checksummed, atomically installed and rotated to a backup the engine
/// recovers from automatically. All methods must be called from the main
/// thread inside a script lifecycle callback. Throws <see cref="TomCatException"/>
/// when no game data directory is published (for example outside Play mode).
/// </summary>
public static class SaveData
{
    /// <summary>Slot names may contain 1-64 ASCII letters, digits, '_' or '-'.</summary>
    public const int MaximumSlotNameLength = 64;
    public const int MaximumPayloadBytes = 16 * 1024 * 1024;

    /// <summary>Writes the document into a slot atomically. The document's
    /// <see cref="SaveDocument.Version"/> travels with the slot.</summary>
    public static void Write(string slot, SaveDocument document)
    {
        ArgumentNullException.ThrowIfNull(document);
        byte[] payload = document.Serialize();
        NativeBridge.SaveWriteSlot(ValidateSlotName(slot), document.Version,
            payload);
    }

    /// <summary>Writes opaque game bytes into a slot atomically.</summary>
    public static void WriteBytes(string slot, uint dataVersion,
        ReadOnlySpan<byte> payload) =>
        NativeBridge.SaveWriteSlot(ValidateSlotName(slot), dataVersion, payload);

    /// <summary>Reads a slot's raw payload. Missing slots return
    /// <see cref="SaveReadStatus.Missing"/> with empty bytes; corrupted slots
    /// return <see cref="SaveReadStatus.Corrupted"/> after backup recovery failed.</summary>
    public static SaveSlotReadResult ReadBytes(string slot) =>
        NativeBridge.SaveReadSlot(ValidateSlotName(slot));

    /// <summary>Reads a slot written by <see cref="SaveData.Write"/> and decodes
    /// its document. Returns null when the slot is missing; throws when every
    /// copy of the slot failed verification.</summary>
    public static SaveDocument? ReadDocument(string slot)
    {
        SaveSlotReadResult result = NativeBridge.SaveReadSlot(
            ValidateSlotName(slot));
        if (result.Status == SaveReadStatus.Missing)
            return null;
        if (result.Status == SaveReadStatus.Corrupted)
            throw new TomCatException(
                $"Save slot '{slot}' exists but every copy failed verification.");
        return result.HasBytes ? SaveDocument.Deserialize(result.Bytes)
            : new SaveDocument();
    }

    /// <summary>Deletes the slot and its backup. Returns false when nothing existed.</summary>
    public static bool Delete(string slot) =>
        NativeBridge.SaveDeleteSlot(ValidateSlotName(slot));

    public static bool Exists(string slot) =>
        NativeBridge.SaveSlotExists(ValidateSlotName(slot));

    /// <summary>Every slot in the save directory, including entries whose file
    /// failed verification (<see cref="SaveSlotInfo.Corrupted"/>).</summary>
    public static SaveSlotInfo[] ListSlots() => NativeBridge.SaveListSlots();

    private static string ValidateSlotName(string slot)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(slot);
        if (slot.Length > MaximumSlotNameLength)
            throw new ArgumentOutOfRangeException(nameof(slot),
                $"Slot names may contain at most {MaximumSlotNameLength} characters.");
        foreach (char character in slot)
        {
            bool allowed = character is >= 'a' and <= 'z' or >= 'A' and <= 'Z'
                or >= '0' and <= '9' or '_' or '-';
            if (!allowed)
                throw new ArgumentException(
                    "Slot names may only contain ASCII letters, digits, '_' and '-'.",
                    nameof(slot));
        }
        return slot;
    }
}
