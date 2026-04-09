using System.Globalization;
using System.Reflection;
using System.Text.Json;
using System.Threading;

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
    private static int s_mutationSeq;

    public static BridgeExecutionResult Execute(CadIntentEnvelope intent, ModelFingerprintStore fingerprints)
    {
        if (intent.Parameters.ValueKind != JsonValueKind.Undefined
            && intent.Parameters.ValueKind != JsonValueKind.Null
            && intent.Parameters.TryGetProperty("_bridge_execute_fail", out var failEl)
            && failEl.ValueKind == JsonValueKind.True)
        {
            return BridgeExecutionResult.Fail(intent, "mock failure (_bridge_execute_fail)");
        }

        var mode = string.IsNullOrWhiteSpace(intent.ExecutionMode) ? "execute" : intent.ExecutionMode.Trim();
        var isHighRisk = string.Equals(intent.Command, "HighRiskStub", StringComparison.Ordinal)
                         || string.Equals(intent.Command, "CreatePointBlock", StringComparison.Ordinal);

        if (isHighRisk
            && string.Equals(mode, "execute", StringComparison.OrdinalIgnoreCase)
            && string.IsNullOrWhiteSpace(intent.HumanConfirmationToken))
        {
            return BridgeExecutionResult.Fail(intent, $"{intent.Command} in execute mode requires humanConfirmationToken");
        }

        var fpBefore = fingerprints.Current;
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
                fingerprints.Bump("m" + Interlocked.Increment(ref s_mutationSeq).ToString(CultureInfo.InvariantCulture));
                t["modelFingerprintAfter"] = fingerprints.Current;
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
                t["modelFingerprint"] = fingerprints.Current;
            }),
            "HighRiskStub" => BridgeExecutionResult.Ok(intent, $"HighRiskStub:{mode}", AddModeTelemetry),
            "CreatePointBlock" => BridgeExecutionResult.Ok(intent, $"CreatePointBlock:{mode}", AddModeTelemetry),
            _ => BridgeExecutionResult.Fail(intent, $"unknown command: {intent.Command}"),
        };
    }
}
