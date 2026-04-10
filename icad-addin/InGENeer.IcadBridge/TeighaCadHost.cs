using System.Text.Json;

namespace InGENeer.IcadBridge;

#if TEIGHA
using Teigha.DatabaseServices;
using Teigha.Geometry;
using Teigha.Runtime;
#endif

/// <summary>
/// Real CAD host executor for Carlson/IntelliCAD via Teigha (ODA) .NET API.
/// <para>
/// Architecture rules enforced here:
/// <list type="bullet">
///   <item><b>Rule 2 (Thread safety):</b> All document mutations marshaled to the UI thread.</item>
///   <item><b>Rule 3 (Transactions):</b> Every mutation wrapped in a host transaction with rollback on failure.</item>
///   <item><b>Rule 4 (No API hallucinations):</b> Only use documented Teigha.DatabaseServices / Teigha.Geometry
///     methods. Unverified calls are marked <c>// TODO</c>.</item>
/// </list>
/// </para>
/// <para>
/// <b>Compile modes:</b>
/// <list type="bullet">
///   <item>Without <c>TEIGHA</c> symbol: stubs only, compiles in CI without Teigha assemblies.</item>
///   <item>With <c>TEIGHA</c> symbol + assembly references: real host execution inside iCAD.</item>
/// </list>
/// </para>
/// </summary>
public sealed class TeighaCadHost : ICadHostExecutor
{
#if TEIGHA
    private readonly SynchronizationContext? _uiContext;

    public TeighaCadHost(SynchronizationContext? uiContext = null)
    {
        _uiContext = uiContext;
    }
#endif

    public BridgeExecutionResult ExecuteCommand(
        CadIntentEnvelope intent,
        string resolvedMode,
        ModelFingerprintStore fingerprints)
    {
        return resolvedMode is "dry_run" or "preview"
            ? ExecuteDryRun(intent, resolvedMode, fingerprints)
            : ExecuteReal(intent, resolvedMode, fingerprints);
    }

    private BridgeExecutionResult ExecuteDryRun(
        CadIntentEnvelope intent,
        string mode,
        ModelFingerprintStore fingerprints)
    {
        var fpBefore = fingerprints.Snapshot();
        return BridgeExecutionResult.Ok(intent, $"{intent.Command}:{mode}", t =>
        {
            t["executionMode"] = mode;
            t["modelFingerprintAfter"] = fpBefore;
            t["plannedSummary"] = $"{intent.Command}:{mode}";
        });
    }

