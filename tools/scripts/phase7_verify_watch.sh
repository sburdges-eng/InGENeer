#!/usr/bin/env bash
# Phase 7 verify watcher — run in tmux pane; polls git HEAD and re-runs gates.
set -u

WT="${INGENEER_PHASE7_WT:-$HOME/Dev-wt/ingenieer-phase7-octree}"
LOG="${WT}/.phase7-verify.log"
INTERVAL="${PHASE7_VERIFY_INTERVAL:-30}"

cd "$WT" || exit 1

last=""
echo "=== phase7 verify watcher $(date) ===" | tee -a "$LOG"
echo "worktree: $WT  interval: ${INTERVAL}s" | tee -a "$LOG"

while true; do
  head="$(git rev-parse --short HEAD 2>/dev/null || echo none)"
  if [ "$head" != "$last" ]; then
    echo "" | tee -a "$LOG"
    echo "--- $(date) HEAD=$head ---" | tee -a "$LOG"
    {
      echo "branch: $(git branch --show-current)"
      git log -1 --oneline
      ./tools/scripts/verify_pointcloud_octree_tasks.sh 2>&1 || true
      ctest --preset dev -R 'pointcloud\.' --output-on-failure 2>&1 || true
    } | tee -a "$LOG"
    last="$head"
  fi
  sleep "$INTERVAL"
done
