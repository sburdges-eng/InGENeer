using System.Reflection;
using System.Text.Json;

namespace InGENeer.IcadBridge;

/// <summary>
/// MVP command dispatch for catalog commands. No Carlson/ITC API calls here—only wire semantics.
/// TODO: Inside iCAD, run <see cref="Execute"/> on the main UI thread and wrap document mutations in host transactions.
/// </summary>
public static class IntentRouter
{
    private static readonly Assembly s_asm = typeof(IntentRouter).Assembly;
    private static readonly string s_hostAssembly = s_asm.GetName().Name ?? "InGENeer.IcadBridge";
    private static readonly string s_hostVersion = s_asm.GetName().Version?.ToString() ?? "0.0.0";

    public static BridgeExecutionResult Execute(CadIntentEnvelope intent, ModelFingerprintStore fingerprints)
    {
        if (intent.Parameters.ValueKind != JsonValueKind.Undefined
            && intent.Parameters.ValueKind != JsonValueKind.Null
            && intent.Parameters.TryGetProperty("_bridge_execute_fail", out var failEl)
            && failEl.ValueKind == JsonValueKind.True)
        {
            return BridgeExecutionResult.Fail(intent, "mock failure (_bridge_execute_fail)");
        }

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

        var fpBefore = live;
        var mode = string.IsNullOrWhiteSpace(intent.ExecutionMode) ? "execute" : intent.ExecutionMode.Trim();
        var isHighRisk = string.Equals(intent.Command, "HighRiskStub", StringComparison.Ordinal)
                         || string.Equals(intent.Command, "DrawPolylineFromCoordinates", StringComparison.Ordinal)
                         || string.Equals(intent.Command, "CreatePointBlocks", StringComparison.Ordinal)
                         || string.Equals(intent.Command, "ImportLandXmlSurface", StringComparison.Ordinal);

        if (isHighRisk
            && string.Equals(mode, "execute", StringComparison.OrdinalIgnoreCase)
            && string.IsNullOrWhiteSpace(intent.HumanConfirmationToken))
        {
            return BridgeExecutionResult.Fail(intent, $"{intent.Command} in execute mode requires humanConfirmationToken");
        }

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
                // Stub: real host queries the named surface from the document.
                t["point_count"] = 1024;
                t["triangle_count"] = 2000;
                t["bounds"] = new[] { new[] { 0.0, 0.0, 0.0 }, new[] { 1000.0, 1000.0, 50.0 } };
            }),
            _ => BridgeExecutionResult.Fail(intent, $"unknown command: {intent.Command}"),
        };
    }
}
