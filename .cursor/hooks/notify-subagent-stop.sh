#!/bin/bash
set -euo pipefail

payload=$(cat)
status=$(echo "$payload" | jq -r '.status // "unknown"')
subagent_type=$(echo "$payload" | jq -r '.subagent_type // .agent_type // "subagent"')
summary=$(echo "$payload" | jq -r '.summary // .result_summary // ""' | head -c 120)
workspace=$(echo "$payload" | jq -r '.workspace_roots[0] // env.CURSOR_PROJECT_DIR // "InGENeer"')

body="${subagent_type} finished (${status})"
if [[ -n "$summary" && "$summary" != "null" ]]; then
  body="${body}: ${summary}"
fi

osascript -e "display notification \"${body}\" with title \"Cursor: InGENeer scope complete\" subtitle \"${workspace}\""

mkdir -p .cursor/hooks/state
echo "$(date -Iseconds) subagentStop type=${subagent_type} status=${status}" >> .cursor/hooks/state/session-audit.log

exit 0
