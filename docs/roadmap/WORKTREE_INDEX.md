# InGENeer roadmap branches and worktrees

Git root is **this repository** (`git rev-parse --show-toplevel`). If the clone lives under **`~/Dev/InGENeer`**, that parent folder is a separate layout; see [PARENT_DEV_MONOREPO.md](../PARENT_DEV_MONOREPO.md). Worktrees below are extra checkouts of **this** repo.

## Scoped IDE workspace

Open **[InGENeer.code-workspace](../../InGENeer.code-workspace)** in Cursor/VS Code to limit search and watchers to orchestrator, `icad-addin`, schemas, docs, AutonomAtIon, and scripts (same idea as `~/Dev/workspaces/*.code-workspace` for sibling projects).

## Bootstrap script

From the repo root, after creating the roadmap branches locally:

```bash
./scripts/bootstrap_worktrees.sh
```

Override output base directory: `INGENEER_WORKTREE_BASE=/path/to/wt ./scripts/bootstrap_worktrees.sh`.

## Branch layout

```text
main
├── ingenieer/roadmap/goal-1/cloud-agent
├── ingenieer/roadmap/goal-1/claude-code
├── ingenieer/roadmap/goal-1/codex
├── ingenieer/roadmap/goal-1/gemini
├── ingenieer/roadmap/goal-2/cloud-agent
├── ingenieer/roadmap/goal-2/claude-code
├── ingenieer/roadmap/goal-2/codex
└── ingenieer/roadmap/goal-2/gemini
```

## Goal focus (from layered playbook)

| Goal | Primary layers | Practice focus |
|------|----------------|----------------|
| **goal-1** | L5 transport, L3 wire | Bridge / RPC / queue; timeouts; matches envelope + result contracts |
| **goal-2** | L6 CAD execution, L3 | Host add-in; UI thread; native transactions; command dispatch |

## Agent lanes (naming)

Model × language rules: [MODEL_LANGUAGE_ROUTING.md](../MODEL_LANGUAGE_ROUTING.md).

| Lane | Typical surface |
|------|-----------------|
| `cloud-agent` | Cloud / IDE agent automation |
| `claude-code` | Claude Code |
| `codex` | Codex |
| `gemini` | Gemini |

## Local worktrees (created alongside this doc)

Default base directory: **`$HOME/Dev-wt`** (override with `INGENEER_WORKTREE_BASE` when running `bootstrap_worktrees.sh`):

| Branch | Worktree directory |
|--------|-------------------|
| `ingenieer/roadmap/goal-1/cloud-agent` | `ingenieer-g1-cloud` |
| `ingenieer/roadmap/goal-1/claude-code` | `ingenieer-g1-claude` |
| `ingenieer/roadmap/goal-1/codex` | `ingenieer-g1-codex` |
| `ingenieer/roadmap/goal-1/gemini` | `ingenieer-g1-gemini` |
| `ingenieer/roadmap/goal-2/cloud-agent` | `ingenieer-g2-cloud` |
| `ingenieer/roadmap/goal-2/claude-code` | `ingenieer-g2-claude` |
| `ingenieer/roadmap/goal-2/codex` | `ingenieer-g2-codex` |
| `ingenieer/roadmap/goal-2/gemini` | `ingenieer-g2-gemini` |

## Recreate worktrees manually (if removed)

Equivalent to `bootstrap_worktrees.sh`; adjust `Dev-wt` to your layout:

```bash
mkdir -p "$HOME/Dev-wt"
git worktree add "$HOME/Dev-wt/ingenieer-g1-cloud"    ingenieer/roadmap/goal-1/cloud-agent
git worktree add "$HOME/Dev-wt/ingenieer-g1-claude"   ingenieer/roadmap/goal-1/claude-code
git worktree add "$HOME/Dev-wt/ingenieer-g1-codex"    ingenieer/roadmap/goal-1/codex
git worktree add "$HOME/Dev-wt/ingenieer-g1-gemini"  ingenieer/roadmap/goal-1/gemini
git worktree add "$HOME/Dev-wt/ingenieer-g2-cloud"    ingenieer/roadmap/goal-2/cloud-agent
git worktree add "$HOME/Dev-wt/ingenieer-g2-claude"   ingenieer/roadmap/goal-2/claude-code
git worktree add "$HOME/Dev-wt/ingenieer-g2-codex"    ingenieer/roadmap/goal-2/codex
git worktree add "$HOME/Dev-wt/ingenieer-g2-gemini"  ingenieer/roadmap/goal-2/gemini
```
