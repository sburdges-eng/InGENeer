namespace InGENeer.IcadBridge;

/// <summary>
/// Service to resolve virtual path keys (e.g. from intent parameters) 
/// to actual filesystem paths.
/// </summary>
public interface IPathResolver
{
    /// <summary>
    /// Resolves a key to an absolute or relative path string.
    /// Returns null if the key is unknown.
    /// </summary>
    string? ResolvePath(string key);
}

/// <summary>
/// Simple dictionary-backed path resolver for tests and loopback host.
/// </summary>
public sealed class SimplePathResolver : IPathResolver
{
    private readonly Dictionary<string, string> _paths = new(StringComparer.OrdinalIgnoreCase);

    public void Add(string key, string path) => _paths[key] = path;

    public string? ResolvePath(string key) => _paths.TryGetValue(key, out var path) ? path : null;
}
