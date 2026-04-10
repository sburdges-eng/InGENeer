"""Batch pipeline: run a sequence of intents with fingerprint threading."""

from __future__ import annotations

from pydantic import BaseModel, Field, field_validator

from ingenieer.models import (
    CadIntentEnvelope,
    PipelineResult,
    ProjectConfig,
)


class ProjectContract(BaseModel):
    """Project-level envelope: metadata + ordered list of intents to execute."""

    project: ProjectConfig = Field(default_factory=ProjectConfig)
    intents: list[CadIntentEnvelope] = Field(min_length=1)
    execution_mode_override: str | None = Field(
        default=None,
        description="If set, override executionMode on all intents (dry_run, preview).",
    )
    human_confirmation_token: str | None = Field(
        default=None,
        description="If set, apply to all high-risk intents that lack one.",
    )

    @field_validator("intents")
    @classmethod
    def _validate_unique_intent_ids(cls, v: list[CadIntentEnvelope]) -> list[CadIntentEnvelope]:
        ids = [i.intentId for i in v]
        dupes = [x for x in ids if ids.count(x) > 1]
        if dupes:
            raise ValueError(f"duplicate intentIds in batch: {set(dupes)}")
        return v


class StepResult(BaseModel):
    """Result of one intent within a batch."""

    step_index: int
    intent_id: str
    command: str
    pipeline_result: PipelineResult
    fingerprint_before: str | None = None
    fingerprint_after: str | None = None


class BatchResult(BaseModel):
    """Aggregate result of a batch pipeline run."""

    project_id: str
    success: bool = True
    completed_steps: int = 0
    total_steps: int = 0
    step_results: list[StepResult] = Field(default_factory=list)
    last_good_fingerprint: str | None = None
    errors: list[str] = Field(default_factory=list)
