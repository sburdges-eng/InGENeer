#!/usr/bin/env bash
# Create roadmap worktrees for parallel agent lanes (see docs/roadmap/WORKTREE_INDEX.md).
set -euo pipefail

ROOT="$(git rev-parse --show-toplevel 2>/dev/null)" || {
  echo "Run from inside the InGENeer git repository." >&2
  exit 1
}

BASE="${INGENEER_WORKTREE_BASE:-$HOME/Dev-wt}"
mkdir -p "$BASE"

cd "$ROOT"

declare -a PAIRS=(
  "ingenieer/roadmap/goal-1/cloud-agent:ingenieer-g1-cloud"
  "ingenieer/roadmap/goal-1/claude-code:ingenieer-g1-claude"
  "ingenieer/roadmap/goal-1/codex:ingenieer-g1-codex"
  "ingenieer/roadmap/goal-1/gemini:ingenieer-g1-gemini"
  "ingenieer/roadmap/goal-2/cloud-agent:ingenieer-g2-cloud"
  "ingenieer/roadmap/goal-2/claude-code:ingenieer-g2-claude"
  "ingenieer/roadmap/goal-2/codex:ingenieer-g2-codex"
  "ingenieer/roadmap/goal-2/gemini:ingenieer-g2-gemini"
)

for pair in "${PAIRS[@]}"; do
  branch="${pair%%:*}"
  dir="${pair##*:}"
  target="$BASE/$dir"
  if [[ -d "$target" ]]; then
    echo "skip exists: $target"
    continue
  fi
  if git show-ref --verify --quiet "refs/heads/$branch"; then
    git worktree add "$target" "$branch"
    echo "added: $target <- $branch"
  else
    echo "skip missing branch: $branch (create it first)" >&2
  fi
done

echo "Done. Worktrees under: $BASE"
