# AI assistant best practices (Claude Max, Gemini Ultra, Codex Pro)

**Language and path routing:** [MODEL_LANGUAGE_ROUTING.md](MODEL_LANGUAGE_ROUTING.md) — which models to use for Python vs C# vs schema vs docs, air-gap handoffs, and a routing matrix.

Guidance for **choosing a tool tier** when working on InGENeer: validated intents, Python orchestrator, JSON contracts, and (later) a **separate** CAD execution workspace. Vendor **plan names and limits change**—revisit this doc when pricing or model cards update.

**Non-negotiables for every tool:** follow [AutonomAtIon/AUTONOMATION_SYSTEM_ARCHITECTURE_RULES.md](../AutonomAtIon/AUTONOMATION_SYSTEM_ARCHITECTURE_RULES.md) and [AutonomAtIon/LAYERED_PRACTICE_PLAYBOOK.md](../AutonomAtIon/LAYERED_PRACTICE_PLAYBOOK.md). Do not invent proprietary CAD APIs; keep orchestrator and CAD plugin contexts **air-gapped** when generating execution code (SOP 2).

---

## When to use **Claude Max** (Anthropic)

Use for **high-stakes reasoning** where mistakes are expensive:

- **Architecture and boundaries:** orchestrator vs bridge vs host; phase design; error and rollback stories.
- **Contract design:** intent envelope evolution, versioning, backward compatibility, audit semantics.
- **Difficult debugging:** intermittent failures, ambiguous stack traces, cross-layer mismatches (envelope vs bridge vs stub phases).
- **Policy-heavy edits:** anything that touches threading, transactions, or “fail closed” behavior—have the model **cite** your rules docs and **preserve** guards (architecture rule 5).

**Practice:** Start chats with pointers to the canonical markdown files; use **small follow-up tasks** after two failed attempts on the same bug (SOP 6).

---

## When to use **Gemini Ultra** (Google)

Use when **breadth of context** matters more than a single-file edit:

- **Repo-wide or multi-doc synthesis:** schema + catalog + `ingenieer` modules + tests in one thread.
- **Comparing alternatives:** transport options, payload shapes, or doc drafts side by side.
- **Long inputs:** pasting large JSON traces, multi-file logs, or several specification excerpts—*still* require explicit “only use methods from this snippet” for any CAD host API work.

**Practice:** Ground the task with **file paths and headings** (“see `orchestrator.py` phase order”). Re-assert **no geometry in orchestrator** and **no guessed IntelliCAD/Carlson APIs** in execution code.

---

## When to use **Codex Pro** (OpenAI)

Use for **tight implementation loops** on well-specified, local work:

- **Mechanical Python:** typing, tests, small refactors in `orchestrator/src/ingenieer/`.
- **Test-driven fixes:** reproduce failure → adjust implementation → keep contracts stable.
- **Boilerplate:** new phase stubs, extra audit fields, JSON schema tweaks **after** the shape is agreed.

**Practice:** Give **exact signatures**, **file paths**, and **acceptance criteria** (“`pytest -q` green”). Avoid asking Codex to **design** proprietary CAD integration from memory—hand it **official** API excerpts or a `TODO` scaffold instead.

---

## Quick routing table

| Task type | Prefer |
|-----------|--------|
| New boundary between orchestrator and host | Claude Max |
| Whole-repo consistency / large doc + code pass | Gemini Ultra |
| Implement `pytest`-covered change in `ingenieer` | Codex Pro |
| C# iCAD add-in (future repo) with vendor docs attached | Claude Max or Gemini Ultra (with **@ doc**); not “guess the API” in any tier |
| Routine test or lint fix | Fastest/cheapest capable model in your editor |

---

## Cursor / IDE hygiene (all tiers)

- **Modality:** planning in chat; surgical edits inline; Composer for **scaffolding** only (architecture SOP 4).
- **Micro-diff audit:** reject diffs that drop `Commit()`, rollback, logging, or fingerprint checks (SOP 5).
- **Git:** commit before big generations; reset after repeated thrash (SOP 6).

---

## Agent least privilege (orchestrator)

- Prefer **`ingenieer-run --phase validate_intent`** (or `--print-plan`) when an agent only needs to normalize or validate an envelope—avoid full `dispatch_execute` / `verify_result` unless the goal is an actual host round-trip.
- Treat **`dry_run` / `preview`** and catalog **risk tiers** as the default path for exploratory or LLM-generated intents; **`execute` + high-risk** requires an operator-issued `humanConfirmationToken` (use CLI `--i-confirm` in automation).
- **Air-gap:** do not load orchestrator + `icad-addin` into one generation context for execution code. Hand off **`schemas/cad_intent_envelope.schema.json` plus two sample JSON envelopes** (see `scripts/copy_schema_handoff.sh` output) the same way production does.

---

## Document control

- **Owner:** project maintainer.
- **When to update:** Plan renames (e.g. “Max” → new product name), or when you add a **new default** model in the team’s Cursor/CLI settings.
