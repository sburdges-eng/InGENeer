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

        return intent.Command switch
        {
            "NoOp" => BridgeExecutionResult.Ok(intent, "NoOp"),
            "PingHost" => BridgeExecutionResult.Ok(intent, "PingHost", t =>
            {
                t["hostId"] = s_hostAssembly;
                t["build"] = s_hostVersion;
            }),
            "GetModelFingerprint" => BridgeExecutionResult.Ok(intent, "GetModelFingerprint", t =>
            {
                t["modelFingerprint"] = fingerprints.Current;
            }),
            _ => BridgeExecutionResult.Fail(intent, $"unknown command: {intent.Command}"),
        };
    }
}
