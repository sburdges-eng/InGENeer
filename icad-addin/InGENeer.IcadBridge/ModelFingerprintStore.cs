namespace InGENeer.IcadBridge;

/// <summary>
/// Holds the current document fingerprint for <see cref="IntentRouter"/> / GET /v1/model-fingerprint.
/// TODO (real add-in): update from Carlson/ITC APIs when the drawing changes; read only on UI thread.
/// </summary>
public sealed class ModelFingerprintStore
{
    private string _value;

    public ModelFingerprintStore(string initial = "loopback-stub-fingerprint")
    {
        _value = initial;
    }

    public string Current => _value;

    public void Set(string value) => _value = value;
}
