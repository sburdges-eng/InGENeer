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
            "NoOp" => BridgeExecutionResult.Ok(intent, $"NoOp:{mode}", t =>
            {
                AddModeTelemetry(t);
                t["status"] = "nop_success";
            }),
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
            "HighRiskStub" => BridgeExecutionResult.Ok(intent, $"HighRiskStub:{mode}", t =>
            {
                AddModeTelemetry(t);
                t["stub_executed"] = true;
            }),
            "DrawPolylineFromCoordinates" => BridgeExecutionResult.Ok(intent, $"DrawPolylineFromCoordinates:{mode}", t =>
            {
                AddModeTelemetry(t);
                t["length"] = 123.45;
                t["point_count"] = intent.Parameters.TryGetProperty("points", out var pts) && pts.ValueKind == JsonValueKind.Array ? pts.GetArrayLength() : 0;
            }),
            "CreatePointBlocks" => BridgeExecutionResult.Ok(intent, $"CreatePointBlocks:{mode}", t =>
            {
                AddModeTelemetry(t);
                t["point_count"] = intent.Parameters.TryGetProperty("points", out var pts) && pts.ValueKind == JsonValueKind.Array ? pts.GetArrayLength() : 0;
            }),
            "ImportLandXmlSurface" => BridgeExecutionResult.Ok(intent, $"ImportLandXmlSurface:{mode}", t =>
            {
                AddModeTelemetry(t);
                t["surface_name"] = intent.Parameters.TryGetProperty("surface_name", out var sn) ? sn.GetString() : "unknown";
                t["point_count"] = 500;
                t["triangle_count"] = 950;
            }),
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
            "CreateCrossSection" => BridgeExecutionResult.Ok(intent, $"CreateCrossSection:{mode}", t =>
            {
                AddModeTelemetry(t);
                t["station_count"] = 5;
            }),
            "CreateCorridorModel" => BridgeExecutionResult.Ok(intent, $"CreateCorridorModel:{mode}", t =>
            {
                AddModeTelemetry(t);
                t["corridor_length"] = 538.52;
            }),
            "BalanceGrading" => BridgeExecutionResult.Ok(intent, $"BalanceGrading:{mode}", t =>
            {
                AddModeTelemetry(t);
                t["cut_volume"] = 1250.0;
                t["fill_volume"] = 1245.0;
                t["net_volume"] = 5.0;
                t["balanced"] = true;
            }),
            "CreateRetentionPond" => BridgeExecutionResult.Ok(intent, $"CreateRetentionPond:{mode}", t =>
            {
                AddModeTelemetry(t);
                t["pond_volume"] = 4500.0;
                t["surface_area"] = 12000.0;
            }),
            "CreateSanitarySewerNetwork" => BridgeExecutionResult.Ok(intent, $"CreateSanitarySewerNetwork:{mode}", t =>
            {
                AddModeTelemetry(t);
                t["structure_count"] = 2;
                t["total_pipe_length"] = 250.0;
            }),
            "AnalyzeStormDrainage" => BridgeExecutionResult.Ok(intent, $"AnalyzeStormDrainage:{mode}", t =>
            {
                AddModeTelemetry(t);
                t["peak_discharge"] = 12.5;
                t["max_velocity"] = 4.2;
                t["capacity_exceeded"] = false;
            }),
            "PlacePlantingLayout" => BridgeExecutionResult.Ok(intent, $"PlacePlantingLayout:{mode}", t =>
            {
                AddModeTelemetry(t);
                t["plant_count"] = 2;
                t["canopy_coverage_area"] = 2513.27;
            }),
            "CreatePavingArea" => BridgeExecutionResult.Ok(intent, $"CreatePavingArea:{mode}", t =>
            {
                AddModeTelemetry(t);
                t["paving_area"] = 2500.0;
                t["perimeter_length"] = 200.0;
            }),
            "DesignIrrigationZone" => BridgeExecutionResult.Ok(intent, $"DesignIrrigationZone:{mode}", t =>
            {
                AddModeTelemetry(t);
                t["head_count"] = 2;
                t["total_flow_gpm"] = 12.4;
                t["pipe_length"] = 35.0;
            }),
            _ => BridgeExecutionResult.Fail(intent, $"unknown command: {intent.Command}"),
        };
    }
}