    private BridgeExecutionResult ExecuteReal(
        CadIntentEnvelope intent,
        string mode,
        ModelFingerprintStore fingerprints)
    {
#if TEIGHA
        // Rule 3: Wrap in host transaction with rollback on failure.
        var db = HostApplicationServices.WorkingDatabase;
        using var tr = db.TransactionManager.StartTransaction();
        try
        {
            var telemetryExtras = new Dictionary<string, object?>();

            switch (intent.Command)
            {
                case "DrawPolylineFromCoordinates":
                    ExecuteDrawPolyline(intent, tr, db, telemetryExtras);
                    break;

                case "CreatePointBlocks":
                    ExecuteCreatePointBlocks(intent, tr, db, telemetryExtras);
                    break;

                case "ImportLandXmlSurface":
                    ExecuteImportLandXmlSurface(intent, telemetryExtras);
                    break;

                case "VerifySurface":
                    ExecuteVerifySurface(intent, tr, db, telemetryExtras);
                    break;

                case "CreateAlignment":
                    // TODO: Implement with Carlson.Civil.Centerline.CreateFromPolyline
                    //   - Rule 4: Need Carlson docs for native alignment representation
                    break;

                case "CreateProfile":
                    // TODO: Implement with Carlson.Civil.Profile.CreateFromSurface
                    //   - Handle optional curve_length / k_value on PVI objects
                    //   - Rule 4: Need Carlson docs for profile creation API
                    break;

                case "CreateCrossSection":
                    // TODO: Implement cross-section template application
                    //   - Rule 4: Need Carlson docs for cross-section API
                    break;

                case "CreateCorridorModel":
                    // TODO: Implement with Carlson RoadNetwork class
                    //   - Rule 4: Need Carlson docs for corridor/roadway design API
                    break;

                case "BalanceGrading":
                    // TODO: Implement with Carlson Volume.Calculate iterative solver
                    //   - Rule 4: Need Carlson docs for surface volume/grading API
                    break;

                case "CreateRetentionPond":
                    // TODO: Implement with Carlson TemplateGrade + Surface.Intersect
                    //   - Rule 4: Need Carlson docs for pond/grading API
                    break;

                case "CreateSanitarySewerNetwork":
                    // TODO: Implement with Carlson SewerNetworkSettings
                    //   - Rule 4: Need Carlson docs for pipe network API
                    break;

                case "AnalyzeStormDrainage":
                    // TODO: Implement with Carlson SewerNetwork.Analyze()
                    //   - Rule 4: Need Carlson docs for hydrology/hydraulics API
                    break;

                case "PlacePlantingLayout":
                    // TODO: Implement with BlockReference (similar to CreatePointBlocks)
                    //   - Scale/rotation from parameters["points"]
                    //   - Attributes for species_id, container_size
                    //   - Rule 4: Need Carlson docs for native planting objects (if any)
                    break;

                case "CreatePavingArea":
                    // TODO: Implement with Polyline + Hatch or Slab entity
                    //   - Parameters for material_type, subbase_depth, permeability_coefficient
                    //   - Rule 4: Need Carlson docs for site/hardscape API
                    break;

                case "DesignIrrigationZone":
                    // TODO: Implement with IrrigationNetwork / Hydrozone class
                    //   - Parameters for target_psi, pipe_material, head_type
                    //   - Rule 4: Need Carlson docs for irrigation API
                    break;

                case "NoOp":
                case "PingHost":
                case "GetModelFingerprint":
                case "HighRiskStub":
                    // Infrastructure commands — no document mutation needed.
                    break;

                default:
                    tr.Abort();
                    return BridgeExecutionResult.Fail(intent, $"unknown command: {intent.Command}");
            }

            tr.Commit();

            fingerprints.CommitAfterSuccessfulExecute(intent.IntentId, intent.Command);

            return BridgeExecutionResult.Ok(intent, $"{intent.Command}:{mode}", t =>
            {
                t["executionMode"] = mode;
                t["modelFingerprintAfter"] = fingerprints.Snapshot();
                t["plannedSummary"] = "";
                foreach (var kv in telemetryExtras)
                    t[kv.Key] = kv.Value;
            });
        }
        catch (Exception ex)
        {
            tr.Abort();
            return BridgeExecutionResult.Fail(intent, $"host execution failed: {ex.Message}");
        }
#else
        // Stub path: no Teigha assemblies available (CI / loopback host).
        try
        {
            switch (intent.Command)
            {
                case "DrawPolylineFromCoordinates":
                case "CreatePointBlocks":
                case "ImportLandXmlSurface":
                case "VerifySurface":
                case "CreateAlignment":
                case "CreateProfile":
                case "CreateCrossSection":
                case "CreateCorridorModel":
                case "BalanceGrading":
                case "CreateRetentionPond":
                case "CreateSanitarySewerNetwork":
                case "AnalyzeStormDrainage":
                case "PlacePlantingLayout":
                case "CreatePavingArea":
                case "DesignIrrigationZone":
                case "NoOp":
                case "PingHost":
                case "GetModelFingerprint":
                case "HighRiskStub":
                    break;

                default:
                    return BridgeExecutionResult.Fail(intent, $"unknown command: {intent.Command}");
            }

            fingerprints.CommitAfterSuccessfulExecute(intent.IntentId, intent.Command);

            return BridgeExecutionResult.Ok(intent, $"{intent.Command}:{mode}", t =>
            {
                t["executionMode"] = mode;
                t["modelFingerprintAfter"] = fingerprints.Snapshot();
                t["plannedSummary"] = "";
            });
        }
        catch (Exception ex)
        {
            return BridgeExecutionResult.Fail(intent, $"host execution failed: {ex.Message}");
        }
#endif
    }

#if TEIGHA
    // --- Verified Teigha patterns (L6 implementation) ---

