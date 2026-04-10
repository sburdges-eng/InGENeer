"""CAD bridge response wrapper — mirrors TOTaLi REPLResult contract shape."""

from __future__ import annotations

from typing import Annotated, Any, Literal, Union

from pydantic import BaseModel, ConfigDict, Discriminator, Field, RootModel, Tag

from ingenieer.contracts import SCHEMA_VERSION, build_contract_payload

# --- Strongly Typed Telemetry Models (L3–L5 Hardening) ---
# Field names match docs/INTENT_COMMAND_CATALOG.md exactly.
# Command-specific fields are Optional so mocks can return base-only telemetry.


class BaseTelemetry(BaseModel):
    model_config = ConfigDict(extra="allow")
    intentId: str
    command: str
    executionMode: str = "execute"
    modelFingerprintAfter: str | None = None
    plannedSummary: str = ""


class NoOpTelemetry(BaseTelemetry):
    command: Literal["NoOp"] = "NoOp"


class PingHostTelemetry(BaseTelemetry):
    command: Literal["PingHost"] = "PingHost"
    hostId: str = ""
    build: str = ""


class GetModelFingerprintTelemetry(BaseTelemetry):
    command: Literal["GetModelFingerprint"] = "GetModelFingerprint"
    modelFingerprint: str = ""


class HighRiskStubTelemetry(BaseTelemetry):
    command: Literal["HighRiskStub"] = "HighRiskStub"


class DrawPolylineTelemetry(BaseTelemetry):
    command: Literal["DrawPolylineFromCoordinates"] = "DrawPolylineFromCoordinates"


class CreatePointBlocksTelemetry(BaseTelemetry):
    command: Literal["CreatePointBlocks"] = "CreatePointBlocks"


class ImportLandXmlSurfaceTelemetry(BaseTelemetry):
    command: Literal["ImportLandXmlSurface"] = "ImportLandXmlSurface"


class VerifySurfaceTelemetry(BaseTelemetry):
    command: Literal["VerifySurface"] = "VerifySurface"
    point_count: int | None = None
    triangle_count: int | None = None
    bounds: list[list[float]] | None = None


class CreateAlignmentTelemetry(BaseTelemetry):
    command: Literal["CreateAlignment"] = "CreateAlignment"
    length: float | None = None
    station_range: list[float] | None = None


class CreateProfileTelemetry(BaseTelemetry):
    command: Literal["CreateProfile"] = "CreateProfile"
    pvi_count: int | None = None
    elevation_range: list[float] | None = None


class CreateCrossSectionTelemetry(BaseTelemetry):
    command: Literal["CreateCrossSection"] = "CreateCrossSection"
    station_count: int | None = None


class CreateCorridorModelTelemetry(BaseTelemetry):
    command: Literal["CreateCorridorModel"] = "CreateCorridorModel"
    corridor_length: float | None = None


class BalanceGradingTelemetry(BaseTelemetry):
    command: Literal["BalanceGrading"] = "BalanceGrading"
    cut_volume: float | None = None
    fill_volume: float | None = None
    net_volume: float | None = None
    balanced: bool | None = None


class CreateRetentionPondTelemetry(BaseTelemetry):
    command: Literal["CreateRetentionPond"] = "CreateRetentionPond"
    pond_volume: float | None = None
    surface_area: float | None = None


class CreateSanitarySewerNetworkTelemetry(BaseTelemetry):
    command: Literal["CreateSanitarySewerNetwork"] = "CreateSanitarySewerNetwork"
    structure_count: int | None = None
    total_pipe_length: float | None = None


class AnalyzeStormDrainageTelemetry(BaseTelemetry):
    command: Literal["AnalyzeStormDrainage"] = "AnalyzeStormDrainage"
    peak_discharge: float | None = None
    max_velocity: float | None = None
    capacity_exceeded: bool | None = None


class PlacePlantingTelemetry(BaseTelemetry):
    command: Literal["PlacePlantingLayout"] = "PlacePlantingLayout"
    plant_count: int | None = None
    canopy_coverage_area: float | None = None


