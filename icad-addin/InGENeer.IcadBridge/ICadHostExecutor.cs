namespace InGENeer.IcadBridge;

/// <summary>
/// Abstraction over CAD host command execution.
/// <para>
/// Implementations handle command dispatch, execution-mode logic (dry_run/preview/execute),
/// fingerprint commits, and telemetry building. The caller (<see cref="IntentRouter"/>)
/// handles orchestration: fingerprint pre-checks, risk/confirmation gates, mode parsing.
/// </para>
/// <para>
/// <see cref="MockCadHost"/>: In-process stubs (no CAD host required). Default for loopback bridge and tests.
/// <see cref="TeighaCadHost"/>: Real Carlson/IntelliCAD execution via Teigha.DatabaseServices (requires host loaded).
/// </para>
/// </summary>
public interface ICadHostExecutor
{
    /// <summary>
    /// Execute a validated, authorized intent against the CAD host.
    /// Called after fingerprint and high-risk checks have passed.
    /// </summary>
    /// <param name="intent">The validated intent envelope.</param>
    /// <param name="resolvedMode">Parsed execution mode: "execute", "dry_run", or "preview".</param>
    /// <param name="fingerprints">Fingerprint store for post-mutation commits and telemetry.</param>
    /// <returns>A <see cref="BridgeExecutionResult"/> with success/fail, telemetry, and invariants.</returns>
    BridgeExecutionResult ExecuteCommand(
        CadIntentEnvelope intent,
        string resolvedMode,
        ModelFingerprintStore fingerprints);
}