    /// <summary>
    /// Draw a 3D polyline using the heavyweight Polyline3d + PolylineVertex3d pattern.
    /// Verified: Teigha.DatabaseServices.Polyline3d, PolylineVertex3d, BlockTable, BlockTableRecord.
    /// </summary>
    private static void ExecuteDrawPolyline(
        CadIntentEnvelope intent,
        Transaction tr,
        Database db,
        Dictionary<string, object?> telemetry)
    {
        var parameters = intent.Parameters;

        // Open ModelSpace for write
        var bt = (BlockTable)tr.GetObject(db.BlockTableId, OpenMode.ForRead);
        var btr = (BlockTableRecord)tr.GetObject(bt[BlockTableRecord.ModelSpace], OpenMode.ForWrite);

        var pline = new Polyline3d();
        btr.AppendEntity(pline);
        tr.AddNewlyCreatedDBObject(pline, true);

        // Parse and append vertices
        if (parameters.TryGetProperty("points", out var pointsEl) && pointsEl.ValueKind == JsonValueKind.Array)
        {
            foreach (var pt in pointsEl.EnumerateArray())
            {
                var coords = new double[3];
                int i = 0;
                foreach (var c in pt.EnumerateArray())
                {
                    if (i < 3) coords[i++] = c.GetDouble();
                }
                var vertex = new PolylineVertex3d(new Point3d(coords[0], coords[1], coords[2]));
                pline.AppendVertex(vertex);
                tr.AddNewlyCreatedDBObject(vertex, true);
            }
        }

        // Set closed property
        if (parameters.TryGetProperty("closed", out var closedEl) && closedEl.GetBoolean())
        {
            pline.Closed = true;
        }

        // Set layer
        if (parameters.TryGetProperty("layer", out var layerEl))
        {
            pline.Layer = layerEl.GetString() ?? "";
        }
    }

    /// <summary>
    /// Insert Carlson-style point blocks (PT / CRD_PT) with attribute references.
    /// Verified: Teigha.DatabaseServices.BlockReference, BlockTable, AttributeReference.
    /// </summary>
    private static void ExecuteCreatePointBlocks(
        CadIntentEnvelope intent,
        Transaction tr,
        Database db,
        Dictionary<string, object?> telemetry)
    {
        var parameters = intent.Parameters;

        var bt = (BlockTable)tr.GetObject(db.BlockTableId, OpenMode.ForRead);
        var btr = (BlockTableRecord)tr.GetObject(bt[BlockTableRecord.ModelSpace], OpenMode.ForWrite);

        // Resolve block definition name
        var blockName = "PT"; // Carlson default
        if (parameters.TryGetProperty("blockName", out var bnEl))
        {
            blockName = bnEl.GetString() ?? "PT";
        }

        if (!bt.Has(blockName))
        {
            throw new InvalidOperationException($"Block definition '{blockName}' not found in BlockTable");
        }
        var blockDefId = bt[blockName];

        var layer = "";
        if (parameters.TryGetProperty("layer", out var layerEl))
        {
            layer = layerEl.GetString() ?? "";
        }

        int count = 0;
        if (parameters.TryGetProperty("points", out var pointsEl) && pointsEl.ValueKind == JsonValueKind.Array)
        {
            foreach (var ptObj in pointsEl.EnumerateArray())
            {
                // Parse location [x, y, z]
                if (!ptObj.TryGetProperty("location", out var locEl)) continue;
                var coords = new double[3];
                int i = 0;
                foreach (var c in locEl.EnumerateArray())
                {
                    if (i < 3) coords[i++] = c.GetDouble();
                }
                var insertPt = new Point3d(coords[0], coords[1], coords[2]);

                // Create BlockReference
                var blkRef = new BlockReference(insertPt, blockDefId);
                blkRef.Layer = layer;
                btr.AppendEntity(blkRef);
                tr.AddNewlyCreatedDBObject(blkRef, true);

                // Populate attributes (PNT_NO, PNT_ELEV, PNT_DESC) from the block definition
                var blkDef = (BlockTableRecord)tr.GetObject(blockDefId, OpenMode.ForRead);
                foreach (var entId in blkDef)
                {
                    var ent = tr.GetObject(entId, OpenMode.ForRead);
                    if (ent is AttributeDefinition attDef)
                    {
                        var attRef = new AttributeReference();
                        attRef.SetAttributeFromBlock(attDef, blkRef.BlockTransform);

                        // Map catalog parameter names to Carlson attribute tags
                        switch (attDef.Tag.ToUpperInvariant())
                        {
                            case "PNT_NO":
                                if (ptObj.TryGetProperty("number", out var numEl))
                                    attRef.TextString = numEl.GetInt32().ToString();
                                break;
                            case "PNT_ELEV":
                                if (ptObj.TryGetProperty("elevation", out var elevEl))
                                    attRef.TextString = elevEl.GetDouble().ToString("F3");
                                break;
                            case "PNT_DESC":
                                if (ptObj.TryGetProperty("description", out var descEl))
                                    attRef.TextString = descEl.GetString() ?? "";
                                break;
                        }

                        blkRef.AttributeCollection.AppendAttribute(attRef);
                        tr.AddNewlyCreatedDBObject(attRef, true);
                    }
                }

                count++;
            }
        }

        telemetry["point_count"] = count;
    }

