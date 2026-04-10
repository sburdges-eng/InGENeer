using System.Net;
using System.Text.Json;
using InGENeer.IcadBridge;

var prefix = Environment.GetEnvironmentVariable("INGENEER_BRIDGE_URL") ?? "http://127.0.0.1:8765/";
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

var listener = new HttpListener();
listener.Prefixes.Add(prefix);
listener.Start();
Console.WriteLine($"InGENeer loopback bridge listening on {prefix}");
Console.WriteLine("POST /v1/execute  GET /v1/model-fingerprint  (Ctrl+C to stop)");

var jsonOptions = new JsonSerializerOptions { PropertyNameCaseInsensitive = true };

while (true)
{
    var ctx = await listener.GetContextAsync();
    _ = Task.Run(() => BridgeHttpTransport.HandleRequestAsync(ctx, fingerprints, pathResolver, validationOptions, jsonOptions));
}
