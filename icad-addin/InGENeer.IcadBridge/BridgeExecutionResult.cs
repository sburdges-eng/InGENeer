using System.Text.Json.Serialization;

namespace InGENeer.IcadBridge;

/// <summary>
/// Wire DTO matching Python <c>BridgeExecutionResult</c> (snake_case JSON keys).
/// </summary>
public sealed class BridgeExecutionResult
{
    [JsonPropertyName("schemaVersion")]
    public string SchemaVersion { get; set; } = "1.0.0";

    [JsonPropertyName("success")]
    public bool Success { get; set; }

    [JsonPropertyName("stdout")]
    public string Stdout { get; set; } = "";

    [JsonPropertyName("error_traceback")]
    public string? ErrorTraceback { get; set; }

    [JsonPropertyName("telemetry")]
    public Dictionary<string, object?> Telemetry { get; set; } = new();

    [JsonPropertyName("invariants")]
    public List<string>? Invariants { get; set; }

    public static BridgeExecutionResult Ok(CadIntentEnvelope intent, string stdout, Action<Dictionary<string, object?>>? fillTelemetry = null)
    {
        var telemetry = new Dictionary<string, object?> { ["intentId"] = intent.IntentId, ["command"] = intent.Command };
        fillTelemetry?.Invoke(telemetry);
        return new BridgeExecutionResult
        {
            Success = true,
            Stdout = stdout,
            ErrorTraceback = null,
            Telemetry = telemetry,
        };
    }

    public static BridgeExecutionResult Fail(CadIntentEnvelope intent, string message)
    {
        return new BridgeExecutionResult
        {
            Success = false,
            Stdout = "",
            ErrorTraceback = message,
            Telemetry = new Dictionary<string, object?> { ["intentId"] = intent.IntentId, ["command"] = intent.Command },
        };
    }
}
