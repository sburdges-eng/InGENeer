namespace InGENeer.IcadBridge;

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
    public static BridgeExecutionResult Execute(
        CadIntentEnvelope intent,
        ModelFingerprintStore fingerprints,
        ICadHostExecutor? host = null)
    {
        host ??= MockCadHost.Instance;

        // Stale-document guard: reject if expected fingerprint doesn't match live state.
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

        // High-risk gate: execute mode requires human confirmation token.
        if (s_highRiskCommands.Contains(intent.Command)
            && string.Equals(mode, "execute", StringComparison.OrdinalIgnoreCase)
            && string.IsNullOrWhiteSpace(intent.HumanConfirmationToken))
        {
            return BridgeExecutionResult.Fail(intent, $"{intent.Command} in execute mode requires humanConfirmationToken");
        }

        return host.ExecuteCommand(intent, mode, fingerprints);
    }
}
