using System.Net;
using System.Net.Sockets;
using System.Text.Json;
using InGENeer.IcadBridge;

var prefix = Environment.GetEnvironmentVariable("INGENEER_BRIDGE_URL") ?? "http://127.0.0.1:8765/";

// --- Port-conflict detection ---
if (Uri.TryCreate(prefix, UriKind.Absolute, out var parsed))
{
    var port = parsed.Port;
    try
    {
        using var probe = new TcpClient();
        probe.Connect(IPAddress.Loopback, port);
        // Connection succeeded → something already owns this port.
        Console.Error.WriteLine($"ERROR: Port {port} is already in use. Another LoopbackHost instance or service may be running.");
        Console.Error.WriteLine("Kill the existing process or set INGENEER_BRIDGE_URL to a different port.");
        Environment.Exit(1);
    }
    catch (SocketException)
    {
        // Connection refused → port is free, carry on.
    }
}

var fingerprints = new ModelFingerprintStore();
var pathResolver = new SimplePathResolver();
pathResolver.Add("surface_file", "C:\\Data\\Existing_Ground.xml");

// Determine schema directory relative to binary:
// In repo: icad-addin/InGENeer.Bridge.LoopbackHost/bin/Debug/net10.0/
// To reach schemas/: ../../../../../../schemas
var schemaDir = Path.GetFullPath(Path.Combine(AppContext.BaseDirectory, "../../../../../schemas"));
var validationOptions = new BridgeValidationOptions
{
    EnforceParameterSchema = true,
    SchemaDirectory = Directory.Exists(schemaDir) ? schemaDir : null
};

if (validationOptions.SchemaDirectory == null)
{
    Console.WriteLine($"Warning: Schema directory not found at {schemaDir}. L7 validation will be skipped.");
}
else
{
    Console.WriteLine($"L7 Validation enabled. Schemas at: {validationOptions.SchemaDirectory}");
}

HttpListener listener;
try
{
    listener = new HttpListener();
    listener.Prefixes.Add(prefix);
    listener.Start();
}
catch (HttpListenerException ex)
{
    Console.Error.WriteLine($"ERROR: Failed to start HTTP listener on {prefix}: {ex.Message}");
    if (ex.ErrorCode == 48 || ex.Message.Contains("address already in use", StringComparison.OrdinalIgnoreCase))
    {
        Console.Error.WriteLine("The port is already bound. Check for a zombie LoopbackHost process:");
        Console.Error.WriteLine($"  lsof -i :{parsed?.Port ?? 8765}");
    }
    Environment.Exit(1);
    return; // unreachable but satisfies definite-assignment analysis
}
catch (Exception ex)
{
    Console.Error.WriteLine($"ERROR: Unexpected failure starting bridge: {ex}");
    Environment.Exit(1);
    return;
}

Console.WriteLine($"InGENeer loopback bridge listening on {prefix}");
Console.WriteLine("POST /v1/execute  GET /v1/model-fingerprint  (Ctrl+C to stop)");

var jsonOptions = new JsonSerializerOptions { PropertyNameCaseInsensitive = true };

while (true)
{
    var ctx = await listener.GetContextAsync();
    _ = Task.Run(() => BridgeHttpTransport.HandleRequestAsync(ctx, fingerprints, pathResolver, validationOptions, jsonOptions));
}
