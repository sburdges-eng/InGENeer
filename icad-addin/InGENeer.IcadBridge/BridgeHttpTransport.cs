using System.Net;
using System.Text;
using System.Text.Json;

namespace InGENeer.IcadBridge;

/// <summary>
/// HTTP surface for the loopback bridge: baseline sync (GET) and dispatch (POST).
/// Matches docs/BRIDGE_TRANSPORT.md.
/// </summary>
public static class BridgeHttpTransport
{
    public static async Task HandleRequestAsync(
        HttpListenerContext ctx,
        ModelFingerprintStore fingerprints,
        JsonSerializerOptions jsonOptions)
    {
        var req = ctx.Request;
        var res = ctx.Response;
        try
        {
            var path = req.Url?.AbsolutePath ?? "";
            if (req.HttpMethod == "GET" && path == "/v1/model-fingerprint")
            {
                await WriteModelFingerprintAsync(res, fingerprints).ConfigureAwait(false);
                return;
            }

            if (req.HttpMethod == "POST" && path == "/v1/execute")
            {
                await HandleExecuteAsync(req, res, fingerprints, jsonOptions).ConfigureAwait(false);
                return;
            }

            res.StatusCode = 404;
            res.Close();
        }
        catch (Exception ex)
        {
            try
            {
                await WriteJsonAsync(res, 500, JsonSerializer.Serialize(new { error = ex.Message }))
                    .ConfigureAwait(false);
            }
            catch
            {
                res.Abort();
            }
        }
    }

    private static async Task WriteModelFingerprintAsync(HttpListenerResponse res, ModelFingerprintStore fingerprints)
    {
        var body = JsonSerializer.Serialize(new { modelFingerprint = fingerprints.Snapshot() });
        await WriteJsonAsync(res, 200, body).ConfigureAwait(false);
    }

    private static async Task HandleExecuteAsync(
        HttpListenerRequest req,
        HttpListenerResponse res,
        ModelFingerprintStore fingerprints,
        JsonSerializerOptions jsonOptions)
    {
        using var reader = new StreamReader(req.InputStream, req.ContentEncoding);
        var raw = await reader.ReadToEndAsync().ConfigureAwait(false);
        var intent = JsonSerializer.Deserialize<CadIntentEnvelope>(raw, jsonOptions);
        if (intent is null || string.IsNullOrEmpty(intent.Command))
        {
            await WriteJsonAsync(res, 400, """{"error":"invalid intent"}""").ConfigureAwait(false);
            return;
        }

        var result = IntentRouter.Execute(intent, fingerprints);
        var outJson = JsonSerializer.Serialize(result);
        await WriteJsonAsync(res, 200, outJson).ConfigureAwait(false);
    }

    private static async Task WriteJsonAsync(HttpListenerResponse res, int status, string json)
    {
        res.StatusCode = status;
        res.ContentType = "application/json; charset=utf-8";
        var buf = Encoding.UTF8.GetBytes(json);
        res.ContentLength64 = buf.Length;
        await res.OutputStream.WriteAsync(buf).ConfigureAwait(false);
        res.Close();
    }
}
