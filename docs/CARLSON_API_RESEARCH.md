# Carlson .NET API Research Report (SDK Audit)

**Date:** 2026-04-10
**Status:** Research Complete (Draft)
**Target:** Carlson Civil Suite / IntelliCAD .NET API

## Summary

Research into the Carlson .NET SDK confirms a modular structure aligned with Carlson's command-line tools. Unlike Civil 3D, which uses complex AEC custom objects, Carlson's API is "entity-light," focusing on manipulation of external data files (`.CL`, `.PRO`, `.TIN`, `.SEW`, `.RMN`) and standard CAD primitives (Polylines) with XData.

## Core Namespaces & DLLs

| DLL | Namespace | Responsibility |
|-----|-----------|----------------|
| `Carlson.Civil.dll` | `Carlson.Civil` | Centerlines, Profiles, Surfaces, Volumes |
| `Carlson.Crd.dll` | `Carlson.Cogo` / `Carlson.Crd` | Points, COGO, .CRD file management |
| `Carlson.Hydrology.dll` | `Carlson.Hydrology.SewerNetwork` | Pipe networks, Rainfall, Hydrographs |
| `Carlson.RoadNet.dll` | `Carlson.Civil.RoadNetwork` | Road Network (.RMN) management |

---

## Command-Specific API Mappings

Based on the research, the following mappings are recommended for the L6 implementation in `TeighaCadHost.cs`:

### 1. CreateAlignment
- **Command:** `CreateAlignment`
- **L6 API:** `Carlson.Civil.Centerline.CreateFromPolyline(polyline, filePath, startStation)`
- **Host Rep:** A `.CL` (Centerline) file on disk + a standard Polyline with `Centerline` XData.

### 2. CreateProfile
- **Command:** `CreateProfile`
- **L6 API:** `Carlson.Civil.Profile.CreateFromSurface(centerlinePath, tinPath, outProPath)` or `Profile.CreateFromPolyline(pline, clPath, outProPath)`
- **Host Rep:** A `.PRO` (Profile) file on disk.

### 3. CreateCrossSection
- **Command:** `CreateCrossSection`
- **L6 API:** `Carlson.Civil.Templates.Template.ApplyToCenterline(...)`
- **Notes:** Need to verify the exact signature for applying a template (.TPL) to an alignment to generate cross-sections (.SCT).

### 4. CreateCorridorModel
- **Command:** `CreateCorridorModel`
- **L6 API:** `Carlson.Civil.RoadNetwork.RoadNetwork` class.
- **Notes:** Use `RoadNetworkProject.Load(rmnPath)` followed by `RoadDesign.Process()` to generate the 3D model.

### 5. BalanceGrading
- **Command:** `BalanceGrading`
- **L6 API:** `Carlson.Civil.Volumes.VolumeCalculator` or `Carlson.Civil.Surface.SurfaceModel.BalanceVolumes()`
- **Notes:** Iterative solver typically found in the Surface or SiteNET libraries.

### 6. CreateRetentionPond
- **Command:** `CreateRetentionPond`
- **L6 API:** `Carlson.Civil.GradingObject` or `Carlson.Civil.Surface.TemplateGrade()`
- **Notes:** Usually involves sloping from a polyline to a surface.

### 7. CreateSanitarySewerNetwork
- **Command:** `CreateSanitarySewerNetwork`
- **L6 API:** `Carlson.Hydrology.SewerNetwork.SewerNetwork` class.
- **Notes:** Manages nodes and pipes in a `.SEW` file. Use `SewerNetwork.AddNode()` and `SewerNetwork.AddPipe()`.

### 8. AnalyzeStormDrainage
- **Command:** `AnalyzeStormDrainage`
- **L6 API:** `Carlson.Hydrology.SewerNetwork.Analyze()`
- **Notes:** Performs hydraulic analysis on a `.SEW` project using rainfall events.

### 9. PlacePlantingLayout
- **Command:** `PlacePlantingLayout`
- **L6 API:** Standard Teigha `BlockReference` insertion.
- **Notes:** Research suggests no proprietary "Planting" object exists in core Carlson; it uses blocks with specific attributes or XData for LandArch reporting.

---

## Implementation Constraints (Rules 2 & 4)

1.  **Thread Safety:** All calls to `Carlson.Civil` or `Carlson.Cogo` that result in document mutation MUST be marshaled to the UI thread (Standard IntelliCAD/AutoCAD practice).
2.  **No Hallucinations:** Implementation should strictly follow the `Carlson.[Module].[Class].Method()` structure identified.
3.  **File-Centric:** Most Carlson commands produce external files. The L6 implementation must handle these file paths (using the injected `IPathResolver`).

## Next Steps for L6

1.  Obtain the `.chm` help file from a Carlson installation to verify exact method signatures (e.g., `CreateFromPolyline` vs `Create`).
2.  Update `TeighaCadHost.cs` stubs with these namespaces once DLLs are available in the build environment.
