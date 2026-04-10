using System.Text.Json;
using Xunit;
using InGENeer.IcadBridge;

namespace InGENeer.IcadBridge.Tests;

public class IntentRouterTests
{
    private readonly ModelFingerprintStore _fingerprints = new();
    private readonly string _schemaDir;

    public IntentRouterTests()
    {
        // Resolve schemas/ directory from test execution path
        // Bin: icad-addin/InGENeer.IcadBridge.Tests/bin/{Config}/net10.0/
        // Repo root: 5 levels up
        _schemaDir = Path.GetFullPath(Path.Combine(AppContext.BaseDirectory, "../../../../../schemas"));
    }

    private BridgeValidationOptions SchemaEnabled => new()
    {
        EnforceParameterSchema = true,
        SchemaDirectory = _schemaDir,
    };

    private static BridgeValidationOptions SchemaDisabled => new()
    {
        EnforceParameterSchema = false,
    };

    [Fact]
    public void Execute_ValidCreateAlignment_ReturnsOk()
    {
        var intent = new CadIntentEnvelope
        {
            IntentId = "test-1",
            Command = "CreateAlignment",
            Parameters = JsonDocument.Parse("""
            {
                "name": "Test Alignment",
                "points": [[0,0,0], [100,100,0]],
                "start_station": 0,
                "layer": "ALIGNMENT"
            }
            """).RootElement,
            ExecutionMode = "dry_run",
        };

        var result = IntentRouter.Execute(intent, _fingerprints, MockCadHost.Instance, SchemaEnabled);

        Assert.True(result.Success, $"Validation should pass but failed: {result.ErrorTraceback}");
    }

    [Fact]
    public void Execute_MissingRequiredField_ReturnsFail()
    {
        var intent = new CadIntentEnvelope
        {
            IntentId = "test-2",
            Command = "CreateAlignment",
            Parameters = JsonDocument.Parse("""
            {
                "name": "Missing Points",
                "start_station": 0,
                "layer": "ALIGNMENT"
            }
            """).RootElement,
            ExecutionMode = "dry_run",
        };

        var result = IntentRouter.Execute(intent, _fingerprints, MockCadHost.Instance, SchemaEnabled);

        Assert.False(result.Success);
        Assert.Contains("Parameter validation failed", result.ErrorTraceback);
    }

    [Fact]
    public void Execute_WrongType_ReturnsFail()
    {
        var intent = new CadIntentEnvelope
        {
            IntentId = "test-3",
            Command = "CreateAlignment",
            Parameters = JsonDocument.Parse("""
            {
                "name": "Bad Points",
                "points": "not-an-array",
                "start_station": 0,
                "layer": "ALIGNMENT"
            }
            """).RootElement,
            ExecutionMode = "dry_run",
        };

        var result = IntentRouter.Execute(intent, _fingerprints, MockCadHost.Instance, SchemaEnabled);

        Assert.False(result.Success);
        Assert.Contains("Parameter validation failed", result.ErrorTraceback);
    }

    [Fact]
    public void Execute_TooFewItems_ReturnsFail()
    {
        var intent = new CadIntentEnvelope
        {
            IntentId = "test-4",
            Command = "CreateAlignment",
            Parameters = JsonDocument.Parse("""
            {
                "name": "Too Few Points",
                "points": [[0,0,0]],
                "start_station": 0,
                "layer": "ALIGNMENT"
            }
            """).RootElement,
            ExecutionMode = "dry_run",
        };

        var result = IntentRouter.Execute(intent, _fingerprints, MockCadHost.Instance, SchemaEnabled);

        Assert.False(result.Success);
        Assert.Contains("Parameter validation failed", result.ErrorTraceback);
    }

    [Fact]
    public void Execute_SchemaDisabled_SkipsParamValidation()
    {
        // Invalid params should still succeed when schema enforcement is off
        var intent = new CadIntentEnvelope
        {
            IntentId = "test-5",
            Command = "CreateAlignment",
            Parameters = JsonDocument.Parse("""{ "garbage": true }""").RootElement,
            ExecutionMode = "dry_run",
        };

        var result = IntentRouter.Execute(intent, _fingerprints, MockCadHost.Instance, SchemaDisabled);

        Assert.True(result.Success);
    }

    [Fact]
    public void Execute_NoSchemaFile_PassesThrough()
    {
        // NoOp has a trivial schema with no required fields — empty params should pass
        var intent = new CadIntentEnvelope
        {
            IntentId = "test-6",
            Command = "NoOp",
            Parameters = JsonDocument.Parse("{}").RootElement,
            ExecutionMode = "execute",
        };

        var result = IntentRouter.Execute(intent, _fingerprints, MockCadHost.Instance, SchemaEnabled);

        Assert.True(result.Success);
    }
}
