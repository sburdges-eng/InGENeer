# Scoped Cursor Workspaces

This directory contains scoped Cursor / VS Code workspace files for InGENeer. Use these instead of opening `~/Dev` or broad parent folders.

Primary index: [`../WORKSPACE_SCOPE_MAP.md`](../WORKSPACE_SCOPE_MAP.md).

Rules:

1. Open the narrowest workspace that matches the task.
2. Do not mix orchestrator and host/CAD execution work in one agent prompt unless the task is explicitly a contract handoff review.
3. Keep implementation hold rules from `../handoff.md` and `../architecture/CONSTRAINTS.md` in force.
4. Workspace files live under `docs/workspaces/` so they do not add new top-level directories ahead of the approved monorepo migration.
