#!/usr/bin/env bash
# Copy intent schema and sample envelopes for CAD-plugin / air-gapped workspaces (SOP 2).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DEST="${1:-"$ROOT/handoff"}"
mkdir -p "$DEST"
cp "$ROOT/schemas/cad_intent_envelope.schema.json" "$DEST/"
cat >"$DEST/sample-intent-execute.json" <<'EOF'
{
  "schemaVersion": "1.1.0",
  "intentId": "example-execute-1",
  "command": "NoOp",
  "parameters": {},
  "executionMode": "execute"
}
EOF
cat >"$DEST/sample-intent-dry-run.json" <<'EOF'
{
  "schemaVersion": "1.1.0",
  "intentId": "example-dry-run-1",
  "command": "NoOp",
  "parameters": {},
  "executionMode": "dry_run"
}
EOF
cat >"$DEST/sample-intent-high-risk.json" <<'EOF'
{
  "schemaVersion": "1.1.0",
  "intentId": "example-high-risk-1",
  "command": "HighRiskStub",
  "parameters": {},
  "executionMode": "execute",
  "humanConfirmationToken": "operator-approved-token",
  "humanConfirmationId": "ticket-12345"
}
EOF
echo "Wrote schema and samples to $DEST"
