using System.Net;
using System.Text;
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
    _ = Task.Run(() => HandleRequest(ctx, fingerprints, jsonOptions));
}

static async Task HandleRequest(HttpListenerContext ctx, ModelFingerprintStore fingerprints, JsonSerializerOptions jsonOptions)
{
    var req = ctx.Request;
    var res = ctx.Response;
    try
    {
        var path = req.Url?.AbsolutePath ?? "";
        if (req.HttpMethod == "GET" && path == "/v1/model-fingerprint")
        {
            var body = JsonSerializer.Serialize(new { modelFingerprint = fingerprints.Current });
            await WriteJson(res, 200, body);
            return;
        }

        if (req.HttpMethod == "POST" && path == "/v1/execute")
        {
            using var reader = new StreamReader(req.InputStream, req.ContentEncoding);
            var raw = await reader.ReadToEndAsync();
            var intent = JsonSerializer.Deserialize<CadIntentEnvelope>(raw, jsonOptions);
            if (intent is null || string.IsNullOrEmpty(intent.Command))
            {
                await WriteJson(res, 400, """{"error":"invalid intent"}""");
                return;
            }

            var result = IntentRouter.Execute(intent, fingerprints);
            var outJson = JsonSerializer.Serialize(result);
            await WriteJson(res, 200, outJson);
            return;
        }

        res.StatusCode = 404;
        res.Close();
    }
    catch (Exception ex)
    {
        try
        {
            await WriteJson(res, 500, JsonSerializer.Serialize(new { error = ex.Message }));
        }
        catch
        {
            res.Abort();
        }
    }
}

static async Task WriteJson(HttpListenerResponse res, int status, string json)
{
    res.StatusCode = status;
    res.ContentType = "application/json; charset=utf-8";
    var buf = Encoding.UTF8.GetBytes(json);
    res.ContentLength64 = buf.Length;
    await res.OutputStream.WriteAsync(buf);
    res.Close();
}
