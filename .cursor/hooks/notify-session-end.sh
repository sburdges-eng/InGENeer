#!/bin/bash
set -euo pipefail

payload=$(cat)
reason=$(echo "$payload" | jq -r '.reason // "unknown"')
session_id=$(echo "$payload" | jq -r '.session_id // "unknown"')
duration_ms=$(echo "$payload" | jq -r '.duration_ms // 0')
final_status=$(echo "$payload" | jq -r '.final_status // .status // "unknown"')
workspace=$(echo "$payload" | jq -r '.workspace_roots[0] // env.CURSOR_PROJECT_DIR // "InGENeer"')

short_id="${session_id:0:8}"

osascript -e "display notification \"Session ${short_id} ended (${reason}, ${final_status}, ${duration_ms}ms)\" with title \"Cursor: InGENeer session complete\" subtitle \"${workspace}\""

mkdir -p .cursor/hooks/state
echo "$(date -Iseconds) sessionEnd reason=${reason} status=${final_status} session=${session_id}" >> .cursor/hooks/state/session-audit.log

exit 0
