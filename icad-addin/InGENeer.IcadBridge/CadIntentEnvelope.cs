using System.Text.Json;
using System.Text.Json.Serialization;

namespace InGENeer.IcadBridge;

/// <summary>
/// Wire DTO matching Python <c>CadIntentEnvelope</c> / schemas/cad_intent_envelope.schema.json.
/// Real iCAD add-in: deserialize on the host, then marshal execution to the UI thread (AutonomAtIon rules).
/// </summary>
public sealed class CadIntentEnvelope
{
    [JsonPropertyName("schemaVersion")]
    public string SchemaVersion { get; set; } = "1.0.0";

    [JsonPropertyName("intentId")]
    public string IntentId { get; set; } = "";

    [JsonPropertyName("command")]
    public string Command { get; set; } = "";

    [JsonPropertyName("parameters")]
    public JsonElement Parameters { get; set; }

    [JsonPropertyName("targetDocumentRef")]
    public string? TargetDocumentRef { get; set; }

    [JsonPropertyName("modelFingerprintExpected")]
    public string? ModelFingerprintExpected { get; set; }
}
