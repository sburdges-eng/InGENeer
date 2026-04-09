using System.Globalization;
using System.Security.Cryptography;
using System.Text;

namespace InGENeer.IcadBridge;

/// <summary>
/// Revision-based document fingerprint for GET /v1/model-fingerprint and execute telemetry.
/// Thread-safe. Values are opaque SHA-256 hex strings (64 chars).
/// </summary>
/// <remarks>
/// Loopback host: simulates post-commit identity after each successful <c>execute</c> mutation.
/// Real iCAD add-in: replace internals with a fingerprint from documented host APIs while
/// keeping the same <see cref="Current"/> contract for the HTTP bridge.
/// </remarks>
public sealed class ModelFingerprintStore
{
    private readonly object _gate = new();
    private readonly byte[] _salt;
    private long _revision;
    private string _lastIntentId = "";
    private string _lastCommand = "";

    public ModelFingerprintStore()
    {
        _salt = RandomNumberGenerator.GetBytes(16);
        _revision = 0;
        Current = ComputeFingerprintLocked();
    }

    public string Current { get; private set; }

    /// <summary>Point-in-time read (e.g. from UI thread in a real host).</summary>
    public string Snapshot()
    {
        lock (_gate)
        {
            return Current;
        }
    }

    /// <summary>Call after a committed document mutation (execute mode only).</summary>
    public void CommitAfterSuccessfulExecute(string intentId, string command)
    {
        lock (_gate)
        {
            _revision++;
            _lastIntentId = intentId;
            _lastCommand = command;
            Current = ComputeFingerprintLocked();
        }
    }

    private string ComputeFingerprintLocked()
    {
        var payload = string.Concat(
            _revision.ToString(CultureInfo.InvariantCulture),
            "\n",
            _lastIntentId,
            "\n",
            _lastCommand,
            "\n",
            Convert.ToHexString(_salt));
        var utf8 = Encoding.UTF8.GetBytes(payload);
        var hash = SHA256.HashData(utf8);
        return Convert.ToHexString(hash).ToLowerInvariant();
    }
}
