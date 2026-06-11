#!/usr/bin/env bash
# InGENeer full local verification gate.
# Runs ruff + pytest + dotnet build + domain-isolation greps + contract-sync,
# collecting per-step status and printing a table at the end.
#
# Flags:
#   --skip-dotnet   Skip the .NET build (e.g. on a Python-only machine).
#   --quick         Run ruff + contract-sync only (skip pytest and dotnet).
set -u

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

SKIP_DOTNET=0
QUICK=0
for arg in "$@"; do
  case "$arg" in
    --skip-dotnet) SKIP_DOTNET=1 ;;
    --quick) QUICK=1 ;;
    *) echo "Unknown flag: $arg" >&2; exit 2 ;;
  esac
done

declare -a NAMES=()
declare -a RESULTS=()
OVERALL=0

record() {
  # record <name> <exit_code>
  NAMES+=("$1")
  if [ "$2" -eq 0 ]; then
    RESULTS+=("PASS")
  else
    RESULTS+=("FAIL")
    OVERALL=1
  fi
}

run_step() {
  # run_step <name> <command...>
  local name="$1"; shift
  echo ">>> $name"
  if "$@"; then record "$name" 0; else record "$name" $?; fi
}

# 1. ruff
run_step "ruff" bash -c "cd orchestrator && ruff check src tests"

# 2. pytest (unless --quick)
if [ "$QUICK" -eq 0 ]; then
  run_step "pytest" bash -c "cd orchestrator && python -m pytest -q"
fi

# 3. dotnet build (unless skipped or --quick)
if [ "$QUICK" -eq 0 ] && [ "$SKIP_DOTNET" -eq 0 ]; then
  run_step "dotnet-build" dotnet build icad-addin/InGENeer.IcadAddin.slnx -c Release
fi

# 4. domain isolation: no geometry in orchestrator Python.
echo ">>> domain-isolation:python"
if grep -rniE "b_rep|brep|tessellat|nurbs" orchestrator/src/ingenieer/ >/dev/null 2>&1; then
  echo "    found geometry symbols in orchestrator/src (Rule 1 violation)"
  record "domain-isolation:python" 1
else
  record "domain-isolation:python" 0
fi

# 5. domain isolation: no LLM/AI in C# execution layer.
echo ">>> domain-isolation:csharp"
if grep -rniE "openai|anthropic|\bllm\b|transformers" icad-addin/ --include="*.cs" --exclude-dir=obj --exclude-dir=bin >/dev/null 2>&1; then
  echo "    found LLM/AI symbols in icad-addin (Rule 1 violation)"
  record "domain-isolation:csharp" 1
else
  record "domain-isolation:csharp" 0
fi

# 6. contract sync
run_step "contract-sync" python tools/checks/check_contract_sync.py

echo ""
echo "==================== VERIFY GATE ===================="
for i in "${!NAMES[@]}"; do
  printf "  [%s] %s\n" "${RESULTS[$i]}" "${NAMES[$i]}"
done
echo "====================================================="
if [ "$OVERALL" -eq 0 ]; then
  echo "RESULT: ALL GATES PASS"
else
  echo "RESULT: GATE FAILURES"
fi
exit "$OVERALL"
