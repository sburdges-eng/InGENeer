# InGENeer roadmap branches and worktrees

Git root for this tree is **`/Users/seanburdges/Dev/InGENeer`** (this repository). The parent **`/Users/seanburdges/Dev`** monorepo does not track `InGENeer/`; worktrees are extra checkouts of **this** repo.

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

| Lane | Typical surface |
|------|-----------------|
| `cloud-agent` | Cloud / IDE agent automation |
| `claude-code` | Claude Code |
| `codex` | Codex |
| `gemini` | Gemini |

## Local worktrees (created alongside this doc)

Paths under `/Users/seanburdges/Dev-wt/`:

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

## Recreate worktrees (if removed)

From `/Users/seanburdges/Dev/InGENeer`:

```bash
mkdir -p /Users/seanburdges/Dev-wt
git worktree add /Users/seanburdges/Dev-wt/ingenieer-g1-cloud    ingenieer/roadmap/goal-1/cloud-agent
git worktree add /Users/seanburdges/Dev-wt/ingenieer-g1-claude   ingenieer/roadmap/goal-1/claude-code
git worktree add /Users/seanburdges/Dev-wt/ingenieer-g1-codex    ingenieer/roadmap/goal-1/codex
git worktree add /Users/seanburdges/Dev-wt/ingenieer-g1-gemini  ingenieer/roadmap/goal-1/gemini
git worktree add /Users/seanburdges/Dev-wt/ingenieer-g2-cloud    ingenieer/roadmap/goal-2/cloud-agent
git worktree add /Users/seanburdges/Dev-wt/ingenieer-g2-claude   ingenieer/roadmap/goal-2/claude-code
git worktree add /Users/seanburdges/Dev-wt/ingenieer-g2-codex    ingenieer/roadmap/goal-2/codex
git worktree add /Users/seanburdges/Dev-wt/ingenieer-g2-gemini  ingenieer/roadmap/goal-2/gemini
```
