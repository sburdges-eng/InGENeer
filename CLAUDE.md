# Claude Code — InGENeer

You are in the **InGENeer** repo: Python orchestrator (`orchestrator/`), C# bridge spike (`icad-addin/`), JSON schemas (`schemas/`), and AutonomAtIon governance docs.

## Before coding

1. Read [AGENTS.md](AGENTS.md) for the full reading order and Cursor rules.
2. Respect **domain isolation**: no B-rep geometry or proprietary CAD APIs in Python; no LLM in C# execution paths; use `// TODO` + docs for uncertain host APIs.

## Quick checks (from repo root)

```bash
cd orchestrator && pip install -e ".[dev]" && ruff check src tests && python -m pytest -q
dotnet build icad-addin/InGENeer.IcadAddin.slnx -c Release
```

## Sage guard (Claude Code)

The **Sage** hook blocks bash that contains **`xargs`** (heuristic: "dispatching download or shell execution"). This is often a false positive on benign pipelines.

**Avoid:** `… | xargs …`, `git ls-files | xargs ls`, `grep -c … | xargs echo`.

**Use instead:**

```bash
# label a count (not: grep -c … | xargs echo)
n=$(grep -ciE 'error|warning:' build.log); echo "warn/err: $n"

# largest tracked files (not: git ls-files | xargs ls -l)
while IFS= read -r f; do [ -n "$f" ] && ls -l "$f"; done < <(git ls-files) | sort -k5 -n | tail

# verification gates — call repo scripts directly
./tools/scripts/verify_pointcloud_octree_tasks.sh
cmake --build --preset dev --target test_octree_metadata && ctest --preset dev -R pointcloud.octree_metadata
```

If Sage blocks a command you need, rewrite without `xargs`, run the script in **tmux/Cursor** manually, or approve the override in Claude Code if offered.

**Phase 7 octree worktree:** `~/Dev-wt/ingenieer-phase7-octree` on branch `feat/phase7-pointcloud-octree`.

## Parent `~/Dev` layout

If this clone sits next to other projects under `~/Dev`, see [docs/PARENT_DEV_MONOREPO.md](docs/PARENT_DEV_MONOREPO.md). Do not assume sibling repos are present or stable.
