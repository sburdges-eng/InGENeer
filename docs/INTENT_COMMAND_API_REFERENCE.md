# InGENeer Intent Command API Reference

> Auto-generated from `schemas/params/*.schema.json`. Do not edit manually.

**19 commands** across the catalog.

---

## AnalyzeStormDrainage

**Risk:** `low` | **Required params:** `network_name`, `design_storm_years`, `runoff_coefficient`

| Parameter | Type | Required | Constraints |
|-----------|------|----------|-------------|
| `network_name` | `string` | yes | minLength: 1 |
| `design_storm_years` | `integer` | yes | min: 1 |
| `runoff_coefficient` | `number` | yes | min: 0; max: 1 |

---

## BalanceGrading

**Risk:** `high` | **Required params:** `existing_surface`, `proposed_surface`, `tolerance`

| Parameter | Type | Required | Constraints |
|-----------|------|----------|-------------|
| `existing_surface` | `string` | yes | minLength: 1 |
| `proposed_surface` | `string` | yes | minLength: 1 |
| `tolerance` | `number` | yes | min: 0 |
| `shrink_swell_factor` | `number` | no | default: 1.0 |

---

## CreateAlignment

**Risk:** `high` | **Required params:** `name`, `points`, `start_station`, `layer`

| Parameter | Type | Required | Constraints |
|-----------|------|----------|-------------|
| `name` | `string` | yes | minLength: 1 |
| `points` | `array` | yes | minItems: 2; [x, y, z] tuples |
| `start_station` | `number` | yes | — |
| `layer` | `string` | yes | minLength: 1 |
| `type` | `string` | no | enum: centerline, offset, curb |

---

## CreateCorridorModel

**Risk:** `high` | **Required params:** `name`, `alignment_name`, `profile_name`, `layer`

| Parameter | Type | Required | Constraints |
|-----------|------|----------|-------------|
| `name` | `string` | yes | minLength: 1 |
| `alignment_name` | `string` | yes | minLength: 1 |
| `profile_name` | `string` | yes | minLength: 1 |
| `layer` | `string` | yes | minLength: 1 |

---

## CreateCrossSection

**Risk:** `high` | **Required params:** `alignment_name`, `profile_name`, `template_name`, `stations`, `layer`

| Parameter | Type | Required | Constraints |
|-----------|------|----------|-------------|
| `alignment_name` | `string` | yes | minLength: 1 |
| `profile_name` | `string` | yes | minLength: 1 |
| `template_name` | `string` | yes | minLength: 1 |
| `stations` | `array` | yes | minItems: 1 |
| `layer` | `string` | yes | minLength: 1 |

---

## CreatePavingArea

**Risk:** `high` | **Required params:** `boundary_points`, `material_type`, `subbase_depth`, `permeability_coefficient`, `layer`

| Parameter | Type | Required | Constraints |
|-----------|------|----------|-------------|
| `boundary_points` | `array` | yes | minItems: 3; [x, y, z] tuples |
| `material_type` | `string` | yes | — |
| `subbase_depth` | `number` | yes | min: 0 |
| `permeability_coefficient` | `number` | yes | min: 0; max: 1 |
| `layer` | `string` | yes | minLength: 1 |

---

## CreatePointBlocks

**Risk:** `high` | **Required params:** `layer`, `blockName`, `points`

| Parameter | Type | Required | Constraints |
|-----------|------|----------|-------------|
| `layer` | `string` | yes | minLength: 1 |
| `blockName` | `string` | yes | minLength: 1 |
| `points` | `array` | yes | minItems: 1; objects with: location |

---

## CreateProfile

**Risk:** `high` | **Required params:** `alignment_name`, `profile_name`, `pvi_data`, `layer`

| Parameter | Type | Required | Constraints |
|-----------|------|----------|-------------|
| `alignment_name` | `string` | yes | minLength: 1 |
| `profile_name` | `string` | yes | minLength: 1 |
| `layer` | `string` | yes | minLength: 1 |
| `pvi_data` | `array` | yes | minItems: 2; objects with: station, elevation |

---

## CreateRetentionPond

**Risk:** `high` | **Required params:** `outline_polyline_id`, `base_elevation`, `side_slope`, `target_surface`, `layer`

| Parameter | Type | Required | Constraints |
|-----------|------|----------|-------------|
| `outline_polyline_id` | `string` | yes | minLength: 1 |
| `base_elevation` | `number` | yes | — |
| `side_slope` | `number` | yes | min: 0 |
| `berm_width` | `number` | no | min: 0 |
| `target_surface` | `string` | yes | minLength: 1 |
| `layer` | `string` | yes | minLength: 1 |

---

## CreateSanitarySewerNetwork

**Risk:** `high` | **Required params:** `network_name`, `alignment_name`, `structures`, `pipe_material`, `pipe_diameter`, `layer`

| Parameter | Type | Required | Constraints |
|-----------|------|----------|-------------|
| `network_name` | `string` | yes | minLength: 1 |
| `alignment_name` | `string` | yes | minLength: 1 |
| `structures` | `array` | yes | minItems: 2; objects with: station, type, rim_elevation, invert_elevation |
| `pipe_material` | `string` | yes | — |
| `pipe_diameter` | `number` | yes | min: 0 |
| `layer` | `string` | yes | minLength: 1 |

---

## DesignIrrigationZone

**Risk:** `high` | **Required params:** `zone_id`, `heads`, `pipe_material`, `target_psi`, `layer`

| Parameter | Type | Required | Constraints |
|-----------|------|----------|-------------|
| `zone_id` | `string` | yes | minLength: 1 |
| `heads` | `array` | yes | minItems: 1; objects with: location, type, radius |
| `pipe_material` | `string` | yes | — |
| `target_psi` | `number` | yes | min: 0 |
| `layer` | `string` | yes | minLength: 1 |

---

## DrawPolylineFromCoordinates

**Risk:** `high` | **Required params:** `points`, `layer`, `closed`

| Parameter | Type | Required | Constraints |
|-----------|------|----------|-------------|
| `points` | `array` | yes | minItems: 2; [x, y, z] tuples |
| `layer` | `string` | yes | minLength: 1 |
| `closed` | `boolean` | yes | — |
| `color` | `string` | no | — |

---

## GetModelFingerprint

**Risk:** `low` | **Required params:** _none_

---

## HighRiskStub

**Risk:** `high` | **Required params:** _none_

---

## ImportLandXmlSurface

**Risk:** `high` | **Required params:** `landxml_path_key`, `surface_name`, `layer`

| Parameter | Type | Required | Constraints |
|-----------|------|----------|-------------|
| `landxml_path_key` | `string` | yes | minLength: 1 |
| `surface_name` | `string` | yes | minLength: 1 |
| `layer` | `string` | yes | minLength: 1 |

---

## NoOp

**Risk:** `low` | **Required params:** _none_

---

## PingHost

**Risk:** `low` | **Required params:** _none_

---

## PlacePlantingLayout

**Risk:** `high` | **Required params:** `species_id`, `points`, `mature_spread`, `layer`

| Parameter | Type | Required | Constraints |
|-----------|------|----------|-------------|
| `species_id` | `string` | yes | minLength: 1 |
| `points` | `array` | yes | minItems: 1; objects with: location |
| `mature_spread` | `number` | yes | min: 0 |
| `layer` | `string` | yes | minLength: 1 |
| `container_size` | `string` | no | — |

---

## VerifySurface

**Risk:** `low` | **Required params:** `surface_name`

| Parameter | Type | Required | Constraints |
|-----------|------|----------|-------------|
| `surface_name` | `string` | yes | minLength: 1 |

---

