# Phase 7 agent lane (2026-06-16)

**Single writer:** `~/Dev-wt/ingenieer-phase7-octree` on `feat/phase7-pointcloud-octree`.

| Lane | Owner | Role |
|------|-------|------|
| Cursor Agent | Tasks 2–7 implementation + commits | Primary coder |
| tmux pane | `./tools/scripts/phase7_verify_watch.sh` | Poll + ctest on each new HEAD |
| Claude Code | **HOLD** until Cursor reaches Task 8 | Avoid worktree clobber |

Claude paste when resuming at Task 8:

```text
Cursor owns T1–7 on feat/phase7-pointcloud-octree. Pull latest, start Task 8 (reader).
No xargs in bash. Use ./tools/scripts/verify_pointcloud_octree_tasks.sh for gates.
```