    /// <summary>
    /// Import a LandXML surface via Carlson's _SURFIMPORT command-line bridge.
    /// This avoids requiring the Carlson .NET DLLs directly (Rule 4 safe bridge).
    /// Verified: Application.DocumentManager.MdiActiveDocument.SendStringToExecute.
    /// </summary>
    private static void ExecuteImportLandXmlSurface(
        CadIntentEnvelope intent,
        Dictionary<string, object?> telemetry)
    {
        var parameters = intent.Parameters;

        // The actual file path must be resolved by the bridge from the outer contract's
        // paths dictionary — the intent envelope only carries the key.
        var pathKey = "";
        if (parameters.TryGetProperty("landxml_path_key", out var pkEl))
        {
            pathKey = pkEl.GetString() ?? "";
        }

        var surfaceName = "";
        if (parameters.TryGetProperty("surface_name", out var snEl))
        {
            surfaceName = snEl.GetString() ?? "";
        }

        // TODO: Path resolution from outer contract to filesystem path
        //
        // ARCHITECTURE ISSUE: landxml_path_key is a dictionary key, not a file path.
        // The intent envelope follows the air-gapped security model and never carries
        // absolute paths. Path resolution must be injected into the host executor:
        //
        //   1. Bridge receives: CadIntentEnvelope with landxml_path_key (e.g., "surface_file")
        //   2. Bridge outer contract provides: paths { "surface_file" => "/absolute/path/file.xml" }
        //   3. Host executor must receive injected IPathResolver to map key => actual path
        //   4. Only then can we call SendStringToExecute with the resolved path
        //
        // See: docs/INTENT_COMMAND_CATALOG.md — "Path Resolution Layer (L5 ↔ L6)"
        // Status: Awaiting path resolver dependency injection.

        telemetry["surface_name"] = surfaceName;
        telemetry["import_method"] = "not_implemented:awaiting_path_resolver";

        throw new InvalidOperationException(
            "ImportLandXmlSurface requires path resolver — see INTENT_COMMAND_CATALOG.md path resolution layer design.");
    }

