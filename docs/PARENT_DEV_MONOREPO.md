# InGENeer beside the `~/Dev` monorepo

Some machines keep **InGENeer** as `~/Dev/InGENeer` alongside other projects (TOTaLi, auracad, KmiDi, workspace-scaffold, etc.). That parent tree is a **separate Git concern** from this repository: InGENeer is its own repo with its own history.

## IDE hygiene (recommended upstream pattern)

The parent runbook **`~/Dev/DEV_OPS_RUNBOOK.md`** (if present) states:

- Do **not** open `~/` or the entire `~/Dev/` folder as a single Cursor/VS Code workspace (heavy indexing and watchers).
- Prefer **per-project** workspace files or open **this repo root** / **[InGENeer.code-workspace](../InGENeer.code-workspace)** only.

This repo’s **[InGENeer.code-workspace](../InGENeer.code-workspace)** scopes folders to orchestrator, `icad-addin`, schemas, docs, AutonomAtIon, and scripts—matching the **air-gap** split between orchestrator and host (see architecture SOP 2).

## Wrapper workspace under `~/Dev/workspaces/`

If your machine uses **`~/Dev/workspaces/`**, a thin launcher **`ingenieer.code-workspace`** may already point at `../InGENeer` (single folder, same relative layout as `auracad.code-workspace`). Prefer the in-repo **[InGENeer.code-workspace](../InGENeer.code-workspace)** when you want multi-root folders (orchestrator, `icad-addin`, schemas, docs, etc.) without relying on `~/Dev/`.

To add shared docs from another repo, duplicate the pattern used by `auracad.code-workspace` (extra `folders` entries) in a **local-only** copy so clones stay portable.

## Sibling tooling

Other projects under `~/Dev` may carry experimental CI, Cursor rules, or worktrees. **Do not copy them blindly** into InGENeer—align with [WORKSPACE_STANDARDS.md](WORKSPACE_STANDARDS.md) and AutonomAtIon rules instead.
