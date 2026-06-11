# AutonomAtIon SYSTEM ARCHITECTURE RULES

**Canonical location:** `docs/governance/autonomation/AUTONOMATION_SYSTEM_ARCHITECTURE_RULES.md` (this file).

**Layered checklists (paths, phases, versioning):** [LAYERED_PRACTICE_PLAYBOOK.md](LAYERED_PRACTICE_PLAYBOOK.md).

**Scope:** `AutonomAtIon` is the parent program. **`InGENeer`** = civil CAD, survey, construction. **`AIrchetect`** = 3D mechanical CAD. This document governs how code is written across those products.

**Current execution targets (update as you add hosts):**

- **Primary (InGENeer):** Carlson **iCAD** (IntelliCAD engine). Native automation is typically **.NET / COM / LISP / IRX** per ITC/Carlson—treat the CAD host as single-threaded for UI/document work unless a specific API documents otherwise.
- **Planned (AIrchetect):** **FreeCAD** (free). Core automation is **Python** (FreeCAD’s API), not C#. See **Domain isolation** below for how that still maps to “orchestrator vs execution.”

**Preferred desktop stack** for manual work and file prep (GIS, 2D CAD, 3D, math): see `.cursor/rules/preferred-desktop-tooling.mdc`.

---

## 1. DOMAIN ISOLATION

- **Python/Node files = “Orchestrator.”** ONLY handles LLM routing, JSON schema validation, and transport. NEVER calculates B-rep geometry.
- **C#/.NET files = “CAD execution layer”** for hosts that expose a .NET API (e.g. IntelliCAD/Carlson iCAD, Revit, AutoCAD-based products). STRICTLY deterministic. ZERO probabilistic AI logic here.
- **FreeCAD execution (when added):** Use a **dedicated Python worker** (or in-process FreeCAD macro runner) that is **deterministic only**—same contract as C#: validated intents in, API calls out, no LLM. The orchestrator does not import FreeCAD; it sends jobs to that worker.

## 2. CAD THREAD SAFETY (CRITICAL)

- CAD APIs for **Revit, Civil 3D, Inventor, IntelliCAD/Carlson iCAD**, and most desktop hosts are **fiercely single-threaded** for document mutation.
- NEVER suggest `async/await` inside native CAD transactions or document modifications unless official docs explicitly allow it for that call path.
- All native API execution MUST be marshaled to the **main UI thread** (host-specific: e.g. `IExternalEventHandler` in Revit, `Application.Idle` patterns in AutoCAD-style hosts, IntelliCAD’s documented threading rules).

## 3. TRANSACTION FAIL-SAFES

- EVERY CAD state mutation MUST be wrapped in the host’s native **transaction** (or documented equivalent).
- MUST include `try` / `catch` / `finally` with an explicit **rollback** on failure. NEVER silently swallow exceptions.
- Host APIs differ (Revit `Transaction` vs AutoCAD `TransactionManager` vs IntelliCAD patterns). **Use the correct type for the host**—do not copy Revit names into an IntelliCAD plugin.

## 4. NO API HALLUCINATIONS

- NEVER guess proprietary **Autodesk, Bentley, Siemens, Carlson, or ITC/IntelliCAD** API methods. If you are not 100% sure of the exact namespace and signature, write `// TODO` and ask for a documentation snippet.
- Do not invent helpers like `Civil3D.CreatePipe()` unless they exist in the cited docs.

## 5. PRESERVATION OF TRUTH

- NEVER silently remove existing error handling, logging, state hashing, or spatial verification gates to make a function “cleaner” or “shorter.”

---

## SOP 2: The “Air-Gapped” Workspace Strategy

Cursor’s biggest danger is **context bleed**. If you ask it to “update the execution bridge,” it may read your Python MCP server and your C# CAD plugin at once and bleed Python paradigms into C#, or suggest .NET imports in Node.

**Physically isolate workspaces:**

- Open your **Orchestrator** (Python/Node) in **Cursor Window A**.
- Open your **CAD plugin** (C# and/or FreeCAD worker) in **Cursor Window B**.

**The sneakernet:**

- Force the AI to communicate across the boundary the same way your apps do.
- When they need to talk, manually copy the **JSON schema** from Window A and paste into Window B with a prompt like:  
  *“Here is the exact JSON payload the orchestrator will send. Generate the strictly-typed C# deserialization struct. Do not write the execution logic yet.”*  
  (Use equivalent strictly-typed Python dataclasses/models for a FreeCAD worker.)

---

## SOP 3: Taming CAD API Hallucinations

LLMs are trained heavily on open web stacks. Training data on **proprietary C# CAD APIs** is sparse, outdated, and polluted by forum guesses. Models will confidently invent types that do not exist.

**Index the sacred texts:**

- Cursor **Settings → Features → Docs → Add Custom Doc**.
- Add URLs for: **Autodesk Platform Services (APS)** (if used), **Revit API** (`revitapidocs.com` or official), **IntelliCAD/Carlson** developer docs as applicable, **MCP SDK** docs, etc.

**Force the citation:**

- When generating CAD execution code, start with something like:  
  *“Using the exact namespace and methods defined in @YourDocName, write … Do not use methods not found in this document.”*

---

## SOP 4: The Modality Matrix (Know Your Shortcuts)

Using the wrong Cursor surface at the wrong time is how codebases turn into spaghetti.

### Tier 1: Cursor Chat (Cmd+L) — Planning

- Design JSON schemas, interrogate documentation, plan architecture.
- Prefer read-only exploration when possible.

### Tier 2: Inline Edit (Cmd+K) — Surgical strikes

- Primary weapon for small edits.
- Example: highlight an empty method body: *“Implement deserialization of the JSON intent. Follow the transaction rollback rule for this host.”*

### Tier 3: Composer (Cmd+I) — Boilerplate only

- Multi-file edits; treat as a loaded weapon.
- Use for **deterministic scaffolding** (empty handlers, registration, file layout).
- Example: *“Scaffold a new MCP tool endpoint: empty handler class + registration.”*
- Avoid: *“Wire up the geometry logic”* in Composer—it will fight your architecture.

---

## SOP 5: The Micro-Diff Audit

When Cursor shows a red/green diff, **never Accept All blindly.**

- **Red lines matter.** Watch for silent deletions: `Commit()` calls, `using` statements, telemetry, guards.
- If error handling is removed to “prettify” the function: **Reject**, re-prompt with explicit preservation rules—do not patch sloppy output by hand unless you must.

---

## SOP 6: The Git-Backed Bailout Protocol

Fast generation also means fast garbage.

**Clean working trees:**

- Commit before risky multi-file work, e.g.  
  `git commit -m "wip: stable json parser"`

**The apology loop:**

- Model fixes a cross-thread bug, introduces another; context gets polluted.

**2-strike rule:**

- If the model **fails twice** on the same bug, **stop**. Do not accumulate nested `try/catch` thrash.

**Rollback:**

- `git reset --hard HEAD` (or revert to last good commit).
- New chat (clear context). Break the task into smaller steps. Retry.

Rollbacks are cheaper than untangling hallucinations.

---

## Naming Rules

| Name           | Meaning |
|----------------|---------|
| **InGENeer**   | Civil CAD, surveyor, construction-oriented tooling and context. |
| **AIrchetect** | 3D mechanical CAD–oriented tooling and context. |
| **AutonomAtIon** | This folder and parent program root for orchestration + execution boundaries above. |

---

## Document control

- **Owner:** project maintainer.
- **When to update:** When you add a CAD host (e.g. FreeCAD worker paths), change transport (MCP vs gRPC), or adopt a new official doc source—keep **SOP 3** doc index in sync.
