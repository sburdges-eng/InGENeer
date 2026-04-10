using Json.Schema;
using System.Text.Json;

namespace InGENeer.IcadBridge;

/// <summary>
/// Options for bridge-side intent validation.
/// </summary>
public sealed class BridgeValidationOptions
{
    /// <summary>
    /// If true, command-specific parameter schemas are loaded and enforced.
    /// </summary>
    public bool EnforceParameterSchema { get; set; } = true;

    /// <summary>
    /// Base directory containing the 'params/' schema folder.
    /// </summary>
    public string? SchemaDirectory { get; set; }
}

/// <summary>
/// Internal cache for JSON schemas to avoid disk I/O on every intent.
/// </summary>
internal static class SchemaRegistry
{
    private static readonly Dictionary<string, JsonSchema> s_cache = new(StringComparer.OrdinalIgnoreCase);
    private static readonly object s_lock = new();

    public static JsonSchema? GetSchema(string command, string? schemaDir)
    {
        if (string.IsNullOrEmpty(schemaDir)) return null;

        lock (s_lock)
        {
            if (s_cache.TryGetValue(command, out var schema)) return schema;

            var path = Path.Combine(schemaDir, "params", $"{command}.schema.json");
            if (!File.Exists(path)) return null;

            try
            {
                var content = File.ReadAllText(path);
                schema = JsonSchema.FromText(content);
                s_cache[command] = schema;
                return schema;
            }
            catch
            {
                return null;
            }
        }
    }
}

/// <summary>
/// Orchestration layer: fingerprint pre-checks, risk/confirmation gates, mode parsing.
/// Delegates command execution to an <see cref="ICadHostExecutor"/>.
/// </summary>
public static class IntentRouter
{
    private static readonly HashSet<string> s_highRiskCommands = new(StringComparer.Ordinal)
    {
        "HighRiskStub",
        "DrawPolylineFromCoordinates",
        "CreatePointBlocks",
        "ImportLandXmlSurface",
        "CreateAlignment",
        "CreateProfile",
        "CreateCrossSection",
        "CreateCorridorModel",
        "BalanceGrading",
        "CreateRetentionPond",
        "CreateSanitarySewerNetwork",
        "PlacePlantingLayout",
        "CreatePavingArea",
        "DesignIrrigationZone",
    };

    /// <summary>
    /// Validate, authorize, and execute an intent.
    /// </summary>
    /// <param name="intent">The intent envelope from the orchestrator.</param>
    /// <param name="fingerprints">Model fingerprint store for stale-document checks.</param>
    /// <param name="host">
    /// CAD host executor. Defaults to <see cref="MockCadHost.Instance"/> (in-process stubs).
    /// Pass a <see cref="TeighaCadHost"/> when running inside a real CAD host.
    /// </param>
    /// <param name="options">Validation options (schema enforcement, etc.).</param>
    public static BridgeExecutionResult Execute(
        CadIntentEnvelope intent,
        ModelFingerprintStore fingerprints,
        ICadHostExecutor? host = null,
        BridgeValidationOptions? options = null)
    {
        host ??= MockCadHost.Instance;
        options ??= new BridgeValidationOptions { EnforceParameterSchema = false };

        // 1. Stale-document guard: reject if expected fingerprint doesn't match live state.
        var live = fingerprints.Snapshot();
        if (!string.IsNullOrWhiteSpace(intent.ModelFingerprintExpected)
            && !string.Equals(
                intent.ModelFingerprintExpected.Trim(),
                live,
                StringComparison.Ordinal))
        {
            return BridgeExecutionResult.Fail(
                intent,
                "modelFingerprintExpected does not match live model fingerprint (stale document)");
        }

        var mode = string.IsNullOrWhiteSpace(intent.ExecutionMode) ? "execute" : intent.ExecutionMode.Trim();

        // 2. High-risk gate: execute mode requires human confirmation token.
        if (s_highRiskCommands.Contains(intent.Command)
            && string.Equals(mode, "execute", StringComparison.OrdinalIgnoreCase)
            && string.IsNullOrWhiteSpace(intent.HumanConfirmationToken))
        {
            return BridgeExecutionResult.Fail(intent, $"{intent.Command} in execute mode requires humanConfirmationToken");
        }

        // 3. L7 Parameter Validation: JSON Schema enforcement.
        if (options.EnforceParameterSchema && !string.IsNullOrEmpty(options.SchemaDirectory))
        {
            var schema = SchemaRegistry.GetSchema(intent.Command, options.SchemaDirectory);
            if (schema != null)
            {
                var result = schema.Evaluate(intent.Parameters, new EvaluationOptions 
                { 
                    OutputFormat = OutputFormat.List 
                });

                if (!result.IsValid)
                {
                    var error = result.Details?
                        .Where(d => !d.IsValid && d.Errors != null)
                        .SelectMany(d => d.Errors!.Values)
                        .FirstOrDefault(); // Just pick the first error for the Fail message

                    return BridgeExecutionResult.Fail(
                        intent, 
                        $"Parameter validation failed for {intent.Command}: {error ?? "unknown schema error"}");
                }
            }
        }

        return host.ExecuteCommand(intent, mode, fingerprints);
    }
}
