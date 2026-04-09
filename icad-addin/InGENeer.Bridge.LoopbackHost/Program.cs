using System.Net;
using System.Text.Json;
using InGENeer.IcadBridge;

var prefix = Environment.GetEnvironmentVariable("INGENEER_BRIDGE_URL") ?? "http://127.0.0.1:8765/";
var fingerprints = new ModelFingerprintStore();

var listener = new HttpListener();
listener.Prefixes.Add(prefix);
listener.Start();
Console.WriteLine($"InGENeer loopback bridge listening on {prefix}");
Console.WriteLine("POST /v1/execute  GET /v1/model-fingerprint  (Ctrl+C to stop)");

var jsonOptions = new JsonSerializerOptions { PropertyNameCaseInsensitive = true };

while (true)
{
    var ctx = await listener.GetContextAsync();
    _ = Task.Run(() => BridgeHttpTransport.HandleRequestAsync(ctx, fingerprints, jsonOptions));
}