    /// <summary>
    /// Query a named surface in the document and return metadata (point count, triangle count, bounds).
    /// Read-only — no document mutation. Uses a read-only transaction to iterate ModelSpace.
    /// <para>
    /// <b>Surface lookup strategy:</b> Carlson stores TIN surfaces as custom entities or
    /// references external .tin files. The standard Teigha approach is to iterate ModelSpace
    /// for entities on the target layer whose XData or type matches the surface name.
    /// </para>
    /// <para>
    /// Verified Teigha API: BlockTable, BlockTableRecord iteration, Entity.Layer, Extents3d.
    /// TODO (Rule 4): Carlson.Survey.Surface / TinSurface API for native surface queries.
    /// </para>
    /// </summary>
    private static void ExecuteVerifySurface(
        CadIntentEnvelope intent,
        Transaction tr,
        Database db,
        Dictionary<string, object?> telemetry)
    {
        var parameters = intent.Parameters;

        var surfaceName = "";
        if (parameters.TryGetProperty("surface_name", out var snEl))
        {
            surfaceName = snEl.GetString() ?? "";
        }

        if (string.IsNullOrWhiteSpace(surfaceName))
        {
            throw new InvalidOperationException("VerifySurface requires non-empty surface_name parameter");
        }

        // Strategy: iterate ModelSpace entities to find surface geometry.
        // Carlson surfaces may be custom entities or standard polyfaces/3d faces on a named layer.
        // We collect point and triangle counts plus the axis-aligned bounding box.
        var bt = (BlockTable)tr.GetObject(db.BlockTableId, OpenMode.ForRead);
        var btr = (BlockTableRecord)tr.GetObject(bt[BlockTableRecord.ModelSpace], OpenMode.ForRead);

        int pointCount = 0;
        int triangleCount = 0;
        double minX = double.MaxValue, minY = double.MaxValue, minZ = double.MaxValue;
        double maxX = double.MinValue, maxY = double.MinValue, maxZ = double.MinValue;
        bool found = false;

        foreach (var entId in btr)
        {
            var ent = tr.GetObject(entId, OpenMode.ForRead) as Entity;
            if (ent == null) continue;

            // Match entities by layer name containing the surface name.
            // TODO (Rule 4): Replace with Carlson.Survey.Surface.FindByName() or
            //   TinSurface query when Carlson .NET API docs are available.
            //   Current heuristic: layer-name match covers the common case where
            //   ImportLandXmlSurface places geometry on a surface-specific layer.
            if (!string.Equals(ent.Layer, surfaceName, StringComparison.OrdinalIgnoreCase))
                continue;

            found = true;

            // Count entity types
            if (ent is PolyFaceMesh pfm)
            {
                // PolyFaceMesh contains vertices and face records
                foreach (var subId in pfm)
                {
                    var sub = tr.GetObject(subId, OpenMode.ForRead);
                    if (sub is PolyFaceMeshVertex vtx)
                    {
                        pointCount++;
                        UpdateBounds(vtx.Position, ref minX, ref minY, ref minZ, ref maxX, ref maxY, ref maxZ);
                    }
                    else if (sub is FaceRecord)
                    {
                        triangleCount++;
                    }
                }
            }
            else if (ent is Face face)
            {
                // Individual 3D face = 1 triangle, up to 4 vertices
                triangleCount++;
                for (short i = 0; i < 4; i++)
                {
                    var pt = face.GetVertexAt(i);
                    UpdateBounds(pt, ref minX, ref minY, ref minZ, ref maxX, ref maxY, ref maxZ);
                }
                pointCount += 3; // approximate: faces share vertices
            }
            else
            {
                // Generic entity: use geometric extents for bounds
                try
                {
                    var ext = ent.GeometricExtents;
                    UpdateBounds(ext.MinPoint, ref minX, ref minY, ref minZ, ref maxX, ref maxY, ref maxZ);
                    UpdateBounds(ext.MaxPoint, ref minX, ref minY, ref minZ, ref maxX, ref maxY, ref maxZ);
                    pointCount++;
                }
                catch (Teigha.Runtime.Exception)
                {
                    // Entity has no geometric extents (e.g., empty block reference)
                }
            }
        }

        if (!found)
        {
            throw new InvalidOperationException(
                $"Surface '{surfaceName}' not found in ModelSpace (no entities on layer '{surfaceName}')");
        }

        telemetry["point_count"] = pointCount;
        telemetry["triangle_count"] = triangleCount;
        telemetry["bounds"] = new[]
        {
            new[] { minX, minY, minZ },
            new[] { maxX, maxY, maxZ },
        };
    }

    private static void UpdateBounds(
        Point3d pt,
        ref double minX, ref double minY, ref double minZ,
        ref double maxX, ref double maxY, ref double maxZ)
    {
        if (pt.X < minX) minX = pt.X;
        if (pt.Y < minY) minY = pt.Y;
        if (pt.Z < minZ) minZ = pt.Z;
        if (pt.X > maxX) maxX = pt.X;
        if (pt.Y > maxY) maxY = pt.Y;
        if (pt.Z > maxZ) maxZ = pt.Z;
    }
#endif
}
