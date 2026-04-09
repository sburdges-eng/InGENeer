using System.Reflection;
using System.Text.Json;

namespace InGENeer.IcadBridge;

/// <summary>
/// In-process mock host: returns stub data for all catalog commands.
/// No CAD host required. Used by the loopback bridge and tests.
/// </summary>
public sealed class MockCadHost : ICadHostExecutor
{
    public static readonly MockCadHost Instance = new();

    private static readonly Assembly s_asm = typeof(MockCadHost).Assembly;
    private static readonly string s_hostAssembly = s_asm.GetName().Name ?? "InGENeer.IcadBridge";
    private static readonly string s_hostVersion = s_asm.GetName().Version?.ToString() ?? "0.0.0";

    public BridgeExecutionResult ExecuteCommand(
        CadIntentEnvelope intent,
        string resolvedMode,
        ModelFingerprintStore fingerprints)
    {
        // Test hook: force failure when _bridge_execute_fail is set.
        if (intent.Parameters.ValueKind != JsonValueKind.Undefined
            && intent.Parameters.ValueKind != JsonValueKind.Null
            && intent.Parameters.TryGetProperty("_bridge_execute_fail", out var failEl)
            && failEl.ValueKind == JsonValueKind.True)
        {
            return BridgeExecutionResult.Fail(intent, "mock failure (_bridge_execute_fail)");
        }

        var fpBefore = fingerprints.Snapshot();
        var mode = resolvedMode;

        void AddModeTelemetry(Dictionary<string, object?> t)
        {
            t["executionMode"] = mode;
            if (mode is "dry_run" or "preview")
            {
                t["modelFingerprintAfter"] = fpBefore;
                t["plannedSummary"] = $"{intent.Command}:{mode}";
            }
            else
            {
                fingerprints.CommitAfterSuccessfulExecute(intent.IntentId, intent.Command);
                t["modelFingerprintAfter"] = fingerprints.Snapshot();
                t["plannedSummary"] = "";
            }
        }

        return intent.Command switch
        {
            "NoOp" => BridgeExecutionResult.Ok(intent, $"NoOp:{mode}", AddModeTelemetry),
            "PingHost" => BridgeExecutionResult.Ok(intent, $"PingHost:{mode}", t =>
            {
                AddModeTelemetry(t);
                t["hostId"] = s_hostAssembly;
                t["build"] = s_hostVersion;
            }),
            "GetModelFingerprint" => BridgeExecutionResult.Ok(intent, $"GetModelFingerprint:{mode}", t =>
            {
                AddModeTelemetry(t);
                t["modelFingerprint"] = fingerprints.Snapshot();
            }),
            "HighRiskStub" => BridgeExecutionResult.Ok(intent, $"HighRiskStub:{mode}", AddModeTelemetry),
            "DrawPolylineFromCoordinates" => BridgeExecutionResult.Ok(intent, $"DrawPolylineFromCoordinates:{mode}", AddModeTelemetry),
            "CreatePointBlocks" => BridgeExecutionResult.Ok(intent, $"CreatePointBlocks:{mode}", AddModeTelemetry),
            "ImportLandXmlSurface" => BridgeExecutionResult.Ok(intent, $"ImportLandXmlSurface:{mode}", AddModeTelemetry),
            "VerifySurface" => BridgeExecutionResult.Ok(intent, $"VerifySurface:{mode}", t =>
            {
                AddModeTelemetry(t);
                t["point_count"] = 1024;
                t["triangle_count"] = 2000;
                t["bounds"] = new[] { new[] { 0.0, 0.0, 0.0 }, new[] { 1000.0, 1000.0, 50.0 } };
            }),
            "CreateAlignment" => BridgeExecutionResult.Ok(intent, $"CreateAlignment:{mode}", t =>
            {
                AddModeTelemetry(t);
                t["length"] = 538.52;
                t["station_range"] = new[] { 0.0, 538.52 };
            }),
            "CreateProfile" => BridgeExecutionResult.Ok(intent, $"CreateProfile:{mode}", t =>
            {
                AddModeTelemetry(t);
                t["pvi_count"] = 3;
                t["elevation_range"] = new[] { 100.0, 105.0 };
            }),
            _ => BridgeExecutionResult.Fail(intent, $"unknown command: {intent.Command}"),
        };
    }
}