class CreatePavingTelemetry(BaseTelemetry):
    command: Literal["CreatePavingArea"] = "CreatePavingArea"
    paving_area: float | None = None
    perimeter_length: float | None = None


class DesignIrrigationTelemetry(BaseTelemetry):
    command: Literal["DesignIrrigationZone"] = "DesignIrrigationZone"
    head_count: int | None = None
    total_flow_gpm: float | None = None
    pipe_length: float | None = None


def _telemetry_discriminator(v: Any) -> str:
    if isinstance(v, dict):
        return v.get("command", "")
    return getattr(v, "command", "")


# Discriminated union for command-specific telemetry
CommandTelemetry = RootModel[
    Annotated[
        Union[
            Annotated[NoOpTelemetry, Tag("NoOp")],
            Annotated[PingHostTelemetry, Tag("PingHost")],
            Annotated[GetModelFingerprintTelemetry, Tag("GetModelFingerprint")],
            Annotated[HighRiskStubTelemetry, Tag("HighRiskStub")],
            Annotated[DrawPolylineTelemetry, Tag("DrawPolylineFromCoordinates")],
            Annotated[CreatePointBlocksTelemetry, Tag("CreatePointBlocks")],
            Annotated[ImportLandXmlSurfaceTelemetry, Tag("ImportLandXmlSurface")],
            Annotated[VerifySurfaceTelemetry, Tag("VerifySurface")],
            Annotated[CreateAlignmentTelemetry, Tag("CreateAlignment")],
            Annotated[CreateProfileTelemetry, Tag("CreateProfile")],
            Annotated[CreateCrossSectionTelemetry, Tag("CreateCrossSection")],
            Annotated[CreateCorridorModelTelemetry, Tag("CreateCorridorModel")],
            Annotated[BalanceGradingTelemetry, Tag("BalanceGrading")],
            Annotated[CreateRetentionPondTelemetry, Tag("CreateRetentionPond")],
            Annotated[CreateSanitarySewerNetworkTelemetry, Tag("CreateSanitarySewerNetwork")],
            Annotated[AnalyzeStormDrainageTelemetry, Tag("AnalyzeStormDrainage")],
            Annotated[PlacePlantingTelemetry, Tag("PlacePlantingLayout")],
            Annotated[CreatePavingTelemetry, Tag("CreatePavingArea")],
            Annotated[DesignIrrigationTelemetry, Tag("DesignIrrigationZone")],
        ],
        Discriminator(_telemetry_discriminator),
    ]
]


class BridgeVerifyResult(BaseModel):
    """Evidence from an independent post-dispatch check (e.g. live fingerprint vs telemetry)."""

    model_config = ConfigDict(extra="forbid")

    success: bool
    transient_failure: bool = False
    message: str = ""
    observed_fingerprint: str | None = None
    expected_fingerprint: str | None = None
    evidence: dict[str, Any] = Field(default_factory=dict)


class BridgeExecutionResult(BaseModel):
    """Result from the in-process CAD add-in / bridge after executing an intent."""

    model_config = ConfigDict(extra="forbid")

    schemaVersion: str = Field(default=SCHEMA_VERSION)
    success: bool
    stdout: str = ""
    error_traceback: str | None = None
    telemetry: dict[str, Any] = Field(default_factory=dict)
    invariants: list[str] = Field(
        default_factory=lambda: [
            "schema_version_required",
            "deterministic_key_order",
            "intent_schema_validated",
            "atomic_rollback_on_failure",
            "strongly_typed_telemetry",
        ]
    )

    def validate_telemetry(self) -> CommandTelemetry:
        """Coerce and validate loose telemetry into a command-specific model."""
        return CommandTelemetry.model_validate(self.telemetry)

    def as_contract(self) -> dict[str, Any]:
        return build_contract_payload(
            artifact_type="icad_bridge_execution_result",
            invariants=self.invariants,
            metadata={"success": self.success},
            data={
                "success": self.success,
                "stdout": self.stdout,
                "errorTraceback": self.error_traceback or "",
                "telemetry": self.telemetry,
            },
        )
